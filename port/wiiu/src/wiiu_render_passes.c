/*
WIIU_RENDER_PASSES.C

The game thread's time in each part of a window's rendering (source/render/render.c marks where each begins), said in the
log every five seconds as the milliseconds each part takes a window. The frame parts line (d3d8_gx2.c) says how long the
rendering takes as a whole (13 to 14 ms a frame on a30, 2026-10-30); this says where those milliseconds go, so that what
is moved off the game thread or made cheaper is chosen by measurement. A part's time includes the Direct3D layer's work
for its draws; "begin" (rasterizer_window_begin) includes the wait for room in the GX2 thread's queue, which at the main menu
is most of a frame (15.6 ms a window at 60 frames a second, 2026-10-31). A mark costs a read of the time base.
*/

#include <coreinit/time.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

void platform_log(const char *format, ...);
void wiiu_render_pass(int pass);

/* the parts, in the order render_window does them; the mark numbered PASS_COUNT ends the window */
static const char *const pass_names[] =
{
	"visibility", "begin", "sky", "objects", "lightmaps", "shadows", "decals",
	"specular", "transparent", "effects", "detail", "hud", "end",
};
#define PASS_COUNT ((int)(sizeof(pass_names) / sizeof(pass_names[0])))

static OSTime pass_ticks[PASS_COUNT];
/* the same parts as monotonic counts of the time base's ticks (32 bits, which wrap in 68 seconds: another thread reads them whole and takes differences), and the
windows made, for the detailed overlay (wiiu_perf.h); the log's sums above are reset every five seconds, these are not */
static volatile uint32_t pass_totals[PASS_COUNT];
static volatile uint32_t pass_windows_total;
static OSTime pass_started, logged_at;
static int current_pass = -1;
static unsigned long windows;
/* the same, since the dynamic quality's (render_governor.c) last line, which says them beside its step */
static OSTime governor_ticks[PASS_COUNT];
static unsigned long governor_windows;

/* one log line of the parts from first to last (a Wii U log line holds 199 characters) */
static void passes_log(int first, int last)
{
	char line[200];
	size_t used = 0;
	int pass;

	for (pass = first; pass <= last && used < sizeof(line); pass++)
	{
		used += (size_t)snprintf(line + used, sizeof(line) - used, "%s%s %.2f", pass == first ? "" : ", ", pass_names[pass],
			(double)OSTicksToMicroseconds(pass_ticks[pass]) / 1000.0 / (double)windows);
	}
	platform_log("render passes, ms a window (%lu windows): %s", windows, line);
}

void wiiu_render_pass(int pass)
{
	const OSTime now = OSGetSystemTime();

	if (current_pass >= 0)
	{
		pass_ticks[current_pass] += now - pass_started;
		governor_ticks[current_pass] += now - pass_started;
		pass_totals[current_pass] += (uint32_t)(now - pass_started);
	}
	pass_started = now;
	current_pass = pass >= 0 && pass < PASS_COUNT ? pass : -1;
	if (pass != PASS_COUNT)
		return;
	windows++;
	governor_windows++;
	pass_windows_total++;
	if (!logged_at)
		logged_at = now;
	if (now - logged_at < (OSTime)OSSecondsToTicks(5))
		return;
	passes_log(0, PASS_COUNT / 2 - 1);
	passes_log(PASS_COUNT / 2, PASS_COUNT - 1);
	memset(pass_ticks, 0, sizeof(pass_ticks));
	windows = 0;
	logged_at = now;
}

/* render_governor.c: the milliseconds a window in each pass since this was last called, and the windows they are the mean of */
unsigned long render_governor_pass_milliseconds(double milliseconds[PASS_COUNT])
{
	const unsigned long counted = governor_windows;
	int pass;

	for (pass = 0; pass < PASS_COUNT; pass++)
		milliseconds[pass] = counted ? (double)OSTicksToMicroseconds(governor_ticks[pass]) / 1000.0 / (double)counted : 0.0;
	memset(governor_ticks, 0, sizeof(governor_ticks));
	governor_windows = 0;
	return counted;
}

/* the detailed overlay's read (wiiu_gx2.c): the parts' totals in the order of pass_names, and the windows made */
unsigned wiiu_render_pass_totals(uint32_t *out, unsigned count, uint32_t *windows_made);

unsigned wiiu_render_pass_totals(uint32_t *out, unsigned count, uint32_t *windows_made)
{
	unsigned i;

	for (i = 0; i < count && i < (unsigned)PASS_COUNT; i++)
		out[i] = pass_totals[i];
	*windows_made = pass_windows_total;
	return (unsigned)PASS_COUNT;
}
