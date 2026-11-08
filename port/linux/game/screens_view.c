/*
SCREENS_VIEW.C

The GamePad's social view made from the social model (halo_social.h): the header, the tabs, the rows of the friends or the party or the bots' two lines, the quick chat's page and a toast.
No game types (the chat's lines are the caller's to put in), so that the harness and the preview tool build it with the real code.
*/

#include "screens.h"
#include "screens_view.h"

#include <stdio.h>
#include <string.h>

static void copy(char *destination, int size, char const *source)
{
	snprintf(destination, (size_t)size, "%s", source ? source : "");
}

/* whether the friend's game can be joined from here */
static int joinable(struct screens_friend_row const *row)
{
	return (row->presence == SCREENS_PRESENCE_PLAYING || row->presence == SCREENS_PRESENCE_LOBBY) && (row->join == SCREENS_JOIN_OPEN || row->join == SCREENS_JOIN_DOWNLOAD);
}

/* the friends tab's rows: those online (the model's order puts the joinable games first), five at most */
static void rows_friends(struct halo_social_view *view, struct screens_view_input const *input, long *row_keys)
{
	struct screens_model const *model = input->model;
	int index;

	for (index = 0; index < model->friends_count[SCREENS_TAB_FRIENDS] && view->row_count < HALO_SOCIAL_ROWS; index++)
	{
		struct screens_friend_row shown = model->friends[SCREENS_TAB_FRIENDS][index];
		struct screens_friend_row const *friend_row = &shown;
		struct screens_cell cells[SCREENS_FRIEND_CELLS];
		struct halo_social_row *row = &view->rows[view->row_count];

		if (friend_row->presence == SCREENS_PRESENCE_OFFLINE)
			continue;
		/* (the status without the map's name: the view's rows are narrow, and the count and the reason are what matters) */
		shown.map[0] = 0;
		screens_friend_cells(SCREENS_TAB_FRIENDS, friend_row, cells);
		copy(row->mark, (int)sizeof(row->mark), cells[SCREENS_FRIEND_MARK].text);
		copy(row->name, (int)sizeof(row->name), cells[SCREENS_FRIEND_NAME].text);
		copy(row->status, (int)sizeof(row->status), cells[SCREENS_FRIEND_STATUS].text);
		row->name_rgb = screens_color_rgb(cells[SCREENS_FRIEND_MARK].color);
		row->status_rgb = screens_color_rgb(cells[SCREENS_FRIEND_STATUS].color);
		row->action = (unsigned char)(input->can_friend_action ? (joinable(friend_row) ? HALO_SOCIAL_ACTION_JOIN : HALO_SOCIAL_ACTION_INVITE) : HALO_SOCIAL_ACTION_NONE);
		row_keys[view->row_count] = friend_row->key;
		view->row_count++;
	}
	if (!view->row_count)
		screens_empty_text(1, SCREENS_TAB_FRIENDS, &model->friends_summary, input->has_friends, view->empty, (int)sizeof(view->empty));
}

static void rows_party(struct halo_social_view *view, struct screens_view_input const *input)
{
	struct screens_model const *model = input->model;
	int index;

	for (index = 0; index < model->party_count && view->row_count < HALO_SOCIAL_ROWS; index++)
	{
		struct screens_cell cells[SCREENS_MEMBER_CELLS];
		struct halo_social_row *row = &view->rows[view->row_count++];

		screens_member_cells(&model->party[index], cells);
		copy(row->mark, (int)sizeof(row->mark), cells[SCREENS_MEMBER_MARK].text);
		copy(row->name, (int)sizeof(row->name), cells[SCREENS_MEMBER_NAME].text);
		copy(row->status, (int)sizeof(row->status), cells[SCREENS_MEMBER_STATE].text);
		row->name_rgb = screens_color_rgb(SCREENS_COLOR_AZURE);
		row->status_rgb = screens_color_rgb(cells[SCREENS_MEMBER_STATE].color);
		row->action = HALO_SOCIAL_ACTION_NONE;
	}
	if (!view->row_count)
		screens_empty_text(1, SCREENS_TAB_PARTY, &model->friends_summary, input->has_friends, view->empty, (int)sizeof(view->empty));
}

void screens_view_make(struct halo_social_view *view, struct screens_view_input const *input, long *row_keys)
{
	struct screens_model const *model = input->model;
	struct screens_roster_summary const *summary = &model->roster_summary;
	char line[SCREENS_LINE_SIZE];
	char head[SCREENS_LINE_SIZE];
	int index;

	memset(view, 0, sizeof(*view));
	view->active = 1;
	view->tab = input->tab;
	screens_player_head(summary, head, (int)sizeof(head));
	snprintf(line, sizeof(line), "%s  %s  %s", summary->map, summary->gametype, head);
	copy(view->head, (int)sizeof(view->head), line);
	if (model->party_summary.present)
		snprintf(view->party, sizeof(view->party), "PARTY %d/%d", model->party_summary.size, model->party_summary.maximum);
	snprintf(view->tabs[HALO_SOCIAL_TAB_FRIENDS], sizeof(view->tabs[0]), "FRIENDS %d", model->friends_summary.online);
	snprintf(view->tabs[HALO_SOCIAL_TAB_PARTY], sizeof(view->tabs[0]), "PARTY %d", model->party_count);
	snprintf(view->tabs[HALO_SOCIAL_TAB_BOTS], sizeof(view->tabs[0]), "BOTS %d", summary->bots);
	if (input->tab == HALO_SOCIAL_TAB_FRIENDS)
		rows_friends(view, input, row_keys);
	else if (input->tab == HALO_SOCIAL_TAB_PARTY)
		rows_party(view, input);
	else
	{
		snprintf(view->bots[0], sizeof(view->bots[0]), "BOTS: %d", summary->bots);
		snprintf(view->bots[1], sizeof(view->bots[1]), "SKILL: %s", screens_skill_word(summary->skill));
		view->bots_host = summary->host && input->can_bots;
	}
	for (index = 0; index < HALO_SOCIAL_TILES; index++)
		copy(view->tiles[index], (int)sizeof(view->tiles[index]), input->tiles[index]);
	copy(view->page, (int)sizeof(view->page), input->page_name);
	if (input->toasts && input->toasts->shown)
	{
		struct screens_toast_state const *toasts = input->toasts;

		view->toast_alpha = screens_toast_alpha(toasts, input->now);
		view->toast_invite = toasts->current.kind == SCREENS_TOAST_INVITE;
		view->toast_kind = toasts->current.kind;
		copy(view->toast, (int)sizeof(view->toast), toasts->current.text);
		copy(view->toast_detail, (int)sizeof(view->toast_detail), toasts->confirming ? "LEAVE THIS GAME? TAP JOIN AGAIN." : toasts->current.detail);
	}
}
