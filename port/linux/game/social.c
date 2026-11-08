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

int social_event_next(struct social_api_event *event)
{
	return social_global_front ? social_front_event(social_global_front, event) : 0;
}
