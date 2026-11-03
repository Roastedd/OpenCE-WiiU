/* The segment and sphere test of fast_vector_intersects_sphere (source/math/real_math.c), adapted from the bundle's
 * wii_collision_sphere.S (the Broadway assembly bundle of 2026-11-01, CC0, see README.md here).
 *
 * Changed from the bundle: the whole function, with its own |vector|^2 (computed where the C computes it, in its order),
 * and every branch is the C comparison's own, the NaN cases included (the bundle's bge and ble take the branch of an
 * unordered compare, where the C's >= and <= do not). Separate multiplies and adds in the C expression's order; no fused
 * operation, no reciprocal or root. Volatile EABI registers only.
 *
 *   int halo_bw_asm_ray_sphere(const float *point (r3), const float *vector (r4), const float *center (r5), float radius (f1));
 *
 *   p = point - center
 *   c = ((px*px + py*py) + pz*pz) - radius*radius;  if (c < 0) hit
 *   b = (vi*px + vj*py) + vk*pz;                    if (b >= 0) miss
 *   a = (vi*vi + vj*vj) + vk*vk
 *   disc = b*b - a*c;                               if (disc <= 0) miss
 *   n = -a - b;                                     if (n < 0) hit
 *   hit if n*n < disc
 */
    .machine "broadway"
    .section .rodata.halo_bw_zero,"a",@progbits
    .balign 4
.Lzero:
    .float 0.0

    .section .text.halo_bw_sphere,"ax",@progbits
    .balign 4
    .global halo_bw_range_sphere_begin
halo_bw_range_sphere_begin:
    .global halo_bw_asm_ray_sphere
    .type halo_bw_asm_ray_sphere,@function
halo_bw_asm_ray_sphere:
    psq_l       2,0(3),0,0          /* (point.x, point.y) */
    psq_l       3,0(5),0,0          /* (center.x, center.y) */
    ps_sub      2,2,3               /* (px, py) */
    lfs         5,8(3)
    lfs         6,8(5)
    fsubs       5,5,6               /* pz */
    ps_mul      4,2,2               /* (px*px, py*py) */
    ps_sum0     4,4,4,4             /* px*px + py*py */
    fmuls       6,5,5               /* pz*pz */
    fadds       4,4,6
    fmuls       1,1,1               /* radius*radius */
    fsubs       4,4,1               /* c */
    lis         6,.Lzero@ha
    lfs         0,.Lzero@l(6)
    fcmpu       0,4,0
    blt         0,.Lhit             /* c < 0 */
    psq_l       3,0(4),0,0          /* (vector.i, vector.j) */
    ps_mul      7,2,3               /* (px*vi, py*vj) */
    ps_sum0     7,7,7,7
    lfs         6,8(4)              /* vector.k */
    fmuls       8,5,6               /* pz*vk */
    fadds       7,7,8               /* b */
    fcmpu       0,7,0
    cror        2,1,2               /* EQ = b > 0 or b == 0, not for unordered */
    beq         0,.Lmiss            /* b >= 0 */
    ps_mul      8,3,3               /* (vi*vi, vj*vj) */
    ps_sum0     8,8,8,8
    fmuls       9,6,6               /* vk*vk */
    fadds       8,8,9               /* a */
    fmuls       9,7,7               /* b*b */
    fmuls       10,8,4              /* a*c */
    fsubs       9,9,10              /* disc */
    fcmpu       0,9,0
    cror        2,0,2               /* EQ = disc < 0 or disc == 0, not for unordered */
    beq         0,.Lmiss            /* disc <= 0 */
    fneg        8,8
    fsubs       8,8,7               /* -a - b */
    fcmpu       0,8,0
    blt         0,.Lhit             /* < 0 */
    fmuls       8,8,8
    fcmpu       0,8,9
    blt         0,.Lhit             /* n*n < disc */
.Lmiss:
    li          3,0
    blr
.Lhit:
    li          3,1
    blr
    .size halo_bw_asm_ray_sphere,.-halo_bw_asm_ray_sphere
    .global halo_bw_range_sphere_end
halo_bw_range_sphere_end:
