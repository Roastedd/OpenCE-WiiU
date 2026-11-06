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

/* 1 and the card's free bytes, or 0 if they cannot be read (no client, or the call failed) */
int wiiu_card_free_bytes(unsigned long long *bytes)
{
	uint64_t free_bytes = 0;

	if (!fsa_started)
	{
		fsa_started = 1;
		FSAInit();
		fsa_client = FSAAddClient(NULL);
	}
	if (fsa_client < 0 || FSAGetFreeSpaceSize(fsa_client, CARD_ROOT, &free_bytes) < 0)
		return 0;
	*bytes = free_bytes;
	return 1;
}
