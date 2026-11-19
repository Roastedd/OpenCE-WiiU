/*
WIIU_NOTICE.C

The slow SD card message, the saving mark and the loading bar's state (wiiu_notice.h). The renderer (wiiu_gx2.c) draws them.
*/

#include "wiiu_notice.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef NOTICE_FOLDER
#define NOTICE_FOLDER "/vol/external01/wiiu/apps/halo"
#endif

/* the platform's (wiiu_shell.c, wiiu_study.c, async_save.c) */
void platform_log(char const *format, ...);
long wiiu_study_number(const char *name, long fallback);
int async_save_running(void);
int wiiu_in_foreground(void);
void wiiu_thread_background(const char *name);
/* main.c: a frame presented with what the last one held, for the loading bar to show on (the game thread) */
void main_loading_present(void);

#define load(word) __atomic_load_n(&(word), __ATOMIC_ACQUIRE)
#define store(word, value) __atomic_store_n(&(word), (value), __ATOMIC_RELEASE)

static uint32_t now_ms(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint32_t)((uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u);
}

static double now_ms_precise(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
}

/* a switch read once: 1 unless halo/<name> says 0 (no file, or -1, is the default) */
static int switch_on(const char *name, volatile int *cache)
{
	if (*cache < 0)
		*cache = wiiu_study_number(name, 1) != 0;
	return *cache;
}

/* ---------- the card */

int wiiu_notice_card_is_slow(float write_mbs, float read_mbs)
{
	return (write_mbs > 0.0f && write_mbs < NOTICE_SLOW_WRITE_MBS) || (read_mbs > 0.0f && read_mbs < NOTICE_SLOW_READ_MBS);
}

void wiiu_notice_card_text(char *out, unsigned capacity, float write_mbs, float read_mbs)
{
	/* (the speed of the one that is slow; the write's if both are) */
	const int write_slow = write_mbs > 0.0f && write_mbs < NOTICE_SLOW_WRITE_MBS;

	snprintf(out, capacity,
		"SD card\n"
		"Your SD card is slow (%.1f MB/s %s).\n"
		"The game may pause for a few seconds when it saves.\n"
		"A faster card (class 10 / U1 or better) helps.\n"
		"(halo/card_check.txt 0 turns this off)",
		(double)(write_slow ? write_mbs : read_mbs), write_slow ? "write" : "read");
}

int wiiu_notice_card_bench(const char *folder, float *write_mbs, float *read_mbs)
{
	enum { TOTAL = 2 * 1024 * 1024, PIECE = 128 * 1024 };
	char path[300];
	char *buffer = NULL;
	int fd;
	unsigned long done = 0;
	double start, write_ms, sync_ms, read_ms = 0.0;

	if (posix_memalign((void **)&buffer, 64, PIECE) || !buffer)
		return 0;
	memset(buffer, 0x5a, PIECE);
	snprintf(path, sizeof(path), "%s/bench.tmp", folder);
	unlink(path);
	fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0666);
	if (fd < 0)
	{
		free(buffer);
		return 0;
	}
	start = now_ms_precise();
	while (done < TOTAL)
	{
		const ssize_t wrote = write(fd, buffer, PIECE);

		if (wrote <= 0)
			break;
		done += (unsigned long)wrote;
	}
	write_ms = now_ms_precise() - start;
	start = now_ms_precise();
	fsync(fd);
	sync_ms = now_ms_precise() - start;
	if (done == TOTAL)
	{
		lseek(fd, 0, SEEK_SET);
		start = now_ms_precise();
		for (done = 0; done < TOTAL;)
		{
			const ssize_t got = read(fd, buffer, PIECE);

			if (got <= 0)
				break;
			done += (unsigned long)got;
		}
		read_ms = now_ms_precise() - start;
	}
	close(fd);
	unlink(path);
	free(buffer);
	if (done != TOTAL || write_ms <= 0.0 || read_ms <= 0.0)
		return 0;
	*write_mbs = (float)(TOTAL / 1048576.0 / (write_ms / 1000.0));
	*read_mbs = (float)(TOTAL / 1048576.0 / (read_ms / 1000.0));
	platform_log("SD card: write %.1f MB/s, read %.1f MB/s (2 MB in 128 KB pieces; the write's fsync took %.0f ms more)", (double)*write_mbs, (double)*read_mbs, sync_ms);
	return 1;
}

static volatile uint32_t card_started, card_done;
static volatile float card_write, card_read;
static volatile int card_check_switch = -1;

int wiiu_notice_card_speeds(float *write_mbs, float *read_mbs)
{
	if (!load(card_done))
		return 0;
	*write_mbs = card_write;
	*read_mbs = card_read;
	return card_write > 0.0f;
}

static void *card_thread(void *unused)
{
	unsigned tries;
	float write_mbs = 0.0f, read_mbs = 0.0f;

	(void)unused;
	wiiu_thread_background("card check");
	/* (a few seconds on: the menu's own reads are done, and the benchmark is not measured against them) */
	sleep(4);
	/* never while a checkpoint is being written, and only in the foreground (the card is written only there); it waits up to a minute, then does not run */
	for (tries = 0; tries < 120; tries++)
	{
		if (!async_save_running() && wiiu_in_foreground())
			break;
		usleep(500000);
	}
	if (tries < 120 && wiiu_notice_card_bench(NOTICE_FOLDER, &write_mbs, &read_mbs))
	{
		/* (a checkpoint that began during the benchmark made it slower than the card is: it is not trusted) */
		if (async_save_running())
		{
			platform_log("SD card: a checkpoint was written during the check; its numbers are not used");
			write_mbs = read_mbs = 0.0f;
		}
		else
		{
			card_write = write_mbs;
			card_read = read_mbs;
		}
	}
	else
		platform_log("SD card: the speed check did not run (halo/card_check.txt)");
	store(card_done, 1u);
	return NULL;
}

static volatile uint32_t main_menu_now;

void wiiu_notice_menus(int main_menu)
{
	store(main_menu_now, main_menu ? 1u : 0u);
	if (main_menu && !load(card_started))
	{
		pthread_t thread;

		store(card_started, 1u);
		if (!switch_on("card_check.txt", &card_check_switch))
		{
			platform_log("SD card: the speed check is off (halo/card_check.txt 0)");
			return;
		}
		if (pthread_create(&thread, NULL, card_thread, NULL) == 0)
			pthread_detach(thread);
		else
			platform_log("SD card: no thread for the speed check");
	}
}

/* ---------- the panel */

#define MESSAGE_MS 20000u
#define GENERATION_BIT 0x80000000u

static uint32_t message_since;   /* the renderer's: when the card's message came up */
static int message_told;         /* ... and it has been shown (once a run) */

static volatile uint32_t loading_active, loading_percent, loading_generation, loading_last_present;
static volatile int loading_switch = -1;

int wiiu_notice_panel(char *text, unsigned capacity, unsigned *generation, int *bar)
{
	*bar = -1;
	if (load(loading_active))
	{
		const uint32_t now = load(loading_generation);
		const unsigned percent = load(loading_percent);

		if ((now | GENERATION_BIT) != *generation && capacity)
		{
			snprintf(text, capacity, "LOADING...\n%u%%", percent);
			*generation = now | GENERATION_BIT;
		}
		*bar = (int)percent;
		return 1;
	}
	if (message_told && !message_since)
		return 0;
	if (load(card_done) && load(main_menu_now))
	{
		float write_mbs, read_mbs;

		if (!message_told && wiiu_notice_card_speeds(&write_mbs, &read_mbs) && wiiu_notice_card_is_slow(write_mbs, read_mbs))
		{
			if (!message_since)
				message_since = now_ms() | 1u;
			if (now_ms() - message_since < MESSAGE_MS)
			{
				if ((GENERATION_BIT | 0x40000000u) != *generation && capacity)
				{
					wiiu_notice_card_text(text, capacity, write_mbs, read_mbs);
					*generation = GENERATION_BIT | 0x40000000u;
				}
				return 1;
			}
		}
		if (message_since)
		{
			message_told = 1;
			message_since = 0;
		}
		return 0;
	}
	/* (the main menu was left while the message was up: it was told) */
	if (message_since)
	{
		message_told = 1;
		message_since = 0;
	}
	return 0;
}

/* ---------- the saving mark */

static volatile int saving_switch = -1;

int wiiu_notice_saving(void)
{
	static uint32_t until;
	uint32_t now;

	if (!switch_on("saving_mark.txt", &saving_switch))
		return 0;
	now = now_ms();
	if (async_save_running())
		until = now + 1000u;
	return until && (int32_t)(until - now) > 0;
}

/* ---------- the loading bar */

void wiiu_loading_begin(void)
{
	if (!switch_on("loading_bar.txt", &loading_switch))
		return;
	store(loading_percent, 0u);
	store(loading_last_present, now_ms());
	__atomic_add_fetch(&loading_generation, 1u, __ATOMIC_ACQ_REL);
	store(loading_active, 1u);
}

void wiiu_loading_stage(int percent)
{
	if (!load(loading_active))
		return;
	if (percent < 0)
		percent = 0;
	if (percent > 100)
		percent = 100;
	if ((unsigned)percent > load(loading_percent))
	{
		store(loading_percent, (unsigned)percent);
		__atomic_add_fetch(&loading_generation, 1u, __ATOMIC_ACQ_REL);
	}
	/* a frame to show it on, if none has been shown for a quarter of a second */
	if (now_ms() - load(loading_last_present) >= 250u)
	{
		store(loading_last_present, now_ms());
		main_loading_present();
	}
}

void wiiu_loading_precache(float fraction)
{
	if (!load(loading_active))
		return;
	if (fraction < 0.0f)
		fraction = 0.0f;
	if (fraction > 1.0f)
		fraction = 1.0f;
	/* (the game presents its own frames in this loop: the stage is set and no frame is made) */
	{
		const unsigned percent = (unsigned)(fraction * 40.0f);

		if (percent > load(loading_percent))
		{
			store(loading_percent, percent);
			__atomic_add_fetch(&loading_generation, 1u, __ATOMIC_ACQ_REL);
		}
		store(loading_last_present, now_ms());
	}
}

void wiiu_loading_end(void)
{
	if (!load(loading_active))
		return;
	store(loading_active, 0u);
}
