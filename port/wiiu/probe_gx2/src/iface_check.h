/*
IFACE_CHECK.H

CafeGLSL compiles a vertex shader and a pixel shader without linking them ("Matching vertex outputs to pixel inputs is
not checked"): the interpolants meet by a number the compiler gives each varying from its name, not by anything checked
at draw time. If the numbers differ, the pixel shader reads another varying than the vertex shader wrote. This reads the
registers of a compiled pair and says whether every input of the pixel shader has an output to come from.

A GX2VertexShader holds spi_vs_out_id[], four 8-bit semantic ids to a word (the first in the low byte, 0xFF unused);
a GX2PixelShader holds spi_ps_input_cntls[], whose low 8 bits are the semantic id it reads.
*/

#ifndef HALO_WIIU_GX2_IFACE_CHECK_H
#define HALO_WIIU_GX2_IFACE_CHECK_H

#include <stdint.h>

/* How many of the pixel shader's inputs no vertex shader output provides; the first one's id in *first_missing
(-1 if none). vs_words is num_spi_vs_out_id (words), ps_inputs is num_spi_ps_input_cntl. */
unsigned iface_missing(const uint32_t *vs_out_id, unsigned vs_words, const uint32_t *ps_input_cntl, unsigned ps_inputs, int *first_missing);

/* The index of the vertex output (the export parameter) that carries semantic id, or -1. */
int iface_output_index(const uint32_t *vs_out_id, unsigned vs_words, unsigned id);

#endif
