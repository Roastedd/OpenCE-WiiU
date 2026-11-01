# Mac PPC reference: remove redundant render-state tracking

October 31, 2026. Branch `codex/render-objects-mac-20261031`, based on
`de64efdea42b1543bc695c67dbdcf8afbd639859`. This work is host-only. It does not
claim a b30 frame-rate improvement and has not been installed or run on Wii U.

The result is a small, independently written Direct3D state-tracking change,
with unchanged replayed draw inputs. It removes known redundant work; its
whole-replay elapsed-time result is noisy and needs console measurement
before promotion. [Counts and every final timing pair](mac-render-state-evidence.json).

## Problem and change

Models, parts, lights and shadows repeatedly set render and texture-stage
states. The current producer marks every setter call, then
`state_writes_send()` scans those marked words and discards values already
sent to the GX2 consumer. The merged per-draw cache already avoids redundant
commands. This change avoids some of the preceding tracking and scan work.

`render_state_store()` and `texture_state_store()` compare the requested
value with the producer's private **last-sent** copy before marking a word.
They still store every requested game-table value. Comparing only with the
current game table would be wrong: a caller can directly prewrite that table
and then call a setter for the same unsent value. A setter restoring the
last-sent value must also undo any direct prewrite in the game table.

The Simple setter is different: its inline callers normally store the value
after calling it, and it historically only marks the table. It suppresses a
mark only when the argument, the current table word and the last-sent word
all agree. Raw mismatched calls therefore retain the old marking behavior.
Unknown methods still force a complete scan. Present/device resync and
compound ZBias updates retain their existing behavior. Pending marks are
never cleared by a no-op setter.

The last-sent arrays are owned by the producer thread; no new read of mutable
consumer-thread memory or synchronization is introduced. There are no new
allocations, cached resource pointers, altered model palettes, floating-point
operations, LOD changes, pass reordering or dropped draws. Tick code and
protocol 41 are unchanged.

## What the Mac actually supplied

The unchanged MacSoft Halo 2.0 PPC slice has SHA-256
`33c536018095e41f0998c94573227ff05aee04465449b84a24616d59a95001cb`.
A private copy of the existing Ghidra 12.1.4 project was processed, without
executing the target or editing the original project. The input identity,
method selection, decompilation and assembly are retained under
`runs/host-mac-perf-20261031/render/mac/` in the surrounding Wii U workspace.

The derived D3D9 device vtable was located through its surviving RTTI
reference. Some initial standard COM slot assumptions were offset by the
Mac class's additional entries; method attribution was checked against
parameter shapes, state-case values and actual instructions rather than
assuming those initial slot numbers were correct.

- The raster-state setter at `0x0032af50` has a cached-word comparison and an
  early return before its large state-specific GL dispatch. Instructions
  `0x0032af9c`–`0x0032afa4` load and compare integer words, then branch to
  the return path when equal.
- The texture-stage state setter at `0x0032e950` similarly compares a cached
  stage/type word before dispatch. The compare/branch is at `0x0032e9ec`–
  `0x0032e9f4`.
- The vertex-shader binding path at `0x00329b80` avoids rebinding the same
  underlying program identifier. The Wii U already has shader/constant
  caches, so that finding was not treated as a new implementation task.

Ghidra inferred misleading floating-point parameter types in parts of the
large raster-state function; the integer comparison claim above comes from
instructions. These methods support the design principle of rejecting
redundant state work early. No Mac code, GL dispatch or binary implementation
was copied. The Wii U implementation follows its own queued-state protocol,
which differs from the Mac wrapper. The high-level Mac `render_objects`
function itself was not positively identified in this bounded pass.

## Host measurements

The existing asset-free PowerPC replay ran 2,470 frames, producing 5,776 draws
and 10,988 draw/command trace lines. Counts use separate instrumented builds;
elapsed timings use uninstrumented executables, alternated in nine reversed
pairs in the same network-disabled `opence-wiiu` container.

| Work/result | Baseline | Candidate |
| --- | ---: | ---: |
| Render-state marks | 41,914 | 41,706 |
| Texture-state marks | 783 | 556 |
| Marked state-word visits | 355,235 | 354,839 |
| Draw/trace digest | `3599a1f106da2276` | identical |
| Synchronous resource Lock digest | `ec90e29cf5d40ffb` | identical |
| Missed states / targets | 0 / 0 | 0 / 0 |
| Median end-to-end QEMU replay seconds | 0.80260 | 0.79778 |

The deterministic saving is 435 mark operations and 396 sparse word visits
in this mixed replay. The elapsed median differs by about 0.60%; only six of
nine pairs favored the candidate. Paired savings ranged from -3.93% to
+3.31%, with a median of +0.45%. **That is not a stable runtime win or a b30
speedup.** QEMU translation/setup time is included and is not Wii U CPU/GPU
time. A provisional version lacking the raw-Simple guard had a stronger
result; those earlier timing numbers are superseded by this final dataset.

The effect may be larger when actual materials repeatedly set identical
states, but that requires measurement. The provided b30 pass timings locate
cost in object rendering; the flat profile does not attribute it to these
setters. Terminal profile/reporting stalls are excluded from ordinary-frame
interpretation.

The retained `tools/bench_wiiu_state_setters.py` rebuilds baseline/candidate
replays and separately records counts and reversed elapsed pairs. Export the
baseline file on the host, then run the tool in the cached PowerPC toolchain:

```text
git show de64efde:port/wiiu/src/d3d8_gx2.c > <baseline-source.c>
D3D8_REPLAY_TARGET='--target=powerpc-linux-gnu -fsigned-char -msvr4-struct-return' D3D8_REPLAY_RUNNER='qemu-ppc -cpu 750' python3 tools/bench_wiiu_state_setters.py --baseline-source <baseline-source.c> --output <new-output-directory>
```

## Verification

- All nine `tools/test_wiiu_d3d8_replay.py` tests pass on big-endian PowerPC,
  including synchronous and asynchronous schedules and deliberate hazards.
  Draw digest and both recorded resource Lock digests are unchanged.
- `tools/test_wiiu_state_setters.py`: host contract and ASAN/UBSAN pass;
  big-endian PowerPC contract passes, with the host sanitizer explicitly
  skipped there. It runs 20,000 generated prewrite/value cases plus raw
  Simple calls, before/after stores, initial resync, invalid arguments and
  specialized texture setters. Four deliberate faults are rejected.
- The changed Wii U unit compiles and its strict instruction audit reports
  zero forbidden instructions.
- The complete release `--wiiu-lto` build compiles and links with zero missing
  platform stubs. WUHB SHA-256:
  `4f74100e61516b609a8cededbeb143a37e9559c8b13b90233ae791ebd4d773e3`.
  Build logs and ELF identity are retained with the host measurements.
  The worktree's previous configure arguments were restored afterward.
- Shared platform/game sources are untouched; Android and Web code are not
  affected by this Wii U-only change. No console connections were made.

## Larger object-path candidates inspected

A cross-pass relative-node-matrix cache could avoid repeated
`matrix4x3_multiply` work in `render_model()` for the same object in normal
and shadow passes. That is a real potential duplicate, but not a safe
pointer-only cache: interpolated matrices and first-person leg storage can
reuse addresses, model inverse matrices belong to tag lifetime, and queued
transparent groups retain matrix data. Before implementation, count repeated
input snapshots per object/window/pass and prove ownership/invalidation.
No broad frame cache was added without that evidence.

For models below 44 nodes, `rasterizer_set_model_skinning()` already prepares
constants once at model begin, and the downstream serial cache avoids
redundant constant sends. Models with at least 44 nodes use per-part Custom
Edition palettes and can repeat conversion of overlapping nodes; no b30
frequency for this path was established. Static object lighting already has
per-object refresh state, while dynamic lighting updates per scene; caching
again by object or light identifier alone could retain stale inputs.
These are specific next instrumentation targets, not measured wins.

## Console acceptance

Before merging this branch, use the same integrated source/build flags,
shader pack, save, camera and detail settings for baseline/candidate b30
runs. Record object-pass and total render CPU time, state setter/scan counts,
tick frequency, GPU time and tail frames. Compare back-buffer output and
all skipped-draw/error counts, including a first-person weapon, multiple
materials, object shadows, split-screen windows and a later state change.
The branch should be rejected or left unapplied if work reduction does not
translate to useful console timing or if any output changes. No main-branch
merge, push, installation or console qualification was performed here.
