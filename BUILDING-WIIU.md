# Building the Wii U application

The source and toolchain support are included here. No Xbox SDK or game data
is required to compile the application. Maps are required to play it.

## Docker toolchain

Install Docker, then run from the repository root:

```sh
docker build -t opence-ppc-be tools/ppc-be
docker build -t opence-wiiu tools/wiiu
RELEASE=1 tools/wiiu/build_game.sh
```

The first image supplies clang 19, LLVM, Python, Ninja, and the big-endian
validation toolchain. The second adds devkitPPC and wut from devkitPro's
image and the `wiiu-clang` wrapper. Image tags are mutable: these commands
provide the development toolchain, not a promise of a byte-identical rc1
binary. The archived rc1 checksums are in [VALIDATION.md](VALIDATION.md).

The build writes `build/linux-wiiu/halo.wuhb`, `halo.rpx`, and `halo.elf`.
`RELEASE=1` enables the release assertion policy and whole-module
optimization used by the release build procedure. Object instruction and
link checks run as part of the build. Retain the ELF to symbolize crashes.
The helper restores a PowerPC Linux build configuration when it exits;
run it again for a later Wii U build.

The equivalent commands inside the `opence-wiiu` container are:

```sh
python3 configure.py --linux-target=wiiu --release --wiiu-lto
ninja linux_wiiu
```

For asset-free checks of the release tooling on the host:

```sh
python3 -m pytest -q tools/test_wiiu_release.py tools/test_wiiu_update.py
```

## Playing and packaging

Building the executable does not generate `shaders.pack`. A playable release
needs the matching shader pack and network broker list; see the
[player guide](port/wiiu/PLAYING.md) and [port internals](port/wiiu/README.md).
The shader compilation workflow has additional GPU shader compiler
requirements. Do not substitute a pack from another build without checking
its coverage and compatibility.

The user's Xbox maps, disc image, saves, and local configuration must stay
out of source control and release archives. Preserve the licence and
third-party notices when distributing binaries, and provide the matching
source and these build instructions.
