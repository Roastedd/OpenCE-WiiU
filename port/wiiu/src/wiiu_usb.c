/*
WIIU_USB.C

A folder on a USB drive for the checkpoint file (z:\savegame.bin: the game state, 20 MB, written at every checkpoint), a test of whether the
SD card's slow writes, and the map reads they hold up, are behind the stalls at a checkpoint.

halo/checkpoint_dir.txt on the card turns it on. Its first line is the name of the folder to use on the drive ("opence-halo" if the line is empty).
The game then mounts the drive's volume (/vol/storage_usb01, the Wii U's own mount of a drive it formatted) through libmocha, which Aroma's Mocha
payload serves, makes the folder and keeps savegame.bin in it. Without the file nothing here runs, and without libmocha in the build
(WIIU_MOCHA=1 at the build, tools/wiiu_build.py) the file is refused with a line in the log. Any failure leaves the file on the card, as before.
*/

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef HALO_WIIU_USB
#include <mocha/mocha.h>
#endif

void platform_log(const char *format, ...);

#define CHECKPOINT_FILE "/vol/external01/wiiu/apps/halo/checkpoint_dir.txt"

static char checkpoint_folder[160];
static int checkpoint_state; /* 0 not asked yet, 1 on, -1 off */

/* the folder (host path) for the checkpoint file, or NULL: it stays on the card */
const char *wiiu_checkpoint_dir(void)
{
	char name[96];
	FILE *file;
	size_t length;

	if (checkpoint_state)
		return checkpoint_state > 0 ? checkpoint_folder : NULL;
	checkpoint_state = -1;
	file = fopen(CHECKPOINT_FILE, "r");
	if (!file)
		return NULL;
	name[0] = 0;
	if (!fgets(name, sizeof(name), file))
		name[0] = 0;
	fclose(file);
	length = strlen(name);
	while (length && (name[length - 1] == '\n' || name[length - 1] == '\r' || name[length - 1] == ' ' || name[length - 1] == '\t'))
		name[--length] = 0;
	if (!name[0])
		strcpy(name, "opence-halo");
	if (strchr(name, '/') || strchr(name, '\\') || strchr(name, ':') || !strcmp(name, "..") || !strcmp(name, "."))
	{
		platform_log("checkpoint dir: the name \"%s\" in halo/checkpoint_dir.txt is not a plain folder name; the checkpoint stays on the card", name);
		return NULL;
	}
#ifdef HALO_WIIU_USB
	{
		MochaUtilsStatus status = Mocha_InitLibrary();
		struct stat information;

		if (status != MOCHA_RESULT_SUCCESS)
		{
			platform_log("checkpoint dir: libmocha did not start (%s); the checkpoint stays on the card", Mocha_GetStatusStr(status));
			return NULL;
		}
		status = Mocha_MountFS("storage_usb", NULL, "/vol/storage_usb01");
		if (status != MOCHA_RESULT_SUCCESS)
		{
			platform_log("checkpoint dir: the USB drive could not be mounted (%s); the checkpoint stays on the card", Mocha_GetStatusStr(status));
			return NULL;
		}
		snprintf(checkpoint_folder, sizeof(checkpoint_folder), "storage_usb:/%s", name);
		if (stat(checkpoint_folder, &information) != 0)
			mkdir(checkpoint_folder, 0777);
		if (stat(checkpoint_folder, &information) != 0)
		{
			platform_log("checkpoint dir: %s could not be made on the USB drive; the checkpoint stays on the card", checkpoint_folder);
			return NULL;
		}
		platform_log("checkpoint dir: the checkpoint file is on the USB drive, in %s", checkpoint_folder);
		checkpoint_state = 1;
		return checkpoint_folder;
	}
#else
	platform_log("checkpoint dir: this build has no USB support (it is built with WIIU_MOCHA=1); the checkpoint stays on the card");
	return NULL;
#endif
}
