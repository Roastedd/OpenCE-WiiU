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

/* halo/governor.txt (the dynamic quality's settings, port/linux/game/render_governor.c): its text in text (size bytes with the end), 0 if there is no such file */
int render_governor_settings_text(char *text, unsigned long size)
{
	char path[256];
	FILE *file;
	size_t length;

	snprintf(path, sizeof(path), "%s/governor.txt", WIIU_DATA_DIRECTORY);
	file = fopen(path, "r");
	if (!file)
		return 0;
	length = fread(text, 1, size - 1, file);
	fclose(file);
	text[length] = 0;
	return 1;
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

/* ... and, after its first job, the core it ran it on and the cores it may run on (OSGetThreadAffinity: 1 is core 0 alone), which say whether it stayed
on core 0 */
void wiiu_study_tick_thread_job_report(void)
{
	wiiu_log("study: the tick thread ran its first job on core %u (its affinity %x: 1 is core 0 alone)", (unsigned)OSGetCoreId(),
		(unsigned)OSGetThreadAffinity(OSGetCurrentThread()));
}
