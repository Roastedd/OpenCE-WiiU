# Playing Halo on the Wii U

This is the guide for players. How the port is built and how it works are in [README.md](README.md).

## What you need

- A Wii U running the **Aroma** homebrew environment.
- An SD card with about **4 GB free**: the maps take 1.7 GB, and the game makes about 770 MB of cache files the first time it starts.
- Your own **Halo: Combat Evolved for the original Xbox**: the `maps` folder from its disc (`a10.map` to `d40.map`, `ui.map`
  and the multiplayer maps). The port includes no game data.
- A computer with **Python 3**, to convert the maps once.
- From the release: `halo.wuhb` (the app), `shaders.pack` (the graphics shaders, compiled for the Wii U's GPU) and `brokers.txt`
  (the servers through which internet games find each other).

## Installing

1. Convert your maps for the Wii U (byte order and memory layout). In this repository:

   ```sh
   python3 tools/wiiu/convert_maps.py /path/to/xbox/maps converted-maps
   ```

   It converts every map in the folder; one that it cannot read is reported and the rest are still converted.
2. On the SD card, make the folder `wiiu/apps/halo/` and put in it:
   - `halo.wuhb`
   - `shaders.pack`
   - `brokers.txt` (needed for internet games only; system link works without it)
   - a folder `data/maps/` holding every converted map.
3. Put the card in the Wii U and start **Halo** from the Aroma menu. The first start takes longer while the cache files are made.

The game keeps its settings in `wiiu/apps/halo/config.toml` and its saves in `wiiu/apps/halo/data/`.

## Controls

| Action | GamePad / Pro Controller |
| --- | --- |
| Move / look | Left stick / right stick |
| Fire | ZR |
| Throw grenade | ZL |
| Jump | B (bottom button) |
| Melee | A (right button) |
| Action, reload | Y (left button) |
| Switch weapons | X (top button) |
| Flashlight | L |
| Switch grenades | R |
| Crouch / zoom | Click the left stick / the right stick |
| Pause | + |
| Back | − |

In the menus, **A selects and B goes back**, as on other Nintendo games, and you can tap items on the GamePad's touch screen. In the game, the face
buttons sit where the Xbox's do, so jump is under your thumb; the on-screen prompts show the Wii U's letters.

- **Gyro aiming**: turn on *Gyro aim* in Settings → your profile → Gamepad Setup → **Wii U GamePad** ("Always", or "Zoomed" for scopes only).
  The GamePad then points the view like a mouse: turn it left and right however you hold it, tilt it to look up and down. Rest a thumb on the
  touch screen to hold the view still while you bring the GamePad back to a comfortable position. Sensitivity 1 turns the view as far as the
  GamePad turns; most players like 2 to 4.
- **Skip a cutscene**: hold B (the jump button), + or a finger on the touch screen for one second.
- **Performance overlay**: L + R + − shows or hides frame rate and timing at the top of the screen.

## Two players (split screen)

Connect a Pro Controller, then in Settings → your profile → Gamepad Setup → Wii U GamePad set **Controllers** to **Separate**: the GamePad is
player 1 and the Pro Controller player 2. With **Merged** (the default) a Pro Controller plays as player 1 alongside the GamePad, for playing on
the TV alone. Start split screen from the Multiplayer menu, or a campaign with two players.

## System link and online

System Link in the Multiplayer menu finds and hosts games on your local network, with other Wii Us and with OpenCE on PC built from the same
version.

## Settings worth knowing

- **Render resolution** (Settings → Video): 720 lines is the default and the best balance; 900 and 1080 are sharper but slower.
- Every setting is also in `config.toml`, each with a comment saying what it does.

## Updates

Release builds look for a new version each time the game starts, if the console is online (Settings → your profile → Network Setup →
**Check for updates**, on by default). When there is one, a panel at the main menu says so:

- **A**: update now. The new files (about 18 MB) are downloaded, checked, and put in place, and the game starts the new version by
  itself. While the panel says the files are being put in place, the HOME button is off for a moment: don't turn the console off.
- **B**: not now. The game asks again the next time it starts.
- **X twice**: never ask again (this turns Check for updates off).

A download stopped with B, with HOME or by closing the game goes on from where it stopped the next time you choose to update. The update
needs the download's size and 32 MB more free on the SD card, and says so if they are not there. It changes only the release's own files
(`halo.wuhb`, `shaders.pack`, `brokers.txt`): never your maps, saves, settings (`config.toml`) or anything in `data/`. A new version whose
maps would have to be converted again is not offered.

If the new version does not reach its main menu (it crashes before it gets there, or does not get there twice in a row), the game puts the
version you had back by itself and tells you at the main menu. That version is not offered again (press Y there if you want it offered
again); a later one is. Without a network, the game starts as it always does.

## If something goes wrong

- If the game stops responding, press **HOME** and choose **Close Software**. If HOME does nothing, hold the console's power button.
- To report a problem, include these files from `wiiu/apps/halo/` on the SD card: `log.txt`, `log-previous.txt`, `crash.txt`, `blackbox.txt`,
  `exit-trail.txt` and `data/debug.txt`. Take them after the game is closed.
- A map missing from `data/maps/` stops the game at the end of the level before it. Make sure every converted map is there.

### If an update went wrong

The updater keeps its files in `wiiu/apps/halo/update/` on the SD card: `<name>.part` (a download not finished), `<name>.new` (downloaded
and checked, not yet in place), `<name>.old` (the version you had, kept until the new one has reached its main menu), `<name>.bad` (a
version set aside, deleted at the next start), and its journal, `state.txt` and `state2.txt`. Whatever stops the console during an update,
the next start finishes the update or undoes it. It cannot when Halo has gone from the Wii U Menu (the power was cut at the one moment
`halo.wuhb` itself was being swapped), or when the new version crashes before it shows anything. Then, on a computer:

1. In `wiiu/apps/halo/update/`, for each file ending in `.old` (`halo.wuhb.old`, `shaders.pack.old`, ...): delete the file of the same
   name in `wiiu/apps/halo/`, if there is one, and move the `.old` file there without its `.old` (`update/halo.wuhb.old` becomes
   `halo.wuhb`).
2. Delete `update/state.txt` and `update/state2.txt`.

That is the version you had, as it was. If there are no `.old` files, install a release by hand as in "Installing" and delete the
`update` folder.
