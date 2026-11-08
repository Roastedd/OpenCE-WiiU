/*
WIIU_PACING.H

Frame pacing on the Wii U: what the log says about the time between frames and the display's flips, and the card's policy for the swap
(halo/pacing.txt). Both are functions of their state and of numbers the caller measured, so that a host test (port/wiiu/tests/test_pacing.c) can
run them on frames made up for it; wiiu_gx2.c's present() measures and calls them.

The statistics are for the log, every five seconds (wiiu_pacing_format_*): how long the frames took (a histogram, and how many were over 17.5, 25
and 34 ms: a refresh is 16.7 ms, one and a half 25, two 33.3, each with a little room), where the GX2 thread's time went (working, waiting for the
game thread to hand it a stream, waiting for the display's flip), and what the display did (how many refreshes each flip lasted, and how many flips
came away from a vsync, which is what a tear is).

The policy (mode, halo/pacing.txt): 0 is the game as it has been (every swap waits for the vsync, the interval GX2 starts with). 1 is "tear when late":
a swap waits for the vsync only if the display has been showing the frame before for less than a refresh; if the frame is late (the display has
passed a vsync with no new frame and nothing waits to be flipped) the swap is made at interval 0, so that the frame is shown as soon as the GPU is
done with it, torn, instead of for a refresh and more of waiting. 2 never waits for the vsync, and holds the frames to a refresh each at the fastest. 3 is "a deeper queue": every swap waits for the vsync, as in 0, but up to
depth swaps (2 unless the card says 1) may wait for their flip before present() waits, so that the game thread gets further ahead of the display and a
slow frame after a fast one is absorbed (no tear, one refresh more of latency at most).

Two more policies are here, each behind a card file of its own, off when the file is absent:

The display-clock stamp (halo/pacing_stamp.txt, wiiu_stamp_*): the interpolation's fraction is the clock's leftover at the moment the frame was begun, so a frame
that took 25 ms to build and is shown for one refresh depicts 25 ms of motion in 16.7 ms of screen time. The stamp predicts, from the frames before, the refresh at
which the frame being built will flip, and moves the picture's time by how much later (or earlier) than the average frame that is. The tick clock is not touched:
only the picture between two ticks. A shrinkage factor learned from the frames that have flipped (never above 1, 0 until it has seen 30) keeps it from doing worse
than not stamping, and the move is clamped to one refresh and never takes the picture's time backwards.

The locked 30 (halo/pacing_lock.txt, wiiu_lock30_*): a swap interval of 2 holds every frame for two refreshes, which is an evenly paced 30 a second. The rule that
switches to it and back is a function of the frames' work and the time, with a dwell and a gap between its two thresholds, so that a scene on the edge does not
change its cadence over and over (a change of cadence is itself a stutter).
*/

#ifndef WIIU_PACING_H
#define WIIU_PACING_H

#include <stdint.h>

#define WIIU_PACING_BINS 10
/* the frame time histogram's upper edges in ms, the last bin being everything above the last edge */
extern const float wiiu_pacing_edges[WIIU_PACING_BINS - 1];

#define WIIU_PACING_REFRESH_MS (1000.0f / 60.0f)

struct wiiu_pacing_stats
{
	/* the frames (time between the ends of two presents) */
	unsigned frames;
	double sum_ms;
	float longest_ms;
	unsigned bins[WIIU_PACING_BINS];
	unsigned over_17, over_25, over_34; /* over 17.5, 25 and 34 ms */
	/* the GX2 thread's time over those frames */
	double work_ms, idle_ms, flip_ms;
	/* the display */
	unsigned flips;
	unsigned shown[4]; /* flips that lasted 1, 2, 3 and 4 or more refreshes */
	unsigned away; /* flips more than 2 ms from a vsync */
	/* the swaps the policy made */
	unsigned swaps_synced, swaps_tearing;
};

void wiiu_pacing_reset(struct wiiu_pacing_stats *stats);
/* a frame: the time since the frame before, and how much of it the GX2 thread spent waiting for the flip and for the game thread (idle_ms < 0: not measured) */
void wiiu_pacing_frame(struct wiiu_pacing_stats *stats, float frame_ms, float flip_wait_ms, float idle_ms);
/* a flip the display made: the time since the flip before, and where in the refresh it came (ms after the last vsync, taken modulo a refresh) */
void wiiu_pacing_flip(struct wiiu_pacing_stats *stats, float since_last_flip_ms, float after_vsync_ms);
/* texts for the log (one line each, under 190 characters); the second is empty if no frames were counted. Return the length. */
int wiiu_pacing_format_frames(const struct wiiu_pacing_stats *stats, char *out, unsigned size);
int wiiu_pacing_format_threads(const struct wiiu_pacing_stats *stats, char *out, unsigned size);
int wiiu_pacing_format_display(const struct wiiu_pacing_stats *stats, char *out, unsigned size);
/* the late frames and flips of the window, as a share and a count a minute of the window's frame time (log only: nothing reads it): "pacing: late: ..." */
int wiiu_pacing_format_late(const struct wiiu_pacing_stats *stats, char *out, unsigned size);

/* the policy */
struct wiiu_pacing_policy
{
	int mode; /* halo/pacing.txt's first number: 0 off, 1 tear when late, 2 never wait for the vsync, 3 a deeper queue that does not tear */
	unsigned depth; /* its second: the swaps that may wait for their flip before present() waits (1 as it has been, but 2 for mode 3; 2 at most: GX2 holds two swaps and blocks the third itself) */
};

/* the card's text read: false if it does not hold a mode of 0 to 3 (the policy is then off) */
int wiiu_pacing_parse(struct wiiu_pacing_policy *policy, const char *text);
/* the swap interval for the swap about to be made: since_flip_ms is the time since the display's latest flip, queued the swaps made and not yet flipped */
unsigned wiiu_pacing_interval(const struct wiiu_pacing_policy *policy, float since_flip_ms, unsigned queued);
/* the time to wait before the swap is made, in ms (mode 2: frames are held to a refresh each, less a little), given the time since the swap before */
float wiiu_pacing_hold_ms(const struct wiiu_pacing_policy *policy, float since_last_swap_ms);

/* ---------- the display-clock stamp */

#define WIIU_STAMP_FRAMES 16
#define WIIU_STAMP_GPU_STEPS 13 /* the candidates for the time from the swap to the frame being ready for a flip: 0, 1 .. 12 ms */
#define WIIU_STAMP_CLASSES 2 /* the frame's class: 0 a frame between ticks, 1 a frame whose picture has a new tick pair (its work is longer) */

struct wiiu_stamp_frame
{
	unsigned sequence; /* 0: not in use */
	unsigned klass;
	double start_ms; /* the clock step of the frame: the fraction is of this moment */
	double predicted_latency_ms; /* the flip expected, less start_ms */
	double predicted_deviation_ms; /* that less the mean of the predictions */
	double predicted_hard_ms; /* the flip expected without the spread: on the grid */
	double predicted_soft_ms; /* the flip expected with it */
	double correction_ms; /* what was (or, in measure mode, would have been) added to the picture's time */
	double swap_ms; /* the swap made, 0 until */
	unsigned swap_index; /* GX2's count of swaps with it */
	double flip_ms; /* the display's flip, 0 until */
	int flip_exact;
	int predicted;
};

struct wiiu_stamp
{
	int mode; /* 0 off, 1 the picture's time is moved, 2 measure only: everything is computed and logged, nothing is moved, 3 a test aid: the move is test_ms, always */
	double test_ms;
	double tick_ms; /* the length of a tick (33.3 ms) */
	double refresh_ms; /* the display's period, learned from the flips (16.667 until then) */
	double grid_ms; /* the latest flip seen: the grid is this plus whole refreshes */
	unsigned flips_seen; /* GX2's count of flips at that flip */
	int have_grid;
	double work_ms[WIIU_STAMP_CLASSES]; /* the time from the frame's start to its swap, by class (an average of the frames before) */
	unsigned work_seen[WIIU_STAMP_CLASSES];
	double spread_ms; /* the uncertainty of that: twice the mean error */
	double hit[WIIU_STAMP_GPU_STEPS]; /* how often each candidate for the time from the swap to ready told the frame's flip */
	unsigned gpu_best;
	double previous_flip_ms; /* the flip of the last frame scored: a frame flips a refresh after it at least */
	double previous_hard_ms; /* the same for the frame last begun, on the grid */
	double prediction_mean_ms, latency_mean_ms; /* running means of the predicted and the actual time from the start to the flip */
	double covariance, variance; /* of the actual latency's deviation with the prediction's, and the prediction's own */
	double shrink; /* covariance / variance, 0 to 1 */
	unsigned scored; /* frames scored since the start */
	unsigned sequence; /* frames begun */
	struct wiiu_stamp_frame frame[WIIU_STAMP_FRAMES];
	double shown_ms; /* the picture time as last shown (ms on the ticks' axis) */
	int have_shown;
	/* the window of the log */
	unsigned window_frames, window_clamped, window_held, window_backward;
	double sum_raw, sum_raw2, sum_stamped, sum_stamped2, sum_error, sum_move_abs, max_move;
};

void wiiu_stamp_reset(struct wiiu_stamp *stamp, int mode, double test_ms);
/* the card's text: "mode [ms]"; false if it is not a mode of 0 to 3 (the mode is 0 then) */
int wiiu_stamp_parse(const char *text, int *mode, double *test_ms);
/* A frame begins (the game thread, once a frame, with the clock step's time in ms and the frame's class). Returns the move of the picture's time in ms, positive being
later; 0 in measure mode. *sequence is the number to give the swap (never 0). */
double wiiu_stamp_begin(struct wiiu_stamp *stamp, double start_ms, unsigned klass, unsigned *sequence);
/* The frame's picture time on the ticks' axis: the tick pair's later tick and the fraction between the two, moved by move_ms. Never earlier than the picture time last
shown, never past the pair's later tick (1.0) nor before its earlier one. Returns the fraction to draw; held: the pair is held at its end and is left as it is. */
double wiiu_stamp_picture(struct wiiu_stamp *stamp, long tick, double fraction, double move_ms, int held);
/* The GX2 thread made the frame's swap (swap_index: GX2's count of swaps with it) at swap_ms; sequence 0 is a frame that was not begun here (a menu's). */
void wiiu_stamp_swap(struct wiiu_stamp *stamp, unsigned sequence, double swap_ms, unsigned swap_index);
/* The GX2 thread looked at the display: flips is GX2's count of flips, last_flip_ms the time of the latest. Frames whose swaps have flipped are scored. */
void wiiu_stamp_flip(struct wiiu_stamp *stamp, double last_flip_ms, unsigned flips);
/* the window's two lines for the log, a newline between (empty if no frame was scored), then the window is cleared */
int wiiu_stamp_format(struct wiiu_stamp *stamp, char *out, unsigned size);

/* ---------- the locked 30 */

struct wiiu_lock30_settings
{
	int mode; /* halo/pacing_lock.txt's first number: 0 off, 1 locked at once and always, 2 automatic */
	double lock_over_ms; /* a frame is slow when its work is over this: the frame misses 45 a second */
	double lock_share; /* lock when at least this share of the frames of lock_s seconds are slow ... */
	double lock_s;
	double hold_ms; /* ... and at most 20% of them are over this, which a locked 30 would not hold either */
	double unlock_over_ms; /* unlock when at most unlock_share of the frames of unlock_s seconds take over this (a frame that would hold 60) */
	double unlock_share;
	double unlock_s;
	double dwell_s; /* the least time in either state */
	double quiet_s; /* nothing is decided in the first seconds after a reset (a map's start) */
	unsigned max_switches; /* in switch_window_s: then it stays as it is */
	double switch_window_s;
};

#define WIIU_LOCK30_WINDOWS 32 /* seconds of history kept */
#define WIIU_LOCK30_SWITCHES 8

struct wiiu_lock30
{
	struct wiiu_lock30_settings set;
	int locked;
	double bucket_start_ms, since_ms, started_ms;
	unsigned frames, slow, over_hold, over_unlock; /* the open second */
	unsigned h_frames[WIIU_LOCK30_WINDOWS], h_slow[WIIU_LOCK30_WINDOWS], h_hold[WIIU_LOCK30_WINDOWS], h_unlock[WIIU_LOCK30_WINDOWS];
	unsigned h_count; /* seconds in the history (at most WIIU_LOCK30_WINDOWS) */
	unsigned h_next;
	double switch_ms[WIIU_LOCK30_SWITCHES];
	unsigned switches, switches_total, hitches;
	char reason[200]; /* why the last switch was made, for the log */
	int changed; /* set when a switch was made by the last frame: the caller reads and clears it */
};

void wiiu_lock30_defaults(struct wiiu_lock30_settings *set);
/* the card's text: "mode [lock_over_ms lock_share lock_s unlock_over_ms unlock_share unlock_s]"; false if the mode is not 0 to 2 or a number is out of range (mode 0 then) */
int wiiu_lock30_parse(struct wiiu_lock30_settings *set, const char *text);
void wiiu_lock30_reset(struct wiiu_lock30 *lock, const struct wiiu_lock30_settings *set, double now_ms);
/* A frame's work in ms (the game thread's CPU time: not the waits for the display) at now_ms. Returns whether the swaps are to be held for two refreshes from here. */
int wiiu_lock30_frame(struct wiiu_lock30 *lock, double work_ms, double now_ms);

#endif
