/*
WIIU_JOIN_TRACE.C

A record of a client's join, written straight to the card from the thread that is at the step (not through the log writer thread, which a freeze takes with it):
one line a step, flushed and fsync'd before the call returns, so that the last line in halo/join-steps.txt names the last stage that was reached. The black
box thread (wiiu_shell.c) adds a heartbeat line every second while the window is open: if those stop too, the whole console stopped, and the step number in the
last one says where.

A window opens at the first step and closes 25 s after the last wiiu_join_step: the steps of a frame (wiiu_join_frame_step) are written for the first
JOIN_FRAME_LINES of them only, so that the writes do not take the game's frames for good.
*/

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <coreinit/time.h>

#include "wiiu_join_trace.h"
#include "wiiu_shell.h"

#define JOIN_FILE WIIU_DATA_DIRECTORY "/join-steps.txt"
#define JOIN_WINDOW_MS 25000u
#define JOIN_FRAME_LINES 170u
#define JOIN_MAXIMUM_LINES 1200u

static FILE *join_file;
static volatile unsigned join_step_number;
static volatile unsigned join_frame_lines;
static volatile unsigned join_lines;
static volatile uint32_t join_last_ms;
static volatile int join_open;
static char join_last_name[64] = "(none)";

static void join_remember(char const *name)
{
	unsigned at;

	for (at = 0; at + 1 < sizeof(join_last_name) && name[at]; at++)
		join_last_name[at] = name[at];
	join_last_name[at] = 0;
}

static uint32_t join_now_ms(void)
{
	return (uint32_t)(OSTicksToMilliseconds(OSGetSystemTime()));
}

long wiiu_study_number(const char *name, long fallback);

/* off unless halo/join_steps.txt holds a 1: every line is a write to the card that the thread waits for */
static int join_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
		enabled = wiiu_study_number("join_steps.txt", 0) > 0;
	return enabled;
}

static void join_write(char const *kind, unsigned number, char const *name)
{
	if (!join_enabled())
		return;
	if (!join_file)
	{
		join_file = fopen(JOIN_FILE, "a");
		if (!join_file)
			return;
		setvbuf(join_file, NULL, _IONBF, 0);
	}
	if (join_lines >= JOIN_MAXIMUM_LINES)
		return;
	join_lines++;
	fprintf(join_file, "%u.%03u %s #%u %s\n", (unsigned)(join_now_ms() / 1000), (unsigned)(join_now_ms() % 1000), kind, number, name);
	fflush(join_file);
	fsync(fileno(join_file));
}

/* a stage of the join: always written; opens the window, or keeps it open */
void wiiu_join_step(char const *name)
{
	unsigned number = ++join_step_number;

	join_remember(name);
	join_last_ms = join_now_ms();
	join_open = 1;
	join_write("step", number, name);
}

/* a stage inside a frame or a tick: written while the window is open, the first JOIN_FRAME_LINES of them */
void wiiu_join_frame_step(char const *name)
{
	unsigned number;

	if (!join_open || join_frame_lines >= JOIN_FRAME_LINES)
		return;
	if (join_now_ms() - join_last_ms > JOIN_WINDOW_MS)
	{
		join_open = 0;
		return;
	}
	join_frame_lines++;
	number = ++join_step_number;
	join_remember(name);
	join_write("frame", number, name);
}

/* the black box thread's: once a second, while the window is open */
void wiiu_join_heartbeat(void)
{
	if (!join_open)
		return;
	if (join_now_ms() - join_last_ms > JOIN_WINDOW_MS)
	{
		join_open = 0;
		return;
	}
	join_write("beat", join_step_number, join_last_name);
}
