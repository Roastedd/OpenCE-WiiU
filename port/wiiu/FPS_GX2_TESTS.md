# Console tests for the Direct3D layer's per-draw work (`wiiu-fps-gx2`)

For the session that holds the console. Nothing here has run on a console.

## What changed

Five commits on top of d8651f2 make the game thread's per-draw work in
`port/wiiu/src/d3d8_gx2.c` depend on what changed since the last draw, not
on the whole state. The commands the GX2 thread is given are unchanged. The
Direct3D replay (`tools/test_wiiu_d3d8_replay.py`) gives the same digest for
d8651f2 and for every commit. That digest covers what the GPU is given for
each draw, and the resources' Lock fields. So the GPU's work and the GX2
thread's work should not change. Only "of which N in the Direct3D layer's
draws" (4.6 ms a frame at a30 on 2026-10-30) and the frame work should fall.

| Build (`[T]/runs/wiiu/fps-gx2-20261031/builds/`) | Commit | Change | Expected, game thread a frame |
| --- | --- | --- | --- |
| `halo-d8651f2c.wuhb` | d8651f2 | base, byte-identical to `installed-d8651f2.wuhb` (sha256 d9d04cca...) | - |
| `halo-849ee1fb.wuhb` | 849ee1f | draw timers in ticks: no `__floatdidf` or division per draw | under 0.1 ms |
| `halo-1569b0ae.wuhb` | 1569b0a | only the render and texture stage states marked by their setters are compared | 0.3 to 0.6 ms |
| `halo-4ad48f3d.wuhb` | 4ad48f3 | targets, Lock stamps and the vertex format looked at again only after a change | 0.3 to 0.8 ms |
| `halo-413dd307.wuhb` | 413dd30 | facts kept from draw to draw; a stage's texture lookup memoised | 0.5 to 1 ms |
| `halo-16368c59.wuhb` | 16368c5 (tip) | draw_write's bookkeeping | under 0.1 ms |

The steps together are expected to save 1.2 to 2.5 ms. That may not be
enough alone for the 15.5 ms target; `wiiu-fps-cpu` is meant to supply the
rest. To rebuild a step:
`OPENCE=<worktree at the commit> tools/wiiu/build_game.sh` gives
`build/linux-wiiu/halo.wuhb`. The builds are reproducible.

## Set-up

- Only one session drives the console. Check that no other session does.
- Card settings as in the a30 baseline of 2026-10-30
  (`runs/wiiu/session-20261030/ab/a30c/base/original-*.txt`): `gx2_thread.txt`
  7, `overlay.txt` 1, `profile.txt` 0, `audio_profile.txt` 0, `game_core.txt` 1,
  `mem1.txt` and `hiz.txt` default, `render_height.txt` 0, `gpu_test.txt` 0,
  `exit_after.txt` 0.
- `config.toml`: `runs/wiiu/session-20261030/ab/a30c/config-bench.toml`. This is
  the user's config with `wiiu_gyro_aim = "off"`, `debug.gpu_stats = true` and
  `render_lines = 720`. Keep a copy of the card's own config first.
- At the end, put the user's `config.toml` back and set `gx2_thread.txt` to 0
  (the safe path). Check both by reading them back.

## The A/B

One trial is:

    python3 tools/wiiu/same_save_benchmark.py --host 192.168.1.42 --console-closed \
        --build runs/wiiu/fps-gx2-20261031/builds/<build>.wuhb \
        --save runs/wiiu/fetch-cache-20261029/recovered-baseline/fixed-save.bin \
        --out runs/wiiu/fps-gx2-20261031/<n>-<A|B>

Here A is `halo-d8651f2c.wuhb` and B is `halo-16368c59.wuhb`. Run them in the
order A B B A A B B A. Review each `start.png` before you compare the numbers:
the same checkpoint, camera and weapon. Summarise each trial with

    python3 scratch/render-analysis/fps_trial_summary.py runs/wiiu/fps-gx2-20261031/<n>-<A|B> --out <n>-<A|B>.json

Then take, for each phase, the mean over the four A runs and over the four B
runs.

Pass (B, standing phase):

- `present_rate_fps` at least 59.5;
- `work_ms_per_frame` at most 15.5;
- frames of two or more refreshes (`gx2_refresh_buckets[1:]`) under 2% of all
  frames;
- the GX2 thread's "per frame over N frames: X ms in the stream" (game.log,
  inside the phase) at most 12 ms. It should be close to A's (about 6.1).

Report these too, A against B: `draw_ms_per_frame` (this work's own measure;
expected 4.6 down to about 2.5 to 3.5), `ticks_ms_per_frame` (it should not
move: no tick code changed), the firing phase's numbers, and over17 and over34.

If B does not pass but `draw_ms_per_frame` fell as expected, the remaining
time is the game's own (the `wiiu-fps-cpu` work). If a step must be found,
run that step's build against A in the same order.

## Pictures

- Compare `start.png` and `after.png` of A and B. Two A runs differ by
  animation and timing, so use an A-against-A pair as the noise floor. B
  should differ from A by no more than that.
- Grep each B `game.log` for `found changed with no setter` and
  `found changed with no SetRenderTarget`. Neither line may appear. Those are
  the frame-end checks that a state or a bound surface changed without the
  layer having been told.

## The GPU test

Run one trial of A and one of B with `gpu_test.txt` set to `1 150`. Compare
the "GPU test:" lines: the frame, no pixels, and no blending, each "handed
over in" and "done in". The commands are the same, so they should agree
within run-to-run noise. Set `gpu_test.txt` back to 0.
