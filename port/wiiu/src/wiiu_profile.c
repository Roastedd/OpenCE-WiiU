/*
WIIU_PROFILE.C

A sampling profiler for the game's thread, for finding where a frame's time goes (the console has no profiler we can use). A thread on the game's core, at a priority above
the game's, wakes every millisecond; the game thread is then stopped by the system and its context holds the exact address it was at (srr0), which is counted. Every ten
seconds the busiest 16-byte pieces of code are logged ("PROF pc=0x... n=..."); tools/wiiu/profile_report.py groups them by function with the game's ELF. System-code samples
may be active library work or waiting; the caller and independent wait accounting help distinguish them. Only the leading 200 entries are printed.

It runs only if halo/profile.txt exists (its first number: seconds between reports, 10 if none). An optional second number of 1 captures the complete table through one
owned buffer and a low-priority writer on core 0. The sampler never waits for that worker or writes a file. Normal text-only profiling creates no writer or snapshot buffer.
Snapshots are diagnostic work with measurable copy/I/O overhead, not a zero-overhead performance mode.
*/

#include "wiiu_shell.h"

#ifndef WIIU_PROFILE_SNAPSHOT_TEST
#include <coreinit/core.h>
#include <coreinit/memdefaultheap.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TABLE_SIZE 8192
#define REPORT_LINES 200

struct profile_record
{
	uint32_t pc;
	uint32_t lr; /* where the function was called from, which says who the caller of memcpy was */
	uint32_t up; /* where the function the sample is in (or, if it has no frame of its own, the one that called it) was called from: who called csmemcpy */
	uint32_t count;
};
typedef char profile_record_is_16_bytes[sizeof(struct profile_record) == 16 ? 1 : -1];
static struct profile_record table[TABLE_SIZE];
static OSThread *target;
static OSThread thread;
static unsigned report_seconds = 10;
static uint32_t samples, lost;

/* The worker alone performs directory/file operations. A successful close is
   required before it can publish FREE; a failed close poisons the ownership
   state rather than allowing the sampler to overwrite possibly-live data. */
#define SNAPSHOT_HEADER_BYTES 96u
#define SNAPSHOT_BYTES (SNAPSHOT_HEADER_BYTES + TABLE_SIZE * 16u)
#define SNAPSHOT_RUN_LIMIT 64u
#define SNAPSHOT_FILE_LIMIT 128u
#define SNAPSHOT_DIRECTORY_LIMIT 4096u
#define SNAPSHOT_STACK_BYTES (64u * 1024u)
enum { SNAPSHOT_FREE, SNAPSHOT_READY, SNAPSHOT_WRITING, SNAPSHOT_STARTING, SNAPSHOT_POISONED };
static struct
{
	uint8_t *bytes, *stack;
	OSThread thread;
	uint32_t owner, disabled, limited, errors;
	uint32_t busy_skips, disabled_skips, limit_skips; /* sampler-owned */
	uint32_t attempts, files; /* writer-owned */
	uint64_t run;
	const char *directory;
} snapshot;
static int snapshot_requested;
static uint32_t report_sequence;

static void snapshot_u32(uint8_t *out, uint32_t value)
{
	out[0] = (uint8_t)(value >> 24); out[1] = (uint8_t)(value >> 16);
	out[2] = (uint8_t)(value >> 8); out[3] = (uint8_t)value;
}

static void snapshot_u64(uint8_t *out, uint64_t value)
{
	snapshot_u32(out, (uint32_t)(value >> 32));
	snapshot_u32(out + 4, (uint32_t)value);
}

static uint32_t snapshot_read_u32(const uint8_t *in)
{
	return (uint32_t)in[0] << 24 | (uint32_t)in[1] << 16 | (uint32_t)in[2] << 8 | in[3];
}

static int snapshot_owned_name(const char *name)
{
	unsigned i;
	if (strlen(name) != 35 && strlen(name) != 36) return 0;
	if (strncmp(name, "profile-", 8) || name[24] != '-') return 0;
	for (i = 8; i < 24; i++)
		if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f'))) return 0;
	for (i = 25; i < 31; i++)
		if (name[i] < '0' || name[i] > '9') return 0;
	return !strcmp(name + 31, ".bin") || !strcmp(name + 31, ".part");
}

static int snapshot_scan(void)
{
	DIR *directory = opendir(snapshot.directory);
	struct dirent *entry;
	unsigned seen = 0;
	int error = 0;
	if (!directory) return 0;
	errno = 0;
	while ((entry = readdir(directory)) != NULL)
	{
		if (++seen > SNAPSHOT_DIRECTORY_LIMIT) { error = 1; break; }
		if (snapshot_owned_name(entry->d_name)) snapshot.files++;
	}
	if (errno) error = 1;
	if (closedir(directory) != 0) error = 1;
	if (snapshot.files >= SNAPSHOT_FILE_LIMIT)
		__atomic_store_n(&snapshot.limited, 1, __ATOMIC_RELEASE);
	return !error;
}

static const char *snapshot_offer(uint32_t sequence, uint64_t begin, uint64_t end, uint64_t frequency)
{
	uint8_t *header = snapshot.bytes;
	const uint32_t owner = __atomic_load_n(&snapshot.owner, __ATOMIC_ACQUIRE);
	if (__atomic_load_n(&snapshot.disabled, __ATOMIC_ACQUIRE))
	{
		snapshot.disabled_skips++;
		return "disabled";
	}
	if (__atomic_load_n(&snapshot.limited, __ATOMIC_ACQUIRE))
	{
		snapshot.limit_skips++;
		return "limit";
	}
	if (owner != SNAPSHOT_FREE)
	{
		snapshot.busy_skips++;
		return "busy";
	}
	memset(header, 0, SNAPSHOT_HEADER_BYTES);
	memcpy(header, "OPCEPROF", 8);
	snapshot_u32(header + 8, 1); /* version */
	snapshot_u32(header + 12, 0x01020304u); /* big-endian marker */
	snapshot_u32(header + 16, SNAPSHOT_HEADER_BYTES);
	snapshot_u32(header + 20, 16);
	snapshot_u32(header + 24, TABLE_SIZE);
	snapshot_u32(header + 28, samples);
	snapshot_u32(header + 32, lost);
	snapshot_u32(header + 36, (uint32_t)(uintptr_t)&wiiu_log);
	snapshot_u32(header + 40, sequence);
	snapshot_u32(header + 44, snapshot.busy_skips);
	snapshot_u32(header + 48, __atomic_load_n(&snapshot.errors, __ATOMIC_RELAXED));
	snapshot_u32(header + 52, snapshot.limit_skips);
	snapshot_u64(header + 56, frequency);
	snapshot_u64(header + 64, snapshot.run);
	snapshot_u64(header + 72, begin);
	snapshot_u64(header + 80, end);
	snapshot_u32(header + 88, TABLE_SIZE * 16u);
	/* The target is paused by this sampler's higher priority on core 1. Only
	   the sampler changes the table; the worker owns the copy until close. */
	memcpy(header + SNAPSHOT_HEADER_BYTES, table, sizeof(table));
	__atomic_store_n(&snapshot.owner, SNAPSHOT_READY, __ATOMIC_RELEASE);
	return "queued";
}

static int snapshot_write_all(int file, const uint8_t *data, size_t size)
{
	size_t done = 0;
	while (done < size)
	{
		const size_t chunk = size - done > 16384u ? 16384u : size - done;
		ssize_t written = write(file, data + done, chunk);
		if (written < 0 && errno == EINTR) continue;
		if (written <= 0 || (size_t)written > chunk) return 0;
		done += (size_t)written;
	}
	return 1;
}

static int snapshot_absent(const char *path)
{
	struct stat info;
	if (stat(path, &info) == 0) return 0;
	return errno == ENOENT;
}

static int snapshot_write_one(void)
{
	char final[192], partial[192];
	uint32_t checksum = 2166136261u, sequence;
	unsigned i;
	int file = -1, ok = 0, close_ok = 1;
	if (__atomic_load_n(&snapshot.owner, __ATOMIC_ACQUIRE) != SNAPSHOT_READY) return 0;
	__atomic_store_n(&snapshot.owner, SNAPSHOT_WRITING, __ATOMIC_RELAXED);
	sequence = snapshot_read_u32(snapshot.bytes + 40);
	if (snapshot.attempts >= SNAPSHOT_RUN_LIMIT || snapshot.files >= SNAPSHOT_FILE_LIMIT)
	{
		__atomic_store_n(&snapshot.limited, 1, __ATOMIC_RELEASE);
		__atomic_store_n(&snapshot.owner, SNAPSHOT_FREE, __ATOMIC_RELEASE);
		return 1;
	}
	snapshot.attempts++;
	if (snprintf(final, sizeof(final), "%s/profile-%016llx-%06u.bin", snapshot.directory,
		(unsigned long long)snapshot.run, (unsigned)sequence) >= (int)sizeof(final) ||
		snprintf(partial, sizeof(partial), "%s/profile-%016llx-%06u.part", snapshot.directory,
		(unsigned long long)snapshot.run, (unsigned)sequence) >= (int)sizeof(partial)) goto finished;
	/* No existing capture is overwritten. One worker owns this namespace;
	   the second absence check also catches a pre-existing final-file collision. */
	if (!snapshot_absent(final)) goto finished;
	file = open(partial, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (file < 0) goto finished;
	snapshot.files++;
	/* Explicit wire order also makes the host ownership tests emit the same
	   format as the big-endian console. Conversion happens only on core 0. */
	for (i = 0; i < TABLE_SIZE; i++)
	{
		uint8_t *at = snapshot.bytes + SNAPSHOT_HEADER_BYTES + i * 16u;
		struct profile_record value;
		memcpy(&value, at, sizeof(value));
		snapshot_u32(at, value.pc); snapshot_u32(at + 4, value.lr);
		snapshot_u32(at + 8, value.up); snapshot_u32(at + 12, value.count);
	}
	/* FNV-1a covers the first 92 header bytes and all records. It detects
	   truncated/corrupt captures; it is not an authentication mechanism. */
	for (i = 0; i < SNAPSHOT_BYTES; i++)
		if (i < 92 || i >= SNAPSHOT_HEADER_BYTES)
			checksum = (checksum ^ snapshot.bytes[i]) * 16777619u;
	snapshot_u32(snapshot.bytes + 92, checksum);
	ok = snapshot_write_all(file, snapshot.bytes, SNAPSHOT_BYTES);
	if (ok && fsync(file) != 0) ok = 0;
	close_ok = close(file) == 0;
	file = -1;
	if (!close_ok) ok = 0;
	if (ok && (!snapshot_absent(final) || rename(partial, final) != 0)) ok = 0;
finished:
	if (!ok)
	{
		__atomic_add_fetch(&snapshot.errors, 1, __ATOMIC_RELAXED);
		__atomic_store_n(&snapshot.disabled, 1, __ATOMIC_RELEASE);
		wiiu_log("PROF SNAP failed run=%016llx seq=%u close=%s; binary capture disabled", (unsigned long long)snapshot.run,
			(unsigned)sequence, close_ok ? "closed" : "uncertain");
	}
	else
		wiiu_log("PROF SNAP saved run=%016llx seq=%u file=profile-%016llx-%06u.bin bytes=%u", (unsigned long long)snapshot.run,
			(unsigned)sequence, (unsigned long long)snapshot.run, (unsigned)sequence, (unsigned)SNAPSHOT_BYTES);
	if (snapshot.attempts >= SNAPSHOT_RUN_LIMIT || snapshot.files >= SNAPSHOT_FILE_LIMIT)
		__atomic_store_n(&snapshot.limited, 1, __ATOMIC_RELEASE);
	__atomic_store_n(&snapshot.owner, close_ok ? SNAPSHOT_FREE : SNAPSHOT_POISONED, __ATOMIC_RELEASE);
	return 1;
}

static int snapshot_writer(int argc, const char **argv)
{
	(void)argc; (void)argv;
	if (!snapshot_scan())
	{
		__atomic_add_fetch(&snapshot.errors, 1, __ATOMIC_RELAXED);
		__atomic_store_n(&snapshot.disabled, 1, __ATOMIC_RELEASE);
	}
	__atomic_store_n(&snapshot.owner, SNAPSHOT_FREE, __ATOMIC_RELEASE);
	while (!wiiu_exit_requested())
	{
		if (wiiu_in_foreground() && snapshot_write_one()) continue;
		OSSleepTicks(OSMillisecondsToTicks(10));
	}
	return 0;
}

static int snapshot_start(int requested)
{
	if (!requested) return 0;
	snapshot_requested = 1;
	snapshot.run = (uint64_t)OSGetSystemTime();
	snapshot.directory = WIIU_DATA_DIRECTORY;
	snapshot.owner = SNAPSHOT_STARTING;
	snapshot.bytes = MEMAllocFromDefaultHeapEx(SNAPSHOT_BYTES, 16);
	snapshot.stack = MEMAllocFromDefaultHeapEx(SNAPSHOT_STACK_BYTES, 16);
	if (!snapshot.bytes || !snapshot.stack ||
		!OSCreateThread(&snapshot.thread, snapshot_writer, 0, NULL, snapshot.stack + SNAPSHOT_STACK_BYTES, SNAPSHOT_STACK_BYTES,
			30, OS_THREAD_ATTRIB_AFFINITY_CPU0))
	{
		if (snapshot.bytes) MEMFreeToDefaultHeap(snapshot.bytes);
		if (snapshot.stack) MEMFreeToDefaultHeap(snapshot.stack);
		snapshot.bytes = snapshot.stack = NULL;
		snapshot.disabled = 1;
		wiiu_log("PROF SNAP unavailable: no buffer, stack or writer; text profiling continues");
		return 0;
	}
	OSSetThreadName(&snapshot.thread, "profile snapshot writer");
	OSResumeThread(&snapshot.thread);
	return 1;
}

static void note(uint32_t pc, uint32_t lr, uint32_t up)
{
	uint32_t key = pc >> 4, slot = ((key * 2654435761u) ^ (lr * 40503u) ^ (up * 31u)) % TABLE_SIZE, probes;

	for (probes = 0; probes < 64; probes++, slot = (slot + 1) % TABLE_SIZE)
	{
		if (table[slot].count == 0)
		{
			table[slot].pc = key << 4;
			table[slot].lr = lr;
			table[slot].up = up;
			table[slot].count = 1;
			return;
		}
		if (table[slot].pc == (key << 4) && table[slot].lr == lr && table[slot].up == up)
		{
			table[slot].count++;
			return;
		}
	}
	lost++;
}

static void report(uint64_t begin, uint64_t end)
{
	unsigned line, i, best;
	static uint8_t done[TABLE_SIZE];

	if (snapshot_requested)
	{
		const uint32_t sequence = ++report_sequence;
		const char *status;
		wiiu_log("PROF %u samples, %u lost, function addresses: wiiu_log %08x run=%016llx seq=%u", (unsigned)samples,
			(unsigned)lost, (unsigned)(uintptr_t)&wiiu_log, (unsigned long long)snapshot.run, (unsigned)sequence);
		status = snapshot_offer(sequence, begin, end, (uint64_t)OSMillisecondsToTicks(1000));
		wiiu_log("PROF SNAP run=%016llx seq=%u begin=%016llx end=%016llx status=%s busy=%u errors=%u limit=%u",
			(unsigned long long)snapshot.run, (unsigned)sequence, (unsigned long long)begin, (unsigned long long)end, status,
			(unsigned)snapshot.busy_skips, (unsigned)__atomic_load_n(&snapshot.errors, __ATOMIC_RELAXED), (unsigned)snapshot.limit_skips);
	}
	else
		wiiu_log("PROF %u samples, %u lost, function addresses: wiiu_log %08x", (unsigned)samples, (unsigned)lost, (unsigned)(uintptr_t)&wiiu_log);
	{
		void wiiu_lockprof_report(void);

		wiiu_lockprof_report();
	}
	memset(done, 0, sizeof(done));
	for (line = 0; line < REPORT_LINES; line++)
	{
		best = TABLE_SIZE;
		for (i = 0; i < TABLE_SIZE; i++)
			if (table[i].count && !done[i] && (best == TABLE_SIZE || table[i].count > table[best].count))
				best = i;
		if (best == TABLE_SIZE)
			break;
		done[best] = 1;
		wiiu_log("PROF pc=0x%08x lr=0x%08x up=0x%08x n=%u", (unsigned)table[best].pc, (unsigned)table[best].lr, (unsigned)table[best].up, (unsigned)table[best].count);
	}
	memset(table, 0, sizeof(table));
	samples = lost = 0;
}

/* The game's code is loaded at about 0x0c000000 (the RPX's 0x02000000 and the load offset: the log's function addresses say where). A thread sampling in system code is at
an address of the system's: its stack is followed (each frame's first word is the caller's frame, the second the return address) to the first return address in the game's code,
which provides caller attribution, without assuming the library was waiting. */
static uint32_t caller_in_code(uint32_t pc, uint32_t lr)
{
	uint32_t sp = (uint32_t)target->context.gpr[1];
	unsigned depth;
	const uint32_t low = (uint32_t)(uintptr_t)target->stackEnd, high = (uint32_t)(uintptr_t)target->stackStart;

	if (pc >= 0x0c000000u && pc < 0x0c800000u)
		return lr;
	if (lr >= 0x0c000000u && lr < 0x0c800000u)
		return lr;
	for (depth = 0; depth < 12; depth++)
	{
		const uint32_t *frame;
		uint32_t ret;

		if (sp < low || sp + 8 > high || (sp & 7))
			break;
		frame = (const uint32_t *)(uintptr_t)sp;
		ret = frame[1];
		if (ret >= 0x0c000000u && ret < 0x0c800000u)
			return ret;
		sp = frame[0];
	}
	return lr;
}

/* the return address of the function the sample is in, or of its caller when the sample is in a leaf that has built a frame (memcpy does, at its first instruction): a function saves
its return address in its caller's frame, at 4 bytes from the word the chain of frames points to, so it is two links down from the stack pointer (the first link is the frame of the
function that called the leaf, the second that function's caller's, where the first one's return address is) */
static uint32_t return_above(void)
{
	const uint32_t low = (uint32_t)(uintptr_t)target->stackEnd, high = (uint32_t)(uintptr_t)target->stackStart;
	uint32_t sp = (uint32_t)target->context.gpr[1], link;
	unsigned depth;

	for (depth = 0; depth < 2; depth++)
	{
		if (sp < low || sp + 8 > high || (sp & 7))
			return 0;
		link = *(const uint32_t *)(uintptr_t)sp;
		if (link <= sp)
			return 0;
		sp = link;
	}
	if (sp < low || sp + 8 > high || (sp & 7))
		return 0;
	return *(const uint32_t *)(uintptr_t)(sp + 4);
}

static int sampler(int argc, const char **argv)
{
	OSTime last_report = OSGetSystemTime();
	OSTime window_begin = last_report;

	(void)argc;
	(void)argv;
	for (;;)
	{
		OSSleepTicks(OSMillisecondsToTicks(1));
		{
			const uint32_t pc = (uint32_t)target->context.srr0, lr = (uint32_t)target->context.lr;

			note(pc, caller_in_code(pc, lr), pc >= 0x0c000000u && pc < 0x0c800000u ? return_above() : 0);
		}
		samples++;
		if ((uint64_t)(OSGetSystemTime() - last_report) >= (uint64_t)OSSecondsToTicks(report_seconds))
		{
			last_report = OSGetSystemTime();
			report((uint64_t)window_begin, (uint64_t)last_report);
			window_begin = OSGetSystemTime();
		}
	}
	return 0;
}

/* called by the game's own thread (the renderer's start) */
void wiiu_profile_start(void);

void wiiu_profile_start(void)
{
	FILE *file = fopen(WIIU_DATA_DIRECTORY "/profile.txt", "r");
	void *stack;
	const uint32_t stack_size = 64 * 1024;
	unsigned seconds = 0, full = 0;

	if (!file)
		return;
	if (target) { fclose(file); return; }
	const int read = fscanf(file, "%u %u", &seconds, &full);

	fclose(file);
	if (read >= 1 && seconds == 0)
		return; /* (a 0 in the file is the profiler off: the file cannot be deleted from the Mac) */
	if (read >= 1 && seconds >= 2)
		report_seconds = seconds;
	target = OSGetCurrentThread();
	{
		/* (wiiu_lockprof.c: the game thread's waits for locks timed too) */
		void wiiu_lockprof_watch(void *thread);

		wiiu_lockprof_watch(target);
	}
	stack = MEMAllocFromDefaultHeapEx(stack_size, 16);
	if (!stack)
		return;
	if (!OSCreateThread(&thread, sampler, 0, NULL, (uint8_t *)stack + stack_size, stack_size, OSGetThreadPriority(target) - 1,
		(OSThreadAttributes)OS_THREAD_ATTRIB_AFFINITY_CPU1))
	{
		MEMFreeToDefaultHeap(stack);
		return;
	}
	snapshot_start(read == 2 && full == 1);
	OSSetThreadName(&thread, "profiler");
	OSResumeThread(&thread);
	wiiu_log("Profiler: sampling the game thread every millisecond, reporting every %u s", report_seconds);
}
