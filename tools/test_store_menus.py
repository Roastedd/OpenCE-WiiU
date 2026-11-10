"""The mod store's screen (tools/port_settings.py's _mod_store and the Mods page's STORE button, port/linux/game/menu_store.inl): the XML on disk is what the generator writes;
every widget, bitmap and font it names exists and every name is the file's own once; every function and data input it runs is in menu_tags.c's tables and has a handler (and
every handler is run); the columns are as wide as the XML says and hold the texts the rows put in them, measured in the menus' fonts; the Mods page's bar of buttons has room
for STORE; the numbers modstore.h and menu_store.inl share are the same; every text the screen says is ASCII 0x20 to 0x7E without "|".

    python3 -m pytest -q tools/test_store_menus.py        (needs Pillow, as tools/test_social_menus.py)
"""
import re
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import port_settings  # noqa: E402
import test_social_menus as social  # noqa: E402

CE = ROOT / "port/assets/menus/ce"
MODS_FILE = CE / (port_settings.MODS.replace("/", ".") + ".xml")
INL = (ROOT / "port/linux/game/menu_store.inl").read_text()
SCREENS_H = (ROOT / "port/linux/game/screens.h").read_text()
SCREENS_C = (ROOT / "port/linux/game/screens.c").read_text()
HEADER = (ROOT / "port/linux/src/modstore.h").read_text()
STORE = port_settings.STORE


def store_widgets():
    root = ElementTree.parse(MODS_FILE).getroot()
    return root, {element.get("name"): element for element in root if element.tag == "widget"}


def test_file_is_what_the_generator_writes():
    lines = port_settings.multiplayer_files()[port_settings.MODS.replace("/", ".") + ".xml"]
    assert MODS_FILE.read_text() == "\n".join(lines), "main_menu.mods.xml is not what tools/port_settings.py writes: tools/ce_menus.py --settings-only"
    assert f'"ce/{MODS_FILE.name}"' in (ROOT / "port/assets/menus/menus.json").read_text()


def test_every_reference_resolves_and_names_are_unique():
    widgets, bitmaps, strings = social.all_widgets()
    names = [element.get("name") for element in ElementTree.parse(MODS_FILE).getroot() if element.tag == "widget"]
    assert len(names) == len(set(names)), "a widget is named twice"
    checked = 0
    for element in ElementTree.parse(MODS_FILE).getroot().iter():
        for attribute in ("widget", "open", "replace", "focus", "description"):
            target = element.get(attribute)
            if target and "\\" not in target:
                assert target in widgets, f"{element.tag} {attribute}={target} is not a widget"
                checked += 1
        bitmap = element.get("bitmap")
        if bitmap and "\\" not in bitmap:
            assert bitmap in bitmaps, f"bitmap {bitmap} is not defined"
    assert checked > 40


def run_names():
    ran, fed = set(), set()
    for element in ElementTree.parse(MODS_FILE).getroot().iter():
        run = element.get("run")
        if run and re.match(r"port (mods store|store )", run):
            ran.add(run)
        if element.tag == "data" and element.get("input", "").startswith("port store"):
            fed.add(element.get("input"))
    return ran, fed


def test_every_function_is_in_the_tables_and_has_a_handler_and_is_run():
    tags = (ROOT / "port/linux/game/menu_tags.c").read_text()
    functions = set(re.findall(r'"([^"]+)"', tags[tags.index("port_function_names[] ="):tags.index("port_game_data_input_names[] =")]))
    inputs = set(re.findall(r'"([^"]+)"', tags[tags.index("port_game_data_input_names[] ="):tags.index("static struct", tags.index("port_game_data_input_names[] ="))]))
    ran, fed = run_names()
    assert len(ran) == 8 and fed == {"port store update"}
    for name in ran:
        assert name in functions, f"{name} is not in menu_tags.c's port_function_names (the loader would refuse the file)"
        assert f'"{name}"' in INL, f"{name} has no handler in menu_store.inl"
    assert "port store update" in inputs and '"port store update"' in INL
    # (every handler the file has is run by the XML)
    for name in re.findall(r'"(port (?:mods store|store [a-z]+))"', INL):
        assert name in ran or name == "port store update", f"{name} has a handler and nothing runs it"
    # (the tables' new names are last, after the social screens', so that no other function has moved)
    tail = tags[tags.index('"port lobby row"'):tags.index("port_game_data_input_names[] =")]
    assert tail.index('"port mods store"') > tail.index('"port lobby bot drop"')


def test_columns_are_as_wide_as_the_xml_and_the_code_say():
    widgets, _, _ = social.all_widgets()
    constants = {name: int(value) for name, value in re.findall(r"SCREENS_(STORE_[A-Z_]+_WIDTH) = (\d+)", SCREENS_H)}
    for column, x, width, font in port_settings.STORE_COLUMNS:
        widget = widgets[f"{STORE}/store_cell_{column}"]
        assert int(widget.get("width")) == width
        assert widget.get("font") == ("ui\\large_ui" if font == "large" else "ui\\small_ui")
        assert constants[f"STORE_{column.upper()}_WIDTH"] == width, f"screens.h's SCREENS_STORE_{column.upper()}_WIDTH is not the XML's"
    columns = port_settings.STORE_COLUMNS
    # (the spacing system: the last column ends 16 before the row's end, the text starts 16 in, the columns are 4 apart or more, and the cells are the social rows')
    assert columns[-1][1] + columns[-1][2] == port_settings.SOCIAL_ROW_WIDTH - 16, "the last column does not end 16 before the row's end"
    assert columns[0][1] == 12
    for (_, x0, w0, _), (_, x1, _, _) in zip(columns, columns[1:]):
        assert x1 - (x0 + w0) >= 4, "a column is under 4 from the next"
    assert int(re.search(r"#define STORE_ROWS (\d+)", INL).group(1)) == port_settings.STORE_ROWS
    assert len([name for name in widgets if re.fullmatch(rf"{STORE}/store_item_\d+", name)]) == port_settings.STORE_ROWS


def test_columns_hold_what_the_rows_put_in_them():
    small = social.advance_table("Overpass-750.ttf", 10)
    inset = int(re.search(r"SCREENS_STORE_INSET = (\d+)", SCREENS_H).group(1))
    by_name = {column: width for column, _, width, _ in port_settings.STORE_COLUMNS}
    # the status words, with the longest percent, and the sizes (a mod is at most 128 MB: "128 MB", and "9.9 MB" below ten, "999 KB" below a thousand KB)
    for word in ("DOWNLOADING 100%", "UPDATING 100%", "NOT PLAYABLE", "INSTALLED", "YOUR OWN", "DOWNLOAD", "REMOVING", "UPDATE"):
        assert small(word) <= by_name["status"] - inset, word
    for word in ("128 MB", "9.9 MB", "999 KB", "10 KB"):
        assert small(word) <= by_name["size"] - inset, word
    # (the words in the code are the ones tested: screens.c's screens_store_cells)
    words = set(re.findall(r'"((?:DOWNLOADING|UPDATING|NOT PLAYABLE|INSTALLED|YOUR OWN|DOWNLOAD|REMOVING|UPDATE)[A-Z %d]*)"', SCREENS_C))
    assert {"INSTALLED", "YOUR OWN", "NOT PLAYABLE", "DOWNLOAD", "REMOVING", "UPDATE"} <= words


def test_the_mods_pages_bar_has_room_for_store():
    _, widgets = store_widgets()
    bar = widgets[f"{port_settings.MODS}/button_bar"]
    placed = []
    for child in bar.findall("child"):
        name = child.get("widget")
        width = int(widgets[name].get("width")) if name in widgets else 128
        placed.append((int(child.get("x", 0)), width, name))
    placed.sort()
    assert [name.rsplit("/", 1)[-1] for _, _, name in placed] == ["button_up", "button_down", "button_store", "button_ok", "common_button_cancel"]
    for (x0, w0, _), (x1, _, _) in zip(placed, placed[1:]):
        assert x0 + w0 <= x1, "two buttons of the Mods page overlap"
    assert placed[-1][0] + placed[-1][1] <= 640
    store = widgets[f"{port_settings.MODS}/button_store"]
    assert store.get("text") == "STORE" and any(on.get("run") == "port mods store" and on.get("open") == f"{STORE}/store_screen" for on in store.findall("on"))
    screen = widgets[f"{port_settings.MODS}/mods_screen"]
    assert any(on.get("event") == "y" and on.get("run") == "port mods store" for on in screen.findall("on"))


def test_the_store_screens_buttons_and_slots():
    _, widgets = store_widgets()
    bar = widgets[f"{STORE}/store_button_bar"]
    slots = {}
    for child in bar.findall("child"):
        slots.setdefault(int(child.get("x")), []).append(child.get("widget").rsplit("/", 1)[-1])
    assert slots == {120: ["store_button_get", "store_button_update", "store_button_stop"], 250: ["store_button_remove"], 380: ["store_button_refresh"],
                     510: ["store_button_back"]}
    for x in slots:
        assert x + 128 <= 640
    screen = widgets[f"{STORE}/store_screen"]
    events = {on.get("event"): on for on in screen.findall("on")}
    assert events["created"].get("run") == "port store open" and events["deleted"].get("run") == "port store close"
    assert events["b"].get("back") == "true" and events["back"].get("back") == "true"
    assert events["x"].get("run") == "port store remove" and events["y"].get("run") == "port store refresh"
    # (every row's A downloads or updates; the list's data is the update)
    for index in range(1, port_settings.STORE_ROWS + 1):
        row = widgets[f"{STORE}/store_item_{index}"]
        assert {on.get("run") for on in row.findall("on") if on.get("event") in ("a", "start")} == {"port store row"}


def test_the_numbers_modstore_h_and_the_code_share_are_the_same():
    phases = re.findall(r"^\t_modstore_(off|idle|loading|ready|failed),", HEADER, re.M)
    assert phases == ["off", "idle", "loading", "ready", "failed"]
    assert re.findall(r"^\t_store_phase_(\w+),", INL, re.M) == phases
    states = re.findall(r"^\t_modstore_mod_(\w+),", HEADER, re.M)
    assert re.findall(r"^\t_store_mod_(\w+),", INL, re.M) == states
    jobs = re.findall(r"^\t_modstore_job_(\w+),", HEADER, re.M)
    assert re.findall(r"^\t_store_job_(\w+),", INL, re.M) == jobs
    fields = re.findall(r"^\t_modstore_field_(\w+),", HEADER, re.M)
    assert fields[:2] == ["id", "name"]
    # (the code's field numbers start at name = 1: id = 0 is not used by the screen)
    assert re.findall(r"^\t_store_field_(\w+)(?: = 1)?,", INL, re.M) == [name for name in fields[1:] if name != "fields"]
    # (the prototypes the code declares are modstore.h's)
    for prototype in re.findall(r"^(?:int|long|void|unsigned long) (modstore_\w+)\(", INL, re.M):
        assert re.search(rf"\b{prototype}\(", HEADER), prototype


def test_the_texts_are_plain_ascii():
    text = MODS_FILE.read_text()
    for value in re.findall(r'\btext="([^"]*)"', text):
        assert all(0x20 <= ord(character) <= 0x7E for character in value) and "|" not in value, value
    # (and what the store says, in its own file: every message is built from printable ASCII; the store makes it upper case)
    for value in re.findall(r'say\("((?:[^"\\]|\\.)*)"', (ROOT / "port/linux/src/modstore.c").read_text()):
        assert all(0x20 <= ord(character) <= 0x7E for character in value) and "|" not in value, value


def test_the_screens_empty_words_fit_their_box():
    """the words of a store that is off, loading, failed or empty, and the longest message of the store's, in the box (3 lines of 430 units and 2 lines of 496, large font)"""
    large = social.advance_table("Overpass-900.ttf", 13)
    messages = [
        "NO MOD STORE IS SET UP. ITS ADDRESS AND KEY GO IN CONFIG.TOML.",
        "THE CATALOG IS NOT SIGNED BY THE STORE'S KEY, SO NOTHING WAS TAKEN FROM IT. PRESS Y TO TRY AGAIN.",
        "COULD NOT REACH THE STORE (THE ANSWER FROM 127.0.0.1 BROKE OFF, OR IS LARGER THAN 1048576 BYTES). PRESS Y TO TRY AGAIN.",
        "THE CATALOG IS OLDER THAN ONE SEEN BEFORE, SO IT WAS NOT USED. PRESS Y TO TRY AGAIN.",
    ]

    def lines_needed(message, width):
        lines, current = 1, ""
        for word in message.split(" "):
            trial = (current + " " + word).strip()
            if large(trial) > width and current:
                lines, current = lines + 1, word
            else:
                current = trial
        return lines

    for message in messages:
        assert lines_needed(message, 430) <= 3, message
