/* wiiu_hud_sheet.h: a sheet drawn from a smaller copy (the meters' and arrows' top rows, the scope masks at half the size) is found at the same place by the
game's coordinates with the copy's v scale. A synthetic 2048 x 2048 sheet with content in its top rows and the heights tools/wiiu_hud_sheets.py keeps, sampled as
the pixel shader does it (nv2a_psh.c: coordinate times the texture's scale, times the image's size, floor; clamped to the image, as the sampler's clamp does). */

#include "../src/wiiu_hud_sheet.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

#define SHEET 2048u

/* a texel of the synthetic sheet: content (never 0) in the rows above content_rows, nothing under them */
static uint32_t sheet_texel(unsigned x, unsigned y, unsigned content_rows)
{
	return y < content_rows ? (uint32_t)(x * 2654435761u + y * 40503u) | 1u : 0u;
}

/* the row of an image of this height that the shader reads for the game's v (the floor of v times the scale times the height, clamped to the image) */
static unsigned row_read(float v, float scale, unsigned height)
{
	const float row = floorf(wiiu_hud_sheet_v(v, scale) * (float)height);

	return row < 0.0f ? 0u : row >= (float)height ? height - 1u : (unsigned)row;
}

/* every row of the sheet's v range, at the centre of each of the sheet's rows, at the top edge of each, and just above the next (the draw's sprite edges are on
the texel boundaries): the copy gives the texel the sheet gives, for the rows the copy holds, and the clamped edge row for those under it */
static void crop_matches(unsigned kept, unsigned content_rows)
{
	const float scale = wiiu_hud_sheet_v_scale(SHEET, SHEET, SHEET, kept);
	unsigned y, x;
	unsigned wrong_centre = 0, wrong_edge = 0, wrong_below = 0;

	for (y = 0; y < SHEET; y++)
	{
		const float centre = ((float)y + 0.5f) / (float)SHEET, edge = (float)y / (float)SHEET;
		const unsigned copy_centre = row_read(centre, scale, kept), copy_edge = row_read(edge, scale, kept);

		/* (the sheet reads row y at both; the copy has to read row y while it has the row, and a row with nothing in it under) */
		if (y < kept)
		{
			if (copy_centre != y)
				wrong_centre++;
			if (copy_edge != y)
				wrong_edge++;
		}
		else if (copy_centre != kept - 1 || copy_edge != kept - 1)
			wrong_below++;
		for (x = 0; x < SHEET; x += 257)
		{
			const uint32_t want = sheet_texel(x, y, content_rows), got = sheet_texel(x, copy_centre, content_rows);

			if (want != got)
				wrong_centre++;
		}
	}
	CHECK(wrong_centre == 0);
	CHECK(wrong_edge == 0);
	CHECK(wrong_below == 0);
}

int main(void)
{
	/* the kept heights of the five sheets cropped (the tool's: tools/wiiu_hud_sheets.py, port/assets/wiiu/sheets.json), with the rows holding content in them */
	crop_matches(768, 664);  /* hud_ammo_meters__0 (sprites to 704) */
	crop_matches(768, 688);  /* hud_unit_meters__0 */
	crop_matches(576, 545);  /* hud_damage_arrows__0 */
	crop_matches(704, 648);  /* ce_hud_ammo_meters */
	crop_matches(1088, 1048); /* ce_hud_unit_meters */
	/* every multiple of 64 up to 1536 (the heights 1600, 1728, 1856 and 1984 round the scale down, so that a texel's top edge is read one row up: the tool does not
	keep them, edges_exact in tools/wiiu_hud_sheets.py, whose test has this C program's numbers) */
	{
		unsigned kept;

		for (kept = 64; kept <= 1536; kept += 64)
			crop_matches(kept, kept - 1);
	}

	/* the scale itself */
	CHECK(wiiu_hud_sheet_v_scale(2048, 2048, 2048, 2048) == 1.0f);
	CHECK(wiiu_hud_sheet_v_scale(2048, 2048, 1024, 1024) == 1.0f);                 /* a mask at half the size: the share of it is the same */
	CHECK(wiiu_hud_sheet_v_scale(2048, 2048, 2048, 1024) == 2.0f);
	CHECK(wiiu_hud_sheet_v_scale(2048, 2048, 2048, 768) == 2048.0f / 768.0f);
	CHECK(fabsf(wiiu_hud_sheet_v_scale(2048, 2048, 1024, 384) - 2048.0f / 768.0f) < 1e-6f); /* a crop that is also halved */
	CHECK(wiiu_hud_sheet_v_scale(2048, 1024, 2048, 1024) == 1.0f);                 /* a sheet that is not square, drawn whole */
	CHECK(wiiu_hud_sheet_v_scale(0, 2048, 2048, 768) == 1.0f && wiiu_hud_sheet_v_scale(2048, 2048, 2048, 0) == 1.0f);

	/* what may stand for a sheet */
	CHECK(wiiu_hud_sheet_fits(2048, 2048, 2048, 768));
	CHECK(wiiu_hud_sheet_fits(2048, 2048, 1024, 1024));
	CHECK(wiiu_hud_sheet_fits(2048, 2048, 1024, 384));
	CHECK(wiiu_hud_sheet_fits(2048, 2048, 2048, 2048));
	CHECK(!wiiu_hud_sheet_fits(2048, 2048, 2048, 2112));  /* taller than the sheet */
	CHECK(!wiiu_hud_sheet_fits(2048, 2048, 1024, 1088));  /* (halved) taller than half */
	CHECK(!wiiu_hud_sheet_fits(2048, 2048, 1000, 1000));  /* not a whole divisor of the width */
	CHECK(!wiiu_hud_sheet_fits(2048, 2048, 0, 100));

	/* when a sheet is made, and by whom: every combination of what the cache knows */
	{
		int async_on, big, ahead, asked, done;

		for (async_on = 0; async_on < 2; async_on++)
			for (big = 0; big < 2; big++)
				for (ahead = WIIU_HUD_AHEAD_NONE; ahead <= WIIU_HUD_AHEAD_READY; ahead++)
					for (asked = 0; asked < 2; asked++)
						for (done = 0; done < 2; done++)
						{
							const int got = wiiu_hud_sheet_next(async_on, big, ahead, asked, done);

							if (!async_on || !big)
								CHECK(got == WIIU_HUD_SHEET_MAKE_HERE); /* a small sheet, or the builder off: as it always was */
							else if (ahead == WIIU_HUD_AHEAD_READY)
								CHECK(got == WIIU_HUD_SHEET_TAKE_AHEAD); /* (whether or not it was asked for: the menus' or a hud_prewarm.txt's too) */
							else if (ahead == WIIU_HUD_AHEAD_WORKING)
								CHECK(got == WIIU_HUD_SHEET_WAIT);
							else if (!asked)
								CHECK(got == WIIU_HUD_SHEET_ASK);
							else
								CHECK(got == (done ? WIIU_HUD_SHEET_MAKE_HERE : WIIU_HUD_SHEET_WAIT));
						}
	}
	/* a life: asked at the first lookup, drawn from the game's bitmap while the builder has it, taken when ready; and a builder that is refused */
	{
		int asked = 0, done = 0, ahead = WIIU_HUD_AHEAD_NONE, drawn_from_game_bitmap = 0, step;

		CHECK(wiiu_hud_sheet_next(1, 1, ahead, asked, done) == WIIU_HUD_SHEET_ASK);
		asked = 1; /* queued */
		for (step = 0; step < 3; step++)
		{
			CHECK(wiiu_hud_sheet_next(1, 1, ahead, asked, done) == WIIU_HUD_SHEET_WAIT);
			drawn_from_game_bitmap++;
		}
		ahead = WIIU_HUD_AHEAD_WORKING; /* the builder has it */
		CHECK(wiiu_hud_sheet_next(1, 1, ahead, asked, done) == WIIU_HUD_SHEET_WAIT);
		ahead = WIIU_HUD_AHEAD_READY;
		done = 1;
		CHECK(wiiu_hud_sheet_next(1, 1, ahead, asked, done) == WIIU_HUD_SHEET_TAKE_AHEAD);
		CHECK(drawn_from_game_bitmap == 3);
		/* refused (no memory for its levels): the builder is done and holds nothing, the game thread makes it, once */
		ahead = WIIU_HUD_AHEAD_NONE;
		CHECK(wiiu_hud_sheet_next(1, 1, ahead, 1, 1) == WIIU_HUD_SHEET_MAKE_HERE);
	}

	printf("%d failures\n", failures);
	return failures ? 1 : 0;
}
