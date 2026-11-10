# The mod store

The Mods page has a STORE button (and Y) that opens a list of the mods in a catalog the player's `config.toml`
names. A on a mod downloads it into the `mods` folder (or updates it), X removes a mod the store installed, Y
asks for the catalog again, B goes back. Nothing contacts a server until `mods.store_url` is set, and then only
when the store screen is opened.

```toml
[mods]
store_url = "https://example.org/mods/catalog.json"   # empty (the default): there is no store
store_key = "d04e8b10...9376"                         # 64 hex digits: the public key the catalog is signed with
```

`store_key` may be left out when the build was made with a key (`-DHALO_MODSTORE_PUBLIC_KEY="..."`). A store with
no key is not used. The address must be `https://` (the certificate is checked), or `http://127.0.0.1` /
`http://localhost` for a test. The Wii U asks only with `https://` and does not follow redirects, so the catalog
and the files must be at their final addresses (GitHub's *release assets* redirect, a repository's *raw* files and
GitHub Pages do not).

The code is `port/linux/src/modstore.c` (the catalog, the checks, the downloads, on a thread of its own),
`port/linux/game/menu_store.inl` (the screen), `tools/port_settings.py` (`_mod_store`, the screen's XML),
`tools/modstore_catalog.py` (the owner's tool), `tools/modstore_server.py` (a local server for trying it) and
`tools/test_modstore.py`, `tools/test_store_menus.py` (the tests).

## What a catalog is

Two or three kinds of file, all at addresses relative to the catalog's directory:

```
catalog.json            the list of mods
catalog.json.sig        its Ed25519 signature (128 hex digits and a newline)
mods/<id>/<version>/files.json      one mod's manifest: its files, with sizes and SHA-256
mods/<id>/<version>/mod.toml        and the files themselves, at their paths in the mod's folder
mods/<id>/<version>/tags/...
```

`catalog.json`:

```json
{
 "format": 1,
 "name": "OpenCE-U mods",
 "serial": 7,
 "mods": [
  {
   "id": "hd_audio",
   "name": "High-res audio",
   "author": "Ann",
   "version": "1.0",
   "description": "The game's sounds at 44 kHz",
   "kind": ["sounds"],
   "licence": "CC-BY-4.0",
   "format": 1,
   "min_build": 0,
   "size": 4597,
   "manifest": {"path": "mods/hd_audio/1.0/files.json", "size": 544, "sha256": "..."}
  }
 ]
}
```

- `serial` goes up with every catalog you publish. A game that has seen a serial refuses a lower one (a replayed old
  catalog), and keeps the highest in `mods/.store-serial`.
- `id` (lower case letters, digits, `_`, `-`, 40 at most) is the mod's folder name. A mod's `mod.toml` may not
  name another `id`.
- `licence` is required. `size` is the sum of the files' sizes (128 MB at most). `format` is the mods format the
  mod is made for (the game's is 1); `min_build` the build it needs, 0 for any (a build with no number plays them).
- `manifest.size` and `manifest.sha256` are the manifest file's own: the signature covers every byte that is
  installed, by way of them.
- A mod of the catalog with anything wrong in its entry is left out (and said in the log), not the whole catalog.

`files.json`: `{"format": 1, "id": "...", "version": "...", "files": [{"path": "mod.toml", "size": 123, "sha256": "..."}, ...]}`.
At most 256 files of 64 MB, `mod.toml` among them. A file's path is `mod.toml`, `LICENSE`, `LICENSE.txt`,
`LICENSE.md`, `README.md` or `README.txt`, or `tags/<folders>/<name><extension>` with an extension of a group the
game plays (`.sound .bitmap .unicode_string_list .string_list .model .gbxmodel`), in letters, digits, spaces and
`_-.(),'+!@&=~` (64 per component, 180 in all, 16 deep, nothing beginning with a dot or a space or ending in one).

## Making and signing one

```sh
python3 tools/modstore_catalog.py keygen --out ~/.opence-store-key      # once; prints the public key
mkdir src && cp -r my_mod src/hd_audio                                  # a folder for each mod: mod.toml, tags/, store.toml
MODSTORE_SIGNING_KEY=$(cat ~/.opence-store-key) python3 tools/modstore_catalog.py build src web
python3 tools/modstore_catalog.py check web --key <public key>
```

`store.toml` in a mod's folder (it is not published) holds `licence`, `redistributable = true` (the tool refuses a mod
without both) and optionally `origin` and `author`. The tool also refuses a file the game would not play, a tag
file whose header the loader would not accept, a link, two files that differ only in case, a `mod.toml` naming
another id, and an `origin` that is the Steam Workshop. `build` writes `web/catalog.json`, `web/catalog.json.sig`
and `web/mods/...`: put the folder on the server. `build` again with the same `web` raises the serial by one.
`sign FILE` signs any file the same way. The secret key (the seed) is never printed; keep it off the server and out of
the repository. The signature is made with `tools/ed25519_ref.py` (slow, not constant-time): for a catalog signed
on your own machine.

If the secret key is lost or stolen, the players' `store_key` (or a build's key) has to be changed: the key is
what a hostile host cannot forge. Keep a copy.

To try it on one machine: `python3 tools/modstore_server.py web` and `store_url = "http://127.0.0.1:8765/catalog.json"`.

## Where to host it

Any web server that gives the files at their own addresses, with `Range` requests (the game asks for a file of more
than 1 MB in pieces of 1 MB; a server without ranges can carry files up to 1 MB only, and the game says so):

- a GitHub repository's raw files: `https://raw.githubusercontent.com/<owner>/<repo>/<branch>/catalog.json`
  (no redirect; 100 MB at most a file, a repository should stay under 1 GB). **Not verified for the console**: that
  the Wii U's list of certificate authorities (`port/wiiu/src/wiiu_update_roots.h`) covers this host's chain, and that
  the host answers `Range` requests the way the game needs. Check with the console checklist in
  `reports/research/2026-11-10/modstore-result.md` before announcing a store.
- GitHub Pages, or any HTTPS host with one of the Wii U's roots.
- Not GitHub's release assets: they answer with a redirect, which the Wii U's request code does not follow.

## What the game checks, and what it does not

Checked before anything is kept: the catalog's signature (nothing in it is read before), its serial, every entry; the
manifest's size and SHA-256 against the signed catalog; every path, size and count; the free space on the card; then
for each file its size and SHA-256 as it arrives and, for a tag file, the header the loader itself checks (group,
version, header size, signature); the mod.toml (TOML, its id, its format). A mod that fails any of these is not
installed, what was written is deleted (or kept as a staging folder to go on from, if the player stopped it or the
connection failed), and the screen says why in words. Nothing downloaded is ever run: a mod is data the loader
reads, only through the paths above, and a folder in `mods` that the store did not make (it has no `.store` file) is
never replaced, updated or removed.

**Not checked**: the contents of a tag file beyond its header (the loaders check those as they read them; a review
of their hardening is in the result report). A mod is trusted as far as the catalog's owner is: the signature says the
owner published the bytes, not that they are harmless. A mod can change what the player sees and hears (and, in a
network game, that is on that player's machine only).

In a network game: mods play on the machine that has them only (docs/mods.md); there is no check that machines
have the same mods. A client sends its mods' folders and versions to the host when it joins (logged there, not used
to refuse anyone). Visual mods (textures, models) can give a player an advantage in a competitive game; the store
does not change that.

## Legal

The project ships no game data. Mods are third-party works: list a mod only if its licence lets you redistribute it
(the catalog tool demands a licence and `redistributable = true`, which are your statement, not a check). Items
from Steam Workshop for the Master Chief Collection are not ours to give and must not be fetched or catalogued
(`reports/research/2026-11-06/features/mcc-files.md`, section 4).
