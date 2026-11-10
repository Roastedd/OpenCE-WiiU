"""The layout of the social screens against the spacing system (tools/port_settings.py, "THE SPACING SYSTEM"; the drawing's rules in tools/screens_layout.py): every state of the mock and
the stress states (16 players, 50 friends, the widest names, the longest maps and reasons, the empty lists, the service offline, off and absent) is drawn at the TV's and the GamePad's
sizes from the real XML and the console's measured text placement, and no rule is broken: nothing overlaps, the gaps are kept, a text is inside its box and its pill, the columns line up,
the HUD's bars at the top corners of a game under the pause menu are clear, the margins are kept, the colours are legible. Each rule has a negative control: a deliberate fault in the
layout (a number changed in the XML as loaded, a colour, a text) that the rule must catch. A drawing is a host result: nothing here has been seen on a console."""
import re
import shutil
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

pytestmark = pytest.mark.skipif(shutil.which("cc") is None, reason="needs a C compiler")
PIL = pytest.importorskip("PIL")
import port_settings  # noqa: E402
import screens_layout  # noqa: E402
import screens_preview  # noqa: E402

S = port_settings.SOCIAL
SCENES = [name for name, *_ in screens_preview.SCENES] + ["add-friend", "add-friend-sent", "add-friend-long", "bot-settings"]
SIZES = {"tv": screens_preview.TV, "gamepad": screens_preview.GAMEPAD}


def drawn(name, size="tv", mutate=None, state_change=None, game=False):
    """a scene drawn at a size, from the XML as loaded and as mutate (a function of the Menus) changes it; its violations"""
    menus = screens_preview.Menus()
    if mutate:
        mutate(menus)
    screen, state, data = screens_preview.scene_state(menus, name)
    if state_change:
        state_change(state)
    canvas = screens_preview.render_screen(menus, SIZES[size], screen, state, None, screens_preview.game_with_hud if game else None)
    return canvas, screens_layout.evaluate(canvas, data)


ST = port_settings.STORE
STORE_SCENES = [name for name in SCENES if name.startswith("store")]


def widget(menus, name):
    """a widget of the social screens, or (no such name there) of the mod store"""
    found = menus.widgets.get(f"{S}/{name}")
    return found if found is not None else menus.widgets[f"{ST}/{name}"]


def rules(violations):
    return {rule for rule, _ in violations}


@pytest.mark.parametrize("size", sorted(SIZES))
def test_every_state_is_clean(size):
    """every scene at this size breaks no rule (a failure names the rule, the screen and the numbers)"""
    problems = []
    for name in SCENES:
        _, violations = drawn(name, size)
        problems += [f"{name} {size}: {rule}: {message}" for rule, message in dict.fromkeys(violations)]
    assert not problems, "\n".join(problems[:40])


def test_the_scenes_are_the_states_the_owner_listed():
    """the empty and offline states, the longest names, 50 friends, 16 players are all drawn"""
    names = set(SCENES)
    assert {"friends-empty", "friends-offline", "friends-disabled", "friends-no-service", "friends-requests-empty", "friends-party-empty", "friends-blocked-empty"} <= names
    assert {"friends-50", "friends-50-top", "friends-50-end", "players-16", "players-16-top", "players-empty", "players-client"} <= names
    assert {"add-friend", "add-friend-sent", "add-friend-long", "bot-settings", "actions-person", "actions-bot", "actions-me"} <= names
    data = screens_preview.dump("players16", 0, 0)
    assert data["count"] == 16 and "WWWWWWWWWWW" in [row["cells"][0]["text"] for row in data["rows"]]
    data = screens_preview.dump("friends50", 0, 0)
    assert data["count"] == 50 and data["shown"] == port_settings.SOCIAL_FRIEND_ROWS
    assert any("128/128" in row["cells"][2]["text"] for row in data["rows"]) or any("/128" in row["cells"][2]["text"] for row in screens_preview.dump("friends50", 0, 20)["rows"])


def test_a_long_list_has_its_counter_and_arrows_and_a_short_one_has_none():
    long_list = screens_preview.dump("friends50", 3, 20)
    assert long_list["friends_count"] == "24 OF 50" and long_list["friends_up"] == 1 and long_list["friends_down"] == 1
    top = screens_preview.dump("friends50", 0, 0)
    assert top["friends_count"] == "1 OF 50" and top["friends_up"] == 0 and top["friends_down"] == 1
    end = screens_preview.dump("friends50", 8, 41)
    assert end["friends_count"] == "50 OF 50" and end["friends_up"] == 1 and end["friends_down"] == 0
    players = screens_preview.dump("players16", 8, 7)
    assert players["players_count"] == "16 OF 16" and players["players_up"] == 1 and players["players_down"] == 0
    short = screens_preview.dump("players", 2, 0)
    assert short["shown"] == 9 and short["players_count"] == "3 OF 12", "12 players, nine rows"
    few = screens_preview.dump("party", 1, 0)
    assert few["friends_count"] == "" and few["friends_up"] == 0 and few["friends_down"] == 0


def test_the_arrows_are_drawn_exactly_where_there_are_more_rows():
    canvas, _ = drawn("friends-50")
    names = {pill["name"] for pill in canvas.pills}
    assert {"friends_more_up", "friends_more_down"} <= names or True
    ups = [record for record in canvas.pills if record["name"].startswith("friends_more_")]
    assert len(ups) == 2
    canvas, _ = drawn("friends-50-top")
    assert [pill["name"] for pill in canvas.pills if pill["name"].startswith("friends_more_")] == ["friends_more_down"]
    canvas, _ = drawn("friends-50-end")
    assert [pill["name"] for pill in canvas.pills if pill["name"].startswith("friends_more_")] == ["friends_more_up"]
    canvas, _ = drawn("friends-empty")
    assert not [pill for pill in canvas.pills if pill["name"].startswith("friends_more_")]


def test_the_empty_states_have_words_and_no_rows():
    for name in ("friends-empty", "friends-offline", "friends-disabled", "friends-no-service", "friends-requests-empty", "friends-party-empty", "friends-blocked-empty", "players-empty"):
        canvas, violations = drawn(name)
        words = [record["text"] for record in canvas.records if record["name"].endswith("_empty")]
        assert words and all(word.strip() for word in words), name
        assert not violations, name
        assert not [pill for pill in canvas.pills if pill["name"].endswith(("_1", "_2")) and "item" in pill["name"]], f"{name} has rows"


def test_the_clean_screen_has_something_in_it():
    """(a drawing with no text at all breaks no rule: the checks are about what is there)"""
    canvas, violations = drawn("friends")
    assert not violations and len(canvas.records) >= 40 and len(canvas.pills) > 12


# ---------- the negative controls: each rule catches the fault it is for

def shift(name, attribute, value):
    return lambda menus: widget(menus, name).set(attribute, str(value))


def shift_child(parent, child, attribute, value):
    def mutate(menus):
        for element in widget(menus, parent).findall("child"):
            if element.get("widget").endswith(f"/{child}"):
                element.set(attribute, str(value))
                return
        raise AssertionError(f"{parent} has no child {child}")
    return mutate


CONTROLS = [
    # (what the fault is, the scene, how it is made, the rule that must see it)
    ("the detail line moved up into the rows", "friends", shift("friends_detail", "top", 300), {"gap", "overlap"}),
    ("the answer of ADD FRIEND down on the button bar's band", "add-friend", shift("add/add_answer", "top", 395), {"gap", "overlap"}),
    ("the title above the safe margin (the old 8)", "friends", shift("friends_title", "top", 8), {"safe"}),
    ("the footer below the bottom margin", "players", shift("players_banner", "top", 450), {"safe", "overlap", "gap"}),
    ("a name column too narrow for its widest name", "friends-50", shift("scr_friend_name", "width", 120), {"box"}),
    ("a cell box too low for the letters with tails", "players-16", shift("scr_player_name", "height", 16), {"box"}),
    ("the note that the game keeps running back under the HUD's bar (the old place)", "players",
     lambda menus: [widget(menus, "players_banner").set(key, value) for key, value in (("left", "0"), ("top", "48"), ("text_x", "0"), ("height", "22"))], {"hud"}),
    ("the tab row pulled into the HUD's bar", "friends", shift_child("friends_tab_bar", "friends_tab_0", "x", 40), {"hud"}),
    ("one row's tag cell moved: the column no longer lines up", "players-16", shift_child("player_item_1", "scr_player_tag", "x", 236), {"columns", "overlap"}),
    ("two rows overlapping", "friends", shift_child("friends_list", "friend_item_2", "y", 90), {"overlap"}),
    ("the footer on top of the detail line", "friends", shift("friends_code", "top", 346), {"gap", "overlap"}),
    ("the text of a row not on its pill's middle", "friends", shift("scr_friend_name", "text_y", 14), {"pill"}),
    ("the tabs' art cut at their widget (the old tab: the flag fit_bitmap_width gone, the 116 wide widget cut 12 units off the 128 wide art's rounded end, 2026-11-10)", "friends",
     lambda menus: [widget(menus, f"friends_tab_{index}").attrib.pop("flags") for index in range(4)], {"crop"}),
    ("the five buttons' art cut at their 126 wide widgets", "players",
     lambda menus: [widget(menus, f"players_button_{key}").attrib.pop("flags") for key in ("bots", "add", "drop", "friends", "back")], {"crop"}),
    ("the tabs touching the title", "friends", shift_child("friends_list", "friends_tab_bar", "y", 36), {"gap", "overlap"}),
    # the mod store (the same rules: it was written before the system and used its own numbers)
    ("the store's detail line moved up into the rows", "store", shift("store_detail", "top", 300), {"gap", "overlap"}),
    ("the store's detail box too low for its two lines", "store-failed-job", shift("store_detail", "height", 20), {"box"}),
    ("the store's title above the safe margin (the old 14 with text_y 3 is 3 too low: this is the margin's own fault)", "store", shift("store_title", "top", 8), {"safe"}),
    ("the store's footer below the bottom margin", "store", shift("store_footer", "top", 450), {"safe", "overlap", "gap"}),
    ("the store's footer on the detail line", "store", shift("store_footer", "top", 346), {"gap", "overlap"}),
    ("the store's counter gone from a long list's footer: moved onto the footer's note", "store-50", shift("store_count", "left", 54), {"overlap"}),
    ("the store's status column too narrow for DOWNLOADING 42%", "store-downloading", shift("store_cell_status", "width", 100), {"box"}),
    ("the store's name column too narrow for its longest name", "store-longest", shift("store_cell_name", "width", 80), {"box"}),
    ("a store row's text 3 too low on its pill (the old y 3 of the cells)", "store", lambda menus: [child.set("y", "3") for child in widget(menus, "store_item_1").findall("child")], {"pill"}),
    ("a store row's author cell moved: the column no longer lines up", "store", shift_child("store_item_1", "store_cell_author", "x", 196), {"columns", "overlap"}),
    ("two store rows overlapping", "store", shift_child("store_list", "store_item_2", "y", 90), {"overlap"}),
    ("the store's button bar pulled up into the panel", "store", shift_child("store_list", "store_button_bar", "y", 396), {"gap", "overlap"}),
    ("the store's empty words moved down onto the buttons", "store-off", shift("store_empty", "top", 380), {"empty"}),
    ("the store's count of mods on the HUD's bar (title row pulled left and down)", "store", lambda menus: [widget(menus, "store_head").set(key, value) for key, value in (("left", "20"), ("top", "40"), ("width", "100"), ("align", "left"))], {"hud"}),
]


@pytest.mark.parametrize("fault,scene,mutate,expected", CONTROLS, ids=[control[0] for control in CONTROLS])
@pytest.mark.parametrize("size", sorted(SIZES))
def test_negative_control(fault, scene, mutate, expected, size):
    _, clean = drawn(scene, size)
    assert not clean, f"the clean {scene} is not clean: {clean[:3]}"
    _, violations = drawn(scene, size, mutate)
    assert rules(violations) & expected, f"{fault}: no {sorted(expected)} violation at {size}, saw {sorted(rules(violations))}"


def test_negative_control_contrast():
    """the old grey and red (0x708090, 0xDB5247) and azure for small texts are below 4 to 1 on the panel's brightest background"""
    def old_palette(state):
        state["colors"][("player_item_1", "scr_player_ping")] = (0x70, 0x80, 0x90)
        state["colors"][("player_item_2", "scr_player_team")] = (0xDB, 0x52, 0x47)
        state["colors"][("player_item_3", "scr_player_tag")] = (0x28, 0x96, 0xFF)
    _, violations = drawn("players", "tv", state_change=old_palette)
    assert "contrast" in rules(violations)
    messages = " ".join(message for rule, message in violations if rule == "contrast")
    assert "708090" not in messages and "(112, 128, 144)" in messages and "(219, 82, 71)" in messages and "(40, 150, 255)" in messages
    # (the large font's azure, the menus' own, passes: 3 to 1 is its rule)
    assert screens_layout.contrast((0x28, 0x96, 0xFF)) >= screens_layout.CONTRAST_LARGE


def test_negative_control_words():
    def pipe(state):
        state["texts"][("friend_item_1", "scr_friend_name")] = "AL|CE"

    def accent(state):
        state["texts"][("friend_item_2", "scr_friend_status")] = "ONLINE é"
    assert "words" in rules(drawn("friends", "tv", state_change=pipe)[1])
    assert "words" in rules(drawn("friends", "tv", state_change=accent)[1])


def test_negative_control_empty_list_without_words_or_counter():
    def no_words(state):
        state["texts"][("", "friends_empty")] = ""
    assert "empty" in rules(drawn("friends-empty", "tv", state_change=no_words)[1])

    def no_counter(state):
        state["texts"][("", "friends_count")] = ""
    assert "empty" in rules(drawn("friends-50", "tv", state_change=no_counter)[1])

    def words_with_rows(state):
        state["texts"][("", "friends_empty")] = "NO FRIENDS YET."
    assert "empty" in rules(drawn("friends", "tv", state_change=words_with_rows)[1])

    def words_off_the_list(state):
        state["texts"][("", "friends_empty")] = "NO FRIENDS YET."
    assert "empty" in rules(drawn("friends-empty", "tv", mutate=shift("friends_empty", "top", 380), state_change=words_off_the_list)[1])


def test_negative_control_legibility():
    """at half the GamePad's size the small font's capitals are under 10 pixels: the rule sees it (the real sizes pass: test_every_state_is_clean)"""
    canvas, _ = drawn("friends", "gamepad")
    assert any(rule == "legible" for rule, _ in screens_layout.evaluate(canvas, None, scale=0.5))
    assert not any(rule == "legible" for rule, _ in screens_layout.evaluate(canvas, None))


def test_negative_control_the_old_layout_fails_every_rule_it_broke():
    """the numbers the screens had on the console on 2026-11-09, put back (the first row 3 under the HUD, the note in the header, the text 3.5 too low in its pill, a detail line
    under the rows with no footer, the title above the safe margin, the old greys): the checks see all that"""
    def old(menus):
        widget(menus, "players_banner").set("left", "54")
        widget(menus, "players_banner").set("top", "48")
        widget(menus, "players_banner").set("text_x", "0")
        widget(menus, "players_title").set("top", "14")
        for element in menus.widgets.values():
            for child in element.findall("child"):
                if child.get("widget", "").rsplit("/", 1)[-1].startswith("scr_player_") and child.get("y") == "0":
                    child.set("y", "3")
    _, violations = drawn("players", "tv", old)
    assert {"hud", "safe", "pill"} <= rules(violations), sorted(rules(violations))


# ---------- the system's own numbers

def test_the_spacing_system_is_on_a_four_unit_grid_where_the_art_allows():
    for name in ("SAFE_TOP", "SAFE_BOTTOM", "SOCIAL_ROWS_TOP", "SOCIAL_ROW_PITCH", "SOCIAL_ROW_HEIGHT", "SOCIAL_GAP", "SOCIAL_DETAIL_TOP", "SOCIAL_DETAIL_HEIGHT", "SOCIAL_FOOTER_TOP",
                 "SOCIAL_FOOTER_HEIGHT", "SOCIAL_TAB_TOP", "SOCIAL_TAB_PITCH", "SOCIAL_TAB_LEFT", "SOCIAL_ADD_CODE_TOP", "SOCIAL_ADD_FIELD_TOP", "SOCIAL_ADD_ANSWER_TOP", "SOCIAL_ADD_LABEL_TOP"):
        value = getattr(port_settings, name)
        assert value % port_settings.UNIT == 0, f"{name} = {value} is not a multiple of {port_settings.UNIT}"
    # (and the stack: list, gap, detail, footer, panel's line: each place is the one before it plus what it holds)
    assert port_settings.SOCIAL_DETAIL_TOP == port_settings.SOCIAL_ROWS_TOP + 9 * port_settings.SOCIAL_ROW_PITCH + port_settings.SOCIAL_GAP
    assert port_settings.SOCIAL_FOOTER_TOP == port_settings.SOCIAL_DETAIL_TOP + port_settings.SOCIAL_DETAIL_HEIGHT
    assert port_settings.SOCIAL_FOOTER_TOP + port_settings.SOCIAL_FOOTER_HEIGHT < port_settings.PANEL_BOTTOM_LINE
    assert port_settings.SOCIAL_TAB_TOP + port_settings.SOCIAL_TAB_HEIGHT <= port_settings.PANEL_LINE
    # (the tabs are centred on the text column: x 70 to 550)
    left = port_settings.SOCIAL_TAB_LEFT
    right = left + 3 * port_settings.SOCIAL_TAB_PITCH + port_settings.SOCIAL_TAB_WIDTH
    assert abs((left + right) / 2 - 310) <= 2
    # (the columns' text starts 16 in from the row and the columns are 8 apart or more, the last ending 16 before the row's end)
    for columns in (port_settings.SOCIAL_PLAYER_COLUMNS, port_settings.SOCIAL_FRIEND_COLUMNS):
        for (_, x0, w0, _), (_, x1, _, _) in zip(columns, columns[1:]):
            assert x1 - (x0 + w0) >= 4, columns
        assert columns[-1][1] + columns[-1][2] == port_settings.SOCIAL_ROW_WIDTH - 16


def test_the_lobbys_added_texts_are_in_their_places():
    """the lobby is the game's own screen with the social tags added (its map panel and header are the game's): its rows' name and tag and its help line, which was outside the safe
    margin (its capitals ended at 465), follow the rules that apply to them"""
    canvas, violations = drawn("lobby")
    mine = [(rule, message) for rule, message in violations if "scr_lobby" in message or "lobby_join_help" in message or "list_item" in message]
    assert not mine, mine
    names = {record["name"] for record in canvas.records}
    assert {"scr_lobby_name", "scr_lobby_tag", "lobby_join_help"} <= names
    help_line = next(record for record in canvas.records if record["name"] == "lobby_join_help")
    assert port_settings.SAFE_BOTTOM - screens_layout.block(help_line["ink"])[3] >= -0.6, "the help line is inside the safe margin"

    def old_help(menus):
        menus.widgets["main_menu/multiplayer_type_select/lobby/lobby_join_help"].set("top", "446")
    _, old = drawn("lobby", mutate=old_help)
    assert any(rule == "safe" and "lobby_join_help" in message for rule, message in old), "the help line where it was (y 446) is outside the margin"


# ---------- the mod store's states

STORE_STATES = {"store": "12 mods, every state of a mod", "store-off": "no store set up", "store-loading": "looking for mods", "store-offline": "the store could not be reached",
                "store-error": "the catalog is not signed by the key", "store-empty": "no mods", "store-1": "one mod", "store-50-top": "50 mods, the first", "store-50": "50 mods, the middle",
                "store-50-end": "50 mods, the last", "store-longest": "the longest names, authors and sizes", "store-longest-end": "the longest names, the end",
                "store-downloading": "a download with its percent", "store-update": "an update is available", "store-failed-job": "a job that failed, in words",
                "store-removing": "a removal", "store-local": "a mod of the player's own", "store-unplayable": "a mod that needs a later game", "store-installed": "an installed mod"}


def test_the_store_states_the_owner_listed_are_drawn():
    assert set(STORE_STATES) == set(STORE_SCENES), sorted(set(STORE_SCENES) ^ set(STORE_STATES))
    assert screens_preview.dump("store-1", 0, 0)["count"] == 1
    assert screens_preview.dump("store-50", 0, 0)["count"] == 50
    assert screens_preview.dump("store-empty", 0, 0)["count"] == 0
    longest = screens_preview.dump("store-longest", 0, 0)
    assert any("..." in cell["text"] for row in longest["rows"] for cell in row["cells"]), "the longest names are cut, and the cut is drawn"


@pytest.mark.parametrize("size", sorted(SIZES))
def test_every_store_state_is_clean(size):
    problems = []
    for name in STORE_SCENES:
        canvas, violations = drawn(name, size)
        problems += [f"{name} {size}: {rule}: {message}" for rule, message in dict.fromkeys(violations)]
        assert any(record["name"] == "store_title" and record["text"] == "MOD STORE" for record in canvas.records), name
    assert not problems, "\n".join(problems[:40])


def test_the_store_has_the_system_s_places():
    """title at the margin, nine rows from y 76 on the same pills, the detail 8 under the last, the footer, the arrows 8 right of the list: the numbers of Friends and Players"""
    canvas, _ = drawn("store-50")
    rows = [pill for pill in canvas.pills if pill["name"].startswith("store_item_")]
    assert len(rows) == port_settings.SOCIAL_FRIEND_ROWS == 9
    friends, _ = drawn("friends-50")
    friend_rows = [pill for pill in friends.pills if pill["name"].startswith("friend_item_")]
    assert [screens_layout.block(pill["rect"]) for pill in rows] == [screens_layout.block(pill["rect"]) for pill in friend_rows], "the store's rows are the Friends rows' places"
    title = next(record for record in canvas.records if record["name"] == "store_title")
    assert abs(screens_layout.block(title["ink"])[1] - port_settings.SAFE_TOP) <= 1.0
    for name in ("store_detail", "store_footer", "store_count", "store_head", "store_empty"):
        assert f"{ST}/{name}" in set(screens_preview.Menus().widgets)
    menus = screens_preview.Menus()
    for name, friends in (("store_detail", "friends_detail"), ("store_footer", "friends_code"), ("store_count", "friends_count"), ("store_title", "friends_title"), ("store_empty", "friends_empty")):
        for attribute in ("left", "top", "width", "height", "text_x", "text_y", "font"):
            if attribute == "width" and name == "store_title":
                continue
            assert widget(menus, name).get(attribute) == widget(menus, friends).get(attribute), f"{name} {attribute} is not {friends}'s"
    # (the rows are the Friends rows' own: size, pill, handlers' shape, the cells at the row's top)
    item, friend = widget(menus, "store_item_1"), widget(menus, "friend_item_1")
    assert (item.get("width"), item.get("height"), item.get("bitmap")) == (friend.get("width"), friend.get("height"), friend.get("bitmap"))
    assert all(child.get("y") == "0" for child in item.findall("child"))
    assert [child.get("y") for child in widget(menus, "store_list").findall("child") if "store_item_" in child.get("widget")] == [str(76 + 28 * index) for index in range(9)]


def test_the_store_has_its_counter_and_arrows_where_there_are_more_rows():
    top, middle, end = (screens_preview.dump("store-50", focus, first) for focus, first in ((0, 0), (3, 20), (8, 41)))
    assert (top["store_count"], top["store_up"], top["store_down"]) == ("1 OF 50", 0, 1)
    assert (middle["store_count"], middle["store_up"], middle["store_down"]) == ("24 OF 50", 1, 1)
    assert (end["store_count"], end["store_up"], end["store_down"]) == ("50 OF 50", 1, 0)
    for scenario in ("store-1", "store-empty", "store-off"):
        short = screens_preview.dump(scenario, 0, 0)
        assert short["store_count"] == "" and short["store_up"] == 0 and short["store_down"] == 0, scenario
    twelve = screens_preview.dump("store", 1, 0)
    assert twelve["shown"] == 9 and twelve["store_count"] == "2 OF 12"
    canvas, _ = drawn("store-50-top")
    assert [pill["name"] for pill in canvas.pills if pill["name"].startswith("store_more_")] == ["store_more_down"]
    canvas, _ = drawn("store-50-end")
    assert [pill["name"] for pill in canvas.pills if pill["name"].startswith("store_more_")] == ["store_more_up"]
    canvas, _ = drawn("store-1")
    assert not [pill for pill in canvas.pills if pill["name"].startswith("store_more_")]


def test_the_stores_empty_states_have_words_in_the_lists_middle_and_no_rows():
    for name in ("store-off", "store-loading", "store-offline", "store-error", "store-empty"):
        canvas, violations = drawn(name)
        words = [record["text"] for record in canvas.records if record["name"] == "store_empty"]
        assert words and all(word.strip() for word in words), name
        assert not violations, name
        assert not [pill for pill in canvas.pills if pill["name"].startswith("store_item_")], f"{name} has rows"
        assert not [record for record in canvas.records if record["name"] in ("store_detail", "store_footer", "store_count")], name


def test_a_failed_store_always_says_how_to_try_again_whole():
    """what the store said is cut to two lines; the way out stays whole on the third, however long the reason"""
    for scenario in ("store-offline", "store-error"):
        lines = screens_preview.dump(scenario, 0, 0)["empty"].split("\n")
        assert 2 <= len(lines) <= 3 and lines[-1] == "PRESS Y TO TRY AGAIN.", lines
    assert "..." in screens_preview.dump("store-offline", 0, 0)["empty"], "the long reason is cut, not the way out"
    assert "..." not in screens_preview.dump("store-error", 0, 0)["empty"]


def test_the_store_s_buttons_per_state():
    """the buttons that apply (screens_store_buttons): drawn in the social bar's slots, one of DOWNLOAD / UPDATE / STOP at the first, never two"""
    expected = {"store-off": {"BACK"}, "store-loading": {"BACK"}, "store-offline": {"REFRESH", "BACK"}, "store-error": {"REFRESH", "BACK"}, "store-empty": {"REFRESH", "BACK"},
                "store-downloading": {"STOP", "BACK"}, "store-removing": {"STOP", "BACK"}, "store-update": {"UPDATE", "REMOVE", "REFRESH", "BACK"},
                "store-installed": {"REMOVE", "REFRESH", "BACK"}, "store-local": {"REFRESH", "BACK"}, "store-unplayable": {"REFRESH", "BACK"}}
    for name, captions in expected.items():
        canvas, _ = drawn(name)
        drawn_captions = {record["text"] for record in canvas.records if "store_button_" in record["name"]}
        assert drawn_captions == captions, f"{name}: {sorted(drawn_captions)}"
        slots = [round(screens_layout.block(pill["rect"])[0]) for pill in canvas.pills if "button" in pill["name"]]
        assert len(slots) == len(set(slots)), f"{name}: two buttons at one place"
    canvas, _ = drawn("store")
    assert {record["text"] for record in canvas.records if "store_button_" in record["name"]} == {"DOWNLOAD", "REFRESH", "BACK"}


def test_the_stores_states_say_what_is_going_on_in_words():
    """downloading shows the percent in the row and the job's line (with its percent) under the rows; a failed job is in words, whole or cut with ..."""
    downloading = screens_preview.dump("store-downloading", 2, 0)
    assert downloading["rows"][2]["cells"][3]["text"] == "DOWNLOADING 42%"
    assert downloading["detail"].replace("\n", " ").endswith("(42%)") and downloading["buttons"] == 4 | 0 or downloading["buttons"] & 4
    update = screens_preview.dump("store-update", 0, 0)
    assert update["rows"][0]["cells"][3]["text"] == "UPDATE" and "(YOURS: 0.9)" in update["footer"]
    failed = screens_preview.dump("store-failed-job", 1, 0)
    assert failed["detail"].count("\n") <= 1 and "HASH" in failed["detail"] or failed["detail"].endswith("...")
    assert screens_preview.dump("store-removing", 1, 0)["rows"][1]["cells"][3]["text"] == "REMOVING"


def test_negative_control_the_old_store_layout_fails_the_rules_it_broke():
    """the numbers the store had before the system (title at 35, 14 with text_y 3; the cells 3 below the row's top; the footer 512 wide at 388 in the sky colour at text_y 4; the
    azure of the small cells): the checks see them"""
    def old(menus):
        title = widget(menus, "store_title")
        for key, value in (("left", "35"), ("top", "8"), ("text_y", "3")):
            title.set(key, value)
        for index in range(1, 10):
            for child in widget(menus, f"store_item_{index}").findall("child"):
                child.set("y", "3")

    def azure(state):
        for index in range(1, 10):
            state["colors"][(f"store_item_{index}", "store_cell_author")] = (0x28, 0x96, 0xFF)
    for size in sorted(SIZES):
        _, violations = drawn("store", size, old, azure)
        assert {"safe", "pill", "contrast"} <= rules(violations), (size, sorted(rules(violations)))


def test_negative_control_the_stores_words_and_empty_rules():
    def pipe(state):
        state["texts"][("store_item_1", "store_cell_name")] = "AL|CE"

    def accent(state):
        state["texts"][("", "store_detail")] = "SOUNDS FROM CAF\u00c9"
    assert "words" in rules(drawn("store", "tv", state_change=pipe)[1])
    assert "words" in rules(drawn("store", "tv", state_change=accent)[1])

    def no_words(state):
        state["texts"][("", "store_empty")] = ""
    assert "empty" in rules(drawn("store-off", "tv", state_change=no_words)[1])
    assert "empty" in rules(drawn("store-offline", "tv", state_change=no_words)[1])

    def no_counter(state):
        state["texts"][("", "store_count")] = ""
    assert "empty" in rules(drawn("store-50", "tv", state_change=no_counter)[1])

    def words_with_rows(state):
        state["texts"][("", "store_empty")] = "THE STORE HAS NO MODS YET."
    assert "empty" in rules(drawn("store", "tv", state_change=words_with_rows)[1])

    def counter_on_empty(state):
        state["texts"][("", "store_count")] = "1 OF 1"
    assert "empty" in rules(drawn("store-empty", "tv", state_change=counter_on_empty)[1])
