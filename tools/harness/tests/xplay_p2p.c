/* xplay_p2p.c: internet play's bytes (the platform layer, port/linux/src and the libraries it carries) on both byte orders:
the primitives that seal the tunnel's packets and sign the listings (SHA-256, HMAC, ChaCha20-Poly1305, X25519, Ed25519: p2p_crypto.c,
Monocypher), KCP, which carries the game's TCP over the tunnel (ikcp.c: its segments are little-endian whatever the
machine), a public game's listing as the host makes it, signs it and a browser reads it (p2p_lobby.c, with a fixed clock), and the
master server's signed list (master_list.c) and the host-signed listings in it, from the master protocol's test vectors
(tests/master_vectors.json, as xplay_vectors.inc). Prints the golden file (tests/xplay_golden/p2p.txt) both byte orders must
equal. */

#include "xplay_common.h"
#define time(place) xplay_time(place)
#include "p2p_lobby.c"
#undef time
#include "ikcp.h"
#include "xplay_vectors.inc"

#define FIXED_TIME 1760900000UL

time_t xplay_time(time_t *place)
{
	if (place)
		*place = (time_t)FIXED_TIME;
	return (time_t)FIXED_TIME;
}

/* ---------- what the listing code calls */

static unsigned char host_seed[P2P_SEED_SIZE], host_public[P2P_KEY_SIZE];

const unsigned char *p2p_signing_key(void)
{
	return host_public;
}

void p2p_sign(const void *message, int size, unsigned char *signature)
{
	p2p_ed25519_sign(host_seed, host_public, message, size, signature);
}

/* (p2p.c's, which is not linked: the hex of bytes) */
void p2p_hex(const unsigned char *bytes, int size, char *text)
{
	static const char digits[] = "0123456789abcdef";
	int index;

	for (index = 0; index < size; index++)
	{
		text[index * 2] = digits[bytes[index] >> 4];
		text[index * 2 + 1] = digits[bytes[index] & 15];
	}
	text[size * 2] = 0;
}

int halo_server_mode(void)
{
	return 0;
}

/* ---------- text */

static int hex_value(char digit)
{
	return digit >= '0' && digit <= '9' ? digit - '0' : digit >= 'a' && digit <= 'f' ? digit - 'a' + 10 : digit - 'A' + 10;
}

static long unhex(const char *text, unsigned char *bytes)
{
	long length = (long)strlen(text) / 2, index;

	for (index = 0; index < length; index++)
		bytes[index] = (unsigned char)(hex_value(text[2 * index]) << 4 | hex_value(text[2 * index + 1]));
	return length;
}

/* ---------- the primitives */

static void test_crypto(void)
{
	unsigned char key[32], nonce[12], digest[64], sealed[200], opened[200], point[32], result[32], public_key[32], seed[32],
		signature[64], message[100];
	long index;
	int size;

	for (index = 0; index < 32; index++)
		key[index] = (unsigned char)(index * 7 + 3);
	for (index = 0; index < 12; index++)
		nonce[index] = (unsigned char)(index * 11 + 1);
	for (index = 0; index < 100; index++)
		message[index] = (unsigned char)(index * 5 + 9);

	p2p_sha256(message, 100, digest);
	xplay_hex("sha256", digest, 32);
	p2p_hmac_sha256(key, 32, message, 100, digest);
	xplay_hex("hmac_sha256", digest, 32);
	p2p_sha512(message, 100, digest);
	xplay_hex("sha512", digest, 64);

	/* (the tunnel's packet: a header of 1 + identifier + 8, authenticated, and the inner bytes sealed) */
	size = p2p_aead_seal(key, nonce, message, 17, message + 20, 63, sealed);
	xplay_hex("aead_seal", sealed, size);
	size = p2p_aead_open(key, nonce, message, 17, sealed, size, opened);
	XPLAY_CHECK(size == 63 && !memcmp(opened, message + 20, 63), "aead: opened %d bytes, not what was sealed", size);
	sealed[3] ^= 1;
	XPLAY_CHECK(p2p_aead_open(key, nonce, message, 17, sealed, 63 + P2P_TAG_SIZE, opened) < 0, "aead: an altered packet opened");

	/* (X25519: RFC 7748, section 6.1) */
	{
		unsigned char alice[32], bob[32], expected[32];

		unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", alice);
		unhex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", bob);
		p2p_x25519(public_key, alice, NULL);
		unhex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", expected);
		XPLAY_CHECK(!memcmp(public_key, expected, 32), "x25519: RFC 7748's public key is not what it gives");
		xplay_hex("x25519_public", public_key, 32);
		p2p_x25519(result, alice, bob);
		unhex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", expected);
		XPLAY_CHECK(!memcmp(result, expected, 32), "x25519: RFC 7748's shared secret is not what it gives");
		xplay_hex("x25519_shared", result, 32);
		(void)bob;
	}

	/* (Ed25519: RFC 8032, test 1 (empty message) and the host's) */
	unhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", seed);
	p2p_ed25519_public(seed, public_key, NULL);
	{
		unsigned char expected[64];

		unhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", expected);
		XPLAY_CHECK(!memcmp(public_key, expected, 32), "ed25519: RFC 8032's public key is not what it gives");
		p2p_ed25519_sign(seed, public_key, "", 0, signature);
		unhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b", expected);
		XPLAY_CHECK(!memcmp(signature, expected, 64), "ed25519: RFC 8032's signature is not what it gives");
	}
	p2p_ed25519_sign(seed, public_key, message, 100, signature);
	xplay_hex("ed25519_signature", signature, 64);
	XPLAY_CHECK(p2p_ed25519_verify(public_key, message, 100, signature), "ed25519: its own signature did not verify");
	{
		unsigned char x[32];

		XPLAY_CHECK(p2p_ed25519_to_x25519(public_key, x), "ed25519: no x25519 form");
		xplay_hex("ed25519_to_x25519", x, 32);
	}
}

/* ---------- KCP */

static unsigned char kcp_out[8][2048];
static long kcp_out_size[8];
static int kcp_out_count;

static int kcp_output(const char *buffer, int length, ikcpcb *kcp, void *user)
{
	if (kcp_out_count < 8)
	{
		memcpy(kcp_out[kcp_out_count], buffer, (size_t)length);
		kcp_out_size[kcp_out_count++] = length;
	}
	(void)kcp;
	(void)user;
	return length;
}

static void test_kcp(void)
{
	ikcpcb *sender = ikcp_create(0x11223344, NULL);
	ikcpcb *receiver = ikcp_create(0x11223344, NULL);
	char message[300], back[400];
	long index;
	int got;

	XPLAY_CHECK(IWORDS_BIG_ENDIAN == (
#ifdef __BIG_ENDIAN__
		1
#else
		0
#endif
		), "KCP thinks this machine is %s-endian", IWORDS_BIG_ENDIAN ? "big" : "little");
	for (index = 0; index < (long)sizeof(message); index++)
		message[index] = (char)(index * 3 + 7);
	sender->output = kcp_output;
	receiver->output = kcp_output;
	ikcp_nodelay(sender, 1, 10, 2, 1);
	ikcp_nodelay(receiver, 1, 10, 2, 1);
	ikcp_wndsize(sender, 64, 64);
	ikcp_send(sender, message, sizeof(message));
	ikcp_update(sender, 1000);
	ikcp_update(sender, 1010);
	for (index = 0; index < kcp_out_count; index++)
	{
		char name[32];

		sprintf(name, "kcp_segment_%ld", index);
		xplay_hex(name, kcp_out[index], kcp_out_size[index]);
		ikcp_input(receiver, (const char *)kcp_out[index], (long)kcp_out_size[index]);
	}
	XPLAY_CHECK(kcp_out_count > 0, "KCP sent nothing");
	ikcp_update(receiver, 1020);
	got = ikcp_recv(receiver, back, sizeof(back));
	XPLAY_CHECK(got == (int)sizeof(message) && !memcmp(back, message, sizeof(message)), "KCP: received %d bytes, not the message", got);
	/* (the acknowledgement it sent back) */
	if (kcp_out_count > 1)
		xplay_hex("kcp_ack", kcp_out[kcp_out_count - 1], kcp_out_size[kcp_out_count - 1]);
	ikcp_release(sender);
	ikcp_release(receiver);
}

/* ---------- the listing */

static void fill_lobby(void)
{
	memset(&lobby, 0, sizeof(lobby));
	strcpy(lobby.name, "Xplay Test");
	strcpy(lobby.map, "bloodgulch");
	strcpy(lobby.gametype, "Team Slayer");
	lobby.engine_type = 1;
	lobby.kind = 0x81;
	lobby.player_count = 5;
	lobby.maximum_player_count = 16;
	lobby.sequence = 411;
	unhex("a0a1a2a3a4a5a6a7a8a9aaabacadaeaf", lobby.token);
}

static void test_listing(void)
{
	unsigned char bytes[MAXIMUM_LISTING_SIZE];
	struct listing listing;
	int size, flags;

	unhex("c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7", host_seed);
	p2p_ed25519_public(host_seed, host_public, NULL);
	fill_lobby();
	flags = _listing_open | _listing_has_teams;
	size = listing_make_token(bytes, flags, lobby.token);
	xplay_hex("listing", bytes, size);
	memset(&listing, 0, sizeof(listing));
	XPLAY_CHECK(listing_read(bytes, size, &listing), "the listing made is not read");
	XPLAY_CHECK(listing.version == HALO_PORT_NETWORK_VERSION && listing.flags == flags && listing.sequence == 412 &&
		listing.time == FIXED_TIME && listing.player_count == 5 && listing.maximum_player_count == 16 &&
		listing.engine_type == 1 && listing.kind == 0x81 && !strcmp(listing.name, "Xplay Test") &&
		!strcmp(listing.map, "bloodgulch") && !strcmp(listing.gametype, "Team Slayer"), "the listing read is not the listing made");
	XPLAY_CHECK(listing_signed(bytes, &listing), "the listing made does not verify");
	XPLAY_CHECK(listing_current(&listing, 1), "the listing made is not current");
}

/* the master protocol's vectors: host-signed listings made by the master's own implementation (Go), read and checked here */
static void test_vectors(void)
{
	long index;
	unsigned char key[32];
	char why[128];

	for (index = 0; index < XPLAY_VECTOR_LISTING_COUNT; index++)
	{
		unsigned char bytes[MAXIMUM_LISTING_SIZE];
		struct listing listing;
		const struct xplay_vector_listing *vector = &xplay_vector_listings[index];
		long size = unhex(vector->hex, bytes);
		char name[32];

		memset(&listing, 0, sizeof(listing));
		sprintf(name, "vector_listing_%ld", index);
		XPLAY_CHECK(listing_read(bytes, (int)size, &listing) == 1, "%s: not read", name);
		XPLAY_CHECK(!vector->valid || (listing.version == vector->version && listing.flags == vector->flags &&
			listing.sequence == vector->sequence && listing.time == vector->time &&
			listing.player_count == vector->players && listing.maximum_player_count == vector->max_players &&
			listing.engine_type == vector->engine && listing.kind == vector->kind &&
			!strcmp(listing.name, vector->name) && !strcmp(listing.map, vector->map) &&
			!strcmp(listing.gametype, vector->gametype)), "%s: read, not what the master made", name);
		XPLAY_CHECK(listing_signed(bytes, &listing) == vector->valid, "%s: its signature is %s", name, vector->valid ? "refused" : "taken");
		printf("%s %d %d %lu %lu %d %d %d %d %s|%s|%s\n", name, listing.version, listing.flags, listing.sequence,
			listing.time, listing.player_count, listing.maximum_player_count, listing.engine_type, listing.kind,
			listing.name, listing.map, listing.gametype);
	}
	/* the master's signed list */
	unhex(xplay_vector_master_key, key);
	XPLAY_CHECK(master_list_verify(xplay_vector_list_body, (unsigned long)strlen(xplay_vector_list_body),
		xplay_vector_list_signature, xplay_vector_list_key_id, key, xplay_vector_list_time + 5, why, sizeof(why)) == 1,
		"the master's list is refused: %s", why);
	XPLAY_CHECK(master_list_verify(xplay_vector_list_body, (unsigned long)strlen(xplay_vector_list_body),
		xplay_vector_list_signature, xplay_vector_list_key_id, key, xplay_vector_list_time + 100000, why, sizeof(why)) == 0,
		"a list days old is taken");
	printf("master_list_verify ok\n");
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	test_crypto();
	test_kcp();
	test_listing();
	test_vectors();
	return xplay_failures ? XPLAY_FAIL_STATUS : 0;
}
