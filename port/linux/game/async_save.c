/*
ASYNC_SAVE.C

The thread a checkpoint's write runs on (async_save.h).
*/

#include "async_save.h"

#ifdef HALO_WIIU

#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* the platform layer's (port/wiiu/src/wiiu_shell.c, wiiu_study.c; the digest harness's glue on the host) */
void platform_log(char const *format, ...);
long wiiu_study_number(const char *name, long fallback);
#ifdef __WIIU__
void wiiu_thread_background(const char *name);
void wiiu_thread_background_offset(const char *name, int offset);
#endif

static struct
{
	pthread_mutex_t lock;
	pthread_cond_t wake;
	int created;
	int failed;
	int busy;
	void (*run)(void *);
	void *argument;
	int enabled, verify;
	unsigned long piece;
} job = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0, 0, NULL, NULL, -1, -1, 0 };

int async_save_enabled(void)
{
	if (job.enabled < 0)
	{
		/* (-1 and no file: on) */
		job.enabled = wiiu_study_number("async_save.txt", -1) != 0;
		platform_log("checkpoint: the game state is written on a thread of its own, beside the game %s (halo/async_save.txt: 0 writes it on the game thread)",
			job.enabled ? "- yes" : "- no");
	}
	return job.enabled && !job.failed;
}

int async_save_verify(void)
{
	if (job.verify < 0)
	{
		job.verify = wiiu_study_number("async_save_verify.txt", 0) > 0;
		if (job.verify)
			platform_log("checkpoint: each checkpoint is checked (halo/async_save_verify.txt): the copy, the file read back, the CRC logged");
	}
	return job.verify;
}

int async_save_exit(void)
{
	static int exit_save = -1;

	if (exit_save < 0)
	{
		/* (-1 and no file: off) */
		exit_save = wiiu_study_number("async_save_exit.txt", 0) > 0;
		if (exit_save)
			platform_log("checkpoint: Save and Quit writes the saved game on the writer thread, beside the menus (halo/async_save_exit.txt)");
	}
	return exit_save;
}

int async_save_delta(void)
{
	static int delta = -1;

	if (delta < 0)
	{
		/* (no file: on; 0: the whole file at every checkpoint. Four checkpoints on b40 wrote 348 to 520 KB of the 20,480 KB and read back the same, 2026-11-19) */
		delta = wiiu_study_number("async_save_delta.txt", 1) > 0;
		if (delta)
			platform_log("checkpoint: only the parts of the game state that changed since the last checkpoint are written (halo/async_save_delta.txt: 0 writes the whole file)");
	}
	return delta;
}

int async_save_revert_image(void)
{
	static int revert_image = -1;

	if (revert_image < 0)
	{
		/* (-1 and no file: on) */
		revert_image = wiiu_study_number("async_save_revert_image.txt", -1) != 0;
		platform_log("checkpoint: a revert takes the game state from the copy of the last checkpoint held in memory, and does not read the card %s (halo/async_save_revert_image.txt: 0 reads it)",
			revert_image ? "- yes" : "- no");
	}
	return revert_image;
}

int async_save_checkpoint_writes(void)
{
	static int mode = -1;

	if (mode < 0)
	{
		const long number = wiiu_study_number("checkpoint_writes.txt", 0);

		/* (-1 and no file: 0) */
		mode = number < 1 ? 0 : number > 2 ? 2 : (int)number;
		if (mode)
			platform_log("checkpoint: the checkpoint's copy on the card is written %s (halo/checkpoint_writes.txt %d); a revert takes it from memory",
				mode == 1 ? "only when the game is paused or ends" : "only when the game ends", mode);
	}
	return mode;
}

unsigned long async_save_piece_bytes(void)
{
	if (!job.piece)
	{
		const long kilobytes = wiiu_study_number("async_save_piece_kb.txt", 128);

		job.piece = (kilobytes < 64 ? 64UL : kilobytes > 4096 ? 4096UL : (unsigned long)kilobytes) * 1024UL;
	}
	return job.piece;
}

static void *worker(void *unused)
{
	(void)unused;
#ifdef __WIIU__
	/* (core 0, below the game thread: it waits for the card, and has no work worth the game's core) */
	{
		/* (halo/async_save_priority.txt: how far below the game's priority the writer runs, 4 by default; 0 is the game's own) */
		const long offset = wiiu_study_number("async_save_priority.txt", 4);

		if (offset == 4)
			wiiu_thread_background("save");
		else
			wiiu_thread_background_offset("save", (int)offset);
	}
#endif
	pthread_mutex_lock(&job.lock);
	for (;;)
	{
		while (!job.busy)
			pthread_cond_wait(&job.wake, &job.lock);
		pthread_mutex_unlock(&job.lock);
		job.run(job.argument);
		pthread_mutex_lock(&job.lock);
		job.busy = 0;
	}
	return NULL;
}

/* The checkpoint's 20 MiB copy stalled the game thread ~106 ms (one memcpy, ~190 MB/s). The parts are copied on two more threads at once;
the caller joins them, so the copy is complete when this returns. Falls back to one memcpy if a thread cannot be made. */
struct copy_part { void *to; const void *from; size_t bytes; };

static void *copy_part_run(void *argument)
{
	struct copy_part *part = argument;

	memcpy(part->to, part->from, part->bytes);
	return NULL;
}

void async_save_copy(void *to, const void *from, size_t bytes)
{
	struct copy_part parts[2];
	pthread_t threads[2];
	int made[2] = { 0, 0 };
	size_t third = (bytes / 3) & ~(size_t)4095;
	size_t index;

	if (third < 1024 * 1024)
	{
		memcpy(to, from, bytes);
		return;
	}
	for (index = 0; index < 2; index++)
	{
		pthread_attr_t attributes;

		parts[index].to = (char *)to + third * (index + 1);
		parts[index].from = (const char *)from + third * (index + 1);
		parts[index].bytes = index == 1 ? bytes - third * 2 : third;
		pthread_attr_init(&attributes);
		pthread_attr_setstacksize(&attributes, 32 * 1024);
		made[index] = pthread_create(&threads[index], &attributes, copy_part_run, &parts[index]) == 0;
		pthread_attr_destroy(&attributes);
		if (!made[index])
			memcpy(parts[index].to, parts[index].from, parts[index].bytes);
	}
	memcpy(to, from, third);
	for (index = 0; index < 2; index++)
		if (made[index])
			pthread_join(threads[index], NULL);
}

static int make_thread(void)
{
	pthread_attr_t attributes;
	pthread_t thread;
	int error;

	if (job.created)
		return 1;
	if (job.failed)
		return 0;
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&attributes, 128 * 1024);
	error = pthread_create(&thread, &attributes, worker, NULL);
	pthread_attr_destroy(&attributes);
	if (error)
	{
		job.failed = 1;
		platform_log("checkpoint: the writer's thread could not be made (%d): the game state is written on the game thread", error);
		return 0;
	}
	job.created = 1;
	return 1;
}

int async_save_prepare(void)
{
	/* (the switches read here, on the game thread, not later on the writer's) */
	if (!async_save_enabled())
		return 0;
	async_save_verify();
	async_save_delta();
	async_save_exit();
	async_save_piece_bytes();
	return make_thread();
}

int async_save_start(void (*run)(void *), void *argument)
{
	if (!async_save_enabled() || !make_thread())
		return 0;
	pthread_mutex_lock(&job.lock);
	if (job.busy)
	{
		pthread_mutex_unlock(&job.lock);
		return 0;
	}
	job.run = run;
	job.argument = argument;
	job.busy = 1;
	pthread_cond_signal(&job.wake);
	pthread_mutex_unlock(&job.lock);
	return 1;
}

int async_save_running(void)
{
	int busy;

	pthread_mutex_lock(&job.lock);
	busy = job.busy;
	pthread_mutex_unlock(&job.lock);
	return busy;
}

int async_save_wait(unsigned long timeout_ms)
{
	unsigned long waited = 0;

	while (async_save_running())
	{
		struct timespec millisecond = { 0, 1000000 };

		if (timeout_ms && waited >= timeout_ms)
			return 0;
		nanosleep(&millisecond, NULL);
		waited++;
	}
	return 1;
}

#endif
