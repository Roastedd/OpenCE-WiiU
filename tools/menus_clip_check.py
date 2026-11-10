#!/usr/bin/env python3
"""A check of EVERY menu file (port/assets/menus/ce/*.xml: the game's own menus and the port's) for the defect the Friends screen's tabs had on the console: a widget that shows less of
its bitmap than there is (the engine draws a bitmap one unit to a texel from the widget's top left and cuts it at the widget's right and bottom edge, so a button narrower than its art
loses its rounded right end: source/interface/ui_widget.c, draw_bitmap_in_rect; a WIDER widget stretches the art across; the flag fit_bitmap_width scales the whole width of the art to the
widget instead), and the same class of fault in the words and the places.

    python3 tools/menus_clip_check.py [--all] [--json]       prints the findings; exit 1 if a finding of the port's own screens (or, with --all, any) is an error

The rules (rule: severity):
  crop      error     a widget's bitmap has drawn texels (alpha over 60 of 255) beyond the widget's width or height: cut off (not for the flat fills, at most 16 units wide, which are
                      tiles and gradients; not with fit_bitmap_width, which scales the art's width to the widget's)
  stretch   note      a widget wider than its bitmap, whose art the engine stretches across (the factor), or a fit_bitmap_width widget whose art is scaled by it
  text      error     a text that is known here (its own words, or its string list's) whose measured width, in the font's real metrics (the preview's: tools/screens_preview.py), does not fit
                      its widget, and does not wrap to lines that fit its height; or, in a widget with a bitmap, does not fit the drawn part of the bitmap's pill with 2 units to spare
  spinner   error     the widest value of a spinner does not fit between its arrows
  offscreen error     a visible widget (text or bitmap) beyond the screen: its place in the 640 x 480 block is outside what a 16:9 screen shows (106 units more on each side:
                      the TV and the GamePad both draw the block in the middle of 852 x 480 units), or above 0 or below 480
  outside   note      a visible widget outside the 640 wide block but inside the 16:9 screen's extra 106 units each side (a header band, the fades)
  safe      note      a text of the port's own screens (known words) whose capitals are above y 24 or below 456 (the title safe margin the spacing system keeps)
  overlap   error     two visible siblings (pills or known texts) whose drawn parts overlap by more than 1 unit each way and neither holds the other (a panel behind its rows does);
                      the pills of a bar of buttons or tabs are also checked for being 2 or more apart
Texts the game fills at run time (data inputs, a list's rows) are not known here: `unknown` counts them. The pause menu's own box is made from the map's tags (port/linux/game/menu_tags.c),
the lobby's rows and the on-screen keyboard from code: the social screens' layout tests (tools/test_screens_layout.py) draw those that are the port's.
"""
import argparse
import json
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import screens_preview  # noqa: E402

CE = screens_preview.CE
MENUS = screens_preview.MENUS
ENGINE = screens_preview.ENGINE
LARGE = "ui\\large_ui"
ALPHA = 60
SAFE_TOP, SAFE_BOTTOM = 24, 456
WIDE_EXTRA = (852 - 640) // 2  # units a 16:9 screen shows each side of the block
LINE_PITCH = {LARGE: 22.0, "ui\\small_ui": 17.0}
# the port's flat fills and tiles (at most 16 units wide): the engine tiles or stretches them and what they cover is one colour or a gradient
FLAT_WIDTH = 16
# a cut of this many units or less takes only the faint edge of a pill's glow (the stock lists' focused frame is drawn 1 unit past its 232 wide widget): a note, not an error
TRIM = 2.0
# buttons or tabs side by side: art to art, one focused: under GAP_ERROR they touch, under GAP_NOTE they are tight (the stock bars are 1.5)
GAP_ERROR, GAP_NOTE = 1.0, 2.0


class Model:
    def __init__(self, folder=CE):
        self.menus = screens_preview.Menus()
        self.widgets, self.ours, self.strings = {}, set(), {}
        self.parent_of = {}
        for path in sorted(folder.glob("*.xml")):
            root = ElementTree.parse(path).getroot()
            text = path.read_text().split("\n", 3)[1] if path.read_text().startswith("<?xml") else ""
            ours = "The port's" in text
            for element in root:
                if element.tag == "widget":
                    self.widgets[element.get("name")] = element
                    if ours:
                        self.ours.add(element.get("name"))
                    element.set("_file", path.name)
                elif element.tag == "strings":
                    self.strings[element.get("name")] = [string.get("text") or "" for string in element.findall("string")]
        for name, element in self.widgets.items():
            for child in element.findall("child"):
                self.parent_of.setdefault(child.get("widget"), set()).add(name)
            if element.get("description"):
                self.parent_of.setdefault(element.get("description"), set()).add(name)
        self.pixels = {}
        self.font_cache = {}

    # ---- the art
    def frames(self, bitmap):
        return self.menus.bitmaps.get(bitmap) or []

    def drawn_extent(self, png, frame_width, frame_height):
        """(x0, y0, x1, y1) in units of the frame's drawn part, or None if the frame is empty or has no picture"""
        if not png:
            return None
        if png not in self.pixels:
            import numpy
            from PIL import Image
            image = Image.open(MENUS / png).convert("RGBA")
            alpha = numpy.array(image)[:, :, 3]
            ys, xs = numpy.where(alpha > ALPHA)
            rx, ry = image.width / max(1, frame_width), image.height / max(1, frame_height)
            self.pixels[png] = None if not len(xs) else (xs.min() / rx, ys.min() / ry, (xs.max() + 1) / rx, (ys.max() + 1) / ry, image.width / rx)
        return self.pixels[png]

    # ---- the words
    def words(self, element):
        """the text the widget shows if it is known here: a list of lines' alternatives, or None"""
        kind = element.get("type", "container")
        if kind == "text":
            text = element.get("text")
            if text is None and element.get("string_list") in self.strings:
                strings = self.strings[element.get("string_list")]
                index = int(element.get("string_index", 0))
                text = strings[index] if index < len(strings) else None
            # (a menu file's text has the engine's line break written \n: menu_tags.c turns it into one)
            return [text.replace("\\n", "\n")] if text else None
        if kind == "spinner":
            if element.get("strings"):
                return [value for value in element.get("strings").split("|") if value]
            if element.get("string_list") in self.strings:
                return [value for value in self.strings[element.get("string_list")] if value.strip()]
        return None

    def measure(self, tag, text):
        font, _ = self.menus.font(tag, 4.0)
        return font.getlength(text) / 4.0

    def lines_needed(self, tag, text, avail):
        """the lines a text takes at a width (greedy, at the spaces; '\\r' and '\\n' break lines)"""
        total = 0
        for paragraph in text.replace("\r\n", "\n").replace("\r", "\n").split("\n"):
            line, count = "", 1
            for word in paragraph.split(" "):
                attempt = (line + " " + word).strip() if line else word
                if line and self.measure(tag, attempt) > avail:
                    count += 1
                    line = word
                else:
                    line = attempt
            total += count
        return total


def element_box(element, x, y):
    """the widget's box: its own left and top from the offset its parents' <child x y> give it (ui_widget.c: a child's offset is its reference's plus its parent's OFFSET; a parent's own
    bounds are not added: a stock child's bounds are in the screen's units)"""
    left = x + float(element.get("left", 0))
    top = y + float(element.get("top", 0))
    return left, top, float(element.get("width", 0)), float(element.get("height", 0))


class Audit:
    def __init__(self, model):
        self.model = model
        self.findings = []
        self.unknown = 0
        self.seen = set()
        self.visible = []  # (screen, name, parent, rect, kind)
        self.places = {}  # name -> the boxes it is placed at

    def add(self, rule, severity, name, element, message, **numbers):
        self.findings.append({"rule": rule, "severity": severity, "widget": name, "file": element.get("_file", "?"), "ours": name in self.model.ours, "message": message, **numbers})

    def roots(self):
        return sorted(name for name in self.model.widgets if name not in self.model.parent_of)

    def run(self):
        for name in sorted(self.model.widgets):
            self.check_widget(name)
        for root in self.roots():
            self.walk(root, 0.0, 0.0, [], root)
        self.siblings()
        # (a widget that is only ever placed beyond what a 16:9 screen shows is not seen: the 3 wide lists' right items wait off the screen's edge to slide in)
        for finding in self.findings:
            places = self.places.get(finding["widget"])
            if finding["rule"] == "crop" and finding["severity"] == "error" and places and all(box[0] > 640 + WIDE_EXTRA or box[2] < -WIDE_EXTRA for box in places):
                finding["severity"] = "note"
                finding["message"] += " (but it waits beyond the screen's edge: never seen)"
        return self.findings

    # ---- one widget on its own: its art and its words against its own size
    def check_widget(self, name):
        model = self.model
        element = model.widgets[name]
        width, height = float(element.get("width", 0)), float(element.get("height", 0))
        bitmap = element.get("bitmap")
        fit = "fit_bitmap_width" in (element.get("flags") or "").split()
        pill_x = None
        if bitmap and bitmap in model.menus.bitmaps and (width or height):
            for index, (png, frame_width, frame_height) in enumerate(model.frames(bitmap)):
                extent = model.drawn_extent(png, frame_width, frame_height)
                if extent is None or not frame_width:
                    continue
                if extent[4] <= FLAT_WIDTH:
                    continue
                x0, y0, x1, y1 = extent[:4]
                if fit:
                    scale = width / frame_width
                    model_x0, model_x1 = x0 * scale, x1 * scale
                    if index == 0:
                        self.add("stretch", "note", name, element, f"fit_bitmap_width: the art's {frame_width} units are scaled to the widget's {width:.0f} ({scale:.2f})", scale=round(scale, 3))
                else:
                    model_x0, model_x1 = x0, min(x1, max(x1, 0))
                    if width > frame_width + 1 and index == 0:
                        self.add("stretch", "note", name, element, f"the widget is {width:.0f} wide, the art {frame_width}: stretched across x{width / frame_width:.2f}", scale=round(width / frame_width, 3))
                    if width and x1 > width + 0.5:
                        self.add("crop", "error" if x1 - width > TRIM else "note", name, element, f"frame {index} of {bitmap}: art drawn to x {x1:.1f} of {frame_width}, widget is {width:.0f} wide: {x1 - width:.1f} units cut off its right end", frame=index, art=round(x1, 1), widget_width=width)
                    if width > frame_width + 1:
                        model_x1 = x1 * width / frame_width
                        model_x0 = x0 * width / frame_width
                if height and y1 > height + 0.5:
                    self.add("crop", "error" if y1 - height > TRIM else "note", name, element, f"frame {index} of {bitmap}: art drawn to y {y1:.1f} of {frame_height}, widget is {height:.0f} high: {y1 - height:.1f} units cut off its bottom", frame=index, art=round(y1, 1), widget_height=height)
                if index == 0 or pill_x is None:
                    pill_x = (model_x0, min(model_x1, width) if width else model_x1)
        elif bitmap and bitmap not in model.menus.bitmaps:
            self.add("noart", "note", name, element, f"the bitmap {bitmap} is not in bitmaps.xml (the game's own tag: drawn from the map, not measurable here)")
        # the words
        words = model.words(element)
        kind = element.get("type", "container")
        if words is None:
            if kind in ("text", "spinner") and element.get("text") is None and not element.get("string_list") and not element.get("strings"):
                self.unknown += 1
            return
        tag = element.get("font", LARGE)
        engine = ENGINE.get(tag, ENGINE[LARGE])
        align = element.get("align")
        text_x = float(element.get("text_x", 0))
        text_y = float(element.get("text_y", 0))
        for text in words:
            if not text:
                continue
            widest = max(model.measure(tag, line) for line in text.replace("\r\n", "\n").replace("\r", "\n").split("\n"))
            if kind == "spinner":
                lo, hi = 0.0, width
                for key, side in (("header_bounds", 0), ("footer_bounds", 1)):
                    bounds = [float(value) for value in (element.get(key) or "").split()]
                    if len(bounds) == 4:
                        if side == 0:
                            lo = max(lo, bounds[3])
                        else:
                            hi = min(hi, bounds[1])
                if widest > hi - lo + 0.5:
                    self.add("spinner", "error", name, element, f"the value {text!r} is {widest:.1f} wide, the room between the arrows is {hi - lo:.1f}", text=text, width=round(widest, 1), room=round(hi - lo, 1))
                continue
            if align == "center":
                avail = width - 2 * abs(text_x)
            elif align == "right":
                avail = width - engine["right"] - text_x
            else:
                avail = width - engine["left"] - text_x
            lines = model.lines_needed(tag, text, avail) if width else 1
            pitch = LINE_PITCH.get(tag, 22.0)
            need_height = text_y + engine["cap_offset"] + engine["caps"] + (lines - 1) * pitch
            if width and (widest > avail + 0.5 and lines == 1 or (lines > 1 and need_height > height + 3.0)):
                self.add("text", "error", name, element,
                         f"{text!r} is {widest:.1f} wide in {tag.split(chr(92))[-1]}; the widget has {avail:.1f} for it" + (f" and wraps to {lines} lines ({need_height:.0f} high) in {height:.0f}" if lines > 1 else ""),
                         text=text, measured=round(widest, 1), room=round(avail, 1), lines=lines)
            elif pill_x is not None and align == "center" and widest > (pill_x[1] - pill_x[0]) - 4 + 0.5:
                self.add("text", "error", name, element, f"{text!r} is {widest:.1f} wide, the pill's drawn part is {pill_x[1] - pill_x[0]:.1f}: under 2 units a side", text=text, measured=round(widest, 1), pill=round(pill_x[1] - pill_x[0], 1))

    # ---- the tree: places on the screen
    def walk(self, name, x, y, path, screen, auto=False):
        model = self.model
        element = model.widgets.get(name)
        if element is None or name in path or len(path) > 24:
            return
        left, top, width, height = element_box(element, x, y)
        key = (screen, name, round(left, 1), round(top, 1))
        if key in self.seen:
            return
        self.seen.add(key)
        kind = element.get("type", "container")
        bitmap = element.get("bitmap")
        shows = False
        if bitmap and bitmap in model.menus.bitmaps and (width or height):
            frames = model.frames(bitmap)
            extent = model.drawn_extent(frames[0][0], frames[0][1], frames[0][2]) if frames else None
            if extent is not None and extent[4] > FLAT_WIDTH:
                shows = True
        text = model.words(element) if kind == "text" else None
        # (a spinner's value and the texts shown: the widget's own box)
        if shows or text:
            rect = (left, top, left + width, top + height)
            focus = rect
            if shows:
                # (a pill is what its art draws, as far as the widget shows it: at rest (frame 0) and focused (frame 1, which is larger: the widget's box has room around both))
                fit = "fit_bitmap_width" in (element.get("flags") or "").split()
                boxes = []
                for png, frame_width, frame_height in frames[:2]:
                    extent = model.drawn_extent(png, frame_width, frame_height)
                    if extent is None or not frame_width:
                        boxes.append(None)
                        continue
                    ratio = width / frame_width if fit or width > frame_width + 1 else 1.0
                    boxes.append((left + extent[0] * ratio, top + extent[1], left + (min(extent[2] * ratio, width) if width else extent[2]), top + (min(extent[3], height) if height else extent[3])))
                boxes = [box for box in boxes if box]
                if boxes:
                    rect, focus = boxes[0], boxes[-1]
            self.visible.append((screen, name, path[-1] if path else None, rect, "pill" if shows else "text", element, auto, focus))
            self.places.setdefault(name, []).append(rect)
            if rect[0] < -WIDE_EXTRA or rect[2] > 640 + WIDE_EXTRA or rect[1] < 0 or rect[3] > 480:
                self.add("offscreen", "error", name, element, f"places it at x {rect[0]:.0f} to {rect[2]:.0f}, y {rect[1]:.0f} to {rect[3]:.0f} on screen {screen}: beyond what a 16:9 screen shows (x {-WIDE_EXTRA} to {640 + WIDE_EXTRA}, y 0 to 480)", rect=[round(value, 1) for value in rect], screen=screen)
            elif rect[0] < 0 or rect[2] > 640:
                self.add("outside", "note", name, element, f"x {rect[0]:.0f} to {rect[2]:.0f} on screen {screen} is outside the 640 block (it shows on a 16:9 screen only)", rect=[round(value, 1) for value in rect], screen=screen)
        if text and name in model.ours and (path or width >= 640):
            tag = element.get("font", LARGE)
            engine = ENGINE.get(tag, ENGINE[LARGE])
            cap_top = top + float(element.get("text_y", 0)) + engine["cap_offset"]
            cap_bottom = cap_top + engine["caps"]
            if cap_top < SAFE_TOP - 0.6 or cap_bottom > SAFE_BOTTOM + 0.6:
                self.add("safe", "note", name, element, f"capitals at y {cap_top:.1f} to {cap_bottom:.1f} on screen {screen}: outside the title safe margin {SAFE_TOP} to {SAFE_BOTTOM}", screen=screen)
        for child in element.findall("child"):
            # (the children of a list with no place of their own are stacked by the engine: they share the list's origin here and are not compared)
            placed = child.get("x") is not None or child.get("y") is not None
            target = model.widgets.get(child.get("widget"))
            placed = placed or (target is not None and (target.get("left") is not None or target.get("top") is not None))
            self.walk(child.get("widget"), x + float(child.get("x", 0)), y + float(child.get("y", 0)), [*path, name], screen, element.get("type") == "column_list" and not placed)
        if element.get("description"):
            self.walk(element.get("description"), x, y, [*path, name], screen)

    # ---- siblings that overlap, buttons and tabs that touch
    def siblings(self):
        groups = {}
        for screen, name, parent, rect, kind, element, auto, focus in self.visible:
            if not auto:
                groups.setdefault((screen, parent), []).append((name, rect, kind, element, focus))
        reported = set()
        for (screen, parent), members in groups.items():
            for index, (a_name, a, a_kind, a_element, a_focus) in enumerate(members):
                for b_name, b, b_kind, b_element, b_focus in members[index + 1:]:
                    alternatives = "header_" in (a_element.get("bitmap") or "") and "header_" in (b_element.get("bitmap") or "")
                    if a_name == b_name or alternatives or (a_name, b_name) in reported or a_kind != b_kind or all(abs(a[i] - b[i]) < 1 for i in range(4)):
                        # (two of one place are alternatives, one for a platform or a setting, or the list stacks them: the same box)
                        continue
                    dx = min(a[2], b[2]) - max(a[0], b[0])
                    dy = min(a[3], b[3]) - max(a[1], b[1])
                    contains = (a[0] <= b[0] + 1 and a[1] <= b[1] + 1 and a[2] >= b[2] - 1 and a[3] >= b[3] - 1) or (b[0] <= a[0] + 1 and b[1] <= a[1] + 1 and b[2] >= a[2] - 1 and b[3] >= a[3] - 1)
                    if dx > 1 and dy > 1 and not contains:
                        reported.add((a_name, b_name))
                        self.add("overlap", "error", a_name, a_element, f"overlaps {b_name} by {dx:.1f} x {dy:.1f} units on screen {screen} ({a_kind}s)", other=b_name, screen=screen, overlap=[round(dx, 1), round(dy, 1)])
                    elif a_kind == "pill" and dx <= 1 and abs((a[3] - a[1]) - (b[3] - b[1])) < 1 and abs(a[1] - b[1]) < 1:
                        # (buttons and tabs side by side: the focused one against the one at rest, either way round)
                        gap = min(max(a_focus[0], b[0]) - min(a_focus[2], b[2]), max(a[0], b_focus[0]) - min(a[2], b_focus[2]))
                        if gap < GAP_NOTE - 1e-6 and min(a[3], b[3]) - max(a[1], b[1]) > 1:
                            reported.add((a_name, b_name))
                            self.add("overlap", "error" if gap < GAP_ERROR else "note", a_name, a_element, f"is {gap:.1f} from {b_name}, art to art with one focused (buttons and tabs in a row are 2 or more apart, 1 at the least) on screen {screen}", other=b_name, screen=screen, gap=round(gap, 1))


def check(folder=CE, mutate=None):
    """the findings (and the audit) of the menu files; `mutate` is a function of the Model that a test uses to put a fault in the widgets as loaded"""
    model = Model(folder)
    if mutate:
        mutate(model)
    audit = Audit(model)
    findings = audit.run()
    # (the same finding reached by several screens is one)
    unique, seen = [], set()
    for finding in findings:
        key = (finding["rule"], finding["widget"], finding["message"].split(" on screen ")[0])
        if key in seen:
            continue
        seen.add(key)
        unique.append(finding)
    return unique, audit


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--all", action="store_true", help="exit 1 for the game's own menus' errors too")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--notes", action="store_true", help="print the notes too")
    arguments = parser.parse_args()
    findings, audit = check()
    if arguments.json:
        print(json.dumps(findings, indent=1))
    else:
        for finding in findings:
            if finding["severity"] == "note" and not arguments.notes:
                continue
            print(f"{'PORT ' if finding['ours'] else 'GAME '}{finding['severity']:5} {finding['rule']:9} {finding['file']}: {finding['widget']}: {finding['message']}")
        print(f"{len(findings)} findings; {audit.unknown} texts are filled at run time")
    bad = [finding for finding in findings if finding["severity"] == "error" and (arguments.all or finding["ours"])]
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
