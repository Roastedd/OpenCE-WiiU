"""A remote player's input as the host relays it (network_distributed.c's relayed actions message): the host's update
first, then each player's entry, on the wire little-endian as every build sends it (protocol 41), and taken by a
client in its own order. The update is turned with the message (distributed_wire_entries) and taken as it is
(distributed_handle_actions): a big-endian client that turned it again took another number for it, so that most
updates looked older than the last it had and the host's players there aimed and ran as an old update had them.

Run big-endian as well (the faults of byte order show only there):

    HALO_HARNESS_TARGET='--target=powerpc-linux-gnu -fsigned-char -fuse-ld=lld -no-pie' HALO_HARNESS_RUNNER=qemu-ppc \\
        python -m pytest -q tools/harness/tests/test_relayed_actions.py
"""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import (CHECK_FAILED, big_endian, build, constant, enum_with, function, inline,  # noqa: E402
                     mutated, read, run, structure)

SOURCE = "port/linux/game/network_distributed.c"
CASES = ["wire-bytes", "x86-host", "round-trip", "newer-updates"]

# a fault, whether only a big-endian machine shows it, and the cases that must catch it
NEGATIVE_CONTROLS = {
    # (the update turned with the message and again as it is taken: the first big-endian build's)
    "update-turned-twice": (("sizeof(update_number), sizeof(byte))", "sizeof(update_number), sizeof(update_number))"),
                            True, ["x86-host", "round-trip", "newer-updates"]),
    "update-sent-in-this-order": (("network_wire_values(entries, sizeof(long), sizeof(long));", "(void)entries;"),
                                  True, ["wire-bytes", "x86-host"]),
    "yaw-taken-in-wire-order": (("end, &relayed->yaw, sizeof(relayed->yaw), sizeof(relayed->yaw))",
                                 "end, &relayed->yaw, sizeof(relayed->yaw), 1)"), True, ["x86-host"]),
    "newer-input-dropped": (("update_client_relayed_actions[player_index].action = *action;", ";"), False,
                            ["newer-updates"]),
}


def checked_send_and_receive(source):
    """the host's and the client's steps the test takes as they are (relayed_actions.c's host_sends, client_takes)"""
    sends = function(source, "distributed_host_send_players")
    assert re.search(r"distributed_packer_begin\(&distributed_packer, \(short\)machine_index, "
                     r"_distributed_message_relayed_actions,\s*&distributed_host_update_number, "
                     r"sizeof\(distributed_host_update_number\)\);", sends), \
        "distributed_host_send_players no longer begins the message with the host's update: change relayed_actions.c"
    handles = function(source, "network_distributed_handle_message")
    assert re.search(r"csmemcpy\(distributed_received_copy, message, size\);\s*message = \(word const \*\)"
                     r"distributed_received_copy;\s*entries = \(byte const \*\)message \+ sizeof\(header\);\s*"
                     r"#ifdef __BIG_ENDIAN__\s*distributed_wire_message\(distributed_received_copy, size\);\s*#endif",
                     handles), \
        "network_distributed_handle_message no longer takes a message in a copy in this machine's order: change " \
        "relayed_actions.c"
    handed = "distributed_handle_actions((byte const *)entries, (byte const *)message + size, header.count);"
    assert handed in handles, \
        "network_distributed_handle_message no longer hands the relayed actions on whole: change relayed_actions.c"


def statistics(game):
    """game.h's game types' statistics and their union"""
    match = re.search(r"^struct slayer_statistics\n\{.*?^union multiplayer_statistics\n\{.*?^\};", game, re.M | re.S)
    assert match, "the game types' statistics not found in source/game/game.h"
    return match.group(0)


def generated(fault=None):
    source = read(SOURCE)
    header = read("port/linux/game/network_distributed.h")
    wire_header = read("port/linux/game/network_wire_order.h")
    wire_source = read("port/linux/game/network_wire_order.c")
    queues = read("source/game/player_queues_new.c")
    players = read("source/game/players.h")
    game = read("source/game/game.h")
    checked_send_and_receive(source)
    datagram = constant(read("source/networking/network_connection.h"), "DATAGRAM_MAXIMUM_SIZE")
    macros = re.search(r"^#define NETWORK_WIRE\(value\).*?(?=\n\n)", wire_header, re.M | re.S).group(0)
    values = re.search(r"^#ifdef __BIG_ENDIAN__\nvoid network_wire_values\(.*?^#endif", wire_header,
                       re.M | re.S).group(0)
    copy = re.search(r"^#ifdef __BIG_ENDIAN__\n/\*[^\n]*\*/\nstatic long distributed_wire_copy\[[^\n]*\n#endif", source,
                     re.M).group(0)
    relayed = re.search(r"^static struct\n\{\n\tboolean valid;\n\tlong update_number;.*?\} "
                        r"update_client_relayed_actions\[MAXIMUM_NUMBER_OF_PLAYERS\];", queues, re.M | re.S).group(0)
    types = "\n".join([
        enum_with(header, "_distributed_message_relayed_actions"),
        enum_with(read("source/game/player_queues_new.h"), "DISTRIBUTED_INPUT_HISTORY = "),
        enum_with(players, "NUMBER_OF_PLAYER_POWERUPS"),
        f"enum {{ DATAGRAM_MAXIMUM_SIZE = {datagram} }};",
        structure(header, "distributed_message_header"),
        structure(players, "player_action"),
        statistics(game), structure(game, "game_statistics"),
        structure(source, "distributed_player_input"), structure(source, "distributed_relayed_action"),
        enum_with(source, "NUMBER_OF_DISTRIBUTED_POSE_VECTORS"),
        enum_with(source, "DISTRIBUTED_RELAYED_ACTION_MINIMUM_SIZE = "),
        structure(source, "distributed_pickup"), structure(source, "distributed_player_statistics"),
        structure(source, "distributed_player_ping"), structure(source, "distributed_structure_bsp"),
        structure(source, "distributed_packer"),
    ]) + "\n"
    code = "\n".join([
        macros, values,
        "#ifdef __BIG_ENDIAN__\n" + function(wire_source, "network_wire_values") + "\n#endif",
        copy,
        *(function(source, name) for name in (
            "distributed_wire_entries", "distributed_wire_message", "distributed_wire_message_to_send",
            "distributed_fill_header", "distributed_packer_begin", "distributed_packer_flush", "distributed_packer_add",
            "distributed_put", "distributed_take", "distributed_angle_pack", "distributed_angle_unpack",
            "distributed_relayed_action_from", "distributed_relayed_action_write", "distributed_relayed_action_read")),
        inline(read("source/math/real_math.h"), "valid_real"),
        relayed,
        *(function(queues, name) for name in (
            "distributed_action_valid", "distributed_add_new_ticks", "update_client_handle_relayed_action")),
        function(source, "distributed_handle_actions"),
    ]) + "\n"
    if fault:
        code = mutated(code, *fault)
    return (("types.inc", types), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("relayed_actions", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control,case", [(control, case) for control, (_, _, cases) in NEGATIVE_CONTROLS.items()
                                          for case in cases])
def test_negative_control(control, case):
    fault, big_endian_only, _ = NEGATIVE_CONTROLS[control]
    # (the wire's order is a little-endian machine's own: a fault of byte order changes nothing there)
    if big_endian_only and not big_endian():
        pytest.skip("only a big-endian machine turns the values")
    status, output = run(build("relayed_actions", generated(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"
