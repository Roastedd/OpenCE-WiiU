#include "wiiu_shell_log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void sleep_ms(uint32_t milliseconds)
{
	struct timespec duration;

	duration.tv_sec = milliseconds / 1000u;
	duration.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
	nanosleep(&duration, NULL);
}

/* appends size bytes to the file; 0, or the errno of what failed */
static int append_file(const char *path, const char *data, size_t size)
{
	size_t done = 0;
	int error = 0;
	int file = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);

	if (file < 0)
		return errno ? errno : EIO;
	while (done < size)
	{
		ssize_t written = write(file, data + done, size - done);

		if (written <= 0)
		{
			error = errno ? errno : EIO;
			break;
		}
		done += (size_t)written;
	}
	if (close(file) != 0 && !error)
		error = errno ? errno : EIO;
	return error;
}

/* takes up to a batch of lines off the queue into log->batch; returns its length (0: none) */
static size_t take_batch(wiiu_shell_log *log)
{
	size_t used = 0;
	uint32_t taken = 0;
	uint32_t dropped_now;

	pthread_mutex_lock(&log->lock);
	while (log->count > 0 && taken < WIIU_LOG_BATCH - 1)
	{
		const char *line = log->slots[log->first];
		size_t length = strlen(line);

		memcpy(log->batch + used, line, length);
		used += length;
		log->batch[used++] = '\n';
		log->first = (log->first + 1) % WIIU_LOG_SLOTS;
		log->count--;
		taken++;
	}
	dropped_now = log->dropped - log->reported_dropped;
	if (taken && dropped_now)
	{
		/* (the one slot of the batch kept back for this) */
		int written = snprintf(log->batch + used, WIIU_LOG_LINE_BYTES, "%u log lines were dropped: the queue was full\n",
			(unsigned)dropped_now);

		if (written > 0)
			used += (size_t)written < WIIU_LOG_LINE_BYTES ? (size_t)written : WIIU_LOG_LINE_BYTES - 1;
		log->reported_dropped += dropped_now;
	}
	if (taken)
	{
		log->busy = 1;
		log->batch_lines = taken;
	}
	pthread_mutex_unlock(&log->lock);
	return used;
}

/* a line in the queue (the lock held); the line is dropped, counted, when the queue is full */
static void queue_locked(wiiu_shell_log *log, const char *line)
{
	if (log->count >= WIIU_LOG_SLOTS)
	{
		log->dropped++;
		return;
	}
	memcpy(log->slots[(log->first + log->count) % WIIU_LOG_SLOTS], line, WIIU_LOG_LINE_BYTES);
	log->count++;
	log->queued++;
}

static void line_with_time(char *line, uint32_t now, const char *text, const char *suffix)
{
	size_t i;

	snprintf(line, WIIU_LOG_LINE_BYTES, "%u.%03u %s%s", (unsigned)(now / 1000u), (unsigned)(now % 1000u), text, suffix);
	/* one line each: a newline inside would be two */
	for (i = 0; line[i]; i++)
		if (line[i] == '\n' || line[i] == '\r')
			line[i] = ' ';
}

/* a remembered line's repeats said (the lock held), and forgotten when it is the one to give way */
static void say_repeats_locked(wiiu_shell_log *log, wiiu_shell_log_recent *entry, uint32_t now)
{
	char line[WIIU_LOG_LINE_BYTES], suffix[40];
	uint32_t seconds = (now - entry->written_ms + 500u) / 1000u;

	if (!entry->repeats)
		return;
	snprintf(suffix, sizeof(suffix), " (%u more in %us)", (unsigned)entry->repeats, (unsigned)seconds);
	line_with_time(line, now, entry->text, suffix);
	queue_locked(log, line);
	entry->repeats = 0;
	entry->written_ms = now;
}

/* (the lock held) whether the line is one just written: then it is only counted */
static int collapse_locked(wiiu_shell_log *log, const char *text, uint32_t now)
{
	uint32_t i;
	wiiu_shell_log_recent *entry;

	for (i = 0; i < WIIU_LOG_RECENT; i++)
	{
		entry = &log->recent[i];
		if (!entry->used || strncmp(entry->text, text, WIIU_LOG_KEY_BYTES - 1))
			continue;
		if (now - entry->written_ms < log->config.collapse_ms)
		{
			entry->repeats++;
			log->collapsed++;
			return 1;
		}
		say_repeats_locked(log, entry, now);
		entry->written_ms = now;
		return 0;
	}
	/* a line not remembered: the oldest entry gives way (and says what it had left out) */
	entry = &log->recent[log->recent_next];
	log->recent_next = (log->recent_next + 1u) % WIIU_LOG_RECENT;
	if (entry->used)
		say_repeats_locked(log, entry, now);
	snprintf(entry->text, sizeof(entry->text), "%s", text);
	entry->written_ms = now;
	entry->repeats = 0;
	entry->used = 1;
	return 0;
}

/* repeats whose time is up said, with nothing new to say them (the worker, each turn) */
static void flush_repeats(wiiu_shell_log *log, int all)
{
	uint32_t now, i;

	if (!log->config.collapse_ms)
		return;
	now = log->config.now_ms ? log->config.now_ms() : 0;
	pthread_mutex_lock(&log->lock);
	for (i = 0; i < WIIU_LOG_RECENT; i++)
		if (log->recent[i].used && log->recent[i].repeats && (all || now - log->recent[i].written_ms >= log->config.collapse_ms))
			say_repeats_locked(log, &log->recent[i], now);
	pthread_mutex_unlock(&log->lock);
}

static void *worker(void *context)
{
	wiiu_shell_log *log = context;

	if (log->config.thread_setup)
		log->config.thread_setup("log");
	for (;;)
	{
		size_t size;

		flush_repeats(log, 0);
		pthread_mutex_lock(&log->lock);
		if (log->stopping)
		{
			log->stopped = 1;
			pthread_mutex_unlock(&log->lock);
			return NULL;
		}
		pthread_mutex_unlock(&log->lock);
		size = take_batch(log);

		if (size)
		{
			int error = append_file(log->path, log->batch, size);

			pthread_mutex_lock(&log->lock);
			if (error)
			{
				/* (the lines are gone: a card that cannot be written to is not retried line by line) */
				log->write_errors++;
				log->lost += log->batch_lines;
			}
			else
			{
				log->written += log->batch_lines;
			}
			log->busy = 0;
			pthread_mutex_unlock(&log->lock);
		}
		/* No log mutex is held. One service turn cannot starve normal batches, even under continuous logging. */
		if (log->config.service && log->config.service(log->config.service_context))
			sleep_ms(1);
		else if (!size)
			sleep_ms(WIIU_LOG_PERIOD_MS);
	}
	return NULL;
}

int wiiu_shell_log_start(wiiu_shell_log *log, const wiiu_shell_log_config *config)
{
	pthread_attr_t attributes;
	pthread_t thread;
	int error;

	memset(log, 0, sizeof(*log));
	log->config = *config;
	snprintf(log->path, sizeof(log->path), "%s", config->path);
	pthread_mutex_init(&log->lock, NULL);
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&attributes, 64 * 1024);
	error = pthread_create(&thread, &attributes, worker, log);
	pthread_attr_destroy(&attributes);
	if (error)
		return error;
	log->started = 1;
	return 0;
}

uint32_t wiiu_shell_log_collapsed(wiiu_shell_log *log)
{
	uint32_t collapsed;

	pthread_mutex_lock(&log->lock);
	collapsed = log->collapsed;
	pthread_mutex_unlock(&log->lock);
	return collapsed;
}

void wiiu_shell_log_text(wiiu_shell_log *log, const char *text)
{
	char line[WIIU_LOG_LINE_BYTES];
	uint32_t now;

	if (!log->started)
		return;
	now = log->config.now_ms ? log->config.now_ms() : 0;
	line_with_time(line, now, text, "");
	if (log->config.echo)
		log->config.echo(line);
	pthread_mutex_lock(&log->lock);
	if (log->config.collapse_ms && collapse_locked(log, line + strcspn(line, " ") + 1, now))
	{
		pthread_mutex_unlock(&log->lock);
		return;
	}
	queue_locked(log, line);
	pthread_mutex_unlock(&log->lock);
}

void wiiu_shell_log_write(wiiu_shell_log *log, const char *format, ...)
{
	char text[WIIU_LOG_LINE_BYTES];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	wiiu_shell_log_text(log, text);
}

void wiiu_shell_log_counts(wiiu_shell_log *log, uint32_t *pending, uint32_t *queued, uint32_t *written, uint32_t *dropped)
{
	pthread_mutex_lock(&log->lock);
	if (pending)
		*pending = log->count;
	if (queued)
		*queued = log->queued;
	if (written)
		*written = log->written;
	if (dropped)
		*dropped = log->dropped;
	pthread_mutex_unlock(&log->lock);
}

int wiiu_shell_log_flush(wiiu_shell_log *log, uint32_t timeout_ms)
{
	uint32_t waited = 0;

	flush_repeats(log, 1);

	for (;;)
	{
		int done;

		pthread_mutex_lock(&log->lock);
		done = log->count == 0 && !log->busy;
		pthread_mutex_unlock(&log->lock);
		if (done)
			return 1;
		if (waited >= timeout_ms)
			return 0;
		sleep_ms(10);
		waited += 10;
	}
}

int wiiu_shell_log_stop(wiiu_shell_log *log, uint32_t timeout_ms)
{
	uint32_t waited = 0;

	if (!log->started)
		return 1;
	pthread_mutex_lock(&log->lock);
	log->stopping = 1;
	pthread_mutex_unlock(&log->lock);
	for (;;)
	{
		int stopped;

		pthread_mutex_lock(&log->lock);
		stopped = log->stopped != 0;
		pthread_mutex_unlock(&log->lock);
		if (stopped)
			return 1;
		if (waited >= timeout_ms)
			return 0;
		sleep_ms(10);
		waited += 10;
	}
}
