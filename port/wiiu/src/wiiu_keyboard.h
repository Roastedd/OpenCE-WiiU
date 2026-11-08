/*
WIIU_KEYBOARD.H

The text chat's touch keyboard for the GamePad: the keys' layout, where a finger lands, the quads that draw it, and the glyph atlas they are cut from. Plain code with no
system calls, so that the harness builds it alone (tools/harness/tests/wiiu_keyboard.c); wiiu_keyboard_font.c rasterizes the atlas on a thread of its own and
wiiu_gx2.c draws the quads on the GamePad after the game's picture (a native layer beside the updater's panel), only while the keyboard is open.

The keyboard is network_chat.c's, published once a frame (halo_chat_keyboard_publish): the same 10 columns and 5 rows, so a key tapped here is the key picked there
(network_chat_keyboard_press), and the line, the channel and the capitals are the chat's. It is drawn over the GamePad's whole 854 x 480 screen, so that its keys are big
(each is 80 x 60 pixels, 12.8 x 9.6 mm on the 6.2 inch screen):

   y   8..52   the channels' chips (EVERYONE, TEAM, VEHICLE), the letters left, CLOSE
   y  60..116  the line, with its prompt and a caret
   y 128..428  five rows of keys, 60 pixels each (the last row's five keys are 160 wide)
   y 440..472  the buttons' words

The TV keeps drawing the chat's own lines, with the line typed, and the controller's keyboard as before.
*/

#ifndef HALO_WIIU_KEYBOARD_H
#define HALO_WIIU_KEYBOARD_H

#include "halo_keyboard.h"

/* the GamePad's picture, in pixels (the touch's units: wiiu_input_touch) */
#define WK_WIDTH 854
#define WK_HEIGHT 480

enum
{
	WK_COLUMNS = HALO_CHAT_KEYBOARD_COLUMNS,
	WK_ROWS = HALO_CHAT_KEYBOARD_ROWS + 1,
	/* the last row's keys: Aa, #+=, space, DEL, SEND */
	WK_WIDE_COLUMNS = 2,
	WK_WIDE_KEYS = 5,
	WK_KEY_WIDTH = 80,
	WK_KEY_HEIGHT = 60,
	WK_KEYS_LEFT = 27,
	WK_KEYS_TOP = 128,
	/* the chips and the line */
	WK_CHIP_TOP = 8,
	WK_CHIP_HEIGHT = 44,
	WK_CHANNELS = 3,
	WK_FIELD_TOP = 60,
	WK_FIELD_HEIGHT = 56,
	WK_HINT_TOP = 440,
	WK_HINT_HEIGHT = 32,
	/* a key's picture is inset from the key by this much on each side (the finger's key is all of the 80 x 60) */
	WK_KEY_INSET = 2,
	/* the characters a line holds (network_chat.c's CHAT_TEXT_SIZE less its end) */
	WK_LINE_CHARACTERS = 63,
};

struct wk_rect
{
	float x0, y0, x1, y1;
};

/* a key's place (row 0 to 4, column 0 to 9; in the last row a column is its wide key's first) */
void wiiu_keyboard_key_rect(int row, int column, struct wk_rect *rectangle);
/* a channel chip's, 0 to WK_CHANNELS - 1 (network_chat.h's _chat_channel_*), and the close chip's */
void wiiu_keyboard_chip_rect(int channel, struct wk_rect *rectangle);
void wiiu_keyboard_close_rect(struct wk_rect *rectangle);

enum
{
	WK_HIT_NONE,
	WK_HIT_KEY,
	WK_HIT_CHANNEL,
	WK_HIT_CLOSE,
};

struct wk_hit
{
	int kind;
	/* WK_HIT_KEY: the key (a wide key's first column); WK_HIT_CHANNEL: channel */
	int row, column, channel;
};

/* what a finger at a place of the GamePad's picture (pixels) touches: a key (all of its 80 x 60 cell), a chip, the close chip, or nothing. Returns the kind. */
int wiiu_keyboard_hit(float x, float y, struct wk_hit *hit);

/* ---------- the glyphs */

enum
{
	/* the keys' letters (ui\large_ui's font) and the line's (ui\small_ui's) */
	WK_FONT_LABEL,
	WK_FONT_TEXT,
	WK_FONTS,
	/* printable ASCII, 0x20 to 0x7E */
	WK_GLYPH_FIRST = 0x20,
	WK_GLYPHS = 95,
	WK_ATLAS_WIDTH = 512,
	WK_ATLAS_HEIGHT = 512,
	/* clear texels around each glyph, so that filtering never reaches the next, and the solid block at the atlas's corner that the rectangles are cut from */
	WK_ATLAS_PADDING = 2,
	WK_SOLID_SIZE = 4,
};

/* the pixels of the font's em the fonts are rasterized at */
#define WK_LABEL_PIXELS 26.0f
#define WK_TEXT_PIXELS 30.0f

struct wk_glyph
{
	/* its texels in the atlas, 0 to 1 */
	float u0, v0, u1, v1;
	/* its quad from the pen on the baseline, in pixels (y down; the top is negative above the baseline) */
	float left, top, right, bottom;
	float advance;
	int present;
};

struct wk_atlas
{
	int ready;
	/* coverage, a byte each; the solid block at the top left is 255 */
	unsigned char pixels[WK_ATLAS_WIDTH * WK_ATLAS_HEIGHT];
	struct wk_glyph glyphs[WK_FONTS][WK_GLYPHS];
	/* above and below the baseline, in pixels, for each font */
	float ascent[WK_FONTS], descent[WK_FONTS];
	/* the middle of the solid block (texture coordinates) */
	float solid_u, solid_v;
};

/* rasterizes one glyph of a font into a buffer of stride bytes a row, at most max_width x max_height: its coverage from the buffer's top left, where it lies from the pen on the
baseline (left, top: top is negative above), its size and the pen's advance, all in pixels; 0 if the font has none (a space has a size of 0 and an advance). code 0 asks for
the font's measures instead: *left is its ascent, *top its descent (pixels, positive), no bitmap. */
typedef int (*wk_raster)(int font, unsigned long code, unsigned char *buffer, int stride, int max_width, int max_height, int *left, int *top, int *width, int *height,
	float *advance);

/* the atlas made: the solid block, then every glyph of each font, packed in rows; atlas->ready when it all fitted. 0 if the rasterizer failed or the glyphs did not fit. */
int wiiu_keyboard_atlas_build(struct wk_atlas *atlas, wk_raster raster);

/* ---------- the quads */

struct wk_quad
{
	float x0, y0, x1, y1;
	float u0, v0, u1, v1;
	float r, g, b, a;
};

/* the most quads a keyboard is */
#define WK_MAXIMUM_QUADS 640

/* The keyboard drawn: the quads, back to front, in pixels of the GamePad's picture. now_ms (any millisecond clock) blinks the caret. 0 quads when the view is closed or the atlas is not ready. */
int wiiu_keyboard_build(const struct halo_chat_keyboard_view *view, const struct wk_atlas *atlas, unsigned long now_ms, struct wk_quad *quads, int maximum);

/* the width of a string in a font at a scale, in pixels (the build's, for the line's tail) */
float wiiu_keyboard_text_width(const struct wk_atlas *atlas, int font, float scale, const char *text, int count);

/* ---------- the keyboard as the game's thread publishes it, and the thread that draws reads it */

/* the game's thread: the keyboard now (halo_chat_keyboard_publish) */
void wiiu_keyboard_publish(const struct halo_chat_keyboard_view *view);
/* the drawing thread, or the game's: the latest published keyboard; always a whole one (never half of one frame's and half of the next's) */
void wiiu_keyboard_snapshot(struct halo_chat_keyboard_view *view);

/* ---------- the console's pieces (wiiu_keyboard_font.c) */

/* input.wiiu_gamepad_keyboard: whether the GamePad shows the keyboard (off: the controller's keyboard on the TV, which the GamePad then mirrors, is all there is) */
int wiiu_keyboard_enabled(void);
/* Rasterizes the atlas on a thread of its own, once, below the game's priority (a network game has started: halo_chat_keyboard_prewarm). Nothing when the keyboard is off. */
void wiiu_keyboard_prewarm(void);
/* the atlas, or NULL while it is not made (or could not be) */
const struct wk_atlas *wiiu_keyboard_atlas(void);
/* whether the GamePad is showing the keyboard now: it is open on the first player's controller, the atlas is made, and the setting is on */
int wiiu_keyboard_shown(void);

#endif
