# The Wii U's own units

C sources here are compiled into the Wii U build (`tools/wiiu_build.py`, `ninja linux_wiiu`): the shell that runs around the game,
what newlib and wut lack, and, as they are written, stand-ins for the platform layer units the build cannot compile
(`WIIU_EXCLUDE` in that module, each with the reason). The link has generated stubs (`tools/wiiu_stubs.py`) for what is still missing.

- A unit named `wiiu_*.c` is the C library's side: built as `posix_*.c` are (the host ABI, no 16-bit `wchar_t`, no XDK
  headers), so that wut's and newlib's structures have their real layout. It includes wut's headers. All of these are.
- Any other unit is built as the platform layer is (the game's ABI, the XDK declarations), and sees the game's types.
- When a unit here replaces an excluded one, take that one off `WIIU_EXCLUDE`, and the symbols it now defines off
  `port/wiiu/stubbed_symbols.txt` (`tools/wiiu_stubs.py --compare` says which).

| Unit | What |
| --- | --- |
| `wiiu_main.c` | The system's `main`: runs the shell with the game's, `halo_game_main`. |
| `wiiu_shell.[ch]` | The shell: ProcUI, the game's thread, the watchdog, the heartbeat, the black box, the log; and what the rest of the port calls (`wiiu_log`, `wiiu_phase`, `wiiu_frame`, `wiiu_exit_requested`, ...). Cafe OS half. |
| `wiiu_shell_watch.[ch]` | The decisions: has the game stalled, is the close deadline past. No clock and no thread of its own. Tested on the desktop. |
| `wiiu_shell_log.[ch]` | The log queue and the worker that writes it to the card. Tested on the desktop. |
| `wiiu_shell_blackbox.[ch]` | The black box's records and file. Tested on the desktop. |
| `wiiu_drs.[ch]` | Dynamic resolution's controller: from each frame's GPU time and its wait for the flip, the step of the screen's lines the next frames draw (`wiiu_gx2.c` runs it at each present). No clock and no thread of its own. Tested on the desktop (`tools/harness/tests/test_wiiu_drs.py`). |
| `wiiu_crash.[ch]` | The fault handler's note. |
| `wiiu_atomic.[ch]` | 64-bit atomics (libatomic's names), for a core that has none. Tested on the desktop. |
| `wiiu_libc.c` | `pause` and `sysconf`, which `xbox_kernel.c` calls and newlib lacks. |
| `wiiu_tls.c` | Thread-local storage: the four `__syscall_tls_*` newlib's pthread keys need and wut lacks, for libgcc's emulated TLS. |
| `wiiu_input.[ch]` | The GamePad (VPAD) and the Pro and Classic Controllers (KPAD): their reads, their connections (`KPADSetConnectCallback`), their lights and motors. |
| `wiiu_pad_players.[ch]` | Which player each controller is: the map that keeps a controller's player while it goes and comes back, the layouts (`input.wiiu_controller_layout`, "auto" among them) and the number its lights show. No system call. Tested on the desktop (`tools/harness/tests/test_wiiu_pad_players.py`). |
| `wiiu_virtual_pads.[ch]` | The virtual controllers of `debug.virtual_gamepads` and `debug.virtual_gamepad_script` (the desktop settings of those names): the script and the clock in, each pad's Xbox buttons and sticks out, which `xinput_wiiu.c` puts on the ports (pad n is port n - 1). For testing split screen with no controllers. No system call. Tested on the desktop (`tools/harness/tests/test_wiiu_virtual_pads.py`). |
| `wiiu_update.[ch]` | The self-updater's Cafe OS half: its worker thread, the panel at the main menu and its buttons, the card, RPXLoadingModule's unmount and launch (through `OSDynLoad`). Off unless the build is given a number, a repository and a key (`../README.md`, "Updates"). |
| `wiiu_update_core.[ch]` | The updater's decisions: the manifest, its Ed25519 signature, the journal, the swap, the trial and the rollback. Tested on the desktop, with the power cut at every step (`tools/test_wiiu_update.py`). |
| `wiiu_update_http.[ch]` | The updater's HTTPS: Mbed TLS over the BSD sockets (nsysnet's here), redirects, ranges, downloads that go on after HOME. Tested on the desktop against local TLS servers. |
| `wiiu_update_roots.h` | The certificate authorities the updater trusts, written by `tools/wiiu/update_roots.py`. |
| `wiiu_bw_kernels.c` | The Broadway kernels' switch (`bw/`: paired-single assembly for `matrix4x3_multiply`, the sphere test, the collision BSP walks, the reference search and the animation quaternion load): the card file `halo/broadway.txt` (0 off, the default), the boot-time self-test against the C each kernel replaces, the timing, and the calls a tick. `wiiu_bw_reference.h` is that C, `wiiu_bw_testcases.h` the cases. `bw/README.md` has the rest. |
| `wiiu_tick_parts.c` | The time of each part of a game tick (the sections of `game_tick`), said in the log every 150 ticks. |
| `wiiu_voice_audio.c` | Voice chat's sound (`voice_audio.h`): the GamePad's microphone (mic.rpl), Opus on a voice thread of its own, the voices for the mixer, HOME and the close, the microphone probe. Cafe OS half. |
| `wiiu_voice_core.[ch]` | Voice chat's queues and rings between its threads, the 32 to 48 kHz resampler, the two readings of the microphone's ring. Tested on the desktop (`tools/harness/tests/test_wiiu_voice.py`). |

None of this has run on a console.
