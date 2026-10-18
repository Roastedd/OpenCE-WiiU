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
#include "wiiu_shell_blackbox.h"
#include "wiiu_shell_log.h"
#include "wiiu_shell_watch.h"

#include <coreinit/core.h>
#include <coreinit/debug.h>
#include <coreinit/memexpheap.h>
#include <coreinit/memheap.h>
#include <coreinit/mutex.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
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
#define EXIT_PRIORITY 50
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
static volatile uint32_t foreground = 1;
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

void wiiu_request_exit(void)
{
	exit_requested = 1;
	/* the main thread's loop ends; the app closes */
	WHBProcStopRunning();
}

void platform_request_quit(void)
{
	wiiu_request_exit();
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

static uint32_t on_release(void *unused)
{
	(void)unused;
	wiiu_phase("HOME: in our release callback");
	foreground = 0;
	wiiu_shell_watch_background(&shell_watch, 1, shell_ms());
	wiiu_log("HOME pressed: the foreground is given up");
	wiiu_phase("HOME: after our release callback, in the others' or the system's");
	return 0;
}

static uint32_t on_acquire(void *unused)
{
	(void)unused;
	foreground = 1;
	wiiu_shell_watch_background(&shell_watch, 0, shell_ms());
	wiiu_phase("back in the foreground");
	wiiu_log("Back in the foreground");
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
	wiiu_log("The system asks the app to stop using the network");
	return 0;
}

static uint32_t on_network_start(void *unused)
{
	(void)unused;
	wiiu_log("The network may be used again");
	return 0;
}

/* ---------- files */

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

static void *watchdog_main(void *unused)
{
	uint32_t frames_before = 0;
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
			wiiu_log("beat: %s; %u log lines waiting", text, (unsigned)pending);
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
	if (!OSCreateThread(&game_thread, game_entry, 0, NULL, game_stack + GAME_STACK_BYTES, GAME_STACK_BYTES, main_priority,
			OS_THREAD_ATTRIB_AFFINITY_CPU1))
	{
		wiiu_log("OSCreateThread failed for the game");
		return EAGAIN;
	}
	OSSetThreadName(&game_thread, "game");
	OSResumeThread(&game_thread);
	return 0;
}

/* ---------- the shell */

int wiiu_shell_run(int (*game_main)(void))
{
	wiiu_shell_log_config log_config;
	char text[160];
	int error;

	base_ms = system_ms();
	main_priority = OSGetThreadPriority(OSGetCurrentThread());
	game_main_function = game_main;
	wiiu_shell_watch_init(&shell_watch, 0);
	wiiu_shell_blackbox_init(&shell_box, BLACKBOX_FILE, thread_where);

	WHBProcInit();
	ProcUIRegisterCallback(PROCUI_CALLBACK_RELEASE, on_release, NULL, RELEASE_PRIORITY);
	ProcUIRegisterCallback(PROCUI_CALLBACK_ACQUIRE, on_acquire, NULL, ACQUIRE_PRIORITY);
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
	wiiu_shell_log_flush(&shell_log, 1500);
	WHBProcShutdown();
	return game_done ? game_result : 0;
}
