#include "cseries.h"
#include "camera_status.h"
#include "game/players.h"
#include "cutscene/cinematics.h"
#include "interface/hud.h"
#include "interface/ui_widget.h"
#include "render/render.h"
#include "rasterizer/rasterizer.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include <string.h>

unsigned long system_milliseconds(void);
void draw_quad(rectangle2d *rectangle, pixel32 color);

static unsigned int pending[MAXIMUM_LOCAL_PLAYERS];
static unsigned int sequence[MAXIMUM_LOCAL_PLAYERS];
static struct
{
	unsigned int observed;
	unsigned long started;
	unsigned int message;
} shown[MAXIMUM_LOCAL_PLAYERS];

void camera_status_reset(void)
{
	memset(pending, 0, sizeof(pending));
	memset(sequence, 0, sizeof(sequence));
	memset(shown, 0, sizeof(shown));
}

void camera_status_post(short local_player_index, short mode, int controls)
{
	unsigned int message;
	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS || mode < 0 || mode >= 5)
		return;
	message = (++sequence[local_player_index] << 8) | (unsigned int)(mode + 1);
	if (controls >= 0)
		message |= controls ? 0x20u : 0x10u;
	__atomic_store_n(&pending[local_player_index], message, __ATOMIC_RELEASE);
}

void camera_status_draw(void)
{
	static char const *const labels[] = {"THIRD PERSON", "ORBIT CAMERA", "FREE CAMERA", "EDITOR CAMERA", "FIRST PERSON"};
	const short player = render.local_player_index;
	unsigned int message, mode;
	unsigned long now, elapsed;
	long font_index;
	struct font_header *font;
	short line_height, width, window_width, window_height;
	rectangle2d window, band, bounds, accent;
	real_argb_color color;
	float alpha;
	int lines;

	if (player < 0 || player >= MAXIMUM_LOCAL_PLAYERS)
		return;
	now = system_milliseconds();
	message = __atomic_load_n(&pending[player], __ATOMIC_ACQUIRE);
	if (!message)
		return;
	if (message != shown[player].observed)
	{
		shown[player].observed = message;
		shown[player].message = message;
		shown[player].started = now;
	}
	elapsed = now - shown[player].started;
	if (elapsed >= 2500 || cinematic_in_progress() || ui_widgets_active())
		return;
	mode = (shown[player].message & 15u) - 1u;
	if (mode >= NUMBEROF(labels))
		return;
	font_index = hud_get_font_index();
	if (font_index == NONE)
		return;
	font = font_definition_get(font_index);
	line_height = (short)(font->ascending_height + font->descending_height + font->leading_height);
	window = render.camera.window_bounds;
	offset_rectangle2d(&window, -render.camera.viewport_bounds.x0, -render.camera.viewport_bounds.y0);
	window_width = (short)(window.x1 - window.x0);
	window_height = (short)(window.y1 - window.y0);
	if (line_height <= 0 || window_width <= 32 || window_height <= 0)
		return;
	alpha = elapsed < 2000 ? 1.0f : (2500.0f - (float)elapsed) / 500.0f;
	lines = (shown[player].message & 0x30u) ? 2 : 1;
	width = (short)MIN(260, window_width - 32);
	band.x0 = (short)(window.x0 + (window_width - width) / 2);
	band.x1 = (short)(band.x0 + width);
	band.y0 = (short)(window.y0 + window_height * 16 / 100);
	band.y1 = (short)(band.y0 + line_height * lines + 8);
	draw_quad(&band, ((pixel32)(alpha * 190.0f) << 24) | 0x00121e2a);
	accent = band;
	accent.y1 = accent.y0 + 1;
	draw_quad(&accent, ((pixel32)(alpha * 255.0f) << 24) | 0x0079ceff);
	color.alpha = alpha;
	color.red = 0.85f; color.green = 0.95f; color.blue = 1.0f;
	bounds = band;
	bounds.x0 += 6; bounds.x1 -= 6;
	bounds.y0 += 4; bounds.y1 = bounds.y0 + line_height;
	draw_string_set_draw_mode(font_index, NONE, 2, 0, &color);
	rasterizer_draw_string(&bounds, NULL, NULL, 0, labels[mode]);
	if (lines == 2)
	{
		bounds.y0 += line_height; bounds.y1 += line_height;
		rasterizer_draw_string(&bounds, NULL, NULL, 0,
			shown[player].message & 0x20u ? "Camera controls on" : "Player controls on");
	}
}
