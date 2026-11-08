"""The messages that turn their own scalars instead of going through the distributed netcode's wire step, on both byte orders
(tools/harness/xplay.py, tests/xplay_direct.c): voice chat's frame up and the host's settings (network_voice.c), map sharing's
request and progress (map_share.c) and the dedicated server's map vote header (network_map_vote.c), made by the game's own
functions and caught as they reach the transport. Each scalar, and the game time of each header, must be little-endian, and both
byte orders must give the committed bytes (tests/xplay_golden/direct.txt).

    tools/ppc-be/run.sh harness tools/harness/tests/test_xplay_direct.py
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import xplay  # noqa: E402

LEGS = xplay.legs()
ORDER = sorted(LEGS, key=lambda name: name != "le")
UNITS = ("source/cseries/cseries.c", "source/bungie_net/common/message_header.c", "port/linux/game/network_wire_order.c")

pytestmark = pytest.mark.skipif(not LEGS, reason="needs 32-bit x86 (-m32 or qemu-i386) or PowerPC (qemu-ppc) and clang")

NEGATIVE_CONTROLS = {
    "voice-game-time-not-turned": ("port/linux/game/network_voice.c", "\tNETWORK_WIRE(header->game_time);\n}", "}"),
    "map-share-game-time-not-turned": ("port/linux/game/map_share.c", "\tNETWORK_WIRE(header->game_time);\n}", "}"),
    "map-vote-game-time-not-turned": ("port/linux/game/network_map_vote.c", "\tNETWORK_WIRE(header->game_time);\n}", "}"),
    "voice-key-not-turned": ("port/linux/game/network_voice.c", "\t\tNETWORK_WIRE(message.up.key);\n", ""),
    "map-share-progress-not-turned": ("port/linux/game/map_share.c", "\tNETWORK_WIRE(message.progress.total);\n", ""),
    "map-share-version-not-turned": ("port/linux/game/map_share.c", "\tNETWORK_WIRE(message.request.version);\n", ""),
}


def build(leg, mutations=()):
    return xplay.build("xplay_direct", leg, UNITS, (), mutations)


@pytest.mark.parametrize("leg", ORDER)
def test_direct_bytes(leg):
    status, output, errors = xplay.run(build(leg), leg)
    assert status == 0, errors
    xplay.check_golden("direct", output, leg)


@pytest.mark.skipif("be" not in LEGS, reason="needs the big-endian leg")
@pytest.mark.parametrize("name", sorted(NEGATIVE_CONTROLS))
def test_negative_control(name):
    path, before, after = NEGATIVE_CONTROLS[name]
    status, output, errors = xplay.run(build("be", ((path, before, after),)), "be")
    assert status != 0 and "FAIL" in errors, f"{name} was not caught"
