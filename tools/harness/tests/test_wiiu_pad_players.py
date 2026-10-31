"""The Wii U's controllers and players (port/wiiu/src/wiiu_pad_players.c), the unit itself compiled with cc against synthetic
connections, disconnections and presses: controllers take the lowest free player in the order they connect, whatever their
channels; one that goes and comes back, on any channel, has its own player again unless another took it, also when two come
back on each other's channels before either was told gone; one told again on its channel keeps its player; a new one takes the
lowest free player even if it was remembered for one that is away (the replacement of a controller whose battery ran out);
a controller without an address is never taken for another without one; "auto" shares the GamePad's player while there is
one player and gives each its own once a second joins, only at rest; "separate" leaves a fourth controller without a player
and "merged" keeps channel n on port n; the lights show the port's number. Each fault below must fail one of the checks."""
import os
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import ROOT, mutated, read, runner

SOURCE = "port/wiiu/src/wiiu_pad_players.c"

FAULTS = {
    "kept": None,
    # (a controller that comes back is given the lowest free player, not its own)
    "no_memory": ("for (player = 1; known && !chosen && player < WIIU_PAD_PLAYER_COUNT; player++)",
                  "for (player = 1; 0 && known && !chosen && player < WIIU_PAD_PLAYER_COUNT; player++)"),
    # (a new controller takes a player another connected controller has)
    "takes_held": ("\t\tif (!player_held(players, player))\n\t\t\tchosen = player;",
                   "\t\tif (1 || !player_held(players, player))\n\t\t\tchosen = player;"),
    # (the controllers change ports with a button held, which presses it again on the new one)
    "not_at_rest": ("separate != players->separate) && at_rest))", "separate != players->separate) && (at_rest || 1)))"),
    # (auto never gives the controllers their own players)
    "never_separate": ("separate = layout == WIIU_PAD_LAYOUT_AUTO ? several_players != 0 : players->separate;",
                       "separate = layout == WIIU_PAD_LAYOUT_AUTO ? 0 : players->separate;"),
    # (separate's fourth controller drives the GamePad's player)
    "separate_shares": ("return player ? player : -1;", "return player;"),
    # (merged follows the map, not the channels)
    "merged_by_map": ("\t\treturn channel;\n", "\t\treturn players->separate ? player : 0;\n"),
    # (two controllers without an address are taken for one another)
    "unknown_matches": ("const int known = address_known(address);", "const int known = 1;"),
    # (a controller's entry on the channel it left holds its player when it is told on another first)
    "keeps_stale": ("for (other = 0; known && other < WIIU_PAD_CHANNELS; other++)",
                    "for (other = 0; 0 && known && other < WIIU_PAD_CHANNELS; other++)"),
    # (the same controller told again on its channel is given a player anew)
    "no_same_controller": ("if (!memcmp(players->channels[channel].address, address, WIIU_PAD_ADDRESS_SIZE))\n"
                           "\t\t\treturn players->channels[channel].player;",
                           "if (0)\n\t\t\treturn players->channels[channel].player;"),
}

CHECKS = r'''
#include "wiiu_pad_players.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition, code) do { if (!(condition)) { printf("check %d failed: %s\n", code, #condition); return code; } } while (0)

static const unsigned char A[6] = { 0x58, 0xbd, 0xa3, 0x01, 0x02, 0x03 }, B[6] = { 0x58, 0xbd, 0xa3, 0x01, 0x02, 0x04 },
	C[6] = { 0x58, 0xbd, 0xa3, 0x01, 0x02, 0x05 }, D[6] = { 0x58, 0xbd, 0xa3, 0x01, 0x02, 0x06 }, UNKNOWN[6] = { 0 };

int main(void)
{
	struct wiiu_pad_players p;
	struct wiiu_pad pad;
	int channel;

	wiiu_pad_players_reset(&p);
	/* the first arrangement is taken at once, whatever is held: auto, one player */
	wiiu_pad_players_arrange(&p, WIIU_PAD_LAYOUT_AUTO, 0, 0);
	CHECK(p.layout == WIIU_PAD_LAYOUT_AUTO && !p.separate, 1);
	/* three controllers take players 2, 3 and 4 (the map's 1 to 3) in the order they connect, whatever their channels */
	CHECK(wiiu_pad_players_connect(&p, 2, A) == 1, 10);
	CHECK(wiiu_pad_players_connect(&p, 0, B) == 2, 11);
	CHECK(wiiu_pad_players_connect(&p, 1, C) == 3, 12);
	/* auto with one player: every controller drives the GamePad's port, lit as player 1; an empty channel drives none */
	for (channel = 0; channel < 3; channel++)
		CHECK(wiiu_pad_players_port(&p, channel) == 0 && wiiu_pad_players_lights(&p, channel) == 1, 13);
	CHECK(wiiu_pad_players_port(&p, 3) == -1 && wiiu_pad_players_lights(&p, 3) == 0, 14);
	/* a second player joins (the menus' pc_menu_split_players) with a button held: nothing moves */
	CHECK(wiiu_pad_players_arrange(&p, WIIU_PAD_LAYOUT_AUTO, 1, 0) == 0 && wiiu_pad_players_port(&p, 2) == 0, 20);
	/* at rest: each its own port, lit with its number */
	CHECK(wiiu_pad_players_arrange(&p, WIIU_PAD_LAYOUT_AUTO, 1, 1) == 1, 21);
	CHECK(wiiu_pad_players_port(&p, 2) == 1 && wiiu_pad_players_port(&p, 0) == 2 && wiiu_pad_players_port(&p, 1) == 3, 22);
	CHECK(wiiu_pad_players_lights(&p, 2) == 2 && wiiu_pad_players_lights(&p, 0) == 3 && wiiu_pad_players_lights(&p, 1) == 4, 23);
	/* (arranged again as it is: no change) */
	CHECK(wiiu_pad_players_arrange(&p, WIIU_PAD_LAYOUT_AUTO, 1, 1) == 0, 24);
	/* player 3's controller (B, channel 0) goes, and comes back on channel 3: its own player again */
	wiiu_pad_players_disconnect(&p, 0);
	CHECK(wiiu_pad_players_port(&p, 0) == -1 && wiiu_pad_players_port(&p, 2) == 1 && wiiu_pad_players_port(&p, 1) == 3, 30);
	CHECK(wiiu_pad_players_connect(&p, 3, B) == 2 && wiiu_pad_players_port(&p, 3) == 2 && wiiu_pad_players_lights(&p, 3) == 3, 31);
	/* told again on its channel (a connection the reads did not miss): the same */
	CHECK(wiiu_pad_players_connect(&p, 3, B) == 2, 32);
	/* a fourth with the three players taken: it shares the GamePad's in auto, and has none in separate */
	CHECK(wiiu_pad_players_connect(&p, 0, D) == 0 && wiiu_pad_players_port(&p, 0) == 0 && wiiu_pad_players_lights(&p, 0) == 1, 40);
	CHECK(wiiu_pad_players_arrange(&p, WIIU_PAD_LAYOUT_SEPARATE, 1, 1) == 1, 41);
	CHECK(wiiu_pad_players_port(&p, 0) == -1 && wiiu_pad_players_lights(&p, 0) == 0, 42);
	CHECK(wiiu_pad_players_port(&p, 2) == 1 && wiiu_pad_players_port(&p, 3) == 2 && wiiu_pad_players_port(&p, 1) == 3, 43);
	/* separate does not follow the number of players */
	CHECK(wiiu_pad_players_arrange(&p, WIIU_PAD_LAYOUT_SEPARATE, 0, 1) == 0 && wiiu_pad_players_port(&p, 2) == 1, 44);
	/* merged, as before the map: channel n on port n */
	CHECK(wiiu_pad_players_arrange(&p, WIIU_PAD_LAYOUT_MERGED, 0, 1) == 1, 45);
	for (channel = 0; channel < 4; channel++)
		CHECK(wiiu_pad_players_port(&p, channel) == channel && wiiu_pad_players_lights(&p, channel) == channel + 1, 46);
	/* a change of layout waits for rest as well */
	CHECK(wiiu_pad_players_arrange(&p, WIIU_PAD_LAYOUT_AUTO, 0, 0) == 0 && wiiu_pad_players_port(&p, 3) == 3, 47);
	CHECK(wiiu_pad_players_arrange(&p, WIIU_PAD_LAYOUT_AUTO, 0, 1) == 1, 48);
	for (channel = 0; channel < 4; channel++)
		CHECK(wiiu_pad_players_port(&p, channel) == 0, 49);

	/* a controller away and a new one: the new one takes the lowest free player, the one remembered for the absent */
	wiiu_pad_players_reset(&p);
	wiiu_pad_players_arrange(&p, WIIU_PAD_LAYOUT_SEPARATE, 1, 1);
	CHECK(wiiu_pad_players_connect(&p, 0, A) == 1 && wiiu_pad_players_connect(&p, 1, B) == 2, 50);
	wiiu_pad_players_disconnect(&p, 0);
	CHECK(wiiu_pad_players_connect(&p, 2, C) == 1, 51);
	/* the absent one comes back: its player was taken, so the next free */
	CHECK(wiiu_pad_players_connect(&p, 0, A) == 3, 52);
	/* two away, back in the other order: each its own */
	wiiu_pad_players_disconnect(&p, 2);
	wiiu_pad_players_disconnect(&p, 1);
	CHECK(wiiu_pad_players_connect(&p, 1, B) == 2, 53);
	CHECK(wiiu_pad_players_connect(&p, 3, C) == 1 && wiiu_pad_players_port(&p, 3) == 1, 54);
	/* another controller on a channel the system gave a new one: the one before has gone */
	CHECK(wiiu_pad_players_connect(&p, 3, D) == 1, 55);
	CHECK(wiiu_pad_players_connect(&p, 2, C) == 0, 56);

	/* controllers without an address: never taken for one another */
	wiiu_pad_players_reset(&p);
	wiiu_pad_players_arrange(&p, WIIU_PAD_LAYOUT_SEPARATE, 1, 1);
	CHECK(wiiu_pad_players_connect(&p, 0, A) == 1 && wiiu_pad_players_connect(&p, 1, UNKNOWN) == 2, 60);
	CHECK(wiiu_pad_players_connect(&p, 2, C) == 3, 61);
	wiiu_pad_players_disconnect(&p, 1);
	wiiu_pad_players_disconnect(&p, 0);
	CHECK(wiiu_pad_players_connect(&p, 3, UNKNOWN) == 1, 62);
	/* (one without an address told again on its channel keeps its player, here not the lowest free) */
	CHECK(wiiu_pad_players_connect(&p, 0, B) == 2 && wiiu_pad_players_connect(&p, 3, UNKNOWN) == 1, 63);
	wiiu_pad_players_reset(&p);
	wiiu_pad_players_arrange(&p, WIIU_PAD_LAYOUT_SEPARATE, 1, 1);
	CHECK(wiiu_pad_players_connect(&p, 0, A) == 1 && wiiu_pad_players_connect(&p, 1, UNKNOWN) == 2, 64);
	wiiu_pad_players_disconnect(&p, 0);
	CHECK(wiiu_pad_players_connect(&p, 1, UNKNOWN) == 2, 65);

	/* two controllers back on each other's channels before a poll has said either went: told channel by channel, each has its own
	player again (the one on the other channel, which has not been told yet, has left it) */
	wiiu_pad_players_reset(&p);
	wiiu_pad_players_arrange(&p, WIIU_PAD_LAYOUT_SEPARATE, 1, 1);
	CHECK(wiiu_pad_players_connect(&p, 0, A) == 1 && wiiu_pad_players_connect(&p, 1, B) == 2, 66);
	CHECK(wiiu_pad_players_connect(&p, 0, B) == 2 && wiiu_pad_players_port(&p, 0) == 2 && wiiu_pad_players_port(&p, 1) == -1, 67);
	CHECK(wiiu_pad_players_connect(&p, 1, A) == 1 && wiiu_pad_players_port(&p, 1) == 1, 68);
	/* one controller that moves to a lower channel keeps its player, though a remembered one is free below it */
	wiiu_pad_players_reset(&p);
	wiiu_pad_players_arrange(&p, WIIU_PAD_LAYOUT_SEPARATE, 1, 1);
	CHECK(wiiu_pad_players_connect(&p, 0, A) == 1 && wiiu_pad_players_connect(&p, 1, B) == 2, 69);
	wiiu_pad_players_disconnect(&p, 0);
	CHECK(wiiu_pad_players_connect(&p, 0, B) == 2 && wiiu_pad_players_port(&p, 1) == -1, 75);

	/* at rest: no button, both sticks inside the game's dead range */
	memset(&pad, 0, sizeof(pad));
	CHECK(wiiu_pad_players_at_rest(&pad), 70);
	pad.left_x = 0.2f;
	pad.right_y = -0.25f;
	CHECK(wiiu_pad_players_at_rest(&pad), 71);
	pad.buttons = WIIU_PAD_PLUS;
	CHECK(!wiiu_pad_players_at_rest(&pad), 72);
	pad.buttons = 0;
	pad.left_y = -0.6f;
	CHECK(!wiiu_pad_players_at_rest(&pad), 73);
	pad.left_y = 0.0f;
	pad.right_x = 0.9f;
	CHECK(!wiiu_pad_players_at_rest(&pad), 74);
	/* (a channel out of range drives nothing) */
	CHECK(wiiu_pad_players_port(&p, -1) == -1 && wiiu_pad_players_port(&p, WIIU_PAD_CHANNELS) == -1, 80);
	CHECK(wiiu_pad_players_connect(&p, WIIU_PAD_CHANNELS, A) == -1, 81);
	return 0;
}
'''


def compiler():
    """cc on the build machine; with HALO_HARNESS_TARGET (tools/ppc-be/run.sh harness), clang for that machine"""
    target = os.environ.get("HALO_HARNESS_TARGET")
    return [os.environ.get("CC", "clang"), *target.split()] if target else ["cc"]


@pytest.mark.parametrize("fault", list(FAULTS))
def test_controllers_keep_their_players(tmp_path, fault):
    source = read(SOURCE)
    if FAULTS[fault]:
        source = mutated(source, *FAULTS[fault])
    (tmp_path / "wiiu_pad_players.c").write_text(source)
    (tmp_path / "checks.c").write_text(CHECKS)
    executable = tmp_path / "pad_players"
    # (a fault may leave a function or an argument unused, which is no warning to fail on)
    relaxed = ["-Wno-unused-function", "-Wno-unused-parameter"] if FAULTS[fault] else []
    subprocess.run([*compiler(), "-std=c99", "-Wall", "-Wextra", "-Werror", *relaxed, "-I", str(ROOT / "port/wiiu/src"),
                    str(tmp_path / "wiiu_pad_players.c"), str(tmp_path / "checks.c"), "-o", str(executable)], check=True)
    result = subprocess.run([*runner(), str(executable)], capture_output=True, text=True, check=False)
    if fault == "kept":
        assert result.returncode == 0, result.stdout
    else:
        assert result.returncode != 0, f"the fault {fault} passed every check"
