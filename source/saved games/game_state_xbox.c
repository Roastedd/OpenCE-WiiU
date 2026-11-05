/*
GAME_STATE_XBOX.C

symbols in this file:
001AFBB0 0160:
	_game_state_allocate_buffer (0000)
001AFD10 0040:
	_game_state_free_buffer (0000)
001AFD50 00d0:
	_game_state_create_or_open_file (0000)
001AFE20 0040:
	_game_state_close_file (0000)
001AFE60 00e0:
	_game_state_write_to_file (0000)
001AFF40 0120:
	_game_state_read_from_file (0000)
001B0060 0090:
	_game_state_write_core (0000)
001B00F0 0080:
	_game_state_read_core_header (0000)
001B0170 00a0:
	_game_state_read_core (0000)
001B0210 0010:
	_game_state_get_persistent_storage_filename (0000)
001B0220 0020:
	_game_state_get_persistent_storage_path (0000)
001B0240 0030:
	_delete_persistent_storage (0000)
001B0270 0190:
	_game_state_open_persistent_storage (0000)
001B0400 01b0:
	_game_state_read_header_from_persistent_storage (0000)
001B05B0 0160:
	_game_state_write_to_persistent_storage (0000)
001B0710 00b0:
	_game_state_read_from_persistent_storage (0000)
001B07C0 0020:
	_game_state_create_persistent_storage (0000)
002A8084 001f:
	??_C@_0BP@IDAHEHI@?$CIunsigned?5long?$CJresult?$DN?$DNaddress?$AA@ (0000)
002A80A4 001e:
	??_C@_0BO@BBKFFHIP@?$CB?$CIgpu_size?$CG?$CICPU_PAGE_SIZE?91?$CJ?$CJ?$AA@ (0000)
002A80C4 001e:
	??_C@_0BO@BHHCKDPL@?$CB?$CIcpu_size?$CG?$CICPU_PAGE_SIZE?91?$CJ?$CJ?$AA@ (0000)
002A80E4 000b:
	??_C@_0L@JBLOLKNC@gpu_size?$DO0?$AA@ (0000)
002A80F0 000b:
	??_C@_0L@CBGHMJFA@cpu_size?$DO0?$AA@ (0000)
002A80FC 002a:
	??_C@_0CK@FIOMLFFF@?$CBxbox_game_state_globals?4buffer_@ (0000)
002A8128 002d:
	??_C@_0CN@KDCMADJG@c?3?2halo?2SOURCE?2saved?5games?2game_@ (0000)
002A8158 0029:
	??_C@_0CJ@FFFLELDG@xbox_game_state_globals?4buffer_a@ (0000)
002A8184 002e:
	??_C@_0CO@BEKHBMJJ@couldn?8t?5open?5or?5create?5saved?5ga@ (0000)
002A81B4 0010:
	??_C@_0BA@MOOKLLKO@z?3?2savegame?4bin?$AA@ (0000)
002A81C4 0023:
	??_C@_0CD@DDMIEKAL@?$CBxbox_game_state_globals?4file_op@ (0000)
002A81E8 0022:
	??_C@_0CC@CLCLOOEI@xbox_game_state_globals?4file_ope@ (0000)
002A820C 0025:
	??_C@_0CF@BLHGLGEN@couldn?8t?5write?5saved?5game?5file?5?$CI@ (0000)
002A8234 0024:
	??_C@_0CE@CPCCDNFP@couldn?8t?5read?5saved?5game?5file?5?$CI?$CD@ (0000)
002A8258 0048:
	??_C@_0EI@JODPNGEB@xbox_game_state_globals?4file_val@ (0000)
002A82A0 000b:
	??_C@_0L@BACPNKLN@d?3?2core?2?$CFs?$AA@ (0000)
002A82AC 0008:
	??_C@_07ELALHFFD@d?3?2core?$AA@ (0000)
002A82B4 0036:
	??_C@_0DG@PGBLNDAA@game?5state?5has?5been?5corrupted?5?$CIt@ (0000)
002A82EC 000d:
	??_C@_0N@KAKGDJDL@savegame?4bin?$AA@ (0000)
002A82FC 0030:
	??_C@_0DA@GAKEGFCK@couldn?8t?5open?5or?5create?5persiste@ (0000)
002A832C 0028:
	??_C@_0CI@LDKKAEDE@couldn?8t?5resize?5persistent?5stora@ (0000)
002A8354 0033:
	??_C@_0DD@EGEEEGAD@couldn?8t?5read?5header?5from?5persis@ (0000)
002A8388 0026:
	??_C@_0CG@JDCOBIBA@checksum?5failed?5on?5persistent?5st@ (0000)
002A83B0 002c:
	??_C@_0CM@LDKGDBEH@failed?5to?5write?5to?5persistent?5st@ (0000)
002A83DC 0021:
	??_C@_0CB@GFGKONGF@header_size?$DMsizeof?$CIsaved_header?$CJ@ (0000)
002A8400 002d:
	??_C@_0CN@DOEIAHAD@failed?5to?5read?5from?5persistent?5s@ (0000)
004D27D0 0014:
	_xbox_game_state_globals (0000)
*/

/* ---------- headers */

#include "cache/physical_memory_map.h"
#include "cseries/cseries.h"
#include "cseries/errors.h"
#include "interface/player_ui.h"
#include "memory/crc.h"
#include "saved games/game_state.h"
#include "sound/sound_manager.h"
#include <xtl.h>
#ifdef HALO_WIIU
#include "async_save.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

/* ---------- constants */

enum
{
	CPU_PAGE_SIZE = 0x1000,
	/* the native builds' larger game state (halo_port_capacity.h); the saved
	game files only need to hold it */
	GAME_STATE_SIZE = HALO_PORT_GAME_STATE_SIZE,
	GAME_STATE_FILE_SIZE = HALO_PORT_GAME_STATE_SIZE
};

/* ---------- structures */

struct xbox_game_state_globals_prefix
{
	boolean buffer_allocated;
	byte reserved001[3];
	void *buffer;
	long buffer_size;
	boolean file_open;
	boolean file_valid_for_read;
	byte reserved00E[2];
	HANDLE handle;
};

typedef char verify_xbox_game_state_buffer_offset[
	offsetof(struct xbox_game_state_globals_prefix, buffer) == 0x4 ? 1 : -1];
typedef char verify_xbox_game_state_file_open_offset[
	offsetof(struct xbox_game_state_globals_prefix, file_open) == 0xC ? 1 : -1];
typedef char verify_xbox_game_state_handle_offset[
	offsetof(struct xbox_game_state_globals_prefix, handle) == 0x10 ? 1 : -1];
typedef char verify_xbox_game_state_globals_prefix_size[
	sizeof(struct xbox_game_state_globals_prefix) == 0x14 ? 1 : -1];

/* ---------- prototypes */

static void game_state_header_terminate(
	void *header,
	long header_size);
static HANDLE game_state_open_persistent_storage(
	const char *directory);
static void *game_state_image_get(
	unsigned long size);
static void game_state_image_release(
	void *image);
#ifdef HALO_WIIU
void platform_log(char const *format, ...);
long wiiu_study_number(const char *name, long fallback);
BOOL platform_write_file_in_pieces(HANDLE handle, LPCVOID buffer, DWORD count, DWORD piece, LPDWORD bytes_written);
static boolean game_state_async_begin(
	void);
static boolean game_state_async_settle(
	unsigned long timeout_ms,
	const char *why);
static void game_state_verify_sync_write(
	void);

/* the checkpoint's write on a thread of its own (below): the one handed over, if any */
static struct
{
	boolean pending; /* a write was handed over and has not been waited for */
	boolean lost;
	volatile int result; /* -1 running, 1 written, 0 failed (the writer thread's) */
	unsigned long number;
	double write_ms; /* the writer thread's */
} game_state_async;
#endif

/* ---------- globals */

static struct xbox_game_state_globals_prefix xbox_game_state_globals = { 0 };

/* ---------- public code */

void *game_state_allocate_buffer(
	unsigned long address,
	unsigned long cpu_size,
	unsigned long gpu_size)
{
	void *result;

	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		46,
		!xbox_game_state_globals.buffer_allocated);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		48,
		address);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		49,
		cpu_size>0);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		50,
		gpu_size>0);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		53,
		!(cpu_size&(CPU_PAGE_SIZE-1)));
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		54,
		!(gpu_size&(CPU_PAGE_SIZE-1)));

	result = physical_memory_get_game_state_base_address();
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		58,
		result);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		59,
		(unsigned long)result==address);

	XPhysicalProtect(
		(void *)(address+cpu_size),
		gpu_size,
		PAGE_READWRITE|PAGE_WRITECOMBINE);

	xbox_game_state_globals.buffer_allocated = TRUE;
	xbox_game_state_globals.buffer = (void *)address;
	xbox_game_state_globals.buffer_size = cpu_size+gpu_size;
#ifdef HALO_WIIU
	/* port: the image a saved game is read into before it is taken, made now
	while the heap is whole (game_state_image_get) */
	game_state_image_get(cpu_size+gpu_size);
	/* port: and the thread a checkpoint's write runs on (async_save.h), made now for the same reason */
	async_save_prepare();
#endif

	return result;
}

void game_state_free_buffer(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		75,
		xbox_game_state_globals.buffer_allocated);
#ifdef HALO_WIIU
	/* port: a checkpoint's write is over before the game state goes (the thread works from a copy, but the file is closed next) */
	game_state_async_settle(10000, "the game state is freed");
#endif
	XPhysicalFree(xbox_game_state_globals.buffer);
	xbox_game_state_globals.buffer_allocated = FALSE;

	return;
}

void game_state_create_or_open_file(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		86,
		xbox_game_state_globals.buffer_allocated);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		87,
		!xbox_game_state_globals.file_open);

	xbox_game_state_globals.handle = CreateFileA("z:\\savegame.bin",
		GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
		FILE_FLAG_NO_BUFFERING|FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (xbox_game_state_globals.handle != INVALID_HANDLE_VALUE &&
		SetFilePointer(xbox_game_state_globals.handle, GAME_STATE_FILE_SIZE, NULL,
			FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
		SetEndOfFile(xbox_game_state_globals.handle))
	{
		xbox_game_state_globals.file_open = TRUE;
	}
	else
	{
		match_vassert(
			"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
			97,
			FALSE,
			csprintf(temporary, "couldn't open or create saved game file (#%d)",
				GetLastError()));
	}

	return;
}

void game_state_close_file(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		106,
		xbox_game_state_globals.file_open);
#ifdef HALO_WIIU
	game_state_async_settle(10000, "the saved game file is closed");
#endif
	CloseHandle(xbox_game_state_globals.handle);
	xbox_game_state_globals.file_open = FALSE;

	return;
}

boolean game_state_write_to_file(
	void)
{
	unsigned long bytes_written;
	boolean result = FALSE;

	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		120,
		xbox_game_state_globals.buffer_allocated);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		121,
		xbox_game_state_globals.file_open);

#ifdef HALO_WIIU
	/* port: the copy is taken here and the write is the writer thread's (game_state_async_begin); where that cannot be, it is made here, as it always was */
	if (game_state_async_begin())
	{
		return TRUE;
	}
#endif
	if (SetFilePointer(xbox_game_state_globals.handle, 0, NULL, FILE_BEGIN) !=
			INVALID_SET_FILE_POINTER &&
		WriteFile(xbox_game_state_globals.handle, xbox_game_state_globals.buffer,
			xbox_game_state_globals.buffer_size, &bytes_written, NULL) &&
		bytes_written == xbox_game_state_globals.buffer_size)
	{
		xbox_game_state_globals.file_valid_for_read = TRUE;
		result = TRUE;
#ifdef HALO_WIIU
		game_state_async.lost = FALSE;
		game_state_verify_sync_write();
#endif
	}
	else
	{
		match_vassert(
			"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
			132,
			FALSE,
			csprintf(temporary, "couldn't write saved game file (#%d)", GetLastError()));
	}

	return result;
}

boolean game_state_read_from_file(
	void)
{
	unsigned long bytes_read;
	boolean result = FALSE;
	void *image;
	/* port: which step failed, in the message (1 no image, 2 seek, 3 read,
	4 short, 5 not accepted) */
	int stage = 1;

	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		144,
		xbox_game_state_globals.buffer_allocated);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		145,
		xbox_game_state_globals.file_open);
	match_assert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		146,
		xbox_game_state_globals.file_valid_for_read || recover_saved_games_hack);

#ifdef HALO_WIIU
	/* port: a checkpoint still being written to this file is over first (and the read is of what it wrote) */
	game_state_async_settle(0, "a revert");
#endif
	/* port: read beside the game state, and taken once checked */
	image = game_state_image_get(xbox_game_state_globals.buffer_size);
	if (image &&
		++stage &&
		SetFilePointer(xbox_game_state_globals.handle, 0, NULL, FILE_BEGIN) !=
			INVALID_SET_FILE_POINTER &&
		++stage &&
		ReadFile(xbox_game_state_globals.handle, image,
			xbox_game_state_globals.buffer_size, &bytes_read, NULL) &&
		++stage &&
		bytes_read == xbox_game_state_globals.buffer_size &&
		++stage &&
		game_state_image_accept(image, xbox_game_state_globals.buffer_size))
	{
		result = TRUE;
	}
	else
	{
		match_vassert(
			"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
			156,
			FALSE,
			csprintf(temporary, "couldn't read saved game file (#%d, step %d)", GetLastError(), stage));
	}
	game_state_image_release(image);

	return result;
}

boolean game_state_write_core(
	const char *name,
	void *buffer,
	long buffer_size)
{
	char path[1024];
	HANDLE file;
	unsigned long bytes_written;
	boolean result = FALSE;

	CreateDirectoryA("d:\\core", NULL);
	sprintf(path, "d:\\core\\%s", name);
	file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
		NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		if (WriteFile(file, buffer, buffer_size, &bytes_written, NULL) &&
			bytes_written == buffer_size)
		{
			result = TRUE;
		}
	}
	CloseHandle(file);

	return result;
}

boolean game_state_read_core_header(
	const char *name,
	void *header,
	long header_size)
{
	char path[1024];
	HANDLE file;
	unsigned long bytes_read;
	boolean result = FALSE;

	sprintf(path, "d:\\core\\%s", name);
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
		NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		if (ReadFile(file, header, header_size, &bytes_read, NULL) &&
			bytes_read == header_size)
		{
			game_state_header_terminate(header, header_size);
			result = TRUE;
		}
	}
	CloseHandle(file);

	return result;
}

void game_state_read_core(
	const char *name,
	void *buffer,
	long buffer_size)
{
	char path[1024];
	HANDLE file;
	unsigned long bytes_read;

	void *image = game_state_image_get(buffer_size);

	sprintf(path, "d:\\core\\%s", name);
	file = CreateFileA(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
		NULL);
	/* port: read beside the game state, and taken once checked */
	match_vassert(
		"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
		226,
		image && file != INVALID_HANDLE_VALUE &&
			ReadFile(file, image, buffer_size, &bytes_read, NULL) &&
			bytes_read == buffer_size &&
			game_state_image_accept(image, buffer_size),
		"game state has been corrupted (thank you, come again)");
	(void)buffer;
	CloseHandle(file);
	game_state_image_release(image);

	return;
}

const char *game_state_get_persistent_storage_filename(
	void)
{
	return "savegame.bin";
}

static __inline boolean game_state_get_persistent_storage_path(
	char *path)
{
	boolean result;

	result = player_ui_get_path_to_local_player_profile_directory(0, path) != FALSE;

	return result;
}

static void delete_persistent_storage(
	void)
{
	char path[256];

	if (game_state_get_persistent_storage_path(path))
		DeleteFileA(path);

	return;
}

static HANDLE game_state_open_persistent_storage(
	const char *directory)
{
	char path[256];
	byte zeroes[16*1024];
	HANDLE file;
	unsigned long bytes_written;
	boolean success = FALSE;

	if (directory || game_state_get_persistent_storage_path(path))
	{
		if (directory)
		{
			strcpy(path, directory);
		}
		else
		{
			game_state_get_persistent_storage_path(path);
		}
		strcat(path, "savegame.bin");

		file = CreateFileA(path, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, 0,
			NULL);
		if (file != INVALID_HANDLE_VALUE)
		{
			success = TRUE;
			if (GetFileSize(file, NULL) != GAME_STATE_FILE_SIZE)
			{
				memset(zeroes, 0, sizeof(zeroes));
				if (!WriteFile(file, zeroes, sizeof(zeroes), &bytes_written, NULL) ||
					bytes_written != sizeof(zeroes) ||
					SetFilePointer(file, GAME_STATE_FILE_SIZE, NULL, FILE_BEGIN) ==
						INVALID_SET_FILE_POINTER ||
					!SetEndOfFile(file))
				{
					match_vassert(
						"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
						491,
						FALSE,
						csprintf(temporary,
							"couldn't resize persistent storage \"%s\"", path));
					delete_persistent_storage();
					CloseHandle(file);
					success = FALSE;
				}
			}
		}
		else
		{
			match_vassert(
				"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
				498,
				FALSE,
				csprintf(temporary, "couldn't open or create persistent storage \"%s\"",
					path));
		}
	}

	return success ? file : INVALID_HANDLE_VALUE;
}

/* port: the strings of a header read from a file end in it (the file is
anyone's) */
static void game_state_header_terminate(
	void *header,
	long header_size)
{
	if (header_size == sizeof(struct game_state_header))
	{
		struct game_state_header *game_state_header = header;

		game_state_header->map_name[NUMBEROF(game_state_header->map_name) - 1] = 0;
		game_state_header->build_number[NUMBEROF(game_state_header->build_number) - 1] = 0;
	}
}

boolean game_state_read_header_from_persistent_storage(
	void *header,
	unsigned long *header_checksum,
	long header_size,
	long buffer_size,
	boolean *corrupted)
{
	byte buffer[128*1024];
	HANDLE file;
	unsigned long checksum;
	unsigned long stored_checksum;
	unsigned long bytes_read;
	long remaining_size;
	long read_size;
	boolean result;

	file = game_state_open_persistent_storage(NULL);
	result = FALSE;
	if (corrupted)
		*corrupted = FALSE;

	if (file != INVALID_HANDLE_VALUE)
	{
		if (SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
			!ReadFile(file, header, header_size, &bytes_read, NULL) ||
			bytes_read != header_size)
		{
			game_state_header_terminate(header, header_size);
			match_vassert(
				"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
				298,
				FALSE,
				csprintf(temporary,
					"couldn't read header from persistent storage (#%d)",
					GetLastError()));
			delete_persistent_storage();
		}
		else
		{
			game_state_header_terminate(header, header_size);
			stored_checksum = *header_checksum;

			crc_new(&checksum);
			*header_checksum = 0;
			crc_checksum_buffer(&checksum, header, header_size);

			remaining_size = buffer_size-header_size;
			while (remaining_size>0)
			{
				read_size = MIN(sizeof(buffer), remaining_size);
				if (ReadFile(file, buffer, read_size, &bytes_read, NULL) &&
					bytes_read == read_size)
				{
					crc_checksum_buffer(&checksum, buffer, read_size);
				}
				sound_idle();
				remaining_size -= read_size;
			}

			if (checksum == stored_checksum)
			{
				result = TRUE;
			}
			else
			{
				if (corrupted && stored_checksum)
					*corrupted = TRUE;
				error(_error_silent, "checksum failed on persistent storage");
			}
		}
		CloseHandle(file);
	}

	return result;
}

void game_state_write_to_persistent_storage(
	void *buffer,
	unsigned long *header_checksum,
	long header_size,
	long buffer_size)
{
	byte saved_header[2048];
	HANDLE file;
	unsigned long checksum;
	unsigned long bytes_written;

	file = game_state_open_persistent_storage(NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		*header_checksum = 0;

		crc_new(&checksum);
		crc_checksum_buffer(&checksum, buffer, GAME_STATE_SIZE);
		*header_checksum = checksum;

		match_assert(
			"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
			333,
			header_size<sizeof(saved_header));

		memcpy(saved_header, buffer, header_size);
		memset(buffer, 0, header_size);

		if (SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
			!WriteFile(file, buffer, buffer_size, &bytes_written, NULL) ||
			bytes_written != buffer_size ||
			SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
			!WriteFile(file, saved_header, header_size, &bytes_written, NULL) ||
			bytes_written != header_size)
		{
			match_vassert(
				"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
				351,
				FALSE,
				csprintf(temporary, "failed to write to persistent storage (#%d)",
					GetLastError()));
			delete_persistent_storage();
		}

		memcpy(buffer, saved_header, header_size);
		CloseHandle(file);
	}

	return;
}

void game_state_read_from_persistent_storage(
	void *buffer,
	long buffer_size)
{
	HANDLE file;
	unsigned long bytes_read;

	file = game_state_open_persistent_storage(NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		/* port: read beside the game state, and taken once checked */
		void *image = game_state_image_get(buffer_size);

		if (!image ||
			SetFilePointer(file, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
			!ReadFile(file, image, buffer_size, &bytes_read, NULL) ||
			bytes_read != buffer_size ||
			!game_state_image_accept(image, buffer_size))
		{
			match_vassert(
				"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
				383,
				FALSE,
				csprintf(temporary, "failed to read from persistent storage (#%d)",
					GetLastError()));
			delete_persistent_storage();
		}
		(void)buffer;
		game_state_image_release(image);
		CloseHandle(file);
	}

	return;
}

void game_state_create_persistent_storage(
	const char *path)
{
	HANDLE handle;

	handle = game_state_open_persistent_storage(path);
	if (handle != INVALID_HANDLE_VALUE)
		CloseHandle(handle);

	return;
}

/* ---------- private code */

/* port: the image a saved game is read into and checked in before it is taken
(game_state_image_accept). The Wii U keeps one for the whole session, made at
start: made again at each revert, the 20 MiB block could not always be had late
in a session, and Save and Quit halted at "couldn't read saved game file (#8)".
The other builds make it each time, as before. */
#ifdef HALO_WIIU
static void *game_state_image;
static unsigned long game_state_image_size;
#endif

static void *game_state_image_get(
	unsigned long size)
{
#ifdef HALO_WIIU
	/* (the image is what a checkpoint's copy is held in until the writer thread has written it: whoever wants it waits for that first) */
	game_state_async_settle(0, "the image is wanted");
	if (game_state_image_size < size)
	{
		/* (the game's free halts on a null pointer: debug_memory.c) */
		if (game_state_image)
		{
			free(game_state_image);
		}
		game_state_image = malloc(size);
		game_state_image_size = game_state_image ? size : 0;
	}

	return game_state_image;
#else
	return malloc(size);
#endif
}

static void game_state_image_release(
	void *image)
{
#ifdef HALO_WIIU
	(void)image;
#else
	if (image)
	{
		free(image);
	}
#endif

	return;
}

/* ---------- port: the checkpoint's write on a thread of its own */

/* A checkpoint wrote the whole game state to z:\savegame.bin on the game thread: one write of 20 MiB (HALO_PORT_GAME_STATE_SIZE) and nothing else (no hash, no compression), which
took 1.3 to 1.6 s on the console, with the game's time halted and nothing drawn (the owner's a30 and b30 sessions of 2026-11-03/04: reports/research/2026-11-04/async-save.md).
Here the game thread copies the game state into the image (the buffer a revert reads a saved game into, made at the start and held: a revert, a load and the next checkpoint
wait for the write first, so the image is never both) and hands the write to the writer thread (async_save.h); the file's bytes are the ones the synchronous write
made. The file is the session's own scratch: nothing reads it after the process ends (file_valid_for_read is false at the start, and a revert reads it only after a
checkpoint of this run), so a write cut short leaves nothing a later run would take; the player's saved game (the profile's savegame.bin, Save and Quit) is not
written here, and is written as it was, after a revert, which waits for this write.
Where the copy cannot be had, or the thread, the write is made here as before; a write that failed on the thread is made once more from the copy on the game thread
when it is waited for, and if that fails too the checkpoint is lost (game_state_port_save_lost: the revert then starts the map over, as a failed write did). */

static double game_state_async_now_ms(
	void)
{
	/* (the port's monotonic millisecond count: the game sources have no time.h) */
	return (double)GetTickCount();
}

static unsigned long game_state_async_crc(
	const void *buffer,
	unsigned long size)
{
	unsigned long checksum;

	crc_new(&checksum);
	crc_checksum_buffer(&checksum, (void *)buffer, size);

	return checksum;
}

/* halo/async_save_fault.txt (a test's: the host runs and a console run with the owner's card snapshotted): 1 makes the writer thread's write of every second
checkpoint fail after its first piece (the game thread then writes it again, from the copy), 2 makes that second write fail too (the checkpoint is lost, and a
revert starts the map over), 3 makes every write take 800 ms more (a slow card: the revert, the next checkpoint, the load of the next map and the end of the game then
find a write still running); 0 or absent: none. */
static long game_state_async_fault(
	void)
{
	static long fault = -1;

	if (fault<0)
	{
		fault = wiiu_study_number("async_save_fault.txt", 0);
		if (fault>0)
		{
			platform_log("checkpoint: FAULTS are made on purpose (halo/async_save_fault.txt %ld): %s", fault,
				fault==3 ? "every write takes 800 ms more" : fault==2 ? "every second checkpoint's write fails, and so does its second try" : "every second checkpoint's write fails");
		}
	}

	return fault;
}

/* the file read back and compared with what was written (halo/async_save_verify.txt), 1 if it is the same */
static boolean game_state_verify_file(
	const void *expected,
	unsigned long size)
{
	const unsigned long piece_size = 1024*1024;
	byte *piece = malloc(piece_size);
	unsigned long offset = 0;
	boolean same = piece!=NULL;

	if (same && SetFilePointer(xbox_game_state_globals.handle, 0, NULL, FILE_BEGIN)==INVALID_SET_FILE_POINTER)
	{
		same = FALSE;
	}
	while (same && offset<size)
	{
		unsigned long count = size-offset<piece_size ? size-offset : piece_size;
		unsigned long bytes_read = 0;

		if (!ReadFile(xbox_game_state_globals.handle, piece, count, &bytes_read, NULL) ||
			bytes_read!=count ||
			memcmp(piece, (const byte *)expected+offset, count)!=0)
		{
			same = FALSE;
		}
		offset += count;
	}
	free(piece);

	return same;
}

static void game_state_verify_sync_write(
	void)
{
	if (async_save_verify())
	{
		game_state_async.number++;
		platform_log("checkpoint %lu: crc %08lx of %lu bytes (written on the game thread), file %s", game_state_async.number,
			game_state_async_crc(xbox_game_state_globals.buffer, xbox_game_state_globals.buffer_size), (unsigned long)xbox_game_state_globals.buffer_size,
			game_state_verify_file(xbox_game_state_globals.buffer, xbox_game_state_globals.buffer_size) ? "read back the same" : "READ BACK DIFFERENT");
	}

	return;
}

/* the writer thread's job: the copy (the image) to the file, from its start, in pieces */
static void game_state_async_write(
	void *argument)
{
	const double start = game_state_async_now_ms();
	const unsigned long size = xbox_game_state_globals.buffer_size;
	void *snapshot = game_state_image;
	unsigned long bytes_written = 0;
	unsigned long error = 0;
	char note[48];
	boolean ok = FALSE;

	(void)argument;
	note[0] = 0;
	if (game_state_async_fault()==3)
	{
		Sleep(800);
	}
	if (game_state_async_fault() && game_state_async_fault()!=3 && (game_state_async.number&1)==0)
	{
		/* (a test's failure: the first piece written, then an error, as a card that fills up or is pulled would */
		SetFilePointer(xbox_game_state_globals.handle, 0, NULL, FILE_BEGIN);
		platform_write_file_in_pieces(xbox_game_state_globals.handle, snapshot, async_save_piece_bytes(), async_save_piece_bytes(), &bytes_written);
		error = 0xFA17;
		snprintf(note, sizeof(note), " - error %lu", error);
	}
	else if (SetFilePointer(xbox_game_state_globals.handle, 0, NULL, FILE_BEGIN)!=INVALID_SET_FILE_POINTER &&
		platform_write_file_in_pieces(xbox_game_state_globals.handle, snapshot, size, async_save_piece_bytes(), &bytes_written) &&
		bytes_written==size)
	{
		ok = TRUE;
	}
	else
	{
		error = GetLastError();
		snprintf(note, sizeof(note), " - error %lu", error);
	}
	if (ok && async_save_verify() && !game_state_verify_file(snapshot, size))
	{
		platform_log("checkpoint %lu: the file read back is NOT the copy", game_state_async.number);
		ok = FALSE;
	}
	game_state_async.write_ms = game_state_async_now_ms()-start;
	platform_log("checkpoint %lu: %lu KB %s on the writer thread in %.0f ms (pieces of %lu KB)%s", game_state_async.number, size/1024,
		ok ? "written" : "NOT written", game_state_async.write_ms, async_save_piece_bytes()/1024, note);
	game_state_async.result = ok ? 1 : 0;

	return;
}

/* copies the game state and hands the write over: FALSE if that cannot be done, and nothing is changed (the caller writes as it always did) */
static boolean game_state_async_begin(
	void)
{
	void *image;
	double start;
	double copy_ms;

	if (!async_save_enabled())
	{
		return FALSE;
	}
	game_state_async_settle(0, "the next checkpoint");
	image = game_state_image_get(xbox_game_state_globals.buffer_size);
	if (!image)
	{
		platform_log("checkpoint: no image to copy the game state to: written on the game thread");
		return FALSE;
	}

	start = game_state_async_now_ms();
	memcpy(image, xbox_game_state_globals.buffer, xbox_game_state_globals.buffer_size);
	copy_ms = game_state_async_now_ms()-start;
	game_state_async.number++;
	if (async_save_verify())
	{
		platform_log("checkpoint %lu: crc %08lx of %lu bytes, copy %s", game_state_async.number,
			game_state_async_crc(image, xbox_game_state_globals.buffer_size), (unsigned long)xbox_game_state_globals.buffer_size,
			memcmp(image, xbox_game_state_globals.buffer, xbox_game_state_globals.buffer_size)==0 ? "the same as the game state" : "DIFFERENT FROM THE GAME STATE");
	}
	game_state_async_fault(); /* (read here, on the game thread) */
	game_state_async.result = -1;
	if (!async_save_start(game_state_async_write, NULL))
	{
		game_state_async.number--;
		platform_log("checkpoint: the writer thread cannot take the write: written on the game thread");
		return FALSE;
	}
	game_state_async.pending = TRUE;
	game_state_async.lost = FALSE;
	xbox_game_state_globals.file_valid_for_read = TRUE;
	platform_log("checkpoint %lu: the game state copied in %.1f ms (%lu KB); the write is on the writer thread", game_state_async.number, copy_ms,
		(unsigned long)xbox_game_state_globals.buffer_size/1024);

	return TRUE;
}

/* waits for the write handed over to end (0: as long as it takes); FALSE if it has not ended in timeout_ms. A write that failed is made once more here from the copy */
static boolean game_state_async_settle(
	unsigned long timeout_ms,
	const char *why)
{
	double start;
	double waited_ms;

	if (!game_state_async.pending)
	{
		return TRUE;
	}
	start = game_state_async_now_ms();
	if (!async_save_wait(timeout_ms))
	{
		platform_log("checkpoint %lu: its write is still running after %lu ms (%s)", game_state_async.number, timeout_ms, why);
		return FALSE;
	}
	waited_ms = game_state_async_now_ms()-start;
	game_state_async.pending = FALSE;
	if (waited_ms>=1.0)
	{
		platform_log("checkpoint %lu: waited %.0f ms for its write (%s)", game_state_async.number, waited_ms, why);
	}
	if (game_state_async.result!=1)
	{
		unsigned long bytes_written = 0;
		void *snapshot = game_state_image_get(xbox_game_state_globals.buffer_size);

		platform_log("checkpoint %lu: its write failed on the writer thread: written again on the game thread", game_state_async.number);
		if (snapshot &&
			!(game_state_async_fault()==2 && (game_state_async.number&1)==0) &&
			SetFilePointer(xbox_game_state_globals.handle, 0, NULL, FILE_BEGIN)!=INVALID_SET_FILE_POINTER &&
			WriteFile(xbox_game_state_globals.handle, snapshot, xbox_game_state_globals.buffer_size, &bytes_written, NULL) &&
			bytes_written==xbox_game_state_globals.buffer_size)
		{
			platform_log("checkpoint %lu: written", game_state_async.number);
		}
		else
		{
			game_state_async.lost = TRUE;
			xbox_game_state_globals.file_valid_for_read = FALSE;
			platform_log("checkpoint %lu: LOST: the file could not be written (error %lu)", game_state_async.number, (unsigned long)GetLastError());
		}
	}

	return TRUE;
}

/* game_state.c's: a revert, a load and the end of the game wait for the write first */
void game_state_port_wait_for_save(
	const char *why)
{
	game_state_async_settle(0, why);

	return;
}

/* whether the last checkpoint is not on the card (its write failed twice) */
boolean game_state_port_save_lost(
	void)
{
	return game_state_async.lost;
}

/* the shell's, at the app's close: 1 if no write is running after at most timeout_ms (nothing else is done) */
int game_state_port_save_wait(
	unsigned long timeout_ms)
{
	return async_save_wait(timeout_ms);
}
