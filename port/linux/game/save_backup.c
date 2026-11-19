/*
SAVE_BACKUP.C

The rolling backup of the saved game and the profiles (save_backup.h).
*/

#include "save_backup.h"

#ifdef HALO_WIIU

#include <stdio.h>
#include <string.h>
#ifndef SAVE_BACKUP_HOST
#include <xtl.h>
#endif

void platform_log(char const *format, ...);
long wiiu_study_number(const char *name, long fallback);

#ifndef FILE_SHARE_READ
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#endif
#define BACKUP_PATH 1100

int save_backup_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
	{
		/* (-1 and no file: on) */
		enabled = wiiu_study_number("save_backup.txt", -1) != 0;
		platform_log("saves: %s (halo/save_backup.txt)",
			enabled ? "the last good copy of the saved game and of each profile is kept as NAME.bak before it is written over, and tried if the file fails its checksum" : "no backup of the saved game or the profiles is kept");
	}
	return enabled;
}

static int named(char *to, const char *path, const char *suffix)
{
	if (strlen(path)+strlen(suffix) >= BACKUP_PATH)
		return 0;
	strcpy(to, path);
	strcat(to, suffix);
	return 1;
}

static int exists(const char *path)
{
	return GetFileAttributesA(path) != (unsigned long)-1;
}

void save_backup_keep(const char *path, int by_rename, unsigned long expected_size, unsigned long head_bytes,
	int (*good)(const unsigned char *head, unsigned long got), const char *what)
{
	char backup[BACKUP_PATH];
	unsigned char head[4096];
	unsigned long got = 0, size, index;
	int is_good = 0;
	HANDLE file;

	if (!save_backup_enabled() || !named(backup, path, ".bak"))
		return;
	file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return; /* (nothing there to keep) */
	size = GetFileSize(file, NULL);
	if (head_bytes > sizeof(head))
		head_bytes = sizeof(head);
	if (size != (unsigned long)-1 && (!expected_size || size == expected_size) && head_bytes)
	{
		DWORD read = 0;

		if (ReadFile(file, head, head_bytes, &read, NULL))
			got = read;
		if (good)
			is_good = good(head, got);
		else
			for (index = 0; index < got && !is_good; index++)
				is_good = head[index] != 0;
	}
	CloseHandle(file);
	if (!is_good)
	{
		platform_log("saves: %s is not a good copy (empty, cut short or failing its checksum): its backup is %s", what, exists(backup) ? "kept as it is" : "not there");
		return;
	}
	if (exists(backup))
		DeleteFileA(backup);
	if (by_rename ? MoveFileA(path, backup) : CopyFileA(path, backup, FALSE))
		platform_log("saves: the last good %s kept as a backup (%s) before it is written", what, by_rename ? "renamed" : "copied");
	else
		platform_log("saves: the backup of %s could not be made (error %lu): it is written over as it was", what, (unsigned long)GetLastError());
}

int save_backup_read(const char *path, void *buffer, unsigned long bytes, const char *what)
{
	char backup[BACKUP_PATH];
	HANDLE file;
	DWORD read = 0;
	int ok = 0;

	if (!save_backup_enabled() || !named(backup, path, ".bak"))
		return 0;
	file = CreateFileA(backup, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return 0;
	ok = ReadFile(file, buffer, bytes, &read, NULL) && read == bytes;
	CloseHandle(file);
	platform_log("saves: %s failed its checksum: its backup %s", what, ok ? "was read" : "could not be read");
	return ok;
}

int save_backup_swap_in(const char *path, const char *what)
{
	char backup[BACKUP_PATH], bad[BACKUP_PATH];

	if (!save_backup_enabled() || !named(backup, path, ".bak") || !named(bad, path, ".bad") || !exists(backup))
		return 0;
	if (exists(bad))
		DeleteFileA(bad); /* (an earlier swap's that was left by a cut: a file that failed its checksum) */
	if (exists(path) && !MoveFileA(path, bad))
	{
		platform_log("saves: %s failed its checksum and could not be put away (error %lu): the backup is not tried", what, (unsigned long)GetLastError());
		return 0;
	}
	if (!MoveFileA(backup, path))
	{
		platform_log("saves: %s failed its checksum; the backup could not be put in its place (error %lu): put back", what, (unsigned long)GetLastError());
		if (exists(bad))
			MoveFileA(bad, path);
		return 0;
	}
	platform_log("saves: %s failed its checksum: the backup is tried in its place", what);
	return 1;
}

void save_backup_swap_out(const char *path, int ok, const char *what)
{
	char backup[BACKUP_PATH], bad[BACKUP_PATH];

	if (!named(backup, path, ".bak") || !named(bad, path, ".bad"))
		return;
	if (ok)
	{
		if (exists(bad))
			DeleteFileA(bad);
		platform_log("saves: %s is the backup's now (the file that failed its checksum is removed)", what);
		return;
	}
	/* (the backup failed too: both put back where they were, nothing removed) */
	if (exists(bad))
	{
		if (!exists(backup) && MoveFileA(path, backup))
			MoveFileA(bad, path);
		else
			platform_log("saves: %s: the backup failed too and was left in place (%s.bad holds the first file)", what, path);
	}
	platform_log("saves: %s: the backup failed its checksum too: both files are kept as they were", what);
}

#endif
