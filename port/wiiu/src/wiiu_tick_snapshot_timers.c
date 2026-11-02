/*
WIIU_TICK_SNAPSHOT_TIMERS.C

The game thread's time in the render view snapshot (port/linux/game/tick_snapshot.c, and the rotations that render_interpolation.c finds when it
takes a snapshot), said in the log every five seconds as the milliseconds each part takes a frame and a capture, in the style of
wiiu_render_passes.c. They are the cost of the stage that copies the renderer's view of the game state (the ticks still run on the game thread),
and so the time that moves to the tick thread when the ticks overlap rendering; the snapshot reads in the frame cost what they cost in
"render passes".

The game's units call the timers through tick_snapshot.c (tick_snapshot_time and tick_snapshot_time_add), which this file installs when the program
starts; a build without this file (the host's) has no timers. A mark costs a read of the time base. Nothing is timed while halo/tick_snapshot.txt
is absent or holds 0, because nothing calls them.
*/

#include <coreinit/time.h>

#include <stdio.h>
#include <string.h>

void platform_log(const char *format, ...);

/* port/linux/game/tick_snapshot.c's (the game's side, whose names the C library's side does not see in a header) */
void tick_snapshot_timers_install(unsigned long long (*clock)(void), void (*add)(int part, unsigned long long since));

/* the parts, as tick_snapshot.c numbers them */
static const char *const part_names[] =
{
	"headers", "blocks", "capture", "rotations", "view begin", "view end",
};
#define PART_COUNT ((int)(sizeof(part_names) / sizeof(part_names[0])))
#define PART_CAPTURE 2
#define PART_FRAME 6

static OSTime part_ticks[PART_COUNT];
static unsigned long part_calls[PART_COUNT];
static unsigned long frames;
static OSTime logged_at;

static unsigned long long snapshot_clock(void)
{
	return (unsigned long long)OSGetSystemTime();
}

static void snapshot_log(void)
{
	char line[200];
	size_t used = 0;
	int part;
	const double frames_counted = frames ? (double)frames : 1.0;
	const double captures = part_calls[PART_CAPTURE] ? (double)part_calls[PART_CAPTURE] : 1.0;

	for (part = 0; part < PART_COUNT && used < sizeof(line); part++)
	{
		used += (size_t)snprintf(line + used, sizeof(line) - used, "%s%s %.3f", part ? ", " : "", part_names[part],
			(double)OSTicksToMicroseconds(part_ticks[part]) / 1000.0 / frames_counted);
	}
	platform_log("tick snapshot, ms a frame (%lu frames, %lu captures, %.2f captures a frame): %s", frames, part_calls[PART_CAPTURE],
		captures / frames_counted, line);
	platform_log("tick snapshot, ms a capture: capture %.3f (headers %.3f, blocks %.3f), rotations %.3f (%lu snapshots, %.1f a capture)",
		(double)OSTicksToMicroseconds(part_ticks[PART_CAPTURE]) / 1000.0 / captures,
		(double)OSTicksToMicroseconds(part_ticks[0]) / 1000.0 / captures,
		(double)OSTicksToMicroseconds(part_ticks[1]) / 1000.0 / captures,
		(double)OSTicksToMicroseconds(part_ticks[3]) / 1000.0 / captures, part_calls[3], (double)part_calls[3] / captures);
	memset(part_ticks, 0, sizeof(part_ticks));
	memset(part_calls, 0, sizeof(part_calls));
	frames = 0;
}

static void snapshot_add(int part, unsigned long long since)
{
	const OSTime now = OSGetSystemTime();

	if (part == PART_FRAME)
	{
		frames++;
		if (!logged_at)
			logged_at = now;
		if (now - logged_at >= (OSTime)OSSecondsToTicks(5))
		{
			snapshot_log();
			logged_at = now;
		}
		return;
	}
	if (part < 0 || part >= PART_COUNT)
		return;
	part_ticks[part] += now - (OSTime)since;
	part_calls[part]++;
}

__attribute__((constructor)) static void snapshot_timers_install(void)
{
	tick_snapshot_timers_install(snapshot_clock, snapshot_add);
}
