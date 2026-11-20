# Setting up the in-game updater (for the owner)

The Wii U port can update itself from a GitHub release. The code is built and tested on a desktop, but it does nothing in the builds made so far, because
it needs three things only the owner can supply: a signing key, the repository the releases are published in, and a build number. This page is the
short version of what to do once, what to do for each release, and what a player sees. The full detail is in [README.md](README.md), "Updates" and
"Turning it on". Nothing here has been run on a console.

## What you do once

1. **Make the signing key.** Do it on a computer that does not build or publish, and keep two copies of the file somewhere safe and offline:

       openssl genpkey -algorithm ed25519 -out halo-wiiu-signing.pem

   Every build trusts this one key. If it is lost, installed consoles can no longer update (they would need a build copied to the card by hand), and
   anyone who has it can publish an update to every console. The file stays out of the repository and off the machine that builds. Nothing in the
   project writes the key anywhere.

2. **Get its public key** (64 hexadecimal digits, safe to share):

       python3 tools/wiiu/make_wiiu_release.py --print-public-key halo-wiiu-signing.pem

3. **Make a public repository for releases only**, for example `owner/halo-wiiu-releases`, with no code and write access for you alone. Turn on
   immutable releases for it, so a published release cannot be changed afterwards. Use another repository and another key for test builds. A draft or
   a pre-release is never offered to players; only the latest normal release is.

## What goes into each build

The updater is switched on by three values given when the game is built (they travel from the environment to `configure.py`). Without the last two
the game logs `Update: off` once and fetches, writes and renames nothing.

| Value | What it is |
| --- | --- |
| `HALO_BUILD_NUMBER` | The build's number, one more than the last release each time (1 to 999999999). A console updates only to a higher number. |
| `HALO_UPDATE_REPOSITORY` | `owner/repo` of the releases repository from step 3. |
| `HALO_UPDATE_PUBLIC_KEY` | The 64 hexadecimal digits from step 2. This is the only part of the key a build contains. |

The Wii U build script (`tools/wiiu/build_game.sh` in the main workspace) passes them into the build container. For a release use `RELEASE=1`:

    HALO_BUILD_NUMBER=124 HALO_UPDATE_REPOSITORY=owner/halo-wiiu-releases HALO_UPDATE_PUBLIC_KEY=<64 hex digits> RELEASE=1 tools/wiiu/build_game.sh

## Making and publishing a release

1. Put these in an empty folder, for example `release-124`: `halo.wuhb` (from `build/linux-wiiu/`, built as above), the `shaders.pack` made for that build's
   shaders, and `brokers.txt` (`port/assets/network/brokers.txt`).
2. Sign it where the key is kept:

       python3 tools/wiiu/make_wiiu_release.py release-124 --build 124 --key halo-wiiu-signing.pem \
           --public-key <64 hex digits> --repository owner/halo-wiiu-releases

   It writes `halo-wiiu-manifest.txt` (the build number, the tag `wiiu-124`, the maps' format, and each file's size and SHA-256) and
   `halo-wiiu-manifest.txt.sig` (the Ed25519 signature). It then checks its own work: the signature, the manifest by the console's own rules, every hash,
   and the build's record inside `halo.wuhb`. It refuses a `halo.wuhb` made with another build number, repository or key; read what it says if it refuses.
3. Publish a normal release (not a draft, not a pre-release) tagged `wiiu-124` in the releases repository, with the files in the folder as its assets:
   the manifest, its `.sig`, `halo.wuhb`, `shaders.pack` and `brokers.txt`.
4. The consoles look at `https://github.com/<repo>/releases/latest/download/halo-wiiu-manifest.txt`, so the newest normal release is the one they find.
   A release is not offered to a console whose converted maps are in a different format from the one the manifest names.

Before the first public release, run the console test plan in README.md ("Console test plan") with a test repository and a test key.

## What players get

A player with Settings → profile → Network Setup → **Check for updates** on (it is on by default) and a console that is online: at each start a worker
asks for the manifest, checks the signature against the key in the build, and when the release is newer a panel at the main menu offers it.

- **A** updates now: the files (about 18 MB) are downloaded to `wiiu/apps/halo/update/`, checked against their hashes, put in place, and the game starts the new
  version by itself. HOME is off for a moment during the swap.
- **B** is "not now"; the game asks again at the next start. **X twice** turns **Check for updates** off.

Only the release's own files are replaced (`halo.wuhb`, `shaders.pack`, `brokers.txt`); saves, settings and maps are never touched. If the new version
does not reach its main menu, the game puts the old one back by itself and does not offer that build again. [PLAYING.md](PLAYING.md), "Updates", is the
player's page and says how to put the old version back by hand if even that fails.
