"""The description pane of the Multiplayer page names every button the list can focus: the owner's picture of the console (2026-11-10) showed <missing string> with FRIENDS focused, because
the bar of buttons at the bottom is one item of the list, whose number (7) was past the seven descriptions the PC version has. The engine (ui_widget_game_data_input_functions.c,
multiplayer_type_menu_update_extended_description) now adds a button's place in the bar to that number, and the port's strings have a description for FRIENDS and one for BACK."""
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
pytest.importorskip("PIL")
pytest.importorskip("numpy")
import menus_clip_check  # noqa: E402
import port_settings  # noqa: E402

LIST = "main_menu/multiplayer_type_select/multiplayer_type_select_list"
STRINGS = "main_menu/multiplayer_type_select/multiplayer_option_descriptions"


def can_handle(model, element):
    return bool(element.findall("on")) or element.get("type") in ("column_list", "spinner") or bool(element.findall("child"))


def description_numbers(model, list_name):
    """the string number the engine shows for each thing the list can focus, as the engine counts: the items that take events before it, plus a button's place in its bar"""
    list_element = model.widgets[list_name]
    numbers, index = {}, 0
    for child in list_element.findall("child"):
        element = model.widgets[child.get("widget")]
        if not can_handle(model, element):
            continue
        if element.get("type") == "column_list":
            for place, button in enumerate(element.findall("child")):
                numbers[button.get("widget")] = index + place
        else:
            numbers[child.get("widget")] = index
        index += 1
    return numbers


def test_every_button_of_the_multiplayer_page_has_its_description():
    model = menus_clip_check.Model()
    strings = model.strings[STRINGS]
    numbers = description_numbers(model, LIST)
    friends = "main_menu/multiplayer_type_select/screens/menu_friends_button"
    assert friends in numbers and "common_button_back" in numbers, sorted(numbers)
    for name, number in numbers.items():
        assert number < len(strings), f"{name}: description number {number} but the strings have {len(strings)}: <missing string>"
        assert strings[number].strip(), name
    assert "friends" in strings[numbers[friends]].lower()
    assert "back" in strings[numbers["common_button_back"]].lower()


def test_the_descriptions_are_the_generators_and_fit_the_pane():
    model = menus_clip_check.Model()
    assert model.strings[STRINGS] == port_settings.STRING_OVERRIDES[STRINGS]
    assert len(port_settings.OVERRIDE_ORIGINALS[STRINGS]) == len(port_settings.STRING_OVERRIDES[STRINGS])
    pane = model.widgets["main_menu/multiplayer_type_select/multiplayer_options_txt"]
    width = float(pane.get("width"))
    for text in model.strings[STRINGS]:
        for line in text.split("\\n"):
            assert model.measure(pane.get("font", "ui\\large_ui"), line) <= width - 4.4, line


def test_negative_control_a_button_with_no_description():
    """the list as it was: the bar's FRIENDS is the eighth item, and seven descriptions leave it with none"""
    model = menus_clip_check.Model()
    strings = model.strings[STRINGS][:7]
    numbers = description_numbers(model, LIST)
    assert max(numbers.values()) >= len(strings)
