#include "wiiu_crash.h"

#include <coreinit/context.h>
#include <coreinit/core.h>
#include <coreinit/exception.h>
#include <coreinit/thread.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

static const char *crash_path;
static const char *const *crash_phase;
static const char *crash_beat;
static OSExceptionCallbackFn previous[16];
static volatile int taken;
static char note[2048];

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

static void write_note(const char *text, size_t size)
{
	size_t done = 0;
	int file = open(crash_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

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

static BOOL handle(OSExceptionType type, OSContext *context)
{
	if (previous[type] && previous[type](context))
		return TRUE;
	/* one note is enough: a second fault in the handler or on another core must not overwrite it */
	if (__sync_bool_compare_and_swap(&taken, 0, 1))
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
		p = put(p, "\nstack ", 16);
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
		write_note(note, (size_t)(p - note));
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
	hook(OS_EXCEPTION_TYPE_MACHINE_CHECK, on_machine_check);
	hook(OS_EXCEPTION_TYPE_DSI, on_dsi);
	hook(OS_EXCEPTION_TYPE_ISI, on_isi);
	hook(OS_EXCEPTION_TYPE_ALIGNMENT, on_alignment);
	hook(OS_EXCEPTION_TYPE_PROGRAM, on_program);
}
