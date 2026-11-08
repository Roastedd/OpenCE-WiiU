/* xplay_messages.c: the game's own messages (network_messages.c: the connection and join handshake, the game settings,
advertisement and update messages, 35 types) as a machine encodes them, and the two structures they carry raw (the game
advertisement and the game settings record, network_wire_order.c).

Each message type is made from a structure with a different value in every field of its packet definition, encoded by the real
create_network_game_message (the data packet encoder, which writes shorts and longs in network byte order) and decoded again
by decode_network_game_message into the structure it was made from. The lines it prints are the golden file
(tests/xplay_golden/messages.txt) both byte orders must equal.

The advertisement and the settings record carry their scalars little-endian, whatever the machine: for them each scalar is
expected little-endian at its offset (from the layout tables), the settings record for each game type whose settings the union
of its variant holds. */

#include "xplay_common.h"
#include "network_messages.c"
#include "network_wire_order.c"
#include "xplay_layouts_raw.inc"

#define COUNTOF(array) ((long)(sizeof(array) / sizeof((array)[0])))

/* ---------- a packet definition's fields */

/* the size of one element of an array field: the fields after it, up to the end that closes them */
static long fields_size(const struct data_packet_field *field, const struct data_packet_field **after)
{
	long size = 0;

	while (field->type != _data_packet_field_end)
	{
		if (field->type == _data_packet_field_array)
		{
			const struct data_packet_field *end;

			fields_size(field + 1, &end);
			size += field->size;
			field = end;
		}
		else
			size += field->size;
		field++;
	}
	if (after)
		*after = field;
	return size;
}

static unsigned long field_seed;

/* fills a structure for the fields: each short, long and 64-bit number with a value of its own, bytes with a pattern (a
raw field of the advertisement and the settings fragment is a pattern too: bytes), padding with zeroes, a string with letters;
arrays have two elements. Returns the bytes of the structure used. */
static long fill_fields(const struct data_packet_field *field, unsigned char *place)
{
	unsigned char *start = place;
	long index;

	while (field->type != _data_packet_field_end)
	{
		unsigned char value[8];

		switch (field->type)
		{
		case _data_packet_field_pad:
			memset(place, 0, (size_t)field->count);
			break;
		case _data_packet_field_bytes:
		case _data_packet_field_raw:
			for (index = 0; index < field->count; index++)
				place[index] = (unsigned char)(xplay_mix(field_seed++) >> 5);
			break;
		case _data_packet_field_shorts:
			for (index = 0; index < field->count; index++)
			{
				xplay_value(value, 2, field_seed++);
				xplay_store_native(place + index * 2, value, 2);
			}
			break;
		case _data_packet_field_longs:
			for (index = 0; index < field->count; index++)
			{
				xplay_value(value, 4, field_seed++);
				xplay_store_native(place + index * 4, value, 4);
			}
			break;
		case _data_packet_field_int64s:
			for (index = 0; index < field->count; index++)
			{
				xplay_value(value, 8, field_seed++);
				xplay_store_native(place + index * 8, value, 8);
			}
			break;
		case _data_packet_field_string:
			memset(place, 0, (size_t)field->count + 1);
			for (index = 0; index < field->count / 2; index++)
				place[index] = (unsigned char)('a' + (xplay_mix(field_seed++) >> 4) % 26);
			break;
		case _data_packet_field_data:
		{
			short used = (short)(field->count / 2);

			memcpy(place, &used, sizeof(short));
			for (index = 0; index < used; index++)
				place[2 + index] = (unsigned char)(xplay_mix(field_seed++) >> 6);
			break;
		}
		case _data_packet_field_array:
		{
			const struct data_packet_field *end;
			long element_size = fields_size(field + 1, &end);
			short elements = field->count < 2 ? field->count : 2;
			long element;

			memcpy(place, &elements, sizeof(short));
			for (element = 0; element < elements; element++)
				fill_fields(field + 1, place + 2 + element * element_size);
			place += field->size;
			field = end + 1;
			continue;
		}
		}
		place += field->size;
		field++;
	}
	return place - start;
}

/* ---------- the messages */

static unsigned char native[8192], again[8192];

static void test_message(long type)
{
	struct data_packet_entry *entry = &data_0030aa68.packets[type];
	struct data_packet_definition *definition = entry->definition;
	unsigned char *message;
	long length;
	char name[96];
	short size, packet_type = 0, version = 1;
	unsigned char out[HALO_PORT_NETWORK_PACKET_SIZE];

	if (!definition)
		return;
	if (getenv("XPLAY_TRACE"))
		fprintf(stderr, "%s\n", definition->name);
	/* (the advertisement and the settings record have their own tests below) */
	memset(native, 0, sizeof(native));
	field_seed = (unsigned long)type * 1000UL + 7;
	fill_fields(definition->fields, native);
	message = create_network_game_message((enum network_game_message_type)type, native, definition->size);
	sprintf(name, "%s", definition->name);
	if (!message)
	{
		XPLAY_CHECK(0, "%s: not made", name);
		return;
	}
	/* (the message header, a word of the length and type in this machine's order, which the transport turns) */
	length = (long)(*(word *)message >> 4);
	memcpy(out, message, (size_t)length);
	byte_swap_message_header((word *)out, _byte_order_network);
	xplay_hex(name, out, length);

	/* decoded again: the structure it was made from */
	memset(again, 0, sizeof(again));
	size = (short)(length - 2);
	XPLAY_CHECK(decode_network_game_message(again, message + 2, &size, &packet_type, &version, entry->packet_class),
		"%s: not decoded", name);
	XPLAY_CHECK(packet_type == type, "%s: decoded as type %d", name, packet_type);
	if (type != _message_server_game_advertise)
	{
		long index;

		for (index = 0; index < definition->size && again[index] == native[index]; index++)
			;
		XPLAY_CHECK(index == definition->size, "%s: decoded, not as made (byte +%ld: %02x, not %02x)", name, index,
			again[index], native[index]);
	}
}

/* ---------- the advertisement */

static const struct xplay_structure *find(const char *name)
{
	long index;

	for (index = 0; index < COUNTOF(xplay_structures_raw); index++)
		if (!strcmp(xplay_structures_raw[index].name, name))
			return &xplay_structures_raw[index];
	return NULL;
}

static void test_advertisement(void)
{
	const struct xplay_structure *structure = find("network_wire_advertisement");
	unsigned char expected[NETWORK_WIRE_ADVERTISEMENT_SIZE];
	unsigned char *message, out[HALO_PORT_NETWORK_PACKET_SIZE], decoded[NETWORK_WIRE_ADVERTISEMENT_SIZE + 8];
	unsigned char made[NETWORK_WIRE_ADVERTISEMENT_SIZE];
	long length, found, index;
	short size, packet_type = 0, version = 1;

	xplay_fill(structure, made, expected, 4242);
	message = create_network_game_message((enum network_game_message_type)_message_server_game_advertise, made,
		sizeof(made));
	XPLAY_CHECK(message != NULL, "the advertisement was not made");
	if (!message)
		return;
	length = (long)(*(word *)message >> 4);
	memcpy(out, message, (size_t)length);
	byte_swap_message_header((word *)out, _byte_order_network);
	xplay_hex("server_game_advertise", out, length);
	found = -1;
	for (index = 2; index + (long)sizeof(expected) <= length; index++)
		if (!memcmp(out + index, expected, sizeof(expected)))
			found = index;
	XPLAY_CHECK(found >= 0, "the advertisement's scalars are not little-endian on the wire");
	memset(decoded, 0, sizeof(decoded));
	size = (short)(length - 2);
	XPLAY_CHECK(decode_network_game_message(decoded, message + 2, &size, &packet_type, &version,
		data_0030aa68.packets[_message_server_game_advertise].packet_class), "the advertisement was not decoded");
	XPLAY_CHECK(!memcmp(decoded, made, sizeof(made)), "the advertisement decoded is not the one made");
}

/* ---------- the settings record */

/* the leaves of the settings record for a game type: the table's, but for the union of the variant's settings, whose scalars are
those of the game type's own member */
static void test_settings_record(const char *label, long engine, const long (*engine_leaves)[2], long engine_leaf_count)
{
	const struct xplay_structure *structure = find("network_game");
	static unsigned char made[sizeof(struct network_game)], expected[sizeof(struct network_game)], work[sizeof(struct network_game)];
	struct xplay_leaf leaves[400];
	struct xplay_structure adjusted;
	long union_offset = offsetof(struct network_game, variant.game_engine_variant);
	long union_size = sizeof(union game_engine_variant);
	long run, count = 0, index;
	unsigned char value[8];
	void const *sent;
	char name[64];

	for (run = 0; run < structure->leaf_count; run++)
	{
		const struct xplay_leaf *leaf = &structure->leaves[run];
		long end = leaf->offset + leaf->width * leaf->count;

		if (leaf->offset >= union_offset && end <= union_offset + union_size)
			continue;
		XPLAY_CHECK(end <= union_offset || leaf->offset >= union_offset + union_size, "a scalar run crosses the union");
		leaves[count++] = *leaf;
	}
	for (index = 0; index < engine_leaf_count; index++)
	{
		leaves[count].offset = engine_leaves[index][0];
		leaves[count].width = engine_leaves[index][1];
		leaves[count].count = 1;
		count++;
	}
	adjusted.name = "network_game";
	adjusted.size = structure->size;
	adjusted.leaves = leaves;
	adjusted.leaf_count = count;
	xplay_fill(&adjusted, made, expected, 600 + (unsigned long)engine);
	/* (the game type, which says which settings the union holds: a number of its own in the leaf) */
	xplay_value(value, 4, 0);
	value[0] = (unsigned char)engine;
	value[1] = value[2] = value[3] = 0;
	xplay_store_native(made + offsetof(struct network_game, variant.game_engine_index), value, 4);
	memcpy(expected + offsetof(struct network_game, variant.game_engine_index), value, 4);

	sent = network_wire_order_game_to_send(made, sizeof(made));
	memcpy(work, sent, sizeof(work));
	sprintf(name, "game_settings.%s", label);
	for (index = 0; index < (long)sizeof(work); index++)
		if (work[index] != expected[index])
			break;
	XPLAY_CHECK(index == (long)sizeof(work), "%s: the settings record's byte +%ld is %02x, not %02x", name, index, work[index],
		expected[index]);
	xplay_hex(name, work, sizeof(work));
	/* received: the record as the sender had it */
	network_wire_order_game((struct network_game *)work, TRUE);
	XPLAY_CHECK(!memcmp(work, made, sizeof(made)), "%s: received, not as sent", name);
}

#define LEAF(member) { (long)offsetof(struct network_game, variant.game_engine_variant.member), (long)sizeof(((struct network_game *)0)->variant.game_engine_variant.member) }

int main(int argc, char **argv)
{
	long type;
	static const long ctf_leaves[][2] = { LEAF(ctf.single_flag_time), LEAF(ctf.points_per_capture), LEAF(ctf.kills_score) };
	static const long oddball_leaves[][2] = { LEAF(oddball.speed_with_ball), LEAF(oddball.trait_with_ball),
		LEAF(oddball.trait_without_ball), LEAF(oddball.oddball_ball_type), LEAF(oddball.ball_spawn_count) };
	static const long race_leaves[][2] = { LEAF(race.race_type), LEAF(race.team_scoring) };

	(void)argc;
	(void)argv;
	if (getenv("XPLAY_TRACE"))
		fprintf(stderr, "initialize\n");
	initialize_network_game_packets();
	if (getenv("XPLAY_TRACE"))
		fprintf(stderr, "initialized\n");
	for (type = 0; type < data_0030aa68.group.packet_type_count; type++)
	{
		if (type == _message_server_game_advertise)
			test_advertisement();
		else
			test_message(type);
	}
	test_settings_record("ctf", game_engine_ctf, ctf_leaves, COUNTOF(ctf_leaves));
	test_settings_record("slayer", game_engine_slayer, NULL, 0);
	test_settings_record("oddball", game_engine_oddball, oddball_leaves, COUNTOF(oddball_leaves));
	test_settings_record("king", game_engine_king, NULL, 0);
	test_settings_record("race", game_engine_race, race_leaves, COUNTOF(race_leaves));
	return xplay_failures ? XPLAY_FAIL_STATUS : 0;
}
