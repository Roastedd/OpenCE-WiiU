/*
HALO_FILE_COUNTS.H

What the file layer does for the menus, counted: the calls the Xbox file API (port/linux/src/xbox_files.c) turns into the card's (a path's
translation, the directory scans it makes, the entries read, the stats, the opens), and the report that names the menu functions they came
from (port/linux/game/file_counts.c). It is off unless the card's halo/filecount.txt holds a number other than 0 (the host's runs:
HALO_FILECOUNT; a negative number also logs the first paths the game thread translates); with it off each count is one test of a flag and the report macros are one test of another.

The counts are of the game thread's calls only (the thread that turns the counting on): the other threads' file work is not the menu's.
*/

#ifndef HALO_FILE_COUNTS_H
#define HALO_FILE_COUNTS_H

enum
{
	/* a path translated (platform_translate_path), the components it resolved, and what resolving them cost */
	FILE_COUNT_TRANSLATE,
	FILE_COUNT_COMPONENT,
	/* a directory read for the one name a component is (a directory opened and entries read), the entries read, and a component answered from the
	directory cache (no card access) */
	FILE_COUNT_SCAN,
	FILE_COUNT_SCAN_ENTRY,
	FILE_COUNT_CACHE_HIT,
	/* stat calls (attributes and sizes, the exact spelling's check, a listed entry's) */
	FILE_COUNT_STAT,
	/* files opened by CreateFile */
	FILE_COUNT_OPEN,
	/* directories listed with FindFirstFile, and the entries read from them */
	FILE_COUNT_FIND,
	FILE_COUNT_FIND_ENTRY,
	NUMBER_OF_FILE_COUNTS
};

/* nonzero while the game thread's calls are counted (2: and the first paths it translates are logged) */
extern volatile int file_counting;
void file_count_add(int kind, unsigned long amount);
/* the counts so far (the sum of each kind) */
void file_counts_read(unsigned long counts[NUMBER_OF_FILE_COUNTS]);
/* turns the counting on for the calling thread (1, or 2 to log the paths too), or off (0) */
void file_counts_enable(int on);
/* the calls that reach the card: directories opened, entries read, stats and opens */
unsigned long file_counts_card_calls(unsigned long const counts[NUMBER_OF_FILE_COUNTS]);

#define FILE_COUNT(kind, amount) do { if (file_counting) file_count_add((kind), (unsigned long)(amount)); } while (0)

#ifdef HALO_WIIU

/* once a menu frame (process_ui_widgets): the report of the frame before */
void file_counts_frame(void);
/* around a menu function: its label, and the counts and the time it began with */
void file_counts_begin(unsigned long begin[NUMBER_OF_FILE_COUNTS], long long *started);
void file_counts_end(char const *label, long number, unsigned long const begin[NUMBER_OF_FILE_COUNTS], long long started);

#define FILE_COUNTS_FRAME() file_counts_frame()
#define FILE_COUNTS_SCOPE_BEGIN() unsigned long file_counts_start[NUMBER_OF_FILE_COUNTS]; long long file_counts_time = 0; if (file_counting) file_counts_begin(file_counts_start, &file_counts_time)
#define FILE_COUNTS_SCOPE_END(label, number) do { if (file_counting) file_counts_end((label), (number), file_counts_start, file_counts_time); } while (0)

#else

#define FILE_COUNTS_FRAME() ((void)0)
#define FILE_COUNTS_SCOPE_BEGIN() ((void)0)
#define FILE_COUNTS_SCOPE_END(label, number) ((void)(label), (void)(number))

#endif

#endif
