/* wiiu_mip.c: the next mip level is the 2 x 2 average, whatever the sizes and pitches. */

#include "../src/wiiu_mip.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

int main(void)
{
	uint8_t source[8 * 8 * 4], out[8 * 4 * 4];
	unsigned x, y, c;

	/* a flat image stays flat, in every size down to 1 x 1 */
	for (x = 0; x < sizeof(source); x++)
		source[x] = 77;
	memset(out, 0, sizeof(out));
	wiiu_mip_next(source, 8, 8, 8, out, 4);
	for (y = 0; y < 4; y++)
		for (x = 0; x < 4 * 4; x++)
			CHECK(out[y * 16 + x] == 77);
	memset(out, 0, sizeof(out));
	wiiu_mip_next(source, 1, 1, 8, out, 1);
	CHECK(out[0] == 77 && out[3] == 77);

	/* averages with rounding to nearest: (0 + 0 + 0 + 255) / 4 = 63.75 -> 64; (1 + 1 + 0 + 0) / 4 = 0.5 -> 1 (the half rounds up) */
	memset(source, 0, sizeof(source));
	source[0 * 4 + 0] = 255; /* texel (0,0) red */
	source[1 * 4 + 1] = 1;
	source[8 * 4 + 1 * 4 + 1] = 1; /* (1,1) green */
	wiiu_mip_next(source, 2, 2, 8, out, 4);
	CHECK(out[0] == 64 && out[1] == 1);

	/* each channel is its own: the channels of four different texels */
	for (c = 0; c < 4; c++)
	{
		source[c] = 10 + c;
		source[4 + c] = 20 + c;
		source[8 * 4 + c] = 30 + c;
		source[8 * 4 + 4 + c] = 40 + c;
	}
	wiiu_mip_next(source, 2, 2, 8, out, 4);
	for (c = 0; c < 4; c++)
		CHECK(out[c] == 25 + c); /* (10+20+30+40 + 4c) / 4 = 25 + c */

	/* the output's pitch is its own: a destination row starts pitch texels after the last */
	memset(out, 0xee, sizeof(out));
	wiiu_mip_next(source, 4, 2, 8, out, 5);
	CHECK(out[4 * 4] == 0xee && out[5 * 4 - 1] == 0xee); /* (only 2 x 1 texels written) */
	CHECK(out[0] == 25);

	/* odd sizes repeat the last row and column: a 3 x 3 gives 1 x 1 from texels (0..1, 0..1) */
	memset(source, 0, sizeof(source));
	source[0] = 100;
	source[4] = 200;
	source[8 * 4] = 100;
	source[8 * 4 + 4] = 200;
	wiiu_mip_next(source, 3, 3, 8, out, 4);
	CHECK(out[0] == 150);

	/* a width of 1 averages the two rows only; a height of 1 the two columns only */
	memset(source, 0, sizeof(source));
	source[0] = 10;
	source[8 * 4] = 30;
	wiiu_mip_next(source, 1, 2, 8, out, 4);
	CHECK(out[0] == 20);
	memset(source, 0, sizeof(source));
	source[0] = 10;
	source[4] = 31;
	wiiu_mip_next(source, 2, 1, 8, out, 4);
	CHECK(out[0] == 21); /* (10 + 31 + 10 + 31 + 2) / 4 = 21 */

	printf(failures ? "%d failures\n" : "mip: all passed\n", failures);
	return failures != 0;
}
