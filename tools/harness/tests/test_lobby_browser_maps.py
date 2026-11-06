"""What the server browser makes of the map a listing names (menu_functions.c's lobby_browser_map_state): a listing
names it without its folder, so the games' own maps are installed, a Custom Edition map this machine has is found
under custom_maps\\ its name, and any other is downloaded as the game is joined if map downloads are on and Custom
Edition maps can run."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["stock", "installed-custom-map", "downloadable", "missing"]

NEGATIVE_CONTROLS = {
    "stripped-name-not-looked-for": (("if (custom_edition_maps_display_index(level_name) != NONE)\n\t\treturn P2P_LOBBY_MAP_INSTALLED;\n\treturn config_boolean(\"network.map_download\") && halo_custom_edition_tag_cache()",
                                      "return config_boolean(\"network.map_download\") && halo_custom_edition_tag_cache()"),
                                     "installed-custom-map"),
    "downloads-ignored": (("config_boolean(\"network.map_download\") && halo_custom_edition_tag_cache() ?",
                           "halo_custom_edition_tag_cache() ?"), "missing"),
    "tag-cache-ignored": (("config_boolean(\"network.map_download\") && halo_custom_edition_tag_cache() ?",
                           "config_boolean(\"network.map_download\") ?"), "missing"),
    "stock-missing": (("if (campaign_level_of(map) != NONE)\n\t\treturn P2P_LOBBY_MAP_INSTALLED;",
                       "if (campaign_level_of(map) != NONE)\n\t\treturn P2P_LOBBY_MAP_MISSING;"), "stock"),
}


def generated(fault=None):
    header = read("port/linux/src/p2p.h")
    code = function(read("port/linux/game/menu_functions.c"), "lobby_browser_map_state")
    if fault:
        code = mutated(code, *fault)
    return (("enums.inc", enum_with(header, "P2P_LOBBY_MAP_INSTALLED")), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("lobby_browser_maps", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("lobby_browser_maps", generated(fault)), case)
    assert status == CHECK_FAILED, f"menu_functions.c with a fault ({control}) passed '{case}': the test cannot see it"
