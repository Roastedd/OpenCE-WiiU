# Owned audio packet storage: bounded reuse candidate

October 31, 2026. Branch `codex/audio-churn-mac-20261031`, based on
`de64efdea42b1543bc695c67dbdcf8afbd639859`. This work is host-only. No Wii U
connection, input, launch, FTP transfer, card change, push or merge was made.

**Result:** the candidate eliminates repeated heap calls in fitting warm
packet cohorts and reduces them under the capped PCM workload. It does not
establish a whole-mixer, b30 frame-time or audio-deadline improvement. Some
host allocation-pressure and oversized cases are slower, and console A/B
qualification is required before adopting it.

## Reference and current work

The MacSoft Halo 2.0 PPC study identifies a PCM storage pool in function
`0x0034b8e0` and a small playback callback at `0x003496f0`. I inspected the
retained Ghidra dossiers and the earlier `audio-platform.md` report. The Mac
uses Sound Manager channels, not this port's software mixer and asynchronous
Xbox ADPCM decoder. The useful principle is retained owned storage; no Mac
code, size-selection algorithm or 256-entry capacity was copied.

At the requested base, `decoder_post` still allocates and copies compressed
input, `decode_adpcm`/`decode_pcm` allocate PCM, and their distinct ownership
endpoints free those allocations. The b30 log reports queue peaks of 2–33
jobs and 0–64 mixes waiting for a packet in its reporting windows. It does
not measure allocation cost or show that allocation caused those waits.

## Implementation and ownership

`wiiu_packet_pool.c` independently implements nine size classes, 128 through
32768 bytes, with constant-size free-list operations. The Wii U-only adapter
in `dsound_sdl.c` uses it for packet copies and decoded PCM. Other platforms
continue to call ordinary `malloc`/`free`. A normal engine packet is capped
at 8192 PCM bytes multiplied by its rate/stereo factors, or 32768 bytes;
larger API requests retain exact-size heap allocation plus the private header.

The cache retains at most **1 MiB of idle payload and 128 headers**. On the
32-bit Wii U those headers add at most 1024 bytes, excluding libc allocator
overhead. This is a cache bound, not a bound on all live sound memory. Live
small allocations round up to a size class and retain their headers; this
can increase active memory compared with the old exact-size request.

The short critical sections use a **blocking pthread mutex**, not a wait-free
allocator. No libc allocation/free, stream access, decoder-lock acquisition,
mixer-lock acquisition or completion callback runs while that mutex is held.
The fixture tracks per-thread lock depth and rejects a deliberate free-under-
lock fault. The cache never holds the pool lock while awaiting another audio
lock, so its existing mixer→pool call order has no reverse pool→mixer edge.

Ownership and completion order are unchanged:

- Submission still copies the game's packet before returning; queued jobs
  never borrow the sound cache's bytes.
- The worker owns its input copy through decoding and releases it afterward.
- Decoded PCM belongs to the packet until completion/flush under the mixer
  lock. Rejected stale publications release the unused PCM.
- Stream lookup and the packet serial guard remain intact. Tests force an
  old and a new stream to share the same address and reject the old serial.
- Flush, release, queue exhaustion and caller-side decoding retain their
  previous status/callback behavior. PCM decoding, ADPCM math, resampling,
  gains, reverb, voice chat, packet sizes and audio scheduling were not changed.

If a real heap request fails, the allocator detaches at most the cached 128
idle buffers under the lock, frees them outside it, then retries once if any
were reclaimed. It never trims borrowed buffers. Overflow rejection makes
no heap call and does not trim. A second heap failure returns NULL to the
existing fallback/failure path. Counters distinguish actual heap attempts,
failed attempts, retries, reclaimed blocks and arithmetic rejections.

Explicit trim exists for controlled host tests and pressure reclamation; no
new shutdown/HOME trim was wired. The ordinary cache is deliberately bounded
process-lifetime storage. Sanitized builds poison returned payloads and
unpoison them on checkout, so caching does not hide late readers from ASAN.

When `audio_profile.txt` enables existing decoder diagnostics, the same
report interval also logs allocation requests, cache hits, heap attempts,
failures/frees, retained payload/blocks and exceptional reclamation. These
are allocation counters, not CPU utilization or mixer timing.

## Measured host results

The allocation-only fixture times ordinary **uncounted** libc calls, then
collects heap counts in a separate identical operation pass. It disables
fixture lock-depth hooks for timing and keeps public pool calls `noinline`,
matching the real separate platform translation-unit boundary. Five paired
M4 repeats alternate baseline/candidate order; every repeat is retained.
There is no decoding or mixing in this timer.

| Payload / simultaneous cohort | Baseline median | Pool median | Pool / baseline |
| --- | ---: | ---: | ---: |
| 2304 bytes / 64 | 23.012 ms | 11.549 ms | 0.502 |
| 8192 bytes / 64 | 23.590 ms | 12.419 ms | 0.526 |
| 8192 bytes / 256, cache pressure | 99.558 ms | 109.011 ms | **1.095** |
| 32768 bytes / 32 | 13.108 ms | 11.468 ms | 0.875 |
| 32768 bytes / 64, cache pressure | 31.916 ms | 74.637 ms | **2.339** |
| 65537 bytes / 16, oversized bypass | 24.909 ms | 32.689 ms | **1.312** |

Each row runs 20000 allocation/free waves. A fitting 64-buffer cohort has
1,280,000 allocation requests: after warmup its heap calls fall to zero.
Pressure/oversized regressions are retained, not discarded. Darwin allocator
size classes, private-header overhead and cache misses can affect these
figures; they are not Wii U newlib timings or a performance guarantee.

A separate fixture executes the actual post, copy, decode, publication and
completion functions with deterministic single-thread queue draining, 64
voices and 6400 packets after warmup:

| Workload | Baseline malloc / free calls | Candidate malloc / free calls | Cache hits |
| --- | ---: | ---: | ---: |
| Mono ADPCM, 2304-byte input / 8192-byte PCM | 12800 / 12800 | **0 / 0** | 12800 |
| Stereo PCM, maximum 32768-byte packets | 12800 / 12800 | **3300 / 3300** | 9500 |

Thus the PCM capped-cache cohort removes about **74.2%** of heap calls; it is
not allocation-free. The ADPCM retained payload is 786432 bytes; PCM reaches
the 1048576-byte cap. Baseline and candidate decoded-byte checksums match;
additional tests compare their full PCM byte streams directly for both formats.

That instrumented full-packet fixture includes decoding, locks, allocation
counters and output checksums. Its host timing is approximately unchanged
or slightly slower (the retained results do not establish a pipeline win).
It is neither an isolated allocation timer nor a live audio deadline test.
The proven savings are heap operations and fitting-cohort allocation cost.

Big-endian `qemu-ppc -cpu 750` repeats also measure allocator/locking behavior,
but use Linux libc and emulation. Their exact-source results are retained
separately and must not be interpreted as Wii U cycle counts or FPS.

## Verification

- Pool tests cover live exclusivity, zero/oversized/overflow requests,
  cached-memory bounds, allocation failure, reclaimed-memory-dependent retry,
  a second failed attempt, borrowed storage surviving trim/reclamation,
  concurrent checkout/return/trim and no heap operation under the pool lock.
  Three deliberate source faults and an ASAN late-reader control are detected.
- Lifetime tests execute the actual queue, decoder worker and completion C.
  They cover copy failure/full-queue synchronous fallback, flush/release during
  decode, stale publication, forced stream-address reuse, and full decoded
  PCM bytes matching an ordinary-heap baseline. Host ASAN/UBSAN passes.
  The combined pool/lifetime suite passes all 13 host tests and all 10
  applicable PowerPC tests, with three host sanitizer checks skipped there.
- The native-qualified ADPCM table/math remains unchanged. Its test allows
  only the allocation-call adapter difference; all complete-packet/transition
  cases and the existing fault controls pass on host and big-endian PowerPC.
- Eleven existing big-endian pending-packet and mixer rendered-byte checks
  pass. The x86 cancellation suite passes all twelve cases/controls, and all
  sixteen existing voice-chat cases pass with the real Opus codec and platform
  stand-ins. Voice-chat source was not modified.
- `--wiiu-lto` builds and links with strict instruction gates and zero missing
  stubs. No paired-single or fused operation was added to tick or mixer math.
- An actual host Android build with the existing NDK 28.2 and cached
  dependencies passes all 1787 steps, including guest and host linking.
  The initial container attempt had no Android target because it lacked an
  NDK. Web was attempted and unavailable because Emscripten was not found;
  it is not reported as a pass. Build graphs were restored byte-for-byte.

Reproduction commands include:

```text
python3 -m unittest discover -s tools -p 'test_wiiu_packet_*.py' -v
python3 tools/test_wiiu_adpcm.py -v
```

The parent workspace's `runs/host-mac-perf-20261031/audio/` retains the full
measurement rows, exact commands, logs, generated fixtures, source hashes
and build identities. The compact evidence summary is adjacent to this report.

## Console acceptance still required

Compare `de64efde` and this branch with identical LTO/release flags, save,
camera, game state and audio settings. On a heavy b30 fight, record cache
hits/misses/retries, caller decode fallbacks, decoder wait distributions,
mixer-lock/handoff time, worst mix duration, ring lead/underruns and frame
time. Include large stereo music and a high-voice-count pressure case, HOME
and resume, stream cancellation, and voice chat while effects play. Check
actual live/retained memory pressure without deliberately exhausting the
user's active game. Listen for late sounds, crackle and missing completions.

The extra short mutex, rounded active capacities and slower miss-heavy host
cases make these checks material. No whole-game speedup, b30 bottleneck fix,
listening acceptance or safe HOME behavior is claimed from host results.
