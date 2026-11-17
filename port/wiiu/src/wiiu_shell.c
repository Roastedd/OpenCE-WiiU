/*
WIIU_SHELL.C

The shell: what runs around the game on a Wii U (wiiu_shell.h says what it is for). The Cafe OS half; the decisions and the
files are in wiiu_shell_watch.c, wiiu_shell_log.c and wiiu_shell_blackbox.c, which a desktop tests.

The threads:

 - main: the system's own. It answers ProcUI (WHBProcIsRunning: HOME, closing) every twenty milliseconds, and nothing else; it
   is the thread the system watches.
 - game: the game's main, on core 1 with a stack of 8 MiB (the size of the Linux builds' main thread, which the game has run on).
 - watchdog: asks the watch every 50 ms whether the game has stalled or the close deadline has passed, and says so; at the
   deadline it ends the process. It does no input or output but the log queue, so that what blocks the others cannot block it,
   and runs at a higher priority than they, so that it runs while one of them spins.
 - black box: a record of where every thread is, every five seconds, to the card.
 - log: writes the log queue to the card.

CoffeeFlix and CoffeeUnlocked have this lifecycle on this console (the ProcUI callbacks at those priorities, the ten seconds to
close, the black box); this is that, in C, for the game. Not run on a console.
*/

#include "wiiu_shell.h"

#include "wiiu_crash.h"
#include "wiiu_debug.h"
#include "wiiu_shell_blackbox.h"
#include "wiiu_shell_log.h"
#include "wiiu_missing_keys.h"
#include "wiiu_shell_watch.h"
#include "wiiu_update.h"
#include "wiiu_map_summary.h"
#include "../../linux/game/smooth_defaults.h" /* the cards and the map (smooth_defaults.c): whether the ticks run beside the drawing */

#include <coreinit/core.h>
#include <sysapp/launch.h>
#include <coreinit/debug.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/memexpheap.h>
#include <coreinit/memheap.h>
#include <coreinit/mutex.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <gx2/event.h>
#include <proc_ui/procui.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <whb/proc.h>

#define GAME_STACK_BYTES (8u * 1024u * 1024u)
#define MONITOR_STACK_BYTES (128u * 1024u)
/* (one second: a freeze of the whole console that the five second record cannot place; the record is a few lines) */
#define BLACKBOX_PERIOD_MS 1000u
#define HEARTBEAT_PERIOD_MS 5000u
/* ProcUI runs the callbacks of the higher priority first; ours runs before SDL's (100) and the like */
#define RELEASE_PRIORITY 2000
#define ACQUIRE_PRIORITY 50
#define ACQUIRE_LATE_PRIORITY 3000 /* (acquire callbacks run lowest first, the reverse of release's: libwhb's, at 100, makes its heap and buffers before this) */
#define EXIT_PRIORITY 50
#define EXIT_TRAIL_FILE WIIU_DATA_DIRECTORY "/exit-trail.txt"
#define EXIT_TRAIL_PREVIOUS_FILE WIIU_DATA_DIRECTORY "/exit-trail-previous.txt"
#define LOG_FILE WIIU_DATA_DIRECTORY "/log.txt"
#define LOG_PREVIOUS_FILE WIIU_DATA_DIRECTORY "/log-previous.txt"
#define BLACKBOX_FILE WIIU_DATA_DIRECTORY "/blackbox.txt"
#define BLACKBOX_PREVIOUS_FILE WIIU_DATA_DIRECTORY "/blackbox-previous.txt"
#define CRASH_FILE WIIU_DATA_DIRECTORY "/crash.txt"
#define CRASH_PREVIOUS_FILE WIIU_DATA_DIRECTORY "/crash-previous.txt"

/* Everything is static and never destroyed: a thread may still be using it as the process ends. */
static wiiu_shell_log shell_log;
static wiiu_shell_watch shell_watch;
static wiiu_shell_blackbox shell_box;
static uint32_t base_ms;
static int32_t main_priority = 16;
static volatile uint32_t started;
static volatile uint32_t exit_requested;
static volatile uint32_t started_game;
/* (wiiu_audio.c's: the AX voices given up in the background and made again) */
void wiiu_audio_suspend(void);
void wiiu_audio_resume(void);
/* (wiiu_voice_audio.c's: the microphone probe at the start; the microphone closed and the voice thread asleep in the background, at most
100 ms waited for; woken again; and the thread ended at the close: 1 if it ended in the time given, 0 if not, 2 if there was none) */
void wiiu_voice_boot(void);
void wiiu_voice_suspend(void);
void wiiu_voice_resume(void);
int wiiu_voice_stop(uint32_t timeout_ms);
static volatile uint32_t foreground = 1;
static volatile uint32_t network_stopped;
static volatile uint32_t game_done;
static volatile uint32_t box_stopping;
static volatile uint32_t box_busy;
static volatile uint32_t box_wanted;
static int game_result;
static int (*game_main_function)(void);
static OSThread game_thread;
static uint8_t *game_stack;
static char last_beat[240];

/* ---------- time and threads */

static uint32_t system_ms(void)
{
	return (uint32_t)OSTicksToMilliseconds(OSGetSystemTime());
}

/* milliseconds since the shell started: the log's, the watch's and the black box's clock */
static uint32_t shell_ms(void)
{
	return system_ms() - base_ms;
}

/* one line into exit-trail.txt, written and closed at once (the steps of a close, for after a hang) */
static void exit_note(const char *text)
{
	FILE *file = fopen(EXIT_TRAIL_FILE, "a");

	if (file)
	{
		fprintf(file, "%u ms: %s\n", (unsigned)shell_ms(), text);
		fclose(file);
	}
}

static void exit_note_last(void)
{
	exit_note("exit: the last registered handler");
}

static void sleep_ms(uint32_t milliseconds)
{
	OSSleepTicks(OSMillisecondsToTicks(milliseconds));
}

/* is p somewhere a pointer in another thread's fields can point? (a stale or garbled one must not fault the reader) */
static int in_app_memory(const void *p, uintptr_t from)
{
	return (uintptr_t)p >= from && (uintptr_t)p < 0x50000000u;
}

static size_t thread_where(void *handle, char *out, size_t capacity)
{
	OSThread *thread = handle;
	char state;
	size_t used;

	if (!in_app_memory(thread, 0x10000000u) || capacity < 2)
		return 0;
	state = thread->suspendCounter > 0                  ? 'S'
	        : (thread->state & OS_THREAD_STATE_RUNNING) ? 'R'
	        : (thread->state & OS_THREAD_STATE_WAITING) ? 'W'
	        : (thread->state & OS_THREAD_STATE_READY)   ? 'r'
	                                                    : '?';
	used = (size_t)snprintf(out, capacity, "%c pc=%08x lr=%08x", state, (unsigned)thread->context.srr0,
		(unsigned)thread->context.lr);
	if (used >= capacity)
		used = capacity - 1;
	if (in_app_memory(thread->mutex, 0x10000000u))
	{
		const OSMutex *lock = thread->mutex;
		OSThread *owner = lock->owner;
		const char *name = in_app_memory(owner, 0x10000000u) ? OSGetThreadName(owner) : NULL;

		if (!in_app_memory(name, 0x01000000u))
			name = NULL;
		used += (size_t)snprintf(out + used, capacity - used, " waits for lock %08x held by %.16s (%08x)", (unsigned)(uintptr_t)lock,
			owner ? (name ? name : "?") : "nobody", (unsigned)(uintptr_t)owner);
	}
	else if (in_app_memory(thread->queue, 0x10000000u))
	{
		used += (size_t)snprintf(out + used, capacity - used, " sleeps on queue %08x", (unsigned)(uintptr_t)thread->queue);
	}
	return used < capacity ? used : capacity - 1;
}

/* The calling thread: its name for the debugger and the black box, and its place among the others: the watchdog above the game
(it has to run while something else spins or waits), the workers below it. 0 is the highest priority, 31 the lowest. */
static void setup_thread(const char *name)
{
	OSThread *self = OSGetCurrentThread();

	OSSetThreadName(self, name);
	/* (the workers on core 0: core 1 is the game thread's, core 2 the GX2 thread's; a pthread may otherwise run on any) */
	if (strcmp(name, "game") && strcmp(name, "main"))
		OSSetThreadAffinity(self, OS_THREAD_ATTRIB_AFFINITY_CPU0);
	if (!strcmp(name, "watchdog"))
		OSSetThreadPriority(self, main_priority > 4 ? main_priority - 4 : 0);
	else if (strcmp(name, "game") && strcmp(name, "main"))
		OSSetThreadPriority(self, main_priority < 27 ? main_priority + 4 : 31);
	wiiu_tag_thread(name);
}

/* ---------- the shell's calls for the rest of the port */

static void echo_to_system(const char *line)
{
	OSReport("%s\n", line);
	wiiu_debug_log_line(line);
}

/* The game's debug.txt (errors.c, write_to_debug_file): its lines go through a log of their own, written by its own thread, so that the game thread never waits for
the card for them; lines from before the log is up are lost (none are written that early) */
#define ERROR_FILE WIIU_DATA_DIRECTORY "/data/debug.txt"
#define ERROR_PREVIOUS_FILE WIIU_DATA_DIRECTORY "/data/debug-previous.txt"
/* A file bigger than this at the start is kept as debug-previous.txt (the one before that let go) and begun again: it held 143,949 lines, 6.2 MB, from 618 runs
by 2026-11-06 (109,661 of them one message, accept_endpoint() returned NULL), every run appending to it. A smaller one goes on, so that a crash's lines survive a
restart or two. */
#define ERROR_FILE_ROLL_BYTES (256 * 1024)
/* The same line, or one of the last eight, written less than this long ago is counted, not written, and said once with its count (wiiu_shell_log.h collapse_ms):
the host's blank notices (64 lines a second for ten seconds, twice a vote), the listening socket's refusals (five a second while a game is hosted) */
#define ERROR_FILE_COLLAPSE_MS 10000u
static wiiu_shell_log error_log;
static pthread_once_t error_log_once = PTHREAD_ONCE_INIT;
static volatile int error_log_started;

static void error_log_start(void)
{
	wiiu_shell_log_config config;

	memset(&config, 0, sizeof(config));
	config.path = ERROR_FILE;
	config.now_ms = shell_ms;
	config.thread_setup = setup_thread;
	config.collapse_ms = ERROR_FILE_COLLAPSE_MS;
	if (wiiu_shell_log_start(&error_log, &config) == 0)
		error_log_started = 1;
}

void wiiu_error_file_text(const char *text);

void wiiu_error_file_text(const char *text)
{
	char line[WIIU_LOG_LINE_BYTES];

	pthread_once(&error_log_once, error_log_start);
	if (!error_log_started || !text)
		return;
	/* (a line at a time, without its \r\n) */
	while (*text)
	{
		size_t length = strcspn(text, "\r\n");

		if (length)
		{
			if (length >= sizeof(line))
				length = sizeof(line) - 1;
			memcpy(line, text, length);
			line[length] = 0;
			wiiu_shell_log_text(&error_log, line);
		}
		text += strcspn(text, "\r\n");
		text += strspn(text, "\r\n");
	}
}

void wiiu_log_text(const char *text)
{
	if (started)
		wiiu_shell_log_text(&shell_log, text);
	else
		OSReport("%s\n", text);
}

void wiiu_log(const char *format, ...)
{
	char text[WIIU_LOG_LINE_BYTES];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	wiiu_log_text(text);
}

void wiiu_phase(const char *phase)
{
	wiiu_shell_watch_phase(&shell_watch, phase, shell_ms());
}

void wiiu_frame(void)
{
	wiiu_shell_watch_frame(&shell_watch);
}

int wiiu_exit_requested(void)
{
	return exit_requested != 0;
}

int wiiu_in_foreground(void)
{
	return foreground != 0;
}

int wiiu_network_allowed(void)
{
	return !network_stopped && foreground && !exit_requested;
}

/* Nothing of the game's may use the GPU, the controllers or the sound while the app is in the background (the system takes them: a call into GX2
then ends in a fault in the system's code, and the console with it). The threads that do, the game's and the audio mixer's, call this at their
safe points; it returns at once in the foreground, and in the background holds the caller until the foreground is back (or the app is closing). The
release callback waits for the game thread to be held before it lets the system have the GPU. */
static volatile uint32_t game_gate_held;
/* the tick thread (source/main/main.c, halo/tick_thread.txt) held at its own gate, before it starts a tick. While it runs ticks the game thread
waits for it (nothing runs at the same time yet), so either being held is the game being held: the game thread cannot be at its gate then, and
the tick thread is not in a GX2 call (the ticks make none). When the ticks run beside the drawing this becomes both, in game_threads_held. */
static volatile uint32_t tick_gate_held;

/* The tick thread beside the drawing (the tick overlap, port/linux/game/tick_overlap.h): both threads run game code, so the game is held when both are quiet.
The game thread is quiet at its gate, or waiting for the tick thread (which cannot be drawing then); the tick thread at its gate, or idle, waiting for a job
the game thread has not given it. */
static volatile uint32_t tick_overlap_registered;
static volatile uint32_t tick_thread_idle;
static volatile uint32_t game_thread_waiting;

static volatile uint32_t game_halted;

static int game_threads_held(void)
{
	/* (a halt: the thread that halted is parked for good, and the other is held at its next gate) */
	if (game_halted)
		return 1;
	if (tick_overlap_registered)
		return (game_gate_held != 0 || game_thread_waiting != 0) && (tick_gate_held != 0 || tick_thread_idle != 0);
	return game_gate_held != 0 || tick_gate_held != 0;
}

void wiiu_foreground_gate(int is_game_thread);

/* The tick thread (the game's ticks on a thread of their own: port/linux/game/tick_owners.h) has a gate of its own. It touches neither
the GPU, the controllers nor the sound, so HOME does not need it held for the system's sake; but a tick thread that goes on in the background
would run the game on with nobody drawing it, and after the game has halted or the app is closing it must not run at all. It registers when it
starts (and unregisters when it ends), and calls wiiu_tick_gate at its safe points, between two ticks; the release callback and the close wait
for it as for the game thread's gate, and a halted game holds it for good. Nothing registers while the ticks run on the game thread. */
static volatile uint32_t tick_gate_registered;
static volatile uint32_t tick_gate_held;

void wiiu_tick_gate_register(int registered);
void wiiu_tick_gate(void);

/* called every few milliseconds by a thread held at the gate: the GX2 thread drains the GPU here when the release callback asks it to (wiiu_gx2.c) */
void (*wiiu_gate_poll)(void);

void wiiu_foreground_gate(int is_game_thread)
{
	if (foreground && !exit_requested)
		return;
	if (is_game_thread)
		game_gate_held = 1;
	/* (and once the app is closing, for good: the system takes the GPU and the process ends under it) */
	while (!foreground || exit_requested)
	{
		if (wiiu_gate_poll)
			wiiu_gate_poll();
		sleep_ms(5);
	}
	if (is_game_thread)
		game_gate_held = 0;
}

/* The tick thread's gate, called before each tick it is asked for: returns at once in the foreground, holds the thread in the background (and for
good once the app is closing), as wiiu_foreground_gate does the game thread. The release callback and the close wait for it as for the game thread. */
void wiiu_tick_thread_gate(void);

void wiiu_tick_thread_gate(void)
{
	if (foreground && !exit_requested && !game_halted)
		return;
	tick_gate_held = 1;
	while (!foreground || exit_requested || game_halted)
	{
		if (wiiu_gate_poll)
			wiiu_gate_poll();
		sleep_ms(5);
	}
	tick_gate_held = 0;
}

/* the tick owners' (port/linux/game/tick_owners.c) calls of the gate: the tick thread gates itself in main.c's loop (wiiu_tick_thread_gate above), so the
registration needs nothing here, and a gate asked for from the game thread returns at once in the foreground and holds in the background as the game
thread's own does */
void wiiu_tick_gate_register(int registered);
void wiiu_tick_gate(void);

void wiiu_tick_gate_register(int registered)
{
	tick_overlap_registered = registered != 0;
}

/* the tick thread waits for a job (1) or runs one (0), and the game thread waits for it to end (1) or does not (0) (tick_overlap.c) */
void wiiu_tick_idle_set(int idle);
void wiiu_game_waiting_set(int waiting);

void wiiu_tick_idle_set(int idle)
{
	tick_thread_idle = idle != 0;
}

void wiiu_game_waiting_set(int waiting)
{
	game_thread_waiting = waiting != 0;
}

/* the calling thread's stack, as the OS says it: the tick snapshot's view belongs to the game thread's, found by it (port/linux/game/tick_stack.h); and the
time base (tick_overlap.c's numbers) */
void wiiu_thread_stack_range(unsigned long *low, unsigned long *size);
unsigned long long wiiu_now_ticks(void);
unsigned long wiiu_ticks_per_second(void);

void wiiu_thread_stack_range(unsigned long *low, unsigned long *size)
{
	const OSThread *thread = OSGetCurrentThread();
	const unsigned long start = (unsigned long)(uintptr_t)thread->stackStart;
	const unsigned long end = (unsigned long)(uintptr_t)thread->stackEnd;

	*low = start < end ? start : end;
	*size = start < end ? end - start : start - end;
}

/* the log's clock in milliseconds since the shell started (port/linux/game/startup_timeline.c's marks are in it) */
unsigned long wiiu_shell_ms(void);

unsigned long wiiu_shell_ms(void)
{
	return (unsigned long)shell_ms();
}

/* the time base's ticks, as the OS counts them (a conversion to microseconds is a 64-bit division, which the blends' two reads an object would pay) */
unsigned long long wiiu_now_ticks(void)
{
	return (unsigned long long)OSGetSystemTime();
}

unsigned long wiiu_ticks_per_second(void)
{
	return (unsigned long)OSTimerClockSpeed;
}

void wiiu_tick_gate(void)
{
	wiiu_tick_thread_gate();
}

/* The game quits (its Quit, or a timed run that is over): the Wii U Menu is asked for, and the system ends the app through ProcUI, as it does for HOME's Close
software (the release and exit callbacks above run). Stopping the loop and returning from main left the console on the last picture with its network dead;
the debug server's quit, which asks this way, closed the app cleanly each time it was run. */
void wiiu_request_exit(void)
{
	static volatile uint32_t asked;

	if (asked)
		return;
	asked = 1;
	/* the map that was playing says how it went (wiiu_map_summary.h) */
	wiiu_map_summary_end();
	/* the missing shaders' records are written only outside gameplay (wiiu_missing_keys.c), and once the Menu is asked for the app is in
	the background, where the close does not write them: the quit, which is not gameplay, gives them up to five seconds first (64 records
	took several on the console's SD card: a scan of the folder, then a file per 8 records, each flushed). As "always", a gameplay flag the
	game raises again meanwhile no longer holds them back. With nothing pending (a complete shader pack) the quit does not wait at all. */
	{
		wiiu_missing_keys_counts counts;
		uint32_t waited = 0;

		wiiu_missing_keys_always(1);
		do
		{
			wiiu_missing_keys_counts_get(&counts);
			/* (all the accepted records are written, and nothing is pending: the first look came before the writer had taken the records the always flag
			releases, saw nothing pending and left after 0 ms, so a session's 13 missing keys were lost: 2026-11-04) */
			if ((counts.written >= counts.accepted && !counts.queue_pending && !counts.batch_records && counts.phase == 0) || counts.failed || waited >= 5000u)
				break;
			sleep_ms(5);
			waited += 5u;
		} while (1);
		if (counts.accepted)
			wiiu_log("Quit: shader capture written %u of %u after %u ms", (unsigned)counts.written, (unsigned)counts.accepted, (unsigned)waited);
	}
	wiiu_launch_menu();
}

void platform_request_quit(void)
{
	wiiu_request_exit();
}

/* for the threads the game and the port make with pthreads (the game's CreateThread: its file reads, input; the vertical blank): off the game thread's core and the GX2
thread's, on core 0. A running thread keeps its core until it is next scheduled (2026-11-02: the tick thread said core 2 right after this), so it
sleeps a moment here and is on core 0 when it returns. */
void wiiu_thread_off_game_core(void)
{
	OSSetThreadAffinity(OSGetCurrentThread(), OS_THREAD_ATTRIB_AFFINITY_CPU0);
	OSSleepTicks(OSMillisecondsToTicks(1));
}

/* The checkpoint's writer (port/linux/game/async_save.c): on core 0, named, and below the game thread's priority like the other workers (setup_thread): it waits for the card
most of its time, and what it has to do on a core is a piece of the file at a time. */
void wiiu_thread_background(const char *name);
void wiiu_thread_background(const char *name)
{
	OSThread *self = OSGetCurrentThread();

	wiiu_thread_off_game_core();
	OSSetThreadName(self, name);
	OSSetThreadPriority(self, main_priority < 27 ? main_priority + 4 : 31);
	wiiu_tag_thread(name);
}

/* The tick thread (port/linux/game/tick_overlap.c): an OSThread made on core 0, not a pthread. wut makes a pthread with OSCreateThread at priority 16 with
every core allowed (OS_THREAD_ATTRIB_AFFINITY_ANY) and resumes it at once; OSSetThreadAffinity from inside it did not keep it on core 0 (2026-11-02:
it said core 2, and core 1 after a sleep of 1 ms). Made with the affinity, as the game thread, the audio and the debug server are, it is on core 0 from
its first instruction. wut's mutexes, conditions and thread-specific values (OSFastMutex, OSFastCond, OSGetThreadSpecific), which the thread and
the game's code use, work in any OSThread. Returns 1 if the thread was made, -1 if it could not be (no stack, no thread). */
int wiiu_tick_thread_create(void *(*entry)(void *), unsigned long stack_bytes);

static OSThread tick_thread;
static uint8_t *tick_thread_stack;
static void *(*tick_thread_entry)(void *);
/* the core the tick thread is made on (halo/tick_core.txt: 0, 1 or 2; 0 if none), and the one it is on now (it is moved to the game thread's while the overlap is late) */
static unsigned tick_core;
static unsigned tick_core_now;
/* the GX2 thread's core (-1: none), the tick thread's priority offset below the game's when it shares that core (halo/tick_priority.txt), and the GX2 thread's sleep between looks then
(halo/gx2_idle_sleep.txt, microseconds): see wiiu_gx2.c's wiiu_gx2_idle_sleep_set */
static int gx2_core = -1;
static int tick_priority_offset = -1; /* -1: not set by the card (0: the game's priority) */
static long gx2_sleep_us = 0; /* 0: the GX2 thread waits for work yielding, as always */
static long tick_quantum_us = -1; /* -1: the thread's own run quantum is left as the system made it */
int wiiu_gx2_thread_core_planned(void);
void wiiu_gx2_idle_sleep_set(uint32_t microseconds);

/* a number from a file of the app's card folder, or the fallback (-1 for no file) */
static long card_number(char const *name, long fallback)
{
	char path[256];
	FILE *file;
	long value = fallback;

	snprintf(path, sizeof(path), WIIU_DATA_DIRECTORY "/%s", name);
	file = fopen(path, "r");
	if (file)
	{
		if (fscanf(file, "%ld", &value) != 1)
			value = fallback;
		fclose(file);
	}
	/* (a negative number, -1 by convention, is the same as no file: a harness that must not delete a card file writes -1 to put it back) */
	return value < 0 ? fallback : value;
}

/* the tick thread's priority (the game's, plus halo/tick_priority.txt), its run quantum (halo/tick_quantum.txt, microseconds, left alone if absent), and the GX2 thread's way of waiting for work when the
tick thread shares its core (halo/gx2_idle_sleep.txt: microseconds, 0 is the yield). The defaults are the plain core-2 setting of the console's first matrix (30 ticks a second, 14 ms a tick): the second
matrix (2026-11-03) found the priority one below with a sleep of 100 microseconds fixed the stream slot waits (0.06 ms a frame against 2.9) and slowed the tick (28 ms against 16), a loss. */
static void tick_core_settle(int log)
{
	const int shares = gx2_core >= 0 && (int)tick_core_now == gx2_core;
	const int offset = tick_priority_offset >= 0 ? tick_priority_offset : 0;
	const int priority = main_priority + offset > 31 ? 31 : (int)main_priority + offset;

	if (tick_thread_stack)
	{
		OSSetThreadPriority(&tick_thread, priority);
		if (tick_quantum_us > 0)
			OSSetThreadRunQuantum(&tick_thread, (uint32_t)tick_quantum_us);
	}
	wiiu_gx2_idle_sleep_set(shares && gx2_sleep_us > 0 ? (uint32_t)gx2_sleep_us : 0);
	if (log)
		wiiu_log("The tick thread is at priority %d (the game's %d, halo/tick_priority.txt), run quantum %ld us (halo/tick_quantum.txt; -1: as made)%s%s", priority, (int)main_priority, tick_quantum_us,
			shares ? ", on the GX2 thread's core; the GX2 thread's wait for work: " : "", shares ? (gx2_sleep_us > 0 ? "a sleep of that many microseconds (halo/gx2_idle_sleep.txt)" : "a yield (halo/gx2_idle_sleep.txt 0)") : "");
}

static OSThreadAttributes core_attribute(unsigned core)
{
	return (OSThreadAttributes)(core == 1 ? OS_THREAD_ATTRIB_AFFINITY_CPU1 : core == 2 ? OS_THREAD_ATTRIB_AFFINITY_CPU2 : OS_THREAD_ATTRIB_AFFINITY_CPU0);
}

static int tick_thread_main(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	tick_thread_entry(NULL);
	return 0;
}

int wiiu_tick_thread_create(void *(*entry)(void *), unsigned long stack_bytes)
{
	if (tick_thread_stack)
		return -1;
	tick_thread_stack = (uint8_t *)MEMAllocFromDefaultHeapEx(stack_bytes, 16);
	if (!tick_thread_stack)
	{
		wiiu_log("The tick thread: no memory for its stack (%lu bytes)", stack_bytes);
		return -1;
	}
	tick_thread_entry = entry;
	{
		/* (no file: core 2 when the ticks run beside the drawing and the game thread is not on it - core 0 has the audio mixer and the system's threads, and its L2 was 3 times as slow in
		a heavy scene on 2026-11-03; core 2 has the GX2 thread, which sleeps between looks then - else core 0, as before) */
		const long set = card_number("tick_core.txt", -1);
		/* (the overlap as the cards and the map have it for the map that wants the thread: smooth_defaults.h) */
		const int overlap = smooth_defaults_value(SMOOTH_TICK_OVERLAP) > 0;

		tick_core = set >= 0 && set <= 2 ? (unsigned)set : (set < 0 && overlap && OSGetCoreId() != 2) ? 2u : 0u;
		tick_core_now = tick_core;
		gx2_core = wiiu_gx2_thread_core_planned();
		tick_priority_offset = (int)card_number("tick_priority.txt", -1);
		if (tick_priority_offset > 8)
			tick_priority_offset = 8;
		gx2_sleep_us = card_number("gx2_idle_sleep.txt", 0);
		tick_quantum_us = card_number("tick_quantum.txt", -1);
		wiiu_log("The tick thread is made on core %u (halo/tick_core.txt: 0, 1 or 2%s), the game thread is on core %u (halo/game_core.txt), the GX2 thread on core %d%s", tick_core,
			set < 0 ? "; absent: 2 with the overlap on (the default for one local player), else 0" : "", (unsigned)OSGetCoreId(), gx2_core,
			tick_core == (unsigned)OSGetCoreId() ? ": the tick thread is on the game thread's core, and the two cannot run beside each other" : "");
	}
	if (!OSCreateThread(&tick_thread, tick_thread_main, 0, NULL, tick_thread_stack + stack_bytes, stack_bytes, main_priority, core_attribute(tick_core)))
	{
		wiiu_log("OSCreateThread failed for the tick thread");
		MEMFreeToDefaultHeap(tick_thread_stack);
		tick_thread_stack = NULL;
		return -1;
	}
	OSSetThreadName(&tick_thread, "ticks");
	tick_core_settle(1);
	OSResumeThread(&tick_thread);
	return 1;
}

/* the tick thread moved to the calling thread's core (the game thread's, while the overlap is late: the game thread waits for it then, and the plain loop's core is the one with the big
L2), or back to the core it was made on. Called with the tick thread idle (waiting for a job): it takes the core when it is next scheduled. */
void wiiu_tick_thread_move(int to_the_calling_core);
void wiiu_tick_thread_move(int to_the_calling_core)
{
	const unsigned target = to_the_calling_core ? (unsigned)OSGetCoreId() : tick_core;

	if (!tick_thread_stack || target == tick_core_now)
		return;
	OSSetThreadAffinity(&tick_thread, core_attribute(target));
	tick_core_now = target;
	wiiu_log("The tick thread is moved to core %u", target);
	tick_core_settle(1);
}

/* the core the tick thread is set to (tick_overlap.c's log), and the calling thread's */
unsigned wiiu_tick_thread_core_set(void);
unsigned wiiu_tick_thread_core_set(void)
{
	return tick_core_now;
}

unsigned wiiu_core_here(void);
unsigned wiiu_core_here(void)
{
	return (unsigned)OSGetCoreId();
}

/* the tick thread (source/main/main.c) at the game thread's priority: above the workers (the log, the debug server, the voice thread run at the game's +4,
setup_thread) so that a tick is not held up behind a write of the log, and below the audio mixer (priority 10, wiiu_audio.c), which shares core 0 with it and
must run when its ring needs filling: a tick is the one that waits. Returns the priority it set. */
int wiiu_tick_thread_priority(void)
{
	OSSetThreadPriority(OSGetCurrentThread(), main_priority);
	return (int)main_priority;
}

/* DirectSound's packet decoder (dsound_sdl.c): on core 2, which the GX2 thread leaves mostly idle, at the GX2 thread's priority: that thread waits for
work yielding, which gives the core only to threads of its priority or above (at 20, below it, the decoder hardly ran: the sound stuttered, came late or not at all) */
void wiiu_thread_beside_gx2(void)
{
	int wiiu_gx2_thread_priority(void);
	OSThread *self = OSGetCurrentThread();
	const int priority = wiiu_gx2_thread_priority();

	OSSetThreadAffinity(self, OS_THREAD_ATTRIB_AFFINITY_CPU2);
	OSSetThreadPriority(self, priority);
	wiiu_log("audio: the packet decoder runs on core 2 at priority %d (the GX2 thread's)", priority);
}

void wiiu_fpu_setup(void)
{
	__asm__ volatile("mtfsb0 29");
}

void wiiu_tag_thread(const char *name)
{
	wiiu_shell_blackbox_tag(&shell_box, name, OSGetCurrentThread());
}

void wiiu_untag_thread(void)
{
	wiiu_shell_blackbox_untag(&shell_box, OSGetCurrentThread());
}

/* ---------- ProcUI: HOME and closing, called by ProcUIProcessMessages on the main thread */

int wiiu_gx2_release_drain(void);

/* closes every socket the game opened and has not closed (wiiu_posix_net.c); returns how many */
int wiiu_posix_net_close_all(void);

/* whether the game thread is held at the foreground gate (for the other cores' release callbacks) */
int wiiu_game_gate_is_held(void);

int wiiu_game_gate_is_held(void)
{
	return game_threads_held();
}

static uint32_t on_release(void *unused)
{
	(void)unused;
	wiiu_phase("HOME: in our release callback");
	wiiu_missing_keys_suspend();
	foreground = 0;
	wiiu_shell_watch_background(&shell_watch, 1, shell_ms());
	wiiu_log("HOME pressed: the foreground is given up");
	/* the game thread is held at its next safe point (a Present, a submission); the GPU is then drained and given up, and the sound stopped */
	{
		uint32_t waited = 0;

		while (!game_threads_held() && waited < 2000u && started_game)
		{
			sleep_ms(5);
			waited += 5u;
		}
		wiiu_log("HOME: the game thread is %s after %u ms", game_threads_held() ? (game_gate_held ? "held" : "waiting for the tick thread, which is held") : "NOT held", (unsigned)waited);
	}
	/* (voice chat first: its thread closes the microphone, which HOME is not documented to do, before the sound goes) */
	wiiu_voice_suspend();
	wiiu_audio_suspend();
	/* (the GPU is drained by the thread that makes the GX2 calls, wiiu_gx2.c: the system waits for the work of the core that made it) */
	if (!wiiu_gx2_release_drain())
		ProcUIDrawDoneRelease();
	else
		wiiu_log("HOME: the GX2 thread's own release callback (core 2) gives the GPU back");
	wiiu_phase("HOME: after our release callback, in the others' or the system's");
	return 0;
}

static uint32_t on_acquire(void *unused)
{
	(void)unused;
	wiiu_audio_resume();
	wiiu_voice_resume();
	foreground = 1;
	wiiu_missing_keys_resume();
	wiiu_shell_watch_background(&shell_watch, 0, shell_ms());
	wiiu_phase("back in the foreground");
	wiiu_log("Back in the foreground");
	return 0;
}

/* after libwhb's own acquire callback (priority 100) has made its buffers: the game's render targets in the eDRAM are made again (wiiu_gx2.c) */
void wiiu_gx2_mem1_restore(void);

static uint32_t on_acquire_late(void *unused)
{
	(void)unused;
	wiiu_gx2_mem1_restore();
	return 0;
}

static uint32_t on_close(void *unused)
{
	(void)unused;
	wiiu_missing_keys_stop(); /* nonblocking even if the GX2 producer has not parked */
	exit_requested = 1;
	wiiu_shell_watch_arm_exit(&shell_watch, shell_ms(), WIIU_WATCH_CLOSE_MS);
	wiiu_phase("closing");
	wiiu_log("The system asks the app to close (%u s to do it)", (unsigned)(WIIU_WATCH_CLOSE_MS / 1000u));
	/* the map that was playing says how it went (wiiu_map_summary.h) */
	wiiu_map_summary_end();
	return 0;
}

/* The system stops and allows networking for apps in the background and on the way to sleep. Nothing to do about it yet, but the
log tells what was going on around a freeze. */
static uint32_t on_network_stop(void *unused)
{
	(void)unused;
	network_stopped = 1;
	wiiu_debug_network(0);
	wiiu_log("The system asks the app to stop using the network");
	return 0;
}

static uint32_t on_network_start(void *unused)
{
	(void)unused;
	network_stopped = 0;
	wiiu_debug_network(1);
	wiiu_log("The network may be used again");
	return 0;
}

/* ---------- files */

/* for the platform layer's units, built with the Xbox's headers (menu_files.c): whether path is a folder */
int wiiu_directory_exists(const char *path);

int wiiu_directory_exists(const char *path)
{
	struct stat status;

	return stat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

static void make_directories(void)
{
	/* each level; failing because it is there is the usual case */
	mkdir("/vol/external01/wiiu", 0755);
	mkdir("/vol/external01/wiiu/apps", 0755);
	mkdir(WIIU_DATA_DIRECTORY, 0755);
}

/* the last run's file out of the way, and its lines into this run's log; 1 if it had any */
static int report_file(const char *previous, const char *what);

static int report_previous(const char *path, const char *previous, const char *what)
{
	if (!strcmp(what, "black box"))
	{
		if (wiiu_shell_blackbox_take_previous(path, previous) == 0)
			return 0;
	}
	else
	{
		unlink(previous);
		if (rename(path, previous) != 0)
			return 0;
	}
	return report_file(previous, what);
}

/* the lines of a file the last run left (an empty one says nothing); 1 if it had any */
static int report_file(const char *previous, const char *what)
{
	static char text[4096];
	size_t size = 0;
	int file;
	char *line;

	file = open(previous, O_RDONLY);
	if (file < 0)
		return 0;
	for (;;)
	{
		ssize_t got = read(file, text + size, sizeof(text) - 1 - size);

		if (got <= 0)
			break;
		size += (size_t)got;
	}
	close(file);
	text[size] = 0;
	if (!size)
		return 0;
	wiiu_log("The last run left a %s (%s), its lines follow:", what, previous);
	for (line = strtok(text, "\n"); line; line = strtok(NULL, "\n"))
		wiiu_log("  %.200s", line);
	return 1;
}

/* ---------- the memory, in a line */

static size_t memory_line(char *out, size_t capacity)
{
	MEMHeapHandle heap = MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM2);

	if (heap && heap->tag == MEM_EXPANDED_HEAP_TAG)
		return (size_t)snprintf(out, capacity, "MEM2 heap: %u KB free, %u KB in the largest block",
			(unsigned)(MEMGetTotalFreeSizeForExpHeap(heap) / 1024u), (unsigned)(MEMGetAllocatableSizeForExpHeapEx(heap, 4) / 1024u));
	out[0] = 0;
	return 0;
}

/* ---------- the monitor threads */

/* tells the log of each place in the game an alignment exception has been answered at, the first time (the heartbeat has the count) */
static void tell_new_fixups(uint32_t *told)
{
	uint32_t count;
	uint32_t pc;

	while ((pc = wiiu_crash_fixup_place(*told, &count)) != 0)
	{
		wiiu_log("A misaligned float or double was made at pc %08X (the first of %u there; the %u-th place): fix the code or leave it to the handler",
			(unsigned)pc, (unsigned)count, (unsigned)(*told + 1));
		(*told)++;
	}
}

/* set as main returns: the app's own close is done, and the watchdog stands down (its _Exit is for a close that never gets there; one during the system's
end of the process would be a second exit from another thread) */
static volatile uint32_t main_returning;

/* The core time of the app's main threads over the last beat, from the OS's own account (OSThread::coreTimeConsumedNs, which leaves out the slice a thread is in, and the number of times
it was woken): the game thread, the tick thread, the audio mixer and the GX2 thread, each with the core it is allowed (the first of its affinity). A tick thread that is busy for the whole
of a job but is given a third of the core's time is one a higher priority thread (the mixer, at 10, on core 0) shares its core with. */
uint64_t wiiu_audio_thread_cpu_ns(uint64_t *wakes);
uint64_t wiiu_gx2_thread_cpu_ns(uint64_t *wakes);

static unsigned affinity_core(const OSThread *thread)
{
	const unsigned mask = (unsigned)OSGetThreadAffinity((OSThread *)thread);

	return mask & 1u ? 0u : mask & 2u ? 1u : 2u;
}

static void threads_report(uint32_t window_ms)
{
	static uint64_t last_ns[4], last_wakes[4];
	uint64_t ns[4], wakes[4];
	char text[200];
	size_t used = 0;
	int which;
	static const char *const names[4] = { "game", "ticks", "audio mixer", "GX2" };
	unsigned cores[4];

	ns[0] = game_thread.coreTimeConsumedNs;
	wakes[0] = game_thread.wakeCount;
	ns[1] = tick_thread_stack ? tick_thread.coreTimeConsumedNs : 0;
	wakes[1] = tick_thread_stack ? tick_thread.wakeCount : 0;
	ns[2] = wiiu_audio_thread_cpu_ns(&wakes[2]);
	ns[3] = wiiu_gx2_thread_cpu_ns(&wakes[3]);
	cores[0] = affinity_core(&game_thread);
	cores[1] = tick_thread_stack ? affinity_core(&tick_thread) : 0;
	cores[2] = 0;
	cores[3] = 2;
	for (which = 0; which < 4; which++)
	{
		if (ns[which] && used < sizeof(text) - 60)
		{
			used += (size_t)snprintf(text + used, sizeof(text) - used, "%s%s %.2f s on core %u (woken %u)", used ? ", " : "", names[which],
				(double)(ns[which] - last_ns[which]) / 1e9, cores[which], (unsigned)(wakes[which] - last_wakes[which]));
		}
		last_ns[which] = ns[which];
		last_wakes[which] = wakes[which];
	}
	wiiu_log("threads: core time in the last %.1f s: %s", (double)window_ms / 1000.0, text);
}

static void *watchdog_main(void *unused)
{
	uint32_t frames_before = 0;
	uint32_t fixup_places_told = 0;
	uint32_t last_beat_ms = shell_ms();

	(void)unused;
	setup_thread("watchdog");
	for (;;)
	{
		uint32_t stalled = 0;
		uint32_t now;
		wiiu_watch_result result;

		sleep_ms(50);
		if (main_returning)
			return NULL;
		now = shell_ms();
		tell_new_fixups(&fixup_places_told);
		result = wiiu_shell_watch_poll(&shell_watch, now, &stalled);
		if (result == WIIU_WATCH_END)
		{
			/* the log may be what is stuck, so the last words are given a moment and the process ends whether they got out */
			wiiu_log("Closing took too long: ending the app");
			exit_note("the close's deadline passed: the watchdog ends the app (_Exit)");
			wiiu_shell_log_flush(&shell_log, 600);
			_Exit(0);
		}
		if (result == WIIU_WATCH_STALLED)
		{
			const char *phase = shell_watch.phase;

			wiiu_log("The game's loop has not gone round for %u s; its phase is: %s", (unsigned)(stalled / 1000u),
				in_app_memory(phase, 0x01000000u) ? phase : "unknown");
			box_wanted = 1;
		}
		if (now - last_beat_ms >= HEARTBEAT_PERIOD_MS)
		{
			char text[200];
			uint32_t pending, queued, written, dropped;

			wiiu_shell_log_counts(&shell_log, &pending, &queued, &written, &dropped);
			wiiu_shell_watch_describe(&shell_watch, now, frames_before, now - last_beat_ms, text, sizeof(text));
			snprintf(last_beat, sizeof(last_beat), "%s", text);
			wiiu_log("beat: %s; %u log lines waiting; %u misaligned accesses made by the handler", text, (unsigned)pending,
				(unsigned)wiiu_crash_fixups());
			threads_report(now - last_beat_ms);
			frames_before = shell_watch.frames;
			last_beat_ms = now;
		}
	}
	return NULL;
}

static void *blackbox_main(void *unused)
{
	uint32_t last = 0;
	int reported_error = 0;

	(void)unused;
	setup_thread("black box");
	/* the first record soon, so that even a short run leaves one */
	last = shell_ms() - (BLACKBOX_PERIOD_MS - 1000u);
	for (;;)
	{
		const uint32_t now = shell_ms();

		sleep_ms(100);
		if (box_stopping)
			continue;
		if (box_wanted || shell_ms() - last >= BLACKBOX_PERIOD_MS)
		{
			wiiu_blackbox_facts facts;
			char memory[120];
			uint32_t pending, queued, written, dropped;
			int error;

			box_wanted = 0;
			box_busy = 1;
			if (!box_stopping)
			{
				wiiu_shell_log_counts(&shell_log, &pending, &queued, &written, &dropped);
				memset(&facts, 0, sizeof(facts));
				facts.now_ms = shell_ms();
				facts.frames = shell_watch.frames;
				facts.phase = in_app_memory(shell_watch.phase, 0x01000000u) ? shell_watch.phase : NULL;
				facts.phase_ago_ms = facts.now_ms - shell_watch.phase_ms;
				facts.background = shell_watch.background;
				facts.log_pending = pending;
				facts.log_written = written;
				facts.log_dropped = dropped;
				facts.memory = memory_line(memory, sizeof(memory)) ? memory : NULL;
				error = wiiu_shell_blackbox_tick(&shell_box, &facts);
				if (error && !reported_error)
				{
					reported_error = 1;
					wiiu_log("The black box cannot be written (error %d)", error);
				}
			}
			box_busy = 0;
			last = now;
		}
	}
	return NULL;
}

static int start_monitor(void *(*function)(void *))
{
	pthread_attr_t attributes;
	pthread_t thread;
	int error;

	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&attributes, MONITOR_STACK_BYTES);
	error = pthread_create(&thread, &attributes, function, NULL);
	pthread_attr_destroy(&attributes);
	return error;
}

/* ---------- the game */

/* port/linux/game/startup_timeline.c: the start-up's marks */
void startup_timeline_mark(const char *name);

static int game_entry(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	wiiu_fpu_setup();
	setup_thread("game");
	wiiu_phase("the game's main");
	startup_timeline_mark("game_thread");
	wiiu_log("The game starts on core %u", (unsigned)OSGetCoreId());
	game_result = game_main_function();
	wiiu_log("The game's main returned %d", game_result);
	wiiu_untag_thread();
	game_done = 1;
	return game_result;
}

static int start_game(void)
{
	game_stack = memalign(32, GAME_STACK_BYTES);
	if (!game_stack)
	{
		wiiu_log("No memory for the game's stack (%u KB)", (unsigned)(GAME_STACK_BYTES / 1024u));
		return ENOMEM;
	}
	/* the core the game runs on (halo/game_core.txt: 0, 1 or 2; 1 if none). GX2 must be driven from the core ProcUI's callbacks run on (1): the system does not run the app's threads on the
	other cores while its HOME callback does, and the GPU's release then never completes: so with the GX2 thread on core 1 the game itself can be on 0 or 2 (wiiu_gx2.c) */
	unsigned game_core = 1;
	/* the tick owners' queues (port/linux/game/tick_owners.h; halo/tick_owners.txt: 0 off, 1 queues applied where the direct call was, 2
	queues applied at their owner's points, off if none; and after it the parts to leave out, a number): the game reads the setting from its environment, as the host runs do */
	{
		FILE *file = fopen(WIIU_DATA_DIRECTORY "/tick_owners.txt", "r");

		if (file)
		{
			int mode_number = 0;
			unsigned mode;
			unsigned long off = 0;
			int read = fscanf(file, "%d %li", &mode_number, (long *)&off);

			mode = mode_number < 0 ? 0 : (unsigned)mode_number; /* (-1: as no file) */

			if (read >= 1 && mode >= 1 && mode <= 2)
			{
				char text[16];

				setenv("HALO_TICK_OWNERS", mode == 1 ? "1" : "2", 1);
				/* (an optional second number: the parts left out, tick_owners.h) */
				if (read == 2 && off)
				{
					snprintf(text, sizeof(text), "0x%lx", off);
					setenv("HALO_TICK_OWNERS_OFF", text, 1);
				}
				wiiu_log("Tick owners: the queues are on, mode %u, parts left out 0x%lx (halo/tick_owners.txt)", mode, off);
			}
			fclose(file);
		}
	}
	{
		FILE *file = fopen(WIIU_DATA_DIRECTORY "/game_core.txt", "r");

		if (file)
		{
			int number = 1;

			if (fscanf(file, "%d", &number) != 1 || number < 0 || number > 2) /* (-1: as no file) */
				number = 1;
			game_core = (unsigned)number;
			fclose(file);
		}
	}
	if (!OSCreateThread(&game_thread, game_entry, 0, NULL, game_stack + GAME_STACK_BYTES, GAME_STACK_BYTES, main_priority,
			(OSThreadAttributes)(game_core == 0 ? OS_THREAD_ATTRIB_AFFINITY_CPU0 : game_core == 2 ? OS_THREAD_ATTRIB_AFFINITY_CPU2 : OS_THREAD_ATTRIB_AFFINITY_CPU1)))
	{
		wiiu_log("OSCreateThread failed for the game");
		return EAGAIN;
	}
	OSSetThreadName(&game_thread, "game");
	OSResumeThread(&game_thread);
	started_game = 1;
	return 0;
}

/* ---------- the game stopped itself */

/* The game's fatal errors end in an endless red screen (halt_and_catch_fire); here that is the app ending: said in the log (with the debug.txt
that has the reason beside it), the Wii U Menu asked for, and this thread parked until the system closes the app. */
void wiiu_game_halted(void);

void wiiu_game_halted(void)
{
	wiiu_log("The game halted (its reason is in debug.txt beside the maps): returning to the Wii U Menu");
	wiiu_shell_log_flush(&shell_log, 1000);
	/* (this thread is parked for good, as the foreground gate parks it: the release callback must not wait for it to be held, as it did, 2 s a time, with the
	console left on the Wii U's logo) */
	game_halted = 1;
	game_gate_held = 1;
	SYSLaunchMenu();
	for (;;)
		sleep_ms(100);
}

/* The Wii U Menu asked for, as when the game halts (the debug server's quit): the system then closes the app through ProcUI (under Aroma it may then
start the app again, or stay on the Wii U logo: HOME and Close software is the sure way out). */
void wiiu_launch_menu(void)
{
	wiiu_log("The Wii U Menu is asked for");
	wiiu_shell_log_flush(&shell_log, 500);
	SYSLaunchMenu();
}

/* Shader harvesting belongs to the main log worker, never the independent error-log worker. */
static uint32_t missing_thread_id(void *unused)
{
	(void)unused;
	return (uint32_t)(uintptr_t)OSGetCurrentThread();
}

static int missing_io_allowed(void *unused)
{
	(void)unused;
	return foreground != 0; /* a close in background is reported incomplete; no callback does file I/O */
}

static void missing_report(void *unused, const char *text)
{
	(void)unused;
	wiiu_log_text(text);
}

/* ---------- the shell */

int wiiu_shell_run(int (*game_main)(void))
{
	wiiu_shell_log_config log_config;
	wiiu_missing_keys_config missing_config;
	uint64_t run_id;
	char text[160];
	int error, crash_note, relaunching;

	base_ms = system_ms();
	{
		/* (the last run's is kept, and its lines logged once the log is up: after a close that hung, the console is often started again before anyone
		has read the file, as on 2026-10-30, when a HOME close stopped after "Closed" and the next start emptied its trail) */
		FILE *trail;

		unlink(EXIT_TRAIL_PREVIOUS_FILE);
		rename(EXIT_TRAIL_FILE, EXIT_TRAIL_PREVIOUS_FILE);
		trail = fopen(EXIT_TRAIL_FILE, "w"); /* (this run's) */

		if (trail)
			fclose(trail);
		atexit(exit_note_last); /* (the first registered runs last) */
	}
	main_priority = OSGetThreadPriority(OSGetCurrentThread());
	game_main_function = game_main;
	wiiu_shell_watch_init(&shell_watch, 0);
	wiiu_shell_blackbox_init(&shell_box, BLACKBOX_FILE, thread_where);

	WHBProcInit();
	ProcUIRegisterCallback(PROCUI_CALLBACK_RELEASE, on_release, NULL, RELEASE_PRIORITY);
	ProcUIRegisterCallback(PROCUI_CALLBACK_ACQUIRE, on_acquire, NULL, ACQUIRE_PRIORITY);
	ProcUIRegisterCallback(PROCUI_CALLBACK_ACQUIRE, on_acquire_late, NULL, ACQUIRE_LATE_PRIORITY);
	ProcUIRegisterCallback(PROCUI_CALLBACK_EXIT, on_close, NULL, EXIT_PRIORITY);
	ProcUIRegisterCallback(PROCUI_CALLBACK_NET_IO_STOP, on_network_stop, NULL, ACQUIRE_PRIORITY);
	ProcUIRegisterCallback(PROCUI_CALLBACK_NET_IO_START, on_network_start, NULL, ACQUIRE_PRIORITY);

	make_directories();
	unlink(LOG_PREVIOUS_FILE);
	rename(LOG_FILE, LOG_PREVIOUS_FILE);
	{
		struct stat error_file;

		/* (before the game writes its first line: the writer is made by that line) */
		if (stat(ERROR_FILE, &error_file) == 0 && error_file.st_size > ERROR_FILE_ROLL_BYTES)
		{
			unlink(ERROR_PREVIOUS_FILE);
			rename(ERROR_FILE, ERROR_PREVIOUS_FILE);
		}
	}
	memset(&missing_config, 0, sizeof(missing_config));
	run_id = OSGetSystemTime();
	missing_config.directory = WIIU_DATA_DIRECTORY;
	missing_config.run_high = (uint32_t)(run_id >> 32);
	missing_config.run_low = (uint32_t)run_id;
	missing_config.thread_id = missing_thread_id;
	missing_config.io_allowed = missing_io_allowed;
	missing_config.report = missing_report;
	error = wiiu_missing_keys_start(&missing_config);
	if (error)
		OSReport("Shader capture cannot be started (error %d)\n", error);
	else
	{
		/* (halo/shader_capture.txt "always": for sweeps that collect the keys a level needs; players never need it) */
		char mode[16] = "";
		FILE *file = fopen(WIIU_DATA_DIRECTORY "/shader_capture.txt", "r");

		if (file)
		{
			if (!fgets(mode, sizeof(mode), file))
				mode[0] = 0;
			fclose(file);
		}
		if (!strncmp(mode, "always", 6))
			wiiu_missing_keys_always(1);
	}
	memset(&log_config, 0, sizeof(log_config));
	log_config.service = wiiu_missing_keys_service;
	log_config.path = LOG_FILE;
	log_config.now_ms = shell_ms;
	log_config.thread_setup = setup_thread;
	log_config.echo = echo_to_system;
	error = wiiu_shell_log_start(&shell_log, &log_config);
	if (error)
		OSReport("The log cannot be started (error %d)\n", error);
	else
		started = 1;

	wiiu_tag_thread("main");
	wiiu_phase("starting");
	startup_timeline_mark("log_up");
	wiiu_log("OpenCE for the Wii U starting: main thread on core %u at priority %d", (unsigned)OSGetCoreId(), (int)main_priority);
	if (memory_line(text, sizeof(text)))
		wiiu_log("%s", text);
	report_file(EXIT_TRAIL_PREVIOUS_FILE, "close trail");
	report_previous(BLACKBOX_FILE, BLACKBOX_PREVIOUS_FILE, "black box");
	crash_note = report_previous(CRASH_FILE, CRASH_PREVIOUS_FILE, "crash note");
	wiiu_crash_install(CRASH_FILE, &shell_watch.phase, last_beat);

	if (start_monitor(watchdog_main) || start_monitor(blackbox_main))
		wiiu_log("A monitor thread could not be started");
	/* the self-updater (wiiu_update.c), which counts a new build's starts, crash notes among them, and may put the old build back before
	the game starts: then the files on the card are another build's, which is being started in place of this one, and the game is not */
	wiiu_phase("the updater's start");
	relaunching = wiiu_update_startup(crash_note);
	startup_timeline_mark("updater");
	/* (halo/mic_probe.txt 1: the microphone probe; voice chat's thread is otherwise started by the first network game's voice; not when the
	updater is starting the other build in this one's place) */
	if (!relaunching)
		wiiu_voice_boot();
	error = relaunching ? 0 : start_game();
	if (error)
		game_done = 1;
	else if (relaunching)
		wiiu_log("Another build is being started in place of this one: the game does not start");
	else
	{
		/* the debug server (screenshots, button presses, quit, the log) listens on the network to anyone on it, so it is for development only: it
		starts when halo/debug_server.txt says 1, and a player's card has no such file */
		int debug_server = 0;
		FILE *file = fopen(WIIU_DATA_DIRECTORY "/debug_server.txt", "r");

		if (file)
		{
			if (fscanf(file, "%d", &debug_server) != 1)
				debug_server = 0;
			fclose(file);
		}
		if (debug_server == 1)
			wiiu_debug_start(memory_line);
		else
			wiiu_log("Debug server off (halo/debug_server.txt 1 turns it on)");
	}

	/* the system's loop: HOME and closing. WHBProcIsRunning does not come back while the app is in the background. */
	while (!game_done && WHBProcIsRunning())
	{
		wiiu_phase("in the system's process loop");
		sleep_ms(20);
	}
	wiiu_missing_keys_stop();
	wiiu_log("Leaving: %s", game_done ? "the game has ended" : exit_requested ? "the app is closing" : "the system is stopping the app");

	/* the game cannot be asked to finish yet (its quit comes with the input layer): a moment for one that is about to */
	if (!game_done)
	{
		wiiu_shell_watch_arm_exit(&shell_watch, shell_ms(), WIIU_WATCH_CLOSE_MS);
		sleep_ms(300);
	}
	/* the game thread is held at its next safe point, and the sound given up, before the process goes (the system waits for the GPU and the sound engine) */
	{
		uint32_t waited = 0;

		while (!game_threads_held() && waited < 1500u && started_game && !game_done)
		{
			sleep_ms(5);
			waited += 5u;
		}
		wiiu_log("Closing: the game thread is %s after %u ms", game_threads_held() ? (game_gate_held ? "held" : "waiting for the tick thread, which is held") : "NOT held", (unsigned)waited);
		/* (voice chat's thread closes the microphone and ends; one that does not within 300 ms is left, as the log writers are, and the
		trail says so) */
		{
			const int voice = wiiu_voice_stop(300);

			if (voice != 2)
				exit_note(voice ? "the voice thread ended" : "the voice thread still running after 300 ms");
		}
		wiiu_audio_suspend();
		/* (in the background the GPU is the system's: waiting for it there never ends, and it was drained when it was given up; and a run
		whose game never started has not used it) */
		if (foreground && started_game)
			GX2DrawDone();
	}
	/* a checkpoint's write still on its way to the card (the file is the game's own scratch, read again only by this run's revert: nothing is lost if it does not end) is waited for
	a moment, so that the card is not taken away from under it; the close does not wait longer */
	{
		int game_state_port_save_wait(unsigned long timeout_ms);

		exit_note(game_state_port_save_wait(1500) ? "no checkpoint write is running" : "a checkpoint write still running after 1500 ms");
	}
	wiiu_phase("shutting down");
	box_stopping = 1;
	{
		/* a record being written (the card can take a while) is waited for a moment; if it is not done, the file stays, and the
		next start reads it as a stop it was not */
		int waited;

		for (waited = 0; waited < 30 && box_busy; waited++)
			sleep_ms(10);
		if (!box_busy)
			wiiu_shell_blackbox_stop(&shell_box);
	}
	/* the updater's worker, which looks at its flags every 50 ms (and its transfers every 100), before the sockets close and the card goes */
	exit_note(wiiu_update_stop(500) ? "the updater's worker ended" : "the updater's worker still running after 500 ms");
	/* Bounded wait only: an ACTIVE producer or slow/absent card never blocks ProcUI indefinitely.
	   All queue/batch storage remains process-lifetime if the deadline expires. Normal log flush is separate. */
	{
		uint32_t waited = 0;
		wiiu_missing_keys_counts counts;

		do
		{
			wiiu_missing_keys_counts_get(&counts);
			if (counts.drained || counts.failed || !foreground || waited >= 300u)
				break;
			sleep_ms(5);
			waited += 5u;
		} while (1);
		wiiu_missing_keys_quiesce(); /* no new worker turn may race FS teardown */
		wiiu_missing_keys_report("close");
	}
	wiiu_log("Closed");
	if (error_log_started)
		wiiu_shell_log_flush(&error_log, 500);
	wiiu_shell_log_flush(&shell_log, 1500);
	exit_note("the log is flushed");
	/* Nothing of the app's is left writing to the card or holding a socket when main returns and wut takes the card away (__fini_wut_devoptab): the logs'
	writers (and the shader capture's, the log's service) are ended, and the game's sockets closed, each step in the trail, so that a close that hangs
	says where. (No GX2 call: one at exit froze the console, 2c6afc9.) */
	if (error_log_started)
		exit_note(wiiu_shell_log_stop(&error_log, 500) ? "debug.txt's writer ended" : "debug.txt's writer still running after 500 ms");
	exit_note(wiiu_shell_log_stop(&shell_log, 500) ? "the log's writer ended" : "the log's writer still running after 500 ms");
	{
		char note[64];

		snprintf(note, sizeof(note), "the game's sockets closed: %d", wiiu_posix_net_close_all());
		exit_note(note);
	}
	main_returning = 1;
	exit_note("the watchdog stood down; WHBProcShutdown next");
	WHBProcShutdown();
	exit_note("WHBProcShutdown returned; main returns");
	return game_done ? game_result : 0;
}

/* What follows main's return is the C library's and the system's: the steps wut takes then (the SD card, newlib) are told in exit-trail.txt, as they happen, so that after a
close that hung the console the file says how far it got (the process's own log is closed by then) */
void __fini_wut_devoptab(void);
void __fini_wut_newlib(void);

void __fini_wut(void)
{
	exit_note("wut fini (the socket library is not linked: nothing for it)");
	exit_note("wut fini: the SD card's devoptab");
	__fini_wut_devoptab();
	__fini_wut_newlib();
}
