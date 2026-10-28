#include "wiiu_debug_protocol.h"

#include "wiiu_input.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct
{
	const char *name;
	unsigned int button;
} button_names[] =
{
	{ "A", WIIU_PAD_A }, { "B", WIIU_PAD_B }, { "X", WIIU_PAD_X }, { "Y", WIIU_PAD_Y }, { "L", WIIU_PAD_L }, { "R", WIIU_PAD_R },
	{ "ZL", WIIU_PAD_ZL }, { "ZR", WIIU_PAD_ZR }, { "PLUS", WIIU_PAD_PLUS }, { "MINUS", WIIU_PAD_MINUS }, { "UP", WIIU_PAD_UP },
	{ "DOWN", WIIU_PAD_DOWN }, { "LEFT", WIIU_PAD_LEFT }, { "RIGHT", WIIU_PAD_RIGHT }, { "STICKL", WIIU_PAD_STICK_L },
	{ "STICKR", WIIU_PAD_STICK_R },
};

unsigned int wiiu_debug_buttons(const char *names)
{
	unsigned int result = 0;

	while (*names)
	{
		char name[16];
		size_t length = 0;
		unsigned int i;
		unsigned int found = 0;

		while (*names && *names != '+')
		{
			if (length >= sizeof(name) - 1)
				return 0;
			name[length++] = (char)toupper((unsigned char)*names++);
		}
		name[length] = 0;
		for (i = 0; i < sizeof(button_names) / sizeof(button_names[0]); i++)
			if (!strcmp(name, button_names[i].name))
				found = button_names[i].button;
		if (!found)
			return 0;
		result |= found;
		if (*names == '+')
			names++;
	}
	return result;
}

static int fail(wiiu_debug_request *request, const char *text)
{
	snprintf(request->error, sizeof(request->error), "%s", text);
	return 0;
}

/* the next word of the line, in word (cut to capacity); NULL at the end */
static const char *next_word(const char *at, char *word, size_t capacity)
{
	size_t length = 0;

	while (*at == ' ')
		at++;
	if (!*at)
		return NULL;
	while (*at && *at != ' ')
	{
		if (length + 1 < capacity)
			word[length++] = *at;
		at++;
	}
	word[length] = 0;
	return at;
}

/* a whole number of milliseconds (or lines), at most limit */
static int number(const char *word, unsigned int limit, unsigned int *out)
{
	char *end;
	unsigned long value;

	if (!*word || !isdigit((unsigned char)*word))
		return 0;
	value = strtoul(word, &end, 10);
	if (*end || value > limit)
		return 0;
	*out = (unsigned int)value;
	return 1;
}

int wiiu_debug_parse(const char *line, wiiu_debug_request *request)
{
	char word[48];
	const char *at;

	memset(request, 0, sizeof(*request));
	at = next_word(line, word, sizeof(word));
	if (!at)
		return fail(request, "an empty line");
	if (!strcmp(word, "hello"))
	{
		request->command = WIIU_DEBUG_HELLO;
	}
	else if (!strcmp(word, "quit"))
	{
		request->command = WIIU_DEBUG_QUIT;
	}
	else if (!strcmp(word, "screenshot"))
	{
		request->command = WIIU_DEBUG_SCREENSHOT;
		if (!(at = next_word(at, word, sizeof(word))))
			return fail(request, "screenshot tv|drc|back [1|2|4]");
		if (!strcmp(word, "tv"))
			request->which = WIIU_DEBUG_SHOT_TV;
		else if (!strcmp(word, "drc"))
			request->which = WIIU_DEBUG_SHOT_DRC;
		else if (!strcmp(word, "back"))
			request->which = WIIU_DEBUG_SHOT_BACK;
		else
			return fail(request, "screenshot tv|drc|back [1|2|4]");
		request->scale = 1;
		if ((at = next_word(at, word, sizeof(word))) && (!number(word, 4, &request->scale) || request->scale == 3 || !request->scale))
			return fail(request, "the scale is 1, 2 or 4");
	}
	else if (!strcmp(word, "press"))
	{
		request->command = WIIU_DEBUG_PRESS;
		request->milliseconds = WIIU_DEBUG_PRESS_DEFAULT_MS;
		if (!(at = next_word(at, word, sizeof(word))))
			return fail(request, "press BUTTONS [milliseconds], buttons joined by +");
		request->buttons = wiiu_debug_buttons(word);
		if (!request->buttons)
			return fail(request, "unknown button (A B X Y L R ZL ZR PLUS MINUS UP DOWN LEFT RIGHT STICKL STICKR)");
		if ((at = next_word(at, word, sizeof(word))) && (!number(word, WIIU_DEBUG_HOLD_MAX_MS, &request->milliseconds) || !request->milliseconds))
			return fail(request, "the time is 1 to 10000 milliseconds");
	}
	else if (!strcmp(word, "stick"))
	{
		unsigned int i;

		request->command = WIIU_DEBUG_STICK;
		for (i = 0; i < 4; i++)
		{
			char *end;
			float value;

			if (!(at = next_word(at, word, sizeof(word))))
				return fail(request, "stick LX LY RX RY milliseconds");
			value = strtof(word, &end);
			if (*end || !(value >= -1.0f && value <= 1.0f))
				return fail(request, "a stick value is -1 to 1");
			request->stick[i] = value;
		}
		if (!(at = next_word(at, word, sizeof(word))) || !number(word, WIIU_DEBUG_HOLD_MAX_MS, &request->milliseconds) || !request->milliseconds)
			return fail(request, "the time is 1 to 10000 milliseconds");
	}
	else if (!strcmp(word, "tap"))
	{
		unsigned int i;

		request->command = WIIU_DEBUG_TAP;
		request->milliseconds = WIIU_DEBUG_PRESS_DEFAULT_MS;
		for (i = 0; i < 2; i++)
		{
			char *end;
			float value;

			if (!(at = next_word(at, word, sizeof(word))))
				return fail(request, "tap X Y [milliseconds]");
			value = strtof(word, &end);
			if (*end || !(value >= 0.0f && value < (i ? 480.0f : 854.0f)))
				return fail(request, "the touch screen is 854 by 480");
			request->tap[i] = value;
		}
		if ((at = next_word(at, word, sizeof(word))) && (!number(word, WIIU_DEBUG_HOLD_MAX_MS, &request->milliseconds) || !request->milliseconds))
			return fail(request, "the time is 1 to 10000 milliseconds");
	}
	else if (!strcmp(word, "gyro"))
	{
		unsigned int i;

		request->command = WIIU_DEBUG_GYRO;
		for (i = 0; i < 2; i++)
		{
			char *end;
			float value;

			if (!(at = next_word(at, word, sizeof(word))))
				return fail(request, "gyro PITCH YAW milliseconds");
			value = strtof(word, &end);
			if (*end || !(value >= -10.0f && value <= 10.0f))
				return fail(request, "a gyro rate is -10 to 10 rotations a second");
			request->gyro[i] = value;
		}
		if (!(at = next_word(at, word, sizeof(word))) || !number(word, WIIU_DEBUG_HOLD_MAX_MS, &request->milliseconds) || !request->milliseconds)
			return fail(request, "the time is 1 to 10000 milliseconds");
	}
	else if (!strcmp(word, "log"))
	{
		request->command = WIIU_DEBUG_LOG;
		request->count = WIIU_DEBUG_LOG_DEFAULT;
		if ((at = next_word(at, word, sizeof(word))) && (!number(word, WIIU_DEBUG_LOG_MAX, &request->count) || !request->count))
			return fail(request, "log takes 1 to 100 lines");
	}
	else
	{
		return fail(request, "unknown command (hello quit screenshot press stick tap gyro log)");
	}
	return 1;
}
