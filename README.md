<div align="center">

<img src="port/wiiu/meta/icon.png" alt="OpenCE-WiiU helmet icon" width="128">

# OpenCE-WiiU

**Halo: Combat Evolved on the Wii U.**

[![Wii U · Aroma](https://img.shields.io/badge/Wii_U-Aroma-009ac7)](port/wiiu/PLAYING.md)
[![Baseline · rc1](https://img.shields.io/badge/baseline-rc1-6c8c4b)](VALIDATION.md)
[![Network · 41](https://img.shields.io/badge/network-protocol_41-536b8e)](#online-play)
[![AGPL-3.0-or-later](https://img.shields.io/badge/license-AGPL--3.0--or--later-blue)](LICENSE)

[Setup](#getting-set-up) ·
[Features](#what-it-can-do) ·
[Controls](#controls) ·
[Questions](#questions) ·
[Building it](BUILDING-WIIU.md) ·
[Issues](https://github.com/Roastedd/OpenCE-WiiU/issues)

<img src="docs/assets/screenshots/menu.png" alt="OpenCE's main menu running on Wii U" width="90%">

<sub>Captured on a Wii U during development. Maintained by <a href="https://github.com/Roastedd">Roastedd</a>, built on <a href="https://github.com/OpenCommunityEdition/OpenCE">OpenCE</a>.</sub>

</div>

OpenCE-WiiU is a port of Halo: Combat Evolved to the Wii U. It is the game's own
code compiled for the console, not an emulator, with a renderer written for the
Wii U's GPU. You can play the campaign on the TV or the GamePad and join online
games.

It is a fan project and it is still rough in places. In our longest test, 24
minutes with six online games, it averaged about 56 frames a second and did not
crash.

> [!IMPORTANT]
> You need your own copy of the game. No maps, discs or game files are included
> and none are downloaded for you. You need Halo: Combat Evolved for the
> original Xbox, as the disc or a disc image. This is an unofficial fan port with
> no connection to Microsoft or the game's makers.

<img src="docs/assets/screenshots/campaign.png" alt="Campaign gameplay captured on Wii U" width="90%">

<sub>A capture from a development build. [Capture details](docs/assets/screenshots/README.md).</sub>

## What it can do

We have played these on a Wii U:

- The single-player campaign, with checkpoints and saves on your SD card. Hold B,
  + or a finger on the touch screen for a second to skip a cutscene.
- Two-player split screen in multiplayer.
- Play on the TV or the GamePad. The GamePad can show player 1's own view while
  the TV is split.
- Online games: a server browser, internet games found through the servers in
  `brokers.txt` (the game uses UPnP to open the port), and system link on your
  own network.
- The GamePad and Pro Controller, with the buttons where the Xbox's are, and the
  touch screen in the menus.
- Graphics modes: Balanced (720 lines at 60 frames a second, the default),
  Performance, Quality (up to 1080 lines at 30, with dynamic resolution) and
  Custom. You can also set the resolution from 480 to 1080 lines, turn on smooth
  motion (a frame for every refresh of the TV instead of 30 a second), and switch
  grass, decals, particles, object shadows, lens flares, specular shine and bump
  mapping on and off.
- Auto detail. When frames start to run long, the game leaves out small and far
  extras a step at a time and puts them back when there is room. The game itself
  plays the same.
- A performance overlay (L + R + -) with a short strip and a detailed panel
  showing frame time, CPU and GPU load, where the frame's time goes and how full
  the memory pools are.
- The community's high-resolution menu and HUD redraws (see the credits).
- Saves, profiles and settings kept on the SD card, with checkpoints written in
  the background so they do not stall the game.

Built and tested on a computer, but not yet tried on a console:

- The two-player co-op campaign, and split screen with three or four players.
- Voice chat through the GamePad's microphone or a headset in its jack.
- Halo Custom Edition maps from your PC copy, with automatic download from a host
  that has the map.
- A Wii Remote with a Nunchuk, including the pointer and Wii Remote Plus motion
  aiming, and gyro aiming on the GamePad.
- Classic Controllers and, through Bloopair, other pads for the extra
  split-screen players.
- Hosting an internet game from the Wii U, and very large games (24 players or
  more).
- Setting up on the Wii U itself, with an importer build that reads `halo.iso`
  from the card.

In the code but switched off for now: bots, friends and parties, the players
page, a keyboard on the GamePad, and automatic updates (nothing is published to
update from yet).

## Getting set up

You will need:

- a Wii U running Aroma,
- an SD card with about 4 GB free (the maps take about 1.7 GB and the game makes
  roughly 0.8 GB of cache files the first time it starts),
- your Xbox copy of Halo as a disc image (`.iso`),
- a Windows PC or a Mac to prepare the card. A Linux PC works too.

### The game files

Download `halo.wuhb`, `shaders.pack` and `brokers.txt` from the
[Releases page](https://github.com/Roastedd/OpenCE-WiiU/releases) and copy them to
this folder on the SD card. A `halo.wuhb` only works with the `shaders.pack` that
came with it, so keep those two together.

```text
sd:/wiiu/apps/halo/
├── halo.wuhb
├── shaders.pack
└── brokers.txt
```

We have not published a complete download bundle yet. Until we do, the source and
the setup helper below are here, and you can build the app yourself with
[these instructions](BUILDING-WIIU.md).

### Your maps

The maps on your Xbox disc are in a form the Wii U cannot read, so they have to be
converted once. The setup helper does that on your computer. It runs locally, needs
only Python, and uploads nothing.

1. Install Python 3.10 or newer from [python.org](https://www.python.org/downloads/)
   if you do not have it. On a Mac, use the installer from python.org. The Python
   that comes with Apple's developer tools can be too old or missing the window.
2. Download the [setup helper](https://github.com/Roastedd/OpenCE-WiiU/releases/tag/setup-helper-v0.2.0)
   and unzip it.
3. On Windows, double-click `Setup Wii U.bat`. On a Mac, double-click
   `Setup Wii U.command`. The first time, macOS may say it is from an unidentified
   developer: Control-click the file, choose Open, and confirm.
4. Pick your Xbox ISO and the SD card, then press Prepare SD card. It takes a few
   minutes. When it says the maps are ready, eject the card.

The helper finds the card by itself if only one Wii U card is plugged in, checks
there is enough room, and removes the stray `._` and `.DS_Store` files a Mac leaves
behind. If the window cannot open, it asks its questions in the terminal instead.

To see whether a card is ready, run `python3 setup_wiiu.py --check`. It lists what is
there and what is missing and does not change anything.

More detail, command-line use and the Mac steps (formatting the card, ejecting it)
are in the [setup guide](docs/ISO_SETUP.md). If you would rather convert the maps
yourself, the [player guide](port/wiiu/PLAYING.md) has those instructions.

### Playing

Put the card in the Wii U and start OpenCE-U from the Wii U Menu. The first start
makes the cache files and takes longer. Your saves are in `wiiu/apps/halo/data/`,
so back that folder up before you try a different build.

### Can I just put the ISO on the card?

On a computer, yes. The helper reads the ISO directly, so there is no separate
extraction step.

On the Wii U itself, not yet. We are testing an importer build that finds
`halo.iso` on the card at the first start and converts the maps on the console. It
builds and its converter passes its tests, but its screens and the whole first run
have not been tried on a console. The rc1 build does not do this. There are
details and a test plan in the [setup guide](docs/ISO_SETUP.md).

An SD card is FAT32, which cannot hold a file of 4 GB or more. When the helper
copies a large ISO for the console to import, it splits it into parts.

## Controls

| Action | GamePad / Pro Controller |
| --- | --- |
| Move / look | Left stick / right stick |
| Fire / grenade | ZR / ZL |
| Jump / melee | B / A |
| Reload, use / switch weapon | Y / X |
| Flashlight / switch grenade | L / R |
| Crouch / zoom | Left stick click / right stick click |
| Pause | + |
| Performance overlay | L + R + - |

In menus, A selects and B goes back, and the touch screen works too. The
[player guide](port/wiiu/PLAYING.md#controls) has every control, the Wii Remote
layout and the gyro settings.

## Online play

This version uses network protocol 41. It can join games hosted by other protocol
41 builds, but not games from newer OpenCE builds, which use protocol 44. A server
showing up in the list does not mean you can join it, so if a game will not
connect, check that first. How smooth a game is depends on the map, the host and
how many people are in it.

## Questions

Does my Wii U need to be modded? It needs Aroma, which loads homebrew apps from the
SD card. Nothing is installed to the console's own storage.

Which Halo do I need? The original Xbox release of Halo: Combat Evolved. The PC
release, Custom Edition, Anniversary and the Master Chief Collection are different
formats and will not work with the setup.

Why can't I just download the maps? They belong to Microsoft, so this project can't
hand them out. Converting your own copy on your own computer gets around that.

It won't start, or the maps are missing. Run `python3 setup_wiiu.py --check` and fix
whatever it marks with FIX. The usual causes are a `shaders.pack` that does not match
the `halo.wuhb`, and an SD card that is not formatted as FAT32. The Mac section of
the [setup guide](docs/ISO_SETUP.md#on-a-mac) shows how to format one.

Will it run at 60 fps? Mostly it sits between 50 and 60 in our tests. The biggest
fights and some online games dip lower, so it is not a locked 60 yet. The overlay
(L + R + -) shows what the game is doing.

I found a bug. Open an [issue](https://github.com/Roastedd/OpenCE-WiiU/issues) with
the build, the map, your controller and what happened. Include the logs the
[player guide](port/wiiu/PLAYING.md) describes, after reading them for anything
personal.

## How well does it work?

This repository starts from rc1, the source tag
[`wiiu-rc1-source`](https://github.com/Roastedd/OpenCE-WiiU/tree/wiiu-rc1-source).
In a 24-minute session on a console it joined six online games with no freeze or
crash and exited normally. Five-second windows averaged 55.9 FPS. The
[per-map results and checksums](VALIDATION.md) are in the repository.

It is still experimental. We have not tested finishing the whole campaign, every
controller setup, split screen or long co-op sessions. A newer merge with upstream
OpenCE (protocol 44) is in progress and still has joining problems, so it is not
part of this baseline.

## Building and contributing

[Build instructions](BUILDING-WIIU.md) cover the Docker toolchain and checks that
need no game files. The app icon and start-up pictures can be redrawn with
`python3 tools/wiiu_app_art.py`.

The original [upstream overview](README.upstream.md) is kept for reference. Its
platform claims and downloads describe upstream OpenCE. Publishing workflows
inherited from it are archived under `.github/upstream-workflows/`, and this
repository's Wii U releases are managed separately.

## Credits

The decompilation is of the Xbox build 2342 (`cachebeta.exe`, SHA-256
`4cc87b45f721270392a96f1674ed2b5cd4a7bb4355faeab4531d1cf1884d9520`).
OpenCE starts from the decompilation of
[bnunu/halo-1](https://github.com/bnunu/halo-1), which is a fork of
[punpckhdq/halo](https://github.com/punpckhdq/halo). The artists of the
high-res redraws are in [AUTHORS.md](AUTHORS.md). The main contributors are Jonas Volman (@bnunu) and MrBruh (@cybersecurity). Everyone else who has contributed is on the
[contributors page](https://github.com/OpenCommunityEdition/OpenCE/graphs/contributors).

## Licence

OpenCE is free software: you can give it to others and change it under the
terms of the GNU Affero General Public License as published by the Free
Software Foundation, version 3 of the License or (at your option) any later
version (`AGPL-3.0-or-later`). It comes with no warranty. The full text is in
[LICENSE](LICENSE).

- **From the change.** The AGPL applies from the commit that made the change,
  and to each release built from that commit or a later one. Earlier
  releases, and the source at every earlier commit, were released under
  [CC0 1.0](LICENSES/CC0-1.0.txt) and stay that way.
- **What OpenCE builds on.** The decompilation of
  [punpckhdq/halo](https://github.com/punpckhdq/halo) is under the AGPL 3.0
  from 17 October 2026, and under CC0 1.0 before that date.
  [bnunu/halo-1](https://github.com/bnunu/halo-1) is under CC0 1.0.
- **Contributions** are under `AGPL-3.0-or-later`. A change taken from a
  project under GPL 3.0 keeps that licence and its notices; the GPL 3.0 and
  the AGPL 3.0 let the two be combined (section 13 of each).
- **Hosts and servers.** If you change the game and let other persons play
  with your version over a network (for example, as the host of an internet
  game, or on a web site), the AGPL asks you to offer them the source of
  your version (section 13). This Wii U version has its source in this repository; each published
  binary release must identify its matching source commit or tag.
- **Third-party code, fonts and art** keep their own licences, which are
  next to them (`port/third_party`, `port/assets/fonts`,
  `port/assets/icons`, `port/assets/text_filter`). The release downloads and
  the web site's `licenses.txt` carry their notices.
- **Halo is not covered.** The licence covers the work of OpenCE's authors.
  It gives no rights in Halo, its maps or anything else that Microsoft owns.


## Disclaimer
OpenCE (Open Community Edition) is an unofficial fan project. It is not endorsed by or affiliated with Microsoft, Xbox Game Studios, Halo Studios, Activision or Bungie. Halo and Halo: Combat Evolved are trademarks of Microsoft Corporation; Halo © Microsoft Corporation.
No game data is included. You must provide your own copy of the game.
