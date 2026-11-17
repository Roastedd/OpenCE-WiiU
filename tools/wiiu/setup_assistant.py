"""Prepare an SD card's maps locally from the user's Xbox Halo disc image."""
import argparse
import os
import queue
import shutil
import struct
import threading
import tempfile
import zlib
from contextlib import nullcontext
from pathlib import Path

import convert_maps
import setup_extras
from disc_image import DiscImage, RETAIL_MAPS, SetupError

class NoWindow(SetupError):
    """the computer cannot show the setup window (no Tk): the terminal questions are used instead"""


FAT32_FILE_LIMIT = 2**32
ISO_PART_BYTES = 1024**3


def prepare_maps(source, sd_card, progress=lambda *_: None):
    source, sd_card = Path(source).expanduser().resolve(), Path(sd_card).expanduser().resolve()
    if not sd_card.is_dir():
        raise SetupError("Choose the SD card's existing root folder.")
    if not source.exists():
        raise SetupError("Choose your original-Xbox Halo ISO/XISO or extracted maps folder.")
    target = sd_card / "wiiu/apps/halo/data/maps"
    for parent in reversed((target, *target.parents)):
        if parent == sd_card or sd_card in parent.parents:
            if parent.is_symlink():
                raise SetupError("The SD card's Halo folders must not be symbolic links.")
    if target.exists():
        raise SetupError("data/maps already exists. Keep your current installation, or back it up and move that folder aside first.")
    problem = setup_extras.space_problem(sd_card)
    if problem:
        raise SetupError(problem)
    context = nullcontext(None) if source.is_dir() else DiscImage(source)
    with context as disc:
        if disc:
            entries = disc.maps()
        else:
            entries = {p.name.lower(): p for p in source.iterdir() if p.is_file()}
            missing = sorted(set(RETAIL_MAPS) - entries.keys())
            if missing:
                raise SetupError("The Xbox maps folder is incomplete. Missing: " + ", ".join(missing))
        # The source has been validated before any output directory is created.
        target.parent.mkdir(parents=True, exist_ok=True)
        converter = convert_maps.converter()
        with tempfile.TemporaryDirectory(prefix="iso-import-", dir=target.parent) as temporary:
            staging = Path(temporary)
            ready = staging / "ready"
            ready.mkdir()
            for number, name in enumerate(RETAIL_MAPS, 1):
                progress(f"Reading {name}", number - 1, len(RETAIL_MAPS))
                raw_path = staging / "source.map"
                if disc:
                    disc.copy(entries[name], raw_path,
                              lambda *_: progress(f"Reading {name}", number - 1, len(RETAIL_MAPS)))
                else:
                    shutil.copyfile(entries[name], raw_path)
                raw = raw_path.read_bytes()
                if len(raw) < 0x800:
                    raise SetupError(f"{name} is incomplete.")
                signature, version, length = struct.unpack_from("<III", raw)
                if signature != 0x68656164 or version != 5 or not 0x800 <= length <= 384 * 1024 * 1024:
                    raise SetupError(f"{name} is not a supported original-Xbox map.")
                if len(raw) == length:
                    data = bytearray(raw)
                else:
                    inflater = zlib.decompressobj()
                    body = inflater.decompress(raw[0x800:], length - 0x800 + 1)
                    if len(body) != length - 0x800 or not inflater.eof or len(inflater.unused_data) >= 0x800 or any(inflater.unused_data):
                        raise SetupError(f"{name} is incomplete or damaged.")
                    data = bytearray(raw[:0x800] + body)
                del raw
                progress(f"Converting {name}", number - 1, len(RETAIL_MAPS))
                try:
                    converted = converter.convert(data, rebase=convert_maps.WIIU_REBASE)
                except convert_maps.map_convert.Refused as error:
                    raise SetupError(f"{name}: {error}") from error
                output = ready / name
                with output.open("wb") as file:
                    file.write(converted.data[:0x800])
                    compressor = zlib.compressobj(6)
                    for at in range(0x800, len(converted.data), 65536):
                        progress(f"Writing {name}", number - 1, len(RETAIL_MAPS))
                        file.write(compressor.compress(memoryview(converted.data)[at:at + 65536]))
                    file.write(compressor.flush())
                    file.flush()
                    os.fsync(file.fileno())
                del data, converted
                raw_path.unlink()
                progress(f"Prepared {name}", number, len(RETAIL_MAPS))
            # Optional disc-provided loading art is copied without modification.
            loading = entries.get("loading.tga")
            if loading and (loading.size if disc else loading.stat().st_size) <= 1024 * 1024:
                if disc:
                    disc.copy(loading, ready / "loading.tga")
                else:
                    shutil.copyfile(loading, ready / "loading.tga")
            progress("Installing maps", len(RETAIL_MAPS), len(RETAIL_MAPS))
            if target.exists():
                raise SetupError("data/maps appeared during setup. It was left untouched.")
            ready.rename(target)
    setup_extras.clean_metadata(sd_card / "wiiu/apps/halo")
    note = setup_extras.filesystem_warning(sd_card)
    progress("Maps ready. Eject the SD card safely, then start OpenCE-U." + (" Note: " + note if note else ""), len(RETAIL_MAPS), len(RETAIL_MAPS))
    return target


def stage_image(source, sd_card, progress=lambda *_: None):
    """Copy a validated disc for console setup, splitting large images for FAT32."""
    source, card = Path(source).expanduser().resolve(), Path(sd_card).expanduser().resolve()
    if not card.is_dir():
        raise SetupError("Choose the SD card's existing root folder.")
    app = card / "wiiu/apps/halo"
    for part in (card / "wiiu", card / "wiiu/apps", app):
        if part.is_symlink():
            raise SetupError("The SD card's Halo folders must not be symbolic links.")
    with DiscImage(source) as image:
        image.maps()
        total = image.ends[-1]
        part_size = ISO_PART_BYTES if total >= FAT32_FILE_LIMIT else total
        count = (total + part_size - 1) // part_size
        if count > 16:
            raise SetupError("The image is too large for the supported 16-part format.")
        names = ["halo.iso"] if count == 1 else [f"halo.iso.{i}" for i in range(count)]
        if app.exists() and any(app.glob("halo.iso*")):
            raise SetupError("A halo.iso image or parts already exist on this SD card. They were left untouched.")
        if (app / "halo.xiso").exists():
            raise SetupError("halo.xiso already exists on this SD card. It was left untouched.")
        if shutil.disk_usage(card).free < total + 1024 * 1024:
            raise SetupError("There is not enough space to copy this ISO to the SD card.")
        app.mkdir(parents=True, exist_ok=True)
        published = []
        try:
            with tempfile.TemporaryDirectory(prefix="iso-copy-", dir=app) as temporary:
                staging = Path(temporary)
                copied = 0
                for number, name in enumerate(names):
                    size = min(part_size, total - copied)
                    with (staging / name).open("wb") as output:
                        for at in range(0, size, 1024 * 1024):
                            progress(f"Copying ISO part {number + 1}/{count}", copied + at, total)
                            output.write(image.read(copied + at, min(1024 * 1024, size - at)))
                        output.flush()
                        os.fsync(output.fileno())
                    copied += size
                progress("Installing ISO files", total, total)
                # Publish part zero last, so an interrupted copy is not offered for import.
                for name in reversed(names):
                    destination = app / name
                    if destination.exists():
                        raise SetupError("An ISO appeared on the card during setup. It was left untouched.")
                    (staging / name).rename(destination)
                    published.append(destination)
        except BaseException:
            for destination in published:
                destination.unlink(missing_ok=True)
            raise
    progress("ISO ready. Eject the SD card, start the importer build, and press A.", total, total)
    return app / names[0]


def graphical_setup(initial="", console=False):
    try:
        import tkinter as tk
        from tkinter import filedialog, messagebox, ttk
    except ImportError as error:
        raise NoWindow("This Python has no Tk window support.") from error
    try:
        root = tk.Tk()
    except tk.TclError as error:
        raise NoWindow("Cannot open a setup window.") from error
    root.title("OpenCE-WiiU setup")
    root.geometry("640x390")
    root.minsize(640, 390)
    frame = ttk.Frame(root, padding=24)
    frame.pack(fill="both", expand=True)
    found = setup_extras.sd_card_candidates()
    source, card, status = tk.StringVar(value=initial), tk.StringVar(value=str(found[0]) if len(found) == 1 else ""), tk.StringVar(value="Your game stays on this computer and SD card. Nothing is uploaded.")
    messages, cancel = queue.Queue(), threading.Event()
    mode = tk.StringVar(value="console" if console else "maps")
    running = False
    ttk.Label(frame, text="Set up Halo for Wii U", font=("Helvetica", 19, "bold")).grid(row=0, column=0, columnspan=3, sticky="w", pady=(0, 18))
    def choose_iso():
        path = filedialog.askopenfilename(title="Your original-Xbox Halo disc image", filetypes=[("Xbox disc images", "*.iso *.xiso *.iso.0 *.xiso.0"), ("All files", "*")])
        if path:
            source.set(path)
    def choose_card():
        path = filedialog.askdirectory(title="Choose the SD card's root")
        if path:
            card.set(path)
    ttk.Label(frame, text="Xbox ISO").grid(row=1, column=0, sticky="w")
    ttk.Entry(frame, textvariable=source).grid(row=1, column=1, sticky="ew", padx=10, pady=8)
    iso_button = ttk.Button(frame, text="Choose…", command=choose_iso)
    iso_button.grid(row=1, column=2)
    ttk.Label(frame, text="SD card").grid(row=2, column=0, sticky="w")
    ttk.Entry(frame, textvariable=card).grid(row=2, column=1, sticky="ew", padx=10, pady=8)
    card_button = ttk.Button(frame, text="Choose…", command=choose_card)
    card_button.grid(row=2, column=2)
    options = ttk.Frame(frame)
    options.grid(row=3, column=0, columnspan=3, sticky="w", pady=(10, 0))
    ttk.Radiobutton(options, text="Convert maps on this computer", variable=mode, value="maps").pack(anchor="w")
    ttk.Radiobutton(options, text="Copy ISO for import on Wii U", variable=mode, value="console").pack(anchor="w")
    bar = ttk.Progressbar(frame, maximum=len(RETAIL_MAPS))
    bar.grid(row=4, column=0, columnspan=3, sticky="ew", pady=(18, 10))
    ttk.Label(frame, textvariable=status, wraplength=580).grid(row=5, column=0, columnspan=3, sticky="w")
    def update(text, done, total):
        if cancel.is_set() and not text.startswith(("Maps ready.", "ISO ready.")):
            raise SetupError("Setup cancelled. Existing maps and saves were left untouched.")
        messages.put(("progress", text, done, total))
    def work(chosen_source, chosen_card, chosen_mode):
        try:
            operation = stage_image if chosen_mode == "console" else prepare_maps
            operation(chosen_source, chosen_card, update)
            messages.put(("done", "Ready. Eject the SD card safely. Console import needs the new importer build." if chosen_mode == "console" else "Maps are ready. Eject the SD card safely.", 1, 1))
        except Exception as error:
            messages.put(("error", str(error), 0, 1))
    def start():
        nonlocal running
        if not source.get() or not card.get():
            messagebox.showinfo("Choose your files", "Choose your Xbox ISO and the SD card's root folder first.")
            return
        running = True
        cancel.clear()
        for button in (start_button, iso_button, card_button):
            button.state(["disabled"])
        status.set("Checking the disc image…")
        threading.Thread(target=work, args=(source.get(), card.get(), mode.get()), daemon=True).start()
    def close():
        if running:
            cancel.set()
            status.set("Stopping after the current conversion. Keep the SD card connected.")
        else:
            root.destroy()
    def poll():
        nonlocal running
        try:
            while True:
                kind, text, done, total = messages.get_nowait()
                status.set(text)
                bar["maximum"] = total
                bar["value"] = done
                if kind in ("done", "error"):
                    running = False
                    for button in (start_button, iso_button, card_button):
                        button.state(["!disabled"])
                    if kind == "error":
                        messagebox.showerror("Setup stopped", text)
        except queue.Empty:
            pass
        root.after(100, poll)
    start_button = ttk.Button(frame, text="Prepare SD card", command=start)
    start_button.grid(row=6, column=1, sticky="e", pady=18)
    ttk.Button(frame, text="Cancel / Close", command=close).grid(row=6, column=2, padx=(10, 0))
    frame.columnconfigure(1, weight=1)
    root.protocol("WM_DELETE_WINDOW", close)
    root.after(100, poll)
    root.mainloop()


def text_setup(initial="", console=False):
    """the same setup by questions in a terminal, for a computer that cannot show the window (or when the window is not wanted)"""
    print("\nOpenCE-WiiU setup\nYour game stays on this computer and the SD card. Nothing is uploaded.\n")
    source = initial or setup_extras.ask_path("Drag your Xbox Halo ISO (or its extracted maps folder) into this window and press Return:")
    found = setup_extras.sd_card_candidates()
    if len(found) == 1:
        answer = input(f"Found an SD card at {found[0]}. Use it? [Y/n] ").strip().lower()
        card = str(found[0]) if answer in ("", "y", "yes") else setup_extras.ask_path("Drag the SD card's folder into this window and press Return:")
    else:
        card = setup_extras.ask_path("Drag the SD card (its top folder) into this window and press Return:")
    (stage_image if console else prepare_maps)(source, card, lambda text, *_: print(text, flush=True) if not text.startswith(("Reading", "Writing")) else None)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?", help="your Xbox ISO/XISO, its .0 part, or an extracted maps folder")
    parser.add_argument("--copy-iso", action="store_true", help="copy/split the ISO for the Wii U importer instead of converting here")
    parser.add_argument("--sd-card", type=Path, help="SD card root; with a source, runs without a window (found by itself when only one is plugged in)")
    parser.add_argument("--check", action="store_true", help="look at the SD card and say what is there and what is missing; changes nothing")
    parser.add_argument("--text", action="store_true", help="ask in the terminal instead of opening the window")
    args = parser.parse_args(argv)
    try:
        if args.check:
            card = args.sd_card
            if card is None:
                found = setup_extras.sd_card_candidates()
                if len(found) != 1:
                    raise SetupError("Say which card to look at: python3 setup_wiiu.py --check --sd-card PATH" +
                                     (" (found: " + ", ".join(str(p) for p in found) + ")" if found else " (no Wii U SD card was found)"))
                card = found[0]
            return 0 if setup_extras.print_check(card) else 2
        card = args.sd_card
        if args.source and card is None and not args.text:
            found = setup_extras.sd_card_candidates()
            if len(found) == 1:
                card = found[0]
                print(f"Using the SD card at {card}", flush=True)
        if card:
            if not args.source:
                parser.error("give an ISO or maps folder with --sd-card")
            (stage_image if args.copy_iso else prepare_maps)(args.source, card, lambda text, *_: print(text, flush=True) if not text.startswith(("Reading", "Writing")) else None)
        elif args.text:
            text_setup(args.source or "", args.copy_iso)
        else:
            try:
                graphical_setup(args.source or "", args.copy_iso)
            except NoWindow as error:
                print(f"{error} Switching to questions in this window instead.")
                text_setup(args.source or "", args.copy_iso)
    except (SetupError, OSError, zlib.error, KeyboardInterrupt) as error:
        print(f"Setup stopped: {error}")
        return 1
    return 0
