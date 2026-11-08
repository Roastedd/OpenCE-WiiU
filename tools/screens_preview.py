#!/usr/bin/env python3
"""Pictures of the social screens as they would be laid out on the Wii U: the TV (1920 x 1080) and the GamePad (854 x 480).

    python3 tools/screens_preview.py OUT_DIR [scenario ...]          needs Pillow, and a C compiler for tools/screens_preview/screens_dump.c

WHAT THIS IS: a layout drawing, not a screenshot of the game. The menus' own XML (port/assets/menus/ce, as tools/port_settings.py writes it) is placed the way the widget engine places it
(a child at its x and y in its parent, a widget's bitmap from its top left cut to its size, a text from its text_y down), with the menus' own PNGs (the row pills, the buttons, the
gradient) and Overpass, the fonts the game draws the menus in (port/assets/fonts/fonts.json), at the sizes their capitals have (13 and 10 units). The texts and colours of the rows are
what port/linux/game/screens.c puts in them for the mock provider (screens_dump.c prints them as JSON: the same functions menu_screens.inl calls). The game's moving background (the ring)
is a stand-in; the pause menu's box and buttons are drawn from the same numbers menu_tags.c uses (the real list is made of the map's tags); the toasts are drawn with a stand-in font.
Where the engine's own text placement differs by a pixel or two, the drawing does: the offsets were checked against the console's pictures of Network Setup
(runs/wiiu/session-20261030/menu-walk/22-network.png: rows at x 54, text 16 units below the row's top) and nothing more.

The screen is 852 x 480 units; the menus' 640 x 480 block is in the middle (106 units each side: MENUS_DRAWN_CENTERED). The TV is 2.2535 pixels a unit, the GamePad 1.0023.
"""
import json
import random
import subprocess
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
MENUS = ROOT / "port/assets/menus"
CE = MENUS / "ce"
FONTS = ROOT / "port/assets/fonts"
sys.path.insert(0, str(ROOT / "tools"))
import port_settings  # noqa: E402

UNITS_WIDE, UNITS_HIGH = 852, 480
BLOCK_LEFT = (UNITS_WIDE - 640) // 2
TV = (1920, 1080)
GAMEPAD = (854, 480)
# the em of each font tag, from its capitals' height (screens_metrics.py: capitals 13 and 10 units)
FONT_FILES = {"ui\\large_ui": ("Overpass-900.ttf", 13.0), "ui\\small_ui": ("Overpass-750.ttf", 10.0)}
AZURE = (0x28, 0x96, 0xFF)
WHITE = (255, 255, 255)


def argb(text, default=AZURE):
    if not text:
        return default
    value = int(text.lstrip("#"), 16)
    return ((value >> 16) & 255, (value >> 8) & 255, value & 255)


class Menus:
    def __init__(self):
        self.widgets, self.bitmaps, self.strings = {}, {}, {}
        for path in sorted(CE.glob("*.xml")):
            for element in ElementTree.parse(path).getroot():
                name = element.get("name")
                if element.tag == "widget":
                    self.widgets[name] = element
                elif element.tag == "bitmap":
                    self.bitmaps[name] = [(frame.get("png"), int(frame.get("width") or 0), int(frame.get("height") or 0)) for frame in element.findall("frame")]
                elif element.tag == "strings":
                    self.strings[name] = [string.get("text") for string in element.findall("string")]
        self.image_cache = {}
        self.font_cache = {}

    def font(self, tag, scale):
        key = (tag, round(scale, 4))
        if key not in self.font_cache:
            file, capitals = FONT_FILES.get(tag, FONT_FILES["ui\\large_ui"])
            probe = ImageFont.truetype(str(FONTS / file), 1000)
            box = probe.getbbox("H")
            em = 1000.0 * capitals / (box[3] - box[1])
            self.font_cache[key] = (ImageFont.truetype(str(FONTS / file), max(1, round(em * scale))), em)
        return self.font_cache[key]

    def frame(self, bitmap, index, width, height, scale):
        frames = self.bitmaps.get(bitmap)
        if not frames:
            return None
        png, frame_width, frame_height = frames[min(index, len(frames) - 1)]
        if not png:
            return None
        key = (png, round(scale, 4))
        if key not in self.image_cache:
            image = Image.open(MENUS / png).convert("RGBA")
            self.image_cache[key] = image.resize((max(1, round(frame_width * scale)), max(1, round(frame_height * scale))), Image.LANCZOS)
        return self.image_cache[key]


class Canvas:
    """a picture of the screen's 852 x 480 units, drawn at `scale` pixels a unit"""

    def __init__(self, menus, size, seed=7):
        self.menus = menus
        self.size = size
        self.scale = size[0] / UNITS_WIDE
        self.image = backdrop(size, seed)
        self.draw = ImageDraw.Draw(self.image, "RGBA")

    def paste_frame(self, bitmap, index, x, y, width, height):
        frame = self.menus.frame(bitmap, index, width, height, self.scale)
        if frame is None:
            return
        # (from its top left, cut to the widget's size)
        cut = frame.crop((0, 0, min(frame.width, round(width * self.scale)), min(frame.height, round(height * self.scale))))
        self.image.paste(cut, (round(x * self.scale), round(y * self.scale)), cut)

    def text(self, tag, x, y, width, height, text, color, align=None, text_x=0, text_y=0, alpha=255):
        if not text:
            return
        if "\n" in text or "\r" in text:
            # (the engine's line break: a line each, one em and a little apart)
            for number, line in enumerate(text.replace("\r\n", "\n").replace("\r", "\n").split("\n")):
                self.text(tag, x, y + number * self.menus.font(tag, 1.0)[1] * 1.15, width, height, line, color, align, text_x, text_y, alpha)
            return
        font, em = self.menus.font(tag, self.scale)
        advance = font.getlength(text)
        left = x + text_x
        if align == "center":
            left = x + (width * self.scale - advance) / self.scale / 2 + text_x * 0
        elif align == "right":
            left = x + width - advance / self.scale
        baseline = y + text_y + em / 1000.0 * 1000.0 * 0 + self.menus.font(tag, 1.0)[1]
        self.draw.text((left * self.scale, baseline * self.scale), text, font=font, fill=(*color, alpha), anchor="ls")

    def rect(self, x0, y0, x1, y1, fill, outline=None, width=1, radius=0):
        box = [x0 * self.scale, y0 * self.scale, x1 * self.scale - 1, y1 * self.scale - 1]
        if radius:
            self.draw.rounded_rectangle(box, radius * self.scale, fill=fill, outline=outline, width=max(1, round(width * self.scale)))
        else:
            self.draw.rectangle(box, fill=fill, outline=outline, width=max(1, round(width * self.scale)))

    def save(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        self.image.save(path, optimize=True)


def backdrop(size, seed):
    """the menus' dark blue with a faint ring and stars, the stand-in for the moving background of the game"""
    width, height = size
    image = Image.new("RGB", size, (4, 10, 24))
    pixels = ImageDraw.Draw(image, "RGBA")
    for y in range(height):
        shade = int(8 + 22 * (1 - abs(y - height * 0.35) / height))
        pixels.line([(0, y), (width, y)], fill=(shade // 3, shade // 2 + 4, shade + 14, 255))
    random.seed(seed)
    for _ in range(int(width * height / 3500)):
        x, y = random.randrange(width), random.randrange(height)
        level = random.randrange(80, 200)
        pixels.point((x, y), fill=(level, level, min(255, level + 30), 255))
    pixels.arc([width * 0.30, height * 0.18, width * 1.55, height * 2.2], 190, 320, fill=(30, 60, 100, 160), width=max(2, width // 90))
    return image


class Drawer:
    """draws the XML widgets of a screen into a canvas, with the state a scenario gives (texts, colours, focus, what is hidden)"""

    def __init__(self, menus, canvas, state):
        self.menus, self.canvas, self.state = menus, canvas, state
        self.drawn_descriptions = set()

    def widget(self, name, x, y, path):
        element = self.menus.widgets.get(name)
        if element is None:
            return
        short = name.rsplit("/", 1)[-1]
        if short in self.state.get("hidden", ()):
            return
        left = x + float(element.get("left", 0))
        top = y + float(element.get("top", 0))
        width, height = float(element.get("width", 0)), float(element.get("height", 0))
        kind = element.get("type", "container")
        key = (path[-1] if path else "", short)
        # the bitmap: a focused row or button has its second frame
        bitmap = element.get("bitmap")
        if bitmap and bitmap in self.menus.bitmaps and (width or height):
            index = 1 if short in self.state.get("focused", ()) or (path and path[-1] in self.state.get("focused", ()) and False) else 0
            if bitmap == "bitmaps/gradient":
                gradient = self.menus.frame(bitmap, 0, width, height, self.canvas.scale)
                if gradient is not None:
                    # (a frame is drawn a unit to a texel from the top left, cut to the widget: its 4 units' width is repeated across, its 512 high cut to 480)
                    stretched = gradient.resize((round(width * self.canvas.scale), gradient.height)).crop((0, 0, round(width * self.canvas.scale), round(height * self.canvas.scale)))
                    stretched.putalpha(stretched.getchannel("A").point(lambda value: int(value * 0.82)))
                    self.canvas.image.paste(stretched, (round(left * self.canvas.scale), round(top * self.canvas.scale)), stretched)
            else:
                self.canvas.paste_frame(bitmap, index, left, top, width, height)
        if kind == "text":
            text = self.state.get("texts", {}).get(key)
            if text is None:
                text = self.state.get("texts", {}).get(("", short))
            if text is None:
                text = element.get("text")
            if text is None and element.get("string_list") in self.menus.strings:
                strings = self.menus.strings[element.get("string_list")]
                text = strings[int(element.get("string_index", 0))] if strings else ""
            color = self.state.get("colors", {}).get(key)
            if color is None:
                color = argb(element.get("color"))
                if short in self.state.get("white", ()):
                    color = WHITE
            self.canvas.text(element.get("font", "ui\\large_ui"), left, top, width, height, text or "", color, element.get("align"), float(element.get("text_x", 0)),
                             float(element.get("text_y", 0)))
        elif kind == "spinner":
            self.spinner(element, short, left, top, width)
        for child in element.findall("child"):
            self.widget(child.get("widget"), left + float(child.get("x", 0)), top + float(child.get("y", 0)), [*path, short])
        # (a list's description, which the engine draws with it: the help line, the lobby's panel)
        if element.get("description") and element.get("description") not in self.drawn_descriptions:
            self.drawn_descriptions.add(element.get("description"))
            self.widget(element.get("description"), left, top, [*path, short])

    def spinner(self, element, short, left, top, width):
        strings = (element.get("strings") or "").split("|")
        text = self.state.get("spinners", {}).get(short, strings[0] if strings else "")
        self.canvas.text(element.get("font", "ui\\large_ui"), left, top, width, 20, text, WHITE, element.get("align"), 0, float(element.get("text_y", 0)))
        for side, bounds_key in (("left", "header_bounds"), ("right", "footer_bounds")):
            bounds = [float(value) for value in (element.get(bounds_key) or "").split()]
            if len(bounds) == 4:
                y0, x0, y1, x1 = bounds
                self.canvas.paste_frame(f"bitmaps/arrow_sm_{side}", 0, left + x0, top + y0, x1 - x0, y1 - y0)


def block(canvas):
    return BLOCK_LEFT, 0


def screen_state_players(data):
    texts, colors, focused, hidden = {}, {}, set(), set()
    rows = data["rows"]
    for index in range(10):
        row = f"player_item_{index + 1}"
        if index >= len(rows):
            hidden.add(row)
            continue
        for cell, name in zip(rows[index]["cells"], ("scr_player_name", "scr_player_tag", "scr_player_team", "scr_player_ping")):
            texts[(row, name)] = cell["text"]
            value = cell["rgb"]
            colors[(row, name)] = ((value >> 16) & 255, (value >> 8) & 255, value & 255)
    if rows:
        focused.add(f"player_item_{data['focus'] + 1}")
    texts[("", "players_head")] = data["head"]
    texts[("", "players_banner")] = data["banner"]
    texts[("", "players_detail")] = data["detail"]
    for name in ("players_head", "players_detail"):
        pass
    if not data["host"]:
        hidden |= {"players_button_add", "players_button_drop"}
    return {"texts": texts, "colors": colors, "focused": focused, "hidden": hidden}


def screen_state_friends(data):
    texts, colors, focused, hidden = {}, {}, set(), set()
    rows = data["rows"]
    for index in range(9):
        row = f"friend_item_{index + 1}"
        if index >= len(rows):
            hidden.add(row)
            continue
        for cell, name in zip(rows[index]["cells"], ("scr_friend_mark", "scr_friend_name", "scr_friend_status")):
            texts[(row, name)] = cell["text"]
            value = cell["rgb"]
            colors[(row, name)] = ((value >> 16) & 255, (value >> 8) & 255, value & 255)
    if rows:
        focused.add(f"friend_item_{data['focus'] + 1}")
    for index, label in enumerate(data["tabs"]):
        texts[("", f"friends_tab_{index}")] = label
        if index == data["tab"]:
            focused.add(f"friends_tab_{index}")
    texts[("", "friends_service")] = data["service"]
    texts[("", "friends_code")] = data["code"]
    texts[("", "friends_detail")] = data["detail"]
    if not data["join"]:
        hidden.add("friends_button_join")
    if not data["invite"]:
        hidden.add("friends_button_invite")
    if not data["add"]:
        hidden.add("friends_button_add")
    return {"texts": texts, "colors": colors, "focused": focused, "hidden": hidden, "white": {"friends_service"}}


def dump(*arguments):
    binary = Path("/tmp/screens_dump_preview")
    sources = [ROOT / "tools/screens_preview/screens_dump.c", ROOT / "port/linux/game/screens.c", ROOT / "port/linux/game/screens_mock.c", ROOT / "port/linux/game/text_safe.c"]
    if not binary.exists() or binary.stat().st_mtime < max(source.stat().st_mtime for source in sources):
        subprocess.run(["cc", "-std=gnu99", "-O1", "-Wno-unused-parameter", "-include", "ctype.h", "-I", str(ROOT / "port/linux/game"), "-o", str(binary), *map(str, sources)], check=True)
    result = subprocess.run([str(binary), *map(str, arguments)], capture_output=True, text=True, check=True)
    return json.loads(result.stdout)


def render_screen(menus, size, screen, state, extra=None):
    canvas = Canvas(menus, size)
    drawer = Drawer(menus, canvas, state)
    drawer.widget(screen, BLOCK_LEFT, 0, [])
    if extra:
        extra(canvas)
    return canvas


def fits_in_units(width_units):
    return width_units


def actions_state(data):
    texts, focused, hidden = {}, set(), set()
    for index in range(8):
        row = f"action_item_{index + 1}"
        if index < len(data["actions"]):
            texts[(row, "scr_action_text")] = data["actions"][index]
        else:
            hidden.add(row)
    texts[("", "actions_title")] = data["title"]
    focused.add("action_item_1")
    return {"texts": texts, "focused": focused, "hidden": hidden}


def pause_menu(canvas, buttons, focus=1, host=True):
    """the in-game pause menu with PLAYERS: the box (menu_tags.c's numbers: 226 wide, 159 + 35 for each button past two high, the list 202 wide at left 12), centred in the screen"""
    height = port_settings.pause_box_height(len(buttons))
    left = BLOCK_LEFT + (640 - 226) / 2
    top = max(2, (480 - height) / 2)
    canvas.rect(left, top, left + 226, top + height, (10, 38, 73, 190), (40, 150, 255, 255), 2, radius=9)
    canvas.rect(left + 1, top + height - 28, left + 225, top + height - 1, (0, 0, 0, 150), None, radius=8)
    canvas.draw.line([(left * canvas.scale, (top + height - 28) * canvas.scale), ((left + 226) * canvas.scale, (top + height - 28) * canvas.scale)], fill=(40, 150, 255, 255), width=max(1, round(canvas.scale)))
    for index, label in enumerate(buttons):
        y = top + 12 + index * 35
        if index == focus:
            canvas.rect(left + 12, y, left + 214, y + 28, (20, 60, 120, 150), (40, 150, 255, 255), 1.5, radius=6)
        font, em = canvas.menus.font("ui\\large_ui", canvas.scale)
        advance = font.getlength(label) / canvas.scale
        canvas.text("ui\\large_ui", left + (226 - advance) / 2, y, 0, 28, label, WHITE if index == focus else AZURE, None, 0, 4)
    canvas.text("ui\\small_ui", left + 14, top + height - 24, 200, 20, "A SELECT    B BACK", (190, 205, 225), None, 0, 5)


def toast_band(canvas, text, detail, kind_color, third=None):
    """a toast as screens_toast.c lays it: 50 percent of the width, at 9 percent of the height, black at 70 percent, the lines centred (a stand-in font)"""
    lines = [(text, kind_color)] + ([(detail, WHITE)] if detail else []) + ([(third, (164, 191, 222))] if third else [])
    width = UNITS_WIDE * 0.50
    left = (UNITS_WIDE - width) / 2
    top = UNITS_HIGH * 0.09
    line_height = 19.0
    height = line_height * len(lines) + line_height / 2
    canvas.rect(left, top, left + width, top + height, (0, 0, 0, 176))
    font = ImageFont.truetype(str(FONTS / "Overpass-750.ttf"), max(1, round(14.3 * canvas.scale)))
    for index, (line, color) in enumerate(lines):
        advance = font.getlength(line)
        canvas.draw.text(((left + width / 2) * canvas.scale - advance / 2, (top + line_height / 4 + index * line_height + line_height * 0.78) * canvas.scale), line, font=font, fill=(*color, 255), anchor="ls")


def render_all(out_dir, only=None):
    menus = Menus()
    S = port_settings.SOCIAL
    results = []

    def emit(name, builder):
        if only and name not in only:
            return
        for label, size in (("tv", TV), ("gamepad", GAMEPAD)):
            canvas = builder(size)
            path = out_dir / f"{name}-{label}.png"
            canvas.save(path)
            results.append(path)

    def players(scenario, focus=2, first=0, name=None):
        data = dump(scenario, focus, first)
        emit(name or scenario, lambda size: render_screen(menus, size, f"{S}/players_screen", screen_state_players(data)))

    players("players", 2, 0, "players")
    players("players", 7, 2, "players-scrolled")
    players("players-client", 1, 0, "players-client")
    players("players-empty", 0, 0, "players-empty")
    for scenario, name in (("actions-person", "actions-person"), ("actions-bot", "actions-bot"), ("actions-me", "actions-me")):
        data = dump(scenario, 0)
        state = actions_state(data)
        state["texts"][("", "actions_answer")] = ""
        emit(name, lambda size, state=state: render_screen(menus, size, f"{S}/actions_screen", state))
    for tab_scenario, focus, name in (("friends", 0, "friends"), ("friends", 5, "friends-needs-map"), ("requests", 0, "friends-requests"), ("party", 1, "friends-party"),
                                      ("blocked", 0, "friends-blocked"), ("friends-empty", 0, "friends-empty"), ("friends-offline", 0, "friends-offline"),
                                      ("friends-nofriendservice", 0, "friends-no-service")):
        data = dump(tab_scenario, focus)
        emit(name, lambda size, data=data: render_screen(menus, size, f"{S}/friends_screen", screen_state_friends(data)))
    data = dump("add-friend", 0, 0, "7F3K 9QX")
    state = {"texts": {("", "add_own_code"): data["own"], ("", "add_answer"): "KEEP TYPING: 7 OF 13.", ("", "code_value"): data["typed"] + "_", ("", "code_label"): "FRIEND'S CODE:"},
             "focused": {"op_code"}, "white": {"add_own_code", "add_answer"}}
    emit("add-friend", lambda size: render_screen(menus, size, f"{S}/add/friend_add_screen", state))
    # the bot settings (spinners on the settings screens' rows)
    bot = f"{port_settings.PE}/bot_settings"
    emit("bot-settings", lambda size: render_screen(menus, size, f"{bot}/bot_settings_screen", {
        "focused": {"op_count"}, "spinners": {"count_spinner": "6", "skill_spinner": "HEROIC", "teams_spinner": "EVEN"},
        "texts": {("", "help"): "How many computer players fill the game's lobby\n(a game on this machine). They leave as it goes down."}}))
    # the lobby (its rows with the tags; the map's panel is a plain frame here)
    lobby = dump("lobby")
    lobby_state = {"texts": {}, "colors": {}, "focused": {"list_item_1"}, "hidden": {"list_item_" + str(i) for i in range(len(lobby["rows"]), 11)} | {"lobby_map_pic", "lobby_map_name"}}
    for index, row in enumerate(lobby["rows"]):
        lobby_state["texts"][(f"list_item_{index}", "scr_lobby_name")] = row["cells"][0]["text"]
        lobby_state["texts"][(f"list_item_{index}", "list_item_text")] = ""
        lobby_state["texts"][(f"list_item_{index}", "scr_lobby_tag")] = row["cells"][1]["text"]
        value = row["cells"][1]["rgb"]
        lobby_state["colors"][(f"list_item_{index}", "scr_lobby_tag")] = ((value >> 16) & 255, (value >> 8) & 255, value & 255)
    lobby_state["hidden"] = {f"list_item_{index}" for index in range(len(lobby["rows"]), 11)}
    lobby_state["hidden"].add("list_item_arrows")
    lobby_state["texts"][("", "lobby_join_help")] = "A: PLAYER OPTIONS   L/R: BOTS"
    lobby_state["texts"][("", "lobby_game_data")] = "Slayer\r\nTeam Slayer\r\n12 of 16 players"
    emit("lobby", lambda size: render_screen(menus, size, "main_menu/multiplayer_type_select/lobby/lobby_screen", lobby_state))
    # the Multiplayer screen's bar with FRIENDS
    # the pause menu with PLAYERS (host and client), over a darkened frame
    emit("pause-host", lambda size: pause_canvas(menus, size, ["RESUME GAME", "PLAYERS", "SETTINGS", "END GAME", "LEAVE GAME"], 1))
    emit("pause-client", lambda size: pause_canvas(menus, size, ["RESUME GAME", "PLAYERS", "SETTINGS", "LEAVE GAME"], 1))
    # toasts, over a stand-in game picture
    toasts = dump("toasts")["toasts"]
    colors = {0: (0x40, 0xD0, 0x50), 1: WHITE, 2: WHITE, 3: WHITE, 4: AZURE, 5: (0xFF, 0xB0, 0x20), 6: WHITE}
    for index, toast in enumerate(toasts):
        third = "D-pad Right  JOIN   Left  LATER" if toast["kind"] == 4 else None
        emit(f"toast-{index + 1}", lambda size, toast=toast, third=third: toast_canvas(menus, size, toast, colors[toast["kind"]], third))
    results += social_view(out_dir, only)
    return results


def social_view(out_dir, only):
    """the GamePad's social view: the real layout code and the real glyphs (tools/screens_preview/social_view_render.c), drawn as the Wii U's quads would be"""
    binary = Path("/tmp/social_view_render_preview")
    sources = [ROOT / "tools/screens_preview/social_view_render.c", ROOT / "port/wiiu/src/wiiu_social_view.c", ROOT / "port/wiiu/src/wiiu_keyboard.c",
               ROOT / "port/linux/game/screens.c", ROOT / "port/linux/game/screens_mock.c", ROOT / "port/linux/game/screens_view.c", ROOT / "port/linux/game/text_safe.c"]
    if not binary.exists() or binary.stat().st_mtime < max(source.stat().st_mtime for source in sources):
        subprocess.run(["cc", "-std=gnu99", "-O1", "-Wno-unused-parameter", "-I", str(ROOT / "port/linux/game"), "-I", str(ROOT / "port/third_party/stb"), "-I", str(ROOT / "port/wiiu/src"),
                        "-iquote", str(ROOT / "port/linux/include"), "-o", str(binary), *map(str, sources), "-lm"], check=True)
    results = []
    for scenario in ("friends", "party", "bots", "bots-client", "empty", "toast", "toast-confirm"):
        name = f"social-{scenario}"
        if only and name not in only:
            continue
        ppm = Path("/tmp") / f"{name}.ppm"
        done = subprocess.run([str(binary), str(ppm), scenario], capture_output=True, text=True, env={"SCREENS_ROOT": str(ROOT)})
        if done.returncode:
            raise SystemExit(f"{scenario}: {done.stderr}")
        path = out_dir / f"{name}-gamepad.png"
        path.parent.mkdir(parents=True, exist_ok=True)
        Image.open(ppm).save(path, optimize=True)
        results.append(path)
        print(done.stderr.strip())
    return results


def lobby_title(canvas, text):
    canvas.text("ui\\large_ui", BLOCK_LEFT + 35, 14, 360, 30, text, (0x29, 0x95, 0xFD), None, 0, 3)


def game_stand_in(size):
    image = Image.new("RGB", size, (30, 50, 38))
    draw = ImageDraw.Draw(image, "RGBA")
    width, height = size
    for y in range(height):
        level = int(60 + 90 * y / height)
        draw.line([(0, y), (width, y)], fill=(level // 2, level, level // 2 + 20, 255))
    draw.polygon([(0, height * 0.55), (width * 0.3, height * 0.4), (width * 0.6, height * 0.5), (width, height * 0.35), (width, height), (0, height)], fill=(40, 70, 50, 255))
    return image


def pause_canvas(menus, size, buttons, focus):
    canvas = Canvas(menus, size)
    canvas.image = game_stand_in(size)
    canvas.draw = ImageDraw.Draw(canvas.image, "RGBA")
    canvas.draw.rectangle([0, 0, size[0], size[1]], fill=(0, 0, 0, 120))
    pause_menu(canvas, buttons, focus)
    return canvas


def toast_canvas(menus, size, toast, color, third):
    canvas = Canvas(menus, size)
    canvas.image = game_stand_in(size)
    canvas.draw = ImageDraw.Draw(canvas.image, "RGBA")
    toast_band(canvas, toast["text"], toast["detail"], color, third)
    return canvas


if __name__ == "__main__":
    out = Path(sys.argv[1])
    for path in render_all(out, set(sys.argv[2:]) or None):
        print(path)
