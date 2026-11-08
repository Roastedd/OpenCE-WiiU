/* social_check.c: the friends layer (port/linux/src/social_*.c) on a fake network of brokers (social_fake.h). The cases are named on the command line
(tools/harness/tests/test_social.py); a failed check exits 1. The sources are included whole, so the tests reach their static functions and the real
structures. */

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
#include "social_party.c"
#include "social_fake.h"

static unsigned long long test_state = 88172645463325252ULL;

static unsigned int random_below(unsigned int limit)
{
	return (unsigned int)(fake_random(&test_state) % limit);
}

/* the machine's secure random bytes, which p2p_crypto.c's p2p_seal wants (not used by the engine) */
static void fill(unsigned char *bytes, int size)
{
	int index;

	for (index = 0; index < size; index++)
		bytes[index] = (unsigned char)(fake_random(&test_state) >> 20);
}

/* two machines that are friends already: each holds the other's circle key and has acknowledged */
static void link_friends(struct fake_node *a, struct fake_node *b)
{
	CHECK(social_friend_add(&a->engine, b->engine.identity.key, b->engine.circle_generation, b->engine.circle_key, 1), "link a->b");
	CHECK(social_friend_add(&b->engine, a->engine.identity.key, a->engine.circle_generation, a->engine.circle_key, 1), "link b->a");
}

static struct social_friend *friend_of(struct fake_node *node, struct fake_node *other)
{
	return social_friend_by_key(&node->engine, other->engine.identity.key);
}

static int online_to(struct fake_node *node, struct fake_node *other)
{
	struct social_friend *friend = friend_of(node, other);

	return friend && social_friend_online(&node->engine, friend);
}

static void to_hex(const unsigned char *bytes, int size, char *text)
{
	social_hex(bytes, size, text);
}

/* ---------- the friend code */

static void case_code_vectors(int argc, char **argv)
{
	/* the code of a seed given in hex on the command line, for the independent check in the Python test */
	unsigned char seed[32];
	struct social_identity identity;
	int index;

	CHECK(argc == 1 && strlen(argv[0]) == 64, "a seed in hex");
	for (index = 0; index < 32; index++)
	{
		unsigned int value;

		sscanf(argv[0] + index * 2, "%2x", &value);
		seed[index] = (unsigned char)value;
	}
	social_identity_make(&identity, seed);
	printf("%s\n", identity.code);
}

/* every single-symbol error and every neighbour swap of random codes is caught */
static void case_code_check(void)
{
	int total = 0;
	int round;

	for (round = 0; round < 3000; round++)
	{
		unsigned char key[32], bytes[8], parsed[8];
		char text[SOCIAL_CODE_TEXT_SIZE], clean[SOCIAL_CODE_TEXT_SIZE];
		int position, alternative;

		fill(key, 32);
		social_code_of_key(key, bytes, text);
		CHECK(strlen(text) == 16 && text[4] == '-' && text[9] == '-' && text[14] == '-', "format %s", text);
		CHECK(social_code_parse(text, parsed, clean) == SOCIAL_CODE_OK && !memcmp(parsed, bytes, 8) && !strcmp(clean, text), "parse %s", text);
		/* every single-symbol substitution */
		for (position = 0; position < 16; position++)
		{
			if (position == 4 || position == 9 || position == 14)
				continue;
			for (alternative = 0; alternative < 32; alternative++)
			{
				char changed[SOCIAL_CODE_TEXT_SIZE];

				strcpy(changed, text);
				if (code_digits[alternative] == text[position])
					continue;
				changed[position] = code_digits[alternative];
				CHECK(social_code_parse(changed, parsed, NULL) != SOCIAL_CODE_OK, "undetected single error in %s -> %s", text, changed);
				total++;
			}
		}
		/* every swap of neighbours that differ (across a dash too) */
		for (position = 0; position < 15; position++)
		{
			char changed[SOCIAL_CODE_TEXT_SIZE];
			int left = position, right = position + 1;

			if (left == 4 || left == 9 || left == 14)
				continue;
			if (right == 4 || right == 9 || right == 14)
				right++;
			if (right > 15 || text[left] == text[right])
				continue;
			strcpy(changed, text);
			changed[left] = text[right];
			changed[right] = text[left];
			CHECK(social_code_parse(changed, parsed, NULL) != SOCIAL_CODE_OK, "undetected swap in %s -> %s", text, changed);
			total++;
		}
	}
	printf("%d single errors and swaps, none undetected\n", total);
}

static void case_code_forgiving(void)
{
	unsigned char key[32], bytes[8], parsed[8];
	char text[SOCIAL_CODE_TEXT_SIZE], typed[64];
	int round;

	for (round = 0; round < 200; round++)
	{
		int index;

		fill(key, 32);
		social_code_of_key(key, bytes, text);
		/* lower case, no dashes, spaces, and the look-alikes: 1 for I and L, 0 for O, V for U */
		typed[0] = 0;
		for (index = 0; text[index]; index++)
		{
			char c = text[index];
			char two[3];

			if (c == '-')
				continue;
			if (c == '1' && random_below(2))
				c = random_below(2) ? 'I' : 'l';
			if (c == '0' && random_below(2))
				c = random_below(2) ? 'O' : 'o';
			if (c == 'V' && random_below(2))
				c = 'u';
			if (c >= 'A' && c <= 'Z' && random_below(2))
				c = (char)(c + 32);
			two[0] = c;
			two[1] = 0;
			strcat(typed, two);
			if (random_below(5) == 0)
				strcat(typed, random_below(2) ? " " : "-");
		}
		CHECK(social_code_parse(typed, parsed, NULL) == SOCIAL_CODE_OK && !memcmp(parsed, bytes, 8), "typed %s for %s", typed, text);
	}
	/* too short, too long, not letters, the check wrong */
	CHECK(social_code_parse("", NULL, NULL) == SOCIAL_CODE_BAD_TEXT, "empty");
	CHECK(social_code_parse("1234-5678-9ABC", NULL, NULL) == SOCIAL_CODE_BAD_TEXT, "short");
	CHECK(social_code_parse("1234-5678-9ABC-DEFG", NULL, NULL) == SOCIAL_CODE_BAD_TEXT, "long");
	CHECK(social_code_parse("1234-5678-9AB!-D", NULL, NULL) == SOCIAL_CODE_BAD_TEXT, "punctuation");
	CHECK(social_code_parse(NULL, NULL, NULL) == SOCIAL_CODE_BAD_TEXT, "null");
	CHECK(social_code_parse("0000-0000-0000-0", NULL, NULL) == SOCIAL_CODE_OK, "all zero has check zero");
	CHECK(social_code_parse("0000-0000-0000-1", NULL, NULL) == SOCIAL_CODE_BAD_CHECK, "all zero with a wrong check");
}

/* ---------- the envelope and the window */

static void case_envelope(void)
{
	unsigned char key[32], other[32], nonce[12], tag[8], plain[200], wire[SOCIAL_MAX_WIRE], opened[SOCIAL_MAX_PLAIN];
	const char *topic = "hceu/3/f/p/00112233445566778899aabbccddeeff";
	int kind;

	fill(key, 32);
	fill(other, 32);
	fill(nonce, 12);
	fill(tag, 8);
	fill(plain, sizeof(plain));
	for (kind = 0; kind < 3; kind++)
	{
		int kinds[3] = { SOCIAL_KIND_REQUEST, SOCIAL_KIND_PRESENCE, SOCIAL_KIND_DIRECT };
		int size = social_seal(key, kinds[kind], tag, topic, plain, 200, nonce, wire, sizeof(wire));
		int header = kinds[kind] == SOCIAL_KIND_DIRECT ? 10 : 2;
		int index;

		CHECK(size == header + 12 + 200 + 16, "size %d", size);
		CHECK(social_open(key, topic, wire, size, opened, sizeof(opened)) == 200 && !memcmp(opened, plain, 200), "round trip");
		CHECK(social_open(other, topic, wire, size, opened, sizeof(opened)) < 0, "wrong key");
		CHECK(social_open(key, "hceu/3/f/p/00112233445566778899aabbccddee00", wire, size, opened, sizeof(opened)) < 0, "wrong topic");
		/* every byte flipped, every bit of it */
		for (index = 0; index < size; index++)
		{
			int bit;

			for (bit = 0; bit < 8; bit++)
			{
				unsigned char copy[SOCIAL_MAX_WIRE];

				memcpy(copy, wire, (size_t)size);
				copy[index] ^= (unsigned char)(1 << bit);
				CHECK(social_open(key, topic, copy, size, opened, sizeof(opened)) < 0, "flip byte %d bit %d accepted (kind %c)", index, bit, kinds[kind]);
			}
		}
		/* every truncation and a longer one */
		for (index = 0; index < size; index++)
			CHECK(social_open(key, topic, wire, index, opened, sizeof(opened)) < 0, "short %d accepted", index);
		wire[size] = 0;
		CHECK(social_open(key, topic, wire, size + 1, opened, sizeof(opened)) < 0, "long accepted");
	}
	/* the kind is bound: a presence message relabelled a request is not opened */
	{
		int size = social_seal(key, SOCIAL_KIND_PRESENCE, NULL, topic, plain, 40, nonce, wire, sizeof(wire));

		wire[0] = SOCIAL_KIND_REQUEST;
		CHECK(social_open(key, topic, wire, size, opened, sizeof(opened)) < 0, "relabelled");
	}
	/* limits */
	CHECK(social_seal(key, SOCIAL_KIND_PRESENCE, NULL, topic, plain, SOCIAL_MAX_PLAIN + 1, nonce, wire, sizeof(wire)) < 0, "too big");
	CHECK(social_seal(key, SOCIAL_KIND_DIRECT, NULL, topic, plain, 10, nonce, wire, sizeof(wire)) < 0, "direct without a tag");
	CHECK(social_seal(key, SOCIAL_KIND_PRESENCE, NULL, topic, plain, 100, nonce, wire, 50) < 0, "capacity");
}

static void case_window(void)
{
	struct social_window window;
	int round;

	memset(&window, 0, sizeof(window));
	CHECK(!social_window_check(&window, 1, 0), "sequence 0 is none");
	CHECK(social_window_check(&window, 1, 1), "first");
	social_window_mark(&window, 1, 1);
	CHECK(!social_window_check(&window, 1, 1), "replay");
	CHECK(social_window_check(&window, 1, 5), "ahead");
	social_window_mark(&window, 1, 5);
	CHECK(social_window_check(&window, 1, 3) && social_window_check(&window, 1, 2), "behind, not seen");
	social_window_mark(&window, 1, 3);
	CHECK(!social_window_check(&window, 1, 3) && !social_window_check(&window, 1, 1) && !social_window_check(&window, 1, 5), "seen");
	CHECK(social_window_check(&window, 1, 4), "a gap filled late");
	CHECK(!social_window_check(&window, 0, 99), "a lower generation");
	CHECK(social_window_check(&window, 2, 1), "a higher generation, from one");
	social_window_mark(&window, 2, 1);
	CHECK(!social_window_check(&window, 1, 9) && !social_window_check(&window, 2, 1), "after a generation jump");
	/* far below the window */
	memset(&window, 0, sizeof(window));
	social_window_mark(&window, 1, 100);
	CHECK(!social_window_check(&window, 1, 67) && social_window_check(&window, 1, 68) && social_window_check(&window, 1, 99), "window edge");
	social_window_mark(&window, 1, 100 + 40);
	CHECK(!social_window_check(&window, 1, 100) && social_window_check(&window, 1, 139), "a big jump forgets the old");
	/* a model: a set of all numbers seen, and the window as the specification says (accept a number never seen, within 32 below the highest or above it) */
	for (round = 0; round < 300; round++)
	{
		static unsigned char seen[4][2000];
		unsigned int generation_high = 1;
		unsigned int highest = 0;
		int step;

		memset(seen, 0, sizeof(seen));
		memset(&window, 0, sizeof(window));
		for (step = 0; step < 400; step++)
		{
			unsigned int generation = generation_high;
			unsigned int sequence;
			int expected, actual;

			if (random_below(60) == 0)
			{
				generation = generation_high + 1;
			}
			else if (random_below(30) == 0 && generation_high > 1)
			{
				generation = generation_high - 1;
			}
			sequence = highest + 1 + random_below(6);
			if (random_below(2) && highest > 5)
				sequence = highest - random_below(45);
			if (sequence >= 2000 || generation > 3)
				continue;
			if (sequence == 0)
				expected = 0;
			else if (generation > generation_high)
				expected = 1;
			else if (generation < generation_high)
				expected = 0;
			else
				expected = sequence > 0 && !seen[generation][sequence] && (sequence > highest || highest - sequence <= 32);
			actual = social_window_check(&window, generation, sequence);
			CHECK(actual == expected, "generation %u sequence %u (high %u/%u): window says %d, model %d", generation, sequence, generation_high, highest, actual, expected);
			if (actual)
			{
				social_window_mark(&window, generation, sequence);
				if (generation > generation_high)
				{
					generation_high = generation;
					highest = sequence;
				}
				else if (sequence > highest)
					highest = sequence;
				seen[generation][sequence] = 1;
			}
		}
	}
}

/* ---------- the messages */

static void sample_presence(struct social_presence *presence, int flags)
{
	memset(presence, 0, sizeof(*presence));
	presence->circle_generation = 3;
	presence->flags = (unsigned char)flags;
	presence->activity = SOCIAL_GAME;
	presence->platform = SOCIAL_PLATFORM_WIIU;
	presence->network_version = 41;
	strcpy(presence->name, "Chief");
	if (flags & SOCIAL_PRESENCE_DETAILS)
	{
		strcpy(presence->map, "bloodgulch");
		strcpy(presence->gametype, "Slayer");
	}
	presence->players = 5;
	presence->maximum_players = 16;
	presence->engine_type = 2;
	if (flags & SOCIAL_PRESENCE_JOINABLE)
	{
		memset(presence->invite_hash, 0xA1, 16);
		memset(presence->invite_token, 0xB2, 16);
	}
	if (flags & SOCIAL_PRESENCE_PARTY_OPEN)
		memset(presence->party_token, 0xC3, 16);
}

static void case_presence_codec(void)
{
	struct social_identity identity;
	unsigned char seed[32], plain[SOCIAL_MAX_PLAIN];
	int flags;

	fill(seed, 32);
	social_identity_make(&identity, seed);
	for (flags = 0; flags < 256; flags++)
	{
		struct social_presence presence, decoded;
		const unsigned char *signature;
		int size, signed_size, cut;

		sample_presence(&presence, flags);
		size = social_presence_encode(&identity, 7, 9, &presence, plain, sizeof(plain));
		CHECK(size > 0 && size <= SOCIAL_MAX_PLAIN, "size %d for flags %02x", size, flags);
		CHECK(social_presence_decode(plain, size, &decoded, &signed_size, &signature), "decode flags %02x", flags);
		CHECK(signed_size == size - 64 && signature == plain + signed_size, "signature place");
		CHECK(decoded.flags == presence.flags && decoded.activity == presence.activity && decoded.platform == presence.platform &&
			decoded.network_version == 41 && !strcmp(decoded.name, "Chief") && decoded.players == 5 && decoded.maximum_players == 16 &&
			decoded.engine_type == 2 && decoded.circle_generation == 3, "fields flags %02x", flags);
		CHECK(!strcmp(decoded.map, presence.map) && !strcmp(decoded.gametype, presence.gametype), "text flags %02x", flags);
		CHECK(!memcmp(decoded.invite_hash, presence.invite_hash, 16) && !memcmp(decoded.invite_token, presence.invite_token, 16) &&
			!memcmp(decoded.party_token, presence.party_token, 16), "tokens flags %02x", flags);
		CHECK(social_presence_verify(identity.key, plain, signed_size, signature), "signature flags %02x", flags);
		/* any byte of the signed part changed breaks it (or the decode) */
		for (cut = 0; cut < size; cut++)
		{
			unsigned char copy[SOCIAL_MAX_PLAIN];
			struct social_presence again;
			const unsigned char *signature_again;
			int signed_again;

			memcpy(copy, plain, (size_t)size);
			copy[cut] ^= 0x01;
			if (social_presence_decode(copy, size, &again, &signed_again, &signature_again))
				CHECK(!social_presence_verify(identity.key, copy, signed_again, signature_again) || cut < 0, "byte %d flipped, flags %02x, still verifies", cut, flags);
			/* every truncation fails to decode */
			CHECK(!social_presence_decode(plain, cut, &again, &signed_again, &signature_again), "truncated to %d decodes", cut);
		}
		/* another key's signature does not do */
		{
			struct social_identity stranger;
			unsigned char other_seed[32];

			fill(other_seed, 32);
			social_identity_make(&stranger, other_seed);
			CHECK(!social_presence_verify(stranger.key, plain, signed_size, signature), "stranger's key verifies");
		}
	}
}

/* text from outside is cleaned where it is read */
static void case_presence_text(void)
{
	struct social_identity identity;
	unsigned char seed[32], plain[SOCIAL_MAX_PLAIN];
	struct social_presence presence, decoded;
	const unsigned char *signature;
	int size, signed_size, offset, index;

	fill(seed, 32);
	social_identity_make(&identity, seed);
	sample_presence(&presence, SOCIAL_PRESENCE_DETAILS);
	size = social_presence_encode(&identity, 1, 1, &presence, plain, sizeof(plain));
	/* the name, replaced in the bytes with a hostile one (an attacker who has the key can sign anything: the reader must not trust the text) */
	offset = 9 + 1 + 1 + 1 + 1 + 2;
	CHECK(plain[offset] == 5, "name length");
	{
		unsigned char bad[SOCIAL_MAX_PLAIN];
		static const unsigned char names[][5] = { { 'a', '|', 'b', 'c', 'd' }, { 1, 2, 3, 4, 5 }, { 0x80, 0xFF, 0xC3, 0xA9, 'x' }, { 0x7F, ' ', ' ', ' ', ' ' }, { 0, 0, 0, 0, 0 } };
		int name;

		for (name = 0; name < 5; name++)
		{
			memcpy(bad, plain, (size_t)size);
			memcpy(bad + offset + 1, names[name], 5);
			if (!social_presence_decode(bad, size, &decoded, &signed_size, &signature))
				continue;
			CHECK(decoded.name[0] != 0, "empty name");
			for (index = 0; decoded.name[index]; index++)
				CHECK(decoded.name[index] >= 0x20 && decoded.name[index] <= 0x7E && decoded.name[index] != '|', "name char %02x", (unsigned char)decoded.name[index]);
		}
	}
	/* a name longer than eleven characters is refused, not cut */
	{
		unsigned char longer[SOCIAL_MAX_PLAIN];

		memcpy(longer, plain, (size_t)offset);
		longer[offset] = 12;
		memset(longer + offset + 1, 'x', 12);
		memcpy(longer + offset + 13, plain + offset + 6, (size_t)(size - offset - 6));
		CHECK(!social_presence_decode(longer, size + 7, &decoded, &signed_size, &signature), "a name of twelve");
	}
}

static void case_message_codecs(void)
{
	struct social_identity identity, other;
	unsigned char seed[32], plain[SOCIAL_MAX_PLAIN];
	struct social_add add;
	struct social_hello hello, hello_back;
	struct social_invite invite, invite_back;
	int size, reason;

	fill(seed, 32);
	social_identity_make(&identity, seed);
	fill(seed, 32);
	social_identity_make(&other, seed);
	size = social_add_encode(&identity, 5, 6, "Cortana", 1, other.code_bytes, plain, sizeof(plain));
	CHECK(size == 9 + 32 + 1 + 7 + 1 + 64, "add size %d", size);
	CHECK(social_add_decode(plain, size, &add) && !strcmp(add.name, "Cortana") && add.flags == 1 && !memcmp(add.key, identity.key, 32), "add decode");
	CHECK(social_add_verify(&add, plain, other.code_bytes), "add verify");
	CHECK(!social_add_verify(&add, plain, identity.code_bytes), "an add re-sealed into another inbox verifies");
	CHECK(!social_add_decode(plain, size - 1, &add) && !social_add_decode(plain, size + 0 - 64 - 1, &add), "add truncated");
	memset(&hello, 0, sizeof(hello));
	hello.circle_generation = 4;
	fill(hello.circle_key, 32);
	hello.have_generation = 2;
	size = social_hello_encode(1, 2, &hello, plain, sizeof(plain));
	CHECK(size == 9 + 1 + 32 + 1, "hello size");
	CHECK(social_hello_decode(plain, size, &hello_back) && !memcmp(&hello, &hello_back, sizeof(hello)), "hello roundtrip");
	CHECK(!social_hello_decode(plain, size - 1, &hello_back) && !social_hello_decode(plain, size + 1, &hello_back), "hello length");
	plain[9] = 0;
	CHECK(!social_hello_decode(plain, size, &hello_back), "hello with generation 0");
	size = social_remove_encode(1, 2, 1, plain, sizeof(plain));
	CHECK(social_remove_decode(plain, size, &reason) && reason == 1 && !social_remove_decode(plain, size - 1, &reason), "remove");
	plain[9] = 7;
	CHECK(!social_remove_decode(plain, size, &reason), "remove reason 7");
	memset(&invite, 0, sizeof(invite));
	invite.kind = 1;
	memset(invite.target.host_hash, 1, 16);
	memset(invite.target.token, 2, 16);
	invite.target.network_version = 41;
	invite.target.players = 3;
	invite.target.maximum_players = 8;
	invite.target.engine_type = 4;
	strcpy(invite.target.map, "damnation");
	strcpy(invite.target.gametype, "King of the Hill");
	size = social_invite_encode(3, 4, &invite, plain, sizeof(plain));
	CHECK(size > 0 && social_invite_decode(plain, size, &invite_back) && invite_back.kind == 1 && !strcmp(invite_back.target.map, "damnation") &&
		!strcmp(invite_back.target.gametype, "King of the Hill") && invite_back.target.network_version == 41 && invite_back.target.players == 3 &&
		!memcmp(invite_back.target.host_hash, invite.target.host_hash, 16) && !memcmp(invite_back.target.token, invite.target.token, 16), "invite game");
	invite.kind = 2;
	memset(invite.party_token, 9, 16);
	invite.party_size = 8;
	size = social_invite_encode(3, 4, &invite, plain, sizeof(plain));
	CHECK(social_invite_decode(plain, size, &invite_back) && invite_back.kind == 2 && invite_back.party_size == 8, "invite party");
	plain[9 + 1 + 16] = 9;
	CHECK(!social_invite_decode(plain, size, &invite_back), "party of nine");
	invite.kind = 3;
	CHECK(social_invite_encode(1, 1, &invite, plain, sizeof(plain)) < 0, "invite kind 3");
}

/* the decoders are fed noise, and valid messages with bytes changed: nothing crashes, and what is read is clean */
static void case_fuzz_decoders(void)
{
	struct social_identity identity;
	unsigned char seed[32], valid[8][SOCIAL_MAX_PLAIN];
	int sizes[8], round;
	struct social_presence presence;
	struct social_hello hello;
	struct social_invite invite;

	fill(seed, 32);
	social_identity_make(&identity, seed);
	sample_presence(&presence, 0xFF);
	sizes[0] = social_presence_encode(&identity, 1, 1, &presence, valid[0], SOCIAL_MAX_PLAIN);
	sample_presence(&presence, 0);
	sizes[1] = social_presence_encode(&identity, 1, 1, &presence, valid[1], SOCIAL_MAX_PLAIN);
	sizes[2] = social_add_encode(&identity, 1, 1, "Name", 0, identity.code_bytes, valid[2], SOCIAL_MAX_PLAIN);
	memset(&hello, 0, sizeof(hello));
	hello.circle_generation = 1;
	sizes[3] = social_hello_encode(1, 1, &hello, valid[3], SOCIAL_MAX_PLAIN);
	sizes[4] = social_remove_encode(1, 1, 0, valid[4], SOCIAL_MAX_PLAIN);
	memset(&invite, 0, sizeof(invite));
	invite.kind = 1;
	strcpy(invite.target.map, "a30");
	sizes[5] = social_invite_encode(1, 1, &invite, valid[5], SOCIAL_MAX_PLAIN);
	invite.kind = 2;
	sizes[6] = social_invite_encode(1, 1, &invite, valid[6], SOCIAL_MAX_PLAIN);
	sizes[7] = 0;
	for (round = 0; round < 60000; round++)
	{
		unsigned char buffer[SOCIAL_MAX_PLAIN + 8];
		int size, type = random_below(7), index, changes;
		struct social_presence p;
		struct social_add a;
		struct social_hello h;
		struct social_invite i;
		const unsigned char *signature;
		int signed_size, reason;

		if (random_below(4) == 0)
		{
			size = (int)random_below(SOCIAL_MAX_PLAIN + 8);
			fill(buffer, size);
			/* (a plausible start: the type byte right) */
			if (size > 0 && random_below(2))
				buffer[0] = (unsigned char)"PPAHXII"[type];
		}
		else
		{
			size = sizes[type];
			memcpy(buffer, valid[type], (size_t)size);
			changes = 1 + (int)random_below(4);
			while (changes--)
			{
				int what = (int)random_below(3);

				if (what == 0 && size > 0)
					buffer[random_below((unsigned)size)] = (unsigned char)random_below(256);
				else if (what == 1 && size > 1)
					size = (int)random_below((unsigned)size);
				else if (size < SOCIAL_MAX_PLAIN)
					buffer[size++] = (unsigned char)random_below(256);
			}
		}
		social_presence_decode(buffer, size, &p, &signed_size, &signature);
		social_add_decode(buffer, size, &a);
		social_hello_decode(buffer, size, &h);
		social_invite_decode(buffer, size, &i);
		social_remove_decode(buffer, size, &reason);
		for (index = 0; p.name[index]; index++)
			CHECK(p.name[index] >= 0x20 && p.name[index] <= 0x7E && p.name[index] != '|', "presence name char");
		for (index = 0; p.map[index]; index++)
			CHECK(p.map[index] >= 0x20 && p.map[index] <= 0x7E && p.map[index] != '|', "map char");
		for (index = 0; a.name[index]; index++)
			CHECK(a.name[index] >= 0x20 && a.name[index] <= 0x7E && a.name[index] != '|', "add name char");
		for (index = 0; i.target.gametype[index]; index++)
			CHECK(i.target.gametype[index] >= 0x20 && i.target.gametype[index] <= 0x7E && i.target.gametype[index] != '|', "gametype char");
	}
}

/* ---------- the engine on the fake network */

static void case_presence_basic(void)
{
	static struct fake_net net;
	struct fake_node *a, *b;
	struct social_place place;
	struct social_friend *friend;

	fake_start(&net, 1);
	a = fake_add(&net, 3, 0);
	b = fake_add(&net, -9, 0);
	link_friends(a, b);
	/* neither's clock is anchored: the generation waits SOCIAL_ANCHOR_WAIT, and nothing is sent until it is chosen */
	fake_run(&net, 5000);
	CHECK(!a->engine.generation_ready && !b->engine.generation_ready && a->published == 0, "nothing is sent before the generation");
	fake_run(&net, 11000);
	CHECK(a->engine.generation_ready && b->engine.generation_ready, "generation chosen after the wait");
	fake_run(&net, 3000);
	CHECK(online_to(a, b) && online_to(b, a), "both online to each other");
	friend = friend_of(a, b);
	CHECK(friend->presence.activity == SOCIAL_MENUS && !strcmp(friend->presence.name, "Node1") && friend->presence.platform == SOCIAL_PLATFORM_WIIU &&
		friend->presence.network_version == 41, "presence fields");
	/* a game with a joinable invite */
	memset(&place, 0, sizeof(place));
	place.activity = SOCIAL_GAME;
	place.hosting = 1;
	place.joinable = 1;
	strcpy(place.map, "bloodgulch");
	strcpy(place.gametype, "Slayer");
	place.players = 3;
	place.maximum_players = 16;
	memset(place.invite_hash, 0x11, 16);
	memset(place.invite_token, 0x22, 16);
	social_set_place(&a->engine, &place);
	fake_run(&net, 4000);
	friend = friend_of(b, a);
	CHECK(friend->presence.activity == SOCIAL_GAME && (friend->presence.flags & SOCIAL_PRESENCE_JOINABLE) && !strcmp(friend->presence.map, "bloodgulch") &&
		!strcmp(friend->presence.gametype, "Slayer") && friend->presence.players == 3 && friend->presence.maximum_players == 16 &&
		friend->presence.invite_hash[0] == 0x11 && friend->presence.invite_token[0] == 0x22, "joinable game seen");
	/* each message came through four brokers: one was used, three were seen before */
	CHECK(b->engine.statistics.dropped_seen >= 3 * 2, "duplicates through the brokers dropped: %u", b->engine.statistics.dropped_seen);
	CHECK(b->engine.statistics.dropped_open == 0 && b->engine.statistics.dropped_signature == 0 && b->engine.statistics.dropped_window == 0, "no loss to errors");
}

/* the same scenario with every wall clock hours off, anchored or not */
static void case_clock_offsets(void)
{
	int round;

	for (round = 0; round < 6; round++)
	{
		static struct fake_net net;
		struct fake_node *a, *b, *c;

		fake_start(&net, 100 + (unsigned)round);
		a = fake_add(&net, (int)random_below(53) - 26, round & 1);
		b = fake_add(&net, (int)random_below(53) - 26, (round >> 1) & 1);
		c = fake_add(&net, (int)random_below(53) - 26, 1);
		link_friends(a, b);
		link_friends(b, c);
		link_friends(a, c);
		fake_run(&net, 30000);
		CHECK(online_to(a, b) && online_to(b, a) && online_to(b, c) && online_to(c, b) && online_to(a, c) && online_to(c, a), "round %d online", round);
		/* an anchored node's generation counts minutes from 2026, an unanchored one's is one more than stored */
		CHECK(a->engine.generation == (round & 1 ? (unsigned)((FAKE_UTC_BASE + (net.time - 30000) / 1000 - 1767225600ULL) / 60) : 1) ||
			a->engine.generation > 1, "generation %u", a->engine.generation);
		CHECK(c->engine.generation > 100000, "an anchored clock's generation is minutes since 2026: %u", c->engine.generation);
	}
}

static void case_offline_and_timeout(void)
{
	static struct fake_net net;
	struct fake_node *a, *b;
	struct social_event event;
	int online_events = 0, offline_events = 0;

	fake_start(&net, 3);
	net.verbose = getenv("SOCIAL_VERBOSE") != NULL;
	a = fake_add(&net, 5, 1);
	b = fake_add(&net, 5, 1);
	link_friends(a, b);
	fake_run(&net, 6000);
	CHECK(online_to(b, a), "online");
	while (social_event_take(&b->engine, &event))
		online_events += event.type == SOCIAL_EVENT_ONLINE;
	CHECK(online_events == 1, "one online event, %d", online_events);
	/* a stops: b calls it offline after 150 seconds, not before */
	a->alive = 0;
	fake_run(&net, 100000);
	CHECK(online_to(b, a), "still online at 100 s");
	fake_run(&net, 60000);
	CHECK(!online_to(b, a), "offline after 160 s");
	while (social_event_take(&b->engine, &event))
		offline_events += event.type == SOCIAL_EVENT_OFFLINE;
	CHECK(offline_events == 1, "one offline event, %d", offline_events);
	/* a returns: online again within a few seconds (b's FIRST answer is not needed: a says FIRST) */
	a->alive = 1;
	a->engine.ready_before = 0;
	fake_run(&net, 6000);
	CHECK(online_to(b, a), "back");
	/* a quits cleanly: offline at once */
	social_shutdown(&a->engine);
	fake_run(&net, 1000);
	CHECK(!online_to(b, a), "offline after a clean quit");
	fake_run(&net, 5000);
	CHECK(!online_to(b, a), "and nothing more is sent");
	social_set_enabled(&a->engine, 0);
	social_set_enabled(&a->engine, 1);
	a->engine.presence_sent = 0;
	a->engine.presence_offline_sent = 0;
	/* appear offline: one OFFLINE message, then silence, but a still hears b */
	social_set_share(&a->engine, 0, SOCIAL_DETAILS_BASIC, 1);
	a->engine.presence_offline_sent = 0;
	a->engine.presence_sent = 1;
	fake_run(&net, 2000);
	CHECK(!online_to(b, a), "hidden");
	{
		int before = a->published;

		fake_run(&net, 200000);
		CHECK(a->published == before, "a hidden machine publishes nothing: %d", a->published - before);
		CHECK(online_to(a, b), "a hidden machine still hears its friends");
	}
	social_set_share(&a->engine, 1, SOCIAL_DETAILS_BASIC, 1);
	fake_run(&net, 6000);
	CHECK(online_to(b, a), "shown again");
}

static void case_details(void)
{
	static struct fake_net net;
	struct fake_node *a, *b;
	struct social_place place;
	struct social_friend *friend;

	fake_start(&net, 4);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	link_friends(a, b);
	memset(&place, 0, sizeof(place));
	place.activity = SOCIAL_GAME;
	place.hosting = 1;
	place.joinable = 0;
	strcpy(place.map, "secret");
	strcpy(place.gametype, "Slayer");
	place.players = 4;
	social_set_place(&a->engine, &place);
	/* basic: activity only unless joinable */
	social_set_share(&a->engine, 1, SOCIAL_DETAILS_BASIC, 1);
	fake_run(&net, 6000);
	friend = friend_of(b, a);
	CHECK(friend->presence.activity == SOCIAL_GAME && friend->presence.map[0] == 0 && friend->presence.players == 0, "basic keeps the map to itself");
	social_set_share(&a->engine, 1, SOCIAL_DETAILS_FULL, 1);
	fake_run(&net, 20000);
	CHECK(!strcmp(friend->presence.map, "secret") && friend->presence.players == 4, "full shows the map");
	social_set_share(&a->engine, 1, SOCIAL_DETAILS_NONE, 1);
	fake_run(&net, 20000);
	CHECK(friend->presence.activity == SOCIAL_BUSY && friend->presence.map[0] == 0, "none shows only that it is online");
	/* a solo game is never joinable whatever the place says */
	place.activity = SOCIAL_SOLO;
	place.joinable = 1;
	social_set_place(&a->engine, &place);
	CHECK(!a->engine.place.joinable, "solo is not joinable");
}

/* the message to a topic a stranger holds the key of cannot be forged, and its replay is dropped */
static void case_forgery_and_replay(void)
{
	static struct fake_net net;
	struct fake_node *a, *b, *c;
	struct social_friend *friend;
	unsigned char plain[SOCIAL_MAX_PLAIN], nonce[12], wire[SOCIAL_MAX_WIRE];
	struct social_presence presence;
	int size, wire_size;
	unsigned int forged_before, window_before;

	fake_start(&net, 5);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	c = fake_add(&net, 0, 1);
	link_friends(a, b);
	link_friends(a, c);
	fake_run(&net, 6000);
	friend = friend_of(b, a);
	CHECK(online_to(b, a), "online");
	/* c holds a's circle key (a's friend): it seals a presence that claims to be a's, signed with its own key */
	sample_presence(&presence, SOCIAL_PRESENCE_DETAILS | SOCIAL_PRESENCE_JOINABLE);
	presence.circle_generation = a->engine.circle_generation;
	strcpy(presence.name, "Node0");
	memset(presence.invite_hash, 0xEE, 16);
	size = social_presence_encode(&c->engine.identity, a->engine.generation, 9999, &presence, plain, sizeof(plain));
	fill(nonce, 12);
	wire_size = social_seal(a->engine.circle_seal, SOCIAL_KIND_PRESENCE, NULL, a->engine.circle_topic, plain, size, nonce, wire, sizeof(wire));
	forged_before = b->engine.statistics.dropped_signature;
	window_before = friend->window.sequence;
	social_heard(&b->engine, a->engine.circle_topic, wire, wire_size);
	CHECK(b->engine.statistics.dropped_signature == forged_before + 1, "forgery refused");
	CHECK(friend->presence.invite_hash[0] != 0xEE && friend->window.sequence == window_before, "the forgery did not poison the window");
	/* a real presence of a's still gets through (sequence 9999 not marked) */
	{
		struct social_place place;

		memset(&place, 0, sizeof(place));
		place.activity = SOCIAL_LOBBY;
		social_set_place(&a->engine, &place);
		fake_run(&net, 5000);
		CHECK(friend->presence.activity == SOCIAL_LOBBY, "the real message after the forgery");
	}
	/* a replay of a real message after the dedupe ring has forgotten it: the window refuses */
	{
		struct social_presence real = friend->presence;
		unsigned char old_plain[SOCIAL_MAX_PLAIN];
		int old_size;
		unsigned int sequence = a->engine.sequence;
		int index;

		(void)real;
		/* the sequence just sent, again, with a fresh nonce (so it is not a duplicate in the ring) */
		old_size = social_presence_encode(&a->engine.identity, a->engine.generation, sequence, &a->engine.sent_presence, old_plain, sizeof(old_plain));
		fill(nonce, 12);
		wire_size = social_seal(a->engine.circle_seal, SOCIAL_KIND_PRESENCE, NULL, a->engine.circle_topic, old_plain, old_size, nonce, wire, sizeof(wire));
		forged_before = b->engine.statistics.dropped_window;
		for (index = 0; index < 2; index++)
			social_heard(&b->engine, a->engine.circle_topic, wire, wire_size);
		CHECK(b->engine.statistics.dropped_window + b->engine.statistics.dropped_seen >= forged_before + 2, "replay refused");
		CHECK(b->engine.statistics.dropped_window >= forged_before + 1, "by the window");
	}
	(void)b;
}

/* malformed input and floods */
static void case_abuse(void)
{
	static struct fake_net net;
	struct fake_node *a, *b;
	struct social_friend *friend;
	unsigned char junk[700];
	int round, verifies;
	unsigned char plain[SOCIAL_MAX_PLAIN], nonce[12], wire[SOCIAL_MAX_WIRE];
	struct social_presence presence;
	struct social_identity stranger;
	unsigned char seed[32];

	fake_start(&net, 6);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	link_friends(a, b);
	fake_run(&net, 6000);
	friend = friend_of(b, a);
	/* noise on every kind of topic: sizes from 0 to 700, topics of any length, none crashes and none is accepted */
	{
		char long_topic[200];
		unsigned int accepted = b->engine.statistics.accepted;

		memset(long_topic, 'x', sizeof(long_topic) - 1);
		long_topic[sizeof(long_topic) - 1] = 0;
		for (round = 0; round < 5000; round++)
		{
			int size = (int)random_below(700);
			const char *topic = random_below(3) == 0 ? b->engine.inbox_topic : random_below(2) ? friend->topic : "hceu/3/f/p/unknown";

			fill(junk, size);
			if (size > 2 && random_below(2))
			{
				junk[0] = (unsigned char)"RPDG"[random_below(4)];
				junk[1] = 1;
			}
			if (random_below(50) == 0)
				topic = long_topic;
			net.time += 30;
			social_heard(&b->engine, topic, junk, size);
		}
		social_heard(&b->engine, NULL, junk, 10);
		social_heard(&b->engine, friend->topic, NULL, 10);
		CHECK(b->engine.statistics.accepted == accepted, "noise accepted");
	}
	/* a flood of sealed presences that fail the signature, from thirty friends who hold the key of their own circles (an attacker with thirty names): what is opened a
	second is bounded (200), and what is checked for a signature a second (8 and 20) */
	{
		static struct social_identity attackers[30];
		static unsigned char circles[30][32];
		static struct social_friend *added[30];
		unsigned int attempts_before, signatures_before;
		int second;

		for (round = 0; round < 30; round++)
		{
			fill(seed, 32);
			fill(circles[round], 32);
			social_identity_make(&attackers[round], seed);
			added[round] = social_friend_add(&b->engine, attackers[round].key, 1, circles[round], 1);
		}
		fill(seed, 32);
		social_identity_make(&stranger, seed);
		sample_presence(&presence, 0);
		fake_run(&net, 1000);
		attempts_before = b->engine.statistics.attempted;
		signatures_before = b->engine.statistics.dropped_signature;
		/* 3000 messages in three seconds of the machine's time, a thousand a second, spread over the thirty */
		for (second = 0; second < 3; second++)
		{
			for (round = 0; round < 1000; round++)
			{
				int who = round % 30, size, wire_size;
				char topic[SOCIAL_TOPIC_SIZE], unused[1];
				unsigned char seal[32];

				(void)unused;
				net.time += 1;
				social_circle_for(circles[who], topic, seal);
				presence.circle_generation = 1;
				strcpy(presence.name, "Flood");
				size = social_presence_encode(&stranger, 5, 1 + (unsigned)(second * 1000 + round), &presence, plain, sizeof(plain));
				fill(nonce, 12);
				wire_size = social_seal(seal, SOCIAL_KIND_PRESENCE, NULL, topic, plain, size, nonce, wire, sizeof(wire));
				social_heard(&b->engine, topic, wire, wire_size);
			}
			social_update(&b->engine);
		}
		CHECK(b->engine.statistics.dropped_rate > 0, "the rates were limited");
		CHECK(b->engine.statistics.attempted - attempts_before <= SOCIAL_OPEN_RATE * 4, "messages opened: %u", b->engine.statistics.attempted - attempts_before);
		CHECK(b->engine.statistics.dropped_signature - signatures_before <= 3 * 1000 / SOCIAL_VERIFY_INTERVAL + SOCIAL_VERIFY_BURST + 2, "signature checks: %u",
			b->engine.statistics.dropped_signature - signatures_before);
		CHECK(b->engine.pending_count <= SOCIAL_MAX_PENDING, "the queue of waiting presences is bounded");
	}
	/* one friend sending valid presences a hundred times a second gets twenty a second */
	{
		struct social_place place;
		unsigned int accepted_before;
		int index;

		fake_run(&net, 5000);
		accepted_before = b->engine.statistics.accepted;
		for (index = 0; index < 300; index++)
		{
			int size, wire_size;

			net.time += 10;
			memset(&place, 0, sizeof(place));
			sample_presence(&presence, 0);
			presence.circle_generation = a->engine.circle_generation;
			strcpy(presence.name, "Node0");
			size = social_presence_encode(&a->engine.identity, a->engine.generation, 100000 + (unsigned)index, &presence, plain, sizeof(plain));
			fill(nonce, 12);
			wire_size = social_seal(a->engine.circle_seal, SOCIAL_KIND_PRESENCE, NULL, a->engine.circle_topic, plain, size, nonce, wire, sizeof(wire));
			social_heard(&b->engine, a->engine.circle_topic, wire, wire_size);
			if (index % 5 == 4)
				social_update(&b->engine);
		}
		fake_run(&net, 1000);
		CHECK(b->engine.statistics.accepted - accepted_before <= 3 * SOCIAL_FRIEND_RATE + 4, "one friend's messages taken: %u", b->engine.statistics.accepted - accepted_before);
		CHECK(b->engine.statistics.accepted - accepted_before >= 2 * SOCIAL_FRIEND_RATE, "and a fair number: %u", b->engine.statistics.accepted - accepted_before);
	}
	/* the friend's real messages recover once the flood stops */
	{
		struct social_place place;

		memset(&place, 0, sizeof(place));
		place.activity = SOCIAL_LOBBY;
		/* (the numbers the test used for a's messages are past a's own: it carries on from them) */
		a->engine.sequence = 100400;
		social_set_place(&a->engine, &place);
		fake_run(&net, 20000);
		CHECK(friend->presence.activity == SOCIAL_LOBBY, "recovered after the flood");
	}
	(void)junk;
	(void)verifies;
}

static void case_queue_and_bucket(void)
{
	static struct fake_net net;
	struct fake_node *a, *b;
	unsigned int before;
	unsigned char plain[20];
	int index, accepted = 0;

	fake_start(&net, 7);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	link_friends(a, b);
	fake_run(&net, 6000);
	before = a->published;
	memset(plain, 1, sizeof(plain));
	/* a hundred messages at once: eight wait, the rest are refused, and what goes out goes at 6 + 2.5 a second */
	for (index = 0; index < 100; index++)
		accepted += social_send(&a->engine, a->engine.circle_topic, SOCIAL_KIND_PRESENCE, a->engine.circle_seal, NULL, plain, sizeof(plain));
	CHECK(accepted == SOCIAL_MAX_QUEUE, "the queue holds %d", accepted);
	{
		unsigned int sent = a->engine.statistics.sent;

		fake_run(&net, 20);
		CHECK(a->engine.statistics.sent - sent <= SOCIAL_SEND_BURST, "a burst of %u", a->engine.statistics.sent - sent);
	}
	CHECK(a->engine.statistics.send_dropped >= 92, "the rest refused");
	fake_run(&net, 10000);
	CHECK(a->engine.queue_count == 0 && a->engine.statistics.sent >= 8, "the queue drains");
	/* with no broker ready, what waits more than two seconds is stale when a broker comes back */
	{
		int broker;

		for (broker = 0; broker < FAKE_BROKERS; broker++)
			a->broker_ready[broker] = 0;
		for (index = 0; index < 5; index++)
			social_send(&a->engine, a->engine.circle_topic, SOCIAL_KIND_PRESENCE, a->engine.circle_seal, NULL, plain, sizeof(plain));
		fake_run(&net, 5000);
		for (broker = 0; broker < FAKE_BROKERS; broker++)
			a->broker_ready[broker] = 1;
		fake_run(&net, 1000);
		CHECK(a->engine.statistics.send_expired == 5, "messages that wait more than two seconds expire: %u", a->engine.statistics.send_expired);
	}
	/* no broker ever saw more than its limit from this machine */
	CHECK(net.limited == 0, "the brokers' limit was hit %u times", net.limited);
	(void)before;
	/* over a minute, the sustained rate is below 3 a second per broker */
	{
		struct social_place place;
		int step;

		before = a->published;
		for (step = 0; step < 60; step++)
		{
			memset(&place, 0, sizeof(place));
			place.activity = (step & 1) ? SOCIAL_LOBBY : SOCIAL_MENUS;
			social_set_place(&a->engine, &place);
			fake_run(&net, 1000);
		}
		CHECK(a->published - before <= 60 * 3 * FAKE_BROKERS, "published %d in a minute", a->published - before);
		CHECK(net.limited == 0, "limit hit while changing every second: %u", net.limited);
	}
}

static void case_subscriptions(void)
{
	static struct fake_net net;
	struct fake_node *a, *b, *c;
	int index;

	fake_start(&net, 8);
	a = fake_add(&net, 0, 1);
	b = fake_add(&net, 0, 1);
	c = fake_add(&net, 0, 1);
	fake_run(&net, 100);
	CHECK(a->subscribed_count == 1 && !strcmp(a->subscribed[0], a->engine.inbox_topic), "only the inbox without friends");
	link_friends(a, b);
	fake_run(&net, 100);
	CHECK(a->subscribed_count == 2 && !strcmp(a->subscribed[1], b->engine.circle_topic), "a friend's topic added");
	link_friends(a, c);
	fake_run(&net, 100);
	CHECK(a->subscribed_count == 3, "three");
	social_set_share(&a->engine, 1, SOCIAL_DETAILS_BASIC, 0);
	fake_run(&net, 100);
	CHECK(a->subscribed_count == 2 && strcmp(a->subscribed[0], a->engine.inbox_topic), "requests off: no inbox");
	social_set_enabled(&a->engine, 0);
	fake_run(&net, 100);
	CHECK(a->subscribed_count == 0, "off: nothing");
	/* the topics are hashes: none shows a key or a code */
	for (index = 0; index < b->subscribed_count; index++)
		CHECK(!strstr(b->subscribed[index], "Node") && strlen(b->subscribed[index]) < SOCIAL_TOPIC_SIZE, "topic text");
}

/* fifty friends: 52 subscriptions, and a broker that takes only 30 */
static void case_fifty_friends(void)
{
	static struct fake_net net;
	int index;
	struct fake_node *hub;

	fake_start(&net, 9);
	hub = fake_add(&net, 0, 1);
	for (index = 0; index < 11; index++)
		fake_add(&net, 0, 1);
	/* made-up friends first (more than the list holds: the rest are refused), then the real ones, which come beyond the 30 topics two brokers take */
	for (index = 0; index < 70; index++)
	{
		unsigned char seed[32];
		struct social_identity fake_identity;
		unsigned char circle[32];
		struct social_friend *added;

		fill(seed, 32);
		fill(circle, 32);
		social_identity_make(&fake_identity, seed);
		if (index == 39)
			break;
		added = social_friend_add(&hub->engine, fake_identity.key, 1, circle, 1);
		CHECK(added, "filler %d", index);
	}
	for (index = 1; index < 12; index++)
		link_friends(hub, &net.nodes[index]);
	{
		unsigned char seed[32], circle[32];
		struct social_identity extra;

		fill(seed, 32);
		fill(circle, 32);
		social_identity_make(&extra, seed);
		CHECK(social_friend_count(&hub->engine) == SOCIAL_MAX_FRIENDS, "50 friends: %d", social_friend_count(&hub->engine));
		CHECK(social_friend_add(&hub->engine, extra.key, 1, circle, 1) == NULL, "a fifty-first refused");
		CHECK(social_friend_count(&hub->engine) == SOCIAL_MAX_FRIENDS, "still 50");
	}
	fake_run(&net, 100);
	CHECK(hub->subscribed_count == 1 + SOCIAL_MAX_FRIENDS, "inbox and fifty topics: %d", hub->subscribed_count);
	net.subscription_cap[0] = 30;
	net.subscription_cap[1] = 30;
	fake_run(&net, 30000);
	{
		int online = 0;

		for (index = 1; index < 12; index++)
			online += online_to(hub, &net.nodes[index]);
		if (online != 11)
		{
			for (index = 1; index < 12; index++)
				fprintf(stderr, "node %d online %d published %d; hub stats received %u accepted %u seen %u rate %u verify %u sig %u deferred %u window %u\n", index, online_to(hub, &net.nodes[index]), net.nodes[index].published, hub->engine.statistics.received, hub->engine.statistics.accepted, hub->engine.statistics.dropped_seen, hub->engine.statistics.dropped_rate, hub->engine.statistics.dropped_verify_budget, hub->engine.statistics.dropped_signature, hub->engine.statistics.deferred, hub->engine.statistics.dropped_window);
		}
		CHECK(online == 11, "all the real friends online through the two brokers with no cap: %d", online);
	}
	net.broker_up[2] = 0;
	net.broker_up[3] = 0;
	for (index = 1; index < 12; index++)
		net.nodes[index].engine.presence_time -= 100000;
	fake_run(&net, 200000);
	{
		int online = 0;

		for (index = 1; index < 12; index++)
			online += online_to(hub, &net.nodes[index]);
		CHECK(online == 0, "friends beyond the cap are not heard where only the capped brokers remain: %d", online);
	}
}

/* ---------- the wire's golden bytes */

static unsigned int golden_counter;

static void golden_random(void *user, unsigned char *bytes, int size)
{
	int index;

	(void)user;
	for (index = 0; index < size; index++)
		bytes[index] = (unsigned char)(++golden_counter * 37 + 11);
}

static void print_hex(const char *label, const unsigned char *bytes, int size)
{
	char text[2 * 1024 + 1];

	to_hex(bytes, size, text);
	printf("%s %s\n", label, text);
}

/* Every message type from a fixed pair of identities with a fixed nonce, as bytes: the same on every machine whatever its byte order. */
static void case_golden(void)
{
	struct social_identity a, b;
	unsigned char seed[32], plain[SOCIAL_MAX_PLAIN], wire[SOCIAL_MAX_WIRE], nonce[12], pair[32], tag_in[8], tag_out[8], pair_b[32], tag_in_b[8], tag_out_b[8];
	unsigned char circle[32], seal[32];
	char topic[SOCIAL_TOPIC_SIZE];
	struct social_presence presence;
	struct social_hello hello;
	struct social_invite invite;
	int index, size, wire_size;

	for (index = 0; index < 32; index++)
		seed[index] = (unsigned char)(index + 1);
	social_identity_make(&a, seed);
	for (index = 0; index < 32; index++)
		seed[index] = (unsigned char)(0xF0 - index);
	social_identity_make(&b, seed);
	for (index = 0; index < 12; index++)
		nonce[index] = (unsigned char)(0xA0 + index);
	for (index = 0; index < 32; index++)
		circle[index] = (unsigned char)(index * 7 + 3);
	print_hex("key-a", a.key, 32);
	print_hex("key-b", b.key, 32);
	printf("code-a %s\ncode-b %s\n", a.code, b.code);
	social_inbox_for(b.code_bytes, topic, seal);
	printf("inbox-topic-b %s\n", topic);
	print_hex("inbox-seal-b", seal, 32);
	social_circle_for(circle, topic, seal);
	printf("circle-topic %s\n", topic);
	print_hex("circle-seal", seal, 32);
	CHECK(social_pair_for(&a, b.key, pair, tag_in, tag_out) && social_pair_for(&b, a.key, pair_b, tag_in_b, tag_out_b), "pair");
	CHECK(!memcmp(pair, pair_b, 32) && !memcmp(tag_in, tag_out_b, 8) && !memcmp(tag_out, tag_in_b, 8), "both sides share the key and the tags");
	print_hex("pair", pair, 32);
	print_hex("tag-a-to-b", tag_out, 8);
	sample_presence(&presence, 0xFF);
	presence.flags = SOCIAL_PRESENCE_FIRST | SOCIAL_PRESENCE_HOSTING | SOCIAL_PRESENCE_JOINABLE | SOCIAL_PRESENCE_PARTY_OPEN | SOCIAL_PRESENCE_DETAILS;
	size = social_presence_encode(&a, 0x01020304, 0x0A0B0C0D, &presence, plain, sizeof(plain));
	print_hex("presence-plain", plain, size);
	wire_size = social_seal(circle, SOCIAL_KIND_PRESENCE, NULL, "hceu/3/f/p/x", plain, size, nonce, wire, sizeof(wire));
	print_hex("presence-wire", wire, wire_size);
	size = social_add_encode(&a, 0x11223344, 0x55667788, "Cortana", 1, b.code_bytes, plain, sizeof(plain));
	print_hex("add-plain", plain, size);
	wire_size = social_seal(seal, SOCIAL_KIND_REQUEST, NULL, "hceu/3/f/i/x", plain, size, nonce, wire, sizeof(wire));
	print_hex("add-wire", wire, wire_size);
	memset(&hello, 0, sizeof(hello));
	hello.circle_generation = 5;
	memcpy(hello.circle_key, circle, 32);
	hello.have_generation = 2;
	size = social_hello_encode(0x01020304, 0x05060708, &hello, plain, sizeof(plain));
	print_hex("hello-plain", plain, size);
	wire_size = social_seal(pair, SOCIAL_KIND_DIRECT, tag_out, "hceu/3/f/i/x", plain, size, nonce, wire, sizeof(wire));
	print_hex("hello-wire", wire, wire_size);
	size = social_remove_encode(0x01020304, 0x05060708, 1, plain, sizeof(plain));
	print_hex("remove-plain", plain, size);
	memset(&invite, 0, sizeof(invite));
	invite.kind = 1;
	memset(invite.target.host_hash, 0x31, 16);
	memset(invite.target.token, 0x42, 16);
	invite.target.network_version = 0x0129;
	invite.target.players = 3;
	invite.target.maximum_players = 16;
	invite.target.engine_type = 2;
	strcpy(invite.target.map, "bloodgulch");
	strcpy(invite.target.gametype, "Slayer");
	size = social_invite_encode(0x01020304, 0x05060708, &invite, plain, sizeof(plain));
	print_hex("invite-game-plain", plain, size);
	invite.kind = 2;
	memset(invite.party_token, 0x55, 16);
	invite.party_size = 4;
	size = social_invite_encode(0x01020304, 0x05060708, &invite, plain, sizeof(plain));
	print_hex("invite-party-plain", plain, size);
	{
		struct social_window window;

		memset(&window, 0, sizeof(window));
		social_window_mark(&window, 0x01020304, 0x0A0B0C0D);
		social_window_mark(&window, 0x01020304, 0x0A0B0C0B);
		printf("window %08x %08x %08x\n", window.generation, window.sequence, window.bitmap);
	}
}

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "code-vectors"))
		case_code_vectors(argc - 2, argv + 2);
	else if (!strcmp(name, "code-check"))
		case_code_check();
	else if (!strcmp(name, "code-forgiving"))
		case_code_forgiving();
	else if (!strcmp(name, "envelope"))
		case_envelope();
	else if (!strcmp(name, "window"))
		case_window();
	else if (!strcmp(name, "presence-codec"))
		case_presence_codec();
	else if (!strcmp(name, "presence-text"))
		case_presence_text();
	else if (!strcmp(name, "message-codecs"))
		case_message_codecs();
	else if (!strcmp(name, "fuzz-decoders"))
		case_fuzz_decoders();
	else if (!strcmp(name, "presence-basic"))
		case_presence_basic();
	else if (!strcmp(name, "clock-offsets"))
		case_clock_offsets();
	else if (!strcmp(name, "offline-and-timeout"))
		case_offline_and_timeout();
	else if (!strcmp(name, "details"))
		case_details();
	else if (!strcmp(name, "forgery-and-replay"))
		case_forgery_and_replay();
	else if (!strcmp(name, "abuse"))
		case_abuse();
	else if (!strcmp(name, "queue-and-bucket"))
		case_queue_and_bucket();
	else if (!strcmp(name, "subscriptions"))
		case_subscriptions();
	else if (!strcmp(name, "fifty-friends"))
		case_fifty_friends();
	else if (!strcmp(name, "golden"))
		case_golden();
	else
	{
		fprintf(stderr, "no case named %s\n", name);
		return 2;
	}
	(void)golden_random;
	return 0;
}
