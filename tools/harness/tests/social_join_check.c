/* social_join_check.c: inviting a friend to a hosted game, the invites that arrive, and joining a friend's game with its failures in plain words
(port/linux/game/social_invite.c, social_join.c) on the fake network of social_fake.h, with a stand-in for internet play's join. Cases are named on the command
line (test_social_join.py). */

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
#include "social_command.c"
#include "social_fake.h"

#include <time.h>

/* (p2p.c's, which is not linked: the monotonic milliseconds, for `social timing`) */
unsigned long p2p_now(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long)(now.tv_sec * 1000UL + now.tv_nsec / 1000000UL);
}

static unsigned long long test_state = 0x77ULL;

static void fill(unsigned char *bytes, int size)
{
	int index;

	for (index = 0; index < size; index++)
		bytes[index] = (unsigned char)(fake_random(&test_state) >> 20);
}

static struct social_snapshot snapshots[2];

static const struct social_snapshot *snap(struct fake_node *node, int slot)
{
	social_front_snapshot(node->front, &snapshots[slot]);
	return &snapshots[slot];
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

static const struct social_friend_info *info_of(struct fake_node *node, struct fake_node *other, int slot)
{
	const struct social_snapshot *snapshot = snap(node, slot);
	int index;

	for (index = 0; index < snapshot->friend_count; index++)
	{
		if (snapshot->friends[index].id == friend_id(node, other))
			return &snapshot->friends[index];
	}
	return NULL;
}

static int take_events(struct fake_node *node, int type, struct social_api_event *last)
{
	struct social_api_event event;
	int count = 0;

	while (social_front_event(node->front, &event))
	{
		if (event.type == type)
		{
			count++;
			if (last)
				*last = event;
		}
	}
	return count;
}

/* a place that hosts a game friends can join */
static void host_place(struct fake_node *node, int joinable, int players)
{
	struct social_place place;

	memset(&place, 0, sizeof(place));
	place.activity = SOCIAL_GAME;
	place.hosting = 1;
	place.has_invite = 1;
	place.joinable = (unsigned char)joinable;
	strcpy(place.map, "bloodgulch");
	strcpy(place.gametype, "Slayer");
	place.players = (unsigned char)players;
	place.maximum_players = 16;
	place.engine_type = 2;
	memset(place.invite_hash, 0x5A, 16);
	memset(place.invite_token, 0xA5, 16);
	social_front_set_place(node->front, &place);
}

/* ---------- invites */

static void case_invite(void)
{
	static struct fake_net net;
	struct fake_node *a, *b, *c;
	struct social_api_event event;
	const struct social_snapshot *snapshot;
	int index;

	fake_start(&net, 41);
	a = fake_add(&net, 2, 1);
	b = fake_add(&net, -2, 1);
	c = fake_add(&net, 5, 1);
	link_friends(a, b);
	link_friends(a, c);
	fake_run(&net, 6000);
	social_front_invite(a->front, friend_id(a, b));
	fake_run(&net, 300);
	CHECK(snap(a, 0)->invite_result == SOCIAL_INVITE_RESULT_NO_GAME, "no game to invite to: %d", snap(a, 0)->invite_result);
	host_place(a, 0, 3);
	fake_run(&net, 6000);
	social_front_invite(a->front, 12345);
	fake_run(&net, 300);
	CHECK(snap(a, 0)->invite_result == SOCIAL_INVITE_RESULT_NO_SUCH_FRIEND, "no such friend");
	social_front_invite(a->front, friend_id(a, b));
	fake_run(&net, 2000);
	CHECK(snap(a, 0)->invite_result == SOCIAL_INVITE_RESULT_SENT, "sent");
	snapshot = snap(b, 0);
	CHECK(snapshot->invite_count == 1, "b has an invite: %d", snapshot->invite_count);
	CHECK(snapshot->invites[0].kind == 1 && !strcmp(snapshot->invites[0].name, "Node0") && !strcmp(snapshot->invites[0].target.map, "bloodgulch") &&
		!strcmp(snapshot->invites[0].target.gametype, "Slayer") && snapshot->invites[0].target.players == 3 && snapshot->invites[0].target.maximum_players == 16 &&
		snapshot->invites[0].target.network_version == 41 && snapshot->invites[0].target.host_hash[0] == 0x5A && snapshot->invites[0].target.token[15] == 0xA5 &&
		snapshot->invites[0].target.invited, "the invite's contents");
	CHECK(take_events(b, SOCIAL_API_EVENT_INVITE, &event) == 1 && event.id == snapshot->invites[0].id, "an invite event");
	CHECK(snap(c, 0)->invite_count == 0, "c, not invited, has none");
	/* inviting again at once: too fast; after ten seconds, the same game renews the one held */
	social_front_invite(a->front, friend_id(a, b));
	fake_run(&net, 500);
	CHECK(snap(a, 0)->invite_result == SOCIAL_INVITE_RESULT_TOO_FAST, "too fast");
	fake_run(&net, 10000);
	social_front_invite(a->front, friend_id(a, b));
	fake_run(&net, 1000);
	CHECK(snap(a, 0)->invite_result == SOCIAL_INVITE_RESULT_SENT && snap(b, 0)->invite_count == 1, "the same game again renews the invite");
	/* it lapses in a minute */
	fake_run(&net, 61000);
	CHECK(snap(b, 0)->invite_count == 0, "lapsed");
	/* a friend offline cannot be invited */
	c->alive = 0;
	fake_run(&net, 160000);
	social_front_invite(a->front, friend_id(a, c));
	fake_run(&net, 500);
	CHECK(snap(a, 0)->invite_result == SOCIAL_INVITE_RESULT_OFFLINE, "offline");
	/* dismissed */
	fake_run(&net, 11000);
	social_front_invite(a->front, friend_id(a, b));
	fake_run(&net, 1500);
	CHECK(snap(b, 0)->invite_count == 1, "again");
	social_front_invite_dismiss(b->front, snap(b, 0)->invites[0].id);
	fake_run(&net, 300);
	CHECK(snap(b, 0)->invite_count == 0, "dismissed");
	/* three from one friend at most: games with different tokens */
	for (index = 0; index < 5; index++)
	{
		struct social_place place = a->engine->place;

		memset(place.invite_token, 0x10 + index, 16);
		social_front_set_place(a->front, &place);
		fake_run(&net, 200);
		a->engine->friends[0].invite_valid = 0;
		a->engine->friends[1].invite_valid = 0;
		social_front_invite(a->front, friend_id(a, b));
		fake_run(&net, 1500);
	}
	CHECK(snap(b, 0)->invite_count == SOCIAL_MAX_INVITES, "three at most from one friend: %d", snap(b, 0)->invite_count);
	{
		int newest = 0;

		for (index = 0; index < snap(b, 0)->invite_count; index++)
			newest += snap(b, 0)->invites[index].target.token[0] == 0x14;
		CHECK(newest == 1, "the newest is kept");
	}
}

/* a stranger who holds no pair key cannot invite; an invite replayed is dropped; hostile bytes are not an invite */
static void case_invite_abuse(void)
{
	static struct fake_net net;
	struct fake_node *a, *b, *s;
	unsigned char plain[SOCIAL_MAX_PLAIN], nonce[12], wire[SOCIAL_MAX_WIRE];
	struct social_invite invite;
	int size, wire_size;
	unsigned char fake_pair[32], fake_tag[8];
	unsigned int replays;

	fake_start(&net, 42);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	s = fake_add(&net, 0, 1);
	link_friends(a, b);
	fake_run(&net, 6000);
	memset(&invite, 0, sizeof(invite));
	invite.kind = 1;
	memset(invite.target.token, 7, 16);
	strcpy(invite.target.map, "hacked");
	size = social_invite_encode(1, 1, &invite, plain, sizeof(plain));
	fill(fake_pair, 32);
	fill(fake_tag, 8);
	fill(nonce, 12);
	/* sealed with a key a does not share with s, on a's presence topic, with a guessed tag */
	wire_size = social_seal(fake_pair, SOCIAL_KIND_DIRECT, fake_tag, a->engine->circle_topic, plain, size, nonce, wire, sizeof(wire));
	social_front_heard(b->front, a->engine->circle_topic, wire, wire_size);
	/* with the right tag but the wrong key */
	{
		struct social_friend *friend = social_friend_by_key(b->engine, a->engine->identity.key);

		wire_size = social_seal(fake_pair, SOCIAL_KIND_DIRECT, friend->tag_in, a->engine->circle_topic, plain, size, nonce, wire, sizeof(wire));
		social_front_heard(b->front, a->engine->circle_topic, wire, wire_size);
		/* with the pair key, a real invite, twice */
		fill(nonce, 12);
		wire_size = social_seal(friend->pair_key, SOCIAL_KIND_DIRECT, friend->tag_in, a->engine->circle_topic, plain, size, nonce, wire, sizeof(wire));
		social_front_heard(b->front, a->engine->circle_topic, wire, wire_size);
		social_front_heard(b->front, a->engine->circle_topic, wire, wire_size);
	}
	fake_run(&net, 500);
	CHECK(snap(b, 0)->invite_count == 0, "an invite with a window number a had not used is accepted only from a real one: the generation 1, sequence 1 is old");
	(void)s;
	/* an invite with the real numbers (above the window) and the real key is taken, once */
	{
		struct social_friend *friend = social_friend_by_key(b->engine, a->engine->identity.key);
		unsigned int generation = friend->window.generation + 1;

		size = social_invite_encode(generation, 5, &invite, plain, sizeof(plain));
		fill(nonce, 12);
		wire_size = social_seal(friend->pair_key, SOCIAL_KIND_DIRECT, friend->tag_in, a->engine->circle_topic, plain, size, nonce, wire, sizeof(wire));
		social_front_heard(b->front, a->engine->circle_topic, wire, wire_size);
		replays = b->engine->statistics.dropped_window;
		fill(nonce, 12);
		wire_size = social_seal(friend->pair_key, SOCIAL_KIND_DIRECT, friend->tag_in, a->engine->circle_topic, plain, size, nonce, wire, sizeof(wire));
		social_front_heard(b->front, a->engine->circle_topic, wire, wire_size);
		CHECK(b->engine->statistics.dropped_window == replays + 1, "the same numbers again, under a new nonce, are dropped by the window");
	}
	fake_run(&net, 500);
	CHECK(snap(b, 0)->invite_count == 1 && !strcmp(snap(b, 0)->invites[0].target.map, "hacked"), "a real invite, from a friend, is taken (what it says is what its sender wrote)");
}

/* the friend's presence shows a game that can be joined, and its target */
static void case_joinable_presence(void)
{
	static struct fake_net net;
	struct fake_node *a, *b;
	const struct social_friend_info *info;

	fake_start(&net, 43);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	link_friends(a, b);
	host_place(a, 1, 4);
	fake_run(&net, 6000);
	info = info_of(b, a, 0);
	CHECK(info && info->online && info->joinable && info->hosting && info->activity == SOCIAL_ACTIVITY_GAME && !strcmp(info->map, "bloodgulch") && info->players == 4 &&
		info->maximum_players == 16 && info->join.host_hash[0] == 0x5A && info->join.token[0] == 0xA5 && info->join.friend_id == info->id && !info->join.invited,
		"joinable, with its target");
	/* the game shown to a friend and not joinable: hosting, no target */
	host_place(a, 0, 4);
	fake_run(&net, 5000);
	info = info_of(b, a, 0);
	CHECK(info && info->hosting && !info->joinable, "hosting, not joinable");
	CHECK(info->join.token[0] == 0, "and no token is shown");
	/* the game ends */
	{
		struct social_place place;

		memset(&place, 0, sizeof(place));
		place.activity = SOCIAL_MENUS;
		social_front_set_place(a->front, &place);
		fake_run(&net, 5000);
		info = info_of(b, a, 0);
		CHECK(info && !info->hosting && !info->joinable && info->activity == SOCIAL_ACTIVITY_MENUS, "back in the menus");
	}
}

/* ---------- the join machine, against a stand-in for internet play */

struct script
{
	unsigned int clock;
	int version;
	int busy;
	int together;
	int map_state;
	int begin_ok;
	/* the progress: a list of (stage, reason) given one a call */
	int steps[8][2];
	const char *words[8];
	int step_count, step;
	int game_ready;
	int cancelled;
	int strict;
	int begun;
	/* the friend as it is now */
	int friend_online;
	int friend_joinable;
	struct social_join_target friend_target;
};

static unsigned int script_now(void *user)
{
	return ((struct script *)user)->clock;
}

static int script_version(void *user)
{
	return ((struct script *)user)->version;
}

static int script_busy(void *user)
{
	return ((struct script *)user)->busy;
}

static int script_together(void *user, const unsigned char *hash)
{
	(void)hash;
	return ((struct script *)user)->together;
}

static int script_map_state(void *user, const char *map)
{
	(void)map;
	return ((struct script *)user)->map_state;
}

static int script_friend_now(void *user, unsigned int id, int *online, struct social_join_target *target)
{
	struct script *script = user;

	(void)id;
	*online = script->friend_online;
	if (script->friend_online && script->friend_joinable)
	{
		*target = script->friend_target;
		return 1;
	}
	return 0;
}

static int script_begin(void *user, const unsigned char *hash, const unsigned char *token)
{
	struct script *script = user;

	(void)hash;
	(void)token;
	script->begun++;
	script->step = 0;
	return script->begin_ok;
}

static int script_progress(void *user, int *stage, char *reason, int size)
{
	struct script *script = user;
	int step = script->step < script->step_count ? script->step : script->step_count - 1;

	snprintf(reason, (size_t)size, "%s", script->words[step] ? script->words[step] : "");
	*stage = script->steps[step][0];
	if (script->step < script->step_count - 1)
		script->step++;
	return *stage;
}

static int script_game_ready(void *user, const unsigned char *hash)
{
	(void)hash;
	return ((struct script *)user)->game_ready;
}

static void script_cancel(void *user)
{
	((struct script *)user)->cancelled++;
}

static int script_strict(void *user)
{
	return ((struct script *)user)->strict;
}

static void script_start(struct script *script, struct social_join_machine *machine)
{
	struct social_join_backend backend;

	memset(script, 0, sizeof(*script));
	script->version = 41;
	script->begin_ok = 1;
	script->game_ready = 1;
	script->friend_online = 1;
	script->friend_joinable = 1;
	script->step_count = 1;
	script->steps[0][0] = P2P_JOIN_ASKING;
	script->friend_target.network_version = 41;
	memset(script->friend_target.host_hash, 0x5A, 16);
	memset(script->friend_target.token, 0xA5, 16);
	strcpy(script->friend_target.map, "bloodgulch");
	strcpy(script->friend_target.gametype, "Slayer");
	script->friend_target.friend_id = 77;
	script->friend_target.players = 3;
	script->friend_target.maximum_players = 16;
	memset(&backend, 0, sizeof(backend));
	backend.user = script;
	backend.now = script_now;
	backend.version = script_version;
	backend.busy = script_busy;
	backend.in_game_of = script_together;
	backend.map_state = script_map_state;
	backend.friend_now = script_friend_now;
	backend.begin = script_begin;
	backend.progress = script_progress;
	backend.game_ready = script_game_ready;
	backend.cancel = script_cancel;
	backend.strict_nat = script_strict;
	social_join_machine_init(machine, &backend);
}

static void run_updates(struct script *script, struct social_join_machine *machine, int count, unsigned int step_ms)
{
	int index;

	for (index = 0; index < count; index++)
	{
		script->clock += step_ms;
		social_join_machine_update(machine);
	}
}

static void check_text(const struct social_join_status *status)
{
	int index;

	CHECK(status->text[0], "a sentence");
	for (index = 0; status->text[index]; index++)
		CHECK(status->text[index] >= 0x20 && status->text[index] <= 0x7E && status->text[index] != '|', "plain text");
}

static void case_join_ok(void)
{
	struct script script;
	struct social_join_machine machine;
	struct social_join_status status;

	script_start(&script, &machine);
	script.step_count = 4;
	script.steps[0][0] = P2P_JOIN_ASKING;
	script.steps[1][0] = P2P_JOIN_ANSWERED;
	script.steps[2][0] = P2P_JOIN_CONNECTED;
	script.steps[3][0] = P2P_JOIN_CONNECTED;
	script.game_ready = 0;
	CHECK(social_join_machine_friend(&machine, 77) == SOCIAL_JOIN_REASON_NONE, "starts");
	social_join_machine_status(&machine, &status);
	CHECK(status.state == SOCIAL_JOIN_WORKING && script.begun == 1 && !strcmp(status.text, "Joining") && !status.map_download, "working");
	run_updates(&script, &machine, 1, 100);
	run_updates(&script, &machine, 1, 100);
	social_join_machine_status(&machine, &status);
	/* the tunnel reaches the host, and the host's game is not in the list yet */
	run_updates(&script, &machine, 3, 100);
	social_join_machine_status(&machine, &status);
	CHECK(status.state == SOCIAL_JOIN_WORKING, "waiting for the game to show");
	script.game_ready = 1;
	run_updates(&script, &machine, 1, 100);
	social_join_machine_status(&machine, &status);
	CHECK(status.state == SOCIAL_JOIN_READY && status.reason == SOCIAL_JOIN_REASON_NONE, "ready");
	social_join_machine_done(&machine);
	social_join_machine_status(&machine, &status);
	CHECK(status.state == SOCIAL_JOIN_IDLE, "idle again");
}

/* every refusal, with the word and the sentence */
static void case_join_failures(void)
{
	static const struct
	{
		const char *progress_word;
		int reason;
		const char *word;
		const char *sentence;
	} cases[] = {
		{ "full", SOCIAL_JOIN_REASON_FULL, "full", "The game is full" },
		{ "closed", SOCIAL_JOIN_REASON_CLOSED, "closed", "The game is closed to new players" },
		{ "banned", SOCIAL_JOIN_REASON_BANNED, "banned", "You are banned from that game" },
		{ "password", SOCIAL_JOIN_REASON_PASSWORD, "password", "That game needs a password" },
		{ "version", SOCIAL_JOIN_REASON_VERSION, "version", "Your friend plays another version of the game" },
		{ "no_answer", SOCIAL_JOIN_REASON_NO_ANSWER, "no_answer", "The game did not answer; it may have ended" },
		{ "no_route", SOCIAL_JOIN_REASON_NO_ROUTE, "no_route", "Could not connect to the game" },
		{ "name", SOCIAL_JOIN_REASON_REFUSED, "refused", "The game would not take you" },
		{ "vpn", SOCIAL_JOIN_REASON_REFUSED, "refused", "The game would not take you" },
		{ "address_full", SOCIAL_JOIN_REASON_FULL, "full", "The game is full" },
	};
	int index;

	for (index = 0; index < (int)(sizeof(cases) / sizeof(*cases)); index++)
	{
		struct script script;
		struct social_join_machine machine;
		struct social_join_status status;

		script_start(&script, &machine);
		script.step_count = 2;
		script.steps[0][0] = P2P_JOIN_ASKING;
		script.steps[1][0] = P2P_JOIN_FAILED;
		script.words[1] = cases[index].progress_word;
		CHECK(social_join_machine_friend(&machine, 77) == SOCIAL_JOIN_REASON_NONE, "starts");
		run_updates(&script, &machine, 3, 100);
		social_join_machine_status(&machine, &status);
		CHECK(status.state == SOCIAL_JOIN_FAILED && status.reason == cases[index].reason, "%s: reason %d", cases[index].progress_word, status.reason);
		CHECK(!strcmp(social_join_reason_word(status.reason), cases[index].word), "word %s", social_join_reason_word(status.reason));
		CHECK(!strcmp(status.text, cases[index].sentence), "sentence '%s'", status.text);
		check_text(&status);
	}
	/* no route on a strict NAT says so */
	{
		struct script script;
		struct social_join_machine machine;
		struct social_join_status status;

		script_start(&script, &machine);
		script.step_count = 2;
		script.steps[0][0] = P2P_JOIN_ASKING;
		script.steps[1][0] = P2P_JOIN_FAILED;
		script.words[1] = "no_route";
		script.strict = 1;
		social_join_machine_friend(&machine, 77);
		run_updates(&script, &machine, 3, 100);
		social_join_machine_status(&machine, &status);
		CHECK(status.reason == SOCIAL_JOIN_REASON_STRICT_NAT && strstr(status.text, "too strict"), "strict NAT: %s", status.text);
	}
}

/* what is known before trying */
static void case_join_checks(void)
{
	struct script script;
	struct social_join_machine machine;
	struct social_join_status status;
	int reason;

	/* another version */
	script_start(&script, &machine);
	script.friend_target.network_version = 40;
	reason = social_join_machine_friend(&machine, 77);
	social_join_machine_status(&machine, &status);
	CHECK(reason == SOCIAL_JOIN_REASON_VERSION && status.state == SOCIAL_JOIN_FAILED && script.begun == 0 && !strcmp(social_join_reason_word(reason), "version"), "version");
	check_text(&status);
	/* the map is missing: the words name it */
	script_start(&script, &machine);
	script.map_state = 2;
	reason = social_join_machine_friend(&machine, 77);
	social_join_machine_status(&machine, &status);
	CHECK(reason == SOCIAL_JOIN_REASON_MAP_MISSING && !strcmp(social_join_reason_word(reason), "wrong_map") && !strcmp(status.text, "You do not have the map bloodgulch") &&
		script.begun == 0, "map missing: %s", status.text);
	/* the map downloads as it joins: goes on, and says so */
	script_start(&script, &machine);
	script.map_state = 1;
	reason = social_join_machine_friend(&machine, 77);
	social_join_machine_status(&machine, &status);
	CHECK(reason == SOCIAL_JOIN_REASON_NONE && status.state == SOCIAL_JOIN_WORKING && status.map_download && strstr(status.text, "bloodgulch") && script.begun == 1, "download");
	check_text(&status);
	/* a game of this machine's own, or the friend's already */
	script_start(&script, &machine);
	script.busy = 1;
	CHECK(social_join_machine_friend(&machine, 77) == SOCIAL_JOIN_REASON_BUSY && script.begun == 0, "busy");
	script_start(&script, &machine);
	script.together = 1;
	CHECK(social_join_machine_friend(&machine, 77) == SOCIAL_JOIN_REASON_TOGETHER, "together");
	/* the friend is offline, or online and in no game */
	script_start(&script, &machine);
	script.friend_online = 0;
	CHECK(social_join_machine_friend(&machine, 77) == SOCIAL_JOIN_REASON_OFFLINE && script.begun == 0, "offline");
	script_start(&script, &machine);
	script.friend_joinable = 0;
	CHECK(social_join_machine_friend(&machine, 77) == SOCIAL_JOIN_REASON_NOT_JOINABLE, "not joinable");
	/* an invite with no token, one p2p does not take */
	script_start(&script, &machine);
	memset(script.friend_target.token, 0, 16);
	CHECK(social_join_machine_friend(&machine, 77) == SOCIAL_JOIN_REASON_BAD_INVITE, "no token");
	script_start(&script, &machine);
	script.begin_ok = 0;
	CHECK(social_join_machine_friend(&machine, 77) == SOCIAL_JOIN_REASON_BAD_INVITE, "p2p refused the invite");
	/* every sentence of every reason is plain ASCII, and every word is different */
	{
		int index, other;

		for (index = 0; index < NUMBER_OF_SOCIAL_JOIN_REASONS; index++)
		{
			const char *text = social_join_reason_text(index, "map");
			int i;

			CHECK(text && text[0] && strcmp(text, "Could not join") != 0 || index == 0, "reason %d has words", index);
			for (i = 0; text[i]; i++)
				CHECK(text[i] >= 0x20 && text[i] <= 0x7E && text[i] != '|', "plain");
			for (other = 0; other < index; other++)
				CHECK(strcmp(social_join_reason_word(index), social_join_reason_word(other)), "words differ: %d %d", index, other);
		}
		CHECK(!strcmp(social_join_reason_text(999, NULL), "Could not join") && !strcmp(social_join_reason_word(-1), "unknown"), "unknown reasons");
	}
}

/* the friend leaves the game while it is being asked; a replaced invite; cancel; timeout */
static void case_join_left(void)
{
	struct script script;
	struct social_join_machine machine;
	struct social_join_status status;

	/* the friend's game gone from its presence */
	script_start(&script, &machine);
	CHECK(social_join_machine_friend(&machine, 77) == SOCIAL_JOIN_REASON_NONE, "starts");
	run_updates(&script, &machine, 5, 100);
	script.friend_joinable = 0;
	run_updates(&script, &machine, 12, 100);
	social_join_machine_status(&machine, &status);
	CHECK(status.state == SOCIAL_JOIN_FAILED && status.reason == SOCIAL_JOIN_REASON_LEFT && script.cancelled == 1 && !strcmp(status.text, "Your friend has left that game"), "left");
	/* the friend offline */
	script_start(&script, &machine);
	social_join_machine_friend(&machine, 77);
	script.friend_online = 0;
	run_updates(&script, &machine, 12, 100);
	social_join_machine_status(&machine, &status);
	CHECK(status.reason == SOCIAL_JOIN_REASON_LEFT, "left when offline");
	/* an invite received: the sender's presence does not matter */
	script_start(&script, &machine);
	script.friend_target.invited = 1;
	social_join_machine_target(&machine, &script.friend_target);
	script.friend_online = 0;
	run_updates(&script, &machine, 20, 100);
	social_join_machine_status(&machine, &status);
	CHECK(status.state == SOCIAL_JOIN_WORKING, "an invite is joined whether or not the friend is shown online");
	/* the invite replaced: the friend shows a new one, and it is tried once */
	script_start(&script, &machine);
	script.step_count = 2;
	script.steps[0][0] = P2P_JOIN_ASKING;
	script.steps[1][0] = P2P_JOIN_FAILED;
	script.words[1] = "token";
	social_join_machine_friend(&machine, 77);
	memset(script.friend_target.token, 0xB6, 16);
	run_updates(&script, &machine, 3, 100);
	social_join_machine_status(&machine, &status);
	CHECK(status.state == SOCIAL_JOIN_WORKING && script.begun == 2 && status.target.token[0] == 0xB6 && status.serial == 1, "retried with the new invite once");
	/* ... and the new invite replaced again: not a third time */
	memset(script.friend_target.token, 0xC7, 16);
	run_updates(&script, &machine, 25, 100);
	social_join_machine_status(&machine, &status);
	CHECK(status.state == SOCIAL_JOIN_FAILED && status.reason == SOCIAL_JOIN_REASON_LEFT, "once only: %d", status.state);
	/* ... and if it shows none, the game is gone */
	script_start(&script, &machine);
	script.step_count = 2;
	script.steps[0][0] = P2P_JOIN_ASKING;
	script.steps[1][0] = P2P_JOIN_FAILED;
	script.words[1] = "token";
	social_join_machine_friend(&machine, 77);
	run_updates(&script, &machine, 2, 100);
	social_join_machine_status(&machine, &status);
	CHECK(status.state == SOCIAL_JOIN_FAILED && status.reason == SOCIAL_JOIN_REASON_LEFT && script.begun == 1, "no new invite: left");
	/* the invite replaced while the host is only asked (it never says): seen in the friend's presence, tried once */
	script_start(&script, &machine);
	social_join_machine_friend(&machine, 77);
	memset(script.friend_target.token, 0xD8, 16);
	run_updates(&script, &machine, 15, 100);
	social_join_machine_status(&machine, &status);
	CHECK(status.state == SOCIAL_JOIN_WORKING && script.begun == 2 && status.target.token[0] == 0xD8, "the new invite seen in presence is tried");
	memset(script.friend_target.token, 0xE9, 16);
	run_updates(&script, &machine, 15, 100);
	social_join_machine_status(&machine, &status);
	CHECK(status.state == SOCIAL_JOIN_FAILED && status.reason == SOCIAL_JOIN_REASON_LEFT && script.begun == 2, "and not again");
	/* cancel */
	script_start(&script, &machine);
	social_join_machine_friend(&machine, 77);
	social_join_machine_cancel(&machine);
	social_join_machine_status(&machine, &status);
	CHECK(status.state == SOCIAL_JOIN_FAILED && status.reason == SOCIAL_JOIN_REASON_CANCELLED && script.cancelled == 1, "cancelled");
	/* a join that goes on and on */
	script_start(&script, &machine);
	social_join_machine_friend(&machine, 77);
	run_updates(&script, &machine, 100, 1000);
	social_join_machine_status(&machine, &status);
	CHECK(status.state == SOCIAL_JOIN_FAILED && status.reason == SOCIAL_JOIN_REASON_TIMEOUT && script.cancelled == 1, "timeout");
	/* the join of another game while one goes on cancels the first */
	script_start(&script, &machine);
	social_join_machine_friend(&machine, 77);
	social_join_machine_friend(&machine, 77);
	CHECK(script.cancelled == 1 && script.begun == 2 && machine.serial == 2, "a second join replaces the first");
}

/* the machine reading the real front's snapshot: a friend starts and ends a game while this one asks for it */
static struct social_front *global_front;

static int net_friend_now(void *user, unsigned int id, int *online, struct social_join_target *target)
{
	static struct social_snapshot snapshot;
	int index;

	(void)user;
	*online = 0;
	social_front_snapshot(global_front, &snapshot);
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

static void case_join_through_the_network(void)
{
	static struct fake_net net;
	struct fake_node *a, *b;
	struct script script;
	struct social_join_machine machine;
	struct social_join_status status;
	struct social_join_backend backend;
	int index;

	fake_start(&net, 44);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	link_friends(a, b);
	host_place(a, 1, 2);
	fake_run(&net, 6000);
	global_front = b->front;
	script_start(&script, &machine);
	backend = machine.backend;
	backend.friend_now = net_friend_now;
	social_join_machine_init(&machine, &backend);
	CHECK(social_join_machine_friend(&machine, friend_id(b, a)) == SOCIAL_JOIN_REASON_NONE, "the join started from the friend's presence");
	/* the host never answers; its presence says it has gone */
	{
		struct social_place place;

		memset(&place, 0, sizeof(place));
		place.activity = SOCIAL_MENUS;
		social_front_set_place(a->front, &place);
	}
	for (index = 0; index < 100 && machine.state == SOCIAL_JOIN_WORKING; index++)
	{
		fake_run(&net, 100);
		script.clock = net.time;
		social_join_machine_update(&machine);
	}
	social_join_machine_status(&machine, &status);
	CHECK(status.state == SOCIAL_JOIN_FAILED && status.reason == SOCIAL_JOIN_REASON_LEFT && script.cancelled == 1, "the friend's game ended while it was asked for: %d", status.reason);
}

/* ---------- the commands */

static const char *say_line(const char *line)
{
	static char reply[2048];

	social_command(line, reply, sizeof(reply));
	return reply;
}

static void case_commands(void)
{
	static struct fake_net net;
	struct fake_node *a, *b;
	struct script script;
	struct social_join_machine machine;
	char code_b[32];
	char line[128];
	int round;

	fake_start(&net, 45);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	script_start(&script, &machine);
	social_join_set_backend(&machine.backend);
	social_global_front = a->front;
	fake_run(&net, 500);
	CHECK(strstr(say_line("code"), a->engine->identity.code), "code: %s", say_line("code"));
	CHECK(strstr(say_line("help"), "party") && strstr(say_line("nonsense"), "no such command"), "help");
	CHECK(strstr(say_line("status"), "4 brokers"), "status: %s", say_line("status"));
	snprintf(code_b, sizeof(code_b), "%s", b->engine->identity.code);
	snprintf(line, sizeof(line), "add %s", code_b);
	CHECK(strstr(say_line(line), "Request sent"), "add");
	CHECK(strstr(say_line("add 1234"), "not a friend code") && strstr(say_line("add AAAA-AAAA-AAAA-A"), "mistake"), "bad codes");
	fake_run(&net, 2000);
	CHECK(strstr(say_line("sent"), code_b), "waiting");
	social_front_accept(b->front, snap(b, 0)->requests[0].id);
	fake_run(&net, 15000);
	CHECK(strstr(say_line("list"), "1 friends") && strstr(say_line("list"), "Node1") && strstr(say_line("list"), "online"), "list: %s", say_line("list"));
	CHECK(strstr(say_line("nick 1 Bee"), "done"), "nick");
	fake_run(&net, 300);
	CHECK(strstr(say_line("list"), "aka Bee"), "the nickname");
	CHECK(strstr(say_line("invite 1"), "asked") && strstr(say_line("activity game"), "done"), "invite");
	fake_run(&net, 500);
	CHECK(strstr(say_line("last"), "You are not hosting"), "last: %s", say_line("last"));
	CHECK(strstr(say_line("party make"), "asked") && strstr(say_line("party"), "no party"), "party");
	fake_run(&net, 300);
	CHECK(strstr(say_line("party"), "party of 1, you lead"), "party: %s", say_line("party"));
	CHECK(strstr(say_line("party chat hi there"), "asked"), "chat");
	fake_run(&net, 300);
	CHECK(strstr(say_line("party"), "chat Node0: hi there"), "chat shown");
	CHECK(strstr(say_line("joinstatus"), "idle") && strstr(say_line("join 9"), "no such command"), "join");
	CHECK(strstr(say_line("timing"), "ed25519 verify"), "timing");
	CHECK(strstr(say_line("details none"), "done") && strstr(say_line("share offline"), "done") && strstr(say_line("requests off"), "done"), "settings");
	fake_run(&net, 300);
	CHECK(strstr(say_line("status"), "appearing offline") && strstr(say_line("status"), "details none") && strstr(say_line("status"), "requests refused"), "and they took");
	CHECK(strstr(say_line("remove 1"), "removed"), "remove");
	fake_run(&net, 300);
	CHECK(strstr(say_line("block 1"), "no such command"), "and then there is no first friend");
	CHECK(strstr(say_line("list"), "0 friends"), "removed");
	/* nonsense of every kind: no crash, an answer each time */
	for (round = 0; round < 4000; round++)
	{
		char noise[120];
		int length = (int)(fake_random(&test_state) % 100), i;
		static const char *const words[] = { "add", "accept", "ignore", "cancel", "remove", "block", "unblock", "nick", "invite", "join", "joininvite", "party", "name", "share", "details",
			"requests", "activity", "kick", "chat", "transfer", "leave", "99", "-1", "0", "x" };

		noise[0] = 0;
		if (round & 1)
			snprintf(noise, sizeof(noise), "%s %s %s", words[fake_random(&test_state) % 25], words[fake_random(&test_state) % 25], words[fake_random(&test_state) % 25]);
		else
		{
			for (i = 0; i < length; i++)
				noise[i] = (char)(1 + fake_random(&test_state) % 254);
			noise[length] = 0;
		}
		CHECK(say_line(noise) != NULL, "reply");
		if (round % 50 == 0)
			fake_run(&net, 50);
	}
	{
		char tiny[8];

		CHECK(social_command("help", tiny, sizeof(tiny)) == 1 && strlen(tiny) < sizeof(tiny), "a small buffer is not overrun");
		CHECK(social_command("help", tiny, 0) == 1, "no buffer");
	}
	social_global_front = NULL;
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "invite"))
		case_invite();
	else if (!strcmp(name, "invite-abuse"))
		case_invite_abuse();
	else if (!strcmp(name, "joinable-presence"))
		case_joinable_presence();
	else if (!strcmp(name, "join-ok"))
		case_join_ok();
	else if (!strcmp(name, "join-failures"))
		case_join_failures();
	else if (!strcmp(name, "join-checks"))
		case_join_checks();
	else if (!strcmp(name, "join-left"))
		case_join_left();
	else if (!strcmp(name, "commands"))
		case_commands();
	else if (!strcmp(name, "join-through-the-network"))
		case_join_through_the_network();
	else
	{
		fprintf(stderr, "no case named %s\n", name);
		return 2;
	}
	return 0;
}
