/*
TEXT_SAFE.H

What may be drawn of text that came from outside: a name, a chat line, a code a person typed. Only printable ASCII, 0x20 to 0x7E, and never "|" (the game's text
drawing takes it for the start of a code, and one at the end reads past the string). Every font in the tree has the ASCII characters, and the one block-drawing path
there is (the Wii U overlay's 3x5 font) is for debug panels, so nothing a stranger typed reaches either as anything else. text_safe.c has no game types, so that
the harness builds it alone (tools/harness/tests/test_text_safe.py).

A name is cleaned once, where it arrives from the network, and only the clean ASCII is kept, so that no screen draws a stranger's raw string.
*/

#ifndef __TEXT_SAFE_H__
#define __TEXT_SAFE_H__

#include <stddef.h>

enum
{
	/* a player's name is wchar_t name[12] (players.h): 11 characters */
	TEXT_SAFE_NAME_LENGTH = 11,
	/* a chat line: CHAT_TEXT_SIZE 64 with its end (network_chat.c) */
	TEXT_SAFE_CHAT_LENGTH = 63,
	/* a friend code's letters and digits, without its dashes: XXXX-XXXX-XXXX-C */
	TEXT_SAFE_CODE_LENGTH = 13,
};

/* whether a character may be drawn: 0x20 to 0x7E but "|" */
int text_safe_character_ok(unsigned long code);

/* A character of a name or line as it may be drawn: itself if it may, else "?". Latin letters with marks (U+00C0 to U+017F) become their plain letters when fold is
nonzero (an English keyboard has no "e with an acute"; the host's ban command names players in ASCII, player_name_character_ascii), and are "?" when not. */
char text_safe_character(unsigned long code, int fold);

/* Text of bytes (a line another machine sent, unended perhaps: it ends at the NUL or at source_size) as ASCII that may be drawn: bytes that may not are "?", the spaces
at its ends dropped, runs of more than two spaces cut to two, at most maximum characters (and size - 1). The text, ended; its length (0: nothing was left). */
short text_safe_text(char *destination, short size, char const *source, short source_size, short maximum);

/* ... and a label that is never empty: "?" for nothing left */
short text_safe_label(char *destination, short size, char const *source, short source_size, short maximum);

/* A name as 16-bit characters (a player's name, as the game keeps it; ended or to count) as ASCII that may be drawn: Unicode's spaces made plain, control and zero-width
characters and "|" left out (player_name_clean's rule), Latin letters with marks folded to plain ones, anything else "?", spaces at the ends dropped and runs cut to one,
at most TEXT_SAFE_NAME_LENGTH characters (and size - 1). A name with nothing in it that a person could read (empty, or all "?") is the fallback instead (NULL: "PLAYER"; the
fallback is itself cleaned). The name, ended; its length, never 0 when size is at least 2. */
short text_safe_name(char *destination, short size, wchar_t const *source, long count, char const *fallback);

/* ... a name as UTF-8 bytes (a friend's, in a message): decoded to 16-bit characters (what is not UTF-8, and anything past U+FFFF, is "?") and cleaned as above */
short text_safe_name_utf8(char *destination, short size, char const *source, long source_size, char const *fallback);

/* A friend code a person typed or pasted, as the code's characters (Crockford's base 32: 0 to 9 and A to Z without I, L, O and U): upper case, spaces and dashes dropped,
I and L read as 1, O as 0, U as V. At most TEXT_SAFE_CODE_LENGTH characters, ended. Returns the length, or -1 if a character is none of those, or too many came. Its check
character is not looked at here (the friends' code, social_*.c). */
short text_safe_code_normalise(char *destination, short size, char const *source);

#endif
