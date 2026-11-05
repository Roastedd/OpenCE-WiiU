/*
GAME_DIGEST_GLUE.C

The game digest's glue (port/wiiu/tests/game_digest/build.py): the real game on big-endian PowerPC Linux, under qemu-ppc, drawing through the
Wii U's Direct3D layer (d3d8_gx2.c and its units) into the D3D8 replay's GX2-thread reader (port/wiiu/tests/d3d8_replay/replay_gx2.c), which
writes each draw, clear, present, marker and visibility test as a line of what the GPU is given. This file has

- the sink of those lines: each frame's digest, in order and as a multiset, with the game's tick, written to GAME_DIGEST_DIR/frames.txt, and
  with GAME_DIGEST_TRACE every line of the frames asked for to GAME_DIGEST_DIR/trace.txt; the run ends at a frame limit or a tick limit,
  with the digests of the whole run and of its frames of the game in GAME_DIGEST_DIR/summary.txt (sink_start has the settings);
- what makes two runs draw the same frames under an emulator whose speed varies: the game's clock in milliseconds counted by the frames, the
  silent sound mixed by the frames, the map's reads and its copy to the cache waited for, the local random numbers set again at each map's
  start, and the lanes of the constants the game never sets made 0 (each section below has why);
- two diagnostics for when two runs' frames part: a frame's constants, vertex format and small vertex streams (GAME_DIGEST_DETAIL, detail.txt),
  and who drew the local random numbers in each frame (GAME_DIGEST_RANDOM, random.txt);
- what the layer and the game's HALO_WIIU code call that only the console has (the GX2 thread's answers, the shell's log, the input's
  questions), as replay_world.c and the console's defaults have them; and the platform's window as the null renderer has it (no window).

The layer runs as gx2_thread 0 does on the console: each stream is read as it is handed over (replay_gx2_start's sync mode). What this changes in
the game's running: when things happen against the machine's time (the loading screens' frames, the reads, the sound's mixing), the local random
numbers (not the simulation's) at a map's start and for the system's nickname, and the unset lanes of the lighting constants. The game's ticks are
the host runs' (every frame of a game is one tick, HALO_FIXED_STEP): the tick log of a run matches
runs/host-mac-perf-20261031/ticks/baseline-ppc-runtime.txt's.
*/

#include "replay.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---------- replay_world.c's hash and random numbers, which replay_gx2.c uses */

uint64_t replay_hash(uint64_t hash, const void *data, unsigned long size)
{
	const unsigned char *at = data;

	while (size--)
		hash = (hash ^ *at++) * 1099511628211ULL;
	return hash;
}

uint32_t replay_random(uint32_t *state)
{
	uint32_t x = *state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return *state = x ? x : 1;
}

/* ---------- the game's clock in milliseconds, by the frames

The game reads the time in milliseconds (system_milliseconds) for what it draws as well as for its timeouts: the menus' plasma scrolls by it
(ui_widget.c's draw_bitmap_in_rect), the loading screen's progress bar turns by it (progress_bar.c, which reads GetTickCount: build.py
renames its reads to game_digest_milliseconds, and the scripted player's, xinput_sdl.c's SDL_GetTicks, to game_digest_sdl_ticks: it moves
the menus' cursor before the main menu's clock runs), the screen effects' noise is seeded by it, and the network code and the caches time their
waits with it. Under qemu the time a frame takes is the emulator's, so those draws would follow the machine's speed. Here every call of the game's (build.py links
them to this, -Wl,--wrap=system_milliseconds) answers the time of 30 frames a second: a second from the start, and a thirtieth of one more for each
frame presented, as on a console that keeps its 30 frames. The game thread does wait on this clock within a frame at times (sound_manager.c's
sound_dispose_from_old_map idles the sound for 300 ms of it at a map's end): when it reads it more than 100000 times in one frame, it moves on a
millisecond a read from then on. Only the game thread's reads count (it is the thread that presents); the others read the same time. */

static volatile unsigned long clock_frames, clock_reads, clock_extra;
static pthread_t clock_thread;
static int clock_thread_known;

static void audio_frame(void);

static void clock_frame(void)
{
	if (!clock_thread_known)
	{
		clock_thread = pthread_self();
		clock_thread_known = 1;
	}
	clock_frames++;
	clock_reads = 0;
	audio_frame();
}

unsigned long __wrap_system_milliseconds(void)
{
	if ((!clock_thread_known || pthread_equal(pthread_self(), clock_thread)) && ++clock_reads > 100000)
		clock_extra++;
	return 1000UL + clock_frames * 100UL / 3UL + clock_extra;
}

/* (the units that read a clock of their own, which build.py's UNIT_RENAMES names: the loading screen's progress bar's GetTickCount, and the
scripted player's SDL_GetTicks before a game's clock runs) */
unsigned long game_digest_milliseconds(void)
{
	return __wrap_system_milliseconds();
}

unsigned long long game_digest_sdl_ticks(void)
{
	return __wrap_system_milliseconds();
}

/* ---------- the silent sound's clock, by the frames

With no sound device (HALO_NO_AUDIO) the platform's sound drains its voices in real time: a thread of dsound_sdl.c's mixes 10 ms of them and sleeps
10 ms (silent_clock_thread). Which sounds have ended by a frame then follows the machine's speed, and with them what the game does next with its
local random numbers, which the effects' particles draw from as well. Here that thread mixes only while the game thread waits at a frame's present,
as many 10 ms as the frames have reached at 30 a second (build.py renames dsound_sdl.c's pthread_create and clock_nanosleep to the two functions
below), so every frame sees the same sound. A mix that does not end in ten seconds is not waited for (with a line on the standard error). */

static pthread_mutex_t audio_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t audio_changed = PTHREAD_COND_INITIALIZER;
static unsigned long audio_done, audio_target;
static int audio_running;
static void *(*audio_main)(void *);

static void *audio_thread_main(void *parameter)
{
	/* (its first mix too waits for a present) */
	pthread_mutex_lock(&audio_lock);
	while (audio_done >= audio_target)
		pthread_cond_wait(&audio_changed, &audio_lock);
	pthread_mutex_unlock(&audio_lock);
	return audio_main(parameter);
}

int game_digest_audio_thread_create(pthread_t *thread, const pthread_attr_t *attributes, void *(*main)(void *), void *parameter)
{
	pthread_mutex_lock(&audio_lock);
	audio_running = 1;
	audio_main = main;
	pthread_mutex_unlock(&audio_lock);
	return pthread_create(thread, attributes, audio_thread_main, parameter);
}

/* (after each 10 ms mixed) */
int game_digest_audio_sleep(int clock, int flags, const void *until, void *left)
{
	(void)clock;
	(void)flags;
	(void)until;
	(void)left;
	pthread_mutex_lock(&audio_lock);
	audio_done++;
	pthread_cond_broadcast(&audio_changed);
	while (audio_done >= audio_target)
		pthread_cond_wait(&audio_changed, &audio_lock);
	pthread_mutex_unlock(&audio_lock);
	return 0;
}

static void audio_frame(void)
{
	static int given_up;
	unsigned long waits = 0;

	pthread_mutex_lock(&audio_lock);
	audio_target = clock_frames * 10UL / 3UL;
	pthread_cond_broadcast(&audio_changed);
	while (audio_running && !given_up && audio_done < audio_target)
	{
		struct timespec until;

		clock_gettime(CLOCK_REALTIME, &until);
		until.tv_sec += 1;
		if (pthread_cond_timedwait(&audio_changed, &audio_lock, &until) && ++waits >= 10)
		{
			fprintf(stderr, "game digest: the silent sound did not mix in ten seconds: it runs on its own from here\n");
			given_up = 1;
		}
	}
	pthread_mutex_unlock(&audio_lock);
}

/* ---------- who draws the local random numbers (GAME_DIGEST_RANDOM=1: random.txt)

The local random numbers (random_math.c's) are drawn by the sounds, the effects, the debug allocator's fill and more, all on the game thread; when two
runs' numbers part (frames.txt's local), this says where: each frame's callers of get_global_local_random_seed_address (build.py links them to this,
-Wl,--wrap), each with how many times it was called, by its return address and the three above it (llvm-symbolizer --obj=halo). */

unsigned long *__real_get_global_local_random_seed_address(void);

#define RANDOM_CALLERS 512
static struct
{
	int on;
	FILE *file;
	unsigned long count;
	void *callers[RANDOM_CALLERS][4];
	unsigned long calls[RANDOM_CALLERS];
} random_log = { -1 };

unsigned long *__wrap_get_global_local_random_seed_address(void)
{
	if (random_log.on)
	{
		void *callers[4] = { __builtin_return_address(0), __builtin_return_address(1), __builtin_return_address(2), __builtin_return_address(3) };
		unsigned long index;

		for (index = 0; index < random_log.count && memcmp(random_log.callers[index], callers, sizeof(callers)); index++)
			;
		if (index == random_log.count && random_log.count < RANDOM_CALLERS)
			memcpy(random_log.callers[random_log.count++], callers, sizeof(callers));
		if (index < random_log.count)
			random_log.calls[index]++;
	}
	return __real_get_global_local_random_seed_address();
}

static void random_frame(unsigned long frame, long tick);

/* ---------- the sink

A frame is the lines up to and including its present. Its digest is of its lines as replay_gx2.c writes them but for the frame's number (draw 812.40
is hashed as draw .40) and the texture cache's numbers of the images (sink_normalize), so that a frame of the game is the same whatever number of frames the menus took before it (the network test's lobby counts
seconds of the fixed step, not frames, and its loading is not a frame); its multiset digest is the sum of each line's hash with the draw's number in
the frame left out too, so that the same draws in another order give the same one. Each frame's line in frames.txt has both, the game's tick at its
present (-1 in the main menu, whose own map has a clock too, and before a game's clock runs), its counts, and what tells two runs apart before their
draws do: the game's random seeds (the simulation's and the local one), the 10 ms of sound mixed and the clock's moves past the frames. The summary's
game-digest is of the frames of the game's digests, in order: two runs whose game-digests are equal drew every frame of the game the same. */

/* the game's (source/game/game_time.c) */
unsigned char game_time_initialized(void);
long game_time_get(void);
/* (source/interface/ui_widget.c's) */
unsigned char main_menu_is_active(void);
/* (source/math/random_math.c's: the game's random numbers, the simulation's and the local ones the effects draw, in frames.txt so that a run whose
draws part from another's can be told whether its numbers did first) */
unsigned long get_random_seed(void);
unsigned long *get_global_local_random_seed_address(void);

static struct
{
	char directory[512];
	unsigned long frame_limit;
	long tick_limit;
	FILE *frames, *trace;
	long trace_first, trace_last; /* (the ticks of the frames traced; trace_first -2: every frame, the menus' too) */
	char *frame_text; /* (the frame's lines so far, for the trace) */
	size_t frame_text_used, frame_text_size;
	char line[8192];
	size_t used;
	uint64_t frame_hash, frame_multiset, run_hash, game_hash;
	unsigned long frame_lines, frame_draws, frame_count, lines, draws, game_frames;
	int finished;
} sink;

/* GAME_DIGEST_PASSES=1: the game's render passes (source/render/render.c's RENDER_PASS marks, named as port/wiiu/src/wiiu_render_passes.c names them) as
markers in the command streams, and for each frame a line of passes.txt: its tick, and for each pass the draws, the indices (or vertices) they hold, the
indexed draws, the decal draws, the state writes, the draw facts, the constant rows and the vertex formats the streams held between its mark and the next
(replay_gx2_totals). The markers are lines of the frame, so a digest of a run with this on is not the digest of one without: the measurement of what a
setting saves, not the parity check. */
/* port/linux/game/render_governor.h's counters (the glue is built without the game's headers) */
#define RENDER_GOVERNOR_COUNTS 8
extern unsigned long render_governor_counts[RENDER_GOVERNOR_COUNTS];

#define PASSES_MAX 14
static const char *const pass_names[PASSES_MAX] = { "visibility", "begin", "sky", "objects", "lightmaps", "shadows", "decals", "specular", "transparent",
	"effects", "detail", "hud", "end", "window_end" };
static struct
{
	int on;
	FILE *file;
	int current; /* the pass the streams are in (PASSES_MAX: before the first mark of the frame) */
	unsigned long at[REPLAY_TOTAL_COUNT];
	unsigned long sum[PASSES_MAX + 1][REPLAY_TOTAL_COUNT];
	unsigned long counts_at[RENDER_GOVERNOR_COUNTS];
} passes = { .current = PASSES_MAX };

static void passes_take(void)
{
	unsigned long now[REPLAY_TOTAL_COUNT];
	int kind;

	replay_gx2_totals(now);
	for (kind = 0; kind < REPLAY_TOTAL_COUNT; kind++)
	{
		passes.sum[passes.current][kind] += now[kind] - passes.at[kind];
		passes.at[kind] = now[kind];
	}
}

static void passes_marker(const char *text, size_t length)
{
	const char *colon = memchr(text, ':', length);
	char name[64];
	int pass;

	if (!colon || colon + 2 > text + length)
		return;
	snprintf(name, sizeof(name), "%.*s", (int)(text + length - (colon + 2)), colon + 2);
	passes_take();
	for (pass = 0; pass < PASSES_MAX; pass++)
		if (!strcmp(name, pass_names[pass]))
			passes.current = pass;
}

static void passes_present(long tick)
{
	int pass, kind;

	passes_take();
	passes.current = PASSES_MAX;
	fprintf(passes.file, "tick %ld", tick);
	for (pass = 0; pass <= PASSES_MAX; pass++)
	{
		fprintf(passes.file, " | %s", pass < PASSES_MAX ? pass_names[pass] : "other");
		for (kind = 0; kind < REPLAY_TOTAL_COUNT; kind++)
			fprintf(passes.file, " %lu", passes.sum[pass][kind]);
	}
	/* (and what the governor's counters in the game counted this frame: objects drawn and culled, shadows, decals and particles drawn and skipped) */
	fprintf(passes.file, " | game");
	for (kind = 0; kind < RENDER_GOVERNOR_COUNTS; kind++)
	{
		fprintf(passes.file, " %lu", render_governor_counts[kind] - passes.counts_at[kind]);
		passes.counts_at[kind] = render_governor_counts[kind];
	}
	fprintf(passes.file, "\n");
	memset(passes.sum, 0, sizeof(passes.sum));
}

static FILE *sink_open(const char *name)
{
	char path[640];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s", sink.directory, name);
	file = fopen(path, "w");
	if (!file)
	{
		fprintf(stderr, "game digest: cannot write %s: %s\n", path, strerror(errno));
		_exit(2);
	}
	return file;
}

static void random_frame(unsigned long frame, long tick)
{
	unsigned long index;

	if (random_log.on < 0)
	{
		const char *wanted = getenv("GAME_DIGEST_RANDOM");

		random_log.on = wanted && *wanted && strcmp(wanted, "0");
		if (random_log.on)
			random_log.file = sink_open("random.txt");
	}
	if (!random_log.on)
		return;
	for (index = 0; index < random_log.count; index++)
		if (random_log.calls[index])
		{
			fprintf(random_log.file, "frame %lu tick %ld %p %p %p %p %lu\n", frame, tick, random_log.callers[index][0], random_log.callers[index][1],
				random_log.callers[index][2], random_log.callers[index][3], random_log.calls[index]);
			random_log.calls[index] = 0;
		}
	fflush(random_log.file);
}

static void detail_flush(void);

static void sink_summary(const char *how)
{
	FILE *summary;

	if (sink.finished)
		return;
	sink.finished = 1;
	summary = sink_open("summary.txt");
	fprintf(summary, "game-digest %016llx\ndigest %016llx\nreplay-digest %016llx\nframes %lu\ngame-frames %lu\nlines %lu\ndraws %lu\nend %s\n",
		(unsigned long long)sink.game_hash, (unsigned long long)sink.run_hash, (unsigned long long)replay_gx2_digest(), sink.frame_count, sink.game_frames,
		sink.lines, sink.draws, how);
	fclose(summary);
	fflush(sink.frames);
	if (passes.file)
		fflush(passes.file);
	if (sink.trace)
		fflush(sink.trace);
	if (random_log.file)
		fflush(random_log.file);
	detail_flush();
	fprintf(stderr, "game digest: %lu frames (%lu of the game), game digest %016llx (%s)\n", sink.frame_count, sink.game_frames,
		(unsigned long long)sink.game_hash, how);
}

/* the line without its frame's number (and with ordinal, without the draw's number in the frame too), and without the texture cache's numbers of
its images ("i53:" is hashed as "i:", followed by the image's contents and sampler as before): which number an image gets follows the order the
images were first made in, which the menus' first frames do not always keep, nor need a change that draws the same pictures */
static size_t sink_normalize(const char *text, size_t length, char *out, int ordinal)
{
	static const char *const kinds[] = { "draw ", "clear ", "present ", "marker ", "visibility begin ", "visibility end " };
	size_t at = 0, used = 0, kind;
	size_t copied;

	for (kind = 0; kind < sizeof(kinds) / sizeof(kinds[0]); kind++)
	{
		size_t size = strlen(kinds[kind]);

		if (length >= size && !memcmp(text, kinds[kind], size))
		{
			memcpy(out, text, size);
			used = at = size;
			while (at < length && text[at] >= '0' && text[at] <= '9')
				at++;
			if (ordinal && at < length && text[at] == '.')
			{
				at++;
				while (at < length && text[at] >= '0' && text[at] <= '9')
					at++;
			}
			break;
		}
	}
	for (copied = used; at < length; at++)
	{
		out[copied++] = text[at];
		if (text[at] == ' ' && at + 2 < length && text[at + 1] == 'i' && text[at + 2] >= '0' && text[at + 2] <= '9')
		{
			size_t after = at + 2;

			while (after < length && text[after] >= '0' && text[after] <= '9')
				after++;
			if (after < length && text[after] == ':')
			{
				out[copied++] = 'i';
				at = after - 1;
			}
		}
	}
	return copied;
}

static void sink_line(const char *text, size_t length)
{
	static char normal[8192];
	size_t size;

	sink.run_hash = replay_hash(sink.run_hash, text, length);
	sink.run_hash = replay_hash(sink.run_hash, "\n", 1);
	size = sink_normalize(text, length, normal, 0);
	sink.frame_hash = replay_hash(sink.frame_hash, normal, size);
	sink.frame_hash = replay_hash(sink.frame_hash, "\n", 1);
	size = sink_normalize(text, length, normal, 1);
	sink.frame_multiset += replay_hash(REPLAY_HASH_INIT, normal, size);
	sink.frame_lines++;
	sink.lines++;
	if (length > 5 && !memcmp(text, "draw ", 5))
	{
		sink.frame_draws++;
		sink.draws++;
	}
	if (passes.on && length > 7 && !memcmp(text, "marker ", 7))
		passes_marker(text, length);
	if (sink.trace)
	{
		if (sink.frame_text_used + length + 1 > sink.frame_text_size)
		{
			sink.frame_text_size = (sink.frame_text_used + length + 1) * 2;
			sink.frame_text = realloc(sink.frame_text, sink.frame_text_size);
			if (!sink.frame_text)
			{
				fprintf(stderr, "game digest: out of memory for the trace\n");
				_exit(2);
			}
		}
		memcpy(sink.frame_text + sink.frame_text_used, text, length);
		sink.frame_text[sink.frame_text_used + length] = '\n';
		sink.frame_text_used += length + 1;
	}
	if (length > 8 && !memcmp(text, "present ", 8))
	{
		const long tick = !main_menu_is_active() && game_time_initialized() ? game_time_get() : -1;

		fprintf(sink.frames, "frame %lu tick %ld digest %016llx multiset %016llx lines %lu draws %lu random %08lx local %08lx sound %lu clock %lu\n",
			sink.frame_count, tick, (unsigned long long)sink.frame_hash, (unsigned long long)sink.frame_multiset, sink.frame_lines, sink.frame_draws,
			get_random_seed(), *__real_get_global_local_random_seed_address(), audio_done, clock_extra);
		random_frame(sink.frame_count, tick);
		if (passes.on)
			passes_present(tick);
		if (sink.trace)
		{
			if (sink.trace_first == -2 || (tick >= 0 && tick >= sink.trace_first && tick <= sink.trace_last))
			{
				fwrite(sink.frame_text, 1, sink.frame_text_used, sink.trace);
				fprintf(sink.trace, "# frame %lu tick %ld\n", sink.frame_count, tick);
			}
			sink.frame_text_used = 0;
		}
		sink.frame_count++;
		clock_frame();
		if (tick >= 0)
		{
			sink.game_frames++;
			sink.game_hash = replay_hash(sink.game_hash, &sink.frame_hash, sizeof(sink.frame_hash));
		}
		sink.frame_hash = REPLAY_HASH_INIT;
		sink.frame_multiset = 0;
		sink.frame_lines = sink.frame_draws = 0;
		if ((sink.frame_limit && sink.frame_count >= sink.frame_limit) || (sink.tick_limit && tick >= sink.tick_limit))
		{
			/* (inside the reader's fprintf: no exit(), whose flush of every stream would take this one's lock again) */
			sink_summary("limit");
			fclose(sink.frames);
			if (sink.trace)
				fclose(sink.trace);
			_exit(0);
		}
	}
}

static ssize_t sink_write(void *cookie, const char *data, size_t size)
{
	size_t index;

	(void)cookie;
	for (index = 0; index < size; index++)
	{
		if (data[index] == '\n')
		{
			sink_line(sink.line, sink.used);
			sink.used = 0;
		}
		else if (sink.used < sizeof(sink.line))
			sink.line[sink.used++] = data[index];
	}
	return (ssize_t)size;
}

static void sink_at_exit(void)
{
	sink_summary("the game ended before the limit");
}

/* GAME_DIGEST_DIR (default .), GAME_DIGEST_TICKS (stop at the first frame of the game at or past this tick), GAME_DIGEST_FRAMES (stop after this many
presents; 100 if neither is given), GAME_DIGEST_TRACE (1: every frame's lines to trace.txt, each frame followed by a "# frame N tick T" line;
FIRST-LAST: only the frames of the game at those ticks), GAME_DIGEST_DETAIL and GAME_DIGEST_RANDOM (their sections) */
__attribute__((constructor)) static void sink_start(void)
{
	const char *directory = getenv("GAME_DIGEST_DIR"), *frames = getenv("GAME_DIGEST_FRAMES"), *ticks = getenv("GAME_DIGEST_TICKS"),
		*trace = getenv("GAME_DIGEST_TRACE");
	cookie_io_functions_t functions = { .write = sink_write };
	FILE *lines;

	snprintf(sink.directory, sizeof(sink.directory), "%s", directory && *directory ? directory : ".");
	posix_make_directory(sink.directory);
	sink.frame_limit = frames && *frames ? strtoul(frames, NULL, 0) : 0;
	sink.tick_limit = ticks && *ticks ? strtol(ticks, NULL, 0) : 0;
	if (!sink.frame_limit && !sink.tick_limit)
		sink.frame_limit = 100;
	sink.frames = sink_open("frames.txt");
	if (getenv("GAME_DIGEST_PASSES") && *getenv("GAME_DIGEST_PASSES") && strcmp(getenv("GAME_DIGEST_PASSES"), "0"))
	{
		passes.on = 1;
		passes.file = sink_open("passes.txt");
	}
	if (trace && *trace && strcmp(trace, "0"))
	{
		char *end;

		sink.trace = sink_open("trace.txt");
		sink.trace_first = -2;
		if (strchr(trace, '-'))
		{
			sink.trace_first = strtol(trace, &end, 0);
			sink.trace_last = strtol(end + 1, NULL, 0);
		}
	}
	sink.frame_hash = sink.run_hash = sink.game_hash = REPLAY_HASH_INIT;
	lines = fopencookie(NULL, "w", functions);
	if (!lines)
	{
		fprintf(stderr, "game digest: fopencookie failed\n");
		_exit(2);
	}
	/* (unbuffered: each line reaches the sink as the reader writes it, so a frame's end is seen at its present) */
	setvbuf(lines, NULL, _IONBF, 0);
	replay_gx2_start(0, 0, 1, lines);
	atexit(sink_at_exit);
}

/* ---------- the detail of a frame's draws (GAME_DIGEST_DETAIL=TICK: detail.txt)

A draw's line has the hash of all 192 constant rows and of the whole vertex format; when two runs' lines differ only there, this says which rows
and fields: at each draw of the frames of the game at TICK (or with fN, of the run's frame N, the menus' included), the rows and the format's fields
that changed since the last draw written, with their values, and a stream in the window's first 256 bytes; fN-M for the frames N to M. The streams are
read here before replay_gx2.c reads them (build.py links the layer's host_gx2_submit to this, -Wl,--wrap). */

void __real_host_gx2_submit(unsigned int commands, unsigned int size);

static struct
{
	long tick, frame, last_frame;
	unsigned long frames;
	FILE *file;
	float constants[192][4], written[192][4];
	struct dk_vertex_format format, format_written;
	unsigned long draws;
	unsigned char set[192]; /* (rows a CONSTANTS command of this frame wrote) */
	int format_set;
} detail = { .tick = -2 };

static void detail_flush(void)
{
	if (detail.file)
		fflush(detail.file);
}

static void detail_draw(long tick, const struct dk_command_draw *draw)
{
	unsigned row, field;
	const uint32_t *now = (const uint32_t *)&detail.format, *before = (const uint32_t *)&detail.format_written;

	fprintf(detail.file, "frame %lu tick %ld draw %lu\n", detail.frames, tick, detail.draws);
	for (row = 0; row < 192; row++)
		if (memcmp(detail.constants[row], detail.written[row], sizeof(detail.constants[row])))
		{
			const uint32_t *bits = (const uint32_t *)detail.constants[row];

			fprintf(detail.file, "  c%u %.9g %.9g %.9g %.9g (%08x %08x %08x %08x)%s\n", row, (double)detail.constants[row][0],
				(double)detail.constants[row][1], (double)detail.constants[row][2], (double)detail.constants[row][3], (unsigned)bits[0], (unsigned)bits[1],
				(unsigned)bits[2], (unsigned)bits[3], detail.set[row] ? "" : " (from an earlier frame)");
		}
	for (field = 0; field < sizeof(detail.format) / 4; field++)
		if (now[field] != before[field])
			fprintf(detail.file, "  format word %u %08x (%.9g)%s\n", field, (unsigned)now[field], (double)((const float *)now)[field],
				detail.format_set ? "" : " (from an earlier frame)");
	/* (a small draw's vertices from memory, word by word: the first stream's first 256 bytes) */
	if (draw->stream_count && !draw->inline_bytes && draw->streams[0][0] && platform_is_contiguous((const void *)(uintptr_t)draw->streams[0][0]))
	{
		const uint32_t *words = (const uint32_t *)(uintptr_t)draw->streams[0][0];
		unsigned long word, count = draw->streams[0][1] / 4 < 64 ? draw->streams[0][1] / 4 : 64;

		fprintf(detail.file, "  stream 0:");
		for (word = 0; word < count; word++)
			fprintf(detail.file, " %08x", (unsigned)words[word]);
		fprintf(detail.file, "\n");
	}
	memcpy(detail.written, detail.constants, sizeof(detail.written));
	detail.format_written = detail.format;
}

void __wrap_host_gx2_submit(unsigned int commands, unsigned int size)
{
	const unsigned char *at = (const unsigned char *)(uintptr_t)commands, *end = at + size;
	long tick = -1;

	if (detail.tick == -2)
	{
		const char *wanted = getenv("GAME_DIGEST_DETAIL");

		detail.tick = detail.frame = -1;
		if (wanted && *wanted == 'f')
		{
			char *end;

			detail.frame = detail.last_frame = strtol(wanted + 1, &end, 0);
			if (*end == '-')
				detail.last_frame = strtol(end + 1, NULL, 0);
		}
		else if (wanted && *wanted)
			detail.tick = strtol(wanted, NULL, 0);
		if (detail.tick >= 0 || detail.frame >= 0)
			detail.file = sink_open("detail.txt");
	}
	if (detail.file)
		tick = !main_menu_is_active() && game_time_initialized() ? game_time_get() : -1;
	while (detail.file && at + sizeof(struct dk_command_header) <= end)
	{
		const struct dk_command_header *header = (const struct dk_command_header *)at;

		if (header->size < sizeof(*header) || at + header->size > end)
			break;
		if (header->type == DK_COMMAND_CONSTANTS)
		{
			const struct dk_command_constants *constants = (const struct dk_command_constants *)header;

			if (constants->first < 192 && constants->count && constants->first + constants->count <= 192)
			{
				memcpy(detail.constants[constants->first], constants->data, constants->count * 4 * sizeof(float));
				memset(detail.set + constants->first, 1, constants->count);
			}
		}
		else if (header->type == DK_COMMAND_VERTEX_FORMAT)
		{
			detail.format = ((const struct dk_command_vertex_format *)header)->format;
			detail.format_set = 1;
		}
		else if (header->type == DK_COMMAND_DRAW)
		{
			detail.draws++;
			if ((detail.tick >= 0 && tick == detail.tick) || (detail.frame >= 0 && (long)detail.frames >= detail.frame && (long)detail.frames <= detail.last_frame))
				detail_draw(tick, (const struct dk_command_draw *)header);
		}
		else if (header->type == DK_COMMAND_PRESENT)
		{
			detail.draws = 0;
			detail.frames++;
			memset(detail.set, 0, sizeof(detail.set));
			detail.format_set = 0;
		}
		at += header->size;
	}
	__real_host_gx2_submit(commands, size);
}

/* ---------- the maps' loading, finished before the game draws on

A map is copied to the cache by a thread of its own (cache_files_decompress_windows.c) while the game thread draws the loading screen, a frame for each
look at its status: how many frames that is follows the time the copy takes, and with them the clocks that run by the frames (the shaders' animations,
the fog's wind), which every frame of the game after it then shows. So a look at the copy's status waits here for the copy to end (build.py links the
game's calls to this, -Wl,--wrap=cache_copy_get_status): each loading is one frame, at a progress of 1, and the frames are the same on every run. A copy
that does not end in two minutes is left to go on as it would, with a line on the standard error (the frames then follow the time again). */

short __real_cache_copy_get_status(float *progress);

short __wrap_cache_copy_get_status(float *progress)
{
	/* (cache_files_decompress_windows.h's _cache_copy_in_progress and _cache_copy_finished) */
	enum { copy_in_progress = 3, copy_finished = 4 };
	unsigned long waited;
	short status = __real_cache_copy_get_status(progress);

	for (waited = 0; status == copy_in_progress && waited < 120000; waited++)
	{
		usleep(1000);
		status = __real_cache_copy_get_status(progress);
	}
	if (status == copy_in_progress)
		fprintf(stderr, "game digest: a map's copy did not end in two minutes: the frames follow the time from here\n");
	else if (status == copy_finished)
		*progress = 1.0f;
	return status;
}

/* ---------- the map's reads, finished before the game goes on

The game reads a bitmap's texels and a sound's samples from the map when they are first wanted, without waiting (cache_files_windows.c's cache_file_read
on its own thread): a bitmap whose read has not ended is not drawn that frame, or is drawn as another, a sound that is not in yet starts a frame
later, and which frame a read ends in follows the time the reads take. The sounds then take the game's local random numbers in another order, and
the effects' particles draw from the same numbers; and the game's own waits for a read look at the clock as they spin (xbox_texture_cache.c idles
the sound every 132 ms of it), which would move the clock of the frames by the reads' time. So here every read is waited for until its thread ends
it, and every ask for a bitmap or a sound that may load waits for it to be in (build.py links the callers of the three to these, -Wl,--wrap): what
is drawn and played is the same from the first frame that wants it, on every run, and no wait of the game's spins. (An ask that may not load is
left as it is.) */

short __real_cache_file_read(long tag_index, long offset, long size, void *buffer, unsigned char *completion, unsigned char blocking);

static int stream_free(void);

short __wrap_cache_file_read(long tag_index, long offset, long size, void *buffer, unsigned char *completion, unsigned char blocking)
{
	short request = __real_cache_file_read(tag_index, offset, size, buffer, completion, blocking);

	unsigned long spins = 0;

	if (stream_free())
		return request;

	/* (the read's thread sets the completion flag, TRUE or failed, when it ends: waited for here without the game's clock, yielding to it, and
	sleeping only for a read that takes long, as a sleep under qemu is a millisecond or more) */
	while (!*(volatile unsigned char *)completion)
	{
		if (++spins < 100000)
			sched_yield();
		else
			usleep(100);
	}
	return request;
}

void *__real__texture_cache_bitmap_get_hardware_format(void *bitmap, unsigned char block, unsigned char load);
void *__real_texture_cache_bitmap_get_hardware_format_streaming(void *bitmap);

/* GAME_DIGEST_STREAM_FREE=1: the asks go through as the game makes them (a test of texture streaming's own path: with HALO_STREAM=1 and HALO_STREAM_DELAY=ms, bitmaps are
drawn from their tails while the rest is read; what is drawn then follows the time, so no digest means anything) */
static int stream_free(void)
{
	static int free_asks = -1;

	if (free_asks < 0)
	{
		const char *value = getenv("GAME_DIGEST_STREAM_FREE");

		free_asks = value && *value == '1';
	}
	return free_asks;
}

/* texture streaming (halo/stream.txt, HALO_STREAM here): the renderer's ask draws a bitmap from its tail while the rest is read, which is the time of the read; here the whole of
it is waited for, as without streaming, so that what is drawn does not follow the time. The bitmap's read is still cut in two and its tail read first (that is in the cache, not
in the ask), so the digests of a run with HALO_STREAM=1 and of one without are the proof that the two reads put the bytes the one read put */
void *__wrap_texture_cache_bitmap_get_hardware_format_streaming(void *bitmap)
{
	if (stream_free())
		return __real_texture_cache_bitmap_get_hardware_format_streaming(bitmap);
	return __real__texture_cache_bitmap_get_hardware_format(bitmap, 1, 1);
}

void *__wrap__texture_cache_bitmap_get_hardware_format(void *bitmap, unsigned char block, unsigned char load)
{
	return stream_free() ? __real__texture_cache_bitmap_get_hardware_format(bitmap, block, load) : __real__texture_cache_bitmap_get_hardware_format(bitmap, load ? 1 : block, load);
}

unsigned char __real__sound_cache_sound_request(void *sound, unsigned char block, unsigned char load, unsigned char reference);

unsigned char __wrap__sound_cache_sound_request(void *sound, unsigned char block, unsigned char load, unsigned char reference)
{
	return __real__sound_cache_sound_request(sound, load ? 1 : block, load, reference);
}

/* ---------- the local random numbers, the same at each map's start

The menus' first frames are not all alike from run to run (a menu sound sometimes plays in the fifth: the widgets' handling of the scripted player's
first stick events, which no clock of the frames has settled), and a sound draws a local random number, which then moves every later draw of them:
the system's nickname (network_game_ui.c's network_game_get_random_player_name), drawn when the network game is made, and the effects' particles
in the game. So here the nickname is drawn from a seed of its own, debug.random_seed's, with the local numbers left as they were, and each map's
start sets the local seed to debug.random_seed again (build.py links the game's calls of the two to these, -Wl,--wrap), as the game's start-up
does (random_math.c): what a game draws does not depend on what the menus did before it. */

const unsigned short *__real_network_game_get_random_player_name(void);

const unsigned short *__wrap_network_game_get_random_player_name(void)
{
	unsigned long *seed = __real_get_global_local_random_seed_address(), kept = *seed;
	const unsigned short *name;

	*seed = (unsigned long)config_integer("debug.random_seed");
	name = __real_network_game_get_random_player_name();
	*seed = kept;
	return name;
}

void __real_game_initialize_for_new_map(void);

void __wrap_game_initialize_for_new_map(void)
{
	*__real_get_global_local_random_seed_address() = (unsigned long)config_integer("debug.random_seed");
	__real_game_initialize_for_new_map();
}

/* ---------- what the game hands the GPU that it never set

A draw's line hashes all 192 constant rows, and the game leaves some lanes it sends unset: rasterizer_set_model_lighting (rasterizer_xbox.c) sends
its lighting constants from a structure on the stack whose fourth lanes the lights do not all set (the shaders do not read them). Those lanes hold
what the stack held, pointers among it, so a frame's digest would move with the program's layout and with whatever ran before on the stack: any
change of the code, and the native packages are such changes, would change frames whose pictures it keeps. So the stack below the game's call is
cleared first (build.py links the callers of the function to this, -Wl,--wrap), and the lanes are 0 on every run of every build.

With GAME_DIGEST_POISON (build.py --poison: the test of this, tools/test_wiiu_game_digest.py) the stack below each frame's rendering is filled with
0xa5 bytes first, and the program's data and heap are laid out otherwise (game_digest_padding.c, linked first): a build that draws every frame of
the game as the plain build does then hands the GPU nothing else it did not set, as far as these runs reach; one that does not names the next
call to clear here. */

/* the 16 KB of stack below the caller's frame, where the next call's locals will be, filled with fill */
static void __attribute__((noinline)) stack_fill(int fill)
{
	volatile unsigned char area[16384];

	memset((void *)area, fill, sizeof(area));
	__asm__ volatile("" : : "r"(area) : "memory");
}

void __real_rasterizer_set_model_lighting(const void *lighting);

void __wrap_rasterizer_set_model_lighting(const void *lighting)
{
	stack_fill(0);
	__real_rasterizer_set_model_lighting(lighting);
}

/* the scene (game_digest_scene.c) gives a frame its fixed camera before it is drawn, when GAME_DIGEST_SCENE says so; with --poison the frame's
rendering has its stack filled first */
void game_digest_scene_render_frame(void *windows, short window_count);

void __real_render_frame(void *windows, short window_count, const void *screenshot_page_index, const void *screenshot_index, void *bitmap,
	float time_delta_since_tick);

void __wrap_render_frame(void *windows, short window_count, const void *screenshot_page_index, const void *screenshot_index, void *bitmap,
	float time_delta_since_tick)
{
#ifdef GAME_DIGEST_POISON
	stack_fill(0xa5);
#endif
	game_digest_scene_render_frame(windows, window_count);
	__real_render_frame(windows, window_count, screenshot_page_index, screenshot_index, bitmap, time_delta_since_tick);
}

/* ---------- the GX2 thread's and the console platform's answers (replay_world.c's, and the console's defaults) */

/* the time base: a hundred ticks a reading, the same on every run */
static OSTime system_time = 1000000;

OSTime OSGetSystemTime(void)
{
	system_time += 100;
	return system_time;
}

/* the TV at 720 lines, as the console runs (the game draws 852x480 at 1280x720) */
BOOL game_digest_screen_mode(long *width, long *height)
{
	*width = 1280;
	*height = 720;
	return 1;
}

BOOL game_digest_video_initialize(unsigned long width, unsigned long height)
{
	(void)width;
	(void)height;
	return 1;
}

void game_digest_video_swap(void)
{
}

/* wiiu_gx2.c's: dynamic resolution asks no step */
unsigned int host_gx2_drs_lines(void)
{
	return 0;
}

void wiiu_foreground_gate(int is_game_thread)
{
	(void)is_game_thread;
}

void wiiu_thread_off_game_core(void)
{
}

int wiiu_input_touch(float *x, float *y)
{
	*x = *y = 0.0f;
	return 0;
}

/* the shader cache's guest half (gx2_shaders.c): the keys a draw needs are the GX2 thread's to find */
void dk_shader_program_note(uint64_t program_hash, const uint32_t *words, unsigned long count)
{
	(void)program_hash;
	(void)words;
	(void)count;
}

void dk_shader_map_loaded(uint32_t map_hash)
{
	(void)map_hash;
}

void dk_shader_start(void)
{
}

void dk_shader_frame(void)
{
}

char *nv2a_dk_vertex_shader_to_glsl(const uint32_t *instructions, unsigned long instruction_count, unsigned long packed_attribute_mask)
{
	(void)instructions;
	(void)instruction_count;
	(void)packed_attribute_mask;
	return calloc(1, 1);
}

char *nv2a_dk_pixel_shader_to_glsl(const struct nv2a_pixel_shader_key *key)
{
	(void)key;
	return calloc(1, 1);
}

/* ---------- the game's HALO_WIIU calls into the console's platform */

/* wiiu_missing_keys.c: the shader-file worker's gate (none here) */
void wiiu_missing_keys_gameplay(int active)
{
	(void)active;
}

/* wiiu_shell.c: the red screen; here, the log and an end */
void wiiu_game_halted(void)
{
	fprintf(stderr, "game digest: the game halted\n");
	exit(3);
}

/* wiiu_debug.c: the debug server's scripts (none) */
void wiiu_debug_console_run(void)
{
}

/* wiiu_shell.c's gate of the tick thread (port/linux/game/tick_owners.c): there is none here */
void wiiu_tick_gate_register(int registered)
{
	(void)registered;
}

void wiiu_tick_gate(void)
{
}

/* the tick thread's calls (source/main/main.c): the FPU, the black box, the HOME gate and the log line are the console's */
void wiiu_fpu_setup(void)
{
}

void wiiu_tag_thread(const char *name)
{
	(void)name;
}

int wiiu_tick_thread_priority(void)
{
	return 16;
}

void wiiu_tick_thread_gate(void)
{
}

void wiiu_study_tick_thread_report(void)
{
}

void wiiu_study_tick_thread_job_report(void)
{
}

/* the console's tick thread is an OSThread on core 0; here none is made (0), and tick_overlap.c makes a pthread */
int wiiu_tick_thread_create(void *(*entry)(void *), unsigned long stack_bytes)
{
	(void)entry;
	(void)stack_bytes;
	return 0;
}

/* wiiu_gx2.c: the detailed overlay's field for the governor's step (render_governor.c calls it whenever the step changes); no overlay here */
void wiiu_overlay_governor(int step, int max_step);

void wiiu_overlay_governor(int step, int max_step)
{
	(void)step;
	(void)max_step;
}

/* wiiu_gpu_offload.c: halo/gpu_offload.txt's bits; here HALO_GPU_OFFLOAD (a number) in the environment, 0 if none (GAME_DIGEST_ENV="HALO_GPU_OFFLOAD=1") */
unsigned wiiu_gpu_offload_bits(void);

unsigned wiiu_gpu_offload_bits(void)
{
	const char *value = getenv("HALO_GPU_OFFLOAD");
	const long bits = value && *value ? strtol(value, NULL, 0) : 0;

	return bits > 0 ? (unsigned)bits & 7u : 0;
}

/* wiiu_study.c: a study switch's number from a file on the card; here from the environment (tick_thread.txt: HALO_TICK_THREAD,
tick_overlap.txt: HALO_TICK_OVERLAP, startup.txt: HALO_STARTUP, stream.txt: HALO_STREAM, hotpaths.txt: HALO_HOTPATHS), so that a run can have the ticks on a thread of their own, and overlapped */
long wiiu_study_number(const char *name, long fallback)
{
	const char *variable = !strcmp(name, "tick_thread.txt") ? "HALO_TICK_THREAD" : !strcmp(name, "tick_overlap.txt") ? "HALO_TICK_OVERLAP" :
		!strcmp(name, "tick_overlap_stall.txt") ? "HALO_TICK_OVERLAP_STALL" : !strcmp(name, "tick_overlap_serialize.txt") ? "HALO_TICK_OVERLAP_SERIALIZE" :
		!strcmp(name, "tick_overlap_jitter.txt") ? "HALO_TICK_OVERLAP_JITTER" : !strcmp(name, "tick_overlap_force_serial.txt") ? "HALO_TICK_OVERLAP_FORCE_SERIAL" :
		!strcmp(name, "startup.txt") ? "HALO_STARTUP" :
		!strcmp(name, "stream.txt") ? "HALO_STREAM" : !strcmp(name, "hotpaths.txt") ? "HALO_HOTPATHS" :
		!strcmp(name, "stream_delay.txt") ? "HALO_STREAM_DELAY" : NULL;
	const char *value;

	/* (the fall back to the serial loop when the tick thread is late is on by default on the console and off here, where qemu is slow: HALO_TICK_OVERLAP_LATE=1) */
	if (!strcmp(name, "tick_overlap_late.txt"))
		variable = "HALO_TICK_OVERLAP_LATE", fallback = 0;
	value = variable ? getenv(variable) : NULL;
	return value && *value ? strtol(value, NULL, 10) : fallback;
}

/* wiiu_shell.c's: whether the tick thread waits for a job and whether the game thread waits for it (the HOME gate; none here), the core a thread runs
on (the game thread, the process's first, is "core 1" and any other "core 0": the tick snapshot's view belongs to the game thread's), and the time */
void wiiu_tick_idle_set(int idle)
{
	(void)idle;
}

void wiiu_game_waiting_set(int waiting)
{
	(void)waiting;
}

/* the calling thread's stack (the game thread's: the tick snapshot's view belongs to it, tick_stack.h) */
void wiiu_thread_stack_range(unsigned long *low, unsigned long *size)
{
	pthread_attr_t attributes;
	void *address = NULL;
	size_t length = 0;

	*low = *size = 0;
	if (pthread_getattr_np(pthread_self(), &attributes) == 0)
	{
		if (pthread_attr_getstack(&attributes, &address, &length) == 0)
		{
			*low = (unsigned long)(uintptr_t)address;
			*size = (unsigned long)length;
		}
		pthread_attr_destroy(&attributes);
	}
}

unsigned long long wiiu_now_ticks(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long long)now.tv_sec * 1000000ULL + (unsigned long long)now.tv_nsec / 1000ULL;
}

unsigned long wiiu_ticks_per_second(void)
{
	return 1000000UL;
}

/* wiiu_perfmon.c, wiiu_shell.c: the performance counters (off here), and the cores */
int wiiu_perfmon_set(void)
{
	return 0;
}

void wiiu_perfmon_start(void)
{
}

void wiiu_perfmon_read(unsigned long values[4])
{
	values[0] = values[1] = values[2] = values[3] = 0;
}

const char *wiiu_perfmon_name(int counter)
{
	(void)counter;
	return "";
}

unsigned wiiu_perfmon_core(void)
{
	return 0;
}

void wiiu_tick_thread_move(int to_the_calling_core)
{
	(void)to_the_calling_core;
}

unsigned wiiu_core_here(void)
{
	return 1;
}

unsigned wiiu_tick_thread_core_set(void)
{
	return 0;
}

long long wiiu_tick_begin(void)
{
	return 0;
}

/* wiiu_crash.c: the misaligned accesses the handler took on this core (the tick snapshot's timers) */
uint32_t wiiu_crash_fixups_here(void)
{
	return 0;
}

/* gx2_shaders.c (the console's shader pack): none is read here */
void dk_shader_pack_prefetch(void)
{
}

/* wiiu_shell.c: the log's clock in milliseconds (startup_timeline.c's marks); here the time since the first read, which a run that is digested
does not look at (it only says in the log where the start-up's time went) */
unsigned long wiiu_shell_ms(void)
{
	static struct timespec first;
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	if (!first.tv_sec && !first.tv_nsec)
		first = now;
	return (unsigned long)((now.tv_sec - first.tv_sec) * 1000L + (now.tv_nsec - first.tv_nsec) / 1000000L);
}

/* wiiu_tick_parts.c: the console's timing of the parts of a tick */
long long wiiu_tick_part_now(void)
{
	return 0;
}

void wiiu_tick_part(const char *name, long long started)
{
	(void)name;
	(void)started;
}

void wiiu_tick_done(long long started)
{
	(void)started;
}

/* The frame work the dynamic quality governor (port/linux/game/render_governor.c) is told: the host's clock is qemu's and says nothing about the console, so
with GAME_DIGEST_FAKE_WORK="FIRST-LAST:MS,FIRST-LAST:MS" (game ticks; 12 ms outside them) each frame of the game is told that many milliseconds of work, 40% of it
ticks, on a clock that advances by it (not under 16.7 ms: a refresh), and the governor decides the steps by them. Without it the governor is told what the layer measured. */
void __real_render_governor_frame(double work_ms, double tick_ms, double render_ms, double now_ms);

void __wrap_render_governor_frame(double work_ms, double tick_ms, double render_ms, double now_ms)
{
	static const char *schedule;
	static int parsed;
	static double clock_ms = 1000.0;
	long tick = !main_menu_is_active() && game_time_initialized() ? game_time_get() : -1;

	if (!parsed)
	{
		parsed = 1;
		schedule = getenv("GAME_DIGEST_FAKE_WORK");
		if (schedule && !*schedule)
			schedule = NULL;
	}
	if (schedule)
	{
		const char *at = schedule;

		work_ms = 12.0;
		while (*at)
		{
			char *end;
			long first = strtol(at, &end, 10), last = strtol(*end == '-' ? end + 1 : end, &end, 10);
			double ms = *end == ':' ? strtod(end + 1, &end) : 12.0;

			if (tick >= first && tick <= last)
				work_ms = ms;
			at = *end == ',' ? end + 1 : end;
			if (end == at && *end)
				break;
		}
		tick_ms = work_ms * 0.4;
		render_ms = work_ms * 0.6;
		clock_ms += work_ms > 16.7 ? work_ms : 16.7;
		now_ms = clock_ms;
	}
	__real_render_governor_frame(work_ms, tick_ms, render_ms, now_ms);
}

/* wiiu_study.c's: halo/governor.txt, the dynamic quality's settings (port/linux/game/render_governor.c); here HALO_GOVERNOR's text, so that a run can hold
a step ("step 3") or tune the ladder, and the governor's own decisions are tested on made-up frame times (they are not the host's: qemu's clock) */
int render_governor_settings_text(char *text, unsigned long size)
{
	const char *value = getenv("HALO_GOVERNOR");

	if (!value || !*value)
		return 0;
	snprintf(text, size, "%s", value);
	return 1;
}

/* wiiu_render_passes.c's: the passes' milliseconds, which the host does not time */
unsigned long render_governor_pass_milliseconds(double milliseconds[13])
{
	memset(milliseconds, 0, 13 * sizeof(double));
	return 0;
}

/* wiiu_render_passes.c and d3d8_gx2.c's frame parts: the console's timing log */
void halo_render_marker(char const *name);

void wiiu_render_pass(int pass)
{
	if (passes.on && pass >= 0 && pass < PASSES_MAX)
		halo_render_marker(pass_names[pass]);
}

/* wiiu_shell.c: debug.txt's lines, written by a thread of their own there; here at once */
void wiiu_error_file_text(char const *text)
{
	fputs(text, stderr);
}

/* xinput_wiiu.c: the face buttons by their places (the console's default) */
int wiiu_face_buttons_by_label(void)
{
	return 0;
}

/* wiiu_input.c: no controllers merged */
int wiiu_input_controllers_merged(void)
{
	return 0;
}

/* wiiu_shell.c: a folder on the card */
int wiiu_directory_exists(const char *path)
{
	void *directory = posix_directory_open(path);

	if (!directory)
		return 0;
	posix_directory_close(directory);
	return 1;
}

/* ---------- the Linux window's OpenGL (sdl_platform.c's platform_video_initialize, which the Wii U's layer does not call: build.py renames
its call to game_digest_video_initialize) */

static void __attribute__((noreturn)) game_digest_missing(const char *name)
{
	fprintf(stderr, "game digest: %s called: the OpenGL renderer is not in this build\n", name);
	_exit(4);
}

int gl_functions_load(void)
{
	game_digest_missing("gl_functions_load");
}

/* (gl_functions.c's pointer to glGetString, which gl_functions_load would have set) */
void (*halo_glGetString)();
