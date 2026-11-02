/* Walks of a run of collision BSP nodes without a call or a tag block lookup for each, adapted from the bundle's
 * wii_collision_bsp.S (the Broadway assembly bundle of 2026-11-01, CC0, see README.md here): the pair lanes calculate
 * point . normal and vector . normal together, and the ray's operands stay in registers across nodes.
 *
 * Changed from the bundle: the walk does exactly what collision_bsp_test_vector_recursive and bsp3d_test_sphere_recursive
 * (source/physics/collision_bsp.c) do at a node that sends the test one way, including the port's checks of the node
 * count, the depth and the node budget (collision_bsp3d_enter_node), and gives the test back to the C, with nothing about
 * the node taken, at anything else: a leaf or an index out of range, the budget or the depth reached, a plane designator
 * out of range, a node the test crosses, and a distance that is not a number. The C then does that node as it does any.
 * Separate multiplies and adds in the C expressions' order (dot_product3d, plane3d_distance_to_point); no fused operation.
 *
 * struct halo_bw_bsp_walk (halo_bw_kernels.h), 32 bytes:
 *   0 nodes (12-byte nodes: plane designator, two children), 4 planes (16 bytes: normal, d), 8 point, 12 vector,
 *   16 node count, 20 plane count, 24 nodes left (in, out), 28 depth (in, out).
 *
 *   int halo_bw_asm_bsp_ray_walk(struct halo_bw_bsp_walk *walk (r3), int node (r4), float t0 (f1), float t1 (f2));
 *   int halo_bw_asm_bsp_sphere_walk(struct halo_bw_bsp_walk *walk (r3), int node (r4), float radius (f1));
 * They return the index of the node the C goes on with.
 */
    .machine "broadway"
    .section .rodata.halo_bw_bsp_zero,"a",@progbits
    .balign 4
.Lzero:
    .float 0.0

    .section .text.halo_bw_bsp,"ax",@progbits
    .balign 4

    .global halo_bw_asm_bsp_ray_walk
    .type halo_bw_asm_bsp_ray_walk,@function
halo_bw_asm_bsp_ray_walk:
    lwz         5,0(3)              /* nodes */
    lwz         6,4(3)              /* planes */
    lwz         7,8(3)              /* point */
    lwz         8,12(3)             /* vector */
    lwz         9,16(3)             /* node count */
    lwz         10,20(3)            /* plane count */
    psq_l       3,0(7),0,0          /* (px, py) */
    psq_l       4,0(8),0,0          /* (vx, vy) */
    ps_merge11  5,3,4               /* (py, vy) */
    ps_merge00  3,3,4               /* (px, vx) */
    ps_mr       4,5                 /* (py, vy) */
    lfs         5,8(7)              /* pz */
    lfs         6,8(8)              /* vz */
    ps_merge00  5,5,6               /* (pz, vz) */
    lis         12,.Lzero@ha
    lfs         0,.Lzero@l(12)
    lwz         7,24(3)             /* nodes left */
    lwz         8,28(3)             /* depth */
.Lray_node:
    cmplw       4,9
    bge         .Lray_exit          /* a leaf (the high bit) or out of range */
    cmpwi       8,128
    bge         .Lray_exit          /* MAXIMUM_BSP3D_TRAVERSAL_DEPTH */
    cmpwi       7,0
    ble         .Lray_exit          /* the node budget is spent */
    mulli       11,4,12
    add         11,5,11             /* the node */
    lwz         12,0(11)            /* its plane designator */
    cmplw       12,10
    bge         .Lray_exit          /* (also a negative one) */
    slwi        12,12,4
    add         12,6,12             /* the plane */
    psq_l       6,0(12),0,0         /* (nx, ny) */
    psq_l       7,8(12),0,0         /* (nz, d) */
    ps_muls0    8,3,6               /* (px*nx, vx*nx) */
    ps_muls1    9,4,6               /* (py*ny, vy*ny) */
    ps_add      8,8,9
    ps_muls0    9,5,7               /* (pz*nz, vz*nz) */
    ps_add      8,8,9               /* (point . n, vector . n) */
    ps_merge11  9,7,7               /* d */
    fsubs       10,8,9              /* distance */
    ps_merge11  11,8,8              /* dot */
    fmuls       12,11,1
    fadds       12,12,10            /* distance0 = dot*t0 + distance */
    fmuls       13,11,2
    fadds       13,13,10            /* distance1 = dot*t1 + distance */
    fcmpu       0,12,0
    fcmpu       1,13,0
    bun         0,.Lray_exit
    bun         1,.Lray_exit
    blt         0,.Lray_back
    blt         1,.Lray_exit        /* both sides: a crossing */
    lwz         4,8(11)             /* in front: children[1] */
    b           .Lray_step
.Lray_back:
    bge         1,.Lray_exit
    lwz         4,4(11)             /* behind: children[0] */
.Lray_step:
    addi        7,7,-1
    addi        8,8,1
    b           .Lray_node
.Lray_exit:
    stw         7,24(3)
    stw         8,28(3)
    mr          3,4
    blr
    .size halo_bw_asm_bsp_ray_walk,.-halo_bw_asm_bsp_ray_walk

    .global halo_bw_asm_bsp_sphere_walk
    .type halo_bw_asm_bsp_sphere_walk,@function
halo_bw_asm_bsp_sphere_walk:
    lwz         5,0(3)
    lwz         6,4(3)
    lwz         7,8(3)              /* the center */
    lwz         9,16(3)
    lwz         10,20(3)
    psq_l       3,0(7),0,0          /* (cx, cy) */
    lfs         4,8(7)              /* cz */
    fneg        2,1                 /* -radius */
    lwz         7,24(3)
    lwz         8,28(3)
.Lsphere_node:
    cmplw       4,9
    bge         .Lsphere_exit
    cmpwi       8,128
    bge         .Lsphere_exit
    cmpwi       7,0
    ble         .Lsphere_exit
    mulli       11,4,12
    add         11,5,11
    lwz         12,0(11)
    cmplw       12,10
    bge         .Lsphere_exit
    slwi        12,12,4
    add         12,6,12
    psq_l       5,0(12),0,0         /* (nx, ny) */
    psq_l       6,8(12),0,0         /* (nz, d) */
    ps_mul      7,3,5               /* (cx*nx, cy*ny) */
    ps_sum0     7,7,7,7
    fmuls       8,4,6               /* cz*nz */
    fadds       8,7,8
    ps_merge11  9,6,6               /* d */
    fsubs       10,8,9              /* distance */
    fcmpu       0,10,2              /* against -radius */
    fcmpu       1,10,1              /* against radius */
    bun         0,.Lsphere_exit
    bun         1,.Lsphere_exit
    ble         0,.Lsphere_back     /* distance <= -radius: children[0] */
    blt         1,.Lsphere_exit     /* -radius < distance < radius: both sides */
    lwz         4,8(11)
    b           .Lsphere_step
.Lsphere_back:
    lwz         4,4(11)
.Lsphere_step:
    addi        7,7,-1
    addi        8,8,1
    b           .Lsphere_node
.Lsphere_exit:
    stw         7,24(3)
    stw         8,28(3)
    mr          3,4
    blr
    .size halo_bw_asm_bsp_sphere_walk,.-halo_bw_asm_bsp_sphere_walk
