/* social_flow_check.c: finishing a join in the menus and leaving a game for an invitation (social_join.c's flow) on the fake brokers of social_fake.h, with stand-ins for what only the
game knows (a game on this machine, the menus up, the screen on top, the Direct Link list). Cases are named on the command line (test_social_flow.py). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition, ...) \
	do \
	{ \
		if (!(condition)) \
		{ \
			fprintf(stderr, "%s:%d: check failed: %s: ", __FILE__, __LINE__, #condition); \
			fprintf(stderr, __VA_ARGS__); \
			fputc('\n', stderr); \
			exit(1); \
		} \
	} while (0)

#include "social_codec.c"
#include "social_core.c"
#include "social_friends.c"
#include "social_invite.c"
#include "social_party.c"
#include "social_store.c"
#include "social_front.c"
#include "social.c"
#include "social_join.c"
#include "social_fake.h"

static struct fake_net net;

/* what the game would say */
static struct
{
	/* a game on this machine: busy until `leave_frames` calls of leave() have been made after the first */
	int busy;
	int leave_calls;
	int leave_frames;
	/* the menus up (after the game is gone); the screen on top */
	int menus_up;
	int screen;
	int opened;
	int open_refused;
	/* the screen on top is still the old one for a few frames after the list is asked for */
	int screen_stays;
	/* internet play */
	int begun;
	int stage;
	int map_state;
	int map_download;
	int game_ready;
	int in_game_of;
	unsigned int clock;
} world;

static unsigned int world_now(void *user)
{
	(void)user;
	return world.clock;
}

static int world_busy(void *user)
{
	(void)user;
	return world.busy;
}

static int world_menus_up(void *user)
{
	(void)user;
	return world.menus_up;
}

static int world_screen(void *user)
{
	(void)user;
	return world.screen;
}

static int world_leave(void *user)
{
	(void)user;
	world.leave_calls++;
	if (world.leave_frames >= 0 && world.leave_calls > world.leave_frames)
		world.busy = 0;
	return 1;
}

static char flow_log_text[4096];

static void world_log(void *user, const char *text)
{
	(void)user;
	strncat(flow_log_text, text, sizeof(flow_log_text) - strlen(flow_log_text) - 2);
	strcat(flow_log_text, "\n");
}

static int world_open_list(void *user)
{
	(void)user;
	if (world.open_refused)
		return 0;
	world.opened++;
	if (!world.screen_stays)
		world.screen = SOCIAL_FLOW_SCREEN_LIST;
	return 1;
}

static int test_hook_map_state(const char *map)
{
	(void)map;
	return world.map_state;
}

static int test_hook_game_ready(const unsigned char *hash)
{
	(void)hash;
	return world.game_ready;
}

static int test_hook_busy(void)
{
	return world.busy;
}

static int test_hook_map_download(void)
{
	return world.map_download;
}

static int backend_version(void *user)
{
	(void)user;
	return 41;
}

static int backend_in_game_of(void *user, const unsigned char *hash)
{
	(void)user;
	(void)hash;
	return world.in_game_of;
}

static int backend_map_state(void *user, const char *map)
{
	(void)user;
	return social_join_hook_map_state(map);
}

static int backend_friend_now(void *user, unsigned int id, int *online, struct social_join_target *target)
{
	static struct social_snapshot snapshot;
	int index;

	(void)user;
	*online = 0;
	social_snapshot_get(&snapshot);
	for (index = 0; index < snapshot.friend_count; index++)
	{
		if (snapshot.friends[index].id == id)
		{
			*online = snapshot.friends[index].online;
			if (snapshot.friends[index].online && snapshot.friends[index].joinable)
			{
				*target = snapshot.friends[index].join;
				return 1;
			}
		}
	}
	return 0;
}

static int backend_begin(void *user, const unsigned char *hash, const unsigned char *token)
{
	(void)user;
	(void)hash;
	(void)token;
	world.begun++;
	return 1;
}

static int backend_progress(void *user, int *stage, char *reason, int size)
{
	(void)user;
	snprintf(reason, (size_t)size, "%s", "");
	*stage = world.stage;
	return *stage;
}

static int backend_game_ready(void *user, const unsigned char *hash)
{
	(void)user;
	return social_join_hook_game_ready(hash);
}

static void backend_cancel(void *user)
{
	(void)user;
}

static int backend_strict(void *user)
{
	(void)user;
	return 0;
}

static void world_start(void)
{
	struct social_join_backend backend;
	struct social_flow_world flow_world;

	memset(&world, 0, sizeof(world));
	world.stage = P2P_JOIN_CONNECTED;
	world.leave_frames = 3;
	world.screen = SOCIAL_FLOW_SCREEN_NONE;
	memset(&backend, 0, sizeof(backend));
	backend.now = world_now;
	backend.version = backend_version;
	backend.busy = world_busy;
	backend.in_game_of = backend_in_game_of;
	backend.map_state = backend_map_state;
	backend.friend_now = backend_friend_now;
	backend.begin = backend_begin;
	backend.progress = backend_progress;
	backend.game_ready = backend_game_ready;
	backend.cancel = backend_cancel;
	backend.strict_nat = backend_strict;
	social_join_set_backend(&backend);
	social_join_set_game_hooks(test_hook_map_state, test_hook_game_ready, test_hook_busy);
	social_join_set_map_download_hook(test_hook_map_download);
	memset(&flow_world, 0, sizeof(flow_world));
	flow_world.now = world_now;
	flow_world.busy = world_busy;
	flow_world.menus_up = world_menus_up;
	flow_world.screen = world_screen;
	flow_world.leave = world_leave;
	flow_world.open_list = world_open_list;
	flow_world.log = world_log;
	flow_log_text[0] = 0;
	social_flow_set_world(&flow_world);
}

static struct social_snapshot snapshots[2];

static const struct social_snapshot *snap(struct fake_node *node)
{
	social_front_snapshot(node->front, &snapshots[0]);
	return &snapshots[0];
}

static void link_friends(struct fake_node *a, struct fake_node *b)
{
	CHECK(social_friend_add(a->engine, b->engine->identity.key, b->engine->circle_generation, b->engine->circle_key, 1), "link a->b");
	CHECK(social_friend_add(b->engine, a->engine->identity.key, a->engine->circle_generation, a->engine->circle_key, 1), "link b->a");
}

static unsigned int friend_id(struct fake_node *node, struct fake_node *other)
{
	struct social_friend *friend = social_friend_by_key(node->engine, other->engine->identity.key);

	return friend ? friend->id : 0;
}

static void host_place(struct fake_node *node, int joinable, int players)
{
	struct social_place place;

	memset(&place, 0, sizeof(place));
	place.activity = SOCIAL_GAME;
	place.hosting = 1;
	place.has_invite = 1;
	place.joinable = (unsigned char)joinable;
	place.private_game = (unsigned char)!joinable;
	strcpy(place.map, "bloodgulch");
	strcpy(place.gametype, "Slayer");
	place.players = (unsigned char)players;
	place.maximum_players = 16;
	place.engine_type = 2;
	memset(place.invite_hash, 0x5A, 16);
	memset(place.invite_token, 0xA5, 16);
	social_front_set_place(node->front, &place);
}

/* what the machine tells friends it is doing */
static void activity(struct fake_node *node, int value)
{
	struct social_place place;

	memset(&place, 0, sizeof(place));
	place.activity = (unsigned char)value;
	social_front_set_place(node->front, &place);
}

static void begin_world(unsigned long long seed, int nodes)
{
	int index;

	fake_start(&net, seed);
	for (index = 0; index < nodes; index++)
		fake_add(&net, index, 1);
	world_start();
}

static void use(struct fake_node *node)
{
	social_global_front = node->front;
}

/* a frame of 100 milliseconds for the world, the fake network and the flow */
static void step(int count)
{
	int index;

	for (index = 0; index < count; index++)
	{
		world.clock += 100;
		fake_run(&net, 100);
		social_flow_update();
	}
}

static struct social_join_status status_now(void)
{
	struct social_join_status status;

	social_join_status_get(&status);
	return status;
}

/* b is invited by a to a's game (b has a first invite in its snapshot); the invite's id */
static unsigned int invite_b(struct fake_node *a, struct fake_node *b)
{
	host_place(a, 1, 3);
	fake_run(&net, 6000);
	social_front_invite(a->front, friend_id(a, b));
	fake_run(&net, 2000);
	CHECK(snap(b)->invite_count == 1, "invited");
	return snap(b)->invites[0].id;
}

/* an invitation answered in a game: the game is left, the menus come up, the join begins, the Direct Link list opens, A is pressed for the game, and the join is done once this machine is in it */
static void case_leave_and_join(void)
{
	struct fake_node *a, *b;
	struct social_join_status status;
	unsigned int id;
	int frames;

	begin_world(21, 2);
	a = &net.nodes[0];
	b = &net.nodes[1];
	link_friends(a, b);
	use(b);
	id = invite_b(a, b);
	world.busy = 1;
	world.screen = SOCIAL_FLOW_SCREEN_NONE;
	CHECK(social_flow_leave_and_join_invite(id) == SOCIAL_JOIN_REASON_NONE, "accepted");
	CHECK(social_flow_leaving() && !strcmp(social_flow_waiting_text(), "Leaving your game..."), "leaving: '%s'", social_flow_waiting_text());
	fake_run(&net, 300);
	CHECK(snap(b)->invite_count == 0, "the invitation is used");
	/* the game is told to go on every frame until it has gone; nothing is joined before */
	step(2);
	CHECK(world.leave_calls >= 2 && world.begun == 0 && status_now().state == SOCIAL_JOIN_IDLE, "leave called %d times, begun %d", world.leave_calls, world.begun);
	for (frames = 0; frames < 20 && world.busy; frames++)
		step(1);
	CHECK(!world.busy && world.begun == 0, "the game is gone: begun %d", world.begun);
	/* the menus are not up yet: still waiting */
	step(3);
	CHECK(world.begun == 0 && !strcmp(social_flow_waiting_text(), "Back to the menus..."), "waits for the menus: '%s'", social_flow_waiting_text());
	world.menus_up = 1;
	world.screen = SOCIAL_FLOW_SCREEN_SAFE;
	step(1);
	CHECK(world.begun == 1 && !social_flow_leaving(), "the join begins: %d", world.begun);
	status = status_now();
	CHECK(status.state == SOCIAL_JOIN_WORKING || status.state == SOCIAL_JOIN_READY, "state %d", status.state);
	/* the host's game appears in the list: the join is ready, and the Direct Link list is opened (once) */
	world.game_ready = 1;
	world.screen_stays = 1;
	step(3);
	status = status_now();
	CHECK(status.state == SOCIAL_JOIN_READY && world.opened == 1, "ready: state %d, opened %d", status.state, world.opened);
	step(10);
	CHECK(world.opened == 1, "opened once: %d", world.opened);
	/* the list presses A for the game: now, then not again for three seconds */
	CHECK(social_flow_press_allowed() == 1, "the first press");
	CHECK(social_flow_press_allowed() == 0, "not twice at once");
	step(31);
	CHECK(social_flow_press_allowed() == 1, "again after three seconds");
	/* the press joined it: this machine is in a game, the join is done */
	world.busy = 1;
	step(1);
	CHECK(status_now().state == SOCIAL_JOIN_IDLE, "done once joined: %d", status_now().state);
	/* the log says what the flow did, for a console run to read */
	CHECK(strstr(flow_log_text, "the game is left first") && strstr(flow_log_text, "the menus are up; the invitation's game is joined") && strstr(flow_log_text, "A pressed for the game\n") &&
		strstr(flow_log_text, "A pressed for the game again") && strstr(flow_log_text, "the join is done"), "the log: %s", flow_log_text);
}

/* a game that cannot be joined is not worth leaving this one for */
static void case_invite_checks(void)
{
	struct fake_node *a, *b;
	unsigned int id;

	begin_world(22, 2);
	a = &net.nodes[0];
	b = &net.nodes[1];
	link_friends(a, b);
	use(b);
	id = invite_b(a, b);
	world.busy = 1;
	/* the map is missing: told in words, nothing left */
	world.map_state = 2;
	CHECK(social_flow_leave_and_join_invite(id) == SOCIAL_JOIN_REASON_MAP_MISSING && !social_flow_leaving() && world.leave_calls == 0, "a missing map: no leaving");
	CHECK(snap(b)->invite_count == 1, "and the invitation is kept");
	world.map_download = 0;
	CHECK(strstr(social_join_reason_text(SOCIAL_JOIN_REASON_MAP_MISSING, "bloodgulch"), "DOWNLOAD MAPS"), "with downloads off, where to turn them on: '%s'", social_join_reason_text(SOCIAL_JOIN_REASON_MAP_MISSING, "bloodgulch"));
	world.map_download = 1;
	CHECK(!strstr(social_join_reason_text(SOCIAL_JOIN_REASON_MAP_MISSING, "bloodgulch"), "DOWNLOAD MAPS") && strstr(social_join_reason_text(SOCIAL_JOIN_REASON_MAP_MISSING, "bloodgulch"), "bloodgulch"), "with downloads on, the map");
	world.map_state = 1;
	/* in that game already */
	world.in_game_of = 1;
	CHECK(social_flow_leave_and_join_invite(id) == SOCIAL_JOIN_REASON_TOGETHER && !social_flow_leaving(), "together");
	world.in_game_of = 0;
	/* an invitation that is not there */
	CHECK(social_flow_leave_and_join_invite(987654) == SOCIAL_JOIN_REASON_BAD_INVITE, "a lapsed invitation");
	/* the version differs (the invitation names another) */
	{
		unsigned char version_before = (unsigned char)b->engine->env.network_version;

		b->engine->env.network_version = 99;
		social_front_invite(a->front, friend_id(a, b));
		fake_run(&net, 100);
		b->engine->env.network_version = version_before;
	}
	/* a downloadable map joins, saying so */
	CHECK(social_flow_leave_and_join_invite(id) == SOCIAL_JOIN_REASON_NONE && social_flow_leaving(), "a downloadable map is joined");
	/* a second invitation while leaving is refused */
	CHECK(social_flow_leave_and_join_invite(id) == SOCIAL_JOIN_REASON_BUSY || social_flow_leave_and_join_invite(id) == SOCIAL_JOIN_REASON_BAD_INVITE, "one at a time");
	world.menus_up = 1;
	world.screen = SOCIAL_FLOW_SCREEN_SAFE;
	step(10);
	CHECK(world.begun == 1 && strstr(status_now().text, "downloads"), "joining, with the download said: '%s'", status_now().text);
}

/* a game that will not go: the join is given up in words after thirty seconds */
static void case_leave_times_out(void)
{
	struct fake_node *a, *b;
	unsigned int id;
	struct social_join_status status;

	begin_world(23, 2);
	a = &net.nodes[0];
	b = &net.nodes[1];
	link_friends(a, b);
	use(b);
	id = invite_b(a, b);
	world.busy = 1;
	world.leave_frames = -1;
	CHECK(social_flow_leave_and_join_invite(id) == SOCIAL_JOIN_REASON_NONE, "begun");
	step(200);
	CHECK(world.busy && social_flow_leaving() && world.begun == 0, "still leaving at 20 s");
	step(120);
	status = status_now();
	CHECK(!social_flow_leaving() && world.begun == 0 && status.state == SOCIAL_JOIN_FAILED && status.reason == SOCIAL_JOIN_REASON_BUSY, "given up: leaving %d state %d reason %d", social_flow_leaving(),
		status.state, status.reason);
	CHECK(!strcmp(status.text, "Leave your current game first"), "in words: '%s'", status.text);
}

/* a join that is ready with another screen in the way waits and says so; the list opens once the screen is one it can be opened over; the join lapses after two minutes */
static void case_ready_screens(void)
{
	struct fake_node *a, *b;
	struct social_join_status status;

	begin_world(24, 2);
	a = &net.nodes[0];
	b = &net.nodes[1];
	link_friends(a, b);
	use(b);
	host_place(a, 1, 3);
	fake_run(&net, 6000);
	world.menus_up = 1;
	world.game_ready = 1;
	world.screen = SOCIAL_FLOW_SCREEN_OTHER;
	CHECK(social_join_friend(friend_id(b, a)) == SOCIAL_JOIN_REASON_NONE, "join begun");
	step(5);
	CHECK(status_now().state == SOCIAL_JOIN_READY && world.opened == 0, "ready, nothing opened over another screen");
	step(30);
	CHECK(strstr(social_flow_waiting_text(), "go back to the main menu") && world.opened == 0, "and it says what to do: '%s'", social_flow_waiting_text());
	world.screen = SOCIAL_FLOW_SCREEN_SAFE;
	step(2);
	CHECK(world.opened == 1 && !social_flow_waiting_text()[0], "opened over a screen it can be: %d", world.opened);
	/* the list could not be opened this frame: tried again */
	social_join_cancel();
	social_join_done();
	world.screen = SOCIAL_FLOW_SCREEN_SAFE;
	world.open_refused = 1;
	CHECK(social_join_friend(friend_id(b, a)) == SOCIAL_JOIN_REASON_NONE, "again");
	step(10);
	CHECK(world.opened == 1, "refused to open: nothing counted");
	world.open_refused = 0;
	step(2);
	CHECK(world.opened == 2, "and opened when it could: %d", world.opened);
	/* the A press three times did nothing: the host's game would not take this machine */
	world.screen = SOCIAL_FLOW_SCREEN_LIST;
	CHECK(social_flow_press_allowed() == 1, "press 1");
	step(31);
	CHECK(social_flow_press_allowed() == 1, "press 2");
	step(31);
	CHECK(social_flow_press_allowed() == 1, "press 3");
	step(31);
	CHECK(social_flow_press_allowed() == 0, "no fourth");
	status = status_now();
	CHECK(status.state == SOCIAL_JOIN_FAILED && status.reason == SOCIAL_JOIN_REASON_REFUSED, "refused: state %d reason %d", status.state, status.reason);
	/* a failed join is held eight seconds and cleared */
	step(100);
	CHECK(status_now().state == SOCIAL_JOIN_IDLE, "cleared");
	/* a join nobody finishes lapses after two minutes */
	world.screen = SOCIAL_FLOW_SCREEN_OTHER;
	CHECK(social_join_friend(friend_id(b, a)) == SOCIAL_JOIN_REASON_NONE, "once more");
	step(1195);
	CHECK(status_now().state == SOCIAL_JOIN_READY, "ready at 119 s");
	step(10);
	CHECK(status_now().state == SOCIAL_JOIN_FAILED && status_now().reason == SOCIAL_JOIN_REASON_CANCELLED, "lapsed: %d", status_now().state);
}

/* the party's follower, in the menus, joins its leader's game by itself; one in a game is not moved; one that opted out stays */
static void case_follow(void)
{
	struct fake_node *a, *b;
	struct social_join_status status;
	int index;

	begin_world(25, 2);
	a = &net.nodes[0];
	b = &net.nodes[1];
	link_friends(a, b);
	use(b);
	fake_run(&net, 6000);
	social_front_party(a->front, 1, 0, 0, NULL);
	fake_run(&net, 500);
	social_front_party(a->front, 2, friend_id(a, b), 0, NULL);
	fake_run(&net, 2000);
	for (index = 0; index < snap(b)->invite_count; index++)
	{
		if (snap(b)->invites[index].kind == 2)
			social_front_party_token(b->front, snap(b)->invites[index].party_token);
	}
	fake_run(&net, 15000);
	CHECK(snap(b)->party.active && !snap(b)->party.joining && snap(b)->party.member_count == 2, "b is in the party");
	/* the leader hosts: nothing happens to a follower that is in a game */
	world.menus_up = 1;
	world.screen = SOCIAL_FLOW_SCREEN_SAFE;
	world.game_ready = 1;
	world.busy = 1;
	activity(b, SOCIAL_GAME);
	host_place(a, 1, 2);
	step(100);
	CHECK(world.begun == 0 && status_now().state == SOCIAL_JOIN_IDLE, "a follower in a game stays: begun %d", world.begun);
	/* it leaves its game for the menus: it follows */
	world.busy = 0;
	activity(b, SOCIAL_MENUS);
	step(100);
	CHECK(world.begun == 1, "the follower joined the leader's game: begun %d state %d", world.begun, status_now().state);
	status = status_now();
	CHECK(status.state == SOCIAL_JOIN_READY && world.opened == 1, "and the list is open for it: state %d opened %d", status.state, world.opened);
	/* the leader sees it joined */
	step(100);
	{
		const struct social_snapshot *leader = snap(a);
		int joined = 0;

		for (index = 0; index < leader->party.member_count; index++)
			joined += !leader->party.members[index].self && (leader->party.members[index].follow_result == SOCIAL_FOLLOW_JOINED);
		CHECK(joined == 1, "the leader's list shows b joined: %d", joined);
	}
	/* the press joined it */
	world.busy = 1;
	step(5);
	CHECK(status_now().state == SOCIAL_JOIN_IDLE, "done");
}

/* following is off: nothing joins */
static void case_follow_off(void)
{
	struct fake_node *a, *b;
	int index;

	begin_world(26, 2);
	a = &net.nodes[0];
	b = &net.nodes[1];
	link_friends(a, b);
	use(b);
	fake_run(&net, 6000);
	social_front_party(a->front, 1, 0, 0, NULL);
	fake_run(&net, 500);
	social_front_party(a->front, 2, friend_id(a, b), 0, NULL);
	fake_run(&net, 2000);
	for (index = 0; index < snap(b)->invite_count; index++)
	{
		if (snap(b)->invites[index].kind == 2)
			social_front_party_token(b->front, snap(b)->invites[index].party_token);
	}
	fake_run(&net, 15000);
	social_front_party(b->front, 9, 0, 0, NULL);
	fake_run(&net, 500);
	world.menus_up = 1;
	world.screen = SOCIAL_FLOW_SCREEN_SAFE;
	world.game_ready = 1;
	activity(b, SOCIAL_MENUS);
	host_place(a, 1, 2);
	step(200);
	CHECK(world.begun == 0, "follow is off: nothing joined (%d)", world.begun);
	social_front_party(b->front, 9, 0, 1, NULL);
	step(60);
	CHECK(world.begun == 1, "follow on again: joined (%d)", world.begun);
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "leave-and-join"))
		case_leave_and_join();
	else if (!strcmp(name, "invite-checks"))
		case_invite_checks();
	else if (!strcmp(name, "leave-times-out"))
		case_leave_times_out();
	else if (!strcmp(name, "ready-screens"))
		case_ready_screens();
	else if (!strcmp(name, "follow"))
		case_follow();
	else if (!strcmp(name, "follow-off"))
		case_follow_off();
	else
		return 2;
	printf("ok\n");
	return 0;
}
