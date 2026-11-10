/*
FILE_COUNTS.C

The report of what the file layer does for the menus (halo_file_counts.h): for each menu frame, the card calls the game thread made (directories
opened and entries read to resolve a path's spelling, stats, opens, directories listed), and which menu function made them. Written for the Wii U's
menu lag of 2026-11-09 (reports/research/2026-11-09/menulag-result.md): the console's profile showed the menu thread in the card's directory and
stat calls and could not say which screen's code called them.

Off unless the card's halo/filecount.txt holds a number other than 0 (the host's runs: HALO_FILECOUNT); the number is the least card calls in a frame that
make it worth a line (1 says every frame that touched the card); 1001 is the same with the first 400 paths the game thread translates said
("files: path d:\\maps\\ui.map"), to see which files a menu function asks for. The lines, in the log:

	files: menu frame 4210: 187 card calls (46 paths, 91 directories opened, 3011 entries read, 46 stats, 4 opens, 0 listed), 213 ms: gt select list update 187 calls 211 ms
	files: 300 menu frames: 300 touched the card, 51200 card calls, worst frame 4210 (187 calls, 213 ms)

The first 40 such frames are said, then one in 60; every 300 frames a summary says how many frames touched the card. The counts are of the game thread's calls
(the thread that read the setting).
*/

#include "halo_file_counts.h"

#ifdef HALO_WIIU

#include <stdio.h>
#include <string.h>

void platform_log(char const *format, ...);
long wiiu_study_number(const char *name, long fallback);
unsigned long long wiiu_now_ticks(void);
unsigned long wiiu_ticks_per_second(void);

enum
{
	LABEL_LIMIT = 12,
	FRAMES_SAID = 40,
	FRAMES_BETWEEN_LINES = 60,
	FRAMES_PER_SUMMARY = 300,
	LINE_BYTES = 480,
};

struct label
{
	char const *name;
	long number;
	unsigned long calls;
	unsigned long long ticks;
};

static struct
{
	int read;
	int paths;
	long threshold;
	unsigned long before[NUMBER_OF_FILE_COUNTS];
	unsigned long long frame_started;
	struct label labels[LABEL_LIMIT];
	int label_count;
	unsigned long frames;
	unsigned long touched;
	unsigned long long calls;
	unsigned long said;
	unsigned long worst_frame, worst_calls;
	unsigned long long worst_ticks;
} t;

static unsigned long long milliseconds(unsigned long long ticks)
{
	unsigned long rate = wiiu_ticks_per_second();

	return rate ? ticks * 1000ull / rate : 0;
}

static void settings_read(void)
{
	t.read = 1;
	t.threshold = wiiu_study_number("filecount.txt", 0);
	/* (1000 and more: the same with the paths said, the number less 1000 the threshold; a negative number is no file, as wiiu_study_number has it) */
	if (t.threshold >= 1000)
	{
		t.threshold = t.threshold - 1000 > 0 ? t.threshold - 1000 : 1;
		t.paths = 1;
	}
	if (t.threshold > 0)
	{
		file_counts_enable(t.paths ? 2 : 1);
		file_counts_read(t.before);
		t.frame_started = wiiu_now_ticks();
		platform_log("files: halo/filecount.txt says %ld: the menu frames that make %ld or more card calls are said", t.threshold, t.threshold);
	}
}

void file_counts_begin(unsigned long begin[NUMBER_OF_FILE_COUNTS], long long *started)
{
	file_counts_read(begin);
	*started = (long long)wiiu_now_ticks();
}

void file_counts_end(char const *label, long number, unsigned long const begin[NUMBER_OF_FILE_COUNTS], long long started)
{
	unsigned long now[NUMBER_OF_FILE_COUNTS];
	unsigned long calls;
	unsigned long long ticks = wiiu_now_ticks() - (unsigned long long)started;
	int index;

	file_counts_read(now);
	calls = file_counts_card_calls(now) - file_counts_card_calls(begin);
	if (!calls)
		return;
	for (index = 0; index < t.label_count; index++)
	{
		if (t.labels[index].name == label && t.labels[index].number == number)
			break;
	}
	if (index == t.label_count)
	{
		if (t.label_count == LABEL_LIMIT)
			return;
		t.labels[index].name = label;
		t.labels[index].number = number;
		t.labels[index].calls = 0;
		t.labels[index].ticks = 0;
		t.label_count++;
	}
	t.labels[index].calls += calls;
	t.labels[index].ticks += ticks;
}

void file_counts_frame(void)
{
	unsigned long now[NUMBER_OF_FILE_COUNTS], delta[NUMBER_OF_FILE_COUNTS];
	unsigned long long ticks;
	unsigned long calls;
	int index;

	if (!t.read)
		settings_read();
	if (t.threshold <= 0)
		return;
	file_counts_read(now);
	for (index = 0; index < NUMBER_OF_FILE_COUNTS; index++)
		delta[index] = now[index] - t.before[index];
	calls = file_counts_card_calls(delta);
	ticks = wiiu_now_ticks() - t.frame_started;
	t.frames++;
	if (calls)
		t.touched++;
	t.calls += calls;
	if (calls > t.worst_calls)
	{
		t.worst_calls = calls;
		t.worst_frame = t.frames;
		t.worst_ticks = ticks;
	}
	if (calls && (long)calls >= t.threshold && (t.said < FRAMES_SAID || (t.touched % FRAMES_BETWEEN_LINES) == 0))
	{
		char line[LINE_BYTES];
		int length = snprintf(line, sizeof(line), "files: menu frame %lu: %lu card calls (%lu paths, %lu directories opened, %lu entries read, %lu stats, %lu opens, %lu listed, %lu answered from the cache), %llu ms",
			t.frames, calls, delta[FILE_COUNT_TRANSLATE], delta[FILE_COUNT_SCAN], delta[FILE_COUNT_SCAN_ENTRY] + delta[FILE_COUNT_FIND_ENTRY], delta[FILE_COUNT_STAT],
			delta[FILE_COUNT_OPEN], delta[FILE_COUNT_FIND], delta[FILE_COUNT_CACHE_HIT], milliseconds(ticks));

		for (index = 0; index < t.label_count && length > 0 && length < LINE_BYTES - 80; index++)
			length += snprintf(line + length, sizeof(line) - (size_t)length, "%s %s %lu calls %llu ms", index ? ";" : ":", t.labels[index].name, t.labels[index].calls,
				milliseconds(t.labels[index].ticks));
		platform_log("%s", line);
		t.said++;
	}
	if (t.frames % FRAMES_PER_SUMMARY == 0)
	{
		platform_log("files: %lu menu frames: %lu touched the card, %llu card calls, worst frame %lu (%lu calls, %llu ms)", t.frames, t.touched, t.calls, t.worst_frame, t.worst_calls,
			milliseconds(t.worst_ticks));
		t.frames = t.touched = 0;
		t.calls = 0;
		t.worst_calls = 0;
		t.worst_ticks = 0;
	}
	memcpy(t.before, now, sizeof(t.before));
	t.label_count = 0;
	t.frame_started = wiiu_now_ticks();
}

#else

typedef int file_counts_has_nothing_here;

#endif
