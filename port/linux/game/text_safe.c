/*
TEXT_SAFE.C

What may be drawn of text that came from outside (text_safe.h).
*/

#include "text_safe.h"

#include <string.h>

int text_safe_character_ok(unsigned long code)
{
	return code >= 0x20 && code <= 0x7E && code != '|';
}

/* the Latin letters with marks of U+00C0 to U+017F as plain ones ("?" for the few with none), the table of players.c's player_name_character_ascii (the harness
checks that they agree) */
static char const latin_fold[] =
	"AAAAAAACEEEEIIIIDNOOOOO?OUUUUYTs"
	"aaaaaaaceeeeiiiidnooooo?ouuuuyty"
	"AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlL"
	"lLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs";

char text_safe_character(unsigned long code, int fold)
{
	if (text_safe_character_ok(code))
		return (char)code;
	if (fold && code >= 0xC0 && code < 0x180)
		return latin_fold[code - 0xC0];
	return '?';
}

short text_safe_text(char *destination, short size, char const *source, short source_size, short maximum)
{
	short length = 0;
	short spaces = 0;
	short index;

	if (!destination || size <= 0)
		return 0;
	if (maximum > size - 1)
		maximum = (short)(size - 1);
	for (index = 0; source && index < source_size && source[index] && length < maximum; index++)
	{
		char character = text_safe_character((unsigned char)source[index], 0);

		if (character == ' ')
		{
			/* (none at the start, and no more than two together) */
			if (length == 0 || spaces >= 2)
				continue;
			spaces++;
		}
		else
			spaces = 0;
		destination[length++] = character;
	}
	while (length > 0 && destination[length - 1] == ' ')
		length--;
	destination[length] = 0;
	return length;
}

short text_safe_label(char *destination, short size, char const *source, short source_size, short maximum)
{
	short length = text_safe_text(destination, size, source, source_size, maximum);

	if (!length && destination && size >= 2)
	{
		destination[0] = '?';
		destination[1] = 0;
		length = 1;
	}
	return length;
}

/* characters that draw as nothing, or a line of their own, or which a person cannot read as a name: dropped (player_name_clean's list, players.c). Unicode's spaces are
made plain ones before this */
static int character_dropped(unsigned short character)
{
	return character < 0x20 || (character >= 0x7F && character <= 0x9F) || character == 0xAD || character == 0x34F || character == 0x115F ||
		character == 0x1160 || character == 0x17B4 || character == 0x17B5 || (character >= 0x180B && character <= 0x180E) ||
		(character >= 0x200B && character <= 0x200F) || (character >= 0x2028 && character <= 0x202E) || (character >= 0x2060 && character <= 0x206F) ||
		character == 0x2800 || character == 0x3164 || (character >= 0xD800 && character <= 0xDFFF) || (character >= 0xFE00 && character <= 0xFE0F) ||
		character == 0xFEFF || character == 0xFFA0 || character >= 0xFFF0 || character == '|';
}

static int character_space(unsigned short character)
{
	return character == ' ' || character == 0xA0 || character == 0x1680 || (character >= 0x2000 && character <= 0x200A) || character == 0x202F ||
		character == 0x205F || character == 0x3000;
}

short text_safe_name(char *destination, short size, wchar_t const *source, long count, char const *fallback)
{
	short length = 0;
	short readable = 0;
	short maximum = TEXT_SAFE_NAME_LENGTH;
	long index;

	if (!destination || size <= 0)
		return 0;
	if (maximum > size - 1)
		maximum = (short)(size - 1);
	for (index = 0; source && index < count && source[index] && length < maximum; index++)
	{
		unsigned short character = (unsigned short)source[index];
		char plain;

		if (character_space(character))
		{
			/* (none at the start, one at a time) */
			if (length == 0 || destination[length - 1] == ' ')
				continue;
			destination[length++] = ' ';
			continue;
		}
		if (character_dropped(character))
			continue;
		plain = text_safe_character(character, 1);
		if (plain != '?')
			readable++;
		destination[length++] = plain;
	}
	while (length > 0 && destination[length - 1] == ' ')
		length--;
	destination[length] = 0;
	if (readable)
		return length;
	/* (nothing a person could read: the fallback, cleaned as a name is, and "PLAYER" for it if it comes to nothing too) */
	{
		char const *text = fallback && fallback[0] ? fallback : "PLAYER";
		short used = 0;
		short at;

		for (at = 0; text[at] && used < maximum; at++)
		{
			char character = text_safe_character((unsigned char)text[at], 0);

			if (character == '?' || (character == ' ' && (used == 0 || destination[used - 1] == ' ')))
				continue;
			destination[used++] = character;
		}
		while (used > 0 && destination[used - 1] == ' ')
			used--;
		if (!used)
		{
			for (at = 0; "PLAYER"[at] && used < maximum; at++)
				destination[used++] = "PLAYER"[at];
		}
		destination[used] = 0;
		return used;
	}
}

short text_safe_name_utf8(char *destination, short size, char const *source, long source_size, char const *fallback)
{
	wchar_t decoded[64];
	long count = 0;
	long index = 0;

	while (source && index < source_size && source[index] && count < (long)(sizeof(decoded) / sizeof(decoded[0])) - 1)
	{
		unsigned char lead = (unsigned char)source[index];
		unsigned long code = 0;
		int extra = 0;
		int at;

		if (lead < 0x80)
			code = lead;
		else if (lead >= 0xC2 && lead < 0xE0)
		{
			code = lead & 0x1F;
			extra = 1;
		}
		else if (lead >= 0xE0 && lead < 0xF0)
		{
			code = lead & 0x0F;
			extra = 2;
		}
		else if (lead >= 0xF0 && lead < 0xF5)
		{
			code = lead & 0x07;
			extra = 3;
		}
		else
			code = 0xFFFD;
		index++;
		for (at = 0; at < extra; at++)
		{
			if (index >= source_size || ((unsigned char)source[index] & 0xC0) != 0x80)
			{
				code = 0xFFFD;
				break;
			}
			code = (code << 6) | ((unsigned char)source[index] & 0x3F);
			index++;
		}
		/* (past U+FFFF there are no 16-bit characters to keep; "?" for it, and for what was malformed) */
		decoded[count++] = (wchar_t)(code > 0xFFFF || code == 0xFFFD ? '?' : code);
	}
	decoded[count] = 0;
	return text_safe_name(destination, size, decoded, count, fallback);
}

short text_safe_code_normalise(char *destination, short size, char const *source)
{
	short length = 0;

	if (!destination || size <= 0)
		return -1;
	destination[0] = 0;
	for (; source && *source; source++)
	{
		char character = *source;

		if (character == ' ' || character == '-' || character == '\t')
			continue;
		if (character >= 'a' && character <= 'z')
			character = (char)(character - 'a' + 'A');
		if (character == 'I' || character == 'L')
			character = '1';
		else if (character == 'O')
			character = '0';
		else if (character == 'U')
			character = 'V';
		if (!((character >= '0' && character <= '9') || (character >= 'A' && character <= 'Z')))
			return -1;
		if (length >= TEXT_SAFE_CODE_LENGTH || length >= size - 1)
			return -1;
		destination[length++] = character;
	}
	destination[length] = 0;
	return length;
}
