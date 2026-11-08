/*
SOCIAL.H

Friends, join and invite, and parties for the game's screens and menus: what to call, and what comes back. Everything here is safe to call from the game's thread
at any time: a call that changes something is queued for the thread that does the work (the p2p thread), a call that reads copies a snapshot made there, and none
waits for the network or the card. With social.enabled off (the default until the console tests are done, and the card file halo/social.txt as the switch) every call
does nothing and the snapshot says it is off.

A friend, a request, a party member is named by its id (an unsigned number, never 0) as the snapshot gives it. Text in a snapshot is printable ASCII, never "|".
Results are plain words: social_add_result_text, social_join_reason_text.
*/

#ifndef __HALO_LINUX_SOCIAL_H
#define __HALO_LINUX_SOCIAL_H

#include "social_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	SOCIAL_API_MAX_FRIENDS = 50,
	SOCIAL_API_MAX_REQUESTS = 16,
	SOCIAL_API_MAX_SENT = 16,
	SOCIAL_API_MAX_INVITES = 12,
};

/* where a person is (presence's activity) */
enum
{
	SOCIAL_ACTIVITY_OFFLINE,
	SOCIAL_ACTIVITY_MENUS,
	SOCIAL_ACTIVITY_LOBBY,
	SOCIAL_ACTIVITY_GAME,
	/* a campaign or solo game: never joinable */
	SOCIAL_ACTIVITY_SOLO,
	/* online, and not saying more (social.share_details none) */
	SOCIAL_ACTIVITY_BUSY,
};

struct social_friend_info
{
	unsigned int id;
	/* what the friend calls itself, and the player's own name for it (empty if none) */
	char name[SOCIAL_NAME_SIZE + 1];
	char nickname[SOCIAL_NAME_SIZE + 1];
	/* "XXXX-XXXX-XXXX-C" */
	char code[SOCIAL_CODE_TEXT_SIZE];
	int online;
	/* seconds since it was heard (0: never this run) */
	unsigned int seconds_since_heard;
	int activity;
	/* SOCIAL_PLATFORM_*: 1 Wii U, 2 Windows, 3 Linux, 4 macOS, 5 Android, 6 web */
	int platform;
	int network_version;
	int hosting, joinable, private_game, in_party, party_open;
	char map[SOCIAL_MAP_SIZE + 1];
	char gametype[SOCIAL_GAMETYPE_SIZE + 1];
	int players, maximum_players, engine_type;
	/* the game it could be joined at, when joinable */
	struct social_join_target join;
	/* the exchange of keys with it is done (until then it cannot see this machine's presence) */
	int keys_exchanged;
};

/* an invite received: to a game (kind 1: target says where, what map, how many) or to a party (kind 2) */
struct social_invite_info
{
	unsigned int id;
	unsigned int friend_id;
	char name[SOCIAL_NAME_SIZE + 1];
	int kind;
	struct social_join_target target;
	unsigned char party_size;
	unsigned int seconds_ago;
};

struct social_request_info
{
	unsigned int id;
	char name[SOCIAL_NAME_SIZE + 1];
	char code[SOCIAL_CODE_TEXT_SIZE];
	unsigned int seconds_ago;
};

struct social_sent_info
{
	unsigned int id;
	char code[SOCIAL_CODE_TEXT_SIZE];
	unsigned int seconds_ago;
};

/* Everything a screen needs, as of the last pass of the thread that does the work. */
struct social_snapshot
{
	/* changes whenever anything in it does (or each second, for the ages) */
	unsigned int version;
	int enabled;
	/* brokers connected now */
	int brokers;
	int share_status, share_details, accept_requests;
	char code[SOCIAL_CODE_TEXT_SIZE];
	char name[SOCIAL_NAME_SIZE + 1];
	int friend_count;
	struct social_friend_info friends[SOCIAL_API_MAX_FRIENDS];
	int request_count;
	struct social_request_info requests[SOCIAL_API_MAX_REQUESTS];
	int sent_count;
	struct social_sent_info sent[SOCIAL_API_MAX_SENT];
	int blocked_count;
	/* invites received that have not lapsed (a minute) or been answered */
	int invite_count;
	struct social_invite_info invites[SOCIAL_API_MAX_INVITES];
	/* what became of the last social_invite_friend: a number that goes up with each, and its SOCIAL_INVITE_RESULT_* */
	unsigned int invite_serial;
	int invite_result;
	/* what became of the last social_add_code once the thread that does the work took it: a number that goes up with each, and its SOCIAL_ADD_RESULT_* */
	unsigned int add_serial;
	int add_result;
};

/* the results of adding a friend by a typed code (social_add_code) */
enum
{
	SOCIAL_ADD_RESULT_SENT,
	SOCIAL_ADD_RESULT_BAD_TEXT,
	SOCIAL_ADD_RESULT_BAD_CHECK,
	SOCIAL_ADD_RESULT_OWN_CODE,
	SOCIAL_ADD_RESULT_ALREADY_FRIENDS,
	SOCIAL_ADD_RESULT_LIST_FULL,
	SOCIAL_ADD_RESULT_TOO_MANY_WAITING,
	SOCIAL_ADD_RESULT_TOO_FAST,
	SOCIAL_ADD_RESULT_OFF,
	SOCIAL_ADD_RESULT_BUSY,
};

/* The words for a result, a short sentence a screen can show as it is. Never NULL. */
const char *social_add_result_text(int result);

/* ---------- reading */

int social_is_enabled(void);
/* a copy of the snapshot: the version (0: nothing was made yet) */
unsigned int social_snapshot_get(struct social_snapshot *snapshot);
/* this machine's code, as friends type it ("" before the identity exists) */
void social_my_code(char *code, int size);

/* ---------- friends */

/* Asks the owner of a typed code to be friends: checked here (a code with a wrong letter is refused with the reason), then sent, and sent again every five minutes
while it waits. Returns SOCIAL_ADD_RESULT_*. */
int social_add_code(const char *text);
/* a request received (snapshot.requests[].id): accepted, turned away for good (it is not asked about again), or a request this machine sent forgotten */
void social_accept(unsigned int id);
void social_ignore(unsigned int id);
void social_cancel(unsigned int id);
/* The friend told it is no longer one, and deleted; this machine's presence is sealed afresh so that it hears nothing more. Block does the same and refuses its key
for good. */
void social_remove(unsigned int id);
void social_block(unsigned int id);
/* one of the blocked, by its place (0 to snapshot.blocked_count - 1), may ask again */
void social_unblock(int index);
void social_set_nickname(unsigned int id, const char *nickname);

/* ---------- invites */

/* the results of inviting a friend (social_invite_friend) */
enum
{
	SOCIAL_INVITE_RESULT_SENT,
	SOCIAL_INVITE_RESULT_NO_SUCH_FRIEND,
	SOCIAL_INVITE_RESULT_OFFLINE,
	SOCIAL_INVITE_RESULT_NO_GAME,
	SOCIAL_INVITE_RESULT_TOO_FAST,
	SOCIAL_INVITE_RESULT_BUSY,
	SOCIAL_INVITE_RESULT_OFF,
};

const char *social_invite_result_text(int result);

/* Asks a friend to join the game this machine hosts for the internet (its id from the snapshot); the result is in the snapshot (invite_result) once the thread that
does the work has taken it. An invite is a message to that friend only, and lapses at its end after a minute. */
void social_invite_friend(unsigned int friend_id);
/* an invite received (snapshot.invites[].id) is let go: after it was joined (social_join_invite does this), or turned down */
void social_invite_dismiss(unsigned int invite_id);

/* ---------- what is shown of this machine */

/* the name shown to friends (the first player's profile name); social_set_share: whether friends see this machine at all (off: appears offline, but still hears
them), how much (SOCIAL_DETAILS_NONE 0, BASIC 1, FULL 2: basic is where it is, and the game too when it can be joined), and whether requests are taken */
void social_set_name(const char *name);
void social_set_share(int show_status, int details, int accept_requests);

/* what the game is doing, for friends to see: SOCIAL_ACTIVITY_MENUS, _LOBBY, _GAME (a multiplayer game), _SOLO (a campaign or a game alone: never joinable), _BUSY.
internet play adds where the game is hosted and how to join it. Cheap: call it when it changes. */
void social_set_activity(int activity);

/* ---------- events (toasts) */

enum
{
	SOCIAL_API_EVENT_NONE,
	/* id: the request's, name and code */
	SOCIAL_API_EVENT_REQUEST,
	/* a friendship began (the friend's id) */
	SOCIAL_API_EVENT_FRIEND,
	/* a friend removed this machine (the id is gone) */
	SOCIAL_API_EVENT_REMOVED,
	SOCIAL_API_EVENT_ONLINE,
	SOCIAL_API_EVENT_OFFLINE,
	/* an invite to a game: id is the invite's (the snapshot has it), name who from; to a party */
	SOCIAL_API_EVENT_INVITE,
	SOCIAL_API_EVENT_PARTY_INVITE,
};

struct social_api_event
{
	int type;
	unsigned int id;
	char name[SOCIAL_NAME_SIZE + 1];
	char code[SOCIAL_CODE_TEXT_SIZE];
};

/* the oldest event, removed; 0 if none */
int social_event_next(struct social_api_event *event);

#ifdef __cplusplus
}
#endif

#endif
