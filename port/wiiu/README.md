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
| `probe_gx2/` | A Wii U application that asks the GPU's questions: CafeGLSL on the console, BC1/BC2/BC3 byte order, little-endian vertex streams, index types. See its README. |
| `src/` | The Wii U's own units: the shell (lifecycle, log, black box, watchdog), thread-local storage, 64-bit atomics, the C library's gaps. See its README. |
| `tests/` | Desktop tests of the shell's portable parts (`tools/wiiu/shell_tests.sh`: ARM64, x86, big-endian PowerPC). |
| `stubbed_symbols.txt` | The symbols the link still lacks, as the build last found them. |

## Building

In the `opence-wiiu` container (`tools/wiiu` in the repository this port is developed in; `tools/wiiu/build_game.sh` runs this
in it):

    python3 configure.py --linux-target=wiiu     # replaces the Linux build in build.ninja
    ninja linux_wiiu                             # build/linux-wiiu/halo.wuhb (for Aroma), halo.rpx, halo.elf, halo.sizes.txt, wiiu_stubs.txt

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

## The shell

`src/wiiu_shell.c` runs around the game (`wiiu_shell.h` is what the rest of the port calls). The system's `main` is
`wiiu_main.c`; the game's own (`shell_xbox.c`'s) is built as `halo_game_main`, as the web build's is, and runs on a thread
of its own on core 1 with an 8 MiB stack, so that the main thread can do what the system expects of it: answer HOME and closing
every twenty milliseconds. Patterns from CoffeeFlix and CoffeeUnlocked, which have run on this console:

- **ProcUI**: release (HOME) at priority 2000, so that it runs before other libraries' callbacks, acquire, exit and the network
  stop and start, each logged; the game is told through `wiiu_in_foreground()` and `wiiu_exit_requested()`.
- **The exit watchdog**: once the system asks the app to close it has ten seconds; a watchdog thread (at a higher priority than
  the game) ends the process at the deadline, after a moment for the log to say so.
- **The stall report**: after the game's first frame (`wiiu_frame()`), a loop that stops for 3 s while the app has the foreground
  is told in the log, again at 8 s, 13 s, and so on up to twelve times; HOME does not count.
- **The log** (`/vol/external01/wiiu/apps/halo/log.txt`): lines from any thread go into a queue; a worker thread appends them to
  the card every tenth of a second. No thread but that one ever waits for the card; a full queue drops the new line and says how
  many it dropped.
- **The black box** (`blackbox.txt`): every five seconds a thread writes where every registered thread is (state, program counter,
  return address, the lock it waits for and who holds it), the phase and frame count, the log's counts and the heap. The file
  holds the last three records and is removed by a clean stop; one found at the next start is reported in the log.
- **The crash note** (`crash.txt`): a fault handler writes the thread, phase, program counter, stack and registers by hand.

The decisions and the files (the queue, the watch, the records) are plain C with pthreads, tested on the desktop under
`tools/wiiu/shell_tests.sh`; the Cafe OS half (ProcUI, `OSThread`, the handler) has been compiled and linked, not run.

## Controls

The GamePad is the first controller (a Pro or Classic Controller on channel 0 joins it); Pro and Classic Controllers on channels 1 to 3 are the other
three (split screen). `src/wiiu_input.c` reads VPAD and KPAD; `src/xinput_wiiu.c` turns them into the Xbox pad the game reads.

| Game | Wii U | Menus |
| --- | --- | --- |
| Move / look | Left stick / right stick | Left stick or D-pad move |
| Fire | ZR | |
| Throw grenade | ZL | |
| Jump | B (the bottom button) | A selects |
| Melee | A (the right-hand button) | B goes back |
| Action, reload | Y (the left-hand button) | Y: the screen's Y prompt (create, ...) |
| Switch weapons | X (the top button) | X: the screen's X prompt (delete, ...) |
| Flashlight | L | |
| Switch grenades | R | |
| Crouch / zoom | Click the left stick / the right stick | |
| Pause (Start) | + | |
| Back (Xbox's Back) | − | |

In the game the face buttons are the Xbox's by **place**, so that jump is under the thumb where it is on an Xbox pad; the prompts on the screen show
the Wii U's letter for each place (`port_button_icon` in `hud_hires_tags.c` swaps the icons: the prompt for the bottom button says B). In the menus
they are matched by **letter** (A selects, B goes back, as on every Nintendo console), because the menus' own prompts say "A = select"; the match
changes as a menu opens and closes (`input.wiiu_face_buttons` `"auto"`; `"position"` or `"label"` keep one match everywhere). The Xbox's black and
white buttons are R and L, its triggers ZR and ZL (buttons on the Wii U, so all or nothing). The GamePad and the Pro Controllers rumble
(`input.wiiu_rumble`).

Measured on the console with the debug server's `press` and `stick`: the d-pad and A and B move through the menus and start a level, the right stick
turns the view in the first level, and the face buttons change match as a menu opens (the log says "Input: the face buttons are now matched by ...").
The GamePad's touch screen is the menus' pointer (`halo_ui_pointer_update` in `d3d8_gx2.c`, with `touch_menu.c` from the Android build): a tap on an item
or on a screen's A and B prompts clicks it, a drag scrolls a list. Measured with the debug server's `tap`: tapping Multiplayer, then Split Screen, opened
each. The menus are drawn at the left of the picture, not centered in its 852 columns (the renderer does not apply the offset the game asks for:
`MENUS_DRAWN_CENTERED` in `d3d8_gx2.c`), and a touch follows what is drawn. Not measured: rumble (it cannot be seen), the Pro and Classic
Controllers (none was connected), split screen play, a real finger (the calibration is the system's, to 854x480). Also written, not run on a
console: the GamePad's gyro aiming (`input.wiiu_gyro_aim` "always" or "zoom"; off by default; the axes' signs and scale are unmeasured), a tap outside the
menus that skips a cinematic, and Classic Controllers. Not done: Wii Remote and Nunchuk.

## The network

`src/wiiu_posix_net.c` is `posix_net.c` on wut's BSD sockets (nsysnet), under the game's Winsock layer (`xnet.c`), so system link and internet play have a
network. Run on the console (the first lines of the log's `Net:` trace, up to 120): the sockets open and bind, system link's search sends its 16-byte
broadcast to 255.255.255.255:5150 every two seconds and it reaches the LAN (seen arriving on a Mac), and internet play's STUN requests (Google's and
Cloudflare's, UDP) go out; the System Link Play entry of the Multiplayer menu is enabled; closing with the sockets in use is clean. Calls fail as "network
is down" while the app is in the background, closing, or told by the system to stop using the network (`wiiu_network_allowed` in the shell). Hosting works
end to end over the loopback (the host's own client connects to its listening socket): System Link, Create Game, Battle Creek, Slayer, the lobby and a
match that starts, with the HUD and the controls answering (ZR fires, the stick moves and turns). Not measured: a game found or joined from a second
machine. `network.wiiu_sockets` turns the sockets off.

## The debug server

`src/wiiu_debug.c` is a small server inside the game's own shell (TCP port 4310, started once the game's thread runs) so that a change can be
run, looked at and judged without a person at the console; its client is `tools/wiiu/wiiu_debug.py`. The lines are read by
`wiiu_debug_protocol.c` (plain C, tested on three machines: `tests/test_debug.c`).

- `hello` (frames, foreground, memory), `log N` (the last lines of the shell's log), `press BUTTONS MS` and `stick LX LY RX RY MS` (merged into
  the GamePad's port in `wiiu_input_read`), `screenshot tv|drc|back [1|2|4]`, `quit`.
- **A picture** is taken by the game's own thread, at the start of its next present (`wiiu_debug_present`, called from `wiiu_gx2.c`'s
  `present`), because only it may use the GPU: the GPU copies the colour buffer into a linear surface and the server sends the pixels. The
  console's network takes about 400 KB a second, so a 1080p picture (8 MB) takes 20 s; `--scale 2` sends every second pixel and row (5 s).
  On the TV and GamePad it is the frame before the one being drawn; `back` is the game's own render target, as it was just drawn.
- **The sockets** are open only in the foreground and while the system allows the network (the server closes them on HOME, on closing and on
  the network-stop callback); in the background it touches nothing.
- **The log mirror**: with the Mac's address in `halo/debug_log_host.txt` ("192.168.1.10"), every log line also goes to it as a UDP datagram
  (port 4311; `wiiu_debug.py listen` prints them). Unlike the card's file, it shows the last lines before a freeze.
- **`quit` is the one that does not work well.** It closes every socket and asks for the Wii U Menu (`SYSLaunchMenu`) from the game's thread;
  the app does close (the release and exit callbacks run, "Closed" is logged), but under Aroma the console then started the app again once, and
  twice stayed on the Wii U logo. HOME and Close software is the way out that has always worked.

## What is written and not run on a console

Compiled, linked and tested on the build machine where it can be, not run on the Wii U: the high-res text (`text_hires.c`: the fonts in the maps' tags replaced by
Overpass at the display's resolution; `tools/wiiu/shell_tests.sh` rasterizes glyphs from the big-endian embedded fonts under qemu), UPnP (miniupnpc built with
`include/wiiu_net_compat.h`; a router's port forwarding for internet play, and `posix_upnp.c`'s port and address byte order fixed for big-endian), the GamePad's
gyro aim (`input.wiiu_gyro_aim`), tap-to-skip and Classic Controllers. The high-res HUD and menu pictures (`hud_hires.c`, `menu_files.c`) are not ported: they
draw meters through shader variants that are not in the shader pack, which is made from what a console run asked for (`halo/shaders_missing.txt`).

## What it takes to run

The link needs **300 stubs** (`stubbed_symbols.txt`), which are what the Wii U's platform layer still has to provide, by the
linker's own count:

| Symbols | What | Approach |
| --- | --- | --- |
| 108 | Direct3D 8 (`D3DDevice_*`, `Direct3DCreate8` and the rest) | a null renderer first (the headless milestone), then GX2 (plan 6.4) |
| 50 | DirectSound (41, `dsound_sdl.c`) and voice chat (9, `voice_audio.c`) | AX; voice later |
| 43 | `posix_*`: files, the command line, network, Discord | newlib's devoptab has most; the network is nsysnet |
| 19 | the port platform layer (`platform_*`, `halo_linux_*`: input, audio devices, bindings) | VPAD/KPAD |
| 21 | `halo_*` hooks the SDL and GL files provide (screen size, menus, keyboard) | with the renderer and input |
| 16 | XAPI devices (`XInitDevices`, `XInput*`, the device tables: 11), and the memory window's `XPhysicalAlloc` and friends with `VirtualProtect` (5) | the devices are small; **the window waits for the probe** |
| 10 | `config_*` (`port_config.c`, which uses SDL for the base path and to load files) | the same code with those two replaced |
| 14 | UPnP (9, optional) and mbedtls's platform functions (5: entropy, time, zeroize) | UPnP out; mbedtls needs its three platform hooks |
| 19 | the high-resolution HUD and text (GL), `memory_watch`, the test input, `pread`/`pwrite` (newlib has neither), `d3d_find_flipcount` | with the renderer; two small shims |

Done: the Win32 and XAPI kernel shims (`xbox_kernel.c`: handles, events, mutexes, critical sections, threads, APCs, time, `Global*`
and `Local*`, debug output) compile for the Wii U as they are, over pthreads, which wut provides (the only changes: an unused
`<sys/mman.h>` is not included, and the log goes to the shell's); `platform_request_quit` (the menus' Quit) is the shell's;
`wiiu_atomic.c` (the 64-bit atomics the 750 core lacks), `wiiu_libc.c` (`pause`, `sysconf`) and `wiiu_tls.c` fill what newlib and wut
do not have.

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

The files (`posix_files.c`'s `UTIME_OMIT`, `pread` and `pwrite`), the null renderer, and the Wii U's input; each replaces stubs.

## Owned missing-shader capture

`gx2_shaders.c` queues one attempt per `(stage, lookup hash)` from its single
GX2 command consumer. The entire seen-table/program scan and copy is covered by
a nonblocking observation gate. Pixel records own the canonical 256-byte wire;
vertex records own up to 136 four-word instructions and retain the raw program
hash separately from the lookup hash. The existing program registry publishes
immutable, process-lifetime data with a release/acquire count. No file work,
allocation, sleeping or mutex waits occur in the missing-key producer.

The main shell log worker services one bounded capture turn after each normal
log batch, including idle turns. The independent error-log worker has no capture
hook. Static storage holds 64 records (2240 bytes each) and a 16 KiB text batch.
A turn selects at most eight records or performs one filesystem operation;
individual writes are at most 4 KiB. Kernel filesystem latency itself remains
unbounded. Ordinary log batches always receive the next turn.

Complete batches are synced, closed and renamed from
`shader-misses-<16 hex run ticks>-<4 decimal batch>.txt.part` to `.txt` in the
application directory. V/P record text remains compatible with the shader pack
generator. A unique run/batch namespace and exclusive part creation avoid
reusing old batches. The directory scan is capped at 4096 entries, and 128 files
with this owned prefix (including incomplete parts) stop further capture.
Nothing is automatically deleted. The application owns this namespace; do not
write competing files into it while the game runs.

A full queue drops new unique keys; a missing vertex payload, ownership
violation or storage failure is counted. Deduplication records attempts, not
persistence. Summaries distinguish accepted, written, loss, drained and complete;
`complete=1` requires producer stop, no active observation or I/O turn, an empty
queue/batch and no recorded capture loss. It certifies only observations admitted
before stop, rather than campaign-wide shader coverage. A storage failure stops
capture and leaves any incomplete part as evidence. Only the main thread emits
the capture summary before `Closed`; worker turns never queue automatic summaries
that could resume behind that final attestation.

HOME atomically suspends new capture turns and retains all buffers/descriptors;
resume continues the owned job. Final close stops admission, waits at most
300 ms for capture independently of normal log flush, then permanently quiesces
new worker turns. A syscall or producer already active can finish with its
static storage intact. An expired deadline is reported incomplete and never
permits a free, reset or blocking join.

Run `python3 tools/test_wiiu_missing_keys.py -v` for source-based queue,
producer/formatter, actual shell-close, worker-fairness and sanitizer checks.
The test accepts `MISSING_TARGET` and `MISSING_RUNNER` for PowerPC Linux/qemu;
Linux32 tests use 64-bit file metadata to avoid host inode overflow. Production
Wii U units require the existing strict object audit too. Native gameplay,
HOME/close and hitch/audio comparison remain separate deployment gates.

The development repository's `tools/wiiu/gx2_run.py --collect-only` collects
committed `.txt` batches into `runs/wiiu/last-run/shader-misses/` and merges unique
validated lines into `runs/wiiu/missing.txt`, the existing shader-pack input.
It excludes `.part`, preserves remote files and older backups, and reports
collection errors. `tools/wiiu/test_missing_keys.py` tests that host workflow
offline. Manual collection must follow the same final-file-only rule; fetch
complete batches after the game has closed, inspect capture status, and retain
incomplete parts separately for diagnosis.
