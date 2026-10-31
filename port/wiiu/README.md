# Wii U port

Players: see [PLAYING.md](PLAYING.md) for installing and controls.

Status: **experimental game builds run on real Wii U hardware.** Menus,
campaign gameplay through A30, the native GX2 renderer, controls and sound
output have hardware test records. Full campaign, multiplayer, HOME edge
cases and sustained 60 FPS remain qualification work; a successful probe or
build does not establish those results.

Much of the bring-up text below records the earlier groundwork and retains
historical limitations. Check the current source and revision-specific test
records before treating an old "not run" statement as current status. The
probe directories remain useful hardware and portable regression helpers;
their sources are reused by later tests and do not need shipping with every
game build.

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

The GamePad is the first controller, player 1. The Pro and Classic Controllers take the players' places as `input.wiiu_controller_layout` says:
`"auto"` (the default) has every controller play as player 1 with the GamePad while one person plays, and gives each its own player, 2 to 4, once a
second joins (ADD PLAYER in the lobby, or CO-OP CAMPAIGN); `"separate"` gives each its own player always; `"merged"` is the layout before these, the
controller of the first KPAD channel joining the GamePad and those of the other three the other players. A controller keeps its player while it goes and comes back,
known by its Bluetooth address on whatever channel the system gives it (`src/wiiu_pad_players.c`, tested on the desktop), and its lights show the number
of the player it drives. `src/wiiu_input.c` reads VPAD and KPAD; `src/xinput_wiiu.c` turns them into the Xbox pad the game reads, and its comment says
how the menus add players and why "auto" waits for a second one.

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
each. The menus are drawn centred in the picture's 852 columns, with the offset the game asks for (`halo_screen_ui_offset`), and a touch follows what
is drawn (`MENUS_DRAWN_CENTERED` in `d3d8_gx2.c`; on 2026-10-22 the renderer still drew them at the left). Not measured: rumble (it cannot be seen), the Pro and Classic
Controllers (none was connected), split screen play, a real finger (the calibration is the system's, to 854x480). Also written, not run on a
console: the GamePad's gyro aiming (`input.wiiu_gyro_aim` "always" or "zoom"; off by default; the axes' signs and scale are unmeasured), a tap outside the
menus that skips a cinematic, and Classic Controllers. Not done: Wii Remote and Nunchuk.

## Split screen for three and four players

Two players have played on the console (the GamePad and a PS4 pad through Bloopair, `"separate"`, 2026-10-30). Three and four are written and
tested on the desktop only (branch `wiiu-split4`).

What three and four take, from the code:

- **Players.** Up to four local players in a network game this machine hosts or joins (split screen is the lobby of one: Multiplayer, Create
  Game, LAN), the controller of port n being local player n. The campaign's co-op stays at two: its level select and its saved game take one or two
  players (`ui_widget_event_handler_functions.c`'s `solo_level_set_next_map_name`, `game_state.c`), as on the Xbox.
- **Joining and controllers**: `input.wiiu_controller_layout` (Controls, above; `src/xinput_wiiu.c`'s comment). With `"auto"` and one player the
  lobby's line under its players says "Another controller: ADD PLAYER.", since a second person's START would be player 1's. The controllers stay
  players of their own while a player of this machine in the lobby is on a controller other than the GamePad, so the second of two who stays when
  the GamePad's player leaves keeps their controller (`menu_functions.c`'s `pc_menu_split_players`, which the desktop's lone gamepad follows too).
- **Layout.** `compute_window_bounds` (`main.c`) splits the title-safe frame at the middle of the wide picture: two players top and bottom, three
  with player 1 across the top, four in a 2x2 grid, in the local players' port order. The dividers are drawn at the wide picture's middle
  (`interface.c`), the motion sensor keeps four players' blips apart (`motion_sensor.c`, fixed for three and four before), and the engine leaves
  out the detail objects, the blur screen effects and the mirrors with more than one view.
- **HUD and text.** Each window's HUD is drawn at the full size, anchored to its window's corners, as on the Xbox, so a quarter is crowded; HUD
  messages use the HUD's multiplayer font, their button icons three quarters of their size (`hud_messaging.c`, followed by `hud_hires_tags.c`).
  The high-res text is sized by the screen's pixels for each of its 480 lines (`halo_screen_pixel_scale`), the same in every window. The
  scoreboard of a split view is the Xbox's, six rows, with its wide tab stops in any window whose title-safe frame is wider than 320 columns (a
  quarter's is about 360 on the 852-wide picture, its score column at 315).
- **Pause menus**: the 2p menu in a full-width window, the 4p one in a quarter (`ui_widget.c`). Found, not changed: the menus' centring offset is
  the whole picture's (106 columns) in every window, so a 4p pause menu, 320 wide, sits against the right edge of its 426-wide quarter, 53
  columns from its centre, and in the right-hand quarters against the picture's right edge, where a TV's overscan may cut it. The offset is set
  for a frame by the platform layer (`halo_screen_ui_offset`) and the GamePad's touch mapping assumes it, in every build; centring each window's
  widgets in its own share, as `hud_zoomed_layout_begin` does for the HUD, would take both.
- **Frame rate and detail**: `display.split_screen_detail` and `display.split_screen_fps` (`port/linux/game/graphics_quality.c`, `port/linux/README.md`).
  With three or four views the defaults leave out the object shadows, the lens flares and the level's specular shine and reflections, and hold the
  frames to 30 a second through `halo_interpolation_enabled` (`src/platform_wiiu.c`) while `display.interpolation` stays as the player set it. The
  overlay (L + R + −) shows `30HZ` while they are held. In another machine's game the host's rules put the lens flares and the level's specular
  parts back every tick (`cheats.c`'s `cheats_network_client_enforce`, as they do the performance mode's), so a client keeps only the shadows off;
  the split screen lobby is a game this machine hosts, which has the whole set.

### The console test plan

For the session that holds the console (one driver at a time). Build `wiiu-split4`, install it, and keep the card's `config.toml`: it has
`input.wiiu_controller_layout = "separate"` from the two-player tests, so set each row's layout by hand and put the owner's values back at the end, read back.
Controllers: the GamePad, a Pro Controller, a PS4 pad through Bloopair and a Wii Remote with a Classic Controller, each synced; mark a row not run
if one is missing. The log's lines: `Input: a Pro Controller on channel N (address) is player P's`, `Input: channel N connected` / `disconnected`,
`Input: channel N's lights show player P`, `Input: the controllers of channels 1 to 4 drive players a b c d (0: none)`, and at the start
`Input: KPAD channels 1 to 4 read (the system allows M)`. The log and this plan number the channels 1 to 4, as the system's lights do (KPAD's 0 to 3).

Controllers, on a small map (Chill Out) in a LAN game this console hosts:

1. `"auto"`, one player: with the four connected, every controller moves the main menu's cursor and every controller's lights show 1; a campaign level
   started with the Pro Controller plays as player 1 with it and with the GamePad.
2. `"auto"`, joining: Create Game, LAN, a map. The line under the players says "Another controller: ADD PLAYER." ADD PLAYER with the GamePad: the line
   says "New player: press START." and, once no controller is held, the controllers' lights show 2, 3 and 4 in the order they connected (the log's
   `drive players` line agrees). START on each, a profile each, START NOW. In the game each controller moves only its own player and only its own
   window answers it (one at a time, the others still); each pause menu (+) opens in its own window.
3. Reconnecting, in that four-player game: turn the PS4 pad off and on (or let it sleep): its window says the controller is unplugged, then its
   player plays on with it, with the same number in the log and on its lights. Take the Classic Controller out of the Wii Remote and put it back: the
   same. Turn two controllers off and on in the other order: each has its own player again, whatever channel the log gives it.
4. Back to one player: in a two-player lobby the GamePad's player leaves first (B): the Pro Controller's player stays in the lobby with their own
   controller (its lights still 2). Then they leave too, to the main menu. Once no controller is held the lights show 1 again and all drive the menus,
   and no "controller unplugged" message comes up for the ports they leave (`input_abstraction.c` shows one for a port that still has a player).
5. Co-op: CO-OP CAMPAIGN with the GamePad; the controllers' lights change to 2 to 4; player 2's profile chosen with the Pro Controller; the level plays
   with the GamePad as player 1 and the Pro Controller as player 2.
6. `"separate"`: the GamePad alone is player 1; START on a controller in the lobby joins without ADD PLAYER; rows 2 and 3 again.
7. `"merged"`: the controller of channel 1 shares the GamePad's player, those of channels 2 to 4 have their own ports, the lights show the channel numbers.
8. HOME during the four-player game and back: within a second every controller's lights show its player again.
9. (A fifth controller, if one is at hand: with `"auto"` and four players it shares player 1; with `"separate"` its lights are off and it drives nobody.)

Frame rate and detail, each on Blood Gulch and on Chill Out, Slayer, one to four players. For each count, three runs: before (`display.split_screen_detail
= "same"`, `display.split_screen_fps = "60"`), the reduced set alone (`"reduced"`, `"60"`), and the defaults (`"reduced"`, `"auto"`). Each run: 30 s with
every player standing still, then 30 s with every player firing; the median of the log's `frame time` lines over each 30 s, the frames over 34 ms, the
game thread's ticks and rendering (`frame parts`), the GPU's time, the overlay's `30HZ` or `60HZ`, and the hitch notes' waits (the dynamic index
buffer has four stores, renewed at each view's start, so at four views a store comes round again a frame later: waits there point at
`RENEWED_STORES` in `src/d3d8_gx2.c`). Also: the 2x2 and three-view layouts, each HUD in its window, a held BACK's scoreboard in each window, the
postgame's single screen (full detail, `60HZ`).

It passes when four players hold a median of 30 frames a second (33.3 ms) on Blood Gulch with the defaults, steadily (frames over 34 ms rare), and
one and two players are as before (two not slower than before). End with the owner's `config.toml` values back and the card left on the safe path.

What only the console can say: whether Bloopair takes three pads at once, alongside a Pro Controller and a Wii Remote; whether `WPADGetAddress` gives
each controller (Bloopair's included) its own address, not zeros; whether `WPADControlLed` lights a Bloopair pad and a Wii Remote with a Classic
Controller; on which thread the connect callback runs, and what `KPADReadEx` reports for a channel whose controller has gone; which channel the
system gives a controller that comes back; and whether the reduced set is enough for 30 frames a second with four views, or the particles (and with
them the contrails) are needed too.

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
`wiiu_debug_protocol.c` (plain C, tested on three machines: `tests/test_debug.c`). It answers anyone on the network, so it is for development
only: it starts when `halo/debug_server.txt` on the card says `1`, and is off without that file (the log says which).

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

## Dynamic resolution and the GamePad's view

Written and tested on the build machine (branch `wiiu-drs`), not run on a console yet.

- **One set of targets, drawn in part.** The screen's targets (the back buffer, the depth buffer, `render_primary_copy` on the depth buffer's
  memory) keep the size of `display.render_lines`, the ceiling. A frame at fewer lines draws into their top left part: `src/d3d8_gx2.c`'s
  `drs_fraction` is that part's share, viewports, scissors, clears and visibility counts follow it through `target_scale`, and a screen
  target sampled as a texture is scaled to it. `src/wiiu_gx2.c` keeps one target for every step (`target_get` leaves the active size out),
  clears a target whole when it is bound at another step than it was last drawn at (`target_step`), and blits only what was drawn. The
  share changes only between frames (`halo_screen_commit`).
- **The steps** are 1, 0.9, 0.8 and 0.75 of the ceiling: 720, 648, 576 and 540 lines, or 1080, 972, 864 and 810.
  `halo/drs_lines.txt` (a number of lines, 240 to the ceiling) fixes one for the whole run, with the controller off whatever
  `display.dynamic_resolution` says.
- **The controller** (`src/wiiu_drs.c`, run in `present()` on the GX2 thread) drops a step when the last 8 frames' GPU time averages over 0.9
  of the budget (16.7 ms, 33.3 at 30 frames a second) and the frame waited more than 1 ms for the flip; it raises one when the time
  predicted at the step above fits in 0.75 of the budget, or after 120 frames without a wait. It holds 30 frames after any change and 3 s
  after a drop, and is off while a menu has the screen (the menus draw every line). `display.dynamic_resolution` (off by default; the graphics
  mode Quality turns it on) and `display.dynamic_resolution_floor` (540) set it.
- **The HUD and text** are drawn at the step, the text from glyphs made for the ceiling (`halo_screen_pixel_scale` does not change, so the
  glyph atlas is not made again), at most 1.33 times smaller. The performance overlay is drawn on the screens and does not change.
- **The GamePad's view** (`input.wiiu_gamepad_view` `"player1"`): while the screen is split, the GamePad's blit samples the first window
  (`port/linux/game/gamepad_window.c`, main.c's `compute_window_bounds`) instead of the whole frame; no more is drawn.

The log says, at the start, `GX2: dynamic resolution: the frames draw N of the targets' M lines for this run (halo/drs_lines.txt)` when the
note is there, and at the first present (and each change of the settings) `GX2: dynamic resolution is on|off ...`; every 5 s, with the other
notes, `GX2:   dynamic resolution: N of M lines (...); since the last note D drops and R rises, W of F frames waited over 1 ms for the
flip, K had a menu; the GPU's time at this step ... ms on average over n frames; C targets cleared at a change of step`. The frame times'
percentiles are the line before it, `GX2:   frame times: median ..., 95th percentile ..., 99th ... ms`. The overlay shows the size drawn.

### The console's tests

For the session that has the console. Copy the build's `halo.wuhb`/`halo.rpx`; keep the card's other notes as that session's practice has
them. Each run: read `log.txt` afterwards.

1. **Each forced step in a10, a30 and Blood Gulch.** At 720 lines (the default), put `648`, then `576`, then `540` in `halo/drs_lines.txt`
   (one run each), and once `1080` lines (`display.render_lines = 1080`, or `halo/render_height.txt` 1080) with `810`. In each map, look at:
   - the HUD (meters, motion sensor, reticle, the text and waypoints): where it is at 720 without the note, only softer;
   - the scope (zoom in with a sniper and a pistol): the view and its blur line up, and no dark band at the right or bottom edge beyond a
     pixel or two (the blur's taps read the cleared pixels past the part drawn: note it if it shows);
   - night vision (the sniper rifle's scope in a dark place: inside a base in Blood Gulch): the green covers the whole picture;
   - lens flares (the sun in a30, lights in a10): as bright as without the note (their visibility counts are scaled to the step);
   - water (a30's shore) and active camouflage (Blood Gulch's power-up, or an Elite that has it in the campaign): the water's ripples
     and the camouflage's refraction line up with what is behind them;
   - the log: no `GX2: color target` or `GX2: depth target` line after the map's first frames, and every target of the screen's size
     (1280x720 or 1920x1080 pixels) says `in the eDRAM` as it does in a run without the note (at 1080 lines the three screen targets and
     the TV's buffers may not all fit in the 32 MB, before this as now: what matters is that the note changes nothing there); the 5 s line
     says `N of M lines (halo/drs_lines.txt; the controller is off)`.
2. **Two players at each step.** Controllers SEPARATE and a Pro Controller (or the PS4 one through Bloopair); split screen in Blood Gulch with
   each forced step: each player's view and HUD stay in their half, the black line between the halves is where it is, nothing of one half
   shows in the other.
3. **The controller.** No note; `display.dynamic_resolution = true`.
   - Graphics mode Quality (1080 lines at 30): in the main menu and Video Setup the 5 s line says `1080 of 1080 lines`, with every frame
     counted under `had a menu` and no drops or rises.
   - Quality in an a30 fight (the first beach): at most 5 drops and rises in each 5 s line (once a second), the 99th percentile of the
     frame times at most 34 ms, and the lines going back up when the fight is over.
   - Balanced (720 at 60) with dynamic resolution on, in Blood Gulch: `720 of 720 lines` throughout, no drops (the GPU takes about 4 ms).
     If it drops there, the controller is reading the game thread's lateness as the GPU's: say what the line's GPU time and flip waits were.
4. **HOME at a lowered step.** With `576` in `halo/drs_lines.txt` in a10, and once with the controller at a lowered step in an a30 fight:
   HOME, wait 10 s, back. The picture comes back as it was, the log says `GX2: back: N render targets are in the eDRAM again, 0 have no
   memory`, no new target line follows, and the 5 s line still says 576 (or the controller's step).
5. **The GamePad's view.** Wii U GamePad Setup, GAMEPAD SHOWS: PLAYER 1. Two players in Blood Gulch: the GamePad shows player 1's half,
   letterboxed, with nothing of player 2's at its bottom edge (with three players too: player 1 has the top half); with four players (if
   controllers allow) player 1's quarter fills it.
   Pause (a menu): the GamePad shows the whole picture and taps work. One player, or a cinematic: the whole picture. Once with `576` in
   `halo/drs_lines.txt`: the same, at that step.

Afterwards: remove `halo/drs_lines.txt`, set GAMEPAD SHOWS back to TV and the graphics mode back to what it was, and leave the card as the
session found it.

What the build machine could not settle: whether a wait for the flip tells a GPU that is behind from a display that holds a frame the GPU
finished early (the GX2 thread waits about 14 ms a frame for the display in Blood Gulch, where the GPU takes 4: the drop also needs the GPU's
time near the budget, which a game thread just under the budget could make it read if the GPU waits for its commands); how long the whole
clears at a change of step take (a clear of a 1080-line target is expected under a millisecond); whether the edge of the part drawn shows
during blurs; and the eDRAM: no target is added (one set serves every step), so its use is as at the same `display.render_lines` today.

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

Optional capture file steps are deferred throughout active gameplay. Wii U
hooks in `game_time.c` publish start/resume before enabling the clock and
pause/end/loading after disabling it; `main_menu_active` publishes menu
transitions on the same game thread. Only a primitive atomic flag crosses to
the writer. The gate starts blocked until authoritative initialization, uses no
tick-gap/draw-count heuristic, and keeps ordinary logging unchanged. Campaign
cutscenes and co-op pause screens that leave the clock running remain blocked.
Owned records wait in bounded RAM until a paused, loading or menu state permits
service; queue overflow remains explicit capture loss. The gameplay bit shares
the exact worker claim word and preserves HOME and permanent-close bits.

An I/O turn admitted while paused can finish if gameplay resumes during that
turn; it cannot be revoked without waiting on the filesystem. No subsequent
optional turn is admitted until an authoritative inactive/menu state returns.
Capture completion therefore requires an eligible drain interval before quit;
it is separate from the measured gameplay interval. This policy does not prove
that optional file work caused any particular frame hitch.

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
