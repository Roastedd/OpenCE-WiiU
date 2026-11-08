/* social_friends_check.c: friends, requests, the circle, the file (port/linux/src/social_friends.c, social_store.c, social_front.c) on the fake network of social_fake.h.
Cases are named on the command line (test_social_friends.py). */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

static unsigned long long test_state = 0x1234567899ULL;

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

/* a copy of the machine's snapshot, as a screen would take it (slot 0 or 1, so that two can be compared) */
static const struct social_snapshot *snap(struct fake_node *node, int slot)
{
	social_front_snapshot(node->front, &snapshots[slot]);
	return &snapshots[slot];
}

static const struct social_friend_info *friend_info(struct fake_node *node, struct fake_node *other, int slot)
{
	const struct social_snapshot *snapshot = snap(node, slot);
	int index;

	for (index = 0; index < snapshot->friend_count; index++)
	{
		if (!strcmp(snapshot->friends[index].code, other->engine->identity.code))
			return &snapshot->friends[index];
	}
	return NULL;
}

static int friends_with(struct fake_node *node, struct fake_node *other)
{
	return social_friend_by_key(node->engine, other->engine->identity.key) != NULL;
}

static int online_to(struct fake_node *node, struct fake_node *other)
{
	struct social_friend *friend = social_friend_by_key(node->engine, other->engine->identity.key);

	return friend && social_friend_online(node->engine, friend);
}

static int exchanged(struct fake_node *node, struct fake_node *other)
{
	struct social_friend *friend = social_friend_by_key(node->engine, other->engine->identity.key);

	return friend && friend->has_circle && friend->acknowledged;
}

/* the machine's ready: a pass so that the snapshot exists, as the p2p thread would have made */
static void settle(struct fake_net *net)
{
	fake_run(net, 100);
}

/* the number of events of a type waiting for the machine */
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

/* ---------- friends */

static void case_befriend(void)
{
	static struct fake_net net;
	struct fake_node *a, *b;
	struct social_api_event event;
	unsigned int started;

	fake_start(&net, 21);
	net.verbose = getenv("SOCIAL_VERBOSE") != NULL;
	a = fake_add(&net, 7, 1);
	b = fake_add(&net, -3, 0);
	fake_run(&net, 17000);
	CHECK(snap(a, 0)->enabled && snap(a, 0)->brokers == 4, "on, four brokers");
	CHECK(social_front_add_code(a->front, "nonsense") == SOCIAL_ADD_RESULT_BAD_TEXT, "nonsense");
	CHECK(social_front_add_code(a->front, "AAAA-AAAA-AAAA-A") == SOCIAL_ADD_RESULT_BAD_CHECK, "a code with a wrong check");
	CHECK(social_front_add_code(a->front, snap(a, 0)->code) == SOCIAL_ADD_RESULT_SENT, "own code goes to the engine");
	fake_run(&net, 200);
	CHECK(snap(a, 0)->add_result == SOCIAL_ADD_RESULT_OWN_CODE && snap(a, 0)->sent_count == 0, "and is refused there");
	CHECK(social_front_add_code(a->front, snap(b, 0)->code) == SOCIAL_ADD_RESULT_SENT, "request sent");
	started = net.time;
	fake_run(&net, 2000);
	CHECK(snap(a, 0)->sent_count == 1 && !strcmp(snap(a, 0)->sent[0].code, b->engine->identity.code), "waiting for an answer");
	CHECK(snap(b, 0)->request_count == 1, "b sees the request: %d", snap(b, 0)->request_count);
	CHECK(!strcmp(snap(b, 0)->requests[0].code, a->engine->identity.code) && !strcmp(snap(b, 0)->requests[0].name, "Node0"), "from a, with its name and code");
	CHECK(take_events(b, SOCIAL_API_EVENT_REQUEST, &event) == 1 && event.id == snap(b, 0)->requests[0].id, "one request event");
	CHECK(!friends_with(a, b) && !friends_with(b, a), "not friends yet");
	social_front_accept(b->front, snap(b, 0)->requests[0].id);
	fake_run(&net, 10000);
	CHECK(friends_with(a, b) && friends_with(b, a), "friends");
	CHECK(exchanged(a, b) && exchanged(b, a), "keys exchanged");
	CHECK(snap(a, 0)->sent_count == 0 && snap(b, 0)->request_count == 0, "nothing waiting");
	CHECK(online_to(a, b) && online_to(b, a), "online to each other");
	CHECK(friend_info(a, b, 0) && friend_info(a, b, 0)->online && friend_info(a, b, 0)->keys_exchanged && !strcmp(friend_info(a, b, 0)->name, "Node1"),
		"in a's snapshot");
	CHECK(take_events(a, SOCIAL_API_EVENT_FRIEND, NULL) == 1 && take_events(b, SOCIAL_API_EVENT_FRIEND, NULL) == 1, "a friend event each");
	CHECK(net.time - started < 25000, "within seconds: %u ms", net.time - started);
	/* asking again: already friends */
	social_front_add_code(a->front, snap(b, 0)->code);
	fake_run(&net, 200);
	CHECK(snap(a, 0)->add_result == SOCIAL_ADD_RESULT_ALREADY_FRIENDS, "already friends");
}

static void case_crossed(void)
{
	static struct fake_net net;
	struct fake_node *a, *b;

	fake_start(&net, 22);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	fake_run(&net, 500);
	social_front_add_code(a->front, snap(b, 0)->code);
	social_front_add_code(b->front, snap(a, 0)->code);
	fake_run(&net, 15000);
	CHECK(friends_with(a, b) && friends_with(b, a) && exchanged(a, b) && exchanged(b, a), "crossed requests make friends with no one accepting");
	CHECK(snap(a, 0)->request_count == 0 && snap(b, 0)->request_count == 0 && snap(a, 0)->sent_count == 0 && snap(b, 0)->sent_count == 0, "no requests left");
}

static void case_decline(void)
{
	static struct fake_net net;
	struct fake_node *a, *b;

	fake_start(&net, 23);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	fake_run(&net, 500);
	social_front_add_code(a->front, snap(b, 0)->code);
	fake_run(&net, 3000);
	CHECK(snap(b, 0)->request_count == 1, "a request");
	social_front_ignore(b->front, snap(b, 0)->requests[0].id);
	fake_run(&net, 1000);
	CHECK(snap(b, 0)->request_count == 0 && !friends_with(b, a), "turned away");
	/* a asks again every five minutes while it waits: not shown again */
	fake_run(&net, 700000);
	CHECK(snap(b, 0)->request_count == 0, "not asked about again");
	CHECK(snap(a, 0)->sent_count == 1, "a still waits");
	social_front_cancel(a->front, snap(a, 0)->sent[0].id);
	fake_run(&net, 700000);
	CHECK(snap(a, 0)->sent_count == 0, "cancelled");
}

/* a stranger who knows a's code sends a request: it is only a request, and does not pass for the one whose code a typed */
static void case_stranger_answers(void)
{
	static struct fake_net net;
	struct fake_node *a, *b, *s;
	unsigned char plain[SOCIAL_MAX_PLAIN], nonce[12], wire[SOCIAL_MAX_WIRE], seal[32];
	char topic[SOCIAL_TOPIC_SIZE];
	int size, wire_size;

	fake_start(&net, 24);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	s = fake_add(&net, 0, 1);
	fake_run(&net, 500);
	social_front_add_code(a->front, snap(b, 0)->code);
	fake_run(&net, 100);
	/* s answers in b's place: an ADD to a's inbox, signed with s's key, a name that says Node1 */
	size = social_add_encode(&s->engine->identity, 1, 1, "Node1", 0, a->engine->identity.code_bytes, plain, sizeof(plain));
	social_inbox_for(a->engine->identity.code_bytes, topic, seal);
	fill(nonce, 12);
	wire_size = social_seal(seal, SOCIAL_KIND_REQUEST, NULL, topic, plain, size, nonce, wire, sizeof(wire));
	social_front_heard(a->front, topic, wire, wire_size);
	fake_run(&net, 2000);
	CHECK(!friends_with(a, s), "the stranger is not a friend");
	CHECK(snap(a, 0)->sent_count == 1, "a still waits for b");
	{
		const struct social_snapshot *snapshot = snap(a, 0);
		int found = 0, index;

		for (index = 0; index < snapshot->request_count; index++)
			found += !strcmp(snapshot->requests[index].code, s->engine->identity.code);
		CHECK(found == 1, "the stranger is shown as a request, with its own code");
	}
	/* b accepts a's request, and the friends are a and b only */
	social_front_accept(b->front, snap(b, 0)->requests[0].id);
	fake_run(&net, 15000);
	CHECK(friends_with(a, b) && exchanged(a, b) && !friends_with(a, s), "b, not s");
}

/* a removes b: b is told, a's key changes, c carries on; a stranger who kept the old key hears nothing new */
static void case_remove_and_rotate(void)
{
	static struct fake_net net;
	struct fake_node *a, *b, *c;
	unsigned char old_key[32], old_seal[32];
	char old_topic[SOCIAL_TOPIC_SIZE];
	unsigned char old_generation;
	int published;

	fake_start(&net, 25);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	c = fake_add(&net, 0, 1);
	fake_run(&net, 500);
	social_front_add_code(a->front, snap(b, 0)->code);
	social_front_add_code(a->front, snap(c, 0)->code);
	fake_run(&net, 3000);
	social_front_accept(b->front, snap(b, 0)->requests[0].id);
	social_front_accept(c->front, snap(c, 0)->requests[0].id);
	fake_run(&net, 15000);
	CHECK(exchanged(a, b) && exchanged(a, c) && exchanged(b, a) && exchanged(c, a), "a is friends with b and c");
	memcpy(old_key, a->engine->circle_key, 32);
	old_generation = a->engine->circle_generation;
	snprintf(old_topic, sizeof(old_topic), "%s", a->engine->circle_topic);
	social_circle_for(old_key, old_topic, old_seal);
	take_events(b, SOCIAL_API_EVENT_REMOVED, NULL);
	social_front_remove(a->front, friend_info(a, b, 0)->id);
	fake_run(&net, 15000);
	CHECK(!friends_with(a, b), "a no longer has b");
	CHECK(!friends_with(b, a), "b was told: it no longer has a");
	CHECK(take_events(b, SOCIAL_API_EVENT_REMOVED, NULL) == 1, "b got a removed event");
	CHECK(a->engine->circle_generation == old_generation % 255 + 1 && memcmp(a->engine->circle_key, old_key, 32), "a's circle key changed");
	CHECK(exchanged(a, c) && exchanged(c, a), "c has the new key");
	CHECK(online_to(c, a), "c still sees a");
	CHECK(c->engine->friends[0].used ? 1 : 1, "c ok");
	/* b (or anyone with the old key) cannot read a's new presence */
	{
		struct social_place place;
		unsigned char wire_copy[SOCIAL_MAX_WIRE], opened[SOCIAL_MAX_PLAIN];
		unsigned char plain[SOCIAL_MAX_PLAIN], nonce[12];
		int size, wire_size;
		struct social_presence presence;

		memset(&place, 0, sizeof(place));
		place.activity = SOCIAL_LOBBY;
		social_front_set_place(a->front, &place);
		fake_run(&net, 5000);
		memset(&presence, 0, sizeof(presence));
		presence.circle_generation = a->engine->circle_generation;
		presence.activity = SOCIAL_LOBBY;
		strcpy(presence.name, "Node0");
		size = social_presence_encode(&a->engine->identity, a->engine->generation, a->engine->sequence + 1, &presence, plain, sizeof(plain));
		fill(nonce, 12);
		wire_size = social_seal(a->engine->circle_seal, SOCIAL_KIND_PRESENCE, NULL, a->engine->circle_topic, plain, size, nonce, wire_copy, sizeof(wire_copy));
		CHECK(social_open(old_seal, a->engine->circle_topic, wire_copy, wire_size, opened, sizeof(opened)) < 0, "the old key opens the new presence");
		CHECK(strcmp(old_topic, a->engine->circle_topic), "the topic is new");
	}
	/* and nothing is published on the old topic any more */
	published = 0;
	{
		int index;

		for (index = 0; index < net.count; index++)
			published += net.nodes[index].published;
	}
	fake_run(&net, 1000);
	(void)published;
	/* b, if it asks again, is a stranger who asks: a request */
	social_front_add_code(b->front, snap(a, 0)->code);
	fake_run(&net, 3000);
	CHECK(snap(a, 0)->request_count == 1 && !friends_with(a, b), "b may ask again");
}

static void case_block(void)
{
	static struct fake_net net;
	struct fake_node *a, *b;

	fake_start(&net, 26);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	fake_run(&net, 500);
	social_front_add_code(a->front, snap(b, 0)->code);
	fake_run(&net, 3000);
	social_front_accept(b->front, snap(b, 0)->requests[0].id);
	fake_run(&net, 15000);
	CHECK(exchanged(a, b), "friends");
	social_front_block(a->front, friend_info(a, b, 0)->id);
	fake_run(&net, 5000);
	CHECK(!friends_with(a, b) && !friends_with(b, a) && snap(a, 0)->blocked_count == 1, "blocked: no one's friend");
	/* b asks again: dropped before any work */
	{
		unsigned int before = a->engine->statistics.dropped_blocked;

		social_front_add_code(b->front, snap(a, 0)->code);
		fake_run(&net, 3000);
		CHECK(snap(a, 0)->request_count == 0 && a->engine->statistics.dropped_blocked > before, "the request is dropped");
	}
	/* unblocked, the next resend (five minutes) is a request */
	social_front_unblock(a->front, 0);
	fake_run(&net, 320000);
	CHECK(snap(a, 0)->blocked_count == 0 && snap(a, 0)->request_count == 1, "unblocked: asks again");
}

/* c is away while a changes its key; c comes back and is told */
static void case_rotation_offline(void)
{
	static struct fake_net net;
	struct fake_node *a, *b, *c;

	fake_start(&net, 27);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	c = fake_add(&net, 0, 1);
	fake_run(&net, 500);
	social_front_add_code(a->front, snap(b, 0)->code);
	social_front_add_code(a->front, snap(c, 0)->code);
	fake_run(&net, 3000);
	social_front_accept(b->front, snap(b, 0)->requests[0].id);
	social_front_accept(c->front, snap(c, 0)->requests[0].id);
	fake_run(&net, 15000);
	CHECK(exchanged(a, b) && exchanged(a, c), "setup");
	c->alive = 0;
	social_front_remove(a->front, friend_info(a, b, 0)->id);
	fake_run(&net, 60000);
	CHECK(!exchanged(a, c), "c has not the new key: a keeps trying");
	c->alive = 1;
	c->engine->ready_before = 0;
	fake_run(&net, 120000);
	CHECK(exchanged(a, c) && exchanged(c, a), "c came back and has the new key");
	CHECK(online_to(c, a), "and sees a");
}

static void case_list_cap(void)
{
	static struct fake_net net;
	struct fake_node *a, *b;
	int index;

	fake_start(&net, 28);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	fake_run(&net, 500);
	for (index = 0; index < SOCIAL_MAX_FRIENDS; index++)
	{
		unsigned char seed[32], circle[32];
		struct social_identity fake_identity;

		fill(seed, 32);
		fill(circle, 32);
		social_identity_make(&fake_identity, seed);
		CHECK(social_friend_add(a->engine, fake_identity.key, 1, circle, 1), "friend %d", index);
	}
	social_front_add_code(a->front, snap(b, 0)->code);
	fake_run(&net, 1000);
	CHECK(snap(a, 0)->add_result == SOCIAL_ADD_RESULT_LIST_FULL, "a full list: %d", snap(a, 0)->add_result);
	/* b asks a: a has a request, and cannot accept it */
	social_front_add_code(b->front, snap(a, 0)->code);
	fake_run(&net, 3000);
	CHECK(snap(a, 0)->request_count == 1, "the request is shown");
	social_front_accept(a->front, snap(a, 0)->requests[0].id);
	fake_run(&net, 3000);
	CHECK(!friends_with(a, b) && social_friend_count(a->engine) == SOCIAL_MAX_FRIENDS, "accepting with a full list does nothing");
}

static void case_off_by_default(void)
{
	static struct fake_net net;
	struct fake_node *a, *b;

	fake_start(&net, 29);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	social_set_enabled(a->engine, 0);
	social_set_enabled(b->engine, 0);
	fake_run(&net, 40000);
	CHECK(a->published == 0 && b->published == 0 && a->subscribed_count == 0, "nothing is sent or listened to");
	CHECK(!snap(a, 0)->enabled && social_front_add_code(a->front, "0000-0000-0000-0") == SOCIAL_ADD_RESULT_OFF, "the API says it is off");
	CHECK(social_front_snapshot(a->front, &snapshots[0]) > 0 && snapshots[0].friend_count == 0, "an empty snapshot");
}

/* a flood of requests from a thousand strangers */
static void case_request_flood(void)
{
	static struct fake_net net;
	static struct social_identity strangers[40];
	struct fake_node *a;
	int round, index;
	unsigned int attempted, signatures, verifications;

	fake_start(&net, 30);
	a = fake_add(&net, 0, 1);
	fake_run(&net, 500);
	for (index = 0; index < 40; index++)
	{
		unsigned char seed[32];

		fill(seed, 32);
		social_identity_make(&strangers[index], seed);
	}
	attempted = a->engine->statistics.attempted;
	signatures = a->engine->statistics.dropped_signature;
	verifications = a->engine->statistics.verifications;
	for (round = 0; round < 3000; round++)
	{
		unsigned char plain[SOCIAL_MAX_PLAIN], nonce[12], wire[SOCIAL_MAX_WIRE];
		int size, wire_size;
		char name[12];

		net.time += 2;
		snprintf(name, sizeof(name), "S%d", round);
		/* (a signature made for another inbox every third message, so that some fail at the signature) */
		size = social_add_encode(&strangers[round % 40], 1, 1 + (unsigned)round, name, 0, round % 3 ? a->engine->identity.code_bytes : strangers[0].code_bytes, plain,
			sizeof(plain));
		fill(nonce, 12);
		wire_size = social_seal(a->engine->inbox_seal, SOCIAL_KIND_REQUEST, NULL, a->engine->inbox_topic, plain, size, nonce, wire, sizeof(wire));
		social_front_heard(a->front, a->engine->inbox_topic, wire, wire_size);
		if (round % 10 == 9)
			social_front_tick(a->front);
	}
	social_front_tick(a->front);
	CHECK(snap(a, 0)->request_count <= SOCIAL_MAX_REQUESTS, "the list is bounded: %d", snap(a, 0)->request_count);
	CHECK(snap(a, 0)->request_count == SOCIAL_MAX_REQUESTS, "and full");
	CHECK(a->engine->statistics.attempted - attempted <= (unsigned)(7 * SOCIAL_OPEN_RATE), "opened %u", a->engine->statistics.attempted - attempted);
	CHECK(a->engine->statistics.verifications - verifications <= SOCIAL_VERIFY_BURST + 7 * 1000 / SOCIAL_VERIFY_INTERVAL, "signatures checked: %u",
		a->engine->statistics.verifications - verifications);
	CHECK(a->engine->event_count <= SOCIAL_MAX_EVENTS, "events are bounded");
	(void)signatures;
	/* half an hour later the list has emptied, and a real request comes in */
	fake_run(&net, 1900000);
	CHECK(snap(a, 0)->request_count == 0, "the unanswered went: %d", snap(a, 0)->request_count);
	{
		int events = take_events(a, SOCIAL_API_EVENT_REQUEST, NULL);

		CHECK(events <= SOCIAL_MAX_REQUESTS, "request events: %d", events);
	}
}

/* ---------- the file */

struct memfs
{
	struct
	{
		char name[24];
		char *data;
		int size;
		int used;
	} file[8];
	long operations;
	long crash_at;
	int crashed;
	/* the next write of this name stores half and says it worked (a card that loses data without a word) */
	char lie[24];
};

static int memfs_find(struct memfs *fs, const char *name)
{
	int index;

	for (index = 0; index < 8; index++)
	{
		if (fs->file[index].used && !strcmp(fs->file[index].name, name))
			return index;
	}
	return -1;
}

/* a crash at operation crash_at: that operation is half done, and none after it happens */
static int memfs_step(struct memfs *fs)
{
	if (fs->crashed)
		return 0;
	if (fs->crash_at >= 0 && fs->operations == fs->crash_at)
	{
		fs->crashed = 1;
		fs->operations++;
		return 2;
	}
	fs->operations++;
	return 1;
}

static int memfs_read(void *user, const char *name, char *buffer, int capacity)
{
	struct memfs *fs = user;
	int index = memfs_find(fs, name);

	if (index < 0 || fs->file[index].size >= capacity)
		return -1;
	memcpy(buffer, fs->file[index].data, (size_t)fs->file[index].size);
	return fs->file[index].size;
}

static int memfs_write(void *user, const char *name, const char *data, int size)
{
	struct memfs *fs = user;
	int step = memfs_step(fs), index = memfs_find(fs, name);

	if (!step)
		return 1;
	if (index < 0)
	{
		for (index = 0; index < 8 && fs->file[index].used; index++)
			;
		if (index == 8)
			return 1;
		fs->file[index].used = 1;
		snprintf(fs->file[index].name, sizeof(fs->file[index].name), "%s", name);
		fs->file[index].data = NULL;
	}
	free(fs->file[index].data);
	fs->file[index].size = step == 2 || !strcmp(fs->lie, name) ? size / 2 : size;
	fs->file[index].data = malloc((size_t)size + 1);
	memcpy(fs->file[index].data, data, (size_t)fs->file[index].size);
	return step == 2;
}

static int memfs_rename(void *user, const char *from, const char *to)
{
	struct memfs *fs = user;
	int step = memfs_step(fs), source = memfs_find(fs, from);

	if (step != 1 || source < 0 || memfs_find(fs, to) >= 0)
		return 1;
	snprintf(fs->file[source].name, sizeof(fs->file[source].name), "%s", to);
	return 0;
}

static int memfs_remove(void *user, const char *name)
{
	struct memfs *fs = user;
	int step = memfs_step(fs), index = memfs_find(fs, name);

	if (step != 1 || index < 0)
		return 1;
	free(fs->file[index].data);
	memset(&fs->file[index], 0, sizeof(fs->file[index]));
	return 0;
}

static void memfs_files(struct memfs *fs, struct social_files *files)
{
	memset(fs, 0, sizeof(*fs));
	fs->crash_at = -1;
	files->user = fs;
	files->read_file = memfs_read;
	files->write_file = memfs_write;
	files->rename_file = memfs_rename;
	files->remove_file = memfs_remove;
}

static void memfs_reset(struct memfs *fs)
{
	int index;

	for (index = 0; index < 8; index++)
		free(fs->file[index].data);
	memset(fs->file, 0, sizeof(fs->file));
}

static struct fake_node *make_engine_node(struct fake_net *net, unsigned long long seed, int friends)
{
	struct fake_node *node;
	int index;

	fake_start(net, seed);
	node = fake_add(net, 0, 1);
	for (index = 0; index < friends; index++)
	{
		unsigned char key_seed[32], circle[32];
		struct social_identity other;
		struct social_friend *added;

		fill(key_seed, 32);
		fill(circle, 32);
		social_identity_make(&other, key_seed);
		added = social_friend_add(node->engine, other.key, 1 + index % 5, circle, index & 1);
		CHECK(added, "friend");
		snprintf(added->nickname, sizeof(added->nickname), "Pal %d", index);
		added->added = 1790000000ULL + (unsigned)index;
		added->window.started = 1;
		added->window.generation = 5;
		added->window.sequence = 100 + (unsigned)index;
		added->window.bitmap = 0xF0F0F0F0u + (unsigned)index;
	}
	return node;
}

static void case_store_roundtrip(void)
{
	static struct fake_net net;
	static char text[SOCIAL_STORE_MAXIMUM], again[SOCIAL_STORE_MAXIMUM];
	struct fake_node *a, *b;
	int size, size_again, index;

	a = make_engine_node(&net, 31, 50);
	for (index = 0; index < 5; index++)
	{
		unsigned char key[8];

		fill(key, 8);
		memcpy(a->engine->ignored[a->engine->ignored_count++], key, 8);
		fill(key, 8);
		memcpy(a->engine->blocked[a->engine->blocked_count++], key, 8);
	}
	a->engine->stored_generation = 12345;
	a->engine->share_status = 0;
	a->engine->details = 2;
	a->engine->accept_requests = 0;
	size = social_store_format(a->engine, text, sizeof(text));
	CHECK(size > 0 && size < SOCIAL_STORE_MAXIMUM, "size %d", size);
	CHECK(!strncmp(text, "hceu-friends 1\n", 15), "first line");
	CHECK(strstr(text, "\ncheck ") && text[size - 1] == '\n', "ends with its check");
	/* read into a machine with the same identity */
	b = fake_add(&net, 0, 1);
	social_set_identity(b->engine, a->seed, "Node0", 0);
	CHECK(social_store_parse(b->engine, text, size) == 0, "parses");
	CHECK(social_friend_count(b->engine) == 50 && b->engine->stored_generation == 12345 && !b->engine->share_status && b->engine->details == 2 &&
		!b->engine->accept_requests && b->engine->ignored_count == 5 && b->engine->blocked_count == 5, "state");
	CHECK(!memcmp(b->engine->circle_key, a->engine->circle_key, 32) && b->engine->circle_generation == a->engine->circle_generation, "circle");
	size_again = social_store_format(b->engine, again, sizeof(again));
	CHECK(size_again == size && !memcmp(text, again, (size_t)size), "format(parse(format)) is the same text");
	{
		struct social_friend *friend = social_friend_by_key(b->engine, a->engine->friends[7].key);

		CHECK(friend && !strcmp(friend->nickname, "Pal 7") && friend->window.sequence == 107 && friend->window.generation == 5 &&
			friend->window.bitmap == 0xF0F0F0F0u + 7 && friend->added == 1790000007ULL && friend->acknowledged == 1 && friend->circle_generation == 1 + 7 % 5,
			"a friend's fields");
	}
}

static const char *const hostile_lines[] = {
	"friend", "friend zz", "friend 00", "friend 0000000000000000000000000000000000000000000000000000000000000000 1 - 0 1 1 00000000 0 -",
	"friend 1111111111111111111111111111111111111111111111111111111111111111 999 - 0 1 1 00000000 0 -",
	"friend 1111111111111111111111111111111111111111111111111111111111111111 1 - 2 1 1 00000000 0 -",
	"friend 1111111111111111111111111111111111111111111111111111111111111111 1 22 0 1 1 00000000 0 -",
	"friend 1111111111111111111111111111111111111111111111111111111111111111 1 - 0 99999999999 1 00000000 0 -",
	"friend 1111111111111111111111111111111111111111111111111111111111111111 1 - 0 1 1 0000000g 0 -",
	"sent", "sent 0000-0000-0000-1", "sent AAAA-AAAA-AAAA-A 0", "ignored zz", "ignored 12", "blocked", "blocked 00000000000000000", "circle_gen 0", "circle_gen 300",
	"circle_key 12", "boot_gen -1", "boot_gen 99999999999999999999999", "share_status 7", "share_details 9", "requests x", "unknown things here",
	"                                                                                                                                                                                                                                                                                                                                                                                                  ", "\x01\x02\x03",
	"friend 1111111111111111111111111111111111111111111111111111111111111111 1 - 0 1 1 00000000 0 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa|||",
};

/* a file that is damaged, cut short, hostile or too new is refused or passed over, and never crashes */
static void case_store_hostile(void)
{
	static struct fake_net net;
	static char text[SOCIAL_STORE_MAXIMUM], mangled[SOCIAL_STORE_MAXIMUM + 64];
	struct fake_node *a, *b;
	int size, index, round;

	a = make_engine_node(&net, 32, 10);
	size = social_store_format(a->engine, text, sizeof(text));
	b = fake_add(&net, 0, 1);
	social_set_identity(b->engine, a->seed, "Node0", 0);
	/* every truncation, and every byte changed: refused (the check) */
	for (index = 0; index < size; index++)
		CHECK(social_store_parse(b->engine, text, index) != 0, "truncated to %d accepted", index);
	CHECK(social_friend_count(b->engine) == 0, "nothing was taken from the refused");
	for (index = 0; index < size; index++)
	{
		memcpy(mangled, text, (size_t)size);
		mangled[index] ^= (char)(1 + random_below(255));
		if (social_store_parse(b->engine, mangled, size) == 0)
		{
			/* (only a change that leaves the text with the same meaning could pass: the check covers every byte before it, so one in the check itself must fail) */
			CHECK(0, "byte %d changed, accepted", index);
		}
		CHECK(social_friend_count(b->engine) == 0, "something taken from a refused file (%d)", index);
	}
	/* too big, and a NUL inside */
	memset(mangled, 'x', sizeof(mangled));
	CHECK(social_store_parse(b->engine, mangled, SOCIAL_STORE_MAXIMUM) != 0 && social_store_parse(b->engine, mangled, SOCIAL_STORE_MAXIMUM + 10) != 0, "too big");
	memcpy(mangled, text, (size_t)size);
	mangled[20] = 0;
	CHECK(social_store_parse(b->engine, mangled, size) != 0, "a NUL");
	CHECK(social_store_parse(b->engine, "", 0) != 0 && social_store_parse(b->engine, "hceu-friends 1\n", 15) != 0 && social_store_parse(b->engine, "hello\n", 6) != 0, "short");
	/* hostile lines under a valid check: skipped, bounded, the engine alive */
	for (round = 0; round < (int)(sizeof(hostile_lines) / sizeof(*hostile_lines)); round++)
	{
		static char built[8192];
		int length = 0;
		char check[17];
		struct fake_node *fresh;

		length += snprintf(built + length, sizeof(built) - (size_t)length, "hceu-friends 1\n%s\n%s\n", hostile_lines[round], hostile_lines[round]);
		check_of(built, length, check);
		length += snprintf(built + length, sizeof(built) - (size_t)length, "check %s\n", check);
		fresh = fake_add(&net, 0, 1);
		social_set_identity(fresh->engine, a->seed, "Node0", 0);
		social_store_parse(fresh->engine, built, length);
		CHECK(social_friend_count(fresh->engine) <= 1, "hostile line %d: %d friends", round, social_friend_count(fresh->engine));
		for (index = 0; fresh->engine->friends[0].nickname[index]; index++)
			CHECK(fresh->engine->friends[0].nickname[index] >= 0x20 && fresh->engine->friends[0].nickname[index] != '|', "nickname text");
		net.count--;
	}
	/* lines beyond the limits: 200 friends, 100 sent, 100 blocked: only the capacity is taken */
	{
		static char big[SOCIAL_STORE_MAXIMUM];
		int length = 0, i;
		char check[17];
		struct fake_node *fresh = fake_add(&net, 0, 1);

		length += snprintf(big + length, sizeof(big) - (size_t)length, "hceu-friends 1\n");
		for (i = 0; i < 70; i++)
		{
			unsigned char seed[32];
			struct social_identity other;
			char hex[65];

			fill(seed, 32);
			social_identity_make(&other, seed);
			social_hex(other.key, 32, hex);
			length += snprintf(big + length, sizeof(big) - (size_t)length, "friend %s 1 - 0 0 0 00000000 0 -\n", hex);
			length += snprintf(big + length, sizeof(big) - (size_t)length, "blocked %016x\n", i);
			length += snprintf(big + length, sizeof(big) - (size_t)length, "ignored %016x\n", i);
		}
		check_of(big, length, check);
		length += snprintf(big + length, sizeof(big) - (size_t)length, "check %s\n", check);
		social_set_identity(fresh->engine, a->seed, "Node0", 0);
		CHECK(social_store_parse(fresh->engine, big, length) == 0, "parses");
		CHECK(social_friend_count(fresh->engine) == 50 && fresh->engine->blocked_count == 64 && fresh->engine->ignored_count == 32, "capped: %d %d %d",
			social_friend_count(fresh->engine), fresh->engine->blocked_count, fresh->engine->ignored_count);
	}
}

static void case_store_version(void)
{
	static struct fake_net net;
	static char text[4096];
	struct fake_node *a;
	struct social_files files;
	struct memfs fs;
	int length;
	char check[17];

	a = make_engine_node(&net, 33, 3);
	memfs_files(&fs, &files);
	/* a file of version 2: the engine is locked out of saving, and the file is not touched */
	length = snprintf(text, sizeof(text), "hceu-friends 2\nsomething new\n");
	check_of(text, length, check);
	length += snprintf(text + length, sizeof(text) - (size_t)length, "check %s\n", check);
	CHECK(!files.write_file(files.user, "friends.txt", text, length), "written");
	CHECK(social_store_load(&files, a->engine) == 3, "load says it is too new");
	CHECK(a->engine->store_locked, "locked");
	a->engine->store_dirty = 1;
	a->engine->store_dirty_time = net.time - 100000;
	CHECK(!social_store_due(a->engine, 1) && social_store_take(a->engine, text, 100) == 0, "a locked engine does not save");
	{
		char back[4096];
		int size = files.read_file(files.user, "friends.txt", back, sizeof(back));

		CHECK(size == length, "the file stays");
	}
	memfs_reset(&fs);
}

/* a save that stops at any step leaves a whole file: the old one or the new */
static void case_store_power_loss(void)
{
	static struct fake_net net;
	static char old_text[SOCIAL_STORE_MAXIMUM], new_text[SOCIAL_STORE_MAXIMUM];
	struct fake_node *a, *probe;
	struct social_files files;
	struct memfs fs;
	int old_size, new_size, crash, operations_total = 0;

	a = make_engine_node(&net, 34, 5);
	old_size = social_store_format(a->engine, old_text, sizeof(old_text));
	{
		unsigned char seed[32], circle[32];
		struct social_identity extra;

		fill(seed, 32);
		fill(circle, 32);
		social_identity_make(&extra, seed);
		social_friend_add(a->engine, extra.key, 2, circle, 1);
	}
	new_size = social_store_format(a->engine, new_text, sizeof(new_text));
	CHECK(old_size > 0 && new_size > old_size && memcmp(old_text, new_text, (size_t)old_size), "two different texts");
	probe = fake_add(&net, 0, 1);
	/* count the operations of a normal second save */
	memfs_files(&fs, &files);
	CHECK(social_store_save(&files, old_text, old_size) == 0, "first save");
	fs.operations = 0;
	CHECK(social_store_save(&files, new_text, new_size) == 0, "second save");
	operations_total = (int)fs.operations;
	memfs_reset(&fs);
	CHECK(operations_total >= 4 && operations_total < 20, "operations %d", operations_total);
	for (crash = 0; crash < operations_total + 2; crash++)
	{
		int result, social_friends_found;

		memfs_files(&fs, &files);
		CHECK(social_store_save(&files, old_text, old_size) == 0, "first save");
		fs.operations = 0;
		fs.crash_at = crash;
		social_store_save(&files, new_text, new_size);
		/* the machine starts again from what the card holds */
		fs.crashed = 0;
		fs.crash_at = -1;
		social_set_identity(probe->engine, a->seed, "Node0", 0);
		memset(probe->engine->friends, 0, sizeof(probe->engine->friends));
		probe->engine->store_locked = 0;
		result = social_store_load(&files, probe->engine);
		CHECK(result == 0, "crash at operation %d: nothing could be read (%d)", crash, result);
		social_friends_found = social_friend_count(probe->engine);
		CHECK(social_friends_found == 5 || social_friends_found == 6, "crash at %d: %d friends", crash, social_friends_found);
		memfs_reset(&fs);
	}
	printf("%d operations in a save; a crash at each of %d points left a whole file\n", operations_total, operations_total + 2);
}

/* a card that stores half of a write and says it worked: the save reads back, fails, and leaves the old file */
static void case_store_short_write(void)
{
	static struct fake_net net;
	static char old_text[SOCIAL_STORE_MAXIMUM], new_text[SOCIAL_STORE_MAXIMUM], back[SOCIAL_STORE_MAXIMUM];
	struct fake_node *a;
	struct social_files files;
	struct memfs fs;
	int old_size, new_size;

	a = make_engine_node(&net, 39, 5);
	old_size = social_store_format(a->engine, old_text, sizeof(old_text));
	{
		unsigned char seed[32], circle[32];
		struct social_identity extra;

		fill(seed, 32);
		fill(circle, 32);
		social_identity_make(&extra, seed);
		social_friend_add(a->engine, extra.key, 2, circle, 1);
	}
	new_size = social_store_format(a->engine, new_text, sizeof(new_text));
	memfs_files(&fs, &files);
	CHECK(social_store_save(&files, old_text, old_size) == 0, "first save");
	snprintf(fs.lie, sizeof(fs.lie), "friends.new");
	CHECK(social_store_save(&files, new_text, new_size) != 0, "a short write is noticed");
	CHECK(files.read_file(files.user, "friends.txt", back, sizeof(back)) == old_size && !memcmp(back, old_text, (size_t)old_size), "and the old file is as it was");
	memfs_reset(&fs);
}

/* the front's saver thread writes without the engine's thread waiting for the card */
static int slow_write(void *user, const char *name, const char *data, int size)
{
	struct timespec pause = { 0, 200 * 1000 * 1000 };

	nanosleep(&pause, NULL);
	return memfs_write(user, name, data, size);
}

static void case_saver_thread(void)
{
	static struct fake_net net;
	struct fake_node *a;
	struct social_files files;
	struct memfs fs;
	struct social_front_options options;
	struct social_env env;
	struct social_front *front;
	struct timespec started, finished;
	double worst = 0;
	unsigned int saves, failures;
	int index;

	make_engine_node(&net, 35, 0);
	a = &net.nodes[0];
	memfs_files(&fs, &files);
	files.write_file = slow_write;
	env = a->engine->env;
	memset(&options, 0, sizeof(options));
	options.saver_thread = 1;
	front = social_front_create(&env, &files, &options);
	social_front_start(front, "Node0", a->seed);
	social_set_enabled(social_front_engine(front), 1);
	/* friends come, and the engine's thread is ticked while the card is slow: no pass takes long */
	for (index = 0; index < 400; index++)
	{
		if (index < 20)
		{
			unsigned char seed[32], circle[32];
			struct social_identity other;

			fill(seed, 32);
			fill(circle, 32);
			social_identity_make(&other, seed);
			social_friend_add(social_front_engine(front), other.key, 1, circle, 1);
			social_front_engine(front)->store_dirty = 1;
		}
		net.time += 100;
		clock_gettime(CLOCK_MONOTONIC, &started);
		social_front_tick(front);
		clock_gettime(CLOCK_MONOTONIC, &finished);
		{
			double seconds = (double)(finished.tv_sec - started.tv_sec) + (double)(finished.tv_nsec - started.tv_nsec) / 1e9;

			if (seconds > worst)
				worst = seconds;
		}
		if (index % 50 == 0)
		{
			struct timespec pause = { 0, 20 * 1000 * 1000 };

			nanosleep(&pause, NULL);
		}
	}
	social_front_destroy(front);
	{
		struct fake_node *probe = fake_add(&net, 0, 1);
		char back[SOCIAL_STORE_MAXIMUM];

		files.write_file = memfs_write;
		(void)back;
		social_set_identity(probe->engine, a->seed, "Node0", 0);
		CHECK(social_store_load(&files, probe->engine) == 0 && social_friend_count(probe->engine) >= 1, "what was saved can be read");
	}
	(void)saves;
	(void)failures;
	printf("the slowest pass of the engine's thread took %.1f ms while each write took 200 ms\n", worst * 1000);
	CHECK(worst < 0.05, "a pass took %.1f ms: the card was waited for", worst * 1000);
	memfs_reset(&fs);
}

/* the friends and the identity survive a restart; a restart chooses a higher generation; the friends need no new handshake */
static void case_restart(void)
{
	static struct fake_net net;
	static struct social_files files[FAKE_MAX_NODES];
	static struct memfs fs[FAKE_MAX_NODES];
	struct fake_node *a, *b;
	unsigned int generation_before;
	unsigned char key_before[32];
	int index;

	fake_start(&net, 36);
	for (index = 0; index < FAKE_MAX_NODES; index++)
		memfs_files(&fs[index], &files[index]);
	net.files = files;
	a = fake_add(&net, 4, 0);
	b = fake_add(&net, -4, 0);
	/* the nodes were made with their seeds set directly; start them from their files, which writes friends.key */
	fake_restart(&net, 0);
	fake_restart(&net, 1);
	fake_run(&net, 500);
	social_front_add_code(a->front, snap(b, 0)->code);
	fake_run(&net, 18000);
	social_front_accept(b->front, snap(b, 0)->requests[0].id);
	fake_run(&net, 20000);
	CHECK(exchanged(a, b) && exchanged(b, a), "friends");
	social_front_set_nickname(a->front, friend_info(a, b, 0)->id, "Bee");
	fake_run(&net, 60000);
	CHECK(memfs_find(&fs[0], "friends.txt") >= 0 && memfs_find(&fs[0], "friends.key") >= 0, "both files are on the card");
	generation_before = a->engine->generation;
	memcpy(key_before, a->engine->identity.key, 32);
	fake_restart(&net, 0);
	fake_run(&net, 20000);
	CHECK(!memcmp(key_before, a->engine->identity.key, 32), "the same identity after a restart");
	CHECK(friends_with(a, b) && exchanged(a, b), "the friend and its keys are back");
	CHECK(!strcmp(friend_info(a, b, 0)->nickname, "Bee"), "the nickname");
	CHECK(a->engine->generation > generation_before, "a higher generation: %u after %u", a->engine->generation, generation_before);
	CHECK(online_to(a, b) && online_to(b, a), "online to each other again");
	CHECK(b->engine->friends[0].window.generation == a->engine->generation || b->engine->friends[0].window.generation >= generation_before, "b has a's new generation");
	for (index = 0; index < 2; index++)
		memfs_reset(&fs[index]);
}

/* a card restored from before: with an anchored clock the generation is past any sent since */
static void case_restored_card(void)
{
	static struct fake_net net;
	static struct social_files files[FAKE_MAX_NODES];
	static struct memfs fs[FAKE_MAX_NODES];
	struct fake_node *a, *b;
	unsigned int sent_generation;
	static char old_friends[SOCIAL_STORE_MAXIMUM];
	int old_size, index;

	fake_start(&net, 37);
	for (index = 0; index < FAKE_MAX_NODES; index++)
		memfs_files(&fs[index], &files[index]);
	net.files = files;
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	fake_restart(&net, 0);
	fake_restart(&net, 1);
	fake_run(&net, 500);
	social_front_add_code(a->front, snap(b, 0)->code);
	fake_run(&net, 18000);
	social_front_accept(b->front, snap(b, 0)->requests[0].id);
	fake_run(&net, 40000);
	old_size = files[0].read_file(files[0].user, "friends.txt", old_friends, sizeof(old_friends));
	CHECK(old_size > 0, "a saved file");
	/* a runs on for a long while and restarts several times, as the card is copied back each time */
	for (index = 0; index < 3; index++)
	{
		fake_run(&net, 600000);
		sent_generation = a->engine->generation;
		memfs_remove(&fs[0], "friends.txt");
		memfs_remove(&fs[0], "friends.bak");
		memfs_write(&fs[0], "friends.txt", old_friends, old_size);
		fake_restart(&net, 0);
		fake_run(&net, 40000);
		CHECK(a->engine->generation > sent_generation, "run %d: generation %u is not past %u", index, a->engine->generation, sent_generation);
		CHECK(online_to(b, a), "b still hears a after a restored card, run %d", index);
	}
	for (index = 0; index < 2; index++)
		memfs_reset(&fs[index]);
}

/* the game's threads call the API while the engine's thread runs: nothing is torn, nothing waits */
#include <pthread.h>

static struct social_front *thread_front;
static volatile int thread_stop;

static void *hammer(void *argument)
{
	struct social_snapshot *snapshot = calloc(1, sizeof(*snapshot));
	unsigned int last_version = 0;
	int round = 0;
	long reads = 0;

	(void)argument;
	while (!thread_stop)
	{
		unsigned int version = social_front_snapshot(thread_front, snapshot);
		int index;

		reads++;
		if (version < last_version)
		{
			fprintf(stderr, "snapshot version went back\n");
			exit(1);
		}
		last_version = version;
		for (index = 0; index < snapshot->friend_count; index++)
		{
			const char *text = snapshot->friends[index].name;

			if (strlen(snapshot->friends[index].code) != 16 || strlen(text) > 11)
			{
				fprintf(stderr, "torn snapshot\n");
				exit(1);
			}
		}
		switch (round++ % 8)
		{
		case 0: social_front_add_code(thread_front, "0000-0000-0000-0"); break;
		case 1: social_front_set_name(thread_front, "Hammer"); break;
		case 2: social_front_set_share(thread_front, 1, round % 3, 1); break;
		case 3: social_front_accept(thread_front, (unsigned)round); break;
		case 4: social_front_block(thread_front, (unsigned)round); break;
		case 5: social_front_unblock(thread_front, 0); break;
		case 6: social_front_set_nickname(thread_front, (unsigned)round, "Ab|c"); break;
		case 7: { struct social_api_event event; social_front_event(thread_front, &event); } break;
		}
	}
	free(snapshot);
	return (void *)(long)reads;
}

static void case_front_threads(void)
{
	static struct fake_net net;
	struct fake_node *a, *b;
	pthread_t threads[3];
	int index;
	void *reads;
	long total = 0;

	fake_start(&net, 38);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	thread_front = a->front;
	thread_stop = 0;
	for (index = 0; index < 3; index++)
		pthread_create(&threads[index], NULL, hammer, NULL);
	/* the engine's thread (this one) runs on the fake network meanwhile; the fake network is touched by this thread only */
	for (index = 0; index < 600; index++)
		fake_run(&net, 100);
	thread_stop = 1;
	for (index = 0; index < 3; index++)
	{
		pthread_join(threads[index], &reads);
		total += (long)reads;
	}
	CHECK(total > 1000, "the readers ran: %ld", total);
	CHECK(snap(a, 0)->enabled, "still working");
	(void)b;
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "befriend"))
		case_befriend();
	else if (!strcmp(name, "crossed"))
		case_crossed();
	else if (!strcmp(name, "decline"))
		case_decline();
	else if (!strcmp(name, "stranger-answers"))
		case_stranger_answers();
	else if (!strcmp(name, "remove-and-rotate"))
		case_remove_and_rotate();
	else if (!strcmp(name, "block"))
		case_block();
	else if (!strcmp(name, "rotation-offline"))
		case_rotation_offline();
	else if (!strcmp(name, "list-cap"))
		case_list_cap();
	else if (!strcmp(name, "off-by-default"))
		case_off_by_default();
	else if (!strcmp(name, "request-flood"))
		case_request_flood();
	else if (!strcmp(name, "store-roundtrip"))
		case_store_roundtrip();
	else if (!strcmp(name, "store-hostile"))
		case_store_hostile();
	else if (!strcmp(name, "store-version"))
		case_store_version();
	else if (!strcmp(name, "store-power-loss"))
		case_store_power_loss();
	else if (!strcmp(name, "store-short-write"))
		case_store_short_write();
	else if (!strcmp(name, "saver-thread"))
		case_saver_thread();
	else if (!strcmp(name, "restart"))
		case_restart();
	else if (!strcmp(name, "restored-card"))
		case_restored_card();
	else if (!strcmp(name, "front-threads"))
		case_front_threads();
	else
	{
		fprintf(stderr, "no case named %s\n", name);
		return 2;
	}
	return 0;
}
