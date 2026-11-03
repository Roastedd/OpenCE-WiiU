/*
WIIU_VIRTUAL_PADS.C

The script of debug.virtual_gamepad_script played on the Wii U's ports (wiiu_virtual_pads.h). It follows xinput_sdl.c's virtual gamepads (the same
entries, the same press of 150 ms and until read 8 times, the same walk), which cannot be the Wii U's: they are SDL's.
*/

#include "wiiu_virtual_pads.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PRESS_MS 150
#define PRESS_POLLS 8
/* the entries of a script that are given polls to be pressed for (the rest are never pressed) */
#define ENTRIES 256

static unsigned long started_at;
static int started;
static int walking[WIIU_VIRTUAL_PADS];
static int entry_polls[ENTRIES];
static struct wiiu_virtual_pad pads[WIIU_VIRTUAL_PADS];

static int pad_count(int count)
{
	return count < 0 ? 0 : count > WIIU_VIRTUAL_PADS ? WIIU_VIRTUAL_PADS : count;
}

void wiiu_virtual_pads_reset(void)
{
	started = 0;
	started_at = 0;
	memset(walking, 0, sizeof(walking));
	memset(entry_polls, 0, sizeof(entry_polls));
	memset(pads, 0, sizeof(pads));
}

void wiiu_virtual_pads_start(int count, unsigned long milliseconds)
{
	if (!started && pad_count(count) > 0)
	{
		started = 1;
		started_at = milliseconds;
	}
}

int wiiu_virtual_pads_started(void)
{
	return started;
}

static unsigned int button_named(const char *what)
{
	static const struct
	{
		const char *name;
		unsigned int button;
	} names[] =
	{
		{ "a", WIIU_VPAD_A }, { "b", WIIU_VPAD_B }, { "x", WIIU_VPAD_X }, { "y", WIIU_VPAD_Y }, { "start", WIIU_VPAD_START },
		{ "back", WIIU_VPAD_BACK }, { "up", WIIU_VPAD_UP }, { "down", WIIU_VPAD_DOWN }, { "left", WIIU_VPAD_LEFT },
		{ "right", WIIU_VPAD_RIGHT }, { "lb", WIIU_VPAD_LB }, { "rb", WIIU_VPAD_RB }, { "lt", WIIU_VPAD_LT }, { "rt", WIIU_VPAD_RT },
	};
	unsigned int index;

	for (index = 0; index < sizeof(names) / sizeof(names[0]); index++)
	{
		if (!strcmp(names[index].name, what))
			return names[index].button;
	}
	return 0;
}

void wiiu_virtual_pads_update(int count, const char *script, unsigned long milliseconds)
{
	unsigned int pressed[WIIU_VIRTUAL_PADS];
	const char *entry = script ? script : "";
	const int pad_total = pad_count(count);
	/* (before the clock starts, nothing is pressed) */
	const double now = started ? (double)(unsigned long)(milliseconds - started_at) / 1000.0 : -1.0;
	int entry_index, pad;

	memset(pressed, 0, sizeof(pressed));
	for (entry_index = 0; pad_total && *entry; entry_index++)
	{
		double at = 0.0, duration = 0.0;
		int number = 0, consumed = 0, held;
		char what[16];

		entry += strspn(entry, " ;");
		if (!*entry)
			break;
		what[0] = 0;
		if (sscanf(entry, "%lf %d %15[a-z]%n", &at, &number, what, &consumed) == 3 && number >= 1 && number <= pad_total && now >= at)
		{
			const unsigned int button = button_named(what);
			const char *rest = entry + consumed + strspn(entry + consumed, " ");

			/* (a button, then how many seconds it is held for: "60 2 rt 30", which the desktop's script does not have) */
			if (*rest && *rest != ';')
			{
				duration = strtod(rest, NULL);
				if (!(duration > 0.0 && duration <= 3600.0))
					duration = 0.0;
			}
			pad = number - 1;
			if (!strcmp(what, "walk"))
			{
				walking[pad] = 1;
			}
			else if (!strcmp(what, "still"))
			{
				walking[pad] = 0;
			}
			else if (button)
			{
				held = entry_index < ENTRIES && (duration > 0.0 ? now < at + duration || entry_polls[entry_index] < 1 :
					now < at + PRESS_MS / 1000.0 || entry_polls[entry_index] < PRESS_POLLS);
				if (held)
				{
					entry_polls[entry_index]++;
					pressed[pad] |= button;
				}
			}
		}
		entry += strcspn(entry, ";");
	}
	for (pad = 0; pad < WIIU_VIRTUAL_PADS; pad++)
	{
		const double t = now + pad * 2.3;
		const int move = walking[pad] && pad < pad_total;

		pads[pad].buttons = pad < pad_total ? pressed[pad] : 0;
		pads[pad].left_x = move ? (short)(cos(t * 0.6 + pad) * 20000.0) : 0;
		pads[pad].left_y = move ? (short)(sin(t * 0.9) * 30000.0) : 0;
		pads[pad].right_x = move ? (short)(sin(t * 0.4 + pad) * 16000.0) : 0;
		pads[pad].right_y = 0;
	}
}

unsigned int wiiu_virtual_pads_ports(int count)
{
	unsigned int ports = 0;
	int port;

	for (port = 1; port < pad_count(count); port++)
		ports |= 1u << port;
	return ports;
}

void wiiu_virtual_pads_get(unsigned int port, struct wiiu_virtual_pad *pad)
{
	if (port < WIIU_VIRTUAL_PADS)
		*pad = pads[port];
	else
		memset(pad, 0, sizeof(*pad));
}
