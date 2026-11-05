/*
WIIU_MAP_SUMMARY.H

One note in the log at the end of each map (a level change, the way back to the main menu, a game ended): how the map played, in the figures the 5 s notes
add up to, so that "smooth" is a number. The game thread feeds it, at the end of each frame's present (d3d8_gx2.c): the time between two presents. Nothing
else is read: the governor's frames at each step and the overlap's falls back to the serial loop come from their units' counters at the map's start and end.

    map summary (levels\b30\b30, smooth defaults on): 18432 frames in 312.4 s, mean 59.0 fps, 1% low 41.2 fps, hitches over 100 ms: 3
    map summary: 5 s windows (62): 57 fps and over 84.9%, 45 to 57 12.1%, under 45 3.0%; serial fallbacks 0 (clock held 1); overlapped frames 17900;
    governor steps 0 71%, 1 20%, 2 6%, 3 3%

The figures are functions of the frame times and the counters (wiiu_map_summary_format), tested apart from the console (port/wiiu/tests/test_map_summary.c).
A 5 s window is five seconds of frames in a row (the frames' times added up), counted from the map's start; the last, shorter than 2.5 s, is left out of the
shares; its frames are in the rest. The 1% low is the frame rate of the frame time 99 of 100 frames of the map are under (a histogram of half milliseconds up
to 200: an exact figure within half a millisecond).
*/

#ifndef WIIU_MAP_SUMMARY_H
#define WIIU_MAP_SUMMARY_H

#ifdef __cplusplus
extern "C" {
#endif

#define WIIU_MAP_SUMMARY_BINS 401 /* half milliseconds up to 200, then everything above */
#define WIIU_MAP_SUMMARY_STEPS 8

struct wiiu_map_summary
{
	char label[96]; /* the map's name, and how it was decided: what the line says it is of */
	unsigned long frames;
	double sum_ms;
	double longest_ms;
	unsigned long hitches; /* frames of 100 ms or more */
	unsigned long bins[WIIU_MAP_SUMMARY_BINS];
	/* the 5 s windows: the one being filled, and how many ended at 57 fps or more, 45 to 57 and under 45 */
	unsigned long window_frames;
	double window_ms;
	unsigned long windows, windows_fast, windows_middle, windows_slow;
	/* the counters at the map's start (the governor's frames at each step, the overlap's: frames, overlapped, serial, falls back, clock held) */
	unsigned long governor_at_start[WIIU_MAP_SUMMARY_STEPS];
	unsigned long overlap_at_start[5];
	int active; /* a map has begun and not ended */
	int skip_first; /* the first frame after a begin carries the map's load (the time since the last present), not the map's: it is not counted */
};

void wiiu_map_summary_reset(struct wiiu_map_summary *summary, char const *label, unsigned long const governor[WIIU_MAP_SUMMARY_STEPS], unsigned long const overlap[5]);
void wiiu_map_summary_add_frame(struct wiiu_map_summary *summary, double frame_ms);
/* The text, in lines of under 190 characters: the first line, the second and (if the governor ran) the third. Returns the number of lines, 0 if the map had no frames.
governor and overlap are the counters now. */
int wiiu_map_summary_format(struct wiiu_map_summary const *summary, unsigned long const governor[WIIU_MAP_SUMMARY_STEPS], unsigned long const overlap[5],
	char lines[3][200]);
/* the frame time 99 of 100 frames are under, ms (0 if none) */
double wiiu_map_summary_p99_ms(struct wiiu_map_summary const *summary);

/* The game's: a map begins (label is its name and the decision), a frame was presented (the time since the frame before), the map ends (the note is written
and the summary is empty again). Game thread only. */
void wiiu_map_summary_begin(char const *label);
void wiiu_map_summary_frame(double frame_ms);
void wiiu_map_summary_end(void);

#ifdef __cplusplus
}
#endif

#endif
