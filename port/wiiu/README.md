# Wii U port

Status: **the game does not run on a Wii U yet.** What exists: the whole game compiles for the console's CPU and ABI and
links into an RPX (with stubs for the platform layer that is not written), a probe measures what Cafe OS allows, and the
big-endian work that makes the game's data and logic correct on a PowerPC runs (and agrees with x86 tick by tick) on Linux under
an emulator. The platform layer that would let the RPX start, and the renderer, are not written; nothing here has run on a
console.

| | |
| --- | --- |
| `abi.mk` | The code generation flags the game's code is built with on the Wii U, for every compiler. |
| `probe/` | A Wii U application that measures the console (memory mapping, FPU, unaligned access, speed). See its README. |
| `src/` | The Wii U's own platform units (`wiiu_tls.c` so far; see its README). |
| `stubbed_symbols.txt` | The symbols the link still lacks, as the build last found them. |

## Building

In the `opence-wiiu` container (`tools/wiiu` in the repository this port is developed in; `tools/wiiu/build_game.sh` runs this
in it):

    python3 configure.py --linux-target=wiiu     # replaces the Linux build in build.ninja
    ninja linux_wiiu                             # build/linux-wiiu/halo.rpx, halo.elf, halo.sizes.txt, wiiu_stubs.txt

The graph is `tools/wiiu_build.py`; its flags are `tools/linux_build.py`'s (the big-endian Linux build's, less the Linux
target), not copies of them, and `test_wiiu_build.py` checks that. What it does:

- compiles the game, `port/linux/game`, the platform layer and the third-party libraries with `wiiu-clang`, less the sources
  `WIIU_EXCLUDE` lists (29, each with its reason) and the miniupnpc library;
- **checks every object as it is made** (`tools/wiiu_check_objects.py`): a fused multiply-add, `fsqrt` or paired single fails the
  compile of the file that has it;
- generates stubs (`tools/wiiu_stubs.py`) for every symbol the linker still finds undefined, each a function that reports its
  name (`OSReport`, `OSFatal`) and stops, so that the link is complete, the RPX starts the game, and the first thing the game
  needs that is not there ends the run with its name on the screen; writing the real function removes its stub;
- links with devkitPPC's GCC driver and wut's specs, checks the link (`tools/wiiu_link_check.py`: weak references that nothing
  defines, newlib's wide-character functions, which assume a 32-bit `wchar_t`), makes the RPX with `elf2rpl`, and reports the
  sizes against wut's memory map (`tools/wiiu_size_report.py`).

No link-time optimisation and no profile: devkitPPC's linker does not read LLVM bitcode.

## The toolchain

Wii U homebrew is built with devkitPPC (GCC, newlib, `elf2rpl`, `wuhbtool`) and wut (the Cafe OS
headers and import libraries). The game is compiled with **clang** (`--target=powerpc-unknown-eabi
-mcpu=750`) against devkitPPC's newlib and wut headers, and linked with devkitPPC's linker and wut's
linker script, for these reasons:

- the Linux builds, including the big-endian one that has been compared with x86, are clang; the game
  takes its meaning from it (the MSVC extensions, and the order function arguments are evaluated in,
  which the AI's random draws depend on), and one front end on every target keeps the netcode's
  determinism where it has been shown to be;
- the same flags then mean the same on every machine (`abi.mk`).

`tools/wiiu/Dockerfile` builds the container (`opence-wiiu`) from the images of devkitPPC and of the
big-endian loop; `tools/wiiu/wiiu-clang` is the compiler, with the two things devkitPPC's toolchain
needs of its objects (no `.note.GNU-stack`, no `.llvm_addrsig`).

## What has been established

All of this is checked by `tools/wiiu/verify.sh` (about two minutes), which runs on a Mac and needs no console.

- **The whole game compiles for the Wii U and links.** 784 objects (435 of the game, 69 of the platform layer's and the port's
  game units, 278 third-party), 29 sources left out for now (SDL, OpenGL and ALSA glue, `sys/mman.h`, `execinfo.h`, `ifaddrs.h`,
  `dlfcn.h`, two POSIX details newlib lacks, mbedtls's platform modules: `WIIU_EXCLUDE` says which and why, and
  `tools/wiiu/survey_gate.py` fails if one of them compiles or another does not). The RPX is 3.4 MB and `readrpl` accepts it.
- **No object contains a fused multiply-add**, `fsqrt` or a paired single. A fused multiply-add rounds once where x86 rounds
  twice, and every machine of a network game must simulate the same floating point. (The check found a real one on the way:
  `source/ai/ai.c`, fixed in `769ed33`; the build fails on it without the fix.)
- **Sizes** (`halo.sizes.txt`, a debug build): code 4.19 MiB of the 224 MiB region; static data 23.1 MiB of the 800 MiB region (`.bss`
  19.5 MiB, `.data` 2.9 MiB, `.rodata` 0.6 MiB). The largest static objects are `custom_edition_maps_globals` (6.4 MB),
  `tag_validate_claims` (3.0 MB), `p2p` (2.7 MB) and `profile_globals` (1.1 MB). The game's 128 MiB Xbox window is not static
  data, so it is outside these numbers.
- **The tag layouts are identical** for the Linux big-endian ABI and for the Wii U's (EABI, newlib), so a map is converted
  once for both.
- The maths the game uses (musl, `halo_sin` and the rest) gives the same checksums on 64-bit ARM, 32-bit x86 and 32-bit
  big-endian PowerPC.
- **An RPX has no thread-local storage.** `elf2rpl` refuses the TLS relocations (the game has `__thread` variables: the wide
  string state, Winsock's last error, the assertion skipper). The build compiles with `-femulated-tls`, which makes libgcc keep
  them behind pthread keys; but newlib's pthread keys call `__syscall_tls_*`, which wut 1.9.1 does not define, and without
  them `pthread_key_create` returns ENOSYS and libgcc aborts at the first access. `src/wiiu_tls.c` defines them over the thread
  specific slots Cafe OS gives every thread. It links; it has not run on a console.

## What it takes to run

The link needs **337 stubs** (331 functions, 6 tables: `stubbed_symbols.txt`), which are what the Wii U's platform layer has to
provide, by the linker's own count:

| Symbols | What | Approach |
| --- | --- | --- |
| 105 | Direct3D 8 (`D3DDevice_*` and the rest) | a null renderer first (the headless milestone), then GX2 (plan 6.4) |
| 50 | DirectSound (41, `dsound_sdl.c`) and voice chat (9, `voice_audio.c`) | AX; voice later |
| 33 | the Win32 and XAPI shims of `xbox_kernel.c` (threads, events, mutexes, ticks), which does not compile (`sys/mman.h`) | over `OSThread` and friends |
| 43 | `posix_*`: files, the command line, network, Discord | newlib's devoptab has most; the network is nsysnet |
| 27 | the port platform layer (`platform_*`, `halo_linux_*`: input, audio devices, bindings) | VPAD/KPAD |
| 21 | `halo_*` hooks the SDL and GL files provide (screen size, menus, keyboard) | with the renderer and input |
| 15 | XAPI devices (`XInitDevices`, the device tables) | small |
| 10 | `config_*` (`port_config.c`, which uses SDL for the base path and to load files) | the same code with those two replaced |
| 14 | UPnP (9, optional) and mbedtls's platform functions (5: entropy, time, zeroize) | UPnP out; mbedtls needs its three platform hooks |
| 19 | the high-resolution HUD and text (GL), `memory_watch`, the test input, `pread`/`pwrite` (newlib has neither), `d3d_find_flipcount` | with the renderer; two small shims |

A unit that replaces an excluded source goes in `src/` (its README says how), and the symbols it defines come off the baseline:
`python3 tools/wiiu_stubs.py --compare build/linux-wiiu/wiiu_stubs.txt port/wiiu/stubbed_symbols.txt` says which.

## What waits for the probe

`probe/` decides these, and nothing that depends on them is written before its report is back:

- whether the 128 MiB at `0x80000000` can be had as it is, or the maps are converted with every tag
  pointer rebased (plan 6.2). This is `xbox_memory.c`'s port and the converter's `--base`;
- whether the game's odd-offset reads of animation data need the platform layer's help;
- which core runs what, from the per-core numbers;
- whether the floating point on the console matches (FPSCR, denormals, the maths checksums).

## What does not wait

The lifecycle every Wii U app needs, from CoffeeFlix (ProcUI release/acquire/exit callbacks and an exit watchdog, a heartbeat and
a black box, logging from a worker thread so that no SD card or network call blocks the main thread) is the next piece of the
shell; then the Win32 and XAPI shims, the files, and the null renderer.
