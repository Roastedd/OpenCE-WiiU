/*
SCREENS.H

The data the social screens show and the one thin interface that feeds them: the Players page (the pause menu's PLAYERS, and the same rows in the lobby), the bot settings, the
Friends screen (friends, requests, party, blocked), the toasts and the GamePad's SOCIAL view. The screens (menu_screens.c: tag-driven menus; screens_toast.c: the toasts, in the
frame's overlays; wiiu_social_view.c: the GamePad) read rows from the provider that is set and never from a backend directly, so the backends (the social stream's friends,
presence and party; the bots stream's roster) fill a table of function pointers and nothing of a screen changes. This file and screens.c have no game types, so that the harness
builds them alone (tools/harness/tests/test_screens.py).

Everything shown is ASCII 0x20 to 0x7E without "|" (text_safe.h): a provider fills names with text_safe_name* once, where the name arrives, and the rows are never drawn from a
stranger's raw string. Names are 11 characters at most (SCREENS_NAME_LENGTH); the other texts a provider fills (a map, a game type) are cut to what their column holds here
(screens_fit), not by the provider.

Settings (config.toml, or the environment):
  screens.enable   0 (the default)  none of these screens exists: no button, no row, no tag, no toast, no drawing, the game and its frames as they were
                   1                the screens exist and are fed by the mock provider (made-up players, friends, a party and toasts, for looking at them with no network)
                   2                the screens exist and are fed by the game's own roster (screens_game.c: the real players of a lobby or a game, bots tagged) and the provider a backend
                                    set (screens_provider_set); a table with no function for something (friends, party) shows that screen empty, as "no friend service"
*/

#ifndef HALO_SCREENS_H
#define HALO_SCREENS_H

#include <stddef.h>

enum
{
	/* a name: 11 characters (players.h's wchar_t name[12]), and the end */
	SCREENS_NAME_LENGTH = 11,
	SCREENS_NAME_SIZE = 12,
	/* a map's or a game type's text as a provider gives it */
	SCREENS_WORD_SIZE = 32,
	/* a friend code as shown: XXXX-XXXX-XXXX-C */
	SCREENS_CODE_SIZE = 20,
	/* a line of any cell, a toast's text, an answer in words */
	SCREENS_LINE_SIZE = 72,
	SCREENS_ROSTER_MAXIMUM = 128,
	SCREENS_FRIENDS_MAXIMUM = 50,
	SCREENS_PARTY_MAXIMUM = 8,
	SCREENS_TOASTS_MAXIMUM = 8,
	SCREENS_ACTIONS_MAXIMUM = 8,
	/* the skills of a bot, bots.c's order */
	SCREENS_SKILLS = 6,
};

/* ---------- colours, as the menus have them (a name for each use, so that no screen picks its own) */

enum
{
	/* the menus' azure (#2896FF): labels, rows, the accent */
	SCREENS_COLOR_AZURE,
	SCREENS_COLOR_WHITE,
	/* a warning, a bot, a state that waits: amber #FFB020 */
	SCREENS_COLOR_AMBER,
	/* online, joinable, a friend: green #40D050 */
	SCREENS_COLOR_GREEN,
	/* offline, nothing: grey #708090 */
	SCREENS_COLOR_GREY,
	/* a refusal, blocked: red #FF4040 */
	SCREENS_COLOR_ALERT,
	/* the teams' colours as the chat has them: red (0.86, 0.32, 0.28), blue (0.34, 0.51, 0.86) */
	SCREENS_COLOR_RED_TEAM,
	SCREENS_COLOR_BLUE_TEAM,
	SCREENS_COLORS
};

/* 0xRRGGBB of a colour */
unsigned long screens_color_rgb(int color);

/* ---------- the Players page: one row for each player of the lobby or the game */

enum
{
	SCREENS_TEAM_NONE = -1,
	SCREENS_TEAM_RED = 0,
	SCREENS_TEAM_BLUE = 1,
};

enum
{
	/* this machine's own (first local) player */
	SCREENS_ROW_ME = 1 << 0,
	/* the game's host */
	SCREENS_ROW_HOST = 1 << 1,
	/* a computer player */
	SCREENS_ROW_BOT = 1 << 2,
	/* in this machine's party */
	SCREENS_ROW_PARTY = 1 << 3,
	/* on this machine's friends list */
	SCREENS_ROW_FRIEND = 1 << 4,
	/* a second to fourth local player of this machine (split screen) */
	SCREENS_ROW_LOCAL = 1 << 5,
	/* talking now / muted by this machine */
	SCREENS_ROW_SPEAKING = 1 << 6,
	SCREENS_ROW_MUTED = 1 << 7,
};

struct screens_roster_row
{
	/* clean ASCII (text_safe_name): never empty, 11 characters at most */
	char name[SCREENS_NAME_SIZE];
	signed char team;
	/* a bot's skill, 0 recruit to 5 legendary; -1 for a person */
	signed char skill;
	unsigned short flags;
	/* round trip in milliseconds; -1: none (a bot, this machine's own players) */
	short ping;
	/* kills (or the game's score); -1: not known */
	short score;
	/* the game's machine for a row (a backend's key: its actions name the row by it) */
	long machine;
};

struct screens_roster_summary
{
	short players;
	short bots;
	/* the game's capacity, 0 if unknown */
	short maximum;
	/* settings the host would add a bot with: the skill (0-5) and the bots the host wants (bots.count) */
	short skill;
	short bots_wanted;
	/* nonzero: this machine hosts, and may add and drop bots and kick */
	unsigned char host;
	/* nonzero: a game with teams */
	unsigned char teams;
	/* nonzero: a game in progress (a network game's clock runs while the page is open) */
	unsigned char running;
	char map[SCREENS_WORD_SIZE];
	char gametype[SCREENS_WORD_SIZE];
};

/* what A on a row offers (by who the row is): the provider says which it can do */
enum
{
	SCREENS_ACTION_NONE,
	SCREENS_ACTION_MUTE,
	SCREENS_ACTION_UNMUTE,
	SCREENS_ACTION_VOTE_KICK,
	SCREENS_ACTION_KICK,
	SCREENS_ACTION_BAN,
	SCREENS_ACTION_MUTE_ALL,
	SCREENS_ACTION_ADD_FRIEND,
	SCREENS_ACTION_INVITE_PARTY,
	SCREENS_ACTION_DROP_BOT,
	SCREENS_ACTION_BOT_SKILL_UP,
	SCREENS_ACTION_BOT_SKILL_DOWN,
	SCREENS_ACTION_CHANGE_TEAM,
	SCREENS_ACTION_JOIN_GAME,
	SCREENS_ACTION_INVITE_GAME,
	SCREENS_ACTION_VIEW_CODE,
	SCREENS_ACTION_REMOVE_FRIEND,
	SCREENS_ACTION_BLOCK,
	SCREENS_ACTION_UNBLOCK,
	SCREENS_ACTION_ACCEPT,
	SCREENS_ACTION_DECLINE,
	SCREENS_ACTION_MAKE_LEADER,
	SCREENS_ACTION_REMOVE_FROM_PARTY,
	SCREENS_ACTION_LEAVE_PARTY,
	SCREENS_ACTION_DISBAND_PARTY,
	SCREENS_ACTION_FOLLOW_ON,
	SCREENS_ACTION_FOLLOW_OFF,
	/* the host's: the bot settings screen (the actions screen opens it) */
	SCREENS_ACTION_BOT_SETTINGS,
	SCREENS_ACTION_CANCEL,
	SCREENS_ACTIONS
};

/* the words of an action, as a menu row (upper case) */
char const *screens_action_word(int action);

/* ---------- the Friends screen: four lists and a code */

enum
{
	SCREENS_TAB_FRIENDS,
	SCREENS_TAB_REQUESTS,
	SCREENS_TAB_PARTY,
	SCREENS_TAB_BLOCKED,
	SCREENS_TABS
};

enum
{
	SCREENS_PRESENCE_OFFLINE,
	SCREENS_PRESENCE_AWAY,
	/* in the menus */
	SCREENS_PRESENCE_ONLINE,
	SCREENS_PRESENCE_LOBBY,
	SCREENS_PRESENCE_PLAYING,
	/* a request this machine sent, waiting; one it got */
	SCREENS_PRESENCE_REQUEST_SENT,
	SCREENS_PRESENCE_REQUEST_GOT,
	SCREENS_PRESENCE_BLOCKED,
};

/* whether a game a friend is in can be joined, and if not why (the words of the join failures) */
enum
{
	SCREENS_JOIN_NONE,
	SCREENS_JOIN_OPEN,
	SCREENS_JOIN_FULL,
	SCREENS_JOIN_CLOSED,
	SCREENS_JOIN_PASSWORD,
	SCREENS_JOIN_NEWER_BUILD,
	SCREENS_JOIN_OLDER_BUILD,
	/* the map is not here and can be fetched (the DOWNLOAD marker) / cannot */
	SCREENS_JOIN_DOWNLOAD,
	SCREENS_JOIN_NEEDS_MAP,
};

struct screens_friend_row
{
	char name[SCREENS_NAME_SIZE];
	char code[SCREENS_CODE_SIZE];
	unsigned char presence;
	unsigned char join;
	/* in this machine's party */
	unsigned char party;
	/* the game's players and capacity, 0 if none */
	short players;
	short maximum;
	char map[SCREENS_WORD_SIZE];
	char gametype[SCREENS_WORD_SIZE];
	/* a backend's key for the row (an action names the row by it) */
	long key;
};

enum
{
	SCREENS_SERVICE_OFF,
	SCREENS_SERVICE_CONNECTING,
	SCREENS_SERVICE_CONNECTED,
	/* the card or the settings switched friends off */
	SCREENS_SERVICE_DISABLED,
};

struct screens_friends_summary
{
	/* this machine's code, XXXX-XXXX-XXXX-C ("" if none yet) */
	char own_code[SCREENS_CODE_SIZE];
	unsigned char service;
	/* the rows of each tab, and how many of the friends are online */
	short counts[SCREENS_TABS];
	short online;
};

/* a party member's state, in words (the failed ones stay in the list with the reason) */
enum
{
	SCREENS_MEMBER_READY,
	SCREENS_MEMBER_MENUS,
	SCREENS_MEMBER_JOINING,
	SCREENS_MEMBER_DOWNLOADING,
	SCREENS_MEMBER_NEEDS_MAP,
	SCREENS_MEMBER_FULL_RETRYING,
	SCREENS_MEMBER_NEWER_BUILD,
	SCREENS_MEMBER_NO_ROUTE,
	SCREENS_MEMBER_IN_GAME,
	SCREENS_MEMBER_OFFLINE,
	SCREENS_MEMBER_STAYING,
};

struct screens_party_member
{
	char name[SCREENS_NAME_SIZE];
	unsigned char leader;
	unsigned char me;
	unsigned char state;
	/* percent of a join or a download, or the seconds of a retry; 0 if none */
	short amount;
	long key;
};

struct screens_party_summary
{
	/* nonzero: this machine is in a party */
	unsigned char present;
	short size;
	short maximum;
	/* follow the leader: 1 on, 0 off */
	unsigned char follow;
	/* the room the leader's game holds for the party: 0 not known, 1 held, 2 not guaranteed */
	unsigned char room;
	char leader_game[SCREENS_LINE_SIZE];
	/* nonzero: this machine leads the party (it may make another leader, remove a member, disband) */
	unsigned char i_lead;
};

/* ---------- toasts */

enum
{
	SCREENS_TOAST_FRIEND_ONLINE,
	SCREENS_TOAST_PARTY_JOINED,
	SCREENS_TOAST_PARTY_LEFT,
	SCREENS_TOAST_REQUEST,
	/* an invitation: answered with the D-pad (right joins, left later) */
	SCREENS_TOAST_INVITE,
	/* a join that did not work, in plain words */
	SCREENS_TOAST_JOIN_FAILED,
	SCREENS_TOAST_NOTICE,
	SCREENS_TOAST_KINDS
};

struct screens_toast
{
	long id;
	unsigned char kind;
	/* the line, and a second line (an invitation's game, a failure's reason) */
	char text[SCREENS_LINE_SIZE];
	char detail[SCREENS_LINE_SIZE];
};

/* ---------- the provider: a table of functions, any of which may be NULL (the screen then does without it) */

struct screens_provider
{
	char const *name;
	/* the number of the model's latest change; a screen builds its rows again only when it moved (the backends' threads count it; reading it takes no lock) */
	unsigned long (*generation)(void);
	/* the players: rows (at most maximum), their count; the summary may be NULL */
	int (*roster)(struct screens_roster_row *rows, int maximum, struct screens_roster_summary *summary);
	/* the friends screen's tab: rows, their count; the summary (the code, the service, the counts) may be NULL */
	int (*friends)(int tab, struct screens_friend_row *rows, int maximum, struct screens_friends_summary *summary);
	int (*party)(struct screens_party_member *members, int maximum, struct screens_party_summary *summary);
	/* the next toast, if one is waiting (nonzero: *toast is filled): a backend queues them as things happen; the UI shows them one at a time */
	int (*toast_pop)(struct screens_toast *toast);
	/* an invitation answered: nonzero accepted (join), 0 later */
	void (*invite_answer)(long id, int accept);
	/* a roster row's action (a SCREENS_ACTION_*): nonzero done. The words of a refusal go in answer. */
	int (*roster_action)(int action, struct screens_roster_row const *row, char *answer, int size);
	/* the bot buttons: add one (team SCREENS_TEAM_NONE: the smaller; skill -1: bots.skill's), drop one (machine -1: the weakest on the bigger team) */
	int (*bot_add)(int team, int skill, char *answer, int size);
	int (*bot_drop)(long machine, char *answer, int size);
	/* the friends screen's actions on a row of a tab; party actions name a member */
	int (*friend_action)(int action, struct screens_friend_row const *row, char *answer, int size);
	int (*party_action)(int action, struct screens_party_member const *member, char *answer, int size);
	/* a friend code typed (the characters as the keyboard gave them; the screen has normalised them): sent as a request, or refused in words */
	int (*friend_add)(char const *code, char *answer, int size);
	/* which actions the row offers (a list of SCREENS_ACTION_*, ended by SCREENS_ACTION_NONE: an array of SCREENS_ACTIONS_MAXIMUM + 1 ints): by who it is; NULL: the screen's own rule */
	void (*roster_actions)(struct screens_roster_row const *row, struct screens_roster_summary const *summary, int *actions);
	void (*friend_actions)(int tab, struct screens_friend_row const *row, int *actions);
	void (*party_actions)(struct screens_party_member const *member, struct screens_party_summary const *summary, int *actions);
};

/* screens.enable (0 off, 1 mock, 2 the game's roster and the provider set), read once */
int screens_enabled(void);
/* what A on a row offers when the provider has no list of its own (screens.c: by who the row is; the last is CANCEL, then SCREENS_ACTION_NONE) */
void screens_default_roster_actions(struct screens_roster_row const *row, struct screens_roster_summary const *summary, int *actions);
void screens_default_friend_actions(int tab, struct screens_friend_row const *row, int *actions);
void screens_default_party_actions(struct screens_party_member const *member, struct screens_party_summary const *summary, int *actions);
/* the line a screen shows in place of rows it has none of, in plain words (screen 0 the Players page, 1 the Friends screen): why the list is empty */
void screens_empty_text(int screen, int tab, struct screens_friends_summary const *summary, int have_provider, char *text, int size);
/* the same words, broken into the two lines of the TV screens' detail line (screens_wrap) */
void screens_empty_wrapped(int screen, int tab, struct screens_friends_summary const *summary, int have_provider, char *text, int size);
/* the provider in use: NULL if none is set (a screen with none shows itself empty, with a word for why) */
struct screens_provider const *screens_provider(void);
void screens_provider_set(struct screens_provider const *provider);
/* the social stream's table (friends, party, toasts, invites, actions on people): a build that has a friends service sets it once at start; the screens with screens.enable 2 take what it has
(a NULL function is a feature the screens leave out), and the game's own players for the roster. NULL: no friends service (the Friends screen says so). */
struct screens_provider const *screens_backend(void);
void screens_backend_set(struct screens_provider const *backend);
/* the mock's table (screens_mock.c): made-up players with bots, friends in every state, a party of five, toasts, and actions that change the made-up data */
struct screens_provider const *screens_mock_provider(void);
/* back to the mock's starting data (tests, and a screen opened afresh) */
void screens_mock_reset(void);
/* what the mock has queued: nothing more than this many toasts left (a test's check) */
int screens_mock_toasts_left(void);

/* ---------- words: the rows' texts, as the menus draw them (all ASCII, upper case where the menus' are) */

char const *screens_skill_word(int skill);
char const *screens_team_word(int team);
char const *screens_presence_word(int presence);
char const *screens_join_word(int join);
char const *screens_member_word(int state);
char const *screens_service_word(int service);
char const *screens_tab_word(int tab);

/* ---------- text fit: a text cut to the width of a column in a font, in the menu's units (the screen is 640 x 480; the capitals of ui\large_ui are 13 high, ui\small_ui's 10) */

enum
{
	SCREENS_FONT_LARGE,
	SCREENS_FONT_SMALL,
	SCREENS_FONTS,
};

/* the width of a text in a font (units of the 640 x 480 screen): the widest face's advances (Overpass), kerning ignored, so that a cut text never runs past its column */
float screens_text_width(int font, char const *text);
/* text cut to fit width (with "..." if it had to be cut; the characters are ASCII, so it is a plain cut), into a buffer of size; returns the length */
int screens_fit(int font, float width, char const *text, char *destination, int size);
/* a sentence broken at its spaces into at most max_lines lines of no more than width each, joined by '\r' (the engine's line break), into a buffer of size. The result is whole
(never cut mid-word) when the number of lines is returned; when a word is wider than a line or there are more lines than max_lines the last line is cut with "..." and the number
is returned negative. The texts the screens write are short enough never to be cut: a test (test_screens.py "wrap-messages") fails for one that is. */
int screens_wrap(int font, float width, int max_lines, char const *text, char *destination, int size);
/* the same with any measure (the real font at run time: the toasts' band): measure(context, text, length) is the width of the first length characters of text */
int screens_wrap_measured(float (*measure)(void *context, char const *text, int length), void *context, float width, int max_lines, char const *text, char *destination, int size);
/* the width of the boxes the sentences go in (units of the 640 x 480 screen): a detail line or an answer under the rows (512 wide, less the 5 units its text starts in on the console
and a margin: the metrics are about the widths the console draws, not above them), the engine's message box (its text area), and how many lines of the large font each has */
enum
{
	SCREENS_WRAP_LINES = 6,
	SCREENS_WRAP_SOURCE = 200,
	SCREENS_LINE_BOX_WIDTH = 496,
	SCREENS_LINE_BOX_LINES = 2,
	SCREENS_MESSAGE_BOX_WIDTH = 244,
	SCREENS_MESSAGE_BOX_LINES = 4,
};

/* ---------- the cells of a row: what each column of a row says, and in which colour (the menus fill their text widgets from these, the previews draw them) */

struct screens_cell
{
	char text[SCREENS_LINE_SIZE];
	unsigned char color;
};

enum
{
	/* the Players page's columns */
	SCREENS_PLAYER_NAME,
	SCREENS_PLAYER_TAG,
	SCREENS_PLAYER_TEAM,
	SCREENS_PLAYER_PING,
	SCREENS_PLAYER_CELLS,
	/* a lobby row: the name, and one cell of tag and team */
	SCREENS_LOBBY_NAME = 0,
	SCREENS_LOBBY_TAG,
	SCREENS_LOBBY_CELLS,
	/* the Friends screen's columns */
	SCREENS_FRIEND_MARK = 0,
	SCREENS_FRIEND_NAME,
	SCREENS_FRIEND_STATUS,
	SCREENS_FRIEND_CELLS,
	/* the party's */
	SCREENS_MEMBER_MARK = 0,
	SCREENS_MEMBER_NAME,
	SCREENS_MEMBER_STATE,
	SCREENS_MEMBER_CELLS,
};

/* the column widths the cells are cut to (units): the XML (tools/port_settings.py) uses the same numbers, and a test reads both */
enum
{
	SCREENS_PLAYER_NAME_WIDTH = 190,
	SCREENS_PLAYER_TAG_WIDTH = 150,
	SCREENS_PLAYER_TEAM_WIDTH = 56,
	SCREENS_PLAYER_PING_WIDTH = 58,
	SCREENS_LOBBY_NAME_WIDTH = 190,
	SCREENS_LOBBY_TAG_WIDTH = 162,
	SCREENS_FRIEND_NAME_WIDTH = 190,
	SCREENS_FRIEND_STATUS_WIDTH = 282,
	SCREENS_MEMBER_STATE_WIDTH = 282,
};

void screens_player_cells(struct screens_roster_row const *row, struct screens_roster_summary const *summary, struct screens_cell *cells);
void screens_lobby_cells(struct screens_roster_row const *row, int teams, struct screens_cell *cells);
void screens_friend_cells(int tab, struct screens_friend_row const *row, struct screens_cell *cells);
void screens_member_cells(struct screens_party_member const *member, struct screens_cell *cells);
/* the line under the list for the row focused (the details a right panel would have had), at most SCREENS_LINE_SIZE */
void screens_player_detail(struct screens_roster_row const *row, struct screens_roster_summary const *summary, char *text, int size);
void screens_friend_detail(int tab, struct screens_friend_row const *row, struct screens_friends_summary const *summary, char *text, int size);
/* the head lines: "12 OF 16  BOTS 4"; a tab's label ("FRIENDS 4", "PARTY 3/8"), with the tab chosen in brackets ("[FRIENDS 4]"); the strip of the four, a party's one-line state */
void screens_player_head(struct screens_roster_summary const *summary, char *text, int size);
void screens_tab_label(int tab, int chosen, struct screens_friends_summary const *summary, struct screens_party_summary const *party, char *text, int size);
void screens_tab_strip(int tab, struct screens_friends_summary const *summary, struct screens_party_summary const *party, char *text, int size);
void screens_party_head(struct screens_party_summary const *party, char *text, int size);
/* the sort of the friends: in a joinable game, in a lobby, online, away, offline; ties by name (a stable sort in place); returns count */
int screens_friends_sort(struct screens_friend_row *rows, int count);

/* ---------- navigation: which control goes where, as a pure model the XML screens and the tests share (the engine's own focus does the real moving; this is the order the XML builds) */

enum
{
	SCREENS_KEY_UP,
	SCREENS_KEY_DOWN,
	SCREENS_KEY_LEFT,
	SCREENS_KEY_RIGHT,
	SCREENS_KEY_A,
	SCREENS_KEY_B,
	SCREENS_KEY_X,
	SCREENS_KEY_Y,
	SCREENS_KEY_L,
	SCREENS_KEY_R,
	SCREENS_KEY_START,
	SCREENS_KEYS
};

/* where the focus is on a screen: a place in its order (tabs, rows, buttons) */
enum
{
	SCREENS_AREA_TABS,
	SCREENS_AREA_ROWS,
	SCREENS_AREA_BUTTONS,
};

struct screens_nav
{
	/* 0 = Players page, 1 = Friends screen */
	int screen;
	int area;
	/* in the tab strip, the focused tab; in the rows, the row (0 up) with the list scrolled by first; in the buttons, the button */
	int tab;
	int row;
	int first;
	int button;
	int rows_visible;
	int row_count;
	int tab_count;
	int button_count;
	/* set by a key: what the screen does (a SCREENS_ACTION_*, or SCREENS_NAV_*) */
	int result;
};

enum
{
	SCREENS_NAV_NOTHING,
	/* B: back to the screen before */
	SCREENS_NAV_BACK,
	/* A on a row: the row's actions open */
	SCREENS_NAV_OPEN_ACTIONS,
	/* a tab was chosen (nav->tab) */
	SCREENS_NAV_TAB,
	/* a button was pressed (nav->button) */
	SCREENS_NAV_BUTTON,
};

void screens_nav_begin(struct screens_nav *nav, int screen, int rows_visible, int tab_count, int button_count);
void screens_nav_rows(struct screens_nav *nav, int row_count);
/* a key (a D-pad direction, A, B, a shoulder): the focus moves as the XML's lists do (Up from the first row goes to the tabs where there are some, else to the last button; Down
from the last row goes to the buttons; the buttons' left and right wrap; the rows scroll one at the ends); nav->result says what else */
void screens_nav_key(struct screens_nav *nav, int key);
/* a tap at a place in a screen's own units: the row, the tab, the button under it (nothing: result SCREENS_NAV_NOTHING) */
void screens_nav_tap(struct screens_nav *nav, float x, float y);

/* the screens' places, in units of the 640 x 480 menu screen (tools/port_settings.py's numbers; a test reads both): the rows are the settings screens' own grid, 512 wide at x 54 */
enum
{
	SCREENS_ROWS_LEFT = 54,
	SCREENS_ROWS_TOP = 76,
	SCREENS_ROW_PITCH = 28,
	SCREENS_ROW_WIDTH = 512,
	SCREENS_ROW_HEIGHT = 28,
	SCREENS_TAB_TOP = 44,
	SCREENS_TAB_HEIGHT = 26,
	SCREENS_TAB_LEFT = 54,
	SCREENS_TAB_PITCH = 128,
	SCREENS_TAB_WIDTH = 124,
	SCREENS_BUTTONS_TOP = 414,
	SCREENS_BUTTON_HEIGHT = 24,
	SCREENS_PLAYER_ROWS = 10,
	SCREENS_FRIEND_ROWS = 9,
};

/* the x and the width of the button n of count on the buttons' line: up to four 128 wide, the right-most at 510, going left by 130 (the Xbox's bar: DEFAULTS at the left, OK 380, CANCEL
510); five 126 wide (the pill's picture is 128: two units of its end are cut) at 4, 131, 258, 385 and 512 */
int screens_button_left(int index, int count);
int screens_button_width(int count);

/* ---------- toasts: the queue and its timing (a pure model; the drawing is screens_toast.c's) */

enum
{
	/* how long a toast stays, how long it fades in and out, the gap between two, and how long an invitation waits for its answer (milliseconds) */
	SCREENS_TOAST_MILLISECONDS = 4000,
	SCREENS_TOAST_FADE_MILLISECONDS = 250,
	SCREENS_TOAST_GAP_MILLISECONDS = 1000,
	SCREENS_INVITE_MILLISECONDS = 30000,
	/* a friend coming online is shown after this long stable, and not more than one a minute for the same row (the provider debounces; this is the second guard) */
	SCREENS_TOAST_ONLINE_HOLD_MILLISECONDS = 10000,
	SCREENS_TOAST_SAME_MILLISECONDS = 60000,
};

struct screens_toast_state
{
	struct screens_toast queue[SCREENS_TOASTS_MAXIMUM];
	int count;
	/* the one shown (shown nonzero) and when it began */
	struct screens_toast current;
	int shown;
	unsigned long began;
	/* when the last one ended (the gap) */
	unsigned long ended;
	/* a confirm: an invitation answered "join" from a game waits for a second press (the leave of a match by one stray press) */
	int confirming;
	unsigned long confirm_began;
	/* the toasts dropped for want of room or as the same again, and the last texts shown (the second guard against a flap) */
	int dropped;
	char recent_text[4][SCREENS_LINE_SIZE];
	unsigned long recent_at[4];
	int recent_next;
};

void screens_toast_reset(struct screens_toast_state *state);
/* a toast arrives (from the provider): queued; an invitation jumps the queue; a full queue drops the oldest notice (not an invitation); the same text within a minute is dropped */
void screens_toast_push(struct screens_toast_state *state, struct screens_toast const *toast, unsigned long now);
/* the clock: the shown toast expires (an invitation after its wait), the next begins after the gap; returns nonzero if one is shown */
int screens_toast_tick(struct screens_toast_state *state, unsigned long now);
/* how opaque the shown toast is, 0 to 255 (fading in and out) */
int screens_toast_alpha(struct screens_toast_state const *state, unsigned long now);
/* the D-pad on an invitation: right joins (from a game: asks again, with the words LEAVE THIS GAME), left later. Returns 1 when an answer was given (the provider's), 0 otherwise. */
int screens_toast_answer(struct screens_toast_state *state, int accept, int in_game, unsigned long now, long *invite_id);

/* ---------- the model: what a screen keeps between frames: the rows it built, rebuilt only when the provider's generation moved */

struct screens_model
{
	unsigned long generation;
	int valid;
	struct screens_roster_row roster[SCREENS_ROSTER_MAXIMUM];
	int roster_count;
	struct screens_roster_summary roster_summary;
	struct screens_friend_row friends[SCREENS_TABS][SCREENS_FRIENDS_MAXIMUM];
	int friends_count[SCREENS_TABS];
	struct screens_friends_summary friends_summary;
	struct screens_party_member party[SCREENS_PARTY_MAXIMUM];
	int party_count;
	struct screens_party_summary party_summary;
};

/* the model brought up to date with the provider (nonzero: it changed); rebuilds only when the generation moved (or force) */
int screens_model_update(struct screens_model *model, int force);

#endif
