/* social_hard_check.c: the friends layer under abuse and at scale (tools/harness/tests/test_social_hard.py): the whole receiving path fed messages sealed with the right
keys around bytes that are not what they say (run under the address and undefined-behaviour sanitisers too); the engine's whole conversation of a scenario as bytes, the
same on every byte order; and eight simulated people on brokers that lose, repeat and reorder messages and go down, with a malicious peer among them. */

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
#include "social_fake.h"

static unsigned long long test_state = 0xABCDEFULL;

static unsigned int random_below(unsigned int limit)
{
	return (unsigned int)(fake_random(&test_state) % limit);
}

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

static int online_to(struct fake_node *node, struct fake_node *other)
{
	struct social_friend *friend = social_friend_by_key(node->engine, other->engine->identity.key);

	return friend && social_friend_online(node->engine, friend);
}

/* ---------- the whole receiving path, fed bytes sealed with keys it accepts */

#define SAMPLES 64

static struct
{
	unsigned char plain[SOCIAL_MAX_PLAIN];
	int size;
	int kind;
	int type;
} samples[4][SAMPLES];
static int sample_counts[4];

static const char *tapped_inbox, *tapped_circle, *tapped_party;
static unsigned char tapped_inbox_seal[32], tapped_party_seal[32];
static unsigned char tapped_circle_seal[32];
static unsigned char tapped_pair_key[32];

static int channel_of_topic(const char *topic)
{
	if (tapped_inbox && !strcmp(topic, tapped_inbox))
		return 0;
	if (tapped_circle && !strcmp(topic, tapped_circle))
		return 1;
	if (tapped_party && !strcmp(topic, tapped_party))
		return 3;
	return -1;
}

static void fuzz_tap(void *user, int node, unsigned int time, const char *topic, const unsigned char *payload, int size)
{
	unsigned char plain[SOCIAL_MAX_PLAIN];
	int channel = channel_of_topic(topic), kind, plain_size = -1;
	const unsigned char *tag;
	const unsigned char *key = NULL;

	(void)user;
	(void)node;
	(void)time;
	if (channel < 0 || !social_wire_header(payload, size, &kind, &tag))
		return;
	if (kind == SOCIAL_KIND_REQUEST)
		key = tapped_inbox_seal;
	else if (kind == SOCIAL_KIND_PRESENCE)
		key = tapped_circle_seal;
	else if (kind == SOCIAL_KIND_PARTY)
		key = tapped_party_seal;
	else if (kind == SOCIAL_KIND_DIRECT)
	{
		key = tapped_pair_key;
		channel = 2;
	}
	if (key)
		plain_size = social_open(key, topic, payload, size, plain, sizeof(plain));
	if (plain_size >= SOCIAL_INNER_HEADER && sample_counts[channel] < SAMPLES)
	{
		memcpy(samples[channel][sample_counts[channel]].plain, plain, (size_t)plain_size);
		samples[channel][sample_counts[channel]].size = plain_size;
		samples[channel][sample_counts[channel]].kind = kind;
		samples[channel][sample_counts[channel]].type = plain[0];
		sample_counts[channel]++;
	}
}

static void check_engine_invariants(struct fake_node *target)
{
	const struct social_snapshot *snapshot = snap(target, 0);
	int index, character;
	struct social_engine *engine = target->engine;

	CHECK(social_friend_count(engine) <= SOCIAL_MAX_FRIENDS && snapshot->friend_count <= SOCIAL_MAX_FRIENDS, "friends");
	CHECK(snapshot->request_count <= SOCIAL_MAX_REQUESTS && snapshot->sent_count <= SOCIAL_MAX_SENT && snapshot->invite_count <= SOCIAL_MAX_INVITES_TOTAL, "lists");
	CHECK(snapshot->party.member_count <= SOCIAL_MAX_PARTY_MEMBERS && engine->party.roster_count <= SOCIAL_MAX_PARTY_MEMBERS, "party");
	CHECK(engine->queue_count >= 0 && engine->queue_count <= SOCIAL_MAX_QUEUE && engine->event_count >= 0 && engine->event_count <= SOCIAL_MAX_EVENTS, "queues");
	CHECK(engine->pending_count >= 0 && engine->pending_count <= SOCIAL_MAX_PENDING, "pending");
	CHECK(engine->blocked_count <= SOCIAL_MAX_BLOCKED && engine->ignored_count <= SOCIAL_MAX_IGNORED, "lists of keys");
	/* every string a screen could draw is plain */
#define PLAIN(text) \
	do \
	{ \
		for (character = 0; (text)[character]; character++) \
			CHECK((text)[character] >= 0x20 && (text)[character] <= 0x7E && (text)[character] != '|', "text %s char %02x", #text, (unsigned char)(text)[character]); \
	} while (0)
	for (index = 0; index < snapshot->friend_count; index++)
	{
		PLAIN(snapshot->friends[index].name);
		PLAIN(snapshot->friends[index].map);
		PLAIN(snapshot->friends[index].gametype);
		PLAIN(snapshot->friends[index].nickname);
		PLAIN(snapshot->friends[index].join.map);
	}
	for (index = 0; index < snapshot->request_count; index++)
		PLAIN(snapshot->requests[index].name);
	for (index = 0; index < snapshot->invite_count; index++)
	{
		PLAIN(snapshot->invites[index].name);
		PLAIN(snapshot->invites[index].target.map);
		PLAIN(snapshot->invites[index].target.gametype);
	}
	for (index = 0; index < snapshot->party.member_count; index++)
		PLAIN(snapshot->party.members[index].name);
	for (index = 0; index < snapshot->party.chat_count; index++)
	{
		PLAIN(snapshot->party.chat[index].name);
		PLAIN(snapshot->party.chat[index].text);
	}
	PLAIN(snapshot->party.game.map);
	PLAIN(snapshot->party.game.gametype);
}

static void case_fuzz_pipeline(int argc, char **argv)
{
	static struct fake_net net;
	struct fake_node *target, *friend_node, *third;
	long iterations = argc > 0 ? atol(argv[0]) : 20000;
	struct social_friend *friend;
	long round;
	unsigned int sequence = 5000000;
	unsigned long long delivered = 0, accepted_before;
	int index;

	fake_start(&net, 70);
	target = fake_add(&net, 0, 1);
	friend_node = fake_add(&net, 0, 1);
	third = fake_add(&net, 0, 1);
	link_friends(target, friend_node);
	link_friends(target, third);
	fake_run(&net, 3000);
	friend = social_friend_by_key(target->engine, friend_node->engine->identity.key);
	CHECK(friend && social_friend_pair(target->engine, friend), "friend and pair");
	tapped_inbox = target->engine->inbox_topic;
	tapped_circle = friend_node->engine->circle_topic;
	memcpy(tapped_inbox_seal, target->engine->inbox_seal, 32);
	memcpy(tapped_circle_seal, friend_node->engine->circle_seal, 32);
	memcpy(tapped_pair_key, friend->pair_key, 32);
	/* a party and some traffic, tapped */
	social_front_party(target->front, 1, 0, 0, NULL);
	fake_run(&net, 500);
	tapped_party = target->engine->party.topic;
	memcpy(tapped_party_seal, target->engine->party.seal, 32);
	net.tap = fuzz_tap;
	social_front_party(target->front, 2, friend_id(target, friend_node), 0, NULL);
	fake_run(&net, 1000);
	social_front_party_token(friend_node->front, target->engine->party.token);
	{
		struct social_place place;
		unsigned char seed[32];
		struct social_identity stranger;
		unsigned char plain[SOCIAL_MAX_PLAIN];
		int size;

		memset(&place, 0, sizeof(place));
		place.activity = SOCIAL_GAME;
		place.hosting = 1;
		place.has_invite = 1;
		place.joinable = 1;
		strcpy(place.map, "bloodgulch");
		strcpy(place.gametype, "Slayer");
		place.invite_token[0] = 1;
		social_front_set_place(friend_node->front, &place);
		social_front_set_place(target->front, &place);
		fake_run(&net, 4000);
		social_front_invite(friend_node->front, friend_id(friend_node, target));
		social_front_party(target->front, 10, 0, 0, "hello party");
		fake_run(&net, 4000);
		/* the inbox: a request, as it comes */
		fill(seed, 32);
		social_identity_make(&stranger, seed);
		size = social_add_encode(&stranger, 1, 1, "Stranger", 0, target->engine->identity.code_bytes, plain, sizeof(plain));
		memcpy(samples[0][sample_counts[0]].plain, plain, (size_t)size);
		samples[0][sample_counts[0]].size = size;
		samples[0][sample_counts[0]].kind = SOCIAL_KIND_REQUEST;
		samples[0][sample_counts[0]++].type = SOCIAL_TYPE_ADD;
		{
			struct social_hello hello;

			memset(&hello, 0, sizeof(hello));
			hello.circle_generation = 2;
			fill(hello.circle_key, 32);
			size = social_hello_encode(1, 1, &hello, plain, sizeof(plain));
			memcpy(samples[2][sample_counts[2]].plain, plain, (size_t)size);
			samples[2][sample_counts[2]].size = size;
			samples[2][sample_counts[2]].kind = SOCIAL_KIND_DIRECT;
			samples[2][sample_counts[2]++].type = SOCIAL_TYPE_HELLO;
			size = social_remove_encode(1, 1, 0, plain, sizeof(plain));
			memcpy(samples[2][sample_counts[2]].plain, plain, (size_t)size);
			samples[2][sample_counts[2]].size = size;
			samples[2][sample_counts[2]].kind = SOCIAL_KIND_DIRECT;
			samples[2][sample_counts[2]++].type = SOCIAL_TYPE_REMOVE;
		}
	}
	net.tap = NULL;
	for (index = 0; index < 4; index++)
		CHECK(sample_counts[index] > 0, "samples of channel %d", index);
	accepted_before = target->engine->statistics.accepted;
	for (round = 0; round < iterations; round++)
	{
		int channel = (int)random_below(4);
		int pick = (int)random_below((unsigned)sample_counts[channel]);
		unsigned char plain[SOCIAL_MAX_PLAIN + 16], nonce[12], wire[SOCIAL_MAX_WIRE + 64];
		int size = samples[channel][pick].size, changes, wire_size;
		const char *topic;
		const unsigned char *key;
		const unsigned char *tag = NULL;
		int kind = samples[channel][pick].kind;

		memcpy(plain, samples[channel][pick].plain, (size_t)size);
		/* a number above the window, so that it is read (the header's type is kept most of the time) */
		sequence++;
		plain[1] = 0x7F;
		plain[2] = 0;
		plain[3] = 0;
		plain[4] = 1;
		plain[5] = (unsigned char)(sequence >> 24);
		plain[6] = (unsigned char)(sequence >> 16);
		plain[7] = (unsigned char)(sequence >> 8);
		plain[8] = (unsigned char)sequence;
		changes = (int)random_below(4);
		while (changes--)
		{
			int what = (int)random_below(6);

			if (what == 0 && size > SOCIAL_INNER_HEADER)
				plain[SOCIAL_INNER_HEADER + random_below((unsigned)(size - SOCIAL_INNER_HEADER))] = (unsigned char)random_below(256);
			else if (what == 1 && size > SOCIAL_INNER_HEADER)
				plain[SOCIAL_INNER_HEADER + random_below((unsigned)(size - SOCIAL_INNER_HEADER))] ^= (unsigned char)(1u << random_below(8));
			else if (what == 2 && size > SOCIAL_INNER_HEADER + 1)
				size = SOCIAL_INNER_HEADER + (int)random_below((unsigned)(size - SOCIAL_INNER_HEADER));
			else if (what == 3 && size < SOCIAL_MAX_PLAIN)
				plain[size++] = (unsigned char)random_below(256);
			else if (what == 4 && size > SOCIAL_INNER_HEADER)
				plain[SOCIAL_INNER_HEADER + random_below((unsigned)(size - SOCIAL_INNER_HEADER))] = random_below(2) ? 0xFF : 0x00;
			else if (what == 5)
				plain[0] = (unsigned char)"AHPIXSLKBTC"[random_below(11)];
		}
		fill(nonce, 12);
		switch (channel)
		{
		case 0:
			topic = target->engine->inbox_topic;
			key = target->engine->inbox_seal;
			break;
		case 1:
			topic = friend_node->engine->circle_topic;
			key = friend_node->engine->circle_seal;
			break;
		case 2:
			topic = random_below(2) ? target->engine->inbox_topic : friend_node->engine->circle_topic;
			key = friend->pair_key;
			tag = friend->tag_in;
			break;
		default:
			topic = target->engine->party.topic;
			key = target->engine->party.seal;
			break;
		}
		if (channel == 2 && (kind != SOCIAL_KIND_DIRECT || random_below(4) == 0))
			kind = SOCIAL_KIND_DIRECT;
		wire_size = social_seal(key, kind, tag, topic, plain, size, nonce, wire, sizeof(wire));
		if (wire_size < 0)
			continue;
		net.time += 60;
		social_front_heard(target->front, topic, wire, wire_size);
		delivered++;
		if (round % 50 == 0)
		{
			social_front_tick(target->front);
			check_engine_invariants(target);
		}
		/* (a hostile hello from the friend may have changed its circle, as it may: put back) */
		if (round % 20 == 0)
		{
			social_friend_add(target->engine, friend_node->engine->identity.key, friend_node->engine->circle_generation, friend_node->engine->circle_key, 1);
			if (!target->engine->party.active)
			{
				memcpy(target->engine->party.token, target->engine->party.token, 16);
			}
		}
		/* (the target's friends and party do not wither while this goes on) */
		if (round % 400 == 0)
		{
			friend_node->engine->presence_wanted = 1;
			fake_run(&net, 10);
		}
	}
	social_front_tick(target->front);
	check_engine_invariants(target);
	printf("%llu messages sealed with the right keys around altered bytes; %llu taken, %u refused as malformed, %u by the window, %u by a signature\n", delivered,
		(unsigned long long)(target->engine->statistics.accepted - accepted_before), target->engine->statistics.dropped_malformed, target->engine->statistics.dropped_window,
		target->engine->statistics.dropped_signature);
	{
		const struct social_statistics *st = &target->engine->statistics;

		printf("received %u accepted %u opened %u: size %u kind %u topic %u rate %u seen %u open %u header %u window %u signature %u verify %u unknown %u blocked %u malformed %u\n", st->received,
			st->accepted, st->opened, st->dropped_size, st->dropped_kind, st->dropped_topic, st->dropped_rate, st->dropped_seen, st->dropped_open, st->dropped_header, st->dropped_window,
			st->dropped_signature, st->dropped_verify_budget, st->dropped_unknown, st->dropped_blocked, st->dropped_malformed);
	}
	(void)third;
}

/* ---------- the engine's conversation as bytes */

static void golden_tap(void *user, int node, unsigned int time, const char *topic, const unsigned char *payload, int size)
{
	char hex[2 * SOCIAL_MAX_WIRE + 1];

	(void)user;
	social_hex(payload, size, hex);
	printf("%u %d %s %s\n", time, node, topic, hex);
}

static void case_golden_engine(void)
{
	static struct fake_net net;
	struct fake_node *a, *b;
	struct social_place place;

	fake_start(&net, 71);
	/* nothing in the network's behaviour is random here but the seed, and the seed is the same on every byte order */
	net.latency_ms = 40;
	a = fake_add(&net, 7, 1);
	b = fake_add(&net, -5, 1);
	net.tap = golden_tap;
	fake_run(&net, 600);
	social_front_add_code(a->front, snap(b, 0)->code);
	fake_run(&net, 2000);
	social_front_accept(b->front, snap(b, 0)->requests[0].id);
	fake_run(&net, 12000);
	memset(&place, 0, sizeof(place));
	place.activity = SOCIAL_GAME;
	place.hosting = 1;
	place.has_invite = 1;
	place.joinable = 1;
	strcpy(place.map, "bloodgulch");
	strcpy(place.gametype, "Slayer");
	place.players = 3;
	place.maximum_players = 16;
	place.engine_type = 2;
	memset(place.invite_hash, 0x5A, 16);
	memset(place.invite_token, 0xA5, 16);
	social_front_set_place(a->front, &place);
	fake_run(&net, 4000);
	social_front_invite(a->front, friend_id(a, b));
	fake_run(&net, 1500);
	social_front_party(a->front, 1, 0, 0, NULL);
	fake_run(&net, 500);
	social_front_party(a->front, 2, friend_id(a, b), 0, NULL);
	fake_run(&net, 1500);
	social_front_party_token(b->front, snap(b, 0)->invites[0].kind == 2 ? snap(b, 0)->invites[0].party_token : snap(b, 0)->invites[1].party_token);
	fake_run(&net, 3000);
	social_front_party(b->front, 10, 0, 0, "hi from b");
	fake_run(&net, 1500);
	social_front_party(a->front, 7, snap(a, 0)->party.members[1].id, 0, NULL);
	fake_run(&net, 3000);
	social_front_party(b->front, 5, 0, 0, NULL);
	fake_run(&net, 1500);
	social_front_remove(a->front, friend_id(a, b));
	fake_run(&net, 3000);
	net.tap = NULL;
}

/* ---------- eight people on bad brokers, with a malicious peer */

struct sim_log
{
	unsigned char payloads[256][SOCIAL_MAX_WIRE];
	int sizes[256];
	char topics[256][SOCIAL_TOPIC_SIZE];
	int count, next;
};

static struct sim_log sim_captured;

static void sim_tap(void *user, int node, unsigned int time, const char *topic, const unsigned char *payload, int size)
{
	(void)user;
	(void)time;
	if (node == 7)
		return;
	if (sim_captured.count < 256 || random_below(8) == 0)
	{
		int slot = sim_captured.count < 256 ? sim_captured.count++ : (int)random_below(256);

		memcpy(sim_captured.payloads[slot], payload, (size_t)size);
		sim_captured.sizes[slot] = size;
		snprintf(sim_captured.topics[slot], SOCIAL_TOPIC_SIZE, "%s", topic);
	}
}

/* the malicious peer's doing, a little every 100 ms of the network's time */
static void sim_attack(struct fake_net *net, int victims)
{
	struct fake_node *evil = &net->nodes[7];
	int round;

	for (round = 0; round < 6; round++)
	{
		struct fake_node *victim = &net->nodes[random_below((unsigned)victims)];
		unsigned char junk[SOCIAL_MAX_WIRE];
		int size, what = (int)random_below(5);

		switch (what)
		{
		case 0:
			/* a request from a fresh key to a victim's inbox */
			{
				unsigned char seed[32], plain[SOCIAL_MAX_PLAIN], nonce[12], wire[SOCIAL_MAX_WIRE];
				struct social_identity fresh;
				int plain_size, wire_size;
				char name[12];

				fill(seed, 32);
				social_identity_make(&fresh, seed);
				snprintf(name, sizeof(name), "Spam%u", random_below(1000));
				plain_size = social_add_encode(&fresh, 1, 1, name, 0, victim->engine->identity.code_bytes, plain, sizeof(plain));
				fill(nonce, 12);
				wire_size = social_seal(victim->engine->inbox_seal, SOCIAL_KIND_REQUEST, NULL, victim->engine->inbox_topic, plain, plain_size, nonce, wire, sizeof(wire));
				social_front_heard(victim->front, victim->engine->inbox_topic, wire, wire_size);
			}
			break;
		case 1:
			/* garbage on a victim's inbox or a friend's topic */
			size = (int)random_below(SOCIAL_MAX_WIRE);
			fill(junk, size);
			if (size > 2)
			{
				junk[0] = (unsigned char)"RPDG"[random_below(4)];
				junk[1] = 1;
			}
			social_front_heard(victim->front, victim->engine->inbox_topic, junk, size);
			break;
		case 2:
			/* a captured message replayed on its own topic, or on another */
			if (sim_captured.count)
			{
				int slot = (int)random_below((unsigned)sim_captured.count);

				social_front_heard(victim->front, sim_captured.topics[slot], sim_captured.payloads[slot], sim_captured.sizes[slot]);
				social_front_heard(victim->front, victim->engine->inbox_topic, sim_captured.payloads[slot], sim_captured.sizes[slot]);
			}
			break;
		case 3:
			/* the same, as the malicious peer's own publish: through the brokers */
			if (sim_captured.count)
			{
				int slot = (int)random_below((unsigned)sim_captured.count);

				fake_publish(evil, sim_captured.topics[slot], sim_captured.payloads[slot], sim_captured.sizes[slot]);
			}
			break;
		default:
			/* a flip in a captured message, so that its tag fails */
			if (sim_captured.count)
			{
				int slot = (int)random_below((unsigned)sim_captured.count);

				memcpy(junk, sim_captured.payloads[slot], (size_t)sim_captured.sizes[slot]);
				junk[random_below((unsigned)sim_captured.sizes[slot])] ^= 0x10;
				social_front_heard(victim->front, sim_captured.topics[slot], junk, sim_captured.sizes[slot]);
			}
			break;
		}
	}
}

static void case_sim(int argc, char **argv)
{
	static struct fake_net net;
	static struct social_files files[FAKE_MAX_NODES];
	int minutes = argc > 0 ? atoi(argv[0]) : 12;
	int index, step, other;
	int honest = 7;
	unsigned long long published_total = 0;
	int online_pairs = 0, pairs = 0;
	struct social_place place;

	fake_start(&net, 72);
	net.loss_percent = 10;
	net.duplicate_percent = 10;
	net.jitter_ms = 300;
	net.latency_ms = 60;
	(void)files;
	for (index = 0; index < 8; index++)
		fake_add(&net, (int)random_below(53) - 26, (int)random_below(2));
	net.tap = sim_tap;
	/* the honest seven befriend in a ring and with some chords, by the real flow: a request and an accept */
	fake_run(&net, 20000);
	for (index = 0; index < honest; index++)
	{
		social_front_add_code(net.nodes[index].front, snap(&net.nodes[(index + 1) % honest], 0)->code);
		fake_run(&net, 700);
		if (index % 2 == 0)
		{
			social_front_add_code(net.nodes[index].front, snap(&net.nodes[(index + 3) % honest], 0)->code);
			fake_run(&net, 700);
		}
	}
	/* requests are answered by accepting whatever is asked, as the honest do */
	for (step = 0; step < 300; step++)
	{
		for (index = 0; index < honest; index++)
		{
			const struct social_snapshot *snapshot = snap(&net.nodes[index], 0);
			int request;

			for (request = 0; request < snapshot->request_count; request++)
			{
				/* (the malicious peer's requests are named Spam: the honest do not accept them) */
				if (strncmp(snapshot->requests[request].name, "Node", 4))
					continue;
				social_front_accept(net.nodes[index].front, snapshot->requests[request].id);
			}
		}
		sim_attack(&net, honest);
		fake_run(&net, 100);
	}
	/* a party of four, a game with its invite, a broker down, one restart */
	social_front_party(net.nodes[0].front, 1, 0, 0, NULL);
	fake_run(&net, 1000);
	for (index = 1; index < 4; index++)
	{
		unsigned int id = friend_id(&net.nodes[0], &net.nodes[index]);

		if (id)
		{
			social_front_party(net.nodes[0].front, 2, id, 0, NULL);
			fake_run(&net, 1500);
			for (other = 0; other < snap(&net.nodes[index], 0)->invite_count; other++)
			{
				if (snap(&net.nodes[index], 0)->invites[other].kind == 2)
				{
					social_front_party_token(net.nodes[index].front, snap(&net.nodes[index], 0)->invites[other].party_token);
					break;
				}
			}
		}
	}
	memset(&place, 0, sizeof(place));
	place.activity = SOCIAL_GAME;
	place.hosting = 1;
	place.has_invite = 1;
	place.joinable = 1;
	strcpy(place.map, "bloodgulch");
	place.invite_token[0] = 9;
	social_front_set_place(net.nodes[0].front, &place);
	for (step = 0; step < minutes * 600; step++)
	{
		if (step == 600)
			net.broker_up[0] = 0;
		if (step == 1800)
			net.broker_up[0] = 1;
		if (step == 2400)
			net.broker_up[2] = 0;
		if (step == 3000)
			net.broker_up[2] = 1;
		if (step % 150 == 20)
			social_front_party(net.nodes[1 + step / 150 % 3].front, 10, 0, 0, "chat");
		sim_attack(&net, honest);
		fake_run(&net, 100);
	}
	fake_run(&net, 60000);
	/* what must hold */
	for (index = 0; index < 8; index++)
		published_total += (unsigned long long)net.nodes[index].published;
	for (index = 0; index < honest; index++)
	{
		CHECK(net.nodes[index].limited == 0, "node %d hit the brokers' limit of publishes a second %d times", index, net.nodes[index].limited);
		check_engine_invariants(&net.nodes[index]);
		CHECK(!social_friend_by_key(net.nodes[index].engine, net.nodes[7].engine->identity.key), "node %d took the malicious peer as a friend", index);
		CHECK(net.nodes[index].engine->statistics.send_dropped < 40, "node %d: %u sends dropped", index, net.nodes[index].engine->statistics.send_dropped);
	}
	/* every pair of friends hears each other, whatever the loss */
	for (index = 0; index < honest; index++)
	{
		for (other = index + 1; other < honest; other++)
		{
			if (social_friend_by_key(net.nodes[index].engine, net.nodes[other].engine->identity.key) || social_friend_by_key(net.nodes[other].engine, net.nodes[index].engine->identity.key))
			{
				pairs++;
				CHECK(social_friend_by_key(net.nodes[index].engine, net.nodes[other].engine->identity.key) &&
					social_friend_by_key(net.nodes[other].engine, net.nodes[index].engine->identity.key), "friends one way only: %d %d", index, other);
				CHECK(online_to(&net.nodes[index], &net.nodes[other]) && online_to(&net.nodes[other], &net.nodes[index]), "friends %d and %d do not hear each other", index, other);
				online_pairs++;
			}
		}
	}
	CHECK(pairs >= 7, "the friendships were made: %d", pairs);
	/* the party agrees */
	{
		int members = snap(&net.nodes[0], 0)->party.member_count, leaders = 0;

		CHECK(members >= 2, "a party of %d", members);
		for (index = 0; index < honest; index++)
		{
			if (snap(&net.nodes[index], 0)->party.active)
			{
				CHECK(snap(&net.nodes[index], 0)->party.member_count == members, "node %d sees %d members, node 0 sees %d", index, snap(&net.nodes[index], 0)->party.member_count, members);
				leaders += snap(&net.nodes[index], 0)->party.is_leader;
			}
		}
		CHECK(leaders == 1, "one leader: %d", leaders);
	}
	printf("%d honest people, %d friendships all heard, %llu messages published in %d minutes of 10%% loss, 10%% repeats, 300 ms of jitter, two brokers down in turn, a restless peer\n",
		honest, online_pairs, published_total, minutes);
	for (index = 0; index < honest; index++)
		printf("node %d: sent %u dropped %u of %u received (seen %u, rate %u, open %u, signature %u, window %u, malformed %u, verify %u, blocked %u)\n", index,
			net.nodes[index].engine->statistics.sent, net.nodes[index].engine->statistics.send_dropped, net.nodes[index].engine->statistics.received,
			net.nodes[index].engine->statistics.dropped_seen, net.nodes[index].engine->statistics.dropped_rate, net.nodes[index].engine->statistics.dropped_open,
			net.nodes[index].engine->statistics.dropped_signature, net.nodes[index].engine->statistics.dropped_window, net.nodes[index].engine->statistics.dropped_malformed,
			net.nodes[index].engine->statistics.dropped_verify_budget, net.nodes[index].engine->statistics.dropped_blocked);
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "fuzz-pipeline"))
		case_fuzz_pipeline(argc - 2, argv + 2);
	else if (!strcmp(name, "golden-engine"))
		case_golden_engine();
	else if (!strcmp(name, "sim"))
		case_sim(argc - 2, argv + 2);
	else
	{
		fprintf(stderr, "no case named %s\n", name);
		return 2;
	}
	return 0;
}
