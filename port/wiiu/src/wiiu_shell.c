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
#include "wiiu_shell_watch.h"

#include <coreinit/core.h>
#include <sysapp/launch.h>
#include <coreinit/debug.h>
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
#define BLACKBOX_PERIOD_MS 5000u
#define HEARTBEAT_PERIOD_MS 5000u
/* ProcUI runs the callbacks of the higher priority first; ours runs before SDL's (100) and the like */
#define RELEASE_PRIORITY 2000
#define ACQUIRE_PRIORITY 50
#define ACQUIRE_LATE_PRIORITY 3000 /* (acquire callbacks run lowest first, the reverse of release's: libwhb's, at 100, makes its heap and buffers before this) */
#define EXIT_PRIORITY 50
#define EXIT_TRAIL_FILE WIIU_DATA_DIRECTORY "/exit-trail.txt"
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

void wiiu_foreground_gate(int is_game_thread);

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

/* The game quits (its Quit, or a timed run that is over): the Wii U Menu is asked for, and the system ends the app through ProcUI, as it does for HOME's Close
software (the release and exit callbacks above run). Stopping the loop and returning from main left the console on the last picture with its network dead;
the debug server's quit, which asks this way, closed the app cleanly each time it was run. */
void wiiu_request_exit(void)
{
	static volatile uint32_t asked;

	if (asked)
		return;
	asked = 1;
	wiiu_launch_menu();
}

void platform_request_quit(void)
{
	wiiu_request_exit();
}

/* for the threads the game and the port make with pthreads (the game's CreateThread: its file reads, input; the vertical blank): off the game thread's core and the GX2
thread's, on core 0 */
void wiiu_thread_off_game_core(void)
{
	OSSetThreadAffinity(OSGetCurrentThread(), OS_THREAD_ATTRIB_AFFINITY_CPU0);
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

/* whether the game thread is held at the foreground gate (for the other cores' release callbacks) */
int wiiu_game_gate_is_held(void);

int wiiu_game_gate_is_held(void)
{
	return game_gate_held != 0;
}

static uint32_t on_release(void *unused)
{
	(void)unused;
	wiiu_phase("HOME: in our release callback");
	foreground = 0;
	wiiu_shell_watch_background(&shell_watch, 1, shell_ms());
	wiiu_log("HOME pressed: the foreground is given up");
	/* the game thread is held at its next safe point (a Present, a submission); the GPU is then drained and given up, and the sound stopped */
	{
		uint32_t waited = 0;

		while (!game_gate_held && waited < 2000u && started_game)
		{
			sleep_ms(5);
			waited += 5u;
		}
		wiiu_log("HOME: the game thread is %s after %u ms", game_gate_held ? "held" : "NOT held", (unsigned)waited);
	}
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
	foreground = 1;
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
	exit_requested = 1;
	wiiu_shell_watch_arm_exit(&shell_watch, shell_ms(), WIIU_WATCH_CLOSE_MS);
	wiiu_phase("closing");
	wiiu_log("The system asks the app to close (%u s to do it)", (unsigned)(WIIU_WATCH_CLOSE_MS / 1000u));
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

/* the last run's file out of the way, and its lines into this run's log */
static void report_previous(const char *path, const char *previous, const char *what)
{
	static char text[4096];
	size_t size = 0;
	int file;
	char *line;

	if (!strcmp(what, "black box"))
	{
		if (wiiu_shell_blackbox_take_previous(path, previous) == 0)
			return;
	}
	else
	{
		unlink(previous);
		if (rename(path, previous) != 0)
			return;
	}
	file = open(previous, O_RDONLY);
	if (file < 0)
		return;
	for (;;)
	{
		ssize_t got = read(file, text + size, sizeof(text) - 1 - size);

		if (got <= 0)
			break;
		size += (size_t)got;
	}
	close(file);
	text[size] = 0;
	wiiu_log("The last run left a %s (%s), its lines follow:", what, previous);
	for (line = strtok(text, "\n"); line; line = strtok(NULL, "\n"))
		wiiu_log("  %.200s", line);
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
		now = shell_ms();
		tell_new_fixups(&fixup_places_told);
		result = wiiu_shell_watch_poll(&shell_watch, now, &stalled);
		if (result == WIIU_WATCH_END)
		{
			/* the log may be what is stuck, so the last words are given a moment and the process ends whether they got out */
			wiiu_log("Closing took too long: ending the app");
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

static int game_entry(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	wiiu_fpu_setup();
	setup_thread("game");
	wiiu_phase("the game's main");
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
	{
		FILE *file = fopen(WIIU_DATA_DIRECTORY "/game_core.txt", "r");

		if (file)
		{
			if (fscanf(file, "%u", &game_core) != 1 || game_core > 2)
				game_core = 1;
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

/* ---------- the shell */

int wiiu_shell_run(int (*game_main)(void))
{
	wiiu_shell_log_config log_config;
	char text[160];
	int error;

	base_ms = system_ms();
	{
		FILE *trail = fopen(EXIT_TRAIL_FILE, "w"); /* (this run's) */

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
	wiiu_log("OpenCE for the Wii U starting: main thread on core %u at priority %d", (unsigned)OSGetCoreId(), (int)main_priority);
	if (memory_line(text, sizeof(text)))
		wiiu_log("%s", text);
	report_previous(BLACKBOX_FILE, BLACKBOX_PREVIOUS_FILE, "black box");
	report_previous(CRASH_FILE, CRASH_PREVIOUS_FILE, "crash note");
	wiiu_crash_install(CRASH_FILE, &shell_watch.phase, last_beat);

	if (start_monitor(watchdog_main) || start_monitor(blackbox_main))
		wiiu_log("A monitor thread could not be started");
	error = start_game();
	if (error)
		game_done = 1;
	else
		wiiu_debug_start(memory_line);

	/* the system's loop: HOME and closing. WHBProcIsRunning does not come back while the app is in the background. */
	while (!game_done && WHBProcIsRunning())
	{
		wiiu_phase("in the system's process loop");
		sleep_ms(20);
	}
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

		while (!game_gate_held && waited < 1500u && started_game && !game_done)
		{
			sleep_ms(5);
			waited += 5u;
		}
		wiiu_log("Closing: the game thread is %s after %u ms", game_gate_held ? "held" : "NOT held", (unsigned)waited);
		wiiu_audio_suspend();
		/* (in the background the GPU is the system's: waiting for it there never ends, and it was drained when it was given up) */
		if (foreground)
			GX2DrawDone();
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
	wiiu_log("Closed");
	if (error_log_started)
		wiiu_shell_log_flush(&error_log, 500);
	wiiu_shell_log_flush(&shell_log, 1500);
	exit_note("the log is flushed; WHBProcShutdown next");
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
