"""The wall clock of internet play (halo_clock.h, anchored by a TLS answer's Date) and the server browser's use of it (p2p_lobby.c's listing_current): a console
whose own clock is hours off sees the listings of machines whose clocks are right, and signs its own so that they do."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure

CASES = ["unanchored", "skew-seen-without-anchor-lost", "skew-anchored-sees-listings", "skew-signing", "steady", "refused", "wrap",
         "listing-difference"]


def generated(clock_fault=None, lobby_fault=None):
    source = read("port/linux/src/p2p_lobby.c")
    clock = read("port/linux/src/halo_clock.h")
    config = ""
    for name in ("P2P_KEY_SIZE", "P2P_SIGNATURE_SIZE", "P2P_KEY_HASH_SIZE", "P2P_TOKEN_SIZE", "P2P_SEALED_TOKEN_SIZE", "P2P_LISTING_NAME_SIZE", "P2P_LISTING_MAP_SIZE",
                 "P2P_LISTING_GAMETYPE_SIZE"):
        config += f"#define {name} 32\n"
    config += re.search(r"^enum\n\{\n\tLISTING_FORMAT.*?^\};\n", source, re.M | re.S).group(0)
    code = structure(source, "listing") + "\n" + function(source, "listing_current")
    if lobby_fault:
        code = mutated(code, *lobby_fault)
    if clock_fault:
        clock = mutated(clock, *clock_fault)
    return (("config.inc", config), ("halo_clock.h", clock), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("halo_clock", generated()), case)
    assert status == 0, output


def test_p2p_lobby_uses_the_corrected_clock():
    """no wall-clock read is left in the listings' signing and window"""
    source = read("port/linux/src/p2p_lobby.c")
    assert "time(NULL)" not in source
    assert "halo_utc_now()" in function(source, "listing_current")
    assert "halo_utc_now()" in function(source, "listing_make_token")


@pytest.mark.parametrize("clock_fault,lobby_fault,case", [
    # the anchor ignored
    (("if (!clock->anchored)\n\t\treturn wall_seconds;", "if (1)\n\t\treturn wall_seconds;"), None, "skew-anchored-sees-listings"),
    # the machine's clock followed after the anchor
    (("return clock->utc + (double)elapsed / 1000.0;", "return wall_seconds;"), None, "skew-anchored-sees-listings"),
    # any sample taken, however far from the 2000 to 2100 range
    (("if (!(utc_seconds >= 946684800.0 && utc_seconds <= 4102444800.0))\n\t\treturn 0;", ""), None, "refused"),
    # the steadiness of the anchor lost
    (("if (utc_seconds - estimate <= HALO_CLOCK_STEADY_SECONDS && estimate - utc_seconds <= HALO_CLOCK_STEADY_SECONDS)\n\t\t\treturn 0;", ""), None,
     "steady"),
    # the anchor not moved forward: a run longer than 49.7 days loses it
    (("if (elapsed >= 1000u)", "if (0)"), None, "wrap"),
    # a window that is always open
    (None, ("return difference <= window && difference >= -window;", "return 1;"), "skew-seen-without-anchor-lost"),
])
def test_negative_control(clock_fault, lobby_fault, case):
    status, output = run(build("halo_clock", generated(clock_fault, lobby_fault)), case)
    assert status == CHECK_FAILED, output
