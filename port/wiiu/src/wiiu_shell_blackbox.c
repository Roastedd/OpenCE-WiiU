#include "wiiu_shell_blackbox.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* printf into the record after what it holds, never beyond its end */
static void put(char *out, size_t capacity, size_t *used, const char *format, ...) __attribute__((format(printf, 4, 5)));

static void put(char *out, size_t capacity, size_t *used, const char *format, ...)
{
	va_list arguments;
	int written;

	if (*used + 1 >= capacity)
		return;
	va_start(arguments, format);
	written = vsnprintf(out + *used, capacity - *used, format, arguments);
	va_end(arguments);
	if (written > 0)
		*used += (size_t)written < capacity - *used ? (size_t)written : capacity - *used - 1;
}

void wiiu_shell_blackbox_init(wiiu_shell_blackbox *box, const char *path, wiiu_blackbox_where where)
{
	memset(box, 0, sizeof(*box));
	pthread_mutex_init(&box->lock, NULL);
	snprintf(box->path, sizeof(box->path), "%s", path);
	box->where = where;
}

int wiiu_shell_blackbox_tag(wiiu_shell_blackbox *box, const char *name, void *thread)
{
	int result = -1;

	pthread_mutex_lock(&box->lock);
	if (box->thread_count < WIIU_BLACKBOX_THREADS)
	{
		box->threads[box->thread_count].name = name;
		box->threads[box->thread_count].thread = thread;
		box->thread_count++;
		result = 0;
	}
	pthread_mutex_unlock(&box->lock);
	return result;
}

void wiiu_shell_blackbox_untag(wiiu_shell_blackbox *box, void *thread)
{
	uint32_t i;

	pthread_mutex_lock(&box->lock);
	for (i = 0; i < box->thread_count; i++)
	{
		if (box->threads[i].thread == thread)
		{
			box->threads[i] = box->threads[box->thread_count - 1];
			box->thread_count--;
			break;
		}
	}
	pthread_mutex_unlock(&box->lock);
}

size_t wiiu_shell_blackbox_format(wiiu_shell_blackbox *box, const wiiu_blackbox_facts *facts, char *out, size_t capacity)
{
	size_t used = 0;
	uint32_t i;

	if (capacity == 0)
		return 0;
	out[0] = 0;
	put(out, capacity, &used, "== %u.%03u s: frame %u", (unsigned)(facts->now_ms / 1000u), (unsigned)(facts->now_ms % 1000u),
		(unsigned)facts->frames);
	if (facts->phase)
		put(out, capacity, &used, ", its %s phase ended %u ms ago", facts->phase, (unsigned)facts->phase_ago_ms);
	put(out, capacity, &used, "; %s; log: %u waiting, %u written, %u dropped", facts->background ? "in the background" : "in the foreground",
		(unsigned)facts->log_pending, (unsigned)facts->log_written, (unsigned)facts->log_dropped);
	if (facts->memory)
		put(out, capacity, &used, "; %s", facts->memory);
	put(out, capacity, &used, "\n");
	/* a thread that holds the list (tagging or untagging) is not waited for: a record that says so is better than none */
	if (pthread_mutex_trylock(&box->lock) != 0)
	{
		put(out, capacity, &used, "  (the thread list is in use)\n");
		return used;
	}
	for (i = 0; i < box->thread_count; i++)
	{
		char where[200];
		size_t length = box->where ? box->where(box->threads[i].thread, where, sizeof(where) - 1) : 0;

		if (length == 0)
			continue;
		where[length < sizeof(where) ? length : sizeof(where) - 1] = 0;
		put(out, capacity, &used, "  %s %s\n", box->threads[i].name, where);
	}
	pthread_mutex_unlock(&box->lock);
	return used;
}

struct piece
{
	const char *data;
	size_t size;
};

/* replaces the file with the pieces; 0, or the errno of what failed */
static int write_file(const char *path, const struct piece *pieces, size_t count)
{
	int error = 0;
	size_t i;
	int file = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	if (file < 0)
		return errno ? errno : EIO;
	for (i = 0; i < count && !error; i++)
	{
		size_t done = 0;

		while (done < pieces[i].size)
		{
			ssize_t written = write(file, pieces[i].data + done, pieces[i].size - done);

			if (written <= 0)
			{
				error = errno ? errno : EIO;
				break;
			}
			done += (size_t)written;
		}
	}
	if (!error && fsync(file) != 0)
		error = errno ? errno : EIO;
	if (close(file) != 0 && !error)
		error = errno ? errno : EIO;
	return error;
}

int wiiu_shell_blackbox_tick(wiiu_shell_blackbox *box, const wiiu_blackbox_facts *facts)
{
	const uint32_t number = box->made++;
	const uint32_t slot = number % WIIU_BLACKBOX_SLOTS;
	const uint32_t first = number >= WIIU_BLACKBOX_SLOTS - 1 ? number - (WIIU_BLACKBOX_SLOTS - 1) : 0;
	struct piece pieces[WIIU_BLACKBOX_SLOTS];
	size_t count = 0;
	uint32_t record;

	box->lengths[slot] = wiiu_shell_blackbox_format(box, facts, box->records[slot], sizeof(box->records[slot]));
	for (record = first; record <= number; record++)
	{
		pieces[count].data = box->records[record % WIIU_BLACKBOX_SLOTS];
		pieces[count].size = box->lengths[record % WIIU_BLACKBOX_SLOTS];
		count++;
	}
	return write_file(box->path, pieces, count);
}

void wiiu_shell_blackbox_stop(wiiu_shell_blackbox *box)
{
	unlink(box->path);
}

size_t wiiu_shell_blackbox_take_previous(const char *path, const char *previous_path)
{
	off_t length;
	int file = open(path, O_RDONLY);

	if (file < 0)
		return 0;
	length = lseek(file, 0, SEEK_END);
	close(file);
	if (length <= 0)
	{
		unlink(path);
		return 0;
	}
	unlink(previous_path);
	if (rename(path, previous_path) != 0)
		return 0;
	return (size_t)length;
}
