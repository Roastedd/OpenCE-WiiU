/*
WIIU_SHELL_BLACKBOX.H

When a Wii U app freezes, the system ends it (or the user does, with the power button) and nothing of what it was doing
is left. The black box is a few lines written to the card every few seconds, by a thread of its own: where every thread
that registered is (running, ready, waiting or suspended; its program counter and return address; what it waits for), what
the game's phase and frame count were, how the log stands. The file holds the last three records and is rewritten each time,
so it never grows; a clean stop removes it. A file found at the next start is the last run's: it did not end cleanly, and the
last record is the best account there is of how it ended.

The record is formatted into a buffer here, from facts the caller gives; where a thread is comes from a function the caller
supplies (OSThread's fields on the console), so that this part runs on a desktop under test.
*/

#ifndef HALO_WIIU_SHELL_BLACKBOX_H
#define HALO_WIIU_SHELL_BLACKBOX_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#define WIIU_BLACKBOX_SLOTS 3
#define WIIU_BLACKBOX_RECORD_BYTES 2800
#define WIIU_BLACKBOX_THREADS 16

/* Writes where the thread is into out (not more than capacity - 1 characters); returns their number. */
typedef size_t (*wiiu_blackbox_where)(void *thread, char *out, size_t capacity);

typedef struct wiiu_shell_blackbox
{
	pthread_mutex_t lock;
	struct
	{
		const char *name;
		void *thread;
	} threads[WIIU_BLACKBOX_THREADS];
	uint32_t thread_count;
	char records[WIIU_BLACKBOX_SLOTS][WIIU_BLACKBOX_RECORD_BYTES];
	size_t lengths[WIIU_BLACKBOX_SLOTS];
	uint32_t made;
	char path[192];
	wiiu_blackbox_where where;
} wiiu_shell_blackbox;

/* what a record says besides the threads */
typedef struct wiiu_blackbox_facts
{
	uint32_t now_ms;
	uint32_t frames;
	const char *phase;      /* may be NULL */
	uint32_t phase_ago_ms;
	uint32_t background;
	uint32_t log_pending;
	uint32_t log_written;
	uint32_t log_dropped;
	const char *memory;     /* a line about the heap; may be NULL */
} wiiu_blackbox_facts;

void wiiu_shell_blackbox_init(wiiu_shell_blackbox *box, const char *path, wiiu_blackbox_where where);

/* Registers a thread (name is a string literal) for the records; at most WIIU_BLACKBOX_THREADS. Returns 0, or -1 if full. */
int wiiu_shell_blackbox_tag(wiiu_shell_blackbox *box, const char *name, void *thread);
void wiiu_shell_blackbox_untag(wiiu_shell_blackbox *box, void *thread);

/* Formats one record into out; returns its length. A thread list in use by another thread is said to be, not waited for. */
size_t wiiu_shell_blackbox_format(wiiu_shell_blackbox *box, const wiiu_blackbox_facts *facts, char *out, size_t capacity);

/* Makes a record, keeps it with the two before it, and rewrites the file. Returns 0, or the errno of what failed. */
int wiiu_shell_blackbox_tick(wiiu_shell_blackbox *box, const wiiu_blackbox_facts *facts);

/* a clean end: the file is removed */
void wiiu_shell_blackbox_stop(wiiu_shell_blackbox *box);

/* Takes the last run's file out of the way, to previous_path, and returns its size (0: there was none or it was empty). */
size_t wiiu_shell_blackbox_take_previous(const char *path, const char *previous_path);

#endif
