/* xplay_direct.c: the messages that skip the distributed netcode's wire step and turn their own scalars, voice chat's
(network_voice.c: a client's frame up, the host's frame down, the host's settings), map sharing's request and progress
(map_share.c) and the dedicated server's map vote (network_map_vote.c), as the game makes and sends them to the transport:
the transport is handed the bytes, which both byte orders must give alike (tests/xplay_golden/direct.txt). Their scalars are
expected little-endian at their offsets (the layout tables), and so is the game time in the header of each. */

#include "xplay_common.h"
#include "network_voice.c"
#include "map_share.c"
#include "network_map_vote.c"
#include "xplay_layouts_voice.inc"

#define GAME_TIME 0x01C2D3E4

/* ---------- what the code calls */

static unsigned char sent[2048];
static long sent_size;

long game_time_get(void)
{
	return GAME_TIME;
}

boolean game_in_progress(void)
{
	return TRUE;
}

struct network_game_server *global_network_game_server_get(void)
{
	return NULL;
}

short network_game_client_get_local_machine_index(void)
{
	return 0;
}

unsigned long system_milliseconds(void)
{
	return 1000;
}

boolean network_distributed_client_send_reliably(void *message, word size)
{
	memcpy(sent, message, size);
	sent_size = size;
	return TRUE;
}

boolean network_distributed_client_send(void *message, word size)
{
	memcpy(sent, message, size);
	sent_size = size;
	return TRUE;
}

boolean network_distributed_server_send_to_machine(long machine_index, void *message, word size)
{
	memcpy(sent, message, size);
	sent_size = size;
	return TRUE;
}

boolean network_distributed_server_send_to_machine_reliably(long machine_index, void *message, word size)
{
	memcpy(sent, message, size);
	sent_size = size;
	return TRUE;
}

static void le32(unsigned char *place, unsigned long value)
{
	place[0] = (unsigned char)value;
	place[1] = (unsigned char)(value >> 8);
	place[2] = (unsigned char)(value >> 16);
	place[3] = (unsigned char)(value >> 24);
}

/* the header every one of them has: (the message header word, the transport's), the type, the count, the game time */
static void check_header(const char *name, byte type)
{
	XPLAY_CHECK(sent_size >= 8, "%s: %ld bytes", name, sent_size);
	XPLAY_CHECK(sent[2] == type && sent[3] == 1, "%s: type %d count %d", name, sent[2], sent[3]);
	XPLAY_CHECK(!memcmp(sent + 4, (unsigned char[]){ GAME_TIME & 0xFF, (GAME_TIME >> 8) & 0xFF, (GAME_TIME >> 16) & 0xFF,
		(GAME_TIME >> 24) & 0xFF }, 4), "%s: the header's game time is %02x%02x%02x%02x, not e4d3c201 (little-endian)", name,
		sent[4], sent[5], sent[6], sent[7]);
}

static void report(const char *name)
{
	unsigned char out[2048];

	memcpy(out, sent, (size_t)sent_size);
	byte_swap_message_header((word *)out, _byte_order_network);
	xplay_hex(name, out, sent_size);
}

int main(int argc, char **argv)
{
	unsigned char packet[VOICE_MAXIMUM_PACKET];
	long index;

	(void)argc;
	(void)argv;
	for (index = 0; index < (long)sizeof(packet); index++)
		packet[index] = (unsigned char)(index * 3 + 1);

	/* a client's frame up to the host */
	voice_key = 0x0A1B2C3D;
	voice_sequence = 0x4E5F;
	sent_size = 0;
	voice_send(packet, 5);
	check_header("voice_up", _distributed_message_voice_up);
	XPLAY_CHECK(sent_size == 8 + 8 + 5, "voice_up: %ld bytes", sent_size);
	XPLAY_CHECK(sent[8] == 0x3D && sent[9] == 0x2C && sent[10] == 0x1B && sent[11] == 0x0A, "voice_up: the key is not little-endian");
	XPLAY_CHECK(sent[12] == 0x5F && sent[13] == 0x4E, "voice_up: the sequence is not little-endian");
	report("voice_up");

	/* the host's settings to a client */
	voice_machines[1].key = 0x1A2B3C4D;
	voice_host_settings.proximity = 12.5f;
	sent_size = 0;
	voice_host_tell(1);
	check_header("voice_config", _distributed_message_voice_config);
	XPLAY_CHECK(sent[8] == 0x4D && sent[9] == 0x3C && sent[10] == 0x2B && sent[11] == 0x1A, "voice_config: the key is not little-endian");
	XPLAY_CHECK(sent[16] == 125 && sent[17] == 0, "voice_config: the proximity is not little-endian (%02x%02x)", sent[16], sent[17]);
	report("voice_config");

	/* map sharing: the request a downloading machine makes, and its progress */
	{
		struct download *download = &map_share_globals.download;

		memset(download, 0, sizeof(*download));
		strcpy(download->level_name, "levels\\test\\bloodgulch");
		download->version = 0x11223344;
		download->wanted = 0x05;
		download->done_bytes = 0x01020304;
		download->total_bytes = 0x0A0B0C0D;
		sent_size = 0;
		download_ask();
		check_header("map_share_request", _distributed_message_map_share_request);
		{
			long at = 8 + (long)offsetof(struct distributed_map_share_request, version);

			XPLAY_CHECK(sent[at] == 0x44 && sent[at + 1] == 0x33 && sent[at + 2] == 0x22 && sent[at + 3] == 0x11,
				"map_share_request: the map version is not little-endian");
		}
		report("map_share_request");
		sent_size = 0;
		download_report(_progress_failed);
		check_header("map_share_progress", _distributed_message_map_share_progress);
		{
			long at = 8 + (long)offsetof(struct distributed_map_share_progress, done);

			XPLAY_CHECK(sent[at] == 0x04 && sent[at + 1] == 0x03 && sent[at + 2] == 0x02 && sent[at + 3] == 0x01,
				"map_share_progress: the bytes done are not little-endian");
			XPLAY_CHECK(sent[at + 4] == 0x0D && sent[at + 5] == 0x0C && sent[at + 6] == 0x0B && sent[at + 7] == 0x0A,
				"map_share_progress: the bytes in all are not little-endian");
		}
		report("map_share_progress");
	}

	/* the dedicated server's map vote: the header */
	{
		struct distributed_message_header header;

		memset(&header, 0, sizeof(header));
		map_vote_fill_header(&header, _distributed_message_map_vote, (word)sizeof(header));
		sent_size = sizeof(header);
		memcpy(sent, &header, sizeof(header));
		check_header("map_vote", _distributed_message_map_vote);
		report("map_vote");
	}
	return xplay_failures ? XPLAY_FAIL_STATUS : 0;
}
