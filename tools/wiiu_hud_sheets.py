#!/usr/bin/env python3
"""Smaller copies of the high-res HUD's largest sheets for the Wii U (port/assets/wiiu), which tools/embed_assets.py --wiiu embeds in their place:

    python3 tools/wiiu_hud_sheets.py build      # writes port/assets/wiiu/*.png and sheets.json from port/assets/hud
    python3 tools/wiiu_hud_sheets.py check      # the committed copies are what build would write, from the sources as they are now

Each 2048 x 2048 sheet is 22 MB with its mip levels and about 0.95 s of the game thread when the HUD first draws it (PNG decode, mip levels, copies:
xbox_textures_gx2.c, hud_image), the console's cost being the pixels, not the file. Two of the sheet kinds can be made smaller without changing what is drawn:

crop: the meters' and damage arrows' sheets hold their sprites in the top third and are empty below. The copy keeps the rows from the top down to the last
    row any sprite of the sheet's layout reaches or any texel with red, green or alpha is set, plus one empty row, rounded up to a multiple of 64. The width and
    the rows kept are exactly the sheet's (nothing is resampled); below the copy the edge row is repeated by the sampler's clamp, and it is a row with no red,
    green or alpha. The game draws the copy with its v coordinate multiplied by (the sheet's height / the rows kept): port/wiiu/src/wiiu_hud_sheet.h.
    (The two Xbox-order meter sheets, hud_ammo_meters__0 and hud_unit_meters__0, have blue left in rows below their sprites, with alpha 0 and no red or
    green: no sprite of the layout reaches them, the meter shader multiplies by alpha, and they are not kept.)
shrink: the scope masks are drawn over the whole screen, minified, so the sampler used their second mip level or a mix of the first two. The copy is the sheet's
    first mip level (the mean of each 2 by 2, rounded to nearest, as wiiu_png_next_level makes it), half the size both ways. The same v coordinate suits it.

The copies are committed (the Wii U's build has no NumPy or Pillow) with the SHA-256 of the sheet each was made from; tools/embed_assets.py --wiiu stops if a
source has changed since.

Needs Pillow and NumPy.
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
HUD_ASSETS = ROOT / "port/assets/hud"
WIIU_ASSETS = ROOT / "port/assets/wiiu"
SHEETS_JSON = WIIU_ASSETS / "sheets.json"
LAYOUTS = (HUD_ASSETS / "layout.json", HUD_ASSETS / "ce_layout.json")
# the sheets made smaller, and how
SHEETS: Dict[str, str] = {
    "hud_ammo_meters__0": "crop",
    "hud_unit_meters__0": "crop",
    "hud_damage_arrows__0": "crop",
    "ce_hud_ammo_meters": "crop",
    "ce_hud_unit_meters": "crop",
    "pistol__pistol_scope_mask__0": "shrink",
    "pistol__pistol_scope_mask_split__0": "shrink",
    "sniper__sniper_scope_mask_sm__0": "shrink",
}
# a copy's height is a multiple of this (a pitch the GPU's tiling and the mip levels like)
ROW_MULTIPLE = 64


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def used_rows(rgba: np.ndarray) -> int:
    """How many rows from the top hold a texel with red, green or alpha set (0 for none)."""
    marked = (rgba[:, :, 0] != 0) | (rgba[:, :, 1] != 0) | (rgba[:, :, 3] != 0)
    rows = np.flatnonzero(marked.any(axis=1))
    return int(rows[-1]) + 1 if len(rows) else 0


def edges_exact(height: int, kept: int) -> bool:
    """Whether the pixel shader finds the row a texel's top edge is in: v = y / height (exact in single precision) times the scale (height / kept, rounded to single
    precision, as wiiu_hud_sheet_v_scale and the uniform hold it) times kept, floored, is y for every row of the copy. A sprite's edge falls on such a v, and the
    meters' thresholds are read by texelFetch at the floor (nv2a_psh.c). Most heights do; a few near the sheet's own (1600 of 2048) round the scale down."""
    f32 = np.float32
    scale = f32(height * height / (kept * height))
    rows = np.arange(kept, dtype=np.float32)
    edge = rows / f32(height)
    return bool(np.array_equal(np.floor((edge * scale) * f32(kept)), rows))


def crop_rows(rgba: np.ndarray, sprite_rows: int = 0, multiple: int = ROW_MULTIPLE) -> int:
    """The rows a sheet's copy keeps: the sprites' last row or the last used row, whichever is lower down, and one row more, up to a multiple (the next multiple
    where the edges of the rows would not be found exactly: edges_exact); the sheet's own height if that is no less than it."""
    height = rgba.shape[0]
    end = max(used_rows(rgba), sprite_rows) + 1
    kept = -(-end // multiple) * multiple
    while kept < height and not edges_exact(height, kept):
        kept += multiple
    return min(kept, height)


def crop(rgba: np.ndarray, rows: int) -> np.ndarray:
    return np.ascontiguousarray(rgba[:rows])


def shrink(rgba: np.ndarray) -> np.ndarray:
    """The sheet's first mip level: each texel the mean of the 2 by 2 above it, plus 2, over 4, per channel (wiiu_png_mean4); the sheet's sides are even."""
    height, width = rgba.shape[:2]
    if height % 2 or width % 2:
        raise ValueError("a sheet to shrink has even sides")
    wide = rgba.astype(np.uint16)
    total = wide[0::2, 0::2] + wide[0::2, 1::2] + wide[1::2, 0::2] + wide[1::2, 1::2] + 2
    return (total >> 2).astype(np.uint8)


def sprite_rows(name: str, layout_assets: Dict[str, Any]) -> int:
    """The last row (in the sheet's pixels) of the Xbox bitmap's sprite rectangles in the layout, 0 if the layout does not give them (cells with an 'xbox'
    rectangle: [left, top, right, bottom] in the bitmap's texels)."""
    asset = layout_assets.get(name)
    if not asset:
        return 0
    bottoms = [cell["xbox"][3] for cell in asset.get("cells", []) if "xbox" in cell]
    return max(bottoms, default=0) * int(asset["scale"])


def layout_assets_load() -> Dict[str, Any]:
    result: Dict[str, Any] = {}
    for listing in LAYOUTS:
        if listing.is_file():
            for asset in json.loads(listing.read_text())["assets"]:
                result[asset["name"]] = asset
    return result


def load_rgba(path: Path) -> np.ndarray:
    return np.asarray(Image.open(path).convert("RGBA"))


def make_copy(name: str, how: str, rgba: np.ndarray, layout_assets: Dict[str, Any]) -> Tuple[np.ndarray, Dict[str, Any]]:
    """A sheet's copy and what to say of it: (pixels, entry)."""
    height, width = rgba.shape[:2]
    entry: Dict[str, Any] = {"name": name, "how": how, "source_width": width, "source_height": height}
    if how == "crop":
        rows = crop_rows(rgba, sprite_rows(name, layout_assets))
        copy = crop(rgba, rows)
        # (what is left out: no red, green or alpha, and below every sprite; the blue left there is not kept)
        if rows < height:
            dropped = rgba[rows:]
            if used_rows(np.ascontiguousarray(dropped)) != 0:
                raise ValueError(f"{name}: rows below the {rows} kept still have red, green or alpha")
            entry["blue_only_texels_dropped"] = int((dropped[:, :, 2] != 0).sum())
    elif how == "shrink":
        copy = shrink(rgba)
    else:
        raise ValueError(f"{name}: unknown way {how}")
    entry["width"] = int(copy.shape[1])
    entry["height"] = int(copy.shape[0])
    return copy, entry


def save_png(path: Path, rgba: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(rgba, "RGBA").save(path, format="PNG", optimize=True)


def build(write: bool) -> List[str]:
    """Makes every copy; writes it if write, and returns the differences from what is committed otherwise (an empty list: it is current)."""
    layout_assets = layout_assets_load()
    entries = []
    problems: List[str] = []
    for name, how in SHEETS.items():
        source = HUD_ASSETS / f"{name}.png"
        copy, entry = make_copy(name, how, load_rgba(source), layout_assets)
        entry["source_sha256"] = sha256(source)
        entries.append(entry)
        target = WIIU_ASSETS / f"{name}.png"
        if write:
            save_png(target, copy)
        elif not target.is_file():
            problems.append(f"{name}: {target.relative_to(ROOT)} is missing")
        elif not np.array_equal(load_rgba(target), copy):
            problems.append(f"{name}: {target.relative_to(ROOT)} is not what the source makes")
    text = json.dumps({"comment": "Written by tools/wiiu_hud_sheets.py: smaller copies of the high-res HUD's largest sheets for the Wii U (tools/embed_assets.py --wiiu).",
                       "assets": entries}, indent=1) + "\n"
    if write:
        SHEETS_JSON.parent.mkdir(parents=True, exist_ok=True)
        SHEETS_JSON.write_text(text)
    elif not SHEETS_JSON.is_file() or SHEETS_JSON.read_text() != text:
        problems.append(f"{SHEETS_JSON.relative_to(ROOT)} is not what the sources make")
    return problems


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("command", choices=("build", "check"))
    arguments = parser.parse_args(argv)
    problems = build(arguments.command == "build")
    for problem in problems:
        print(problem, file=sys.stderr)
    if arguments.command == "build":
        for entry in json.loads(SHEETS_JSON.read_text())["assets"]:
            print(f"{entry['name']}: {entry['source_width']}x{entry['source_height']} -> {entry['width']}x{entry['height']} ({entry['how']})")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
