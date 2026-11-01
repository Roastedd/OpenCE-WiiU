/*
WIIU_STUDY.C

A number from a file beside config.toml (halo/<name>), for a study switch the game's code reads (source/main/main.c's
main_study_setting: halo/tick_thread.txt). The game's own fopen takes the Xbox's paths (port/linux/include/stdio.h), so
the file is read here, with the C library's.
*/

#include "wiiu_shell.h"

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
