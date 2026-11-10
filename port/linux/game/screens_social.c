/*
SCREENS_SOCIAL.C

The social screens' GamePad view, the game's half (halo_social.h; the Wii U's half is port/wiiu/src/wiiu_social_view.c): once a frame, in a game with input.wiiu_gamepad_view "social", the
view is made from the social model (the rows the menus show) and the chat's lines and the quick chat's page, and published; what the finger touched comes back as events (a tab, a row's
JOIN or INVITE, a quick chat line, the page's arrows, the bots' - and +, a toast's JOIN and LATER) and goes to the provider and the chat. With the setting off, or on a platform that
does not draw the view (halo_social_view_wanted), nothing is made.

The KEYBOARD button opens the on-screen keyboard (network_chat_keyboard_request) for the tab shown: the party's chat on PARTY, a friend's code on FRIENDS, the game's chat on BOTS; the line typed comes
back to screens_social_line.
*/

#include "cseries.h"
#include "game/game.h"
#include "interface/ui_widget.h"

#include "halo_social.h"
#include "network_chat.h"
#include "screens.h"
#include "screens_view.h"

#include <stdio.h>
#include <string.h>

long config_integer(const char *name);
int config_write(const char *name, const char *value);
void platform_log(char const *format, ...);
unsigned long system_milliseconds(void);
const char *config_string(const char *name);

/* network_chat.c's */
int network_chat_recent(struct halo_social_line *lines, int maximum);
int network_chat_quick_pages(void);
char const *network_chat_quick_page_name(int page);
char const *network_chat_quick_line(int page, int slot);
void network_chat_quick_send(int page, int slot);

static struct
{
	struct screens_model model;
	unsigned long updated;
	int tab;
	int page;
	/* the keys of the friends the rows show, for the touch (the model's rows are rebuilt as it changes) */
	long row_keys[HALO_SOCIAL_ROWS];
	int was_active;
	/* the answer to a typed line, for the next frame's toasts (the line is taken in the chat's update, which has no toast queue) */
	struct screens_toast line_toast;
	int line_toast_pending;
	int handler_set;
} social;

static void copy(char *destination, int size, char const *source)
{
	snprintf(destination, (size_t)size, "%s", source ? source : "");
}

/* the view: the model's rows, the chat's lines and the quick chat's page */
static void view_make(struct halo_social_view *view, struct screens_toast_state const *toasts, unsigned long now)
{
	struct screens_provider const *provider = screens_provider();
	struct screens_view_input input;
	int index;

	memset(&input, 0, sizeof(input));
	input.model = &social.model;
	input.tab = social.tab;
	for (index = 0; index < HALO_SOCIAL_TILES; index++)
		input.tiles[index] = network_chat_quick_line(social.page, index);
	input.page_name = network_chat_quick_page_name(social.page);
	input.toasts = toasts;
	input.now = now;
	input.has_friends = provider && provider->friends;
	input.can_friend_action = provider && provider->friend_action;
	input.can_bots = provider && provider->bot_add;
	screens_view_make(view, &input, social.row_keys);
	view->chat_count = network_chat_recent(view->chat, HALO_SOCIAL_CHAT_LINES);
}

static void skill_change(int delta)
{
	int skill = (int)social.model.roster_summary.skill + delta;
	char value[16];
	char const *word;

	if (skill < 0 || skill >= SCREENS_SKILLS)
		return;
	word = screens_skill_word(skill);
	for (delta = 0; word[delta] && delta < (int)sizeof(value) - 1; delta++)
		value[delta] = (char)(word[delta] >= 'A' && word[delta] <= 'Z' ? word[delta] + 32 : word[delta]);
	value[delta] = 0;
	config_write("bots.skill", value);
}

/* where a line typed on the keyboard goes, by the tab shown: the party's chat, a friend's code, else the game's chat */
static int keyboard_destination(int tab)
{
	return tab == HALO_SOCIAL_TAB_PARTY ? NETWORK_CHAT_TO_PARTY : tab == HALO_SOCIAL_TAB_FRIENDS ? NETWORK_CHAT_TO_FRIEND_CODE : NETWORK_CHAT_TO_GAME;
}

/* what the finger touched */
static void event_handle(struct halo_social_event const *event, struct screens_toast_state *toasts, unsigned long now)
{
	struct screens_provider const *provider = screens_provider();
	char answer[SCREENS_LINE_SIZE];

	answer[0] = 0;
	switch (event->kind)
	{
	case HALO_SOCIAL_EVENT_TAB:
		social.tab = event->index;
		platform_log("screens: social view, tab %d", social.tab);
		break;
	case HALO_SOCIAL_EVENT_ROW:
		if (event->index >= 0 && event->index < HALO_SOCIAL_ROWS && provider && provider->friend_action)
		{
			struct screens_friend_row const *rows = social.model.friends[SCREENS_TAB_FRIENDS];
			int index, done = 0;

			for (index = 0; index < social.model.friends_count[SCREENS_TAB_FRIENDS]; index++)
			{
				if (rows[index].key == social.row_keys[event->index])
				{
					int joinable = (rows[index].presence == SCREENS_PRESENCE_PLAYING || rows[index].presence == SCREENS_PRESENCE_LOBBY) &&
						(rows[index].join == SCREENS_JOIN_OPEN || rows[index].join == SCREENS_JOIN_DOWNLOAD);

					done = provider->friend_action(joinable ? SCREENS_ACTION_JOIN_GAME : SCREENS_ACTION_INVITE_GAME, &rows[index], answer, (int)sizeof(answer));
					platform_log("screens: social view, %s %s: %s %s", joinable ? "join" : "invite", rows[index].name, done ? "done" : "refused", answer);
				}
			}
			if (!done && answer[0])
			{
				struct screens_toast toast;

				memset(&toast, 0, sizeof(toast));
				toast.kind = SCREENS_TOAST_JOIN_FAILED;
				copy(toast.text, (int)sizeof(toast.text), answer);
				screens_toast_push(toasts, &toast, now);
			}
		}
		break;
	case HALO_SOCIAL_EVENT_TILE:
		platform_log("screens: social view, quick chat line %d of page %d", event->index, social.page);
		network_chat_quick_send(social.page, event->index);
		break;
	case HALO_SOCIAL_EVENT_PAGE_PREVIOUS:
	case HALO_SOCIAL_EVENT_PAGE_NEXT:
		social.page = (social.page + (event->kind == HALO_SOCIAL_EVENT_PAGE_NEXT ? 1 : network_chat_quick_pages() - 1)) % network_chat_quick_pages();
		break;
	case HALO_SOCIAL_EVENT_KEYBOARD:
		/* (the keyboard opens as D-pad Up opens it; the line typed goes where the tab shows: the party's chat, a friend's code, else the game's chat) */
		{
			int destination = keyboard_destination(social.tab);

			platform_log("screens: social view, KEYBOARD for %s", destination == NETWORK_CHAT_TO_PARTY ? "the party's chat" : destination == NETWORK_CHAT_TO_FRIEND_CODE ? "a friend's code" : "the game's chat");
			network_chat_keyboard_request(destination);
		}
		break;
	case HALO_SOCIAL_EVENT_BOTS_MINUS:
	case HALO_SOCIAL_EVENT_BOTS_PLUS:
		if (event->index == 0 && provider)
		{
			int plus = event->kind == HALO_SOCIAL_EVENT_BOTS_PLUS;
			int done = plus ? (provider->bot_add ? provider->bot_add(SCREENS_TEAM_NONE, -1, answer, (int)sizeof(answer)) : 0) :
				(provider->bot_drop ? provider->bot_drop(-1, answer, (int)sizeof(answer)) : 0);

			platform_log("screens: social view, %s a bot: %s %s", plus ? "add" : "drop", done ? "done" : "refused", answer);
		}
		else if (event->index == 1)
		{
			skill_change(event->kind == HALO_SOCIAL_EVENT_BOTS_PLUS ? 1 : -1);
		}
		break;
	case HALO_SOCIAL_EVENT_TOAST_JOIN:
	case HALO_SOCIAL_EVENT_TOAST_LATER:
		{
			long id = 0;
			int accept = event->kind == HALO_SOCIAL_EVENT_TOAST_JOIN;

			if (screens_toast_answer(toasts, accept, game_in_progress() ? 1 : 0, now, &id) && provider && provider->invite_answer)
				provider->invite_answer(id, accept);
		}
		break;
	default:
		break;
	}
}

/* a line typed on the keyboard for the friends layer: the party's chat, or a friend's code sent as a request; what the provider answers is shown as a toast */
static int screens_social_line(int destination, char const *text)
{
	struct screens_provider const *provider = screens_provider();
	char answer[SCREENS_LINE_SIZE];
	int done = 0;

	answer[0] = 0;
	if (provider && destination == NETWORK_CHAT_TO_PARTY && provider->party_chat)
		done = provider->party_chat(text, answer, (int)sizeof(answer));
	else if (provider && destination == NETWORK_CHAT_TO_FRIEND_CODE && provider->friend_add)
		done = provider->friend_add(text, answer, (int)sizeof(answer));
	else
		return 0;
	platform_log("screens: social view, a line for %s: %s %s", destination == NETWORK_CHAT_TO_PARTY ? "the party" : "a friend's code", done ? "done" : "refused", answer);
	if (answer[0])
	{
		struct screens_toast toast;

		memset(&toast, 0, sizeof(toast));
		toast.kind = done ? SCREENS_TOAST_NOTICE : SCREENS_TOAST_JOIN_FAILED;
		copy(toast.text, (int)sizeof(toast.text), answer);
		social.line_toast = toast;
		social.line_toast_pending = 1;
	}
	return 1;
}

/* once a frame (screens_toast.c's): the view published while a game is on and no menu is up, and the touches taken */
void screens_social_frame(struct screens_toast_state *toasts, unsigned long now)
{
	struct halo_social_view view;
	struct halo_social_event event;
	int active;

	if (!halo_social_view_wanted())
		return;
	active = game_in_progress() && !ui_widgets_active();
	if (!active)
	{
		if (social.was_active)
		{
			memset(&view, 0, sizeof(view));
			halo_social_view_publish(&view);
			social.was_active = 0;
		}
		while (halo_social_event_next(&event))
			;
		return;
	}
	social.was_active = 1;
	if (!social.handler_set)
	{
		social.handler_set = 1;
		network_chat_set_line_handler(screens_social_line);
	}
	if (social.line_toast_pending)
	{
		social.line_toast_pending = 0;
		screens_toast_push(toasts, &social.line_toast, now);
	}
	/* (the model four times a second, at most) */
	if (now - social.updated >= 250 || !social.model.valid)
	{
		social.updated = now;
		screens_model_update(&social.model, 0);
	}
	while (halo_social_event_next(&event))
		event_handle(&event, toasts, now);
	view_make(&view, toasts, now);
	halo_social_view_publish(&view);
}
