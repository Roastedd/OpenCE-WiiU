/*
WIIU_KEYBOARD_FONT.C

The GamePad keyboard's glyph atlas (wiiu_keyboard.h): the game's menu fonts (ui\large_ui's Overpass-900 for the keys, ui\small_ui's Overpass-750 for the line) rasterized
with stb_truetype (text_hires_raster) on a thread of its own when a network game starts, so that the first keystroke rasterizes nothing and the game's thread waits for
nothing. The GX2 thread makes its texture of the atlas when it is ready (wiiu_gx2.c, keyboard_update) and draws it only while the keyboard is open.
*/

#include "wiiu_keyboard.h"
#include "wiiu_shell.h"
#include "port_config.h"
#include "text_hires.h"

#include <pthread.h>
#include <string.h>

/* (wiiu_shell.c: core 0, below the game thread's priority, named) */
void wiiu_thread_background(const char *name);

static struct wk_atlas atlas;
/* 0 not started, 1 being made, 2 ready, 3 could not be made */
static volatile int state;

int wiiu_keyboard_enabled(void)
{
	static int enabled;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("input.wiiu_gamepad_keyboard");
	}
	return enabled;
}

static int rasterize(int font, unsigned long code, unsigned char *buffer, int stride, int max_width, int max_height, int *left, int *top, int *width, int *height, float *advance)
{
	if (font == WK_FONT_LABEL)
		return text_hires_raster("ui\\large_ui", WK_LABEL_PIXELS, code, buffer, stride, max_width, max_height, left, top, width, height, advance);
	return text_hires_raster("ui\\small_ui", WK_TEXT_PIXELS, code, buffer, stride, max_width, max_height, left, top, width, height, advance);
}

static void *atlas_thread(void *unused)
{
	(void)unused;
	wiiu_thread_background("keyboard atlas");
	if (wiiu_keyboard_atlas_build(&atlas, rasterize))
	{
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		state = 2;
		wiiu_log("GamePad keyboard: the glyphs are made (%d x %d, labels %.0f px, line %.0f px)", WK_ATLAS_WIDTH, WK_ATLAS_HEIGHT, (double)WK_LABEL_PIXELS, (double)WK_TEXT_PIXELS);
	}
	else
	{
		state = 3;
		wiiu_log("GamePad keyboard: the glyphs could not be made (the fonts are missing or the atlas is full): the TV's keyboard is all there is");
	}
	return NULL;
}

void wiiu_keyboard_prewarm(void)
{
	pthread_attr_t attributes;
	pthread_t thread;

	if (state != 0 || !wiiu_keyboard_enabled())
		return;
	state = 1;
	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, 128 * 1024);
	if (pthread_create(&thread, &attributes, atlas_thread, NULL) == 0)
		pthread_detach(thread);
	else
		state = 3;
	pthread_attr_destroy(&attributes);
}

const struct wk_atlas *wiiu_keyboard_atlas(void)
{
	if (state != 2)
		return NULL;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	return &atlas;
}

/* the keyboard that is drawn is the menu's field while one is edited, else the chat's, as the game published it; both come from the game's thread */
static struct halo_chat_keyboard_view chat_view;
static struct wk_field field;

static void publish_current(void)
{
	struct halo_chat_keyboard_view view;

	if (field.active)
		wiiu_keyboard_field_view(&field, &view);
	else
		view = chat_view;
	wiiu_keyboard_publish(&view);
}

int wiiu_keyboard_shown(void)
{
	struct halo_chat_keyboard_view view;

	if (!wiiu_keyboard_atlas() || !wiiu_keyboard_enabled())
		return 0;
	wiiu_keyboard_snapshot(&view);
	return view.open;
}

/* the game's thread: the chat's keyboard as it is now, once a frame; a network game started */
void halo_chat_keyboard_publish(const struct halo_chat_keyboard_view *view)
{
	chat_view = *view;
	publish_current();
}

void halo_chat_keyboard_prewarm(void)
{
	wiiu_keyboard_prewarm();
}

/* ---------- a menu's text field */

void wiiu_keyboard_field_set(int typing, int masked)
{
	if (typing && wiiu_keyboard_enabled())
	{
		/* (the glyphs, if they are not made: the keyboard shows when they are) */
		wiiu_keyboard_prewarm();
		wiiu_keyboard_keys_clear();
		wiiu_keyboard_field_begin(&field, masked);
	}
	else
	{
		wiiu_keyboard_field_end(&field);
		wiiu_keyboard_keys_clear();
	}
	publish_current();
}

int wiiu_keyboard_field_active(void)
{
	return field.active;
}

/* menu_functions.c, every frame the field is edited: what is typed, for the keyboard's line */
void platform_text_field_text(char const *text, int maximum)
{
	if (!field.active)
		return;
	wiiu_keyboard_field_text(&field, text, maximum);
	publish_current();
}

int wiiu_keyboard_field_press(float x, float y)
{
	struct wk_action action;

	if (!field.active || !wiiu_keyboard_shown())
		return WK_ACTION_NONE;
	wiiu_keyboard_field_tap(&field, x, y, &action);
	if (action.kind == WK_ACTION_CHARACTER)
		wiiu_keyboard_keys_type(action.character);
	else if (action.kind == WK_ACTION_DELETE)
		wiiu_keyboard_keys_delete();
	publish_current();
	return action.kind;
}
