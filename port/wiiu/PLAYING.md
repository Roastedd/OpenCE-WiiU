# OpenCE-U (Halo: Combat Evolved on the Wii U): player's guide

This is the guide for players. How the port is built and how it works are in [README.md](README.md).

OpenCE is an unofficial fan port of the game's decompiled code to the Wii U. It is not made, endorsed or supported by Microsoft or the game's
makers, and it **includes no game data**: you need your own copy of Halo: Combat Evolved for the original Xbox. Halo is a trademark of Microsoft.
The port is free software under the GNU Affero General Public License ([LICENSE](../../LICENSE), [README.md](../../README.md) at the top of the
repository); the licence covers the work of OpenCE's authors and gives no rights in the game or its maps.

## What you need

- A Wii U running the **Aroma** homebrew environment.
- An SD card with about **4 GB free**: the maps take 1.7 GB, and the game makes about 770 MB of cache files the first time it starts.
- Your own **Halo: Combat Evolved for the original Xbox**, as the disc or a disc image (`.iso`, `.xiso`). The port needs the disc's `maps` folder:
  `a10.map` to `d40.map`, `ui.map` and the multiplayer maps (24 files). A program that extracts the files of an Xbox disc image (for
  example extract-xiso) gives you the folder.
- A computer with **Python 3**, to convert the maps once. Nothing else is needed.
- From the release: `halo.wuhb` (the app), `shaders.pack` (the graphics shaders, compiled for the Wii U's GPU) and `brokers.txt` (the servers
  through which internet games find each other). **`halo.wuhb` and `shaders.pack` of a release belong together**: do not mix a new one with an
  old one.

## Installing

1. Convert your maps for the Wii U (byte order and memory layout). In the repository:

   ```sh
   python3 tools/wiiu/convert_maps.py /path/to/xbox/maps converted-maps
   ```

   It converts every map in the folder, and a map it cannot read is reported while the rest are still converted. A map already converted is
   skipped when you run it again (`--force` converts it again). Convert the Xbox maps only: a Halo PC or Halo Custom Edition map is not converted
   (see "Custom Edition maps").
2. On the SD card, make the folder `wiiu/apps/halo/` and put in it:
   - `halo.wuhb`
   - `shaders.pack`
   - `brokers.txt` (for internet games only; system link works without it)
   - a folder `data/maps/` holding every converted map. **Put all of them there**: a campaign level whose next map is missing stops the game
     at the end of the level before it.
3. Put the card in the Wii U and start **OpenCE-U** from the Aroma menu. The first start takes longer while the game makes its
   cache files.

The game keeps its settings in `wiiu/apps/halo/config.toml` (made at the first start; every setting has a comment saying what it does) and its
saves in `wiiu/apps/halo/data/`. Copy `data/` to keep your saves, and put it back to restore them.

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
| Talk (voice chat, push to talk) | TV, held (GamePad) |
| Performance overlay | L + R + − |

In the menus, **A selects and B goes back**, as on other Nintendo games, and you can tap items on the GamePad's touch screen. In the game, the face
buttons sit where the Xbox's do, so jump is under your thumb; the on-screen prompts show the Wii U's letters. A **Wii Remote with a Nunchuk** plays too (below); a Wii Remote alone only works the menus.

- **Gyro aiming**: turn on *Gyro aim* in Settings → your profile → Gamepad Setup → **Wii U GamePad** ("Always", or "Zoomed" for scopes only).
  The GamePad then points the view like a mouse: turn it left and right however you hold it, tilt it to look up and down. Rest a thumb on the
  touch screen to hold the view still while you bring the GamePad back to a comfortable position. Sensitivity 1 turns the view as far as the
  GamePad turns; most players like 2 to 4. (Written and tested on a computer; not yet checked on a console.)
- **Wii Remote with Nunchuk** (sync it with the Wii U's SYNC button, as for any Wii Remote; its lights show its player's number). The Nunchuk's stick
  moves. B fires, A jumps, Nunchuk Z throws a grenade, C crouches, 1 reloads and uses, 2 is the melee; on the d-pad, up switches weapons, left is the
  flashlight, right switches grenades, down zooms; + pauses and − is Back. (Setting `input.wiiu_wiimote_layout = "z_fire"` in `config.toml` has Z fire and
  B throw grenades.) A Wii Remote without a Nunchuk has the menus' buttons: d-pad, A selects, B goes back, 1 and 2 are the X and Y prompts.
  To look around, point at the screen (needs a sensor bar: the Wii U's own, or the GamePad as one through the *DRC Sensorbar* Aroma plugin): the view
  does not turn while you point inside the middle of the screen, and turns faster the further out you point (`input.wiiu_wiimote_pointer_box` and
  `_pointer_speed` set the size of the middle and the speed). A **Wii Remote Plus** (or a MotionPlus adapter) needs no bar: it turns the view as you
  turn it (and keeps turning while you point it well to one side of where it was zeroed, so you do not run out of wrist: `input.wiiu_wiimote_motion_edge`, `_box`, `_range`, `_speed`), and holding it still for a second and a half zeroes it. `input.wiiu_wiimote_look` chooses between the two (`"auto"` prefers the MotionPlus).
  (Written and tested on a computer; not yet checked on a console.)
- **Skip a cutscene**: hold B (the jump button), + or a finger on the touch screen for one second.

## Settings that matter

In Settings → your profile → **Video Settings**, kept in `config.toml`:

- **Graphics mode**: **Balanced** is the default: 720 lines at 60 frames a second, everything the Xbox drew. **Performance** keeps the frame rate
  first: it leaves out the grass, the objects' shadows, the lens flares and the shine on the level, and turns on auto detail (below).
  **Quality** draws up to 1080 lines at 30 frames a second, with dynamic resolution on. **Custom** is the rows as you set them (changing a row that a
  mode sets switches to Custom). The lines take effect from the next start of the game.
- **Render resolution**: 480, 540, 720, 900 or 1080 lines. 720 is the best balance; 900 and 1080 are sharper but slower.
- **Dynamic resolution** and **Lowest lines**: draws fewer lines (down to the lowest you set, 540 by default at 720) while the GPU falls behind, and all
  of them again when it catches up. Quality turns it on. It helps where the GPU is the limit (open views, high resolutions); in a firefight the
  game's own logic is usually the limit, and fewer lines change nothing.
- **Smooth motion**: draws a frame for every refresh of the TV between the game's 30 ticks a second. Off holds the game to 30 frames, as on the Xbox.
- **Detail**: switches for the grass, decals, particles, object shadows, lens flares, specular shine and bump mapping.
- **Auto detail** (the governor) has no row: Performance mode turns it on, or set `display.dynamic_quality = true` in `config.toml`, or put `on` in
  `governor.txt` (see "Card files"). When frames take longer than 22 ms, the game leaves out optional drawing a step at a time (small and far things,
  small objects' shadows, thinned decals and particles, then the grass and the like), and puts it back when there is room. Only what is drawn
  changes: the game itself plays the same, and no unit, vehicle, weapon or projectile is ever left out.
- **Performance overlay**: **L + R + −** goes round off, a compact strip (frame rate, frame time, CPU and GPU share) and a detailed panel (where the
  frame's time goes, the governor's step, how full the memory pools are). `overlay.txt` (see "Card files") starts the game in one of them.

## Smoother fights (optional, off by default)

The release candidates of this port were played with five small files that put the game's ticks on a second processor core beside the drawing, and the blends
cheaper. They are off unless you make them (the game then plays as it did before them): in `wiiu/apps/halo/` make `tick_thread.txt`,
`tick_snapshot.txt`, `tick_overlap.txt` and `gpu_offload.txt` each holding `1`, and `governor.txt` holding `on`. Take them away (or write `-1` in each)
to go back. They work in the campaign and in a game of one player on this console; a network game and split screen stay on the plain loop on
purpose. They are still being qualified: if the game halts with them on, take them away and send the logs (see "If something goes wrong").

## Split screen (two to four players)

The GamePad is always player 1. Connect up to three more controllers: Pro Controllers, Classic Controllers on Wii Remotes, Wii Remotes with Nunchuks, or other pads through
Bloopair. While you play alone, any of them plays as player 1 with the GamePad, so you can play on the TV with a Pro Controller.

- **Multiplayer**: create a game (LAN, or internet), pick a map, and in the lobby choose **ADD PLAYER**. The controllers then become players of their
  own: each one's lights show its number (2 to 4, in the order they connected). Each new player presses START on their controller and picks a
  profile; once a second player is in, the others can just press START. Then START NOW. Each player needs a profile of their own.
- **Campaign**: **CO-OP CAMPAIGN** in the Multiplayer menu, for two players; player 2 picks a profile with their own controller.
- A controller that turns off or loses its connection keeps its player: turn it on again and it plays on.
- **Two players** have played on a console. **Three and four players are written and tested on a computer only**: with three or four views the game holds
  30 frames a second and leaves out shadows, lens flares and some shine, so that every view stays as smooth as it can, and it is estimated at 22 to 29
  frames a second at four views. Video Settings → **Split screen detail** and **Split screen FPS** change that.

In Settings → your profile → Gamepad Setup → Wii U GamePad, **Controllers** is **Auto** (as above); **Separate** makes each controller its own
player even when you play alone; **Merged** is the old behaviour (the first controller joins the GamePad, the others are players 2 to 4).
**GamePad Shows: Player 1** puts player 1's view alone on the GamePad while the screen is split; **TV** (the default) shows the TV's picture.

## System link and online play

- **System Link** (Multiplayer menu) finds and hosts games on your local network, with other Wii Us and with OpenCE on a computer built from the same
  network version (the Wii U port speaks protocol 41, the same as upstream OpenCE's PC builds of that version). A game that tells you the version is
  different is from another build: update that side.
- **Internet games** find each other through the servers in `brokers.txt` and use UPnP on your router to open a port. Playing over the internet, hosting
  one from the Wii U, and large games (24 players and more) have **not been tried on a console**; a system link game with a computer standing in for the second
  player has. The console has room for 27 network connections in all.
- **Voice chat** uses the GamePad's microphone, or a headset in its headphone jack. Hold the GamePad's **TV** button to talk (Settings → your profile →
  Audio Setup → **Voice chat**: Push to talk is the default; **Open mic** talks whenever you speak, and wants a headset, because the GamePad's
  microphone also picks up its own speaker). The Pro Controller has no microphone or jack; USB headsets do not work. **Voice chat has not been tried
  on a console yet.**
- A network game plays on the plain loop: it is not faster for the files in "Smoother fights".

## Saves

Saves, profiles and the settings live in `wiiu/apps/halo/data/` and `config.toml`. The game autosaves at checkpoints and when you quit a level. A save
is written to the card while you play; do not take the card out or turn the console off while the game is running, and leave through HOME and **Close
Software**, or the pause menu's Quit. If the card or the console is changed, copy the whole `wiiu/apps/halo/` folder back.

## Halo Custom Edition maps

The game can play maps made for Halo Custom Edition on PC (`.map` files of cache version 609), as OpenCE on PC does
([docs/custom_edition_caches.md](../../docs/custom_edition_caches.md)). **They have not been tried on a console yet**, and the audit of this feature
found that no Custom Edition map was available on the development machines, so treat it as untested.

- Put the maps **as they are** in `wiiu/apps/halo/data/custom_maps/` on the SD card: do not convert them with `convert_maps.py`, which is for the Xbox
  maps only (a Custom Edition map's file must stay as it is: network games tell its versions apart by its contents, and send it to players who lack
  it). The game turns each one for the Wii U in memory as it loads.
- Put your own copies of Halo Custom Edition's `bitmaps.map`, `sounds.map` and `loc.map` (from the `maps` folder of your Halo Custom Edition
  install) in the same folder. Most maps need them; a map that needs one that is not there is refused, and `data/debug.txt` names it.
- A map can have a picture and a description beside it: `<name>.bmp` (an uncompressed 24- or 32-bit bitmap) and `<name>.txt`.
- The maps are listed under **CUSTOM MULTIPLAYER** and **CUSTOM SINGLEPLAYER** in the map lists, by their file's names.
- Maps that need **OpenSauce** (their header asks for its memory upgrades, data files and the like) are refused; maps that only carry
  OpenSauce's header play as stock Custom Edition plays them.
- In a network game on a Custom Edition map you do not have, the map (and the resource maps you lack) is downloaded from the host into
  `custom_maps` (`network.map_download` in `config.toml`; and `network.map_share` to send yours to others).
- A new map can use graphics shaders that `shaders.pack` does not have yet: the game draws a missing one with the closest shader the pack has, or a
  plain stand-in, so a custom map can look wrong (a model, the sky or the HUD with the wrong shine or colour) until the pack is made again with the map's shaders.
- `game.custom_edition` in `config.toml` (on by default) turns them off; the game then has 23 MB more memory for everything else.

## What is not done, and what to expect

- **Frame rate**: the campaign is meant to run at 60 frames a second out of combat and at 50 or more in fights. The big fights (the beach of The
  Silent Cartographer, the level Halo, Blood Gulch) are still the slowest places (the game's own logic, not the GPU, is the limit); auto detail leaves a
  few things out there on purpose. The game is not yet at a steady 60 in every fight. Heavy scenes can still have a hitch
  the first time a room is drawn (the textures are read from the card then).
- **Not tried on a console**: voice chat, Halo Custom Edition maps, split screen with three or four players, the gyro aim's sensitivity, mods, and
  anything with 24 or more players, and the Wii Remote with Nunchuk (the pointer, the MotionPlus, the buttons). USB headsets do not work.
- **Automatic updates** are built but nothing is published to update from yet.
- A graphics shader that `shaders.pack` lacks is drawn with the closest one it has, or a plain stand-in: if something looks wrong (a meter, a scope, a
  sky), say what and in which level, and which `shaders.pack` you have.

## If something goes wrong

- If the game stops responding, press **HOME** and choose **Close Software**. If HOME does nothing, hold the console's power button.
- **A black screen, or the game does not come up**: check that `halo.wuhb` and `shaders.pack` are the same release, that `data/maps/` has the maps
  (at least `ui.map` and the level you are starting), and that the card has free space (the first start writes about 770 MB of cache files). If it
  was working and does not now, take away any card files you made (see below) and try again.
- **A freeze or a halt**: the game writes why into `data/debug.txt` (a line that starts `EXCEPTION halt` says which check stopped it). Do not
  open the game again before you copy the files below: the next start renames them.
- To report a problem, include these files from `wiiu/apps/halo/` on the SD card: `log.txt`, `log-previous.txt`, `crash.txt`, `blackbox.txt`,
  `exit-trail.txt` and `data/debug.txt`, and say which release and which settings and card files you used. Take them after the game is closed.
  The log says what the game was doing in the last seconds, and the performance overlay's numbers help for a slow place.
- A map missing from `data/maps/` stops the game at the end of the level before it. Make sure every converted map is there.
- A Custom Edition map that does not load takes you back to the menus; `data/debug.txt` says why, in its lines that start with
  `custom edition:`.

## Card files (for testers and advanced players)

Small text files in `wiiu/apps/halo/` that change what the game does. Each holds one number (or a word where stated), is read once at start, and
**a file holding -1 is the same as no file**. They are all off or at the default with no file: you do not need any to play.

| File | Default | What it does |
| --- | --- | --- |
| `overlay.txt` | 0 | 0 no performance overlay, 1 the compact strip, 2 the detailed panel (L + R + − goes round them any time) |
| `governor.txt` | as Graphics mode says | text: `on` or `off` for auto detail; `target 25` the frame time it aims for in ms (at least 20), `max_step 2` the deepest step (0 to 3), `step 2` hold one step (a test) |
| `tick_thread.txt` | 0 | 1: the game's ticks run on a thread of their own |
| `tick_snapshot.txt` | 0 | 1: the drawing reads a copy of the objects (needed by the overlap) |
| `tick_overlap.txt` | 0 | 1: the ticks run beside the drawing (needs the two above); campaign and one-player games only |
| `tick_core.txt` | 2 with the overlap | which core runs the ticks: 0, 1 or 2 |
| `gpu_offload.txt` | 0 | 1: the interpolation's blends are cheaper (no correctly rounded square roots; the picture is the same to the eye) |
| `stream.txt` | 0 | 1: texture streaming (the game draws from a bitmap's small tail while the rest is read from the card) |
| `memcpy_touch.txt` | 0 | 1: the port's memory copy also asks the cache for the source ahead (faster or slower depending on the scene; for timing tests) |
| `hotpaths.txt` | 0 | 1: the ticks ask the cache for the next objects ahead (for timing tests) |
| `pacing.txt` | 0 | `1 [D]` lets a late frame tear instead of waiting a refresh; `2 [D]` never waits for the vsync (for tests) |
| `gx2_thread.txt` | 7 | the graphics thread's mode; **0 is the safe fall back** (the game thread makes the graphics calls itself) |
| `game_core.txt` | 1 | the core of the game thread: 0, 1 or 2 |
| `mem1.txt` | 1 | 0: the big render targets in main memory instead of the fast eDRAM (slower; a fall back) |
| `startup.txt` | all on | a number whose bits leave start-up changes out; 0 is the old start-up (a fall back) |
| `render_height.txt`, `drs_lines.txt` | the config's | lines drawn for one run (a test) |
| `debug_server.txt` | 0 | 1 starts the debug server (screenshots, button presses, the log) for anyone on the network: leave it off unless you are testing |
| `shader_capture.txt` | off | `always`: the shaders a level needs are written down during play, for making a new pack (a tester's) |
| `profile.txt` | absent | its presence starts the sampling profiler (a tester's; delete the file to stop) |
| `broadway.txt` | 0 | the Broadway kernels: **stays 0** (one failed its self-test on a console) |

Files the game writes there: `log.txt`, `log-previous.txt`, `blackbox.txt`, `crash.txt`, `exit-trail.txt`, `shader-misses-*.txt`, and `data/debug.txt`.

## Updates

A release that was built with an update source (a repository and a signing key; the log says `Update: off` for one that was not, and such a
build never looks for anything) looks for a new version each time the game starts, if the console is online (Settings → your profile → Network
Setup → **Check for updates**, on by default). When there is one, a panel at the main menu says so:

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

## Credits and licence

The port is OpenCE's work on the decompilation of the Xbox build 2342 of Halo: Combat Evolved, which starts from [bnunu/halo-1](https://github.com/bnunu/halo-1),
a fork of [punpckhdq/halo](https://github.com/punpckhdq/halo). The artists of the high-resolution HUD, buttons and cursor are named in
[AUTHORS.md](../../AUTHORS.md); the menus' text is set in Overpass (SIL Open Font License) and the Wii U app's own icon and start-up pictures are
drawn for this port by `tools/wiiu_app_art.py`. OpenCE is under the GNU Affero General Public License 3.0 or later; third-party code, fonts and art
keep their own licences, next to them in the repository. The Wii U build uses wut and the devkitPro tools, Mbed TLS and Monocypher (for the updater)
and Opus (voice chat) under their licences.
