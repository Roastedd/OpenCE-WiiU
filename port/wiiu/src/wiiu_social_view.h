/*
WIIU_SOCIAL_VIEW.H

The GamePad's social view (input.wiiu_gamepad_view = "social"): in a network game or its lobby the GamePad stops mirroring the TV and shows the social screens instead, for touch: the
chat, the friends, the party and the bots, the quick chat's lines and a button for the keyboard. Plain code with no system calls, so that the harness builds it alone
(tools/harness/tests/wiiu_social_logic.c); wiiu_gx2.c draws the quads on the GamePad (the keyboard's glyph atlas is the texture, wiiu_keyboard.h) and xinput_wiiu.c passes the touch.

   y   0..36    the header: where and who, the party
   y  44..324   the chat (seven lines of 40, 22 px)                   |  the tabs (44 high), then five rows of 48 (44 high touch)
   y 348..404   four quick chat lines (126 x 56), KEYBOARD (150 x 56), the page's arrows (58 x 56)
   y 410..440   the page's name; y 448..476 a hint

Every touch target is at least 44 pixels high (7 mm on the 6.2 inch screen). The view is the game's (halo_social.h), published once a frame; what is touched goes back as events.
*/

#ifndef HALO_WIIU_SOCIAL_VIEW_H
#define HALO_WIIU_SOCIAL_VIEW_H

#include "halo_social.h"
#include "wiiu_keyboard.h"

enum
{
	WSV_WIDTH = 854,
	WSV_HEIGHT = 480,
	WSV_HEADER_HEIGHT = 36,
	WSV_PANE_TOP = 44,
	WSV_CHAT_LEFT = 16,
	WSV_CHAT_WIDTH = 430,
	WSV_CHAT_PITCH = 40,
	WSV_PANE_LEFT = 462,
	WSV_PANE_WIDTH = 376,
	WSV_TAB_HEIGHT = 44,
	WSV_TAB_PITCH = 126,
	WSV_TAB_WIDTH = 124,
	WSV_ROWS_TOP = 96,
	WSV_ROW_PITCH = 48,
	WSV_ROW_HEIGHT = 44,
	WSV_ROW_BUTTON_LEFT = 740,
	WSV_BOTS_BUTTON_WIDTH = 60,
	WSV_TILES_TOP = 348,
	WSV_TILE_HEIGHT = 56,
	WSV_TILE_LEFT = 16,
	WSV_TILE_PITCH = 132,
	WSV_TILE_WIDTH = 126,
	WSV_KEYBOARD_LEFT = 548,
	WSV_KEYBOARD_WIDTH = 150,
	WSV_PAGE_LEFT = 710,
	WSV_PAGE_PITCH = 70,
	WSV_PAGE_WIDTH = 58,
	WSV_TOAST_TOP = 40,
	WSV_TOAST_HEIGHT = 52,
	WSV_MINIMUM_TOUCH = 44,
	WSV_MAXIMUM_QUADS = 1800,
};

/* the places (pixels of the GamePad's picture) */
void wiiu_social_tab_rect(int tab, struct wk_rect *rectangle);
void wiiu_social_row_rect(int row, struct wk_rect *rectangle);
/* the row's button's touch place (the row's right end, the row's whole height) */
void wiiu_social_row_button_rect(int row, struct wk_rect *rectangle);
void wiiu_social_tile_rect(int tile, struct wk_rect *rectangle);
void wiiu_social_keyboard_rect(struct wk_rect *rectangle);
void wiiu_social_page_rect(int next, struct wk_rect *rectangle);
/* the bots tab's - and + of a line (0 bots, 1 skill) */
void wiiu_social_bots_rect(int line, int plus, struct wk_rect *rectangle);
/* a toast's JOIN and LATER */
void wiiu_social_toast_rect(int later, struct wk_rect *rectangle);

/* what a finger at a place touches (event->kind HALO_SOCIAL_EVENT_NONE for nothing): the toast's buttons first while an invitation is shown, then the tabs, a row's button, the bots'
buttons (the host's, on the bots tab), the quick chat's lines, the keyboard, the page's arrows. Returns the kind. */
int wiiu_social_view_hit(const struct halo_social_view *view, float x, float y, struct halo_social_event *event);

/* The view drawn: the quads, back to front, in pixels of the GamePad's picture, all of it (a background covers the picture: the TV's mirror is not drawn under it). 0 quads when the view
is not active or the atlas is not ready. */
int wiiu_social_view_build(const struct halo_social_view *view, const struct wk_atlas *atlas, struct wk_quad *quads, int maximum);

/* text cut to a width in the atlas's font at a scale ("..." if cut): the build's own cut, so that no text runs out of its place; returns the length */
int wiiu_social_fit(const struct wk_atlas *atlas, int font, float scale, float width, const char *text, char *out, int size);

/* ---------- the view as the game's thread publishes it, the thread that draws reads it, and what is touched */

void wiiu_social_view_publish(const struct halo_social_view *view);
void wiiu_social_view_snapshot(struct halo_social_view *view);
/* the touch thread (the game's thread, as it reads the pads): an event for the game, dropped if the queue is full (eight) */
void wiiu_social_event_push(const struct halo_social_event *event);
int wiiu_social_event_pop(struct halo_social_event *event);

/* the console's pieces (wiiu_keyboard_font.c): whether input.wiiu_gamepad_view is "social", and the view drawn now (it is active, the atlas is made) */
int wiiu_social_view_enabled(void);
int wiiu_social_view_shown(void);

#endif
