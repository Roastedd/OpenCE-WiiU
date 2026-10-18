/*
WIIU_SHELL_WATCH.H

What the Wii U shell watches a running game for, as decisions over numbers, with no clock and no thread of its own: the
caller says what time it is. The game's threads report (a frame went round, what phase they are in, HOME took the
foreground away); a watcher thread asks, now and then, whether anything is to be done:

 - a stall: the game has drawn frames and has stopped for seconds while it has the foreground. It is told in the log at
   3 s, then every 5 s more, up to twelve times, so that a freeze leaves its length and where it was;
 - the end: the system asked the app to close and the game has not finished by the deadline. The shell ends the process;
   an app that does not close in time is killed by the system with nothing in the log.

The fields the threads write are 32 bits wide and read with atomic loads: the Wii U's CPU has no 64-bit atomic
instructions. Times are milliseconds and wrap after 49 days, which a difference of two of them does not mind.
*/

#ifndef HALO_WIIU_SHELL_WATCH_H
#define HALO_WIIU_SHELL_WATCH_H

#include <stddef.h>
#include <stdint.h>

#define WIIU_WATCH_STALL_FIRST_MS 3000u
#define WIIU_WATCH_STALL_STEP_MS 5000u
#define WIIU_WATCH_STALL_TELLINGS 12u
/* how long the app has to close once the system has asked (CoffeeFlix's, which the system has never cut short) */
#define WIIU_WATCH_CLOSE_MS 10000u

typedef struct wiiu_shell_watch
{
	/* written by the game's threads */
	uint32_t frames;
	const char *phase;
	uint32_t phase_ms;
	uint32_t background;
	/* written when the system asks the app to close */
	uint32_t exit_armed;
	uint32_t exit_deadline_ms;
	/* the watcher's own */
	uint32_t seen_frames;
	uint32_t moved_ms;
	uint32_t told;
} wiiu_shell_watch;

typedef enum wiiu_watch_result
{
	WIIU_WATCH_QUIET,
	WIIU_WATCH_STALLED, /* tell the log; *stalled_ms says how long */
	WIIU_WATCH_END      /* the close deadline has passed: end the process */
} wiiu_watch_result;

void wiiu_shell_watch_init(wiiu_shell_watch *watch, uint32_t now_ms);

/* from the game's threads */
void wiiu_shell_watch_frame(wiiu_shell_watch *watch);
/* phase is a string literal (it is read from another thread, perhaps after this one has moved on) */
void wiiu_shell_watch_phase(wiiu_shell_watch *watch, const char *phase, uint32_t now_ms);
/* HOME took the foreground, or gave it back (the time in the background does not count as a stall) */
void wiiu_shell_watch_background(wiiu_shell_watch *watch, int background, uint32_t now_ms);
/* the system asked the app to close; the earliest deadline stands */
void wiiu_shell_watch_arm_exit(wiiu_shell_watch *watch, uint32_t now_ms, uint32_t within_ms);

/* from the watcher */
wiiu_watch_result wiiu_shell_watch_poll(wiiu_shell_watch *watch, uint32_t now_ms, uint32_t *stalled_ms);

/* One line for the log: the frames since the last (and in how long), the phase and when it ended, the foreground. */
size_t wiiu_shell_watch_describe(const wiiu_shell_watch *watch, uint32_t now_ms, uint32_t frames_before,
	uint32_t gap_ms, char *out, size_t capacity);

#endif
