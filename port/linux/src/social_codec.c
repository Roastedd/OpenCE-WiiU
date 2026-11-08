/*
SOCIAL_CODEC.C

The bytes of the friends layer (social_codec.h).
*/

#include "p2p_internal.h"
#include "social_core.h"
#include "../game/text_safe.h"

#include <stdio.h>
#include <string.h>

/* ---------- bytes */

void social_writer_start(struct social_writer *writer, unsigned char *bytes, int capacity)
{
	writer->bytes = bytes;
	writer->size = 0;
	writer->capacity = capacity;
	writer->bad = 0;
}

void social_put8(struct social_writer *writer, unsigned int value)
{
	if (writer->size + 1 > writer->capacity)
	{
		writer->bad = 1;
		return;
	}
	writer->bytes[writer->size++] = (unsigned char)value;
}

void social_put16(struct social_writer *writer, unsigned int value)
{
	social_put8(writer, value >> 8);
	social_put8(writer, value);
}

void social_put32(struct social_writer *writer, unsigned int value)
{
	social_put8(writer, value >> 24);
	social_put8(writer, value >> 16);
	social_put8(writer, value >> 8);
	social_put8(writer, value);
}

void social_put_bytes(struct social_writer *writer, const void *bytes, int size)
{
	if (size < 0 || writer->size + size > writer->capacity)
	{
		writer->bad = 1;
		return;
	}
	memcpy(writer->bytes + writer->size, bytes, (size_t)size);
	writer->size += size;
}

void social_put_text(struct social_writer *writer, const char *text, int maximum)
{
	char clean[SOCIAL_CHAT_SIZE + 2];
	short length;

	if (maximum > SOCIAL_CHAT_SIZE)
		maximum = SOCIAL_CHAT_SIZE;
	length = text_safe_text(clean, (short)sizeof(clean), text ? text : "", (short)(text ? strlen(text) : 0), (short)maximum);
	social_put8(writer, (unsigned int)length);
	social_put_bytes(writer, clean, length);
}

void social_reader_start(struct social_reader *reader, const unsigned char *bytes, int size)
{
	reader->bytes = bytes;
	reader->size = size;
	reader->offset = 0;
	reader->bad = size < 0;
}

unsigned int social_get8(struct social_reader *reader)
{
	if (reader->bad || reader->offset + 1 > reader->size)
	{
		reader->bad = 1;
		return 0;
	}
	return reader->bytes[reader->offset++];
}

unsigned int social_get16(struct social_reader *reader)
{
	unsigned int high = social_get8(reader);

	return high << 8 | social_get8(reader);
}

unsigned int social_get32(struct social_reader *reader)
{
	unsigned int high = social_get16(reader);

	return high << 16 | social_get16(reader);
}

void social_get_bytes(struct social_reader *reader, void *bytes, int size)
{
	if (reader->bad || size < 0 || reader->offset + size > reader->size)
	{
		reader->bad = 1;
		memset(bytes, 0, (size_t)(size > 0 ? size : 0));
		return;
	}
	memcpy(bytes, reader->bytes + reader->offset, (size_t)size);
	reader->offset += size;
}

void social_get_text(struct social_reader *reader, char *text, int size, int maximum)
{
	char raw[256];
	unsigned int length = social_get8(reader);

	text[0] = 0;
	if (reader->bad)
		return;
	if ((int)length > maximum || length > sizeof(raw))
	{
		reader->bad = 1;
		return;
	}
	social_get_bytes(reader, raw, (int)length);
	if (!reader->bad)
		text_safe_text(text, (short)size, raw, (short)length, (short)maximum);
}

void social_hex(const unsigned char *bytes, int size, char *text)
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

/* ---------- the friend code */

/* Crockford's base 32: no I, L, O or U */
static const char code_digits[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

static int code_value(char character)
{
	int index;

	for (index = 0; index < 32; index++)
	{
		if (code_digits[index] == character)
			return index;
	}
	return -1;
}

/* multiplication by x in GF(2^5), the polynomial x^5 + x^2 + 1 */
static int multiply_by_two(int value)
{
	return ((value << 1) & 31) ^ (value & 16 ? 5 : 0);
}

int social_code_check_symbol(const unsigned char *symbols)
{
	int value = 0;
	int index;

	for (index = 0; index < SOCIAL_CODE_SYMBOLS - 1; index++)
		value = multiply_by_two(value ^ (symbols[index] & 31));
	return value;
}

/* five bits of the code's 60, most significant first */
static int code_symbol_of_bytes(const unsigned char *bytes, int index)
{
	int value = 0;
	int bit;

	for (bit = 0; bit < 5; bit++)
	{
		int position = index * 5 + bit;

		value = value << 1 | ((bytes[position >> 3] >> (7 - (position & 7))) & 1);
	}
	return value;
}

static void code_text(const unsigned char *code_bytes, char *text)
{
	unsigned char symbols[SOCIAL_CODE_SYMBOLS];
	int index;
	int length = 0;

	for (index = 0; index < SOCIAL_CODE_SYMBOLS - 1; index++)
		symbols[index] = (unsigned char)code_symbol_of_bytes(code_bytes, index);
	symbols[SOCIAL_CODE_SYMBOLS - 1] = (unsigned char)social_code_check_symbol(symbols);
	for (index = 0; index < SOCIAL_CODE_SYMBOLS; index++)
	{
		if (index == 4 || index == 8 || index == 12)
			text[length++] = '-';
		text[length++] = code_digits[symbols[index]];
	}
	text[length] = 0;
}

void social_code_of_key(const unsigned char *key, unsigned char *code_bytes, char *text)
{
	static const char prefix[] = "hceu-friend-code-1";
	unsigned char data[sizeof(prefix) - 1 + SOCIAL_KEY_SIZE];
	unsigned char digest[P2P_SHA256_SIZE];

	memcpy(data, prefix, sizeof(prefix) - 1);
	memcpy(data + sizeof(prefix) - 1, key, SOCIAL_KEY_SIZE);
	p2p_sha256(data, (int)sizeof(data), digest);
	memcpy(code_bytes, digest, SOCIAL_CODE_BYTES);
	code_bytes[7] &= 0xF0;
	if (text)
		code_text(code_bytes, text);
}

int social_code_parse(const char *text, unsigned char *code_bytes, char *clean_text)
{
	char normal[SOCIAL_CODE_SYMBOLS + 4];
	unsigned char symbols[SOCIAL_CODE_SYMBOLS];
	unsigned char bytes[SOCIAL_CODE_BYTES];
	int index;
	int value = 0;

	if (!text || text_safe_code_normalise(normal, (short)sizeof(normal), text) != SOCIAL_CODE_SYMBOLS)
		return SOCIAL_CODE_BAD_TEXT;
	for (index = 0; index < SOCIAL_CODE_SYMBOLS; index++)
	{
		int symbol = code_value(normal[index]);

		if (symbol < 0)
			return SOCIAL_CODE_BAD_TEXT;
		symbols[index] = (unsigned char)symbol;
		value = multiply_by_two(value ^ symbol);
	}
	/* (all thirteen through the same loop end at 0) */
	if (value != 0)
		return SOCIAL_CODE_BAD_CHECK;
	memset(bytes, 0, sizeof(bytes));
	for (index = 0; index < SOCIAL_CODE_SYMBOLS - 1; index++)
	{
		int bit;

		for (bit = 0; bit < 5; bit++)
		{
			int position = index * 5 + bit;

			if ((symbols[index] >> (4 - bit)) & 1)
				bytes[position >> 3] |= (unsigned char)(1 << (7 - (position & 7)));
		}
	}
	if (code_bytes)
		memcpy(code_bytes, bytes, sizeof(bytes));
	if (clean_text)
		code_text(bytes, clean_text);
	return SOCIAL_CODE_OK;
}

void social_identity_make(struct social_identity *identity, const unsigned char *seed)
{
	memset(identity, 0, sizeof(*identity));
	memcpy(identity->seed, seed, SOCIAL_SEED_SIZE);
	p2p_ed25519_public(seed, identity->key, identity->secret);
	p2p_sha256(identity->key, SOCIAL_KEY_SIZE, identity->hash);
	social_code_of_key(identity->key, identity->code_bytes, identity->code);
}

/* ---------- keys and topics */

static void hmac_label(const unsigned char *key, int key_size, const char *label, const void *data, int size, unsigned char *digest)
{
	unsigned char message[160];
	int label_size = (int)strlen(label);

	if (label_size + size > (int)sizeof(message))
	{
		memset(digest, 0, P2P_SHA256_SIZE);
		return;
	}
	memcpy(message, label, (size_t)label_size);
	if (size > 0)
		memcpy(message + label_size, data, (size_t)size);
	p2p_hmac_sha256(key, key_size, message, label_size + size, digest);
}

void social_hmac(const unsigned char *key, const char *label, const void *data, int size, unsigned char *digest)
{
	hmac_label(key, SOCIAL_KEY_SIZE, label, data, size, digest);
}

static void topic_for(const char *prefix, const unsigned char *secret, int secret_size, char *topic, unsigned char *seal)
{
	unsigned char digest[P2P_SHA256_SIZE];
	char text[2 * SOCIAL_HASH_SIZE + 1];

	hmac_label(secret, secret_size, "topic", NULL, 0, digest);
	social_hex(digest, SOCIAL_HASH_SIZE, text);
	snprintf(topic, SOCIAL_TOPIC_SIZE, "%s%s", prefix, text);
	hmac_label(secret, secret_size, "seal", NULL, 0, seal);
}

void social_inbox_for(const unsigned char *code_bytes, char *topic, unsigned char *seal)
{
	unsigned char secret[P2P_SHA256_SIZE];

	hmac_label(code_bytes, SOCIAL_CODE_BYTES, "hceu-f/inbox", NULL, 0, secret);
	topic_for("hceu/3/f/i/", secret, P2P_SHA256_SIZE, topic, seal);
}

void social_circle_for(const unsigned char *circle_key, char *topic, unsigned char *seal)
{
	topic_for("hceu/3/f/p/", circle_key, SOCIAL_KEY_SIZE, topic, seal);
}

void social_party_for(const unsigned char *token, char *topic, unsigned char *seal)
{
	topic_for("hceu/3/f/g/", token, SOCIAL_TOKEN_SIZE, topic, seal);
}

int social_pair_for(const struct social_identity *identity, const unsigned char *their_key, unsigned char *pair_key, unsigned char *tag_in,
	unsigned char *tag_out)
{
	unsigned char their_x[SOCIAL_KEY_SIZE];
	unsigned char shared[SOCIAL_KEY_SIZE];
	unsigned char data[2 * SOCIAL_KEY_SIZE];
	unsigned char hash[P2P_SHA256_SIZE];
	unsigned char digest[P2P_SHA256_SIZE];
	int order = memcmp(identity->key, their_key, SOCIAL_KEY_SIZE);

	if (!p2p_ed25519_to_x25519(their_key, their_x))
		return 0;
	p2p_x25519(shared, identity->secret, their_x);
	memcpy(data, order < 0 ? identity->key : their_key, SOCIAL_KEY_SIZE);
	memcpy(data + SOCIAL_KEY_SIZE, order < 0 ? their_key : identity->key, SOCIAL_KEY_SIZE);
	hmac_label(shared, SOCIAL_KEY_SIZE, "hceu-f/pair/1", data, (int)sizeof(data), pair_key);
	/* a message's tag names its sender: the first bytes of the hash of the sender's key follow "tag" */
	p2p_sha256(their_key, SOCIAL_KEY_SIZE, hash);
	hmac_label(pair_key, SOCIAL_KEY_SIZE, "tag", hash, SOCIAL_TAG_SIZE, digest);
	memcpy(tag_in, digest, SOCIAL_TAG_SIZE);
	p2p_sha256(identity->key, SOCIAL_KEY_SIZE, hash);
	hmac_label(pair_key, SOCIAL_KEY_SIZE, "tag", hash, SOCIAL_TAG_SIZE, digest);
	memcpy(tag_out, digest, SOCIAL_TAG_SIZE);
	return 1;
}

/* ---------- the sealed envelope */

static int header_size_for(int kind)
{
	return kind == SOCIAL_KIND_DIRECT ? 2 + SOCIAL_TAG_SIZE : 2;
}

int social_seal(const unsigned char *key, int kind, const unsigned char *tag, const char *topic, const unsigned char *plain, int plain_size,
	const unsigned char *nonce, unsigned char *wire, int capacity)
{
	unsigned char additional[2 + SOCIAL_TAG_SIZE + SOCIAL_TOPIC_SIZE];
	int header = header_size_for(kind);
	int topic_size = (int)strlen(topic);
	int total = header + SOCIAL_NONCE_SIZE + plain_size + SOCIAL_AEAD_TAG_SIZE;

	if (plain_size < 0 || plain_size > SOCIAL_MAX_PLAIN || total > capacity || topic_size >= SOCIAL_TOPIC_SIZE ||
		(kind == SOCIAL_KIND_DIRECT && !tag))
	{
		return -1;
	}
	wire[0] = (unsigned char)kind;
	wire[1] = SOCIAL_WIRE_VERSION;
	if (kind == SOCIAL_KIND_DIRECT)
		memcpy(wire + 2, tag, SOCIAL_TAG_SIZE);
	memcpy(additional, wire, (size_t)header);
	memcpy(additional + header, topic, (size_t)topic_size);
	memcpy(wire + header, nonce, SOCIAL_NONCE_SIZE);
	p2p_aead_seal(key, nonce, additional, header + topic_size, plain, plain_size, wire + header + SOCIAL_NONCE_SIZE);
	return total;
}

int social_wire_header(const unsigned char *wire, int wire_size, int *kind, const unsigned char **tag)
{
	int header;

	if (wire_size < 2 || wire[1] != SOCIAL_WIRE_VERSION)
		return 0;
	if (wire[0] != SOCIAL_KIND_REQUEST && wire[0] != SOCIAL_KIND_PRESENCE && wire[0] != SOCIAL_KIND_DIRECT && wire[0] != SOCIAL_KIND_PARTY)
		return 0;
	header = header_size_for(wire[0]);
	if (wire_size < header + SOCIAL_NONCE_SIZE + SOCIAL_AEAD_TAG_SIZE || wire_size > SOCIAL_MAX_WIRE)
		return 0;
	*kind = wire[0];
	*tag = wire[0] == SOCIAL_KIND_DIRECT ? wire + 2 : NULL;
	return 1;
}

int social_open(const unsigned char *key, const char *topic, const unsigned char *wire, int wire_size, unsigned char *plain, int capacity)
{
	unsigned char additional[2 + SOCIAL_TAG_SIZE + SOCIAL_TOPIC_SIZE];
	const unsigned char *tag;
	int kind;
	int header;
	int topic_size = (int)strlen(topic);
	int sealed_size;

	if (!social_wire_header(wire, wire_size, &kind, &tag) || topic_size >= SOCIAL_TOPIC_SIZE)
		return -1;
	header = header_size_for(kind);
	sealed_size = wire_size - header - SOCIAL_NONCE_SIZE;
	if (sealed_size - SOCIAL_AEAD_TAG_SIZE > capacity)
		return -1;
	memcpy(additional, wire, (size_t)header);
	memcpy(additional + header, topic, (size_t)topic_size);
	return p2p_aead_open(key, wire + header, additional, header + topic_size, wire + header + SOCIAL_NONCE_SIZE, sealed_size, plain);
}

/* ---------- the replay window */

int social_window_check(const struct social_window *window, unsigned int generation, unsigned int sequence)
{
	unsigned int difference;

	if (sequence == 0)
		return 0;
	if (!window->started || generation > window->generation)
		return 1;
	if (generation < window->generation)
		return 0;
	if (sequence > window->sequence)
		return 1;
	difference = window->sequence - sequence;
	if (difference == 0 || difference > 32)
		return 0;
	return !((window->bitmap >> (difference - 1)) & 1u);
}

void social_window_mark(struct social_window *window, unsigned int generation, unsigned int sequence)
{
	unsigned int difference;

	if (sequence == 0)
		return;
	if (!window->started || generation > window->generation)
	{
		window->started = 1;
		window->generation = generation;
		window->sequence = sequence;
		window->bitmap = 0;
		return;
	}
	if (generation < window->generation)
		return;
	if (sequence > window->sequence)
	{
		difference = sequence - window->sequence;
		if (difference > 32)
			window->bitmap = 0;
		else if (difference == 32)
			window->bitmap = 1u << 31;
		else
			window->bitmap = (window->bitmap << difference) | (1u << (difference - 1));
		window->sequence = sequence;
		return;
	}
	difference = window->sequence - sequence;
	if (difference >= 1 && difference <= 32)
		window->bitmap |= 1u << (difference - 1);
}

/* ---------- messages */

int social_inner_header(const unsigned char *plain, int size, struct social_inner *inner)
{
	struct social_reader reader;

	social_reader_start(&reader, plain, size);
	inner->type = (int)social_get8(&reader);
	inner->generation = social_get32(&reader);
	inner->sequence = social_get32(&reader);
	return !reader.bad;
}

static void put_inner(struct social_writer *writer, int type, unsigned int generation, unsigned int sequence)
{
	social_put8(writer, (unsigned int)type);
	social_put32(writer, generation);
	social_put32(writer, sequence);
}

/* label, extra and the message in one buffer for the signature */
static int signed_message(unsigned char *message, int capacity, const char *label, const unsigned char *extra, int extra_size, const unsigned char *plain, int size)
{
	int label_size = (int)strlen(label);

	if (label_size + extra_size + size > capacity)
		return -1;
	memcpy(message, label, (size_t)label_size);
	if (extra_size)
		memcpy(message + label_size, extra, (size_t)extra_size);
	memcpy(message + label_size + extra_size, plain, (size_t)size);
	return label_size + extra_size + size;
}

int social_verify(const char *label, const unsigned char *extra, int extra_size, const unsigned char *key, const unsigned char *plain, int size,
	const unsigned char *signature)
{
	unsigned char message[SOCIAL_MAX_PLAIN + 64];
	int total = signed_message(message, (int)sizeof(message), label, extra, extra_size, plain, size);

	return total >= 0 && p2p_ed25519_verify(key, message, total, signature);
}

void social_sign(const struct social_identity *identity, const char *label, const unsigned char *extra, int extra_size, const unsigned char *plain, int size,
	unsigned char *signature)
{
	unsigned char message[SOCIAL_MAX_PLAIN + 64];
	int total = signed_message(message, (int)sizeof(message), label, extra, extra_size, plain, size);

	if (total < 0)
	{
		memset(signature, 0, SOCIAL_SIGNATURE_SIZE);
		return;
	}
	p2p_ed25519_sign(identity->seed, identity->key, message, total, signature);
}

static void name_label(char *name)
{
	if (!name[0])
		strcpy(name, "?");
}

int social_add_encode(const struct social_identity *identity, unsigned int generation, unsigned int sequence, const char *name, int flags,
	const unsigned char *recipient_code, unsigned char *plain, int capacity)
{
	struct social_writer writer;
	unsigned char signature[SOCIAL_SIGNATURE_SIZE];

	social_writer_start(&writer, plain, capacity);
	put_inner(&writer, SOCIAL_TYPE_ADD, generation, sequence);
	social_put_bytes(&writer, identity->key, SOCIAL_KEY_SIZE);
	social_put_text(&writer, name, SOCIAL_NAME_SIZE);
	social_put8(&writer, (unsigned int)flags);
	if (writer.bad)
		return -1;
	social_sign(identity, "hceu-f/add", recipient_code, SOCIAL_CODE_BYTES, plain, writer.size, signature);
	social_put_bytes(&writer, signature, SOCIAL_SIGNATURE_SIZE);
	return writer.bad ? -1 : writer.size;
}

int social_add_decode(const unsigned char *plain, int size, struct social_add *add)
{
	struct social_reader reader;
	struct social_inner inner;

	memset(add, 0, sizeof(*add));
	if (!social_inner_header(plain, size, &inner) || inner.type != SOCIAL_TYPE_ADD)
		return 0;
	social_reader_start(&reader, plain, size);
	reader.offset = SOCIAL_INNER_HEADER;
	social_get_bytes(&reader, add->key, SOCIAL_KEY_SIZE);
	social_get_text(&reader, add->name, (int)sizeof(add->name), SOCIAL_NAME_SIZE);
	add->flags = (unsigned char)social_get8(&reader);
	add->signed_size = reader.offset;
	add->signature = plain + reader.offset;
	reader.offset += SOCIAL_SIGNATURE_SIZE;
	if (reader.offset > reader.size)
		reader.bad = 1;
	if (reader.bad || reader.offset != reader.size)
		return 0;
	name_label(add->name);
	return 1;
}

int social_add_verify(const struct social_add *add, const unsigned char *plain, const unsigned char *recipient_code)
{
	return social_verify("hceu-f/add", recipient_code, SOCIAL_CODE_BYTES, add->key, plain, add->signed_size, add->signature);
}

int social_hello_encode(unsigned int generation, unsigned int sequence, const struct social_hello *hello, unsigned char *plain, int capacity)
{
	struct social_writer writer;

	social_writer_start(&writer, plain, capacity);
	put_inner(&writer, SOCIAL_TYPE_HELLO, generation, sequence);
	social_put8(&writer, hello->circle_generation);
	social_put_bytes(&writer, hello->circle_key, SOCIAL_KEY_SIZE);
	social_put8(&writer, hello->have_generation);
	return writer.bad ? -1 : writer.size;
}

int social_hello_decode(const unsigned char *plain, int size, struct social_hello *hello)
{
	struct social_reader reader;
	struct social_inner inner;

	memset(hello, 0, sizeof(*hello));
	if (!social_inner_header(plain, size, &inner) || inner.type != SOCIAL_TYPE_HELLO)
		return 0;
	social_reader_start(&reader, plain, size);
	reader.offset = SOCIAL_INNER_HEADER;
	hello->circle_generation = (unsigned char)social_get8(&reader);
	social_get_bytes(&reader, hello->circle_key, SOCIAL_KEY_SIZE);
	hello->have_generation = (unsigned char)social_get8(&reader);
	return !reader.bad && reader.offset == reader.size && hello->circle_generation != 0;
}

int social_remove_encode(unsigned int generation, unsigned int sequence, int reason, unsigned char *plain, int capacity)
{
	struct social_writer writer;

	social_writer_start(&writer, plain, capacity);
	put_inner(&writer, SOCIAL_TYPE_REMOVE, generation, sequence);
	social_put8(&writer, (unsigned int)reason);
	return writer.bad ? -1 : writer.size;
}

int social_remove_decode(const unsigned char *plain, int size, int *reason)
{
	struct social_reader reader;
	struct social_inner inner;

	if (!social_inner_header(plain, size, &inner) || inner.type != SOCIAL_TYPE_REMOVE)
		return 0;
	social_reader_start(&reader, plain, size);
	reader.offset = SOCIAL_INNER_HEADER;
	*reason = (int)social_get8(&reader);
	return !reader.bad && reader.offset == reader.size && *reason <= 1;
}

int social_invite_encode(unsigned int generation, unsigned int sequence, const struct social_invite *invite, unsigned char *plain, int capacity)
{
	struct social_writer writer;

	social_writer_start(&writer, plain, capacity);
	put_inner(&writer, SOCIAL_TYPE_INVITE, generation, sequence);
	social_put8(&writer, (unsigned int)invite->kind);
	if (invite->kind == 1)
	{
		social_put_bytes(&writer, invite->target.host_hash, SOCIAL_HASH_SIZE);
		social_put_bytes(&writer, invite->target.token, SOCIAL_TOKEN_SIZE);
		social_put16(&writer, invite->target.network_version);
		social_put8(&writer, invite->target.players);
		social_put8(&writer, invite->target.maximum_players);
		social_put8(&writer, invite->target.engine_type);
		social_put_text(&writer, invite->target.map, SOCIAL_MAP_SIZE);
		social_put_text(&writer, invite->target.gametype, SOCIAL_GAMETYPE_SIZE);
	}
	else if (invite->kind == 2)
	{
		social_put_bytes(&writer, invite->party_token, SOCIAL_TOKEN_SIZE);
		social_put8(&writer, invite->party_size);
	}
	else
		return -1;
	return writer.bad ? -1 : writer.size;
}

int social_invite_decode(const unsigned char *plain, int size, struct social_invite *invite)
{
	struct social_reader reader;
	struct social_inner inner;

	memset(invite, 0, sizeof(*invite));
	if (!social_inner_header(plain, size, &inner) || inner.type != SOCIAL_TYPE_INVITE)
		return 0;
	social_reader_start(&reader, plain, size);
	reader.offset = SOCIAL_INNER_HEADER;
	invite->kind = (int)social_get8(&reader);
	if (invite->kind == 1)
	{
		social_get_bytes(&reader, invite->target.host_hash, SOCIAL_HASH_SIZE);
		social_get_bytes(&reader, invite->target.token, SOCIAL_TOKEN_SIZE);
		invite->target.network_version = (unsigned short)social_get16(&reader);
		invite->target.players = (unsigned char)social_get8(&reader);
		invite->target.maximum_players = (unsigned char)social_get8(&reader);
		invite->target.engine_type = (unsigned char)social_get8(&reader);
		social_get_text(&reader, invite->target.map, (int)sizeof(invite->target.map), SOCIAL_MAP_SIZE);
		social_get_text(&reader, invite->target.gametype, (int)sizeof(invite->target.gametype), SOCIAL_GAMETYPE_SIZE);
	}
	else if (invite->kind == 2)
	{
		social_get_bytes(&reader, invite->party_token, SOCIAL_TOKEN_SIZE);
		invite->party_size = (unsigned char)social_get8(&reader);
		if (invite->party_size > SOCIAL_MAX_PARTY_MEMBERS)
			return 0;
	}
	else
		return 0;
	return !reader.bad && reader.offset == reader.size;
}

/* ---------- presence */

int social_presence_encode(const struct social_identity *identity, unsigned int generation, unsigned int sequence, const struct social_presence *presence,
	unsigned char *plain, int capacity)
{
	struct social_writer writer;
	unsigned char signature[SOCIAL_SIGNATURE_SIZE];

	social_writer_start(&writer, plain, capacity);
	put_inner(&writer, SOCIAL_TYPE_PRESENCE, generation, sequence);
	social_put8(&writer, presence->circle_generation);
	social_put8(&writer, presence->flags);
	social_put8(&writer, presence->activity);
	social_put8(&writer, presence->platform);
	social_put16(&writer, presence->network_version);
	social_put_text(&writer, presence->name, SOCIAL_NAME_SIZE);
	if (presence->flags & SOCIAL_PRESENCE_DETAILS)
	{
		social_put_text(&writer, presence->map, SOCIAL_MAP_SIZE);
		social_put_text(&writer, presence->gametype, SOCIAL_GAMETYPE_SIZE);
	}
	social_put8(&writer, presence->players);
	social_put8(&writer, presence->maximum_players);
	social_put8(&writer, presence->engine_type);
	if (presence->flags & SOCIAL_PRESENCE_JOINABLE)
	{
		social_put_bytes(&writer, presence->invite_hash, SOCIAL_HASH_SIZE);
		social_put_bytes(&writer, presence->invite_token, SOCIAL_TOKEN_SIZE);
	}
	if (presence->flags & SOCIAL_PRESENCE_PARTY_OPEN)
		social_put_bytes(&writer, presence->party_token, SOCIAL_TOKEN_SIZE);
	if (writer.bad)
		return -1;
	social_sign(identity, "hceu-f/presence", identity->hash, SOCIAL_HASH_SIZE, plain, writer.size, signature);
	social_put_bytes(&writer, signature, SOCIAL_SIGNATURE_SIZE);
	return writer.bad ? -1 : writer.size;
}

int social_presence_decode(const unsigned char *plain, int size, struct social_presence *presence, int *signed_size, const unsigned char **signature)
{
	struct social_reader reader;
	struct social_inner inner;

	memset(presence, 0, sizeof(*presence));
	if (!social_inner_header(plain, size, &inner) || inner.type != SOCIAL_TYPE_PRESENCE)
		return 0;
	social_reader_start(&reader, plain, size);
	reader.offset = SOCIAL_INNER_HEADER;
	presence->circle_generation = (unsigned char)social_get8(&reader);
	presence->flags = (unsigned char)social_get8(&reader);
	presence->activity = (unsigned char)social_get8(&reader);
	presence->platform = (unsigned char)social_get8(&reader);
	presence->network_version = (unsigned short)social_get16(&reader);
	social_get_text(&reader, presence->name, (int)sizeof(presence->name), SOCIAL_NAME_SIZE);
	if (presence->flags & SOCIAL_PRESENCE_DETAILS)
	{
		social_get_text(&reader, presence->map, (int)sizeof(presence->map), SOCIAL_MAP_SIZE);
		social_get_text(&reader, presence->gametype, (int)sizeof(presence->gametype), SOCIAL_GAMETYPE_SIZE);
	}
	presence->players = (unsigned char)social_get8(&reader);
	presence->maximum_players = (unsigned char)social_get8(&reader);
	presence->engine_type = (unsigned char)social_get8(&reader);
	if (presence->flags & SOCIAL_PRESENCE_JOINABLE)
	{
		social_get_bytes(&reader, presence->invite_hash, SOCIAL_HASH_SIZE);
		social_get_bytes(&reader, presence->invite_token, SOCIAL_TOKEN_SIZE);
	}
	if (presence->flags & SOCIAL_PRESENCE_PARTY_OPEN)
		social_get_bytes(&reader, presence->party_token, SOCIAL_TOKEN_SIZE);
	*signed_size = reader.offset;
	*signature = plain + reader.offset;
	reader.offset += SOCIAL_SIGNATURE_SIZE;
	if (reader.offset > reader.size)
		reader.bad = 1;
	if (reader.bad || reader.offset != reader.size || presence->activity > SOCIAL_BUSY || presence->circle_generation == 0)
		return 0;
	name_label(presence->name);
	return 1;
}

int social_presence_verify(const unsigned char *key, const unsigned char *plain, int signed_size, const unsigned char *signature)
{
	unsigned char hash[P2P_SHA256_SIZE];

	p2p_sha256(key, SOCIAL_KEY_SIZE, hash);
	return social_verify("hceu-f/presence", hash, SOCIAL_HASH_SIZE, key, plain, signed_size, signature);
}
