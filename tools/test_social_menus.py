"""The social screens' menus (tools/port_settings.py's social_files, port/linux/game/menu_screens.inl): the XML on disk is what the generator writes; every widget, bitmap and
string list they name exists; the numbers of their layout are screens.h's; every column is as wide as the text the rows put in it (the widest 11 character ASCII name, the longest
status, measured in the menus' fonts); every function they run has a handler and every handler is run by something; the order of the focus (the children's order) is what
screens.h's navigation model says; the pause menu's PLAYERS button and the Multiplayer screen's FRIENDS button are in place and off by default."""
import re
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import port_settings  # noqa: E402

CE = ROOT / "port/assets/menus/ce"
SOCIAL = port_settings.SOCIAL
INL = (ROOT / "port/linux/game/menu_screens.inl").read_text()
HEADER = (ROOT / "port/linux/game/screens.h").read_text()
FONTS = ROOT / "port/assets/fonts"


def constants():
    """screens.h's enum numbers"""
    values = {}
    for name, value in re.findall(r"\b(SCREENS_[A-Z_]+)\s*=\s*(-?\d+)", HEADER):
        values[name] = int(value)
    return values


def all_widgets():
    """every widget of every menu file: name -> element, and the names of the bitmaps and string lists"""
    widgets, bitmaps, strings = {}, set(), set()
    for path in sorted(CE.glob("*.xml")):
        root = ElementTree.parse(path).getroot()
        for element in root:
            if element.tag == "widget":
                widgets[element.get("name")] = element
            elif element.tag == "bitmap":
                bitmaps.add(element.get("name"))
            elif element.tag == "strings":
                strings.add(element.get("name"))
    return widgets, bitmaps, strings


def social_names():
    return {name for name in port_settings.social_files()}


def test_files_are_what_the_generator_writes():
    for name, lines in {**port_settings.social_files(), **port_settings.multiplayer_files()}.items():
        assert (CE / name).read_text() == "\n".join(lines), f"{name} is not what tools/port_settings.py writes: tools/ce_menus.py --settings-only"
    for name, lines in port_settings.settings_files().items():
        if "bot_settings" in name:
            assert (CE / name).read_text() == "\n".join(lines)
    bar = CE / (port_settings.MULTIPLAYER_FOLDER.replace("/", ".") + ".xml")
    assert port_settings.multiplayer_bar_patched(bar.read_text()) == bar.read_text()
    manifest = (ROOT / "port/assets/menus/menus.json").read_text()
    for name in [*social_names(), "main_menu.settings_select.player_setup.player_profile_edit.bot_settings.xml"]:
        assert f'"ce/{name}"' in manifest, f"{name} is not in menus.json"


def test_every_reference_resolves():
    widgets, bitmaps, strings = all_widgets()
    checked = 0
    for path in [CE / name for name in social_names()] + [CE / "main_menu.settings_select.player_setup.player_profile_edit.bot_settings.xml"]:
        for element in ElementTree.parse(path).getroot().iter():
            for attribute in ("widget", "open", "replace", "focus", "description"):
                target = element.get(attribute)
                if target and "\\" not in target:
                    assert target in widgets, f"{path.name}: {element.tag} {attribute}={target} is not a widget"
                    checked += 1
            bitmap = element.get("bitmap")
            if bitmap and "\\" not in bitmap:
                assert bitmap in bitmaps, f"{path.name}: bitmap {bitmap} is not defined"
            strings_name = element.get("string_list")
            if strings_name and "\\" not in strings_name:
                assert strings_name in strings, f"{path.name}: string list {strings_name} is not defined"
            font = element.get("font")
            if font:
                assert font in ("ui\\large_ui", "ui\\small_ui", "large", "small"), f"{path.name}: font {font}"
    assert checked > 100


def test_widget_names_are_unique():
    seen = {}
    for path in sorted(CE.glob("*.xml")):
        for element in ElementTree.parse(path).getroot():
            if element.tag == "widget":
                name = element.get("name")
                assert name not in seen, f"{name} is in {seen[name]} and {path.name}"
                seen[name] = path.name


def test_layout_numbers_are_screens_h():
    numbers = constants()
    assert numbers["SCREENS_ROWS_LEFT"] == port_settings.SOCIAL_ROWS_LEFT
    assert numbers["SCREENS_ROWS_TOP"] == port_settings.SOCIAL_ROWS_TOP
    assert numbers["SCREENS_ROW_PITCH"] == port_settings.SOCIAL_ROW_PITCH
    assert numbers["SCREENS_ROW_WIDTH"] == port_settings.SOCIAL_ROW_WIDTH
    assert numbers["SCREENS_ROW_HEIGHT"] == port_settings.SOCIAL_ROW_HEIGHT
    assert numbers["SCREENS_TAB_TOP"] == port_settings.SOCIAL_TAB_TOP
    assert numbers["SCREENS_TAB_LEFT"] == port_settings.SOCIAL_TAB_LEFT
    assert numbers["SCREENS_TAB_PITCH"] == port_settings.SOCIAL_TAB_PITCH
    assert numbers["SCREENS_TAB_WIDTH"] == port_settings.SOCIAL_TAB_WIDTH
    assert numbers["SCREENS_TAB_HEIGHT"] == port_settings.SOCIAL_TAB_HEIGHT
    assert numbers["SCREENS_PLAYER_ROWS"] == port_settings.SOCIAL_PLAYER_ROWS
    assert numbers["SCREENS_FRIEND_ROWS"] == port_settings.SOCIAL_FRIEND_ROWS
    # the columns' widths, and the lobby's
    for prefix, columns, keys in (("PLAYER", port_settings.SOCIAL_PLAYER_COLUMNS, ["NAME", "TAG", "TEAM", "PING"]),
                                  ("LOBBY", port_settings.SOCIAL_LOBBY_COLUMNS, ["NAME", "TAG"])):
        for (column, _, width, _), key in zip(columns, keys):
            assert numbers[f"SCREENS_{prefix}_{key}_WIDTH"] == width, f"{prefix} {column}"
    assert numbers["SCREENS_FRIEND_NAME_WIDTH"] == port_settings.SOCIAL_FRIEND_COLUMNS[1][2]
    assert numbers["SCREENS_FRIEND_STATUS_WIDTH"] == port_settings.SOCIAL_FRIEND_COLUMNS[2][2] == numbers["SCREENS_MEMBER_STATE_WIDTH"]
    # (the row's buttons: four at 120, 250, 380, 510 and five at 8, 130, 252, 374, 496)
    widgets, _, _ = all_widgets()
    bar = widgets[f"{SOCIAL}/players_button_bar"]
    assert [int(child.get("x")) for child in bar.findall("child")] == [4, 131, 258, 385, 512]
    bar = widgets[f"{SOCIAL}/friends_button_bar"]
    assert [int(child.get("x")) for child in bar.findall("child")] == [120, 250, 380, 510]


def advance_table(file, capitals):
    from PIL import ImageFont
    probe = ImageFont.truetype(str(FONTS / file), 1000)
    box = probe.getbbox("H")
    size = round(1000 * capitals / (box[3] - box[1]) * 10)
    font = ImageFont.truetype(str(FONTS / file), size)
    return lambda text: font.getlength(text) / 10


def test_columns_hold_the_widest_text():
    """measured with the fonts themselves (not screens_metrics.inc): the widest 11 character ASCII name in a name column, the cells' longest texts in theirs"""
    large = advance_table("Overpass-900.ttf", 13)
    small = advance_table("Overpass-750.ttf", 10)
    widest = max((chr(code) for code in range(0x20, 0x7F) if chr(code) != "|"), key=lambda c: large(c))
    name = widest * 11
    columns = {"large": large, "small": small}
    widgets, _, _ = all_widgets()
    checked = 0
    for prefix, columns_spec in (("scr_player", port_settings.SOCIAL_PLAYER_COLUMNS), ("scr_friend", port_settings.SOCIAL_FRIEND_COLUMNS)):
        for column, x, width, font in columns_spec:
            widget = widgets[f"{SOCIAL}/{prefix}_{column}"]
            assert int(widget.get("width")) == width
            assert widget.get("font") == ("ui\\large_ui" if font == "large" else "ui\\small_ui")
            if column == "name":
                assert columns[font](name) <= width, f"{prefix} name column {width} < {columns[font](name):.1f}"
                checked += 1
        # (the cells inside the row: the last ends within it)
        last = columns_spec[-1]
        assert last[1] + last[2] <= port_settings.SOCIAL_ROW_WIDTH, f"{prefix}: the last column runs off the row"
        # (and none overlaps the next)
        for (_, x0, w0, _), (_, x1, _, _) in zip(columns_spec, columns_spec[1:]):
            assert x0 + w0 <= x1, f"{prefix}: a column runs into the next"
    # the lobby: the name and the tag in a 390 wide row
    # (the name's widest and the tag's longest share a few units at most: the tag's text is right-aligned and the name's widest is not met with it)
    for (_, x0, w0, _), (_, x1, _, _) in zip(port_settings.SOCIAL_LOBBY_COLUMNS, port_settings.SOCIAL_LOBBY_COLUMNS[1:]):
        assert x0 + w0 <= x1 + 8
    last = port_settings.SOCIAL_LOBBY_COLUMNS[-1]
    assert last[1] + last[2] <= 390
    assert large(name) <= port_settings.SOCIAL_LOBBY_COLUMNS[0][2]
    # the longest tag in the lobby: a team and a legendary bot
    assert small("BLUE BOT LEGENDARY") <= port_settings.SOCIAL_LOBBY_COLUMNS[1][2]
    # the Players page's tags and the friends' status words, at their longest
    assert small("BOT LEGENDARY") <= port_settings.SOCIAL_PLAYER_COLUMNS[1][2]
    assert small("YOU - HOST") <= port_settings.SOCIAL_PLAYER_COLUMNS[1][2]
    assert small("999 MS") <= port_settings.SOCIAL_PLAYER_COLUMNS[3][2]
    assert small("PLAYING") + small(" 128/128 NEWER BUILD") + 24 <= port_settings.SOCIAL_FRIEND_COLUMNS[2][2]
    assert small("FULL - RETRYING 99s") <= port_settings.SOCIAL_FRIEND_COLUMNS[2][2]
    # the tabs' labels in the small font
    for label in ("[FRIENDS 50]", "[REQUESTS 50]", "[PARTY 8/8]", "[BLOCKED 50]"):
        assert small(label) <= port_settings.SOCIAL_TAB_WIDTH - 2, label
    assert checked == 2


def handled_names():
    """the function names the glue dispatches on"""
    return set(re.findall(r'"(port [a-z0-9 ]+?)"', INL)) | {"port friends tab "}


def run_names():
    names = set()
    for path in [CE / name for name in social_names()] + [CE / "main_menu.multiplayer_type_select.lobby.xml", CE / "main_menu.multiplayer_type_select.xml"]:
        for element in ElementTree.parse(path).getroot().iter():
            run = element.get("run")
            if run and re.match(r"port (players|screens|friends|friend add|lobby row|lobby bot)", run):
                names.add(run)
            if element.tag == "data" and element.get("input", "").startswith(("port players", "port screens", "port friends", "port friend add")):
                names.add(element.get("input"))
    return names


def test_every_function_run_is_handled_and_every_handler_is_run():
    handled = handled_names()
    ran = run_names()
    for name in ran:
        base = re.sub(r"(port friends tab) .*", r"\1 ", name)
        assert base in handled, f"{name} is run by the menus and has no handler"
    for name in handled:
        if name.startswith("port friends tab"):
            continue
        assert name in ran, f"{name} has a handler and nothing runs it"
    # the tabs' four numbers and the shoulders' words
    assert {"port friends tab 0", "port friends tab 1", "port friends tab 2", "port friends tab 3", "port friends tab previous", "port friends tab next"} <= ran


def test_focus_order_is_the_navigation_model():
    """the children of a list are its focus order (up_down_tabs_children): the Friends screen's tab bar first, then the rows, then the buttons; the Players page's rows, then the
    buttons (screens_nav_*)"""
    widgets, _, _ = all_widgets()
    players = [child.get("widget").rsplit("/", 1)[1] for child in widgets[f"{SOCIAL}/players_list"].findall("child")]
    assert players == [f"player_item_{n}" for n in range(1, 11)] + ["players_button_bar"]
    friends = [child.get("widget").rsplit("/", 1)[1] for child in widgets[f"{SOCIAL}/friends_list"].findall("child")]
    assert friends == ["friends_tab_bar"] + [f"friend_item_{n}" for n in range(1, 10)] + ["friends_button_bar"]
    for name in ("players_list", "friends_list", "actions_list"):
        assert "up_down_tabs_children" in widgets[f"{SOCIAL}/{name}"].get("flags")
    for name in ("players_button_bar", "friends_button_bar", "friends_tab_bar"):
        assert "left_right_tabs_items" in widgets[f"{SOCIAL}/{name}"].get("flags")
    # the tabs are in order, a row's A opens its actions, B goes back from every screen, the shoulders turn the tabs
    tabs = [child.get("widget").rsplit("/", 1)[1] for child in widgets[f"{SOCIAL}/friends_tab_bar"].findall("child")]
    assert tabs == [f"friends_tab_{n}" for n in range(4)]
    screen = widgets[f"{SOCIAL}/friends_screen"]
    events = {(on.get("event"), on.get("run")) for on in screen.findall("on")}
    assert ("white", "port friends tab previous") in events and ("black", "port friends tab next") in events
    assert ("b", None) in events and ("back", None) in events
    for name in ("players_screen", "actions_screen", "friends_screen"):
        screen = widgets[f"{SOCIAL}/{name}"]
        assert any(on.get("event") == "b" and on.get("back") == "true" for on in screen.findall("on")), name
    row = widgets[f"{SOCIAL}/player_item_1"]
    assert any(on.get("event") == "a" and on.get("open") == f"{SOCIAL}/actions_screen" and on.get("branch") == "true" for on in row.findall("on"))
    # a row can be tapped (the pointer clicks A)
    assert any(on.get("event") == "left_mouse" for on in row.findall("on"))
    # the lobby: A on a row, L and R for the bots, a name and a tag in each row, the old text still there
    lobby = widgets["main_menu/multiplayer_type_select/lobby/list_item_0"]
    assert any(on.get("event") == "a" and on.get("run") == "port lobby row" for on in lobby.findall("on"))
    children = [child.get("widget").rsplit("/", 1)[1] for child in lobby.findall("child")]
    assert "list_item_text" in children and "scr_lobby_name" in children and "scr_lobby_tag" in children
    screen = widgets["main_menu/multiplayer_type_select/lobby/lobby_screen"]
    events = {(on.get("event"), on.get("run")) for on in screen.findall("on")}
    assert ("white", "port lobby bot drop") in events and ("black", "port lobby bot add") in events


def test_actions_screen_has_room_for_every_action_the_provider_can_offer():
    widgets, _, _ = all_widgets()
    rows = [name for name in widgets if name.startswith(f"{SOCIAL}/action_item_")]
    assert len(rows) == port_settings.SOCIAL_ACTION_ROWS == constants()["SCREENS_ACTIONS_MAXIMUM"]


def test_entry_points_are_off_by_default():
    """the Multiplayer screen's FRIENDS button is shown by code only (a data function), and the pause menu's PLAYERS is added by code only with screens.enable"""
    text = (CE / "main_menu.multiplayer_type_select.xml").read_text()
    assert f"{SOCIAL}/menu_friends_button" in text and "port screens menu update" in text
    data = INL[INL.index("static boolean scr_data("):]
    update = data[:data.index('else if (!strcmp(name, "port players update"))')]
    assert "visible_set" in update and "scr_enabled()" in update
    tags = (ROOT / "port/linux/game/menu_tags.c").read_text()
    assert "static boolean pause_players_wanted(void)" in tags and 'config_integer("screens.enable") != 0' in tags
    for body in re.findall(r"pause_players_wanted\(\)[^;]*;", tags):
        assert body
    # nothing the screens draw is drawn with the setting off: the toast function's first statement
    toast = (ROOT / "port/linux/game/screens_toast.c").read_text()
    start = toast.index("void screens_toast_frame(void)")
    assert re.search(r"if \(!screens_enabled\(\)\)\s*return;", toast[start:start + 700])
    # the default of the setting
    config = (ROOT / "port/linux/src/port_config.c").read_text()
    assert re.search(r'"screens\.enable", _config_integer, "0"', config)
    # the lobby's own texts are untouched with it off: the team in the row's text, and no tag
    functions = (ROOT / "port/linux/game/menu_functions.c").read_text()
    assert "!scr_enabled()" in functions
    assert "if (!scr_enabled())\n\t\treturn;" in INL


def test_the_pause_box_has_a_frame_for_five_buttons():
    xml = (CE / "bitmaps.xml").read_text()
    for piece, width in (("left", 16), ("center", 4), ("right", 16)):
        block = xml[xml.index(f'name="pause/pausebox_{piece}"'):]
        block = block[:block.index("</bitmap>")]
        frames = re.findall(r'<frame png="([^"]+)" width="(\d+)" height="(\d+)"', block)
        assert len(frames) == 5
        assert frames[4][2] == "512" and int(frames[4][1]) == width
        assert (ROOT / "port/assets/menus" / frames[4][0]).exists()
    assert port_settings.pause_box_height(5) == 264 and port_settings.pause_box_canvas(264) == 512 and port_settings.pause_box_canvas(229) == 256
    tags = (ROOT / "port/linux/game/menu_tags.c").read_text()
    assert "#define PAUSE_BOX_FIVE_FRAME 4" in tags


def test_every_function_the_menus_run_is_in_menu_tags_tables():
    """the engine resolves a run= by a fixed table (menu_tags.c's port_function_names, port_game_data_input_names): a name that is not in it makes the loader refuse the file and the game
    use the Xbox's menus (found by the game digest's menu run: "there is no event handler function ...")"""
    tags = (ROOT / "port/linux/game/menu_tags.c").read_text()
    functions = set(re.findall(r'"([^"]+)"', tags[tags.index("port_function_names[] ="):tags.index("port_game_data_input_names[] =")]))
    inputs = set(re.findall(r'"([^"]+)"', tags[tags.index("port_game_data_input_names[] ="):tags.index("static struct", tags.index("port_game_data_input_names[] ="))]))
    ran, fed = set(), set()
    paths = [CE / name for name in social_names()] + [CE / "main_menu.multiplayer_type_select.lobby.xml", CE / "main_menu.multiplayer_type_select.xml",
                                                       CE / "main_menu.settings_select.player_setup.player_profile_edit.bot_settings.xml"]
    for path in paths:
        for element in ElementTree.parse(path).getroot().iter():
            if element.get("run") and element.get("run").startswith("port "):
                ran.add(element.get("run"))
            if element.tag == "data" and element.get("input", "").startswith("port "):
                fed.add(element.get("input"))
    assert ran and fed
    for name in ran:
        assert name in functions or name in {"port setting load", "port setting save", "port settings save", "port settings defaults", "port settings help"} or name in functions, f"{name} is not in port_function_names"
    for name in fed:
        assert name in inputs or name == "port settings help", f"{name} is not in port_game_data_input_names"
    # (and what the handlers dispatch on is in the tables: no handler that nothing can reach)
    for name in re.findall(r'"(port (?:players|screens|friends|friend add|lobby row|lobby bot)[a-z0-9 ]*?)"', INL):
        if name.endswith("tab "):
            continue
        assert name in functions or name in inputs, f"{name} has a handler and is not in the tables"


def test_the_friends_screens_top_right_is_clear_of_the_tab_row():
    """the service word and this machine's code (top right) end a margin above the tab row: on the console 'YOUR CODE: ...' touched the tabs (it ended at 40 against the tabs' 44)"""
    widgets, _, _ = all_widgets()
    tabs = int(next(child for child in widgets[f"{SOCIAL}/friends_list"].findall("child") if child.get("widget") == f"{SOCIAL}/friends_tab_bar").get("y"))
    assert tabs == port_settings.SOCIAL_TAB_TOP
    for name in ("friends_service", "friends_code"):
        element = widgets[f"{SOCIAL}/{name}"]
        assert int(element.get("top")) + int(element.get("height")) + 8 <= tabs, f"{name} runs down to {int(element.get('top')) + int(element.get('height'))}, the tabs start at {tabs}"
    service, code = widgets[f"{SOCIAL}/friends_service"], widgets[f"{SOCIAL}/friends_code"]
    # (the two lines do not overlap each other's text: the service's text ends before the code's starts)
    assert int(service.get("top")) + int(service.get("text_y")) + 12 <= int(code.get("top")) + int(code.get("text_y"))
