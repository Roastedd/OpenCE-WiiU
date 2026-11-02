/*
WIIU_TICK_PARTS.C

The game thread's time in each part of a tick (source/game/game.c's game_tick: the parts its profile_scope sections name,
units, ai, effects, objects and the rest), said in the log every 150 ticks as the milliseconds each takes a tick, most first.
The frame parts line (d3d8_gx2.c) gives a tick's whole time; this says where it goes, which the sampler's flat profile of
b30 could not (2026-10-31). A part costs two reads of the time base.
*/

#include <coreinit/time.h>

#include <stdio.h>
#include <string.h>

void platform_log(const char *format, ...);

/* wiiu_bw_kernels.c: what the game asked of the Broadway kernels' functions in these ticks */
void wiiu_bw_tick_report(unsigned long ticks);

#define PART_LIMIT 32

static struct
{
	const char *name;
	OSTime ticks;
} parts[PART_LIMIT];
static int part_count;
static unsigned long game_ticks;
static OSTime whole;

long long wiiu_tick_part_now(void)
{
	return (long long)OSGetSystemTime();
}

void wiiu_tick_part(const char *name, long long started)
{
	const OSTime spent = OSGetSystemTime() - (OSTime)started;
	int index;

	for (index = 0; index < part_count; index++)
	{
		if (parts[index].name == name || !strcmp(parts[index].name, name))
			break;
	}
	if (index == part_count)
	{
		if (part_count == PART_LIMIT)
			return;
		parts[part_count++].name = name;
	}
	parts[index].ticks += spent;
}

static const char *short_name(const char *name)
{
	static char text[40];
	size_t length;

	if (!strncmp(name, "game_tick_", 10))
		name += 10;
	snprintf(text, sizeof(text), "%s", name);
	length = strlen(text);
	if (length > 8 && !strcmp(text + length - 8, "_section"))
		text[length - 8] = '\0';
	return text;
}

/* at the end of each tick */
void wiiu_tick_done(long long started)
{
	char line[200];
	size_t used = 0;
	int order[PART_LIMIT], count = 0, index, slot;

	whole += OSGetSystemTime() - (OSTime)started;
	if (++game_ticks < 150)
		return;
	/* (most first) */
	for (index = 0; index < part_count; index++)
	{
		for (slot = count; slot > 0 && parts[order[slot - 1]].ticks < parts[index].ticks; slot--)
			order[slot] = order[slot - 1];
		order[slot] = index;
		count++;
	}
	platform_log("tick parts, ms a tick (%lu ticks, %.2f ms each in all):", game_ticks,
		(double)OSTicksToMicroseconds(whole) / 1000.0 / (double)game_ticks);
	for (index = 0; index < count; index++)
	{
		used += (size_t)snprintf(line + used, sizeof(line) - used, "%s%s %.2f", used ? ", " : "", short_name(parts[order[index]].name),
			(double)OSTicksToMicroseconds(parts[order[index]].ticks) / 1000.0 / (double)game_ticks);
		if (used > 150 || index == count - 1)
		{
			platform_log("tick parts: %s", line);
			used = 0;
		}
	}
	wiiu_bw_tick_report(game_ticks);
	for (index = 0; index < part_count; index++)
		parts[index].ticks = 0;
	game_ticks = 0;
	whole = 0;
}
