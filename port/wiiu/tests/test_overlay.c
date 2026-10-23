/* wiiu_overlay.c: the frame rate, the text and the graph. Writes overlay.ppm in the current directory if given an argument, to look at. */

#include "../src/wiiu_overlay.h"

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

int main(int argc, char **argv)
{
	static struct overlay overlay;
	static uint8_t pixels[OVERLAY_WIDTH * OVERLAY_HEIGHT * 4];
	char line1[OVERLAY_LINE], line2[OVERLAY_LINE];
	unsigned i;

	overlay_reset(&overlay);
	CHECK(overlay_fps(&overlay) == 0.0f);
	/* steady 60 a second */
	for (i = 0; i < 100; i++)
		overlay_push(&overlay, 16.667f, 10.0f, 2.0f, 1.0f, 8.0f, 300, 250);
	CHECK(overlay_fps(&overlay) > 59.9f && overlay_fps(&overlay) < 60.1f);
	CHECK(overlay_longest(&overlay, 120) < 16.7f);
	/* a hitch shows in the longest, and the rate a little */
	overlay_push(&overlay, 120.0f, 30.0f, 2.0f, 1.0f, 8.0f, 300, 250);
	CHECK(overlay_longest(&overlay, 120) > 119.0f);
	CHECK(overlay_longest(&overlay, 1) > 119.0f);
	CHECK(overlay_longest(&overlay, 120) > overlay_longest(&overlay, 0));
	for (i = 0; i < 40; i++)
		overlay_push(&overlay, 33.333f, 25.0f, 4.0f, 2.0f, 0.0f, 300, 250); /* (gpu 0: not known, keeps the last average) */
	CHECK(overlay_fps(&overlay) > 29.9f && overlay_fps(&overlay) < 30.1f);
	CHECK(overlay.gpu_average > 7.9f && overlay.gpu_average < 8.1f);
	overlay.render_width = 960;
	overlay.render_height = 540;
	overlay.interpolation = 1;
	overlay_text(&overlay, line1, line2);
	printf("%s\n%s\n", line1, line2);
	CHECK(strstr(line1, "FPS 30.0") != NULL && strstr(line2, "960x540") != NULL && strstr(line2, "INTERP ON") != NULL);
	CHECK(strlen(line1) < OVERLAY_LINE / 4 * 4 && strlen(line1) <= OVERLAY_WIDTH / 4 && strlen(line2) <= OVERLAY_WIDTH / 4);
	/* rendered: a ground, lit text, bars */
	overlay_render(&overlay, pixels);
	{
		unsigned lit = 0, ground = 0, bars = 0;

		for (i = 0; i < OVERLAY_WIDTH * OVERLAY_HEIGHT; i++)
		{
			if (pixels[i * 4 + 3] == 255 && i / OVERLAY_WIDTH < 12)
				lit++;
			if (pixels[i * 4 + 3] == 176)
				ground++;
			if (pixels[i * 4 + 3] == 255 && i / OVERLAY_WIDTH >= 12)
				bars++;
		}
		CHECK(lit > 100 && ground > 1000 && bars > 1000);
	}
	/* the newest frame is the rightmost bar: red for the hitch's neighbours is not there, the last 40 are 33 ms: yellow */
	CHECK(pixels[((size_t)23 * OVERLAY_WIDTH + OVERLAY_WIDTH - 1) * 4 + 0] == 0xe0 && pixels[((size_t)23 * OVERLAY_WIDTH + OVERLAY_WIDTH - 1) * 4 + 1] == 0xc0);
	if (argc > 1)
	{
		FILE *file = fopen(argv[1], "wb");

		if (file)
		{
			fprintf(file, "P6\n%d %d\n255\n", OVERLAY_WIDTH, OVERLAY_HEIGHT);
			for (i = 0; i < OVERLAY_WIDTH * OVERLAY_HEIGHT; i++)
			{
				unsigned a = pixels[i * 4 + 3], c;

				for (c = 0; c < 3; c++)
					fputc((int)(pixels[i * 4 + c] * a / 255 + 90 * (255 - a) / 255), file); /* (over a grey, as on a picture) */
			}
			fclose(file);
		}
	}
	printf(failures ? "%d failures\n" : "overlay: all passed\n", failures);
	return failures != 0;
}
