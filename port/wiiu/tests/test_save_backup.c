/* port/linux/game/save_backup.c (the rolling backup of the saved game and the profiles) on its own, over the Win32 calls it makes done with POSIX files
   cc -std=gnu11 -Wall -Werror port/wiiu/tests/test_save_backup.c   (run in a folder it may write in: argv[1], a fresh one)
   Exit status 0 when everything holds; tools/test_wiiu_save_backup.py runs it. */

#define HALO_WIIU
#define SAVE_BACKUP_HOST
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef void *HANDLE;
typedef unsigned long DWORD;
typedef int BOOL;
#define FALSE 0
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define GENERIC_READ 1
#define OPEN_EXISTING 3
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2

static HANDLE CreateFileA(const char *path, DWORD access, DWORD share, void *security, DWORD how, DWORD flags, void *template_file)
{
	int fd = open(path, O_RDONLY);

	(void)access; (void)share; (void)security; (void)how; (void)flags; (void)template_file;
	return fd < 0 ? INVALID_HANDLE_VALUE : (HANDLE)(long)(fd + 1);
}
static DWORD GetFileSize(HANDLE h, DWORD *high)
{
	struct stat s;

	(void)high;
	return fstat((int)(long)h - 1, &s) ? (DWORD)-1 : (DWORD)s.st_size;
}
static BOOL ReadFile(HANDLE h, void *buffer, DWORD bytes, DWORD *read_bytes, void *overlapped)
{
	ssize_t got = read((int)(long)h - 1, buffer, bytes);

	(void)overlapped;
	*read_bytes = got < 0 ? 0 : (DWORD)got;
	return got >= 0;
}
static BOOL CloseHandle(HANDLE h) { return close((int)(long)h - 1) == 0; }
static BOOL DeleteFileA(const char *path) { return unlink(path) == 0; }
static BOOL MoveFileA(const char *from, const char *to)
{
	struct stat s;

	if (stat(to, &s) == 0) /* (as the port's: it does not replace) */
		return 0;
	return rename(from, to) == 0;
}
static BOOL CopyFileA(const char *from, const char *to, BOOL fail_if_exists)
{
	char buffer[4096];
	ssize_t got;
	int in = open(from, O_RDONLY), out;

	if (in < 0)
		return 0;
	out = open(to, O_WRONLY | O_CREAT | (fail_if_exists ? O_EXCL : O_TRUNC), 0644);
	if (out < 0)
	{
		close(in);
		return 0;
	}
	while ((got = read(in, buffer, sizeof(buffer))) > 0)
		if (write(out, buffer, (size_t)got) != got)
			break;
	close(in);
	close(out);
	return got == 0;
}
static DWORD GetFileAttributesA(const char *path) { struct stat s; return stat(path, &s) ? (DWORD)-1 : 0x80; }
static DWORD GetLastError(void) { return 0; }

static int logged;
void platform_log(char const *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vprintf(format, arguments);
	printf("\n");
	va_end(arguments);
	logged++;
}
long wiiu_study_number(const char *name, long fallback)
{
	const char *value = getenv("HALO_SAVE_BACKUP");

	return !strcmp(name, "save_backup.txt") && value ? strtol(value, NULL, 10) : fallback;
}

#include "../../linux/game/save_backup.c"

static int failures;
#define CHECK(condition) do { if (!(condition)) { printf("check failed, line %d: %s\n", __LINE__, #condition); failures++; } } while (0)

static void put(const char *path, const char *text, size_t size)
{
	FILE *f = fopen(path, "wb");
	size_t i;

	for (i = 0; i < size; i++)
		fputc(text[i % strlen(text)], f);
	fclose(f);
}
static int has(const char *path) { struct stat s; return stat(path, &s) == 0; }
static int starts(const char *path, char c) { FILE *f = fopen(path, "rb"); int got = f ? fgetc(f) : -1; if (f) fclose(f); return got == c; }
static int profile_good(const unsigned char *head, unsigned long got) { return got >= 8 && head[0] == 'P'; }

int main(int argc, char **argv)
{
	char path[600], backup[600], bad[600];

	if (argc < 2)
		return 2;
	snprintf(path, sizeof(path), "%s/savegame.bin", argv[1]);
	snprintf(backup, sizeof(backup), "%s.bak", path);
	snprintf(bad, sizeof(bad), "%s.bad", path);

	/* a good saved game is renamed to its backup; the old backup is replaced */
	put(backup, "old", 1000);
	put(path, "Gnew", 1000);
	save_backup_keep(path, 1, 1000, 64, NULL, "saved game");
	CHECK(!has(path) && starts(backup, 'G'));

	/* the write that was cut: the header zeroed. The backup stays the only good copy. */
	{
		unsigned char zeroes[1000] = { 0 };
		FILE *f = fopen(path, "wb");

		fwrite(zeroes, 1, sizeof(zeroes), f);
		fclose(f);
	}
	save_backup_keep(path, 1, 1000, 64, NULL, "saved game");
	CHECK(has(path) && starts(backup, 'G'));

	/* a file of the wrong size is not good either */
	put(path, "Hshort", 500);
	save_backup_keep(path, 1, 1000, 64, NULL, "saved game");
	CHECK(has(path) && starts(backup, 'G'));

	/* swap in: the failing file put away and the backup in its place; good: the failing one removed */
	CHECK(save_backup_swap_in(path, "the saved game") == 1);
	CHECK(starts(path, 'G') && starts(bad, 'H') && !has(backup));
	save_backup_swap_out(path, 1, "the saved game");
	CHECK(starts(path, 'G') && !has(bad) && !has(backup));

	/* swap in with the backup failing too: everything back as it was, nothing removed */
	put(backup, "Zback", 100);
	put(path, "Yfirst", 100);
	CHECK(save_backup_swap_in(path, "the saved game") == 1);
	save_backup_swap_out(path, 0, "the saved game");
	CHECK(starts(path, 'Y') && starts(backup, 'Z') && !has(bad));

	/* no backup: nothing to swap */
	unlink(backup);
	CHECK(save_backup_swap_in(path, "the saved game") == 0 && starts(path, 'Y'));

	/* a profile: copied, with the callback's idea of good; a bad one keeps the backup */
	snprintf(path, sizeof(path), "%s/00.sav", argv[1]);
	snprintf(backup, sizeof(backup), "%s.bak", path);
	put(path, "Pgood....", 512);
	save_backup_keep(path, 0, 0, 512, profile_good, "player profile");
	CHECK(has(path) && starts(backup, 'P'));
	put(path, "Xbroken..", 512);
	save_backup_keep(path, 0, 0, 512, profile_good, "player profile");
	CHECK(starts(path, 'X') && starts(backup, 'P'));
	{
		char block[512];

		CHECK(save_backup_read(path, block, 512, "a player profile") == 1 && block[0] == 'P');
		CHECK(save_backup_read(path, block, 600, "a player profile") == 0);
	}

	/* a file that is not there leaves the backup alone */
	unlink(path);
	save_backup_keep(path, 0, 0, 512, profile_good, "player profile");
	CHECK(!has(path) && starts(backup, 'P'));

	printf("%d failures\n", failures);
	return failures ? 1 : 0;
}
