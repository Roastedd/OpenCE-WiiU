#include "crash.h"

#include "card.h"

#include <coreinit/context.h>
#include <coreinit/core.h>
#include <coreinit/exception.h>
#include <stdint.h>

static const char *volatile current_stage = "starting";
static OSExceptionCallbackFn previous[16];
static volatile int taken;
static char note[1024];

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

static BOOL handle(OSExceptionType type, OSContext *context)
{
	if (previous[type] && previous[type](context))
		return TRUE;
	/* one note is enough: a second fault in the handler or on another core must not overwrite it */
	if (__sync_bool_compare_and_swap(&taken, 0, 1))
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
		p = put(p, "\nstack");
		*p++ = ' ';
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
		card_write(PROBE_CRASH_FILE, note, (size_t)(p - note), 0);
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
