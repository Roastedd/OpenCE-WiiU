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

/* port: a failed assertion here stops the game in a release build too (cseries.h, HALO_ASSERTS_STOP) */
#define HALO_ASSERTS_STOP

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
#include "save_backup.h"
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
/* the image a saved game is read into, and a checkpoint's copy is held in (below, game_state_image_get), the size it was made for, and whether it holds the last checkpoint's bytes */
static void *game_state_image;
static unsigned long game_state_image_size;
static boolean game_state_image_is_checkpoint;
static void *game_state_image_get_keeping(
	unsigned long size);
static double game_state_async_now_ms(
	void);
void platform_log(char const *format, ...);
long wiiu_study_number(const char *name, long fallback);
BOOL platform_write_file_in_pieces(HANDLE handle, LPCVOID buffer, DWORD count, DWORD piece, LPDWORD bytes_written);
static void game_state_backup_persistent_storage(
	const char *why);
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
	boolean stuck; /* it did not end in the time given (a card that does not answer): nothing is done with the file or the copy until it does */
	HANDLE job_handle; /* the file the write was handed over for (the writer thread's own copy of it: the file may be closed and made again meanwhile) */
	volatile int result; /* -1 running, 1 written, 0 failed (the writer thread's) */
	unsigned long number;
	double write_ms; /* the writer thread's */
} game_state_async;

/* halo/checkpoint_writes.txt (1, 2): a checkpoint's copy is kept in the image and its write to the card is left for later (game_state_deferred_flush): at the pause (1) and when
the game ends. The file on the card holds an older complete image, or none (file_valid_for_read says a revert can be had: from the image; anything that reads the file or
takes the image for another use writes the checkpoint first). */
static struct
{
	boolean pending; /* the image holds a checkpoint that is not on the card */
	unsigned long skipped; /* writes left out since the last one made */
} game_state_deferred;
static void game_state_deferred_flush(
	const char *why);

/* what the file z:\\savegame.bin holds after the last write that ended well (the writer thread's; halo/async_save_delta.txt): the next checkpoint writes only
the blocks that differ from it. Any other write to the file, or the file made again, makes it not valid (game_state_delta_invalidate) and the next write is whole. */
#define DELTA_BLOCK 4096UL
#define DELTA_GAP_BLOCKS 8UL /* changed blocks this near each other are written as one run, the blocks between them too */
static unsigned char *game_state_baseline;
static unsigned long game_state_baseline_size;
static HANDLE game_state_baseline_file;
static volatile int game_state_baseline_valid;

static void game_state_delta_invalidate(void)
{
	game_state_baseline_valid = 0;
}

static void delete_persistent_storage(
	void);

/* Save and Quit's write of the saved game (halo/async_save_exit.txt), on the writer thread */
static struct
{
	boolean pending;
	HANDLE file;
	unsigned char *image;
	unsigned long checksum_offset;
	unsigned long header_size;
	unsigned long buffer_size;
	unsigned long game_state_size;
	volatile int result; /* -1 running, 1 written, 0 failed */
	double write_ms;
	char directory[256]; /* the profile's directory and the saved game's file, taken on the game thread: the writer thread makes the backup and opens the file (a file made new is 20 MiB to allocate, seconds on a card) */
	char backup_path[512];
} game_state_persist;
static boolean game_state_get_persistent_storage_path(
	char *path);
static boolean game_state_persistent_file_path(
	char *path);

static void game_state_persist_job(void *argument)
{
	const double start = game_state_async_now_ms();
	byte saved_header[2048];
	unsigned long checksum;
	DWORD bytes_written = 0;
	unsigned long *header_checksum = (unsigned long *)(game_state_persist.image + game_state_persist.checksum_offset);

	(void)argument;
	if (game_state_persist.backup_path[0])
	{
		save_backup_keep(game_state_persist.backup_path, 1, GAME_STATE_FILE_SIZE, 2048, NULL, "saved game");
	}
	game_state_persist.file = game_state_open_persistent_storage(game_state_persist.directory);
	if (game_state_persist.file==INVALID_HANDLE_VALUE)
	{
		game_state_persist.result = 0;
		game_state_persist.write_ms = game_state_async_now_ms()-start;
		platform_log("save and quit: the saved game's file could not be opened on the writer thread");
		return;
	}
	*header_checksum = 0;
	crc_new(&checksum);
	crc_checksum_buffer(&checksum, game_state_persist.image, game_state_persist.game_state_size);
	*header_checksum = checksum;
	memcpy(saved_header, game_state_persist.image, game_state_persist.header_size);
	memset(game_state_persist.image, 0, game_state_persist.header_size);
	if (SetFilePointer(game_state_persist.file, 0, NULL, FILE_BEGIN)!=INVALID_SET_FILE_POINTER &&
		platform_write_file_in_pieces(game_state_persist.file, game_state_persist.image, game_state_persist.buffer_size, async_save_piece_bytes(), &bytes_written) &&
		bytes_written==game_state_persist.buffer_size &&
		SetFilePointer(game_state_persist.file, 0, NULL, FILE_BEGIN)!=INVALID_SET_FILE_POINTER &&
		WriteFile(game_state_persist.file, saved_header, game_state_persist.header_size, &bytes_written, NULL) &&
		bytes_written==game_state_persist.header_size)
	{
		game_state_persist.result = 1;
	}
	else
	{
		game_state_persist.result = 0;
	}
	CloseHandle(game_state_persist.file);
	game_state_persist.write_ms = game_state_async_now_ms()-start;
	platform_log("save and quit: the saved game %s on the writer thread in %.0f ms", game_state_persist.result==1 ? "written" : "NOT written", game_state_persist.write_ms);
}

/* waits for Save and Quit's write, if one is running, and takes the saved game away if it failed. Called before anything reads or makes the saved game, and before
the next checkpoint's copy. */
static void game_state_persist_settle(void)
{
	if (!game_state_persist.pending)
		return;
	async_save_wait(0);
	game_state_persist.pending = FALSE;
	if (game_state_persist.result!=1)
	{
		platform_log("save and quit: the write failed: the saved game is removed");
		delete_persistent_storage();
	}
}

/* Save and Quit's write on the writer thread: the game state is copied (a few frames), the checksum and the 20 MiB are the writer's. FALSE if that cannot be done
and nothing was changed (the caller writes it itself). */
static boolean game_state_persist_async(void *buffer, unsigned long *header_checksum, long header_size, long buffer_size)
{
	void *image;

	if (!async_save_enabled() || !async_save_exit() || header_size>2048 || buffer_size!=(long)GAME_STATE_SIZE)
		return FALSE;
	game_state_persist_settle();
	if (!game_state_async_settle(15000, "Save and Quit"))
		return FALSE;
	game_state_persist.backup_path[0] = 0;
	if (!game_state_get_persistent_storage_path(game_state_persist.directory))
		return FALSE;
	if (save_backup_enabled() && !game_state_persistent_file_path(game_state_persist.backup_path))
		game_state_persist.backup_path[0] = 0;
	image = game_state_image_get((unsigned long)buffer_size);
	if (!image)
	{
		return FALSE;
	}
	async_save_copy(image, buffer, (size_t)buffer_size);
	game_state_image_is_checkpoint = FALSE;
	game_state_persist.file = INVALID_HANDLE_VALUE;
	game_state_persist.image = image;
	game_state_persist.checksum_offset = (unsigned long)((unsigned char *)header_checksum-(unsigned char *)buffer);
	game_state_persist.header_size = (unsigned long)header_size;
	game_state_persist.buffer_size = (unsigned long)buffer_size;
	game_state_persist.game_state_size = GAME_STATE_SIZE;
	game_state_persist.result = -1;
	if (!async_save_start(game_state_persist_job, NULL))
	{
		return FALSE;
	}
	game_state_persist.pending = TRUE;
	platform_log("save and quit: the game state copied; the saved game is written on the writer thread");
	return TRUE;
}

static boolean game_state_block_same(const unsigned char *image, unsigned long block, unsigned long size)
{
	const unsigned long at = block*DELTA_BLOCK;
	const unsigned long length = size-at < DELTA_BLOCK ? size-at : DELTA_BLOCK;

	return memcmp(image+at, game_state_baseline+at, length)==0;
}

/* the writer thread's: the image to the file. With the delta switch on and a valid baseline, only the runs of blocks that differ; else all of it (and the
baseline is made from it). TRUE if the file holds the image. */
static boolean game_state_write_image(HANDLE file, void *snapshot, unsigned long size, unsigned long *bytes_written)
{
	const unsigned char *image = snapshot;
	const unsigned long piece = async_save_piece_bytes();
	const double start = game_state_async_now_ms();

	DWORD whole = 0;

	*bytes_written = 0;
	if (async_save_delta())
	{
		if (!game_state_baseline || game_state_baseline_size!=size)
		{
			if (game_state_baseline)
				free(game_state_baseline); /* (the debug memory stops the game on free(NULL)) */
			game_state_baseline = malloc(size);
			game_state_baseline_size = game_state_baseline ? size : 0;
			game_state_baseline_valid = 0;
		}
		if (game_state_baseline && game_state_baseline_valid && game_state_baseline_file==file)
		{
			const unsigned long blocks = (size+DELTA_BLOCK-1)/DELTA_BLOCK;
			unsigned long runs = 0, written = 0, block = 0;

			game_state_baseline_valid = 0;
			while (block<blocks)
			{
				unsigned long last_changed, next, end, at, length;
				DWORD wrote = 0;

				if (game_state_block_same(image, block, size))
				{
					block++;
					continue;
				}
				last_changed = block;
				for (next = block+1; next<blocks && next-last_changed<=DELTA_GAP_BLOCKS; next++)
				{
					if (!game_state_block_same(image, next, size))
						last_changed = next;
				}
				end = last_changed+1;
				at = block*DELTA_BLOCK;
				length = (end*DELTA_BLOCK<size ? end*DELTA_BLOCK : size)-at;
				if (SetFilePointer(file, (LONG)at, NULL, FILE_BEGIN)==INVALID_SET_FILE_POINTER ||
					!platform_write_file_in_pieces(file, image+at, length, piece, &wrote) || wrote!=length)
				{
					return FALSE;
				}
				memcpy(game_state_baseline+at, image+at, length);
				written += length;
				runs++;
				block = end;
			}
			game_state_baseline_valid = 1;
			*bytes_written = size;
			platform_log("checkpoint %lu: only what changed was written: %lu KB in %lu runs of the %lu KB, in %.0f ms", game_state_async.number, written/1024, runs, size/1024,
				game_state_async_now_ms()-start);
			return TRUE;
		}
	}
	game_state_baseline_valid = 0;
	if (SetFilePointer(file, 0, NULL, FILE_BEGIN)!=INVALID_SET_FILE_POINTER &&
		platform_write_file_in_pieces(file, snapshot, size, piece, &whole) && whole==size)
	{
		*bytes_written = whole;
		if (async_save_delta() && game_state_baseline)
		{
			memcpy(game_state_baseline, image, size);
			game_state_baseline_file = file;
			game_state_baseline_valid = 1;
		}
		return TRUE;
	}
	return FALSE;
}
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
	game_state_port_flush_checkpoint(1);
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

#ifdef HALO_WIIU
	/* (a new game's file: whatever the image holds is the last game's) */
	game_state_image_is_checkpoint = FALSE;
	game_state_deferred.pending = FALSE;
	game_state_deferred.skipped = 0;
#endif
#ifdef HALO_WIIU
	game_state_delta_invalidate();
#endif
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
	if (!game_state_async_settle(10000, "the saved game file is closed"))
	{
		/* (the writer thread is in the middle of a write to it: the file is left open rather than closed under it) */
		xbox_game_state_globals.file_open = FALSE;

		return;
	}
#endif
#ifdef HALO_WIIU
	game_state_delta_invalidate();
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
	if (game_state_async.stuck && game_state_async.job_handle==xbox_game_state_globals.handle)
	{
		/* (a write to this very file has not ended: not another on top of it) */
		platform_log("checkpoint: no checkpoint is saved, the last write to the file has not ended");

		return FALSE;
	}
#endif
#ifdef HALO_WIIU
	/* (written here from the game state itself: the image holds an older checkpoint's bytes, or none) */
	game_state_image_is_checkpoint = FALSE;
	game_state_deferred.pending = FALSE;
#endif
#ifdef HALO_WIIU
	game_state_delta_invalidate();
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
	if (!game_state_async_settle(15000, "a revert"))
	{
		match_vassert(
			"c:\\halo\\SOURCE\\saved games\\game_state_xbox.c",
			156,
			FALSE,
			csprintf(temporary, "couldn't read saved game file (#%d, step %d): its write has not ended", GetLastError(), 0));
		return FALSE;
	}
#endif
#ifdef HALO_WIIU
	/* port: the last checkpoint's bytes are in the image already (the copy that was written to the file): the game state is taken from them, checked as a read file is,
	and the 20 MiB are not read back from the card. A copy that is not accepted falls through to the file. */
	if (async_save_revert_image() && game_state_image_is_checkpoint && game_state_image && xbox_game_state_globals.file_valid_for_read && !game_state_async.lost)
	{
		const double taken_start = game_state_async_now_ms();

		if (game_state_image_accept(game_state_image, xbox_game_state_globals.buffer_size))
		{
			platform_log("checkpoint: the saved game is taken from the copy held in memory in %.0f ms (the card is not read; halo/async_save_revert_image.txt: 0 reads it)",
				game_state_async_now_ms()-taken_start);

			return TRUE;
		}
		platform_log("checkpoint: the copy held in memory is not accepted: the file is read");
	}
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

#ifdef HALO_WIIU
/* port: the saved game's file (the profile's directory and its name), for the backup beside it (halo/save_backup.txt) */
static boolean game_state_persistent_file_path(
	char *path)
{
	if (!game_state_get_persistent_storage_path(path))
		return FALSE;
	strcat(path, game_state_get_persistent_storage_filename());

	return TRUE;
}

/* (a saved game's header is zeroed while its write is under way and written last: a header of zeroes is a write that did not end, or no saved game) */
static void game_state_backup_persistent_storage(
	const char *why)
{
	char path[512];

	(void)why;
	if (save_backup_enabled() && game_state_persistent_file_path(path))
	{
		save_backup_keep(path, 1, GAME_STATE_FILE_SIZE, 2048, NULL, "saved game");
	}

	return;
}
#endif

static boolean game_state_read_header_from_persistent_storage_once(
	void *header,
	unsigned long *header_checksum,
	long header_size,
	long buffer_size,
	boolean *corrupted);

boolean game_state_read_header_from_persistent_storage(
	void *header,
	unsigned long *header_checksum,
	long header_size,
	long buffer_size,
	boolean *corrupted)
{
	boolean result = game_state_read_header_from_persistent_storage_once(header, header_checksum, header_size, buffer_size, corrupted);

#ifdef HALO_WIIU
	/* port: a saved game that fails its checksum (or cannot be read) is put away and the one before it tried, once (halo/save_backup.txt) */
	if (!result && save_backup_enabled())
	{
		char path[512];

		if (game_state_persistent_file_path(path) && save_backup_swap_in(path, "the saved game"))
		{
			result = game_state_read_header_from_persistent_storage_once(header, header_checksum, header_size, buffer_size, corrupted);
			save_backup_swap_out(path, result != FALSE, "the saved game");
		}
	}
#endif

	return result;
}

static boolean game_state_read_header_from_persistent_storage_once(
	void *header,
	unsigned long *header_checksum,
	long header_size,
	long buffer_size,
	boolean *corrupted)
{
#ifdef HALO_WIIU
	game_state_persist_settle();
#endif
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

boolean game_state_read_header_only_from_persistent_storage(
	void *header,
	long header_size)
{
	HANDLE file = game_state_open_persistent_storage(NULL);
	unsigned long bytes_read = 0;
	boolean result = FALSE;

	if (file != INVALID_HANDLE_VALUE)
	{
		if (SetFilePointer(file, 0, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
			ReadFile(file, header, header_size, &bytes_read, NULL) && bytes_read == (unsigned long)header_size)
		{
			game_state_header_terminate(header, header_size);
			result = TRUE;
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

#ifdef HALO_WIIU
	if (game_state_persist_async(buffer, header_checksum, header_size, buffer_size))
	{
		return;
	}
	game_state_backup_persistent_storage("Save and Quit");
#endif
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
#ifdef HALO_WIIU
	game_state_persist_settle();
#endif
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

/* port: whether the image holds the bytes of the last checkpoint: the copy game_state_async_begin took (the file has the same bytes once the writer thread has written it). A revert
takes the game state from it and does not read the 20 MiB back from the card (1.4 to 1.8 s on the console, 4 times in the owner's 30 minutes of a50, 2026-11-06). Whoever gets the image
for any other use (game_state_image_get) puts something else in it, and it is not the checkpoint any more. */
#ifdef HALO_WIIU
static void *game_state_image_get_keeping(
	unsigned long size)
{
	/* (the image is what a checkpoint's copy is held in until the writer thread has written it: whoever wants it waits for that first) */
	if (!game_state_async_settle(15000, "the image is wanted"))
	{
		return NULL;
	}
	if (game_state_image_size < size)
	{
		/* (the game's free halts on a null pointer: debug_memory.c) */
		if (game_state_image)
		{
			free(game_state_image);
		}
		game_state_image = malloc(size);
		game_state_image_size = game_state_image ? size : 0;
		game_state_image_is_checkpoint = FALSE;
	}

	return game_state_image;
}
#endif

static void *game_state_image_get(
	unsigned long size)
{
#ifdef HALO_WIIU
	void *image;

	/* (a checkpoint kept in the image for the pause is on the card before the image is another's: halo/checkpoint_writes.txt) */
	game_state_deferred_flush("the image is wanted");
	image = game_state_image_get_keeping(size);

	/* (what the caller puts in it is its own) */
	game_state_image_is_checkpoint = FALSE;

	return image;
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

#ifdef HALO_WIIU
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
find a write still running), 4 makes it 4 s more, 5 makes it 9 s more and the time every wait for a write is given 0.5 s (a card that does not answer: the end of the game,
a revert and the next checkpoint then give up on the write, which the log says); 0 or absent: none. */
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
				fault==3 ? "every write takes 800 ms more" : fault==4 ? "every write takes 4 s more" : fault>4 ? "every write takes 9 s more, and a wait for it gives up after 0.5 s" : fault==2 ? "every second checkpoint's write fails, and so does its second try" : "every second checkpoint's write fails");
		}
	}

	return fault;
}

/* how long a wait for the write is given (a write takes 1.5 s on the console: a card that has not answered in many times that is not coming back) */
static unsigned long game_state_async_bound(
	unsigned long milliseconds)
{
	return game_state_async_fault()>=5 ? 500 : milliseconds;
}

/* the buffer the file is read back in: made on the game thread (the game's memory is not the writer thread's to allocate from: debug_memory.c is not made for two threads),
only when the check is on, and kept */
enum
{
	GAME_STATE_VERIFY_PIECE_SIZE = 1024*1024
};

static byte *game_state_verify_piece;

static void game_state_verify_piece_make(
	void)
{
	if (async_save_verify() && !game_state_verify_piece)
	{
		game_state_verify_piece = malloc(GAME_STATE_VERIFY_PIECE_SIZE);
	}

	return;
}

/* the file read back and compared with what was written (halo/async_save_verify.txt), 1 if it is the same */
static boolean game_state_verify_file(
	HANDLE file,
	const void *expected,
	unsigned long size)
{
	const unsigned long piece_size = GAME_STATE_VERIFY_PIECE_SIZE;
	byte *piece = game_state_verify_piece;
	unsigned long offset = 0;
	boolean same = piece!=NULL;

	if (same && SetFilePointer(file, 0, NULL, FILE_BEGIN)==INVALID_SET_FILE_POINTER)
	{
		same = FALSE;
	}
	while (same && offset<size)
	{
		unsigned long count = size-offset<piece_size ? size-offset : piece_size;
		unsigned long bytes_read = 0;

		if (!ReadFile(file, piece, count, &bytes_read, NULL) ||
			bytes_read!=count ||
			memcmp(piece, (const byte *)expected+offset, count)!=0)
		{
			same = FALSE;
		}
		offset += count;
	}

	return same;
}

static void game_state_verify_sync_write(
	void)
{
	game_state_verify_piece_make();
	if (async_save_verify())
	{
		game_state_async.number++;
		platform_log("checkpoint %lu: crc %08lx of %lu bytes (written on the game thread), file %s", game_state_async.number,
			game_state_async_crc(xbox_game_state_globals.buffer, xbox_game_state_globals.buffer_size), (unsigned long)xbox_game_state_globals.buffer_size,
			game_state_verify_file(xbox_game_state_globals.handle, xbox_game_state_globals.buffer, xbox_game_state_globals.buffer_size) ? "read back the same" : "READ BACK DIFFERENT");
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
	const HANDLE file = game_state_async.job_handle;
	unsigned long bytes_written = 0;
	unsigned long error = 0;
	char note[48];
	boolean ok = FALSE;

	(void)argument;
	note[0] = 0;
	if (game_state_async_fault()>=3)
	{
		Sleep(game_state_async_fault()==3 ? 800 : game_state_async_fault()==4 ? 4000 : 9000);
	}
	if (game_state_async_fault() && game_state_async_fault()<3 && (game_state_async.number&1)==0)
	{
		/* (a test's failure: the first piece written, then an error, as a card that fills up or is pulled would */
		SetFilePointer(file, 0, NULL, FILE_BEGIN);
		platform_write_file_in_pieces(file, snapshot, async_save_piece_bytes(), async_save_piece_bytes(), &bytes_written);
		error = 0xFA17;
		snprintf(note, sizeof(note), " - error %lu", error);
	}
	else if (game_state_write_image(file, snapshot, size, &bytes_written) &&
		bytes_written==size)
	{
		ok = TRUE;
	}
	else
	{
		error = GetLastError();
		snprintf(note, sizeof(note), " - error %lu", error);
	}
	if (ok && async_save_verify() && !game_state_verify_file(file, snapshot, size))
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

/* a checkpoint kept in memory without its write (halo/checkpoint_writes.txt) is written now: handed to the writer thread, or made here if that cannot be. Nothing if none is kept. */
static void game_state_deferred_flush(
	const char *why)
{
	unsigned long bytes_written = 0;
	const unsigned long size = xbox_game_state_globals.buffer_size;

	if (!game_state_deferred.pending)
		return;
	if (!xbox_game_state_globals.file_open || !game_state_image_is_checkpoint || !game_state_image || game_state_image_size < size)
	{
		game_state_deferred.pending = FALSE;
		platform_log("checkpoint %lu: kept in memory and not written to the card (%s): the file is not open or the image is not the checkpoint any more", game_state_async.number, why);
		return;
	}
	if (!game_state_async_settle(15000, why))
		return; /* (a write is stuck: the checkpoint stays pending, the image and the file are left alone) */
	game_state_deferred.pending = FALSE;
	game_state_async.job_handle = xbox_game_state_globals.handle;
	game_state_async.result = -1;
	game_state_async_fault();
	game_state_verify_piece_make();
	if (async_save_start(game_state_async_write, NULL))
	{
		game_state_async.pending = TRUE;
		platform_log("checkpoint %lu: written to the card now (%s), %lu earlier write%s skipped; on the writer thread", game_state_async.number, why, game_state_deferred.skipped-1,
			game_state_deferred.skipped==2 ? "" : "s");
		game_state_deferred.skipped = 0;
		return;
	}
	game_state_deferred.skipped = 0;
	if (game_state_write_image(xbox_game_state_globals.handle, game_state_image, size, &bytes_written) && bytes_written==size)
	{
		platform_log("checkpoint %lu: written to the card now (%s), on the game thread", game_state_async.number, why);
	}
	else
	{
		game_state_async.lost = TRUE;
		game_state_image_is_checkpoint = FALSE;
		xbox_game_state_globals.file_valid_for_read = FALSE;
		platform_log("checkpoint %lu: LOST: it could not be written to the card (%s, error %lu)", game_state_async.number, why, (unsigned long)GetLastError());
	}
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
	game_state_persist_settle();
	if (!game_state_async_settle(15000, "the next checkpoint"))
	{
		/* (the last write has not ended: this one is made here, from the game state, to this file) */
		platform_log("checkpoint: the last write is still running: written on the game thread");
		return FALSE;
	}
	image = game_state_image_get_keeping(xbox_game_state_globals.buffer_size);
	if (!image)
	{
		platform_log("checkpoint: no image to copy the game state to: written on the game thread");
		return FALSE;
	}
	game_state_image_is_checkpoint = FALSE; /* (until the copy is whole) */

	start = game_state_async_now_ms();
	async_save_copy(image, xbox_game_state_globals.buffer, xbox_game_state_globals.buffer_size);
	copy_ms = game_state_async_now_ms()-start;
	game_state_image_is_checkpoint = TRUE;
	game_state_async.number++;
	if (async_save_verify())
	{
		platform_log("checkpoint %lu: crc %08lx of %lu bytes, copy %s", game_state_async.number,
			game_state_async_crc(image, xbox_game_state_globals.buffer_size), (unsigned long)xbox_game_state_globals.buffer_size,
			memcmp(image, xbox_game_state_globals.buffer, xbox_game_state_globals.buffer_size)==0 ? "the same as the game state" : "DIFFERENT FROM THE GAME STATE");
	}
	if (async_save_checkpoint_writes() && async_save_revert_image())
	{
		/* (halo/checkpoint_writes.txt: the copy is the checkpoint, a revert takes the game state from it; the card is written at the pause (1) and when the game ends) */
		if (game_state_deferred.pending)
			platform_log("checkpoint %lu: the one before it (%lu) was never written to the card: this one replaces it", game_state_async.number, game_state_async.number-1);
		game_state_deferred.pending = TRUE;
		game_state_deferred.skipped++;
		game_state_async.lost = FALSE;
		xbox_game_state_globals.file_valid_for_read = TRUE;
		platform_log("checkpoint %lu: the game state copied in %.1f ms (%lu KB); the write to the card is skipped (halo/checkpoint_writes.txt %d), it is kept in memory",
			game_state_async.number, copy_ms, (unsigned long)xbox_game_state_globals.buffer_size/1024, async_save_checkpoint_writes());
		return TRUE;
	}
	game_state_deferred.pending = FALSE;
	game_state_async_fault(); /* (read here, on the game thread) */
	game_state_verify_piece_make();
	game_state_async.job_handle = xbox_game_state_globals.handle;
	game_state_async.result = -1;
	if (!async_save_start(game_state_async_write, NULL))
	{
		game_state_async.number--;
		game_state_image_is_checkpoint = FALSE;
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

/* waits for the write handed over to end, at most timeout_ms (game_state_async_bound); FALSE if it has not: the write is stuck (the card), the file and the copy are left
alone until it ends, and the checkpoint is counted lost. A write that failed is made once more here from the copy. */
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
	if (game_state_async.stuck && async_save_running())
	{
		return FALSE;
	}
	start = game_state_async_now_ms();
	if (!async_save_wait(game_state_async_bound(timeout_ms)))
	{
		platform_log("checkpoint %lu: its write is still running after %lu ms (%s): given up on", game_state_async.number, game_state_async_bound(timeout_ms), why);
		game_state_async.stuck = TRUE;
		game_state_async.lost = TRUE;

		return FALSE;
	}
	waited_ms = game_state_async_now_ms()-start;
	game_state_async.pending = FALSE;
	if (game_state_async.stuck)
	{
		game_state_async.stuck = FALSE;
		platform_log("checkpoint %lu: its write has ended (after the wait for it was given up)", game_state_async.number);
		if (game_state_async.job_handle!=xbox_game_state_globals.handle || !xbox_game_state_globals.file_open)
		{
			/* (the game it was for is over, and its file closed or left: nothing of it is wanted) */
			return TRUE;
		}
		game_state_async.lost = FALSE;
	}
	if (waited_ms>=1.0)
	{
		platform_log("checkpoint %lu: waited %.0f ms for its write (%s)", game_state_async.number, waited_ms, why);
	}
	if (game_state_async.result!=1)
	{
		unsigned long bytes_written = 0;
		void *snapshot = game_state_image_get_keeping(xbox_game_state_globals.buffer_size);

		platform_log("checkpoint %lu: its write failed on the writer thread: written again on the game thread", game_state_async.number);
		game_state_delta_invalidate();
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
			game_state_image_is_checkpoint = FALSE;
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
	game_state_async_settle(15000, why);

	return;
}

/* the pause menu opens (ui_widget.c) or the game ends: a checkpoint kept in memory is written (halo/checkpoint_writes.txt 1: at the pause too, 2: only when the game ends) */
void game_state_port_flush_checkpoint(
	int ending)
{
	if (game_state_deferred.pending && (ending || async_save_checkpoint_writes()==1))
		game_state_deferred_flush(ending ? "the game ends" : "the game is paused");

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
	game_state_port_flush_checkpoint(1);

	return async_save_wait(timeout_ms);
}
#else
/* (the checkpoint's write on a thread of its own is the Wii U's: elsewhere the write is made where it always was, and none is waited for) */
void game_state_port_flush_checkpoint(
	int ending)
{
	(void)ending;

	return;
}

void game_state_port_wait_for_save(
	const char *why)
{
	(void)why;

	return;
}

boolean game_state_port_save_lost(
	void)
{
	return FALSE;
}

int game_state_port_save_wait(
	unsigned long timeout_ms)
{
	(void)timeout_ms;

	return 1;
}
#endif
