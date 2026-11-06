/*
WIIU_CARD.C

The SD card's free space for the game's map downloads (port/linux/game/map_share.c). newlib's statvfs refuses
the card (the updater found the same, wiiu_update.c), so this asks the file system service as the updater does:
FSAGetFreeSpaceSize on the card's root. It reads the card's allocation table and can take a while on a big
card, so the caller asks from a thread of its own. Not run on a console.
*/

#include <coreinit/filesystem_fsa.h>
#include <stdint.h>

#define CARD_ROOT "/vol/external01"

int wiiu_card_free_bytes(unsigned long long *bytes);

static int fsa_started;
static FSAClientHandle fsa_client = -1;

/* 0: the card's free space is not asked for. FSAGetFreeSpaceSize read the card's allocation table with the file system
service held, and on the owner's card a first download stalled every file read of the game for 29.7 seconds (a
64 KB read of cache002.map took 29,689 ms, 2026-11-06), long enough to lose the connection. A download goes on
without the room check (map_share.c: "could not be read: going on"), and a full card ends it with a write error. */
int wiiu_card_free_bytes(unsigned long long *bytes)
{
	(void)bytes;
	(void)fsa_started;
	(void)fsa_client;
	return 0;
}
