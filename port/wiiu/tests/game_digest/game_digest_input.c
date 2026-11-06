/*
GAME_DIGEST_INPUT.C

The stand-ins for what reads the console's controllers, for the game digest built with --wiiu-input (build.py): xinput_wiiu.c, the Wii U's own controller layer,
is linked in place of SDL's, over these. No pad is connected: the GamePad's port 0 is always there and at rest, and the other ports have what the
script of debug.virtual_gamepads gives them (wiiu_virtual_pads.c). The script's clock is the game digest's, by the frames (game_digest_glue.c).
*/

#include "platform.h"
#include "port_config.h"
#include "wiiu_input.h"
#include "wiiu_virtual_pads.h"

#include <stdarg.h>
#include <wchar.h>
#include <stdio.h>
#include <string.h>

unsigned long game_digest_milliseconds(void);

void wiiu_log(const char *format, ...)
{
	va_list arguments;

	fputs("wiiu: ", stderr);
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fputc('\n', stderr);
}

void wiiu_input_set_layout(int layout)
{
	(void)layout;
}

void wiiu_input_poll(int several_players)
{
	(void)several_players;
}

int wiiu_input_port_present(unsigned int port)
{
	return port == 0;
}

int wiiu_input_read(unsigned int port, struct wiiu_pad *pad)
{
	memset(pad, 0, sizeof(*pad));
	return port == 0;
}

void wiiu_input_rumble(unsigned int port, float strength)
{
	(void)port;
	(void)strength;
}

void wiiu_input_set_gyro_space(int space)
{
	(void)space;
}

void wiiu_input_gyro_take(float *turn, float *pitch)
{
	*turn = *pitch = 0.0f;
}

void wiiu_input_set_wimote(const char *layout, const char *look, float box, float speed, int invert_x)
{
	(void)layout;
	(void)look;
	(void)box;
	(void)speed;
	(void)invert_x;
}

int wiiu_input_remote_look_take(float *turn, float *pitch)
{
	*turn = *pitch = 0.0f;
	return 0;
}

unsigned long wiiu_input_milliseconds(void)
{
	return game_digest_milliseconds();
}

int wiiu_update_panel_input(unsigned port, unsigned buttons)
{
	(void)port;
	(void)buttons;
	return 0;
}

void wiiu_update_menus(int main_menu, int menus)
{
	(void)main_menu;
	(void)menus;
}

void wiiu_skip_progress(float progress)
{
	(void)progress;
}

void wiiu_overlay_toggle(void)
{
}

/* the game's (player_profile.c): a new player profile on the disk */
long player_profile_new(short local_player_index, wchar_t *name);

/* platform_wiiu.c's: the script's clock starts when the network lobby first opens. The disk the digest runs on has no player's profile, and each player of a
split screen game needs a profile of their own (join4_select: "Another player has that profile."), as the card has: four are made, once, the first time. */
void halo_virtual_gamepads_start(void)
{
	static int made;

	if (!made && config_integer("debug.virtual_gamepads") > 0)
	{
		static wchar_t alpha[] = L"Alpha", bravo[] = L"Bravo", charlie[] = L"Charlie", delta[] = L"Delta";
		wchar_t *names[4] = { alpha, bravo, charlie, delta };
		int index;

		made = 1;
		for (index = 0; index < 4; index++)
			fprintf(stderr, "digest input: profile %d made: %ld\n", index, player_profile_new(0, names[index]));
	}
	wiiu_virtual_pads_start((int)config_integer("debug.virtual_gamepads"), wiiu_input_milliseconds());
}
