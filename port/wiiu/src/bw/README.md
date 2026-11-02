# Broadway kernels

Paired-single assembly for the Espresso (a Broadway descendant), for six hot pieces of the game. They are adapted from the Broadway
assembly bundle the owner supplied on 2026-11-01 (`broadway-assembly-20261101`, the working Wii port's code, CC0; the licence text is
`LICENSES/CC0-1.0.txt`). The bundle's `donor-reference-only` files are not used and not linked. Each file keeps the bundle's attribution
in its header comment and says what was changed.

| file | replaces (in `source/`) | from the bundle's |
| --- | --- | --- |
| `halo_bw_matrix.S` | `matrix4x3_multiply` (`math/matrix_math.c`) | `wii_broadway_matrix.S` |
| `halo_bw_sphere.S` | `fast_vector_intersects_sphere` (`math/real_math.c`) | `wii_collision_sphere.S` |
| `halo_bw_bsp.S` | the straight runs of `collision_bsp_test_vector_recursive` and `bsp3d_test_sphere_recursive` (`physics/collision_bsp.c`) | `wii_collision_bsp.S` |
| `halo_bw_reference.S` | the search in `reference_list_remove` (`structures/cluster_partitions.c`) | `wii_reference_find.S` |
| `halo_bw_quaternion.S` | `quaternion_decompress_8byte` (`models/model_animations.c`) | `wii_broadway_quaternion.S` (the load) |
| `halo_bw_probe.S` | nothing: the start-up's leaves (a paired add, a read of GQR0 to GQR7, the write of GQR5 that `halo/broadway.txt` 3 asks for) | written for this port |

Not taken from the bundle, and why:

- `convex_hull2d_area` (`wii_broadway_area.S`): the game calls it only when a map is built (`structures/leaf_map.c`), never in play;
  the kernel is scalar and uses fused multiply-adds, so it would be wrong and gain nothing.
- `quaternions_interpolate_and_normalize` (the bundle's blend): it normalises with `frsqrte` and Newton steps in double precision,
  which does not give the float the game's `1 / sqrtf` gives (`wiiu_sqrtf.c` is correctly rounded), and its interpolation is
  scalar work the compiler does as well. It stays C.
- the root, pose, fog and `ps_guMtxConcat` files: renderer support for the Wii's GX, not simulation or collision.

Rules every kernel here follows (`tools/wiiu_check_objects.py --bw-kernel-object` checks the object, instruction by instruction):

- the same bits as the C it replaces, for every input: no fused multiply-add (`ps_madd*`, `fmadds`, ...), no estimate (`frsqrte`,
  `fres`), every multiply and add separate and in the C expression's order, every branch the C comparison's own (NaNs included);
- a kernel that meets what it does not do hands it to the C and has taken nothing from it;
- volatile EABI registers only, no stack, no calls, no writes to control registers.

They are off until the card's `halo/broadway.txt` says 1 and each has passed its self-test on the console
(`port/wiiu/src/wiiu_bw_kernels.c`). Host proof: `port/wiiu/tests/bw_tests.sh`.

The kernels are leaves, so the start-up can run them under an exception guard: a program, alignment, data-access or floating-point
exception at an address inside kernel code (a core that does not run paired singles, a misaligned quantized load) is caught, counted and
said in the log, and the kernel stays off, where it would have been a crash. The guard is on only while the start-up runs.

## Why each kernel gives the C's bits

The ticks must come out the same on a Wii U and on a PC. What a kernel may differ in is only how many operations it does at once, never
which operations or in what order.

- `matrix4x3_multiply`: every element is `(b0*a + b1*a') + b2*a''` with the three products rounded to single precision one at a time and
  added left to right, and in the position row `value * scale + position`, as the C writes it; the pair lanes are two columns of the same
  expression. The bundle's `ps_madds0` and `fmadds` fuse the multiply and the add into one rounding and so gave other bits than the C.
  The kernel reads all of `a` before it writes, and each row of `b` before it writes that row, so `out` may be `a` or `b`.
- `fast_vector_intersects_sphere`: the whole function, squares and sums in the C's order, and each branch the C comparison's own. A NaN
  makes `b >= 0` and `disc <= 0` false in the C; the bundle's `bge` and `ble` after `fcmpu` take their branch for an unordered compare, which
  answers "miss" where the C goes on (a squared length that overflowed to infinity times a zero gives a NaN `disc`, and the C then answers
  "hit" from `-a - b`). Here those two tests are `cror` of the greater/less bit with the equal bit, which an unordered compare does not set.
- the BSP walks: at a node the kernel does what the C does when the test passes to one side (the same dot products in `dot_product3d`'s
  order, `distance0 = dot*t0 + distance`, the same comparisons), and for anything else it stops before the node, having changed nothing, so
  the C does that node. It stops at: a leaf or an index out of range, depth 128, a spent node budget (the C's `collision_bsp3d_enter_node`
  checks, in the C's order, with nothing taken when one fails), a plane designator out of range, a crossing, and any distance that is a
  NaN (the C's own behaviour with a NaN is to go to `children[0]`; stopping is always safe). The state it carries is the budget and the depth.
- the reference search: integer work. Each reference on the list is checked as `datum_get` checks it (the index, the identifier rules and the
  `identifier_zero_invalid` assert), and anything that `datum_get` would refuse returns NULL, so that the C runs and reports it.
- `quaternion_decompress_8byte`: four signed 16-bit samples converted exactly by the quantized load (GQR5 = type 7, scale 0), each times
  `1.f / 32767` (`0x38000100`), rounded once. If the thread's GQR5 is anything else the kernel returns 0 and writes nothing.

## How they are proven

1. `port/wiiu/tests/bw_tests.sh`: the scalar model of each kernel (`port/linux/include/halo_bw_model.h`, operation for operation) against the
   C (`wiiu_bw_reference.h`), millions of cases in six input classes (normal, ties, wide, denormal, inf, NaN), every output bit compared, on
   the container's CPU, on 32-bit x86 and on 32-bit big-endian PowerPC. `tools/test_bw_reference_sync.py` keeps the reference equal to the
   game's code.
2. `port/wiiu/tests/bw_asm_sim_tests.sh`: the real machine code (as devkitPPC assembles it), run by a small instruction simulator
   (`gekko_sim.h`) through the same cases, to prove the assembly is what the model says; it also checks that only volatile registers are
   written and that no kernel relies on the second half of a register a scalar instruction wrote.
3. `port/wiiu/tests/bw_tick_run.sh` and `bw_tick_compare.py`: the b30 co-op host run (`tickbot:7`, seed 12345, a tick a frame) under qemu-ppc
   with the models in the kernels' places, the same binary with them off, and a build of the branch's base without any of this, compared
   tick by tick on everything the ticks decide (the players, items, score, hits and drift; not the network's counts or the local camera,
   which differ between two runs of one build). The run also says at its end how often each model was asked and answered.
4. On the console, at start (below): the same cases through the assembly itself, which finds what only the hardware does (subnormals and NaN
   payloads in paired operations).

## On the console

`halo/broadway.txt` on the card: `0` off (the default; nothing changes, no paired-single instruction runs), `1` the self-test runs and the
kernels that pass are used, `2` the self-test and the timing run and every kernel stays off, `3` as `1`, and the game thread's GQR5 is first
set to the signed 16-bit load with `mtspr 901` if it is not already (the quaternion kernel needs it; this is the one write of a special
register in the kernels, whether a user program may do it is not known, and a run that ends at that log line says no: use `1`).
The log then has, in order:

    Broadway kernels: halo/broadway.txt 1; self-test on core N, FPSCR 0000xxxx; the first paired-single instruction is next
    Broadway kernels: GQR0-7 ...; GQR0 is ...; GQR5 is ...
    Broadway reference: the game's own ... equal the reference
    Broadway kernel matrix4x3_multiply: every bit equals the C (N cases: normal ok, ties ok, wide ok, denormal ok, inf ok, nan ok)
    ... (one line for each kernel; a class that differs says so, with the first case)
    Broadway timing matrix4x3_multiply: C x ns a call, kernel y ns
    Broadway kernels: in use
    Broadway kernels: matrix on, ray-sphere on, ...

and every 150 ticks, next to the tick parts lines:

    bw calls a tick, asked/answered by a kernel (broadway.txt 1, 63 in use): matrix 41.2/41.2, ray-sphere ..., bsp-ray 9.1/6.0 (21.3 nodes), ...

To measure: the same save and the same fight twice, once with `0` and once with `1`; compare the `tick parts` lines: the first line's
`ms each in all`, and `objects` (animation, node matrices, object references and the physics: where `matrix4x3_multiply`, the quaternion
load and the reference search are mostly called), `ai` and `units` (the sight and aim tests: the BSP walks and the sphere test). Read the
calls a tick to see how much each function is asked; the saving of a kernel is about (calls a tick) x (the game's call with the C - the game's
call with the kernel) from the timing lines. The expected total is under a millisecond a tick; the logs say what it is.
