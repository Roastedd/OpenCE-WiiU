/*
WIIU_HUD_SHEET.H

The Wii U draws some of the high-res HUD's largest sheets from smaller copies (port/assets/wiiu, made by tools/wiiu_hud_sheets.py; embedded by
tools/embed_assets.py --wiiu): the meters' and damage arrows' sheets cut after their last used row, the scope masks at half the size. The game places a sprite by
its coordinates in the Xbox bitmap, a share of the bitmap (0 to 1 each way), and the sheet is that bitmap's layout at 8 times the size, so a copy that keeps only
the sheet's top rows needs the v coordinate multiplied by (the sheet's height / the rows kept) to find the same texel; a copy at half the size both ways needs
nothing (the share is the same).

The embedded table keeps the sheet's own size (the bitmap's size times its scale); the copy's size comes from the image once it is decoded, and the multiplier is
the ratio of the two shapes.
*/

#ifndef WIIU_HUD_SHEET_H
#define WIIU_HUD_SHEET_H

/* the factor for v: sheet_height * width / (height * sheet_width); 1 for a copy of the sheet's own shape (also a copy shrunk both ways), and for a size of 0 */
static inline float wiiu_hud_sheet_v_scale(unsigned long sheet_width, unsigned long sheet_height, unsigned long width, unsigned long height)
{
	if (!sheet_width || !sheet_height || !width || !height)
		return 1.0f;
	/* (done in doubles, from whole numbers: 2048 * 2048 / (768 * 2048) is the double nearest 8 / 3, and the float nearest that) */
	return (float)((double)sheet_height * (double)width / ((double)height * (double)sheet_width));
}

/* whether a copy of width x height can stand for the sheet: the same width, or the sheet's divided by a whole number d, and then no more rows than the
sheet's divided by d (a crop of its top rows, shrunk by d), at least one */
static inline int wiiu_hud_sheet_fits(unsigned long sheet_width, unsigned long sheet_height, unsigned long width, unsigned long height)
{
	unsigned long divisor;

	if (!sheet_width || !sheet_height || !width || !height || sheet_width % width)
		return 0;
	divisor = sheet_width / width;
	return height * divisor <= sheet_height;
}

/* the v a sheet's sprite has in the copy (the shader: v times the multiplier the texture's scale holds) */
static inline float wiiu_hud_sheet_v(float v, float scale)
{
	return v * scale;
}

/* ---------- when a sheet is made, and by whom

What the texture cache does at a lookup of a sheet it has not made (xbox_textures_gx2.c, hud_image): the answer of this function from what it knows. A sheet that is
big (a million pixels or more of the sheet, not a menu's picture) and a cache with the builder thread on do not make it on the game thread unless the builder
could not: the first lookup asks for it (the game's own bitmap is drawn), later lookups wait while the builder has it, and the lookup that finds its image made
ahead takes it. A builder that is done and has nothing (it was refused) or that could not be asked leaves the game thread to make it, as it always did. */
enum
{
	WIIU_HUD_SHEET_MAKE_HERE,  /* decode, make the levels and send, on the game thread */
	WIIU_HUD_SHEET_TAKE_AHEAD, /* the builder's image is ready: send it (the copies are the game thread's) */
	WIIU_HUD_SHEET_ASK,        /* ask the builder, draw the game's own bitmap meanwhile (if asking fails: make it here) */
	WIIU_HUD_SHEET_WAIT        /* the game's own bitmap is drawn; the builder has it, or has it asked for */
};
/* the made-ahead image's state: none, being made, ready (the enum of xbox_textures_gx2.c's PREPARED_FREE, PREPARED_WORKING, PREPARED_READY: 0, 1, 2) */
enum { WIIU_HUD_AHEAD_NONE, WIIU_HUD_AHEAD_WORKING, WIIU_HUD_AHEAD_READY };

/* builder_done: 0 while the builder has not finished the sheet it was asked for (queued or working), nonzero when it has (made, or refused) */
static inline int wiiu_hud_sheet_next(int async_on, int big, int ahead, int asked, int builder_done)
{
	if (!async_on || !big)
		return WIIU_HUD_SHEET_MAKE_HERE;
	if (ahead == WIIU_HUD_AHEAD_READY)
		return WIIU_HUD_SHEET_TAKE_AHEAD;
	if (ahead == WIIU_HUD_AHEAD_WORKING)
		return WIIU_HUD_SHEET_WAIT;
	if (!asked)
		return WIIU_HUD_SHEET_ASK;
	return builder_done ? WIIU_HUD_SHEET_MAKE_HERE : WIIU_HUD_SHEET_WAIT;
}

#endif
