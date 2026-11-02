/*
WIIU_STUDY.C

A number from a file beside config.toml (halo/<name>), for a study switch the game's code reads (source/main/main.c's
main_study_setting: halo/tick_thread.txt). The game's own fopen takes the Xbox's paths (port/linux/include/stdio.h), so
the file is read here, with the C library's.
*/

#include "wiiu_shell.h"

#include <coreinit/core.h>
#include <coreinit/thread.h>

#include <stdint.h>
#include <stdio.h>

long wiiu_study_number(const char *name, long fallback)
{
	char path[256];
	FILE *file;
	long number = fallback;

	snprintf(path, sizeof(path), "%s/%s", WIIU_DATA_DIRECTORY, name);
	file = fopen(path, "r");
	if (!file)
		return fallback;
	if (fscanf(file, "%ld", &number) != 1)
		number = fallback;
	fclose(file);
	return number;
}

/* The tick thread (source/main/main.c) says once, in the log, that it runs and on which core, and what its FPU's status register holds: Cafe OS starts
a thread with the non-IEEE bit (00000004) set, which wiiu_fpu_setup clears, and a tick that ran with it set would flush denormals where a PC keeps
them. Call it after wiiu_fpu_setup, wiiu_thread_off_game_core and wiiu_tick_thread_priority. */
void wiiu_study_tick_thread_report(void)
{
	union
	{
		double value;
		uint64_t bits;
	} status;

	__asm__ volatile("mffs %0" : "=f"(status.value));
	wiiu_log("study: the tick thread runs on core %u at priority %d, FPSCR %08x (bit 00000004 clear: IEEE denormals)", (unsigned)OSGetCoreId(),
		(int)OSGetThreadPriority(OSGetCurrentThread()), (unsigned)(status.bits & 0xFFFFFFFFu));
}
