#include "wiiu_mip.h"

void wiiu_mip_next(const uint8_t *source, uint32_t source_width, uint32_t source_height, uint32_t source_pitch, uint8_t *destination, uint32_t destination_pitch)
{
	const uint32_t width = source_width > 1 ? source_width / 2 : 1, height = source_height > 1 ? source_height / 2 : 1;
	uint32_t x, y, channel;

	for (y = 0; y < height; y++)
	{
		const uint32_t top = y * 2 < source_height ? y * 2 : source_height - 1, bottom = top + 1 < source_height ? top + 1 : top;
		const uint8_t *row0 = source + (size_t)top * source_pitch * 4, *row1 = source + (size_t)bottom * source_pitch * 4;
		uint8_t *out = destination + (size_t)y * destination_pitch * 4;

		for (x = 0; x < width; x++)
		{
			const uint32_t left = x * 2 < source_width ? x * 2 : source_width - 1, right = left + 1 < source_width ? left + 1 : left;

			for (channel = 0; channel < 4; channel++)
				out[x * 4 + channel] = (uint8_t)((row0[left * 4 + channel] + row0[right * 4 + channel] + row1[left * 4 + channel] + row1[right * 4 + channel] + 2) >> 2);
		}
	}
}
