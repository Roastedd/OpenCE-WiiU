/* Optional first-run setup. Existing converted maps bypass this entire path.
The worker owns only data/iso-import; saves and installed maps are untouched. */
#include "wiiu_import.h"
#include "wiiu_import_core.h"
#include "wiiu_shell.h"
#include "xiso.h"
#include "posix.h"

#include <coreinit/mutex.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <vpad/input.h>
#include <proc_ui/procui.h>
#include <whb/log.h>
#include <whb/log_console.h>
#include <whb/proc.h>
#include <sysapp/launch.h>

#include <dirent.h>
#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DATA WIIU_DATA_DIRECTORY "/data"
#define STAGING DATA "/iso-import"
#define RAW STAGING "/maps"
#define READY STAGING "/ready"
#define MAPS DATA "/maps"
#define MARKER "OpenCE-WiiU ISO import v1\n"
#define IMPORT_STACK (256u * 1024u)

static char const *const retail_maps[] = {
	"a10.map", "a30.map", "a50.map", "b30.map", "b40.map", "c10.map",
	"c20.map", "c40.map", "d20.map", "d40.map", "beavercreek.map",
	"bloodgulch.map", "boardingaction.map", "carousel.map", "chillout.map",
	"damnation.map", "hangemhigh.map", "longest.map", "prisoner.map",
	"putput.map", "ratrace.map", "sidewinder.map", "ui.map", "wizard.map"
};

static struct {
	OSMutex mutex;
	OSThread worker;
	int cancelled, done, success, map_number;
	unsigned long percent;
	char image[512], stage[64], name[256], problem[256];
} setup;

static void update(char const *stage, char const *name, unsigned long percent)
{
	OSLockMutex(&setup.mutex);
	snprintf(setup.stage, sizeof(setup.stage), "%s", stage);
	if (name)
		snprintf(setup.name, sizeof(setup.name), "%s", name);
	setup.percent = percent;
	OSUnlockMutex(&setup.mutex);
}

static int cancelled(void *unused)
{
	int result;

	(void)unused;
	OSLockMutex(&setup.mutex);
	result = setup.cancelled;
	OSUnlockMutex(&setup.mutex);
	return result;
}

static void extraction_progress(void *unused, char const *name, unsigned long long done,
	unsigned long long total)
{
	(void)unused;
	update("Extracting", name, total ? (unsigned long)(done * 100 / total) : 0);
}

static int conversion_progress(void *unused, char const *stage, unsigned long done, unsigned long total)
{
	update(stage, NULL, total ? (unsigned long)((unsigned long long)done * 100 / total) : 0);
	return cancelled(unused);
}

/* Only an explicitly marked temporary directory may be cleared. No symlinks
are followed, and nothing under the final maps directory is removed. */
static int clear_directory(char const *path, int depth)
{
	DIR *directory;
	struct dirent *entry;
	int ok = 1;

	if (depth > 2)
		return 0;
	directory = opendir(path);
	if (!directory)
		return errno == ENOENT;
	while (ok && (entry = readdir(directory)) != NULL)
	{
		char child[1024];
		struct stat info;

		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") ||
			(depth == 0 && !strcmp(entry->d_name, "owner.txt")))
			continue;
		if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >= (int)sizeof(child) ||
			lstat(child, &info) != 0 || S_ISLNK(info.st_mode))
		{
			ok = 0;
			break;
		}
		if (S_ISDIR(info.st_mode))
			ok = clear_directory(child, depth + 1) && rmdir(child) == 0;
		else
			ok = S_ISREG(info.st_mode) && unlink(child) == 0;
	}
	closedir(directory);
	return ok;
}

static int prepare_staging(void)
{
	struct stat info;
	FILE *marker;
	char text[64] = "";

	if (lstat(STAGING, &info) == 0)
	{
		if (!S_ISDIR(info.st_mode) || S_ISLNK(info.st_mode))
			return 0;
		marker = fopen(STAGING "/owner.txt", "rb");
		if (!marker)
			return 0;
		fgets(text, sizeof(text), marker);
		fclose(marker);
		if (strcmp(text, MARKER) || !clear_directory(STAGING, 0))
			return 0;
	}
	else if (errno != ENOENT || mkdir(STAGING, 0755) != 0)
		return 0;
	marker = fopen(STAGING "/owner.txt.part", "wb");
	if (!marker)
		return 0;
	if (fputs(MARKER, marker) < 0 || fflush(marker) != 0 || fsync(fileno(marker)) != 0)
	{
		fclose(marker);
		return 0;
	}
	return fclose(marker) == 0 && rename(STAGING "/owner.txt.part", STAGING "/owner.txt") == 0 &&
		mkdir(READY, 0755) == 0;
}

static int copy_loading_picture(void)
{
	FILE *input = fopen(RAW "/loading.tga", "rb"), *output;
	unsigned char buffer[4096];
	size_t bytes;
	int ok = 1;

	if (!input)
		return errno == ENOENT;
	/* Optional loading art is small; do not copy arbitrary oversized files. */
	if (fseek(input, 0, SEEK_END) != 0 || ftell(input) > 1024 * 1024 || fseek(input, 0, SEEK_SET) != 0)
	{
		fclose(input);
		return 1;
	}
	output = fopen(READY "/loading.tga", "wb");
	if (!output)
	{
		fclose(input);
		return 0;
	}
	while ((bytes = fread(buffer, 1, sizeof(buffer), input)) != 0)
	{
		if (cancelled(NULL) || fwrite(buffer, 1, bytes, output) != bytes)
		{
			ok = 0;
			break;
		}
	}
	ok = ok && !ferror(input) && fflush(output) == 0 && fsync(fileno(output)) == 0;
	fclose(input);
	if (fclose(output) != 0)
		ok = 0;
	return ok;
}

static int worker(int argc, void *argv)
{
	char problem[256] = "", spelling[256], source[1024], destination[1024];
	int ok = 0;
	unsigned index;

	(void)argc;
	(void)argv;
	if (!prepare_staging())
	{
		snprintf(problem, sizeof(problem), "Cannot prepare data/iso-import. Check its owner marker and the card's free space.");
		goto done;
	}
	if (!xiso_extract_maps_cancellable(setup.image, STAGING, extraction_progress, NULL,
		problem, sizeof(problem), cancelled))
		goto done;
	/* Check the complete retail set before converting anything. */
	for (index = 0; index < sizeof(retail_maps) / sizeof(retail_maps[0]); index++)
	{
		if (!posix_find_entry_case_insensitive(RAW, retail_maps[index], spelling, sizeof(spelling)))
		{
			snprintf(problem, sizeof(problem), "The disc is missing %s. Use a complete original-Xbox Halo disc image.", retail_maps[index]);
			goto done;
		}
	}
	for (index = 0; index < sizeof(retail_maps) / sizeof(retail_maps[0]); index++)
	{
		if (cancelled(NULL))
			goto done;
		posix_find_entry_case_insensitive(RAW, retail_maps[index], spelling, sizeof(spelling));
		snprintf(source, sizeof(source), RAW "/%s", spelling);
		snprintf(destination, sizeof(destination), READY "/%s", retail_maps[index]);
		OSLockMutex(&setup.mutex);
		setup.map_number = (int)index + 1;
		OSUnlockMutex(&setup.mutex);
		update("Preparing", retail_maps[index], 0);
		if (!xbox_map_import_file(source, destination, conversion_progress, NULL, problem, sizeof(problem)))
			goto done;
	}
	if (!copy_loading_picture())
	{
		snprintf(problem, sizeof(problem), "Cannot write the loading picture. Check free space.");
		goto done;
	}
	if (cancelled(NULL))
		goto done;
	/* The complete directory is published once. Existing maps are never replaced. */
	{
		struct stat info;

		if (lstat(MAPS, &info) == 0 || errno != ENOENT || rename(READY, MAPS) != 0)
		{
			snprintf(problem, sizeof(problem), "Cannot install maps. Existing data/maps was left untouched.");
			goto done;
		}
	}
	update("Cleaning temporary files", "", 100);
	if (clear_directory(STAGING, 0))
	{
		unlink(STAGING "/owner.txt");
		rmdir(STAGING);
	}
	ok = 1;
done:
	OSLockMutex(&setup.mutex);
	snprintf(setup.problem, sizeof(setup.problem), "%s", setup.cancelled ?
		"Import cancelled. Your maps and saves were left untouched." : problem);
	setup.success = ok;
	setup.done = 1;
	OSUnlockMutex(&setup.mutex);
	return 0;
}

int wiiu_import_startup(void)
{
	char const *images[] = { WIIU_DATA_DIRECTORY "/halo.iso", WIIU_DATA_DIRECTORY "/halo.xiso",
		WIIU_DATA_DIRECTORY "/halo.iso.0" };
	char displayed[384] = "";
	void *stack = NULL;
	FILE *file, *record = NULL;
	struct stat info;
	int has_maps = 0, has_shaders = 0, found = 0, active = 0, ready = 0, started = 0, closing = 0, menu_requested = 0;
	unsigned index;

	/* Preserve the zero-setup startup path for an existing installation. */
	if (stat(MAPS, &info) == 0)
	{
		unsigned char header[8];

		has_maps = 1;
		file = fopen(MAPS "/ui.map", "rb");
		if (file)
		{
			ready = fread(header, 1, sizeof(header), file) == sizeof(header) &&
				!memcmp(header, "head\0\0\0\5", 8);
			fclose(file);
		}
		if (ready)
			return 1;
	}
	file = fopen(WIIU_DATA_DIRECTORY "/shaders.pack", "rb");
	if (file)
	{
		char magic[4];

		has_shaders = fread(magic, 1, sizeof(magic), file) == sizeof(magic) &&
			!memcmp(magic, "HSP1", 4) && fseek(file, 0, SEEK_END) == 0 && ftell(file) >= 16;
		fclose(file);
	}
	memset(&setup, 0, sizeof(setup));
	OSInitMutex(&setup.mutex);
	for (index = 0; index < sizeof(images) / sizeof(images[0]); index++)
	{
		if (stat(images[index], &info) == 0 && S_ISREG(info.st_mode))
		{
			snprintf(setup.image, sizeof(setup.image), "%s", images[index]);
			found = 1;
			break;
		}
	}
	/* libwhb's software console uses OSScreen, not GX2. Its current Init
	returns FALSE even after success, so it is not a success predicate. */
	WHBLogConsoleInit();
	WHBLogConsoleSetColor(0x0C1C30FF);
	VPADInit();
	mkdir(DATA, 0755);
	record = fopen(WIIU_DATA_DIRECTORY "/import-log.txt", "w");
	WHBLogPrintf("OpenCE-WiiU - first-time setup");
	if (has_maps)
		WHBLogPrintf("Existing data/maps is not ready. Back it up and move it aside before importing. B: Wii U Menu");
	else if (!has_shaders)
		WHBLogPrintf("Copy shaders.pack from the same release as halo.wuhb into wiiu/apps/halo first. B: Wii U Menu");
	else if (!found)
	{
		WHBLogPrintf("Put your own Xbox Halo disc image at wiiu/apps/halo/halo.iso, then start again.");
		WHBLogPrintf("Split images: halo.iso.0, halo.iso.1, ... (parts under 4 GiB).");
		WHBLogPrintf("Or use the computer setup helper to prepare data/maps. B: Wii U Menu");
	}
	else
	{
		WHBLogPrintf("Xbox disc image found. Import may take several minutes. Keep the console powered on.");
		WHBLogPrintf("Allow at least 5 GiB free in addition to the ISO. Working maps and saves are preserved.");
		WHBLogPrintf("A: prepare maps on this Wii U    B: Wii U Menu");
	}
	while (WHBProcIsRunning())
	{
		VPADStatus pad;
		VPADReadError error = VPAD_READ_NO_SAMPLES;
		int done, success;

		memset(&pad, 0, sizeof(pad));
		if (ProcUIInForeground())
			VPADRead(VPAD_CHAN_0, &pad, 1, &error);
		if (error == VPAD_READ_SUCCESS && (pad.trigger & VPAD_BUTTON_B))
		{
			OSLockMutex(&setup.mutex);
			setup.cancelled = 1;
			OSUnlockMutex(&setup.mutex);
			closing = 1;
			WHBLogPrintf("Finishing the current operation before closing...");
		}
		if (!active && !started && !closing && found && has_shaders && !has_maps && error == VPAD_READ_SUCCESS &&
			(pad.trigger & VPAD_BUTTON_A))
		{
			stack = memalign(32, IMPORT_STACK);
			if (stack && OSCreateThread(&setup.worker, worker, 0, NULL,
				(char *)stack + IMPORT_STACK, IMPORT_STACK, 16, OS_THREAD_ATTRIB_AFFINITY_CPU0))
			{
				active = started = 1;
				OSResumeThread(&setup.worker);
			}
			else
			{
				free(stack);
				stack = NULL;
				WHBLogPrintf("Cannot start setup. B: Wii U Menu");
			}
		}
		OSLockMutex(&setup.mutex);
		done = setup.done;
		success = setup.success;
		if (active)
		{
			char line[384];

			snprintf(line, sizeof(line), "%s %s - %lu%% (map %d/24)",
				setup.stage, setup.name, setup.percent / 5 * 5, setup.map_number);
			if (strcmp(displayed, line))
			{
				snprintf(displayed, sizeof(displayed), "%s", line);
				WHBLogPrint(line);
				if (record)
				{
					fprintf(record, "%s\n", line);
					fflush(record);
				}
			}
		}
		OSUnlockMutex(&setup.mutex);
		if (active && done)
		{
			OSJoinThread(&setup.worker, NULL);
			free(stack);
			stack = NULL;
			active = 0;
			if (success)
			{
				WHBLogPrintf(closing ? "Maps are ready. Returning to the Wii U Menu." :
					"Maps are ready. Starting Halo. You may remove the ISO later.");
				if (!closing)
				{
					ready = 1;
					break;
				}
			}
			else
			{
				WHBLogPrint(setup.problem);
				if (record)
					fprintf(record, "%s\n", setup.problem);
				if (!closing)
					WHBLogPrintf("Fix the issue, then relaunch to retry. B: Wii U Menu");
			}
		}
		if (closing && !active && !menu_requested)
		{
			menu_requested = 1;
			SYSLaunchMenu();
		}
		WHBLogConsoleDraw();
		OSSleepTicks(OSMillisecondsToTicks(50));
	}
	OSLockMutex(&setup.mutex);
	setup.cancelled = 1;
	OSUnlockMutex(&setup.mutex);
	if (active)
		OSJoinThread(&setup.worker, NULL);
	free(stack);
	if (record)
		fclose(record);
	WHBLogConsoleFree();
	/* These are the setup console's callbacks. The game registers its own
	only after this function returns; stale console callbacks must not run. */
	if (ready)
		ProcUIClearCallbacks();
	return ready;
}
