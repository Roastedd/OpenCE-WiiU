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
