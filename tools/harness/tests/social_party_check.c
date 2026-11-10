/* social_party_check.c: parties of up to eight (port/linux/game/social_party.c) on the fake network of social_fake.h: making and joining, the leader's list, a leader
that leaves, falls silent or hands over, two claims of one term, partitions, kicking, party chat, following the leader's game, and a malicious member. Cases are named
on the command line (test_social_party.py). */

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

static unsigned long long test_state = 0x99ULL;
#define fake_start(net, seed) (fake_start((net), (seed)), (net)->verbose = getenv("SOCIAL_VERBOSE") != NULL)

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

/* the node that leads in the node's eyes (its name), "" if none */
static const char *leader_name(struct fake_node *node)
{
	const struct social_snapshot *snapshot = snap(node, 0);
	int index;

	for (index = 0; index < snapshot->party.member_count; index++)
	{
		if (snapshot->party.members[index].leader)
			return snapshot->party.members[index].name;
	}
	return "";
}

static int members_seen(struct fake_node *node)
{
	return snap(node, 0)->party.member_count;
}

/* runs until the condition holds, at most max_ms; returns how long it took (max_ms + 1 if it never did) */
#define WAIT_FOR(net, max_ms, condition, ...) \
	({ \
		unsigned int waited = 0; \
		while (waited <= (max_ms) && !(condition)) \
		{ \
			fake_run((net), 100); \
			waited += 100; \
		} \
		CHECK(waited <= (max_ms), __VA_ARGS__); \
		waited; \
	})

/* (a node accepts the party invite it holds, through its front: the global function reads one front) */
static void social_party_join_invite_for_impl(struct fake_net *net, int index)
{
	const struct social_snapshot *snapshot = snap(&net->nodes[index], 0);
	int i;

	for (i = 0; i < snapshot->invite_count; i++)
	{
		if (snapshot->invites[i].kind == 2)
		{
			social_front_party_token(net->nodes[index].front, snapshot->invites[i].party_token);
			social_front_invite_dismiss(net->nodes[index].front, snapshot->invites[i].id);
			return;
		}
	}
	CHECK(0, "no party invite for node %d", index);
}

#define social_party_join_invite_for social_party_join_invite_for_impl

/* a leader, node 0, and the others its friends, all invited and joined */
static void form_party(struct fake_net *net, int members)
{
	int index;

	for (index = 1; index < members; index++)
		link_friends(&net->nodes[0], &net->nodes[index]);
	fake_run(net, 17000);
	social_front_party(net->nodes[0].front, 1, 0, 0, NULL);
	fake_run(net, 500);
	CHECK(snap(&net->nodes[0], 0)->party.active && snap(&net->nodes[0], 0)->party.is_leader, "the party is made");
	for (index = 1; index < members; index++)
	{
		social_front_party(net->nodes[0].front, 2, friend_id(&net->nodes[0], &net->nodes[index]), 0, NULL);
		/* (an invite a friend at a time, ten seconds apart: the least between two to one friend is not between two friends) */
		fake_run(net, 700);
	}
	for (index = 1; index < members; index++)
	{
		const struct social_snapshot *snapshot = snap(&net->nodes[index], 0);

		CHECK(snapshot->invite_count >= 1, "node %d has the invite", index);
		social_party_join_invite_for(net, index);
	}
}

static void case_basic(void)
{
	static struct fake_net net;
	struct fake_node *a, *b;
	struct social_api_event event;
	unsigned int took;

	fake_start(&net, 51);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	link_friends(a, b);
	fake_run(&net, 6000);
	social_front_party(b->front, 2, friend_id(b, a), 0, NULL);
	fake_run(&net, 300);
	CHECK(snap(b, 0)->party.result == SOCIAL_PARTY_RESULT_NOT_IN_PARTY, "not in a party");
	social_front_party(a->front, 1, 0, 0, NULL);
	fake_run(&net, 300);
	CHECK(snap(a, 0)->party.active && snap(a, 0)->party.is_leader && snap(a, 0)->party.member_count == 1 && take_events(a, SOCIAL_API_EVENT_PARTY_JOINED, NULL) == 1,
		"a party of one");
	social_front_party(a->front, 1, 0, 0, NULL);
	fake_run(&net, 300);
	CHECK(snap(a, 0)->party.result == SOCIAL_PARTY_RESULT_ALREADY_IN_PARTY, "twice");
	social_front_party(b->front, 1, 0, 0, NULL);
	fake_run(&net, 300);
	social_front_party(b->front, 5, 0, 0, NULL);
	fake_run(&net, 300);
	CHECK(!snap(b, 0)->party.active, "b made one and left it");
	take_events(b, SOCIAL_API_EVENT_PARTY_JOINED, NULL);
	take_events(b, SOCIAL_API_EVENT_PARTY_LEFT, NULL);
	social_front_party(b->front, 2, friend_id(b, a), 0, NULL);
	social_front_party(a->front, 2, friend_id(a, b), 0, NULL);
	fake_run(&net, 2000);
	CHECK(snap(b, 0)->invite_count == 1 && snap(b, 0)->invites[0].kind == 2 && snap(b, 0)->invites[0].party_size == 1, "b has the invite");
	CHECK(take_events(b, SOCIAL_API_EVENT_PARTY_INVITE, &event) == 1 && !strcmp(event.name, "Node0"), "and an event");
	social_party_join_invite_for(&net, 1);
	took = WAIT_FOR(&net, 15000, snap(b, 0)->party.active && !snap(b, 0)->party.joining && members_seen(b) == 2 && members_seen(a) == 2, "joined");
	CHECK(!strcmp(leader_name(a), "Node0") && !strcmp(leader_name(b), "Node0"), "the leader is a: '%s' '%s'", leader_name(a), leader_name(b));
	CHECK(snap(a, 0)->party.members[0].self && snap(b, 0)->party.members[1].self && !snap(b, 0)->party.is_leader, "who is who");
	CHECK(take_events(b, SOCIAL_API_EVENT_PARTY_JOINED, NULL) == 1, "b's joined event");
	CHECK(snap(b, 0)->invite_count == 0, "the invite was used");
	CHECK(snap(a, 0)->party.members[1].online && snap(a, 0)->party.members[1].auto_follow, "b follows by default");
	printf("joined in %u ms of the fake network's time\n", took);
	/* the friend's presence shows it in a party */
	fake_run(&net, 5000);
	{
		const struct social_snapshot *snapshot = snap(a, 0);
		int index;

		for (index = 0; index < snapshot->friend_count; index++)
			CHECK(snapshot->friends[index].in_party, "b shows in a party");
	}
	CHECK(a->engine->statistics.send_dropped == 0 && b->engine->statistics.send_dropped == 0, "nothing was too big or too much to send");
	/* b leaves: a is alone again */
	social_front_party(b->front, 5, 0, 0, NULL);
	WAIT_FOR(&net, 3000, members_seen(a) == 1 && !snap(b, 0)->party.active, "b left");
	CHECK(take_events(b, SOCIAL_API_EVENT_PARTY_LEFT, NULL) == 1, "left event");
	CHECK(snap(a, 0)->party.is_leader, "a still leads");
}

static void case_eight(void)
{
	static struct fake_net net;
	struct fake_node *nine;
	int index;

	fake_start(&net, 52);
	for (index = 0; index < 9; index++)
		fake_add(&net, index * 3 - 12, index & 1);
	form_party(&net, 8);
	WAIT_FOR(&net, 40000, members_seen(&net.nodes[0]) == 8 && members_seen(&net.nodes[7]) == 8 && members_seen(&net.nodes[3]) == 8, "eight members: %d %d %d",
		members_seen(&net.nodes[0]), members_seen(&net.nodes[7]), members_seen(&net.nodes[3]));
	for (index = 1; index < 8; index++)
		CHECK(!strcmp(leader_name(&net.nodes[index]), "Node0"), "node %d agrees on the leader", index);
	/* a ninth: the leader will not invite, and one who asks with the token is not listed */
	nine = &net.nodes[8];
	link_friends(&net.nodes[0], nine);
	nine->engine->presence_wanted = 1;
	net.nodes[0].engine->presence_wanted = 1;
	fake_run(&net, 17000);
	social_front_party(net.nodes[0].front, 2, friend_id(&net.nodes[0], nine), 0, NULL);
	fake_run(&net, 500);
	CHECK(snap(&net.nodes[0], 0)->party.result == SOCIAL_PARTY_RESULT_FULL, "the leader's invite to a ninth: full (%d)", snap(&net.nodes[0], 0)->party.result);
	social_front_party_token(nine->front, net.nodes[0].engine->party.token);
	fake_run(&net, 20000);
	CHECK(!snap(nine, 0)->party.active && take_events(nine, SOCIAL_API_EVENT_PARTY_LEFT, NULL) == 1, "the ninth gave up");
	CHECK(members_seen(&net.nodes[0]) == 8, "and is not listed");
	/* every STATE fits: the largest is the leader's with eight in the list */
	CHECK(net.nodes[0].engine->statistics.send_dropped == 0, "nothing too big");
}

static void case_leader_dies(void)
{
	static struct fake_net net;
	int index;
	unsigned int took;
	struct social_api_event event;

	fake_start(&net, 53);
	for (index = 0; index < 4; index++)
		fake_add(&net, 0, 1);
	form_party(&net, 4);
	WAIT_FOR(&net, 30000, members_seen(&net.nodes[3]) == 4 && members_seen(&net.nodes[1]) == 4, "four");
	for (index = 0; index < 4; index++)
		take_events(&net.nodes[index], SOCIAL_API_EVENT_PARTY_LEADER, NULL);
	/* the leader vanishes */
	net.nodes[0].alive = 0;
	fake_run(&net, 9000);
	CHECK(!strcmp(leader_name(&net.nodes[1]), "Node0"), "still Node0's at 9 s");
	took = WAIT_FOR(&net, 12000, !strcmp(leader_name(&net.nodes[1]), "Node1") && !strcmp(leader_name(&net.nodes[2]), "Node1") && !strcmp(leader_name(&net.nodes[3]), "Node1"),
		"the next in the list leads: '%s' '%s' '%s'", leader_name(&net.nodes[1]), leader_name(&net.nodes[2]), leader_name(&net.nodes[3]));
	printf("a new leader after %u ms more (12 s is the silence)\n", 9000 + took);
	CHECK(snap(&net.nodes[1], 0)->party.is_leader && !snap(&net.nodes[2], 0)->party.is_leader, "node 1 leads, the others follow");
	CHECK(net.nodes[1].engine->party.term == 2 && net.nodes[2].engine->party.term == 2, "term 2");
	CHECK(take_events(&net.nodes[2], SOCIAL_API_EVENT_PARTY_LEADER, &event) >= 1 && !strcmp(event.name, "Node1"), "a leader event");
	WAIT_FOR(&net, 10000, members_seen(&net.nodes[1]) == 3 && members_seen(&net.nodes[2]) == 3, "the old leader is off the list: %d", members_seen(&net.nodes[1]));
	/* the old leader comes back: it learns the new one, who remembers it was a member, and it is listed again, as a member */
	net.nodes[0].alive = 1;
	WAIT_FOR(&net, 25000, members_seen(&net.nodes[1]) == 4 && members_seen(&net.nodes[0]) == 4 && !snap(&net.nodes[0], 0)->party.is_leader && !snap(&net.nodes[0], 0)->party.joining,
		"it is a member again: %d %d", members_seen(&net.nodes[1]), members_seen(&net.nodes[0]));
	CHECK(!strcmp(leader_name(&net.nodes[0]), "Node1") && snap(&net.nodes[1], 0)->party.is_leader, "and node 1 still leads");
}

static void case_leave_transfer(void)
{
	static struct fake_net net;
	int index;

	fake_start(&net, 54);
	for (index = 0; index < 4; index++)
		fake_add(&net, 0, 1);
	form_party(&net, 4);
	WAIT_FOR(&net, 30000, members_seen(&net.nodes[3]) == 4 && members_seen(&net.nodes[1]) == 4 && members_seen(&net.nodes[2]) == 4, "four");
	/* the leader hands the lead to node 2 */
	{
		unsigned int target = 0;
		const struct social_snapshot *snapshot = snap(&net.nodes[0], 0);

		for (index = 0; index < snapshot->party.member_count; index++)
			if (!strcmp(snapshot->party.members[index].name, "Node2"))
				target = snapshot->party.members[index].id;
		CHECK(target, "node 2's id");
		social_front_party(net.nodes[1].front, 7, target, 0, NULL);
		fake_run(&net, 300);
		CHECK(snap(&net.nodes[1], 0)->party.result == SOCIAL_PARTY_RESULT_NOT_LEADER, "only the leader hands over");
		social_front_party(net.nodes[0].front, 7, target, 0, NULL);
	}
	WAIT_FOR(&net, 5000, !strcmp(leader_name(&net.nodes[0]), "Node2") && !strcmp(leader_name(&net.nodes[1]), "Node2") && !strcmp(leader_name(&net.nodes[3]), "Node2") &&
		snap(&net.nodes[2], 0)->party.is_leader, "node 2 leads");
	CHECK(!snap(&net.nodes[0], 0)->party.is_leader, "node 0 does not");
	WAIT_FOR(&net, 8000, members_seen(&net.nodes[0]) == 4 && members_seen(&net.nodes[3]) == 4, "all four still");
	/* the new leader leaves: it names the next */
	social_front_party(net.nodes[2].front, 5, 0, 0, NULL);
	WAIT_FOR(&net, 4000, snap(&net.nodes[0], 0)->party.is_leader || snap(&net.nodes[1], 0)->party.is_leader || snap(&net.nodes[3], 0)->party.is_leader, "someone leads at once");
	fake_run(&net, 1000);
	{
		int leaders = 0;

		for (index = 0; index < 4; index++)
			leaders += snap(&net.nodes[index], 0)->party.active && snap(&net.nodes[index], 0)->party.is_leader;
		CHECK(leaders == 1, "one leader: %d", leaders);
	}
	CHECK(members_seen(&net.nodes[1]) == 3, "three left: %d", members_seen(&net.nodes[1]));
}

/* a member that falls silent is dropped by the leader after thirty seconds, and the others follow the leader's list */
static void case_silent_member(void)
{
	static struct fake_net net;
	int index;

	fake_start(&net, 61);
	for (index = 0; index < 4; index++)
		fake_add(&net, 0, 1);
	form_party(&net, 4);
	WAIT_FOR(&net, 30000, members_seen(&net.nodes[1]) == 4 && members_seen(&net.nodes[2]) == 4 && members_seen(&net.nodes[3]) == 4, "four");
	net.nodes[3].alive = 0;
	fake_run(&net, 20000);
	CHECK(members_seen(&net.nodes[0]) == 4, "not dropped at 20 s");
	WAIT_FOR(&net, 25000, members_seen(&net.nodes[0]) == 3 && members_seen(&net.nodes[1]) == 3 && members_seen(&net.nodes[2]) == 3, "dropped, and the others follow the list: %d %d %d",
		members_seen(&net.nodes[0]), members_seen(&net.nodes[1]), members_seen(&net.nodes[2]));
	/* it comes back: it asks to be listed again, and, once a member, is */
	net.nodes[3].alive = 1;
	WAIT_FOR(&net, 25000, members_seen(&net.nodes[0]) == 4 && members_seen(&net.nodes[3]) == 4 && !snap(&net.nodes[3], 0)->party.joining, "back in the party");
}

static void case_kick(void)
{
	static struct fake_net net;
	int index;
	unsigned int target = 0;

	fake_start(&net, 55);
	for (index = 0; index < 3; index++)
		fake_add(&net, 0, 1);
	form_party(&net, 3);
	WAIT_FOR(&net, 30000, members_seen(&net.nodes[1]) == 3 && members_seen(&net.nodes[2]) == 3, "three");
	{
		const struct social_snapshot *snapshot = snap(&net.nodes[0], 0);

		for (index = 0; index < snapshot->party.member_count; index++)
			if (!strcmp(snapshot->party.members[index].name, "Node2"))
				target = snapshot->party.members[index].id;
	}
	social_front_party(net.nodes[1].front, 6, target, 0, NULL);
	fake_run(&net, 300);
	CHECK(snap(&net.nodes[1], 0)->party.result == SOCIAL_PARTY_RESULT_NOT_LEADER, "members cannot kick");
	social_front_party(net.nodes[0].front, 6, 4242, 0, NULL);
	fake_run(&net, 300);
	CHECK(snap(&net.nodes[0], 0)->party.result == SOCIAL_PARTY_RESULT_NO_SUCH_MEMBER, "no such member");
	take_events(&net.nodes[2], SOCIAL_API_EVENT_PARTY_KICKED, NULL);
	social_front_party(net.nodes[0].front, 6, target, 0, NULL);
	WAIT_FOR(&net, 3000, !snap(&net.nodes[2], 0)->party.active && members_seen(&net.nodes[1]) == 2, "kicked");
	CHECK(take_events(&net.nodes[2], SOCIAL_API_EVENT_PARTY_KICKED, NULL) == 1, "a kicked event");
	/* it holds the token still: asked to be listed, it is refused, however invited it was before */
	social_front_party_token(net.nodes[2].front, net.nodes[0].engine->party.token);
	fake_run(&net, 20000);
	CHECK(!snap(&net.nodes[2], 0)->party.active && members_seen(&net.nodes[0]) == 2, "the kicked member is not let in again");
	CHECK(net.nodes[0].engine->statistics.dropped_blocked > 0, "its words were dropped");
}

/* the leader ends the party: every member hears it, one that is not the leader cannot, and a replay of the old DISBAND does not end a new party */
static void case_disband(void)
{
	static struct fake_net net;
	struct social_api_event event;
	int index;

	fake_start(&net, 58);
	for (index = 0; index < 3; index++)
		fake_add(&net, 0, 1);
	form_party(&net, 3);
	WAIT_FOR(&net, 30000, members_seen(&net.nodes[1]) == 3 && members_seen(&net.nodes[2]) == 3, "three");
	for (index = 0; index < 3; index++)
		take_events(&net.nodes[index], SOCIAL_API_EVENT_PARTY_LEFT, NULL);
	social_front_party(net.nodes[1].front, 11, 0, 0, NULL);
	fake_run(&net, 500);
	CHECK(snap(&net.nodes[1], 0)->party.result == SOCIAL_PARTY_RESULT_NOT_LEADER && snap(&net.nodes[0], 0)->party.active && snap(&net.nodes[2], 0)->party.active, "a member cannot disband");
	social_front_party(net.nodes[0].front, 11, 0, 0, NULL);
	WAIT_FOR(&net, 3000, !snap(&net.nodes[0], 0)->party.active && !snap(&net.nodes[1], 0)->party.active && !snap(&net.nodes[2], 0)->party.active, "disbanded for everyone");
	CHECK(take_events(&net.nodes[1], SOCIAL_API_EVENT_PARTY_LEFT, &event) == 1 && strstr(event.text, "disband"), "a left event that says why: '%s'", event.text);
	CHECK(take_events(&net.nodes[0], SOCIAL_API_EVENT_PARTY_LEFT, &event) == 1, "the leader's own");
	social_front_party(net.nodes[0].front, 11, 0, 0, NULL);
	fake_run(&net, 300);
	CHECK(snap(&net.nodes[0], 0)->party.result == SOCIAL_PARTY_RESULT_NOT_IN_PARTY, "and nothing to disband twice");
	/* a new party of the same people is not ended by the old party's last words */
	social_front_party(net.nodes[0].front, 1, 0, 0, NULL);
	fake_run(&net, 300);
	CHECK(snap(&net.nodes[0], 0)->party.active, "a new party");
	fake_run(&net, 20000);
	CHECK(snap(&net.nodes[0], 0)->party.active && !snap(&net.nodes[1], 0)->party.active, "and it stands");
}

static void case_chat(void)
{
	static struct fake_net net;
	struct social_api_event event;
	int index;

	fake_start(&net, 56);
	for (index = 0; index < 3; index++)
		fake_add(&net, 0, 1);
	form_party(&net, 3);
	WAIT_FOR(&net, 30000, members_seen(&net.nodes[1]) == 3 && members_seen(&net.nodes[2]) == 3, "three");
	take_events(&net.nodes[0], SOCIAL_API_EVENT_PARTY_CHAT, NULL);
	social_front_party(net.nodes[1].front, 10, 0, 0, "hello | all\x01 \xC3\xA9");
	fake_run(&net, 500);
	CHECK(take_events(&net.nodes[0], SOCIAL_API_EVENT_PARTY_CHAT, &event) == 1 && !strcmp(event.name, "Node1"), "a chat event");
	CHECK(strchr(event.text, '|') == NULL, "no pipe in what arrives: '%s'", event.text);
	for (index = 0; event.text[index]; index++)
		CHECK(event.text[index] >= 0x20 && event.text[index] <= 0x7E, "plain");
	CHECK(snap(&net.nodes[1], 0)->party.chat_count == 1 && snap(&net.nodes[0], 0)->party.chat_count == 1 && snap(&net.nodes[2], 0)->party.chat_count == 1, "in all three snapshots");
	/* empty, and too fast: three at once, then it waits */
	social_front_party(net.nodes[1].front, 10, 0, 0, "");
	fake_run(&net, 100);
	CHECK(snap(&net.nodes[1], 0)->party.result == SOCIAL_PARTY_RESULT_BAD_TEXT, "empty");
	for (index = 0; index < 6; index++)
	{
		social_front_party(net.nodes[1].front, 10, 0, 0, "again");
		fake_run(&net, 350);
	}
	CHECK(snap(&net.nodes[1], 0)->party.chat_count <= 4 && snap(&net.nodes[1], 0)->party.chat_count >= 3, "some went, some waited: %d", snap(&net.nodes[1], 0)->party.chat_count);
	/* a member sending faster than four a second is dropped by the receivers */
	{
		struct social_member *sender = &net.nodes[0].engine->party.members[net.nodes[0].engine->party.self];
		unsigned int before = net.nodes[0].engine->statistics.dropped_rate;
		struct fake_node *spammer = &net.nodes[1];

		(void)sender;
		for (index = 0; index < 30; index++)
		{
			unsigned char plain[SOCIAL_MAX_PLAIN], nonce[12], wire[SOCIAL_MAX_WIRE];
			struct social_writer writer;
			int wire_size;
			unsigned char signature[64];

			social_writer_start(&writer, plain, sizeof(plain));
			social_put8(&writer, SOCIAL_TYPE_PARTY_CHAT);
			social_put32(&writer, spammer->engine->generation);
			social_put32(&writer, 50000 + (unsigned)index);
			social_put_bytes(&writer, spammer->engine->identity.key, 32);
			social_put_text(&writer, "spam", 63);
			social_sign(&spammer->engine->identity, "hceu-f/party", spammer->engine->party.token, 16, plain, writer.size, signature);
			social_put_bytes(&writer, signature, 64);
			fill(nonce, 12);
			wire_size = social_seal(spammer->engine->party.seal, SOCIAL_KIND_PARTY, NULL, spammer->engine->party.topic, plain, writer.size, nonce, wire, sizeof(wire));
			net.time += 20;
			social_front_heard(net.nodes[0].front, spammer->engine->party.topic, wire, wire_size);
		}
		CHECK(net.nodes[0].engine->statistics.dropped_rate > before + 10, "a flood of lines is dropped: %u", net.nodes[0].engine->statistics.dropped_rate - before);
	}
}

static void case_open_party(void)
{
	static struct fake_net net;
	struct fake_node *a, *b, *c;

	fake_start(&net, 57);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	c = fake_add(&net, 0, 1);
	link_friends(a, b);
	link_friends(a, c);
	fake_run(&net, 6000);
	social_front_party(a->front, 1, 0, 0, NULL);
	fake_run(&net, 6000);
	{
		const struct social_snapshot *snapshot = snap(b, 0);
		int index;

		for (index = 0; index < snapshot->friend_count; index++)
			CHECK(snapshot->friends[index].in_party && !snapshot->friends[index].party_open, "in a party, closed");
	}
	/* closed: b asks with nothing */
	social_front_party(b->front, 4, friend_id(b, a), 0, NULL);
	fake_run(&net, 500);
	CHECK(snap(b, 0)->party.result == SOCIAL_PARTY_RESULT_FRIEND_OFFLINE && !snap(b, 0)->party.active, "closed: no way in");
	social_front_party(a->front, 8, 0, 1, NULL);
	WAIT_FOR(&net, 8000, ({ const struct social_snapshot *s = snap(b, 0); s->friend_count && s->friends[0].party_open; }), "the party shows open");
	social_front_party(b->front, 4, friend_id(b, a), 0, NULL);
	WAIT_FOR(&net, 15000, snap(b, 0)->party.active && !snap(b, 0)->party.joining && members_seen(a) == 2, "b joins an open party of its friend's");
	/* a stranger who got the token (not a friend of the leader) is not listed even if open */
	{
		struct fake_node *s = fake_add(&net, 0, 1);

		social_front_party_token(s->front, a->engine->party.token);
		fake_run(&net, 20000);
		CHECK(!snap(s, 0)->party.active && members_seen(a) == 2, "a stranger with the token is not listed");
	}
	(void)c;
	/* closing it again */
	social_front_party(a->front, 8, 0, 0, NULL);
	fake_run(&net, 8000);
	CHECK(snap(c, 0)->friend_count && !snap(c, 0)->friends[0].party_open, "closed again");
	/* closed, a friend with a token it should not have is not listed either */
	social_front_party_token(c->front, a->engine->party.token);
	fake_run(&net, 20000);
	CHECK(!snap(c, 0)->party.active && members_seen(a) == 2, "a friend with the token, the party closed: not listed");
}

/* a partition: both halves lose the leader and each elects, then they meet again and one term wins */
static void case_partition(void)
{
	static struct fake_net net;
	int index, broker;
	int leaders;

	fake_start(&net, 58);
	for (index = 0; index < 5; index++)
		fake_add(&net, 0, 1);
	form_party(&net, 5);
	WAIT_FOR(&net, 40000, members_seen(&net.nodes[4]) == 5 && members_seen(&net.nodes[1]) == 5 && members_seen(&net.nodes[2]) == 5 && members_seen(&net.nodes[3]) == 5, "five");
	/* the leader dies; nodes 1 and 2 on brokers 0 and 1 only, nodes 3 and 4 on brokers 2 and 3 only */
	net.nodes[0].alive = 0;
	for (index = 1; index < 5; index++)
	{
		for (broker = 0; broker < FAKE_BROKERS; broker++)
			net.nodes[index].broker_ready[broker] = (index <= 2) ? broker < 2 : broker >= 2;
	}
	fake_run(&net, 40000);
	leaders = 0;
	for (index = 1; index < 5; index++)
		leaders += snap(&net.nodes[index], 0)->party.is_leader;
	CHECK(leaders == 2, "two halves, two leaders: %d", leaders);
	CHECK(snap(&net.nodes[1], 0)->party.is_leader && snap(&net.nodes[3], 0)->party.is_leader, "the first in each half");
	/* the partition heals */
	for (index = 1; index < 5; index++)
		for (broker = 0; broker < FAKE_BROKERS; broker++)
			net.nodes[index].broker_ready[broker] = 1;
	WAIT_FOR(&net, 20000, ({ int n = 0; for (index = 1; index < 5; index++) n += snap(&net.nodes[index], 0)->party.is_leader; n == 1; }), "one leader after the healing");
	fake_run(&net, 15000);
	leaders = 0;
	for (index = 1; index < 5; index++)
		leaders += snap(&net.nodes[index], 0)->party.is_leader;
	CHECK(leaders == 1, "one leader: %d", leaders);
	/* the one of the lower place in the list: node 1 */
	CHECK(snap(&net.nodes[1], 0)->party.is_leader, "node 1, the lower place, won");
	/* the others see it, and at least the members of its side are listed */
	CHECK(!strcmp(leader_name(&net.nodes[2]), "Node1") && !strcmp(leader_name(&net.nodes[4]), "Node1"), "all agree: '%s' '%s'", leader_name(&net.nodes[2]), leader_name(&net.nodes[4]));
}

/* ---------- following the leader's game */

static void host_place(struct fake_node *node, int token)
{
	struct social_place place;

	memset(&place, 0, sizeof(place));
	place.activity = SOCIAL_GAME;
	place.hosting = 1;
	place.has_invite = 1;
	place.joinable = 1;
	strcpy(place.map, "bloodgulch");
	strcpy(place.gametype, "CTF");
	place.players = 2;
	place.maximum_players = 16;
	memset(place.invite_hash, 0x5A, 16);
	memset(place.invite_token, token, 16);
	social_front_set_place(node->front, &place);
}

static void case_follow(void)
{
	static struct fake_net net;
	struct fake_node *a, *b, *c;
	struct social_join_target target;
	unsigned int order, first_order;
	int index;

	fake_start(&net, 59);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	c = fake_add(&net, 0, 1);
	form_party(&net, 3);
	WAIT_FOR(&net, 30000, members_seen(b) == 3 && members_seen(c) == 3, "three");
	CHECK(!social_front_follow(b->front, &target, &order), "nothing to follow yet");
	host_place(a, 0x77);
	WAIT_FOR(&net, 8000, social_front_follow(b->front, &target, &order) && social_front_follow(c->front, &target, &order), "the members are to follow");
	first_order = order;
	CHECK(target.host_hash[0] == 0x5A && target.token[0] == 0x77 && !strcmp(target.map, "bloodgulch") && !strcmp(target.gametype, "CTF") && target.network_version == 41 &&
		target.invited, "the leader's game: map, type, token");
	CHECK(snap(b, 0)->party.game_present && snap(b, 0)->party.game_order == first_order, "in the snapshot too");
	CHECK(!social_front_follow(a->front, &target, &order), "the leader follows no one");
	/* b: joining, then joined; the row says */
	social_front_follow_report(b->front, first_order, SOCIAL_FOLLOW_JOINING);
	fake_run(&net, 300);
	CHECK(!social_front_follow(b->front, &target, &order), "b is joining: not asked again");
	social_front_follow_report(b->front, first_order, SOCIAL_FOLLOW_JOINED);
	fake_run(&net, 3000);
	CHECK(!social_front_follow(b->front, &target, &order), "b is done with this game");
	{
		const struct social_snapshot *snapshot = snap(a, 0);
		int found = 0;

		for (index = 0; index < snapshot->party.member_count; index++)
			if (!strcmp(snapshot->party.members[index].name, "Node1"))
				found = snapshot->party.members[index].follow_result == SOCIAL_FOLLOW_JOINED && !strcmp(snapshot->party.members[index].follow_text, "joined");
		CHECK(found, "the leader sees b joined");
	}
	/* c: the game is full: again every 3 s, seven times, then once more a minute later, then no more */
	{
		int tries = 0;
		unsigned int started = net.time;

		for (;;)
		{
			if (social_front_follow(c->front, &target, &order))
			{
				tries++;
				social_front_follow_report(c->front, order, SOCIAL_FOLLOW_JOINING);
				fake_run(&net, 100);
				social_front_follow_report(c->front, order, SOCIAL_FOLLOW_FULL);
			}
			fake_run(&net, 100);
			if (net.time - started > 200000)
				break;
		}
		CHECK(tries == SOCIAL_PARTY_FOLLOW_TRIES + 1, "tried %d times", tries);
		{
			const struct social_snapshot *snapshot = snap(a, 0);
			int found = 0;

			for (index = 0; index < snapshot->party.member_count; index++)
				if (!strcmp(snapshot->party.members[index].name, "Node2"))
					found = snapshot->party.members[index].follow_result == SOCIAL_FOLLOW_FULL && !strcmp(snapshot->party.members[index].follow_text, "game is full");
			CHECK(found, "the leader sees c's game is full");
		}
	}
	/* a new game of the leader: both are asked afresh, even c; one in a game already is not */
	host_place(a, 0x78);
	WAIT_FOR(&net, 8000, social_front_follow(c->front, &target, &order) && order != first_order && target.token[0] == 0x78, "a new game, a new order");
	{
		struct social_place place;

		memset(&place, 0, sizeof(place));
		place.activity = SOCIAL_GAME;
		social_front_set_place(c->front, &place);
		fake_run(&net, 500);
		CHECK(!social_front_follow(c->front, &target, &order), "c is in a game: not moved out of it");
		place.activity = SOCIAL_MENUS;
		social_front_set_place(c->front, &place);
		fake_run(&net, 500);
		CHECK(social_front_follow(c->front, &target, &order), "back in the menus: asked");
	}
	/* opting out */
	social_front_party(c->front, 9, 0, 0, NULL);
	fake_run(&net, 3000);
	CHECK(!social_front_follow(c->front, &target, &order), "opted out");
	{
		const struct social_snapshot *snapshot = snap(a, 0);
		int found = 0;

		for (index = 0; index < snapshot->party.member_count; index++)
			if (!strcmp(snapshot->party.members[index].name, "Node2"))
				found = !snapshot->party.members[index].auto_follow && snapshot->party.members[index].follow_result == SOCIAL_FOLLOW_OPTED_OUT;
		CHECK(found, "the leader sees it");
	}
	/* a refusal that is not 'full' is final */
	social_front_party(c->front, 9, 0, 1, NULL);
	fake_run(&net, 1000);
	CHECK(social_front_follow(c->front, &target, &order), "following again");
	social_front_follow_report(c->front, order, SOCIAL_FOLLOW_MAP_MISSING);
	fake_run(&net, 70000);
	CHECK(!social_front_follow(c->front, &target, &order), "no map: not tried again");
	/* the game ends */
	{
		struct social_place place;

		memset(&place, 0, sizeof(place));
		place.activity = SOCIAL_MENUS;
		social_front_set_place(a->front, &place);
		WAIT_FOR(&net, 8000, !snap(b, 0)->party.game_present, "the game is gone from the party's state");
	}
}

/* a malicious member and an outsider */
static void case_abuse(void)
{
	static struct fake_net net;
	struct fake_node *a, *b, *c, *outsider;
	unsigned char plain[SOCIAL_MAX_PLAIN], nonce[12], wire[SOCIAL_MAX_WIRE];
	struct social_writer writer;
	unsigned char signature[64];
	int wire_size, round;
	unsigned int term_before;

	fake_start(&net, 60);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	c = fake_add(&net, 0, 1);
	outsider = fake_add(&net, 0, 1);
	form_party(&net, 3);
	WAIT_FOR(&net, 30000, members_seen(b) == 3 && members_seen(c) == 3, "three");
	term_before = a->engine->party.term;
	/* an outsider with the token (a leaked one) claims leadership with a high term: its key is not listed, nobody listens */
	for (round = 0; round < 3; round++)
	{
		struct social_member *me;
		unsigned char token[16];

		memcpy(token, a->engine->party.token, 16);
		social_writer_start(&writer, plain, sizeof(plain));
		social_put8(&writer, SOCIAL_TYPE_PARTY_STATE);
		social_put32(&writer, 99);
		social_put32(&writer, 1 + (unsigned)round);
		social_put_bytes(&writer, outsider->engine->identity.key, 32);
		social_put16(&writer, 60000);
		social_put8(&writer, 0);
		social_put8(&writer, _state_leader);
		social_put8(&writer, 0);
		social_put8(&writer, 0);
		social_put16(&writer, 41);
		social_put_text(&writer, "Intruder", 11);
		{
			int i;

			for (i = 0; i < 16; i++)
				social_put8(&writer, 0);
		}
		social_put16(&writer, 5);
		social_put8(&writer, 1);
		social_put_bytes(&writer, "evilevilevilevil", 16);
		social_put_bytes(&writer, "tokentokentokent", 16);
		social_put_text(&writer, "hacked", 32);
		social_put_text(&writer, "x", 24);
		social_put8(&writer, 1);
		social_put8(&writer, 1);
		social_put8(&writer, 0);
		(void)me;
		social_sign(&outsider->engine->identity, "hceu-f/party", token, 16, plain, writer.size, signature);
		social_put_bytes(&writer, signature, 64);
		fill(nonce, 12);
		wire_size = social_seal(a->engine->party.seal, SOCIAL_KIND_PARTY, NULL, a->engine->party.topic, plain, writer.size, nonce, wire, sizeof(wire));
		social_front_heard(b->front, a->engine->party.topic, wire, wire_size);
		social_front_heard(c->front, a->engine->party.topic, wire, wire_size);
		social_front_heard(a->front, a->engine->party.topic, wire, wire_size);
	}
	fake_run(&net, 2000);
	CHECK(a->engine->party.term == term_before && b->engine->party.term == term_before && !strcmp(leader_name(b), "Node0") && !snap(b, 0)->party.game_present,
		"the intruder did not take the lead or put a game up");
	CHECK(members_seen(a) == 3 && members_seen(b) == 3, "and is no member");
	/* a member forging another's STATE: the signature is wrong */
	{
		unsigned int before = c->engine->statistics.dropped_signature;

		social_writer_start(&writer, plain, sizeof(plain));
		social_put8(&writer, SOCIAL_TYPE_PARTY_STATE);
		social_put32(&writer, a->engine->generation + 5);
		social_put32(&writer, 1);
		social_put_bytes(&writer, a->engine->identity.key, 32);
		social_put16(&writer, 77);
		social_put8(&writer, 0);
		social_put8(&writer, _state_leader);
		social_put8(&writer, 0);
		social_put8(&writer, 0);
		social_put16(&writer, 41);
		social_put_text(&writer, "Node0", 11);
		{
			int i;

			for (i = 0; i < 16; i++)
				social_put8(&writer, 0);
		}
		social_put16(&writer, 5);
		social_put8(&writer, 0);
		social_put8(&writer, 0);
		/* (signed by b, as a) */
		social_sign(&b->engine->identity, "hceu-f/party", b->engine->party.token, 16, plain, writer.size, signature);
		social_put_bytes(&writer, signature, 64);
		fill(nonce, 12);
		wire_size = social_seal(a->engine->party.seal, SOCIAL_KIND_PARTY, NULL, a->engine->party.topic, plain, writer.size, nonce, wire, sizeof(wire));
		social_front_heard(c->front, a->engine->party.topic, wire, wire_size);
		CHECK(c->engine->statistics.dropped_signature == before + 1 && c->engine->party.term == term_before, "a forged STATE: refused");
	}
	/* an old STATE of the leader replayed */
	{
		unsigned int before = c->engine->statistics.dropped_window + c->engine->statistics.dropped_seen;

		fake_run(&net, 100);
		social_writer_start(&writer, plain, sizeof(plain));
		social_put8(&writer, SOCIAL_TYPE_PARTY_STATE);
		social_put32(&writer, c->engine->party.members[member_index_by_key(&c->engine->party, a->engine->identity.key)].window.generation);
		social_put32(&writer, c->engine->party.members[member_index_by_key(&c->engine->party, a->engine->identity.key)].window.sequence);
		social_put_bytes(&writer, a->engine->identity.key, 32);
		social_put16(&writer, 1);
		social_put8(&writer, 0);
		social_put8(&writer, _state_leader);
		social_put8(&writer, 0);
		social_put8(&writer, 0);
		social_put16(&writer, 41);
		social_put_text(&writer, "Node0", 11);
		{
			int i;

			for (i = 0; i < 16; i++)
				social_put8(&writer, 0);
		}
		social_put16(&writer, 9);
		social_put8(&writer, 1);
		social_put_bytes(&writer, "oldoldoldoldoldo", 16);
		social_put_bytes(&writer, "tokentokentokent", 16);
		social_put_text(&writer, "oldmap", 32);
		social_put_text(&writer, "x", 24);
		social_put8(&writer, 1);
		social_put8(&writer, 1);
		social_put8(&writer, 3);
		social_put_bytes(&writer, a->engine->party.roster[0], 8);
		social_put_bytes(&writer, a->engine->party.roster[1], 8);
		social_put_bytes(&writer, a->engine->party.roster[2], 8);
		social_sign(&a->engine->identity, "hceu-f/party", a->engine->party.token, 16, plain, writer.size, signature);
		social_put_bytes(&writer, signature, 64);
		fill(nonce, 12);
		wire_size = social_seal(a->engine->party.seal, SOCIAL_KIND_PARTY, NULL, a->engine->party.topic, plain, writer.size, nonce, wire, sizeof(wire));
		social_front_heard(c->front, a->engine->party.topic, wire, wire_size);
		CHECK(c->engine->statistics.dropped_window + c->engine->statistics.dropped_seen > before && !snap(c, 0)->party.game_present, "the replay of an old STATE: dropped");
	}
	/* noise on the party topic */
	{
		unsigned char junk[600];

		for (round = 0; round < 1500; round++)
		{
			int size = (int)(fake_random(&test_state) % 600);

			fill(junk, size);
			if (size > 2 && (round & 1))
			{
				junk[0] = 'G';
				junk[1] = 1;
			}
			fake_run(&net, 10);
			social_front_heard(b->front, a->engine->party.topic, junk, size);
		}
		fake_run(&net, 3000);
		CHECK(members_seen(b) == 3 && !strcmp(leader_name(b), "Node0"), "the party survives noise");
	}
	/* a message sealed with another party's key on this topic */
	{
		unsigned char other_key[32];

		fill(other_key, 32);
		social_writer_start(&writer, plain, sizeof(plain));
		social_put8(&writer, SOCIAL_TYPE_PARTY_DISBAND);
		social_put32(&writer, 99);
		social_put32(&writer, 1);
		social_put16(&writer, 99);
		social_sign(&a->engine->identity, "hceu-f/party", a->engine->party.token, 16, plain, writer.size, signature);
		social_put_bytes(&writer, signature, 64);
		fill(nonce, 12);
		wire_size = social_seal(other_key, SOCIAL_KIND_PARTY, NULL, a->engine->party.topic, plain, writer.size, nonce, wire, sizeof(wire));
		social_front_heard(b->front, a->engine->party.topic, wire, wire_size);
		CHECK(snap(b, 0)->party.active, "a message sealed with another key does not disband");
	}
	/* a real DISBAND from the leader ends it for everyone */
	{
		social_writer_start(&writer, plain, sizeof(plain));
		social_put8(&writer, SOCIAL_TYPE_PARTY_DISBAND);
		social_put32(&writer, a->engine->generation);
		social_put32(&writer, 900000);
		social_put16(&writer, a->engine->party.term);
		social_sign(&a->engine->identity, "hceu-f/party", a->engine->party.token, 16, plain, writer.size, signature);
		social_put_bytes(&writer, signature, 64);
		fill(nonce, 12);
		wire_size = social_seal(a->engine->party.seal, SOCIAL_KIND_PARTY, NULL, a->engine->party.topic, plain, writer.size, nonce, wire, sizeof(wire));
		social_front_heard(b->front, a->engine->party.topic, wire, wire_size);
		social_front_heard(c->front, a->engine->party.topic, wire, wire_size);
		fake_run(&net, 500);
		CHECK(!snap(b, 0)->party.active && !snap(c, 0)->party.active, "the leader's DISBAND ends the party");
	}
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "basic"))
		case_basic();
	else if (!strcmp(name, "eight"))
		case_eight();
	else if (!strcmp(name, "leader-dies"))
		case_leader_dies();
	else if (!strcmp(name, "leave-transfer"))
		case_leave_transfer();
	else if (!strcmp(name, "silent-member"))
		case_silent_member();
	else if (!strcmp(name, "disband"))
		case_disband();
	else if (!strcmp(name, "kick"))
		case_kick();
	else if (!strcmp(name, "chat"))
		case_chat();
	else if (!strcmp(name, "open-party"))
		case_open_party();
	else if (!strcmp(name, "partition"))
		case_partition();
	else if (!strcmp(name, "follow"))
		case_follow();
	else if (!strcmp(name, "abuse"))
		case_abuse();
	else
	{
		fprintf(stderr, "no case named %s\n", name);
		return 2;
	}
	return 0;
}
