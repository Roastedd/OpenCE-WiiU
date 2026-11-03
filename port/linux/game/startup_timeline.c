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

#include "cseries.h"
#include "halo_startup.h"

#ifdef HALO_WIIU

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void platform_log(char const *format, ...);
unsigned long wiiu_shell_ms(void);
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
	const char *name;
	unsigned long ms;
};

struct work
{
	const char *name;
	unsigned long long ticks;
	unsigned long calls;
	unsigned long long bytes;
};

volatile int startup_timeline_finished;

static struct
{
	volatile int lock;
	struct mark marks[MARK_LIMIT];
	int mark_count;
	struct mark notes[NOTE_LIMIT];
	int note_count;
	struct work works[WORK_LIMIT];
	int work_count;
	int reported;
	int live;
	unsigned long frames;
	unsigned long frames_at_live;
} t;

static void lock(void)
{
	while (__sync_lock_test_and_set(&t.lock, 1))
		;
}

static void unlock(void)
{
	__sync_lock_release(&t.lock);
}

int startup_timeline_running(void)
{
	return !__atomic_load_n(&t.reported, __ATOMIC_RELAXED);
}

void startup_timeline_mark(const char *name)
{
	if (!startup_timeline_running())
		return;
	lock();
	if (t.mark_count < MARK_LIMIT)
	{
		t.marks[t.mark_count].name = name;
		t.marks[t.mark_count].ms = wiiu_shell_ms();
		t.mark_count++;
	}
	/* (the point the report waits for) */
	if (!t.live && !strcmp(name, "menu_live"))
	{
		t.live = 1;
		t.frames_at_live = t.frames;
	}
	unlock();
}

void startup_timeline_note(const char *name)
{
	if (!startup_timeline_running())
		return;
	lock();
	if (t.note_count < NOTE_LIMIT)
	{
		t.notes[t.note_count].name = name;
		t.notes[t.note_count].ms = wiiu_shell_ms();
		t.note_count++;
	}
	unlock();
}

long long startup_timeline_now(void)
{
	return startup_timeline_running() ? (long long)wiiu_now_ticks() : 0;
}

void startup_timeline_add(const char *name, long long started, unsigned long items, unsigned long bytes)
{
	unsigned long long spent;
	int index;

	if (!started || !startup_timeline_running())
		return;
	spent = wiiu_now_ticks() - (unsigned long long)started;
	lock();
	for (index = 0; index < t.work_count; index++)
	{
		if (t.works[index].name == name || !strcmp(t.works[index].name, name))
			break;
	}
	if (index == t.work_count && t.work_count < WORK_LIMIT)
	{
		t.works[index].name = name;
		t.work_count++;
	}
	if (index < t.work_count)
	{
		t.works[index].ticks += spent;
		t.works[index].calls++;
		t.works[index].bytes += bytes;
		(void)items;
	}
	unlock();
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
	int mark_count, note_count, work_count, index;
	unsigned long live_ms = 0, previous = 0;
	struct lines lines;

	lock();
	if (t.reported)
	{
		unlock();
		return;
	}
	t.reported = 1;
	startup_timeline_finished = 1;
	memcpy(marks, t.marks, sizeof(marks));
	memcpy(notes, t.notes, sizeof(notes));
	memcpy(works, t.works, sizeof(works));
	mark_count = t.mark_count;
	note_count = t.note_count;
	work_count = t.work_count;
	unlock();
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
			const unsigned long long centi = per_second ? works[index].ticks * 100000u / per_second : 0u;

			lines_add(&lines, "%s %lux %llu.%02llu ms %llu KB", works[index].name, works[index].calls, centi / 100u, centi % 100u,
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
