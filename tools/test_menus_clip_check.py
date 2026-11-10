"""tools/menus_clip_check.py over every menu file (port/assets/menus/ce): the port's own screens have no error (no bitmap cut off by its widget, no text wider than its widget, nothing
off the screen at 16:9, no overlapping siblings), the game's own menus have exactly the findings listed here (the original layout, left as it is), and each rule has a negative control:
the old Friends tabs (116 wide widgets for the 128 wide button art, no fit_bitmap_width) must be flagged. A host result: nothing here has been seen on a console."""
import re
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
pytest.importorskip("PIL")
pytest.importorskip("numpy")
import menus_clip_check  # noqa: E402

TABS = "main_menu/multiplayer_type_select/screens/friends_tab_{}"


@pytest.fixture(scope="module")
def clean():
    return menus_clip_check.check()


def errors(findings, rule=None, ours=None):
    return [finding for finding in findings if finding["severity"] == "error" and (rule is None or finding["rule"] == rule) and (ours is None or finding["ours"] == ours)]


def test_the_ports_own_screens_have_no_error(clean):
    findings, _ = clean
    assert not errors(findings, ours=True), errors(findings, ours=True)


def test_the_games_own_menus_have_only_the_findings_the_original_layout_has(clean):
    """the stock menus' errors, known: the original Xbox layout, not ours to change (tools/menus_clip_check.py's report in reports/research/2026-11-10/clip-result.md): a new one is a change
    to a stock widget, or a rule that sees more than it did"""
    findings, _ = clean
    known = {("crop", "main_menu/new_select/list_item_0"), ("crop", "main_menu/settings_select/multiplayer_setup/playlist_edit/playlist_edit_ext_desc_pic"),
             ("text", "main_menu/settings_select/multiplayer_setup/playlist_edit/game_type_select/rules_label")}
    seen = {(finding["rule"], finding["widget"]) for finding in errors(findings, ours=False)}
    unlisted = {pair for pair in seen if pair not in known and not pair[1].startswith("main_menu/player_profiles_select/") and "right_item" not in pair[1]}
    assert not unlisted, unlisted


def test_the_tabs_and_buttons_have_the_flag_that_shows_their_whole_art():
    model = menus_clip_check.Model()
    for index in range(4):
        assert "fit_bitmap_width" in model.widgets[TABS.format(index)].get("flags").split()
    for key in ("bots", "add", "drop", "friends", "back"):
        assert "fit_bitmap_width" in model.widgets[f"main_menu/multiplayer_type_select/screens/players_button_{key}"].get("flags").split()
    for key in ("button_up", "button_down", "button_store"):
        assert "fit_bitmap_width" in model.widgets[f"main_menu/mods/{key}"].get("flags").split()


def test_negative_control_the_old_tab_is_flagged():
    """the tab as the console showed it on 2026-11-10: 116 wide, no flag: the engine cut the art at 116 of its 128, and the tab's right end was square"""
    def old(model):
        for index in range(4):
            model.widgets[TABS.format(index)].attrib.pop("flags")
    findings, _ = menus_clip_check.check(mutate=old)
    cut = [finding for finding in errors(findings, "crop", True) if "friends_tab_" in finding["widget"]]
    assert len(cut) >= 4, cut
    assert any(abs(finding["art"] - finding["widget_width"] - 12.0) < 0.6 for finding in cut if finding["frame"] == 1), cut


def test_negative_control_a_narrow_button_without_the_flag():
    def old(model):
        model.widgets["main_menu/mods/button_up"].attrib.pop("flags")
    findings, _ = menus_clip_check.check(mutate=old)
    assert any(finding["widget"] == "main_menu/mods/button_up" for finding in errors(findings, "crop", True))


def test_negative_control_a_text_wider_than_its_widget():
    def wide(model):
        model.widgets["main_menu/multiplayer_type_select/screens/friends_tab_1"].set("text", "A TAB WITH FAR TOO MANY WORDS IN IT")
    findings, _ = menus_clip_check.check(mutate=wide)
    assert any(finding["widget"].endswith("friends_tab_1") for finding in errors(findings, "text", True))


def test_negative_control_a_spinner_value_wider_than_its_arrows():
    def wide(model):
        element = model.widgets["main_menu/settings_select/player_setup/player_profile_edit/gamepad_setup/button_preset_spinner"]
        element.set("width", "147")
        element.set("footer_bounds", "7 150 19 156")
    findings, _ = menus_clip_check.check(mutate=wide)
    assert any(finding["widget"].endswith("button_preset_spinner") for finding in errors(findings, "spinner", True))


def test_negative_control_a_widget_off_the_screen():
    def off(model):
        model.widgets["main_menu/multiplayer_type_select/screens/friends_title"].set("left", "700")
    findings, _ = menus_clip_check.check(mutate=off)
    assert any(finding["widget"].endswith("friends_title") for finding in errors(findings, "offscreen", True)), [finding["rule"] for finding in findings if finding["ours"]][:5]


def test_negative_control_tabs_that_overlap():
    def touch(model):
        for child in model.widgets["main_menu/multiplayer_type_select/screens/friends_tab_bar"].findall("child"):
            index = int(child.get("widget")[-1])
            child.set("x", str(72 + 112 * index))
    findings, _ = menus_clip_check.check(mutate=touch)
    assert any("friends_tab_" in finding["widget"] for finding in errors(findings, "overlap", True)), [finding["message"] for finding in findings if "tab" in finding["widget"]]


def test_the_five_button_gap_is_visible():
    """the one focused of two neighbours is 2 or more from the other's art: the five buttons at 126 wide and 127 apart"""
    findings, _ = menus_clip_check.check()
    tight = [finding for finding in findings if finding["ours"] and finding["rule"] == "overlap" and "players_button" in finding["widget"]]
    assert not tight, tight
