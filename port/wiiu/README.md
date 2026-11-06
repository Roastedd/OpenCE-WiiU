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
| `tests/` | Desktop tests of the shell's portable parts (`tools/wiiu/shell_tests.sh`: ARM64, x86, big-endian PowerPC), and of voice chat over stand-ins for Cafe OS (`test_voice_audio.c`, `voice_mock/`; run by `tools/harness/tests/test_wiiu_voice_audio.py`), and internet play with many machines on one computer (`internet_swarm.sh`, with a test broker, `mqtt_test_broker.py`: "The network"). |
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

No link-time optimisation across the whole program and no profile: devkitPPC's linker does not read LLVM bitcode. `--wiiu-lto` (`LTO=1` in
`build_game.sh`) optimises the game's own sources together as one module before they are linked, which the console's builds since 2026-10-31 use.

### The release build (`RELEASE=1`)

`RELEASE=1 tools/wiiu/build_game.sh` (in the tools repository; the same as `python3 configure.py --linux-target=wiiu --release --wiiu-lto`, then
`ninja linux_wiiu`) builds the flavour that players are given. What it is, and what it is not (the figures are in
`reports/research/2026-11-04/release-polish.md` of the tools repository):

- **`HALO_RELEASE`**, as the desktop builds' `--release`: a failed assertion is noted in `data/debug.txt` and the game goes on, as the retail game's
  (whose assertions were compiled out) did, where a default build stops at it ("halted"). The assertions' expressions are still evaluated (some do
  work the game relies on), so this is not a speed switch. **Exception: the saved games' sources** (`saved_game_files.c`, `game_state.c`,
  `game_state_xbox.c`, `player_profile.c`, `playlist_profile.c`) define `HALO_ASSERTS_STOP` (`cseries.h`): a failed fatal assertion there still stops the
  game, after its note, because it guards what is read from or written to a player's saves. `error()`'s own halts (a datum lookup that fails,
  a map that does not load) are not assertions and stay as they are.
- **The HUD's stack sentinel is left out** (`hud_draw.h`): each of the 25 guarded HUD functions of the Xbox's development build fills a 512-byte local
  with 0x62 and checks it and its own return address when it ends. Nothing can change either, so the checks only cost the HUD's draw calls their
  instructions; in a release build they fold away. The picture is the same.
  `HALO_NO_HUD_SENTINEL=1` in `configure.py`'s environment (`LEAN=1` in `build_game.sh`) leaves it out of a build that is not a release one, so the
  default flavour's assertions keep stopping the game: the one measurable saving without the change of what a failed assertion does.
- `-DDEBUG` **stays**: it is the Xbox development build's own define (every port's compile line has it), not the flavour. The network's join
  token is made `#ifndef DEBUG` from the build's compile time and the tags' checksum (`network_server_manager.c`), so a build without it could join no
  game of any other build; what else it turns on is the checks of zlib's compressor and a dump of the network games' state, which a game does not
  run. The build says `release` to the hosts it joins (`HALO_BUILD_FLAVOR`).
- What stays on as it is in a default build, on purpose: the log and its 5-second lines, the black box, the watchdog, the crash note, the exit trail,
  the profile sampler and the debug server (both need a card file), the shader capture's census (`always` needs a card file). They are
  what a bug report is made from and cost little (sections 3 and 4 of the report). The card files' defaults are as in the default build.


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

The GamePad is the first controller, player 1. The Pro and Classic Controllers, and the Wii Remotes (next section), take the players' places as `input.wiiu_controller_layout` says:
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
| Talk in voice chat (push to talk) | TV, held (the GamePad's) | |

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
menus that skips a cinematic, Classic Controllers, and the Wii Remote with a Nunchuk (next section).

## Wii Remote

Branch `wiiu-wimote`. A Wii Remote is read like the Pro and Classic Controllers: its own KPAD channel, a player in `src/wiiu_pad_players.c`'s map by its
Bluetooth address, the player's number on its lights, the motor for the game's rumble. Written and tested on the desktop; nothing here has run with a real
Remote (see "What only the console can say").

**What is read** (`src/wiiu_input.c`; the pure parts, tested on the desktop, are `src/wiiu_wimote.c`, which wiiu_input.c calls with the values from
`KPADReadEx`):

- The extension, `KPADStatus.extensionType`: a Remote with nothing, with a Nunchuk (`WPAD_EXT_NUNCHUK`), with the MotionPlus (`WPAD_EXT_MPLUS`, a Wii
  Remote Plus or an adapter) or with both (`WPAD_EXT_MPLUS_NUNCHUK`). A Remote with a Classic Controller is still read as the Classic Controller, and a Pro
  Controller as one.
- The buttons (`hold`, and the Nunchuk's Z and C from `nunchuk.hold`: the rest of that field is the stick's emulated d-pad, which is not taken), the
  Nunchuk's stick (`nunchuk.stick`: a dead centre of a tenth, and 0.85 of the way out is the full throw), the pointer (`pos` and `posValid`), the
  accelerometer and, with the MotionPlus on, its turn rates (`mplus.acc`, taken to be degrees a second; `mplus.angles` is only logged).
- The Remote's face buttons reach the game as the Xbox's own A, B, X and Y (`wiiu_pad.xbox_faces`), not through the Wii U's by-place or by-letter match
  (`input.wiiu_face_buttons` does not change them), so A selects in the menus and jumps in the game. Hold-to-skip and the updater's panel see them as the
  letters of the same name.

**Layouts** (`input.wiiu_wiimote_layout`; the Xbox's meaning is what the game gets, the same as the table in Controls):

| Remote and Nunchuk | `"default"` | `"z_fire"` | Xbox pad |
| --- | --- | --- | --- |
| Nunchuk stick | move | move | left stick |
| B (trigger) | fire | throw grenade | right trigger / left trigger |
| Nunchuk Z | throw grenade | fire | left trigger / right trigger |
| A | jump | jump | A |
| 2 | melee | melee | B |
| 1 | action, reload | action, reload | X |
| D-pad up | switch weapons | switch weapons | Y |
| D-pad left | flashlight | flashlight | white |
| D-pad right | switch grenades | switch grenades | black |
| D-pad down | zoom | zoom | right stick click |
| Nunchuk C | crouch | crouch | left stick click |
| + | pause | pause | Start |
| − | Back | Back | Back |

A Remote without a Nunchuk has the menus' layout whatever the setting says: the d-pad, A (Xbox A: selects), B (Xbox B: goes back), 1 (Xbox X), 2 (Xbox Y),
+ (Start) and − (Back). The Nunchuk plugged in or out changes it at once. HOME is the system's.

**Looking** (`input.wiiu_wiimote_look`, one source for each Remote at a time):

- `"auto"` (the default): the MotionPlus if the Remote has it (switched on) and it is the first player's; else the pointer, while it sees the sensor bar;
  else none. `"pointer"`, `"motion"` (the MotionPlus, first player only) and `"off"` choose one.
- **Pointer**: a free-aim box in the middle of the pointer's range (`input.wiiu_wiimote_pointer_box`, the half width as a share of the range, 0.3). Inside
  it the view does not turn. Outside it the view turns at a rate that grows with the distance past the box's edge, gently at first, up to the full push of
  the right stick at the edge of the screen (`input.wiiu_wiimote_pointer_speed`, 1; the box is checked on each axis alone). The rate goes to the game as the
  right stick, lifted past its dead range so that it starts from nothing, so the game's look acceleration, inversion and aim assist apply as to a stick; not
  in the menus. No sensor bar seen (`posValid` 0): no turning. The pointer is switched on with `KPADEnableDPD` for "auto" and "pointer" and off otherwise.
- **MotionPlus**: `KPADSetMplsWorkarea` is given a buffer of `KPADGetMplsWorkSize()` bytes once, and `KPADEnableMpls` is asked for when a Remote connects
  (`WPAD_MPLS_MODE_MPLS_NUNCHUK` with a Nunchuk, `WPAD_MPLS_MODE_MPLS_ONLY` without; again if the extension changes, at most three times). The callback's
  success or failure is in the log ("MotionPlus is on" or "could not be started": a Remote that is not a Plus, with no adapter, says the second, once, and
  is not asked again). The turn then comes the way the GamePad's gyro does (`halo_linux_mouse_look`, as a mouse: not through the stick): the same
  `input.wiiu_gyro_space` ("player", "turn", "lean"), `input.wiiu_gyro_sensitivity`, `input.wiiu_gyro_invert_y` (false: lifting the Remote's nose looks up),
  `input.wiiu_gyro_pause` ("touch" is the GamePad's screen, "stick" the right stick) and the tightening of small rates. It replaces the GamePad's gyro
  while the first player's Remote has it, and aims whatever `input.wiiu_gyro_aim` says (that is the GamePad's). Only the first player's Remote turns the
  view this way (the game's mouse path is the first player's); a Remote with a MotionPlus that is another player's falls back to the pointer. The Remote's
  motor is not rumbled while its motion turns the view. **To recentre, hold the Remote still for a second and a half**: it asks the library to zero the
  MotionPlus (`KPADResetMpls`) once for each time it is held still (the log says so); a drifting view is cured by that. The view is turned by the rates, so
  the Remote's own idea of its angle does not matter. `input.wiiu_wiimote_motion_invert_x` turns the turn round, since its direction is a guess.
- The log, in the format of the other `Input:` lines, says at connect which extension, layout and look a Remote has ("Input: a Wii Remote on channel 1
  (...) is player 1's", then "Input: at connect: channel 1's Wii Remote: extension a Nunchuk, layout default, look none (the setting is auto: the pointer
  once it sees the sensor bar)"), the same again with "now" when the extension or the look changes (the MotionPlus came on, the pointer found the bar), the
  pointer finding and losing the sensor bar, and, while the MotionPlus turns, now and then the rates' peak against how far KPAD's own angle changed in the
  same second (`MotionPlus: rates peaked at ...; the angles changed ...`: if the units are degrees a second, the angle's change is about the peak's rate
  times a second, and when it is not, that line says what the scale should be).

**Players.** Each Remote is a player the way a Pro Controller is (`input.wiiu_controller_layout`); the GamePad's gyro is player 1's only, and so is the
MotionPlus look. The pointer look and the buttons work for every player.

**Not done.** The settings have no GamePad-screen menu page (the config rows only). The virtual-pad test layer (`wiiu_virtual_pads`) was not extended
with a Remote kind: it feeds the Xbox pad under the Remote layer, so a kind of its own would test nothing the layer does. The speaker, the balance board,
the Remote's own sound and a pointer cursor in the menus are not used.

**Tests.** Host: `tools/harness/tests/test_wiiu_wimote.py` compiles `wiiu_wimote.c` against injected values: every button of the three layouts, the
Nunchuk's stick, the box's edges and rate curve with its clamp and the lift, `posValid` 0, the look source for every setting, extension, player and pointer
state, the MotionPlus mode asked for each extension, the turn and tilt from injected rates (level, tilted, pointed up, the Remote's own yaw and roll, tightening,
the sample's time limit, signs), and the zeroing after being held still; each of 20 faults must fail it. The existing pad-player and virtual-pad tests still
pass, and the game's default host digest is unchanged. The Wii U build compiles, links and passes `readrpl`.

**What only the console can say.**

- Whether `KPADReadEx` reports the pointer steadily (`posValid`), and its range and sign (the code takes positive as right and down, as the header says; the
  box and speed are shares of the range), at the owner's distance, with the Wii U's sensor bar and with the GamePad as one.
- The units and signs of the MotionPlus rates (`mplus.acc`), and whether `KPADResetMpls` is the zeroing the library has for a Remote held still. The log's
  peak-and-angle line says the units; the direction of the turn is `input.wiiu_wiimote_motion_invert_x`, the pitch `input.wiiu_gyro_invert_y`.
- Whether a Remote Plus starts with `WPAD_MPLS_MODE_MPLS_NUNCHUK` with a Nunchuk plugged in, what `extensionType` it then reports, and what the extension
  does while it starts (the code holds a Remote's state for a second if it is unknown).
- The Nunchuk's real reach and centre (the stick's dead centre of a tenth and its reach of 0.85 are guesses); the channel and the address when a Remote sleeps.

**A test plan for the owner** (nothing needs the game's levels beyond the first; end with `gx2_thread 0` as for any session):

1. Sync a Wii Remote with a Nunchuk (SYNC on the console, then the Remote's red button). The log should say the Remote is on a channel and is player 1's;
   its first light should be on. In the menus the Nunchuk's stick moves, A selects and 2 goes back (with a Nunchuk the Remote's B is the game's fire, not a
   menu button). Take the Nunchuk out: the menus' layout (d-pad, B goes back) returns.
2. In a level with no sensor bar and a plain Remote (not Plus): the buttons of the table; the view does not turn; the log says the MotionPlus could not be
   started, once.
3. With the Wii U's sensor bar placed as for any Wii game, or the GamePad as one (the *DRC Sensorbar* plugin, the GamePad facing the player at one to three
   feet): the log says "sees the sensor bar" when the Remote points at it. Point inside the middle: the view holds still. Point out towards each edge: it
   turns, faster further out; away from the bar, it stops. Change `input.wiiu_wiimote_pointer_box` and `_pointer_speed` and see the middle grow and the
   turning quicken.
4. A Wii Remote Plus (or a plain Remote with a MotionPlus adapter), with and without a sensor bar: the log should say the MotionPlus is on, the look should
   be "the MotionPlus", and the view should follow the Remote as a mouse. If left and right are the wrong way, set `input.wiiu_wiimote_motion_invert_x`
   to true; if up and down, `input.wiiu_gyro_invert_y`. Send the log's "rates peaked at ... the angles changed ..." lines. Hold the Remote still on a table
   for two seconds: "was held still" in the log, and no creeping of the view.
5. The Remote alone (no Nunchuk, Plus or not): the menus work with it; it is player 1's, then player 2's once ADD PLAYER makes the controllers players of
   their own (with the lights showing 2).
6. Rumble: the Remote's motor with a pointer look; none while its MotionPlus turns the view.

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
- **Pause menus**: the 2p menu in a full-width window, the 4p one in a quarter (`ui_widget.c`). Each view centres its own menus: `render.c` asks
  `halo_screen_ui_offset_view` (`src/d3d8_gx2.c`) for the offset of the view's width, 640 columns centred in a half or the whole picture and a quarter's
  320 centred in the quarter (upstream's ba759d5b, merged after this section was first written; it said then that every window used the whole picture's
  106 columns, which is no longer so). What still assumes the whole picture's offset is the GamePad's touch mapping (`menu_point_from_gamepad`,
  `src/d3d8_gx2.c`): a tap on player 1's pause menu in a quarter lands up to 53 columns to the left of where it is drawn (not run on a console).
- **Frame rate and detail**: `display.split_screen_detail` and `display.split_screen_fps` (`port/linux/game/graphics_quality.c`, `port/linux/README.md`).
  With three or four views the defaults leave out the object shadows, the lens flares and the level's specular shine and reflections, and hold the
  frames to 30 a second through `halo_interpolation_enabled` (`src/platform_wiiu.c`) while `display.interpolation` stays as the player set it. The
  overlay (L + R + −) shows `30HZ` while they are held. In another machine's game the host's rules put the lens flares and the level's specular
  parts back every tick (`cheats.c`'s `cheats_network_client_enforce`, as they do the performance mode's), so a client keeps only the shadows off;
  the split screen lobby is a game this machine hosts, which has the whole set.

### Virtual controllers: split screen with one person, or none

`debug.virtual_gamepads` (0 to 4) and `debug.virtual_gamepad_script` are the desktop build's test controllers, and the Wii U build plays them too
(`src/wiiu_virtual_pads.[ch]`, put on the Xbox pads by `src/xinput_wiiu.c`; they were settings the Wii U's `config.toml` listed that did nothing until
this was written). Pad n drives the game's port n - 1: pad 1 is the GamePad's port (merged with the GamePad and the controllers that share it), pads 2 to 4
are ports 1 to 3 and are there as long as the count says, whatever is connected. The script is `"<seconds> <pad> <what>; ..."`, the seconds counted from
when the network lobby first opens (the log says `Input: N virtual controllers (debug.virtual_gamepads): the script's clock starts`), `what` one of `a b x y
start back up down left right lb rb lt rt` (the Xbox's buttons, whatever the face buttons' match is; pressed 150 ms and until the game has read them 8 times) or
`walk` / `still` (the sticks move from then on, each pad its own way). On the Wii U a button may be followed by a number of seconds to hold it for (`"60 2 rt 30"`:
pad 2 fires for 30 s; the desktop's script does not have it), and a script has up to 256 entries (the desktop's 64). Pad 0 is the desktop's keyboard: the Wii U has none,
and its entries do nothing.

A game of 2, 3 or 4 from `config.toml` alone, with no hands (a LAN game this console hosts: `debug.network_test` hosts it as Create Game does, adds player 1,
and starts it `debug.network_test_start` seconds after the lobby opens):

```toml
[debug]
network_test = "host:bloodgulch"        # or another multiplayer map's name
network_test_start = 25.0
virtual_gamepads = 4
virtual_gamepad_script = "3 2 start; 4 2 right; 4.5 2 right; 5 2 a; 6 3 start; 7 3 right; 7.5 3 right; 8 3 right; 8.5 3 a; 9.5 4 start; 10.5 4 right; 11 4 right; 11.5 4 right; 12 4 right; 12.5 4 a; 14 2 a; 30 1 walk; 30 2 walk; 30 3 walk; 30 4 walk"
```

What the script does, as the game's lobby has it (`menu_functions.c`'s `join4_*`): pad 2's START joins and opens the 4-way screen; its spinner starts on a
profile no one has, and the first two profiles of the list are the game's defaults, whose names a game cannot use ("the profile's name can't be used in
multiplayer"), so the script moves it right twice, to the first profile the card has made; A selects it (the screen says "Another player has that
profile." for one that another player has: **each player needs a profile of their own, so 3 or 4 players need that many made on the card, and the owner's card
has two**); pads 3 and 4 do the same from their own START (the first free profile is the default ones, then the card's, so they move right once more for
each pad before them); a selected player's A ends the screen. A card with other profiles, or in another order, needs other `right` presses: the first run
shows the spinner's choice (the debug server's screenshot) and the entries are adjusted. `network_test = "local:..."` is no use here: it starts the game
at once with one player.

The same script plays on the host with no console: `tools/test_wiiu_game_digest.py run NAME --mode split4` (the Wii U's controller layer and Direct3D layer
under qemu-ppc, `--mode split2` and `split3` too; build once with `build --wiiu-input`). Measured so (2026-11-02, Blood Gulch at its spawns, 400 ticks, the
players standing, one walking): 1 player 239 draws a frame (median), 2 players 349, 3 players 433, 4 players 619; with `display.split_screen_detail =
"same"`: 496 for 3 and 694 for 4, so the reduced set takes 12% and 11% of the draws. No error or assertion in the log of any of them.

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

### Sockets, and internet play's peers in the process

The app has 27 sockets: nsysnet numbers them 3 to 31, the 28th `socket()` fails with EMFILE, and `poll` takes all of them (wut's poll is nsysnet's select,
whose sets end at 31; measured with the socket probe, 2026-10-31). A host's own sockets, as a Linux host counts them (`posix_socket_census`, the `net:`
lines below): 8 with one signalling broker, that is the game's five (its listening socket and datagram socket, its own client's connection, both ends,
and datagram socket), the tunnel, the hand-off socket and the broker. The card's `brokers.txt` lists four brokers (three more), and the debug server
takes two while it is on. A LAN machine takes one more (its connection).

Internet play's stand-ins took a host three sockets for each peer (a UDP stand-in for the peer's port, and the two ends of a local connection carrying
its stream), four while one joins (its search's stand-in): measured on a Linux host, 20 sockets with 4 peers and 56 with 16. On the Wii U that is about
four peers. `network.tunnel_in_process`, on by default here, hands the game what peers send without them (`port/linux/src/p2p.c`'s header): a peer's
datagram goes into an inbox of the game's socket that has its port, which `xnet.c` reads before the socket, and a peer's connection waits on the game's
listening socket for its `accept`, which gives it a number that no socket has (`p2p_stream_socket`), read and written in the process on its KCP stream.
Measured on a big-endian Linux host (`tests/internet_swarm.sh`, 16 official upstream x86 builds of network version 41 joining by invite): 8 sockets with 16
peers, all 16 connections in the process, about 480 datagrams a second handed over and none lost. What the game reaches out to still uses stand-ins: a
Wii U joining an internet game has a listener and its accepted connection for its host, and a UDP stand-in for its connected datagram socket (three
sockets). Nothing the tunnel sends changes, so official builds play with it.

A stream socket's buffers are set to 32 KB (`transport_endpoint_winsock.c`; nsysnet refuses 64 KB): Linux grows them as a connection needs, nsysnet keeps
its own sizes, which no console has reported yet. They carry the game's real connections: LAN machines', the host's own client's, and a joiner's
connection through its stand-in. A host sends a machine that joins a game in progress what fits there each frame (the rest waits in the connection's
256 KB queue), and a joiner reads at most that between frames. The log's first `Net: stream socket N: SO_SNDBUF was X, 32768 asked, now Y` lines say what
nsysnet gave. Connections taken in the process have no socket and are not limited by it: the tunnel's window holds 128 KB of each one's.

`debug.network_statistics`, on by default here, logs `net:` lines each second of a network game (`port/linux/NETCODE.md`, Testing): the machines and
players, datagrams and bytes each way, refused sends, the tunnel's packets, what was handed over in the process and lost, the sockets open and the
highest nsysnet number, the netcode's time a tick, each client machine's input gaps, the corrections, and the longest round trip.

### The console test plan for internet hosting

For the session that holds the console (one driver at a time; check `ps` for another session's scripts first). Build `wiiu-net` (`OPENCE=<its worktree>
tools/wiiu/build_game.sh`), keep the card's `config.toml` and put it back at the end, read back, and leave the card on the safe path. Put
`port/assets/network/brokers.txt` on the card as `halo/brokers.txt`. On the Mac, the joiners are copies of the official upstream x86 build (protocol 41,
`scratch/opence-upstream-x86`) in the default Colima machine, behind the Mac's NAT as players behind a router are: `port/wiiu/tests/internet_swarm.sh join
<output> <invite> <joiners> upstream-x86 <seconds>` from the tools repository's folder, with the public brokers and STUN.

1. Hosting with nobody: `debug.network_test = "host:bloodgulch"`, `debug.test_input = "bot:7"`. The log says `Internet play: the peers' datagrams and
   connections are handed to the game in the process`, then `network test: invite halo://join/...` (whole). Note the `net:` lines' `sockets N open,
   highest H` before anyone joins: the Wii U's own count.
2. Peers in steps: 4, 8, then 15 joiners from the Mac by that invite (a run each, 300 s), keeping the Mac's copies to what its cores carry (each takes
   about a third of a core at 30 frames a second; past that they lag, and their round trips with them). For each, from the Wii U's `net:` lines: the
   sockets and the highest number stay as in step 1, whatever the peers; `in the process` about 30 datagrams a second for each peer and `lost` 0;
   `refused` 0 on both lines (a refused send is a datagram lost: nsysnet's send buffer full); the netcode's milliseconds a tick, and the frame times
   (`frame time` lines), against step 1; the input gaps of each machine and the longest round trip. On the Mac: every joiner's log has the host's
   player, and `tools/net_compare.py --compare <the Wii U's log> <a joiner's log>` (OpenCE) for two or three of them.
3. The same with `network.tunnel_in_process = false` and 4 joiners, then 6: the sockets should rise by three for each peer, to the ceiling at about
   four. Note what fails at the 28th (EMFILE in the `Net:` trace) and whether the game recovers when the peers leave. Optional: it runs the console
   out of sockets.
4. Another network, if the owner can arrange one (a phone's hotspot, a friend's PC): an official OpenCE PC build joins the Wii U's game by its invite
   over the internet, with Mac joiners as in step 2; the same readings, and that its player plays.
5. From the menus: Multiplayer, Create Game, Internet; the invite on the screen; a Mac joiner by it.
6. Joining in progress (the 32 KB buffers): a LAN machine (the Colima `lan` profile's x86 build) joins the Wii U's game a minute into a match; the
   log's `Net: stream socket` lines, how long the joiner takes from the join to its first `network test: tick` line, and `streams out` on the host's
   `net:` line meanwhile. Then the Wii U joins a Mac host's game in progress.

It passes when 15 peers play a Wii U-hosted game for five minutes with the sockets as with none, no datagram lost in the process, and the joiners'
views agreeing with the host's as `net_compare.py` judges two machines.

## Owned game-audio packet storage

The host-qualified audio branch reuses compressed packet copies and decoded
PCM through `src/wiiu_packet_pool.c`. Idle payload is bounded to 1 MiB plus
128 private headers; live buffers retain their independent ownership. A
failed heap request reclaims only idle cache storage and retries once.
Submission copies, packet serial guards and completion/flush order remain
unchanged. The cache uses a short blocking mutex, with no heap operation
under that lock. It is not a wait-free mixer.

The existing `audio_profile.txt` flag also enables allocation/cache/retry
counters. Host fitting-cohort allocation costs improve, but pressure cases
can regress and no whole-mixer or console speedup is established. See the
[measurements and console acceptance plan](../../docs/performance/mac-audio-churn.md).

## Voice chat

`src/wiiu_voice_audio.c` is voice chat's sound for the Wii U (`voice_audio.h`; the other builds' is `port/linux/src/voice_audio.c`), and
`port/linux/game/network_voice.c` decides who hears whom, as on every build. Written, built and tested on the build machine, natively,
on 32-bit x86 and big-endian (`tools/harness/tests/test_wiiu_voice.py`: the queues, rings, resampler and microphone readings;
`test_wiiu_voice_audio.py`: `wiiu_voice_audio.c` itself with the real Opus over stand-ins for Cafe OS and mic.rpl,
`tests/test_voice_audio.c`); not run on a console.

- **Talking**: into the GamePad's microphone, or a headset plugged into the GamePad's jack, which takes the built-in microphone's place on
  the same stream (`MIC_INSTANCE_0`; the log says which, from `VPADStatus.usingHeadphones`). The Pro Controller has no jack, and a USB headset
  has no way in (`nsysuhs` has no isochronous transfers). VOICE CHAT in Settings > Audio Setup: PUSH TO TALK, the default, is the GamePad's **TV**
  button held (every other button is the game's; while a voice session asks for it the system's TV menu is off, `VPADSetTVMenuInvalid`, and it
  is back a second after); OPEN MIC sends whenever the microphone hears speech, and is best with a headset: the GamePad's own microphone hears
  its speaker. VOICE VOLUME sets the others' voices.
- **Threads**: the game thread only copies (a frame taken, a frame given to be encoded and the packet of the one before taken back, a packet
  heard queued). A voice thread on core 0 at priority 20, below the mixer's 10, with 128 KB of stack, reads the microphone every 10 ms,
  resamples its 32 kHz to Opus's 48 kHz, keeps at most five frames (100 ms) waiting, encodes and decodes (the desktop's Opus settings, its
  sixteen speakers, its concealment of up to three lost frames and its 60 ms of buffering). The mixer adds a ring for each speaker and never waits
  for the voice thread. `halo/voice_core.txt` `2` puts the voice thread on core 2 at the GX2 thread's priority, as DirectSound's packet decoder
  is, should the mixer miss chunks with it on core 0.
- **Latency**: a frame's packet comes back at the next call, 20 ms or a game frame later; a packet more than 150 ms old by then is not
  sent (the tail of a talk that ended, or of a stall). The packets of the last frames given before the talking stopped (push to talk let go)
  would be waiting for a call that never comes, so `network_voice.c` also asks for them, once an update (`voice_audio_packet_ready`, under
  `HALO_WIIU`): without it every talk lost its last one to three packets, 20 to 60 ms. Each call takes back one packet, so at a slow frame rate one can wait a game frame
  more: 150 ms keeps every packet down to 15 game frames a second (60 ms lost a fifth of them at 30).
- **HOME**: the voice thread closes the microphone and drops what waits before the sound is given up (the release callback waits 100 ms at most;
  a thread that does not answer has the microphone closed for it by the callback, if it is between microphone calls), and opens it again when
  the foreground is back, every 10 s while that fails. With no microphone open the callback does not wait at all ("HOME with no microphone
  open"): the console's logs of 2026-10-31 had "did not answer in 100 ms" at every one of 41 HOMEs, each with the microphone closed, so the
  100 ms was spent for nothing. The thread says "noticed HOME N ms after it was asked" when it does notice late, "its loop did not come round for N ms" when it
  runs again after a gap of a quarter of a second, and the lines of HOME say how long ago its loop last came round: those settle whether the
  thread is starved or stopped by the system in the background. The close ends the thread within 300 ms, and `exit-trail.txt` says "the voice thread
  ended".
- **What the host is told**: a machine says it hears voices only while it can play them (`voice_audio_hears`: the voice thread running, in the
  foreground, with the sound mixing), so a host sends none to a Wii U that cannot play them; a microphone that does not open does not stop it
  hearing.
- **The microphone's ring**: the system fills a ring in the app's memory, and `MICGetStatus` says how far; the published sources disagree on what
  its numbers mean. Consume: `availableData` samples wait from the read index `bufferPos`, and `MICSetDataConsumed` moves it on (Cemu, and
  capitalistspz's wiiu_audio_examples). Position: `bufferPos` is where the system writes, in bytes, followed by the app, which never consumes
  (WiiUCamera). Both are written (`src/wiiu_voice_core.c`, tested over simulated rings, in either unit), and `audio.wiiu_microphone_read` chooses:
  `"auto"` (the default) is what the probe found, else consume.
- **Opus's cost**: the encoder's complexity is the desktop's 5 unless `halo/voice_complexity.txt` holds 0 to 10 ("Voice chat: Opus complexity N").
  The one console window measured (2026-10-31, `Voice:` line) had 29 frames encoded at a mean 4.33 ms and at most 10.71 ms, wall time on core 0
  including whatever preempted the voice thread, against the 1.4 to 2.3 ms estimated from instruction counts.
- **The log**: "Voice chat:" lines when the microphone opens and closes and why, and every 10 s of a session "Voice:" lines: first what the
  microphone brought ("the microphone brought N samples in 10 s (32000 a second), level L, peak P of full scale": a rate far from 32000 says the
  unit or the rate is wrong; a peak of exactly 0 says the ring is not being seen; the level rises as one speaks), then frames encoded
  and packets decoded with their mean and longest times, frames made up, speakers, the slowest turn, and what was dropped. The mixer's
  profile (`halo/audio_profile.txt` 1) times the voices' mix as its "chat" stage.
- `debug.voice_test` works as on the other builds: a 440 Hz tone in place of the microphone, and a "voice test: speaker N level L gain G pan P
  bytes B" line each second for each voice heard.

### The microphone probe

Put `1` in `halo/mic_probe.txt` and start the game. Ten seconds after the start (for the menus to be up and the GamePad read), the voice thread
opens the microphone, logs "mic probe:" lines for two seconds, then records ten. Speak into the GamePad, or the headset, while it records; press
TV a few times; and, to test the background, press HOME during the recording and come back. Then close the game and take `log.txt`,
`mic_probe_result.txt` and `mic_probe.wav` from `wiiu/apps/halo/`. Remove `mic_probe.txt` afterwards: the probe runs at every start while it is
there. (It writes the recording to the card from the voice thread, after recording it into memory.)

| Lines | What they say |
| --- | --- |
| `MICInit(instance 0, ...): handle H, error E`, `MICOpen: error E` | whether the microphone opens from the voice thread's core (error 0), and how long that takes |
| `state N (name): error E, value V` | mic.rpl's states 0 to 8 (Cemu's names, partly guessed: 0 the rate, 1 the gain, 2 to 4 its range, 5 mute, 7 echo cancellation, 8 auto selection) |
| `unconsumed N: ... availableData A, bufferPos P; samples at bufferPos ...` | a second with nothing consumed: `P` moving by itself from the start (in the first 250 ms, before the ring could fill) is a write position, counting bytes if it goes past 10240; `A` growing while `P` stands is a read index. The samples show where the sound is |
| `consumed N: before ...; MICSetDataConsumed(n) e; after ...`, then `consuming, bufferPos moved on by what was consumed after X of Y readings, and went up to Z` | a second consuming each reading: a read index moves on by what was consumed and `availableData` falls; one that goes past the ring's 10240 samples (`Z`) counts bytes |
| `the reading is "consume samples"` (or `consume bytes`, `position bytes`, `position samples`, `unknown`) | the finding, which is also the first line of `mic_probe_result.txt`; `audio.wiiu_microphone_read` `"auto"` reads by it from then on (`unknown`: nothing moved, no sound came) |
| `N samples in M ms, level L of full scale, peak P` | each second of the recording: about 32000 samples a second if the rate is 32 kHz and the reading right, and a level that rises as you speak |
| `the TV button is down` / `up` | the TV button reaches the game with the system's TV menu off |
| `HOME during the probe: the last status ...`, `closed (HOME): MICClose E, MICUninit E`, `back in the foreground after N ms`, `the first status after HOME` | what HOME does to an open microphone, and whether it opens again; the shell's "Voice chat: given up for the background after N ms" (or "did not answer") says whether the voice thread ran during the release callback |
| `N samples (S s at 32 kHz) written to .../mic_probe.wav` | play it on a computer: the voice at its own pitch and speed. Too fast or too slow: the rate is not 32 kHz; noise: the byte order or the reading is wrong |

### Testing voice chat on a console

For the session that has the console. Each step names what to set on the card, what to do, and the lines that answer. Put the card's
`config.toml` back afterwards and read it back (as `runs/lan-20261030/lan_test.sh` in the development repository does), and leave the card as
it was.

1. **The probe**, above. It settles the reading, the rate, the byte order, the TV button and HOME with the microphone open. If it finds anything
   but `consume samples`, the runs after it read by its finding.
2. **A tone against an x86 build on the LAN.** On the Wii U: `debug.voice_test = true`, `audio.voice_chat = "open_mic"` (the tone is always above
   the open mic's level) and `debug.network_test = "host:bloodgulch"`. The x86 build of the same commit joins from the bridged Colima machine, as
   `lan_test.sh` has it, with `HALO_VOICE_TEST=1 HALO_VOICE_CHAT=open_mic HALO_NETWORK_TEST=join` (the tools repository's `tools/ppc-be/run.sh` passes the first two on to its container; until 2026-11-02 it did not, and the Mac side would have run without the tone). The same two machines' voice was run on the build machine in both byte orders (big-endian host with an x86 client, and the other way round) with `runs/mic-20261102/voice_cross.sh`: each side logged the other's tone at level 0.195 to 0.214. Each side's log has a line each second, `voice
   test: speaker N level 0.212 ...`, for the other's tone (0.212 is the tone's 0.3 over the square root of 2; `gain 1.00 pan 0.00` in the
   lobby, others when the game's proximity places the speaker), and the Wii U's "Voice:" lines count about 500 frames encoded and 500 packets
   decoded in 10 s. Then the other way round: the x86 build hosts and the Wii U joins (`debug.network_test = "join"`).
3. **Eight x86 talkers.** The Wii U hosts as in 2, and eight x86 copies with `HALO_VOICE_TEST=1 HALO_VOICE_CHAT=open_mic` join, each with an
   address of its own on the LAN (copies on one address cannot all bind the game's ports, and one bound to a single address hears no broadcasts:
   a macvlan Docker network on the bridged machine gives each container a LAN address of its own; not tried yet). The host relays at most 8
   talkers. Read every 10 s: the voice thread's "Voice:" lines (encoding and decoding times, the slowest turn, what was dropped), the mixer's
   "Audio ring: N apparent overtakes, M at deadline" (its missed chunks; with `halo/audio_profile.txt` 1, "Audio mix profile" and its chat stage),
   and the heartbeat's frame rate, against a run without the talkers. If the mixer misses chunks, run it again with `halo/voice_core.txt` 2.
4. **Real voice.** `debug.voice_test` off on the Wii U, a headset in the GamePad's jack, VOICE CHAT on PUSH TO TALK, and an x86 copy listening
   with `HALO_VOICE_TEST=1` (its "voice test:" level for the Wii U rises when the player speaks). Look for "Input: the GamePad's jack: a headset",
   "Input: the GamePad's TV button is push to talk", the microphone opening at the first press of TV, speech heard while TV is held and not
   after; OPEN MIC with the headset and without it (without, the log warns, and the other side hears the game through the GamePad's speaker);
   HOME in a session ("Voice chat: given up for the background", the microphone closed, open again on return); and the GamePad taken out of
   range (a second of MICGetStatus failing closes the microphone; it is tried again every 10 s, "tried again" when it opens).

What only a console can settle: whether mic.rpl opens from core 0 under Aroma; which reading is right, the rate, the byte order, and whether
flushing the ring's cache lines before each reading (`DCFlushRange`, right whether the system writes it by DMA or through a core's
cache) is enough to see the samples; what states 0 to 8 hold; that a headset in the jack
replaces the built-in microphone on the same stream; whether the TV button reaches the game with the TV menu off, and whether the system gives the
menu back at the close; whether the voice thread runs during HOME's release callback, and whether the microphone opens again after HOME and after
the GamePad goes out of range; what Opus costs on Espresso (estimated at 1.4 to 2.3 ms to encode a frame and a fifth of that to decode one, from
instruction counts under qemu-ppc) and whether eight talkers cost the mixer chunks or the game frames; and how much of the GamePad's speaker its
microphone hears.

## The debug server

`src/wiiu_debug.c` is a small server inside the game's own shell (TCP port 4310, started once the game's thread runs) so that a change can be
run, looked at and judged without a person at the console; its client is `tools/wiiu/wiiu_debug.py`. The lines are read by
`wiiu_debug_protocol.c` (plain C, tested on three machines: `tests/test_debug.c`). It answers anyone on the network, so it is for development
only: it starts when `halo/debug_server.txt` on the card says `1`, and is off without that file (the log says which).

- `hello` (frames, foreground, memory), `log N` (the last lines of the shell's log), `press BUTTONS MS` (`TV` is voice chat's push to talk) and `stick LX LY RX RY MS` (merged into
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
- **The controller** (`src/wiiu_drs.c`, run in `present()` on the GX2 thread) drops a step when the median of the last 8 frames' GPU time
  is over 0.9 of the budget (16.7 ms, 33.3 at 30 frames a second); the GPU's time plus 2.5 ms of blits is at least 0.9 of the frames' interval
  over the last 64 frames; the median of the 8 backlogs is over 0.06 of the budget; and the frame waited more than 1 ms for the flip. The
  interval is what tells a GPU that is the limit (0.86 to 1.02 in c10, b40 and the a30 shore) from a game thread that is (0.59 to 0.85 in the a30
  fight and b30, where the GPU's time still reads 16 to 18 ms). The backlog is the part of a frame's GPU time still to run when the game thread
  had handed over its last draw; the display holds nearly every early frame (11 to 16 ms a frame at 60 in the console's logs), so a wait for the
  flip is no sign of a GPU that is behind. It raises a step when the median
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
flip, K had a menu; the GPU's time at this step ... ms on average and in the median, its backlog ... ms in the median, over n frames, and ... of the frames' interval over 64 (the share the controller drops at); C targets cleared at a change of step`. The frame times'
percentiles are the line before it, `GX2:   frame times: median ..., 95th percentile ..., 99th ... ms`. The overlay shows the size drawn.

### The console's tests

For the session that has the console. Copy the build's `halo.wuhb`/`halo.rpx`; keep the card's other notes as that session's practice has
them. Each run: read `log.txt` afterwards.

0. **The backlog, controller off.** Before any of the below, with `display.dynamic_resolution = false` and no note: the 5 s log has the line
   `GX2:   of it still to run when the game thread had handed over the frame's last draw (the GPU's backlog ...): X ms on average, Y the longest,
   N of M frames over 2 ms` after the GPU's time. Expected from the old logs (estimated, not measured): about 0 in Blood Gulch, a10, a50 and in
   fights the game thread limits (a30 firefight, b30); 3 to 8 ms in c10, b40 and the a30 shore at 45 to 50 frames a second. The controller's
   threshold (`BACKLOG_SHARE` in `src/wiiu_drs.c`, 0.06 of the budget, 1 ms at 60) is to be set from these figures.
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

## Updates

A release build updates itself from GitHub (`src/wiiu_update.c`, with `wiiu_update_core.c` and `wiiu_update_http.c`, which run on a desktop
for their tests). It is off unless the build is given three things at configure time: a build number (`HALO_BUILD_NUMBER`, greater than
0), the repository its releases are published in (`HALO_UPDATE_REPOSITORY`, `owner/repo`) and the Ed25519 public key their manifests are
signed with (`HALO_UPDATE_PUBLIC_KEY`, 64 hexadecimal digits). Without them the log says `Update: off` once and nothing is fetched,
written or renamed: a build made the usual way (`build_game.sh`) has none of them. What players see is in [PLAYING.md](PLAYING.md),
"Updates".

**A release** is the files the app owns beside the player's data (`halo.wuhb`, `shaders.pack` made for that build's shaders,
`brokers.txt`), with a manifest and its signature:

    halo-wiiu-manifest 1
    build 124
    tag wiiu-124
    maps c1ea0000
    file shaders.pack 2401600 <SHA-256>
    file brokers.txt 611 <SHA-256>
    file halo.wuhb 15038800 <SHA-256>

`halo-wiiu-manifest.txt.sig` is the 64 bytes of `openssl pkeyutl -sign -rawin` over it, checked with Monocypher's `crypto_ed25519_check`.
`maps` is the format of the maps the build reads (`tools/wiiu/convert_maps.py`'s rebase): a release whose maps differ is not offered. The
parser refuses anything else: lines out of order, numbers with a sign or a leading zero, a name that is not `halo.wuhb`, `brokers.txt` or a
`.pack` (so never `config.toml`, `data/`, `maps/` or `menus/`), a name twice, more than 8 files or 512 MB, no `halo.wuhb`.

**At the start**, after the shell has read the last run's crash note and close trail and before the game starts
(`wiiu_update_startup`), the journal is followed. Then a worker thread on core 0 fetches
`https://github.com/<repo>/releases/latest/download/halo-wiiu-manifest.txt` and its `.sig` (the documented link to the latest release,
which is neither a draft nor a pre-release, and uses no API quota: it answers 302 to a signed link to `release-assets.githubusercontent.com`,
which expires), checks the signature, and offers a newer build, not the one rejected, at the main menu. A is "update", B "not now", X
twice "never" (`update.auto = false`). `debug.update_answer` (`yes`, `no`, `never`) answers for automated runs.

**HTTPS** is the port's Mbed TLS 3.6.7 over nsysnet's sockets, with its own roots (`src/wiiu_update_roots.h`, from the CA bundle by
`tools/wiiu/update_roots.py`: ISRG X1 and X2, USERTrust RSA and ECC, Sectigo R46 and E46, and DigiCert Global Root G2 as a spare); the
console's NSSL is not used, as its roots do not reach GitHub's file host (Let's Encrypt, under ISRG X1) or github.com (Sectigo E46, by
USERTrust ECC). The console's entropy is weak (`src/wiiu_mbedtls.c`), so TLS only carries the files: the signature and the SHA-256s vouch
for them. Every wait is at most 100 ms, between which the worker's flags are read.

**The files** go to `update/<name>.part`, hashed as they are written, in 64 KB pieces, only in the foreground, with the network allowed
(`wiiu_network_allowed`) and the main menu up. HOME (or the menus going) drops the connection and keeps the part; the download goes on
with a `Range` request after a fresh redirect. A whole file is flushed (`fsync`) and renamed to `.new`. A file the folder already has,
byte for byte, is not downloaded. The card must have the download's size and 32 MB more free (`FSAGetFreeSpaceSize`: newlib's `statvfs`
refuses the SD card).

**The swap**: every `.new` is read back and checked, HOME is turned off (`OSEnableHomeButtonMenu`), the journal says `swapping`, the
running bundle is unmounted (RPXLoadingModule keeps it mounted as romfs until the app ends; its `RL_UnmountCurrentRunningBundle` is
reached through `OSDynLoad_Acquire("homebrew_rpx_loader")` and `OSDynLoad_FindExport`, as librpxloader reaches it, so neither
librpxloader nor anything else is added to the link), each current file is renamed to `update/<name>.old` and its `.new` into its place,
`halo.wuhb` last, the volume is flushed, the journal says `trial`, HOME is turned on, and the new build is started
(`RL_LaunchHomebrew("wiiu/apps/halo/halo.wuhb")`, the Wii U Menu if that fails). The bundle keeps its path, so Homebrew on Menu keeps its
tile. A console without RPXLoadingModule, or an app started from another path (`RL_GetPathOfRunningExecutable`), is never offered an
update. A rename is never asked to replace a file: FAT's rename may not.

**The journal** (`update/state.txt` and `update/state2.txt`, written in turn, each with a sequence number and a CRC-32; the newer whole one
counts) makes every step one the next start can resolve. Because `halo.wuhb` moves last, the build that is running says how far a swap
got: the new build finishes it, the old build undoes it, file by file, from which of each file's places hold a file. A new build is on
trial until it reaches its main menu (then `good`, and the `.old` files are deleted at the next start). A trial start that follows a crash
note, or the third start of a build that has twice not reached its menu, puts the old build's files back the same way (`restoring`: the
new ones to `.bad`), records the new build as rejected (never offered again; a later one is), starts the old build, and tells the player
at its main menu, where Y has the rejected build offered again. Rollback is automatic because a build that cannot reach its menu cannot
ask. `tools/test_wiiu_update.py` cuts the power at every step of a swap and of a rollback, then at every step of the start after it,
and checks that the folder always ends as one build or the other; the one state the console cannot leave by itself is a cut between
`halo.wuhb`'s two renames, which leaves no `halo.wuhb` (PLAYING.md, "If an update went wrong", is the recovery by hand).

### Turning it on

Nothing is published, signed or turned on by default; these are the owner's steps.

1. **The signing key**, once, on a machine that does not build or publish (offline), with two backups: `openssl genpkey -algorithm
   ed25519 -out halo-wiiu-signing.pem`. Every build trusts this key alone: losing it means installed builds can no longer update (a new
   key needs a build installed by hand), and anyone with it can publish to every console.
2. **Its public key**, for the builds: `python3 tools/wiiu/make_wiiu_release.py --print-public-key halo-wiiu-signing.pem`.
3. **A public repository for releases only** (`owner/halo-wiiu-releases`): no code, no workflows that publish, write access for the
   owner alone. Turn on immutable releases for it (a published release's assets then cannot be replaced or its tag moved). Test builds
   go in another repository with another key; a pre-release or a draft is never "latest", so it is never offered.
4. **A build per release**, its number one higher each time (`build_game.sh` does not pass these to the container yet):

       docker run --rm -v "$OPENCE:/src" -w /src -e HALO_BUILD_NUMBER=124 -e HALO_UPDATE_REPOSITORY=owner/halo-wiiu-releases \
           -e HALO_UPDATE_PUBLIC_KEY=<64 hex digits> opence-wiiu \
           bash -ec 'python3 configure.py --linux-target=wiiu --release > /dev/null && ninja linux_wiiu'

5. **The release's folder**: `build/linux-wiiu/halo.wuhb`, the `shaders.pack` made for that build, `port/assets/network/brokers.txt`.
6. **The manifest**, signed where the key is: `python3 tools/wiiu/make_wiiu_release.py release-124 --build 124 --key
   halo-wiiu-signing.pem --public-key <hex> --repository owner/halo-wiiu-releases`. It reads the build's own record out of `halo.wuhb`
   (its RPX's compressed sections: `build=124 maps=c1ea0000 repository=... key=...`) and refuses a bundle of another build, maps,
   repository or key; then it signs, and checks the signature (openssl), the manifest (the console's rules) and every hash again.
7. **The release**, tagged `wiiu-124`, not a pre-release, with the five files.

### What only the console can settle

- How fast Mbed TLS is on Espresso: the handshake (an ECDHE and a certificate chain each time, one connection a request), AES-GCM or
  ChaCha20 and SHA-256 at the card's speed. The log times each fetch and each file.
- Whether Aroma honours `OSEnableHomeButtonMenu(FALSE)` during the swap (if not, a swap cut off by HOME's Close is resolved at the next
  start).
- `RL_UnmountCurrentRunningBundle` while the game runs (it removes the bundle's /vol/content and /vol/save layers, which Halo does not
  use), the renames of `halo.wuhb` after it, and `RL_LaunchHomebrew` straight after the swap.
- What `RL_GetPathOfRunningExecutable` says for a bundle started from Homebrew on Menu (`wiiu/apps/halo/halo.wuhb` is expected).
- How long `FSAGetFreeSpaceSize` takes on a large card (wut's `statvfs` refuses the SD card, perhaps for that); the log times it.
- The console's clock, against which certificates' dates are checked.
- Mbed TLS is built without its threading module: the updater and internet play's DTLS (`posix_dtls.c`) share its random generator
  without a lock, as the desktop updater and DTLS do.

### Console test plan

With the owner at the console, in a test repository with a test key (never the releases' own), each build made as in step 4:

1. **Update**: install build A by hand; publish B (newer). A offers B at the main menu (`Update: build B is offered`); A downloads, swaps
   and starts B (`Update: build B is in place of build A`); B logs its trial and `reached its main menu: it is kept`; the next start
   deletes `update/*.old`. `config.toml`, `data/` and the maps are unchanged.
2. **HOME and closing during a download**: HOME while the bar moves, then back: the download goes on (a `Range` in the log after the
   redirect, `downloaded` with the right hash). Then close the app during a download (HOME, Close software): the exit trail says `the
   updater's worker ended` and the close takes no longer than without it; the next start's A goes on from the `.part`.
3. **Refusals**: a manifest signed with another key (`not signed with this build's key: it is ignored`, no panel); a release whose
   `halo.wuhb` is not the manifest's (`damaged twice`, nothing changed, the `.part` deleted).
4. **Low free space**: a card with less than the release and 32 MB free: `NOT ENOUGH ROOM ON THE SD CARD`, nothing changed.
5. **Rollback**: publish C, a build made to crash after the shell has started and before the main menu (a deliberate fault in a scratch
   commit). B updates to C; C crashes; the next start puts B back and starts it (`build C crashed before it reached the main menu`, then
   `build B is back; build C is set aside`); B's main menu shows the notice; C is not offered again. Also with C hanging instead (no crash
   note): the third start puts B back.
6. **Offline**: no network at the start (unplugged, or the network off in the system settings): the game starts as usual; the log says the
   manifest could not be fetched; no panel.
7. **End on the production build**: put the production build back by hand, delete `update/`, and leave the card on the safe settings.

## Halo Custom Edition maps

Players put Custom Edition maps on the card as they are (`data/custom_maps/`, with their own `bitmaps.map`, `sounds.map` and `loc.map`:
[PLAYING.md](PLAYING.md)). A Custom Edition map is never converted ahead of time as the Xbox maps are: its hash is its network version and map
sharing sends its bytes, so the file stays as Halo PC wrote it, little-endian, and the game turns it as it loads
([docs/custom_edition_caches.md](../../docs/custom_edition_caches.md), "Big-endian builds"):

- **The tag cache.** The maps' tag data is linked at `0x40440000`, 23 MiB, which is in the app's MEM2 heap 3.4 MiB below the Xbox window and free
  at start-up: `src/wiiu_window.c` takes it right after the window, the same way, and logs `Custom Edition tag cache: 23 MiB at 40440000` (or
  why not: then no Custom Edition map runs). `src/xbox_memory_wiiu.c` gives it to the game while `game.custom_edition` is on, and back to the
  heap when it is off. With it, the map lists, loading and map sharing (which waited for it) run as on the desktop.
- **The byte order.** The loader, the conversion and the group inference read the file's little-endian values as they always have; then
  `port/linux/game/tag_swap.c` turns the tags to big-endian, by the same schemas and scalar layouts the Xbox maps were converted with
  (`tools/wiiu/map_convert.py`, whose walk it is), and everything after reads them as the console's own: the sounds decoded, the validator, the
  bitmaps, the scripts and the models (their model data turned as it is read). Each structure BSP is turned as it is read. Pixels and ADPCM
  samples stay little-endian, as in the converted Xbox maps.
- **Textures** whose channels Halo PC keeps elsewhere (model shaders' multipurpose maps, HUD meters) are sampled through their image's
  component map (`src/xbox_textures_gx2.c`, `src/wiiu_gx2.c`), as the desktop's texture swizzle samples them. A meter drawn from a sheet in this
  build's order, which the desktop's pixel shader reads in Halo PC's for that draw, is drawn as its texture is (the shader pack has no such key).
- **Memory.** Besides the 23 MiB tag cache (in the heap, not the window), a Custom Edition map takes from the 128 MiB window what an Xbox map
  does (the Xbox's 22 MB texture cache and 4 MB sound cache: Custom Edition maps can draw more texture than that, and the rest is drawn as the
  default texture) plus its models' and BSP's compressed geometry, up to about 15 MB for the largest maps; geometry that does not fit is drawn
  from the heap (slower, and logged). Ogg Vorbis sounds are decoded at load into a buffer of the heap.
- **Shaders.** What a draw's shader key is not in `shaders.pack` draws is skipped (`halo/shader-misses-*.txt`), so a map's new keys must be
  captured on the console and the pack remade.

Checked on the Mac (no Custom Edition map is on it, so the repository's test maps stood in: StageKit's, `tools/stagekit`, written little-endian
as Custom Edition caches are, and the synthetic caches of `tools/test_cache_file_formats.py`): the C swap and `map_convert.py --custom-edition`
make the same bytes of every map's tags, BSPs and model data, and the C swap and `map_convert.py` the same bytes of all 24 Xbox maps
(`map_validate --swap`, `--swap-xbox`, under qemu-ppc and qemu-i386); `map_validate` needs no correction of the StageKit maps, big-endian or
x86, and fuzzes them the same; the big-endian Linux build plays them tick for tick as the x86 build does. Not run on a console.

### Before the console: the shader keys, on the Mac

A Custom Edition map's draws need pixel shader keys the pack may lack (below). The digest harness (`tools/test_wiiu_game_digest.py`) plays a map through the
Wii U's Direct3D layer on the Mac and writes the keys the console's GX2 thread would look up, in the lines the missing-shader capture writes:

    GAME_DIGEST_CUSTOM_MAPS=<folder of the maps with bitmaps.map, sounds.map, loc.map> GAME_DIGEST_CONFIG=<a copy of the card's config.toml> \
      python3 tools/test_wiiu_game_digest.py run NAME --custom-map bloodgulch --ticks 1500 --keys
    python3 tools/wiiu/key_diff.py build/game-digest/runs/NAME/out/keys.txt <tools/wiiu/shader_keys.txt> -o missing.txt

`missing.txt` goes to `tools/wiiu/shader_pack.sh`. The run sees what the bot sees from where the map starts, so it finds the keys of what is in view (on Xbox Blood
Gulch it found 6 the pack lacks; on b30's first 1,800 ticks 24), not every key the map will ask for: of the 117 keys the console missed at b30's start on
2026-11-02 it had 4, but of their 35 shaders (canonical keys, as the branch `wiiu-shader-fallback` counts them) 23. The console's capture stays the way to the rest. Nothing is drawn by it, nothing is
written to a console; a map's own scripts and sounds run as in any run of the digest harness.

### The Custom Edition test plan

With the owner at the console and the card on the safe settings, one driver at a time; a Custom Edition map (stock `bloodgulch.map` first),
`bitmaps.map`, `sounds.map` and `loc.map` in `data/custom_maps/`:

1. **Start-up**: `log.txt` has `Memory window: 128 MiB at 41ea0000` and `Custom Edition tag cache: 23 MiB at 40440000` (the attempts before
   it, if any, say how far the heap's start moved), and the black box's MEM2 heap line. With `game.custom_edition = false`: `given back to the
   heap`, and the heap 23 MiB larger.
2. **The map lists**: Create Game (LAN) → Map shows CUSTOM MULTIPLAYER with Bloodgulch (its picture if `bloodgulch.bmp` is there).
3. **Bloodgulch, slayer**: it loads and plays. `data/debug.txt`: `custom edition: loading`, the tags' counts, `N tags turned to big-endian`,
   `the map's tags needed 1 corrections` (loc.map's gamespy font, as on the desktop), the conversion's counts as the desktop records them
   (79 BSP materials, 21 shaders renumbered, 10 chicago extended, 676 bitmaps, 32 HUD placements), the Ogg Vorbis sounds converted to ADPCM,
   `model parts converted`, `the structure BSP ... has its vertices compressed`; no `cannot be turned` or `refused`. On screen: the level, its
   models and its HUD drawn (the HUD meters' fill, the multipurpose maps' shine on the vehicles); sounds and the announcer play.
4. **Shader keys**: `halo/shader-misses-*.txt` after a few minutes of play (and of each later map): the keys go into the pack
   (`tools/wiiu/shader_pack.sh`), and the next run's log shows no draw skipped for a missing key.
5. **Times and memory**: from `loading` to the first frame, and the swap's share of it (the log's timestamps around `turned to
   big-endian`); the MEM2 heap's free bytes in the game; `YOU GOT STABBED` (the texture cache full) or `SOUND CACHE BLOWN` in `debug.txt`;
   `geometry drawn from outside contiguous memory` (the window full).
6. **Larger maps**, one at a time, the same checks: a large multiplayer map (hugeass, bigass), a texture-heavy one (Elite_Alpha_Siege),
   one with many Ogg Vorbis sounds, a campaign map (CMT's a30), a protected map.
7. **Map transfer**: OpenCE on an x86 PC (Linux or Windows, the same network version) hosts a LAN game on a Custom Edition map the Wii U
   lacks; the Wii U joins and downloads it (and any resource map it lacks) into `custom_maps`, then plays; the downloaded file's SHA-256 is the
   host's. With `network.map_download = false` the Wii U shows the error and stays out. Then the Wii U hosts a map the PC lacks, and the PC
   downloads it from the Wii U.
8. **End**: delete the test maps (or leave them: they change nothing else), and leave the card on the safe settings.

## What is written and not run on a console

Compiled, linked and tested on the build machine where it can be, not run on the Wii U: the high-res text (`text_hires.c`: the fonts in the maps' tags replaced by
Overpass at the display's resolution; `tools/wiiu/shell_tests.sh` rasterizes glyphs from the big-endian embedded fonts under qemu), UPnP (miniupnpc built with
`include/wiiu_net_compat.h`; a router's port forwarding for internet play, and `posix_upnp.c`'s port and address byte order fixed for big-endian), the GamePad's
gyro aim (`input.wiiu_gyro_aim`), tap-to-skip, Classic Controllers, and voice chat (above: its queues, rings, resampler and microphone readings
tested on three machines, the rest built). The high-res HUD and menu pictures (`hud_hires.c`, `menu_files.c`) are not ported: they
draw meters through shader variants that are not in the shader pack, which is made from what a console run asked for (`halo/shader-misses-*.txt`).

## What it takes to run

The link needs **300 stubs** (`stubbed_symbols.txt`), which are what the Wii U's platform layer still has to provide, by the
linker's own count:

| Symbols | What | Approach |
| --- | --- | --- |
| 108 | Direct3D 8 (`D3DDevice_*`, `Direct3DCreate8` and the rest) | a null renderer first (the headless milestone), then GX2 (plan 6.4) |
| 50 | DirectSound (41, `dsound_sdl.c`) and voice chat (9, `voice_audio.c`) | AX; voice chat is `src/wiiu_voice_audio.c` |
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

## Shaders the pack lacks

The shaders are compiled ahead of time into `halo/shaders.pack` (`gx2_shaders.c`, `tools/wiiu/shader_pack.sh`), so a key nobody
met before has none. A pixel key holds render states its shader never reads (the combiner stages past the count keep the values
of the last draw that used them): the 2200 pixel keys met by 2026-10-30 have 186 shaders between them. So a pixel key the pack
lacks is looked for again as its canonical key (`shader_key_canonical`, `shader_key_wire.h`: what `nv2a_psh.c` does not read
set to 0), whose shader is the same; the pack builder writes every key's shader under its canonical key too, and stops if the two
GLSL texts differ (`pack_gen.c`; `pack_gen --check` tries random keys). Failing that, the draw is made with a stand-in
(`shader_key_fallback`): what the key's final color and alpha depend on is followed through its combiner program, and the
stand-in keeps what it can of that: stage 0's texture (2D, 3D or cube, as the key samples it) times the diffuse color, each where
the color or the alpha depends on them (untextured, the diffuse color, else C0, else nothing), with the key's alpha test and alpha
kill. It is itself a key, generated like every other, so it meets every vertex shader; the pack has all 114 of them. Models, the
first-person weapon, the sky and the HUD are then drawn roughly rather than not at all (on 2026-10-31, with 29 of Blood Gulch's
shaders taken out of a test pack, 8400 of 11100 draws were stand-ins, and the sky was drawn once its stand-in followed its alpha).
A key in `tools/wiiu/shader_exclude.txt` is marked in the pack (stage 2 or 3, no shader) and still skipped, and a vertex key the
pack lacks still skips the draw. The 5-second log counts the draws made with a canonical key's shader and with a stand-in, and the
first 40 such keys are logged by name; each key is written down for the next pack as before.

### Keeping the pack complete, on the Mac

The keys a map needs are the game's own, so they can be met without the console: the digest harness (`tools/test_wiiu_game_digest.py`) plays the
real game under qemu-ppc through the Wii U's Direct3D layer, and `--keys` writes the pixel and vertex keys the GX2 thread would look up (the lines
the missing-shader capture writes, each after a `# frame N site ADDRESS` comment saying the frame and the code that set the pixel shader).
`--level NAME --sweep bsp+lit+objects+effects+damage+weapons` (`game_digest_sweep.c`) tours what the map can draw instead of waiting for a player to
meet it: every BSP structure's materials from a camera at a triangle of each, again with a dynamic light at each stop; every object tag (made where
the player is not, looked at, taken away), with a unit made active-camouflaged, with a light beside it, and in a cluster with fog; every effect and
damage effect tag; each weapon in the player's hands, zoomed; and with `scripts` and `GAME_DIGEST_SWEEP_SCRIPTS`, the level's cutscene scripts by name.
`tools/wiiu/key_classes.py RUN/out/keys.txt --have tools/wiiu/shader_keys.txt` says what that is worth: the keys are grouped by canonical key, and
only a key in a class the list has none of is a new shader (the others are drawn with their canonical key's shader). `GAME_DIGEST_KEYS_ALL_VERTEX`
(set by `--keys`) lists, at the end of a run, every vertex key the game can ask for (its 67 vertex programs, each with the attributes its declaration
packs and with none, for immediate-mode draws): they can all be known in advance, and have to be, since a vertex key the pack lacks has no fallback.
What this cannot reach: the raw pixel keys of the console (a stage past the count holds what an earlier draw left, so the same shader has as many keys as
there were draw orders, which the canonical key makes one), anything that needs more than one player, the network, the pause menu, and the settings
that change the keys (`display.high_res_hud`: the meter keys; the Custom Edition maps' own shaders, `--custom-map`).

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

### GPU mip filtering qualification

The fallback for a composite mip level the game did not render has a staged
GPU implementation (`src/wiiu_gx2_mip.c`). It copies the exact preceding
level into a single-level scratch texture, filters into a separate colour
scratch image, then copies to the destination level. The shader reconstructs
byte values before computing `(sum + 2) / 4`, preserving the CPU reference's
per-channel half-up rounding, one-dimensional images and discarded odd edges.
Scratch images are reused in GPU command order; replaced allocations use the
renderer's existing deferred retirement. The quad is immutable. The helper
does no CPU readback or explicit completion wait. A failed shader or scratch
allocation leaves the existing CPU filter available.
Growing scratch uses the renderer's normal retirement policy, which can
drain the GPU if its global deferred-allocation queue is full. Steady-state
filtering does not grow or retire scratch storage.

This implementation is not enabled by default until native qualification:

- `halo/gpu_test.txt` `0`: the CPU reference, with mip calls/pixels and wait
  time recorded in the ordinary five-second GX2 log.
- `2`: run the asset-free native qualification at startup. Its 40 byte-exact
  readback checks queue 20 derives before one completion wait. They cover
  independent RGBA calibration, all 1021 possible four-byte sums in every
  channel, half ties, alpha, odd/one-dimensional sizes, nonzero source and
  destination levels, queued resizes, chains, later updates and explicit
  intermediate levels. GPU derivation is enabled for this run only if all
  checks pass. The game context is restored after the qualification.
- `3`: enable the staged GPU path for same-save comparisons after a successful
  qualification of the same binary. No qualification readbacks run in this
  timing mode. The existing `1 [draws]` frame-replay test is unchanged.

`GPU submit` in the mip log is CPU time issuing the helper's commands, not
GPU execution time. Actual GPU-frame timing starts before this helper when
it is the frame's first GPU draw. The existing source-update stamps and
context restoration still govern normal scenes; a passing helper test does
not establish a game speedup or prove that an affected scene derives mips.
Compare CPU/GPU call counts, reused levels, failures, pixels, waits, image
output and whole-frame timing from the same camera/save before promotion.

An explicit image must replace a generated level even when their numeric
stamps coincide. After a copy or derivation, only the following contiguous
generated chain is invalidated; an explicit intermediate remains independent.
`python3 tools/test_wiiu_composite_mips.py` executes the actual refresh policy
with recorded graphics calls and covers those transitions, unchanged reuse,
failed derivation and context restoration. It also runs on big-endian PowerPC.

`python3 tools/test_wiiu_gx2_mip.py` includes the production helper in a
call-recording host fixture under ASAN/UBSAN. It exercises shader/allocation
failure ordering, invalid and zero-sized inputs, scratch alignment/retirement
and immutable vertices. Two deliberate faults must fail. It does not emulate
GPU pixels. The fixed shader's sources and byte identity live in
`shaders/mip.*`; regenerate the embedded GFD with
`python3 port/wiiu/tools/embed_mip_shader.py --compiler <existing-glslcompiler>`.

## Smooth by default: the tick overlap and the governor (since `wiiu-rc4`)

A game of **one local player with no network of any kind** is smooth with no card file at all: the game's ticks run on a thread of their own on core 2 beside the
drawing (the tick overlap, with the tick snapshot behind it), and the dynamic quality governor holds the frame near 22 ms (steps 0 to 3: it sheds only small, far or
thin things, never a unit, a vehicle, a weapon or a projectile). On the console, in the scripted one-player b30 fight, that was about 25 to 31 frames a second without
it and 45 to 47 with it (`reports/research/2026-11-01/ticks-stage4.md` sections 11 to 15, `ticks-stage5-snapshot-cost.md`); the owner's 25 minutes of rc2 with it ran at a
mean of 58.7 (`reports/research/2026-11-04/rc4-defaults.md` has the figures and the checks).

**The map decides, at each map start.** The log says it, once a map: `smooth defaults: ON for this map (one local player, no network)` or
`smooth defaults: OFF for this map: <the reason>`, and then what each of the four switches came to and where it came from. The smooth settings are for a map with
exactly one local player, no network session of any kind (hosted, joined, system link, internet: a network game stays the serial loop, with the ticks on the game
thread), no split screen, no saved film, no dedicated server, no editor, not the main menu's scene, and `display.interpolation` on. Every other map, and the start-up
before the first map, runs as the build without the smooth defaults does. The decision is taken again at every map start (a level change, the way back to the main menu,
a game started from it, a loaded save), and the overlap's state is made again from nothing each time: the tick thread idle, the snapshot's ring and its marks, the owners'
queues' mode, the late state, the listeners. A map that was one and stops being one (a second local player counted after the map began, a network session opened, a film)
is decided again at the next frame's sync point (`smooth defaults: this map stops being one for them`). A revert to a checkpoint is the same map: the decision stands and
the ring is forgotten (as before). The serial fallback stays: when the tick thread is late the clock is held to one tick a frame, and below 20 ticks a second the frames
are the serial loop's for 10 s and more (`tick_overlap_late.txt`). The tick thread is never on core 0 by default (`tick_core.txt`: absent means core 2).

**The four card files** (`tick_thread.txt`, `tick_snapshot.txt`, `tick_overlap.txt`, `governor.txt`) **changed meaning in `wiiu-rc4`**: before, absent and -1 meant off, and now they mean
the default, which is on for a map of one player. The other files of this section keep -1 as "no file".

| Word in the file | Meaning | In the log |
| --- | --- | --- |
| absent, or -1 (`governor.txt`: or a first word of -1 or `default`) | **the default: the smooth setting, for a map the smooth defaults are for; off for any other** | `<switch> on, the default (halo/<file>: absent or -1)` |
| 0 (`governor.txt`: `off`) | **off, for every map**: an explicit opt-out for a test or a comparison. One 0 among the first three turns the whole tick overlap off (it needs the thread and the snapshot; the other two then default to off, not to half of it); the governor is its own | `<switch> off: its card file holds 0` / `off: another of the three is 0` |
| above 0 (`governor.txt`: `on`, `1`, or any line of settings) | on, as it has always been (the snapshot's number is its bits), **but never for a map that is not one for them**: a network game, a split screen game and the main menu stay the serial loop whatever the file holds | `<switch> on, its card file`; for a map that is not one: `off: not for this map`, and `a card file that holds 1 is on only for the maps the smooth settings are for` |

The owner's card (`tick_thread.txt` 1, `tick_snapshot.txt` 1, `tick_overlap.txt` 1, `governor.txt` `on`) is therefore the default for him in a one-player map, and
now also off in a network game and in split screen, where before the overlap's switches were on in the build and the frames were the serial loop's. A harness that writes
-1 to put a file back now puts the default back, which is on: **write 0 for off.** `gpu_offload.txt` is not part of this: it stays off unless the file says 1 (not
reproduced on the console). Off by default and unchanged: `gpu_offload`, `hotpaths`, the Broadway kernels, `stream`, `pacing`, dynamic resolution, the overlay (L + R + - on
the GamePad).

**The map summary.** At the end of each map (a level change, the way back to the main menu, the game's quit) the log says how it played: `map summary (levels\b30\b30,
smooth defaults on): 18432 frames in 312.4 s, mean 59.0 fps, 1% low 41.2 fps (24.3 ms), longest frame 190 ms, hitches over 100 ms: 3`, then the share of the map's 5 s
windows at 57 fps and over, 45 to 57 and under 45, the overlap's falls back to the serial loop and times the clock was held, and the share of frames at each governor
step. The 5 s lines of the log are unchanged.

## Threading study files on the card

These files of `wiiu/apps/halo/` hold one number each and are read once at start. **A negative number (-1) is the same as no file** (the default),
so that a harness that must not delete a card file can write -1 to put one back. 0 and the positive numbers keep their meanings. **Except the four of the section above**
(`tick_thread.txt`, `tick_snapshot.txt`, `tick_overlap.txt`, `governor.txt`), whose default is now the smooth setting for a map of one local player.

| File | Absent or -1 | Meaning |
| --- | --- | --- |
| `tick_thread.txt` | **on** for a map of one local player (0 for any other) | 1: the game's ticks run on a thread of their own; 0: off |
| `tick_overlap.txt` | **on** for a map of one local player | 1: the ticks run beside the drawing (needs `tick_thread.txt` and `tick_snapshot.txt` not 0); 0: off |
| `tick_snapshot.txt` | **1** for a map of one local player | bits: 1 on, 128 the check of the objects left out, 256 to 2048 turn one change off, 16384 the tick thread writes its copies back to memory, 32768 the game thread asks for the newest copies' cache lines when a frame begins (the last two are off unless set; the report `reports/research/2026-11-01/ticks-stage5-snapshot-cost.md`); 0: off |
| `tick_core.txt` | 2 with the overlap on (the default for one player), else 0 | the core of the tick thread: 0, 1 or 2 |
| `tick_priority.txt` | 0 | the tick thread's priority offset below the game's, 0 to 8 |
| `gx2_idle_sleep.txt` | 0 | microseconds the GX2 thread sleeps between looks for work when the tick thread shares its core; 0 is the yield |
| `tick_quantum.txt` | the system's | microseconds: the tick thread's run quantum (`OSSetThreadRunQuantum`), untouched if absent |
| `stream_slots.txt` | 6 | 2 to 8: the stream slots the Direct3D layer uses (the game thread waits for a free one) |
| `tick_overlap_late.txt` | 1 | the answer to a late tick thread (under 25 ticks a second over 5 s, and the thread busy over 95% or the picture 1.5 ticks behind): the clock held to one tick a frame, and the serial loop only if the game is then under 20 ticks a second. 0 off, 1 on, above 1 the fewest frames a window needs, for tests (the clock alone then makes a window late) |
| `tick_overlap_serialize.txt`, `tick_overlap_stall.txt`, `tick_overlap_jitter.txt` (ms), `tick_overlap_force_serial.txt` (1), `tick_overlap_frame_stall.txt` (ms) | 0 | test aids of the host runs: wait for each job; hold each job back; the tick thread sleeps at random at 9 places of a job; every frame is the serial loop's |
| `perf_counters.txt` | 0 | 1, 2 or 3: the event set of the cores' performance counters |
| `tick_owners.txt` | 0 | the tick owners' queues, mode 1 or 2 |
| `game_core.txt` | 1 | the core of the game thread: 0, 1 or 2 |
| `gx2_thread.txt` | 7 | the GX2 thread's mode (0: the game thread makes the calls) |
| `gpu_offload.txt` | 0 | bits: 1 the interpolation's blends without square roots, 2 the GX2 census lines, 4 host-only comparison; 0 or a negative number: none |
| `governor.txt` | **on** (the capped governor: target 22 ms, steps 0 to 3) for a map of one local player; elsewhere as `display.dynamic_quality` sets it | text, not a number: a first word `on`/`off`/`1`/`0`, or settings (`step 2`, `target 25`, `max_step 3`); **a file whose first word is -1 or `default` is the same as no file**; `off` is off for every map, `on` and settings are on only for the maps the smooth defaults are for |
| `hotpaths.txt` | 0 | 1: while an object or an actor updates, the tick asks (dcbt) for the cache lines of the next ones (`halo_ppc_hotpaths.h`); an A/B switch, off unless 1. The memory probe of 2026-11-04 found sequential loads with dcbt slower than without, so time it both ways |
| `memcpy_touch.txt` | 0 | 1: the port's memcpy (`wiiu_memcpy.c`) asks for the source's lines 96 bytes ahead with dcbt (as it did until 2026-11-04); 0, absent or -1: it does not (the default: three alternating pairs of fight runs on the console had the tick thread 0.7 to 1.8 ms faster and the objects pass 0.5 to 1.4 ms faster without it; the dcbt of the destination (dcbz) stays) |
| `async_save.txt` | absent | 0: a checkpoint's 20 MiB write to `z/savegame.bin` is made on the game thread, as it was until 2026-11-04 (the game stood still 1.3 to 1.6 s at every checkpoint); 1, absent or -1: the game thread copies the game state (a few ms) and a writer thread (core 0, below the game thread) writes it in pieces; a revert, a load, the next checkpoint and the app's close wait for the write (`port/linux/game/async_save.c`, `source/saved games/game_state_xbox.c`; reports/research/2026-11-04/async-save.md) |
| `async_save_verify.txt` | 0 | 1: every checkpoint is checked (the copy against the game state, the file read back against the copy) and the copy's CRC is logged; costs a second or two per checkpoint, for tests |
| `async_save_piece_kb.txt` | 1024 | the size of the pieces the writer makes the write in (64 to 4096) |
| `async_save_fault.txt` | 0 | a test's faults on the checkpoint's write: 1 every second write fails after its first piece (the game thread writes it again), 2 and the second try fails too (the checkpoint is lost), 3 a write takes 800 ms more, 4 4 s more, 5 9 s more and the waits for it give up after 0.5 s. Only with the card snapshotted: it touches `z/savegame.bin`, the session's own scratch file, nothing of a profile |
| `stream.txt` | 0 | 1: texture streaming (the section below); `stream_delay.txt` (milliseconds, a test aid for hosts) |
| `overlay.txt` | 0 | 0 off, 1 the compact strip, 2 the detailed panel (L + R + - on the GamePad goes round the three); -1 is off |
| `pacing.txt` | 0 | text: `0` off, `1 [D]` tear when the frame is late, `2 [D]` never wait for the vsync (D: at most 1 or 2 swaps wait for their flip); -1 is off |
| `broadway.txt` | 0 | 1: the Broadway kernels that pass the self-test, 2: the self-test only, 3: 1 and GQR5. The console's self-test of 2026-11-04 failed `matrix4x3_multiply` (wide and denormal cases): it stays off |

## Start-up

The log says where the first seconds go. Once the main menu's widgets have been processed (the first time its highlight can be drawn) and the menu has been
drawn a few times, `log.txt` has a block of lines, each starting `startup:`, from `port/linux/game/startup_timeline.c`:

- the milliseconds to a live menu, and the segments: each is the mark that ends it and the milliseconds since the mark before (`log_up`, `updater`,
  `game_thread`, `main`, `memory`, `tag_files`, `game_state`, `rasterizer`, `input`, `sound`, `game_init`, `map_tags` to `menus_widgets` for the menus'
  map and their build, `ui_map`, `widgets`, `precache`, `present_N`, `menu_live`);
- events of other threads, in milliseconds since the start (`menus_read_end`, `menu_art_ready`, `shader_pack_ready`, `free_space_end`, `fs_start`,
  `fs_playlists`, `fs_profiles`, `fs_end`, `gx2_present_N`);
- work added up under a name, as calls, milliseconds and kilobytes (`png_decode`, `png_mips`, `png_send`, `png_all`, `atlas_all`, `tex_get`, `texture_wait`,
  `read_big` and `read_small` for the map and card reads, `shader_pack_read`, `shader_pack_wait`, `fs_checks`, `fs_all`, `free_space`, `ui_wait_checks` for the
  frames the menu's widgets were held back by the saved games' checks, `mt_*` for the menus' build), and, for the first twelve, `startup: waited N ms for the
  pixels of <bitmap>`.

Four things the start-up does differently from before, each switched off by its bit in `halo/startup.txt` (a number; no file: all on):

| bit | what |
| --- | --- |
| 1 | the saved games' checks, which the main menu's widgets wait for, start when the game's systems are up, not after the menu's map is loaded |
| 2 | the menus' XML is read by a thread of its own from the start of the game's memory |
| 4 | that thread then decodes the pictures of the main menu (every frame of each: the highlight's too) into the texture cache's chains |
| 8 | the shader pack is read by a thread of its own, not by the GX2 thread at the first draw |
| 16 | the card's free space is asked on a thread of its own, which the saved games' checks do not wait for |
| 32 | after the main menu's pictures, those of the screens its items open, as far as 32 MB of ready images go (the first page of each costs about 45 ms a picture otherwise) |

## Texture streaming (`halo/stream.txt`, default 0)

The game thread waits for a bitmap's pixels the first time the renderer draws with it (`rasterizer_set_texture_bitmap_data` asks the texture cache to block): the
map's file is read by the cache file thread and the game thread spins until the read is in. On the console that is 50 ms or more for a 350 KB bitmap (the card gives
about 14 MB a second for big reads and takes 4 ms for a small one), a second and more when a room's worth is drawn for the first time. With `halo/stream.txt` set to 1:

- a 2D bitmap with at least 48 KB of pixels before its small mipmaps (`port/linux/game/texture_stream.h`: swizzled or compressed, power of two, three or more
  levels, not palettized) is read in two reads that write no byte twice: the tail (the levels of 32 by 32 texels and under, 0.5 to 8 KB) first, before the reads that
  are waiting (`cache_file_read_first`), then the rest as the read always was;
- while the rest is not in, the renderer draws with the tail: a second texture header (`xbox_texture_cache.c`, `texture_streams`) whose data pointer is at the tail's first
  level and whose size and level count are the tail's. The picture is the texture at 32 texels or less, and the full one from the frame after the rest is in;
- a bitmap that is not cut (small, cube, 3D, linear, palettized), or whose tail has not come in 100 ms, is waited for as before;
- the fences are the layer's own: a texture's block is not given to another while the draws that used the texture or its tail are not over (`texture_cache_locked_block_proc`
  asks both headers' `Lock`, the GX2 thread's retired submission), and the cache file thread's writes announced to the layer (`halo_stream_before_write`) never hand the
  draws over, which are the game thread's.

`halo/stream_delay.txt` (milliseconds, default 0) makes the cache file thread wait before each read that does not go first, to test the path on a machine whose reads are
quick. The log says `stream: <bitmap> is read in two ...` for the first six, `stream: <bitmap> was drawn from its tail ... until the whole of it was in` for the first twelve
that were, and every 10 s the totals (bitmaps cut and not, drawn from tails, the time to the whole bitmap, the waits). With the file absent or 0 the code does what it
did (the digest harness's `game-digest` is `ceb14fd37a6d23ee` over 1,400 ticks, as before).

`halo/startup.txt` `0` is the old start-up (the menus' tags look their names up in hash tables, and the texture work is done a word at a time, in either).
The drawn frames are the same with any of them (the digest harness: tools/test_wiiu_game_digest.py), but for the first two of the main menu, where the highlight is
there a frame or two sooner with bit 1.

## Taking drawing work off the game thread (`halo/gpu_offload.txt`)

`halo/gpu_offload.txt` is a number (no file, `0` or a negative number: none on, the frames drawn exactly as `wiiu-big-endian` draws them). The study behind it
(`reports/research/2026-11-03/gpu-offload.md`) found that almost none of the game thread's drawing time can move to the GPU in this renderer: its vertex programs are the
Xbox's, their constants are the 192 uniform registers of one shader mode, and a GX2 call costs the GX2 thread about 0.1 microsecond. The bits are what it did find:

| bit | what | cost, gain (console, expected; the log lines say what happened) |
| --- | --- | --- |
| 1 | the interpolation's blends without square roots: the test that a basis is a rotation compares squared lengths, the quaternion of a basis and the normalising of a blend use 1 / sqrt from `frsqrte` and three Newton steps (`port/linux/game/render_interpolation_fast.h`) | the blends were 2.3 ms of a fight's frame, nine correctly rounded square roots (about 140 cycles each) and seven divisions for each node, 40 percent of its 3,200 cycles; expected 0.7 to 1.0 ms less. The poses differ by a few units in a float's last place |
| 2 | the census: the GX2 thread times its state derivation, the shader keys' lookup and the decals' expansion, and counts the draws that repeat the draw before, an earlier draw of the frame (state, shaders and textures), and the same mesh with them | about 0.1 ms a frame on the GX2 thread; changes nothing drawn |
| 4 | with bit 1: every blend is made both ways and the largest difference said every 600 frames | doubles the blends' cost: for the host's runs |

The log says the bits at the first draw (`gpu offload (halo/gpu_offload.txt found): ...`), the census every five seconds (`gpu offload: the GX2 thread per frame ...`,
`gpu offload: of N draws a frame ...`) and the check (`gpu offload check: ...`). The blends' time is the objects pass's and the `blends` figure of the overlap's log lines.
`tools/test_wiiu_gpu_offload.py` checks the arithmetic against the original's over 400,000 random bases, natively and big-endian under qemu.
