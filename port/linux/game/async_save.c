/*
ASYNC_SAVE.C

The thread a checkpoint's write runs on (async_save.h).
*/

#include "async_save.h"

#ifdef HALO_WIIU

#include <pthread.h>
#include <stdlib.h>
#include <time.h>

/* the platform layer's (port/wiiu/src/wiiu_shell.c, wiiu_study.c; the digest harness's glue on the host) */
void platform_log(char const *format, ...);
long wiiu_study_number(const char *name, long fallback);
#ifdef __WIIU__
void wiiu_thread_background(const char *name);
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

unsigned long async_save_piece_bytes(void)
{
	if (!job.piece)
	{
		const long kilobytes = wiiu_study_number("async_save_piece_kb.txt", 1024);

		job.piece = (kilobytes < 64 ? 64UL : kilobytes > 4096 ? 4096UL : (unsigned long)kilobytes) * 1024UL;
	}
	return job.piece;
}

static void *worker(void *unused)
{
	(void)unused;
#ifdef __WIIU__
	/* (core 0, below the game thread: it waits for the card, and has no work worth the game's core) */
	wiiu_thread_background("save");
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
