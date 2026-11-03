/*
STARTUP_TIMELINE.C

The Wii U's start-up timeline (halo_startup.h). The game thread's start-up passes marks (STARTUP_MARK) in order; other threads note events
(STARTUP_NOTE: the saved games' check ending, the shader pack read); pieces of work are added up under a name (STARTUP_ADD: the PNGs decoded,
the files read, the shaders made). Once the main menu is live and has been presented a few times, the log says it all:

	startup: 8312 ms to a live menu; boot 1090, ...      (each segment: the mark that ends it, and the milliseconds since the mark before)
	startup: also: fs_end 7021, ...                      (the other threads' events: milliseconds since the start)
	startup: work: png 14x 410 ms 3200 KB, ...           (what was added up: the calls, the milliseconds, the kilobytes)

The times are the log's own (the shell's clock): a mark is the log line's time. The marks' cost is a read of the clock and a store; the report is
made once, then the marks and timers do nothing.
*/

#include "halo_startup.h"

#ifdef HALO_WIIU

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void platform_log(char const *format, ...);
unsigned long wiiu_shell_ms(void);
long wiiu_study_number(const char *name, long fallback);
unsigned long long wiiu_now_ticks(void);
unsigned long wiiu_ticks_per_second(void);

enum
{
	MARK_LIMIT = 48,
	NOTE_LIMIT = 24,
	WORK_LIMIT = 24,
	/* the frames after the menu is live that are waited for, so that the report has its first presented frames */
	FRAMES_AFTER_LIVE = 3,
	/* a run that never gets a menu live (a map from init.txt) says what it has after this many frames */
	FRAMES_AT_MOST = 1500,
	LINE_BYTES = 200,
};

struct mark
{
	const char *volatile name; /* (written last: a slot with no name is not yet written) */
	unsigned long ms;
};

struct work
{
	const char *volatile name;
	unsigned long ticks;
	unsigned long calls;
	unsigned long bytes;
};

volatile int startup_timeline_finished;

/* No lock: the marks are made by any thread, the GX2 thread's and the helpers' among them, and a lock taken by a thread that a higher one then
preempts, on a core where that one spins for it, would hold the start-up for good. A slot is taken with an atomic add (or, for a kind of work, by
compare and swap on its name), its numbers are added atomically, and the report reads what has been published. The ticks are summed as 32-bit words: 68
seconds of the time base, more than the start-up is. */
static struct
{
	struct mark marks[MARK_LIMIT];
	unsigned long mark_count;
	struct mark notes[NOTE_LIMIT];
	unsigned long note_count;
	struct work works[WORK_LIMIT];
	int reported;
	int live;
	unsigned long frames;
	unsigned long frames_at_live;
} t;

/* halo/startup.txt: a number whose bits switch the start-up changes (halo_startup.h); none, or no file: all of them */
int startup_option(unsigned long bit)
{
	static int read;
	static unsigned long options;

	if (!__atomic_load_n(&read, __ATOMIC_ACQUIRE))
	{
		options = (unsigned long)wiiu_study_number("startup.txt", -1L);
		if (options != (unsigned long)-1L)
			platform_log("startup: halo/startup.txt says %lu: the changes it leaves out are off", options);
		__atomic_store_n(&read, 1, __ATOMIC_RELEASE);
	}
	return (options & bit) != 0;
}

int startup_timeline_running(void)
{
	return !__atomic_load_n(&t.reported, __ATOMIC_RELAXED);
}

void startup_timeline_mark(const char *name)
{
	unsigned long index;

	if (!startup_timeline_running())
		return;
	index = __atomic_fetch_add(&t.mark_count, 1, __ATOMIC_RELAXED);
	if (index < MARK_LIMIT)
	{
		t.marks[index].ms = wiiu_shell_ms();
		__atomic_store_n(&t.marks[index].name, name, __ATOMIC_RELEASE);
	}
	/* (the point the report waits for; the game thread's) */
	if (!t.live && !strcmp(name, "menu_live"))
	{
		t.frames_at_live = t.frames;
		t.live = 1;
	}
}

void startup_timeline_note(const char *name)
{
	unsigned long index;

	if (!startup_timeline_running())
		return;
	index = __atomic_fetch_add(&t.note_count, 1, __ATOMIC_RELAXED);
	if (index < NOTE_LIMIT)
	{
		t.notes[index].ms = wiiu_shell_ms();
		__atomic_store_n(&t.notes[index].name, name, __ATOMIC_RELEASE);
	}
}

long long startup_timeline_now(void)
{
	return startup_timeline_running() ? (long long)wiiu_now_ticks() : 0;
}

void startup_timeline_add(const char *name, long long started, unsigned long items, unsigned long bytes)
{
	unsigned long spent;
	int index;

	(void)items;
	if (!started || !startup_timeline_running())
		return;
	spent = (unsigned long)(wiiu_now_ticks() - (unsigned long long)started);
	for (index = 0; index < WORK_LIMIT; index++)
	{
		const char *found = __atomic_load_n(&t.works[index].name, __ATOMIC_ACQUIRE);

		if (!found)
		{
			const char *expected = NULL;

			if (__atomic_compare_exchange_n(&t.works[index].name, &expected, name, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
				found = name;
			else
				found = expected;
		}
		if (found == name || !strcmp(found, name))
		{
			__atomic_fetch_add(&t.works[index].ticks, spent, __ATOMIC_RELAXED);
			__atomic_fetch_add(&t.works[index].calls, 1, __ATOMIC_RELAXED);
			__atomic_fetch_add(&t.works[index].bytes, bytes, __ATOMIC_RELAXED);
			return;
		}
	}
}

/* pieces of a line, split into log lines of about LINE_BYTES at the pieces' ends (the log's lines are 240 bytes) */
struct lines
{
	char text[LINE_BYTES + 100];
	char prefix[64];
	size_t used;
	int pieces;
};

/* the line so far said, and a new one begun with this beginning */
static void lines_begin(struct lines *lines, const char *prefix)
{
	if (lines->pieces)
		platform_log("%s", lines->text);
	snprintf(lines->prefix, sizeof(lines->prefix), "%s", prefix);
	snprintf(lines->text, sizeof(lines->text), "%s", prefix);
	lines->used = strlen(lines->text);
	lines->pieces = 0;
}

static void lines_add(struct lines *lines, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void lines_add(struct lines *lines, const char *format, ...)
{
	char piece[96], prefix[64];
	va_list arguments;
	size_t length;

	va_start(arguments, format);
	vsnprintf(piece, sizeof(piece), format, arguments);
	va_end(arguments);
	length = strlen(piece);
	if (lines->used + length + 2 > LINE_BYTES)
	{
		/* (the next line begins as this one did) */
		memcpy(prefix, lines->prefix, sizeof(prefix));
		lines_begin(lines, prefix);
	}
	snprintf(lines->text + lines->used, sizeof(lines->text) - lines->used, "%s%s", lines->pieces ? ", " : " ", piece);
	lines->used = strlen(lines->text);
	lines->pieces++;
}

static void report(void)
{
	struct mark marks[MARK_LIMIT], notes[NOTE_LIMIT];
	struct work works[WORK_LIMIT];
	int mark_count = 0, note_count = 0, work_count = 0, index;
	unsigned long live_ms = 0, previous = 0;
	struct lines lines;

	if (__atomic_exchange_n(&t.reported, 1, __ATOMIC_ACQ_REL))
		return;
	startup_timeline_finished = 1;
	/* (what other threads are in the middle of writing is not published: its name is not yet there) */
	for (index = 0; index < MARK_LIMIT && (unsigned long)index < __atomic_load_n(&t.mark_count, __ATOMIC_RELAXED); index++)
	{
		marks[mark_count].name = __atomic_load_n(&t.marks[index].name, __ATOMIC_ACQUIRE);
		marks[mark_count].ms = t.marks[index].ms;
		mark_count += marks[mark_count].name != NULL;
	}
	for (index = 0; index < NOTE_LIMIT && (unsigned long)index < __atomic_load_n(&t.note_count, __ATOMIC_RELAXED); index++)
	{
		notes[note_count].name = __atomic_load_n(&t.notes[index].name, __ATOMIC_ACQUIRE);
		notes[note_count].ms = t.notes[index].ms;
		note_count += notes[note_count].name != NULL;
	}
	for (index = 0; index < WORK_LIMIT; index++)
	{
		works[work_count].name = __atomic_load_n(&t.works[index].name, __ATOMIC_ACQUIRE);
		works[work_count].ticks = __atomic_load_n(&t.works[index].ticks, __ATOMIC_RELAXED);
		works[work_count].calls = __atomic_load_n(&t.works[index].calls, __ATOMIC_RELAXED);
		works[work_count].bytes = __atomic_load_n(&t.works[index].bytes, __ATOMIC_RELAXED);
		work_count += works[work_count].name != NULL;
	}
	for (index = 0; index < mark_count; index++)
	{
		if (!strcmp(marks[index].name, "menu_live"))
			live_ms = marks[index].ms;
	}
	memset(&lines, 0, sizeof(lines));
	lines_begin(&lines, "startup:");
	if (live_ms)
		lines_add(&lines, "%lu ms to a live menu (segments: the mark that ends each, ms since the one before)", live_ms);
	else
		lines_add(&lines, "no menu was live (the marks so far)");
	for (index = 0; index < mark_count; index++)
	{
		lines_add(&lines, "%s %lu", marks[index].name, marks[index].ms - previous);
		previous = marks[index].ms;
	}
	if (note_count)
	{
		lines_begin(&lines, "startup: events (ms since the start):");
		for (index = 0; index < note_count; index++)
			lines_add(&lines, "%s %lu", notes[index].name, notes[index].ms);
	}
	if (work_count)
	{
		const unsigned long long per_second = wiiu_ticks_per_second();

		lines_begin(&lines, "startup: work (calls, ms, KB):");
		for (index = 0; index < work_count; index++)
		{
			const unsigned long long centi = per_second ? (unsigned long long)works[index].ticks * 100000u / per_second : 0u;

			lines_add(&lines, "%s %lux %llu.%02llu ms %lu KB", works[index].name, works[index].calls, centi / 100u, centi % 100u,
				works[index].bytes / 1024u);
		}
	}
	lines_begin(&lines, "");
}

/* once a frame, where the game presents it */
void startup_timeline_frame(void)
{
	if (!startup_timeline_running())
		return;
	t.frames++;
	if ((t.live && t.frames >= t.frames_at_live + FRAMES_AFTER_LIVE) || t.frames >= FRAMES_AT_MOST)
		report();
}

#else

typedef int startup_timeline_not_for_this_build;

#endif
