/*
WIIU_USB.C

A folder on a USB drive for the checkpoint file (z:\savegame.bin: the game state, 20 MB, written at every checkpoint), a test of whether the
SD card's slow writes, and the map reads they hold up, are behind the stalls at a checkpoint.

halo/checkpoint_dir.txt on the card turns it on. Its first line is the name of the folder to use on the drive ("opence-halo" if the line is empty).
The game then mounts the drive's volume (/vol/storage_usb01, the Wii U's own mount of a drive it formatted) through libmocha, which Aroma's Mocha
payload serves, makes the folder and keeps savegame.bin in it. Without the file nothing here runs, and without libmocha in the build
(WIIU_MOCHA=1 at the build, tools/wiiu_build.py) the file is refused with a line in the log. Any failure leaves the file on the card, as before.
*/

#include <fcntl.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef HALO_WIIU_USB
#include <mocha/mocha.h>
#endif

void platform_log(const char *format, ...);

#define CHECKPOINT_FILE "/vol/external01/wiiu/apps/halo/checkpoint_dir.txt"


/* how fast a folder takes writes: bytes written in pieces to a new file (optionally after making it its full size first, as the game state's file is
made), in ms, logged. A test of the USB drive against the card (halo/checkpoint_dir.txt). */
static double bench_now_ms(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
}

static void bench_write(const char *folder, const char *label, unsigned long total, unsigned long piece, int preallocate)
{
	char path[256];
	char *buffer = memalign(64, piece);
	int fd;
	unsigned long done = 0;
	double start, wrote_ms, read_ms = 0.0;

	if (!buffer)
		return;
	memset(buffer, 0x5a, piece);
	snprintf(path, sizeof(path), "%s/bench.tmp", folder);
	unlink(path);
	fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0666);
	if (fd < 0)
	{
		platform_log("bench %s: %s could not be opened", label, path);
		free(buffer);
		return;
	}
	start = bench_now_ms();
	if (preallocate)
		ftruncate(fd, (off_t)total);
	while (done < total)
	{
		ssize_t wrote = write(fd, buffer, piece < total - done ? piece : total - done);

		if (wrote <= 0)
			break;
		done += (unsigned long)wrote;
	}
	wrote_ms = bench_now_ms() - start;
	start = bench_now_ms();
	fsync(fd);
	{
		const double sync_ms = bench_now_ms() - start;

		lseek(fd, 0, SEEK_SET);
		start = bench_now_ms();
		for (done = 0; done < total;)
		{
			ssize_t got = read(fd, buffer, piece);

			if (got <= 0)
				break;
			done += (unsigned long)got;
		}
		read_ms = bench_now_ms() - start;
		platform_log("bench %s: %lu KB in pieces of %lu KB: written in %.0f ms (%.2f MB/s), fsync %.0f ms, read back in %.0f ms (%.2f MB/s)", label, total / 1024,
			piece / 1024, wrote_ms, wrote_ms > 0 ? total / 1048576.0 / (wrote_ms / 1000.0) : 0.0, sync_ms, read_ms, read_ms > 0 ? total / 1048576.0 / (read_ms / 1000.0) : 0.0);
	}
	close(fd);
	unlink(path);
	free(buffer);
}

static void bench_folder(const char *folder, const char *name)
{
	char label[64];

	snprintf(label, sizeof(label), "%s 128K", name);
	bench_write(folder, label, 2 * 1024 * 1024, 128 * 1024, 0);
	snprintf(label, sizeof(label), "%s 128K prealloc", name);
	bench_write(folder, label, 2 * 1024 * 1024, 128 * 1024, 1);
	snprintf(label, sizeof(label), "%s 32K", name);
	bench_write(folder, label, 2 * 1024 * 1024, 32 * 1024, 0);
	snprintf(label, sizeof(label), "%s 1M", name);
	bench_write(folder, label, 2 * 1024 * 1024, 1024 * 1024, 0);
}

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
		bench_folder("/vol/external01/wiiu/apps/halo", "sd card");
		bench_folder(checkpoint_folder, "usb drive");
		platform_log("checkpoint dir: the checkpoint file is on the USB drive, in %s", checkpoint_folder);
		checkpoint_state = 1;
		return checkpoint_folder;
	}
#else
	platform_log("checkpoint dir: this build has no USB support (it is built with WIIU_MOCHA=1); the checkpoint stays on the card");
	return NULL;
#endif
}
