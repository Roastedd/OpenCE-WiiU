#include "harness.h"

/* What may be drawn of text from outside (port/linux/game/text_safe.c): every code point of a 16-bit character through the sanitisers, the folding table against
players.c's, the name rule against player_name_clean's, and the chat rule against network_chat.c's chat_text_clean. */
#include <wchar.h>

#include "config.inc"
#include "text_safe.h"
#include "text_safe_code.inc"
#include "under_test.inc"

static int ok_text(char const *text, int maximum)
{
	int length = 0;

	for (; text[length]; length++)
	{
		if (!text_safe_character_ok((unsigned char)text[length]))
			return 0;
	}
	return length <= maximum;
}

/* a small generator of the same numbers on every host */
static unsigned long seed_state = 12345;
static unsigned long next_random(void)
{
	seed_state = seed_state * 1103515245UL + 12345UL;
	return (seed_state >> 8) & 0xFFFFFF;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	char out[32];
	unsigned long code;

	/* every 16-bit code point: a character drawn is 0x20 to 0x7E and never "|", folded or not */
	CASE("characters")
	{
		for (code = 0; code <= 0xFFFF; code++)
		{
			char plain = text_safe_character(code, 0), folded = text_safe_character(code, 1);

			CHECK(text_safe_character_ok((unsigned char)plain) && text_safe_character_ok((unsigned char)folded), "code %04lX gives %02X and %02X", code, (unsigned)plain & 255, (unsigned)folded & 255);
			CHECK(text_safe_character_ok(code) ? plain == (char)code && folded == (char)code : plain == '?', "code %04lX unfolded %c", code, plain);
		}
		CHECK(!text_safe_character_ok('|') && !text_safe_character_ok(0x7F) && !text_safe_character_ok(0x1F) && text_safe_character_ok(' ') && text_safe_character_ok('~'), "edges");
	}

	/* the folding agrees with the game's own (player_name_character_ascii), except that the game's keeps "|" (player_name_clean takes it out first) */
	CASE("fold-agrees")
	{
		for (code = 0; code <= 0xFFFF; code++)
		{
			char theirs = player_name_character_ascii((wchar_t)code);

			CHECK(text_safe_character(code, 1) == (theirs == '|' ? '?' : theirs), "code %04lX: %c against %c", code, text_safe_character(code, 1), theirs);
		}
	}

	/* every code point alone, in a name of letters, and repeated: 1 to 11 characters, all drawable, never empty */
	CASE("name-all-codepoints")
	{
		wchar_t name[16];
		int repeat;

		for (code = 0; code <= 0xFFFF; code++)
		{
			short length;

			memset(name, 0, sizeof(name));
			name[0] = (wchar_t)code;
			length = text_safe_name(out, sizeof(out), name, 12, NULL);
			CHECK(length >= 1 && length <= TEXT_SAFE_NAME_LENGTH && length == (short)strlen(out) && ok_text(out, 11), "alone %04lX: '%s'", code, out);
			name[0] = 'a';
			name[1] = (wchar_t)code;
			name[2] = 'b';
			length = text_safe_name(out, sizeof(out), name, 12, NULL);
			CHECK(length >= 1 && length <= TEXT_SAFE_NAME_LENGTH && length == (short)strlen(out) && ok_text(out, 11), "in %04lX: '%s'", code, out);
			for (repeat = 0; repeat < 12; repeat++)
				name[repeat] = (wchar_t)code;
			name[12] = 0;
			length = text_safe_name(out, sizeof(out), name, 13, NULL);
			CHECK(length >= 1 && length <= TEXT_SAFE_NAME_LENGTH && length == (short)strlen(out) && ok_text(out, 11), "repeated %04lX: '%s'", code, out);
			CHECK(out[0] != ' ' && out[length - 1] != ' ', "spaces at the ends: '%s'", out);
		}
	}

	/* a name made of random 16-bit characters, as the destination's size allows: never past it, drawable, never empty */
	CASE("name-random")
	{
		int round;

		for (round = 0; round < 20000; round++)
		{
			wchar_t name[40];
			char guarded[20];
			short size = (short)(2 + next_random() % 14);
			long count = (long)(next_random() % 30);
			long index;
			short length;

			for (index = 0; index < count; index++)
			{
				unsigned long pick = next_random();

				name[index] = (wchar_t)(pick % 4 == 0 ? (pick >> 4) & 0xFFFF : pick % 4 == 1 ? 0x20 + (pick >> 4) % 95 : pick % 4 == 2 ? 0xC0 + (pick >> 4) % 0xC0 : " |\t\xA0"[(pick >> 4) % 4]);
			}
			name[count] = 0;
			memset(guarded, 0x55, sizeof(guarded));
			length = text_safe_name(guarded, size, name, count, round % 3 ? NULL : "x|y");
			CHECK(length >= 1 && length <= TEXT_SAFE_NAME_LENGTH && length <= size - 1 && guarded[length] == 0 && ok_text(guarded, size - 1), "round %d size %d: '%s'", round, size, guarded);
			CHECK((unsigned char)guarded[size] == 0x55 || size >= (short)sizeof(guarded), "wrote past the size");
		}
	}

	/* nothing a person could read gives the fallback, cleaned, or PLAYER */
	CASE("name-fallback")
	{
		wchar_t empty[4] = { 0 };
		wchar_t spaces[6] = { ' ', 0xA0, 0x3000, ' ', 0, 0 };
		wchar_t zero_width[4] = { 0x200B, 0x200D, 0xFEFF, 0 };
		wchar_t marks[5] = { '?', '?', '?', 0, 0 };
		wchar_t cjk[3] = { 0x65E5, 0x672C, 0 };
		wchar_t pipe[3] = { '|', '|', 0 };
		wchar_t good[8] = { 'B', 'o', 'b', 0, 0, 0, 0, 0 };

		CHECK(text_safe_name(out, sizeof(out), empty, 4, NULL) == 6 && !strcmp(out, "PLAYER"), "empty: '%s'", out);
		CHECK(text_safe_name(out, sizeof(out), spaces, 6, NULL) == 6 && !strcmp(out, "PLAYER"), "spaces: '%s'", out);
		CHECK(text_safe_name(out, sizeof(out), zero_width, 4, NULL) == 6 && !strcmp(out, "PLAYER"), "zero width: '%s'", out);
		CHECK(text_safe_name(out, sizeof(out), marks, 5, NULL) == 6 && !strcmp(out, "PLAYER"), "marks: '%s'", out);
		CHECK(text_safe_name(out, sizeof(out), cjk, 3, NULL) == 6 && !strcmp(out, "PLAYER"), "cjk: '%s'", out);
		CHECK(text_safe_name(out, sizeof(out), pipe, 3, "") == 6 && !strcmp(out, "PLAYER"), "pipes: '%s'", out);
		CHECK(text_safe_name(out, sizeof(out), cjk, 3, "PLAYER 4D2P") == 11 && !strcmp(out, "PLAYER 4D2P"), "code prefix: '%s'", out);
		CHECK(text_safe_name(out, sizeof(out), cjk, 3, "PLAYER 4D2P99") == 11 && !strcmp(out, "PLAYER 4D2P"), "long fallback cut: '%s'", out);
		CHECK(text_safe_name(out, sizeof(out), cjk, 3, "a|b") == 2 && !strcmp(out, "ab"), "fallback cleaned: '%s'", out);
		CHECK(text_safe_name(out, sizeof(out), cjk, 3, "|??|") == 6 && !strcmp(out, "PLAYER"), "garbage fallback: '%s'", out);
		CHECK(text_safe_name(out, sizeof(out), good, 8, NULL) == 3 && !strcmp(out, "Bob"), "a good name: '%s'", out);
	}

	/* the rule is player_name_clean's, then the fold, runs of spaces cut to one, 11 characters: for the names it calls visible, the same text */
	CASE("name-agrees")
	{
		int round;

		for (round = 0; round < 40000; round++)
		{
			wchar_t name[13], cleaned[13];
			char expected[16], got[16];
			long index, length = 0;
			boolean visible;
			short count = (short)(next_random() % 13);

			memset(name, 0, sizeof(name));
			for (index = 0; index < count; index++)
			{
				unsigned long pick = next_random();

				name[index] = (wchar_t)(pick % 3 == 0 ? (pick >> 4) & 0xFFFF : pick % 3 == 1 ? 0x20 + (pick >> 4) % 95 : 0xA0 + (pick >> 4) % 0xE0);
				if (!name[index])
					name[index] = 'z';
			}
			memcpy(cleaned, name, sizeof(name));
			visible = player_name_clean(cleaned, 13);
			if (!visible)
				continue;
			for (index = 0; cleaned[index]; index++)
			{
				char plain = player_name_character_ascii(cleaned[index]);

				if (plain == '|')
					plain = '?';
				if (plain == ' ' && length > 0 && expected[length - 1] == ' ')
					continue;
				expected[length++] = plain;
			}
			if (length > 11)
				length = 11;
			while (length > 0 && expected[length - 1] == ' ')
				length--;
			expected[length] = 0;
			text_safe_name(got, sizeof(got), name, 13, NULL);
			CHECK(!strcmp(got, expected), "round %d: '%s' against '%s'", round, got, expected);
		}
	}

	/* every byte through a line: drawable, cut as asked, the spaces at the ends dropped, runs of spaces cut to two, and nothing written past the size */
	CASE("text-all-bytes")
	{
		int byte;
		char source[8];
		short length;

		for (byte = 0; byte < 256; byte++)
		{
			source[0] = 'a';
			source[1] = (char)byte;
			source[2] = 'b';
			source[3] = 0;
			length = text_safe_text(out, sizeof(out), source, 4, 63);
			CHECK(length >= (byte ? 2 : 1) && length == (short)strlen(out) && ok_text(out, 63), "byte %02X: '%s'", byte, out);
			source[0] = (char)byte;
			source[1] = 0;
			length = text_safe_text(out, sizeof(out), source, 2, 63);
			CHECK(length == (short)strlen(out) && ok_text(out, 63), "alone %02X: '%s'", byte, out);
			CHECK(byte == 0 || byte == ' ' ? length == 0 : length == 1, "alone %02X length %d", byte, length);
			length = text_safe_label(out, sizeof(out), source, 2, 63);
			CHECK(length >= 1 && ok_text(out, 63), "label %02X", byte);
		}
		length = text_safe_text(out, sizeof(out), "   a     b  ", 12, 63);
		CHECK(length == 4 && !strcmp(out, "a  b"), "spaces: %d '%s'", length, out);
		length = text_safe_text(out, 4, "abcdefgh", 8, 63);
		CHECK(length == 3 && !strcmp(out, "abc"), "size: '%s'", out);
		length = text_safe_text(out, sizeof(out), "abcdefgh", 8, 5);
		CHECK(length == 5 && !strcmp(out, "abcde"), "maximum: '%s'", out);
		length = text_safe_text(out, sizeof(out), "abcdefgh", 3, 63);
		CHECK(length == 3 && !strcmp(out, "abc"), "source size: '%s'", out);
		length = text_safe_text(out, sizeof(out), "gg|", 3, 63);
		CHECK(length == 3 && !strcmp(out, "gg?"), "pipe: '%s'", out);
		length = text_safe_text(out, sizeof(out), "", 1, 63);
		CHECK(length == 0 && out[0] == 0, "empty");
		length = text_safe_label(out, sizeof(out), "  ", 2, 63);
		CHECK(length == 1 && !strcmp(out, "?"), "label: '%s'", out);
	}

	/* a line as the chat has it: the same text as chat_text_clean gives for lines with no run of spaces to cut */
	CASE("text-agrees-with-chat")
	{
		int byte, other;
		char source[8];
		char theirs[CHAT_TEXT_SIZE], ours[CHAT_TEXT_SIZE];

		for (byte = 0; byte < 256; byte++)
		{
			for (other = 0; other < 256; other += 1)
			{
				source[0] = (char)byte;
				source[1] = (char)other;
				source[2] = 'x';
				source[3] = 0;
				chat_text_clean(theirs, source, 4);
				text_safe_text(ours, sizeof(ours), source, 4, 63);
				CHECK(!strcmp(theirs, ours), "%02X %02X: '%s' against '%s'", byte, other, ours, theirs);
			}
		}
	}

	/* a name in UTF-8: letters with marks folded, anything else "?", a bad sequence "?", never past the size */
	CASE("utf8")
	{
		CHECK(text_safe_name_utf8(out, sizeof(out), "Bob", 3, NULL) == 3 && !strcmp(out, "Bob"), "ascii");
		CHECK(text_safe_name_utf8(out, sizeof(out), "J\xC3\xA9r\xC3\xB4me", 8, NULL) == 6 && !strcmp(out, "Jerome"), "folded: '%s'", out);
		CHECK(text_safe_name_utf8(out, sizeof(out), "\xE6\x97\xA5\xE6\x9C\xAC", 6, NULL) == 6 && !strcmp(out, "PLAYER"), "cjk: '%s'", out);
		CHECK(text_safe_name_utf8(out, sizeof(out), "a\xF0\x9F\x98\x80" "b", 6, NULL) == 3 && !strcmp(out, "a?b"), "emoji: '%s'", out);
		CHECK(text_safe_name_utf8(out, sizeof(out), "a\xC0\x80" "b", 4, NULL) >= 2 && ok_text(out, 11), "overlong: '%s'", out);
		CHECK(text_safe_name_utf8(out, sizeof(out), "ab\xE6\x97", 4, NULL) >= 2 && ok_text(out, 11), "truncated: '%s'", out);
		CHECK(text_safe_name_utf8(out, sizeof(out), "a|b", 3, NULL) == 2 && !strcmp(out, "ab"), "pipe: '%s'", out);
		{
			int round;

			for (round = 0; round < 30000; round++)
			{
				char bytes[24];
				long count = (long)(next_random() % 24);
				long index;
				short length;

				for (index = 0; index < count; index++)
					bytes[index] = (char)(next_random() >> 3);
				length = text_safe_name_utf8(out, sizeof(out), bytes, count, NULL);
				CHECK(length >= 1 && length <= 11 && length == (short)strlen(out) && ok_text(out, 11), "round %d: '%s'", round, out);
			}
		}
	}

	/* a friend code as typed: case, dashes and spaces, the letters Crockford folds, and what it refuses */
	CASE("code")
	{
		CHECK(text_safe_code_normalise(out, sizeof(out), "7f3k-9qxa-2m4d-r") == 13 && !strcmp(out, "7F3K9QXA2M4DR"), "typed: '%s'", out);
		CHECK(text_safe_code_normalise(out, sizeof(out), " 7F3K 9QXA 2M4D R ") == 13 && !strcmp(out, "7F3K9QXA2M4DR"), "spaces: '%s'", out);
		CHECK(text_safe_code_normalise(out, sizeof(out), "iLoU") == 4 && !strcmp(out, "110V"), "folded: '%s'", out);
		CHECK(text_safe_code_normalise(out, sizeof(out), "ab|c") == -1, "pipe");
		CHECK(text_safe_code_normalise(out, sizeof(out), "ab\xE9") == -1, "high byte");
		CHECK(text_safe_code_normalise(out, sizeof(out), "00000000000000") == -1, "too long");
		CHECK(text_safe_code_normalise(out, 4, "abcd") == -1, "size");
		CHECK(text_safe_code_normalise(out, sizeof(out), "") == 0 && out[0] == 0, "empty");
		CHECK(text_safe_code_normalise(out, sizeof(out), NULL) == 0, "null");
		for (code = 0; code < 256; code++)
		{
			char source[3] = { 'A', (char)code, 0 };
			short length = text_safe_code_normalise(out, sizeof(out), source);

			if (length >= 0)
			{
				short at;

				for (at = 0; at < length; at++)
					CHECK((out[at] >= '0' && out[at] <= '9') || (out[at] >= 'A' && out[at] <= 'Z'), "byte %02lX gives '%s'", code, out);
			}
		}
	}
	return 0;
}
