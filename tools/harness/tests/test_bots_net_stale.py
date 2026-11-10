"""tools/harness/bots_net_stale.py: the staleness table and the actions table on canned logs of a host and a client."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import bots_net_stale as stale  # noqa: E402


def tick_line(tick, positions):
    return f"halo-linux: network test: tick {tick} " + " ".join(f"player {p}: ({x} {y} 0.1) h1.00/1.00" for p, (x, y) in positions.items()) + " | items 1"


def relay_line(update, player, now, yaw=0.5, flags=(0, 0, 0, 0)):
    return (f"halo-linux: bots: net relay update {update} player {player} now {now} yaw {yaw:.4f} pitch 0.0000 throttle 1.000 0.000 trigger 0.00 flags "
            + " ".join(f"{f:x}" for f in flags) + " weapon -1 grenade -1 zoom -1")


def act_line(tick, player, yaw=0.5, flags=0):
    return f"halo-linux: bots: net act tick {tick} player {player} yaw {yaw:.4f} pitch 0.0000 throttle 1.000 0.000 trigger 0.00 flags {flags:x} weapon -1 grenade -1 zoom -1"


def test_error_by_the_age_of_the_word():
    # (player 1 is a bot: fresh word at tick 30, the copy 0.5 off; at tick 600 the latest word is update 40, 560 old, the copy 12 off; a human, 9 old, 0.2 off)
    host = "\n".join([tick_line(30, {1: (10.0, 0.0), 0: (5.0, 5.0)}), tick_line(600, {1: (20.0, 0.0), 0: (6.0, 6.0)})])
    client = "\n".join([relay_line(25, 1, 30), relay_line(25, 0, 30), relay_line(40, 1, 100), relay_line(591, 0, 595),
                        tick_line(30, {1: (10.5, 0.0), 0: (5.2, 5.0)}), tick_line(600, {1: (32.0, 0.0), 0: (6.2, 6.0)})])
    table = stale.staleness_table(host, client)
    assert table["bot"]["0-10"][0] == 1 and abs(table["bot"]["0-10"][1] - 0.5) < 1e-9
    assert table["bot"]["240+"][0] == 1 and abs(table["bot"]["240+"][1] - 12.0) < 1e-9
    assert abs(table["human 0"]["0-10"][1] - 0.2) < 1e-9


def test_actions_relayed_equal_and_pulses():
    host = "\n".join([act_line(1, 1), act_line(2, 1, flags=0x2), act_line(3, 1), act_line(4, 1, yaw=1.0)])
    # (the client got updates 1, 3 and 4 of player 1: 4 with the buttons of the tick before it, 3, 2 and 1: the jump of tick 2 in its third place; 4's yaw is wrong)
    client = "\n".join([relay_line(1, 1, 5), relay_line(3, 1, 7), relay_line(4, 1, 8, yaw=2.0, flags=(0, 0, 0x2, 0))])
    table = stale.actions_table(host, client)[1]
    assert table["built"] == 4 and table["relayed"] == 3 and table["equal"] == 2
    # (the jump, bit 1, built at tick 2, was in update 4's history at its index 2)
    assert table["pulses"] == {1: (1, 1)}
