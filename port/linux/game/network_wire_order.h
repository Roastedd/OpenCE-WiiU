/*
NETWORK_WIRE_ORDER.H

The byte order of what machines send each other raw (network_wire_order.c).
*/

#ifndef __NETWORK_WIRE_ORDER_H
#define __NETWORK_WIRE_ORDER_H
#pragma once

/* ---------- headers */

#include "cseries.h"

/* ---------- macros */

/* a value, an array's values, or a structure's parts of one width (a
real_point3d's reals, a distributed_vector's shorts), in the wire's order,
little-endian: turned on a big-endian machine, as they are sent and received
(the same turn both ways); nothing on a little-endian one */
#define NETWORK_WIRE(value) network_wire_values(&(value), sizeof(value), sizeof(value))
#define NETWORK_WIRE_ARRAY(array) network_wire_values((array), sizeof(array), sizeof((array)[0]))
#define NETWORK_WIRE_PARTS(value, part_type) network_wire_values(&(value), sizeof(value), sizeof(part_type))

/* ---------- prototypes/NETWORK_WIRE_ORDER.C */

/* the size bytes at data, as values of width bytes each (1, 2, 4 or 8): each
turned between the wire's order and this machine's */
#ifdef __BIG_ENDIAN__
void network_wire_values(
	void *data,
	long size,
	long width);
#else
#define network_wire_values(data, size, width) ((void)0)
#endif


struct network_game;

/* the game settings record (network_messages.c's
message_server_game_settings_update pieces) in the wire's order, little-endian,
before it is sent (from_wire FALSE), or in this machine's once it is in
(from_wire TRUE): nothing on a little-endian machine */
void network_wire_order_game(
	struct network_game *game,
	boolean from_wire);

/* the game settings record of game_size bytes at game as the wire has it: on a
big-endian machine a copy turned to little-endian (kept until the next call),
on a little-endian one game itself */
void const *network_wire_order_game_to_send(
	void const *game,
	long game_size);

/* the same of a game advertisement (network_messages.c's
message_server_game_advertise, 0x114 bytes) */
void network_wire_order_advertisement(
	void *advertisement,
	boolean from_wire);

#endif // __NETWORK_WIRE_ORDER_H
