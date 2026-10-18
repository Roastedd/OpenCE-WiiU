# Wii U probe

A small Wii U application that measures what the console and Cafe OS allow, so that the port is
designed on what they do instead of on what is assumed (plan: `WIIU_PORT_PLAN.md`, phase 0 and
section 6.2). It runs once, writes a text report to the SD card and shows the checks on screen.

It writes nothing to the console's own storage: it reads system information, reserves address space
and maps the physical memory the system reports as available in its own process (and gives both
back), allocates and frees memory, and writes `report-<compiler>.txt` (and `crash-<compiler>.txt` after a fault) in
`sd:/wiiu/apps/opence-probe/`. The mapping is the one step that is not an ordinary application's;
a refusal is an answer, a crash is a finding, and a hang is ended with the power button.

The one real risk is in that step: wut's documentation does not say whether the physical range
`OSGetAvailPhysAddrRange` returns is the application's alone. The probe maps from it, and writes a word every 4 KiB
into what it mapped. If something else used that memory, the write could disturb it; the effect would be in RAM, which a
restart clears, and the probe touches no storage but the two files. Run it from a fresh start with nothing else open and
nothing being saved, and stop at the first crash.

## Running it

Build (in the `opence-wiiu` container, from the repository around OpenCE):

    tools/wiiu/build_probe.sh gcc dist        # builds both compilers' probes

Copy the folder `port/wiiu/probe/dist/wiiu` into the SD card's root (it merges into `wiiu/apps/opence-probe/`).
Start `opence-probe-clang.wuhb` from the Aroma menu (clang is what the port is built with, so its answers are the
ones that count), wait for "finished" on the screen (the time is not known yet: the stages are small, but none has
run on a console), press HOME. Then `opence-probe-gcc.wuhb`, to see whether the two compilers agree. Take
`report-clang.txt` and `report-gcc.txt` (and `crash-clang.txt` or `crash-gcc.txt`, if there are any) off the card,
from `sd:/wiiu/apps/opence-probe/`.

If the console stops in the middle of it, hold the power button. The report is written after each
stage and before each mapping attempt, so it ends at the step that did not return; that is a
finding, send it.

## What it answers

| Stage | Question | Decides |
| --- | --- | --- |
| `system` | Which console, clocks, cores; is the card there? | nothing; context for the rest |
| `abi` | Is the compiled ABI the one the port assumes (big-endian, ILP32, 16-bit `wchar_t`, signed `char`, 8-byte `double` alignment, MSB-first bit fields)? | the compiler flags in `port/wiiu/abi.mk` |
| `numeric` | FPSCR (rounding, denormals, traps), no fused multiply-add, denormals kept, what out-of-range float to int conversions give | whether the netcode's floating point can match x86 bit for bit |
| `maths` | The game's own maths (musl, `halo_sin` and the rest) gives the checksums every other machine gives | cross-play determinism |
| `cpu` | Per core: memory copy, fill, read and byte-swap throughput (cache-resident and not), dependent multiply-add latency | which core runs what; what swapping at load would cost |
| `memory` | The address space page by page; the heaps; whether `0x80000000` can be reserved and mapped; where a 128 MiB range goes if not; mapped memory's speed against the heap's | plan 6.2: map the window, or rebase the tag pointers |

| `unaligned` | Which misaligned loads and stores work, at what cost (a 750 does some in hardware and some by an alignment exception) | whether the engine's odd-offset reads of animation data need the port layer's help (last stage: it may end the run) |

Reading the `memory` stage:

- `map at 0x80000000` all `PASS`, including `the range is at 80000000`: the Xbox game state
  window can be had as it is (plan 6.2 option 1).
- It reserves elsewhere, or `map anywhere` works and the exact address does not: rebase (option 2). The
  address the system gives (`map anywhere`) says whether it is the same on every run; if it is not,
  the rebase has to be done at load.
- Neither maps: the game state lives in a heap block (`128 MiB from the heap`) and every tag
  pointer is rebased to wherever that is.
- `address space restored` must pass: everything the probe took was given back.

## What it does not do

The GPU questions of phase 0 (compile a GLSL ES shader with the CafeGLSL compiler and draw a
triangle; BC1/BC3 texture byte order; little-endian vertex streams and `U16_LE` indices) need GX2
and the shader compiler. They are a second probe, not this one.

## Layout

- `src/` is portable C, the same on the console and on a desktop: `report` (the report text, fixed
  buffers), `probe_logic` (hashes, statistics, address-range runs), `bench`, `maths_check` (the
  checksums), and the stages `stage_abi`, `stage_numeric`, `stage_maths`.
- `src/wiiu/` is the console's: `main.c` (ProcUI, screen, the order of stages), `card.c`, `crash.c`
  (a fault handler that writes `crash-<compiler>.txt`), `clock.c` and the stages `stage_system`, `stage_cpu`,
  `stage_memory`, `stage_unaligned`.
- `tests/` runs the portable code on known answers.

## Checking it

    tools/wiiu/probe_tests.sh                 # host tests: 64-bit ARM, 32-bit x86, 32-bit big-endian PowerPC
    tools/wiiu/build_probe.sh gcc             # -Wall -Wextra -Werror; also: clang
    tools/wiiu/build_probe.sh gcc analyze     # clang's static analyzer
    tools/wiiu/check_objects.py ...           # no fused multiply-adds, fsqrt or paired singles in the objects

`probe_tests.sh` also prints the game's maths checksums on the three machines and requires them
to be identical; those are the numbers the console is compared with
(`src/maths_check.c`, `maths_check_expected_table`).

None of this has run on a console yet. The stages that call Cafe OS (`system`, `cpu`, `memory` and
`main.c`) are checked to build and to link against wut's imports (`readrpl -i`), not to behave.
