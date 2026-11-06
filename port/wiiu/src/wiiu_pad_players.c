/*
WIIU_PAD_PLAYERS.C

The controllers' players (wiiu_pad_players.h), from what wiiu_input.c says the system did: no system call here, so that the desktop
tests it (tools/harness/tests/test_wiiu_pad_players.py).

A controller is known by its Bluetooth address, not by its KPAD channel: the system gives a controller that connects the lowest free
channel, so one that comes back after another connected (or after a Wii Remote took a channel) can have another channel, and the
channel would give it another player. Its address gives it its own back, as a split screen game needs (the game shows "controller
unplugged" for its player until the port comes back).

A new controller takes the lowest player no connected controller has, even one remembered for a controller that is away: in a two
player game, a second Pro Controller picked up when the first one's battery runs out then plays as player 2 at once, where keeping
player 2 for the first would make it player 3, which has no window. The first, if it comes back, takes the next free player.
*/

#include "wiiu_pad_players.h"

#include <string.h>

/* (the game's dead range, input_xbox.c's GAMEPAD_STICK_DEAD_RANGE: 9000 of 32767; a stick inside it reads as centred) */
#define AT_REST_STICK 0.27f

static int address_known(const unsigned char address[WIIU_PAD_ADDRESS_SIZE])
{
	int index;

	for (index = 0; index < WIIU_PAD_ADDRESS_SIZE; index++)
	{
		if (address[index])
			return 1;
	}
	return 0;
}

/* whether a connected controller has the player */
static int player_held(const struct wiiu_pad_players *players, int player)
{
	int channel;

	for (channel = 0; channel < WIIU_PAD_CHANNELS; channel++)
	{
		if (players->channels[channel].present && players->channels[channel].player == player)
			return 1;
	}
	return 0;
}

void wiiu_pad_players_reset(struct wiiu_pad_players *players)
{
	memset(players, 0, sizeof(*players));
}

int wiiu_pad_players_connect(struct wiiu_pad_players *players, int channel, const unsigned char address[WIIU_PAD_ADDRESS_SIZE])
{
	const int known = address_known(address);
	int player, other, chosen = 0;

	if (channel < 0 || channel >= WIIU_PAD_CHANNELS)
		return -1;
	if (players->channels[channel].present)
	{
		/* (the same controller told again, as after a connection the reads had not missed; without an address, the channel's is taken for the same) */
		if (!memcmp(players->channels[channel].address, address, WIIU_PAD_ADDRESS_SIZE))
			return players->channels[channel].player;
		/* (another on the channel: the one before has gone) */
		wiiu_pad_players_disconnect(players, channel);
	}
	/* (and a controller is on one channel only: one with this address on another has left it, though nobody has said so yet, as when two
	controllers come back on each other's channels between two polls; left there, it would hold this controller's own player) */
	for (other = 0; known && other < WIIU_PAD_CHANNELS; other++)
	{
		if (other != channel && players->channels[other].present &&
			!memcmp(players->channels[other].address, address, WIIU_PAD_ADDRESS_SIZE))
		{
			wiiu_pad_players_disconnect(players, other);
		}
	}
	/* one that comes back: its own player, unless another controller took it while it was away */
	for (player = 1; known && !chosen && player < WIIU_PAD_PLAYER_COUNT; player++)
	{
		if (players->remembered[player].known && !memcmp(players->remembered[player].address, address, WIIU_PAD_ADDRESS_SIZE) &&
			!player_held(players, player))
		{
			chosen = player;
		}
	}
	/* a new one: the lowest player no connected controller has */
	for (player = 1; !chosen && player < WIIU_PAD_PLAYER_COUNT; player++)
	{
		if (!player_held(players, player))
			chosen = player;
	}
	players->channels[channel].present = 1;
	memcpy(players->channels[channel].address, address, WIIU_PAD_ADDRESS_SIZE);
	players->channels[channel].player = chosen;
	/* (0: the three were taken, and it shares the GamePad's player, which remembers nobody) */
	if (chosen)
	{
		players->remembered[chosen].known = known;
		memcpy(players->remembered[chosen].address, address, WIIU_PAD_ADDRESS_SIZE);
	}
	return chosen;
}

void wiiu_pad_players_disconnect(struct wiiu_pad_players *players, int channel)
{
	if (channel < 0 || channel >= WIIU_PAD_CHANNELS)
		return;
	/* (its player's memory of it stays, for when it comes back) */
	players->channels[channel].present = 0;
	players->channels[channel].player = 0;
}

int wiiu_pad_players_arrange(struct wiiu_pad_players *players, int layout, int several_players, int at_rest)
{
	int before[WIIU_PAD_CHANNELS];
	int channel, separate, changed = 0;

	for (channel = 0; channel < WIIU_PAD_CHANNELS; channel++)
		before[channel] = wiiu_pad_players_port(players, channel);
	/* auto: players of their own once this machine has a second player (or is adding one: ADD PLAYER in the lobby, co-op's player 2) */
	separate = layout == WIIU_PAD_LAYOUT_AUTO ? several_players != 0 : players->separate;
	if (!players->arranged || ((layout != players->layout || separate != players->separate) && at_rest))
	{
		players->layout = layout;
		players->separate = separate;
		players->arranged = 1;
	}
	for (channel = 0; channel < WIIU_PAD_CHANNELS; channel++)
	{
		if (wiiu_pad_players_port(players, channel) != before[channel])
			changed = 1;
	}
	return changed;
}

int wiiu_pad_players_port(const struct wiiu_pad_players *players, int channel)
{
	int player;

	if (channel < 0 || channel >= WIIU_PAD_CHANNELS || !players->channels[channel].present)
		return -1;
	player = players->channels[channel].player;
	switch (players->layout)
	{
	case WIIU_PAD_LAYOUT_SEPARATE:
		/* (the GamePad's player is the GamePad's alone: a fourth controller has none, as channel 3's had none before the map) */
		return player ? player : -1;
	case WIIU_PAD_LAYOUT_AUTO:
		/* (a fourth controller shares the GamePad's player either way: four people with Pro Controllers and the GamePad on the table) */
		return players->separate ? player : 0;
	default:
		/* merged, as before the map: the channel's own port */
		return channel;
	}
}

int wiiu_pad_players_lights(const struct wiiu_pad_players *players, int channel)
{
	return wiiu_pad_players_port(players, channel) + 1;
}

int wiiu_pad_players_at_rest(const struct wiiu_pad *pad)
{
	return !pad->buttons && !pad->xbox_faces && pad->left_x < AT_REST_STICK && pad->left_x > -AT_REST_STICK && pad->left_y < AT_REST_STICK &&
		pad->left_y > -AT_REST_STICK && pad->right_x < AT_REST_STICK && pad->right_x > -AT_REST_STICK && pad->right_y < AT_REST_STICK &&
		pad->right_y > -AT_REST_STICK;
}
