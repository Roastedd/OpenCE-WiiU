/*
TICK_OVERLAP.H

Stage 4 of the plan to run the game's ticks on a thread of their own (the tools repository's reports/research/2026-11-01/ticks-off-thread.md,
section 9; ticks-stage4.md says what was built, what each state does and what to run on the console): the ticks run beside the drawing. For the
Wii U only (HALO_WIIU).

What runs where, when the overlap is on (halo/tick_overlap.txt, with halo/tick_thread.txt and halo/tick_snapshot.txt on; the queues of
tick_owners.h are put on with it):

- The tick thread (core 0) runs the tick side of each frame as a job the game thread gives it: the tick-side steps of the main loop (the debug keys,
  the cheats, the hs lines waiting for it, player_control_update, the ticks, the director, the observer and the game engine's per-frame update) on a
  copy of the input the game thread took (tick_input.h). The job's ticks are the ones the clock decides on the game thread before the job is posted
  (game_time_overlap_step), so they are the ones the serial loop would run, in the same order, from the same input.
- The game thread posts the job and goes on to draw the frame, from the ring of the render view snapshot (tick_snapshot.h): the last two ticks the
  tick thread finished, blended by the clock delayed by one tick, so that the tick it needs has been published. The frame's own work (game_frame:
  the particles, contrails, widgets, the looping sounds, the wind) runs on the game thread inside that view.
- The game thread waits for the job in flight at the top of the next iteration (the sync point). That is where it takes what the tick thread
  published (the display facing, the camera for the sound), applies what the tick thread asked of it (the queues of tick_owners.h), and handles
  the main loop's flags (a map to load, a save): with the tick thread idle, as in the serial loop. So the tick thread is at most one frame ahead of
  the game thread's drawing, and the game thread never waits for it inside a frame.

When any of these holds, the frame is the serial loop's (the tick side run by the game thread, or by the stage 1 thread with the game thread waiting
for it): a network game or a saved film, more than one local player, a cinematic, the main menu's scene, the game paused (the menu's or the
clock's), the clock stopped or not at its own speed, a game engine running, a recording, no interpolation (display.interpolation). When the
frame after is the overlap's again, the ring is looked at first: if it has no capture of the tick it needs, that frame's job is run at once
and waited for (and its picture is the latest tick's).
*/

#ifndef __TICK_OVERLAP_H
#define __TICK_OVERLAP_H

#ifdef HALO_WIIU

#include "tick_owners.h"
#include "tick_stack.h"
#include "math/real_math.h"

struct observer_result;

/* ---------- the switch */

/* halo/tick_overlap.txt (HALO_TICK_OVERLAP on the host) holds a number above 0, and so does halo/tick_thread.txt. Read once. */
boolean tick_overlap_requested(void);
/* and the rest holds: the snapshot is on (its mode's bit 1), the interpolation is (display.interpolation), the thread started. The first call
decides, and logs. */
boolean tick_overlap_enabled(void);

/* ---------- threads */

/* set once a tick thread exists: the markers of the collision tests and the object searches are then the game thread's own and the tick thread's
(objects.c, structures.c) */
extern volatile int tick_overlap_markers_split;

/* set once the ticks run beside the drawing: what they read of the sound listeners is a copy taken at the start of each frame (sound_manager.c) */
extern volatile int tick_overlap_listeners_frozen;
void sound_manager_freeze_listeners(void);

/* The game thread tells where its stack is when its main loop starts (tick_stack.h); the calling thread is the game thread if its stack is there. */
void tick_overlap_note_render_thread(void);
#define tick_overlap_thread_is_render() (TICK_OVERLAP_ON_RENDER_STACK())
/* the calling thread is the tick thread */
boolean tick_overlap_thread_is_tick(void);

/* The tick thread, which the serial loop's stage 1 mode (halo/tick_thread.txt alone) uses too: work run on it while the game thread waits. Starts
the thread if it is not (once); FALSE if it could not be started. */
typedef void (*tick_overlap_work)(void *argument);
boolean tick_overlap_start_thread(void);
void tick_overlap_run_and_wait(tick_overlap_work work, void *argument);

/* ---------- the frame (the main loop's) */

/* The top of each iteration of the main loop: waits for the job in flight, takes what it published, applies what it asked of the game thread. */
void tick_overlap_sync_point(void);

/* The tick side of this frame, if it is the overlap's: the job is posted and the picture of the frame is set up (the view of the clock, the pair
of the ring); the frame's dt is what the serial loop gives player_control_update and the clock. FALSE if the frame is the serial loop's. */
boolean tick_overlap_post_frame(real dt);
/* a line of the log said in lines that fit the console's (tick_snapshot.c) */
void tick_snapshot_say(char const *format, ...);
/* the tick thread was late for a while (tick_overlap.c): the frames are the serial loop's until it is tried again */
boolean tick_overlap_late(void);
/* whether the frame being run is the overlap's (the main loop's game thread, until the frame has been presented) */
boolean tick_overlap_frame_active(void);
/* the job is still running (the game thread draws meanwhile) */
boolean tick_overlap_job_in_flight(void);
/* the frame's work is done but for the throttle and the present */
void tick_overlap_draw_done(void);
/* the end of the frame, after its present */
void tick_overlap_frame_end(void);
/* a map starts or ends, a game state is loaded: the tick thread is idle (the main loop's flags are handled at the sync point); forget the ring and the
shadows */
void tick_overlap_map_reset(void);
/* the map's decision (smooth_defaults.c): the overlap, the snapshot behind it and the owners' queues made again for the map, from nothing; the tick thread idle */
void tick_overlap_decide(int mid_map);
void tick_snapshot_decide(void);
/* the numbers the map summary reads, since the start of the run: frames, overlapped frames, serial frames, falls back to the serial loop, times the clock was held */
void tick_overlap_summary_counts(unsigned long counts[5]);

/* ---------- what the queues ask (tick_owners.c) */

/* the game thread's queues are not drained while a job runs (what the job asks for is applied at the next sync point, whichever way the threads
are timed), unless a producer waits for room in one */
boolean tick_overlap_defer_drain(struct tick_queue const *queue);
/* the tag a record pushed now carries: the jobs posted so far, if the game thread pushes it; and the tag a drain by the tick thread stops
after: it applies what the game thread pushed before its job was posted, not what it pushes beside it */
unsigned long tick_overlap_push_tag(void);
unsigned long tick_overlap_drain_limit(void);
/* the owner of a queue is not running (a producer may apply its records itself) */
boolean tick_overlap_owner_blocked(long owner);
/* the calling thread is the owner's (a producer of a full queue of its own drains it) */
boolean tick_overlap_owner_is_caller(long owner);
/* the sync point drains the game thread's queues whatever is in flight */
boolean tick_overlap_forcing_drain(void);

/* ---------- reads of what the tick thread published */

/* the game thread's copy of the camera and facing the last job left, taken at the sync point (observer.c, player_control.c) */
boolean tick_overlap_reading_published(void);

/* the clock's pair for the frame being drawn */
real tick_overlap_view_fraction(void);
long tick_overlap_view_tick(void);

/* ---------- the tick-side steps (main.c) */

/* The steps of the main loop's tick side that the tick thread runs for a job: returns the ticks run. */
long main_overlap_tick_side(real dt, long ticks_elapsed, real leftover_after, long previous_time);

/* ---------- timing (the console's clock, in its ticks; a host clock's microseconds elsewhere) */

unsigned long long tick_overlap_now(void);
/* the game thread's time in the blends of a frame, and the log (every few seconds, from the frame's end) */
void tick_overlap_note_blend(unsigned long long elapsed);
/* the tick thread's, adding to the numbers under no lock (single words; a count that loses an add is no matter): a capture of a tick, and the
cameras and the commit that make a job's captures visible */
void tick_overlap_note_capture(unsigned long long elapsed);
void tick_overlap_note_publish(unsigned long long elapsed);
/* a producer waits for room in a queue */
void tick_overlap_note_push_wait(void);
void tick_overlap_log(unsigned long long now);
/* the overlap's monotonic counts for the detailed performance overlay (port/wiiu/src/wiiu_perf.h), written into the overlap_ fields of totals */
struct perf_totals;
void tick_overlap_perf_read(struct perf_totals *totals);

/* ---------- the other units' (tick_snapshot.c, game_time.c, render_interpolation.c) */

/* the ring of the snapshot */
int tick_snapshot_enabled(void);
long tick_snapshot_none(void);
void tick_snapshot_capture_tick(long tick, boolean commit);
void tick_snapshot_commit(void);
long tick_snapshot_published_tick(void);
boolean tick_snapshot_pin(long tick);
void tick_snapshot_unpin(void);
void tick_snapshot_job_end(long tick, long ticks_in_job, long previous_job_tick);
real_matrix4x3 const *tick_snapshot_previous_node_matrices(long object_index, short node_count);
long tick_snapshot_pose_serial(long absolute_index, boolean previous);
int tick_snapshot_rotation_cache(void);
int tick_snapshot_view_cameras(short local_player_index, struct observer_result const **previous, struct observer_result const **latest);
void render_interpolation_camera_between(struct observer_result const *a, struct observer_result const *b, real t, struct observer_result *result);
void render_interpolation_overlap_set(boolean on, long tick);
/* the fine timers of the copies (tick_snapshot.c, render_interpolation.c): the game thread's reads, and the blends' numbers, said by the overlap's log */
void tick_snapshot_reads_total(unsigned long *reads, unsigned long *fallbacks, unsigned long *applied);
void render_interpolation_blend_log(double ticks_per_ms, unsigned long frames);
void tick_snapshot_view_log(double ticks_per_ms);

/* a test aid (halo/tick_overlap_jitter.txt, milliseconds; the host: HALO_TICK_OVERLAP_JITTER): the tick thread sleeps a random time up to that, at a quarter of the places it calls this from, while
a job runs, so that the drawing thread meets the game state at all the moments a tick leaves it in: between a light's leaving its clusters and its being let go of, between a tick and the capture,
and so on. It does nothing on any other thread, outside a job, and with the file absent. */
void tick_overlap_jitter(void);

/* Something the tick thread's job does that cannot be done beside a frame being drawn - a structure bsp switched (a campaign script's switch_bsp: the bsp's tag data is let go of and another
loaded, and every cluster number the frame's copies hold is another's) - is done by the game thread instead, at its sync point, where it waits for the job, while the tick thread waits here for
it to be done: the script goes on after it as it did with the ticks on one thread. FALSE (nothing done, nothing waited for) if the caller is not the tick thread in an overlap job: it does it itself. */
boolean tick_overlap_game_thread_do(void (*function)(void *), void *argument);

/* the lights that fade (object_lights.c), once after the ticks of each job */
void lights_fade_update(void);

/* A network game's overlap frame (halo/net_overlap.txt, smooth_defaults.h): the drawing waits for the tick thread's job before it reads what the snapshot does not copy of the game
engine's and the netcode's state (tick_overlap.c has why). Cheap when no such frame is being drawn: a load and a compare. */
extern volatile int tick_overlap_net_pending;
void tick_overlap_net_barrier(void);
#define TICK_OVERLAP_NET_BARRIER() do { if (tick_overlap_net_pending) tick_overlap_net_barrier(); } while (0)

/* the clock */
boolean game_time_overlap_step(real time_delta_sec, long *time_now, long *ticks_elapsed, real *leftover_after, real *speed);
long game_time_overlap_run(long ticks_elapsed, real leftover_after);
void game_time_set_view(boolean on, long time, short elapsed, real fraction);
long game_time_overlap_time(void);
real game_time_overlap_leftover(void);

#endif /* HALO_WIIU */

#endif /* __TICK_OVERLAP_H */
