/* social_peer.c: one friends layer (a front, an engine) driven over stdin and stdout, so that a program on another machine type can stand in for the brokers
(tools/harness/tests/test_social_mixed.py runs one built for 32-bit x86 and one for big-endian PowerPC against each other). A line in, a line out:

	init <name> <seed hex> <wall hours> <anchored 0|1>	-> code <code>
	tick <ms>						the clock moves on, in steps of 10 ms (a pass each)
	heard <topic> <hex>					a message from the brokers
	do <verb> [arguments]					add <code>, accept <n>, place <map> <players> <token byte>, invite <n>, party make|invite <n>|join|leave|chat <text>|transfer <n>, remove <n>
	dump							-> dump <what the snapshot holds>
	out:	pub <topic> <hex>	sub <topic> ...	(the whole set)	*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition, ...) \
	do \
	{ \
		if (!(condition)) \
		{ \
			fprintf(stderr, "social_peer: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
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

static unsigned int clock_ms = 1000;
static long long wall_offset;
static int anchored;
static unsigned long long rng = 1;

static unsigned int peer_now(void *user)
{
	(void)user;
	return clock_ms;
}

static unsigned long long peer_utc(void *user, int *is_anchored)
{
	(void)user;
	*is_anchored = anchored;
	return (unsigned long long)(1790000000LL + clock_ms / 1000 + (anchored ? 0 : wall_offset));
}

static void peer_random(void *user, unsigned char *bytes, int size)
{
	int index;

	(void)user;
	for (index = 0; index < size; index++)
	{
		rng ^= rng << 13;
		rng ^= rng >> 7;
		rng ^= rng << 17;
		bytes[index] = (unsigned char)(rng >> 24);
	}
}

static int peer_publish(void *user, const char *topic, const unsigned char *payload, int size)
{
	char hex[2 * SOCIAL_MAX_WIRE + 1];

	(void)user;
	social_hex(payload, size, hex);
	printf("pub %s %s\n", topic, hex);
	return 1;
}

static void peer_subscribe(void *user, const char (*topics)[SOCIAL_TOPIC_SIZE], int count)
{
	int index;

	(void)user;
	printf("sub");
	for (index = 0; index < count; index++)
		printf(" %s", topics[index]);
	printf("\n");
}

static int peer_ready(void *user)
{
	(void)user;
	return 1;
}

static struct social_front *front;
static struct social_snapshot snapshot;

static int hex_digit(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

int main(void)
{
	char line[4096];

	setvbuf(stdout, NULL, _IOLBF, 0);
	while (fgets(line, sizeof(line), stdin))
	{
		char *word = strtok(line, " \r\n");
		char *rest;

		if (!word)
			continue;
		if (!strcmp(word, "init"))
		{
			struct social_env env;
			char *name = strtok(NULL, " \r\n"), *seed_hex = strtok(NULL, " \r\n"), *hours = strtok(NULL, " \r\n"), *anchor = strtok(NULL, " \r\n");
			unsigned char seed[32];
			int index;

			CHECK(name && seed_hex && hours && anchor && strlen(seed_hex) == 64);
			for (index = 0; index < 32; index++)
				seed[index] = (unsigned char)(hex_digit(seed_hex[index * 2]) << 4 | hex_digit(seed_hex[index * 2 + 1]));
			wall_offset = atoll(hours) * 3600;
			anchored = atoi(anchor);
			memcpy(&rng, seed, 8);
			rng |= 1;
			memset(&env, 0, sizeof(env));
			env.now = peer_now;
			env.utc = peer_utc;
			env.random = peer_random;
			env.publish = peer_publish;
			env.subscribe = peer_subscribe;
			env.ready = peer_ready;
			env.platform = SOCIAL_PLATFORM_LINUX;
			env.network_version = 41;
			front = social_front_create(&env, NULL, NULL);
			social_set_identity(social_front_engine(front), seed, name, 0);
			social_set_enabled(social_front_engine(front), 1);
			social_front_tick(front);
			printf("code %s\n", social_front_engine(front)->identity.code);
		}
		else if (!strcmp(word, "tick"))
		{
			int ms = atoi(strtok(NULL, " \r\n"));

			while (ms > 0)
			{
				clock_ms += 10;
				ms -= 10;
				social_front_tick(front);
			}
			printf("ticked %u\n", clock_ms);
		}
		else if (!strcmp(word, "heard"))
		{
			char *topic = strtok(NULL, " \r\n"), *hex = strtok(NULL, " \r\n");
			unsigned char payload[SOCIAL_MAX_WIRE + 8];
			int size = (int)strlen(hex) / 2, index;

			CHECK(size <= (int)sizeof(payload));
			for (index = 0; index < size; index++)
				payload[index] = (unsigned char)(hex_digit(hex[index * 2]) << 4 | hex_digit(hex[index * 2 + 1]));
			social_front_heard(front, topic, payload, size);
		}
		else if (!strcmp(word, "do"))
		{
			char *verb = strtok(NULL, " \r\n");

			rest = strtok(NULL, "\r\n");
			social_front_snapshot(front, &snapshot);
			if (!strcmp(verb, "add"))
				social_front_add_code(front, rest);
			else if (!strcmp(verb, "accept"))
				social_front_accept(front, snapshot.requests[atoi(rest)].id);
			else if (!strcmp(verb, "place"))
			{
				struct social_place place;
				char map[40];
				int players, token;

				memset(&place, 0, sizeof(place));
				sscanf(rest, "%39s %d %d", map, &players, &token);
				place.activity = SOCIAL_GAME;
				place.hosting = place.has_invite = place.joinable = 1;
				snprintf(place.map, sizeof(place.map), "%s", map);
				strcpy(place.gametype, "Slayer");
				place.players = (unsigned char)players;
				place.maximum_players = 16;
				memset(place.invite_hash, 0x5A, 16);
				memset(place.invite_token, token, 16);
				social_front_set_place(front, &place);
			}
			else if (!strcmp(verb, "invite"))
				social_front_invite(front, snapshot.friends[atoi(rest)].id);
			else if (!strcmp(verb, "remove"))
				social_front_remove(front, snapshot.friends[atoi(rest)].id);
			else if (!strcmp(verb, "party"))
			{
				char *what = strtok(rest, " ");
				char *argument = strtok(NULL, "\r\n");

				if (!strcmp(what, "make"))
					social_front_party(front, 1, 0, 0, NULL);
				else if (!strcmp(what, "invite"))
					social_front_party(front, 2, snapshot.friends[atoi(argument)].id, 0, NULL);
				else if (!strcmp(what, "join") && snapshot.invite_count)
					social_front_party_token(front, snapshot.invites[snapshot.invite_count - 1].party_token);
				else if (!strcmp(what, "leave"))
					social_front_party(front, 5, 0, 0, NULL);
				else if (!strcmp(what, "chat"))
					social_front_party(front, 10, 0, 0, argument);
				else if (!strcmp(what, "transfer"))
					social_front_party(front, 7, snapshot.party.members[atoi(argument)].id, 0, NULL);
			}
		}
		else if (!strcmp(word, "dump"))
		{
			int index;

			social_front_snapshot(front, &snapshot);
			printf("dump friends %d requests %d invites %d party %d leader", snapshot.friend_count, snapshot.request_count, snapshot.invite_count, snapshot.party.member_count);
			for (index = 0; index < snapshot.party.member_count; index++)
				if (snapshot.party.members[index].leader)
					printf(" %s", snapshot.party.members[index].name);
			for (index = 0; index < snapshot.friend_count; index++)
				printf(" | %s %s online=%d joinable=%d map=%s players=%d platform=%d version=%d party=%d", snapshot.friends[index].name, snapshot.friends[index].code,
					snapshot.friends[index].online, snapshot.friends[index].joinable, snapshot.friends[index].map, snapshot.friends[index].players, snapshot.friends[index].platform,
					snapshot.friends[index].network_version, snapshot.friends[index].in_party);
			for (index = 0; index < snapshot.invite_count; index++)
				printf(" | invite kind=%d from=%s map=%s version=%d", snapshot.invites[index].kind, snapshot.invites[index].name, snapshot.invites[index].target.map,
					snapshot.invites[index].target.network_version);
			for (index = 0; index < snapshot.party.chat_count; index++)
				printf(" | chat %s: %s", snapshot.party.chat[index].name, snapshot.party.chat[index].text);
			printf("\n");
		}
		else if (!strcmp(word, "quit"))
			break;
	}
	return 0;
}
