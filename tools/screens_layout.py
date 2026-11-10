"""The layout checks of the social screens: what tools/screens_preview.py drew (every text's ink, every pill and panel, in units of the menus' 640 x 480 screen) against the spacing system
in tools/port_settings.py. `evaluate(canvas, data)` returns the violations as (rule, message) pairs; none means the screen is clean. tools/test_screens_layout.py runs every state of the
mock (and the stress states: 16 players, 50 friends, the widest names, the empty and the offline lists) at the TV's and the GamePad's sizes, and each rule has a negative control.

The rules (the numbers are the spacing system's: port_settings.py, "THE SPACING SYSTEM"):
  safe      no text above y 24 or below 456, none outside the 640 wide block
  box       a text is inside its own widget (the engine clips at the widget's right and bottom edge)
  pill      a text in a row, a tab or a button is inside its pill, 12 from the pill's ends (8 for a tab or a button), and centred on it (capitals' middle within 2.5 of the pill's)
  overlap   no two texts of different widgets touch (4 apart), the cells of a row are 8 apart; no text touches a pill that is not its own; the rows' pills do not overlap
  gap       the title is 6 or more above the tabs, the tabs 3 above the panel's line, the first row 4 below it, the detail 8 below the last row, the footer 8 below the detail
            and 8 above the panel's bottom line
  hud       no text and no pill but the panel's touches the HUD's bars at the top corners (the console's picture of a game under the pause menu)
  columns   a column's texts start at one x (within 3, the letters' own bearings), a row's last text ends 12 before its pill's end
  contrast  a text's colour against the panel's brightest background on the console's pictures (the brightest 5 percent): 4 to 1 for the small font, 3 to 1 for the large
  words     every text is ASCII 0x20 to 0x7E (or the engine's line break) and has no '|'
  empty     a list with no rows has its words in the middle of its place, and no row, and no counter; a list of more rows than it shows has its counter
  legible   the small font's capitals are at least 10 pixels high at the GamePad's size
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import port_settings  # noqa: E402
import screens_preview  # noqa: E402

BLOCK_LEFT = screens_preview.BLOCK_LEFT
# the panel's brightest background on the console's pictures (the 95th percentile of its luminance in the moonlit game of runs/wiiu/mcp/20261109-230803-tv.png, 0.053)
PANEL_BACKGROUND_LUMINANCE = 0.053
CONTRAST_SMALL, CONTRAST_LARGE = 4.0, 3.0
TOLERANCE = 0.6
LARGE = "ui\\large_ui"
ROW_PREFIXES = ("player_item_", "friend_item_", "action_item_", "store_item_")


def luminance(rgb):
    def linear(value):
        value /= 255.0
        return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4
    r, g, b = (linear(value) for value in rgb)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(rgb):
    return (luminance(rgb) + 0.05) / (PANEL_BACKGROUND_LUMINANCE + 0.05)


def block(rect):
    """a rectangle in the block's own units (x from the block's left)"""
    return (rect[0] - BLOCK_LEFT, rect[1], rect[2] - BLOCK_LEFT, rect[3])


def intersects(a, b, margin=0.0):
    """whether two rectangles are closer than margin (touch, or overlap)"""
    return a[0] < b[2] + margin and b[0] < a[2] + margin and a[1] < b[3] + margin and b[1] < a[3] + margin


def row_of(record):
    """the row a cell is in (its parent's name), or None"""
    for name in reversed(record["path"]):
        if name.startswith(ROW_PREFIXES):
            return name
    return None


def evaluate(canvas, data=None, scale=None):
    """the violations of a drawn screen: a list of (rule, message)"""
    out = []
    records = [record for record in canvas.records if record["text"].strip()]
    pills = canvas.pills
    scale = scale or canvas.scale
    block_pills = [pill for pill in pills if pill["kind"] == "pill"]

    def say(rule, message):
        out.append((rule, message))

    # -- words
    for record in records:
        text = record["text"]
        if any(not (0x20 <= ord(character) <= 0x7E) for character in text) or "|" in text:
            say("words", f"{record['name']}: {text!r} has a character outside ASCII 0x20-0x7E or a '|'")
    # -- safe margins
    for record in records:
        x0, y0, x1, y1 = block(record["ink"])
        if y0 < port_settings.SAFE_TOP - TOLERANCE or y1 > port_settings.SAFE_BOTTOM + TOLERANCE:
            say("safe", f"{record['name']} {record['text']!r}: ink y {y0:.1f} to {y1:.1f} is outside the margin {port_settings.SAFE_TOP} to {port_settings.SAFE_BOTTOM}")
        if x0 < -TOLERANCE or x1 > 640 + TOLERANCE:
            say("safe", f"{record['name']} {record['text']!r}: ink x {x0:.1f} to {x1:.1f} is outside the block")
    for pill in block_pills:
        x0, y0, x1, y1 = block(pill["rect"])
        if y1 > port_settings.SAFE_BOTTOM + TOLERANCE or y0 < -TOLERANCE:
            say("safe", f"{pill['name']}: y {y0:.1f} to {y1:.1f} is outside the margin")
    # -- box: the text inside its own widget
    for record in records:
        box = record["box"]
        x0, y0, x1, y1 = record["ink"]
        if x0 < box[0] - TOLERANCE or x1 > box[2] + TOLERANCE:
            say("box", f"{record['name']} {record['text']!r}: ink x {x0 - BLOCK_LEFT:.1f} to {x1 - BLOCK_LEFT:.1f} is outside its box {box[0] - BLOCK_LEFT:.0f} to {box[2] - BLOCK_LEFT:.0f}")
        if y0 < box[1] - TOLERANCE or y1 > box[3] + TOLERANCE:
            say("box", f"{record['name']} {record['text']!r}: ink y {y0:.1f} to {y1:.1f} is outside its box {box[1]:.0f} to {box[3]:.0f}")
    # -- pill: a text in a pill
    for record in records:
        pill = record["owner"]
        if pill is None or pill["kind"] != "pill":
            continue
        x0, y0, x1, y1 = record["ink"]
        px0, py0, px1, py1 = pill["rect"]
        is_row = pill["name"].startswith(ROW_PREFIXES)
        pad = 12 if is_row else 8
        if record["name"] != "scr_friend_mark" and (x0 < px0 + pad - TOLERANCE or x1 > px1 - pad + TOLERANCE):
            say("pill", f"{record['name']} {record['text']!r}: ink x {x0 - BLOCK_LEFT:.1f} to {x1 - BLOCK_LEFT:.1f} is within {pad} of its pill's ends {px0 - BLOCK_LEFT:.0f} to {px1 - BLOCK_LEFT:.0f}")
        middle = (record["caps"][0] + record["caps"][1]) / 2
        if abs(middle - (py0 + py1) / 2) > 2.5:
            say("pill", f"{record['name']} {record['text']!r}: capitals' middle {middle:.1f} is not on its pill's {(py0 + py1) / 2:.1f}")
    # -- overlap: texts of different widgets, texts against pills not their own, pills of rows
    for index, a in enumerate(records):
        for b in records[index + 1:]:
            if a["name"] == b["name"] and a["path"] == b["path"]:
                continue
            same_row = row_of(a) is not None and row_of(a) == row_of(b)
            if intersects(a["ink"], b["ink"], (8 if same_row else 4) - TOLERANCE):
                say("overlap", f"{a['name']} {a['text']!r} and {b['name']} {b['text']!r} are less than {8 if same_row else 4} apart")
    for record in records:
        for pill in block_pills:
            if record["owner"] is pill:
                continue
            if intersects(record["ink"], pill["rect"], -TOLERANCE):
                say("overlap", f"{record['name']} {record['text']!r} touches the pill {pill['name']}")
    rows = [pill for pill in block_pills if pill["name"].startswith(ROW_PREFIXES)]
    for index, a in enumerate(rows):
        for b in rows[index + 1:]:
            if a["rect"][1] < b["rect"][3] - 1.0 and b["rect"][1] < a["rect"][3] - 1.0:
                say("overlap", f"the rows {a['name']} and {b['name']} overlap")
    # -- gap: the stack from the title down
    by_name = {}
    for record in records:
        by_name.setdefault(record["name"], []).append(record)

    def ink_bottom(*names):
        return max((record["ink"][3] for name in names for record in by_name.get(name, ())), default=None)

    def ink_top(*names):
        return min((record["ink"][1] for name in names for record in by_name.get(name, ())), default=None)

    title_bottom = ink_bottom("friends_title", "players_title", "actions_title", "add_title", "store_title")
    tabs = [pill for pill in block_pills if pill["name"].startswith("friends_tab_")]
    if tabs:
        top, bottom = min(pill["rect"][1] for pill in tabs), max(pill["rect"][3] for pill in tabs)
        if title_bottom is not None and top - title_bottom < 6 - TOLERANCE:
            say("gap", f"the title ends {title_bottom:.1f} and the tabs begin {top:.1f}: under 6")
        if port_settings.PANEL_LINE - bottom < 3 - TOLERANCE:
            say("gap", f"the tabs end {bottom:.1f}, the panel's line is at {port_settings.PANEL_LINE}: under 3")
    if rows:
        first, last = min(pill["rect"][1] for pill in rows), max(pill["rect"][3] for pill in rows)
        if first - (port_settings.PANEL_LINE + 1) < 4 - TOLERANCE:
            say("gap", f"the first row begins {first:.1f}, the panel's line ends {port_settings.PANEL_LINE + 1}: under 4")
        under = ink_top("players_detail", "friends_detail", "actions_answer", "store_detail")
        if under is not None and under - last < 8 - TOLERANCE:
            say("gap", f"the last row ends {last:.1f} and the detail begins {under:.1f}: under 8")
    else:
        last = None
    detail_bottom = ink_bottom("players_detail", "friends_detail", "store_detail")
    footer_top = ink_top("players_banner", "friends_code", "players_count", "friends_count", "store_footer", "store_count")
    footer_bottom = ink_bottom("players_banner", "friends_code", "players_count", "friends_count", "store_footer", "store_count")
    if detail_bottom is not None and footer_top is not None and footer_top - detail_bottom < 8 - TOLERANCE:
        say("gap", f"the detail ends {detail_bottom:.1f} and the footer begins {footer_top:.1f}: under 8")
    if footer_bottom is not None and port_settings.PANEL_BOTTOM_LINE - footer_bottom < 8 - TOLERANCE:
        say("gap", f"the footer ends {footer_bottom:.1f}, the panel's bottom line is at {port_settings.PANEL_BOTTOM_LINE}: under 8")
    answer_bottom = ink_bottom("actions_answer", "add_answer")
    if answer_bottom is not None and port_settings.PANEL_BOTTOM_LINE - answer_bottom < 8 - TOLERANCE:
        say("gap", f"the answer ends {answer_bottom:.1f}, the panel's bottom line is at {port_settings.PANEL_BOTTOM_LINE}: under 8")
    buttons = [pill for pill in block_pills if "button" in pill["name"]]
    for button in buttons:
        if button["rect"][1] - port_settings.PANEL_BOTTOM_LINE < 3 - TOLERANCE:
            say("gap", f"{button['name']} begins {button['rect'][1]:.1f}, the panel's bottom line is at {port_settings.PANEL_BOTTOM_LINE}")
    # -- hud
    zones = [(x0 + BLOCK_LEFT, y0, x1 + BLOCK_LEFT, y1) for x0, y0, x1, y1 in screens_preview.HUD_ZONES]
    for record in records:
        for zone in zones:
            if intersects(record["ink"], zone):
                say("hud", f"{record['name']} {record['text']!r}: ink {block(record['ink'])} is in the HUD's bar {zone}")
    for pill in block_pills:
        for zone in zones:
            if intersects(pill["rect"], zone, 2 - TOLERANCE):
                say("hud", f"the pill {pill['name']} {block(pill['rect'])} is less than 2 from the HUD's bar")
    # -- columns
    columns = {}
    for record in records:
        row = row_of(record)
        if row is None or record["name"] in ("scr_friend_mark", "scr_action_text"):
            continue
        # (a right aligned text lines up on its right end, the others on their left)
        columns.setdefault((row.rsplit("_", 1)[0], record["name"]), []).append(record["ink"][2] if record.get("align") == "right" else record["ink"][0])
    for key, xs in columns.items():
        if max(xs) - min(xs) > 3.0:
            say("columns", f"{key[1]} lines up at x between {min(xs) - BLOCK_LEFT:.1f} and {max(xs) - BLOCK_LEFT:.1f} in its rows")
    for row in rows:
        cells = [record for record in records if row_of(record) == row["name"] and record["name"] != "scr_friend_mark"]
        if cells and max(record["ink"][2] for record in cells) > row["rect"][2] - 12 + TOLERANCE:
            say("columns", f"the row {row['name']}'s last text ends {max(record['ink'][2] for record in cells) - BLOCK_LEFT:.1f}, its pill's end is {row['rect'][2] - BLOCK_LEFT:.0f}: under 12")
    # -- contrast and legibility
    for record in records:
        ratio = contrast(record["color"])
        need = CONTRAST_LARGE if record["font"] == LARGE else CONTRAST_SMALL
        if ratio < need:
            say("contrast", f"{record['name']} {record['text']!r}: colour {record['color']} is {ratio:.1f} to 1 on the panel's brightest background, under {need}")
        if record["font"] != LARGE and (record["caps"][1] - record["caps"][0]) * scale < 9.5:
            say("legible", f"{record['name']}: capitals are {(record['caps'][1] - record['caps'][0]) * scale:.1f} pixels high")
    # -- the empty list and the long list
    if data is not None and "rows" in data and "count" in data and data.get("scenario", "").startswith(("players", "friends", "requests", "party", "blocked", "store")):
        empty_texts = [record for record in records if record["name"].endswith("_empty")]
        counters = [record for record in records if record["name"].endswith("_count")]
        if data["count"] == 0 or data.get("empty"):
            if not empty_texts:
                say("empty", "the list has no rows and no words for it")
            for record in empty_texts:
                x0, y0, x1, y1 = record["ink"]
                if y0 < port_settings.SOCIAL_ROWS_TOP or y1 > port_settings.SOCIAL_LIST_BOTTOM:
                    say("empty", f"the words of the empty list are at y {y0:.1f} to {y1:.1f}, not in the list's place")
            if rows:
                say("empty", "an empty list has rows")
            if counters:
                say("empty", "an empty list has a counter")
        elif data["count"] > data["shown"] and not counters:
            say("empty", "a list longer than its rows has no counter")
        if data.get("count", 0) and empty_texts:
            say("empty", "a list with rows has the empty words too")
    return out
