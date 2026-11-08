"""The distributed netcode's messages on the wire, little-endian and big-endian (tools/harness/xplay.py, tests/xplay_wire.c).

Every message the game sends from a native structure (ids 1 to 95 of network_distributed.c, the entries of the 24 that carry
structures) is made with a different value in every scalar, sent as the game sends it (distributed_send: the header, the wire
step, the host's batch or the reliable stream) and compared with the bytes it must be, each scalar little-endian; the hand-packed
unit states and relayed actions are written, read and put through their packers. The same run on 32-bit x86 and on 32-bit
big-endian PowerPC must give the bytes of the committed golden file (tests/xplay_golden/wire.txt), which is what the PC players
send and expect.

    tools/ppc-be/run.sh harness tools/harness/tests/test_xplay_wire.py       both legs, in the opence-ppc-be image
    HALO_XPLAY_UPDATE=1 ... rewrites the golden file (from the little-endian leg: check the diff, a changed byte is a wire change)
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import xplay  # noqa: E402

LEGS = xplay.legs()
ORDER = sorted(LEGS, key=lambda name: name != "le")  # (the little-endian leg first: it makes the golden file)
UNITS = ("source/cseries/cseries.c", "source/bungie_net/common/message_header.c", "port/linux/game/network_objects.c",
         "port/linux/game/network_damage.c", "port/linux/game/network_actors.c", "port/linux/game/network_coop.c",
         "port/linux/game/network_wire_order.c")

pytestmark = pytest.mark.skipif(not LEGS, reason="needs 32-bit x86 (-m32 or qemu-i386) or PowerPC (qemu-ppc) and clang")

# a fault in the wire step, and what must catch it on the big-endian leg (which turns, so the one that does not turn differs)
NEGATIVE_CONTROLS = {
    "objects-index-not-turned": ("port/linux/game/network_objects.c", "\t\t\tNETWORK_WIRE(change->object_index);\n", ""),
    "coop-device-power-not-turned": ("port/linux/game/network_coop.c", "\t\t\tNETWORK_WIRE(state->power);\n", ""),
    "damage-body-part-narrow": ("port/linux/game/network_damage.c", "\t\t\tNETWORK_WIRE(event->body_part);\n", ""),
    "statistics-deaths-not-turned": ("port/linux/game/network_distributed.c", "\t\t\tNETWORK_WIRE(statistics->deaths);\n", ""),
    "input-host-time-not-turned": ("port/linux/game/network_distributed.c", "\t\t\tNETWORK_WIRE(input->host_time);\n", ""),
    "header-game-time-not-turned": ("port/linux/game/network_distributed.c", "\tNETWORK_WIRE(header->game_time);\n\tdistributed_wire_entries",
                                    "\tdistributed_wire_entries"),
    "batch-length-not-turned": ("port/linux/game/network_distributed.c", "\tNETWORK_WIRE(wire_length);\n", ""),
    "relayed-update-not-turned": ("port/linux/game/network_distributed.c",
                                  "\t\t\tnetwork_wire_values(entries, sizeof(long), sizeof(long));\n", ""),
}


def build(leg, mutations=()):
    return xplay.build("xplay_wire", leg, UNITS, (), mutations)


@pytest.mark.parametrize("leg", ORDER)
def test_wire_bytes(leg):
    status, output, errors = xplay.run(build(leg), leg)
    assert status == 0, errors
    xplay.check_golden("wire", output, leg)


@pytest.mark.skipif(len(LEGS) < 2, reason="needs both legs")
def test_legs_agree():
    outputs = {leg: xplay.run(build(leg), leg) for leg in LEGS}
    assert outputs["le"][1] == outputs["be"][1]


@pytest.mark.skipif("be" not in LEGS, reason="needs the big-endian leg")
@pytest.mark.parametrize("name", sorted(NEGATIVE_CONTROLS))
def test_negative_control(name):
    path, before, after = NEGATIVE_CONTROLS[name]
    status, output, errors = xplay.run(build("be", ((path, before, after),)), "be")
    assert status != 0 and "FAIL" in errors, f"{name} was not caught: {errors[-500:]}"
