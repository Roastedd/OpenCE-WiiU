"""What a map download (map_share.c) does for the machine that makes it: it fails before it writes a byte if the card
has no room for the files, goes on when the room cannot be read, and asks again while it is being read; a part file on
a card that cannot be written past its end has the gap before a piece written with zeros; the Wii U fetches from the
host alone over the internet; and a machine whose Custom Edition maps or map downloads are off is told why a map was
not asked for."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["room-short", "room-enough", "room-exact", "room-unknown", "room-being-read", "room-no-offer-yet", "extend",
         "sources", "refusal"]

# a fault in map_share.c, the function it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "no-spare": (("download->space_needed - SPARE_BYTES + 0xFFFFF", "download->space_needed + 0xFFFFF"),
                 "download_space_check", "room-short"),
    "room-is-enough-with-the-spare-short": (("free_space_bytes < download->space_needed",
                                             "free_space_bytes + SPARE_BYTES < download->space_needed"),
                                            "download_space_check", "room-exact"),
    "unknown-room-fails": (("if (free_space_state != 2)\n\t{", "if (free_space_state != 2)\n\t{\n\t\tdownload_fail(\"x\");"),
                           "download_space_check", "room-unknown"),
    "checked-every-time": (("if (download->space_checked || !download->space_needed)\n\t\treturn TRUE;",
                            "if (!download->space_needed)\n\t\treturn TRUE;"), "download_space_check", "room-enough"),
    "does-not-wait": (("if (free_space_state == 1 && !elapsed(free_space_asked_at, now, FREE_SPACE_TIMEOUT))\n\t\treturn TRUE;", ""),
                      "download_space_check", "room-being-read"),
    "no-gap-filled": (("if (offset > file->extent)", "if (offset > file->extent && FALSE)"), "download_file_extend",
                      "extend"),
    "gap-of-ones": (("fwrite(zeros, 1, bytes, file->stream) != bytes", "fwrite(\"\\xFF\", 1, 1, file->stream) != 1"),
                    "download_file_extend", "extend"),
    "peers-over-the-internet": (("return internet ? 0 : WIIU_LAN_SOURCES;", "return WIIU_LAN_SOURCES;"),
                                "download_sources_allowed", "sources"),
    "silent-when-downloads-off": (("if (!config_boolean(\"network.map_download\"))", "if (FALSE)"),
                                  "map_share_download_refusal", "refusal"),
    "host-told": (("|| map_share_host() ||", "||"), "map_share_download_refusal", "refusal"),
}

FUNCTIONS = ("elapsed", "download_space_check", "download_file_extend", "download_sources_allowed",
             "map_share_download_refusal")


def generated(fault=None, faulty_function=None):
    header = read("port/linux/game/map_share.h")
    source = read("port/linux/game/map_share.c")
    config = "#define P2P_MAP_SHARE_PORT %d\n" % constant(read("port/linux/src/p2p.h"), "P2P_MAP_SHARE_PORT")
    config += "#define P2P_PLAYER_KEY_HASH_SIZE %d\n" % constant(read("port/linux/src/p2p.h"),
                                                                 "P2P_PLAYER_KEY_HASH_SIZE")
    config += "#define HALO_PORT_MAXIMUM_NETWORK_MACHINES %d\n" % constant(
        read("port/linux/include/halo_port_limits.h"), "HALO_PORT_MAXIMUM_NETWORK_MACHINES")
    config += enum_with(header, "MAP_SHARE_HASH_BYTES") + "\n"
    config += structure(header, "map_share_file") + "\n"
    for member in ("OFFER_TIMEOUT", "_offer_ok", "_offer_in_progress_bit", "_progress_downloading",
                   "_file_awaiting_hashes"):
        config += enum_with(source, member) + "\n"
    for name in ("distributed_map_share_request", "distributed_map_share_offer", "distributed_map_share_offer_file",
                 "distributed_map_share_progress", "download_file", "source", "download", "request", "serving",
                 "connection", "host_machine", "map_share_globals"):
        config += structure(source, name) + "\n"
    text = ""
    for name in FUNCTIONS:
        code = function(source, name)
        if fault and name == faulty_function:
            code = mutated(code, *fault)
        text += code + "\n"
    return (("config.inc", config), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("map_share_download", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, faulty_function, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("map_share_download", generated(fault, faulty_function)), case)
    assert status == CHECK_FAILED, f"map_share.c with a fault ({control}) passed '{case}': the test cannot see it"
