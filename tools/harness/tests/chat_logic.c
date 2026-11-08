/*
CHAT_LOGIC.C (test)

The rules of port/linux/game/network_chat.c's text chat: what a line's text
may hold (another machine's is untrusted), how many lines the host takes
from a machine, who hears a team's line, how long a line shows, and the
host's /mute (what it reads from a line, the player a name means, how long a
mute lasts), the links it can refuse and which lines its settings refuse.
test_chat_logic.py takes the constants and structures (config.inc) and the
code (under_test.inc).
*/

#include "harness.h"

#ifndef NUMBEROF
#define NUMBEROF(array) (sizeof(array) / sizeof((array)[0]))
#endif
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define csstrlen strlen
#define csstrcmp strcmp
#define csstrncpy strncpy
#define csmemset memset
#include "halo_keyboard.h"

#include "config.inc"
#include "under_test.inc"

/* the text cleaned from a source of size bytes */
static short clean(
	char *text,
	char const *source,
	short size)
{
	return chat_text_clean(text, source, size);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	char text[CHAT_TEXT_SIZE];
	char source[256];

	/* printable ASCII kept as it is, the spaces at its ends dropped */
	CASE("text-printable")
	{
		CHECK(clean(text, "  eat my dedi  ", 64) == 11, "length %d", (int)strlen(text));
		CHECK(!strcmp(text, "eat my dedi"), "'%s'", text);
		CHECK(clean(text, "a~!{}", 64) == 5 && !strcmp(text, "a~!{}"), "'%s'", text);
	}

	/* control characters, DEL and bytes past ASCII never reach the screen */
	CASE("text-control-characters")
	{
		CHECK(clean(text, "a\tb\x1b" "c\x7f" "d\xe9", 64) == 8, "length %d", (int)strlen(text));
		CHECK(!strcmp(text, "a?b?c?d?"), "'%s'", text);
	}

	/* "|", which the game's text drawing takes for the start of a code (one
	at a line's end reads past it), never reaches the screen, sent or typed */
	CASE("text-pipe")
	{
		char line[CHAT_TEXT_SIZE] = "a";

		CHECK(clean(text, "gg|", 64) == 3 && !strcmp(text, "gg?"), "'%s'", text);
		CHECK(clean(text, "a|nb", 64) == 4 && !strcmp(text, "a?nb"), "'%s'", text);
		chat_text_append(line, "|b|");
		CHECK(!strcmp(line, "ab"), "'%s'", line);
	}

	/* a message cut at CHAT_TEXT_SIZE - 1 characters, and never read past
	its buffer when another machine sent it unended */
	CASE("text-bounds")
	{
		memset(source, 'x', sizeof(source));
		CHECK(clean(text, source, sizeof(source)) == CHAT_TEXT_SIZE - 1, "length %d", (int)strlen(text));
		CHECK(text[CHAT_TEXT_SIZE - 1] == 0, "not ended");
		memset(source, 'y', sizeof(source));
		source[10] = 'z';
		CHECK(clean(text, source, 5) == 5 && !strcmp(text, "yyyyy"), "read past its size: '%s'", text);
	}

	/* nothing but spaces is no line */
	CASE("text-empty")
	{
		CHECK(clean(text, "     ", 64) == 0 && !text[0], "'%s'", text);
		CHECK(clean(text, "", 64) == 0, "'%s'", text);
	}

	/* typing and pasting: printable ASCII only, no further than a line holds */
	CASE("append")
	{
		char line[CHAT_TEXT_SIZE] = "hi";

		chat_text_append(line, " there\n\x01!");
		CHECK(!strcmp(line, "hi there!"), "'%s'", line);
		memset(source, 'w', 200);
		source[200] = 0;
		chat_text_append(line, source);
		CHECK(strlen(line) == CHAT_TEXT_SIZE - 1, "length %d", (int)strlen(line));
	}

	/* CHAT_BURST lines at once, then one a period */
	CASE("rate-burst")
	{
		struct chat_rate rate;
		unsigned long now = 1000;
		int index;

		memset(&rate, 0, sizeof(rate));
		for (index = 0; index < CHAT_BURST; index++)
			CHECK(chat_rate_take(&rate, now), "line %d of the burst refused", index);
		CHECK(!chat_rate_take(&rate, now), "a line past the burst taken");
		CHECK(!chat_rate_take(&rate, now + CHAT_REFILL_MILLISECONDS - 1), "a line taken before its period");
		CHECK(chat_rate_take(&rate, now + CHAT_REFILL_MILLISECONDS), "the line a period later refused");
		CHECK(!chat_rate_take(&rate, now + CHAT_REFILL_MILLISECONDS), "two lines in one period");
	}

	/* a machine quiet a long while has its burst again, and no more */
	CASE("rate-refill")
	{
		struct chat_rate rate;
		unsigned long now = 5000;
		int index;
		int taken = 0;

		memset(&rate, 0, sizeof(rate));
		for (index = 0; index < CHAT_BURST; index++)
			chat_rate_take(&rate, now);
		now += 100 * CHAT_REFILL_MILLISECONDS;
		for (index = 0; index < 4 * CHAT_BURST; index++)
			taken += chat_rate_take(&rate, now);
		CHECK(taken == CHAT_BURST, "%d lines taken at once after a long quiet", taken);
	}

	/* the clock wrapping round (system_milliseconds) changes nothing */
	CASE("rate-wrap")
	{
		struct chat_rate rate;
		unsigned long now = 0xFFFFFFFFUL - 200;
		int index;

		memset(&rate, 0, sizeof(rate));
		for (index = 0; index < CHAT_BURST; index++)
			CHECK(chat_rate_take(&rate, now), "line %d refused", index);
		CHECK(!chat_rate_take(&rate, now + 300), "a line taken past the burst across the wrap");
		CHECK(chat_rate_take(&rate, now + CHAT_REFILL_MILLISECONDS), "the line a period later refused across the wrap");
	}

	/* a team's line by that team alone, or by everyone without teams */
	CASE("hears")
	{
		CHECK(chat_hears(_chat_channel_everyone, TRUE, 0, 1, NONE, 7), "a line to everyone not heard by the other team");
		CHECK(chat_hears(_chat_channel_team, TRUE, 1, 1, NONE, NONE), "a team's line not heard by the team");
		CHECK(!chat_hears(_chat_channel_team, TRUE, 0, 1, 7, 7), "a team's line heard by the other team");
		CHECK(chat_hears(_chat_channel_team, FALSE, 0, 1, NONE, NONE), "a team's line not heard without teams");
	}

	/* a vehicle's line by those riding in the sender's vehicle alone, of any
	team; by no one when the sender rides in none */
	CASE("hears-vehicle")
	{
		CHECK(chat_hears(_chat_channel_vehicle, TRUE, 0, 1, 7, 7), "not heard in the same vehicle (another team)");
		CHECK(!chat_hears(_chat_channel_vehicle, TRUE, 0, 0, 7, 8), "heard in another vehicle");
		CHECK(!chat_hears(_chat_channel_vehicle, TRUE, 0, 0, 7, NONE), "heard on foot");
		CHECK(!chat_hears(_chat_channel_vehicle, FALSE, 0, 0, NONE, NONE), "heard on foot from a sender on foot");
	}

	/* a line's channel through its flags, as sent */
	CASE("flags")
	{
		short channel;

		for (channel = 0; channel < NUMBER_OF_CHAT_CHANNELS; channel++)
			CHECK(chat_channel_from_flags(chat_channel_flags(channel)) == channel, "channel %d came back as %d",
				channel, chat_channel_from_flags(chat_channel_flags(channel)));
		CHECK(chat_channel_from_flags(0) == _chat_channel_everyone, "no flags is not everyone");
	}

	/* shown whole, then fading, then gone; whole while a line is typed */
	CASE("alpha")
	{
		CHECK(chat_line_alpha(0, FALSE) == 1.0f, "a new line faded");
		CHECK(chat_line_alpha(CHAT_LINE_MILLISECONDS - 1, FALSE) == 1.0f, "faded early");
		CHECK(chat_line_alpha(CHAT_LINE_MILLISECONDS + CHAT_FADE_MILLISECONDS / 2, FALSE) > 0.4f &&
			chat_line_alpha(CHAT_LINE_MILLISECONDS + CHAT_FADE_MILLISECONDS / 2, FALSE) < 0.6f, "not half way");
		CHECK(chat_line_alpha(CHAT_LINE_MILLISECONDS + CHAT_FADE_MILLISECONDS, FALSE) == 0.0f, "still shown");
		CHECK(chat_line_alpha(10 * CHAT_LINE_MILLISECONDS, TRUE) == 1.0f, "faded while typing");
	}

	/* /mute: the player (a name of more than one word too) and the minutes,
	the word in either case; the default when it gives none, a day at most;
	the name cut to its buffer */
	CASE("command-mute")
	{
		struct
		{
			char name[8];
			char canary[64];
		} bounded;
		char name[CHAT_TEXT_SIZE];
		short minutes = 0;

		CHECK(chat_command_parse("/mute Milenko 5", name, sizeof(name), &minutes) == _chat_command_mute,
			"not a mute");
		CHECK(!strcmp(name, "Milenko") && minutes == 5, "'%s' %d", name, minutes);
		CHECK(chat_command_parse("/MUTE Master Chief   12", name, sizeof(name), &minutes) == _chat_command_mute,
			"not a mute in capitals");
		CHECK(!strcmp(name, "Master Chief") && minutes == 12, "'%s' %d", name, minutes);
		CHECK(chat_command_parse("/mute jonas", name, sizeof(name), &minutes) == _chat_command_mute &&
			!strcmp(name, "jonas") && minutes == CHAT_MUTE_DEFAULT_MINUTES, "'%s' %d", name, minutes);
		/* (a name all digits, alone, is the name) */
		CHECK(chat_command_parse("/mute 1337", name, sizeof(name), &minutes) == _chat_command_mute &&
			!strcmp(name, "1337") && minutes == CHAT_MUTE_DEFAULT_MINUTES, "'%s' %d", name, minutes);
		/* (a last word not all digits is the name's) */
		CHECK(chat_command_parse("/mute agent 47b", name, sizeof(name), &minutes) == _chat_command_mute &&
			!strcmp(name, "agent 47b") && minutes == CHAT_MUTE_DEFAULT_MINUTES, "'%s' %d", name, minutes);
		CHECK(chat_command_parse("/mute x 99999999999999999999", name, sizeof(name), &minutes) == _chat_command_mute &&
			minutes == CHAT_MUTE_MAXIMUM_MINUTES, "%d minutes", minutes);
		memset(&bounded, 'Z', sizeof(bounded));
		chat_command_parse("/mute averyveryverylongname 3", bounded.name, sizeof(bounded.name), &minutes);
		CHECK(bounded.canary[0] == 'Z' && strlen(bounded.name) == sizeof(bounded.name) - 1, "the name overran: '%s'",
			bounded.name);
	}

	CASE("command-unmute")
	{
		char name[CHAT_TEXT_SIZE];
		short minutes = 0;

		CHECK(chat_command_parse("/unmute Milenko", name, sizeof(name), &minutes) == _chat_command_unmute &&
			!strcmp(name, "Milenko"), "'%s'", name);
		/* (an unmute takes no minutes: a last word of digits is the name's) */
		CHECK(chat_command_parse("/Unmute Milenko 5", name, sizeof(name), &minutes) == _chat_command_unmute &&
			!strcmp(name, "Milenko 5"), "'%s'", name);
	}

	/* any other line is said as it is */
	CASE("command-not")
	{
		char name[CHAT_TEXT_SIZE];
		short minutes = 0;

		CHECK(chat_command_parse("/muted Milenko", name, sizeof(name), &minutes) == _chat_command_none, "/muted");
		CHECK(chat_command_parse("/mutex", name, sizeof(name), &minutes) == _chat_command_none, "/mutex");
		CHECK(chat_command_parse("mute Milenko 5", name, sizeof(name), &minutes) == _chat_command_none, "no slash");
		CHECK(chat_command_parse("hi /mute x", name, sizeof(name), &minutes) == _chat_command_none, "within a line");
		CHECK(chat_command_parse("/mut", name, sizeof(name), &minutes) == _chat_command_none, "/mut");
		CHECK(chat_command_parse("", name, sizeof(name), &minutes) == _chat_command_none, "nothing");
	}

	/* without a player, or for no minutes: how to say it */
	CASE("command-usage")
	{
		char name[CHAT_TEXT_SIZE];
		short minutes = 0;

		CHECK(chat_command_parse("/mute", name, sizeof(name), &minutes) == _chat_command_usage, "/mute alone");
		CHECK(chat_command_parse("/unmute", name, sizeof(name), &minutes) == _chat_command_usage, "/unmute alone");
		CHECK(chat_command_parse("/mute Milenko 0", name, sizeof(name), &minutes) == _chat_command_usage,
			"a mute of no minutes");
		CHECK(chat_command_parse("/mute Milenko 000", name, sizeof(name), &minutes) == _chat_command_usage,
			"a mute of no minutes");
	}

	/* the whole name in either case first, else the one it begins; none,
	or more than one */
	CASE("name-match")
	{
		static char const names[6][CHAT_NAME_SIZE] = { "Milenko", "", "Mil", "jonas", "Jonathan", "Bob" };

		CHECK(chat_name_match("Milenko", names, 6) == 0, "the whole name: %d", chat_name_match("Milenko", names, 6));
		CHECK(chat_name_match("milenko", names, 6) == 0, "in lower case");
		CHECK(chat_name_match("mil", names, 6) == 2, "the whole name before one it begins: %d",
			chat_name_match("mil", names, 6));
		CHECK(chat_name_match("mile", names, 6) == 0, "the one it begins: %d", chat_name_match("mile", names, 6));
		CHECK(chat_name_match("jon", names, 6) == CHAT_NAME_AMBIGUOUS, "two names begin with it: %d",
			chat_name_match("jon", names, 6));
		CHECK(chat_name_match("JONAS", names, 6) == 3, "the whole name over one it begins");
		CHECK(chat_name_match("Alice", names, 6) == NONE, "no such player");
		CHECK(chat_name_match("", names, 6) == NONE, "no name");
	}

	/* a name longer than any player's means none (never read past a name) */
	CASE("name-bounds")
	{
		static char const names[2][CHAT_NAME_SIZE] = { "Bob", "Bobby" };

		CHECK(chat_name_match("Bobbyyyyyyyyyyyyyyyyyy", names, 2) == NONE, "a longer name matched");
		CHECK(chat_name_match("Bobb", names, 2) == 1, "%d", chat_name_match("Bobb", names, 2));
	}

	/* a mute's minutes, the one begun counted whole, then none; let go */
	CASE("mute-time")
	{
		struct chat_mute mutes[2];
		unsigned long now = 1000;

		memset(mutes, 0, sizeof(mutes));
		CHECK(chat_mute_set(mutes, 2, "Milenko", 5, now), "not muted");
		CHECK(chat_mute_find(mutes, 2, "Milenko", now) != NULL, "not found");
		/* (in either case, as the host keeps names apart) */
		CHECK(chat_mute_find(mutes, 2, "milenko", now) != NULL, "not found in another case");
		CHECK(chat_mute_find(mutes, 2, "MILENKO", now) != NULL, "not found in capitals");
		CHECK(chat_mute_find(mutes, 2, "Milenk", now) == NULL, "found by another name");
		CHECK(chat_mute_find(mutes, 2, "Milenkos", now) == NULL, "found by a longer name");
		CHECK(chat_mute_minutes_left(&mutes[0], now) == 5, "%d", chat_mute_minutes_left(&mutes[0], now));
		CHECK(chat_mute_minutes_left(&mutes[0], now + 1) == 5, "%d", chat_mute_minutes_left(&mutes[0], now + 1));
		CHECK(chat_mute_minutes_left(&mutes[0], now + CHAT_MILLISECONDS_PER_MINUTE) == 4, "%d",
			chat_mute_minutes_left(&mutes[0], now + CHAT_MILLISECONDS_PER_MINUTE));
		CHECK(chat_mute_minutes_left(&mutes[0], now + 5 * CHAT_MILLISECONDS_PER_MINUTE - 1) == 1, "the last minute");
		CHECK(chat_mute_find(mutes, 2, "Milenko", now + 5 * CHAT_MILLISECONDS_PER_MINUTE) == NULL, "still muted");
		CHECK(!mutes[0].active, "not let go");
		CHECK(!strcmp(chat_minutes_plural(1), "") && !strcmp(chat_minutes_plural(2), "s"), "plurals");
	}

	/* the clock wrapping round (system_milliseconds) changes nothing */
	CASE("mute-wrap")
	{
		struct chat_mute mute;
		unsigned long now = 0xFFFFFFFFUL - 1000;

		memset(&mute, 0, sizeof(mute));
		chat_mute_set(&mute, 1, "Milenko", 1, now);
		CHECK(chat_mute_minutes_left(&mute, now + 2000) == 1, "ended across the wrap");
		CHECK(chat_mute_minutes_left(&mute, now + CHAT_MILLISECONDS_PER_MINUTE) == 0, "not ended across the wrap");
	}

	/* muting a player again sets their minutes again, in the one mute */
	CASE("mute-again")
	{
		struct chat_mute mutes[3];
		unsigned long now = 1000;
		int index;
		int held = 0;

		memset(mutes, 0, sizeof(mutes));
		chat_mute_set(mutes, 3, "Milenko", 5, now);
		chat_mute_set(mutes, 3, "Milenko", 1, now + 1000);
		for (index = 0; index < 3; index++)
			held += mutes[index].active;
		CHECK(held == 1, "%d mutes held for one player", held);
		CHECK(chat_mute_minutes_left(chat_mute_find(mutes, 3, "Milenko", now + 1000), now + 1000) == 1,
			"the minutes not set again");
	}

	/* every mute held: no other player's mute ends early to make room;
	one run out is taken again */
	CASE("mute-full")
	{
		struct chat_mute mutes[2];
		unsigned long now = 1000;

		memset(mutes, 0, sizeof(mutes));
		CHECK(chat_mute_set(mutes, 2, "a", 10, now) && chat_mute_set(mutes, 2, "b", 1, now), "not muted");
		CHECK(!chat_mute_set(mutes, 2, "c", 5, now), "a third mute taken from two");
		CHECK(chat_mute_find(mutes, 2, "a", now) && chat_mute_find(mutes, 2, "b", now), "a mute let go early");
		CHECK(chat_mute_set(mutes, 2, "c", 5, now + CHAT_MILLISECONDS_PER_MINUTE), "the run out mute not taken");
		CHECK(chat_mute_find(mutes, 2, "a", now + CHAT_MILLISECONDS_PER_MINUTE) &&
			chat_mute_find(mutes, 2, "c", now + CHAT_MILLISECONDS_PER_MINUTE), "the wrong mute taken");
	}

	/* links: a scheme, www., a name and a domain (with a path, at the end,
	before punctuation), an address of four numbers, in either case */
	CASE("link-found")
	{
		static char const *const links[] =
		{
			"join https://example.org/x", "ftp://files", "go to WWW.SITE.NET now", "www.halo",
			"discord.gg/abc", "my clan is at Example.COM", "check yt.tv, it's good", "halo.lol/x!",
			"ip 192.168.1.20 port 2302", "10.0.0.1",
		};
		int index;

		for (index = 0; index < (int)NUMBEROF(links); index++)
			CHECK(chat_text_has_link(links[index]), "not a link: '%s'", links[index]);
	}

	/* the dots written out, as a link is dressed to get past a filter */
	CASE("link-written-out")
	{
		static char const *const links[] = { "discord(dot)gg/abc", "example[dot]com", "site(.)net", "evil[.]io",
			"EXAMPLE(DOT)COM" };
		int index;

		for (index = 0; index < (int)NUMBEROF(links); index++)
			CHECK(chat_text_has_link(links[index]), "not a link: '%s'", links[index]);
	}

	/* the dots written out with spaces about them, or a dot with spaces both
	sides */
	CASE("link-spaced")
	{
		static char const *const links[] = { "example [dot] com", "discord (dot) gg/abc", "site [.] net",
			"example . com", "1 . 2 . 3 . 4", "join discord dot gg/x", "EXAMPLE DOT COM", "evil {dot} io" };
		static char const *const lines[] = { "polka dot me", "the dot is red", "dot dot dot", "the end . then",
			"wait . . . ok" };
		int index;

		for (index = 0; index < (int)NUMBEROF(links); index++)
			CHECK(chat_text_has_link(links[index]), "not a link: '%s'", links[index]);
		for (index = 0; index < (int)NUMBEROF(lines); index++)
			CHECK(!chat_text_has_link(lines[index]), "a link: '%s'", lines[index]);
	}

	/* a domain that is a word as well counts only with a path, a port or
	another domain after it: "go.to" is a slip of typing */
	CASE("link-word-domains")
	{
		static char const *const links[] = { "youtu.be/abc", "t.co/xyz", "example.co.uk", "halo.me:8080",
			"clan.us/join" };
		static char const *const lines[] = { "go.to the base", "he.is camping", "nice shot.Me", "wow.live",
			"next.fun round", "k.top", "lol.it was close", "me.to", "x.win", "ok.so" };
		int index;

		for (index = 0; index < (int)NUMBEROF(links); index++)
			CHECK(chat_text_has_link(links[index]), "not a link: '%s'", links[index]);
		for (index = 0; index < (int)NUMBEROF(lines); index++)
			CHECK(!chat_text_has_link(lines[index]), "a link: '%s'", lines[index]);
	}

	/* what is not one: sentences, numbers, abbreviations, a dot and a word
	that is not a domain, a domain's letters running into more */
	CASE("link-not")
	{
		static char const *const lines[] =
		{
			"gg ez", "nice shot. com on", "the score is 1.5 to 2", "e.g. the sniper", "i.e. no", "U.S.A",
			"version 1.2.3", "lol...ok", "use the www to read", "wait.comrades", "Mr.Chief", "a.b", "dot com",
			"hello world", "3.14159", "", "it's .com", "1.2.3.4567", "player.com2", "v1.2.3.4.5",
		};
		int index;

		for (index = 0; index < (int)NUMBEROF(lines); index++)
			CHECK(!chat_text_has_link(lines[index]), "a link: '%s'", lines[index]);
	}

	/* the host's settings: no chat refuses every line; links refused unless
	allowed */
	CASE("refused")
	{
		byte on = (byte)FLAG(_chat_config_allowed_bit);
		byte links = (byte)(FLAG(_chat_config_allowed_bit) | FLAG(_chat_config_links_bit));

		CHECK(chat_refused(0, "hello") != NULL, "a line taken with the chat off");
		CHECK(chat_refused((byte)FLAG(_chat_config_links_bit), "hello") != NULL, "links alone let the chat on");
		CHECK(chat_refused(on, "hello") == NULL, "a line refused with the chat on");
		CHECK(chat_refused(on, "see discord.gg/x") != NULL, "a link taken where links are refused");
		CHECK(chat_refused(links, "see discord.gg/x") == NULL, "a link refused where links are allowed");
	}

	/* a public game's chat: off in one a player hosts (on the host, and on a
	client that joined it from the server browser), on in a dedicated
	server's, whose own setting then decides; a game not listed keeps its
	host's setting */
	CASE("listing")
	{
		CHECK(chat_listing_allows(FALSE, FALSE), "a private or LAN game has no chat");
		CHECK(chat_listing_allows(FALSE, TRUE), "an unlisted dedicated server has no chat");
		CHECK(!chat_listing_allows(TRUE, FALSE), "a public game a player hosts has chat");
		CHECK(chat_listing_allows(TRUE, TRUE), "a public dedicated server has no chat");
	}

	/* the server's word filter: its modes and levels as server.chat_filter*
	spell them (in either case; anything else off), and a line's marked
	characters starred, its spaces kept */
	CASE("filter")
	{
		char line[] = "you are a bad word ok";
		unsigned char marked[sizeof(line)] = { 0 };
		long index;

		CHECK(chat_filter_mode("censor") == _chat_filter_censor && chat_filter_mode("BLOCK") == _chat_filter_block &&
			chat_filter_mode("off") == _chat_filter_off && chat_filter_mode("nonsense") == _chat_filter_off &&
			chat_filter_mode(NULL) == _chat_filter_off, "the modes");
		CHECK(chat_filter_level("mild") == _text_filter_mild && chat_filter_level("Strong") == _text_filter_strong &&
			chat_filter_level("severe") == _text_filter_severe && chat_filter_level("off") == _text_filter_off &&
			chat_filter_level("") == _text_filter_off, "the levels");
		for (index = 10; index < 18; index++)
			marked[index] = 1;
		chat_filter_star(line, marked, (long)sizeof(line));
		CHECK(!strcmp(line, "you are a *** **** ok"), "'%s'", line);
	}

	/* the on-screen keyboard's characters: a letter in capitals with shift,
	nothing else changed; none on the last row, or off the keyboard */
	CASE("keyboard-character")
	{
		/* (and a row past them, which no key reads) */
		static char const *const rows[CHAT_KEYBOARD_ROWS] =
			{ "1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.?", "##########" };

		CHECK(chat_keyboard_character(rows, 1, 0, FALSE) == 'q', "'%c'", chat_keyboard_character(rows, 1, 0, FALSE));
		CHECK(chat_keyboard_character(rows, 1, 0, TRUE) == 'Q', "'%c'", chat_keyboard_character(rows, 1, 0, TRUE));
		CHECK(chat_keyboard_character(rows, 0, 3, TRUE) == '4', "a digit changed by shift");
		CHECK(chat_keyboard_character(rows, 3, 9, TRUE) == '?', "a sign changed by shift");
		CHECK(chat_keyboard_character(rows, CHAT_KEYBOARD_ROWS - 1, 0, FALSE) == 0, "the last row has a character");
		CHECK(chat_keyboard_character(rows, -1, 0, FALSE) == 0 && chat_keyboard_character(rows, 0, CHAT_KEYBOARD_COLUMNS,
			FALSE) == 0, "a key off the keyboard");
	}

	/* moving: round the edges; on the last row a wide key at a time, and its
	first column */
	CASE("keyboard-move")
	{
		short row = 0;
		short column = 0;

		chat_keyboard_move(&row, &column, -1, 0);
		CHECK(row == CHAT_KEYBOARD_ROWS - 1 && column == 0, "up from the top: %d %d", row, column);
		chat_keyboard_move(&row, &column, 0, -1);
		CHECK(column == CHAT_KEYBOARD_COLUMNS - CHAT_KEYBOARD_WIDE_COLUMNS, "left round the last row: %d", column);
		row = 3;
		column = 5;
		chat_keyboard_move(&row, &column, 1, 0);
		CHECK(row == CHAT_KEYBOARD_ROWS - 1 && column == 4, "down onto a wide key: %d %d", row, column);
		chat_keyboard_move(&row, &column, 0, 1);
		CHECK(column == 4 + CHAT_KEYBOARD_WIDE_COLUMNS, "right on the last row: %d", column);
		chat_keyboard_move(&row, &column, 1, 0);
		CHECK(row == 0 && column == 4 + CHAT_KEYBOARD_WIDE_COLUMNS, "down round the bottom: %d %d", row, column);
		column = CHAT_KEYBOARD_COLUMNS - 1;
		chat_keyboard_move(&row, &column, 0, 1);
		CHECK(column == 0, "right round a row: %d", column);
	}

	/* a tap: the key under it, a wide key's first column; none outside */
	CASE("keyboard-tap")
	{
		short row = -1;
		short column = -1;

		CHECK(chat_keyboard_key_at(0.05f, 0.05f, &row, &column) && row == 0 && column == 0, "%d %d", row, column);
		CHECK(chat_keyboard_key_at(0.95f, 0.3f, &row, &column) && row == 1 && column == 9, "%d %d", row, column);
		CHECK(chat_keyboard_key_at(0.55f, 0.9f, &row, &column) && row == CHAT_KEYBOARD_ROWS - 1 && column == 4,
			"the space key: %d %d", row, column);
		CHECK(!chat_keyboard_key_at(-0.01f, 0.5f, &row, &column) && !chat_keyboard_key_at(0.5f, 1.0f, &row, &column),
			"a tap outside taken");
	}

	/* the keyboard given to the platform that draws it: nothing but "closed" while it is not up, else the line, the channel, the capitals and the key picked, and the
	characters of its keys as the page and the capitals have them */
	CASE("keyboard-view")
	{
		static char const *const letters[CHAT_KEYBOARD_ROWS - 1] = { "1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.?" };
		static char const *const symbols[CHAT_KEYBOARD_ROWS - 1] = { "1234567890", "!@#$%^&*()", "-_=+[]{};:", "\"'<>/\\?~,." };
		struct halo_chat_keyboard_view view;

		memset(&view, 0x55, sizeof(view));
		chat_keyboard_view_fill(&view, FALSE, 2, TRUE, TRUE, 3, 4, "ignored", letters);
		CHECK(!view.open && view.length == 0 && view.text[0] == 0 && view.channel == 0 && !view.shift && view.keys[0][0] == 0, "closed is all zero");
		chat_keyboard_view_fill(&view, TRUE, 1, FALSE, FALSE, 2, 3, "need help", letters);
		CHECK(view.open && view.channel == 1 && !view.shift && !view.symbols && view.row == 2 && view.column == 3, "the state");
		CHECK(view.length == 9 && !strcmp(view.text, "need help"), "the line: '%s'", view.text);
		CHECK(!memcmp(view.keys[0], "1234567890", 10) && !memcmp(view.keys[1], "qwertyuiop", 10) && !memcmp(view.keys[3], "zxcvbnm,.?", 10), "the letters");
		chat_keyboard_view_fill(&view, TRUE, 0, TRUE, FALSE, 0, 0, "", letters);
		CHECK(view.shift && !memcmp(view.keys[1], "QWERTYUIOP", 10) && !memcmp(view.keys[0], "1234567890", 10) && !memcmp(view.keys[3], "ZXCVBNM,.?", 10), "capitals");
		chat_keyboard_view_fill(&view, TRUE, 0, FALSE, TRUE, 0, 0, "", symbols);
		CHECK(view.symbols && !memcmp(view.keys[1], "!@#$%^&*()", 10) && view.keys[3][0] == '"', "the symbols' page");
		/* a longest line fits, ended */
		{
			char line[CHAT_TEXT_SIZE + 8];

			memset(line, 'x', sizeof(line));
			line[sizeof(line) - 1] = 0;
			chat_keyboard_view_fill(&view, TRUE, 0, FALSE, FALSE, 0, 0, line, letters);
			CHECK(view.length == HALO_CHAT_KEYBOARD_TEXT - 1 && view.text[HALO_CHAT_KEYBOARD_TEXT - 1] == 0, "a long line is cut: %d", view.length);
		}
	}

	/* a tap of the platform's keyboard: the key it is (a wide key of the last row by its first column); none off the keyboard */
	CASE("keyboard-platform-tap")
	{
		short row, column;

		row = 0;
		column = 9;
		CHECK(chat_keyboard_tap_key(&row, &column) && row == 0 && column == 9, "a letter");
		row = CHAT_KEYBOARD_ROWS - 1;
		column = 5;
		CHECK(chat_keyboard_tap_key(&row, &column) && row == CHAT_KEYBOARD_ROWS - 1 && column == 4, "the second half of a wide key: %d", column);
		row = CHAT_KEYBOARD_ROWS - 1;
		column = CHAT_KEYBOARD_COLUMNS - 1;
		CHECK(chat_keyboard_tap_key(&row, &column) && column == CHAT_KEYBOARD_COLUMNS - 2, "the last key's second half");
		row = CHAT_KEYBOARD_ROWS;
		column = 0;
		CHECK(!chat_keyboard_tap_key(&row, &column), "a row past the keyboard");
		row = 0;
		column = CHAT_KEYBOARD_COLUMNS;
		CHECK(!chat_keyboard_tap_key(&row, &column), "a column past it");
		row = -1;
		column = 0;
		CHECK(!chat_keyboard_tap_key(&row, &column), "before it");
		row = 0;
		column = -1;
		CHECK(!chat_keyboard_tap_key(&row, &column), "before it");
	}

	/* the quick chat's lines: a page of four, each a line the chat sends as it is (nothing a clean-up would change), short enough for a tile, with no repeat on a page */
	CASE("quick-table")
	{
		short page, slot, other;
		char cleaned[CHAT_TEXT_SIZE];

		CHECK(CHAT_QUICK_PAGES == 3 && CHAT_QUICK_LINES == 4, "three pages of four");
		for (page = 0; page < CHAT_QUICK_PAGES; page++)
		{
			CHECK(chat_quick_page_names[page] && strlen(chat_quick_page_names[page]) >= 3 && strlen(chat_quick_page_names[page]) <= 8, "page %d has a name", page);
			for (slot = 0; slot < CHAT_QUICK_LINES; slot++)
			{
				char const *line = chat_quick_lines[page][slot];

				CHECK(line && line[0], "page %d line %d is empty", page, slot);
				CHECK(strlen(line) <= 20, "'%s' is %d characters", line, (int)strlen(line));
				CHECK(chat_text_clean(cleaned, line, CHAT_TEXT_SIZE) == (short)strlen(line) && !strcmp(cleaned, line), "'%s' is changed by the chat: '%s'", line, cleaned);
				CHECK(!strchr(line, '|') && line[0] != '/' && line[0] != '!', "'%s' is a command or has a pipe", line);
				for (other = 0; other < slot; other++)
					CHECK(strcmp(line, chat_quick_lines[page][other]), "'%s' twice on page %d", line, page);
			}
		}
	}

	/* a button pressed picks its line: up, left, right, down, the first if more are pressed; the others none */
	CASE("quick-slot")
	{
		CHECK(chat_quick_slot(FLAG(HALO_CHAT_PAD_UP)) == 0 && chat_quick_slot(FLAG(HALO_CHAT_PAD_LEFT)) == 1 && chat_quick_slot(FLAG(HALO_CHAT_PAD_RIGHT)) == 2 &&
			chat_quick_slot(FLAG(HALO_CHAT_PAD_DOWN)) == 3, "the four");
		CHECK(chat_quick_slot(FLAG(HALO_CHAT_PAD_DOWN) | FLAG(HALO_CHAT_PAD_LEFT)) == 1 && chat_quick_slot(FLAG(HALO_CHAT_PAD_RIGHT) | FLAG(HALO_CHAT_PAD_UP)) == 0, "the first");
		CHECK(chat_quick_slot(0) == NONE && chat_quick_slot(FLAG(HALO_CHAT_PAD_A) | FLAG(HALO_CHAT_PAD_B) | FLAG(HALO_CHAT_PAD_START) | FLAG(HALO_CHAT_PAD_PAGE_NEXT) |
			FLAG(HALO_CHAT_PAD_STICK_UP) | FLAG(HALO_CHAT_PAD_X) | FLAG(HALO_CHAT_PAD_Y)) == NONE, "the others");
	}

	/* the pages turn round, and say who they are to: the calls and the plan to the team (everyone without teams), the rest to everyone */
	CASE("quick-page")
	{
		short page;

		CHECK(chat_quick_page_turn(0, 1) == 1 && chat_quick_page_turn(1, 1) == 2 && chat_quick_page_turn(2, 1) == 0, "forward round");
		CHECK(chat_quick_page_turn(0, -1) == 2 && chat_quick_page_turn(2, -1) == 1 && chat_quick_page_turn(1, -1) == 0, "back round");
		CHECK(chat_quick_channel(0, TRUE) == _chat_channel_team && chat_quick_channel(1, TRUE) == _chat_channel_team && chat_quick_channel(2, TRUE) == _chat_channel_everyone, "with teams");
		for (page = 0; page < CHAT_QUICK_PAGES; page++)
			CHECK(chat_quick_channel(page, FALSE) == _chat_channel_everyone, "page %d without teams", page);
	}

	/* a page left open closes after 4 seconds without a press, across the clock's wrap */
	CASE("quick-expired")
	{
		CHECK(!chat_quick_expired(1000, 1000) && !chat_quick_expired(1000, 4999) && chat_quick_expired(1000, 5000), "4 seconds");
		/* (the game's clock is 32 bits; on a host with 64-bit longs the wrap is not there to cross) */
		if (sizeof(unsigned long) == 4)
			CHECK(!chat_quick_expired(0xFFFFFF00UL, 0x100UL) && chat_quick_expired(0xFFFFFF00UL, 0x100UL + 4000UL), "across the wrap");
	}

	/* the limits: three lines at once, one more every 2 seconds, none within 0.8 s of the last, the same line not again within 5 s */
	CASE("quick-limit")
	{
		struct chat_quick_limit limit;
		unsigned long now = 100000;

		memset(&limit, 0, sizeof(limit));
		limit.last_line = NONE;
		CHECK(chat_quick_take(&limit, 0, now) == _chat_quick_sent, "the first");
		/* too soon after it, whatever the line: nothing counted */
		CHECK(chat_quick_take(&limit, 1, now + 100) == _chat_quick_spacing && chat_quick_take(&limit, 1, now + 799) == _chat_quick_spacing, "spacing");
		CHECK(chat_quick_take(&limit, 1, now + 800) == _chat_quick_sent, "after the spacing");
		/* the same line again */
		CHECK(chat_quick_take(&limit, 1, now + 1700) == _chat_quick_repeat, "the same line again");
		CHECK(chat_quick_take(&limit, 2, now + 1700) == _chat_quick_sent, "the third of three");
		/* three used: the fourth waits (the bucket refills one a period after the first: 2 s) */
		CHECK(chat_quick_take(&limit, 3, now + 1800) == _chat_quick_spacing, "100 ms after the third");
		CHECK(chat_quick_take(&limit, 3, now + 2500) == _chat_quick_sent, "the fourth, when a token is back (2 s after the first)");
	}
	CASE("quick-limit-burst")
	{
		struct chat_quick_limit limit;
		unsigned long now = 200000;
		short sent = 0, burst = 0, line;

		memset(&limit, 0, sizeof(limit));
		limit.last_line = NONE;
		/* a line every second for ten lines of different words: the bucket lets 3 at once, then about one in two seconds */
		for (line = 0; line < 10; line++)
		{
			short result = chat_quick_take(&limit, line, now + (unsigned long)line * 1000);

			sent += result == _chat_quick_sent;
			burst += result == _chat_quick_burst;
		}
		CHECK(sent == 7 && burst == 3, "sent %d, held back %d: three at once and one every two seconds", sent, burst);
		/* a line every 0.8 s is held back more: the spacing's nothing counts */
		memset(&limit, 0, sizeof(limit));
		limit.last_line = NONE;
		sent = 0;
		for (line = 0; line < 20; line++)
			sent += chat_quick_take(&limit, line, now + (unsigned long)line * 800) == _chat_quick_sent;
		CHECK(sent == 10, "at 0.8 s apart: %d of 20 (3, then one in 2 s)", sent);
		/* after a long wait the burst is back, no more: eight lines a second apart, six get through */
		limit.last_line = NONE;
		sent = 0;
		for (line = 0; line < 8; line++)
			sent += chat_quick_take(&limit, line, now + 1000000 + (unsigned long)line * 1000) == _chat_quick_sent;
		CHECK(sent == 6, "after a long wait: %d of 8", sent);
	}
	/* the clock's wrap in the limits */
	CASE("quick-limit-wrap")
	{
		struct chat_quick_limit limit;
		unsigned long now = 0xFFFFF000UL;

		/* (the game's clock is 32 bits; on a host with 64-bit longs the wrap is not there to cross) */
		if (sizeof(unsigned long) != 4)
			return 0;
		memset(&limit, 0, sizeof(limit));
		limit.last_line = NONE;
		CHECK(chat_quick_take(&limit, 0, now) == _chat_quick_sent, "before the wrap");
		CHECK(chat_quick_take(&limit, 1, now + 0x2000UL) == _chat_quick_sent, "after it, 8 seconds on");
		CHECK(chat_quick_take(&limit, 0, now + 0x2001UL + 1000UL) == _chat_quick_sent, "the first line again, long since");
	}

	return 0;
}
