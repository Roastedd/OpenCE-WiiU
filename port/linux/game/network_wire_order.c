/*
NETWORK_WIRE_ORDER.C

The byte order of what machines send each other raw.

The game's messages encode their fields one by one (data_packets.c: shorts and
longs in network byte order, by SWAP2 and SWAP4), but two of them carry a
structure raw, as its bytes are in the sender's memory: the game advertisement
and the game settings record (network_messages.c's _data_packet_field_raw).
Every machine was little-endian, so that is the wire's order; a big-endian
machine (the Wii U's) turns these structures to little-endian as it sends them
and back as it receives them, so that it plays with the others. On a
little-endian machine the functions do nothing.

What is turned is each value of 2 bytes or more, by its field: bytes and
characters are as they are, and an IPv4 address (the advertisement's XNADDR)
is in network byte order already. A game variant's per-game-type settings are a
union: the variant's game type (game_engine_index) says which.

tools/ppc-be/wire_layouts.py (the Wii U port's tools) lists the structures'
fields from their declarations, to check these against.
*/

/* ---------- headers */

#include "cseries.h"
#include "game/game_engine.h"
#include "networking/network_game_manager.h"
#include "network_wire_order.h"

#include <stddef.h>

/* ---------- constants */

enum
{
	/* message_server_game_advertise's size, network_messages.c */
	NETWORK_WIRE_ADVERTISEMENT_SIZE = 0x114,
};

/* ---------- structures */

/* message_server_game_advertise's layout (network_client_message_handler.c's
and network_server_message_handler.c's, which are private to them), for the
values it holds: what is only bytes is bytes here */
struct network_wire_advertisement
{
	byte client_nonce[8];
	byte nonce[8];
	byte key_id[8];
	byte key[16];
	/* (an XNADDR: bytes, and an IPv4 address in network byte order) */
	byte xnaddr[12];
	word port;
	word version;
	word platform;
	wchar_t game_name[16];
	byte reserved[0x1A];
	struct network_game_map map;
	short engine_type;
	short machine_count;
	short player_count;
	short maximum_player_count;
	short variant_setting;
	word flags;
	byte join_game_token[16];
};

typedef char network_wire_advertisement_size_assert[
	sizeof(struct network_wire_advertisement) == NETWORK_WIRE_ADVERTISEMENT_SIZE ? 1 : -1];
typedef char network_wire_advertisement_map_offset_assert[
	offsetof(struct network_wire_advertisement, map) == 0x74 ? 1 : -1];
typedef char network_wire_advertisement_engine_type_offset_assert[
	offsetof(struct network_wire_advertisement, engine_type) == 0xF8 ? 1 : -1];

/* ---------- private code */

#ifdef __BIG_ENDIAN__

static void wire_short(
	void *value)
{
	word *place = value;

	*place = __builtin_bswap16(*place);
}

static void wire_long(
	void *value)
{
	unsigned long *place = value;

	*place = __builtin_bswap32(*place);
}

static void wire_shorts(
	void *values,
	long count)
{
	word *place = values;
	long index;

	for (index = 0; index < count; index++)
		place[index] = __builtin_bswap16(place[index]);
}

static void wire_map(
	struct network_game_map *map)
{
	/* (its name is characters) */
	wire_long(&map->version);
}

static void wire_variant(
	struct game_variant *variant,
	boolean from_wire)
{
	long game_engine_index;

	/* (the game type that says which settings the union holds: read in this
	machine's order) */
	if (from_wire)
		wire_long(&variant->game_engine_index);
	game_engine_index = variant->game_engine_index;
	if (!from_wire)
		wire_long(&variant->game_engine_index);

	wire_shorts(variant->human_readable_game_description, NUMBEROF(variant->human_readable_game_description));
	wire_long(&variant->universal_variant.flags);
	wire_long(&variant->universal_variant.goal_radar);
	wire_long(&variant->universal_variant.respawn_time_growth);
	wire_long(&variant->universal_variant.respawn_time);
	wire_long(&variant->universal_variant.suicide_penalty);
	wire_long(&variant->universal_variant.lives);
	wire_long(&variant->universal_variant.health);
	wire_long(&variant->universal_variant.score_to_win);
	wire_long(&variant->universal_variant.weapon_set);
	wire_long(&variant->universal_variant.vehicle_set);
	switch (game_engine_index)
	{
	case game_engine_ctf:
		wire_long(&variant->game_engine_variant.ctf.single_flag_time);
		/* (Kills Score's: game_engine.h) */
		wire_long(&variant->game_engine_variant.ctf.points_per_capture);
		wire_long(&variant->game_engine_variant.ctf.kills_score);
		break;
	case game_engine_oddball:
		wire_long(&variant->game_engine_variant.oddball.speed_with_ball);
		wire_long(&variant->game_engine_variant.oddball.trait_with_ball);
		wire_long(&variant->game_engine_variant.oddball.trait_without_ball);
		wire_long(&variant->game_engine_variant.oddball.oddball_ball_type);
		wire_long(&variant->game_engine_variant.oddball.ball_spawn_count);
		break;
	case game_engine_race:
		wire_long(&variant->game_engine_variant.race.race_type);
		wire_long(&variant->game_engine_variant.race.team_scoring);
		break;
	default:
		/* (slayer's and king's are booleans and bytes) */
		break;
	}
	wire_short(&variant->flags);
}

#endif

/* ---------- public code */

#ifdef __BIG_ENDIAN__
void network_wire_values(
	void *data,
	long size,
	long width)
{
	byte *place = data;
	long offset;

	switch (width)
	{
	case 2:
		for (offset = 0; offset + 2 <= size; offset += 2)
		{
			byte low = place[offset];

			place[offset] = place[offset + 1];
			place[offset + 1] = low;
		}
		break;
	case 4:
		for (offset = 0; offset + 4 <= size; offset += 4)
		{
			byte value[4];

			csmemcpy(value, place + offset, 4);
			place[offset] = value[3];
			place[offset + 1] = value[2];
			place[offset + 2] = value[1];
			place[offset + 3] = value[0];
		}
		break;
	case 8:
		for (offset = 0; offset + 8 <= size; offset += 8)
		{
			byte value[8];
			long index;

			csmemcpy(value, place + offset, 8);
			for (index = 0; index < 8; index++)
				place[offset + index] = value[7 - index];
		}
		break;
	default:
		/* (bytes: as they are) */
		break;
	}

	return;
}
#endif

void network_wire_order_game(
	struct network_game *game,
	boolean from_wire)
{
#ifdef __BIG_ENDIAN__
	long index;

	wire_shorts(game->name, NUMBEROF(game->name));
	wire_map(&game->map);
	wire_variant(&game->variant, from_wire);
	wire_short(&game->difficulty);
	wire_short(&game->machine_count);
	for (index = 0; index < NUMBEROF(game->machines); index++)
		wire_shorts(game->machines[index].name, NUMBEROF(game->machines[index].name));
	wire_short(&game->player_count);
	for (index = 0; index < NUMBEROF(game->players); index++)
	{
		wire_shorts(game->players[index].name, NUMBEROF(game->players[index].name));
		wire_short(&game->players[index].primary_color_index);
		wire_short(&game->players[index].icon_index);
	}
	wire_short(&game->__unknown426);
	wire_long(&game->random_seed);
	wire_long(&game->number_of_games_played);
	wire_short(&game->variant_options.time_limit);
	wire_short(&game->variant_options.friendly_fire);
	wire_short(&game->variant_options.friendly_fire_penalty);
	wire_short(&game->variant_options.vehicle_respawn_time);
#else
	(void)game;
	(void)from_wire;
#endif

	return;
}

void const *network_wire_order_game_to_send(
	void const *game,
	long game_size)
{
#ifdef __BIG_ENDIAN__
	/* (the network runs on the main thread: one copy serves) */
	static struct network_game wire_game;

	if (game_size != sizeof(wire_game))
		return game;
	csmemcpy(&wire_game, game, sizeof(wire_game));
	network_wire_order_game(&wire_game, FALSE);

	return &wire_game;
#else
	(void)game_size;

	return game;
#endif
}

void network_wire_order_advertisement(
	void *advertisement,
	boolean from_wire)
{
#ifdef __BIG_ENDIAN__
	struct network_wire_advertisement *wire = advertisement;

	(void)from_wire;
	wire_short(&wire->port);
	wire_short(&wire->version);
	wire_short(&wire->platform);
	wire_shorts(wire->game_name, NUMBEROF(wire->game_name));
	wire_map(&wire->map);
	wire_short(&wire->engine_type);
	wire_short(&wire->machine_count);
	wire_short(&wire->player_count);
	wire_short(&wire->maximum_player_count);
	wire_short(&wire->variant_setting);
	wire_short(&wire->flags);
#else
	(void)advertisement;
	(void)from_wire;
#endif

	return;
}
