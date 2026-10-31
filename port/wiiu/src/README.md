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
| `wiiu_crash.[ch]` | The fault handler's note. |
| `wiiu_atomic.[ch]` | 64-bit atomics (libatomic's names), for a core that has none. Tested on the desktop. |
| `wiiu_libc.c` | `pause` and `sysconf`, which `xbox_kernel.c` calls and newlib lacks. |
| `wiiu_tls.c` | Thread-local storage: the four `__syscall_tls_*` newlib's pthread keys need and wut lacks, for libgcc's emulated TLS. |
| `wiiu_voice_audio.c` | Voice chat's sound (`voice_audio.h`): the GamePad's microphone (mic.rpl), Opus on a voice thread of its own, the voices for the mixer, HOME and the close, the microphone probe. Cafe OS half. |
| `wiiu_voice_core.[ch]` | Voice chat's queues and rings between its threads, the 32 to 48 kHz resampler, the two readings of the microphone's ring. Tested on the desktop (`tools/harness/tests/test_wiiu_voice.py`). |

None of this has run on a console.
