# OpenCE-WiiU

Halo: Combat Evolved on Wii U, maintained by [Roastedd](https://github.com/Roastedd).
Based on [OpenCommunityEdition/OpenCE](https://github.com/OpenCommunityEdition/OpenCE)
and its Xbox decompilation lineage. This repository preserves their commit
history and credits. It is an unofficial fan port; no game data is included.

## Getting started

You need a Wii U with Aroma, an SD card, and your own copy of Halo: Combat
Evolved for the original Xbox. See the [player guide](port/wiiu/PLAYING.md)
for map conversion, installation, and controls. See [building](BUILDING-WIIU.md)
to build the Wii U application.

This repository starts from **rc1**, source commit
`381913d6d843b1a49a17e405d7ab0473d75cf270`. Documentation and build-support
changes on top of that commit do not change the game code. The original
source is also marked by the `wiiu-rc1-source` tag.

rc1 completed a 24-minute hardware session with six online joins and a
normal exit, without a recorded crash or freeze. This is the tested baseline,
not a guarantee for every map or mode. See [validation](VALIDATION.md).

**Online compatibility:** rc1 uses network protocol **41**. It cannot join
protocol 44 games from newer OpenCE builds. Fewer compatible servers may be
available. Full campaign completion, every controller configuration, split
screen, and long co-op sessions have not all been qualified.

Binary downloads will be listed on this repository's
[Releases page](https://github.com/Roastedd/OpenCE-WiiU/releases) when packaged.
A playable bundle needs the matching `halo.wuhb`, `shaders.pack`, and
`brokers.txt`; the source repository alone is not that bundle.

## Project and upstream

Roastedd maintains the Wii U platform work: the native GX2 renderer,
big-endian platform support, audio and input integration, performance work,
and hardware testing. OpenCE provides the underlying port, game features,
networking, tools, and artwork; the decompilation projects provide the game
source foundation. Contributor authorship is retained in Git history.

The original [upstream overview](README.upstream.md) is preserved for
reference. Its download links and platform claims describe upstream OpenCE;
Wii U status and downloads are documented here. The newer merged build is
not part of this baseline and remains separate development work.

Inherited desktop/web release and server-image workflows are archived under
`.github/upstream-workflows/` so this repository does not automatically
publish those products. Wii U releases will be managed separately.

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
