#include "readback.h"

#include <string.h>

int readback_detect_order(const uint8_t found[4], const uint8_t expected[4], readback_order *order)
{
	unsigned i, j;
	int ok = 1;

	for (i = 0; i < 4; i++)
	{
		unsigned matches = 0;

		order->source[i] = (uint8_t)i;
		for (j = 0; j < 4; j++)
		{
			if (found[j] == expected[i])
			{
				order->source[i] = (uint8_t)j;
				matches++;
			}
		}
		if (matches != 1)
			ok = 0;
	}
	if (!ok)
	{
		for (i = 0; i < 4; i++)
			order->source[i] = (uint8_t)i;
		return 0;
	}
	return 1;
}

void readback_pixel(const uint8_t *image, unsigned pitch_bytes, unsigned x, unsigned y, const readback_order *order, uint8_t rgba[4])
{
	const uint8_t *p = image + (size_t)y * pitch_bytes + (size_t)x * 4;
	unsigned i;

	for (i = 0; i < 4; i++)
		rgba[i] = p[order->source[i]];
}

unsigned readback_distance(const uint8_t a[4], const uint8_t b[4])
{
	unsigned i, worst = 0;

	for (i = 0; i < 4; i++)
	{
		unsigned d = a[i] > b[i] ? (unsigned)(a[i] - b[i]) : (unsigned)(b[i] - a[i]);

		if (d > worst)
			worst = d;
	}
	return worst;
}

void readback_compare_image(const uint8_t *image, unsigned pitch_bytes, unsigned width, unsigned height, const readback_order *order,
	const uint8_t *expected, unsigned tolerance, readback_compare *result)
{
	unsigned x, y;

	memset(result, 0, sizeof(*result));
	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			uint8_t found[4];
			const uint8_t *want = expected + ((size_t)y * width + x) * 4;
			unsigned d;

			readback_pixel(image, pitch_bytes, x, y, order, found);
			d = readback_distance(found, want);
			result->pixels++;
			if (d > result->max_error)
				result->max_error = d;
			if (d > tolerance)
			{
				if (!result->mismatches)
				{
					result->first_x = x;
					result->first_y = y;
					memcpy(result->first_found, found, 4);
					memcpy(result->first_expected, want, 4);
				}
				result->mismatches++;
			}
		}
	}
}

uint32_t readback_hash(const uint8_t *image, unsigned pitch_bytes, unsigned width, unsigned height, const readback_order *order)
{
	uint32_t hash = 2166136261u;
	unsigned x, y, k;

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			uint8_t p[4];

			readback_pixel(image, pitch_bytes, x, y, order, p);
			for (k = 0; k < 4; k++)
				hash = (hash ^ p[k]) * 16777619u;
		}
	}
	return hash;
}
