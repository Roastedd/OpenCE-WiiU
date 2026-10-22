/*
WIIU_DEBUG_PROTOCOL.H

The debug server's lines (wiiu_debug.c) read into requests: plain C, the same on the console and on a desktop, where the tests run it.

One command to a line, words separated by spaces:

	hello                          frames, foreground, memory
	quit                           the Wii U Menu is asked for; the app ends
	screenshot tv|drc|back [1|2|4] the TV's or the GamePad's picture, or the game's own back buffer; every 2nd or 4th pixel and row (the console's
	                               network takes about 400 KB a second: a 1080p picture is 8 MB, 2 MB at 2)
	press A+UP 150                 buttons (names as in wiiu_input.h, joined by +) held for some milliseconds
	stick 0 1 0 0 500              left x y, right x y (-1 to 1, up and right positive) held for some milliseconds
	log 20                         the last lines of the shell's log
*/

#ifndef HALO_WIIU_DEBUG_PROTOCOL_H
#define HALO_WIIU_DEBUG_PROTOCOL_H

enum
{
	WIIU_DEBUG_NONE,
	WIIU_DEBUG_HELLO,
	WIIU_DEBUG_QUIT,
	WIIU_DEBUG_SCREENSHOT,
	WIIU_DEBUG_PRESS,
	WIIU_DEBUG_STICK,
	WIIU_DEBUG_LOG,
};

enum
{
	WIIU_DEBUG_SHOT_BACK = 1,
	WIIU_DEBUG_SHOT_TV,
	WIIU_DEBUG_SHOT_DRC,
};

#define WIIU_DEBUG_PRESS_DEFAULT_MS 150u
#define WIIU_DEBUG_HOLD_MAX_MS 10000u
#define WIIU_DEBUG_LOG_DEFAULT 20u
#define WIIU_DEBUG_LOG_MAX 100u

typedef struct wiiu_debug_request
{
	int command;
	int which;              /* screenshot: WIIU_DEBUG_SHOT_* */
	unsigned int scale;     /* screenshot: 1, 2 or 4 */
	unsigned int buttons;   /* press: WIIU_PAD_* bits */
	float stick[4];         /* stick: left x, left y, right x, right y */
	unsigned int milliseconds;
	unsigned int count;     /* log: lines */
	char error[96];         /* when parsing failed */
} wiiu_debug_request;

/* Reads a line (without its newline). Returns 1 with the request, or 0 with request->error saying what is wrong with the line. */
int wiiu_debug_parse(const char *line, wiiu_debug_request *request);

/* "A+UP" as WIIU_PAD_* bits; 0 if a name is not a button (or there is none) */
unsigned int wiiu_debug_buttons(const char *names);

#endif
