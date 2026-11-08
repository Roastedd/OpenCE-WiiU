# Asset-free regression tests

The engine's real functions, taken from the sources and compiled against a small fake world: no game assets, SDK or
graphics, so a fix can carry a test that CI runs.

    python -m pytest -q tools/harness

They need `clang` (or `CC`) and a 32-bit C runtime (`lib32-glibc`, `gcc-multilib`), as the Linux build does. pytest
finds every `tests/test_*.py`, so a new test needs no change to CI.

## A test

- `tests/<name>.c`: the fake world, only what the code reads, with what it calls stubbed as macros that name only the
  arguments they use; then `#include "under_test.inc"` and a `main` that runs the case named on its command line,
  checked with `CHECK`.
- `tests/test_<name>.py`: takes the code, enums and constants from the sources (`function`, `inline`, `enum_with`,
  `constant`) rather than copying them, so the test checks what the game builds; `build` compiles it (warnings are
  errors) and each case is a pytest test.
- Negative controls: the code with a deliberate fault (`mutated`) must fail a named case's `CHECK`. A test that cannot
  fail proves nothing.

## Limits

Functions are taken by matching braces; one that is not found fails the test. Code with many dependencies is better
compiled as whole files against stub headers. Whatever needs real maps or the whole game is checked in the game
(offline: `debug.null_renderer`).

## Wire tests on both byte orders

`tests/test_xplay_*.py` (tools/harness/xplay.py) prove that a big-endian build (the Wii U's) and the little-endian build every other
player has put the same bytes on the wire. Unlike the tests above they compile the game's own source files with the port's flags
(`tools/linux_build.py`), once for 32-bit x86 and once for 32-bit PowerPC, run both (qemu-i386 and qemu-ppc, or native `-m32`), and hold both
to the golden files in `tests/xplay_golden`:

    tools/ppc-be/run.sh harness tools/harness/tests/test_xplay_wire.py     # in the opence-ppc-be image, which has both
    HALO_XPLAY_UPDATE=1 ...                                                # rewrites the golden files from the x86 run

- `xplay_wire`: the distributed netcode's messages (ids 1 to 95): every entry structure with a value of its own in every scalar, sent as
  the game sends it (header, wire step, the host's batch or the reliable stream), each scalar expected little-endian at its offset;
  the hand-packed unit states and relayed actions.
- `xplay_messages`: the 35 game messages (join handshake, settings, updates), and the advertisement and settings record scalar by scalar.
- `xplay_direct`: voice, map sharing and map vote messages, which turn their own scalars.
- `xplay_engines`: each game type's state (capture the flag, oddball, king, slayer, race, infection).
- `xplay_p2p`: crypto (RFC vectors), KCP, a listing, and the master protocol's vectors and signed list.
- `xplay_layouts_*.inc` list the scalars of the structures from the declarations (`xplay_layouts.py`, libclang, in the image); the
  tests compile them against the declarations on both targets, and `test_layouts_current` regenerates them to find a field added
  without an entry.
- Each test has deliberate faults (negative controls) that the big-endian run must catch.
- `xplay_two_machines.sh`: a host and a client, each x86 or PowerPC, in one container on the loopback (needs the retail maps).
