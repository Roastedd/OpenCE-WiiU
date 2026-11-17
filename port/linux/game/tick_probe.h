/*
TICK_PROBE.H

A named part of a tick, timed for the Wii U's log (the tick parts, port/wiiu/src/wiiu_tick_parts.c): two reads of the time base. A part inside another
(bp_physics inside bp_moving inside obj_biped) is counted in both; the log lists every part by its time a tick, most first, so the nesting is read from the
names. Nothing on any other build, and nothing here touches what a tick computes.

	TICK_PROBE_START(started);
	... the part ...
	TICK_PROBE_STOP(started, "name");
*/
#ifndef TICK_PROBE_H
#define TICK_PROBE_H

#ifdef HALO_WIIU
long long wiiu_tick_part_now(void);
void wiiu_tick_part(const char *name, long long started);
#define TICK_PROBE_START(variable) long long variable = wiiu_tick_part_now()
#define TICK_PROBE_STOP(variable, name) wiiu_tick_part(name, variable)
#else
#define TICK_PROBE_START(variable) ((void)0)
#define TICK_PROBE_STOP(variable, name) ((void)0)
#endif

#endif
