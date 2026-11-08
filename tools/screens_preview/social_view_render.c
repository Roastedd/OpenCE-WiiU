/* social_view_render: the GamePad's social view as the Wii U draws it: the real glyph atlas (stb_truetype, Overpass at the sizes the console makes it), the real layout code
   (wiiu_social_view.c), the quads a textured-quad rasterizer puts in an 854 x 480 picture. Writes a PPM; tools/screens_preview.py turns it into a PNG.

   social_view_render OUT.ppm SCENARIO     (friends | party | bots | bots-client | empty | toast | toast-confirm | longest)
   cc ... tools/screens_preview/social_view_render.c port/wiiu/src/wiiu_social_view.c port/wiiu/src/wiiu_keyboard.c port/linux/game/screens*.c ... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "text_safe.h"
#include "screens.h"
#include "screens_view.h"
#include "wiiu_social_view.h"

long config_integer(const char *name)
{
	(void)name;
	return 1;
}

static stbtt_fontinfo fonts[2];
static unsigned char *font_data[2];

static unsigned char *read_file(char const *path)
{
	FILE *file = fopen(path, "rb");
	long size;
	unsigned char *data;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)size);
	if (fread(data, 1, (size_t)size, file) != (size_t)size)
		return NULL;
	fclose(file);
	return data;
}

/* wk_raster: font 0 (WK_FONT_LABEL) is Overpass-900 at 26 px, font 1 Overpass-750 at 30: the em in pixels (wiiu_keyboard_font.c's rasterize) */
static int raster(int font, unsigned long code, unsigned char *buffer, int stride, int max_width, int max_height, int *left, int *top, int *width, int *height, float *advance)
{
	const float pixels = font == WK_FONT_LABEL ? WK_LABEL_PIXELS : WK_TEXT_PIXELS;
	const float scale = stbtt_ScaleForMappingEmToPixels(&fonts[font], pixels);
	int ascent, descent, gap, x0, y0, x1, y1, advance_width, bearing;

	stbtt_GetFontVMetrics(&fonts[font], &ascent, &descent, &gap);
	if (!code)
	{
		*left = (int)(ascent * scale + 0.5f);
		*top = (int)(-descent * scale + 0.5f);
		*width = *height = 0;
		*advance = 0.0f;
		return 1;
	}
	stbtt_GetCodepointHMetrics(&fonts[font], (int)code, &advance_width, &bearing);
	*advance = advance_width * scale;
	stbtt_GetCodepointBitmapBox(&fonts[font], (int)code, scale, scale, &x0, &y0, &x1, &y1);
	*left = x0;
	*top = y0;
	*width = x1 - x0;
	*height = y1 - y0;
	if (*width > max_width || *height > max_height)
		return 0;
	if (*width > 0 && *height > 0)
		stbtt_MakeCodepointBitmap(&fonts[font], buffer, *width, *height, stride, scale, scale, (int)code);
	return 1;
}

static struct wk_atlas atlas;
static struct wk_quad quads[WSV_MAXIMUM_QUADS];
static float picture[WSV_HEIGHT][WSV_WIDTH][3];

static float sample(float u, float v)
{
	float x = u * WK_ATLAS_WIDTH - 0.5f, y = v * WK_ATLAS_HEIGHT - 0.5f;
	int x0 = (int)x, y0 = (int)y;
	float fx = x - (float)x0, fy = y - (float)y0;
	float value = 0.0f;
	int i, j;

	if (x < 0.0f)
		x0 = 0, fx = 0.0f;
	if (y < 0.0f)
		y0 = 0, fy = 0.0f;
	for (j = 0; j < 2; j++)
		for (i = 0; i < 2; i++)
		{
			int px = x0 + i, py = y0 + j;
			float weight = (i ? fx : 1.0f - fx) * (j ? fy : 1.0f - fy);

			if (px >= WK_ATLAS_WIDTH)
				px = WK_ATLAS_WIDTH - 1;
			if (py >= WK_ATLAS_HEIGHT)
				py = WK_ATLAS_HEIGHT - 1;
			value += weight * (float)atlas.pixels[py * WK_ATLAS_WIDTH + px] / 255.0f;
		}
	return value;
}

static void draw(int count)
{
	int index;

	for (index = 0; index < count; index++)
	{
		const struct wk_quad *q = &quads[index];
		int x0 = (int)(q->x0 + 0.5f), x1 = (int)(q->x1 + 0.5f), y0 = (int)(q->y0 + 0.5f), y1 = (int)(q->y1 + 0.5f), x, y;

		for (y = y0 < 0 ? 0 : y0; y < y1 && y < WSV_HEIGHT; y++)
		{
			for (x = x0 < 0 ? 0 : x0; x < x1 && x < WSV_WIDTH; x++)
			{
				float u = q->u0 + (q->u1 - q->u0) * (((float)x + 0.5f - q->x0) / (q->x1 - q->x0));
				float v = q->v0 + (q->v1 - q->v0) * (((float)y + 0.5f - q->y0) / (q->y1 - q->y0));
				float alpha = q->a * sample(u, v);

				picture[y][x][0] += (q->r - picture[y][x][0]) * alpha;
				picture[y][x][1] += (q->g - picture[y][x][1]) * alpha;
				picture[y][x][2] += (q->b - picture[y][x][2]) * alpha;
			}
		}
	}
}

int main(int argc, char **argv)
{
	char const *scenario = argc > 2 ? argv[2] : "friends";
	char const *root = getenv("SCREENS_ROOT") ? getenv("SCREENS_ROOT") : ".";
	char path[512];
	static struct screens_model model;
	struct screens_toast_state toasts;
	struct screens_view_input input;
	struct halo_social_view view;
	struct screens_toast toast;
	long row_keys[HALO_SOCIAL_ROWS];
	static char const *const lines[4][4] = { { "Enemy here!", "Need backup!", "Cover me!", "Fall back!" }, { "Follow me", "Wait for me", "Go go go!", "Hold here" },
		{ "Good game", "Nice shot!", "Thanks!", "Sorry!" }, { "", "", "", "" } };
	FILE *out;
	int count, x, y, index;

	snprintf(path, sizeof(path), "%s/port/assets/fonts/Overpass-900.ttf", root);
	font_data[0] = read_file(path);
	snprintf(path, sizeof(path), "%s/port/assets/fonts/Overpass-750.ttf", root);
	font_data[1] = read_file(path);
	if (!font_data[0] || !font_data[1] || !stbtt_InitFont(&fonts[0], font_data[0], 0) || !stbtt_InitFont(&fonts[1], font_data[1], 0))
	{
		fprintf(stderr, "no fonts under %s\n", root);
		return 1;
	}
	if (!wiiu_keyboard_atlas_build(&atlas, raster))
	{
		fprintf(stderr, "the atlas did not fit\n");
		return 1;
	}
	screens_mock_reset();
	screens_model_update(&model, 1);
	screens_toast_reset(&toasts);
	memset(&input, 0, sizeof(input));
	input.model = &model;
	input.tab = !strncmp(scenario, "party", 5) ? HALO_SOCIAL_TAB_PARTY : !strncmp(scenario, "bots", 4) ? HALO_SOCIAL_TAB_BOTS : HALO_SOCIAL_TAB_FRIENDS;
	for (index = 0; index < 4; index++)
		input.tiles[index] = lines[0][index];
	input.page_name = "CALLS";
	input.toasts = &toasts;
	input.now = 5000;
	input.has_friends = 1;
	input.can_friend_action = 1;
	input.can_bots = 1;
	if (!strcmp(scenario, "bots-client"))
		model.roster_summary.host = 0;
	if (!strcmp(scenario, "empty"))
	{
		memset(model.friends_count, 0, sizeof(model.friends_count));
		model.friends_summary.online = 0;
	}
	if (!strncmp(scenario, "toast", 5))
	{
		memset(&toast, 0, sizeof(toast));
		while (screens_mock_provider()->toast_pop(&toast))
		{
			if (toast.kind == SCREENS_TOAST_INVITE)
				break;
		}
		screens_toast_push(&toasts, &toast, 5000);
		screens_toast_tick(&toasts, 5000);
		input.now = 6000;
		if (!strcmp(scenario, "toast-confirm"))
		{
			long id;

			screens_toast_answer(&toasts, 1, 1, 6000, &id);
		}
	}
	screens_view_make(&view, &input, row_keys);
	/* the chat's lines: a made-up conversation, the longest name and line among them */
	{
		static char const *const names[7] = { "ALICE", "BOB", "CARL", "WWWWWWWWWWW", "JOHNSON", "DAN", "ALICE" };
		static char const *const text[7] = { "need help at the flag", "on my way", "ready", "mmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm", "Enemy here!",
			"nice shot!", "one minute" };
		static const signed char teams[7] = { 0, 0, 2, 1, 1, 2, 0 };

		for (index = 0; index < HALO_SOCIAL_CHAT_LINES; index++)
		{
			memset(&view.chat[index], 0, sizeof(view.chat[index]));
			snprintf(view.chat[index].name, sizeof(view.chat[index].name), "%s", names[index]);
			snprintf(view.chat[index].text, sizeof(view.chat[index].text), "%s", text[index]);
			view.chat[index].team = teams[index];
		}
		view.chat_count = HALO_SOCIAL_CHAT_LINES;
	}
	for (y = 0; y < WSV_HEIGHT; y++)
		for (x = 0; x < WSV_WIDTH; x++)
			picture[y][x][0] = picture[y][x][1] = picture[y][x][2] = 0.0f;
	count = wiiu_social_view_build(&view, &atlas, quads, WSV_MAXIMUM_QUADS);
	draw(count);
	out = fopen(argv[1], "wb");
	fprintf(out, "P6\n%d %d\n255\n", WSV_WIDTH, WSV_HEIGHT);
	for (y = 0; y < WSV_HEIGHT; y++)
		for (x = 0; x < WSV_WIDTH; x++)
		{
			unsigned char rgb[3];
			int channel;

			for (channel = 0; channel < 3; channel++)
				rgb[channel] = (unsigned char)(picture[y][x][channel] * 255.0f + 0.5f);
			fwrite(rgb, 1, 3, out);
		}
	fclose(out);
	fprintf(stderr, "%s: %d quads\n", scenario, count);
	return 0;
}
