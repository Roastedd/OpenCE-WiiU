/*
PLATFORM_WIIU.C

The platform layer's calls that the Wii U does not need yet, or has no use for, answered as a build with nothing to answer them with would: the
high-resolution HUD, text and menu art (OpenGL drawing in the Linux builds; the Wii U has none of them), the keyboard, mouse and touch aiming,
the clipboard, the display settings, the network test's input hooks, the text chat's on-screen keyboard (it reads the Linux
builds' gamepads: the Wii U shows the chat, and has no way to type it yet), controller remapping, questions in a message box, a browser, and the
self-updater's HTTPS (the master servers' lists are not fetched: the server browser lists the brokers' games and the LAN's). Each is what the
Linux code does when its feature is off. What the Wii U does need (input, audio, voice chat, files, sockets) is in units of its own.
*/

#include "platform.h"
#include "sdl_platform.h"
#include "port_config.h"
#include "hud_hires.h"
#include "text_hires.h"
#include "menu_files.h"
#include "halo_menus.h"
#include "controller_remap.h"
#include "update.h"

#include <stdio.h>
#include <string.h>

/* (the game's floating-point type, cseries.h's) */
typedef float real;

/* ---------- display settings (the Wii U's display is the TV's, fixed) */

void platform_display_apply(void)
{
}

int platform_display_resolutions(long *widths, long *heights, int maximum)
{
	(void)widths;
	(void)heights;
	(void)maximum;
	return 0;
}

int platform_window_sizes(long *widths, long *heights, int maximum)
{
	(void)widths;
	(void)heights;
	(void)maximum;
	return 0;
}

/* graphics_quality.c's: whether the split screen's views hold the frames to 30 a second (display.split_screen_fps) */
int graphics_quality_split_screen_capped(void);

/* the TV's refresh is 60 Hz and the game's ticks 30 Hz: with display.interpolation, two frames to a tick (the frames between ticks drawn from the blend of the last two); without,
the game's own frame throttle holds it to 30 frames a second, a frame to a tick, each drawn as the tick left the world. The game asks every frame (main.c's main_update_time,
its frame's drawing and main_rasterizer_throttle; render_interpolation.c), so while three or four views are drawn the split screen's 30 frames a second
(graphics_quality_split_screen_capped, set as the views' frame begins) are display.interpolation off for those frames, with the setting itself left as the player has it */
int halo_interpolation_enabled(void)
{
	static int enabled = 1;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("display.interpolation");
	}
	return enabled && !graphics_quality_split_screen_capped();
}

int platform_audio_devices(int recording, char (*names)[PLATFORM_AUDIO_DEVICE_NAME_SIZE], int maximum)
{
	(void)recording;
	(void)names;
	(void)maximum;
	return 0;
}

/* ---------- messages, the clipboard, the keyboard */

void platform_show_message(char const *title, char const *message)
{
	platform_log("%s: %s", title, message);
}

int platform_clipboard_get(char *text, int size)
{
	(void)text;
	(void)size;
	return 0;
}

void platform_clipboard_set(char const *text)
{
	(void)text;
}

void platform_text_field(int typing, int password)
{
	(void)typing;
	(void)password;
}

void platform_text_typing(int typing)
{
	(void)typing;
}

void platform_binding_capture_begin(void)
{
}

int platform_binding_capture_poll(int *input)
{
	(void)input;
	return 0;
}

void platform_scoreboard_scroll(int open, long *notches, long *pages)
{
	(void)open;
	*notches = 0;
	*pages = 0;
}

/* the game's data is put on the card by the user: nothing offers it */
BOOL platform_offer_game_data(const char *destination)
{
	(void)destination;
	return FALSE;
}

void halo_input_name(int input, char *name, size_t size)
{
	(void)input;
	if (size)
		name[0] = 0;
}

unsigned long halo_keyboard_actions(short controller_index)
{
	(void)controller_index;
	return 0;
}

/* voice chat's push to talk: the GamePad's TV button, the one button the game does not have (wiiu_input.c, which takes it from the system's TV menu
while it is asked for) */
int wiiu_input_push_to_talk(void);

int halo_push_to_talk_held(void)
{
	return wiiu_input_push_to_talk();
}

/* ---------- aiming and moving by mouse or touch: none */

int halo_linux_touch_aiming(short gamepad_index)
{
	(void)gamepad_index;
	return 0;
}

int halo_linux_touch_look(short gamepad_index, real *yaw, real *pitch, real *gyro_yaw, real *gyro_pitch)
{
	(void)gamepad_index;
	(void)yaw;
	(void)pitch;
	(void)gyro_yaw;
	(void)gyro_pitch;
	return 0;
}

int halo_linux_touch_move(short controller_index, real *forward, real *strafe)
{
	(void)controller_index;
	(void)forward;
	(void)strafe;
	return 0;
}

/* ---------- the network test's input hooks (port/linux/game/network_test.c): there is no scripted input on the console */

void test_input_hold_action(int hold)
{
	(void)hold;
}

void test_input_press(unsigned long buttons)
{
	(void)buttons;
}

/* (the menus' files and art, and the high-resolution HUD, text and titles: menu_files.c, hud_hires.c and text_hires.c, built for the Wii U) */

/* (voice chat: wiiu_voice_audio.c) */

/* ---------- questions, a browser (sdl_platform.c's) */

/* (port_mods.c: mods that cannot be played together are played all, as when a desktop cannot ask) */
int platform_ask(const char *title, const char *text, const char *const *labels, int count, int default_index)
{
	(void)labels;
	(void)count;
	(void)default_index;
	platform_log("%s: %s (not asked: the Wii U has no message box)", title, text);
	return -1;
}

/* (player_link.c: a master's page to link a Discord account, which Network Setup does not offer on the Wii U) */
void platform_open_url(const char *url)
{
	platform_log("not opened (the Wii U has no browser to open): %s", url);
}

void platform_binding_capture_cancel(void)
{
}

/* ---------- the self-updater's HTTPS (posix_update.c, in WIIU_EXCLUDE: no TLS sockets or certificate authorities here) */

int update_https_request(const char *method, const char *url, const char *headers, const void *body,
	unsigned long body_size, unsigned long maximum, struct update_response *response, char *error, int error_size)
{
	(void)method;
	(void)url;
	(void)headers;
	(void)body;
	(void)body_size;
	(void)maximum;
	memset(response, 0, sizeof(*response));
	if (error && error_size > 0)
		snprintf(error, (size_t)error_size, "the Wii U makes no HTTPS requests");
	return 0;
}

/* ---------- the keyboard and the gamepads, as xinput_sdl.c has them: no keyboard, no on-screen keyboard's reading, no virtual
gamepads, no debug camera's mouse */

unsigned long halo_chat_pad_buttons(void)
{
	return 0;
}

unsigned long halo_chat_pad_buttons_for_port(short port)
{
	(void)port;
	return 0;
}

int halo_chat_taps(float *x, float *y)
{
	(void)x;
	(void)y;
	return 0;
}

int halo_chat_wheel_steps(void)
{
	return 0;
}

int halo_chat_touch_layout(void)
{
	return 0;
}

void halo_input_shown_name(const char *binding, char *shown, size_t size)
{
	if (size)
		snprintf(shown, size, "%s", binding ? binding : "");
}

unsigned char halo_keyboard_join_held(void)
{
	return 0;
}

void halo_keyboard_alone_set(unsigned char alone)
{
	(void)alone;
}

unsigned char halo_keyboard_shares_gamepad(void)
{
	return 0;
}

void halo_virtual_gamepads_start(void)
{
}

int halo_linux_debug_camera_input(int *middle, int *up, int *down, float *wheel)
{
	*middle = 0;
	*up = 0;
	*down = 0;
	*wheel = 0.0f;
	return 0;
}

/* ---------- controller remapping (controller_remap_input.c, on SDL's gamepads): none; the Wii U's buttons are xinput_wiiu.c's, and
Controller Setup has no BUTTON REMAPPING there */

void controller_remap_input_reset(void)
{
}

void controller_remap_capture_begin(short port)
{
	(void)port;
}

void controller_remap_capture_end(void)
{
}

/* (3: the capture ended, as with no device to capture from) */
int controller_remap_capture_poll(int *button)
{
	(void)button;
	return 3;
}

int controller_remap_connected(short port)
{
	(void)port;
	return 0;
}

unsigned int controller_remap_held(short port)
{
	(void)port;
	return 0;
}

unsigned int controller_remap_pressed(short port, unsigned int mask)
{
	(void)port;
	(void)mask;
	return 0;
}
