/*
SOCIAL_JOIN.H

Joining a friend's game, from the friends list or from an invite, and why it did not work in plain words. The game's thread runs it (social_join_update once a frame);
what it asks of the rest of the game and of internet play it asks through a backend (struct social_join_backend), so that the harness runs it against a stand-in.

The steps: the checks that can be made before trying (the friend still there, the version, the map, whether this machine is in a game already), then the join of the
host (p2p_join_invite and p2p_join_progress: asking the brokers, the host's answer, the tunnel), then the host's game appears in the System Link list and the menus join
it (the status says READY: that last step, an A press in the server browser's code, is the menus'). Each failure is one of SOCIAL_JOIN_REASON_*, with a sentence for
a screen (social_join_reason_text).
*/

#ifndef __HALO_LINUX_SOCIAL_JOIN_H
#define __HALO_LINUX_SOCIAL_JOIN_H

#include "social.h"

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	SOCIAL_JOIN_IDLE,
	/* asking the brokers, the host answering, the tunnel being made */
	SOCIAL_JOIN_WORKING,
	/* the host is reached and its game is in the System Link list: the menus join it now */
	SOCIAL_JOIN_READY,
	SOCIAL_JOIN_FAILED,
};

/* why a join did not work (social_join_status.reason) */
enum
{
	SOCIAL_JOIN_REASON_NONE,
	/* the friend is not online */
	SOCIAL_JOIN_REASON_OFFLINE,
	/* the friend is online but in no game that can be joined */
	SOCIAL_JOIN_REASON_NOT_JOINABLE,
	/* it was, and it has gone (the game ended, the friend left, or made a new invite) */
	SOCIAL_JOIN_REASON_LEFT,
	/* the friend plays another version of the game */
	SOCIAL_JOIN_REASON_VERSION,
	/* the map is not on this machine and cannot be downloaded */
	SOCIAL_JOIN_REASON_MAP_MISSING,
	SOCIAL_JOIN_REASON_FULL,
	/* the game is under way and takes no more, or is closed */
	SOCIAL_JOIN_REASON_CLOSED,
	SOCIAL_JOIN_REASON_BANNED,
	SOCIAL_JOIN_REASON_PASSWORD,
	/* the host never answered (it quit, or no broker is shared) */
	SOCIAL_JOIN_REASON_NO_ANSWER,
	/* it answered and the tunnel never reached it */
	SOCIAL_JOIN_REASON_NO_ROUTE,
	/* ... and this network's NAT is the strict kind that cannot be got through */
	SOCIAL_JOIN_REASON_STRICT_NAT,
	/* this machine is in a game or hosts one: it is not left for a join */
	SOCIAL_JOIN_REASON_BUSY,
	/* this machine is in that game already */
	SOCIAL_JOIN_REASON_TOGETHER,
	/* an invite that is not one */
	SOCIAL_JOIN_REASON_BAD_INVITE,
	/* the host refused it for another reason (its name, a link it needs) */
	SOCIAL_JOIN_REASON_REFUSED,
	SOCIAL_JOIN_REASON_TIMEOUT,
	SOCIAL_JOIN_REASON_CANCELLED,
	/* friends are turned off */
	SOCIAL_JOIN_REASON_OFF,
	NUMBER_OF_SOCIAL_JOIN_REASONS,
};

struct social_join_status
{
	int state;
	int reason;
	/* a sentence for a screen: why it failed, or what is going on */
	char text[96];
	/* the map is not on this machine but can be downloaded while joining */
	int map_download;
	struct social_join_target target;
	/* a number that goes up with each join begun */
	unsigned int serial;
};

/* The short word of a reason ("full", "wrong_map", "version", "left", ...), for the log, and its sentence for a screen (map is the map's name, shown where the
sentence has one; may be NULL). Never NULL. */
const char *social_join_reason_word(int reason);
const char *social_join_reason_text(int reason, const char *map);

/* What a join asks of the rest. Every call is made by the thread that runs the machine. */
struct social_join_backend
{
	void *user;
	unsigned int (*now)(void *user);
	/* the network version this machine plays */
	int (*version)(void *user);
	/* this machine hosts a game or is in one */
	int (*busy)(void *user);
	/* this machine is in the game of the host with this key hash */
	int (*in_game_of)(void *user, const unsigned char *host_hash);
	/* what a map is to this machine: 0 installed, 1 it can be downloaded while joining, 2 missing (P2P_LOBBY_MAP_*) */
	int (*map_state)(void *user, const char *map);
	/* the friend as it is now (a snapshot of it): whether it is online, and the game it could be joined at (1 if there is one) */
	int (*friend_now)(void *user, unsigned int friend_id, int *online, struct social_join_target *target);
	/* p2p_join_invite of the invite; 0 if it is none */
	int (*begin)(void *user, const unsigned char *host_hash, const unsigned char *token);
	/* p2p_join_progress: the stage (P2P_JOIN_*), and once it failed why as a word ("no_answer", "no_route", "full", "closed", "banned", "password", "version",
	"token", ...) */
	int (*progress)(void *user, int *stage, char *reason, int size);
	/* the host's game is in the System Link list (the menus' lobby_browser_joined_game test); NULL: as soon as the tunnel reaches the host */
	int (*game_ready)(void *user, const unsigned char *host_hash);
	/* gives up the join in progress */
	void (*cancel)(void *user);
	/* p2p_nat_strict */
	int (*strict_nat)(void *user);
};

struct social_join_machine
{
	struct social_join_backend backend;
	int state;
	int reason;
	struct social_join_target target;
	int map_download;
	unsigned int started;
	unsigned int checked;
	unsigned int serial;
	int retried;
};

void social_join_machine_init(struct social_join_machine *machine, const struct social_join_backend *backend);
/* a friend's game, as the friend's presence shows it now; returns the reason it could not start (SOCIAL_JOIN_REASON_NONE: started) */
int social_join_machine_friend(struct social_join_machine *machine, unsigned int friend_id);
/* a game by its invite's two halves and what is known of it (an invite received) */
int social_join_machine_target(struct social_join_machine *machine, const struct social_join_target *target);
void social_join_machine_update(struct social_join_machine *machine);
void social_join_machine_cancel(struct social_join_machine *machine);
/* the menus joined the game, or gave it up: back to idle */
void social_join_machine_done(struct social_join_machine *machine);
void social_join_machine_status(const struct social_join_machine *machine, struct social_join_status *status);

/* ---------- the game's own: the one machine of the process, social.h's calls */

/* The backend of the process (social_p2p.inc sets it before anything calls the rest). The game fills in what only it knows (what maps are installed, whether the
server browser's lobby holds the host's game) through social_join_set_game_hooks. */
void social_join_set_backend(const struct social_join_backend *backend);
void social_join_set_game_hooks(int (*map_state)(const char *map), int (*game_ready)(const unsigned char *host_hash), int (*busy)(void));
/* what the backend of the process asks of those hooks (0: installed, 1: ready, 0: not busy, while the game has set none) */
int social_join_hook_map_state(const char *map);
int social_join_hook_game_ready(const unsigned char *host_hash);
int social_join_hook_busy(void);

/* Joins the game a friend is at now (the friend's id, from the snapshot) or the one an invite names (the invite's id, from the snapshot). Returns the reason it did
not start, SOCIAL_JOIN_REASON_NONE if it did. Game thread. */
int social_join_friend(unsigned int friend_id);
int social_join_invite(unsigned int invite_id);
/* once a frame (cheap when idle) */
void social_join_update(void);
void social_join_cancel(void);
void social_join_done(void);
void social_join_status_get(struct social_join_status *status);

/* ---------- finishing a join in the menus, and leaving a game first

A join ends in the menus' hands (the host's game is in the Direct Link list: the A press that joins it), and an invitation answered in a game starts with leaving that game. The
flow drives both once a frame, through what only the game can do (struct social_flow_world); the harness runs it against a stand-in. Game thread. */

/* which screen is on top, as the flow needs to know */
enum
{
	/* none, or a game is on */
	SOCIAL_FLOW_SCREEN_NONE,
	/* a screen the Direct Link list can be opened over (the main menu, Multiplayer, Friends, Players) */
	SOCIAL_FLOW_SCREEN_SAFE,
	/* the Direct Link list itself: its code presses A for the game */
	SOCIAL_FLOW_SCREEN_LIST,
	/* any other screen (a setting being edited, a lobby, a dialog): nothing is opened over it */
	SOCIAL_FLOW_SCREEN_OTHER,
};

struct social_flow_world
{
	void *user;
	/* milliseconds, any origin */
	unsigned int (*now)(void *user);
	/* a game or a lobby is on this machine (what a join is not started over) */
	int (*busy)(void *user);
	/* the menus are up and nothing is loading: a screen can be opened over the one on top */
	int (*menus_up)(void *user);
	/* the screen on top: SOCIAL_FLOW_SCREEN_* */
	int (*screen)(void *user);
	/* leave the game (called every frame while leaving, until it returns 1: the game is on its way out) */
	int (*leave)(void *user);
	/* open the Direct Link list over the screen on top; 1 if it was opened */
	int (*open_list)(void *user);
	/* a line for the log (what the flow did, not a line a frame); NULL: none */
	void (*log)(void *user, const char *text);
};

void social_flow_set_world(const struct social_flow_world *world);
/* once a frame: leaving a game for an invitation, the join (and the party's following of its leader), the Direct Link list opened when the game is ready, the join given up when
nothing finishes it. Cheap when idle. */
void social_flow_update(void);
/* an invitation answered in a game: this machine leaves it, then joins the game the invitation names. Returns SOCIAL_JOIN_REASON_NONE if it began, else why not. The invitation is used. */
int social_flow_leave_and_join_invite(unsigned int invite_id);
/* a game's row in the Direct Link list may be pressed now (at most three presses for a join, three seconds apart): 1 once; after the third the join fails as refused */
int social_flow_press_allowed(void);
/* what the flow is waiting for, as a sentence for a screen ("" if nothing): leaving, or a screen that is in the way */
const char *social_flow_waiting_text(void);
/* 1 while a leave for an invitation is going on */
int social_flow_leaving(void);

/* Starts a join at a game's two halves and what is known of it (an invitation's target), as social_join_invite does; returns the reason it did not start. */
int social_join_start_target(const struct social_join_target *target);
/* map downloads are on (network.map_download): a missing map then says it cannot be fetched, else where to turn downloads on. NULL: not told */
void social_join_set_map_download_hook(int (*enabled)(void));

#ifdef __cplusplus
}
#endif

#endif
