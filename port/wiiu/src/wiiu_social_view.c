/* wiiu_social_view.c: the GamePad's social view (wiiu_social_view.h): its places, where a finger lands, the quads that draw it, and the view as the game's thread publishes it. */

#include "wiiu_social_view.h"

#include <string.h>

/* ---------- places */

static void rectangle_set(struct wk_rect *rectangle, float x0, float y0, float x1, float y1)
{
	rectangle->x0 = x0;
	rectangle->y0 = y0;
	rectangle->x1 = x1;
	rectangle->y1 = y1;
}

void wiiu_social_tab_rect(int tab, struct wk_rect *rectangle)
{
	rectangle_set(rectangle, (float)(WSV_PANE_LEFT + tab * WSV_TAB_PITCH), (float)WSV_PANE_TOP, (float)(WSV_PANE_LEFT + tab * WSV_TAB_PITCH + WSV_TAB_WIDTH),
		(float)(WSV_PANE_TOP + WSV_TAB_HEIGHT));
}

void wiiu_social_row_rect(int row, struct wk_rect *rectangle)
{
	rectangle_set(rectangle, (float)WSV_PANE_LEFT, (float)(WSV_ROWS_TOP + row * WSV_ROW_PITCH), (float)(WSV_PANE_LEFT + WSV_PANE_WIDTH),
		(float)(WSV_ROWS_TOP + row * WSV_ROW_PITCH + WSV_ROW_HEIGHT));
}

void wiiu_social_row_button_rect(int row, struct wk_rect *rectangle)
{
	wiiu_social_row_rect(row, rectangle);
	rectangle->x0 = (float)WSV_ROW_BUTTON_LEFT;
}

void wiiu_social_tile_rect(int tile, struct wk_rect *rectangle)
{
	rectangle_set(rectangle, (float)(WSV_TILE_LEFT + tile * WSV_TILE_PITCH), (float)WSV_TILES_TOP, (float)(WSV_TILE_LEFT + tile * WSV_TILE_PITCH + WSV_TILE_WIDTH),
		(float)(WSV_TILES_TOP + WSV_TILE_HEIGHT));
}

void wiiu_social_keyboard_rect(struct wk_rect *rectangle)
{
	rectangle_set(rectangle, (float)WSV_KEYBOARD_LEFT, (float)WSV_TILES_TOP, (float)(WSV_KEYBOARD_LEFT + WSV_KEYBOARD_WIDTH), (float)(WSV_TILES_TOP + WSV_TILE_HEIGHT));
}

void wiiu_social_page_rect(int next, struct wk_rect *rectangle)
{
	rectangle_set(rectangle, (float)(WSV_PAGE_LEFT + next * WSV_PAGE_PITCH), (float)WSV_TILES_TOP, (float)(WSV_PAGE_LEFT + next * WSV_PAGE_PITCH + WSV_PAGE_WIDTH),
		(float)(WSV_TILES_TOP + WSV_TILE_HEIGHT));
}

void wiiu_social_bots_rect(int line, int plus, struct wk_rect *rectangle)
{
	const float left = plus ? 764.0f : 692.0f;

	rectangle_set(rectangle, left, (float)(WSV_ROWS_TOP + line * WSV_ROW_PITCH), left + (float)WSV_BOTS_BUTTON_WIDTH, (float)(WSV_ROWS_TOP + line * WSV_ROW_PITCH + WSV_ROW_HEIGHT));
}

void wiiu_social_toast_rect(int later, struct wk_rect *rectangle)
{
	rectangle_set(rectangle, later ? 724.0f : 600.0f, (float)(WSV_TOAST_TOP + 4), later ? 832.0f : 716.0f, (float)(WSV_TOAST_TOP + 4 + WSV_MINIMUM_TOUCH));
}

static int inside(const struct wk_rect *rectangle, float x, float y)
{
	return x >= rectangle->x0 && x < rectangle->x1 && y >= rectangle->y0 && y < rectangle->y1;
}

int wiiu_social_view_hit(const struct halo_social_view *view, float x, float y, struct halo_social_event *event)
{
	struct wk_rect rectangle;
	int index;

	event->kind = HALO_SOCIAL_EVENT_NONE;
	event->index = 0;
	if (!view->active)
		return event->kind;
	/* (an invitation's buttons are above everything else) */
	if (view->toast_alpha > 0 && view->toast_invite)
	{
		for (index = 0; index < 2; index++)
		{
			wiiu_social_toast_rect(index, &rectangle);
			if (inside(&rectangle, x, y))
			{
				event->kind = index ? HALO_SOCIAL_EVENT_TOAST_LATER : HALO_SOCIAL_EVENT_TOAST_JOIN;
				return event->kind;
			}
		}
	}
	for (index = 0; index < HALO_SOCIAL_TABS; index++)
	{
		wiiu_social_tab_rect(index, &rectangle);
		if (inside(&rectangle, x, y))
		{
			event->kind = HALO_SOCIAL_EVENT_TAB;
			event->index = index;
			return event->kind;
		}
	}
	if (view->tab == HALO_SOCIAL_TAB_BOTS)
	{
		for (index = 0; index < 4 && view->bots_host; index++)
		{
			wiiu_social_bots_rect(index / 2, index % 2, &rectangle);
			if (inside(&rectangle, x, y))
			{
				event->kind = index % 2 ? HALO_SOCIAL_EVENT_BOTS_PLUS : HALO_SOCIAL_EVENT_BOTS_MINUS;
				event->index = index / 2;
				return event->kind;
			}
		}
	}
	else
	{
		for (index = 0; index < view->row_count && index < HALO_SOCIAL_ROWS; index++)
		{
			wiiu_social_row_button_rect(index, &rectangle);
			if (view->rows[index].action != HALO_SOCIAL_ACTION_NONE && inside(&rectangle, x, y))
			{
				event->kind = HALO_SOCIAL_EVENT_ROW;
				event->index = index;
				return event->kind;
			}
		}
	}
	for (index = 0; index < HALO_SOCIAL_TILES; index++)
	{
		wiiu_social_tile_rect(index, &rectangle);
		if (view->tiles[index][0] && inside(&rectangle, x, y))
		{
			event->kind = HALO_SOCIAL_EVENT_TILE;
			event->index = index;
			return event->kind;
		}
	}
	wiiu_social_keyboard_rect(&rectangle);
	if (inside(&rectangle, x, y))
	{
		event->kind = HALO_SOCIAL_EVENT_KEYBOARD;
		return event->kind;
	}
	for (index = 0; index < 2; index++)
	{
		wiiu_social_page_rect(index, &rectangle);
		if (view->page[0] && inside(&rectangle, x, y))
		{
			event->kind = index ? HALO_SOCIAL_EVENT_PAGE_NEXT : HALO_SOCIAL_EVENT_PAGE_PREVIOUS;
			return event->kind;
		}
	}
	return event->kind;
}

/* ---------- the quads */

struct builder
{
	const struct wk_atlas *atlas;
	struct wk_quad *quads;
	int count, maximum;
};

static void quad_add(struct builder *b, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const float *color)
{
	struct wk_quad *q;

	if (b->count >= b->maximum)
		return;
	q = &b->quads[b->count++];
	q->x0 = x0;
	q->y0 = y0;
	q->x1 = x1;
	q->y1 = y1;
	q->u0 = u0;
	q->v0 = v0;
	q->u1 = u1;
	q->v1 = v1;
	q->r = color[0];
	q->g = color[1];
	q->b = color[2];
	q->a = color[3];
}

static void rect_add(struct builder *b, float x0, float y0, float x1, float y1, const float *color)
{
	quad_add(b, x0, y0, x1, y1, b->atlas->solid_u, b->atlas->solid_v, b->atlas->solid_u, b->atlas->solid_v, color);
}

static void rect_add_r(struct builder *b, const struct wk_rect *rectangle, const float *color)
{
	rect_add(b, rectangle->x0, rectangle->y0, rectangle->x1, rectangle->y1, color);
}

/* a rectangle with a one pixel edge in another colour */
static void frame_add(struct builder *b, const struct wk_rect *rectangle, const float *fill, const float *edge)
{
	rect_add_r(b, rectangle, edge);
	rect_add(b, rectangle->x0 + 1.0f, rectangle->y0 + 1.0f, rectangle->x1 - 1.0f, rectangle->y1 - 1.0f, fill);
}

static const struct wk_glyph *glyph_of(const struct wk_atlas *atlas, int font, char character)
{
	int index = (unsigned char)character - WK_GLYPH_FIRST;

	if (index < 0 || index >= WK_GLYPHS || !atlas->glyphs[font][index].present)
		index = '?' - WK_GLYPH_FIRST;
	return &atlas->glyphs[font][index];
}

int wiiu_social_fit(const struct wk_atlas *atlas, int font, float scale, float width, const char *text, char *out, int size)
{
	int length = 0;
	float used = 0.0f, dots;
	int index;

	if (size <= 0)
		return 0;
	out[0] = 0;
	if (wiiu_keyboard_text_width(atlas, font, scale, text, (int)strlen(text)) <= width)
	{
		strncpy(out, text, (size_t)size - 1);
		out[size - 1] = 0;
		return (int)strlen(out);
	}
	dots = wiiu_keyboard_text_width(atlas, font, scale, "...", 3);
	for (index = 0; text[index] && length < size - 4; index++)
	{
		const float next = used + glyph_of(atlas, font, text[index])->advance * scale;

		if (next + dots > width)
			break;
		used = next;
		length++;
	}
	while (length > 0 && text[length - 1] == ' ')
		length--;
	memcpy(out, text, (size_t)length);
	if (width < dots)
		length = 0;
	memcpy(out + length, "...", 4);
	return length + 3;
}

/* the text from x on the baseline */
static float text_add(struct builder *b, int font, float scale, float x, float baseline, const char *text, const float *color)
{
	int index;

	for (index = 0; text[index]; index++)
	{
		const struct wk_glyph *glyph = glyph_of(b->atlas, font, text[index]);

		if (glyph->right > glyph->left)
			quad_add(b, x + glyph->left * scale, baseline + glyph->top * scale, x + glyph->right * scale, baseline + glyph->bottom * scale, glyph->u0, glyph->v0, glyph->u1, glyph->v1, color);
		x += glyph->advance * scale;
	}
	return x;
}

enum
{
	ALIGN_LEFT,
	ALIGN_CENTER,
	ALIGN_RIGHT,
};

/* a text in a box (x0 to x1) on a baseline, cut to fit, aligned */
static void text_box(struct builder *b, int font, float scale, float x0, float x1, float baseline, const char *text, int align, const float *color)
{
	char fitted[HALO_SOCIAL_TEXT + 8];
	float width;

	wiiu_social_fit(b->atlas, font, scale, x1 - x0, text, fitted, (int)sizeof(fitted));
	width = wiiu_keyboard_text_width(b->atlas, font, scale, fitted, (int)strlen(fitted));
	text_add(b, font, scale, align == ALIGN_CENTER ? (x0 + x1 - width) * 0.5f : align == ALIGN_RIGHT ? x1 - width : x0, baseline, fitted, color);
}

/* a text centred in a rectangle at the largest of three sizes (in pixels of the em) that fits it: a quick chat line is up to twelve characters, in a tile 126 wide */
static void text_in_fit(struct builder *b, int font, float pixels, const struct wk_rect *rectangle, const char *text, const float *color)
{
	const float em = font == WK_FONT_LABEL ? WK_LABEL_PIXELS : WK_TEXT_PIXELS;
	float size = pixels;
	int step;

	for (step = 0; step < 3; step++, size -= 3.0f)
	{
		if (wiiu_keyboard_text_width(b->atlas, font, size / em, text, (int)strlen(text)) <= rectangle->x1 - rectangle->x0 - 8.0f)
			break;
	}
	{
		const float scale = size / em;

		text_box(b, font, scale, rectangle->x0 + 4.0f, rectangle->x1 - 4.0f, (rectangle->y0 + rectangle->y1) * 0.5f + 0.35f * em * scale, text, ALIGN_CENTER, color);
	}
}

/* a text centred in a rectangle: the baseline a third of an em below the middle (the keyboard's rule) */
static void text_in(struct builder *b, int font, float scale, const struct wk_rect *rectangle, const char *text, const float *color)
{
	const float pixels = font == WK_FONT_LABEL ? WK_LABEL_PIXELS : WK_TEXT_PIXELS;

	text_box(b, font, scale, rectangle->x0 + 4.0f, rectangle->x1 - 4.0f, (rectangle->y0 + rectangle->y1) * 0.5f + 0.35f * pixels * scale, text, ALIGN_CENTER, color);
}

static void rgb_color(unsigned long rgb, float *color)
{
	color[0] = (float)((rgb >> 16) & 0xFF) / 255.0f;
	color[1] = (float)((rgb >> 8) & 0xFF) / 255.0f;
	color[2] = (float)(rgb & 0xFF) / 255.0f;
	color[3] = 1.0f;
}

/* the sizes of the texts, in pixels of the font's em */
#define NAME_PIXELS 22.0f
#define TEXT_PIXELS 20.0f
#define SMALL_PIXELS 16.0f
#define HEAD_PIXELS 18.0f

static const float color_background[4] = { 0.016f, 0.031f, 0.071f, 1.0f };
static const float color_header[4] = { 0.035f, 0.090f, 0.180f, 1.0f };
static const float color_pane[4] = { 0.030f, 0.060f, 0.120f, 1.0f };
static const float color_row[4] = { 0.060f, 0.100f, 0.170f, 1.0f };
static const float color_tile[4] = { 0.157f, 0.220f, 0.282f, 1.0f };
static const float color_accent[4] = { 0.157f, 0.588f, 1.0f, 1.0f };
static const float color_edge[4] = { 0.157f, 0.588f, 1.0f, 0.55f };
static const float color_white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float color_pale[4] = { 0.64f, 0.75f, 0.87f, 1.0f };
static const float color_dark[4] = { 0.02f, 0.05f, 0.10f, 1.0f };
static const float color_dim[4] = { 0.44f, 0.51f, 0.60f, 1.0f };
static const float color_red[4] = { 0.86f, 0.32f, 0.28f, 1.0f };
static const float color_blue[4] = { 0.34f, 0.51f, 0.86f, 1.0f };
static const float color_gold[4] = { 0.93f, 0.82f, 0.45f, 1.0f };
static const float color_button[4] = { 0.08f, 0.26f, 0.50f, 1.0f };

static const char *const tab_names[HALO_SOCIAL_TABS] = { "FRIENDS", "PARTY", "BOTS" };

int wiiu_social_view_build(const struct halo_social_view *view, const struct wk_atlas *atlas, struct wk_quad *quads, int maximum)
{
	struct builder b;
	struct wk_rect rectangle;
	int index;
	const float label_scale = NAME_PIXELS / WK_LABEL_PIXELS;
	const float text_scale = TEXT_PIXELS / WK_TEXT_PIXELS;
	const float small_scale = SMALL_PIXELS / WK_TEXT_PIXELS;
	const float head_scale = HEAD_PIXELS / WK_LABEL_PIXELS;

	b.atlas = atlas;
	b.quads = quads;
	b.count = 0;
	b.maximum = maximum;
	if (!view->active || !atlas || !atlas->ready)
		return 0;
	rect_add(&b, 0.0f, 0.0f, (float)WSV_WIDTH, (float)WSV_HEIGHT, color_background);
	/* the header */
	rect_add(&b, 0.0f, 0.0f, (float)WSV_WIDTH, (float)WSV_HEADER_HEIGHT, color_header);
	rect_add(&b, 0.0f, (float)WSV_HEADER_HEIGHT - 1.0f, (float)WSV_WIDTH, (float)WSV_HEADER_HEIGHT, color_edge);
	text_box(&b, WK_FONT_LABEL, head_scale, 16.0f, 560.0f, 24.0f, view->head, ALIGN_LEFT, color_white);
	text_box(&b, WK_FONT_LABEL, head_scale, 570.0f, 838.0f, 24.0f, view->party, ALIGN_RIGHT, color_accent);
	/* the chat */
	rectangle.x0 = (float)WSV_CHAT_LEFT;
	rectangle.y0 = (float)WSV_PANE_TOP;
	rectangle.x1 = (float)(WSV_CHAT_LEFT + WSV_CHAT_WIDTH);
	rectangle.y1 = (float)(WSV_PANE_TOP + HALO_SOCIAL_CHAT_LINES * WSV_CHAT_PITCH);
	rect_add_r(&b, &rectangle, color_pane);
	for (index = 0; index < view->chat_count && index < HALO_SOCIAL_CHAT_LINES; index++)
	{
		const struct halo_social_line *line = &view->chat[index];
		const float *name_color = line->team == 0 ? color_red : line->team == 1 ? color_blue : line->team == 2 ? color_accent : color_pale;
		const float top = (float)(WSV_PANE_TOP + index * WSV_CHAT_PITCH);
		char name[16];
		float width;

		wiiu_social_fit(atlas, WK_FONT_LABEL, label_scale, 150.0f, line->name, name, (int)sizeof(name));
		width = wiiu_keyboard_text_width(atlas, WK_FONT_LABEL, label_scale, name, (int)strlen(name));
		text_add(&b, WK_FONT_LABEL, label_scale, rectangle.x0 + 10.0f, top + 27.0f, name, name_color);
		text_add(&b, WK_FONT_TEXT, text_scale, rectangle.x0 + 10.0f + width, top + 27.0f, ":", name_color);
		text_box(&b, WK_FONT_TEXT, text_scale, rectangle.x0 + 20.0f + width, rectangle.x1 - 8.0f, top + 27.0f, line->text, ALIGN_LEFT, color_pale);
	}
	if (!view->chat_count)
		text_box(&b, WK_FONT_TEXT, small_scale, rectangle.x0 + 10.0f, rectangle.x1 - 8.0f, rectangle.y0 + 30.0f, "NOTHING SAID YET.", ALIGN_LEFT, color_dim);
	/* the tabs and the pane */
	rectangle.x0 = (float)WSV_PANE_LEFT;
	rectangle.y0 = (float)(WSV_PANE_TOP + WSV_TAB_HEIGHT + 2);
	rectangle.x1 = (float)(WSV_PANE_LEFT + WSV_PANE_WIDTH);
	rectangle.y1 = (float)(WSV_ROWS_TOP + HALO_SOCIAL_ROWS * WSV_ROW_PITCH);
	rect_add_r(&b, &rectangle, color_pane);
	for (index = 0; index < HALO_SOCIAL_TABS; index++)
	{
		wiiu_social_tab_rect(index, &rectangle);
		if (index == view->tab)
		{
			rect_add_r(&b, &rectangle, color_accent);
			text_in(&b, WK_FONT_LABEL, head_scale, &rectangle, view->tabs[index][0] ? view->tabs[index] : tab_names[index], color_dark);
		}
		else
		{
			frame_add(&b, &rectangle, color_tile, color_edge);
			text_in(&b, WK_FONT_LABEL, head_scale, &rectangle, view->tabs[index][0] ? view->tabs[index] : tab_names[index], color_white);
		}
	}
	if (view->tab == HALO_SOCIAL_TAB_BOTS)
	{
		for (index = 0; index < 2; index++)
		{
			wiiu_social_row_rect(index, &rectangle);
			rect_add_r(&b, &rectangle, color_row);
			text_box(&b, WK_FONT_LABEL, label_scale, rectangle.x0 + 12.0f, view->bots_host ? 684.0f : rectangle.x1 - 8.0f, rectangle.y0 + 30.0f, view->bots[index], ALIGN_LEFT, color_white);
			if (view->bots_host)
			{
				int sign;

				for (sign = 0; sign < 2; sign++)
				{
					wiiu_social_bots_rect(index, sign, &rectangle);
					rectangle.y0 += 2.0f;
					rectangle.y1 -= 2.0f;
					rectangle.x0 += 2.0f;
					rectangle.x1 -= 2.0f;
					rect_add_r(&b, &rectangle, color_button);
					text_in(&b, WK_FONT_LABEL, 30.0f / WK_LABEL_PIXELS, &rectangle, sign ? "+" : "-", color_white);
				}
			}
		}
		if (!view->bots_host)
			text_box(&b, WK_FONT_TEXT, small_scale, (float)WSV_PANE_LEFT + 12.0f, (float)(WSV_PANE_LEFT + WSV_PANE_WIDTH) - 12.0f, (float)(WSV_ROWS_TOP + 2 * WSV_ROW_PITCH + 30),
				"ONLY THE HOST CHANGES THE BOTS.", ALIGN_CENTER, color_pale);
	}
	else
	{
		for (index = 0; index < view->row_count && index < HALO_SOCIAL_ROWS; index++)
		{
			const struct halo_social_row *row = &view->rows[index];
			float name_color[4], status_color[4];

			wiiu_social_row_rect(index, &rectangle);
			rect_add_r(&b, &rectangle, color_row);
			rgb_color(row->name_rgb, name_color);
			rgb_color(row->status_rgb, status_color);
			text_box(&b, WK_FONT_LABEL, label_scale, rectangle.x0 + 10.0f, rectangle.x0 + 32.0f, rectangle.y0 + 28.0f, row->mark, ALIGN_CENTER, name_color);
			text_box(&b, WK_FONT_LABEL, label_scale, rectangle.x0 + 38.0f, row->action ? (float)WSV_ROW_BUTTON_LEFT - 6.0f : rectangle.x1 - 8.0f, rectangle.y0 + 22.0f, row->name,
				ALIGN_LEFT, name_color);
			text_box(&b, WK_FONT_TEXT, small_scale, rectangle.x0 + 38.0f, row->action ? (float)WSV_ROW_BUTTON_LEFT - 6.0f : rectangle.x1 - 8.0f, rectangle.y0 + 39.0f, row->status,
				ALIGN_LEFT, status_color);
			if (row->action)
			{
				wiiu_social_row_button_rect(index, &rectangle);
				rectangle.x0 += 4.0f;
				rectangle.x1 -= 6.0f;
				rectangle.y0 += 4.0f;
				rectangle.y1 -= 4.0f;
				rect_add_r(&b, &rectangle, color_button);
				text_in(&b, WK_FONT_LABEL, 16.0f / WK_LABEL_PIXELS, &rectangle, row->action == HALO_SOCIAL_ACTION_JOIN ? "JOIN" : "INVITE", color_white);
			}
		}
		/* (an empty list's words in the middle of the pane, centred, as the TV's are in the middle of their list; they were at its top left) */
		if (!view->row_count)
			text_box(&b, WK_FONT_TEXT, small_scale, (float)WSV_PANE_LEFT + 12.0f, (float)(WSV_PANE_LEFT + WSV_PANE_WIDTH) - 12.0f, (float)(WSV_ROWS_TOP + 2 * WSV_ROW_PITCH + 30), view->empty,
				ALIGN_CENTER, color_pale);
	}
	/* the quick chat's lines, the keyboard, the page */
	for (index = 0; index < HALO_SOCIAL_TILES; index++)
	{
		wiiu_social_tile_rect(index, &rectangle);
		if (view->tiles[index][0])
		{
			frame_add(&b, &rectangle, color_tile, color_edge);
			text_in_fit(&b, WK_FONT_TEXT, TEXT_PIXELS, &rectangle, view->tiles[index], color_white);
		}
		else
		{
			rect_add_r(&b, &rectangle, color_row);
		}
	}
	wiiu_social_keyboard_rect(&rectangle);
	rect_add_r(&b, &rectangle, color_accent);
	text_in(&b, WK_FONT_LABEL, label_scale, &rectangle, "KEYBOARD", color_dark);
	for (index = 0; index < 2 && view->page[0]; index++)
	{
		wiiu_social_page_rect(index, &rectangle);
		frame_add(&b, &rectangle, color_tile, color_edge);
		text_in(&b, WK_FONT_LABEL, 28.0f / WK_LABEL_PIXELS, &rectangle, index ? ">" : "<", color_white);
	}
	if (view->page[0])
	{
		char line[48];

		line[0] = 0;
		strncpy(line, "QUICK CHAT: ", sizeof(line) - 1);
		strncat(line, view->page, sizeof(line) - strlen(line) - 1);
		text_box(&b, WK_FONT_LABEL, head_scale, 16.0f, 700.0f, 430.0f, line, ALIGN_LEFT, color_gold);
	}
	text_box(&b, WK_FONT_TEXT, 14.0f / WK_TEXT_PIXELS, 16.0f, 838.0f, 466.0f, "TAP A LINE TO SAY IT. THE GAME IS ON THE TV.", ALIGN_LEFT, color_dim);
	/* a toast: a card across the top, with an invitation's two buttons */
	if (view->toast_alpha > 0)
	{
		float card[4], text[4], dim[4];
		const float alpha = (float)view->toast_alpha / 255.0f;

		memcpy(card, color_header, sizeof(card));
		card[3] = alpha;
		memcpy(text, color_white, sizeof(text));
		text[3] = alpha;
		memcpy(dim, color_pale, sizeof(dim));
		dim[3] = alpha;
		rectangle.x0 = 16.0f;
		rectangle.y0 = (float)WSV_TOAST_TOP;
		rectangle.x1 = 838.0f;
		rectangle.y1 = (float)(WSV_TOAST_TOP + WSV_TOAST_HEIGHT);
		rect_add_r(&b, &rectangle, card);
		rect_add(&b, rectangle.x0, rectangle.y0, rectangle.x0 + 4.0f, rectangle.y1, color_accent);
		text_box(&b, WK_FONT_LABEL, label_scale, 28.0f, view->toast_invite ? 590.0f : 828.0f, (float)(WSV_TOAST_TOP + 22), view->toast, ALIGN_LEFT, text);
		text_box(&b, WK_FONT_TEXT, small_scale, 28.0f, view->toast_invite ? 590.0f : 828.0f, (float)(WSV_TOAST_TOP + 43), view->toast_detail, ALIGN_LEFT, dim);
		if (view->toast_invite)
		{
			for (index = 0; index < 2; index++)
			{
				wiiu_social_toast_rect(index, &rectangle);
				rect_add_r(&b, &rectangle, index ? color_tile : color_accent);
				text_in(&b, WK_FONT_LABEL, label_scale, &rectangle, index ? "LATER" : "JOIN", index ? color_white : color_dark);
			}
		}
	}
	return b.count;
}

/* ---------- publishing and the events */

static struct
{
	volatile unsigned int sequence;
	struct halo_social_view view;
} published;

void wiiu_social_view_publish(const struct halo_social_view *view)
{
	if (!memcmp(&published.view, view, sizeof(*view)))
		return;
	published.sequence++;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	memcpy(&published.view, view, sizeof(*view));
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	published.sequence++;
}

void wiiu_social_view_snapshot(struct halo_social_view *view)
{
	int tries;

	for (tries = 0; tries < 16; tries++)
	{
		const unsigned int before = published.sequence;

		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		memcpy(view, &published.view, sizeof(*view));
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		if (!(before & 1u) && before == published.sequence)
			return;
	}
	memset(view, 0, sizeof(*view));
}

static struct
{
	struct halo_social_event events[8];
	int read, written;
} queue;

void wiiu_social_event_push(const struct halo_social_event *event)
{
	if (queue.written - queue.read >= 8)
		return;
	queue.events[queue.written % 8] = *event;
	queue.written++;
}

int wiiu_social_event_pop(struct halo_social_event *event)
{
	if (queue.read == queue.written)
		return 0;
	*event = queue.events[queue.read % 8];
	queue.read++;
	return 1;
}
