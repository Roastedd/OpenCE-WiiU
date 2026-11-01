# BSP vector traversal: preserve the result and remove tail-call overhead

October 31, 2026. Branch `codex/b30-ticks-mac-20261031`, based on
`de64efdea42b1543bc695c67dbdcf8afbd639859`. This work is host-side only;
no console connection, card write, push or main-branch merge was performed.
[Identities and detailed results](mac-ppc-ticks-evidence.json).

## Accepted change and what it saves

The last far-child descent and the single-child descent in
`collision_bsp_test_vector_recursive` now return the recursive boolean
directly. The earlier near-child call remains recursive because the caller
must inspect its result before proceeding. The old code tested the final
recursive result and converted it back into `TRUE` or `FALSE`.

All original return paths produce those canonical values. Returning the
final child's result therefore preserves the raw return byte, not merely
its truthiness. The function has no cleanup, callback or state update after
those tail calls. Plane arithmetic, argument values, visit order, logical
depth increments, node budget, malformed-map handling and leaf/result
updates are unchanged. No floating-point expression or API was replaced.

The equivalence argument is by traversal depth: guard/leaf cases are
unchanged; the first side of a split and its early exits are unchanged;
each modified last descent receives the same arguments and performs the
same state updates. Its recursively canonical return is exactly what the
old caller's final normalization produced. The existing depth/node budget
bounds apply to both versions, including malformed cycles. This argument
concerns defined execution of this function; it does not claim that a
finite formatted game log hashes every byte of the simulation.

The current Wii U one-module LTO build confirms the compiler benefit:

| Property | `de64efde` baseline | Candidate |
| --- | ---: | ---: |
| Static recursive call sites in this function | 2 | 1 |
| Function code bytes | 2,696 (`0xa88`) | 2,656 (`0xa60`) |
| Per-invocation stack frame | 176 bytes | 176 bytes |

The tail edge now reuses that frame rather than allocating another one.
This is a control-flow optimization; the bounds and depth guards remain.
The actual `game_lto.o` passes `tools/wiiu_check_objects.py` with zero
forbidden instructions. There are no paired singles or fused arithmetic
introduced into this tick path, and protocol 41 is unchanged.

## Measured host result

Paired source-extracted benchmarks alternate the frozen reference and
candidate three times for each shape. The final PowerPC 750 QEMU run used
150,000 straight-path calls or 50,000 balanced-path calls per sample.

| Traversal shape | Depths | Median elapsed reduction |
| --- | --- | --- |
| Single-sided descent | 1, 2, 4, 8, 16, 32, 64 | 5.1–8.7% |
| Balanced near/far crossings | 1, 3, 5, 7 | 7.5–9.8% |

An earlier straight-path run measured 7.7–9.8%. Native ARM64 measurements
also improve the straight paths, while the deepest balanced case is within
timing noise. These are local kernel elapsed times under an emulator/host
compiler, not Wii U cycles, whole-tick savings or an FPS result. The fixture
uses a recorded leaf callback instead of rendering or actual geometry leaf
tests; its purpose is to isolate the changed recursion overhead.

The recorded b30 profile is flat and contains substantial profiling/logging
stalls. The unprofiled sweep's roughly 14.5 ms per tick does not establish
this function's share. No percentage of the full tick or prediction of b30
FPS follows from the microbenchmark.

## What the Mac reference established

The unchanged MacSoft Halo 2.0 PPC slice has SHA-256
`33c536018095e41f0998c94573227ff05aee04465449b84a24616d59a95001cb`.
A private copy of its existing Ghidra project was inspected read-only with
Ghidra 12.1.4. The game was not executed.

- `0x00244800`, 112 bytes, matches the BSP2D point walker: 20-byte node
  stride, high-bit leaf encoding, two-dimensional plane test and child loop.
- `0x00244ff0`, 1,732 bytes, matches the BSP vector walker: 12-byte nodes,
  16-byte planes, near/far recursion, early result-t termination, leaf
  contents/flag handling, its call to the BSP2D walker and output fields.

The Mac vector walker also has two recursive call sites and normalizes its
last recursive boolean. It does not supply a pre-existing tail-call fix.
Its floating-point accumulation order and fused instructions differ, and
its direct reads do not supply this port's malformed-map safeguards. Those
aspects were not adopted. The accepted C change is independently written
from the available engine's return-value contract. No Mac code or
commercial pseudocode is included in the branch.

A node-storage-validation hoist inspired by the Mac's direct BSP2D loop was
also tested. It passed 60,580 differential cases, but showed no useful
PowerPC improvement and short-depth regressions on ARM64. It was removed;
the negative patch and measurements remain in the private evidence folder.
Actor-perception scheduling, object-update ordering and matrix arithmetic
were left unchanged: no measured safe algorithmic replacement was
established for those paths in this pass. Reordering perception or adopting
Mac fused matrix math would not satisfy the simulation contract.

## Exact local validation

`tools/test_collision_tail.py` extracts the active function, checked tag
getter, guards and plane/dot helpers from the real sources. It compares the
active function with the version-pinned reference in
`tools/harness/tests/collision_tail_reference.inc`.

The final suite passes **160,532 cases** on ARM64 and big-endian PowerPC,
including sanitizer execution on the host. It compares raw returns,
complete initialized result bytes, mutable context fields, errors/fallbacks
and ordered traversal/leaf-call events with raw floating-point argument
bits. Cases include random trees and flags, both-side crossings, hits,
early exits, cycles, invalid indices, depth limits, NaNs, infinities,
subnormals and signed zero. The leaf callback is mocked; actual leaf
geometry code is unchanged by the production patch. Wrong-child and
wrong-depth deliberate controls both fail. The pytest harness wrapper runs
the regression and controls in the normal asset-free test collection.

Both full baseline architectures and both candidates were built from the
same base. Each independent b30 host run used `tickbot:7`, seed 12345,
fixed-step mode, `HALO_NETWORK_TEST_LOG=1`, separate saves/config/logs,
read-only maps and a Docker network namespace with no external interface.
These are independent deterministic runs, not connected cross-play peers.

With interpolation disabled consistently, the unmodified
`tools/ppc-be/compare_network_tests.py` reports:

| Pair | Complete common consecutive ticks | Result |
| --- | ---: | --- |
| Baseline PPC / baseline x86 | 1–1304 | All logged fields equal; exit 0 |
| Candidate PPC / baseline PPC | 1–1304 | All logged fields equal; exit 0 |
| Candidate PPC / baseline x86 | 1–1321 | All logged fields equal; exit 0 |
| Candidate PPC / candidate x86 | 1–1321 | All logged fields equal; exit 0 |

The faster runs have additional unpaired ticks. The formatted log covers
the fields the tool compares, not raw bytes of the entire game heap.
Exact local kernel differential tests and the unchanged arithmetic/order
support the stronger bitwise contract for the changed function.

Longer x86 comparisons are retained as failures: 2,396 player states agree,
but 153 camera-only rest fields differ between candidate and baseline.
Repeating the *same unchanged baseline ELF* produces 160 camera-only
differences over the same common range, beginning at tick 1380; all other
logged rest fields agree. Disabling model interpolation did not remove
this existing camera nondeterminism. No fields were dropped from the
official comparator and these longer comparisons are not labeled passes.

The Wii U LTO build and strict instruction audit pass. The Android guest
ELF also compiles and links using the installed NDK's actual builtin
archive, existing Linux LLVM utilities and cached public dependencies in a
temporary adapted graph. Full Android host libraries/APK were not tested.
The ordinary container lacks an NDK and Emscripten; the original Android
and web target attempts are recorded as unavailable. The worktree's prior
build graph was restored byte-for-byte after these checks.

Commands, logs, binaries, full comparator failures, Mac dossiers and
rejected work remain private under
`runs/host-mac-perf-20261031/ticks/`. The committed JSON contains identities
and results without game data or commercial pseudocode.

## Console acceptance still needed

Compare the baseline and this branch using the same current `--wiiu-lto`
configuration, pack, b30 save, camera and scripted input. Measure ordinary
tick spans and tail-frame distribution without the verbose sample profiler
printing during the timed interval. Verify representative movement,
perception, collision, weapons and vehicle behavior, then repeat a30 as a
regression case. Actual vector-call/depth frequency is needed to relate the
kernel improvement to the map's overall cost. No console run should be
started until the console coordinator grants that stage.
