#include "wiiu_shell_watch.h"

#include <stdio.h>

#define LOAD(field) __atomic_load_n(&(field), __ATOMIC_RELAXED)
#define STORE(field, value) __atomic_store_n(&(field), (value), __ATOMIC_RELAXED)

void wiiu_shell_watch_init(wiiu_shell_watch *watch, uint32_t now_ms)
{
	watch->frames = 0;
	watch->phase = NULL;
	watch->phase_ms = now_ms;
	watch->background = 0;
	watch->exit_armed = 0;
	watch->exit_deadline_ms = 0;
	watch->seen_frames = 0;
	watch->moved_ms = now_ms;
	watch->told = 0;
}

void wiiu_shell_watch_frame(wiiu_shell_watch *watch)
{
	__atomic_fetch_add(&watch->frames, 1, __ATOMIC_RELAXED);
}

void wiiu_shell_watch_phase(wiiu_shell_watch *watch, const char *phase, uint32_t now_ms)
{
	STORE(watch->phase_ms, now_ms);
	__atomic_store_n(&watch->phase, phase, __ATOMIC_RELAXED);
}

void wiiu_shell_watch_background(wiiu_shell_watch *watch, int background, uint32_t now_ms)
{
	STORE(watch->background, background ? 1u : 0u);
	/* the watcher counts a stall from its last sight of a new frame: time away is not time stalled */
	if (!background)
		STORE(watch->moved_ms, now_ms);
}

void wiiu_shell_watch_arm_exit(wiiu_shell_watch *watch, uint32_t now_ms, uint32_t within_ms)
{
	const uint32_t deadline = now_ms + within_ms;

	/* (a deadline already armed stands unless this is sooner; the two fields are written by one thread at a time:
	the system's callbacks run on the main thread) */
	if (!LOAD(watch->exit_armed) || (int32_t)(deadline - LOAD(watch->exit_deadline_ms)) < 0)
		STORE(watch->exit_deadline_ms, deadline);
	STORE(watch->exit_armed, 1u);
}

wiiu_watch_result wiiu_shell_watch_poll(wiiu_shell_watch *watch, uint32_t now_ms, uint32_t *stalled_ms)
{
	const uint32_t frames = LOAD(watch->frames);

	if (LOAD(watch->exit_armed) && (int32_t)(now_ms - LOAD(watch->exit_deadline_ms)) >= 0)
		return WIIU_WATCH_END;
	/* no stall before the first frame: a game that is loading has not yet drawn any */
	if (frames != watch->seen_frames || frames == 0)
	{
		watch->seen_frames = frames;
		STORE(watch->moved_ms, now_ms);
		watch->told = 0;
		return WIIU_WATCH_QUIET;
	}
	if (!LOAD(watch->background))
	{
		const uint32_t stalled = now_ms - LOAD(watch->moved_ms);

		if (watch->told < WIIU_WATCH_STALL_TELLINGS &&
			stalled >= WIIU_WATCH_STALL_FIRST_MS + WIIU_WATCH_STALL_STEP_MS * watch->told)
		{
			watch->told++;
			*stalled_ms = stalled;
			return WIIU_WATCH_STALLED;
		}
	}
	return WIIU_WATCH_QUIET;
}

size_t wiiu_shell_watch_describe(const wiiu_shell_watch *watch, uint32_t now_ms, uint32_t frames_before,
	uint32_t gap_ms, char *out, size_t capacity)
{
	const uint32_t frames = LOAD(watch->frames);
	const char *phase = __atomic_load_n(&watch->phase, __ATOMIC_RELAXED);
	size_t used = 0;
	int written;

	if (capacity == 0)
		return 0;
	written = snprintf(out, capacity, "frame %u", (unsigned)frames);
	if (written > 0)
		used = (size_t)written < capacity ? (size_t)written : capacity - 1;
	if (gap_ms)
	{
		written = snprintf(out + used, capacity - used, gap_ms < 1500u ? " (%u in the last second)" : " (%u in the last %u s)",
			(unsigned)(frames - frames_before), (unsigned)((gap_ms + 500u) / 1000u));
		if (written > 0)
			used += (size_t)written < capacity - used ? (size_t)written : capacity - used - 1;
	}
	if (phase)
	{
		const uint32_t ago = now_ms - LOAD(watch->phase_ms);

		written = snprintf(out + used, capacity - used, ", its %s phase ended %u.%02u s ago", phase, (unsigned)(ago / 1000u),
			(unsigned)((ago % 1000u) / 10u));
		if (written > 0)
			used += (size_t)written < capacity - used ? (size_t)written : capacity - used - 1;
	}
	written = snprintf(out + used, capacity - used, LOAD(watch->background) ? "; in the background" : "; in the foreground");
	if (written > 0)
		used += (size_t)written < capacity - used ? (size_t)written : capacity - used - 1;
	return used;
}
