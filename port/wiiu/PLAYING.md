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

## Split screen (two to four players)

The GamePad is always player 1. Connect up to three more controllers: Pro Controllers, Classic Controllers on Wii Remotes, or other pads through
Bloopair. While you play alone, any of them plays as player 1 with the GamePad, so you can play on the TV with a Pro Controller.

- **Multiplayer**: create a game (LAN, or internet), pick a map, and in the lobby choose **ADD PLAYER**. The controllers then become players of their
  own: each one's lights show its number (2 to 4, in the order they connected). Each new player presses START on their controller and picks a
  profile; once a second player is in, the others can just press START. Then START NOW.
- **Campaign**: **CO-OP CAMPAIGN** in the Multiplayer menu, for two players; player 2 picks a profile with their own controller.
- A controller that turns off or loses its connection keeps its player: turn it on again and it plays on.
- With three or four players the game holds 30 frames a second and leaves out shadows, lens flares and some shine, so that every view stays
  smooth. Settings → Video → **Split screen detail** and **Split screen FPS** change that.

In Settings → your profile → Gamepad Setup → Wii U GamePad, **Controllers** is **Auto** (as above); **Separate** makes each controller its own
player even when you play alone; **Merged** is the old behaviour (the first controller joins the GamePad, the others are players 2 to 4).

## System link and online

System Link in the Multiplayer menu finds and hosts games on your local network, with other Wii Us and with OpenCE on PC built from the same
version.

## Settings worth knowing

- **Render resolution** (Settings → Video): 720 lines is the default and the best balance; 900 and 1080 are sharper but slower.
- Every setting is also in `config.toml`, each with a comment saying what it does.

## If something goes wrong

- If the game stops responding, press **HOME** and choose **Close Software**. If HOME does nothing, hold the console's power button.
- To report a problem, include these files from `wiiu/apps/halo/` on the SD card: `log.txt`, `log-previous.txt`, `crash.txt`, `blackbox.txt`,
  `exit-trail.txt` and `data/debug.txt`. Take them after the game is closed.
- A map missing from `data/maps/` stops the game at the end of the level before it. Make sure every converted map is there.
