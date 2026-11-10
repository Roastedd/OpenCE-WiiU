/*
SOCIAL.C

social.h's calls, on the one friends layer of the process (social_p2p.c makes it); the words for the results.
*/

#include "social_front.h"

#include <string.h>

/* set once by social_p2p.c before the p2p thread starts, and never changed */
struct social_front *social_global_front;

const char *social_add_result_text(int result)
{
	switch (result)
	{
	case SOCIAL_ADD_RESULT_SENT:
		return "Request sent";
	case SOCIAL_ADD_RESULT_BAD_TEXT:
		return "That is not a friend code";
	case SOCIAL_ADD_RESULT_BAD_CHECK:
		return "The code has a mistake in it";
	case SOCIAL_ADD_RESULT_OWN_CODE:
		return "That is your own code";
	case SOCIAL_ADD_RESULT_ALREADY_FRIENDS:
		return "You are friends already";
	case SOCIAL_ADD_RESULT_LIST_FULL:
		return "Your friends list is full";
	case SOCIAL_ADD_RESULT_TOO_MANY_WAITING:
		return "Too many requests are waiting for an answer";
	case SOCIAL_ADD_RESULT_TOO_FAST:
		return "Wait a minute before sending more requests";
	case SOCIAL_ADD_RESULT_OFF:
		return "Friends are turned off";
	case SOCIAL_ADD_RESULT_BUSY:
		return "Try again in a moment";
	}
	return "Unknown result";
}

const char *social_invite_result_text(int result)
{
	switch (result)
	{
	case SOCIAL_INVITE_RESULT_SENT:
		return "Invite sent";
	case SOCIAL_INVITE_RESULT_NO_SUCH_FRIEND:
		return "That friend is not on your list";
	case SOCIAL_INVITE_RESULT_OFFLINE:
		return "Your friend is offline";
	case SOCIAL_INVITE_RESULT_NO_GAME:
		return "You are not hosting a game a friend could join";
	case SOCIAL_INVITE_RESULT_TOO_FAST:
		return "Wait a few seconds before inviting that friend again";
	case SOCIAL_INVITE_RESULT_BUSY:
		return "Try again in a moment";
	case SOCIAL_INVITE_RESULT_OFF:
		return "Friends are turned off";
	}
	return "Unknown result";
}

const char *social_party_result_text(int result)
{
	switch (result)
	{
	case SOCIAL_PARTY_RESULT_OK:
		return "Done";
	case SOCIAL_PARTY_RESULT_NOT_IN_PARTY:
		return "You are not in a party";
	case SOCIAL_PARTY_RESULT_ALREADY_IN_PARTY:
		return "You are in a party already";
	case SOCIAL_PARTY_RESULT_NOT_LEADER:
		return "Only the party's leader can do that";
	case SOCIAL_PARTY_RESULT_NO_SUCH_FRIEND:
		return "That friend is not on your list";
	case SOCIAL_PARTY_RESULT_FRIEND_OFFLINE:
		return "Your friend is offline, or has no party open";
	case SOCIAL_PARTY_RESULT_FULL:
		return "The party is full";
	case SOCIAL_PARTY_RESULT_TOO_FAST:
		return "Wait a moment";
	case SOCIAL_PARTY_RESULT_OFF:
		return "Friends are turned off";
	case SOCIAL_PARTY_RESULT_NO_SUCH_MEMBER:
		return "That is not a member of the party";
	case SOCIAL_PARTY_RESULT_BAD_TEXT:
		return "There is nothing to send";
	}
	return "Unknown result";
}

const char *social_follow_text(int follow_result)
{
	switch (follow_result)
	{
	case 1:
		return "joining";
	case 2:
		return "joined";
	case 3:
		return "game is full";
	case 4:
		return "game is closed";
	case 5:
		return "other version";
	case 6:
		return "needs a map";
	case 7:
		return "cannot connect";
	case 8:
		return "no answer";
	case 9:
		return "in another game";
	case 10:
		return "not following";
	case 11:
		return "banned";
	case 12:
		return "game was replaced";
	}
	return "";
}

/* what the game last said it is doing: one int, written by any thread and read by the engine's */
static volatile int social_activity;

void social_set_activity(int activity)
{
	social_activity = activity;
}

int social_activity_get(void)
{
	return social_activity;
}

int social_is_enabled(void)
{
	return social_global_front && social_front_enabled(social_global_front);
}

unsigned int social_snapshot_get(struct social_snapshot *snapshot)
{
	if (!social_global_front)
	{
		memset(snapshot, 0, sizeof(*snapshot));
		return 0;
	}
	return social_front_snapshot(social_global_front, snapshot);
}

void social_my_code(char *code, int size)
{
	if (size <= 0)
		return;
	code[0] = 0;
	if (social_global_front)
		social_front_code(social_global_front, code, size);
}

int social_add_code(const char *text)
{
	return social_global_front ? social_front_add_code(social_global_front, text) : SOCIAL_ADD_RESULT_OFF;
}

void social_accept(unsigned int id) { if (social_global_front) social_front_accept(social_global_front, id); }
void social_ignore(unsigned int id) { if (social_global_front) social_front_ignore(social_global_front, id); }
void social_cancel(unsigned int id) { if (social_global_front) social_front_cancel(social_global_front, id); }
void social_remove(unsigned int id) { if (social_global_front) social_front_remove(social_global_front, id); }
void social_block(unsigned int id) { if (social_global_front) social_front_block(social_global_front, id); }
void social_unblock(int index) { if (social_global_front) social_front_unblock(social_global_front, index); }
void social_set_nickname(unsigned int id, const char *nickname) { if (social_global_front) social_front_set_nickname(social_global_front, id, nickname); }
void social_set_name(const char *name) { if (social_global_front) social_front_set_name(social_global_front, name); }

void social_set_share(int show_status, int details, int accept_requests)
{
	if (social_global_front)
		social_front_set_share(social_global_front, show_status, details, accept_requests);
}

void social_invite_friend(unsigned int friend_id) { if (social_global_front) social_front_invite(social_global_front, friend_id); }
void social_invite_dismiss(unsigned int invite_id) { if (social_global_front) social_front_invite_dismiss(social_global_front, invite_id); }

void social_party_make(void) { if (social_global_front) social_front_party(social_global_front, 1, 0, 0, NULL); }
void social_party_invite(unsigned int friend_id) { if (social_global_front) social_front_party(social_global_front, 2, friend_id, 0, NULL); }
void social_party_join_friend(unsigned int friend_id) { if (social_global_front) social_front_party(social_global_front, 4, friend_id, 0, NULL); }
void social_party_leave(void) { if (social_global_front) social_front_party(social_global_front, 5, 0, 0, NULL); }
void social_party_kick(unsigned int member_id) { if (social_global_front) social_front_party(social_global_front, 6, member_id, 0, NULL); }
void social_party_transfer(unsigned int member_id) { if (social_global_front) social_front_party(social_global_front, 7, member_id, 0, NULL); }
void social_party_set_open(int open) { if (social_global_front) social_front_party(social_global_front, 8, 0, open, NULL); }
void social_party_set_auto_follow(int on) { if (social_global_front) social_front_party(social_global_front, 9, 0, on, NULL); }
void social_party_disband(void) { if (social_global_front) social_front_party(social_global_front, 11, 0, 0, NULL); }
void social_party_chat(const char *text) { if (social_global_front) social_front_party(social_global_front, 10, 0, 0, text); }

void social_party_join_invite(unsigned int invite_id)
{
	static struct social_snapshot snapshot;
	int index;

	if (!social_global_front)
		return;
	social_front_snapshot(social_global_front, &snapshot);
	for (index = 0; index < snapshot.invite_count; index++)
	{
		if (snapshot.invites[index].id == invite_id && snapshot.invites[index].kind == 2)
		{
			social_front_party_token(social_global_front, snapshot.invites[index].party_token);
			social_front_invite_dismiss(social_global_front, invite_id);
			return;
		}
	}
}

void social_set_show_status(int show) { if (social_global_front) social_front_set_share_part(social_global_front, 0, show); }
void social_set_details(int details) { if (social_global_front) social_front_set_share_part(social_global_front, 1, details); }
void social_set_accept_requests(int accept) { if (social_global_front) social_front_set_share_part(social_global_front, 2, accept); }

int social_event_next(struct social_api_event *event)
{
	return social_global_front ? social_front_event(social_global_front, event) : 0;
}
