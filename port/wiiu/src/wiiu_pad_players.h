/*
WIIU_PAD_PLAYERS.H

Which player each Pro Controller and Classic Controller is, as plain C that the desktop tests (tools/harness/tests/test_wiiu_pad_players.py):
the map from the controllers to the game's players, which keeps a controller's player while it goes and comes back; the layouts
(input.wiiu_controller_layout), "auto" among them, which decides when the controllers share the GamePad's player and when each is a
player of its own; and the number each controller's lights show. wiiu_input.c keeps one and tells it what the system says.

The game's players here are its gamepad ports, 0 to 3: input_xbox.c opens them, player_control.c reads the port of a player's local
player index, a network game makes the controller of port n local player n (network_client_manager.c) and a campaign's player 1 is the
port that chose the profile (menu_functions.c, main.c's create_local_players). Port 0 is the GamePad's, always.
*/

#ifndef HALO_WIIU_PAD_PLAYERS_H
#define HALO_WIIU_PAD_PLAYERS_H

#include "wiiu_input.h"

/* the KPAD channels read: the system's four (KPADGetMaxControllers), which with the GamePad are as many players as the game has */
#define WIIU_PAD_CHANNELS 4
/* the game's local players, its gamepad ports (port 0 the GamePad's) */
#define WIIU_PAD_PLAYER_COUNT 4
/* a controller's Bluetooth address (WPADGetAddress), which names it whatever channel it has */
#define WIIU_PAD_ADDRESS_SIZE 6

enum
{
	/* "merged", as before the map: the controller of channel n is port n's, so that channel 0's shares the GamePad's player */
	WIIU_PAD_LAYOUT_MERGED,
	/* "separate": the GamePad is player 1 alone, and each controller is its player in the map (2 to 4); a fourth has none */
	WIIU_PAD_LAYOUT_SEPARATE,
	/* "auto": every controller shares the GamePad's player while this machine has one player, and each is its player in the map once a
	second joins (wiiu_pad_players_arrange); a fourth shares the GamePad's either way */
	WIIU_PAD_LAYOUT_AUTO,
};

struct wiiu_pad_players
{
	struct
	{
		/* a controller the game reads (a Pro or Classic Controller) is on the channel */
		int present;
		/* its address, all zero where the system gave none */
		unsigned char address[WIIU_PAD_ADDRESS_SIZE];
		/* its player in the map: 1 to 3, or 0 when the three were taken (it shares the GamePad's) */
		int player;
	} channels[WIIU_PAD_CHANNELS];
	/* each player's last controller, so that one that comes back has its player again */
	struct
	{
		int known;
		unsigned char address[WIIU_PAD_ADDRESS_SIZE];
	} remembered[WIIU_PAD_PLAYER_COUNT];
	/* the layout the ports follow, and auto's choice: the controllers players of their own (1) or the GamePad's (0) */
	int layout;
	int separate;
	/* 0 until the first arrangement, which is taken at once */
	int arranged;
};

/* Nothing connected, nothing remembered. */
void wiiu_pad_players_reset(struct wiiu_pad_players *players);

/* A controller the game reads is on the channel, with its address (all zero if the system gave none). It has the player it last had if no
other controller has taken it since, else the lowest player no controller has (taking it over from one that is away), else 0: the three
are taken and it shares the GamePad's, until it connects again. The same controller again on its channel keeps its player; one with its
address on another channel has left that one. Returns its player. */
int wiiu_pad_players_connect(struct wiiu_pad_players *players, int channel, const unsigned char address[WIIU_PAD_ADDRESS_SIZE]);

/* The channel's controller has gone (or is one the game does not read, as a Wii Remote whose Classic Controller was taken out); its
player remembers it. */
void wiiu_pad_players_disconnect(struct wiiu_pad_players *players, int channel);

/* The ports the controllers have for the layout, and for "auto" whether this machine has two or more players or is adding one
(several_players: the menus' pc_menu_split_players). A change of ports is taken only while every controller is at rest
(wiiu_pad_players_at_rest), so that a button held across it is not pressed again on the other port, as xinput_sdl.c's lone gamepad does;
the first arrangement is taken at once. Returns 1 if a controller's port changed. */
int wiiu_pad_players_arrange(struct wiiu_pad_players *players, int layout, int several_players, int at_rest);

/* The port (the game's player) the channel's controller drives now, 0 to 3; -1 for none (no controller, or a fourth in "separate"). */
int wiiu_pad_players_port(const struct wiiu_pad_players *players, int channel);

/* The player number the channel's controller's lights show: its port's, 1 to 4; 0 for none. */
int wiiu_pad_players_lights(const struct wiiu_pad_players *players, int channel);

/* Whether a controller's state is at rest: no button down, both sticks inside the game's dead range (about a quarter of their throw). */
int wiiu_pad_players_at_rest(const struct wiiu_pad *pad);

#endif
