"""The game's own network messages on the wire, little-endian and big-endian (tools/harness/xplay.py, tests/xplay_messages.c).

The connection and join handshake (the join request, accept and reject, the player requests), the game settings messages, the
pregame, update and game over messages, 35 types in all, are made by the game's create_network_game_message from a structure with a
value of its own in every field, and decoded again into it. The game advertisement and the game settings record, which go out
raw, are checked scalar by scalar (each little-endian), the settings record for each game type whose settings its variant's union
holds. Both byte orders must give the committed golden bytes (tests/xplay_golden/messages.txt).

    tools/ppc-be/run.sh harness tools/harness/tests/test_xplay_messages.py
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import xplay  # noqa: E402

LEGS = xplay.legs()
ORDER = sorted(LEGS, key=lambda name: name != "le")
UNITS = ("source/cseries/cseries.c", "source/bungie_net/common/message_header.c", "source/memory/data_packets.c",
         "source/memory/data_packet_groups.c", "source/memory/data_encoding.c", "source/memory/byte_swapping.c",
         "source/memory/crc.c")

pytestmark = pytest.mark.skipif(not LEGS, reason="needs 32-bit x86 (-m32 or qemu-i386) or PowerPC (qemu-ppc) and clang")

# a fault in the big-endian turning, and the line it must change or fail
NEGATIVE_CONTROLS = {
    "settings-health-not-turned": ("port/linux/game/network_wire_order.c",
                                   "\twire_long(&variant->universal_variant.health);\n", ""),
    "settings-oddball-count-not-turned": ("port/linux/game/network_wire_order.c",
                                          "\t\twire_long(&variant->game_engine_variant.oddball.ball_spawn_count);\n", ""),
    "settings-race-not-turned": ("port/linux/game/network_wire_order.c",
                                 "\t\twire_long(&variant->game_engine_variant.race.team_scoring);\n", ""),
    "settings-player-colour-not-turned": ("port/linux/game/network_wire_order.c",
                                          "\t\twire_short(&game->players[index].primary_color_index);\n", ""),
    "advertisement-flags-not-turned": ("port/linux/game/network_wire_order.c", "\twire_short(&wire->flags);\n", ""),
    "advertisement-map-not-turned": ("port/linux/game/network_wire_order.c", "\twire_map(&wire->map);\n", ""),
    "packets-shorts-turned-on-big-endian": ("source/memory/byte_swapping.c", "\t\t\t*values = SWAP2(*values);\n",
                                            "\t\t\t*values = (unsigned short)((*values >> 8) | (*values << 8));\n"),
}


def build(leg, mutations=()):
    return xplay.build("xplay_messages", leg, UNITS, (), mutations)


@pytest.mark.parametrize("leg", ORDER)
def test_message_bytes(leg):
    status, output, errors = xplay.run(build(leg), leg)
    assert status == 0, errors
    xplay.check_golden("messages", output, leg)


@pytest.mark.skipif(len(LEGS) < 2, reason="needs both legs")
def test_legs_agree():
    assert xplay.run(build("le"), "le")[1] == xplay.run(build("be"), "be")[1]


@pytest.mark.skipif("be" not in LEGS, reason="needs the big-endian leg")
@pytest.mark.parametrize("name", sorted(NEGATIVE_CONTROLS))
def test_negative_control(name):
    path, before, after = NEGATIVE_CONTROLS[name]
    status, output, errors = xplay.run(build("be", ((path, before, after),)), "be")
    golden = xplay.golden_path("messages").read_text()
    assert status != 0 or output != golden, f"{name} was not caught"
