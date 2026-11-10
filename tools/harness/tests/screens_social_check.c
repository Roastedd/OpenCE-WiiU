/* screens_social_check.c: the friends layer as the social screens' backend (port/linux/game/screens_social_backend.c) on the fake brokers of social_fake.h: a friend added by its code,
seen in the FRIENDS tab's model, invited, and the invitation shown as a toast on the other machine; the party; the failure words; the roster's flags; the holds of the "online"
toasts; and the screens with the layer off. Cases are named on the command line (test_screens_social.py). */

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

#include "screens.c"
#include "screens_social_backend.c"

/* (screens_mock.c is not part of this program) */
struct screens_provider const *screens_mock_provider(void)
{
	return NULL;
}

static long enable_setting = 2;

long config_integer(const char *name)
{
	(void)name;
	return enable_setting;
}

static struct fake_net net;

static unsigned long fake_clock(void)
{
	return net.time;
}

static int fake_version(void)
{
	return 41;
}

/* the join's stand-in for internet play (what the process's social_p2p.inc is): the friend as the global front's snapshot has it */
static struct
{
	int busy;
	int map_state;
	int game_ready;
	int begun;
	int stage;
	unsigned int clock;
	/* the flow's: leaving a game, the menus, the screen on top, the list opened */
	int leave_calls;
	int menus_up;
	int screen;
	int opened;
} world;

static unsigned int flow_now(void *user)
{
	(void)user;
	return world.clock;
}

static int flow_busy(void *user)
{
	(void)user;
	return world.busy;
}

static int flow_menus_up(void *user)
{
	(void)user;
	return world.menus_up;
}

static int flow_screen(void *user)
{
	(void)user;
	return world.screen;
}

static int flow_leave(void *user)
{
	(void)user;
	world.leave_calls++;
	world.busy = 0;
	return 1;
}

static int flow_open_list(void *user)
{
	(void)user;
	world.opened++;
	world.screen = SOCIAL_FLOW_SCREEN_LIST;
	return 1;
}

static int world_map_state(const char *map)
{
	(void)map;
	return world.map_state;
}

static int world_game_ready(const unsigned char *hash)
{
	(void)hash;
	return world.game_ready;
}

static int world_busy(void)
{
	return world.busy;
}

static unsigned int backend_now(void *user)
{
	(void)user;
	return world.clock;
}

static int backend_version(void *user)
{
	(void)user;
	return 41;
}

static int backend_busy(void *user)
{
	(void)user;
	return world.busy;
}

static int backend_in_game_of(void *user, const unsigned char *hash)
{
	(void)user;
	(void)hash;
	return 0;
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

	memset(&world, 0, sizeof(world));
	world.stage = P2P_JOIN_CONNECTED;
	memset(&backend, 0, sizeof(backend));
	backend.now = backend_now;
	backend.version = backend_version;
	backend.busy = backend_busy;
	backend.in_game_of = backend_in_game_of;
	backend.map_state = backend_map_state;
	backend.friend_now = backend_friend_now;
	backend.begin = backend_begin;
	backend.progress = backend_progress;
	backend.game_ready = backend_game_ready;
	backend.cancel = backend_cancel;
	backend.strict_nat = backend_strict;
	social_join_set_backend(&backend);
	social_join_set_game_hooks(world_map_state, world_game_ready, world_busy);
	{
		struct social_flow_world flow_world = { NULL, flow_now, flow_busy, flow_menus_up, flow_screen, flow_leave, flow_open_list };

		social_flow_set_world(&flow_world);
	}
}

/* ---------- helpers */

struct hooks_set
{
	int dummy;
};

/* the machine whose layer the screens read now: the process has one, so the test points it at one node or another */
static void use(struct fake_node *node)
{
	social_global_front = node->front;
	screens_social_backend_reset();
}

static void begin_world(unsigned long long seed, int nodes)
{
	struct screens_social_hooks hooks;
	int index;

	fake_start(&net, seed);
	for (index = 0; index < nodes; index++)
		fake_add(&net, index, 1);
	hooks.milliseconds = fake_clock;
	hooks.network_version = fake_version;
	screens_social_backend_hooks(&hooks);
	world_start();
	screens_provider_set(screens_social_backend());
	screens_backend_set(screens_social_backend());
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

static int ascii_clean(char const *text)
{
	for (; *text; text++)
	{
		if ((unsigned char)*text < 0x20 || (unsigned char)*text > 0x7E || *text == '|')
			return 0;
	}
	return 1;
}

static struct screens_model model;

/* the model of the screens, rebuilt from the provider in use (the rebuild only if the generation moved) */
static void look(void)
{
	screens_model_update(&model, 0);
}

static int toast_kinds[16];

/* every toast waiting, in order (the kinds in toast_kinds); the last text and detail are kept */
static struct screens_toast last_toast[16];

static int drain_toasts(void)
{
	int count = 0;
	struct screens_toast toast;
	struct screens_provider const *provider = screens_provider();

	while (count < 16 && provider->toast_pop(&toast))
	{
		CHECK(ascii_clean(toast.text) && ascii_clean(toast.detail) && strlen(toast.text) < SCREENS_LINE_SIZE, "a toast of clean text: '%s' '%s'", toast.text, toast.detail);
		toast_kinds[count] = toast.kind;
		last_toast[count] = toast;
		count++;
	}
	return count;
}

static int find_toast(int count, int kind)
{
	int index;

	for (index = 0; index < count; index++)
	{
		if (toast_kinds[index] == kind)
			return index;
	}
	return -1;
}

/* the typed code of a node: lower case, in groups of four, as a person types it */
static void typed_code(struct fake_node *node, char *text)
{
	int index, length = 0;

	for (index = 0; node->engine->identity.code[index]; index++)
	{
		char c = node->engine->identity.code[index];

		if (c == '-')
			c = ' ';
		else if (c >= 'A' && c <= 'Z')
			c = (char)(c + 32);
		text[length++] = c;
	}
	text[length] = 0;
}

static struct screens_friend_row const *friend_named(char const *name)
{
	int index;

	for (index = 0; index < model.friends_count[SCREENS_TAB_FRIENDS]; index++)
	{
		if (!strcmp(model.friends[SCREENS_TAB_FRIENDS][index].name, name))
			return &model.friends[SCREENS_TAB_FRIENDS][index];
	}
	return NULL;
}

/* ---------- the end to end case */

static void case_befriend_and_invite(void)
{
	struct fake_node *a, *b;
	struct screens_provider const *provider;
	struct screens_friend_row row;
	char answer[SCREENS_LINE_SIZE], code[40];
	int count, index;

	begin_world(11, 2);
	a = &net.nodes[0];
	b = &net.nodes[1];
	provider = screens_provider();
	use(a);
	fake_run(&net, 1000);
	/* the layer is on, with its brokers: the screen says so, and has this machine's code to show */
	look();
	CHECK(model.friends_summary.service == SCREENS_SERVICE_CONNECTED, "connected: %d", model.friends_summary.service);
	CHECK(!strcmp(model.friends_summary.own_code, a->engine->identity.code), "own code '%s' against '%s'", model.friends_summary.own_code, a->engine->identity.code);
	CHECK(model.friends_count[SCREENS_TAB_FRIENDS] == 0 && model.friends_count[SCREENS_TAB_REQUESTS] == 0, "empty lists");
	/* ADD FRIEND: a wrong code is refused in words, with the code unsent; the right one, typed in lower case with spaces, is sent */
	CHECK(!provider->friend_add("hello", answer, sizeof(answer)) && strstr(answer, "13 LETTERS"), "too short: %s", answer);
	CHECK(!provider->friend_add("ABCD EFGH IJK$ M", answer, sizeof(answer)) && strstr(answer, "NO I, L, O OR U"), "not a code: %s", answer);
	CHECK(!provider->friend_add("ABCD EFGH IJKL M", answer, sizeof(answer)) && strstr(answer, "MISTAKE"), "a letter that reads as another, and a wrong check: %s", answer);
	CHECK(!provider->friend_add("aaaa aaaa aaaa a", answer, sizeof(answer)) && strstr(answer, "MISTAKE"), "a code with a wrong check: %s", answer);
	typed_code(a, code);
	CHECK(!provider->friend_add(code, answer, sizeof(answer)) && strstr(answer, "YOUR OWN CODE"), "own code: %s", answer);
	typed_code(b, code);
	CHECK(provider->friend_add(code, answer, sizeof(answer)) && strstr(answer, "REQUEST SENT") && ascii_clean(answer), "sent: %s", answer);
	fake_run(&net, 1500);
	look();
	CHECK(model.friends_count[SCREENS_TAB_REQUESTS] == 1 && model.friends[SCREENS_TAB_REQUESTS][0].presence == SCREENS_PRESENCE_REQUEST_SENT &&
		!strcmp(model.friends[SCREENS_TAB_REQUESTS][0].code, b->engine->identity.code), "the sent request is in the REQUESTS tab");
	CHECK(model.friends_summary.counts[SCREENS_TAB_REQUESTS] == 1, "and counted");
	/* the other machine sees the request as a toast and in its REQUESTS tab, and accepts through the screens' action */
	use(b);
	look();
	CHECK(model.friends_count[SCREENS_TAB_REQUESTS] == 1 && model.friends[SCREENS_TAB_REQUESTS][0].presence == SCREENS_PRESENCE_REQUEST_GOT &&
		!strcmp(model.friends[SCREENS_TAB_REQUESTS][0].name, "Node0"), "the request got: %d rows", model.friends_count[SCREENS_TAB_REQUESTS]);
	count = drain_toasts();
	index = find_toast(count, SCREENS_TOAST_REQUEST);
	CHECK(index >= 0 && !strcmp(last_toast[index].text, "NODE0 WANTS TO BE YOUR FRIEND"), "the request's toast: %d '%s'", count, count ? last_toast[0].text : "");
	row = model.friends[SCREENS_TAB_REQUESTS][0];
	CHECK(provider->friend_action(SCREENS_ACTION_ACCEPT, &row, answer, sizeof(answer)), "accept");
	fake_run(&net, 15000);
	look();
	CHECK(model.friends_count[SCREENS_TAB_FRIENDS] == 1 && model.friends_count[SCREENS_TAB_REQUESTS] == 0, "friends now: %d, requests %d", model.friends_count[SCREENS_TAB_FRIENDS],
		model.friends_count[SCREENS_TAB_REQUESTS]);
	/* the first machine: the new friend in the FRIENDS tab, online, with its code and name */
	use(a);
	fake_run(&net, 100);
	look();
	typed_code(b, code);
	CHECK(!provider->friend_add(code, answer, sizeof(answer)) && strstr(answer, "FRIENDS ALREADY"), "adding a friend again: %s", answer);
	CHECK(model.friends_count[SCREENS_TAB_FRIENDS] == 1 && model.friends_count[SCREENS_TAB_REQUESTS] == 0, "a: friends %d, requests %d", model.friends_count[SCREENS_TAB_FRIENDS],
		model.friends_count[SCREENS_TAB_REQUESTS]);
	row = model.friends[SCREENS_TAB_FRIENDS][0];
	CHECK(!strcmp(row.name, "Node1") && !strcmp(row.code, b->engine->identity.code) && row.presence == SCREENS_PRESENCE_ONLINE && model.friends_summary.online == 1,
		"the friend: '%s' '%s' presence %d online %d", row.name, row.code, row.presence, model.friends_summary.online);
	/* its toasts: a friend made now; "online" only after the hold */
	count = drain_toasts();
	CHECK(find_toast(count, SCREENS_TOAST_NOTICE) >= 0 && strstr(last_toast[find_toast(count, SCREENS_TOAST_NOTICE)].text, "NODE1 IS NOW YOUR FRIEND"), "the friendship's toast");
	CHECK(find_toast(count, SCREENS_TOAST_FRIEND_ONLINE) < 0, "and not yet 'online': held");
	fake_run(&net, 12000);
	count = drain_toasts();
	index = find_toast(count, SCREENS_TOAST_FRIEND_ONLINE);
	CHECK(index >= 0 && !strcmp(last_toast[index].text, "NODE1 IS ONLINE"), "online after the hold: %d toasts", count);
	/* a hosts a game; the friend row is joinable on b, and a's invitation reaches b as a toast with the game in its detail */
	host_place(a, 1, 3);
	fake_run(&net, 6000);
	look();
	CHECK(provider->friend_action(SCREENS_ACTION_INVITE_GAME, &row, answer, sizeof(answer)), "invite");
	fake_run(&net, 2000);
	count = drain_toasts();
	CHECK(find_toast(count, SCREENS_TOAST_NOTICE) >= 0 && strstr(last_toast[find_toast(count, SCREENS_TOAST_NOTICE)].text, "NODE1 WAS INVITED"), "a is told the invitation went");
	use(b);
	look();
	{
		struct screens_friend_row const *host = friend_named("Node0");

		CHECK(host && host->presence == SCREENS_PRESENCE_PLAYING && host->join == SCREENS_JOIN_OPEN && host->players == 3 && host->maximum == 16 && !strcmp(host->map, "bloodgulch") &&
			!strcmp(host->gametype, "Slayer"), "b sees a's game, joinable");
	}
	count = drain_toasts();
	index = find_toast(count, SCREENS_TOAST_INVITE);
	CHECK(index >= 0 && !strcmp(last_toast[index].text, "NODE0 INVITES YOU") && !strcmp(last_toast[index].detail, "bloodgulch - Slayer - 3 of 16"), "the invitation's toast: '%s' / '%s'",
		index >= 0 ? last_toast[index].text : "", index >= 0 ? last_toast[index].detail : "");
	/* b answers it (the D-pad's answer): the join starts and the screen can say what it is doing */
	{
		struct social_join_status status;
		char line[SCREENS_LINE_SIZE * 2];
		long id = last_toast[index].id;

		CHECK(snap(b)->invite_count == 1 && (long)snap(b)->invites[0].id == id, "the toast's id is the invitation's");
		provider->invite_answer(id, 1);
		social_join_status_get(&status);
		CHECK(status.state == SOCIAL_JOIN_WORKING && world.begun == 1, "joining: state %d begun %d", status.state, world.begun);
		CHECK(screens_social_join_line(line, sizeof(line)) && strstr(line, "JOINING") && strstr(line, "NODE0") && ascii_clean(line), "the screen's line: '%s'", line);
		social_join_update();
		world.game_ready = 1;
		world.clock += 1000;
		social_join_update();
		social_join_status_get(&status);
		CHECK(status.state == SOCIAL_JOIN_READY, "the game is in the list: state %d", status.state);
		CHECK(screens_social_join_line(line, sizeof(line)) && strstr(line, "JOINING THE GAME"), "and the line says what is happening: '%s'", line);
		social_join_done();
	}
	use(a);
}

/* the join from a friend's row, and its refusals in words */
static void case_join_words(void)
{
	struct fake_node *a, *b;
	struct screens_provider const *provider;
	struct screens_friend_row row;
	char answer[SCREENS_LINE_SIZE];
	struct social_join_status status;
	int count;

	begin_world(12, 2);
	a = &net.nodes[0];
	b = &net.nodes[1];
	provider = screens_provider();
	link_friends(a, b);
	host_place(a, 1, 3);
	fake_run(&net, 6000);
	use(b);
	look();
	row = *friend_named("Node0");
	CHECK(row.join == SCREENS_JOIN_OPEN, "joinable");
	/* the map is missing, or can be downloaded: the row says so, and a missing one refuses in words */
	world.map_state = 1;
	screens_social_backend_reset();
	screens_model_update(&model, 1);
	CHECK(friend_named("Node0")->join == SCREENS_JOIN_DOWNLOAD, "download");
	world.map_state = 2;
	screens_model_update(&model, 1);
	CHECK(friend_named("Node0")->join == SCREENS_JOIN_NEEDS_MAP, "needs map");
	CHECK(!provider->friend_action(SCREENS_ACTION_JOIN_GAME, &row, answer, sizeof(answer)) && !strcmp(answer, "YOU DO NOT HAVE THE MAP BLOODGULCH."), "map missing: %s", answer);
	world.map_state = 0;
	/* this machine is in a game already */
	world.busy = 1;
	CHECK(!provider->friend_action(SCREENS_ACTION_JOIN_GAME, &row, answer, sizeof(answer)) && !strcmp(answer, "LEAVE YOUR CURRENT GAME FIRST."), "busy: %s", answer);
	world.busy = 0;
	CHECK(world.begun == 0, "nothing was tried");
	/* the game ends under it: the host is still being asked, and the friend's presence no longer shows the game */
	world.stage = P2P_JOIN_ASKING;
	CHECK(provider->friend_action(SCREENS_ACTION_JOIN_GAME, &row, answer, sizeof(answer)) && world.begun == 1, "joins");
	social_join_status_get(&status);
	CHECK(status.state == SOCIAL_JOIN_WORKING, "working");
	{
		struct social_place place;

		memset(&place, 0, sizeof(place));
		place.activity = SOCIAL_MENUS;
		social_front_set_place(a->front, &place);
	}
	fake_run(&net, 6000);
	world.clock += 6000;
	social_join_update();
	social_join_status_get(&status);
	CHECK(status.state == SOCIAL_JOIN_FAILED && status.reason == SOCIAL_JOIN_REASON_LEFT, "the friend left: state %d reason %d", status.state, status.reason);
	/* the failure is a toast, once */
	count = drain_toasts();
	CHECK(find_toast(count, SCREENS_TOAST_JOIN_FAILED) >= 0 && !strcmp(last_toast[find_toast(count, SCREENS_TOAST_JOIN_FAILED)].text, "YOUR FRIEND HAS LEFT THAT GAME."), "the failure's toast: %d",
		count);
	CHECK(drain_toasts() == 0, "told once");
	social_join_done();
	look();
	CHECK(friend_named("Node0")->join == SCREENS_JOIN_NONE && friend_named("Node0")->presence == SCREENS_PRESENCE_ONLINE, "the row without a game");
	/* an invitation that is left: the row is joinable by it, and joining uses it */
	host_place(a, 0, 3);
	fake_run(&net, 6000);
	/* (a private game is not joined by its presence: the row says it is by invitation only, and JOIN is not offered) */
	use(b);
	look();
	CHECK(friend_named("Node0")->join == SCREENS_JOIN_INVITE_ONLY, "a private game: join %d", friend_named("Node0")->join);
	row = *friend_named("Node0");
	{
		int offered[SCREENS_ACTIONS_MAXIMUM + 1], at;

		provider->friend_actions(SCREENS_TAB_FRIENDS, &row, offered);
		for (at = 0; offered[at] != SCREENS_ACTION_NONE; at++)
			CHECK(offered[at] != SCREENS_ACTION_JOIN_GAME, "no JOIN for a private game");
	}
	CHECK(!strcmp(screens_join_word(SCREENS_JOIN_INVITE_ONLY), "INVITE ONLY"), "its word");
	social_front_invite(a->front, social_friend_by_key(a->engine, b->engine->identity.key)->id);
	fake_run(&net, 3000);
	use(b);
	look();
	CHECK(snap(b)->invite_count == 1, "invited to a private game");
	CHECK(friend_named("Node0")->presence == SCREENS_PRESENCE_PLAYING && friend_named("Node0")->join == SCREENS_JOIN_OPEN, "the row offers the invitation's game: presence %d join %d",
		friend_named("Node0")->presence, friend_named("Node0")->join);
	row = *friend_named("Node0");
	CHECK(provider->friend_action(SCREENS_ACTION_JOIN_GAME, &row, answer, sizeof(answer)), "joined by the invitation");
	fake_run(&net, 300);
	CHECK(snap(b)->invite_count == 0, "and the invitation was used");
	social_join_cancel();
	social_join_done();
	(void)count;
}

/* an invitation answered in a game leaves it and then joins; one that cannot be joined is said at once and the game goes on; later leaves it alone */
static void case_invite_in_game(void)
{
	struct fake_node *a, *b;
	struct screens_provider const *provider;
	struct social_join_status status;
	long id;
	int count, index;

	begin_world(13, 2);
	a = &net.nodes[0];
	b = &net.nodes[1];
	provider = screens_provider();
	link_friends(a, b);
	host_place(a, 1, 3);
	fake_run(&net, 6000);
	social_front_invite(a->front, social_friend_by_key(a->engine, b->engine->identity.key)->id);
	fake_run(&net, 2000);
	use(b);
	count = drain_toasts();
	index = find_toast(count, SCREENS_TOAST_INVITE);
	CHECK(index >= 0, "invited");
	id = last_toast[index].id;
	/* later: nothing happens to it, in a game or not */
	world.busy = 1;
	provider->invite_answer(id, 0);
	fake_run(&net, 300);
	CHECK(snap(b)->invite_count == 1 && world.begun == 0 && world.leave_calls == 0, "later leaves it");
	/* a map that is missing is said, and the game is not left */
	world.map_state = 2;
	provider->invite_answer(id, 1);
	social_join_status_get(&status);
	CHECK(status.state == SOCIAL_JOIN_IDLE && world.begun == 0 && !social_flow_leaving(), "nothing started for a missing map");
	count = drain_toasts();
	index = find_toast(count, SCREENS_TOAST_JOIN_FAILED);
	CHECK(index >= 0 && !strcmp(last_toast[index].text, "YOU DO NOT HAVE THE MAP BLOODGULCH."), "told: '%s'", index >= 0 ? last_toast[index].text : "");
	CHECK(snap(b)->invite_count == 1, "the invitation stays");
	world.map_state = 0;
	/* a game it can join: it is left, the menus come up, the join begins, the list opens */
	provider->invite_answer(id, 1);
	count = drain_toasts();
	index = find_toast(count, SCREENS_TOAST_NOTICE);
	CHECK(index >= 0 && !strcmp(last_toast[index].text, "LEAVING THE GAME TO JOIN NODE0") && social_flow_leaving(), "leaving: '%s'", index >= 0 ? last_toast[index].text : "");
	{
		char line[SCREENS_LINE_SIZE * 2];

		CHECK(screens_social_join_line(line, sizeof(line)) && strstr(line, "LEAVING YOUR GAME"), "the screen says so: '%s'", line);
	}
	world.clock += 100;
	social_flow_update();
	CHECK(world.leave_calls == 1 && !world.busy, "the game was told to go");
	world.clock += 100;
	social_flow_update();
	CHECK(world.begun == 0, "the menus are not up yet");
	world.menus_up = 1;
	world.screen = SOCIAL_FLOW_SCREEN_SAFE;
	world.game_ready = 1;
	world.clock += 100;
	social_flow_update();
	world.clock += 100;
	social_flow_update();
	social_join_status_get(&status);
	CHECK(world.begun == 1 && status.state == SOCIAL_JOIN_READY && world.opened == 1, "joined: begun %d state %d opened %d", world.begun, status.state, world.opened);
	fake_run(&net, 300);
	CHECK(snap(b)->invite_count == 0, "the invitation was used");
	social_join_cancel();
	social_join_done();
	/* an invitation that is gone */
	provider->invite_answer(987654, 1);
	count = drain_toasts();
	CHECK(find_toast(count, SCREENS_TOAST_NOTICE) >= 0 && strstr(last_toast[find_toast(count, SCREENS_TOAST_NOTICE)].text, "LAPSED"), "a lapsed invitation");
}

/* the requests tab, the blocked tab and the actions on rows */
static void case_actions(void)
{
	struct fake_node *a, *b, *c;
	struct screens_provider const *provider;
	struct screens_friend_row row;
	char answer[SCREENS_LINE_SIZE], code[40];
	int actions[SCREENS_ACTIONS_MAXIMUM + 1], index, found;

	begin_world(14, 3);
	a = &net.nodes[0];
	b = &net.nodes[1];
	c = &net.nodes[2];
	provider = screens_provider();
	link_friends(a, b);
	use(a);
	fake_run(&net, 6000);
	look();
	/* the friends row's actions: join only with a game; invite to the game and to the party; view code, remove, block */
	row = *friend_named("Node1");
	provider->friend_actions(SCREENS_TAB_FRIENDS, &row, actions);
	found = 0;
	for (index = 0; actions[index] != SCREENS_ACTION_NONE; index++)
		found |= 1 << actions[index];
	CHECK(!(found & (1 << SCREENS_ACTION_JOIN_GAME)) && (found & (1 << SCREENS_ACTION_INVITE_GAME)) && (found & (1 << SCREENS_ACTION_INVITE_PARTY)) &&
		(found & (1 << SCREENS_ACTION_VIEW_CODE)) && (found & (1 << SCREENS_ACTION_REMOVE_FRIEND)) && (found & (1 << SCREENS_ACTION_BLOCK)) && (found & (1 << SCREENS_ACTION_CANCEL)),
		"the friend's actions: 0x%x", found);
	CHECK(provider->friend_action(SCREENS_ACTION_VIEW_CODE, &row, answer, sizeof(answer)) && !strcmp(answer, b->engine->identity.code), "view code: %s", answer);
	/* a request from c: accept/decline/block on its row; blocking a request must not block (or remove) a friend */
	social_front_add_code(c->front, a->engine->identity.code);
	fake_run(&net, 3000);
	look();
	CHECK(model.friends_count[SCREENS_TAB_REQUESTS] == 1, "c's request");
	row = model.friends[SCREENS_TAB_REQUESTS][0];
	provider->friend_actions(SCREENS_TAB_REQUESTS, &row, actions);
	CHECK(actions[0] == SCREENS_ACTION_ACCEPT && actions[1] == SCREENS_ACTION_DECLINE && actions[2] == SCREENS_ACTION_BLOCK, "request actions");
	CHECK(provider->friend_action(SCREENS_ACTION_BLOCK, &row, answer, sizeof(answer)), "block a request");
	fake_run(&net, 1000);
	look();
	CHECK(model.friends_count[SCREENS_TAB_REQUESTS] == 0 && model.friends_count[SCREENS_TAB_FRIENDS] == 1 && snap(a)->blocked_count == 0, "a request is turned away; the friend is still one, nothing is blocked");
	/* a row of another kind asked for what does not apply to it */
	row = *friend_named("Node1");
	row.presence = SCREENS_PRESENCE_REQUEST_SENT;
	CHECK(!provider->friend_action(SCREENS_ACTION_ACCEPT, &row, answer, sizeof(answer)) && strstr(answer, "DOES NOT APPLY"), "accept on a sent request: %s", answer);
	CHECK(!provider->friend_action(SCREENS_ACTION_BLOCK, &row, answer, sizeof(answer)), "block on a sent request");
	CHECK(snap(a)->friend_count == 1, "and the friend is still there");
	/* block a friend, see it in the BLOCKED tab, unblock */
	row = *friend_named("Node1");
	row.presence = SCREENS_PRESENCE_ONLINE;
	CHECK(provider->friend_action(SCREENS_ACTION_BLOCK, &row, answer, sizeof(answer)), "block");
	fake_run(&net, 1000);
	look();
	CHECK(model.friends_count[SCREENS_TAB_FRIENDS] == 0 && model.friends_count[SCREENS_TAB_BLOCKED] == 1 && model.friends_summary.counts[SCREENS_TAB_BLOCKED] == 1, "blocked");
	row = model.friends[SCREENS_TAB_BLOCKED][0];
	CHECK(row.presence == SCREENS_PRESENCE_BLOCKED && ascii_clean(row.name) && strlen(row.name) <= SCREENS_NAME_LENGTH, "the blocked row: '%s'", row.name);
	CHECK(provider->friend_action(SCREENS_ACTION_UNBLOCK, &row, answer, sizeof(answer)), "unblock");
	fake_run(&net, 500);
	look();
	CHECK(model.friends_count[SCREENS_TAB_BLOCKED] == 0, "unblocked");
	/* a sent request is cancelled from its row */
	typed_code(c, code);
	CHECK(provider->friend_add(code, answer, sizeof(answer)), "add c");
	fake_run(&net, 500);
	look();
	CHECK(model.friends_count[SCREENS_TAB_REQUESTS] == 1 && model.friends[SCREENS_TAB_REQUESTS][0].presence == SCREENS_PRESENCE_REQUEST_SENT, "sent");
	row = model.friends[SCREENS_TAB_REQUESTS][0];
	CHECK(provider->friend_action(SCREENS_ACTION_DECLINE, &row, answer, sizeof(answer)), "cancel");
	fake_run(&net, 500);
	look();
	CHECK(model.friends_count[SCREENS_TAB_REQUESTS] == 0, "forgotten");
}

/* the party */
static void case_party(void)
{
	struct fake_node *a, *b;
	struct screens_provider const *provider;
	struct screens_friend_row row;
	struct screens_party_member members[SCREENS_PARTY_MAXIMUM];
	struct screens_party_summary summary;
	char answer[SCREENS_LINE_SIZE];
	int count, index, actions[SCREENS_ACTIONS_MAXIMUM + 1], found;

	begin_world(15, 2);
	a = &net.nodes[0];
	b = &net.nodes[1];
	provider = screens_provider();
	link_friends(a, b);
	use(a);
	fake_run(&net, 6000);
	look();
	CHECK(!model.party_summary.present && model.party_count == 0, "no party");
	/* invite a friend to a party: the party is made first */
	row = *friend_named("Node1");
	CHECK(provider->friend_action(SCREENS_ACTION_INVITE_PARTY, &row, answer, sizeof(answer)) && strstr(answer, "PARTY INVITATION SENT"), "invite to the party: %s", answer);
	fake_run(&net, 3000);
	look();
	CHECK(model.party_summary.present && model.party_summary.i_lead && model.party_count == 1 && model.party[0].me && model.party[0].leader, "a party of one, led by this machine");
	CHECK(find_toast(drain_toasts(), SCREENS_TOAST_PARTY_JOINED) >= 0, "you are in a party: a toast");
	/* b gets it as a toast, and accepts */
	use(b);
	count = drain_toasts();
	index = find_toast(count, SCREENS_TOAST_INVITE);
	CHECK(index >= 0 && !strcmp(last_toast[index].text, "NODE0 INVITES YOU TO A PARTY") && !strcmp(last_toast[index].detail, "A party of 1"), "the party's invitation: '%s' / '%s'",
		index >= 0 ? last_toast[index].text : "", index >= 0 ? last_toast[index].detail : "");
	provider->invite_answer(last_toast[index].id, 1);
	fake_run(&net, 5000);
	screens_model_update(&model, 1);
	CHECK(model.party_summary.present && model.party_count == 2 && !model.party_summary.i_lead, "b is in the party: %d members", model.party_count);
	for (index = 0; index < model.party_count; index++)
		CHECK(ascii_clean(model.party[index].name), "member names");
	/* b's actions on its own row: follow off, leave (a member is no leader: no disband) */
	provider->party_actions(&model.party[0], &model.party_summary, actions);
	found = 0;
	for (index = 0; actions[index] != SCREENS_ACTION_NONE; index++)
		found |= 1 << actions[index];
	for (index = 0; index < model.party_count; index++)
	{
		if (model.party[index].me)
		{
			provider->party_actions(&model.party[index], &model.party_summary, actions);
			found = 0;
			for (count = 0; actions[count] != SCREENS_ACTION_NONE; count++)
				found |= 1 << actions[count];
			CHECK((found & (1 << SCREENS_ACTION_FOLLOW_OFF)) && (found & (1 << SCREENS_ACTION_LEAVE_PARTY)) && !(found & (1 << SCREENS_ACTION_DISBAND_PARTY)), "a member's actions: 0x%x", found);
		}
	}
	/* the leader's: its own row offers disband and leave; a member cannot disband (in words) */
	use(b);
	for (index = 0; index < model.party_count; index++)
	{
		if (model.party[index].me)
			CHECK(!provider->party_action(SCREENS_ACTION_DISBAND_PARTY, &model.party[index], answer, sizeof(answer)) && strstr(answer, "ONLY THE LEADER"), "a member cannot disband: %s", answer);
	}
	use(a);
	fake_run(&net, 3000);
	screens_model_update(&model, 1);
	CHECK(model.party_count == 2 && model.party_summary.i_lead, "a leads two");
	for (index = 0; index < model.party_count; index++)
	{
		if (model.party[index].me)
		{
			provider->party_actions(&model.party[index], &model.party_summary, actions);
			found = 0;
			for (count = 0; actions[count] != SCREENS_ACTION_NONE; count++)
				found |= 1 << actions[count];
			CHECK((found & (1 << SCREENS_ACTION_DISBAND_PARTY)) && (found & (1 << SCREENS_ACTION_LEAVE_PARTY)) && (found & (1 << SCREENS_ACTION_FOLLOW_OFF)), "the leader's actions: 0x%x", found);
		}
	}
	for (index = 0; index < model.party_count; index++)
	{
		if (!model.party[index].me)
		{
			CHECK(provider->party_action(SCREENS_ACTION_MAKE_LEADER, &model.party[index], answer, sizeof(answer)), "hand over");
			break;
		}
	}
	fake_run(&net, 3000);
	screens_model_update(&model, 1);
	CHECK(!model.party_summary.i_lead && model.party_count == 2, "b leads now");
	CHECK(provider->party_action(SCREENS_ACTION_FOLLOW_OFF, &model.party[0], answer, sizeof(answer)), "follow off");
	fake_run(&net, 500);
	screens_model_update(&model, 1);
	CHECK(!model.party_summary.follow, "follow is off");
	CHECK(provider->party_action(SCREENS_ACTION_LEAVE_PARTY, &model.party[0], answer, sizeof(answer)), "leave");
	fake_run(&net, 1500);
	screens_model_update(&model, 1);
	CHECK(!model.party_summary.present, "left");
	(void)summary;
	(void)members;
}

/* the roster's flags: a friend in a game, a member of the party, by name; and not others */
static void case_roster_flags(void)
{
	struct fake_node *a, *b, *c;

	begin_world(16, 3);
	a = &net.nodes[0];
	b = &net.nodes[1];
	c = &net.nodes[2];
	link_friends(a, b);
	link_friends(a, c);
	host_place(b, 1, 3);
	/* c is online, in the menus */
	use(a);
	fake_run(&net, 6000);
	CHECK(screens_social_roster_flags("Node1", "bloodgulch") == SCREENS_ROW_FRIEND, "a friend in a game here");
	CHECK(screens_social_roster_flags("Node1", "BloodGulch") == SCREENS_ROW_FRIEND, "the map by any case");
	CHECK(screens_social_roster_flags("Node1", "chillout") == 0, "the same name in another map is no friend");
	CHECK(screens_social_roster_flags("Node1", "") == SCREENS_ROW_FRIEND, "no map named: by name");
	CHECK(screens_social_roster_flags("Node2", "bloodgulch") == 0, "a friend in the menus is not in this game");
	CHECK(screens_social_roster_flags("Nobody", "bloodgulch") == 0 && screens_social_roster_flags("", "bloodgulch") == 0, "a stranger, and no name");
	CHECK(screens_social_roster_friend("Node1", "bloodgulch") != 0 && screens_social_roster_friend("Node2", "bloodgulch") == 0, "the friend's id");
	/* the party: a member that is online */
	social_front_party(a->front, 1, 0, 0, NULL);
	social_front_party(a->front, 2, social_friend_by_key(a->engine, c->engine->identity.key)->id, 0, NULL);
	fake_run(&net, 3000);
	{
		struct screens_toast toast;
		int found = 0;

		use(c);
		while (screens_provider()->toast_pop(&toast))
		{
			if (toast.kind == SCREENS_TOAST_INVITE)
			{
				screens_provider()->invite_answer(toast.id, 1);
				found = 1;
			}
		}
		CHECK(found, "c is invited");
		use(a);
	}
	fake_run(&net, 6000);
	CHECK((screens_social_roster_flags("Node2", "bloodgulch") & SCREENS_ROW_PARTY) != 0, "a party member");
	CHECK((screens_social_roster_flags("Node1", "bloodgulch") & SCREENS_ROW_PARTY) == 0, "and not the others");
	/* off: no flags */
	social_global_front = NULL;
	screens_social_backend_reset();
	CHECK(screens_social_roster_flags("Node1", "bloodgulch") == 0, "off: no flags");
}

/* "online" toasts: held for ten seconds after the friend is seen online, none for one that hides again, one line for a crowd */
static void case_online_toasts(void)
{
	struct fake_node *a, *b;
	unsigned int seen = 0, told = 0;
	int count, step;

	begin_world(17, 2);
	a = &net.nodes[0];
	b = &net.nodes[1];
	link_friends(a, b);
	use(a);
	/* it comes online at the start: the toast comes ten seconds after the friend is seen, not before */
	for (step = 0; step < 100; step++)
	{
		fake_run(&net, 250);
		if (!seen && snap(a)->friend_count == 1 && snap(a)->friends[0].online)
			seen = net.time;
		count = drain_toasts();
		if (!told && find_toast(count, SCREENS_TOAST_FRIEND_ONLINE) >= 0)
			told = net.time;
	}
	CHECK(seen && told, "seen at %u, told at %u", seen, told);
	CHECK(told - seen >= SCREENS_TOAST_ONLINE_HOLD_MILLISECONDS - 600, "held: seen %u, told %u", seen, told);
	CHECK(told - seen <= SCREENS_TOAST_ONLINE_HOLD_MILLISECONDS + 3000, "and not forgotten: seen %u, told %u", seen, told);
	CHECK(drain_toasts() == 0, "once");
	/* it hides within the hold (appears offline): nothing is said */
	social_front_set_share(b->front, 0, SOCIAL_DETAILS_BASIC, 1);
	fake_run(&net, 20000);
	drain_toasts();
	social_front_set_share(b->front, 1, SOCIAL_DETAILS_BASIC, 1);
	fake_run(&net, 3500);
	social_front_set_share(b->front, 0, SOCIAL_DETAILS_BASIC, 1);
	fake_run(&net, 30000);
	count = drain_toasts();
	CHECK(find_toast(count, SCREENS_TOAST_FRIEND_ONLINE) < 0, "gone again within the hold: no toast (%d)", count);
}

/* the layer off: the screens' rows are empty, the service says so, every action refuses in words */
static void case_off(void)
{
	struct screens_provider const *provider;
	struct screens_friend_row row;
	struct screens_toast toast;
	char answer[SCREENS_LINE_SIZE];
	struct screens_friends_summary summary;
	struct screens_friend_row rows[4];

	begin_world(18, 1);
	provider = screens_provider();
	social_global_front = NULL;
	screens_social_backend_reset();
	memset(&row, 0, sizeof(row));
	CHECK(provider->friends(SCREENS_TAB_FRIENDS, rows, 4, &summary) == 0 && summary.service == SCREENS_SERVICE_DISABLED && !summary.own_code[0], "off: no friends, service disabled");
	CHECK(provider->party(NULL, 0, NULL) == 0, "off: no party");
	CHECK(!provider->toast_pop(&toast), "off: no toast");
	CHECK(!provider->friend_add("aaaa aaaa aaaa a", answer, sizeof(answer)) && strstr(answer, "FRIENDS ARE OFF"), "off: add: %s", answer);
	CHECK(!provider->friend_action(SCREENS_ACTION_INVITE_GAME, &row, answer, sizeof(answer)) && strstr(answer, "FRIENDS ARE OFF"), "off: invite: %s", answer);
	CHECK(!provider->party_action(SCREENS_ACTION_LEAVE_PARTY, NULL, answer, sizeof(answer)) && strstr(answer, "FRIENDS ARE OFF"), "off: party: %s", answer);
	provider->invite_answer(5, 1);
	CHECK(world.begun == 0, "off: an answer does nothing");
	/* a layer that is on but has made no snapshot yet */
	CHECK(provider->generation() == provider->generation(), "the generation is steady with nothing changing");
}

/* the model rebuilds when the backend's generation moves, and not when it does not */
static void case_generation(void)
{
	struct fake_node *a, *b;
	struct screens_provider const *provider;
	char answer[SCREENS_LINE_SIZE];
	unsigned long before;

	begin_world(19, 2);
	a = &net.nodes[0];
	b = &net.nodes[1];
	provider = screens_provider();
	link_friends(a, b);
	use(a);
	fake_run(&net, 6000);
	look();
	before = model.generation;
	look();
	CHECK(model.generation == before, "steady");
	{
		struct screens_friend_row row = *friend_named("Node1");

		CHECK(provider->friend_action(SCREENS_ACTION_VIEW_CODE, &row, answer, sizeof(answer)), "an action");
	}
	look();
	CHECK(model.generation != before, "an action of ours moves the generation");
	before = model.generation;
	host_place(b, 1, 5);
	fake_run(&net, 6000);
	look();
	CHECK(model.generation != before && friend_named("Node1")->players == 5, "a change in the layer moves it");
}

/* text from the layer, whatever it holds, comes out clean and short */
static void case_clean_text(void)
{
	struct fake_node *a, *b;
	struct screens_provider const *provider;
	struct screens_friend_row row;
	char answer[SCREENS_LINE_SIZE];
	int index, count;

	begin_world(20, 2);
	a = &net.nodes[0];
	b = &net.nodes[1];
	provider = screens_provider();
	link_friends(a, b);
	use(a);
	fake_run(&net, 6000);
	look();
	row = *friend_named("Node1");
	for (index = SCREENS_ACTION_MUTE; index < SCREENS_ACTIONS; index++)
	{
		answer[0] = 0;
		provider->friend_action(index, &row, answer, sizeof(answer));
		CHECK(ascii_clean(answer) && strlen(answer) < SCREENS_LINE_SIZE, "action %d answer '%s'", index, answer);
		answer[0] = 0;
		provider->party_action(index, &model.party[0], answer, sizeof(answer));
		CHECK(ascii_clean(answer) && strlen(answer) < SCREENS_LINE_SIZE, "party action %d answer '%s'", index, answer);
		answer[0] = 0;
		provider->roster_action(index, &(struct screens_roster_row){ .name = "Node1", .machine = 3 }, answer, sizeof(answer));
		CHECK(ascii_clean(answer) && strlen(answer) < SCREENS_LINE_SIZE, "roster action %d answer '%s'", index, answer);
	}
	count = drain_toasts();
	(void)count;
	/* the roster actions only a friend's row can do */
	CHECK(!provider->roster_action(SCREENS_ACTION_ADD_FRIEND, &(struct screens_roster_row){ .name = "Stranger" }, answer, sizeof(answer)) && strstr(answer, "CODE"), "a stranger's code is not known: %s", answer);
	CHECK(!provider->roster_action(SCREENS_ACTION_INVITE_PARTY, &(struct screens_roster_row){ .name = "Stranger" }, answer, sizeof(answer)) && strstr(answer, "ONLY A FRIEND"), "only a friend: %s", answer);
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "befriend-and-invite"))
		case_befriend_and_invite();
	else if (!strcmp(name, "join-words"))
		case_join_words();
	else if (!strcmp(name, "invite-in-game"))
		case_invite_in_game();
	else if (!strcmp(name, "actions"))
		case_actions();
	else if (!strcmp(name, "party"))
		case_party();
	else if (!strcmp(name, "roster-flags"))
		case_roster_flags();
	else if (!strcmp(name, "online-toasts"))
		case_online_toasts();
	else if (!strcmp(name, "off"))
		case_off();
	else if (!strcmp(name, "generation"))
		case_generation();
	else if (!strcmp(name, "clean-text"))
		case_clean_text();
	else
	{
		fprintf(stderr, "no case named %s\n", name);
		return 2;
	}
	return 0;
}
