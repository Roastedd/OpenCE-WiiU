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
done with it, torn, instead of for a refresh and more of waiting. 2 never waits for the vsync, and holds the frames to a refresh each at the fastest.
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

/* the policy */
struct wiiu_pacing_policy
{
	int mode; /* halo/pacing.txt's first number: 0 off, 1 tear when late, 2 never wait for the vsync */
	unsigned depth; /* its second: the swaps that may wait for their flip before present() waits (1 as it has been; at most 3) */
};

/* the card's text read: false if it does not hold a mode of 0 to 2 (the policy is then off) */
int wiiu_pacing_parse(struct wiiu_pacing_policy *policy, const char *text);
/* the swap interval for the swap about to be made: since_flip_ms is the time since the display's latest flip, queued the swaps made and not yet flipped */
unsigned wiiu_pacing_interval(const struct wiiu_pacing_policy *policy, float since_flip_ms, unsigned queued);
/* the time to wait before the swap is made, in ms (mode 2: frames are held to a refresh each, less a little), given the time since the swap before */
float wiiu_pacing_hold_ms(const struct wiiu_pacing_policy *policy, float since_last_swap_ms);

#endif
