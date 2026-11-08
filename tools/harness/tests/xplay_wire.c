/* xplay_wire.c: the distributed netcode's messages (network_distributed.c's, ids 1 to 95) as a machine sends them: each
entry structure of the layout tables (xplay_layouts_*.inc) filled with a different value in every scalar, the message made
as the sender's code makes it (a header and entries) and put through the real wire step, distributed_wire_message
(network_distributed.c), which turns a message to the wire's byte order, little-endian, on a big-endian machine and does
nothing on a little-endian one. Expected, for each scalar: its bytes little-endian. The message is then put through the step
again, as a receiver does, and must be the sender's structures again.

Prints one line of hex a message, which is the golden file (tests/xplay_golden/wire.txt) the two byte orders must both
equal. */

#include "xplay_common.h"
#include "network_distributed.c"
#include "network_shield_effects.c"
#include "network_aim_assist.c"
#include "xplay_layouts_distributed.inc"
#include "xplay_layouts_objects.inc"
#include "xplay_layouts_damage.inc"
#include "xplay_layouts_actors.inc"
#include "xplay_layouts_coop.inc"
#include "xplay_layouts_voice.inc"
#include "xplay_layouts_chat.inc"
#include "xplay_layouts_votekick.inc"
#include "xplay_layouts_map_vote.inc"
#include "xplay_layouts_map_share.inc"
#include "xplay_layouts_infection.inc"

/* ---------- what the transport would be handed: captured */

static unsigned char sent_unreliable[1500];
static long sent_unreliable_size;
static unsigned char sent_reliable[4096];
static long sent_reliable_size;

#define GAME_TIME 0x01C2D3E4
long game_time_get(void)
{
	return GAME_TIME;
}

boolean network_distributed_client_send(void *message, word size)
{
	memcpy(sent_unreliable, message, size);
	sent_unreliable_size = size;
	return TRUE;
}

boolean network_distributed_server_send_to_machine_reliably(long machine_index, void *message, word size)
{
	memcpy(sent_reliable, message, size);
	sent_reliable_size = size;
	return TRUE;
}

boolean network_distributed_client_send_reliably(void *message, word size)
{
	memcpy(sent_reliable, message, size);
	sent_reliable_size = size;
	return TRUE;
}

#define COUNTOF(array) ((long)(sizeof(array) / sizeof((array)[0])))

static const struct xplay_structure *find_structure(const char *name)
{
#define SEARCH(table) \
	{ \
		long index; \
		for (index = 0; index < COUNTOF(table); index++) \
			if (!strcmp(table[index].name, name)) \
				return &table[index]; \
	}
	SEARCH(xplay_structures_distributed)
	SEARCH(xplay_structures_objects)
	SEARCH(xplay_structures_damage)
	SEARCH(xplay_structures_actors)
	SEARCH(xplay_structures_coop)
	SEARCH(xplay_structures_voice)
	SEARCH(xplay_structures_chat)
	SEARCH(xplay_structures_votekick)
	SEARCH(xplay_structures_map_vote)
	SEARCH(xplay_structures_map_share)
	SEARCH(xplay_structures_infection)
#undef SEARCH
	return NULL;
}

/* the messages that go through distributed_wire_entries, with the entry each carries */
static const struct
{
	byte type;
	const char *name;
	const char *entry;
	short count;
} messages[] = {
	{ _distributed_message_player_statistics, "player_statistics", "distributed_player_statistics", 0 },
	{ _distributed_message_pings, "pings", "distributed_player_ping", 0 },
	{ _distributed_message_structure_bsp, "structure_bsp", "distributed_structure_bsp", 0 },
	{ _distributed_message_pickups, "pickups", "distributed_pickup", 0 },
	{ _distributed_message_player_inputs, "player_inputs", "distributed_player_input", 0 },
	{ _distributed_message_object_changes, "object_changes", "distributed_object_change", 0 },
	{ _distributed_message_object_states, "object_states", "distributed_object_state", 0 },
	{ _distributed_message_vehicle_prediction, "vehicle_prediction", "distributed_object_state", 0 },
	{ _distributed_message_inventories, "inventories", "distributed_inventory", 0 },
	{ _distributed_message_damage_animations, "damage_animations", "distributed_damage_animation", 0 },
	{ _distributed_message_damage_events, "damage_events", "distributed_damage_event", 0 },
	{ _distributed_message_hit_reports, "hit_reports", "distributed_hit_report", 0 },
	{ _distributed_message_fire_origins, "fire_origins", "distributed_fire_origin", 0 },
	{ _distributed_message_actor_states, "actor_states", "distributed_actor_state", 0 },
	{ _distributed_message_actor_damage, "actor_damage", "distributed_actor_damage", 0 },
	{ _distributed_message_coop_presentation, "coop_presentation", "distributed_coop_presentation", 0 },
	{ _distributed_message_coop_events, "coop_events", "distributed_coop_event", 0 },
	{ _distributed_message_coop_device_groups, "coop_device_groups", "distributed_coop_device_group", 0 },
	{ _distributed_message_coop_object_names, "coop_object_names", "distributed_coop_object_names", 0 },
	{ _distributed_message_coop_object_transforms, "coop_object_transforms", "distributed_coop_object_transform", 0 },
	{ _distributed_message_coop_object_looks, "coop_object_looks", "distributed_coop_object_look", 0 },
	{ _distributed_message_coop_screen_effect, "coop_screen_effect", "rasterizer_screen_effect_port_state", 0 },
	{ _distributed_message_coop_device_states, "coop_device_states", "distributed_coop_device_state", 0 },
	/* messages of bytes, which the wire step must leave as they are (the header's game time alone is turned) */
	{ _distributed_message_chat_up, "chat_up", "distributed_chat_line", 1 },
	{ _distributed_message_chat_down, "chat_down", "distributed_chat_line", 1 },
	{ _distributed_message_chat_config, "chat_config", "distributed_chat_config", 1 },
	{ _distributed_message_votekick, "votekick", "distributed_votekick_request", 1 },
	{ _distributed_message_votekick_status, "votekick_status", "distributed_votekick_status", 1 },
	{ _distributed_message_map_vote_choice, "map_vote_choice", "distributed_map_vote_choice", 1 },
	{ _distributed_message_client_identity, "client_identity", "distributed_client_identity", 1 },
	{ _distributed_message_client_build, "client_build", "distributed_client_build", 1 },
	{ _distributed_message_coop_skip_vote, "coop_skip_vote", "distributed_coop_skip_vote", 1 },
};

static unsigned char native_message[8 + 3 * 2048];
static unsigned char expected_message[8 + 3 * 2048];
static unsigned char wire_message[8 + 3 * 2048];

/* the first byte that is not as expected, for the failure's words */
static long first_difference(const unsigned char *a, const unsigned char *b, long size)
{
	long index;

	for (index = 0; index < size; index++)
		if (a[index] != b[index])
			return index;
	return -1;
}

static const char *leaf_at(const struct xplay_structure *structure, long offset)
{
	static char text[96];
	long run;

	for (run = 0; run < structure->leaf_count; run++)
	{
		const struct xplay_leaf *leaf = &structure->leaves[run];

		if (offset >= leaf->offset && offset < leaf->offset + leaf->width * leaf->count)
		{
			sprintf(text, "scalar %ld of %ld bytes at +%ld", run, leaf->width, leaf->offset);
			return text;
		}
	}
	return "a byte";
}

static char batch_text[96];

static void test_entries(byte type, const char *name, const struct xplay_structure *entry, short count)
{
	struct distributed_message_header header;
	long size = (long)sizeof(header) + count * entry->size;
	long index, difference;
	unsigned char value[8];

	if (getenv("XPLAY_TRACE"))
		fprintf(stderr, "%s\n", name);
	memset(native_message, 0, sizeof(native_message));
	memset(&header, 0, sizeof(header));
	header.header = 0x5A3C;
	header.type = type;
	header.count = (byte)count;
	header.game_time = 0x01C2D3E4;
	memcpy(native_message, &header, sizeof(header));
	memcpy(expected_message, native_message, sizeof(header));
	/* (the header: its message header is the transport's; its game time is the one scalar) */
	xplay_value(value, 4, 99);
	xplay_store_native(native_message + offsetof(struct distributed_message_header, game_time), value, 4);
	memcpy(expected_message + offsetof(struct distributed_message_header, game_time), value, 4);
	for (index = 0; index < count; index++)
		xplay_fill(entry, native_message + sizeof(header) + index * entry->size,
			expected_message + sizeof(header) + index * entry->size, (unsigned long)(type * 16 + index + 1));

	memcpy(wire_message, native_message, (size_t)size);
	distributed_wire_message(wire_message, size);
	difference = first_difference(wire_message, expected_message, size);
	if (difference >= 0)
	{
		long within = difference < (long)sizeof(header) ? difference : (difference - (long)sizeof(header)) % entry->size;

		XPLAY_CHECK(difference < 0, "%s (%s): the wire has %02x at +%ld, not %02x (%s of the %s)", name, entry->name,
			wire_message[difference], difference, expected_message[difference],
			difference < (long)sizeof(header) ? "the header" : leaf_at(entry, within), entry->name);
	}
	{
		/* (what goes out is the message header in network byte order, which the transport turns: message_header.c) */
		unsigned char out[sizeof(wire_message)];

		memcpy(out, wire_message, (size_t)size);
		byte_swap_message_header((word *)out, _byte_order_network);
		xplay_hex(name, out, size);
	}

	/* (received: the sender's structures again) */
	distributed_wire_message(wire_message, size);
	difference = first_difference(wire_message, native_message, size);
	XPLAY_CHECK(difference < 0, "%s: received, byte +%ld is %02x, not %02x", name, difference,
		difference >= 0 ? wire_message[difference] : 0, difference >= 0 ? native_message[difference] : 0);

	/* the same message sent as the game sends it, reliably to the host and in the host's batch: the transport is handed the
	message in the wire's order (and the header's game time the game's), the batch in the format below */
	{
		unsigned char out[sizeof(wire_message)];
		unsigned char batch[1500];
		long batch_size;
		short length;

		memcpy(wire_message, native_message, (size_t)size);
		sent_reliable_size = 0;
		distributed_send(wire_message, type, count, (word)size, _distributed_to_host_reliably);
		XPLAY_CHECK(sent_reliable_size == size, "%s: sent reliably %ld bytes, not %ld", name, sent_reliable_size, size);
		/* (distributed_fill_header put the tick in, and the message header, which is the transport's to turn) */
		memcpy(out, expected_message, (size_t)size);
		memcpy(out + offsetof(struct distributed_message_header, game_time), (unsigned char[]){ GAME_TIME & 0xFF,
			(GAME_TIME >> 8) & 0xFF, (GAME_TIME >> 16) & 0xFF, (GAME_TIME >> 24) & 0xFF }, 4);
		memcpy(wire_message, sent_reliable, (size_t)size);
		byte_swap_message_header((word *)wire_message, _byte_order_network);
		difference = first_difference(wire_message + 2, out + 2, size - 2);
		XPLAY_CHECK(difference < 0, "%s: sent reliably, byte +%ld is %02x, not %02x", name, difference + 2,
			difference >= 0 ? wire_message[difference + 2] : 0, difference >= 0 ? out[difference + 2] : 0);
		sprintf(batch_text, "%s.reliable", name);
		xplay_hex(batch_text, wire_message, size);

		/* the batch: its header (8 bytes), then each message's length (2, little-endian), its type, count and game time
		(6) and its entries */
		memcpy(wire_message, native_message, (size_t)size);
		sent_unreliable_size = 0;
		distributed_send(wire_message, type, count, (word)size, _distributed_to_host);
		distributed_batch_flush(HOST_SENDER);
		length = (short)(size - 2);
		batch_size = 8 + 2 + length;
		XPLAY_CHECK(sent_unreliable_size == batch_size, "%s: the batch is %ld bytes, not %ld", name, sent_unreliable_size,
			batch_size);
		memcpy(out + 8 + 2, expected_message + 2, (size_t)length);
		out[0] = 0;
		out[1] = 0;
		out[2] = _distributed_message_batch;
		out[3] = 0;
		out[4] = GAME_TIME & 0xFF;
		out[5] = (GAME_TIME >> 8) & 0xFF;
		out[6] = (GAME_TIME >> 16) & 0xFF;
		out[7] = (GAME_TIME >> 24) & 0xFF;
		out[8] = (unsigned char)(length & 0xFF);
		out[9] = (unsigned char)(length >> 8);
		/* (the message's game time is the tick's) */
		out[12] = GAME_TIME & 0xFF;
		out[13] = (GAME_TIME >> 8) & 0xFF;
		out[14] = (GAME_TIME >> 16) & 0xFF;
		out[15] = (GAME_TIME >> 24) & 0xFF;
		memcpy(batch, sent_unreliable, (size_t)sent_unreliable_size);
		byte_swap_message_header((word *)batch, _byte_order_network);
		/* (what the batch's header word holds is the batch's size, in the message header's bits) */
		sprintf(batch_text, "%s.batch", name);
		xplay_hex(batch_text, batch, sent_unreliable_size);
		difference = first_difference(batch + 2, out + 2, batch_size - 2);
		XPLAY_CHECK(difference < 0, "%s: the batch, byte +%ld is %02x, not %02x", name, difference + 2,
			difference >= 0 ? batch[difference + 2] : 0, difference >= 0 ? out[difference + 2] : 0);
	}
}

/* ---------- the messages the code packs by hand (value by value: distributed_put) */

static void hex_of(const char *name, const void *data, long size)
{
	xplay_hex(name, data, size);
}

static void test_unit_state(void)
{
	const struct xplay_structure *structure = find_structure("distributed_unit_state");
	unsigned char native[sizeof(struct distributed_unit_state)], expected[sizeof(struct distributed_unit_state)];
	struct distributed_unit_state state, again;
	byte buffer[DISTRIBUTED_UNIT_STATE_MAXIMUM_SIZE];
	word size, taken;
	long variant;

	for (variant = 0; variant < 3; variant++)
	{
		char text[64];

		xplay_fill(structure, native, expected, 40 + (unsigned long)variant);
		memcpy(&state, native, sizeof(state));
		state.player_index = 3;
		state.killing_player_index = 5;
		state.unit_flags = 0x41;
		state.active_camouflage = 0x99;
		if (variant == 0)
		{
			/* alive, placed, predicted, in a vehicle, damaged, with powerups and camouflage: every part */
			state.flags = FLAG(_distributed_unit_alive_bit) | FLAG(_distributed_unit_placed_bit) |
				FLAG(_distributed_unit_predicted_bit);
			state.unit_index = 0x1234ABCD;
			state.vehicle_index = 0x0BCD1234;
			state.up.i = 0x1122;
			state.up.j = 0x3344;
			state.up.k = 0x5566;
			state.powerup_durations[0] = 0x0102;
		}
		else if (variant == 1)
		{
			/* alive, not placed, no vehicle, nothing else */
			state.flags = FLAG(_distributed_unit_alive_bit);
			state.unit_index = 0x1234ABCD;
			state.vehicle_index = NONE;
			state.current_body_damage = state.recent_body_damage = state.current_shield_damage =
				state.recent_shield_damage = 0;
			memset(state.powerup_durations, 0, sizeof(state.powerup_durations));
			state.unit_flags = 0;
			state.active_camouflage = 0;
		}
		else
		{
			/* dead, killed by a player */
			state.flags = 0;
		}
		size = distributed_unit_state_write(&state, TRUE, buffer);
		sprintf(text, "unit_state.%ld", variant);
		hex_of(text, buffer, size);
		taken = distributed_unit_state_read(buffer, buffer + size, &again);
		XPLAY_CHECK(taken == size, "%s: read %d of %d bytes", text, taken, size);
		XPLAY_CHECK(again.player_index == state.player_index && again.flags == state.flags, "%s: player and flags", text);
		if (TEST_FLAG(state.flags, _distributed_unit_alive_bit))
		{
			XPLAY_CHECK(again.unit_index == state.unit_index, "%s: unit %lx, not %lx", text, again.unit_index,
				state.unit_index);
			XPLAY_CHECK(again.body_vitality == state.body_vitality && again.shield_vitality == state.shield_vitality,
				"%s: vitality", text);
			if (variant == 0)
			{
				XPLAY_CHECK(again.vehicle_index == state.vehicle_index && again.seat_index == state.seat_index,
					"%s: vehicle", text);
				XPLAY_CHECK(!memcmp(&again.position, &state.position, sizeof(state.position)), "%s: position", text);
				XPLAY_CHECK(!memcmp(&again.velocity, &state.velocity, sizeof(state.velocity)) &&
					!memcmp(&again.forward, &state.forward, sizeof(state.forward)) &&
					!memcmp(&again.up, &state.up, sizeof(state.up)), "%s: direction", text);
				XPLAY_CHECK(again.predicted_time == state.predicted_time, "%s: tick", text);
				XPLAY_CHECK(again.current_body_damage == state.current_body_damage &&
					again.recent_shield_damage == state.recent_shield_damage, "%s: damage", text);
				XPLAY_CHECK(!memcmp(again.powerup_durations, state.powerup_durations, sizeof(state.powerup_durations)),
					"%s: powerups", text);
				XPLAY_CHECK(again.unit_flags == state.unit_flags, "%s: camouflage", text);
			}
		}
		else
			XPLAY_CHECK(again.killing_player_index == state.killing_player_index, "%s: killer", text);
	}
}

static void test_relayed_action(void)
{
	const struct xplay_structure *structure = find_structure("distributed_relayed_action");
	unsigned char native[sizeof(struct distributed_relayed_action)], expected[sizeof(struct distributed_relayed_action)];
	struct distributed_relayed_action action, again;
	byte buffer[DISTRIBUTED_RELAYED_ACTION_MAXIMUM_SIZE];
	word size, taken;
	short history;

	xplay_fill(structure, native, expected, 77);
	memcpy(&action, native, sizeof(action));
	action.player_index = 6;
	action.desired_weapon_index = 1;
	action.desired_grenade_index = 2;
	action.desired_zoom_level = 3;
	action.throttle_i = -100;
	action.throttle_j = 90;
	action.primary_trigger = 0xC8;
	/* (the buttons of the ticks before the latest, each different from the one after) */
	for (history = 1; history < DISTRIBUTED_INPUT_HISTORY; history++)
		if (action.control_flags[history] == action.control_flags[history - 1])
			action.control_flags[history] ^= 0x4001;
	size = distributed_relayed_action_write(&action, buffer);
	hex_of("relayed_action", buffer, size);
	taken = distributed_relayed_action_read(buffer, buffer + size, &again);
	XPLAY_CHECK(taken == size, "relayed action: read %d of %d bytes", taken, size);
	XPLAY_CHECK(again.player_index == action.player_index && again.yaw == action.yaw && again.pitch == action.pitch &&
		again.throttle_i == action.throttle_i && again.throttle_j == action.throttle_j &&
		again.primary_trigger == action.primary_trigger, "relayed action: the sticks");
	XPLAY_CHECK(!memcmp(again.control_flags, action.control_flags, sizeof(action.control_flags)), "relayed action: buttons");
}

/* a packed message in the host's batch: the unit states of two players, and the host's relayed actions with the update first */
static void test_packed_messages(void)
{
	struct distributed_unit_state state;
	struct distributed_relayed_action action;
	byte entry[DISTRIBUTED_UNIT_STATE_MAXIMUM_SIZE];
	byte action_entry[DISTRIBUTED_RELAYED_ACTION_MAXIMUM_SIZE];
	unsigned long update = 0x0A1B2C3D;
	unsigned char out[1500];
	long index;

	memset(&state, 0, sizeof(state));
	state.unit_index = 0x1234ABCD;
	state.vehicle_index = NONE;
	state.flags = FLAG(_distributed_unit_alive_bit);
	state.body_vitality = 0x1234;
	state.shield_vitality = 0x5678;
	memset(&action, 0, sizeof(action));
	action.control_flags[0] = 0x1357;
	action.yaw = 0x2468;

	sent_unreliable_size = 0;
	distributed_packer_begin(&distributed_packer, HOST_SENDER, _distributed_message_unit_states, NULL, 0);
	for (index = 0; index < 2; index++)
	{
		state.player_index = (byte)(index + 1);
		distributed_packer_add(&distributed_packer, entry, distributed_unit_state_write(&state, FALSE, entry));
	}
	distributed_packer_flush(&distributed_packer);
	distributed_batch_flush(HOST_SENDER);
	memcpy(out, sent_unreliable, (size_t)sent_unreliable_size);
	byte_swap_message_header((word *)out, _byte_order_network);
	hex_of("packed.unit_states", out, sent_unreliable_size);

	sent_unreliable_size = 0;
	distributed_packer_begin(&distributed_packer, HOST_SENDER, _distributed_message_relayed_actions, &update, sizeof(update));
	for (index = 0; index < 2; index++)
	{
		action.player_index = (byte)(index + 1);
		distributed_packer_add(&distributed_packer, action_entry, distributed_relayed_action_write(&action, action_entry));
	}
	distributed_packer_flush(&distributed_packer);
	distributed_batch_flush(HOST_SENDER);
	memcpy(out, sent_unreliable, (size_t)sent_unreliable_size);
	byte_swap_message_header((word *)out, _byte_order_network);
	hex_of("packed.relayed_actions", out, sent_unreliable_size);
	/* (the update, the message's first long, is the wire's: at the batch's byte 16, after the batch header (8), the length
	(2) and the message's type, count and game time (6)) */
	XPLAY_CHECK(sent_unreliable_size > 20 && out[16] == 0x3D && out[17] == 0x2C && out[18] == 0x1B && out[19] == 0x0A,
		"the relayed actions' update is %02x%02x%02x%02x, not 3d2c1b0a", out[16], out[17], out[18], out[19]);
}

/* the messages whose own code turns their words before they go: the host's shield effects choice and its aim assist policy,
and the messages of bytes with no entries (the game state's and notices' are bytes, a header alone for the object sync and the
client's ready) */
static void test_policy_messages(void)
{
	struct shield_effects_message shield;
	struct aim_assist_policy_message policy;
	unsigned char out[64];

	memset(&shield, 0, sizeof(shield));
	shield_effects_message_make(&shield, 0x1203);
	sent_reliable_size = 0;
	distributed_send_to_machine_reliably(0, &shield, _distributed_message_shield_effects, 1, (word)sizeof(shield));
	XPLAY_CHECK(sent_reliable_size == (long)sizeof(shield) && sent_reliable[8] == 0x03 && sent_reliable[9] == 0x12 &&
		sent_reliable[10] == 0 && sent_reliable[11] == 0, "shield effects: the choice is %02x%02x on the wire, not 0312", sent_reliable[8],
		sent_reliable[9]);
	memcpy(out, sent_reliable, (size_t)sent_reliable_size);
	byte_swap_message_header((word *)out, _byte_order_network);
	xplay_hex("shield_effects", out, sent_reliable_size);

	memset(&policy, 0, sizeof(policy));
	policy.block_mouse = 0x0102;
	aim_assist_policy_message_wire_order(&policy);
	sent_reliable_size = 0;
	distributed_send_to_machine_reliably(0, &policy, _distributed_message_aim_assist_policy, 1, (word)sizeof(policy));
	XPLAY_CHECK(sent_reliable_size == (long)sizeof(policy) && sent_reliable[8] == 0x02 && sent_reliable[9] == 0x01,
		"aim assist policy: the word is %02x%02x on the wire, not 0201", sent_reliable[8], sent_reliable[9]);
	memcpy(out, sent_reliable, (size_t)sent_reliable_size);
	byte_swap_message_header((word *)out, _byte_order_network);
	xplay_hex("aim_assist_policy", out, sent_reliable_size);
}

static const struct xplay_structure bytes_only[] = {
	{ "bytes", 0, NULL, 0 },
	{ "bytes", 37, NULL, 0 },
};

int main(int argc, char **argv)
{
	long index;

	(void)argc;
	(void)argv;
	for (index = 0; index < COUNTOF(messages); index++)
	{
		const struct xplay_structure *entry = find_structure(messages[index].entry);

		if (!entry)
		{
			XPLAY_CHECK(0, "no layout for %s", messages[index].entry);
			continue;
		}
		test_entries(messages[index].type, messages[index].name, entry, messages[index].count ? messages[index].count : 2);
	}
	test_entries(_distributed_message_objects_synchronized, "objects_synchronized", &bytes_only[0], 0);
	test_entries(_distributed_message_client_ready, "client_ready", &bytes_only[0], 1);
	test_entries(_distributed_message_game_state, "game_state", &bytes_only[1], 0);
	test_entries(_distributed_message_notice, "notice", &bytes_only[1], 0);
	test_policy_messages();
	test_unit_state();
	test_relayed_action();
	test_packed_messages();
	return xplay_failures ? XPLAY_FAIL_STATUS : 0;
}
