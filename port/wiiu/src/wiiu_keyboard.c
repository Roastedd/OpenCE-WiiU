/*
WIIU_KEYBOARD.C

The text chat's touch keyboard for the GamePad (wiiu_keyboard.h): plain code, no system calls but the compiler's atomics.
*/

#include "wiiu_keyboard.h"

#include <stdio.h>
#include <string.h>

/* ---------- the layout */

void wiiu_keyboard_key_rect(int row, int column, struct wk_rect *rectangle)
{
	const int span = row == WK_ROWS - 1 ? WK_WIDE_COLUMNS : 1;

	rectangle->x0 = (float)(WK_KEYS_LEFT + column * WK_KEY_WIDTH);
	rectangle->x1 = (float)(WK_KEYS_LEFT + (column + span) * WK_KEY_WIDTH);
	rectangle->y0 = (float)(WK_KEYS_TOP + row * WK_KEY_HEIGHT);
	rectangle->y1 = rectangle->y0 + (float)WK_KEY_HEIGHT;
}

/* the chips: EVERYONE, TEAM, VEHICLE, 150 wide with 8 between, from the keys' left */
#define CHIP_WIDTH 150
#define CHIP_GAP 8
#define CLOSE_WIDTH 100

void wiiu_keyboard_chip_rect(int channel, struct wk_rect *rectangle)
{
	rectangle->x0 = (float)(WK_KEYS_LEFT + channel * (CHIP_WIDTH + CHIP_GAP));
	rectangle->x1 = rectangle->x0 + (float)CHIP_WIDTH;
	rectangle->y0 = (float)WK_CHIP_TOP;
	rectangle->y1 = (float)(WK_CHIP_TOP + WK_CHIP_HEIGHT);
}

void wiiu_keyboard_close_rect(struct wk_rect *rectangle)
{
	rectangle->x1 = (float)(WK_KEYS_LEFT + WK_COLUMNS * WK_KEY_WIDTH);
	rectangle->x0 = rectangle->x1 - (float)CLOSE_WIDTH;
	rectangle->y0 = (float)WK_CHIP_TOP;
	rectangle->y1 = (float)(WK_CHIP_TOP + WK_CHIP_HEIGHT);
}

static int inside(const struct wk_rect *rectangle, float x, float y)
{
	return x >= rectangle->x0 && x < rectangle->x1 && y >= rectangle->y0 && y < rectangle->y1;
}

int wiiu_keyboard_hit(float x, float y, struct wk_hit *hit)
{
	struct wk_rect rectangle;
	int channel;

	memset(hit, 0, sizeof(*hit));
	hit->kind = WK_HIT_NONE;
	if (x >= (float)WK_KEYS_LEFT && x < (float)(WK_KEYS_LEFT + WK_COLUMNS * WK_KEY_WIDTH) && y >= (float)WK_KEYS_TOP && y < (float)(WK_KEYS_TOP + WK_ROWS * WK_KEY_HEIGHT))
	{
		hit->row = (int)((y - (float)WK_KEYS_TOP) / (float)WK_KEY_HEIGHT);
		hit->column = (int)((x - (float)WK_KEYS_LEFT) / (float)WK_KEY_WIDTH);
		if (hit->row > WK_ROWS - 1)
			hit->row = WK_ROWS - 1;
		if (hit->column > WK_COLUMNS - 1)
			hit->column = WK_COLUMNS - 1;
		if (hit->row == WK_ROWS - 1)
			hit->column -= hit->column % WK_WIDE_COLUMNS;
		hit->kind = WK_HIT_KEY;
		return hit->kind;
	}
	for (channel = 0; channel < WK_CHANNELS; channel++)
	{
		wiiu_keyboard_chip_rect(channel, &rectangle);
		if (inside(&rectangle, x, y))
		{
			hit->channel = channel;
			hit->kind = WK_HIT_CHANNEL;
			return hit->kind;
		}
	}
	wiiu_keyboard_close_rect(&rectangle);
	if (inside(&rectangle, x, y))
		hit->kind = WK_HIT_CLOSE;
	return hit->kind;
}

/* ---------- the atlas */

int wiiu_keyboard_atlas_build(struct wk_atlas *atlas, wk_raster raster)
{
	/* the largest glyph: a 30 pixel font's widest and tallest, with room */
	enum { MAXIMUM_GLYPH = 64, SOLID_BLOCK = 8 };
	unsigned char bitmap[MAXIMUM_GLYPH * MAXIMUM_GLYPH];
	int x = SOLID_BLOCK + WK_ATLAS_PADDING, y = 0, row_height = SOLID_BLOCK;
	int font, index, at;

	memset(atlas, 0, sizeof(*atlas));
	if (!raster)
		return 0;
	for (at = 0; at < SOLID_BLOCK; at++)
		memset(atlas->pixels + (size_t)at * WK_ATLAS_WIDTH, 255, SOLID_BLOCK);
	atlas->solid_u = (float)(SOLID_BLOCK / 2) / (float)WK_ATLAS_WIDTH;
	atlas->solid_v = (float)(SOLID_BLOCK / 2) / (float)WK_ATLAS_HEIGHT;
	for (font = 0; font < WK_FONTS; font++)
	{
		int left, top, width, height;
		float advance;

		if (!raster(font, 0, NULL, 0, 0, 0, &left, &top, &width, &height, &advance))
			return 0;
		atlas->ascent[font] = (float)left;
		atlas->descent[font] = (float)top;
		for (index = 0; index < WK_GLYPHS; index++)
		{
			struct wk_glyph *glyph = &atlas->glyphs[font][index];

			width = height = left = top = 0;
			advance = 0.0f;
			memset(bitmap, 0, sizeof(bitmap));
			if (!raster(font, (unsigned long)(WK_GLYPH_FIRST + index), bitmap, MAXIMUM_GLYPH, MAXIMUM_GLYPH, MAXIMUM_GLYPH, &left, &top, &width, &height, &advance))
				continue;
			if (width < 0 || height < 0 || width > MAXIMUM_GLYPH || height > MAXIMUM_GLYPH)
				return 0;
			glyph->present = 1;
			glyph->advance = advance;
			if (!width || !height)
				continue;
			if (x + width + WK_ATLAS_PADDING > WK_ATLAS_WIDTH)
			{
				y += row_height + WK_ATLAS_PADDING;
				x = 0;
				row_height = 0;
			}
			if (y + height + WK_ATLAS_PADDING > WK_ATLAS_HEIGHT)
				return 0;
			for (at = 0; at < height; at++)
				memcpy(atlas->pixels + (size_t)(y + at) * WK_ATLAS_WIDTH + x, bitmap + (size_t)at * MAXIMUM_GLYPH, (size_t)width);
			glyph->u0 = (float)x / (float)WK_ATLAS_WIDTH;
			glyph->v0 = (float)y / (float)WK_ATLAS_HEIGHT;
			glyph->u1 = (float)(x + width) / (float)WK_ATLAS_WIDTH;
			glyph->v1 = (float)(y + height) / (float)WK_ATLAS_HEIGHT;
			glyph->left = (float)left;
			glyph->top = (float)top;
			glyph->right = (float)(left + width);
			glyph->bottom = (float)(top + height);
			x += width + WK_ATLAS_PADDING;
			if (height > row_height)
				row_height = height;
		}
	}
	atlas->ready = 1;
	return 1;
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

static const struct wk_glyph *glyph_of(const struct wk_atlas *atlas, int font, char character)
{
	int index = (unsigned char)character - WK_GLYPH_FIRST;

	if (index < 0 || index >= WK_GLYPHS || !atlas->glyphs[font][index].present)
		index = '?' - WK_GLYPH_FIRST;
	return &atlas->glyphs[font][index];
}

float wiiu_keyboard_text_width(const struct wk_atlas *atlas, int font, float scale, const char *text, int count)
{
	float width = 0.0f;
	int index;

	for (index = 0; index < count && text[index]; index++)
		width += glyph_of(atlas, font, text[index])->advance * scale;
	return width;
}

/* the text from x on the baseline */
static float text_add(struct builder *b, int font, float scale, float x, float baseline, const char *text, int count, const float *color)
{
	int index;

	for (index = 0; index < count && text[index]; index++)
	{
		const struct wk_glyph *glyph = glyph_of(b->atlas, font, text[index]);

		if (glyph->right > glyph->left)
			quad_add(b, x + glyph->left * scale, baseline + glyph->top * scale, x + glyph->right * scale, baseline + glyph->bottom * scale, glyph->u0, glyph->v0, glyph->u1, glyph->v1, color);
		x += glyph->advance * scale;
	}
	return x;
}

/* a string centred in a rectangle (on its caps: the baseline a third of an em below the middle) */
static void text_centred(struct builder *b, int font, float scale, const struct wk_rect *rectangle, const char *text, const float *color)
{
	const float pixels = font == WK_FONT_LABEL ? WK_LABEL_PIXELS : WK_TEXT_PIXELS;
	const float width = wiiu_keyboard_text_width(b->atlas, font, scale, text, (int)strlen(text));

	text_add(b, font, scale, (rectangle->x0 + rectangle->x1 - width) * 0.5f, (rectangle->y0 + rectangle->y1) * 0.5f + 0.35f * pixels * scale, text, (int)strlen(text), color);
}

/* the colours: the TV keyboard's (network_chat.c's chat_keyboard_draw) on a dark panel, the accent blue of the menus */
static const float color_panel[4] = { 0.02f, 0.03f, 0.06f, 0.94f };
static const float color_key[4] = { 0.157f, 0.220f, 0.282f, 0.95f };
static const float color_picked[4] = { 0.337f, 0.510f, 0.863f, 1.0f };
static const float color_lit[4] = { 0.251f, 0.376f, 0.565f, 1.0f };
static const float color_accent[4] = { 0.157f, 0.588f, 1.0f, 1.0f };
static const float color_field[4] = { 0.06f, 0.09f, 0.14f, 1.0f };
static const float color_close[4] = { 0.42f, 0.17f, 0.17f, 1.0f };
static const float color_amber[4] = { 1.0f, 0.690f, 0.125f, 1.0f };
static const float color_white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static const float color_pale[4] = { 0.70f, 0.80f, 0.92f, 1.0f };
static const float color_dim[4] = { 0.55f, 0.65f, 0.78f, 1.0f };

static const char *const channel_names[WK_CHANNELS] = { "EVERYONE", "TEAM", "VEHICLE" };
static const char *const prompts[WK_CHANNELS] = { "Say:", "Say to team:", "Say to vehicle:" };

static void inset(struct wk_rect *rectangle, float amount)
{
	rectangle->x0 += amount;
	rectangle->y0 += amount;
	rectangle->x1 -= amount;
	rectangle->y1 -= amount;
}

int wiiu_keyboard_build(const struct halo_chat_keyboard_view *view, const struct wk_atlas *atlas, unsigned long now_ms, struct wk_quad *quads, int maximum)
{
	struct builder b;
	struct wk_rect rectangle;
	char text[HALO_CHAT_KEYBOARD_TEXT + 2];
	char label[8];
	int row, column, channel, length, start, menu, maximum_characters;
	float pen, available, caret_x;
	const float *prompt_color;

	if (!view || !atlas || !quads || !view->open || !atlas->ready)
		return 0;
	b.atlas = atlas;
	b.quads = quads;
	b.count = 0;
	b.maximum = maximum;
	menu = view->mode == 1;
	/* the panel over the whole screen (the game's picture shows faintly through) */
	rect_add(&b, 0.0f, 0.0f, (float)WK_WIDTH, (float)WK_HEIGHT, color_panel);
	/* the chips (a menu's field has DONE, and CANCEL where CLOSE is) */
	for (channel = 0; channel < (menu ? 1 : WK_CHANNELS); channel++)
	{
		wiiu_keyboard_chip_rect(channel, &rectangle);
		rect_add(&b, rectangle.x0, rectangle.y0, rectangle.x1, rectangle.y1, menu || channel == view->channel ? color_accent : color_key);
		text_centred(&b, WK_FONT_LABEL, 0.8f, &rectangle, menu ? "DONE" : channel_names[channel], color_white);
	}
	wiiu_keyboard_close_rect(&rectangle);
	rect_add(&b, rectangle.x0, rectangle.y0, rectangle.x1, rectangle.y1, color_close);
	text_centred(&b, WK_FONT_LABEL, 0.8f, &rectangle, menu ? "CANCEL" : "CLOSE", color_white);
	/* the letters left, between the chips and CLOSE, to the right */
	maximum_characters = view->limit > 0 && view->limit < WK_LINE_CHARACTERS ? view->limit : WK_LINE_CHARACTERS;
	length = view->length < 0 ? 0 : view->length > maximum_characters ? maximum_characters : view->length;
	snprintf(label, sizeof(label), "%d", maximum_characters - length);
	{
		const float width = wiiu_keyboard_text_width(atlas, WK_FONT_LABEL, 0.8f, label, (int)strlen(label));

		text_add(&b, WK_FONT_LABEL, 0.8f, rectangle.x0 - 16.0f - width, (float)WK_CHIP_TOP + (float)WK_CHIP_HEIGHT * 0.5f + 0.35f * WK_LABEL_PIXELS * 0.8f, label, (int)strlen(label),
			length >= maximum_characters ? color_amber : color_dim);
	}
	/* the line: a box (its edge amber when the line is full), the prompt, the end of the line and a caret */
	{
		const float x0 = (float)WK_KEYS_LEFT, x1 = (float)(WK_KEYS_LEFT + WK_COLUMNS * WK_KEY_WIDTH);
		const float y0 = (float)WK_FIELD_TOP, y1 = (float)(WK_FIELD_TOP + WK_FIELD_HEIGHT);
		const float baseline = (y0 + y1) * 0.5f + 0.35f * WK_TEXT_PIXELS;
		const char *prompt = menu ? "Type:" : prompts[view->channel < 0 || view->channel >= WK_CHANNELS ? 0 : view->channel];

		if (length >= maximum_characters)
			rect_add(&b, x0 - 2.0f, y0 - 2.0f, x1 + 2.0f, y1 + 2.0f, color_amber);
		rect_add(&b, x0, y0, x1, y1, color_field);
		prompt_color = view->channel == 1 ? color_accent : color_pale;
		pen = text_add(&b, WK_FONT_LABEL, 0.8f, x0 + 12.0f, baseline, prompt, (int)strlen(prompt), prompt_color) + 10.0f;
		available = x1 - 12.0f - 10.0f - pen;
		memcpy(text, view->text, HALO_CHAT_KEYBOARD_TEXT);
		text[HALO_CHAT_KEYBOARD_TEXT] = 0;
		text[length] = 0;
		/* (a password's stars) */
		if (menu && view->masked)
			memset(text, '*', (size_t)length);
		/* (the end of the line, where it is wider than the box) */
		for (start = 0; start < length && wiiu_keyboard_text_width(atlas, WK_FONT_TEXT, 1.0f, text + start, length - start) > available; start++)
			;
		caret_x = text_add(&b, WK_FONT_TEXT, 1.0f, pen, baseline, text + start, length - start, color_white);
		if ((now_ms / 500u) % 2u == 0)
		{
			const float caret[4] = { 0.157f, 0.588f, 1.0f, 1.0f };

			rect_add(&b, caret_x + 1.0f, y0 + 10.0f, caret_x + 4.0f, y1 - 10.0f, caret);
		}
	}
	/* the keys */
	for (row = 0; row < WK_ROWS; row++)
	{
		const int span = row == WK_ROWS - 1 ? WK_WIDE_COLUMNS : 1;

		for (column = 0; column < WK_COLUMNS; column += span)
		{
			const int picked = row == view->row && column == view->column;
			const float *color = color_key;

			wiiu_keyboard_key_rect(row, column, &rectangle);
			inset(&rectangle, (float)WK_KEY_INSET);
			if (row == WK_ROWS - 1)
			{
				const int key = column / span;

				if (key == 0 && view->shift)
					color = color_lit;
				else if (key == 1 && view->symbols)
					color = color_lit;
				else if (key == 4)
					color = color_accent;
			}
			if (picked)
				color = color_picked;
			rect_add(&b, rectangle.x0, rectangle.y0, rectangle.x1, rectangle.y1, color);
			if (row < WK_ROWS - 1)
			{
				label[0] = view->keys[row][column];
				label[1] = 0;
			}
			else
			{
				static const char *const wide[WK_WIDE_KEYS] = { "Aa", "#+=", "SPACE", "DEL", "SEND" };

				snprintf(label, sizeof(label), "%s", column / span == 1 && view->symbols ? "ABC" : menu && column / span == 4 ? "DONE" : wide[column / span]);
			}
			text_centred(&b, WK_FONT_LABEL, row < WK_ROWS - 1 ? 1.0f : 0.8f, &rectangle, label, color_white);
		}
	}
	/* the buttons' words */
	{
		static const char hint_chat[] = "B: CLOSE    X: DELETE    Y: SHIFT    +: SEND    UP / DOWN: CHANNEL";
		static const char hint_menu[] = "A: DONE    B: CANCEL";
		const char *hint = menu ? hint_menu : hint_chat;
		const int hint_length = menu ? (int)sizeof(hint_menu) - 1 : (int)sizeof(hint_chat) - 1;
		const float scale = 0.6f;
		const float width = wiiu_keyboard_text_width(atlas, WK_FONT_LABEL, scale, hint, hint_length);

		text_add(&b, WK_FONT_LABEL, scale, ((float)WK_WIDTH - width) * 0.5f, (float)WK_HINT_TOP + (float)WK_HINT_HEIGHT * 0.5f + 0.35f * WK_LABEL_PIXELS * scale, hint,
			hint_length, color_dim);
	}
	return b.count;
}

/* ---------- a menu's text field */

/* (network_chat.c's chat_keyboard_letters and chat_keyboard_symbols, no "|": the harness checks that they agree) */
static const char *const page_letters[HALO_CHAT_KEYBOARD_ROWS] = { "1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.?" };
static const char *const page_symbols[HALO_CHAT_KEYBOARD_ROWS] = { "1234567890", "!@#$%^&*()", "-_=+[]{};:", "\"'<>/\\?~,." };

char wiiu_keyboard_page_key(int symbols, int shift, int row, int column)
{
	char character;

	if (row < 0 || row >= HALO_CHAT_KEYBOARD_ROWS || column < 0 || column >= HALO_CHAT_KEYBOARD_COLUMNS)
		return 0;
	character = (symbols ? page_symbols : page_letters)[row][column];
	if (shift && character >= 'a' && character <= 'z')
		character = (char)(character - 'a' + 'A');
	return character;
}

void wiiu_keyboard_field_begin(struct wk_field *field, int masked)
{
	memset(field, 0, sizeof(*field));
	field->active = 1;
	field->masked = masked != 0;
	field->limit = WK_LINE_CHARACTERS;
	field->row = 1;
}

void wiiu_keyboard_field_end(struct wk_field *field)
{
	memset(field, 0, sizeof(*field));
}

void wiiu_keyboard_field_text(struct wk_field *field, const char *text, int maximum)
{
	int length = 0;

	if (!field->active)
		return;
	field->limit = maximum > 0 && maximum < WK_LINE_CHARACTERS ? maximum : WK_LINE_CHARACTERS;
	for (; text && *text && length < WK_LINE_CHARACTERS; text++)
	{
		if (*text >= ' ' && *text <= '~' && *text != '|')
			field->text[length++] = *text;
	}
	field->text[length] = 0;
	field->length = length;
}

int wiiu_keyboard_field_tap(struct wk_field *field, float x, float y, struct wk_action *action)
{
	struct wk_hit hit;

	action->kind = WK_ACTION_NONE;
	action->character = 0;
	if (!field->active)
		return action->kind;
	switch (wiiu_keyboard_hit(x, y, &hit))
	{
	case WK_HIT_KEY:
		field->row = hit.row;
		field->column = hit.column;
		if (hit.row < WK_ROWS - 1)
		{
			action->character = wiiu_keyboard_page_key(field->symbols, field->shift, hit.row, hit.column);
			if (action->character)
				action->kind = WK_ACTION_CHARACTER;
		}
		else
		{
			switch (hit.column / WK_WIDE_COLUMNS)
			{
			case 0:
				field->shift = !field->shift;
				action->kind = WK_ACTION_PAGE;
				break;
			case 1:
				field->symbols = !field->symbols;
				action->kind = WK_ACTION_PAGE;
				break;
			case 2:
				action->kind = WK_ACTION_CHARACTER;
				action->character = ' ';
				break;
			case 3:
				action->kind = WK_ACTION_DELETE;
				break;
			default:
				action->kind = WK_ACTION_DONE;
				break;
			}
		}
		break;
	case WK_HIT_CHANNEL:
		/* (DONE is the first chip; the others are not there) */
		if (hit.channel == 0)
			action->kind = WK_ACTION_DONE;
		break;
	case WK_HIT_CLOSE:
		action->kind = WK_ACTION_CANCEL;
		break;
	default:
		break;
	}
	return action->kind;
}

void wiiu_keyboard_field_view(const struct wk_field *field, struct halo_chat_keyboard_view *view)
{
	int row, column;

	memset(view, 0, sizeof(*view));
	if (!field->active)
		return;
	view->open = 1;
	view->mode = 1;
	view->masked = field->masked;
	view->limit = field->limit;
	view->shift = field->shift;
	view->symbols = field->symbols;
	view->row = field->row;
	view->column = field->column;
	memcpy(view->text, field->text, sizeof(view->text));
	view->text[sizeof(view->text) - 1] = 0;
	view->length = (int)strlen(view->text);
	for (row = 0; row < HALO_CHAT_KEYBOARD_ROWS; row++)
		for (column = 0; column < HALO_CHAT_KEYBOARD_COLUMNS; column++)
			view->keys[row][column] = wiiu_keyboard_page_key(field->symbols, field->shift, row, column);
}

/* the Windows virtual keys the game's input table knows (input_xbox.c's virtual_key_to_key_code): a letter or digit is its own; a symbol is on one of these keys, shifted or not */
int wiiu_keyboard_keystroke(char character, unsigned char *virtual_key, unsigned char *flags)
{
	static const struct { char plain, shifted; unsigned char key; } symbols[] =
	{
		{ '1', '!', '1' }, { '2', '@', '2' }, { '3', '#', '3' }, { '4', '$', '4' }, { '5', '%', '5' }, { '6', '^', '6' }, { '7', '&', '7' }, { '8', '*', '8' }, { '9', '(', '9' },
		{ '0', ')', '0' }, { ';', ':', 0xBA }, { '=', '+', 0xBB }, { ',', '<', 0xBC }, { '-', '_', 0xBD }, { '.', '>', 0xBE }, { '/', '?', 0xBF }, { '`', '~', 0xC0 },
		{ '[', '{', 0xDB }, { '\\', '|', 0xDC }, { ']', '}', 0xDD }, { '\'', '"', 0xDE },
	};
	unsigned i;

	*virtual_key = 0;
	*flags = 0;
	if (character == ' ')
	{
		*virtual_key = 0x20;
		return 1;
	}
	if (character >= 'a' && character <= 'z')
	{
		*virtual_key = (unsigned char)(character - 'a' + 'A');
		return 1;
	}
	if (character >= 'A' && character <= 'Z')
	{
		*virtual_key = (unsigned char)character;
		*flags = 0x02;
		return 1;
	}
	for (i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++)
	{
		if (character == symbols[i].plain || character == symbols[i].shifted)
		{
			*virtual_key = symbols[i].key;
			*flags = character == symbols[i].shifted && character != symbols[i].plain ? 0x02 : 0;
			return 1;
		}
	}
	return 0;
}

static struct
{
	struct wk_keystroke queue[WK_KEYSTROKE_QUEUE];
	unsigned int read, write;
} keystrokes;

void wiiu_keyboard_keys_clear(void)
{
	keystrokes.read = keystrokes.write = 0;
}

static void key_push(unsigned char virtual_key, char ascii, unsigned char flags)
{
	if (keystrokes.write - keystrokes.read >= WK_KEYSTROKE_QUEUE)
		return;
	keystrokes.queue[keystrokes.write % WK_KEYSTROKE_QUEUE].virtual_key = virtual_key;
	keystrokes.queue[keystrokes.write % WK_KEYSTROKE_QUEUE].ascii = ascii;
	keystrokes.queue[keystrokes.write % WK_KEYSTROKE_QUEUE].flags = flags;
	keystrokes.write++;
}

/* a press and its release (0x40: XINPUT_DEBUG_KEYSTROKE_FLAG_KEYUP), together or not at all: the queue has room for both */
static void key_pair(unsigned char virtual_key, char ascii, unsigned char flags)
{
	if (keystrokes.write - keystrokes.read + 2 > WK_KEYSTROKE_QUEUE)
		return;
	key_push(virtual_key, ascii, flags);
	key_push(virtual_key, ascii, (unsigned char)(flags | 0x40));
}

void wiiu_keyboard_keys_type(char character)
{
	unsigned char virtual_key, flags;

	if (character >= ' ' && character <= '~' && character != '|' && wiiu_keyboard_keystroke(character, &virtual_key, &flags))
		key_pair(virtual_key, character, flags);
}

void wiiu_keyboard_keys_delete(void)
{
	/* (VK_BACK; the Xbox keyboard's Ascii for it is 8) */
	key_pair(0x08, 8, 0);
}

int wiiu_keyboard_keys_next(struct wk_keystroke *keystroke)
{
	if (keystrokes.read == keystrokes.write)
		return 0;
	*keystroke = keystrokes.queue[keystrokes.read % WK_KEYSTROKE_QUEUE];
	keystrokes.read++;
	return 1;
}

/* ---------- publishing */

static struct
{
	volatile unsigned int sequence;
	struct halo_chat_keyboard_view view;
} published;

void wiiu_keyboard_publish(const struct halo_chat_keyboard_view *view)
{
	/* (one writer: an odd sequence says it is writing) */
	if (!memcmp(&published.view, view, sizeof(*view)))
		return;
	published.sequence++;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	memcpy(&published.view, view, sizeof(*view));
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	published.sequence++;
}

void wiiu_keyboard_snapshot(struct halo_chat_keyboard_view *view)
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
	/* (the writer is slow to finish: the keyboard as it was is not known whole; closed is safe, the next frame draws it) */
	memset(view, 0, sizeof(*view));
}
