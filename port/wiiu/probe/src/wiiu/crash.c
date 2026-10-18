#include "crash.h"

#include "card.h"

#include <coreinit/context.h>
#include <coreinit/core.h>
#include <coreinit/exception.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <stdint.h>

#define WRITER_STACK_BYTES (16 * 1024)
/* how long the faulting thread waits for the note to be written */
#define WAIT_MILLISECONDS 3000

static const char *volatile current_stage = "starting";
static OSExceptionCallbackFn previous[16];
static char note[2048];
static volatile size_t note_size;
/* 0 nothing yet; 1 being filled in; 2 ready to be written; 3 written */
static volatile int state;
static OSThread writer_thread;
static uint8_t writer_stack[WRITER_STACK_BYTES] __attribute__((aligned(16)));
static volatile int recover_alignment;
static volatile uint32_t alignment_traps;
static volatile uint32_t alignment_pc;
static volatile uint32_t alignment_address;

static char *put(char *p, const char *text)
{
	while (*text)
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

/* the thread that writes the note: it runs on another core than the one stuck in the handler */
static int writer_main(int argc, const char **argv)
{
	(void)argc;
	(void)argv;
	for (;;)
	{
		if (state == 2)
		{
			card_write(PROBE_CRASH_FILE, note, note_size, 0);
			state = 3;
		}
		OSSleepTicks(OSMillisecondsToTicks(20));
	}
	return 0;
}

static void fill_note(OSExceptionType type, OSContext *context)
{
	char *p = note;
	uint32_t frame = context->gpr[1];
	int depth;
	int i;

	p = put(p, "CRASH ");
	p = put(p, type_name(type));
	p = put(p, "\nstage ");
	p = put(p, current_stage);
	p = put(p, "\ncore ");
	p = hex(p, OSGetCoreId());
	p = put(p, " pc ");
	p = hex(p, context->srr0);
	p = put(p, " lr ");
	p = hex(p, context->lr);
	p = put(p, " sp ");
	p = hex(p, context->gpr[1]);
	p = put(p, " dar ");
	p = hex(p, context->dar);
	p = put(p, " dsisr ");
	p = hex(p, context->dsisr);
	p = put(p, "\nstack ");
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
	p = put(p, "\nregisters");
	for (i = 0; i < 32; i++)
	{
		*p++ = ' ';
		p = hex(p, context->gpr[i]);
	}
	*p++ = '\n';
	note_size = (size_t)(p - note);
}

static BOOL handle(OSExceptionType type, OSContext *context)
{
	if (previous[type] && previous[type](context))
		return TRUE;
	if (type == OS_EXCEPTION_TYPE_ALIGNMENT && recover_alignment)
	{
		/* counted, the instruction skipped, the thread resumed: no I/O, nothing that waits */
		alignment_pc = context->srr0;
		alignment_address = context->dar;
		alignment_traps++;
		context->srr0 += 4;
		return TRUE;
	}
	/* one note is enough: a second fault in the handler or on another core must not overwrite it */
	if (__sync_bool_compare_and_swap(&state, 0, 1))
	{
		const OSTime started = OSGetSystemTime();

		fill_note(type, context);
		state = 2;
		/* the writer is on another core; this one waits for it, a few seconds at most */
		while (state != 3 && OSGetSystemTime() - started < (OSTime)OSMillisecondsToTicks(WAIT_MILLISECONDS))
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

void crash_install(void)
{
	if (OSCreateThread(&writer_thread, writer_main, 0, NULL, writer_stack + WRITER_STACK_BYTES, WRITER_STACK_BYTES, 1,
			OS_THREAD_ATTRIB_AFFINITY_ANY | OS_THREAD_ATTRIB_DETACHED))
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

void crash_set_stage(const char *stage)
{
	current_stage = stage;
}

void crash_recover_alignment(int on)
{
	recover_alignment = on;
}

uint32_t crash_alignment_traps(void)
{
	return alignment_traps;
}

void crash_alignment_last(uint32_t *pc, uint32_t *address)
{
	*pc = alignment_pc;
	*address = alignment_address;
}
