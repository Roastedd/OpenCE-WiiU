#include "harness.h"
#include <stdarg.h>
#include <wchar.h>

/* The BOTS row of the lobby of a game on this machine (port/linux/game/menu_screens.inl, scr_lobby_bots_text): the count the setting holds, the keys that change it when the screens are on, the
words for a game type with no bots, and nothing for a game that is hosted or joined. Stand-ins for the engine's world. */

typedef unsigned short wchar_ascii;
struct network_game
{
	struct
	{
		long game_engine_index;
	} variant;
};

static int local = 1, have_server = 1, screens, allowed = 1;
static long wanted = 3;

static size_t ustrlen(wchar_t const *text)
{
	size_t length = 0;

	while (text[length])
		length++;
	return length;
}
static int usnprintf(wchar_t *buffer, size_t size, wchar_t const *format, ...)
{
	/* (the three formats the code under test uses, written out) */
	va_list arguments;
	char narrow[96];
	size_t index;

	va_start(arguments, format);
	if (!wcscmp(format, L"\r\nBots: %ld%s"))
	{
		long count = va_arg(arguments, long);
		wchar_t const *keys = va_arg(arguments, wchar_t const *);
		char keys_narrow[16];

		for (index = 0; keys[index] && index < sizeof(keys_narrow) - 1; index++)
			keys_narrow[index] = (char)keys[index];
		keys_narrow[index] = 0;
		snprintf(narrow, sizeof(narrow), "\r\nBots: %ld%s", count, keys_narrow);
	}
	else
		snprintf(narrow, sizeof(narrow), "\r\nNo bots in this game type");
	va_end(arguments);
	for (index = 0; narrow[index] && index + 1 < size; index++)
		buffer[index] = (wchar_t)narrow[index];
	buffer[index] = 0;
	return (int)index;
}
static boolean network_game_is_splitscreen_local(void) { return local; }
static void *global_network_game_server_get(void) { static int server; return have_server ? &server : NULL; }
static boolean scr_enabled(void) { return screens != 0; }
static boolean bots_game_allowed(long engine) { return allowed && engine == 1; }
static long bots_wanted_count(void) { return wanted; }

#include "lobby_bots_code.inc"

static void narrow(wchar_t const *text, char *out, int size)
{
	int index;

	for (index = 0; text[index] && index < size - 1; index++)
		out[index] = (char)text[index];
	out[index] = 0;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	struct network_game game;
	wchar_t text[256];
	char out[256];

	memset(&game, 0, sizeof(game));
	game.variant.game_engine_index = 1;
	CASE("count")
	{
		/* the lobby of a game on this machine: the count after the panel's lines, with the keys when the screens are on */
		wcscpy(text, L"Slayer");
		scr_lobby_bots_text(&game, text, 256);
		narrow(text, out, sizeof(out));
		CHECK(!strcmp(out, "Slayer\r\nBots: 3"), "'%s'", out);
		screens = 1;
		wcscpy(text, L"Slayer");
		scr_lobby_bots_text(&game, text, 256);
		narrow(text, out, sizeof(out));
		CHECK(!strcmp(out, "Slayer\r\nBots: 3  (L/R)"), "'%s'", out);
		wanted = 0;
		wcscpy(text, L"Slayer");
		scr_lobby_bots_text(&game, text, 256);
		narrow(text, out, sizeof(out));
		CHECK(strstr(out, "Bots: 0"), "'%s'", out);
		wanted = 3;
		/* a game type with no bots says so */
		game.variant.game_engine_index = 3;
		wcscpy(text, L"CTF");
		scr_lobby_bots_text(&game, text, 256);
		narrow(text, out, sizeof(out));
		CHECK(strstr(out, "No bots in this game type"), "'%s'", out);
		game.variant.game_engine_index = 1;
	}
	else CASE("not-here")
	{
		/* hosted for others, joined, or no game: nothing is added */
		wcscpy(text, L"Slayer");
		local = 0;
		scr_lobby_bots_text(&game, text, 256);
		CHECK(!wcscmp(text, L"Slayer"), "a hosted game");
		local = 1;
		have_server = 0;
		scr_lobby_bots_text(&game, text, 256);
		CHECK(!wcscmp(text, L"Slayer"), "a game joined");
		have_server = 1;
		scr_lobby_bots_text(NULL, text, 256);
		CHECK(!wcscmp(text, L"Slayer"), "no game");
		/* a text with no room is not overrun */
		wcscpy(text, L"Slayer");
		scr_lobby_bots_text(&game, text, 8);
		CHECK(!wcscmp(text, L"Slayer"), "no room: nothing is added");
	}
	else
		return 2;
	return 0;
}
