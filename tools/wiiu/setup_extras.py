"""Small helpers for the computer setup: finding the SD card, checking it, tidying a Mac's leftovers, and asking for paths in a terminal."""
import os
import plistlib
import shlex
import shutil
import string
import subprocess
import sys
from pathlib import Path

from disc_image import RETAIL_MAPS, SetupError

# What the converted maps need on the card (they total about 1.7 GB), and what the game adds on its first start (about 0.8 GB of cache files).
MAPS_BYTES = 2 * 1024**3
GAME_BYTES = 1024**3


def _mounts():
    """every place a removable card is usually mounted, as Paths"""
    found = []
    if sys.platform == "darwin":
        found += sorted(Path("/Volumes").glob("*"))
    elif sys.platform.startswith("linux"):
        user = os.environ.get("USER", "")
        for base in (f"/media/{user}", f"/run/media/{user}", "/media", "/mnt"):
            found += sorted(Path(base).glob("*")) if Path(base).is_dir() else []
    elif os.name == "nt":
        for letter in string.ascii_uppercase:
            drive = Path(f"{letter}:/")
            if letter not in "C" and drive.exists():
                found.append(drive)
    return [path for path in found if path.is_dir()]


def sd_card_candidates():
    """mounted volumes that look like a Wii U SD card: they hold a `wiiu` folder, or (when none does) a small FAT volume with nothing else on it"""
    with_wiiu = [path for path in _mounts() if (path / "wiiu").is_dir()]
    return with_wiiu


def filesystem_name(card):
    """the card's file system as the computer names it ('MS-DOS FAT32', 'ExFAT', ...), or None where that cannot be told"""
    if sys.platform != "darwin":
        return None
    try:
        out = subprocess.run(["diskutil", "info", "-plist", str(card)], capture_output=True, timeout=10, check=True).stdout
        info = plistlib.loads(out)
        return info.get("FilesystemName") or info.get("FilesystemType")
    except (OSError, subprocess.SubprocessError, plistlib.InvalidFileException, ValueError):
        return None


def filesystem_warning(card):
    name = filesystem_name(card)
    if name and "fat32" not in name.lower().replace(" ", "") and name.lower() not in ("msdos", "fat32", "ms-dos fat32"):
        return (f"This card is formatted as {name}. The Wii U reads FAT32 most reliably: if the game does not see its files, "
                "back the card up and format it as MS-DOS (FAT) in Disk Utility, with the MBR scheme.")
    return None


def free_space(card):
    return shutil.disk_usage(card).free


def space_problem(card, needed=MAPS_BYTES):
    free = free_space(card)
    if free < needed:
        return (f"The SD card has {free / 1024**3:.1f} GB free; the maps need about {needed / 1024**3:.0f} GB and the game "
                f"adds about {GAME_BYTES / 1024**3:.0f} GB when it first starts. Free some space or use a larger card.")
    return None


def clean_metadata(folder):
    """remove what a Mac leaves on a card (._* files beside each file, .DS_Store) inside folder; nothing else is touched; returns how many were removed"""
    removed = 0
    folder = Path(folder)
    if not folder.is_dir():
        return 0
    for path in folder.rglob("*"):
        if path.is_file() and not path.is_symlink() and (path.name == ".DS_Store" or path.name.startswith("._")):
            try:
                path.unlink()
                removed += 1
            except OSError:
                pass
    return removed


def check_card(card):
    """what is on the card, as [(ok, text)]: the app, its shader pack, its broker list, the 24 maps, and any Mac leftovers"""
    card = Path(card)
    app = card / "wiiu/apps/halo"
    lines = []

    def add(ok, text):
        lines.append((ok, text))

    add(card.is_dir(), f"SD card folder: {card}")
    add((app / "halo.wuhb").is_file(), "the game (wiiu/apps/halo/halo.wuhb)" if (app / "halo.wuhb").is_file()
        else "the game is missing: put halo.wuhb from the release in wiiu/apps/halo/")
    add((app / "shaders.pack").is_file(), "graphics shaders (shaders.pack)" if (app / "shaders.pack").is_file()
        else "shaders.pack is missing: it comes in the same release as halo.wuhb, and the two belong together")
    add((app / "brokers.txt").is_file(), "server list (brokers.txt)" if (app / "brokers.txt").is_file()
        else "brokers.txt is missing (optional: only internet games need it)")
    maps = app / "data/maps"
    have = {p.name.lower() for p in maps.glob("*") if p.is_file()} if maps.is_dir() else set()
    missing = [name for name in RETAIL_MAPS if name not in have]
    add(not missing, "all 24 maps are in place" if not missing else
        ("no maps yet: run the setup with your Xbox disc image" if not have else f"{len(missing)} maps are missing: " + ", ".join(missing[:6]) + (" ..." if len(missing) > 6 else "")))
    junk = [p for p in app.rglob("*") if p.is_file() and (p.name == ".DS_Store" or p.name.startswith("._"))] if app.is_dir() else []
    add(not junk, "no Mac leftover files" if not junk else f"{len(junk)} Mac leftover files (._ and .DS_Store): the setup removes them, or run: dot_clean \"{app}\"")
    note = filesystem_warning(card)
    if note:
        add(False, note)
    return lines


def print_check(card):
    if not Path(card).is_dir():
        print(f"  FIX  {card} is not a folder. Plug in the SD card and give its top folder (on a Mac: /Volumes/NAME).")
        return False
    ok_all = True
    for ok, text in check_card(card):
        print(("  OK   " if ok else "  FIX  ") + text)
        ok_all = ok_all and ok
    return ok_all


def clean_path(text):
    """a path as typed or dragged into a terminal: surrounding quotes and backslash-escaped spaces removed, ~ expanded"""
    text = text.strip()
    if not text:
        return ""
    try:
        parts = shlex.split(text)
    except ValueError:
        parts = [text.strip("'\"")]
    return os.path.expanduser(parts[0]) if parts else ""


def ask_path(question, must_exist=True):
    while True:
        answer = clean_path(input(question + " "))
        if not answer:
            continue
        if must_exist and not Path(answer).exists():
            print("  That path does not exist. Try again (you can drag the file or folder into this window).")
            continue
        return answer


# ---------- the app files that come in the release download (a folder called app next to setup_wiiu.py)

ROOT = Path(__file__).resolve().parent.parent.parent
APP_FILES = ("halo.wuhb", "shaders.pack", "brokers.txt")
# (halo.wuhb and shaders.pack belong together, so both are always replaced; brokers.txt may have been edited, so an existing one stays)
REPLACED = ("halo.wuhb", "shaders.pack")


def bundle_dir():
    """the folder holding the release's app files, or None when the helper was not unzipped from a release (a source checkout)"""
    folder = ROOT / "app"
    return folder if (folder / "halo.wuhb").is_file() and (folder / "shaders.pack").is_file() else None


def maps_complete(card):
    maps = Path(card) / "wiiu/apps/halo/data/maps"
    if not maps.is_dir():
        return False
    have = {p.name.lower() for p in maps.iterdir() if p.is_file()}
    return all(name in have for name in RETAIL_MAPS)


def install_app(card, progress=lambda *_: None):
    """put the release's halo.wuhb, shaders.pack and brokers.txt into sd:/wiiu/apps/halo/. A halo.wuhb or shaders.pack that is there and
    differs is kept beside the new one as NAME.backup. Returns the names written."""
    source = bundle_dir()
    if source is None:
        return []
    card = Path(card)
    for part in (card / "wiiu", card / "wiiu/apps", card / "wiiu/apps/halo"):
        if part.is_symlink():
            raise SetupError("The SD card's Halo folders must not be symbolic links.")
    app = card / "wiiu/apps/halo"
    app.mkdir(parents=True, exist_ok=True)
    written = []
    for name in APP_FILES:
        origin, target = source / name, app / name
        if not origin.is_file():
            continue
        if target.exists() and name not in REPLACED:
            continue
        if target.exists() and target.read_bytes() == origin.read_bytes():
            continue
        progress(f"Copying {name}", 0, 1)
        if target.exists():
            shutil.copyfile(target, app / (name + ".backup"))
        temporary = app / (name + ".new")
        shutil.copyfile(origin, temporary)
        os.replace(temporary, target)
        written.append(name)
    return written
