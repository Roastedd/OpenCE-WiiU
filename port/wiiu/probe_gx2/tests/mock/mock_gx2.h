/*
MOCK_GX2.H

A stand-in for the console under the probe's stages, for tests/test_stages_mock.c: the GX2, WHB, coreinit, VPAD and
CafeGLSL functions the stages call, with a little GPU behind them that draws what the portable models (bc.c,
vertex_test.c, index_test.c) say a GPU with the configured behaviour would draw into a 64 x 64 target. It exists to run the
stages' own glue -- pixel positions, comparisons, classification, answers, the report -- on hypothetical consoles whose
behaviour is known, before a real one is tried. It is built for big-endian PowerPC (run under qemu-ppc): the stages
write floats and index data in the CPU's own byte order, and the GPU model reads them the way the Latte does.
*/

#ifndef HALO_WIIU_GX2_MOCK_H
#define HALO_WIIU_GX2_MOCK_H

#include "../../src/bc.h"
#include "../../src/readback.h"
#include "../../src/vertex_test.h"

typedef struct mock_config
{
	readback_order target_order; /* the channel order of a color buffer's bytes */
	int target_unreadable;       /* the CPU never sees what the GPU wrote */
	bc_permutation bc1_gpu;      /* what the GPU does to the bytes of a BC1 block */
	bc_permutation bc2_gpu;
	bc_permutation bc3_gpu;
	int pitch_in_blocks;         /* a compressed surface's pitch counts blocks, not texels */
	int rows_flipped;            /* textures are sampled upside down */
	vtx_swap default_swap;       /* what GX2_ENDIAN_SWAP_DEFAULT does to a vertex stream's bytes */
	int le_indices_broken;       /* the _LE index types read as if they swapped */
	int fail_es_dialect;         /* CompileShaderPair refuses "#version 300 es" */
	int fail_pair;               /* ... refuses the n-th pair (1-based; 0 none) */
	int different_programs;      /* the runtime compile gives other program bytes than the GSH file has */
} mock_config;

extern mock_config mock;

/* defaults: everything works the way the model says, bytes in the order R, G, B, A */
void mock_reset(void);
/* counters, for the test to look at */
extern unsigned mock_draws, mock_scene_draws;

#endif
