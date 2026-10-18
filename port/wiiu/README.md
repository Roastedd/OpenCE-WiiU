# Wii U port

Status: **the game does not run on a Wii U yet.** What exists is the groundwork: the code compiles for
the console's CPU and ABI, a probe measures what Cafe OS allows, and the big-endian work that
makes the game's data and logic correct on a PowerPC runs (and agrees with x86 tick by tick) on
Linux under an emulator. The platform layer that would let it link and start, and the renderer, are
not written.

| | |
| --- | --- |
| `abi.mk` | The code generation flags the game's code is built with on the Wii U, for every compiler. |
| `probe/` | A Wii U application that measures the console (memory mapping, FPU, unaligned access, speed). See its README. |

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

All of this is checked by `tools/wiiu/verify.sh`, which runs on a Mac and needs no console.

- **782 of 811 sources compile** for the Wii U (the 811 are the big-endian Linux build's, third-party
  libraries included). The 29 that do not are listed with their reasons in
  `tools/wiiu/survey-known-failures.txt`: SDL, OpenGL and ALSA glue, `sys/mman.h`, `execinfo.h`,
  `ifaddrs.h`, `dlfcn.h`, two POSIX details newlib lacks, and mbedtls's platform modules.
- **No object contains a fused multiply-add**, `fsqrt` or a paired single. A fused multiply-add rounds
  once where x86 rounds twice, and every machine of a network game must simulate the same floating
  point. (The check found a real one on the way: `source/ai/ai.c`, fixed in `769ed33`.)
- **The tag layouts are identical** for the Linux big-endian ABI and for the Wii U's (EABI, newlib),
  so a map is converted once for both.
- The maths the game uses (musl, `halo_sin` and the rest) gives the same checksums on 64-bit ARM, 32-bit
  x86 and 32-bit big-endian PowerPC.

## What it takes to link

Linking the 782 objects against the Wii U's libraries (devkitPPC's newlib, libsysbase and libpthread, libm, libgcc and
wut's) leaves **337 undefined symbols**, by the linker's own count (`tools/wiiu/link_survey.py`). They are what
the Wii U's platform layer has to provide:

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

The same list again, after any change: `tools/wiiu/survey.sh -O2 keep`, then `tools/wiiu/link_survey.py`.

## What waits for the probe

`probe/` decides these, and nothing that depends on them is written before its report is back:

- whether the 128 MiB at `0x80000000` can be had as it is, or the maps are converted with every tag
  pointer rebased (plan 6.2). This is `xbox_memory.c`'s port and the converter's `--base`;
- whether the game's odd-offset reads of animation data need the platform layer's help;
- which core runs what, from the per-core numbers;
- whether the floating point on the console matches (FPSCR, denormals, the maths checksums).

## What does not wait

The lifecycle every Wii U app needs, from CoffeeFlix (ProcUI release/acquire/exit callbacks and an
exit watchdog, a heartbeat and a black box, logging from a worker thread so that no SD card or
network call blocks the main thread) is the next piece of the shell, with the build integration
(`configure.py --linux-target=wiiu`, which would put the survey's compile and the gates in the build).
