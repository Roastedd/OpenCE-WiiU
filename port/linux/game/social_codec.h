/*
SOCIAL_CODEC.H

The bytes of the friends layer (social_core.h): the friend code, the keys and topics derived from keys, the sealed envelope, the replay window, and each message's
encoding and decoding. Pure code over p2p_crypto.c's primitives and text_safe.h; no clock, no globals, no input or output, so the harness runs it on both byte
orders against one golden file.

Wire format (version 1). Every integer is big-endian, written byte by byte.

	envelope on a topic "hceu/3/f/..."
		0	1	kind: 'R' request (to an inbox, sealed with the inbox key), 'P' presence (the circle key), 'D' direct (the pair key), 'G' party (the party key)
		1	1	wire version, 1
		2	8	'D' only: the tag of sender and receiver
		n	12	nonce
		n+12	size	ciphertext
		end	16	Poly1305 tag
	The additional data are the bytes before the nonce and the topic's text, so that a message cannot move to another topic or be relabelled.

	inside, every message: type(1) generation(4) sequence(4), then its body (social_codec.c has each)
*/

#ifndef __HALO_LINUX_SOCIAL_CODEC_H
#define __HALO_LINUX_SOCIAL_CODEC_H

enum
{
	SOCIAL_SEED_SIZE = 32,
	SOCIAL_KEY_SIZE = 32,
	SOCIAL_SIGNATURE_SIZE = 64,
	/* the first bytes of a key's SHA-256: what presence and the invite's host go by */
	SOCIAL_HASH_SIZE = 16,
	SOCIAL_HASH_SIZE_FULL = 32,
	SOCIAL_TOKEN_SIZE = 16,
	SOCIAL_TAG_SIZE = 8,
	/* the friend code's 60 bits, left-aligned in 8 bytes */
	SOCIAL_CODE_BYTES = 8,
	/* "XXXX-XXXX-XXXX-C" and its end */
	SOCIAL_CODE_TEXT_SIZE = 17,
	/* the letters and digits of a code without its dashes, the check last */
	SOCIAL_CODE_SYMBOLS = 13,
	SOCIAL_NAME_SIZE = 11,
	SOCIAL_MAP_SIZE = 32,
	SOCIAL_GAMETYPE_SIZE = 24,
	SOCIAL_CHAT_SIZE = 63,
	SOCIAL_TOPIC_SIZE = 64,
	SOCIAL_NONCE_SIZE = 12,
	SOCIAL_AEAD_TAG_SIZE = 16,
	/* the most a message holds before it is sealed, and the most that goes on the wire (the brokers' path takes 512 and 28) */
	SOCIAL_MAX_PLAIN = 440,
	SOCIAL_MAX_WIRE = 2 + SOCIAL_TAG_SIZE + SOCIAL_NONCE_SIZE + SOCIAL_MAX_PLAIN + SOCIAL_AEAD_TAG_SIZE,
	SOCIAL_WIRE_VERSION = 1,
	/* type, generation, sequence */
	SOCIAL_INNER_HEADER = 9,
	SOCIAL_MAX_PARTY_MEMBERS = 8,
};

/* the envelope's kinds */
enum
{
	SOCIAL_KIND_REQUEST = 'R',
	SOCIAL_KIND_PRESENCE = 'P',
	SOCIAL_KIND_DIRECT = 'D',
	SOCIAL_KIND_PARTY = 'G',
};

/* the messages' types */
enum
{
	SOCIAL_TYPE_ADD = 'A',
	SOCIAL_TYPE_HELLO = 'H',
	SOCIAL_TYPE_PRESENCE = 'P',
	SOCIAL_TYPE_INVITE = 'I',
	SOCIAL_TYPE_REMOVE = 'X',
	SOCIAL_TYPE_PARTY_STATE = 'S',
	SOCIAL_TYPE_PARTY_LEAVE = 'L',
	SOCIAL_TYPE_PARTY_KICK = 'K',
	SOCIAL_TYPE_PARTY_DISBAND = 'B',
	SOCIAL_TYPE_PARTY_TRANSFER = 'T',
	SOCIAL_TYPE_PARTY_CHAT = 'C',
};

/* presence's flags */
enum
{
	SOCIAL_PRESENCE_FIRST = 0x01,
	SOCIAL_PRESENCE_OFFLINE = 0x02,
	SOCIAL_PRESENCE_HOSTING = 0x04,
	SOCIAL_PRESENCE_JOINABLE = 0x08,
	SOCIAL_PRESENCE_IN_PARTY = 0x10,
	SOCIAL_PRESENCE_PARTY_OPEN = 0x20,
	SOCIAL_PRESENCE_PRIVATE = 0x40,
	SOCIAL_PRESENCE_DETAILS = 0x80,
};

/* ---------- bytes, big-endian */

struct social_writer
{
	unsigned char *bytes;
	int size, capacity;
	int bad;
};

struct social_reader
{
	const unsigned char *bytes;
	int size, offset;
	int bad;
};

void social_writer_start(struct social_writer *writer, unsigned char *bytes, int capacity);
void social_put8(struct social_writer *writer, unsigned int value);
void social_put16(struct social_writer *writer, unsigned int value);
void social_put32(struct social_writer *writer, unsigned int value);
void social_put_bytes(struct social_writer *writer, const void *bytes, int size);
/* a length byte and text (cut to maximum characters, cleaned of what may not be drawn: text_safe.h) */
void social_put_text(struct social_writer *writer, const char *text, int maximum);

void social_reader_start(struct social_reader *reader, const unsigned char *bytes, int size);
unsigned int social_get8(struct social_reader *reader);
unsigned int social_get16(struct social_reader *reader);
unsigned int social_get32(struct social_reader *reader);
void social_get_bytes(struct social_reader *reader, void *bytes, int size);
/* a length byte and text: more than maximum characters is bad. The text is cleaned (a stranger's raw string is never kept), ended. */
void social_get_text(struct social_reader *reader, char *text, int size, int maximum);

void social_hex(const unsigned char *bytes, int size, char *text);

/* ---------- identity and the friend code */

struct social_identity
{
	unsigned char seed[SOCIAL_SEED_SIZE];
	unsigned char key[SOCIAL_KEY_SIZE];
	/* the X25519 secret of the same key, for the keys two friends share */
	unsigned char secret[SOCIAL_KEY_SIZE];
	unsigned char hash[SOCIAL_HASH_SIZE_FULL];
	unsigned char code_bytes[SOCIAL_CODE_BYTES];
	char code[SOCIAL_CODE_TEXT_SIZE];
};

void social_identity_make(struct social_identity *identity, const unsigned char *seed);
/* the code of a public key: its 8 bytes and its text */
void social_code_of_key(const unsigned char *key, unsigned char *code_bytes, char *text);
/* the check symbol of 12 symbols' values (0 to 31): a Damm check over GF(32); running all 13 through the same loop gives 0 */
int social_code_check_symbol(const unsigned char *symbols);
/* a typed code: forgiving (case, dashes, I L O U) and checked. 0 and its 8 bytes, or the reason: 1 not 13 symbols or not letters and digits, 2 the check fails */
enum
{
	SOCIAL_CODE_OK,
	SOCIAL_CODE_BAD_TEXT,
	SOCIAL_CODE_BAD_CHECK,
};
int social_code_parse(const char *text, unsigned char *code_bytes, char *clean_text);

/* ---------- keys and topics */

/* HMAC-SHA256 of label (text) and data under key */
void social_hmac(const unsigned char *key, const char *label, const void *data, int size, unsigned char *digest);
/* the inbox of a code: its topic and the key requests to it are sealed with */
void social_inbox_for(const unsigned char *code_bytes, char *topic, unsigned char *seal);
/* the topic a circle key is published on and the key presence under it is sealed with */
void social_circle_for(const unsigned char *circle_key, char *topic, unsigned char *seal);
/* the party's topic and key from its token */
void social_party_for(const unsigned char *token, char *topic, unsigned char *seal);
/* the key two friends share (X25519 of this identity's secret and the other's key, 0 if the other's key is unusable), and the tag of a message from each to the other */
int social_pair_for(const struct social_identity *identity, const unsigned char *their_key, unsigned char *pair_key, unsigned char *tag_in,
	unsigned char *tag_out);

/* ---------- the sealed envelope */

/* header (2 bytes, or 10 with a tag), nonce, ciphertext, tag, for the topic. The size, or -1 if it does not fit or the arguments are wrong */
int social_seal(const unsigned char *key, int kind, const unsigned char *tag, const char *topic, const unsigned char *plain, int plain_size,
	const unsigned char *nonce, unsigned char *wire, int capacity);
/* the plain size, or -1 if the key, the topic or the bytes are not those it was sealed with. kind and version have been looked at (social_wire_header) */
int social_open(const unsigned char *key, const char *topic, const unsigned char *wire, int wire_size, unsigned char *plain, int capacity);
/* a wire message's kind, whether it carries a tag, the tag; 0 if it is too short for its kind or its version is not ours */
int social_wire_header(const unsigned char *wire, int wire_size, int *kind, const unsigned char **tag);

/* ---------- the replay window */

/* The highest (generation, sequence) heard of a sender and which of the 32 below it were: a sender's messages come through up to four brokers in different orders, so
a window rather than "above the last". A higher generation (a restart) is accepted at once; a lower one never. Sequence 0 is none. */
struct social_window
{
	unsigned int generation;
	unsigned int sequence;
	unsigned int bitmap;
	int started;
};

int social_window_check(const struct social_window *window, unsigned int generation, unsigned int sequence);
void social_window_mark(struct social_window *window, unsigned int generation, unsigned int sequence);

/* ---------- messages */

struct social_inner
{
	int type;
	unsigned int generation;
	unsigned int sequence;
};

/* the inner header of a plain message: 0 if it is too short */
int social_inner_header(const unsigned char *plain, int size, struct social_inner *inner);

/* Ed25519 over label, extra and plain[0, size); checks the signature */
int social_verify(const char *label, const unsigned char *extra, int extra_size, const unsigned char *key, const unsigned char *plain, int size,
	const unsigned char *signature);
void social_sign(const struct social_identity *identity, const char *label, const unsigned char *extra, int extra_size, const unsigned char *plain, int size,
	unsigned char *signature);

/* ADD: I would like to be friends (key, name, flags: 1 = not accepting other requests now) */
struct social_add
{
	unsigned char key[SOCIAL_KEY_SIZE];
	char name[SOCIAL_NAME_SIZE + 1];
	unsigned char flags;
	/* where the signature is and what it covers */
	int signed_size;
	const unsigned char *signature;
};
int social_add_encode(const struct social_identity *identity, unsigned int generation, unsigned int sequence, const char *name, int flags,
	const unsigned char *recipient_code, unsigned char *plain, int capacity);
int social_add_decode(const unsigned char *plain, int size, struct social_add *add);
int social_add_verify(const struct social_add *add, const unsigned char *plain, const unsigned char *recipient_code);

/* HELLO: this is my circle key; the generation of yours that I hold (0: none) */
struct social_hello
{
	unsigned char circle_generation;
	unsigned char circle_key[SOCIAL_KEY_SIZE];
	unsigned char have_generation;
};
int social_hello_encode(unsigned int generation, unsigned int sequence, const struct social_hello *hello, unsigned char *plain, int capacity);
int social_hello_decode(const unsigned char *plain, int size, struct social_hello *hello);

/* REMOVE: reason 0 removed, 1 blocked */
int social_remove_encode(unsigned int generation, unsigned int sequence, int reason, unsigned char *plain, int capacity);
int social_remove_decode(const unsigned char *plain, int size, int *reason);

/* a game that can be joined: the host's key hash and the invite's token, as halo://join/ has them */
struct social_join_target
{
	unsigned char host_hash[SOCIAL_HASH_SIZE];
	unsigned char token[SOCIAL_TOKEN_SIZE];
	unsigned short network_version;
	unsigned char players, maximum_players, engine_type;
	char map[SOCIAL_MAP_SIZE + 1];
	char gametype[SOCIAL_GAMETYPE_SIZE + 1];
	/* whose game it is */
	char name[SOCIAL_NAME_SIZE + 1];
	unsigned int friend_id;
	/* it came as an invite (sent to this person), not from presence */
	int invited;
};

/* INVITE: kind 1 a game, 2 a party */
struct social_invite
{
	int kind;
	struct social_join_target target;
	unsigned char party_token[SOCIAL_TOKEN_SIZE];
	unsigned char party_size;
};
int social_invite_encode(unsigned int generation, unsigned int sequence, const struct social_invite *invite, unsigned char *plain, int capacity);
int social_invite_decode(const unsigned char *plain, int size, struct social_invite *invite);

/* PRESENCE (social_core.h's struct social_presence is its body); the signature is the sender's, over its key hash too */
struct social_presence;
int social_presence_encode(const struct social_identity *identity, unsigned int generation, unsigned int sequence, const struct social_presence *presence,
	unsigned char *plain, int capacity);
/* parse only; *signed_size and *signature say what social_presence_verify checks */
int social_presence_decode(const unsigned char *plain, int size, struct social_presence *presence, int *signed_size, const unsigned char **signature);
int social_presence_verify(const unsigned char *key, const unsigned char *plain, int signed_size, const unsigned char *signature);

#endif
