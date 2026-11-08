/*
SOCIAL_STORE.C

friends.key and friends.txt (social_store.h).
*/

#include "../src/p2p_internal.h"
#include "social_store.h"
#include "text_safe.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The game's folder compiles fopen, remove and rename as the translators of Xbox paths (port/linux/include/stdio.h: "d:\\maps", "z:\\saved"), which take a host path such as
the card's folder for a path under the data root and open a file that is not there ("/vol/external01/..." becomes "<data root>/vol/external01/..."). The files here are named by
host path (config_folder), so these three are the C library's. */
#undef fopen
#undef remove
#undef rename

enum
{
	LINE_MAXIMUM = 300,
};

static const char name_file[] = "friends.txt";
static const char name_new[] = "friends.new";
static const char name_backup[] = "friends.bak";

/* ---------- the text */

static void append(char *text, int capacity, int *length, int *failed, const char *format, ...)
{
	va_list arguments;
	int written;

	if (*failed)
		return;
	va_start(arguments, format);
	written = vsnprintf(text + *length, (size_t)(capacity - *length), format, arguments);
	va_end(arguments);
	if (written < 0 || written >= capacity - *length)
		*failed = 1;
	else
		*length += written;
}

/* the check line's value: the first 8 bytes of the SHA-256 of the text before it */
static void check_of(const char *text, int size, char *hex)
{
	unsigned char digest[P2P_SHA256_SIZE];

	p2p_sha256(text, size, digest);
	social_hex(digest, 8, hex);
}

int social_store_format(const struct social_engine *engine, char *text, int capacity)
{
	int length = 0, failed = 0, index;
	char hex[2 * SOCIAL_KEY_SIZE + 1];
	char check[17];

	append(text, capacity, &length, &failed, "hceu-friends %d\n", SOCIAL_STORE_VERSION);
	append(text, capacity, &length, &failed, "boot_gen %u\n", engine->stored_generation);
	append(text, capacity, &length, &failed, "share_status %d\nshare_details %d\nrequests %d\n", engine->share_status, engine->details, engine->accept_requests);
	social_hex(engine->circle_key, SOCIAL_KEY_SIZE, hex);
	append(text, capacity, &length, &failed, "circle_gen %d\ncircle_key %s\n", engine->circle_generation, hex);
	for (index = 0; index < SOCIAL_MAX_FRIENDS; index++)
	{
		const struct social_friend *friend = &engine->friends[index];
		char key[2 * SOCIAL_KEY_SIZE + 1];

		if (!friend->used)
			continue;
		social_hex(friend->key, SOCIAL_KEY_SIZE, key);
		if (friend->has_circle)
			social_hex(friend->circle_key, SOCIAL_KEY_SIZE, hex);
		else
			strcpy(hex, "-");
		append(text, capacity, &length, &failed, "friend %s %d %s %d %u %u %08x %llu %s\n", key, friend->has_circle ? friend->circle_generation : 0, hex,
			friend->acknowledged ? 1 : 0, friend->window.generation, friend->window.sequence, friend->window.bitmap, friend->added,
			friend->nickname[0] ? friend->nickname : "-");
	}
	for (index = 0; index < SOCIAL_MAX_SENT; index++)
	{
		if (engine->sent[index].used)
			append(text, capacity, &length, &failed, "sent %s %llu\n", engine->sent[index].code, engine->sent[index].added);
	}
	for (index = 0; index < engine->ignored_count; index++)
	{
		social_hex(engine->ignored[index], SOCIAL_BLOCK_SIZE, hex);
		append(text, capacity, &length, &failed, "ignored %s\n", hex);
	}
	for (index = 0; index < engine->blocked_count; index++)
	{
		social_hex(engine->blocked[index], SOCIAL_BLOCK_SIZE, hex);
		append(text, capacity, &length, &failed, "blocked %s\n", hex);
	}
	if (failed)
		return -1;
	check_of(text, length, check);
	append(text, capacity, &length, &failed, "check %s\n", check);
	return failed ? -1 : length;
}

/* ---------- reading it */

static int hex_value(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return -1;
}

/* exactly 2 * size hex digits; 0 if it is anything else */
static int parse_hex(const char *text, unsigned char *bytes, int size)
{
	int index;

	if ((int)strlen(text) != 2 * size)
		return 0;
	for (index = 0; index < size; index++)
	{
		int high = hex_value(text[index * 2]), low = hex_value(text[index * 2 + 1]);

		if (high < 0 || low < 0)
			return 0;
		bytes[index] = (unsigned char)(high << 4 | low);
	}
	return 1;
}

/* a decimal number, nothing else, up to maximum */
static int parse_number(const char *text, unsigned long long maximum, unsigned long long *value)
{
	unsigned long long result = 0;
	int digits = 0;

	for (; *text; text++)
	{
		if (*text < '0' || *text > '9' || digits >= 20)
			return 0;
		if (result > (maximum - (unsigned)(*text - '0')) / 10)
			return 0;
		result = result * 10 + (unsigned)(*text - '0');
		digits++;
	}
	*value = result;
	return digits > 0;
}

/* the next word of the line, in token; the rest of the line from *line when rest is set */
static int next_token(const char **line, char *token, int size, int rest)
{
	int length = 0;

	while (**line == ' ')
		(*line)++;
	if (!**line)
		return 0;
	while (**line && (rest || **line != ' '))
	{
		if (length >= size - 1)
			return 0;
		token[length++] = *(*line)++;
	}
	token[length] = 0;
	return length > 0;
}

/* the lines of the text, each ended; 0 if one is too long */
static int parse_friend_line(struct social_engine *engine, const char *line)
{
	char token[LINE_MAXIMUM];
	unsigned char key[SOCIAL_KEY_SIZE], circle[SOCIAL_KEY_SIZE], window_bits[4];
	unsigned long long generation, circle_generation, acknowledged, sequence, added;
	int has_circle = 0;
	struct social_friend *friend;
	char nickname[SOCIAL_NAME_SIZE + 1];

	if (!next_token(&line, token, sizeof(token), 0) || !parse_hex(token, key, SOCIAL_KEY_SIZE))
		return 0;
	if (!next_token(&line, token, sizeof(token), 0) || !parse_number(token, 255, &circle_generation))
		return 0;
	if (!next_token(&line, token, sizeof(token), 0))
		return 0;
	if (strcmp(token, "-"))
	{
		if (!parse_hex(token, circle, SOCIAL_KEY_SIZE) || circle_generation < 1)
			return 0;
		has_circle = 1;
	}
	if (!next_token(&line, token, sizeof(token), 0) || !parse_number(token, 1, &acknowledged))
		return 0;
	if (!next_token(&line, token, sizeof(token), 0) || !parse_number(token, 0xFFFFFFFFULL, &generation))
		return 0;
	if (!next_token(&line, token, sizeof(token), 0) || !parse_number(token, 0xFFFFFFFFULL, &sequence))
		return 0;
	if (!next_token(&line, token, sizeof(token), 0) || !parse_hex(token, window_bits, 4))
		return 0;
	if (!next_token(&line, token, sizeof(token), 0) || !parse_number(token, 0xFFFFFFFFFFFFULL, &added))
		return 0;
	nickname[0] = 0;
	if (next_token(&line, token, sizeof(token), 1) && strcmp(token, "-"))
		text_safe_text(nickname, (short)sizeof(nickname), token, (short)strlen(token), SOCIAL_NAME_SIZE);
	friend = social_friend_add(engine, key, has_circle ? (int)circle_generation : 0, has_circle ? circle : NULL, (int)acknowledged);
	if (!friend)
		return 1;
	friend->added = added;
	strcpy(friend->nickname, nickname);
	if (generation > 0 && sequence > 0)
	{
		friend->window.started = 1;
		friend->window.generation = (unsigned int)generation;
		friend->window.sequence = (unsigned int)sequence;
		friend->window.bitmap = (unsigned int)window_bits[0] << 24 | (unsigned int)window_bits[1] << 16 | (unsigned int)window_bits[2] << 8 | window_bits[3];
	}
	return 1;
}

/* whether the text is a friends file this version can read, before anything is taken from it: 0 yes (*check_at is where the check line starts); 1 it is no friends file;
2 the check fails, or a line is too long, or there are too many; 3 a newer version wrote it */
static int validate(const char *text, int size, int *check_at)
{
	const char *cursor = text, *end = text + size;
	char line[LINE_MAXIMUM];
	unsigned long long number;
	char expected[17];
	int lines = 0, length = 0;

	if (size <= 0 || size >= SOCIAL_STORE_MAXIMUM || memchr(text, 0, (size_t)size))
		return 1;
	while (length < size && text[length] != '\n')
		length++;
	if (length >= (int)sizeof(line) || length >= size)
		return 1;
	memcpy(line, text, (size_t)length);
	line[length] = 0;
	if (strncmp(line, "hceu-friends ", 13) || !parse_number(line + 13, 1000, &number))
		return 1;
	if (number > SOCIAL_STORE_VERSION)
		return 3;
	if (number < 1)
		return 1;
	/* the check: the last line, over everything before it */
	{
		const char *last = end - 1;
		const char *start;

		if (*last != '\n')
			return 2;
		start = last;
		while (start > text && start[-1] != '\n')
			start--;
		if (end - start != 6 + 16 + 1 || strncmp(start, "check ", 6))
			return 2;
		check_of(text, (int)(start - text), expected);
		if (strncmp(start + 6, expected, 16))
			return 2;
		*check_at = (int)(start - text);
	}
	/* every line short enough, and not too many */
	while (cursor < text + *check_at)
	{
		const char *newline = memchr(cursor, '\n', (size_t)(text + *check_at - cursor));

		if (!newline || newline - cursor >= LINE_MAXIMUM || ++lines > 400)
			return 2;
		cursor = newline + 1;
	}
	return 0;
}

int social_store_parse(struct social_engine *engine, const char *text, int size)
{
	char line[LINE_MAXIMUM];
	char token[LINE_MAXIMUM];
	const char *cursor, *end;
	int check_at = 0, result;
	unsigned long long number;
	int friends = 0, sent = 0;

	if (!engine->has_identity)
		return 1;
	result = validate(text, size, &check_at);
	if (result == 3)
	{
		/* (left as it is: it is not ours to read, or to write over) */
		engine->store_locked = 1;
		return 3;
	}
	if (result)
		return result;
	cursor = text;
	end = text + check_at;
	while (cursor < end)
	{
		const char *newline = memchr(cursor, '\n', (size_t)(end - cursor));
		const char *rest;
		int length;

		length = (int)(newline - cursor);
		memcpy(line, cursor, (size_t)length);
		line[length] = 0;
		cursor = newline + 1;
		rest = line;
		if (!next_token(&rest, token, sizeof(token), 0))
			continue;
		if (!strcmp(token, "boot_gen") && next_token(&rest, token, sizeof(token), 0) && parse_number(token, 0xFFFFFFFFULL, &number))
		{
			if ((unsigned int)number > engine->stored_generation)
				engine->stored_generation = (unsigned int)number;
		}
		else if (!strcmp(token, "share_status") && next_token(&rest, token, sizeof(token), 0) && parse_number(token, 1, &number))
			engine->share_status = (int)number;
		else if (!strcmp(token, "share_details") && next_token(&rest, token, sizeof(token), 0) && parse_number(token, 2, &number))
			engine->details = (int)number;
		else if (!strcmp(token, "requests") && next_token(&rest, token, sizeof(token), 0) && parse_number(token, 1, &number))
			engine->accept_requests = (int)number;
		else if (!strcmp(token, "circle_gen") && next_token(&rest, token, sizeof(token), 0) && parse_number(token, 255, &number))
			engine->store_circle_generation = (int)number;
		else if (!strcmp(token, "circle_key") && next_token(&rest, token, sizeof(token), 0))
		{
			unsigned char key[SOCIAL_KEY_SIZE];

			if (parse_hex(token, key, SOCIAL_KEY_SIZE) && engine->store_circle_generation >= 1)
				social_set_circle(engine, engine->store_circle_generation, key);
		}
		else if (!strcmp(token, "friend") && friends < SOCIAL_MAX_FRIENDS)
		{
			if (parse_friend_line(engine, rest))
				friends++;
		}
		else if (!strcmp(token, "sent") && sent < SOCIAL_MAX_SENT && next_token(&rest, token, sizeof(token), 0))
		{
			unsigned char code_bytes[SOCIAL_CODE_BYTES];
			char clean[SOCIAL_CODE_TEXT_SIZE];
			int slot;

			if (social_code_parse(token, code_bytes, clean) == SOCIAL_CODE_OK && memcmp(code_bytes, engine->identity.code_bytes, SOCIAL_CODE_BYTES))
			{
				for (slot = 0; slot < SOCIAL_MAX_SENT && engine->sent[slot].used; slot++)
					;
				if (slot < SOCIAL_MAX_SENT)
				{
					memset(&engine->sent[slot], 0, sizeof(engine->sent[slot]));
					engine->sent[slot].used = 1;
					memcpy(engine->sent[slot].code_bytes, code_bytes, SOCIAL_CODE_BYTES);
					snprintf(engine->sent[slot].code, sizeof(engine->sent[slot].code), "%s", clean);
					engine->sent[slot].id = (unsigned int)code_bytes[0] << 24 | (unsigned int)code_bytes[1] << 16 | (unsigned int)code_bytes[2] << 8 | code_bytes[3];
					if (!engine->sent[slot].id)
						engine->sent[slot].id = 1;
					if (next_token(&rest, token, sizeof(token), 0) && parse_number(token, 0xFFFFFFFFFFFFULL, &number))
						engine->sent[slot].added = number;
					sent++;
				}
			}
		}
		else if (!strcmp(token, "ignored") && engine->ignored_count < SOCIAL_MAX_IGNORED && next_token(&rest, token, sizeof(token), 0))
		{
			if (parse_hex(token, engine->ignored[engine->ignored_count], SOCIAL_BLOCK_SIZE))
				engine->ignored_count++;
		}
		else if (!strcmp(token, "blocked") && engine->blocked_count < SOCIAL_MAX_BLOCKED && next_token(&rest, token, sizeof(token), 0))
		{
			if (parse_hex(token, engine->blocked[engine->blocked_count], SOCIAL_BLOCK_SIZE))
				engine->blocked_count++;
		}
		/* (a line it does not know is passed over: a newer version of the same format may have more) */
	}
	engine->ignored_next = engine->ignored_count % SOCIAL_MAX_IGNORED;
	engine->topics_dirty = 1;
	engine->version++;
	return 0;
}

/* ---------- writing it safely */

int social_store_save(const struct social_files *files, const char *text, int size)
{
	static char readback[SOCIAL_STORE_MAXIMUM];
	int read_size;
	int had_file;

	if (size <= 0 || size >= SOCIAL_STORE_MAXIMUM)
		return 1;
	if (files->write_file(files->user, name_new, text, size))
	{
		files->remove_file(files->user, name_new);
		return 2;
	}
	read_size = files->read_file(files->user, name_new, readback, (int)sizeof(readback));
	if (read_size != size || memcmp(readback, text, (size_t)size))
	{
		files->remove_file(files->user, name_new);
		return 3;
	}
	had_file = files->read_file(files->user, name_file, readback, (int)sizeof(readback)) >= 0;
	files->remove_file(files->user, name_backup);
	/* (never a rename over a name that exists: the card's file system is not known to do it) */
	if (had_file && files->rename_file(files->user, name_file, name_backup))
		return 4;
	if (files->rename_file(files->user, name_new, name_file))
	{
		if (had_file)
			files->rename_file(files->user, name_backup, name_file);
		return 5;
	}
	return 0;
}

int social_store_load(const struct social_files *files, struct social_engine *engine)
{
	static char text[SOCIAL_STORE_MAXIMUM];
	const char *names[3];
	int index, found = 0, locked = 0;

	names[0] = name_file;
	names[1] = name_new;
	names[2] = name_backup;
	for (index = 0; index < 3; index++)
	{
		int size = files->read_file(files->user, names[index], text, (int)sizeof(text) - 1);
		int result;

		if (size < 0)
			continue;
		found = 1;
		text[size] = 0;
		result = social_store_parse(engine, text, size);
		if (result == 3)
		{
			locked = 1;
			break;
		}
		if (result == 0)
			return 0;
	}
	return locked ? 3 : found ? 2 : 1;
}

/* ---------- when */

int social_store_due(struct social_engine *engine, int foreground)
{
	if (engine->store_locked || !foreground || !engine->has_identity)
		return 0;
	/* the generation chosen is saved at once: a message with a lower one must never be sent after a restart */
	if (engine->generation_changed)
		return 1;
	if (engine->store_dirty && social_elapsed(engine, engine->store_dirty_time, SOCIAL_STORE_DEBOUNCE) &&
		(!engine->store_saved || social_elapsed(engine, engine->store_saved_time, SOCIAL_STORE_INTERVAL)))
	{
		return 1;
	}
	if (engine->store_soft && social_elapsed(engine, engine->store_soft_time, SOCIAL_STORE_SOFT_INTERVAL) &&
		(!engine->store_saved || social_elapsed(engine, engine->store_saved_time, SOCIAL_STORE_SOFT_INTERVAL)))
	{
		return 1;
	}
	return 0;
}

int social_store_take(struct social_engine *engine, char *text, int capacity)
{
	int size;

	if (engine->store_locked)
		return 0;
	size = social_store_format(engine, text, capacity);
	if (size <= 0)
		return 0;
	engine->store_dirty = 0;
	engine->store_soft = 0;
	engine->generation_changed = 0;
	engine->store_saved = 1;
	engine->store_saved_time = social_now(engine);
	return size;
}

/* ---------- the identity's file */

int social_key_format(const unsigned char *seed, char *text, int capacity)
{
	char hex[2 * SOCIAL_SEED_SIZE + 1];
	int written;

	social_hex(seed, SOCIAL_SEED_SIZE, hex);
	written = snprintf(text, (size_t)capacity,
		"# The identity of this install in the friends layer. Keep it to yourself: whoever has this line is you, to your friends.\n"
		"# Deleting this file starts a new identity, and the friends list with it. It is not for bug reports (friends.txt is).\n"
		"# \n%s\n", hex);
	return written > 0 && written < capacity ? written : -1;
}

int social_key_parse(const char *text, int size, unsigned char *seed)
{
	const char *cursor = text, *end = text + size;

	while (cursor < end)
	{
		const char *newline = memchr(cursor, '\n', (size_t)(end - cursor));
		int length = (int)((newline ? newline : end) - cursor);
		char line[2 * SOCIAL_SEED_SIZE + 4];

		if (length && cursor[length - 1] == '\r')
			length--;
		if (length > 0 && cursor[0] != '#')
		{
			if (length >= (int)sizeof(line))
				return 0;
			memcpy(line, cursor, (size_t)length);
			line[length] = 0;
			return parse_hex(line, seed, SOCIAL_SEED_SIZE);
		}
		if (!newline)
			break;
		cursor = newline + 1;
	}
	return 0;
}

/* ---------- the real files */

static void path_for(const struct social_posix_folder *folder, const char *name, char *path, int size)
{
	int length = (int)strlen(folder->path);

	if (length && folder->path[length - 1] != '/' && folder->path[length - 1] != '\\')
		snprintf(path, (size_t)size, "%s/%s", folder->path, name);
	else
		snprintf(path, (size_t)size, "%s%s", folder->path, name);
}

static int posix_read(void *user, const char *name, char *buffer, int capacity)
{
	char path[640];
	FILE *file;
	int size;

	path_for(user, name, path, sizeof(path));
	file = fopen(path, "rb");
	if (!file)
		return -1;
	size = (int)fread(buffer, 1, (size_t)capacity, file);
	/* (a file that does not fit is none) */
	if (size == capacity || ferror(file))
		size = -1;
	fclose(file);
	return size;
}

static int posix_write(void *user, const char *name, const char *data, int size)
{
	char path[640];
	FILE *file;
	int failed;

	path_for(user, name, path, sizeof(path));
	file = fopen(path, "wb");
	if (!file)
		return 1;
	failed = fwrite(data, 1, (size_t)size, file) != (size_t)size;
	failed |= fflush(file) != 0;
	failed |= fclose(file) != 0;
	return failed;
}

static int posix_rename(void *user, const char *from, const char *to)
{
	char from_path[640], to_path[640];

	path_for(user, from, from_path, sizeof(from_path));
	path_for(user, to, to_path, sizeof(to_path));
	return rename(from_path, to_path);
}

static int posix_remove(void *user, const char *name)
{
	char path[640];

	path_for(user, name, path, sizeof(path));
	return remove(path);
}

void social_files_posix(struct social_files *files, struct social_posix_folder *folder, const char *path)
{
	snprintf(folder->path, sizeof(folder->path), "%s", path);
	files->user = folder;
	files->read_file = posix_read;
	files->write_file = posix_write;
	files->rename_file = posix_rename;
	files->remove_file = posix_remove;
}
