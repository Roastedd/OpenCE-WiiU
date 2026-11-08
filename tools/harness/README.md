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

## The friends layer

`tests/test_social*.py` run the friends layer (`port/linux/game/social_*.c`) whole, not cut into functions: `social.py` compiles a driver (`tests/social_*_check.c`) that includes the sources, `p2p_crypto.c`
and Monocypher, against a network of fake MQTT brokers (`tests/social_fake.h`: loss, repeats, reordering, a limit of publishes a second, a cap on subscriptions, brokers that go down, machines whose wall clocks
are hours out). A negative control copies the sources to a folder of its own, makes one deliberate fault there and builds the driver from that copy (`social.build(driver, mutations=...)`).

    HALO_HARNESS_TARGET=-fsigned-char python -m pytest -q tools/harness/tests/test_social*.py        # the Mac
    docker run --rm -v "$PWD:/src" -e HALO_HARNESS_TARGET="--target=powerpc-linux-gnu -fsigned-char -fuse-ld=lld -no-pie" \
        -e HALO_HARNESS_RUNNER=qemu-ppc opence-ppc-be python3 -m pytest -q tools/harness/tests/test_social*.py   # big-endian PowerPC
    ... -e QEMU_LD_PREFIX=/usr/i686-linux-gnu -e HALO_HARNESS_TARGET="--target=i686-linux-gnu -m32 -fuse-ld=lld -no-pie" -e HALO_HARNESS_RUNNER=qemu-i386    # 32-bit x86

- `SOCIAL_SANITIZE=1` builds with the address and undefined-behaviour sanitisers (`SOCIAL_FUZZ=200000` sets the fuzz's length).
- `HALO_SOCIAL_UPDATE=1` rewrites `tests/social_golden/wire.txt` and `engine.txt` (from a little-endian run only; the big-endian run is held to them).
- `test_social_mixed.py` runs a 32-bit x86 and a big-endian PowerPC build of `social_peer.c` against each other (needs the `opence-ppc-be` image; skipped elsewhere).
- `test_social_units.py` compiles each unit alone, as the game's build does.

## The friends layer as the social screens' backend

`tests/test_screens_social.py` (`screens_social_check.c`) builds `port/linux/game/screens_social_backend.c` with the screens' model (`screens.c`) and the friends layer, on the same fake brokers: a friend added by its typed code, seen in
the FRIENDS tab's model, invited, and the invitation a toast on the other machine; the join and its refusals in plain words (the join's stand-in for internet play is in the driver); the requests, blocked and party tabs and the actions on
their rows; the roster's friend and party flags; the hold of the "online" toasts; and the screens with the layer off. The process has one friends layer, so a case points `social_global_front` at one node or another (`use`).

    HALO_HARNESS_TARGET=-fsigned-char python -m pytest -q tools/harness/tests/test_screens_social.py
