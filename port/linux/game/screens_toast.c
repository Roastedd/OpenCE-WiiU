/*
SCREENS_TOAST.C

The social screens' toasts, drawn over the whole frame (the frame's overlays: interface.c's interface_draw_fullscreen_overlays, where the map download's band is drawn): a friend is
online, someone joined or left the party, a request came, an invitation, a join that failed in plain words. One at a time, queued, a band at the top centre inside the title-safe
frame, black at 70 percent with white text, fading in and out. An invitation stays thirty seconds and is answered with the D-pad (right joins, left later; from a game right asks
again with LEAVE THIS GAME, so that a match is not lost to one stray press).

The queue and its timing are screens.c's (the harness tests them); this file has the provider's polling, the D-pad and the drawing. With screens.enable 0 the one function here
returns at once and nothing is polled, kept or drawn.
*/

#include "cseries.h"
#include "game/game.h"
#include "cutscene/cinematics.h"
#include "interface/hud.h"
#include "interface/interface.h"
#include "interface/ui_widget.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"

#include "halo_keyboard.h"
#include "network_votekick.h"
#include "screens.h"

#include <stdio.h>
#include <string.h>

unsigned long system_milliseconds(void);
unsigned char network_chat_keyboard_open(void);
void draw_quad(rectangle2d *rectangle, pixel32 color);
/* screens_social.c's, menu_functions.c's (the game's roster provider for screens.enable 2) */
void screens_social_frame(struct screens_toast_state *state, unsigned long now);
void menu_screens_provider_prepare(void);

enum
{
	/* the provider is asked for toasts this often (milliseconds) */
	POLL_MILLISECONDS = 250,
	/* the band, as a share of the window: its width, and its top (the title-safe frame's, 36 of 480 lines, and a little) */
	BAND_WIDTH_PERCENT = 50,
	BAND_TOP_PERCENT = 9,
	BAND_COLOR = 0xB0000000,
};

static struct
{
	struct screens_toast_state state;
	unsigned long last_poll;
	unsigned long pad_before;
} toasts;

/* the colour of the toast's first line by what it is */
static void toast_colors(struct screens_toast const *toast, real_argb_color *color)
{
	int shade = toast->kind == SCREENS_TOAST_JOIN_FAILED ? SCREENS_COLOR_AMBER : toast->kind == SCREENS_TOAST_FRIEND_ONLINE ? SCREENS_COLOR_GREEN :
		toast->kind == SCREENS_TOAST_INVITE ? SCREENS_COLOR_AZURE : SCREENS_COLOR_WHITE;
	unsigned long rgb = screens_color_rgb(shade);

	color->alpha = 1.0f;
	color->red = (real)((rgb >> 16) & 0xFF) / 255.0f;
	color->green = (real)((rgb >> 8) & 0xFF) / 255.0f;
	color->blue = (real)(rgb & 0xFF) / 255.0f;
}

/* the provider's new toasts, queued; the D-pad's answer to an invitation */
static void toasts_poll(unsigned long now)
{
	struct screens_provider const *provider = screens_provider();
	struct screens_toast toast;

	if (provider && provider->toast_pop && now - toasts.last_poll >= POLL_MILLISECONDS)
	{
		toasts.last_poll = now;
		while (toasts.state.count < SCREENS_TOASTS_MAXIMUM && provider->toast_pop(&toast))
			screens_toast_push(&toasts.state, &toast, now);
	}
	screens_toast_tick(&toasts.state, now);
}

static void toasts_pad(unsigned long now)
{
	unsigned long pad = halo_chat_pad_buttons(), pressed = pad & ~toasts.pad_before;
	struct screens_provider const *provider = screens_provider();
	long id = 0;
	int accept;

	toasts.pad_before = pad;
	if (!toasts.state.shown || toasts.state.current.kind != SCREENS_TOAST_INVITE || ui_widgets_active() || network_chat_keyboard_open() || network_vote_pad_open())
		return;
	if (pressed & (1ul << HALO_CHAT_PAD_RIGHT))
		accept = 1;
	else if (pressed & (1ul << HALO_CHAT_PAD_LEFT))
		accept = 0;
	else
		return;
	if (screens_toast_answer(&toasts.state, accept, game_in_progress() ? 1 : 0, now, &id) && provider && provider->invite_answer)
		provider->invite_answer(id, accept);
}

void screens_toast_frame(void)
{
	unsigned long now;
	long font_tag_index;
	struct font_header *font;
	short line_height, window_width, window_height, lines;
	rectangle2d window, band, bounds;
	real_argb_color color;
	char line[SCREENS_LINE_SIZE * 2];
	struct screens_toast const *toast;
	int alpha;

	if (!screens_enabled())
		return;
	now = system_milliseconds();
	menu_screens_provider_prepare();
	toasts_poll(now);
	toasts_pad(now);
	screens_social_frame(&toasts.state, now);
	if (!toasts.state.shown || cinematic_in_progress() || (hud_scripted_globals && !hud_scripted_globals->show_hud && game_in_progress()))
		return;
	alpha = screens_toast_alpha(&toasts.state, now);
	if (alpha <= 0)
		return;
	toast = &toasts.state.current;
	/* (in a game the HUD's font; in the menus, which have no HUD, the terminal's: the texts are clean ASCII either way) */
	font_tag_index = game_in_progress() ? hud_get_font_index() : NONE;
	if (font_tag_index == NONE)
		font_tag_index = interface_get_tag_index(_interface_font_terminal);
	if (font_tag_index == NONE)
		return;
	font = font_definition_get(font_tag_index);
	line_height = (short)(font->ascending_height + font->descending_height + font->leading_height);
	window = render.camera.window_bounds;
	offset_rectangle2d(&window, -render.camera.viewport_bounds.x0, -render.camera.viewport_bounds.y0);
	window_width = (short)(window.x1 - window.x0);
	window_height = (short)(window.y1 - window.y0);
	if (line_height <= 0 || window_width <= 0 || window_height <= 0)
		return;
	lines = (short)(1 + (toast->detail[0] != 0) + (toast->kind == SCREENS_TOAST_INVITE));
	band.x0 = (short)(window.x0 + window_width * (100 - BAND_WIDTH_PERCENT) / 200);
	band.x1 = (short)(window.x1 - window_width * (100 - BAND_WIDTH_PERCENT) / 200);
	band.y0 = (short)(window.y0 + window_height * BAND_TOP_PERCENT / 100);
	band.y1 = (short)(band.y0 + line_height * lines + line_height / 2);
	draw_quad(&band, (pixel32)(((unsigned long)(alpha * (BAND_COLOR >> 24) / 255) << 24)));
	bounds = band;
	bounds.x0 = (short)(band.x0 + 4);
	bounds.x1 = (short)(band.x1 - 4);
	bounds.y0 = (short)(band.y0 + line_height / 4);
	bounds.y1 = (short)(bounds.y0 + line_height);
	toast_colors(toast, &color);
	color.alpha = (real)alpha / 255.0f;
	draw_string_set_draw_mode(font_tag_index, NONE, 2 /* centred */, 0, &color);
	rasterizer_draw_string(&bounds, NULL, NULL, 0, toast->text);
	if (toast->detail[0])
	{
		color.red = color.green = color.blue = 1.0f;
		bounds.y0 = (short)(bounds.y0 + line_height);
		bounds.y1 = (short)(bounds.y0 + line_height);
		draw_string_set_draw_mode(font_tag_index, NONE, 2, 0, &color);
		rasterizer_draw_string(&bounds, NULL, NULL, 0, toast->detail);
	}
	if (toast->kind == SCREENS_TOAST_INVITE)
	{
		color.red = 0.64f;
		color.green = 0.75f;
		color.blue = 0.87f;
		bounds.y0 = (short)(bounds.y0 + line_height);
		bounds.y1 = (short)(bounds.y0 + line_height);
		if (ui_widgets_active())
			snprintf(line, sizeof(line), "Friends > Requests");
		else if (toasts.state.confirming)
			snprintf(line, sizeof(line), "LEAVE THIS GAME?  D-pad Right  YES   Left  NO");
		else
			snprintf(line, sizeof(line), "D-pad Right  JOIN   Left  LATER");
		draw_string_set_draw_mode(font_tag_index, NONE, 2, 0, &color);
		rasterizer_draw_string(&bounds, NULL, NULL, 0, line);
	}
}
