# Wii U probe

A small Wii U application that measures what the console and Cafe OS allow, so that the port is
designed on what they do instead of on what is assumed (plan: `WIIU_PORT_PLAN.md`, phase 0 and
section 6.2). It runs once, writes a text report to the SD card and shows the checks on screen.

It changes nothing on the console: it reads system information, reserves and maps address space
in its own process (and gives it back), allocates and frees memory, and writes two files in
`sd:/wiiu/apps/opence-probe/`.

## Running it

Build (in the `opence-wiiu` container, from the repository around OpenCE):

    tools/wiiu/build_probe.sh gcc dist        # or: clang dist

Copy `port/wiiu/probe/dist/wiiu/apps/opence-probe/` into `wiiu/apps/` on the SD card, start
`opence-probe.wuhb` from the Aroma menu, wait (about half a minute), press HOME. Then take
`sd:/wiiu/apps/opence-probe/report.txt` (and `crash.txt`, if there is one) off the card.

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
  (a fault handler that writes `crash.txt`), `clock.c` and the stages `stage_system`, `stage_cpu`,
  `stage_memory`.
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
