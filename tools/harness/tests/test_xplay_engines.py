"""The game type's state the host sends its clients, on both byte orders (tools/harness/xplay.py, tests/xplay_engines.c): capture the
flag, oddball, king of the hill, slayer, race and infection each have a state structure the host sends whole in the game state
message, turned to the wire's order by the game type's own function. Each is filled with a value of its own in every scalar, turned,
and expected little-endian at each scalar's offset, and turned back to what was sent; both byte orders must give the committed
bytes (tests/xplay_golden/engines.txt).

    tools/ppc-be/run.sh harness tools/harness/tests/test_xplay_engines.py
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import xplay  # noqa: E402

LEGS = xplay.legs()
ORDER = sorted(LEGS, key=lambda name: name != "le")
UNITS = ("source/cseries/cseries.c", "port/linux/game/network_wire_order.c")

pytestmark = pytest.mark.skipif(not LEGS, reason="needs 32-bit x86 (-m32 or qemu-i386) or PowerPC (qemu-ppc) and clang")

# (source, layout table, state name, state type, the function that turns it)
ENGINES = [
    ("game_engine_ctf.c", "engine_ctf", "ctf_network_state", "struct ctf_network_state", "game_engine_ctf_network_state_wire_order"),
    ("game_engine_oddball.c", "engine_oddball", "oddball_network_state", "struct oddball_network_state",
     "game_engine_oddball_network_state_wire_order"),
    ("game_engine_slayer.c", "engine_slayer", "slayer_network_state", "struct slayer_network_state",
     "game_engine_slayer_network_state_wire_order"),
    ("game_engine_race.c", "engine_race", "race_network_state", "struct race_network_state",
     "game_engine_race_network_state_wire_order"),
    ("game_engine_king.c", "engine_king", "king_globals", "struct king_globals", "game_engine_king_network_state_wire_order"),
    ("infection.c", "infection", "distributed_infection_state", "struct distributed_infection_state", "infection_state_wire_order"),
]

NEGATIVE_CONTROLS = {
    "ctf-swap-timer-not-turned": ("source/game/game_engine_ctf.c", "\tNETWORK_WIRE(state->flag_swap_timer);\n", ""),
    "oddball-scores-not-turned": ("source/game/game_engine_oddball.c", "\tNETWORK_WIRE_ARRAY(state->individual_score);\n", ""),
    "slayer-speeds-not-turned": ("source/game/game_engine_slayer.c", "\tNETWORK_WIRE_ARRAY(state->speeds);\n", ""),
    "race-laps-not-turned": ("source/game/game_engine_race.c", "\tNETWORK_WIRE_ARRAY(state->laps_completed);\n", ""),
    "king-hill-points-not-turned": ("source/game/game_engine_king.c", "\tNETWORK_WIRE_PARTS(state->hill_points, real);\n", ""),
    "infection-phase-not-turned": ("port/linux/game/infection.c", "\tNETWORK_WIRE(state->phase);\n", ""),
}


def build(leg, engine, mutations=()):
    source, table, state, state_type, function = engine
    defines = (f'-DXPLAY_ENGINE_SOURCE="{source}"', f'-DXPLAY_ENGINE_INC="xplay_layouts_{table}.inc"',
               f"-DXPLAY_STRUCTURES=xplay_structures_{table}", f'-DXPLAY_STATE="{state}"', f"-DXPLAY_STATE_TYPE={state_type}",
               f"-DXPLAY_WIRE_ORDER={function}")
    return xplay.build("xplay_engines", leg, UNITS, defines, mutations)


def run_all(leg, mutations=()):
    outputs, errors = "", ""
    status = 0
    for engine in ENGINES:
        try:
            code, output, error = xplay.run(build(leg, engine, mutations), leg)
        except LookupError:
            # (the mutation is of another engine's file)
            continue
        status |= code
        outputs += output
        errors += error
    return status, outputs, errors


@pytest.mark.parametrize("leg", ORDER)
def test_engine_state_bytes(leg):
    status, output, errors = run_all(leg)
    assert status == 0, errors
    xplay.check_golden("engines", output, leg)


@pytest.mark.skipif("be" not in LEGS, reason="needs the big-endian leg")
@pytest.mark.parametrize("name", sorted(NEGATIVE_CONTROLS))
def test_negative_control(name):
    path, before, after = NEGATIVE_CONTROLS[name]
    status, output, errors = run_all("be", ((path, before, after),))
    assert status != 0 and "FAIL" in errors, f"{name} was not caught"
