#include "vertex_test.h"

#include <string.h>

const char *vtx_format_name(vtx_format format)
{
	static const char *const names[VTX_FORMAT_COUNT] =
	{
		"float32 x4", "float32 x2", "unorm8 x4 (D3DCOLOR)", "snorm16 x4", "sint16 x2 as float", "uint32 (NORMPACKED3)"
	};

	return (unsigned)format < VTX_FORMAT_COUNT ? names[format] : "?";
}

const char *vtx_swap_name(vtx_swap swap)
{
	static const char *const names[VTX_SWAP_COUNT] = { "none", "8_IN_16", "8_IN_32" };

	return (unsigned)swap < VTX_SWAP_COUNT ? names[swap] : "?";
}

unsigned vtx_format_bytes(vtx_format format)
{
	switch (format)
	{
	case VTX_FLOAT32X4: return 16;
	case VTX_FLOAT32X2: return 8;
	case VTX_SNORM16X4: return 8;
	default: return 4;
	}
}

/* ---------- the test vectors */

static const float float4_values[2][4] = { { 0.25f, -0.5f, 1.5f, 1.0f }, { -1.0f, 0.75f, 0.5f, -0.25f } };
static const float float2_values[2][2] = { { 0.5f, -0.75f }, { -1.5f, 1.25f } };
/* B, G, R, A as the D3DCOLOR dword 0xAARRGGBB holds them in memory */
static const uint8_t color_bytes[2][4] = { { 0x30, 0x60, 0x90, 0xC0 }, { 0xE1, 0x52, 0x13, 0x84 } };
static const int16_t snorm_values[2][4] = { { 9320, -8029, 23324, -32767 }, { -16001, 28701, -5120, 7777 } };
static const int16_t short_values[2][2] = { { 1, -2 }, { -1, 2 } };
/* x and y 11 bits, z 10 bits, signed */
static const int packed_values[2][3] = { { 700, -300, 200 }, { -500, 400, -100 } };

static void put_le32(uint8_t *out, uint32_t value)
{
	out[0] = (uint8_t)(value & 0xFF);
	out[1] = (uint8_t)((value >> 8) & 0xFF);
	out[2] = (uint8_t)((value >> 16) & 0xFF);
	out[3] = (uint8_t)((value >> 24) & 0xFF);
}

static uint32_t get_le32(const uint8_t *in)
{
	return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

static void put_float(uint8_t *out, float value)
{
	uint32_t bits;

	memcpy(&bits, &value, 4);
	put_le32(out, bits);
}

static float get_float(const uint8_t *in)
{
	uint32_t bits = get_le32(in);
	float value;

	memcpy(&value, &bits, 4);
	return value;
}

static void put_le16(uint8_t *out, int value)
{
	out[0] = (uint8_t)(value & 0xFF);
	out[1] = (uint8_t)((value >> 8) & 0xFF);
}

static int get_le16(const uint8_t *in)
{
	int v = in[0] | (in[1] << 8);

	return v >= 32768 ? v - 65536 : v;
}

void vtx_pack_le(vtx_format format, unsigned which, uint8_t out[VTX_MAX_BYTES])
{
	unsigned i;

	which &= 1;
	memset(out, 0, VTX_MAX_BYTES);
	switch (format)
	{
	case VTX_FLOAT32X4:
		for (i = 0; i < 4; i++)
			put_float(out + 4 * i, float4_values[which][i]);
		break;
	case VTX_FLOAT32X2:
		for (i = 0; i < 2; i++)
			put_float(out + 4 * i, float2_values[which][i]);
		break;
	case VTX_UNORM8X4:
		memcpy(out, color_bytes[which], 4);
		break;
	case VTX_SNORM16X4:
		for (i = 0; i < 4; i++)
			put_le16(out + 2 * i, snorm_values[which][i]);
		break;
	case VTX_SSCALED16X2:
		for (i = 0; i < 2; i++)
			put_le16(out + 2 * i, short_values[which][i]);
		break;
	default:
		put_le32(out, ((uint32_t)packed_values[which][0] & 0x7FF) | (((uint32_t)packed_values[which][1] & 0x7FF) << 11) |
			(((uint32_t)packed_values[which][2] & 0x3FF) << 22));
		break;
	}
}

/* the shader's unpack_normpacked3 (nv2a_vsh.c): two 11-bit and one 10-bit signed fields */
static void unpack_packed3(uint32_t p, float out[4])
{
	int x = (int)(p & 0x7FF), y = (int)((p >> 11) & 0x7FF), z = (int)((p >> 22) & 0x3FF);

	if (x >= 1024)
		x -= 2048;
	if (y >= 1024)
		y -= 2048;
	if (z >= 512)
		z -= 1024;
	out[0] = (float)x / 1023.0f;
	out[1] = (float)y / 1023.0f;
	out[2] = (float)z / 511.0f;
	out[3] = 1.0f;
}

static float snorm16(int v)
{
	float f = (float)v / 32767.0f;

	return f < -1.0f ? -1.0f : f;
}

void vtx_intended(vtx_format format, unsigned which, float out[4])
{
	which &= 1;
	switch (format)
	{
	case VTX_FLOAT32X4:
		memcpy(out, float4_values[which], 16);
		break;
	case VTX_FLOAT32X2:
		out[0] = float2_values[which][0];
		out[1] = float2_values[which][1];
		out[2] = 0.0f;
		out[3] = 1.0f;
		break;
	case VTX_UNORM8X4:
		/* (r, g, b, a) from the bytes b, g, r, a */
		out[0] = (float)color_bytes[which][2] / 255.0f;
		out[1] = (float)color_bytes[which][1] / 255.0f;
		out[2] = (float)color_bytes[which][0] / 255.0f;
		out[3] = (float)color_bytes[which][3] / 255.0f;
		break;
	case VTX_SNORM16X4:
	{
		unsigned i;

		for (i = 0; i < 4; i++)
			out[i] = snorm16(snorm_values[which][i]);
		break;
	}
	case VTX_SSCALED16X2:
		out[0] = (float)short_values[which][0];
		out[1] = (float)short_values[which][1];
		out[2] = 0.0f;
		out[3] = 1.0f;
		break;
	default:
	{
		uint8_t bytes[VTX_MAX_BYTES];

		vtx_pack_le(VTX_PACKED3, which, bytes);
		unpack_packed3(get_le32(bytes), out);
		break;
	}
	}
}

/* ---------- what the hardware makes of the bytes */

static void swap_units(vtx_swap swap, const uint8_t *in, unsigned n, uint8_t *out)
{
	unsigned unit = swap == VTX_SWAP_8_IN_16 ? 2 : swap == VTX_SWAP_8_IN_32 ? 4 : 0;
	unsigned i, j;

	if (!unit)
	{
		memcpy(out, in, n);
		return;
	}
	for (i = 0; i + unit <= n; i += unit)
		for (j = 0; j < unit; j++)
			out[i + j] = in[i + unit - 1 - j];
}

void vtx_decode(vtx_format format, vtx_swap swap, const uint8_t bytes[VTX_MAX_BYTES], float out[4])
{
	uint8_t b[VTX_MAX_BYTES];
	unsigned i;

	swap_units(swap, bytes, vtx_format_bytes(format), b);
	switch (format)
	{
	case VTX_FLOAT32X4:
		for (i = 0; i < 4; i++)
			out[i] = get_float(b + 4 * i);
		break;
	case VTX_FLOAT32X2:
		out[0] = get_float(b);
		out[1] = get_float(b + 4);
		out[2] = 0.0f;
		out[3] = 1.0f;
		break;
	case VTX_UNORM8X4:
		for (i = 0; i < 4; i++)
			out[i] = (float)b[i] / 255.0f;
		break;
	case VTX_SNORM16X4:
		for (i = 0; i < 4; i++)
			out[i] = snorm16(get_le16(b + 2 * i));
		break;
	case VTX_SSCALED16X2:
		out[0] = (float)get_le16(b);
		out[1] = (float)get_le16(b + 2);
		out[2] = 0.0f;
		out[3] = 1.0f;
		break;
	default:
		unpack_packed3(get_le32(b), out);
		break;
	}
}

void vtx_select(const float in[4], const uint8_t sel[4], float out[4])
{
	unsigned i;

	for (i = 0; i < 4; i++)
		out[i] = sel[i] < 4 ? in[sel[i]] : sel[i] == VTX_SEL_ONE ? 1.0f : 0.0f;
}

void vtx_selection_for(vtx_format format, vtx_swap swap, uint8_t sel[4])
{
	static const uint8_t identity[4] = { 0, 1, 2, 3 };

	memcpy(sel, identity, 4);
	if (format == VTX_FLOAT32X2 || format == VTX_SSCALED16X2)
	{
		sel[2] = VTX_SEL_ZERO;
		sel[3] = VTX_SEL_ONE;
	}
	else if (format == VTX_UNORM8X4)
	{
		/* the bytes arrive as b, g, r, a, rearranged by the swap; the output wants r, g, b, a */
		uint8_t memory_index[4] = { 2, 1, 0, 3 }; /* which byte of the memory dword is r, g, b, a */
		unsigned i;

		for (i = 0; i < 4; i++)
		{
			unsigned m = memory_index[i];
			unsigned position = swap == VTX_SWAP_8_IN_32 ? 3 - m : swap == VTX_SWAP_8_IN_16 ? (m ^ 1) : m;

			sel[i] = (uint8_t)position;
		}
	}
}

void vtx_to_pixel(const float value[4], uint8_t rgba[4])
{
	unsigned i;

	for (i = 0; i < 4; i++)
	{
		float v = value[i] * 0.25f + 0.5f;

		v = !(v >= 0.0f) ? 0.0f : v > 1.0f ? 1.0f : v; /* (not a number: 0) */
		rgba[i] = (uint8_t)(v * 255.0f + 0.5f);
	}
}

void vtx_expected_pixel(vtx_format format, unsigned which, vtx_swap hypothesis, const uint8_t sel[4], uint8_t rgba[4])
{
	uint8_t bytes[VTX_MAX_BYTES];
	float fetched[4], selected[4];

	vtx_pack_le(format, which, bytes);
	vtx_decode(format, hypothesis, bytes, fetched);
	vtx_select(fetched, sel, selected);
	vtx_to_pixel(selected, rgba);
}

void vtx_value_for_pixel(const uint8_t rgba[4], float value[4])
{
	unsigned i;

	for (i = 0; i < 4; i++)
		value[i] = ((float)rgba[i] / 255.0f - 0.5f) * 4.0f;
}

unsigned vtx_classify(vtx_format format, const uint8_t sel[4], const uint8_t first[4], const uint8_t second[4], unsigned tolerance)
{
	unsigned mask = 0, h, i;

	for (h = 0; h < VTX_SWAP_COUNT; h++)
	{
		uint8_t want[2][4];
		int match = 1;

		vtx_expected_pixel(format, 0, (vtx_swap)h, sel, want[0]);
		vtx_expected_pixel(format, 1, (vtx_swap)h, sel, want[1]);
		for (i = 0; i < 4; i++)
		{
			unsigned d0 = first[i] > want[0][i] ? (unsigned)(first[i] - want[0][i]) : (unsigned)(want[0][i] - first[i]);
			unsigned d1 = second[i] > want[1][i] ? (unsigned)(second[i] - want[1][i]) : (unsigned)(want[1][i] - second[i]);

			if (d0 > tolerance || d1 > tolerance)
				match = 0;
		}
		if (match)
			mask |= 1u << h;
	}
	return mask;
}
