#!/usr/bin/env python3
"""Pictures of the social screens as they would be laid out on the Wii U: the TV (1920 x 1080) and the GamePad (854 x 480).

    python3 tools/screens_preview.py OUT_DIR [scenario ...]          needs Pillow, and a C compiler for tools/screens_preview/screens_dump.c

WHAT THIS IS: a layout drawing, not a screenshot of the game. The menus' own XML (port/assets/menus/ce, as tools/port_settings.py writes it) is placed the way the widget engine places it
(a child at its x and y in its parent, a widget's bitmap from its top left cut to its size, a text from its text_y down), with the menus' own PNGs (the row pills, the buttons, the
gradient) and Overpass, the fonts the game draws the menus in (port/assets/fonts/fonts.json), at the sizes their capitals have (13 and 10 units). The texts and colours of the rows are
what port/linux/game/screens.c puts in them for the mock provider (screens_dump.c prints them as JSON: the same functions menu_screens.inl calls). The game's moving background (the ring)
is a stand-in; the pause menu's box and buttons are drawn from the same numbers menu_tags.c uses (the real list is made of the map's tags); the toasts are drawn with a stand-in font.
The engine's text placement is the console's own, measured on its pictures of 2026-11-09 (runs/wiiu/mcp/20261109-223325-tv.png, -223601-tv.png, 20261110-002347-tv.png): a text's
capitals begin 3.5 units (the large font) or 3.7 (the small) below the widget's top plus its text_y, a text starts 4.4 or 3.1 units in from the widget's left and a right
aligned one ends 3.9 in from its right edge (ENGINE below; the text of the console's rows, titles and details matched the drawing's to within 0.5 units after it). The panel's darkness is
the bitmap's own (alpha 176 of 255) and the header band is the engine's widen_to_screen rule (ui_widget.c) applied to the bitmaps it applies to.

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
# the engine's placement of a text in its widget, measured on the console's pictures (units): where a font's capitals start below top + text_y, and its inset from the left and right
ENGINE = {"ui\\large_ui": {"cap_offset": 3.5, "left": 4.4, "right": 3.9, "caps": 13.0}, "ui\\small_ui": {"cap_offset": 3.7, "left": 3.1, "right": 3.9, "caps": 10.0}}
# the HUD of a game under the pause menu, from the console's picture (runs/wiiu/mcp/20261109-224339-tv.png), in units of the menus' block (x 0 to 640): the shield bars at the left
# top corner end at x 64, the health bar at the right begins at 556; their y
HUD_ZONES = [(-60.0, 37.0, 64.0, 73.0), (556.0, 37.0, 700.0, 67.0)]


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

    def extents(self, bitmap, index):
        """the drawn part of a frame (where its alpha is solid), in units from its top left: a row's pill is not the 28 or 32 of its frame"""
        frames = self.bitmaps.get(bitmap)
        if not frames:
            return None
        png, frame_width, frame_height = frames[min(index, len(frames) - 1)]
        if not png:
            return None
        key = ("extents", png)
        if key not in self.image_cache:
            image = Image.open(MENUS / png).convert("RGBA")
            alpha = image.getchannel("A").point(lambda value: 255 if value > 60 else 0)
            box = alpha.getbbox()
            ratio_x, ratio_y = image.width / max(1, frame_width), image.height / max(1, frame_height)
            self.image_cache[key] = (box[0] / ratio_x, box[1] / ratio_y, box[2] / ratio_x, box[3] / ratio_y) if box else None
        return self.image_cache[key]

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

    def text(self, tag, x, y, width, height, text, color, align=None, text_x=0, text_y=0, alpha=255, record=None, origin=None):
        """a text as the engine places it (ENGINE): its capitals' top at y + text_y + the font's offset, its left 4 in; `record` is told its ink (units): one line, the lines of a
        '\\r' text are drawn one under the other as the engine's line breaks are, 22 units apart for the large font"""
        if not text:
            return
        if "\n" in text or "\r" in text:
            pitch = 22.0 if tag == "ui\\large_ui" else 17.0
            for number, line in enumerate(text.replace("\r\n", "\n").replace("\r", "\n").split("\n")):
                self.text(tag, x, y + number * pitch, width, height, line, color, align, text_x, text_y, alpha, record, origin or (x, y))
            return
        font, _ = self.menus.font(tag, self.scale)
        engine = ENGINE.get(tag, ENGINE["ui\\large_ui"])
        advance = font.getlength(text) / self.scale
        left = x + engine["left"] + text_x
        if align == "center":
            left = x + (width - advance) / 2 + text_x
        elif align == "right":
            left = x + width - engine["right"] - advance
        baseline = y + text_y + engine["cap_offset"] + engine["caps"]
        self.draw.text((left * self.scale, baseline * self.scale), text, font=font, fill=(*color, alpha), anchor="ls")
        if record is not None:
            box = font.getbbox(text, anchor="ls")
            record({"font": tag, "text": text, "color": color, "ink": (left + box[0] / self.scale, baseline + box[1] / self.scale, left + box[2] / self.scale, baseline + box[3] / self.scale),
                    "caps": (baseline - engine["caps"], baseline), "box": (origin[0], origin[1], origin[0] + width, origin[1] + height) if origin else (x, y, x + width, y + height),
                    "align": align})

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
    """draws the XML widgets of a screen into a canvas, with the state a scenario gives (texts, colours, focus, what is hidden), and records what it drew: every text's ink and
    its box (`records`), every pill and panel (`pills`), for the layout checks (tools/test_screens_layout.py)"""

    def __init__(self, menus, canvas, state):
        self.menus, self.canvas, self.state = menus, canvas, state
        self.drawn_descriptions = set()
        # (the text widgets with a bitmap, tabs and buttons, that were drawn with no words: a button that shows nothing is a defect the model's own texts can hide)
        self.silent = []
        self.records, self.pills = [], []
        self.stack = []

    def bitmap(self, element, short, left, top, width, height):
        """the widget's bitmap as the engine draws it: from the top left, a unit to a texel, cut to the widget; a flat or vertical fill (at most 16 texels wide) of a widget whose
        bounds cover the whole 640 x 480 design space is widened to the whole screen (ui_widget.c's widen_to_screen: bounds from 0, 0 to at least 320 x 240)"""
        bitmap = element.get("bitmap")
        frames = self.menus.bitmaps.get(bitmap)
        if not frames or not (width or height):
            return None
        index = 1 if short in self.state.get("focused", ()) else 0
        frame = self.menus.frame(bitmap, index, width, height, self.canvas.scale)
        if frame is None:
            return None
        scale = self.canvas.scale
        png = Image.open(MENUS / frames[min(index, len(frames) - 1)][0])
        if png.width <= 16:
            x0, x1 = left, left + width
            definition = (float(element.get("left", 0)), float(element.get("top", 0)), float(element.get("left", 0)) + width, float(element.get("top", 0)) + height)
            if definition[0] <= 0 and definition[1] <= 0 and definition[2] >= 320 and definition[3] >= 240:
                x0, x1 = left - BLOCK_LEFT, left + width + BLOCK_LEFT
            rows = max(1, round(height * scale))
            # (a texture shorter than the widget, a flat fill: its last row goes on)
            source = frame.crop((0, 0, 1, min(frame.height, rows)))
            column = Image.new("RGBA", (1, rows))
            column.paste(source, (0, 0))
            for extra_row in range(source.height, rows):
                column.paste(source.crop((0, source.height - 1, 1, source.height)), (0, extra_row))
            column = column.resize((max(1, round((x1 - x0) * scale)), rows))
            self.canvas.image.paste(column, (round(x0 * scale), round(top * scale)), column)
            self.pills.append({"name": short, "bitmap": bitmap, "rect": (x0, top, x1, top + rows / scale), "kind": "panel"})
            return None
        self.canvas.paste_frame(bitmap, index, left, top, width, height)
        extent = self.menus.extents(bitmap, index)
        if extent is None:
            return None
        rect = (left + extent[0], top + extent[1], min(left + extent[2], left + width), min(top + extent[3], top + height))
        pill = {"name": short, "bitmap": bitmap, "rect": rect, "kind": "pill", "focused": index == 1}
        self.pills.append(pill)
        return pill

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
        pill = self.bitmap(element, short, left, top, width, height) if element.get("bitmap") else None
        if pill:
            self.stack.append(pill)
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
            if element.get("bitmap") and not (text or "").strip():
                self.silent.append(short)
            owner = self.stack[-1] if self.stack else None
            self.canvas.text(element.get("font", "ui\\large_ui"), left, top, width, height, text or "", color, element.get("align"), float(element.get("text_x", 0)),
                             float(element.get("text_y", 0)),
                             record=lambda record, short=short, path=tuple(path), owner=owner: self.records.append({**record, "name": short, "path": path, "owner": owner}))
        elif kind == "spinner":
            self.spinner(element, short, left, top, width)
        for child in element.findall("child"):
            self.widget(child.get("widget"), left + float(child.get("x", 0)), top + float(child.get("y", 0)), [*path, short])
        if pill:
            self.stack.pop()
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
    for index in range(port_settings.SOCIAL_PLAYER_ROWS):
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
    texts[("", "players_count")] = data["players_count"]
    texts[("", "players_empty")] = data["empty"].replace("\n", "\r")
    texts[("", "players_detail")] = data["detail"]
    if not data["players_up"]:
        hidden.add("players_more_up")
    if not data["players_down"]:
        hidden.add("players_more_down")
    if not data["host"]:
        hidden |= {"players_button_add", "players_button_drop"}
    return {"texts": texts, "colors": colors, "focused": focused, "hidden": hidden}


def screen_state_friends(data):
    texts, colors, focused, hidden = {}, {}, set(), set()
    rows = data["rows"]
    for index in range(port_settings.SOCIAL_FRIEND_ROWS):
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
    texts[("", "friends_count")] = data["friends_count"]
    texts[("", "friends_empty")] = data["empty"].replace("\n", "\r")
    texts[("", "friends_detail")] = data["detail"]
    if not data["friends_up"]:
        hidden.add("friends_more_up")
    if not data["friends_down"]:
        hidden.add("friends_more_down")
    if not data["join"]:
        hidden.add("friends_button_join")
    if not data["invite"]:
        hidden.add("friends_button_invite")
    if not data["add"]:
        hidden.add("friends_button_add")
    return {"texts": texts, "colors": colors, "focused": focused, "hidden": hidden, "white": {"friends_service"}}


def screen_state_store(data):
    """the mod store's screen: the rows' cells, the counter, the arrows, the words of an empty store, the detail and the footer, the buttons that apply (screens.c's screens_store_*)"""
    texts, colors, focused, hidden = {}, {}, set(), set()
    rows = data["rows"]
    for index in range(port_settings.STORE_ROWS):
        row = f"store_item_{index + 1}"
        if index >= len(rows):
            hidden.add(row)
            continue
        for cell, name in zip(rows[index]["cells"], ("store_cell_name", "store_cell_author", "store_cell_size", "store_cell_status")):
            texts[(row, name)] = cell["text"]
            value = cell["rgb"]
            colors[(row, name)] = ((value >> 16) & 255, (value >> 8) & 255, value & 255)
    if rows:
        focused.add(f"store_item_{data['focus'] + 1}")
    texts[("", "store_head")] = data["head"]
    texts[("", "store_count")] = data["store_count"]
    texts[("", "store_empty")] = data["empty"].replace("\n", "\r")
    texts[("", "store_detail")] = data["detail"]
    texts[("", "store_footer")] = data["footer"]
    if not data["store_up"]:
        hidden.add("store_more_up")
    if not data["store_down"]:
        hidden.add("store_more_down")
    for bit, name in ((1, "get"), (2, "update"), (4, "stop"), (8, "remove"), (16, "refresh")):
        if not data["buttons"] & bit:
            hidden.add(f"store_button_{name}")
    return {"texts": texts, "colors": colors, "focused": focused, "hidden": hidden}


def dump(*arguments):
    binary = Path("/tmp/screens_dump_preview")
    sources = [ROOT / "tools/screens_preview/screens_dump.c", ROOT / "port/linux/game/screens.c", ROOT / "port/linux/game/screens_mock.c", ROOT / "port/linux/game/text_safe.c"]
    if not binary.exists() or binary.stat().st_mtime < max(source.stat().st_mtime for source in sources):
        subprocess.run(["cc", "-std=gnu99", "-O1", "-Wno-unused-parameter", "-include", "ctype.h", "-I", str(ROOT / "port/linux/game"), "-o", str(binary), *map(str, sources)], check=True)
    result = subprocess.run([str(binary), *map(str, arguments)], capture_output=True, text=True, check=True)
    return json.loads(result.stdout)


class SilentButtons(Exception):
    """a tab or button was drawn with no words in it (the Friends screen's four tabs, on the console, once)"""

    def __init__(self, screen, names):
        super().__init__(f"{screen}: no text in {', '.join(names)}")
        self.names = names


def render_screen(menus, size, screen, state, extra=None, backdrop_image=None):
    canvas = Canvas(menus, size)
    if backdrop_image is not None:
        canvas.image = backdrop_image(size)
        canvas.draw = ImageDraw.Draw(canvas.image, "RGBA")
    drawer = Drawer(menus, canvas, state)
    drawer.widget(screen, BLOCK_LEFT, 0, [])
    if drawer.silent:
        raise SilentButtons(screen, drawer.silent)
    if extra:
        extra(canvas)
    # (what was drawn, in units: tools/test_screens_layout.py's checks read it)
    canvas.records, canvas.pills = drawer.records, drawer.pills
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
    """a toast as screens_toast.c lays it in a game: 50 percent of the width, at 16 percent of the height (under the HUD's bars; 9 percent in the menus), black at 70 percent, the
    lines centred (a stand-in font)"""
    lines = [(text, kind_color)] + ([(detail, WHITE)] if detail else []) + ([(third, (164, 191, 222))] if third else [])
    width = UNITS_WIDE * 0.50
    left = (UNITS_WIDE - width) / 2
    top = UNITS_HIGH * 0.16
    line_height = 19.0
    height = line_height * len(lines) + line_height / 2
    canvas.rect(left, top, left + width, top + height, (0, 0, 0, 176))
    font = ImageFont.truetype(str(FONTS / "Overpass-750.ttf"), max(1, round(14.3 * canvas.scale)))
    for index, (line, color) in enumerate(lines):
        advance = font.getlength(line)
        canvas.draw.text(((left + width / 2) * canvas.scale - advance / 2, (top + line_height / 4 + index * line_height + line_height * 0.78) * canvas.scale), line, font=font, fill=(*color, 255), anchor="ls")


# the screens' states that are drawn and checked: name, screen, dump scenario, focus, first row shown. "add" and "lobby" and "bot-settings" have their own states below
SCENES = [
    ("players", "players", "players", 2, 0), ("players-scrolled", "players", "players", 6, 2), ("players-client", "players", "players-client", 1, 0),
    ("players-empty", "players", "players-empty", 0, 0), ("players-16", "players", "players16", 8, 7), ("players-16-top", "players", "players16", 0, 0),
    ("friends", "friends", "friends", 0, 0), ("friends-download", "friends", "friends", 2, 0), ("friends-needs-map", "friends", "friends", 5, 0),
    ("friends-requests", "friends", "requests", 0, 0), ("friends-party", "friends", "party", 1, 0), ("friends-blocked", "friends", "blocked", 0, 0),
    ("friends-50", "friends", "friends50", 3, 20), ("friends-50-top", "friends", "friends50", 0, 0), ("friends-50-end", "friends", "friends50", 8, 41),
    ("friends-empty", "friends", "friends-empty", 0, 0), ("friends-offline", "friends", "friends-offline", 0, 0), ("friends-disabled", "friends", "friends-disabled", 0, 0),
    ("friends-no-service", "friends", "friends-nofriendservice", 0, 0), ("friends-requests-empty", "friends", "requests-empty", 0, 0),
    ("friends-party-empty", "friends", "party-empty", 0, 0), ("friends-blocked-empty", "friends", "blocked-empty", 0, 0),
    ("store", "store", "store", 0, 0), ("store-off", "store", "store-off", 0, 0), ("store-loading", "store", "store-loading", 0, 0), ("store-offline", "store", "store-offline", 0, 0),
    ("store-error", "store", "store-error", 0, 0), ("store-empty", "store", "store-empty", 0, 0), ("store-1", "store", "store-1", 0, 0), ("store-50-top", "store", "store-50", 0, 0),
    ("store-50", "store", "store-50", 3, 20), ("store-50-end", "store", "store-50", 8, 41), ("store-longest", "store", "store-longest", 0, 0), ("store-longest-end", "store", "store-longest", 8, 3),
    ("store-downloading", "store", "store-downloading", 2, 0), ("store-update", "store", "store-update", 0, 0), ("store-failed-job", "store", "store-failed-job", 1, 0),
    ("store-removing", "store", "store-removing", 1, 0), ("store-local", "store", "store", 4, 0), ("store-unplayable", "store", "store", 5, 0), ("store-installed", "store", "store", 1, 0),
    ("actions-person", "actions", "actions-person", 0, 0), ("actions-bot", "actions", "actions-bot", 0, 0), ("actions-me", "actions", "actions-me", 0, 0),
]
# the same screens over a game (the pause menu's way in): the picture has the HUD's bars at the top corners behind them
GAME_SCENES = ["players", "players-16", "friends", "friends-50", "friends-empty", "add-friend", "store", "store-downloading"]


def game_with_hud(size):
    """a stand-in game picture with the HUD's shield and health bars at the top corners, where the console's picture has them (HUD_ZONES): translucent blue, as the game draws
    them, under whatever the menus draw"""
    image = game_stand_in(size)
    canvas_scale = size[0] / UNITS_WIDE
    draw = ImageDraw.Draw(image, "RGBA")
    for x0, y0, x1, y1 in HUD_ZONES:
        draw.rounded_rectangle([(BLOCK_LEFT + x0) * canvas_scale, y0 * canvas_scale, (BLOCK_LEFT + x1) * canvas_scale, y1 * canvas_scale], 4 * canvas_scale, fill=(70, 130, 220, 150),
                               outline=(150, 200, 255, 200), width=max(1, round(canvas_scale)))
    return image


def scene_state(menus, name, *, game=False):
    """(screen widget name, state, data) of a named scene; the add friend and bot settings states are made here"""
    S = port_settings.SOCIAL
    if name in ("add-friend", "add-friend-sent", "add-friend-long"):
        long_answer = "THAT PLAYER'S CODE IS NOT KNOWN. ASK FOR IT: FRIENDS > ADD FRIEND."
        data = dump("add-friend", 0, 0, "7F3K 9QX", long_answer)
        answer = {"add-friend": "KEEP TYPING: 7 OF 13.", "add-friend-sent": data["sent"], "add-friend-long": data["wrapped"]}[name]
        state = {"texts": {("", "add_own_code"): data["own"], ("", "add_answer"): answer, ("", "code_value"): data["typed"] + "_", ("", "code_label"): "FRIEND'S CODE:"},
                 "focused": {"op_code"}, "white": {"add_own_code", "add_answer"}}
        return f"{S}/add/friend_add_screen", state, data
    if name == "lobby":
        lobby = dump("lobby")
        state = {"texts": {}, "colors": {}, "focused": {"list_item_1"}, "hidden": {"list_item_" + str(i) for i in range(len(lobby["rows"]), 11)} | {"lobby_map_pic", "lobby_map_name"}}
        for index, row in enumerate(lobby["rows"]):
            state["texts"][(f"list_item_{index}", "scr_lobby_name")] = row["cells"][0]["text"]
            state["texts"][(f"list_item_{index}", "list_item_text")] = ""
            state["texts"][(f"list_item_{index}", "scr_lobby_tag")] = row["cells"][1]["text"]
            value = row["cells"][1]["rgb"]
            state["colors"][(f"list_item_{index}", "scr_lobby_tag")] = ((value >> 16) & 255, (value >> 8) & 255, value & 255)
        state["hidden"] = {f"list_item_{index}" for index in range(len(lobby["rows"]), 11)}
        state["hidden"].add("list_item_arrows")
        state["texts"][("", "lobby_join_help")] = "A: PLAYER OPTIONS   L/R: BOTS"
        state["texts"][("", "lobby_game_data")] = "Slayer\r\nTeam Slayer\r\n12 of 16 players"
        return "main_menu/multiplayer_type_select/lobby/lobby_screen", state, None
    if name == "bot-settings":
        bot = f"{port_settings.PE}/bot_settings"
        return f"{bot}/bot_settings_screen", {"focused": {"op_count"}, "spinners": {"count_spinner": "6", "skill_spinner": "HEROIC", "teams_spinner": "EVEN"},
                                             "texts": {("", "help"): "Bots only play in a game on this machine for now:\nnot in a hosted or online game."}}, None
    for scene, screen, scenario, focus, first in SCENES:
        if scene == name:
            data = dump(scenario, focus, first)
            if screen == "players":
                return f"{S}/players_screen", screen_state_players(data), data
            if screen == "friends":
                return f"{S}/friends_screen", screen_state_friends(data), data
            if screen == "store":
                return f"{port_settings.STORE}/store_screen", screen_state_store(data), data
            state = actions_state(data)
            state["texts"][("", "actions_answer")] = ""
            return f"{S}/actions_screen", state, data
    raise KeyError(name)


def scene(menus, name, size, game=False):
    """one named scene drawn at a size: the canvas, with its records and pills (units)"""
    screen, state, _ = scene_state(menus, name)
    return render_screen(menus, size, screen, state, None, game_with_hud if game else None)


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

    for name in [scene_name for scene_name, *_ in SCENES] + ["add-friend", "add-friend-sent", "add-friend-long", "bot-settings"]:
        emit(name, lambda size, name=name: scene(menus, name, size))
    for name in GAME_SCENES:
        emit(f"{name}-game", lambda size, name=name: scene(menus, name, size, game=True))
    # the lobby (its rows with the tags; the map's panel is a plain frame here)
    emit("lobby", lambda size: scene(menus, "lobby", size))
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
    canvas.image = game_with_hud(size)
    canvas.draw = ImageDraw.Draw(canvas.image, "RGBA")
    toast_band(canvas, toast["text"], toast["detail"], color, third)
    return canvas


if __name__ == "__main__":
    out = Path(sys.argv[1])
    for path in render_all(out, set(sys.argv[2:]) or None):
        print(path)
