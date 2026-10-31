/*
SDL3/SDL.h, as the Wii U build has it: the SDL calls port_config.c makes (the base path of the app; loading, saving and freeing a file; a path's
size and time, to say when a config.toml beside the one read is not read), made with stdio and posix_stat. The Wii U build has no SDL; the rest of the
platform layer's use of it (windows, events, audio) has Wii U units of its own.
*/

#ifndef HALO_WIIU_SDL_SHIM_H
#define HALO_WIIU_SDL_SHIM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "posix.h" /* (posix_stat: the platform layer's units have the XDK's struct stat) */

/* the SD card folder the app keeps its files in, with its separator (WIIU_DATA_DIRECTORY in port/wiiu/src/wiiu_shell.h) */
static inline const char *SDL_GetBasePath(void)
{
	return "/vol/external01/wiiu/apps/halo/";
}

static inline void *SDL_LoadFile(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	void *data = NULL;
	long length;

	if (!file)
		return NULL;
	if (fseek(file, 0, SEEK_END) == 0 && (length = ftell(file)) >= 0 && fseek(file, 0, SEEK_SET) == 0)
	{
		data = malloc((size_t)length + 1);
		if (data && fread(data, 1, (size_t)length, file) == (size_t)length)
			*size = (size_t)length;
		else
		{
			free(data);
			data = NULL;
		}
	}
	fclose(file);
	return data;
}

static inline int SDL_SaveFile(const char *path, const void *data, size_t size)
{
	FILE *file = fopen(path, "wb");
	int written;

	if (!file)
		return 0;
	written = fwrite(data, 1, size, file) == size;
	return fclose(file) == 0 && written;
}

static inline void SDL_free(void *memory)
{
	free(memory);
}

/* (SDL's has the type and the times of creation and access too) */
typedef struct SDL_PathInfo
{
	unsigned long long size;
	long long modify_time;
} SDL_PathInfo;

static inline int SDL_GetPathInfo(const char *path, SDL_PathInfo *info)
{
	struct posix_file_information information;

	if (posix_stat(path, &information) != 0)
		return 0;
	if (info)
	{
		info->size = (unsigned long long)information.size_high << 32 | information.size_low;
		info->modify_time = (long long)information.modification_seconds * 1000000000LL +
			(long long)information.modification_nanoseconds;
	}
	return 1;
}

#endif
