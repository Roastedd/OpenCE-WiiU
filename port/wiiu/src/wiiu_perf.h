/*
WIIU_PERF.H

The numbers of the detailed performance overlay (halo/overlay.txt 2): what is read, from which thread, and the arithmetic that makes them the panel's
and the log's. The overlay is read-only: it counts, and changes nothing of what the game does or draws.

Where each number comes from (reports/research/2026-11-03/overlay-detail.md has the whole table):

- the GX2 thread (wiiu_gx2.c present) owns the frame time between presents, the GPU's own time for a frame, the wait for the display's flip, the time
  its streams took to run, and the draws made: perf_meter_frame adds them as each frame is presented;
- the game thread (d3d8_gx2.c Present, source/main/main.c's frame parts) keeps totals of its time in the ticks, in the rendering, in the waits for the GX2
  thread's slots, in the 30 Hz throttle and in Present's wait for a refresh; the tick thread's and the overlap's totals are tick_overlap.c's; the render
  passes' are wiiu_render_passes.c's; the mixer's and the decoder's are the audio threads'. Each is a monotonic count in the time base's ticks (or a
  count), kept in 32 bits so that another thread reads it whole, and the panel uses the difference between two reads (perf_delta: it holds across a wrap);
- the pools are read at a quarter of a second by the game thread, which owns the caches (wiiu_gx2.c platform_pump_events), and are instantaneous.

Nothing here is a lock, and nothing a thread waits for.
*/

#ifndef WIIU_PERF_H
#define WIIU_PERF_H

#include <stdint.h>
#include <stddef.h>

/* the render passes' marks (wiiu_render_passes.c: visibility, begin, sky, objects, lightmaps, shadows, decals, specular, transparent, effects, detail, hud, end) */
#define PERF_PASSES 13

/* the totals of the other threads, as one read of them (each field a monotonic 32-bit count: time base ticks unless a name says otherwise) */
struct perf_totals
{
	/* the game thread (d3d8_gx2.c) */
	uint32_t game_frames;      /* its presents */
	uint32_t game_work;        /* from one present's end to the next's start: the ticks, the rendering and what is between */
	uint32_t game_present;     /* inside Present: the hand-over of the frame's stream, the waits it makes, the hold for a refresh */
	uint32_t game_sim;         /* the part of its frame in the game's ticks (serial) or in posting the tick thread's job (overlap) */
	uint32_t game_render;      /* the part in main_game_render */
	uint32_t game_ticks;       /* the game's ticks the frames ran or posted (a count) */
	uint32_t game_slot_wait;   /* waiting for the GX2 thread: a free stream slot, a retired stream, an idle renderer */
	uint32_t game_throttle;    /* the 30 Hz throttle's sleeps (main_rasterizer_throttle) */
	uint32_t game_vsync_hold;  /* Present's hold for the display's refresh */
	/* the tick overlap (tick_overlap.c) */
	uint32_t overlap_enabled;  /* the overlap is on (not a count) */
	uint32_t overlap_frames, overlap_overlapped, overlap_serial;
	uint32_t overlap_wait;     /* the game thread's wait for the tick thread at the sync points */
	uint32_t overlap_job;      /* the tick thread's time in its jobs */
	uint32_t overlap_jobs;
	/* the render passes (wiiu_render_passes.c), and the windows they were made in */
	uint32_t pass[PERF_PASSES];
	uint32_t pass_windows;
	/* the audio threads (wiiu_audio.c's mixer, dsound_sdl.c's decoder) */
	uint32_t mixer_ticks, mixer_chunks, decoder_ticks;
	/* the objects the visibility search considered and found (structure_visibility.c) */
	uint32_t objects_considered, objects_found;
};

/* the pools (an instant, in KiB so that 4 GiB fits a word); `known` 0: not read (the panel says so) */
enum
{
	PERF_POOL_TEXTURE, PERF_POOL_GEOMETRY, PERF_POOL_SOUND, PERF_POOL_STATE, PERF_POOL_TAGS, PERF_POOL_MEM2, PERF_POOL_EDRAM,
	PERF_POOL_COUNT
};

struct perf_pool
{
	uint32_t used_kib, capacity_kib;
	uint32_t hot_kib;  /* of used: what was touched lately (the caches that keep what they last held), else 0 */
	int fixed;         /* the pool's use is set when the game starts and does not move in play (the game state's allocations): it is never amber or red, and its meter is neutral */
	int lru;           /* a cache that keeps what it last held, so that it fills and stays full: its level follows hot_kib, what is in use, not used_kib */
	int known;
	int full;          /* the pool turned a request away since it was last read (red whatever its level) */
};

/* what the panel says that is not a total */
struct perf_state
{
	unsigned render_width, render_height;
	unsigned drs_lines, drs_full_lines;  /* the lines the game draws at, and at most */
	int drs_on, drs_step;
	int interpolation;                   /* the game draws 60 frames a second (display.interpolation) */
	int governor_step, governor_max;     /* -1: no governor in this build or none running */
};

/* one frame presented by the GX2 thread */
struct perf_frame
{
	float frame_ms;       /* since the last present */
	float flip_wait_ms;   /* of it, the GX2 thread's wait for the display's flip */
	float gx2_busy_ms;    /* the time its streams and this present took, the flip's wait not counted */
	unsigned draws;
};

/* the panel's numbers: the averages of a window, the worst frame in it */
struct perf_view
{
	int valid;
	float seconds;
	unsigned frames;
	float frame_ms, fps, worst_ms;
	float work_ms, gpu_ms, gpu_wait_ms, vsync_ms;   /* gpu_ms 0: no sample */
	float sim_ms, sim_wait_ms, draw_ms, gx2_ms, gx2_busy_percent, audio_ms, audio_percent;
	float tick_thread_ms, tick_thread_busy_percent, ticks_per_second;
	int overlap;                                    /* 0 off, 1 on, 2 serial fallback (on, but this window's frames were the serial loop's) */
	float draw_part_ms[6];                          /* vis, begin, objects, decals, effects, hud (the passes grouped) */
	float draws, objects_found, objects_culled;     /* a frame's */
	struct perf_state state;
	struct perf_pool pool[PERF_POOL_COUNT];
};

/* the sums of a window of GX2 thread frames, and the totals when it began */
struct perf_meter
{
	struct perf_totals base;
	int have_base;
	unsigned frames, gpu_reads;
	double frame_sum, worst, flip_sum, busy_sum, gpu_sum, draws;
};

/* ---------- arithmetic */

/* b - a across a wrap of the 32-bit counts */
uint32_t perf_delta(uint32_t before, uint32_t after);
void perf_meter_reset(struct perf_meter *meter);
void perf_meter_frame(struct perf_meter *meter, const struct perf_frame *frame);
/* the GPU's own time for a frame, when it is read (it comes some frames after the frame) */
void perf_meter_gpu(struct perf_meter *meter, float gpu_ms);
/* The window ends: `view` is made from the sums and from the totals' change since the window began (ticks_per_ms: the time base), and the window
begins again. The first call only takes the totals and returns 0. */
int perf_meter_close(struct perf_meter *meter, const struct perf_totals *now, double ticks_per_ms, const struct perf_state *state,
	const struct perf_pool pools[PERF_POOL_COUNT], struct perf_view *view);

/* ---------- pools */

enum { PERF_LEVEL_OK, PERF_LEVEL_AMBER, PERF_LEVEL_RED };

const char *perf_pool_name(int pool);
/* used as a part of 100 of capacity (0 if there is none); the level is amber over 85 percent and red over 95 (or when the pool turned a request away), of
used, or of hot for a cache that keeps what it last held (it is full by design: what is in use is what can run out) */
float perf_pool_percent(const struct perf_pool *pool);
int perf_pool_level(const struct perf_pool *pool);
/* KiB as MiB text with one decimal ("13.4"), at most 9 characters */
void perf_mib_text(char *out, size_t size, uint32_t kib);

/* ---------- text */

/* The log's lines every five seconds from a view, each under 199 characters: 0 the frame, work and waits, 1 the split and the rest ("overlay: ..."), 2 the
pools ("overlay pools: ..."). Returns 0 for a line number past the last. */
#define PERF_LOG_LINES 3
int perf_log_line(char *out, size_t size, const struct perf_view *view, int index);

#endif
