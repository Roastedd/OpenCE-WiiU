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
	static struct { uint8_t before[16], pixels[OVERLAY_WIDTH * OVERLAY_HEIGHT * 4], after[16]; } guarded;
	uint8_t *pixels = guarded.pixels;
	unsigned i;

	overlay_reset(&overlay);
	CHECK(overlay_fps(&overlay) == 0.0f);
	/* steady 60 a second */
	for (i = 0; i < 100; i++)
		overlay_push(&overlay, 16.667f, 10.0f, 2.0f, 1.0f, 8.0f, 300, 250);
	CHECK(overlay_fps(&overlay) > 59.9f && overlay_fps(&overlay) < 60.1f);
	CHECK(overlay_longest(&overlay, 120) < 16.7f);
	CHECK(overlay_percentile(&overlay, 50) > 16.6f && overlay_percentile(&overlay, 99) < 16.7f);
	overlay_update_view(&overlay);
	CHECK(overlay.view.fps > 59.9f && overlay.view.low_fps > 59.0f);
	CHECK(overlay.view.cpu_percent > 59.0f && overlay.view.cpu_percent < 61.0f); /* 10 of 16.7 ms */
	CHECK(overlay.view.gpu_percent > 47.0f && overlay.view.gpu_percent < 49.0f); /* 8 of 16.7 */
	/* a hitch shows in the longest and the low, hardly in the rate */
	overlay_push(&overlay, 120.0f, 30.0f, 2.0f, 1.0f, 8.0f, 300, 250);
	CHECK(overlay_longest(&overlay, 120) > 119.0f);
	CHECK(overlay_longest(&overlay, 1) > 119.0f);
	CHECK(overlay_percentile(&overlay, 100) > 119.0f && overlay_percentile(&overlay, 50) < 17.0f);
	for (i = 0; i < 150; i++)
		overlay_push(&overlay, i % 3 ? 16.667f : 33.333f, 25.0f, 4.0f, 2.0f, 0.0f, 372, 289); /* (gpu 0: not known, keeps the last average) */
	overlay.render_width = 960;
	overlay.render_height = 540;
	overlay.interpolation = 1;
	overlay_update_view(&overlay);
	CHECK(overlay.view.fps > 44.0f && overlay.view.fps < 46.0f);
	CHECK(overlay.view.low_fps < 31.0f && overlay.view.low_fps > 29.0f); /* the 99th percentile frame is a 33 ms one */
	CHECK(overlay.view.cpu_percent > 114.0f && overlay.view.cpu_percent < 116.0f); /* 25 ms of 22 */
	CHECK(overlay.gpu_average > 7.9f && overlay.gpu_average < 8.1f);
	/* The strip fits the TV and GamePad at their drawing scales and touches the top edge. */
	CHECK(OVERLAY_HEIGHT == 7);
	CHECK(overlay_scale(1920, 3) == 3);
	CHECK(overlay_scale(854, 2) == 2);
	CHECK(overlay_scale(720, 3) == 2);
	CHECK(overlay_scale(522, 3) == 1);
	CHECK(OVERLAY_WIDTH * 3 + 1920 * 2 / 100 <= 1920 && OVERLAY_HEIGHT * 3 <= 1080);
	CHECK(OVERLAY_WIDTH * 2 + 854 * 2 / 100 <= 854 && OVERLAY_HEIGHT * 2 <= 480);
	memset(&guarded, 0x5a, sizeof(guarded));
	overlay_render(&overlay, pixels);
	for (i = 0; i < sizeof(guarded.before); i++)
		CHECK(guarded.before[i] == 0x5a && guarded.after[i] == 0x5a);
	{
		unsigned lit = 0, ground = 0, bars = 0;

		for (i = 0; i < OVERLAY_WIDTH * OVERLAY_HEIGHT; i++)
		{
			const unsigned alpha = pixels[i * 4 + 3];

			if (alpha == 0)
			{
				printf("FAIL a pixel with no alpha at %u\n", i);
				failures++;
				break;
			}
			if (alpha == 255 && i % OVERLAY_WIDTH < 220)
				lit++;
			if (alpha == 0xb8)
				ground++;
			if (alpha == 255 && i % OVERLAY_WIDTH >= 220)
				bars++;
		}
		CHECK(lit > 150 && ground > 800 && bars > 20);
	}
	/* Extreme field values remain in their allotted space and a recent hitch
	appears as a red bar at the right of the history graph. */
	{
		struct overlay extreme;
		static uint8_t other[OVERLAY_WIDTH * OVERLAY_HEIGHT * 4];
		unsigned newest = (5 * OVERLAY_WIDTH + OVERLAY_WIDTH - 2) * 4;

		overlay_reset(&extreme);
		overlay_push(&extreme, 120.0f, 200.0f, 0.0f, 0.0f, 200.0f, 100000, 0);
		extreme.render_width = 100000;
		extreme.render_height = 100000;
		overlay_update_view(&extreme);
		overlay_render(&extreme, other);
		CHECK(other[newest] == 0xe8 && other[newest + 1] == 0x48 && other[newest + 2] == 0x40);
		CHECK(other[(OVERLAY_WIDTH - 1) * 4 + 3] == 0xb8);
	}
	/* An unavailable GPU timestamp has a distinct indicator from a low,
	but valid, GPU elapsed interval. */
	{
		struct overlay empty;
		static uint8_t absent[OVERLAY_WIDTH * OVERLAY_HEIGHT * 4];
		static uint8_t present[OVERLAY_WIDTH * OVERLAY_HEIGHT * 4];
		int differs = 0;

		overlay_reset(&empty);
		overlay_push(&empty, 16.667f, 10.0f, 0, 0, 0, 0, 0);
		overlay_update_view(&empty);
		CHECK(empty.view.gpu_ms == 0.0f);
		overlay_render(&empty, absent);
		overlay_push(&empty, 16.667f, 10.0f, 0, 0, 0.01f, 0, 0);
		overlay_update_view(&empty);
		CHECK(empty.view.gpu_ms > 0.0f);
		overlay_render(&empty, present);
		for (i = 0; i < OVERLAY_HEIGHT; i++)
			if (memcmp(absent + (i * OVERLAY_WIDTH + 111) * 4,
				present + (i * OVERLAY_WIDTH + 111) * 4, 20 * 4) != 0)
				differs = 1;
		CHECK(differs);
	}
	/* the panel is the same size whatever the numbers: the rendering of two very different views touches the same rows and columns of the ground */
	{
		static uint8_t other[OVERLAY_WIDTH * OVERLAY_HEIGHT * 4];
		struct overlay empty;

		overlay_reset(&empty);
		overlay_update_view(&empty);
		overlay_render(&empty, other);
		for (i = 0; i < OVERLAY_WIDTH * OVERLAY_HEIGHT; i++)
			if ((pixels[i * 4 + 3] != 0) != (other[i * 4 + 3] != 0))
			{
				printf("FAIL the panels differ in size\n");
				failures++;
				break;
			}
	}
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
