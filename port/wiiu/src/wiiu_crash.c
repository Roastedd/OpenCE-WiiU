#include "wiiu_crash.h"

#include "wiiu_align_fixup.h"

#include <coreinit/context.h>
#include <coreinit/core.h>
#include <coreinit/exception.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

static const char *crash_path;
static const char *const *crash_phase;
static const char *crash_beat;
static OSExceptionCallbackFn previous[16];
static char note[2048];
static volatile size_t note_size;
/* the first part of the note: the kind of fault, the thread, the phase and where (no memory of the app's read but the strings, checked) */
static volatile size_t part_one;
/* 0 nothing yet; 1 being filled in; 2 ready to be written; 3 written */
static volatile int state;
static OSThread writer_thread;
static uint8_t writer_stack[16 * 1024] __attribute__((aligned(16)));
/* the alignment exceptions answered by making the access (wiiu_align_fixup.c): how many, and where (the program counters) */
#define FIXUP_PLACES 64
static struct
{
	volatile uint32_t pc;
	volatile uint32_t count;
} fixup_places[FIXUP_PLACES];
static volatile uint32_t fixup_total;
/* by the core the faulting thread was on (a handler runs on it): the tick snapshot's timers ask for the calling core's, to say how many a capture took */
static volatile uint32_t fixup_core[3];
static volatile uint32_t fixup_place_count;
/* how long the faulting thread waits for its note to be written */
#define WAIT_MILLISECONDS 3000

static char *put(char *p, const char *text, size_t limit)
{
	while (*text && limit--)
		*p++ = *text++;
	return p;
}

static char *hex(char *p, uint32_t value)
{
	int shift;

	for (shift = 28; shift >= 0; shift -= 4)
		*p++ = "0123456789ABCDEF"[(value >> shift) & 15];
	return p;
}

static const char *type_name(OSExceptionType type)
{
	switch (type)
	{
	case OS_EXCEPTION_TYPE_MACHINE_CHECK:
		return "machine check";
	case OS_EXCEPTION_TYPE_DSI:
		return "DSI (an address that cannot be read or written)";
	case OS_EXCEPTION_TYPE_ISI:
		return "ISI (an address that cannot be run)";
	case OS_EXCEPTION_TYPE_ALIGNMENT:
		return "alignment";
	case OS_EXCEPTION_TYPE_PROGRAM:
		return "program (an instruction that cannot be run)";
	default:
		return "exception";
	}
}

/* somewhere a stack can be: word aligned, in the heap's part of the address space */
static int stack_address(uint32_t address)
{
	return address >= 0x10000000u && address < 0x50000000u && !(address & 3u);
}

/* somewhere a string of the app's can be (its code and read-only data are low, its heap above) */
static int string_address(const void *pointer)
{
	return (uintptr_t)pointer >= 0x01000000u && (uintptr_t)pointer < 0x50000000u;
}

static void write_note(const char *text, size_t size, int append)
{
	size_t done = 0;
	int file = open(crash_path, O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);

	if (file < 0)
		return;
	while (done < size)
	{
		ssize_t written = write(file, text + done, size - done);

		if (written <= 0)
			break;
		done += (size_t)written;
	}
	fsync(file);
	close(file);
}

/* The note is written by a thread of its own, on another core: the probe's first runs froze the console in a fault handler that wrote
the file itself (writing goes through messages to another processor, which a core stuck in an exception may not be able to wait
for; measured, not proven), and survived the same faults once the handler did no input or output. */
static int writer_main(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	for (;;)
	{
		if (state == 2)
		{
			write_note(note, note_size, 0);
			state = 3;
		}
		else if (state == 4)
		{
			/* the second part: where the stack and the registers were (kept apart: a fault on the way through the stack must not take
			the first part with it) */
			write_note(note + part_one, note_size - part_one, 1);
			state = 5;
		}
		OSSleepTicks(OSMillisecondsToTicks(20));
	}
	return 0;
}

static void fill_note(OSExceptionType type, OSContext *context)
{
	char *p = note;
	uint32_t frame = context->gpr[1];
	OSThread *thread = OSGetCurrentThread();
	const char *name = string_address(thread) ? OSGetThreadName(thread) : NULL;
	const char *phase = crash_phase ? *crash_phase : NULL;
	int depth;
	int i;

	p = put(p, "CRASH ", 8);
	p = put(p, type_name(type), 80);
	p = put(p, "\nthread ", 16);
	p = put(p, string_address(name) ? name : "?", 40);
	p = put(p, "\nphase ", 16);
	p = put(p, string_address(phase) ? phase : "?", 120);
	if (crash_beat)
	{
		p = put(p, "\nlast heartbeat ", 24);
		p = put(p, crash_beat, 200);
	}
	p = put(p, "\ncore ", 16);
	p = hex(p, OSGetCoreId());
	p = put(p, " pc ", 8);
	p = hex(p, context->srr0);
	p = put(p, " lr ", 8);
	p = hex(p, context->lr);
	p = put(p, " sp ", 8);
	p = hex(p, context->gpr[1]);
	p = put(p, " dar ", 8);
	p = hex(p, context->dar);
	p = put(p, " dsisr ", 8);
	p = hex(p, context->dsisr);
	*p++ = '\n';
	part_one = (size_t)(p - note);
	note_size = part_one;
	state = 2;
	{
		const OSTime started = OSGetSystemTime();

		while (state != 3 && OSGetSystemTime() - started < (OSTime)OSMillisecondsToTicks(WAIT_MILLISECONDS / 2))
		{
		}
	}
	p = put(p, "stack ", 16);
	p = hex(p, context->lr);
	for (depth = 0; depth < 12 && stack_address(frame); depth++)
	{
		uint32_t next = *(const uint32_t *)frame; /* the caller's frame, whose return address is 4 into it */

		if (next <= frame || next - frame > 0x200000u || !stack_address(next))
			break;
		*p++ = ' ';
		p = hex(p, *(const uint32_t *)(next + 4));
		frame = next;
	}
	p = put(p, "\nregisters", 16);
	for (i = 0; i < 32; i++)
	{
		*p++ = ' ';
		p = hex(p, context->gpr[i]);
	}
	*p++ = '\n';
	note_size = (size_t)(p - note);
}

static void record_fixup(uint32_t pc)
{
	uint32_t i;

	__sync_fetch_and_add(&fixup_total, 1);
	{
		const uint32_t core = OSGetCoreId();

		if (core < 3)
			__sync_fetch_and_add(&fixup_core[core], 1);
	}
	for (;;)
	{
		const uint32_t known = fixup_place_count;

		for (i = 0; i < known && i < FIXUP_PLACES; i++)
		{
			if (fixup_places[i].pc == pc)
			{
				__sync_fetch_and_add(&fixup_places[i].count, 1);
				return;
			}
		}
		if (known >= FIXUP_PLACES)
			return;
		/* a place nobody has recorded: claim the slot after the known ones; if another core claimed it first, look again */
		if (__sync_bool_compare_and_swap(&fixup_place_count, known, known + 1))
		{
			fixup_places[known].count = 1;
			fixup_places[known].pc = pc;
			return;
		}
	}
}

uint32_t wiiu_crash_fixups(void)
{
	return fixup_total;
}

uint32_t wiiu_crash_fixups_here(void)
{
	const uint32_t core = OSGetCoreId();

	return core < 3 ? fixup_core[core] : 0;
}

uint32_t wiiu_crash_fixup_place(uint32_t index, uint32_t *count)
{
	if (index >= FIXUP_PLACES || index >= fixup_place_count || !fixup_places[index].pc)
		return 0;
	*count = fixup_places[index].count;
	return fixup_places[index].pc;
}

static BOOL handle(OSExceptionType type, OSContext *context)
{
	if (previous[type] && previous[type](context))
		return TRUE;
	/* A float or double at an address that is not a multiple of 4 raises an alignment exception on this core (the probe measured it);
	the access is made here, the instruction is skipped and the thread goes on, for about a microsecond. No input or output, nothing
	that waits. */
	if (type == OS_EXCEPTION_TYPE_ALIGNMENT &&
		wiiu_align_fixup(*(const uint32_t *)context->srr0, context->dar, context->gpr, context->fpr))
	{
		record_fixup(context->srr0);
		context->srr0 += 4;
		return TRUE;
	}
	/* one note is enough: a second fault in the handler or on another core must not overwrite it */
	if (__sync_bool_compare_and_swap(&state, 0, 1))
	{
		const OSTime started = OSGetSystemTime();

		fill_note(type, context);
		state = 4;
		while (state != 5 && OSGetSystemTime() - started < (OSTime)OSMillisecondsToTicks(WAIT_MILLISECONDS))
		{
		}
	}
	return FALSE;
}

#define HANDLER(name, type) \
	static BOOL on_##name(OSContext *context) \
	{ \
		return handle(type, context); \
	}

HANDLER(machine_check, OS_EXCEPTION_TYPE_MACHINE_CHECK)
HANDLER(dsi, OS_EXCEPTION_TYPE_DSI)
HANDLER(isi, OS_EXCEPTION_TYPE_ISI)
HANDLER(alignment, OS_EXCEPTION_TYPE_ALIGNMENT)
HANDLER(program, OS_EXCEPTION_TYPE_PROGRAM)

static void hook(OSExceptionType type, OSExceptionCallbackFn callback)
{
	previous[type] = OSSetExceptionCallbackEx(OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, type, callback);
}

void wiiu_crash_install(const char *path, const char *const *phase, const char *beat)
{
	crash_path = path;
	crash_phase = phase;
	crash_beat = beat;
	if (OSCreateThread(&writer_thread, writer_main, 0, NULL, writer_stack + sizeof(writer_stack), sizeof(writer_stack), 1,
			(OSThreadAttributes)(OS_THREAD_ATTRIB_AFFINITY_ANY | OS_THREAD_ATTRIB_DETACHED)))
	{
		OSSetThreadName(&writer_thread, "crash note writer");
		OSResumeThread(&writer_thread);
	}
	hook(OS_EXCEPTION_TYPE_MACHINE_CHECK, on_machine_check);
	hook(OS_EXCEPTION_TYPE_DSI, on_dsi);
	hook(OS_EXCEPTION_TYPE_ISI, on_isi);
	hook(OS_EXCEPTION_TYPE_ALIGNMENT, on_alignment);
	hook(OS_EXCEPTION_TYPE_PROGRAM, on_program);
}
