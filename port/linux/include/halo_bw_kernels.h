/*
HALO_BW_KERNELS.H

The Broadway kernels: hand-written paired-single assembly for the Wii U's Espresso (port/wiiu/src/bw, from the Broadway assembly
bundle of 2026-11-01, CC0) for six hot pieces of the game's maths, collision and animation loading, and the switch that decides
whether a call takes the kernel or the C function it replaces.

Only the Wii U build (HALO_BW_KERNELS, with the assembly) and the host test builds (HALO_BW_KERNELS with HALO_BW_MODEL, a scalar C
model of each kernel that does the same operations in the same order) have any of this; every other build is untouched.

The kernels are exact: the same bits as the C function for every input, because the game's 30 Hz ticks must give the same results on
a Wii U as on a PC. Nothing is fused, no reciprocal or root is estimated, and a kernel that meets a case it does not do hands it to the
C (reference_find returns NULL, the BSP walks stop before the node, quaternion_load returns 0). They are off until the card's
halo/broadway.txt says 1 and each kernel has passed its self-test on the console (port/wiiu/src/wiiu_bw_kernels.c).
*/

#ifndef HALO_BW_KERNELS_H
#define HALO_BW_KERNELS_H

#ifdef HALO_BW_KERNELS

enum halo_bw_kernel
{
	HALO_BW_K_MATRIX,		/* matrix4x3_multiply */
	HALO_BW_K_SPHERE,		/* fast_vector_intersects_sphere */
	HALO_BW_K_BSP_RAY,		/* the straight runs of collision_bsp_test_vector_recursive */
	HALO_BW_K_BSP_SPHERE,	/* the straight runs of bsp3d_test_sphere_recursive */
	HALO_BW_K_REFERENCE,	/* the search of reference_list_remove */
	HALO_BW_K_QUATERNION,	/* quaternion_decompress_8byte */
	HALO_BW_K_COUNT
};

#define HALO_BW_BIT(kernel) (1u << (kernel))

/* the kernels in use: a bit of HALO_BW_BIT for each (zero until the console's self-test turns them on) */
extern unsigned halo_bw_flags;
/* calls of each replaced function; those a kernel answered (the rest ran the C); the nodes the BSP walks went through */
extern unsigned long halo_bw_calls[HALO_BW_K_COUNT];
extern unsigned long halo_bw_used[HALO_BW_K_COUNT];
extern unsigned long halo_bw_walked[HALO_BW_K_COUNT];

#define HALO_BW_COUNT(kernel) (halo_bw_calls[kernel]++)
#define HALO_BW_USED(kernel) (halo_bw_used[kernel]++)

/* The BSP walks' state (the offsets are the assembly's; this is 32 bytes on the Wii U):
nodes: 12-byte nodes (plane designator, two children), planes: 16-byte planes (normal, d), the point and vector of the ray (or the
sphere's center and no vector), the counts of both blocks, the node budget left and the depth reached (both in and out). */
struct halo_bw_bsp_walk
{
	const void *nodes;
	const void *planes;
	const float *point;
	const float *vector;
	unsigned node_count;
	unsigned plane_count;
	int nodes_left;
	int depth;
};

#ifdef HALO_BW_MODEL

#include "halo_bw_model.h"

#define halo_bw_matrix4x3_multiply halo_bw_model_matrix4x3_multiply
#define halo_bw_ray_sphere halo_bw_model_ray_sphere
#define halo_bw_bsp_ray_walk halo_bw_model_bsp_ray_walk
#define halo_bw_bsp_sphere_walk halo_bw_model_bsp_sphere_walk
#define halo_bw_reference_find halo_bw_model_reference_find
#define halo_bw_quaternion_load halo_bw_model_quaternion_load

#else

/* the assembly (port/wiiu/src/bw); arguments and registers as each .S says */
void halo_bw_asm_matrix4x3_multiply(const float *a, const float *b, float *out);
int halo_bw_asm_ray_sphere(const float *point, const float *vector, const float *center, float radius);
int halo_bw_asm_bsp_ray_walk(struct halo_bw_bsp_walk *walk, int node, float t0, float t1);
int halo_bw_asm_bsp_sphere_walk(struct halo_bw_bsp_walk *walk, int node, float radius);
int *halo_bw_asm_reference_find(const void *references, unsigned count, unsigned zero_invalid, int *first, int datum);
int halo_bw_asm_quaternion_load(const void *compressed, float *quaternion);
void halo_bw_asm_read_gqr(unsigned *eight);

#define halo_bw_matrix4x3_multiply halo_bw_asm_matrix4x3_multiply
#define halo_bw_ray_sphere halo_bw_asm_ray_sphere
#define halo_bw_bsp_ray_walk halo_bw_asm_bsp_ray_walk
#define halo_bw_bsp_sphere_walk halo_bw_asm_bsp_sphere_walk
#define halo_bw_reference_find halo_bw_asm_reference_find
#define halo_bw_quaternion_load halo_bw_asm_quaternion_load

#endif /* HALO_BW_MODEL */

#endif /* HALO_BW_KERNELS */

#endif
