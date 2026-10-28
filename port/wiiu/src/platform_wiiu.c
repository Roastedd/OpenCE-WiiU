/*
PLATFORM_WIIU.C

The platform layer's calls that the Wii U does not need yet, or has no use for, answered as a build with nothing to answer them with would: the
high-resolution HUD, text and menu art (OpenGL drawing in the Linux builds; the Wii U has none of them), the keyboard, mouse and touch aiming,
the clipboard, the display settings, the voice chat, the network test's input hooks. Each is what the Linux code does when its feature is off.
What the Wii U does need (input, audio, files, sockets) is in units of its own.
*/

#include "platform.h"
#include "sdl_platform.h"
#include "port_config.h"
#include "hud_hires.h"
#include "text_hires.h"
#include "menu_files.h"
#include "voice_audio.h"
#include "halo_menus.h"

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

/* the TV's refresh is 60 Hz and the game's ticks 30 Hz: with display.interpolation, two frames to a tick (the frames between ticks drawn from the blend of the last two); without,
the game's own frame throttle holds it to 30 frames a second, a frame to a tick, each drawn as the tick left the world */
int halo_interpolation_enabled(void)
{
	static int enabled = 1;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		enabled = config_boolean("display.interpolation");
	}
	return enabled;
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

int halo_push_to_talk_held(void)
{
	return 0;
}

/* ---------- aiming and moving by mouse or touch: none */

int halo_linux_mouse_aiming(short gamepad_index)
{
	(void)gamepad_index;
	return 0;
}

int halo_linux_mouse_look(short gamepad_index, real *yaw, real *pitch)
{
	(void)gamepad_index;
	(void)yaw;
	(void)pitch;
	return 0;
}

int halo_linux_touch_aiming(short gamepad_index)
{
	(void)gamepad_index;
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

void test_input_set_game_time(long ticks)
{
	(void)ticks;
}

/* (the menus' files and art, and the high-resolution HUD, text and titles: menu_files.c, hud_hires.c and text_hires.c, built for the Wii U) */

/* ---------- voice chat: none (the GamePad's microphone, later) */

int voice_audio_microphone(int open)
{
	(void)open;
	return 0;
}

int voice_audio_read_frame(float *frame)
{
	(void)frame;
	return 0;
}

float voice_audio_level(const float *frame)
{
	(void)frame;
	return 0.0f;
}

int voice_audio_encode(const float *frame, int bitrate, unsigned char *packet, int maximum)
{
	(void)frame;
	(void)bitrate;
	(void)packet;
	(void)maximum;
	return 0;
}

void voice_audio_forget(int speaker)
{
	(void)speaker;
}

void voice_audio_forget_all(void)
{
}

int voice_audio_speaking(int speaker)
{
	(void)speaker;
	return 0;
}

void voice_audio_set_volume(float volume)
{
	(void)volume;
}

void voice_audio_play(int speaker, unsigned short sequence, const unsigned char *packet, int length, float gain, float pan)
{
	(void)speaker;
	(void)sequence;
	(void)packet;
	(void)length;
	(void)gain;
	(void)pan;
}

void voice_audio_mix(float *output, unsigned long frames)
{
	(void)output;
	(void)frames;
}
