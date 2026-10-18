# The Wii U's own platform units

C sources here are compiled into the Wii U build (`tools/wiiu_build.py`, `ninja linux_wiiu`) and stand in for the
platform layer units it cannot compile (`WIIU_EXCLUDE` in that module, each with the reason). Nothing is here yet; the
build links with generated stubs (`tools/wiiu_stubs.py`) for what is missing.

- A unit named `wiiu_*.c` is the C library's side: built as `posix_*.c` are (the host ABI, no 16-bit `wchar_t`, no XDK
  headers), so that wut's and newlib's structures have their real layout. It includes wut's headers.
- Any other unit is built as the platform layer is (the game's ABI, the XDK declarations), and sees the game's types.
- When a unit here replaces an excluded one, take that one off `WIIU_EXCLUDE`, and the symbols it now defines off
  `port/wiiu/stubbed_symbols.txt` (`tools/wiiu_stubs.py --compare` says which).
