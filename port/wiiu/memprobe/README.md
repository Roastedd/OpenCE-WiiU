# Wii U memory probe

A small Wii U application (not the game) that measures, on each of the three cores, how fast the CPU reads and
writes the memories an app can have: an ordinary MEM2 allocation (the DDR3 the game's 128 MiB window is in),
the eDRAM (MEM1, from the MEM1 base heap, virtual 0xF4000000) and the foreground bucket (0xE0000000). It answers
whether the game's hot data (object pool, AI data, collision BSP, script nodes) would run faster in the eDRAM,
and what the L1/L2/memory latencies are that the core-0 L2 misses are paid at.

It writes only `memprobe-report.txt` (and, if asked, nothing else) to `sd:/wiiu/apps/opence-memprobe/`. It
allocates 16 MiB from each region and gives it back; it maps nothing, uses no fixed address, makes only aligned
accesses, and runs the one instruction that could fault (`dcbz`, which raises an alignment exception on
cache-inhibited memory) under an exception guard that turns a fault into a line of the report. About 3 to 6
minutes.

## How the coordinator runs it

1. The build is `opence-memprobe.wuhb` (and `.rpx`) in this folder's build directory
   (`runs/wiiu/session-20261103/builds/memprobe/`).
2. Start it, either way (nothing else may be running on the console; one driver at a time):
   - `tools/wiiu/wiiu_load.py runs/wiiu/session-20261103/builds/memprobe/opence-memprobe.wuhb` (Wiiload; replaces
     the running app), or
   - put the `.wuhb` in `sd:/wiiu/apps/opence-memprobe/` and start it from the Aroma menu.
3. Leave the console alone. The screen scrolls the report lines. **Do not press HOME until it says
   `== finished ==`**: HOME takes the eDRAM and the foreground bucket away from the app while it is reading them.
   (If it happens anyway the report on the card ends at the last finished row; that is still data, and the next
   run starts again from the top.)
4. When "finished" shows, press HOME and close the app (the usual way). Fetch the report:
   `tools/wiiu/wiiu_ftp.py get /wiiu/apps/opence-memprobe/memprobe-report.txt runs/wiiu/session-20261103/memprobe-report.txt`
   (or take it off the card). The report is overwritten by the next run, so copy it first.
5. Optional second run of the locked-cache stage: create an empty file `lc.txt` in
   `sd:/wiiu/apps/opence-memprobe/` and run again. That stage reads (never writes) the kernel's
   `lockedCacheBase0..2` (from `__KernelGetInfo`) and the user-mode copies of the Wii's locked-cache registers
   (UHID2, UWPAR, UDMAU, UDMAL) under an exception guard. A read that raises a program exception is caught
   and reported, but it is the one step that has not run on a console before: skip it unless nobody needs the
   console afterwards. The default run does not do it.

## What the report contains

All numbers are measured on the console; the file is plain text.

- Header: build id, clocks, the MEM1/MEM2 bounds, the base heaps, where each tested block landed (virtual
  and physical address; physical addresses below 0x02000000 are the eDRAM).
- `[latency]` three tables, one row per region and core, one column per working set (16K ... 16M):
  `ns` (nanoseconds per dependent load), `cyc` (core cycles per load, from the performance counters) and
  `l2m` (L2 data misses per load). Sets of 16K and 32K show the L1 (32 KiB), 512K / 2M the L2, 4M and up the
  memory behind it. A `counter check` line says whether cycles / ns matches the 1.243 GHz clock.
- `[line size]` core 1, strides 32/64/128/256 bytes at 1M and 8M: how much a miss brings in.
- `[bandwidth]` per region and core: sequential 32-bit loads, stores and `dcbz` fills at 16K / 256K / 8M, in
  MB/s. A 16K figure far below the 256K one, or a dcbz line saying "raised an exception", means the memory
  is not write-back cached.
- `[prefetch]` 8 MiB sequential loads without and with `dcbt` N bytes ahead.
- `[line fill]` independent loads, one per 32/64/128 bytes: ns per line when the core can overlap its misses.
- `[contention]` core 1 chasing through 8 MiB alone, while cores 0 and 2 read, and while they store, in the
  same kind of memory, with the other two cores' MB/s.
- `[locked cache]` only with `lc.txt`.

## Reading the result (the decision it supports)

See `reports/research/2026-11-03/mem1-and-streaming.md`, section "What the numbers decide".

## Building

    docker run --rm -v <OpenCE>:/src -w /src/port/wiiu/memprobe opence-wiiu make BUILD_ID=<commit>
    (COMPILER=gcc for devkitPPC's GCC; the measuring loops are written so that both give the same code)

Not run on a console yet when this was written: only built, linked against wut's imports and disassembled.
