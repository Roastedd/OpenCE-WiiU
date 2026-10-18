/*
VERTEX_TEST.H

Little-endian vertex streams on a big-endian console. The Xbox's vertex data is little-endian and the converter that makes
the maps big-endian does not touch what the renderer builds at run time, so it matters whether GX2's attribute fetch can
read a little-endian stream as it is (GX2AttribStream.endianSwap), for each of the formats the game's vertex
declarations use: D3DVSDT_FLOAT1-4, D3DCOLOR, SHORT and NORMSHORT, NORMPACKED3 (one 32-bit integer the shader unpacks).

This module is what does not need the console: the stream bytes the test uploads, what a fetch that swaps bytes in
2- or 4-byte units (GX2's 8_IN_16 and 8_IN_32; "none" is no swap) makes of them, the component selection (the attribute
mask) and the pixel the test's pixel shader makes of a value, so the program can say which setting gave the value the
game means. A test vector is one that the three swaps disagree about (tests/test_gx2_logic.c checks it).
*/

#ifndef HALO_WIIU_GX2_VERTEX_TEST_H
#define HALO_WIIU_GX2_VERTEX_TEST_H

#include <stdint.h>

typedef enum vtx_format
{
	VTX_FLOAT32X4,   /* D3DVSDT_FLOAT4 */
	VTX_FLOAT32X2,   /* D3DVSDT_FLOAT2 (z = 0, w = 1) */
	VTX_UNORM8X4,    /* D3DVSDT_D3DCOLOR: bytes B, G, R, A in memory; the shader gets (r, g, b, a) */
	VTX_SNORM16X4,   /* D3DVSDT_NORMSHORT4 */
	VTX_SSCALED16X2, /* D3DVSDT_SHORT2: the integer as a float (z = 0, w = 1) */
	VTX_PACKED3,     /* D3DVSDT_NORMPACKED3: one 32-bit integer, unpacked in the shader */
	VTX_FORMAT_COUNT
} vtx_format;

/* what the fetch does to the bytes: no swap, or reverse the bytes of every 16-bit or 32-bit unit */
typedef enum vtx_swap
{
	VTX_SWAP_NONE,
	VTX_SWAP_8_IN_16,
	VTX_SWAP_8_IN_32,
	VTX_SWAP_COUNT
} vtx_swap;

#define VTX_MAX_BYTES 16

const char *vtx_format_name(vtx_format format);
const char *vtx_swap_name(vtx_swap swap);
unsigned vtx_format_bytes(vtx_format format);

/* The test vectors: which = 0 is the first triangle's value, 1 the second's. The bytes the Xbox's stream holds
(little-endian), and the vec4 the game's shader should see. */
void vtx_pack_le(vtx_format format, unsigned which, uint8_t out[VTX_MAX_BYTES]);
void vtx_intended(vtx_format format, unsigned which, float out[4]);

/* the vec4 a fetch that swaps as given delivers from the stream's bytes, before the mask */
void vtx_decode(vtx_format format, vtx_swap swap, const uint8_t bytes[VTX_MAX_BYTES], float out[4]);

/* The component selection. Each of the four outputs takes component 0-3 of the fetched value (x, y, z, w), or
VTX_SEL_ZERO or VTX_SEL_ONE. */
#define VTX_SEL_ZERO 4
#define VTX_SEL_ONE 5
void vtx_select(const float in[4], const uint8_t sel[4], float out[4]);

/* the selection that makes a fetch which swaps as given deliver the intended vec4 (the identity, with zero and one
filling the missing components, for all but the byte color) */
void vtx_selection_for(vtx_format format, vtx_swap swap, uint8_t sel[4]);

/* the pixel the test's pixel shader draws for a value: -2..2 shown as 0..1 */
void vtx_to_pixel(const float value[4], uint8_t rgba[4]);

/* the pixel for a stream under a hypothesis about what the hardware does with the bytes, and a selection */
void vtx_expected_pixel(vtx_format format, unsigned which, vtx_swap hypothesis, const uint8_t sel[4], uint8_t rgba[4]);

/* the value that makes the test's pixel shader draw a pixel (for building index and clear tests) */
void vtx_value_for_pixel(const uint8_t rgba[4], float value[4]);

/* Which hypotheses (bit 1 << vtx_swap) agree with the pixels drawn for the two triangles. */
unsigned vtx_classify(vtx_format format, const uint8_t sel[4], const uint8_t first[4], const uint8_t second[4], unsigned tolerance);

#endif
