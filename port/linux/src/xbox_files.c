/*
XBOX_FILES.C

Win32 file API for the Linux build, over POSIX descriptors.

Xbox paths are translated below two roots. d:\ is the data root, the
directory holding the game's maps/ folder: paths.data (port_config.c), else the current
directory when it has maps/, else assets/ in the current directory or two
levels above the executable (the repository root for build/linux/halo).
h:\ is the Halo Custom Edition install paths.custom_edition names, if any
(its maps folder holds the resource maps Custom Edition maps need, and more
maps).
Every other drive letter X:\ is the subdirectory X/ of the save root (z:\ holds the persistent cache and saves,
u:\ user data, t:\ title data): paths.saves, else
$XDG_DATA_HOME/halo-linux or ~/.local/share/halo-linux. Path components are
matched case-insensitively, like the Xbox's FATX volumes.
*/

#ifdef HALO_WIIU
void *memalign(unsigned long alignment, unsigned long size); /* newlib's, malloc.h */
#endif
#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "server_mode.h"
#include "halo_startup.h"
#include "halo_file_counts.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---------- the menus' file work, counted (halo_file_counts.h) */

volatile int file_counting;
static unsigned long file_count_totals[NUMBER_OF_FILE_COUNTS];
static pthread_t file_counting_thread;

void file_counts_enable(int on)
{
	if (on)
		file_counting_thread = pthread_self();
	file_counting = on;
}

void file_count_add(int kind, unsigned long amount)
{
	if (kind >= 0 && kind < NUMBER_OF_FILE_COUNTS && pthread_equal(pthread_self(), file_counting_thread))
		file_count_totals[kind] += amount;
}

void file_counts_read(unsigned long counts[NUMBER_OF_FILE_COUNTS])
{
	memcpy(counts, file_count_totals, sizeof(file_count_totals));
}

unsigned long file_counts_card_calls(unsigned long const counts[NUMBER_OF_FILE_COUNTS])
{
	return counts[FILE_COUNT_SCAN] + counts[FILE_COUNT_SCAN_ENTRY] + counts[FILE_COUNT_STAT] + counts[FILE_COUNT_OPEN] +
		counts[FILE_COUNT_FIND] + counts[FILE_COUNT_FIND_ENTRY];
}

/* every stat of this file goes through here, to be counted */
static int counted_stat(const char *path, struct posix_file_information *information)
{
	FILE_COUNT(FILE_COUNT_STAT, 1);
	return posix_stat(path, information);
}

/* ---------- the directories' names, kept (directory_cache.h): on the Wii U only (the card there is not case sensitive, so that a name kept out of date
changes a path's spelling and nothing else); the digest harness builds it too (HALO_DIRECTORY_CACHE) */

#if defined(HALO_WIIU) || defined(HALO_DIRECTORY_CACHE)
#define USE_DIRECTORY_CACHE 1

static void *counted_directory_open(const char *path)
{
	FILE_COUNT(FILE_COUNT_SCAN, 1);
	return posix_directory_open(path);
}

static int counted_directory_next(void *directory, char *name, unsigned long size)
{
	int result = posix_directory_next(directory, name, size);

	if (result)
		FILE_COUNT(FILE_COUNT_SCAN_ENTRY, 1);
	return result;
}

static unsigned long long directory_cache_milliseconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long long)now.tv_sec * 1000ull + (unsigned long long)now.tv_nsec / 1000000ull;
}

#define DIRECTORY_CACHE_OPEN(path) counted_directory_open(path)
#define DIRECTORY_CACHE_NEXT(handle, name, size) counted_directory_next((handle), (name), (size))
#define DIRECTORY_CACHE_CLOSE(handle) posix_directory_close(handle)
#define DIRECTORY_CACHE_NOW() directory_cache_milliseconds()
#include "directory_cache.h"
#define DIRECTORY_CACHE_FLUSH() directory_cache_flush()

long wiiu_study_number(const char *name, long fallback);

/* the card's halo/directory_cache.txt (HALO_DIRECTORY_CACHE on the host): 0 reads the directories again for each component of each path, as before, to compare a run */
static int directory_cache_enabled(void)
{
	static volatile int state;

	if (!state)
		state = wiiu_study_number("directory_cache.txt", 1) != 0 ? 1 : 2;
	return state == 1;
}
#else
#define DIRECTORY_CACHE_FLUSH() ((void)0)
#endif

/* ---------- paths */

static BOOL directory_exists(const char *path)
{
	struct posix_file_information information;

	return counted_stat(path, &information) == 0;
}

/* whether directory has a maps folder, in any case */
static BOOL has_maps(const char *directory)
{
	char on_disk[256];

	return directory_exists(directory) &&
		posix_find_entry_case_insensitive(directory, "maps", on_disk, sizeof(on_disk));
}

static void trim_separators(char *path)
{
	while (strlen(path) > 1 && path[strlen(path) - 1] == '/')
		path[strlen(path) - 1] = '\0';
}

const char *platform_data_root(void)
{
	static char root[MAX_PATH];

	if (!root[0])
	{
		const char *environment = config_string("paths.data");

		if (*environment)
		{
			snprintf(root, sizeof(root), "%s", environment);
		}
		else if (has_maps("."))
		{
			snprintf(root, sizeof(root), ".");
		}
#ifdef HALO_WIIU
		else if (has_maps("/vol/external01/wiiu/apps/halo/data"))
		{
			/* the SD card: the maps converted and rebased for this window go beside the app (tools/ppc-be/convert_maps.sh, REBASE) */
			snprintf(root, sizeof(root), "/vol/external01/wiiu/apps/halo/data");
		}
#endif
		else
		{
			char executable[MAX_PATH];
			char executable_directory[MAX_PATH] = "";
			ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);

			if (length > 0)
			{
				char *slash;

				executable[length] = '\0';
				snprintf(executable_directory, sizeof(executable_directory), "%s", executable);
				slash = strrchr(executable_directory, '/');
				if (slash)
					*slash = '\0';
			}
			snprintf(root, sizeof(root), ".");
			if (executable_directory[0] && has_maps(executable_directory))
			{
				/* next to the executable, where the desktop ports' first start
				extracts it (platform_offer_game_data) */
				snprintf(root, sizeof(root), "%s", executable_directory);
			}
			else if (has_maps("assets"))
			{
				snprintf(root, sizeof(root), "assets");
			}
			else if (length > 0)
			{
				/* <repository>/build/linux/halo -> <repository>/assets */
				char *slash;
				int level;

				executable[length] = '\0';
				for (level = 0; level < 3 && (slash = strrchr(executable, '/')); level++)
					*slash = '\0';
				if (level == 3 && strlen(executable) + sizeof("/assets") <= sizeof(executable))
				{
					strcat(executable, "/assets");
					if (has_maps(executable))
						snprintf(root, sizeof(root), "%s", executable);
				}
			}
#if !defined(HALO_ANDROID) && !defined(HALO_WEB)
			/* (a dedicated server asks nobody: it says what is missing and
			stops, server_mode.c) */
			if (!has_maps(root) && executable_directory[0] && !halo_server_mode() &&
				platform_offer_game_data(executable_directory) &&
				has_maps(executable_directory))
			{
				snprintf(root, sizeof(root), "%s", executable_directory);
			}
#endif
			if (!has_maps(root))
				platform_log("no maps/ folder found; set paths.data in config.toml to the folder that holds maps/");
		}
		trim_separators(root);
		platform_log("data root: %s (the game's log: debug.txt there)", root);
	}
	return root;
}

/* creates every missing directory along path */
static void make_directories(const char *path)
{
	char partial[MAX_PATH];
	unsigned long index;

	snprintf(partial, sizeof(partial), "%s", path);
	for (index = 1; partial[index]; index++)
	{
		if (partial[index] == '/')
		{
			partial[index] = '\0';
			if (!directory_exists(partial))
			{
				posix_make_directory(partial);
				DIRECTORY_CACHE_FLUSH();
			}
			partial[index] = '/';
		}
	}
	if (!directory_exists(partial))
	{
		posix_make_directory(partial);
		DIRECTORY_CACHE_FLUSH();
	}
}

const char *platform_save_root(void)
{
	static char root[MAX_PATH];

	if (!root[0])
	{
		const char *environment = config_string("paths.saves");
		const char *data_home = getenv("XDG_DATA_HOME");
		const char *home = getenv("HOME");

		if (*environment)
			snprintf(root, sizeof(root), "%s", environment);
#ifdef _WIN32
		/* the Windows build (port/windows) keeps saves in the roaming
		application data folder */
		else if (getenv("APPDATA") && *getenv("APPDATA"))
			snprintf(root, sizeof(root), "%s/halo", getenv("APPDATA"));
#endif
		else if (data_home && *data_home)
			snprintf(root, sizeof(root), "%s/halo-linux", data_home);
		else if (home && *home)
			snprintf(root, sizeof(root), "%s/.local/share/halo-linux", home);
		else
			snprintf(root, sizeof(root), "%s", platform_data_root());
		trim_separators(root);
		make_directories(root);
		platform_log("save root: %s", root);
	}
	return root;
}

const char *platform_custom_edition_root(void)
{
	static char root[MAX_PATH];
	static int found;

	if (!found)
	{
		char maps[MAX_PATH + 8];
		struct posix_file_information information;

		found = 1;
		snprintf(root, sizeof(root), "%s", config_string("paths.custom_edition"));
		trim_separators(root);
		snprintf(maps, sizeof(maps), "%s/maps", root);
		if (root[0] && counted_stat(maps, &information) != 0)
		{
			platform_log("Halo Custom Edition: %s has no maps folder; not using it", root);
			root[0] = 0;
		}
		if (root[0])
			platform_log("Halo Custom Edition: using the install at %s", root);
	}
	return root;
}

/* whether the first length characters of a and b match, ignoring case
(the Windows build has no strings.h); a shorter string matches only up to
its end, which must come at the same place in both */
static BOOL names_match(const char *a, const char *b, unsigned long length)
{
	unsigned long index;

	for (index = 0; index < length; index++)
	{
		if (tolower((unsigned char)a[index]) != tolower((unsigned char)b[index]))
			return FALSE;
		if (!a[index])
			return TRUE;
	}
	return TRUE;
}

/* the data root's files and folders kept elsewhere when the settings say
(paths.logs, paths.custom_maps): a read-only data root, a dedicated
server's maps mounted so. The rest of the path after them in rest */
static int translate_elsewhere(const char *cursor, char *resolved, unsigned long size, const char **rest)
{
	static const char *const logs[] = { "debug.txt", "bans.txt", "cheaters.txt" };
	const char *logs_folder = config_string("paths.logs");
	const char *custom_maps = config_string("paths.custom_maps");
	unsigned long index;

	while (*cursor == '\\' || *cursor == '/')
		cursor++;
	for (index = 0; logs_folder[0] && index < sizeof(logs) / sizeof(logs[0]); index++)
	{
		if (names_match(cursor, logs[index], strlen(logs[index]) + 1))
		{
			make_directories(logs_folder);
			snprintf(resolved, size, "%s", logs_folder);
			trim_separators(resolved);
			*rest = cursor;
			return 1;
		}
	}
	if (custom_maps[0] && names_match(cursor, "custom_maps", 11) && (!cursor[11] || cursor[11] == '\\' ||
		cursor[11] == '/'))
	{
		snprintf(resolved, size, "%s", custom_maps);
		trim_separators(resolved);
		*rest = cursor + 11;
		return 1;
	}
	return 0;
}

/* the name in a directory that matches name in any case (the first, in the directory's order), by reading the directory; 0 if there is none */
static int scan_entry_case_insensitive(const char *directory, const char *name, char *result, unsigned long result_size)
{
	void *stream = posix_directory_open(*directory ? directory : ".");
	char entry[MAX_PATH];
	int found = 0;
	unsigned long entries = 0;

	FILE_COUNT(FILE_COUNT_SCAN, 1);
	if (!stream)
		return 0;
	while (posix_directory_next(stream, entry, sizeof(entry)))
	{
		entries++;
		if (names_match(entry, name, (unsigned long)strlen(name) + 1) && strlen(entry) + 1 <= result_size)
		{
			strcpy(result, entry);
			found = 1;
			break;
		}
	}
	posix_directory_close(stream);
	FILE_COUNT(FILE_COUNT_SCAN_ENTRY, entries);
	return found;
}

/* the name in a directory that matches name in any case: 1, 0 if there is none. exact: 1 when the directory is known to have name as it is (the stat of the exact
spelling is then not needed), else 0. The directory's names are kept where USE_DIRECTORY_CACHE says. */
static int find_entry_case_insensitive(const char *directory, const char *name, char *result, unsigned long result_size, int *exact)
{
	*exact = 0;
#ifdef USE_DIRECTORY_CACHE
	{
		unsigned long scans = file_count_totals[FILE_COUNT_SCAN];
		int answer = directory_cache_enabled() ? directory_cache_find(*directory ? directory : ".", name, result, result_size, exact) : -1;

		if (answer >= 0)
		{
			if (file_counting && file_count_totals[FILE_COUNT_SCAN] == scans)
				FILE_COUNT(FILE_COUNT_CACHE_HIT, 1);
			return answer;
		}
		/* (a directory that cannot be read or is too big to keep is read as it was) */
		*exact = 0;
	}
#endif
	return scan_entry_case_insensitive(directory, name, result, result_size);
}

#ifdef USE_DIRECTORY_CACHE
/* the generation of the directories' cache at which each drive's directory was found (+ 1; 0: not yet) */
static unsigned long drive_known[26];
#endif

#ifdef HALO_WIIU
/* port/wiiu/src/wiiu_usb.c: where the checkpoint file is kept when halo/checkpoint_dir.txt says (a folder on a USB drive), else NULL */
const char *wiiu_checkpoint_dir(void);
#endif

void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size)
{
	char resolved[1024];
	const char *cursor = xbox_path;
	unsigned long length;

	FILE_COUNT(FILE_COUNT_TRANSLATE, 1);
#ifdef HALO_WIIU
	/* (the game state's file, z:\savegame.bin, on a USB drive: a test, halo/checkpoint_dir.txt) */
	if ((xbox_path[0] == 'z' || xbox_path[0] == 'Z') && xbox_path[1] == ':' && (xbox_path[2] == '\\' || xbox_path[2] == '/') &&
		names_match(xbox_path + 3, "savegame.bin", 13))
	{
		const char *folder = wiiu_checkpoint_dir();

		if (folder)
		{
			snprintf(host_path, host_path_size, "%s/savegame.bin", folder);
			return;
		}
	}
#endif
	snprintf(resolved, sizeof(resolved), "%s", platform_data_root());
	if (((cursor[0] >= 'a' && cursor[0] <= 'z') || (cursor[0] >= 'A' && cursor[0] <= 'Z')) && cursor[1] == ':')
	{
		char drive = (char)(cursor[0] | 0x20);

		if (drive == 'h' && platform_custom_edition_root()[0])
		{
			snprintf(resolved, sizeof(resolved), "%s", platform_custom_edition_root());
		}
		else if (drive == 'd' && translate_elsewhere(cursor + 2, resolved, sizeof(resolved), &cursor))
		{
			cursor -= 2;
		}
		else if (drive != 'd')
		{
			struct posix_file_information information;

			snprintf(resolved, sizeof(resolved), "%s/%c", platform_save_root(), drive);
			/* every Xbox drive always exists; create its directory on first use */
#ifdef USE_DIRECTORY_CACHE
			/* (known to be there since the last file or directory made or removed: not asked again for each path) */
			if (drive >= 'a' && drive <= 'z' && drive_known[drive - 'a'] == directory_cache_generation + 1)
				;
			else
#endif
			{
				if (counted_stat(resolved, &information) != 0)
				{
					posix_make_directory(resolved);
					DIRECTORY_CACHE_FLUSH();
				}
#ifdef USE_DIRECTORY_CACHE
				if (drive >= 'a' && drive <= 'z')
					drive_known[drive - 'a'] = directory_cache_generation + 1;
#endif
			}
		}
		cursor += 2;
	}

	while (*cursor)
	{
		char component[256];
		char on_disk[256];
		unsigned long component_length = 0;
		int name_is_exact;

		while (*cursor == '\\' || *cursor == '/')
			cursor++;
		while (*cursor && *cursor != '\\' && *cursor != '/' && component_length + 1 < sizeof(component))
			component[component_length++] = *cursor++;
		component[component_length] = '\0';
		/* . and .. (and Windows' spellings of them, with trailing dots and
		spaces) are skipped: FATX names are never only dots, and .. would
		leave the root */
		if (!component_length || strspn(component, ". ") == component_length)
			continue;

		FILE_COUNT(FILE_COUNT_COMPONENT, 1);
		if (find_entry_case_insensitive(resolved, component, on_disk, sizeof(on_disk), &name_is_exact))
		{
			/* prefer an exact match when several spellings exist */
			char exact[1100];
			struct posix_file_information information;

			if (name_is_exact)
			{
				snprintf(on_disk, sizeof(on_disk), "%s", component);
			}
			else
			{
				snprintf(exact, sizeof(exact), "%s/%s", resolved, component);
				if (counted_stat(exact, &information) == 0)
					snprintf(on_disk, sizeof(on_disk), "%s", component);
			}
		}
		else
		{
			snprintf(on_disk, sizeof(on_disk), "%s", component);
		}
		length = strlen(resolved);
		snprintf(resolved + length, sizeof(resolved) - length, "/%s", on_disk);
	}
	snprintf(host_path, host_path_size, "%s", resolved);
	if (file_counting > 1 && pthread_equal(pthread_self(), file_counting_thread))
	{
		/* (halo/filecount.txt 1000 and more: the first paths the game thread has translated, to see which files a menu function asks for) */
		static unsigned long said;

		if (said++ < 400)
			platform_log("files: path %s -> %s", xbox_path, host_path);
	}
}

/* ---------- the maps folder's maps */

enum
{
	/* a cache file's header: 'head' and its version */
	MAP_HEADER_SIGNATURE = 0x68656164,
	/* (Halo PC's: retail's, and Custom Edition's) */
	HALO_PC_MAP_VERSION = 7,
	CUSTOM_EDITION_MAP_VERSION = 609,
};

/* a map file's cache version, by its header; 0 if it is not a cache file
(little-endian, as the Xbox and Halo PC wrote them; and on a big-endian host
big-endian too, as it plays maps converted to its order, tools/wiiu/
map_convert.py) */
static long map_file_version(const char *path)
{
	unsigned char header[8];
	FILE *file = fopen(path, "rb");
	long version = 0;

	if (!file)
		return 0;
	if (fread(header, 1, sizeof(header), file) == sizeof(header))
	{
		if ((header[0] | header[1] << 8 | header[2] << 16 | (unsigned long)header[3] << 24) == MAP_HEADER_SIGNATURE)
			version = (long)(header[4] | header[5] << 8 | header[6] << 16 | (unsigned long)header[7] << 24);
#ifdef __BIG_ENDIAN__
		else if (((unsigned long)header[0] << 24 | header[1] << 16 | header[2] << 8 | header[3]) == MAP_HEADER_SIGNATURE)
			version = (long)((unsigned long)header[4] << 24 | header[5] << 16 | header[6] << 8 | header[7]);
#endif
	}
	fclose(file);
	return version;
}

static BOOL map_version_is_pc(long version)
{
	return version == HALO_PC_MAP_VERSION || version == CUSTOM_EDITION_MAP_VERSION;
}

static BOOL name_ends_with(const char *name, const char *ending)
{
	size_t length = strlen(name);
	size_t ending_length = strlen(ending);
	size_t index;

	if (length < ending_length)
		return FALSE;
	for (index = 0; index < ending_length; index++)
	{
		if (tolower((unsigned char)name[length - ending_length + index]) != tolower((unsigned char)ending[index]))
			return FALSE;
	}
	return TRUE;
}

/* the Halo PC maps in the maps folder, which play only from custom_maps:
how many, and the first PLATFORM_MAPS_FOLDER_NAMED's names */
static long pc_maps_in_maps(char *names, unsigned long names_size)
{
	char folder[1024];
	char name[256];
	long count = 0;
	void *directory;

	platform_translate_path("d:\\maps", folder, sizeof(folder));
	directory = posix_directory_open(folder);
	if (!directory)
		return 0;
	while (posix_directory_next(directory, name, sizeof(name)))
	{
		char path[1300];

		if (!name_ends_with(name, ".map"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", folder, name);
		if (!map_version_is_pc(map_file_version(path)))
			continue;
		if (count < PLATFORM_MAPS_FOLDER_NAMED)
		{
			size_t length = strlen(names);

			snprintf(names + length, names_size - length, "%s%s", length ? ", " : "", name);
		}
		count++;
	}
	posix_directory_close(directory);
	return count;
}

void platform_maps_folder_check(struct platform_maps_folder *maps)
{
	const char *root = platform_data_root();
	char path[1024];

	memset(maps, 0, sizeof(*maps));
	/* the data root as the player finds it (whole: Windows' is the
	executable's folder's, or paths.data's) */
#ifndef _WIN32
	if (root[0] != '/' && getcwd(maps->root, sizeof(maps->root)))
	{
		size_t length = strlen(maps->root);

		if (strcmp(root, "."))
			snprintf(maps->root + length, sizeof(maps->root) - length, "/%s", root);
	}
	else
#endif
	{
		snprintf(maps->root, sizeof(maps->root), "%s", root);
	}
	platform_translate_path("d:\\maps\\ui.map", path, sizeof(path));
	maps->ui_version = map_file_version(path);
	maps->state = map_version_is_pc(maps->ui_version) ? _maps_folder_halo_pc :
		!maps->ui_version ? _maps_folder_no_ui : _maps_folder_xbox;
	maps->stray_pc_maps = pc_maps_in_maps(maps->stray_names, sizeof(maps->stray_names));
}

/* ---------- file handles */

struct platform_file
{
	int descriptor;
	char path[1024];
#ifdef HALO_WIIU
	char *preload;
	unsigned long long preload_size;
	int preload_tried;
#endif
};

static void file_destroy(struct platform_handle *handle)
{
	struct platform_file *file = handle->data;

	close(file->descriptor);
#ifdef HALO_WIIU
	free(file->preload);
#endif
	free(file);
}

static struct platform_file *file_from_handle(HANDLE handle)
{
	struct platform_handle *record = platform_handle_get(handle, _platform_handle_file);

	return record ? record->data : NULL;
}

HANDLE WINAPI CreateFileA(LPCSTR file_name, DWORD desired_access, DWORD share_mode,
	LPSECURITY_ATTRIBUTES security_attributes, DWORD creation_disposition,
	DWORD flags_and_attributes, HANDLE template_file)
{
	struct platform_file *file;
	struct platform_handle *handle;
	struct posix_file_information information;
	char path[1024];
	int flags = 0;
	int existed;
	int descriptor;

	(void)share_mode;
	(void)security_attributes;
	(void)flags_and_attributes;
	(void)template_file;

	platform_translate_path(file_name, path, sizeof(path));
#ifdef HALO_WIIU
	{
		static unsigned open_trace_count;

		if (open_trace_count < 600)
		{
			const size_t length = strlen(path);

			open_trace_count++;
			platform_log("open trace: %s access %lx", path + (length > 60 ? length - 60 : 0), (unsigned long)desired_access);
		}
	}
#endif
	existed = counted_stat(path, &information) == 0;

	if ((desired_access & GENERIC_READ) && (desired_access & GENERIC_WRITE))
		flags = O_RDWR;
	else if (desired_access & GENERIC_WRITE)
#ifdef HALO_WIIU
		/* (wut's file system opens a write-only file as "w", which truncates it, or refuses it: read and write is the mode that leaves the file be) */
		flags = O_RDWR;
#else
		flags = O_WRONLY;
#endif
	else
		flags = O_RDONLY;

	switch (creation_disposition)
	{
	case CREATE_NEW:
		flags |= O_CREAT | O_EXCL;
		break;
	case CREATE_ALWAYS:
		flags |= O_CREAT | O_TRUNC;
		break;
	case OPEN_EXISTING:
		break;
	case OPEN_ALWAYS:
		flags |= O_CREAT;
		break;
	case TRUNCATE_EXISTING:
		flags |= O_TRUNC;
		break;
	default:
		SetLastError(ERROR_INVALID_PARAMETER);
		return INVALID_HANDLE_VALUE;
	}
	if ((flags & O_TRUNC) && (flags & O_ACCMODE) == O_RDONLY)
		flags = (flags & ~O_ACCMODE) | O_RDWR;

	if (existed && (information.flags & _posix_file_is_directory))
	{
		SetLastError(ERROR_ACCESS_DENIED);
		return INVALID_HANDLE_VALUE;
	}

	descriptor = open(path, flags | O_CLOEXEC, 0644);
	if (descriptor < 0)
	{
#ifdef HALO_WIIU
		/* (the first failures that were not a read of a file that is not there, in the log: what the card refuses to make or open for writing, which saved games
		and the settings depend on, and which the game only says "failed to open" of) */
		{
			static unsigned int told;

			if ((flags & (O_CREAT | O_RDWR | O_WRONLY | O_TRUNC)) && (flags & O_ACCMODE) != O_RDONLY && told++ < 40u)
				platform_log("Files: %s for %s%s%s failed, errno %d (%s)", (flags & O_CREAT) ? "creating" : "opening", path,
					(flags & O_EXCL) ? " (exclusive)" : "", (flags & O_TRUNC) ? " (truncating)" : "", errno, strerror(errno));
		}
#endif
		platform_set_last_error_from_errno(errno);
		if (errno == ENOENT && !existed && (flags & O_CREAT))
			SetLastError(ERROR_PATH_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}

	file = calloc(1, sizeof(*file));
	handle = file ? platform_handle_new(_platform_handle_file, file, file_destroy) : NULL;
	if (!handle)
	{
		close(descriptor);
		free(file);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return INVALID_HANDLE_VALUE;
	}
	file->descriptor = descriptor;
	snprintf(file->path, sizeof(file->path), "%s", path);
	FILE_COUNT(FILE_COUNT_OPEN, 1);
	if (!existed && (flags & O_CREAT))
		DIRECTORY_CACHE_FLUSH();

	/* Win32 reports whether an OPEN_ALWAYS/CREATE_ALWAYS target existed */
	SetLastError((existed && (creation_disposition == OPEN_ALWAYS || creation_disposition == CREATE_ALWAYS)) ?
		ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
	return handle;
}

#define READ_BOUNCE_SIZE 0x40000UL

#ifdef HALO_WIIU
/* The map copy at a join (the game inflates maps/<name>.map into a cache file, 128 KB read, then 4 MB written, over and over) took 5 s some days and 54 s on
others (2026-11-20: reads of 0.86 s each while the card was being written). A map of at most 48 MB is read whole, in 1 MB pieces with nothing written, on the first
read of 64 KB or more, and the copy's reads are served from memory; the time is logged. A file that cannot be held is read as before. */
static double file_io_now_ms(void);
void platform_log(const char *format, ...);

#define MAP_PRELOAD_LIMIT (48ULL * 1024 * 1024)

static void map_preload(struct platform_file *file)
{
	off_t size;
	double started;
	unsigned long long done = 0;

	file->preload_tried = 1;
	if (!strstr(file->path, "/maps/") || (size = lseek(file->descriptor, 0, SEEK_END)) <= 0 ||
		(unsigned long long)size > MAP_PRELOAD_LIMIT)
		return;
	file->preload = malloc((size_t)size);
	if (!file->preload)
	{
		platform_log("map preload: no room for %s (%llu KB)", file->path, (unsigned long long)size / 1024);
		return;
	}
	started = file_io_now_ms();
	while (done < (unsigned long long)size)
	{
		size_t wanted = (size_t)(size - done);
		ssize_t got;

		if (wanted > 0x100000)
			wanted = 0x100000;
		got = pread(file->descriptor, file->preload + done, wanted, (off_t)done);
		if (got <= 0)
			break;
		done += (unsigned long long)got;
	}
	if (done != (unsigned long long)size)
	{
		platform_log("map preload: %s stopped at %llu of %llu KB; read as before", file->path, done / 1024,
			(unsigned long long)size / 1024);
		free(file->preload);
		file->preload = NULL;
		return;
	}
	file->preload_size = done;
	{
		const double ms = file_io_now_ms() - started;

		platform_log("map preload: %s read whole, %llu KB in %.0f ms (%.1f MB/s)", file->path, done / 1024, ms,
			ms > 0.0 ? (double)done / 1048576.0 / (ms / 1000.0) : 0.0);
	}
}
#endif

static ssize_t read_some(struct platform_file *file, void *buffer, size_t count, BOOL positioned,
	unsigned long long offset)
{
#ifdef HALO_WIIU
	if (positioned && !file->preload_tried && count >= 65536)
		map_preload(file);
	if (positioned && file->preload)
	{
		size_t n;

		if (offset >= file->preload_size)
			return 0;
		n = (size_t)(file->preload_size - offset);
		if (n > count)
			n = count;
		memcpy(buffer, file->preload + offset, n);
		return (ssize_t)n;
	}
#endif
	return positioned ?
		pread(file->descriptor, buffer, count, (off_t)offset) :
		read(file->descriptor, buffer, count);
}

static BOOL read_at_inner(struct platform_file *file, LPVOID buffer, DWORD count, LPDWORD bytes_read,
	BOOL positioned, unsigned long long offset);

/* (the Wii U's start-up timeline, startup_timeline.c, adds up the reads: those of 256 KB and more, and the rest) */
static BOOL read_at(struct platform_file *file, LPVOID buffer, DWORD count, LPDWORD bytes_read,
	BOOL positioned, unsigned long long offset)
{
	STARTUP_TIMER(started);
	BOOL result = read_at_inner(file, buffer, count, bytes_read, positioned, offset);

	if (count >= 262144)
		STARTUP_ADD("read_big", started, 1, bytes_read ? *bytes_read : 0);
	else
		STARTUP_ADD("read_small", started, 1, bytes_read ? *bytes_read : 0);
	return result;
}

static BOOL read_at_inner(struct platform_file *file, LPVOID buffer, DWORD count, LPDWORD bytes_read,
	BOOL positioned, unsigned long long offset)
{
	DWORD total = 0;
	/* The renderer write-protects guest memory it caches (memory_watch.c),
	and the kernel fails a read() into a protected page instead of
	faulting. Guest memory is therefore filled through a bounce buffer: the
	copy faults like any other write, at the moment the data really lands,
	so a texture uploaded while the read is in flight is refreshed. */
	BOOL bounce = platform_is_contiguous(buffer) ||
		platform_is_contiguous((char *)buffer + (count ? count - 1 : 0));
	char *staging = bounce ? malloc(count < READ_BOUNCE_SIZE ? count : READ_BOUNCE_SIZE) : NULL;

	if (bounce && !staging)
	{
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return FALSE;
	}
	while (total < count)
	{
		size_t wanted = count - total;
		ssize_t result;

		if (bounce && wanted > READ_BOUNCE_SIZE)
			wanted = READ_BOUNCE_SIZE;
		result = read_some(file, bounce ? staging : (char *)buffer + total, wanted, positioned, offset + total);
		if (result < 0)
		{
			if (errno == EINTR)
				continue;
			platform_set_last_error_from_errno(errno);
			if (bytes_read)
				*bytes_read = total;
			free(staging);
			return FALSE;
		}
		if (result == 0)
			break;
		if (bounce)
		{
			/* (where pages are watched by their hashes, port/web, so that a
			page long unchanged is checked again in the next frame) */
			memory_watch_prepare_write((char *)buffer + total, (unsigned long)result);
			memcpy((char *)buffer + total, staging, (size_t)result);
		}
		total += (DWORD)result;
	}
	free(staging);
	if (bytes_read)
		*bytes_read = total;
	return TRUE;
}

static BOOL write_at(struct platform_file *file, LPCVOID buffer, DWORD count, LPDWORD bytes_written,
	BOOL positioned, unsigned long long offset)
{
	DWORD total = 0;

#ifdef HALO_WIIU
	/* (a file on the USB drive, port/wiiu/src/wiiu_usb.c, is written from a buffer of the heap, in pieces of 128 KB: the drive wrote the game state's own
	buffer, in the Xbox memory window, at 0.3 MB/s, and a heap buffer at 7 MB/s) */
	if (!positioned && strncmp(file->path, "storage_usb:", 12) == 0)
	{
		static char *bounce;
		const DWORD chunk = 128 * 1024;

		if (!bounce)
			bounce = memalign(64, chunk);
		if (bounce)
		{
			while (total < count)
			{
				const DWORD size = count - total < chunk ? count - total : chunk;
				ssize_t result;

				memcpy(bounce, (const char *)buffer + total, size);
				result = write(file->descriptor, bounce, size);
				if (result < 0)
				{
					if (errno == EINTR)
						continue;
					platform_set_last_error_from_errno(errno);
					if (bytes_written)
						*bytes_written = total;
					return FALSE;
				}
				total += (DWORD)result;
			}
			if (bytes_written)
				*bytes_written = total;
			return TRUE;
		}
	}
#endif
	while (total < count)
	{
		ssize_t result = positioned ?
			pwrite(file->descriptor, (const char *)buffer + total, count - total, (off_t)(offset + total)) :
			write(file->descriptor, (const char *)buffer + total, count - total);

		if (result < 0)
		{
			if (errno == EINTR)
				continue;
			platform_set_last_error_from_errno(errno);
			if (bytes_written)
				*bytes_written = total;
			return FALSE;
		}
		total += (DWORD)result;
	}
	if (bytes_written)
		*bytes_written = total;
	return TRUE;
}

static unsigned long long overlapped_offset(LPOVERLAPPED overlapped)
{
	return ((unsigned long long)overlapped->OffsetHigh << 32) | overlapped->Offset;
}

/* port: the time the game's file reads and writes take, for the frame pacing notes: a frame that took long (a checkpoint's 20 MB, a map's data) says it was the card. An
operation of 30 ms or more is logged with its file (the first 200 of a run); the totals are read by platform_file_io_snapshot and counted from the start. */

static struct platform_file_io file_io;
static pthread_mutex_t file_io_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned file_io_slow_said;

static double file_io_now_ms(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
}

#ifdef HALO_WIIU
/* the checkpoint's writer waits for the card between pieces while the game is reading from it (a read behind a piece waited up to the piece's whole write) */
static int reads_in_flight;
#define READ_TRACE_LIMIT 4000
static unsigned read_trace_count;
static unsigned long long read_trace_offset;
void platform_log(const char *format, ...);
static double write_yielded_ms;
static unsigned long write_pieces;
static double write_call_ms, write_pause_ms, write_slowest_ms;
long wiiu_study_number(const char *name, long fallback);
static int write_yield_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
		enabled = wiiu_study_number("write_yield.txt", 1) != 0;
	return enabled;
}
static volatile double last_read_end_ms;
#endif

static void file_io_note(const struct platform_file *file, int write, DWORD bytes, double start_ms)
{
	const double ms = file_io_now_ms() - start_ms;
	int slow;

	pthread_mutex_lock(&file_io_lock);
	if (write)
	{
		file_io.write_ms += ms;
		file_io.writes++;
		file_io.bytes_written += bytes;
	}
	else
	{
		file_io.read_ms += ms;
		file_io.reads++;
		file_io.bytes_read += bytes;
	}
	if (ms > file_io.longest_ms)
		file_io.longest_ms = ms;
	slow = ms >= 30.0 && file_io_slow_said < 200;
	if (slow)
		file_io_slow_said++;
	pthread_mutex_unlock(&file_io_lock);
	if (slow)
	{
		const size_t length = strlen(file->path);

		platform_log("File: %s of %lu KB, %s%s, took %.0f ms", write ? "a write" : "a read", (unsigned long)bytes / 1024, length > 70 ? "..." : "",
			file->path + (length > 70 ? length - 70 : 0), ms);
	}
}

void platform_file_io_snapshot(struct platform_file_io *out)
{
	pthread_mutex_lock(&file_io_lock);
	*out = file_io;
	pthread_mutex_unlock(&file_io_lock);
}

BOOL WINAPI ReadFile(HANDLE handle, LPVOID buffer, DWORD count, LPDWORD bytes_read, LPOVERLAPPED overlapped)
{
	struct platform_file *file = file_from_handle(handle);
	BOOL result;
	DWORD done = 0;
	double start_ms;

	if (!file)
		return FALSE;
	start_ms = file_io_now_ms();
#ifdef HALO_WIIU
	__atomic_add_fetch(&reads_in_flight, 1, __ATOMIC_SEQ_CST);
	if (read_trace_count < READ_TRACE_LIMIT)
		read_trace_offset = overlapped ? overlapped_offset(overlapped) : (unsigned long long)lseek(file->descriptor, 0, SEEK_CUR);
#endif
	result = read_at(file, buffer, count, &done, overlapped != NULL, overlapped ? overlapped_offset(overlapped) : 0);
#ifdef HALO_WIIU
	last_read_end_ms = file_io_now_ms();
	__atomic_sub_fetch(&reads_in_flight, 1, __ATOMIC_SEQ_CST);
	if (read_trace_count < READ_TRACE_LIMIT)
	{
		const size_t length = strlen(file->path);

		read_trace_count++;
		platform_log("read trace: %s offset %llu size %lu took %.0f ms", file->path + (length > 40 ? length - 40 : 0), read_trace_offset,
			(unsigned long)count, last_read_end_ms - start_ms);
	}
#endif
	file_io_note(file, 0, done, start_ms);
	if (bytes_read)
		*bytes_read = done;
	if (overlapped)
	{
		overlapped->Internal = result ? 0 : GetLastError();
		overlapped->InternalHigh = done;
		if (overlapped->hEvent)
			SetEvent(overlapped->hEvent);
	}
	return result;
}

BOOL WINAPI WriteFile(HANDLE handle, LPCVOID buffer, DWORD count, LPDWORD bytes_written, LPOVERLAPPED overlapped)
{
	struct platform_file *file = file_from_handle(handle);
	BOOL result;
	DWORD done = 0;
	double start_ms;

	if (!file)
		return FALSE;
	start_ms = file_io_now_ms();
	result = write_at(file, buffer, count, &done, overlapped != NULL, overlapped ? overlapped_offset(overlapped) : 0);
	file_io_note(file, 1, done, start_ms);
	if (bytes_written)
		*bytes_written = done;
	if (overlapped)
	{
		overlapped->Internal = result ? 0 : GetLastError();
		overlapped->InternalHigh = done;
		if (overlapped->hEvent)
			SetEvent(overlapped->hEvent);
	}
	return result;
}

/* WriteFile for count bytes made in pieces of at most piece bytes from the file pointer (a checkpoint's 20 MiB, async_save.h: one request that size holds the card while
other reads wait behind it), noted as the one write it is in the totals and the slow log. Only the pointer is used, not an overlapped offset. */
BOOL platform_write_file_in_pieces(HANDLE handle, LPCVOID buffer, DWORD count, DWORD piece, LPDWORD bytes_written)
{
	struct platform_file *file = file_from_handle(handle);
	BOOL result = TRUE;
	DWORD done = 0;
	double start_ms;

	if (!file)
		return FALSE;
	if (piece == 0)
		piece = count ? count : 1;
	start_ms = file_io_now_ms();
#ifdef HALO_WIIU
	write_yielded_ms = 0.0;
	write_pieces = 0;
	write_call_ms = 0.0;
	write_pause_ms = 0.0;
	write_slowest_ms = 0.0;
#endif
	while (done < count)
	{
		DWORD wrote = 0;
		const DWORD size = count - done < piece ? count - done : piece;
#ifdef HALO_WIIU
		const double call_start = file_io_now_ms();
#endif

		result = write_at(file, (const char *)buffer + done, size, &wrote, FALSE, 0);
#ifdef HALO_WIIU
		{
			const double took = file_io_now_ms() - call_start;

			write_pieces++;
			write_call_ms += took;
			if (took > write_slowest_ms)
				write_slowest_ms = took;
		}
#endif
		done += wrote;
		if (!result)
			break;
#ifdef HALO_WIIU
		/* Give queued map reads a chance between checkpoint pieces (halo/write_yield.txt 0: do not). */
		if (done < count && write_yield_enabled())
		{
			const struct timespec pause = { 0, 2000000 };
			const double begun = file_io_now_ms();
			const double give_up = begun + 150.0;

			nanosleep(&pause, NULL);
			/* (a level's first minute is a burst of reads of the map, 30 reads totalling 5 s in the log of 2026-11-14, queued behind this write's pieces;
			the writer now stays out of the way for as long as the game has read within the last 40 ms, up to 150 ms a piece and 8 s in all, and then
			writes on, so that a checkpoint is always finished long before the next one) */
			while (file_io_now_ms() < give_up && write_yielded_ms < 8000.0 &&
				(__atomic_load_n(&reads_in_flight, __ATOMIC_SEQ_CST) > 0 || file_io_now_ms() - last_read_end_ms < 40.0))
				nanosleep(&pause, NULL);
			write_yielded_ms += file_io_now_ms() - begun;
			write_pause_ms += file_io_now_ms() - begun;
		}
#endif
	}
#ifdef HALO_WIIU
	if (done >= 1024 * 1024)
		platform_log("write: %lu KB in %lu pieces of %lu KB, %.0f ms in all: %.0f ms inside the write calls, %.0f ms waiting for reads, the slowest piece %.0f ms (%s)",
			(unsigned long)(done / 1024), write_pieces, (unsigned long)(piece / 1024), file_io_now_ms() - start_ms, write_call_ms, write_pause_ms, write_slowest_ms,
			strncmp(file->path, "storage_usb:", 12) == 0 ? "USB drive" : "SD card");
#endif
	file_io_note(file, 1, done, start_ms);
	if (bytes_written)
		*bytes_written = done;
	return result;
}

/* ReadFileEx/WriteFileEx complete immediately; as on Win32, the completion
routine runs during the issuing thread's next alertable wait. */

static void file_completion_apc(void *routine, void *overlapped, void *unused)
{
	LPOVERLAPPED request = overlapped;

	(void)unused;
	((LPOVERLAPPED_COMPLETION_ROUTINE)routine)((DWORD)request->Internal, (DWORD)request->InternalHigh, request);
}

BOOL WINAPI ReadFileEx(HANDLE handle, LPVOID buffer, DWORD count, LPOVERLAPPED overlapped,
	LPOVERLAPPED_COMPLETION_ROUTINE completion_routine)
{
	struct platform_file *file = file_from_handle(handle);
	DWORD done = 0;
	BOOL result;

	if (!file || !overlapped)
	{
		if (file)
			SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	{
		const double start_ms = file_io_now_ms();

		result = read_at(file, buffer, count, &done, TRUE, overlapped_offset(overlapped));
		file_io_note(file, 0, done, start_ms);
	}
	overlapped->Internal = result ? ERROR_SUCCESS : GetLastError();
	if (result && done == 0 && count > 0)
		overlapped->Internal = ERROR_HANDLE_EOF;
	overlapped->InternalHigh = done;
	platform_queue_apc(file_completion_apc, (void *)completion_routine, overlapped, NULL);
	SetLastError(ERROR_SUCCESS);
	return TRUE;
}

BOOL WINAPI WriteFileEx(HANDLE handle, LPCVOID buffer, DWORD count, LPOVERLAPPED overlapped,
	LPOVERLAPPED_COMPLETION_ROUTINE completion_routine)
{
	struct platform_file *file = file_from_handle(handle);
	DWORD done = 0;
	BOOL result;

	if (!file || !overlapped)
	{
		if (file)
			SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	{
		const double start_ms = file_io_now_ms();

		result = write_at(file, buffer, count, &done, TRUE, overlapped_offset(overlapped));
		file_io_note(file, 1, done, start_ms);
	}
	overlapped->Internal = result ? ERROR_SUCCESS : GetLastError();
	overlapped->InternalHigh = done;
	platform_queue_apc(file_completion_apc, (void *)completion_routine, overlapped, NULL);
	SetLastError(ERROR_SUCCESS);
	return TRUE;
}

DWORD WINAPI SetFilePointer(HANDLE handle, LONG distance, PLONG distance_high, DWORD method)
{
	struct platform_file *file = file_from_handle(handle);
	unsigned long low, high;
	int whence;

	if (!file)
		return INVALID_SET_FILE_POINTER;
	switch (method)
	{
	case FILE_BEGIN: whence = SEEK_SET; break;
	case FILE_CURRENT: whence = SEEK_CUR; break;
	case FILE_END: whence = SEEK_END; break;
	default:
		SetLastError(ERROR_INVALID_PARAMETER);
		return INVALID_SET_FILE_POINTER;
	}
	if (posix_seek(file->descriptor, distance,
		distance_high ? *distance_high : (distance < 0 ? -1 : 0), whence, &low, &high) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return INVALID_SET_FILE_POINTER;
	}
	if (distance_high)
		*distance_high = (LONG)high;
	SetLastError(ERROR_SUCCESS);
	return (DWORD)low;
}

DWORD WINAPI GetFileSize(HANDLE handle, LPDWORD size_high)
{
	struct platform_file *file = file_from_handle(handle);
	struct posix_file_information information;

	if (!file)
		return INVALID_FILE_SIZE;
	if (posix_fstat(file->descriptor, &information) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return INVALID_FILE_SIZE;
	}
	if (size_high)
		*size_high = information.size_high;
	SetLastError(ERROR_SUCCESS);
	return information.size_low;
}

BOOL WINAPI SetEndOfFile(HANDLE handle)
{
	struct platform_file *file = file_from_handle(handle);
	unsigned long low, high;

	if (!file)
		return FALSE;
	if (posix_seek(file->descriptor, 0, 0, SEEK_CUR, &low, &high) != 0 ||
		posix_truncate(file->descriptor, low, high) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI GetFileTime(HANDLE handle, LPFILETIME creation_time, LPFILETIME last_access_time, LPFILETIME last_write_time)
{
	struct platform_file *file = file_from_handle(handle);
	struct posix_file_information information;

	if (!file)
		return FALSE;
	if (posix_fstat(file->descriptor, &information) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	if (creation_time)
		platform_unix_time_to_filetime(information.creation_seconds, information.creation_nanoseconds, creation_time);
	if (last_access_time)
		platform_unix_time_to_filetime(information.access_seconds, information.access_nanoseconds, last_access_time);
	if (last_write_time)
		platform_unix_time_to_filetime(information.modification_seconds, information.modification_nanoseconds, last_write_time);
	return TRUE;
}

BOOL WINAPI SetFileTime(HANDLE handle, CONST FILETIME *creation_time, CONST FILETIME *last_access_time,
	CONST FILETIME *last_write_time)
{
	struct platform_file *file = file_from_handle(handle);
	unsigned long access_seconds = 0, access_nanoseconds = 0;
	unsigned long write_seconds = 0, write_nanoseconds = 0;

	(void)creation_time;
	if (!file)
		return FALSE;
	if (last_access_time)
		platform_filetime_to_unix_time(last_access_time, &access_seconds, &access_nanoseconds);
	if (last_write_time)
		platform_filetime_to_unix_time(last_write_time, &write_seconds, &write_nanoseconds);
	if (posix_set_file_times(file->path, access_seconds, access_nanoseconds, write_seconds, write_nanoseconds) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	return TRUE;
}

/* ---------- operations on paths */

static void fill_attribute_data(const struct posix_file_information *information, WIN32_FILE_ATTRIBUTE_DATA *data)
{
	data->dwFileAttributes = 0;
	if (information->flags & _posix_file_is_directory)
		data->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
	if (information->flags & _posix_file_is_read_only)
		data->dwFileAttributes |= FILE_ATTRIBUTE_READONLY;
	if (!data->dwFileAttributes)
		data->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
	platform_unix_time_to_filetime(information->creation_seconds, information->creation_nanoseconds, &data->ftCreationTime);
	platform_unix_time_to_filetime(information->access_seconds, information->access_nanoseconds, &data->ftLastAccessTime);
	platform_unix_time_to_filetime(information->modification_seconds, information->modification_nanoseconds, &data->ftLastWriteTime);
	data->nFileSizeHigh = information->size_high;
	data->nFileSizeLow = information->size_low;
}

DWORD WINAPI GetFileAttributesA(LPCSTR file_name)
{
	struct posix_file_information information;
	WIN32_FILE_ATTRIBUTE_DATA data;
	char path[1024];

	platform_translate_path(file_name, path, sizeof(path));
	if (counted_stat(path, &information) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return (DWORD)-1;
	}
	fill_attribute_data(&information, &data);
	return data.dwFileAttributes;
}

BOOL WINAPI GetFileAttributesExA(LPCSTR file_name, GET_FILEEX_INFO_LEVELS level, LPVOID file_information)
{
	struct posix_file_information information;
	char path[1024];

	if (level != GetFileExInfoStandard)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	platform_translate_path(file_name, path, sizeof(path));
	if (counted_stat(path, &information) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	fill_attribute_data(&information, file_information);
	return TRUE;
}

BOOL WINAPI SetFileAttributesA(LPCSTR file_name, DWORD attributes)
{
	char path[1024];
	struct posix_file_information information;

	platform_translate_path(file_name, path, sizeof(path));
	if (counted_stat(path, &information) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	/* only the read-only bit has a POSIX equivalent */
	if (posix_set_read_only(path, (attributes & FILE_ATTRIBUTE_READONLY) != 0) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	return TRUE;
}

BOOL WINAPI DeleteFileA(LPCSTR file_name)
{
	char path[1024];

	platform_translate_path(file_name, path, sizeof(path));
	if (unlink(path) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	DIRECTORY_CACHE_FLUSH();
	return TRUE;
}

BOOL WINAPI MoveFileA(LPCSTR existing_file_name, LPCSTR new_file_name)
{
	char from[1024], to[1024];
	struct posix_file_information information;

	platform_translate_path(existing_file_name, from, sizeof(from));
	platform_translate_path(new_file_name, to, sizeof(to));
	if (counted_stat(to, &information) == 0)
	{
		SetLastError(ERROR_ALREADY_EXISTS);
		return FALSE;
	}
	if (rename(from, to) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	DIRECTORY_CACHE_FLUSH();
	return TRUE;
}

BOOL WINAPI CopyFileA(LPCSTR existing_file_name, LPCSTR new_file_name, BOOL fail_if_exists)
{
	char from[1024], to[1024];
	char buffer[65536];
	int source, destination;
	BOOL success = TRUE;

	platform_translate_path(existing_file_name, from, sizeof(from));
	platform_translate_path(new_file_name, to, sizeof(to));
	source = open(from, O_RDONLY | O_CLOEXEC);
	if (source < 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	destination = open(to, O_WRONLY | O_CREAT | O_CLOEXEC | (fail_if_exists ? O_EXCL : O_TRUNC), 0644);
	if (destination < 0)
	{
		platform_set_last_error_from_errno(errno);
		close(source);
		return FALSE;
	}
	for (;;)
	{
		ssize_t count = read(source, buffer, sizeof(buffer));

		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
		{
			if (count < 0)
			{
				platform_set_last_error_from_errno(errno);
				success = FALSE;
			}
			break;
		}
		if (write(destination, buffer, (size_t)count) != count)
		{
			platform_set_last_error_from_errno(errno);
			success = FALSE;
			break;
		}
	}
	close(source);
	close(destination);
	DIRECTORY_CACHE_FLUSH();
	return success;
}

BOOL WINAPI CreateDirectoryA(LPCSTR path_name, LPSECURITY_ATTRIBUTES security_attributes)
{
	char path[1024];

	(void)security_attributes;
	platform_translate_path(path_name, path, sizeof(path));
	if (posix_make_directory(path) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	DIRECTORY_CACHE_FLUSH();
	return TRUE;
}

BOOL WINAPI RemoveDirectoryA(LPCSTR path_name)
{
	char path[1024];

	platform_translate_path(path_name, path, sizeof(path));
	if (rmdir(path) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	DIRECTORY_CACHE_FLUSH();
	return TRUE;
}

BOOL WINAPI GetDiskFreeSpaceExA(LPCSTR directory_name, PULARGE_INTEGER free_bytes_available,
	PULARGE_INTEGER total_bytes, PULARGE_INTEGER total_free_bytes)
{
	char path[1024];
	unsigned long free_low, free_high, total_low, total_high;

	platform_translate_path(directory_name ? directory_name : "d:\\", path, sizeof(path));
	if (posix_disk_space(path, &free_low, &free_high, &total_low, &total_high) != 0)
	{
		platform_set_last_error_from_errno(errno);
		return FALSE;
	}
	if (free_bytes_available)
	{
		free_bytes_available->LowPart = free_low;
		free_bytes_available->HighPart = free_high;
	}
	if (total_free_bytes)
	{
		total_free_bytes->LowPart = free_low;
		total_free_bytes->HighPart = free_high;
	}
	if (total_bytes)
	{
		total_bytes->LowPart = total_low;
		total_bytes->HighPart = total_high;
	}
	return TRUE;
}

/* ---------- directory enumeration */

struct platform_find
{
	void *directory;
	char directory_path[1024];
	char pattern[MAX_PATH];
};

static void find_destroy(struct platform_handle *handle)
{
	struct platform_find *find = handle->data;

	posix_directory_close(find->directory);
	free(find);
}

/* Win32 wildcard match: '*' any run, '?' one character, case-insensitive;
"*.*" also matches names without a dot */
static BOOL wildcard_match(const char *pattern, const char *name)
{
	if (!strcmp(pattern, "*.*") || !strcmp(pattern, "*"))
		return TRUE;
	while (*pattern)
	{
		if (*pattern == '*')
		{
			while (*pattern == '*')
				pattern++;
			if (!*pattern)
				return TRUE;
			for (; *name; name++)
			{
				if (wildcard_match(pattern, name))
					return TRUE;
			}
			return FALSE;
		}
		if (!*name)
			return FALSE;
		if (*pattern != '?' && tolower((unsigned char)*pattern) != tolower((unsigned char)*name))
			return FALSE;
		pattern++;
		name++;
	}
	return *name == '\0';
}

static BOOL find_next_entry(struct platform_find *find, LPWIN32_FIND_DATAA data)
{
	char name[MAX_PATH];

	while (posix_directory_next(find->directory, name, sizeof(name)))
	{
		char full_path[1400];

		FILE_COUNT(FILE_COUNT_FIND_ENTRY, 1);
		struct posix_file_information information;
		WIN32_FILE_ATTRIBUTE_DATA attributes;

		if (!wildcard_match(find->pattern, name))
			continue;
		snprintf(full_path, sizeof(full_path), "%s/%s", find->directory_path, name);
		if (counted_stat(full_path, &information) != 0)
			continue;
		fill_attribute_data(&information, &attributes);
		memset(data, 0, sizeof(*data));
		data->dwFileAttributes = attributes.dwFileAttributes;
		data->ftCreationTime = attributes.ftCreationTime;
		data->ftLastAccessTime = attributes.ftLastAccessTime;
		data->ftLastWriteTime = attributes.ftLastWriteTime;
		data->nFileSizeHigh = attributes.nFileSizeHigh;
		data->nFileSizeLow = attributes.nFileSizeLow;
		snprintf(data->cFileName, sizeof(data->cFileName), "%s", name);
		return TRUE;
	}
	SetLastError(ERROR_NO_MORE_FILES);
	return FALSE;
}

HANDLE WINAPI FindFirstFileA(LPCSTR file_name, LPWIN32_FIND_DATAA data)
{
	struct platform_find *find = calloc(1, sizeof(*find));
	struct platform_handle *handle;
	char xbox_directory[MAX_PATH];
	const char *separator = strrchr(file_name, '\\');

	if (!find)
	{
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return INVALID_HANDLE_VALUE;
	}
	if (!separator)
		separator = strrchr(file_name, '/');
	if (separator)
	{
		snprintf(xbox_directory, sizeof(xbox_directory), "%.*s", (int)(separator - file_name), file_name);
		snprintf(find->pattern, sizeof(find->pattern), "%s", separator + 1);
	}
	else
	{
		xbox_directory[0] = '\0';
		snprintf(find->pattern, sizeof(find->pattern), "%s", file_name);
	}
	platform_translate_path(xbox_directory, find->directory_path, sizeof(find->directory_path));
	FILE_COUNT(FILE_COUNT_FIND, 1);
	find->directory = posix_directory_open(find->directory_path);
	if (!find->directory)
	{
		platform_set_last_error_from_errno(errno);
		free(find);
		return INVALID_HANDLE_VALUE;
	}
	handle = platform_handle_new(_platform_handle_find, find, find_destroy);
	if (!handle)
	{
		posix_directory_close(find->directory);
		free(find);
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		return INVALID_HANDLE_VALUE;
	}
	if (!find_next_entry(find, data))
	{
		CloseHandle(handle);
		SetLastError(ERROR_FILE_NOT_FOUND);
		return INVALID_HANDLE_VALUE;
	}
	return handle;
}

BOOL WINAPI FindNextFileA(HANDLE find_file, LPWIN32_FIND_DATAA data)
{
	struct platform_handle *handle = platform_handle_get(find_file, _platform_handle_find);

	if (!handle)
		return FALSE;
	return find_next_entry(handle->data, data);
}

/* FindClose is an XDK macro for CloseHandle */
