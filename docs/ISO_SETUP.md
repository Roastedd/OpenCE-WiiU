# Two ways to set up your Xbox copy of Halo

The original rc1 executable needs preconverted maps. The **new importer
build** adds first-time setup on Wii U. It has built successfully and its
converter has passed host tests, but its setup screen, HOME handling, and
first boot still need a console trial. Keep your working rc1 installation
as a fallback while testing.

Both methods use your own Halo: Combat Evolved disc image for the original
Xbox. Halo PC, Custom Edition, Anniversary and MCC disc images are different
formats. No game data is supplied or uploaded by either method.

## Option 1: let the Wii U prepare the maps

1. Put the **importer build's** `halo.wuhb`, its matching `shaders.pack`,
   and `brokers.txt` in `sd:/wiiu/apps/halo/`.
2. Copy your Xbox Halo image into that folder and name it **`halo.iso`**
   (`halo.xiso` works too).
3. Start **OpenCE-U**. On a first installation it shows setup instructions.
   Press **A on the GamePad** to extract and convert the maps. Progress is
   shown on both screens. This can take several minutes on an SD card.
4. When all 24 maps are ready, the game starts. You can remove the ISO
   afterward to recover space. Later launches use the prepared maps directly.

Allow **at least 5 GiB free after copying the ISO** for temporary maps and
caches. A 16 GB or larger card is a practical choice for a full-disc image.
Existing saves are kept. An existing `data/maps` folder is never replaced;
if it is incomplete or contains unconverted maps, setup asks you to back it
up and move it aside first.

### Images larger than 4 GiB

A FAT32 SD card cannot hold a single file of 4 GiB or more. The computer
helper below can copy the ISO in parts automatically: choose **Copy ISO for
import on Wii U**. It does not convert the maps in this mode.

The console accepts consecutive parts named `halo.iso.0`, `halo.iso.1`,
`halo.iso.2`, and so on, up to 16 parts. Copy every part. Select the `.0`
file when using an already split image in the helper.

### Cancelling or retrying

Press **B** to cancel and return to the Wii U Menu. Setup finishes the
current conversion before closing. It writes only to its marked temporary
folder, `data/iso-import`, and installs `data/maps` once the whole set is
complete. A failed or interrupted import can be retried at the next start;
it starts preparation again. The original ISO and existing saves are kept.

The last setup messages are in `wiiu/apps/halo/import-log.txt`. If setup
stops, send that file with an issue report after reviewing it for personal
information. If the temporary folder lacks its ownership marker, setup
refuses to clear it automatically.

## Option 2: prepare the SD card on a computer

If you downloaded the release zip (`opence-wiiu.zip`), it already holds the game files in its `app` folder. The setup program copies `halo.wuhb`, `shaders.pack` and `brokers.txt` to `sd:/wiiu/apps/halo/` for you (an older `halo.wuhb` or `shaders.pack` is kept beside the new one as `.backup`, and an existing `brokers.txt` is left alone), then converts your maps. On a card that already has all its maps, run it again with the new zip and leave the ISO empty to update the game only.

Install **Python 3.10 or later** from [python.org](https://www.python.org/downloads/).
No separate extractor or Python packages are required. The window uses Tk,
included with the standard Windows/macOS Python installers; Linux may need
its distribution's Python Tk package. The command-line method works without Tk.

1. Extract the setup-helper zip into a folder on your computer.
2. On Windows, open **`Setup Wii U.bat`**. On macOS, open
   **`Setup Wii U.command`**. You can also run `python3 setup_wiiu.py`.
3. Choose your Xbox ISO and the **root of the SD card**.
4. Choose **Convert maps on this computer**, then **Prepare SD card**.
5. When setup finishes, eject the card safely and start OpenCE-U.

This writes converted maps to `wiiu/apps/halo/data/maps/`, leaving the app,
settings, and saves intact. Existing maps are never overwritten. The app,
shader pack, and broker list come from the game release; the helper does
not download or install an executable. These converted maps work with rc1.

The helper also does these small things for you:

- **Finds the SD card** by itself when one Wii U card (one with a `wiiu` folder)
  is plugged in, and fills it in. Otherwise you choose the folder.
- **Checks there is room** (about 2 GB for the maps, plus about 1 GB the game adds
  on its first start) before it starts, and says so plainly if there is not.
- **Cleans up after a Mac**: it removes the `._` files and `.DS_Store` that macOS
  scatters across a card, inside `wiiu/apps/halo/` only.
- **Falls back to questions in the terminal** when the setup window cannot open
  (for example a Python without Tk). Drag the ISO and the card into the Terminal
  window to fill in their paths. `--text` asks for this on purpose.
- **Has a check mode**: `python3 setup_wiiu.py --check` (add `--sd-card PATH` to
  name the card) lists what is on the card and what is missing, and changes
  nothing.

For a computer without a setup window:

```sh
python3 setup_wiiu.py "/path/to/Halo.iso" --sd-card "/path/to/SD card"
```

An already extracted Xbox `maps` folder can be used in place of the ISO.
To copy the ISO for **console** conversion instead:

```sh
python3 setup_wiiu.py "/path/to/Halo.iso" --sd-card "/path/to/SD card" --copy-iso
```

The helper checks the image's file table and map set, prepares the files in
a temporary folder, then installs the complete maps directory. Cancelling
or failing before installation removes the helper's temporary output.

## On a Mac

1. **Python.** Install the macOS installer from [python.org](https://www.python.org/downloads/).
   The Python that comes with Apple's developer tools can be too old or lack the
   window; the helper then asks its questions in the Terminal instead, which also works.
2. **The first open.** macOS may refuse `Setup Wii U.command` the first time
   ("unidentified developer"). Control-click it, choose **Open**, then **Open** again.
   Or run `python3 setup_wiiu.py` from a Terminal in the unzipped folder.
3. **The SD card.** In **Disk Utility**, select the card (the device, not just its
   volume), choose **Erase**, Format **MS-DOS (FAT)**, Scheme **Master Boot Record**.
   This erases the card, so back it up first. The Wii U reads FAT32 most reliably.
4. **Finder copies.** If you copy files to the card in Finder, macOS adds `._` files
   beside them. The helper removes the ones inside `wiiu/apps/halo/`; to clean a
   whole folder yourself run `dot_clean /Volumes/YOURCARD/wiiu`.
5. **Eject** the card (the eject button in Finder) before pulling it out.

The same helper works on Linux with `python3 setup_wiiu.py`; it may need your
distribution's Python Tk package for the window, and falls back to questions
without it.

## Validation and console trial

The native converter was run as a 32-bit big-endian PowerPC program under
qemu. All 24 local Xbox maps produced the same inflated bytes as the
established computer converter, including relocated pointers. Synthetic
disc tests cover whole-disc partition offsets, split boundaries, reads
beyond 4 GiB, truncation and unsafe names. Computer-helper tests cover both
ISO and extracted-folder conversion, splitting, cancellation and save
preservation. No retail data is included in the tests or repository.

Before releasing the importer build, test on a spare SD card:

- Import from a compact ISO, reach the menu, and enter a campaign map.
- Close and launch again; setup should be bypassed and saves should load.
- Import from a complete split image.
- Cancel during extraction and conversion, then retry.
- Open HOME during setup, return, and close normally.
- Check missing-image, missing-pack, incomplete-image, and insufficient-space errors.
- Compare gameplay and repeated online joins against the original rc1 build.

The stable source remains tagged `wiiu-rc1-source`. These host tests do not
replace that console trial.
