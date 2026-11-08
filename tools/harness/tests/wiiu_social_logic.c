#include "harness.h"

/* The GamePad's social view (port/wiiu/src/wiiu_social_view.c): every touch target at least 44 pixels each way and inside the screen, none over another (the toast's over the tabs on
purpose), where a finger lands, the quads that draw it (inside the screen, no text out of its place, nothing at all when the view is off), and the published view and the touch events. */
#include "wiiu_social_view.h"
/* (the two files' own names for things, which are static in each) */
#define builder kbd_builder
#define quad_add kbd_quad_add
#define rect_add kbd_rect_add
#define glyph_of kbd_glyph_of
#define text_add kbd_text_add
#define inside kbd_inside
#define published kbd_published
#define color_accent kbd_color_accent
#define color_white kbd_color_white
#define color_pale kbd_color_pale
#define color_dim kbd_color_dim
#include "wiiu_keyboard_code.inc"
#undef builder
#undef quad_add
#undef rect_add
#undef glyph_of
#undef text_add
#undef inside
#undef published
#undef color_accent
#undef color_white
#undef color_pale
#undef color_dim
#include "wiiu_social_view_code.inc"

/* a rasterizer with no font: every glyph a box 10 wide in a 12 advance (labels) or 14 (text) */
static int mock_raster(int font, unsigned long code, unsigned char *buffer, int stride, int max_width, int max_height, int *left, int *top, int *width, int *height, float *advance)
{
	int x, y;

	if (!code)
	{
		*left = font ? 24 : 20;
		*top = font ? 8 : 6;
		*width = *height = 0;
		*advance = 0.0f;
		return 1;
	}
	*advance = font ? 14.0f : 12.0f;
	*left = 1;
	*top = font ? -20 : -16;
	*width = code == ' ' ? 0 : 10;
	*height = code == ' ' ? 0 : 16;
	if (*width > max_width || *height > max_height)
		return 0;
	for (y = 0; y < *height; y++)
		for (x = 0; x < *width; x++)
			buffer[y * stride + x] = (unsigned char)code;
	return 1;
}

static struct wk_atlas atlas;
static struct wk_quad quads[WSV_MAXIMUM_QUADS];

static void atlas_make(void)
{
	CHECK(wiiu_keyboard_atlas_build(&atlas, mock_raster) == 1 && atlas.ready, "atlas");
}

static void view_full(struct halo_social_view *view)
{
	int index;

	memset(view, 0, sizeof(*view));
	view->active = 1;
	view->tab = HALO_SOCIAL_TAB_FRIENDS;
	snprintf(view->head, sizeof(view->head), "bloodgulch  Slayer  12 OF 16   BOTS 6");
	snprintf(view->party, sizeof(view->party), "PARTY 3/8");
	snprintf(view->tabs[0], sizeof(view->tabs[0]), "FRIENDS 11");
	snprintf(view->tabs[1], sizeof(view->tabs[1]), "PARTY 5");
	snprintf(view->tabs[2], sizeof(view->tabs[2]), "BOTS 6");
	for (index = 0; index < HALO_SOCIAL_CHAT_LINES; index++)
	{
		snprintf(view->chat[index].name, sizeof(view->chat[index].name), "WWWWWWWWWWW");
		memset(view->chat[index].text, 'm', HALO_SOCIAL_TEXT - 1);
		view->chat[index].team = (signed char)(index % 3 - 1);
	}
	view->chat_count = HALO_SOCIAL_CHAT_LINES;
	for (index = 0; index < HALO_SOCIAL_ROWS; index++)
	{
		snprintf(view->rows[index].mark, sizeof(view->rows[index].mark), "*");
		snprintf(view->rows[index].name, sizeof(view->rows[index].name), "WWWWWWWWWWW");
		memset(view->rows[index].status, 'W', sizeof(view->rows[index].status) - 1);
		view->rows[index].name_rgb = 0x2896FF;
		view->rows[index].status_rgb = 0x40D050;
		view->rows[index].action = (unsigned char)(index % 2 ? HALO_SOCIAL_ACTION_INVITE : HALO_SOCIAL_ACTION_JOIN);
	}
	view->row_count = HALO_SOCIAL_ROWS;
	snprintf(view->bots[0], sizeof(view->bots[0]), "BOTS: 6");
	snprintf(view->bots[1], sizeof(view->bots[1]), "SKILL: LEGENDARY");
	view->bots_host = 1;
	for (index = 0; index < HALO_SOCIAL_TILES; index++)
		snprintf(view->tiles[index], sizeof(view->tiles[index]), "Need backup!%d", index);
	snprintf(view->page, sizeof(view->page), "CALLS");
}

static int inside_screen(const struct wk_rect *r)
{
	return r->x0 >= 0 && r->y0 >= 0 && r->x1 <= (float)WSV_WIDTH && r->y1 <= (float)WSV_HEIGHT && r->x1 > r->x0 && r->y1 > r->y0;
}

static int overlap(const struct wk_rect *a, const struct wk_rect *b)
{
	return a->x0 < b->x1 && b->x0 < a->x1 && a->y0 < b->y1 && b->y0 < a->y1;
}

#define MAXIMUM_TARGETS 64

static int targets(struct wk_rect *list, int *kinds, int *is_toast)
{
	int count = 0, index;

	for (index = 0; index < HALO_SOCIAL_TABS; index++)
	{
		wiiu_social_tab_rect(index, &list[count]);
		kinds[count] = 1;
		is_toast[count++] = 0;
	}
	for (index = 0; index < HALO_SOCIAL_ROWS; index++)
	{
		wiiu_social_row_button_rect(index, &list[count]);
		kinds[count] = 2;
		is_toast[count++] = 0;
	}
	for (index = 0; index < HALO_SOCIAL_TILES; index++)
	{
		wiiu_social_tile_rect(index, &list[count]);
		kinds[count] = 3;
		is_toast[count++] = 0;
	}
	wiiu_social_keyboard_rect(&list[count]);
	kinds[count] = 4;
	is_toast[count++] = 0;
	for (index = 0; index < 2; index++)
	{
		wiiu_social_page_rect(index, &list[count]);
		kinds[count] = 5;
		is_toast[count++] = 0;
	}
	for (index = 0; index < 4; index++)
	{
		wiiu_social_bots_rect(index / 2, index % 2, &list[count]);
		kinds[count] = 6;
		is_toast[count++] = 0;
	}
	for (index = 0; index < 2; index++)
	{
		wiiu_social_toast_rect(index, &list[count]);
		kinds[count] = 7;
		is_toast[count++] = 1;
	}
	return count;
}

static void layout(void)
{
	struct wk_rect list[MAXIMUM_TARGETS];
	int kinds[MAXIMUM_TARGETS], is_toast[MAXIMUM_TARGETS];
	int count = targets(list, kinds, is_toast), a, b;

	for (a = 0; a < count; a++)
	{
		CHECK(inside_screen(&list[a]), "target %d is inside the screen", a);
		CHECK(list[a].x1 - list[a].x0 >= (float)WSV_MINIMUM_TOUCH && list[a].y1 - list[a].y0 >= (float)WSV_MINIMUM_TOUCH, "target %d (kind %d) is %.0f x %.0f", a, kinds[a],
			list[a].x1 - list[a].x0, list[a].y1 - list[a].y0);
		for (b = a + 1; b < count; b++)
		{
			/* the toast's buttons are over the tabs and the rows; the bots tab's buttons and the friends rows' are never shown together */
			if (is_toast[a] || is_toast[b] || (kinds[a] == 2 && kinds[b] == 6) || (kinds[a] == 6 && kinds[b] == 2))
				continue;
			CHECK(!overlap(&list[a], &list[b]), "target %d (kind %d) overlaps %d (kind %d)", a, kinds[a], b, kinds[b]);
		}
	}
	/* the bots rows hold their buttons, the rows' own rectangles are the same as the touch places' rows */
	{
		struct wk_rect row, bots;

		wiiu_social_row_rect(0, &row);
		wiiu_social_bots_rect(0, 1, &bots);
		CHECK(bots.y0 == row.y0 && bots.y1 == row.y1 && bots.x1 <= row.x1, "the bots' buttons are in the first row");
	}
}

static int hit(const struct halo_social_view *view, float x, float y, struct halo_social_event *event)
{
	return wiiu_social_view_hit(view, x, y, event);
}

static void hits(void)
{
	struct halo_social_view view;
	struct halo_social_event event;
	struct wk_rect r;
	int index;

	view_full(&view);
	for (index = 0; index < HALO_SOCIAL_TABS; index++)
	{
		wiiu_social_tab_rect(index, &r);
		CHECK(hit(&view, r.x0, r.y0, &event) == HALO_SOCIAL_EVENT_TAB && event.index == index, "tab %d's corner", index);
		CHECK(hit(&view, r.x1 - 0.5f, r.y1 - 0.5f, &event) == HALO_SOCIAL_EVENT_TAB && event.index == index, "tab %d's far corner", index);
		CHECK(hit(&view, r.x1, r.y0 + 10, &event) == HALO_SOCIAL_EVENT_NONE || event.kind == HALO_SOCIAL_EVENT_TAB, "past tab %d", index);
	}
	wiiu_social_tab_rect(0, &r);
	CHECK(hit(&view, r.x0 - 1, r.y0 + 10, &event) == HALO_SOCIAL_EVENT_NONE, "left of the tabs");
	CHECK(hit(&view, r.x0 + 10, r.y0 - 1, &event) == HALO_SOCIAL_EVENT_NONE, "above the tabs");
	for (index = 0; index < HALO_SOCIAL_ROWS; index++)
	{
		wiiu_social_row_button_rect(index, &r);
		CHECK(hit(&view, r.x0 + 1, r.y0 + 1, &event) == HALO_SOCIAL_EVENT_ROW && event.index == index, "row %d's button", index);
		CHECK(hit(&view, r.x0 - 1, r.y0 + 10, &event) == HALO_SOCIAL_EVENT_NONE, "left of row %d's button: the row's name is no button", index);
	}
	view.rows[2].action = HALO_SOCIAL_ACTION_NONE;
	wiiu_social_row_button_rect(2, &r);
	CHECK(hit(&view, r.x0 + 1, r.y0 + 1, &event) == HALO_SOCIAL_EVENT_NONE, "a row with no action has no button");
	view.row_count = 2;
	wiiu_social_row_button_rect(3, &r);
	CHECK(hit(&view, r.x0 + 1, r.y0 + 1, &event) == HALO_SOCIAL_EVENT_NONE, "a row past the list has no button");
	for (index = 0; index < HALO_SOCIAL_TILES; index++)
	{
		wiiu_social_tile_rect(index, &r);
		CHECK(hit(&view, r.x0 + 2, r.y0 + 2, &event) == HALO_SOCIAL_EVENT_TILE && event.index == index, "tile %d", index);
	}
	view.tiles[1][0] = 0;
	wiiu_social_tile_rect(1, &r);
	CHECK(hit(&view, r.x0 + 2, r.y0 + 2, &event) == HALO_SOCIAL_EVENT_NONE, "a tile with no line");
	wiiu_social_keyboard_rect(&r);
	CHECK(hit(&view, r.x0 + 2, r.y0 + 2, &event) == HALO_SOCIAL_EVENT_KEYBOARD, "the keyboard");
	wiiu_social_page_rect(0, &r);
	CHECK(hit(&view, r.x0 + 2, r.y0 + 2, &event) == HALO_SOCIAL_EVENT_PAGE_PREVIOUS, "the page before");
	wiiu_social_page_rect(1, &r);
	CHECK(hit(&view, r.x0 + 2, r.y0 + 2, &event) == HALO_SOCIAL_EVENT_PAGE_NEXT, "the page after");
	/* the bots tab: the host's buttons, nobody else's */
	view.tab = HALO_SOCIAL_TAB_BOTS;
	for (index = 0; index < 4; index++)
	{
		wiiu_social_bots_rect(index / 2, index % 2, &r);
		CHECK(hit(&view, r.x0 + 2, r.y0 + 2, &event) == (index % 2 ? HALO_SOCIAL_EVENT_BOTS_PLUS : HALO_SOCIAL_EVENT_BOTS_MINUS) && event.index == index / 2, "bots button %d", index);
	}
	view.bots_host = 0;
	wiiu_social_bots_rect(0, 1, &r);
	CHECK(hit(&view, r.x0 + 2, r.y0 + 2, &event) == HALO_SOCIAL_EVENT_NONE, "a client has no bots buttons");
	/* the friends rows' buttons are not there on the bots tab */
	wiiu_social_row_button_rect(3, &r);
	CHECK(hit(&view, r.x0 + 1, r.y0 + 1, &event) == HALO_SOCIAL_EVENT_NONE, "no row buttons on the bots tab");
	/* an invitation's buttons are above the tabs; a toast with no invitation is none; nothing at all when the view is off */
	view_full(&view);
	wiiu_social_toast_rect(0, &r);
	CHECK(hit(&view, r.x0 + 2, r.y0 + 2, &event) == HALO_SOCIAL_EVENT_TAB, "no toast: the tab under it");
	view.toast_alpha = 255;
	CHECK(hit(&view, r.x0 + 2, r.y0 + 2, &event) == HALO_SOCIAL_EVENT_TAB, "a toast that is no invitation has no buttons");
	view.toast_invite = 1;
	CHECK(hit(&view, r.x0 + 2, r.y0 + 2, &event) == HALO_SOCIAL_EVENT_TOAST_JOIN, "JOIN");
	wiiu_social_toast_rect(1, &r);
	CHECK(hit(&view, r.x0 + 2, r.y0 + 2, &event) == HALO_SOCIAL_EVENT_TOAST_LATER, "LATER");
	view.active = 0;
	CHECK(hit(&view, r.x0 + 2, r.y0 + 2, &event) == HALO_SOCIAL_EVENT_NONE, "off: nothing");
}

static int is_glyph(const struct wk_quad *q)
{
	return q->u0 != atlas.solid_u || q->v0 != atlas.solid_v;
}

static void quads_check(void)
{
	struct halo_social_view view;
	int count, index;

	atlas_make();
	view_full(&view);
	count = wiiu_social_view_build(&view, &atlas, quads, WSV_MAXIMUM_QUADS);
	CHECK(count > 100 && count < WSV_MAXIMUM_QUADS, "a full view: %d quads", count);
	for (index = 0; index < count; index++)
	{
		CHECK(quads[index].x0 >= 0 && quads[index].y0 >= -2 && quads[index].x1 <= (float)WSV_WIDTH && quads[index].y1 <= (float)WSV_HEIGHT + 2, "quad %d (%.0f,%.0f)-(%.0f,%.0f) is in the screen", index,
			quads[index].x0, quads[index].y0, quads[index].x1, quads[index].y1);
		CHECK(quads[index].a >= 0.0f && quads[index].a <= 1.0f, "alpha");
	}
	/* the first quad is the background: all of the picture (the mirror is not drawn under it) */
	CHECK(quads[0].x0 == 0 && quads[0].y0 == 0 && quads[0].x1 == (float)WSV_WIDTH && quads[0].y1 == (float)WSV_HEIGHT && quads[0].a == 1.0f && !is_glyph(&quads[0]), "a background covers it");
	/* the texts stay in their places: the chat's glyphs left of the pane (x under 446), the pane's glyphs right of it, with the longest names and lines */
	for (index = 0; index < count; index++)
	{
		if (!is_glyph(&quads[index]))
			continue;
		if (quads[index].y0 >= (float)WSV_PANE_TOP + 2 && quads[index].y1 <= (float)(WSV_PANE_TOP + HALO_SOCIAL_CHAT_LINES * WSV_CHAT_PITCH) && quads[index].x0 < 450.0f)
			CHECK(quads[index].x1 <= (float)(WSV_CHAT_LEFT + WSV_CHAT_WIDTH), "a chat glyph runs out of the chat's pane: %.0f", quads[index].x1);
		if (quads[index].y0 >= (float)WSV_ROWS_TOP && quads[index].y1 <= (float)(WSV_ROWS_TOP + HALO_SOCIAL_ROWS * WSV_ROW_PITCH) && quads[index].x0 >= 462.0f)
			CHECK(quads[index].x1 <= (float)(WSV_PANE_LEFT + WSV_PANE_WIDTH), "a pane glyph runs out of the pane: %.0f", quads[index].x1);
	}
	/* a row's name and status do not run into the row's button */
	for (index = 0; index < count; index++)
	{
		if (is_glyph(&quads[index]) && quads[index].y0 >= (float)WSV_ROWS_TOP && quads[index].y1 <= (float)(WSV_ROWS_TOP + HALO_SOCIAL_ROWS * WSV_ROW_PITCH) && quads[index].x0 >= 470.0f &&
			quads[index].x0 < (float)WSV_ROW_BUTTON_LEFT)
		{
			CHECK(quads[index].x1 <= (float)WSV_ROW_BUTTON_LEFT, "a row's text runs into its button: %.0f", quads[index].x1);
		}
	}
	/* off, or no atlas: nothing */
	view.active = 0;
	CHECK(wiiu_social_view_build(&view, &atlas, quads, WSV_MAXIMUM_QUADS) == 0, "off: no quads");
	view_full(&view);
	{
		struct wk_atlas none;

		memset(&none, 0, sizeof(none));
		CHECK(wiiu_social_view_build(&view, &none, quads, WSV_MAXIMUM_QUADS) == 0, "no atlas: no quads");
	}
	/* capped */
	CHECK(wiiu_social_view_build(&view, &atlas, quads, 50) == 50, "capped at what the caller has room for");
	/* every tab and the toast */
	view.tab = HALO_SOCIAL_TAB_PARTY;
	CHECK(wiiu_social_view_build(&view, &atlas, quads, WSV_MAXIMUM_QUADS) > 100, "party tab");
	view.tab = HALO_SOCIAL_TAB_BOTS;
	view.toast_alpha = 200;
	snprintf(view.toast, sizeof(view.toast), "ALICE INVITES YOU");
	snprintf(view.toast_detail, sizeof(view.toast_detail), "bloodgulch - Slayer - 6 of 16");
	view.toast_invite = 1;
	count = wiiu_social_view_build(&view, &atlas, quads, WSV_MAXIMUM_QUADS);
	CHECK(count > 100 && quads[count - 1].a > 0, "bots tab with a toast: %d", count);
	/* a view with nothing in it still draws its frame and says so */
	memset(&view, 0, sizeof(view));
	view.active = 1;
	snprintf(view.empty, sizeof(view.empty), "NO FRIENDS YET.");
	count = wiiu_social_view_build(&view, &atlas, quads, WSV_MAXIMUM_QUADS);
	CHECK(count > 30, "an empty view: %d", count);
}

static void fit(void)
{
	char out[80];
	float width;

	atlas_make();
	width = wiiu_keyboard_text_width(&atlas, WK_FONT_TEXT, 1.0f, "HELLO", 5);
	CHECK(wiiu_social_fit(&atlas, WK_FONT_TEXT, 1.0f, width, "HELLO", out, (int)sizeof(out)) == 5 && !strcmp(out, "HELLO"), "exact fit: '%s'", out);
	CHECK(wiiu_social_fit(&atlas, WK_FONT_TEXT, 1.0f, width - 1.0f, "HELLO", out, (int)sizeof(out)) > 0 && !strcmp(out + strlen(out) - 3, "..."), "cut: '%s'", out);
	CHECK(wiiu_keyboard_text_width(&atlas, WK_FONT_TEXT, 1.0f, out, (int)strlen(out)) <= width - 1.0f, "the cut text fits");
	CHECK(wiiu_social_fit(&atlas, WK_FONT_TEXT, 1.0f, 3.0f, "HELLO", out, (int)sizeof(out)) == 3 && !strcmp(out, "..."), "a width too small for anything: '%s'", out);
	CHECK(wiiu_social_fit(&atlas, WK_FONT_TEXT, 1.0f, 1000.0f, "HELLO", out, 3) == 2, "a small buffer");
}

static void publish(void)
{
	struct halo_social_view view, copy;
	struct halo_social_event event;
	int index;

	memset(&copy, 0, sizeof(copy));
	wiiu_social_view_snapshot(&copy);
	CHECK(!copy.active, "nothing published: not active");
	view_full(&view);
	wiiu_social_view_publish(&view);
	wiiu_social_view_snapshot(&copy);
	CHECK(!memcmp(&view, &copy, sizeof(view)), "what was published is what is read");
	/* a half-written view is never read: an odd sequence says it is being written */
	published.sequence++;
	wiiu_social_view_snapshot(&copy);
	CHECK(!copy.active, "a view half written reads as off");
	published.sequence++;
	wiiu_social_view_snapshot(&copy);
	CHECK(copy.active, "and whole again");
	/* the events: in order, eight at most */
	CHECK(!wiiu_social_event_pop(&event), "no event");
	for (index = 0; index < 10; index++)
	{
		event.kind = HALO_SOCIAL_EVENT_TILE;
		event.index = index;
		wiiu_social_event_push(&event);
	}
	for (index = 0; index < 8; index++)
	{
		CHECK(wiiu_social_event_pop(&event) && event.index == index, "event %d", index);
	}
	CHECK(!wiiu_social_event_pop(&event), "eight kept, two dropped");
	event.kind = HALO_SOCIAL_EVENT_TAB;
	event.index = 2;
	wiiu_social_event_push(&event);
	CHECK(wiiu_social_event_pop(&event) && event.kind == HALO_SOCIAL_EVENT_TAB && event.index == 2, "after wrapping");
}

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "layout"))
		layout();
	else if (!strcmp(name, "hits"))
		hits();
	else if (!strcmp(name, "quads"))
		quads_check();
	else if (!strcmp(name, "fit"))
		fit();
	else if (!strcmp(name, "publish"))
		publish();
	else
	{
		fprintf(stderr, "no case named %s\n", name);
		return 2;
	}
	return 0;
}
