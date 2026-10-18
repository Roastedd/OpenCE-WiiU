/*
WIIU_SHELL_LOG.H

The log of a Wii U run: lines from any thread, written to a file on the SD card by a worker thread of its own.

Nothing that writes a line waits for the card. A game thread (or the main thread, which the system expects to answer HOME
within moments) puts the line in a queue and goes on; the worker takes what is queued every tenth of a second and appends it
to the file, opening and closing the file for each batch, so that a crash loses at most one. A card that is slow, or gone,
or full costs lines, not frames: when the queue is full the new line is dropped and counted, and the worker says how many
were dropped when it next writes.

The queue and its worker are plain C with pthreads (the same on the console and on a desktop, where the tests run them).
*/

#ifndef HALO_WIIU_SHELL_LOG_H
#define HALO_WIIU_SHELL_LOG_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#define WIIU_LOG_LINE_BYTES 240
#define WIIU_LOG_SLOTS 512
/* lines the worker takes at a time (and holds in a buffer of its own while it writes) */
#define WIIU_LOG_BATCH 64
#define WIIU_LOG_PERIOD_MS 100

typedef struct wiiu_shell_log_config
{
	const char *path;                /* the file to append to */
	uint32_t (*now_ms)(void);        /* a monotonic clock in milliseconds (the lines carry it) */
	void (*thread_setup)(const char *name); /* names, prioritises the worker; may be NULL */
	void (*echo)(const char *line);  /* each line also goes here, from the writer's thread (the system's debug log); may be NULL */
} wiiu_shell_log_config;

typedef struct wiiu_shell_log
{
	pthread_mutex_t lock;
	char slots[WIIU_LOG_SLOTS][WIIU_LOG_LINE_BYTES];
	uint32_t first;   /* the oldest queued line */
	uint32_t count;   /* lines queued */
	uint32_t queued;  /* lines accepted since the start */
	uint32_t written; /* lines on the card */
	uint32_t lost;    /* lines taken off the queue and not written: the card refused them */
	uint32_t batch_lines;
	uint32_t dropped; /* lines refused because the queue was full */
	uint32_t reported_dropped;
	uint32_t write_errors;
	uint32_t busy;    /* the worker is writing a batch */
	uint32_t started;
	char batch[WIIU_LOG_BATCH * WIIU_LOG_LINE_BYTES];
	wiiu_shell_log_config config;
	char path[192];
} wiiu_shell_log;

/* Makes the log; the file is appended to (rotating it is the caller's). Returns 0, or the errno of what failed. */
int wiiu_shell_log_start(wiiu_shell_log *log, const wiiu_shell_log_config *config);

/* Queues a line, "SECONDS.MILLIS text" (a longer one is cut). Safe from any thread; never waits for the card. */
void wiiu_shell_log_write(wiiu_shell_log *log, const char *format, ...) __attribute__((format(printf, 2, 3)));
void wiiu_shell_log_text(wiiu_shell_log *log, const char *text);

/* lines waiting, accepted, written, dropped */
void wiiu_shell_log_counts(wiiu_shell_log *log, uint32_t *pending, uint32_t *queued, uint32_t *written, uint32_t *dropped);

/* Waits up to timeout_ms for the queue to be empty and the worker idle. Returns 1 if it is. */
int wiiu_shell_log_flush(wiiu_shell_log *log, uint32_t timeout_ms);

#endif
