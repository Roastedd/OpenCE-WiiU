/*
HALO_SOCIAL.H

What the game gives a platform that draws the social screens' GamePad view itself (the Wii U's: port/wiiu/src/wiiu_social_view.h), and what the platform gives back: plain numbers and
characters only, so that the game's code and the platform's, built with different ABIs, agree on it (as halo_keyboard.h's keyboard view). The game builds the view from the social
model (port/linux/game/screens_social.c: the same rows the menus show), the platform draws it on the GamePad's screen alone and tells the game what was touched.

Setting: input.wiiu_gamepad_view = "social" (opt-in; the TV is then the game's alone). Nothing here does anything without it.
*/

#ifndef HALO_SOCIAL_H
#define HALO_SOCIAL_H

enum
{
	HALO_SOCIAL_CHAT_LINES = 7,
	HALO_SOCIAL_ROWS = 5,
	HALO_SOCIAL_TABS = 3,
	HALO_SOCIAL_TILES = 4,
	HALO_SOCIAL_TEXT = 64,
};

enum
{
	HALO_SOCIAL_TAB_FRIENDS,
	HALO_SOCIAL_TAB_PARTY,
	HALO_SOCIAL_TAB_BOTS,
};

/* what a row's button does (none: no button) */
enum
{
	HALO_SOCIAL_ACTION_NONE,
	HALO_SOCIAL_ACTION_JOIN,
	HALO_SOCIAL_ACTION_INVITE,
};

struct halo_social_line
{
	/* clean ASCII (text_safe), never empty when the line is used */
	char name[12];
	char text[HALO_SOCIAL_TEXT];
	/* -1 none, 0 red, 1 blue; 2 party */
	signed char team;
};

struct halo_social_row
{
	char mark[4];
	char name[12];
	char status[48];
	/* 0xRRGGBB of the mark and name, and of the status */
	unsigned long name_rgb, status_rgb;
	unsigned char action;
};

struct halo_social_view
{
	/* nonzero: the view is to be drawn (the setting is on, a game or lobby is up, no menu is) */
	int active;
	int tab;
	/* the header: where and who ("bloodgulch  Slayer  12 of 16") and the party */
	char head[HALO_SOCIAL_TEXT];
	char party[24];
	char tabs[HALO_SOCIAL_TABS][20];
	struct halo_social_line chat[HALO_SOCIAL_CHAT_LINES];
	int chat_count;
	struct halo_social_row rows[HALO_SOCIAL_ROWS];
	int row_count;
	/* a line for an empty list, in words */
	char empty[HALO_SOCIAL_TEXT];
	/* the bots tab: two lines (BOTS 4, SKILL HEROIC), and whether this machine may change them */
	char bots[2][24];
	int bots_host;
	/* the quick chat page: its four lines and its name */
	char tiles[HALO_SOCIAL_TILES][24];
	char page[24];
	/* a toast now (alpha 0: none) with its kind (screens.h's SCREENS_TOAST_*), and whether it can be answered */
	char toast[HALO_SOCIAL_TEXT];
	char toast_detail[HALO_SOCIAL_TEXT];
	int toast_kind;
	int toast_alpha;
	int toast_invite;
};

enum
{
	HALO_SOCIAL_EVENT_NONE,
	/* a tab: index */
	HALO_SOCIAL_EVENT_TAB,
	/* a row's button: index (the row) */
	HALO_SOCIAL_EVENT_ROW,
	/* a quick chat line: index */
	HALO_SOCIAL_EVENT_TILE,
	HALO_SOCIAL_EVENT_KEYBOARD,
	HALO_SOCIAL_EVENT_PAGE_PREVIOUS,
	HALO_SOCIAL_EVENT_PAGE_NEXT,
	/* the bots tab: index 0 bots, 1 skill; the sign */
	HALO_SOCIAL_EVENT_BOTS_MINUS,
	HALO_SOCIAL_EVENT_BOTS_PLUS,
	/* the toast's invitation: join, later */
	HALO_SOCIAL_EVENT_TOAST_JOIN,
	HALO_SOCIAL_EVENT_TOAST_LATER,
};

struct halo_social_event
{
	int kind;
	int index;
};

/* the view as it is now (every frame, from the game's thread); the platform keeps the latest for the thread that draws it. Nothing on the desktop. */
void halo_social_view_publish(const struct halo_social_view *view);
/* the next touch that meant something (nonzero: *event is filled), from the game's thread; nothing on the desktop */
int halo_social_event_next(struct halo_social_event *event);
/* whether the platform draws the view (input.wiiu_gamepad_view is "social" and the glyphs are made); nonzero on the Wii U only */
int halo_social_view_wanted(void);

#endif
