/*
MODSTORE.C

The mod store (modstore.h): a signed catalog of mods, and their download into the mods folder (port_mods.c).

What is trusted, and what is not. The catalog (a JSON file at mods.store_url) is trusted only when its signature
(the file at the same address with ".sig" added: the Ed25519 signature of the catalog's exact bytes, as 128 hexadecimal
digits) is the store key's (mods.store_key in config.toml, else the key the build was made with,
HALO_MODSTORE_PUBLIC_KEY). Nothing in it is looked at before the signature is good. Each mod of the catalog names
a manifest (a JSON file of the mod's files, each with its size and SHA-256) by its own size and SHA-256, and the
manifest names the files: so the signature covers every byte that is installed, and the server that holds the files
can do nothing but serve them as signed or serve nothing. A file that is not the size and the hash its manifest says
is deleted and the install stops.

What is installed is data the game reads and never runs: a mod.toml and Halo tag files, in the folders and under the
names the mods loader (port_mods.c, mods.c) looks for. A path that is not one of those, that leaves the mod's
folder (".." or an absolute path, a folder or a file that begins with a dot, a name the card cannot hold), a file
of a group the loader does not play, a tag file whose header the loader would not accept (mods_tag_file_header_valid:
group, version, header size and signature) and a mod.toml that is not TOML or names another mod's id make the whole
mod refused, and the reason is said in words. Limits: 256 files of 64 MB at most, 128 MB in all, a catalog of
512 KB with 128 mods.

An install is made in a folder of its own (.store-<id>.tmp, which the mods loader passes over, as it does every name
that begins with a dot), file by file, each in pieces of one MB asked for with a Range request and kept as it
arrives (<name>.part), so that a stop, a lost connection or a power cut leaves what was done to go on from. When
every file is whole and checked, the folder takes its place by renames (an update puts the old folder aside as
.store-<id>.old first, and gives it back if the new one cannot be put in place). A mod the store installed has a
.store file in its folder that lists its files; removing a mod deletes those files only. A folder in the mods
folder that has no .store is the player's own and is never touched, replaced or removed.

The work is done on a thread of its own, one job at a time (the catalog's fetch, an install, a removal).
*/

#include "platform.h"
#include "port_config.h"
#include "modstore.h"
#include "p2p_internal.h"
#include "posix.h"
#include "server_json.h"
#include "tomlc17.h"

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(HALO_ANDROID) && !defined(HALO_WEB)

#include "update.h"

/* (the guest's own C library, the Wii U's: no SDL; a test's: no SDL either) */
#if defined(HALO_ARM64_GUEST) || defined(HALO_WIIU) || defined(MODSTORE_STDIO)
#define MODSTORE_USES_STDIO 1
#ifndef _WIN32
#include <unistd.h>
#endif
#else
#include <SDL3/SDL.h>
#endif

#ifdef HALO_WIIU
/* (port/wiiu/src/wiiu_shell.c: core 0, below the game thread's priority, named) */
void wiiu_thread_background(const char *name);
#endif

/* the build's number (the workflow's run number), 0 when it has none: a mod that wants a later build is then not refused */
#ifndef HALO_BUILD_NUMBER
#define HALO_BUILD_NUMBER 0
#endif

/* ---------- constants */

enum
{
	/* the mods' format this build plays (port_mods.c's MODS_FORMAT) */
	STORE_MODS_FORMAT = 1,
	/* what the catalog and a manifest may be */
	MAXIMUM_CATALOG_BYTES = 512 * 1024,
	MAXIMUM_MANIFEST_BYTES = 128 * 1024,
	MAXIMUM_SIGNATURE_BYTES = 1024,
	MAXIMUM_CATALOG_MODS = 128,
	MAXIMUM_MOD_FILES = 256,
	/* the largest tag file the loader reads whole (mods.h's MAXIMUM_MOD_TAG_FILE_SIZE) */
	MAXIMUM_FILE_BYTES = 64 * 1024 * 1024,
	MAXIMUM_MOD_BYTES = 128 * 1024 * 1024,
	MAXIMUM_TOML_BYTES = 64 * 1024,
	/* the pieces a file is asked for in */
	PIECE_BYTES = 1024 * 1024,
	/* the card keeps this much free beyond a mod's own bytes */
	FREE_MARGIN_BYTES = 8 * 1024 * 1024,
	MAXIMUM_PATH = 1024,
	MAXIMUM_URL = 1024,
	MAXIMUM_FILE_PATH = 180,
	MAXIMUM_COMPONENT = 64,
	MAXIMUM_DEPTH = 16,
	MAXIMUM_ID = 40,
	MAXIMUM_MARKER_BYTES = 128 * 1024,
	MAXIMUM_TREE_DEPTH = 24,
	MAXIMUM_TREE_ENTRIES = 20000,
	/* the Wii U's job thread makes the TLS handshakes (Mbed TLS, wiiu_https.c): more stack than its default has */
	WIIU_STORE_STACK_BYTES = 256 * 1024,
	HASH_BYTES = 32,
};

#define MARKER_NAME ".store"
#define SERIAL_NAME ".store-serial"
#define PLAN_NAME ".plan"
#define MARKER_HEADER "opence-modstore 1"

/* ---------- structures */

struct file_entry
{
	char path[MAXIMUM_FILE_PATH + 1];
	unsigned long size;
	unsigned char hash[HASH_BYTES];
};

struct manifest
{
	long count;
	unsigned long total;
	struct file_entry *files;
};

struct entry
{
	char id[MAXIMUM_ID + 1];
	char name[48];
	char author[40];
	char version[20];
	char description[200];
	char kind[64];
	char licence[40];
	unsigned long size;
	unsigned long format;
	unsigned long minimum_build;
	char manifest_url[MAXIMUM_URL];
	unsigned long manifest_size;
	unsigned char manifest_hash[HASH_BYTES];
	int state;
	char installed[20];
	char why[96];
};

/* ---------- globals */

static struct
{
	pthread_mutex_t lock;
	int phase;
	unsigned long generation;
	char message[160];
	int job;
	long job_index;
	unsigned long done, total;
	int cancel;
	/* a job's thread is alive */
	int running;
	int changed;
	int opened;
	struct entry *entries;
	long count;
	char directory_url[MAXIMUM_URL];
} store = { .lock = PTHREAD_MUTEX_INITIALIZER };

/* ---------- a lock, and the state the screen reads */

static void bump(void)
{
	store.generation++;
}

static void say(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *format, ...)
{
	va_list arguments;

	pthread_mutex_lock(&store.lock);
	va_start(arguments, format);
	vsnprintf(store.message, sizeof(store.message), format, arguments);
	va_end(arguments);
	{
		char *letter;

		for (letter = store.message; *letter; letter++)
			*letter = (char)toupper((unsigned char)*letter);
	}
	bump();
	pthread_mutex_unlock(&store.lock);
}

static void set_phase(int phase)
{
	pthread_mutex_lock(&store.lock);
	store.phase = phase;
	bump();
	pthread_mutex_unlock(&store.lock);
}

static int cancelled(void)
{
	int value;

	pthread_mutex_lock(&store.lock);
	value = store.cancel;
	pthread_mutex_unlock(&store.lock);
	return value;
}

static void progress_set(unsigned long done, unsigned long total)
{
	pthread_mutex_lock(&store.lock);
	store.done = done;
	store.total = total;
	bump();
	pthread_mutex_unlock(&store.lock);
}

/* ---------- text */

static char lower_case(char character)
{
	return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

static int names_equal_case_n(const char *first, const char *second, size_t count)
{
	for (; count; count--, first++, second++)
	{
		if (lower_case(*first) != lower_case(*second))
			return 0;
	}
	return 1;
}

static int names_equal_case(const char *first, const char *second)
{
	for (; *first && *second; first++, second++)
	{
		if (lower_case(*first) != lower_case(*second))
			return 0;
	}
	return *first == *second;
}

/* a text kept to what the menus can show (ASCII 0x20 to 0x7E, no "|": text_safe.h), cut to the size of destination */
static void text_clean(char *destination, size_t size, const char *source)
{
	size_t length = 0;

	if (!size)
		return;
	for (; *source && length + 1 < size; source++)
	{
		unsigned char character = (unsigned char)*source;

		destination[length++] = character < 0x20 || character > 0x7E ? ' ' : character == '|' ? '/' : (char)character;
	}
	destination[length] = 0;
	while (length && destination[length - 1] == ' ')
		destination[--length] = 0;
}

static int hex_digit(int character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;
	return -1;
}

/* count bytes from 2 * count hexadecimal digits, and then (trailing 0) nothing, (1) only white space or (2) anything; 1 if so */
static int hex_parse(const char *text, unsigned char *bytes, int count, int trailing)
{
	int index;

	for (index = 0; index < count; index++)
	{
		int high = hex_digit((unsigned char)text[index * 2]), low = high < 0 ? -1 : hex_digit((unsigned char)text[index * 2 + 1]);

		if (high < 0 || low < 0)
			return 0;
		bytes[index] = (unsigned char)(high * 16 + low);
	}
	text += count * 2;
	if (trailing == 2)
		return 1;
	if (!trailing)
		return *text == 0;
	while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
		text++;
	return *text == 0;
}

static void hex_write(const unsigned char *bytes, int count, char *text)
{
	static const char digits[] = "0123456789abcdef";
	int index;

	for (index = 0; index < count; index++)
	{
		text[index * 2] = digits[bytes[index] >> 4];
		text[index * 2 + 1] = digits[bytes[index] & 15];
	}
	text[count * 2] = 0;
}

/* ---------- SHA-256, a piece at a time (p2p_crypto.c's is one call on all the bytes) */

struct sha256
{
	uint32_t state[8];
	uint64_t length;
	unsigned char block[64];
	int used;
};

static const uint32_t sha256_constants[64] =
{
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROTATE_RIGHT(value, count) (((value) >> (count)) | ((value) << (32 - (count))))

static void sha256_block(struct sha256 *context, const unsigned char *block)
{
	uint32_t words[64], a, b, c, d, e, f, g, h;
	int index;

	for (index = 0; index < 16; index++)
		words[index] = (uint32_t)block[index * 4] << 24 | (uint32_t)block[index * 4 + 1] << 16 |
			(uint32_t)block[index * 4 + 2] << 8 | block[index * 4 + 3];
	for (index = 16; index < 64; index++)
	{
		uint32_t s0 = ROTATE_RIGHT(words[index - 15], 7) ^ ROTATE_RIGHT(words[index - 15], 18) ^ (words[index - 15] >> 3);
		uint32_t s1 = ROTATE_RIGHT(words[index - 2], 17) ^ ROTATE_RIGHT(words[index - 2], 19) ^ (words[index - 2] >> 10);

		words[index] = words[index - 16] + s0 + words[index - 7] + s1;
	}
	a = context->state[0]; b = context->state[1]; c = context->state[2]; d = context->state[3];
	e = context->state[4]; f = context->state[5]; g = context->state[6]; h = context->state[7];
	for (index = 0; index < 64; index++)
	{
		uint32_t s1 = ROTATE_RIGHT(e, 6) ^ ROTATE_RIGHT(e, 11) ^ ROTATE_RIGHT(e, 25);
		uint32_t choice = (e & f) ^ (~e & g);
		uint32_t first = h + s1 + choice + sha256_constants[index] + words[index];
		uint32_t s0 = ROTATE_RIGHT(a, 2) ^ ROTATE_RIGHT(a, 13) ^ ROTATE_RIGHT(a, 22);
		uint32_t majority = (a & b) ^ (a & c) ^ (b & c);

		h = g; g = f; f = e; e = d + first;
		d = c; c = b; b = a; a = first + s0 + majority;
	}
	context->state[0] += a; context->state[1] += b; context->state[2] += c; context->state[3] += d;
	context->state[4] += e; context->state[5] += f; context->state[6] += g; context->state[7] += h;
}

static void sha256_begin(struct sha256 *context)
{
	static const uint32_t initial[8] =
	{
		0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
	};

	memcpy(context->state, initial, sizeof(initial));
	context->length = 0;
	context->used = 0;
}

static void sha256_add(struct sha256 *context, const void *data, size_t size)
{
	const unsigned char *bytes = data;

	context->length += size;
	while (size)
	{
		size_t count = (size_t)(64 - context->used) < size ? (size_t)(64 - context->used) : size;

		memcpy(context->block + context->used, bytes, count);
		context->used += (int)count;
		bytes += count;
		size -= count;
		if (context->used == 64)
		{
			sha256_block(context, context->block);
			context->used = 0;
		}
	}
}

static void sha256_end(struct sha256 *context, unsigned char *digest)
{
	uint64_t bits = context->length * 8;
	unsigned char length[8];
	int index;

	for (index = 0; index < 8; index++)
		length[index] = (unsigned char)(bits >> (56 - index * 8));
	sha256_add(context, "\x80", 1);
	while (context->used != 56)
		sha256_add(context, "", 1);
	sha256_add(context, length, 8);
	for (index = 0; index < 8; index++)
	{
		digest[index * 4] = (unsigned char)(context->state[index] >> 24);
		digest[index * 4 + 1] = (unsigned char)(context->state[index] >> 16);
		digest[index * 4 + 2] = (unsigned char)(context->state[index] >> 8);
		digest[index * 4 + 3] = (unsigned char)context->state[index];
	}
}

static void sha256_all(const void *data, size_t size, unsigned char *digest)
{
	struct sha256 context;

	sha256_begin(&context);
	sha256_add(&context, data, size);
	sha256_end(&context, digest);
}

/* ---------- the files (stdio on the Wii U and in the test, SDL's elsewhere: the data root may hold any characters) */

static void *file_open(const char *path, const char *mode)
{
#ifdef MODSTORE_USES_STDIO
	return fopen(path, mode);
#else
	return SDL_IOFromFile(path, mode);
#endif
}

static size_t file_read(void *file, void *buffer, size_t size)
{
#ifdef MODSTORE_USES_STDIO
	return fread(buffer, 1, size, (FILE *)file);
#else
	return SDL_ReadIO((SDL_IOStream *)file, buffer, size);
#endif
}

static int file_write(void *file, const void *buffer, size_t size)
{
#ifdef MODSTORE_USES_STDIO
	return fwrite(buffer, 1, size, (FILE *)file) == size;
#else
	return SDL_WriteIO((SDL_IOStream *)file, buffer, size) == size;
#endif
}

/* 1 when everything written is on its way to the card */
static int file_close(void *file)
{
#ifdef MODSTORE_USES_STDIO
	return fclose((FILE *)file) == 0;
#else
	return SDL_CloseIO((SDL_IOStream *)file);
#endif
}

static int file_rename(const char *from, const char *to)
{
#ifdef MODSTORE_USES_STDIO
	return rename(from, to) == 0;
#else
	return SDL_RenamePath(from, to);
#endif
}

/* a file, or an empty folder; 1 if it is gone now */
static int file_remove(const char *path)
{
	struct posix_file_information information;

#ifdef MODSTORE_USES_STDIO
	if (remove(path) == 0)
		return 1;
#ifndef _WIN32
	/* (remove() takes a folder on most systems; for the rest) */
	if (rmdir(path) == 0)
		return 1;
#endif
#else
	if (SDL_RemovePath(path))
		return 1;
#endif
	return posix_stat(path, &information) != 0;
}

/* what is at path: 0 nothing, 1 a file, 2 a folder; *size is a file's */
static int path_kind(const char *path, unsigned long *size)
{
	struct posix_file_information information;

	if (size)
		*size = 0;
	if (posix_stat(path, &information) != 0)
		return 0;
	if (information.flags & _posix_file_is_directory)
		return 2;
	if (size)
		*size = information.size_high ? 0xFFFFFFFFul : information.size_low;
	return 1;
}

/* the file whole into memory (a NUL after it), at most maximum bytes; NULL if there is none or it is larger */
static char *file_whole(const char *path, size_t maximum, size_t *size)
{
	unsigned long length;
	char *text;
	void *file;
	size_t read;

	if (path_kind(path, &length) != 1 || length > maximum)
		return NULL;
	text = malloc((size_t)length + 1);
	file = text ? file_open(path, "rb") : NULL;
	if (!file)
	{
		free(text);
		return NULL;
	}
	read = file_read(file, text, (size_t)length);
	file_close(file);
	if (read != length)
	{
		free(text);
		return NULL;
	}
	text[length] = 0;
	if (size)
		*size = (size_t)length;
	return text;
}

/* a file written whole, over any other, by way of a file beside it */
static int file_put(const char *path, const void *data, size_t size)
{
	char temporary[MAXIMUM_PATH];
	void *file;
	int written;

	if (snprintf(temporary, sizeof(temporary), "%s.new", path) >= (int)sizeof(temporary))
		return 0;
	file = file_open(temporary, "wb");
	if (!file)
		return 0;
	written = file_write(file, data, size);
	written = file_close(file) && written;
	if (written)
	{
		file_remove(path);
		written = file_rename(temporary, path);
	}
	if (!written)
		file_remove(temporary);
	return written;
}

/* makes the folders of `relative` (a validated path's, "/" between) under `root`, and the last too if `last` */
static int make_folders(const char *root, const char *relative, int last)
{
	char path[MAXIMUM_PATH];
	size_t length = strlen(root), at = 0;

	if (length >= sizeof(path))
		return 0;
	memcpy(path, root, length + 1);
	while (relative[at])
	{
		size_t end = at;
		int kind;

		while (relative[end] && relative[end] != '/')
			end++;
		if (!relative[end] && !last)
			break;
		if (length + 1 + (end - at) >= sizeof(path))
			return 0;
		path[length++] = '/';
		memcpy(path + length, relative + at, end - at);
		length += end - at;
		path[length] = 0;
		kind = path_kind(path, NULL);
		if (kind == 1 || (!kind && posix_make_directory(path) != 0 && path_kind(path, NULL) != 2))
			return 0;
		at = relative[end] ? end + 1 : end;
	}
	return 1;
}

/* a folder and everything in it deleted (these are folders the store made: no link is followed out of them) */
static int tree_delete_inner(const char *path, int depth, long *entries)
{
	void *directory;
	char name[256];
	int clean = 1;

	if (depth > MAXIMUM_TREE_DEPTH)
		return 0;
	directory = posix_directory_open(path);
	if (directory)
	{
		while (posix_directory_next(directory, name, sizeof(name)))
		{
			char child[MAXIMUM_PATH];
			int kind;

			if (++*entries > MAXIMUM_TREE_ENTRIES || snprintf(child, sizeof(child), "%s/%s", path, name) >= (int)sizeof(child))
			{
				clean = 0;
				break;
			}
			kind = path_kind(child, NULL);
			if (kind == 2)
				clean = tree_delete_inner(child, depth + 1, entries) && clean;
			else if (kind)
				clean = file_remove(child) && clean;
		}
		posix_directory_close(directory);
	}
	return file_remove(path) && clean;
}

static int tree_delete(const char *path)
{
	long entries = 0;

	return tree_delete_inner(path, 0, &entries);
}

/* ---------- paths: what a manifest may name */

/* the extensions of the tag files the loader plays, and the group and version a file of each must have (mods.h's
mods_tag_file_header_valid; the sounds', bitmaps', strings' and models' modules) */
static const struct
{
	const char *extension;
	uint32_t group;
	int version;
} tag_groups[] =
{
	{ ".sound", 'snd!', 4 },
	{ ".bitmap", 'bitm', 7 },
	{ ".unicode_string_list", 'ustr', 1 },
	{ ".string_list", 'str#', 1 },
	{ ".model", 'mode', 4 },
	{ ".gbxmodel", 'mod2', 5 },
};

static int tag_group_find(const char *path)
{
	size_t length = strlen(path), index;

	for (index = 0; index < sizeof(tag_groups) / sizeof(*tag_groups); index++)
	{
		size_t extension = strlen(tag_groups[index].extension);

		if (length > extension && names_equal_case(path + length - extension, tag_groups[index].extension))
			return (int)index;
	}
	return -1;
}

/* whether the first bytes of a tag file are a header the loader accepts, for the group the name's extension says
(mods.c's mods_tag_file_header_valid) */
static int tag_header_valid(const unsigned char *file, int group)
{
	uint32_t found = (uint32_t)file[0x24] << 24 | (uint32_t)file[0x25] << 16 | (uint32_t)file[0x26] << 8 | file[0x27];
	uint32_t header_size = (uint32_t)file[0x2C] << 24 | (uint32_t)file[0x2D] << 16 | (uint32_t)file[0x2E] << 8 | file[0x2F];
	int version = (int)(short)((unsigned)file[0x38] << 8 | file[0x39]);

	return found == tag_groups[group].group && !memcmp(file + 0x3C, "blam", 4) && header_size == 0x40 &&
		version == tag_groups[group].version;
}

/* (ASCII only, whatever the locale: the C library's isalnum would follow it) */
static int ascii_alnum(unsigned char character)
{
	return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
}

static int component_character_ok(unsigned char character)
{
	return ascii_alnum(character) || (character && strchr(" _-.(),'+!@&=~", character) != NULL);
}

/* whether a file of a mod may be named so: at most 180 characters of at most 64 in components of letters, digits and
a few signs, separated by "/", no component empty, "." or "..", beginning with a dot or a space, or ending in a dot or
a space; mod.toml, a licence or a readme at the top, else a tag file of a group the loader plays in tags/ */
static int path_ok(const char *path)
{
	size_t length = strlen(path), at = 0;
	int depth = 0;

	if (!length || length > MAXIMUM_FILE_PATH)
		return 0;
	while (path[at])
	{
		size_t end = at;

		while (path[end] && path[end] != '/')
		{
			if (!component_character_ok((unsigned char)path[end]))
				return 0;
			end++;
		}
		if (end == at || end - at > MAXIMUM_COMPONENT || path[at] == '.' || path[at] == ' ' || path[end - 1] == '.' ||
			path[end - 1] == ' ' || ++depth > MAXIMUM_DEPTH)
		{
			return 0;
		}
		at = path[end] ? end + 1 : end;
		if (path[end] && !path[at])
			return 0;
	}
	if (!strchr(path, '/'))
	{
		static const char *const top[] = { "mod.toml", "LICENSE", "LICENSE.txt", "LICENSE.md", "README.md", "README.txt" };
		size_t index;

		for (index = 0; index < sizeof(top) / sizeof(*top); index++)
		{
			if (!strcmp(path, top[index]))
				return 1;
		}
		return 0;
	}
	return !strncmp(path, "tags/", 5) && depth >= 3 && tag_group_find(path) >= 0;
}

static int path_inside_case(const char *folder, const char *path)
{
	size_t length = strlen(folder);

	return strlen(path) > length && path[length] == '/' && names_equal_case_n(folder, path, length);
}

/* an id: lower case letters, digits, "_" and "-", at most 40, beginning with a letter or digit */
static int id_ok(const char *id)
{
	size_t index, length = strlen(id);

	if (!length || length > MAXIMUM_ID || !(isdigit((unsigned char)id[0]) || (id[0] >= 'a' && id[0] <= 'z')))
		return 0;
	for (index = 0; index < length; index++)
	{
		if (!((id[index] >= 'a' && id[index] <= 'z') || isdigit((unsigned char)id[index]) || id[index] == '_' || id[index] == '-'))
			return 0;
	}
	return 1;
}

static int version_ok(const char *version)
{
	size_t index, length = strlen(version);

	if (!length || length > 16)
		return 0;
	for (index = 0; index < length; index++)
	{
		if (!(ascii_alnum((unsigned char)version[index]) || strchr("._+-", version[index])))
			return 0;
	}
	return 1;
}

/* ---------- the addresses */

/* whether a store's address is one this machine may use: https (or http for this machine, a test's), without a
query, a fragment or characters a request line would make trouble of */
static int url_ok(const char *url)
{
	size_t index, length = strlen(url);

	if (length < 12 || length > 400)
		return 0;
	if (strncmp(url, "https://", 8) && strncmp(url, "http://127.0.0.1", 16) && strncmp(url, "http://localhost", 16))
		return 0;
	for (index = 0; index < length; index++)
	{
		unsigned char character = (unsigned char)url[index];

		if (character <= 0x20 || character >= 0x7F || strchr("?#\\\"<>|^`{}", character))
			return 0;
	}
	return 1;
}

/* the address with its last segment cut off, ending in "/" */
static int url_directory(const char *url, char *directory, size_t size)
{
	const char *slash = strrchr(url + 8, '/');
	size_t length;

	if (!slash)
		return 0;
	length = (size_t)(slash - url) + 1;
	if (length >= size)
		return 0;
	memcpy(directory, url, length);
	directory[length] = 0;
	return 1;
}

/* a relative address as the catalog writes it (letters, digits, "._~-/", no ".." and no leading "/"), or an absolute
https:// one, made whole against the catalog's directory */
static int url_resolve(const char *directory, const char *address, char *url, size_t size)
{
	size_t index, length = strlen(address);

	if (!length || length > 300)
		return 0;
	if (!strncmp(address, "https://", 8) || !strncmp(address, "http://", 7))
	{
		if (!url_ok(address) || strlen(address) >= size)
			return 0;
		strcpy(url, address);
		return 1;
	}
	if (address[0] == '/' || strstr(address, "..") || strstr(address, "//"))
		return 0;
	for (index = 0; index < length; index++)
	{
		if (!(ascii_alnum((unsigned char)address[index]) || strchr("._~-/", address[index])))
			return 0;
	}
	return snprintf(url, size, "%s%s", directory, address) < (int)size;
}

/* a mod's file's address: the manifest's directory and the file's path, with what a request line may not have
(a space) as %XX */
static int url_file(const char *manifest_url, const char *path, char *url, size_t size)
{
	char directory[MAXIMUM_URL];
	size_t length;

	if (!url_directory(manifest_url, directory, sizeof(directory)))
		return 0;
	length = strlen(directory);
	if (length >= size)
		return 0;
	memcpy(url, directory, length);
	for (; *path; path++)
	{
		unsigned char character = (unsigned char)*path;

		if (ascii_alnum(character) || strchr("-._~/", character))
		{
			if (length + 2 > size)
				return 0;
			url[length++] = (char)character;
		}
		else
		{
			if (length + 4 > size)
				return 0;
			length += (size_t)snprintf(url + length, size - length, "%%%02X", character);
		}
	}
	url[length] = 0;
	return 1;
}

/* ---------- the folders */

static void mods_directory(char *path, size_t size)
{
	snprintf(path, size, "%s/mods", platform_data_root());
}

static int mod_folder(const char *id, char *path, size_t size)
{
	char root[MAXIMUM_PATH];

	mods_directory(root, sizeof(root));
	return id_ok(id) && snprintf(path, size, "%s/%s", root, id) < (int)size;
}

static int store_folder(const char *id, const char *suffix, char *path, size_t size)
{
	char root[MAXIMUM_PATH];

	mods_directory(root, sizeof(root));
	return id_ok(id) && snprintf(path, size, "%s/.store-%s.%s", root, id, suffix) < (int)size;
}

/* ---------- the marker a store install leaves in its folder */

struct marker
{
	char id[MAXIMUM_ID + 1];
	char version[20];
	char manifest[HASH_BYTES * 2 + 1];
	struct manifest files;
};

static void manifest_free(struct manifest *manifest)
{
	free(manifest->files);
	memset(manifest, 0, sizeof(*manifest));
}

/* the next line of a text (cut at its end, the text moved on); NULL at the end */
static char *line_next(char **text)
{
	char *line = *text, *end;

	if (!*line)
		return NULL;
	end = strchr(line, '\n');
	if (end)
	{
		*end = 0;
		*text = end + 1;
	}
	else
	{
		*text = line + strlen(line);
	}
	end = line + strlen(line);
	if (end > line && end[-1] == '\r')
		end[-1] = 0;
	return line;
}

/* the marker of a mod's folder: 0 if there is none (or it is not one: the folder is the player's), else 1; with
`whole` the files it lists are read too (each checked with path_ok: the file is on a card anyone can write) */
static int marker_read(const char *folder, struct marker *marker, int whole)
{
	char path[MAXIMUM_PATH];
	char *text, *cursor, *line;
	size_t size;

	memset(marker, 0, sizeof(*marker));
	if (snprintf(path, sizeof(path), "%s/" MARKER_NAME, folder) >= (int)sizeof(path))
		return 0;
	text = file_whole(path, MAXIMUM_MARKER_BYTES, &size);
	if (!text)
		return 0;
	cursor = text;
	line = line_next(&cursor);
	if (!line || strcmp(line, MARKER_HEADER))
	{
		free(text);
		return 0;
	}
	if (whole)
	{
		marker->files.files = calloc(MAXIMUM_MOD_FILES, sizeof(*marker->files.files));
		if (!marker->files.files)
		{
			free(text);
			return 0;
		}
	}
	while ((line = line_next(&cursor)) != NULL)
	{
		if (!strncmp(line, "id ", 3))
		{
			if (strlen(line + 3) <= MAXIMUM_ID)
				strcpy(marker->id, line + 3);
		}
		else if (!strncmp(line, "version ", 8))
		{
			if (strlen(line + 8) < sizeof(marker->version))
				strcpy(marker->version, line + 8);
		}
		else if (!strncmp(line, "manifest ", 9))
		{
			if (strlen(line + 9) == HASH_BYTES * 2)
				strcpy(marker->manifest, line + 9);
		}
		else if (whole && !strncmp(line, "file ", 5))
		{
			struct file_entry *file;
			char *end;
			unsigned long size_value = strtoul(line + 5, &end, 10);

			if (marker->files.count >= MAXIMUM_MOD_FILES || *end != ' ' || strlen(end + 1) < HASH_BYTES * 2 + 2)
				continue;
			file = &marker->files.files[marker->files.count];
			file->size = size_value;
			if (!hex_parse(end + 1, file->hash, HASH_BYTES, 2) || end[1 + HASH_BYTES * 2] != ' ' ||
				strlen(end + 2 + HASH_BYTES * 2) > MAXIMUM_FILE_PATH || !path_ok(end + 2 + HASH_BYTES * 2))
			{
				continue;
			}
			strcpy(file->path, end + 2 + HASH_BYTES * 2);
			marker->files.count++;
		}
	}
	free(text);
	if (!marker->id[0] || !marker->version[0])
	{
		manifest_free(&marker->files);
		return 0;
	}
	return 1;
}

/* ---------- the catalog */

static int json_text(struct server_json_document const *document, int object, const char *key, char *text, size_t size)
{
	char raw[512];
	int token = server_json_member(document, object, key);

	if (token < 0 || !server_json_get_string(document, token, raw, sizeof(raw)))
		return 0;
	text_clean(text, size, raw);
	return 1;
}

/* a whole number of the document in 0 .. maximum */
static int json_count(struct server_json_document const *document, int object, const char *key, unsigned long maximum,
	unsigned long *value)
{
	double number;
	int token = server_json_member(document, object, key);

	if (token < 0 || !server_json_get_number(document, token, &number) || number < 0 || number > (double)maximum ||
		number != (double)(unsigned long)number)
	{
		return 0;
	}
	*value = (unsigned long)number;
	return 1;
}

static int json_hash(struct server_json_document const *document, int object, const char *key, unsigned char *hash)
{
	char text[96];
	int token = server_json_member(document, object, key);

	return token >= 0 && server_json_get_string(document, token, text, sizeof(text)) && strlen(text) == HASH_BYTES * 2 &&
		hex_parse(text, hash, HASH_BYTES, 0);
}

/* one mod of the catalog's list into `entry`: 1, else 0 with the reason (in words) in why */
static int entry_parse(struct server_json_document const *document, int object, const char *directory, struct entry *entry,
	char *why, size_t why_size)
{
	int manifest, kinds, index;
	char address[320];
	unsigned long value;

	memset(entry, 0, sizeof(*entry));
	if (!json_text(document, object, "id", entry->id, sizeof(entry->id)) || !id_ok(entry->id))
	{
		snprintf(why, why_size, "A MOD HAS NO VALID ID");
		return 0;
	}
	if (!json_text(document, object, "name", entry->name, sizeof(entry->name)) || !entry->name[0])
		text_clean(entry->name, sizeof(entry->name), entry->id);
	json_text(document, object, "author", entry->author, sizeof(entry->author));
	json_text(document, object, "description", entry->description, sizeof(entry->description));
	if (!json_text(document, object, "version", entry->version, sizeof(entry->version)) || !version_ok(entry->version))
	{
		snprintf(why, why_size, "%s HAS NO VALID VERSION", entry->id);
		return 0;
	}
	if (!json_text(document, object, "licence", entry->licence, sizeof(entry->licence)) || !entry->licence[0])
	{
		snprintf(why, why_size, "%s NAMES NO LICENCE", entry->id);
		return 0;
	}
	if (!json_count(document, object, "size", MAXIMUM_MOD_BYTES, &entry->size) || !entry->size)
	{
		snprintf(why, why_size, "%s HAS NO SIZE UNDER %d MB", entry->id, MAXIMUM_MOD_BYTES >> 20);
		return 0;
	}
	if (!json_count(document, object, "format", 1000, &entry->format))
		entry->format = STORE_MODS_FORMAT;
	json_count(document, object, "min_build", 999999999ul, &entry->minimum_build);
	kinds = server_json_member(document, object, "kind");
	for (index = 0; kinds >= 0 && server_json_element(document, kinds, index) >= 0 && index < 8; index++)
	{
		char kind[24];
		size_t length = strlen(entry->kind);

		if (server_json_get_string(document, server_json_element(document, kinds, index), kind, sizeof(kind)))
		{
			char clean[24];

			text_clean(clean, sizeof(clean), kind);
			snprintf(entry->kind + length, sizeof(entry->kind) - length, "%s%s", length ? ", " : "", clean);
		}
	}
	manifest = server_json_member(document, object, "manifest");
	if (manifest < 0 || !json_text(document, manifest, "path", address, sizeof(address)) ||
		!url_resolve(directory, address, entry->manifest_url, sizeof(entry->manifest_url)) ||
		!json_count(document, manifest, "size", MAXIMUM_MANIFEST_BYTES, &entry->manifest_size) || !entry->manifest_size ||
		!json_hash(document, manifest, "sha256", entry->manifest_hash))
	{
		snprintf(why, why_size, "%s HAS NO VALID MANIFEST", entry->id);
		return 0;
	}
	(void)value;
	return 1;
}

/* the state of a catalog's mod against the mods folder */
static void entry_state(struct entry *entry)
{
	char folder[MAXIMUM_PATH];
	struct marker marker;
	int kind;

	entry->installed[0] = 0;
	entry->why[0] = 0;
	if (entry->format > STORE_MODS_FORMAT)
	{
		entry->state = _modstore_mod_unplayable;
		snprintf(entry->why, sizeof(entry->why), "IT IS MADE FOR A LATER MODS FORMAT");
		return;
	}
	if (HALO_BUILD_NUMBER && entry->minimum_build > (unsigned long)HALO_BUILD_NUMBER)
	{
		entry->state = _modstore_mod_unplayable;
		snprintf(entry->why, sizeof(entry->why), "IT NEEDS BUILD %lu OR LATER", entry->minimum_build);
		return;
	}
	entry->state = _modstore_mod_available;
	if (!mod_folder(entry->id, folder, sizeof(folder)))
		return;
	kind = path_kind(folder, NULL);
	if (!kind)
		return;
	if (kind == 2 && marker_read(folder, &marker, 0) && !strcmp(marker.id, entry->id))
	{
		snprintf(entry->installed, sizeof(entry->installed), "%s", marker.version);
		entry->state = strcmp(marker.version, entry->version) ? _modstore_mod_update : _modstore_mod_installed;
		return;
	}
	entry->state = _modstore_mod_local;
	snprintf(entry->why, sizeof(entry->why), "A FOLDER OF THAT NAME IS ALREADY IN THE MODS FOLDER");
}

/* ---------- the requests */

/* the value of a header of a response ("Name: value" lines), or NULL */
static int header_value(const char *headers, const char *name, char *value, size_t size)
{
	size_t length = strlen(name);

	value[0] = 0;
	while (*headers)
	{
		const char *end = strchr(headers, '\n');
		size_t line = end ? (size_t)(end - headers) : strlen(headers);

		if (line > length + 1 && headers[length] == ':' && names_equal_case_n(headers, name, length))
		{
			const char *text = headers + length + 1;

			while (text < headers + line && (*text == ' ' || *text == '\t'))
				text++;
			line -= (size_t)(text - headers);
			if (line && text[line - 1] == '\r')
				line--;
			if (line >= size)
				line = size - 1;
			memcpy(value, text, line);
			value[line] = 0;
			return 1;
		}
		if (!end)
			break;
		headers = end + 1;
	}
	return 0;
}

/* ---------- the requests (continued) */

/* one GET of a store's address: 1 with the answer in `response` (its body the caller's to free), else 0 and why in words;
asked twice before it is given up, unless the player stopped it */
static int http_get(const char *url, const char *headers, unsigned long maximum, struct update_response *response,
	char *why, size_t why_size)
{
	char error[200] = "";
	int attempt;

	for (attempt = 0; attempt < 2; attempt++)
	{
		if (cancelled())
		{
			snprintf(why, why_size, "STOPPED");
			return 0;
		}
		memset(response, 0, sizeof(*response));
		error[0] = 0;
		if (update_https_request("GET", url, headers, NULL, 0, maximum, response, error, sizeof(error)))
			return 1;
	}
	text_clean(error, sizeof(error), error);
	{
		char *letter;

		for (letter = error; *letter; letter++)
			*letter = (char)toupper((unsigned char)*letter);
	}
	snprintf(why, why_size, "COULD NOT REACH THE STORE (%.100s)", error);
	return 0;
}

static void answer_free(struct update_response *response)
{
	free(response->body);
	response->body = NULL;
}

/* ---------- the manifest of a mod */

static int manifest_parse(const char *text, unsigned long size, const struct entry *entry, struct manifest *manifest,
	char *why, size_t why_size)
{
	struct server_json_document document;
	char id[MAXIMUM_ID + 1], version[20];
	unsigned long format, total = 0;
	int files, index, mod_toml = 0;
	long other;

	memset(manifest, 0, sizeof(*manifest));
	if (!server_json_parse_tokens(&document, text, (int)size, 8, 8192))
	{
		snprintf(why, why_size, "THE LIST OF %s'S FILES IS NOT READABLE", entry->id);
		return 0;
	}
	files = server_json_member(&document, 0, "files");
	if (!json_count(&document, 0, "format", 1000, &format) || format != 1 || !json_text(&document, 0, "id", id, sizeof(id)) ||
		strcmp(id, entry->id) || !json_text(&document, 0, "version", version, sizeof(version)) ||
		strcmp(version, entry->version) || files < 0)
	{
		server_json_document_free(&document);
		snprintf(why, why_size, "THE LIST OF %s'S FILES IS NOT THE CATALOG'S", entry->id);
		return 0;
	}
	manifest->files = calloc(MAXIMUM_MOD_FILES, sizeof(*manifest->files));
	if (!manifest->files)
	{
		server_json_document_free(&document);
		snprintf(why, why_size, "OUT OF MEMORY");
		return 0;
	}
	for (index = 0; server_json_element(&document, files, index) >= 0; index++)
	{
		int object = server_json_element(&document, files, index);
		struct file_entry *file;
		char path[MAXIMUM_FILE_PATH + 40];
		unsigned long length;

		if (index >= MAXIMUM_MOD_FILES)
		{
			snprintf(why, why_size, "%s HAS MORE THAN %d FILES", entry->id, MAXIMUM_MOD_FILES);
			goto refused;
		}
		file = &manifest->files[index];
		if (server_json_member(&document, object, "path") < 0 ||
			!server_json_get_string(&document, server_json_member(&document, object, "path"), path, sizeof(path)) ||
			!path_ok(path))
		{
			snprintf(why, why_size, "%s NAMES A FILE THE GAME WOULD NOT PLAY (FILE %d)", entry->id, index + 1);
			goto refused;
		}
		strcpy(file->path, path);
		if (!json_count(&document, object, "size", MAXIMUM_FILE_BYTES, &length) || !length ||
			!json_hash(&document, object, "sha256", file->hash))
		{
			snprintf(why, why_size, "%s HAS A FILE WITH NO SIZE OR HASH: %.60s", entry->id, path);
			goto refused;
		}
		file->size = length;
		total += length;
		if (total > MAXIMUM_MOD_BYTES)
		{
			snprintf(why, why_size, "%s IS MORE THAN %d MB", entry->id, MAXIMUM_MOD_BYTES >> 20);
			goto refused;
		}
		if (!strcmp(path, "mod.toml"))
			mod_toml = 1;
		manifest->count = index + 1;
	}
	if (!manifest->count || !mod_toml)
	{
		snprintf(why, why_size, "%s HAS NO MOD.TOML", entry->id);
		goto refused;
	}
	/* (one name once, in any case; a file never also a folder: the card does not tell them apart by case) */
	for (index = 0; index < manifest->count; index++)
	{
		for (other = index + 1; other < manifest->count; other++)
		{
			if (names_equal_case(manifest->files[index].path, manifest->files[other].path) ||
				path_inside_case(manifest->files[index].path, manifest->files[other].path) ||
				path_inside_case(manifest->files[other].path, manifest->files[index].path))
			{
				snprintf(why, why_size, "%s NAMES THE SAME FILE TWICE: %.60s", entry->id, manifest->files[index].path);
				goto refused;
			}
		}
	}
	if (total != entry->size)
	{
		snprintf(why, why_size, "THE FILES OF %s ADD UP TO ANOTHER SIZE THAN THE CATALOG SAYS", entry->id);
		goto refused;
	}
	manifest->total = total;
	server_json_document_free(&document);
	return 1;
refused:
	server_json_document_free(&document);
	manifest_free(manifest);
	return 0;
}

/* ---------- a file of a mod */

/* whether the file at path is size bytes with this hash (-1 if it could not be read) */
static int file_matches(const char *path, unsigned long size, const unsigned char *hash)
{
	unsigned char piece[16384], digest[HASH_BYTES];
	struct sha256 context;
	unsigned long length, left;
	void *file;

	if (path_kind(path, &length) != 1)
		return 0;
	if (length != size)
		return 0;
	file = file_open(path, "rb");
	if (!file)
		return -1;
	sha256_begin(&context);
	for (left = length; left;)
	{
		size_t want = left < sizeof(piece) ? (size_t)left : sizeof(piece), got = file_read(file, piece, want);

		if (got != want)
		{
			file_close(file);
			return -1;
		}
		sha256_add(&context, piece, got);
		left -= (unsigned long)got;
	}
	file_close(file);
	sha256_end(&context, digest);
	return !memcmp(digest, hash, HASH_BYTES);
}

/* a file's address's range header for the piece that begins at `from` */
static void range_header(char *header, size_t size, unsigned long from, unsigned long count)
{
	snprintf(header, size, "Range: bytes=%lu-%lu\r\n", from, from + count - 1);
}

/* the file in the folder `stage`, kept as <name>.part as it arrives and renamed when whole and checked: 1, else 0 with
why (and the file left to go on from, unless it was whole and wrong); *cancelled says the player stopped it */
static int download_file(const struct entry *entry, const struct file_entry *file, const char *stage, unsigned long *done,
	unsigned long total, char *why, size_t why_size)
{
	char destination[MAXIMUM_PATH], part[MAXIMUM_PATH], url[MAXIMUM_URL];
	unsigned char digest[HASH_BYTES];
	struct sha256 context;
	unsigned long have = 0, counted = 0;
	int group = tag_group_find(file->path);
	int matches;

	if (snprintf(destination, sizeof(destination), "%s/%s", stage, file->path) >= (int)sizeof(destination) ||
		snprintf(part, sizeof(part), "%s.part", destination) >= (int)sizeof(part) ||
		!url_file(entry->manifest_url, file->path, url, sizeof(url)))
	{
		snprintf(why, why_size, "THE ADDRESS OF %.60s IS TOO LONG", file->path);
		return 0;
	}
	if (!make_folders(stage, file->path, 0))
	{
		snprintf(why, why_size, "COULD NOT MAKE A FOLDER ON THE CARD");
		return 0;
	}
	/* (whole already, from an earlier try) */
	matches = file_matches(destination, file->size, file->hash);
	if (matches > 0)
	{
		*done += file->size;
		progress_set(*done, total);
		return 1;
	}
	if (path_kind(destination, NULL))
		file_remove(destination);
	/* (a part of it: kept, and hashed again, to go on from) */
	if (path_kind(part, &have) == 1 && have <= file->size)
	{
		unsigned char piece[16384];
		void *old = file_open(part, "rb");
		unsigned long left = have;

		sha256_begin(&context);
		if (!old)
			have = 0;
		while (old && left)
		{
			size_t want = left < sizeof(piece) ? (size_t)left : sizeof(piece), got = file_read(old, piece, want);

			if (got != want)
			{
				have = 0;
				break;
			}
			sha256_add(&context, piece, got);
			left -= (unsigned long)got;
		}
		if (old)
			file_close(old);
	}
	else
	{
		have = 0;
	}
	if (!have)
	{
		file_remove(part);
		sha256_begin(&context);
	}
	counted = have;
	while (have < file->size)
	{
		struct update_response response;
		char header[96], range[160];
		unsigned long piece = file->size - have < PIECE_BYTES ? file->size - have : PIECE_BYTES;
		int ranged = !(have == 0 && file->size <= PIECE_BYTES), whole_instead = 0;
		void *output;
		int written;

		if (cancelled())
		{
			snprintf(why, why_size, "STOPPED");
			return 0;
		}
		header[0] = 0;
		if (ranged)
			range_header(header, sizeof(header), have, piece);
		if (!http_get(url, ranged ? header : NULL, ranged ? piece : file->size, &response, why, why_size))
		{
			/* (a store with no ranges sends a file whole, which is more than a piece: the request says so as "larger than") */
			if (ranged && strstr(why, "LARGER THAN"))
				snprintf(why, why_size, "THE STORE DOES NOT ALLOW PARTIAL DOWNLOADS, AND %.40s IS TOO BIG TO TAKE WHOLE", file->path);
			return 0;
		}
		if (ranged && response.status == 206 && response.body_size == piece)
		{
			/* (the range the server says it gave, if it says) */
			if (header_value(response.headers, "Content-Range", range, sizeof(range)))
			{
				unsigned long first = 0, last = 0, all = 0;

				if (sscanf(range, "bytes %lu-%lu/%lu", &first, &last, &all) != 3 || first != have || last != have + piece - 1 ||
					all != file->size)
				{
					answer_free(&response);
					snprintf(why, why_size, "THE STORE SENT THE WRONG PART OF %.60s", file->path);
					return 0;
				}
			}
		}
		else if (ranged && response.status == 200 && response.body_size == file->size)
		{
			/* (a server with no ranges sent all of it: take it all, if it came) */
			whole_instead = 1;
		}
		else if (response.status == 200 && !ranged && response.body_size == file->size)
		{
			whole_instead = 0;
		}
		else
		{
			int status = response.status;
			int ignored = ranged && status == 200;

			answer_free(&response);
			if (ignored)
				snprintf(why, why_size, "THE STORE DOES NOT ALLOW PARTIAL DOWNLOADS, AND %.40s IS TOO BIG TO TAKE WHOLE", file->path);
			else
				snprintf(why, why_size, "THE STORE ANSWERED %d FOR %.60s", status, file->path);
			return 0;
		}
		if (whole_instead)
		{
			file_remove(part);
			sha256_begin(&context);
			have = 0;
			*done -= counted;
			counted = 0;
			piece = file->size;
		}
		output = file_open(part, have ? "ab" : "wb");
		written = output && file_write(output, response.body, piece);
		written = output && file_close(output) && written;
		if (!written)
		{
			answer_free(&response);
			snprintf(why, why_size, "THE CARD COULD NOT BE WRITTEN (IS IT FULL?)");
			return 0;
		}
		sha256_add(&context, response.body, piece);
		answer_free(&response);
		have += piece;
		counted += piece;
		*done += piece;
		progress_set(*done, total);
	}
	sha256_end(&context, digest);
	if (memcmp(digest, file->hash, HASH_BYTES))
	{
		file_remove(part);
		*done -= counted;
		snprintf(why, why_size, "%.60s IS NOT THE FILE THE CATALOG NAMES (ITS HASH IS WRONG)", file->path);
		return 0;
	}
	if (group >= 0)
	{
		unsigned char head[0x40];
		void *input = file_open(part, "rb");
		int valid = input && file_read(input, head, sizeof(head)) == sizeof(head) && tag_header_valid(head, group);

		if (input)
			file_close(input);
		if (!valid)
		{
			file_remove(part);
			*done -= counted;
			snprintf(why, why_size, "%.60s IS NOT A TAG FILE THE GAME CAN PLAY", file->path);
			return 0;
		}
	}
	if (!file_rename(part, destination))
	{
		snprintf(why, why_size, "COULD NOT PUT %.60s IN PLACE", file->path);
		return 0;
	}
	return 1;
}

/* the staged mod.toml: TOML, naming no other id than the mod's own, for a mods format this build plays */
static int mod_toml_check(const char *stage, const char *id, char *why, size_t why_size)
{
	char path[MAXIMUM_PATH];
	char *text;
	size_t size;
	toml_result_t result;
	toml_datum_t value;

	snprintf(path, sizeof(path), "%s/mod.toml", stage);
	text = file_whole(path, MAXIMUM_TOML_BYTES, &size);
	if (!text)
	{
		snprintf(why, why_size, "ITS MOD.TOML COULD NOT BE READ");
		return 0;
	}
	result = toml_parse(text, (int)size);
	free(text);
	if (!result.ok)
	{
		toml_free(result);
		snprintf(why, why_size, "ITS MOD.TOML IS NOT VALID TOML, AND THE GAME WOULD PLAY THE MOD WITHOUT WHAT IT SAYS");
		return 0;
	}
	value = toml_get(result.toptab, "id");
	if (value.type == TOML_STRING && !names_equal_case(value.u.s, id))
	{
		toml_free(result);
		snprintf(why, why_size, "ITS MOD.TOML GIVES THE MOD ANOTHER ID THAN THE CATALOG'S");
		return 0;
	}
	value = toml_get(result.toptab, "format");
	if (value.type == TOML_INT64 && (value.u.int64 < 1 || value.u.int64 > STORE_MODS_FORMAT))
	{
		toml_free(result);
		snprintf(why, why_size, "IT IS MADE FOR A LATER MODS FORMAT THAN THIS BUILD PLAYS");
		return 0;
	}
	toml_free(result);
	return 1;
}

/* ---------- the jobs */

static long job_index_get(void)
{
	long index;

	pthread_mutex_lock(&store.lock);
	index = store.job_index;
	pthread_mutex_unlock(&store.lock);
	return index;
}

static void states_refresh(long only)
{
	long index, count;

	pthread_mutex_lock(&store.lock);
	count = store.count;
	pthread_mutex_unlock(&store.lock);
	/* (the folders are looked at without the lock held: a card is slow, and the screen asks for the state every frame) */
	for (index = 0; index < count; index++)
	{
		struct entry *copy;

		if (only >= 0 && only != index)
			continue;
		copy = malloc(sizeof(*copy));
		if (!copy)
			return;
		pthread_mutex_lock(&store.lock);
		if (index < store.count)
			*copy = store.entries[index];
		else
			copy->id[0] = 0;
		pthread_mutex_unlock(&store.lock);
		if (copy->id[0])
		{
			entry_state(copy);
			pthread_mutex_lock(&store.lock);
			if (index < store.count && !strcmp(store.entries[index].id, copy->id))
			{
				store.entries[index].state = copy->state;
				memcpy(store.entries[index].installed, copy->installed, sizeof(copy->installed));
				memcpy(store.entries[index].why, copy->why, sizeof(copy->why));
			}
			pthread_mutex_unlock(&store.lock);
		}
		free(copy);
	}
	pthread_mutex_lock(&store.lock);
	bump();
	pthread_mutex_unlock(&store.lock);
}

/* what a half-done install or update left: an old folder put aside is given back if the mod's folder is gone, else
deleted; a staging folder with no mod in the catalog (when it is known: keep != NULL) is deleted */
static void recover(const struct entry *keep, long keep_count)
{
	char root[MAXIMUM_PATH], name[256];
	void *directory;

	mods_directory(root, sizeof(root));
	directory = posix_directory_open(root);
	if (!directory)
		return;
	while (posix_directory_next(directory, name, sizeof(name)))
	{
		size_t length = strlen(name);
		char id[MAXIMUM_ID + 1], path[MAXIMUM_PATH], folder[MAXIMUM_PATH];

		if (strncmp(name, ".store-", 7) || length < 12 || length - 11 > MAXIMUM_ID + 7)
			continue;
		if (!strcmp(name + length - 4, ".old") && length - 11 <= MAXIMUM_ID)
		{
			memcpy(id, name + 7, length - 11);
			id[length - 11] = 0;
			if (!id_ok(id) || !mod_folder(id, folder, sizeof(folder)) || snprintf(path, sizeof(path), "%s/%s", root, name) >= (int)sizeof(path))
				continue;
			if (!path_kind(folder, NULL))
			{
				if (file_rename(path, folder))
					platform_log("mod store: %s was put back (an update was cut short)", id);
			}
			else
			{
				tree_delete(path);
			}
		}
		else if (keep && length > 11 && !strcmp(name + length - 4, ".tmp") && length - 11 <= MAXIMUM_ID)
		{
			long index;

			memcpy(id, name + 7, length - 11);
			id[length - 11] = 0;
			for (index = 0; index < keep_count; index++)
			{
				if (!strcmp(keep[index].id, id))
					break;
			}
			if (index == keep_count && snprintf(path, sizeof(path), "%s/%s", root, name) < (int)sizeof(path))
				tree_delete(path);
		}
	}
	posix_directory_close(directory);
}

static int key_get(unsigned char *key)
{
	const char *configured = config_string("mods.store_key");

	if (configured && configured[0])
		return hex_parse(configured, key, 32, 1);
#ifdef HALO_MODSTORE_PUBLIC_KEY
	return hex_parse(HALO_MODSTORE_PUBLIC_KEY, key, 32, 1);
#else
	return 0;
#endif
}

/* the highest serial a catalog has had on this machine */
static double serial_read(void)
{
	char root[MAXIMUM_PATH], path[MAXIMUM_PATH];
	char *text;
	double serial = 0;

	mods_directory(root, sizeof(root));
	snprintf(path, sizeof(path), "%s/" SERIAL_NAME, root);
	text = file_whole(path, 64, NULL);
	if (text)
	{
		serial = strtod(text, NULL);
		free(text);
	}
	return serial;
}

static void serial_write(double serial)
{
	char root[MAXIMUM_PATH], path[MAXIMUM_PATH], text[48];

	mods_directory(root, sizeof(root));
	make_folders(platform_data_root(), "mods", 1);
	snprintf(path, sizeof(path), "%s/" SERIAL_NAME, root);
	snprintf(text, sizeof(text), "%.0f\n", serial);
	file_put(path, text, strlen(text));
}

static int store_address(char *url, size_t size)
{
	const char *configured = config_string("mods.store_url");

	if (!configured || !configured[0])
		return 0;
	snprintf(url, size, "%s", configured);
	return 1;
}

/* the catalog fetched, its signature checked, and its mods listed against the mods folder */
static void catalog_job(void)
{
	char url[MAXIMUM_URL], signature_url[MAXIMUM_URL], directory[MAXIMUM_URL], why[160];
	unsigned char key[32], signature[64];
	struct update_response catalog, signed_by;
	struct server_json_document document;
	struct entry *entries;
	long count = 0;
	double serial, newest;
	int mods, index;

	set_phase(_modstore_loading);
	say("LOOKING FOR MODS...");
	recover(NULL, 0);
	if (!store_address(url, sizeof(url)) || !url_ok(url) || snprintf(signature_url, sizeof(signature_url), "%s.sig", url) >= (int)sizeof(signature_url) ||
		!url_directory(url, directory, sizeof(directory)))
	{
		set_phase(_modstore_failed);
		say("THE STORE'S ADDRESS IS NOT AN HTTPS ADDRESS THIS GAME CAN USE");
		return;
	}
	if (!key_get(key))
	{
		set_phase(_modstore_failed);
		say("THERE IS NO KEY TO CHECK THE STORE'S SIGNATURE WITH (MODS.STORE_KEY)");
		return;
	}
	if (!http_get(url, NULL, MAXIMUM_CATALOG_BYTES, &catalog, why, sizeof(why)))
	{
		set_phase(_modstore_failed);
		say("%s", why);
		return;
	}
	if (catalog.status != 200 || !catalog.body_size)
	{
		int status = catalog.status;

		answer_free(&catalog);
		set_phase(_modstore_failed);
		say("THE STORE ANSWERED %d, NOT A CATALOG", status);
		return;
	}
	if (!http_get(signature_url, NULL, MAXIMUM_SIGNATURE_BYTES, &signed_by, why, sizeof(why)))
	{
		answer_free(&catalog);
		set_phase(_modstore_failed);
		say("%s", why);
		return;
	}
	if (signed_by.status != 200 || !signed_by.body || !hex_parse(signed_by.body, signature, 64, 1) ||
		!p2p_ed25519_verify(key, catalog.body, (int)catalog.body_size, signature))
	{
		answer_free(&catalog);
		answer_free(&signed_by);
		set_phase(_modstore_failed);
		say("THE CATALOG IS NOT SIGNED BY THE STORE'S KEY, SO NOTHING WAS TAKEN FROM IT");
		return;
	}
	answer_free(&signed_by);
	/* (the signature is good: now the catalog is read) */
	/* (the document points into the body: it is freed after the document is) */
	if (!server_json_parse_tokens(&document, catalog.body, (int)catalog.body_size, 8, 16384))
	{
		answer_free(&catalog);
		set_phase(_modstore_failed);
		say("THE CATALOG CANNOT BE READ");
		return;
	}
	{
		unsigned long format = 0;
		double number = 0;
		int token = server_json_member(&document, 0, "serial");

		if (!json_count(&document, 0, "format", 1000, &format) || format != 1 || token < 0 ||
			!server_json_get_number(&document, token, &number) || number < 0 || number > 9007199254740991.0)
		{
			server_json_document_free(&document);
			answer_free(&catalog);
			set_phase(_modstore_failed);
			say("THE CATALOG IS NOT OF A FORMAT THIS GAME READS");
			return;
		}
		newest = number;
	}
	serial = serial_read();
	if (newest < serial)
	{
		server_json_document_free(&document);
		answer_free(&catalog);
		set_phase(_modstore_failed);
		say("THE CATALOG IS OLDER THAN ONE SEEN BEFORE, SO IT WAS NOT USED");
		return;
	}
	entries = calloc(MAXIMUM_CATALOG_MODS, sizeof(*entries));
	mods = server_json_member(&document, 0, "mods");
	if (!entries || mods < 0)
	{
		free(entries);
		server_json_document_free(&document);
		answer_free(&catalog);
		set_phase(_modstore_failed);
		say("THE CATALOG LISTS NO MODS");
		return;
	}
	for (index = 0; server_json_element(&document, mods, index) >= 0 && count < MAXIMUM_CATALOG_MODS; index++)
	{
		struct entry *entry = &entries[count];
		long other;

		if (!entry_parse(&document, server_json_element(&document, mods, index), directory, entry, why, sizeof(why)))
		{
			platform_log("mod store: a mod of the catalog is left out: %s", why);
			continue;
		}
		for (other = 0; other < count; other++)
		{
			if (!strcmp(entries[other].id, entry->id))
				break;
		}
		if (other < count)
		{
			platform_log("mod store: %s is listed twice: the second is left out", entry->id);
			continue;
		}
		entry_state(entry);
		count++;
	}
	server_json_document_free(&document);
	answer_free(&catalog);
	if (newest > serial)
		serial_write(newest);
	recover(entries, count);
	pthread_mutex_lock(&store.lock);
	free(store.entries);
	store.entries = entries;
	store.count = count;
	snprintf(store.directory_url, sizeof(store.directory_url), "%s", directory);
	store.phase = _modstore_ready;
	snprintf(store.message, sizeof(store.message), count ? "%ld MODS" : "THE STORE HAS NO MODS YET", count);
	bump();
	pthread_mutex_unlock(&store.lock);
}

/* an install, or an update, of the catalog's mod `entry` */
static void install_job(struct entry entry)
{
	char why[200] = "", root[MAXIMUM_PATH], folder[MAXIMUM_PATH], stage[MAXIMUM_PATH], old[MAXIMUM_PATH];
	char plan_path[MAXIMUM_PATH], plan[128], hash_text[HASH_BYTES * 2 + 1];
	struct update_response answer;
	struct manifest manifest;
	struct marker marker;
	unsigned char digest[HASH_BYTES];
	unsigned long done = 0;
	posix_ulong free_low, free_high, total_low, total_high;
	char *text;
	size_t used = 0, capacity;
	long index;
	int update = entry.state == _modstore_mod_update;
	int stopped = 0;

	memset(&manifest, 0, sizeof(manifest));
	mods_directory(root, sizeof(root));
	if (!mod_folder(entry.id, folder, sizeof(folder)) || !store_folder(entry.id, "tmp", stage, sizeof(stage)) ||
		!store_folder(entry.id, "old", old, sizeof(old)))
	{
		say("%s HAS A NAME THE CARD CANNOT HOLD", entry.id);
		return;
	}
	say("GETTING THE LIST OF FILES OF %s...", entry.name);
	if (!http_get(entry.manifest_url, NULL, entry.manifest_size, &answer, why, sizeof(why)))
		goto failed;
	/* (the manifest is the one the signed catalog names, byte for byte, or nothing is looked at) */
	sha256_all(answer.body ? answer.body : "", answer.body_size, digest);
	if (answer.status != 200 || answer.body_size != entry.manifest_size || memcmp(digest, entry.manifest_hash, HASH_BYTES))
	{
		int status = answer.status;

		answer_free(&answer);
		if (status != 200)
			snprintf(why, sizeof(why), "THE STORE ANSWERED %d FOR THE LIST OF FILES", status);
		else
			snprintf(why, sizeof(why), "THE LIST OF FILES IS NOT THE ONE THE CATALOG NAMES (ITS HASH IS WRONG)");
		goto failed;
	}
	hex_write(digest, HASH_BYTES, hash_text);
	if (!manifest_parse(answer.body, answer.body_size, &entry, &manifest, why, sizeof(why)))
	{
		answer_free(&answer);
		goto failed;
	}
	answer_free(&answer);
	/* (what is in the folder now: the player's own folder is never replaced) */
	if (path_kind(folder, NULL) && !(path_kind(folder, NULL) == 2 && marker_read(folder, &marker, 0) && !strcmp(marker.id, entry.id)))
	{
		snprintf(why, sizeof(why), "A FOLDER CALLED %s IS ALREADY IN THE MODS FOLDER AND IS NOT THE STORE'S, SO IT WAS LEFT ALONE", entry.id);
		goto failed;
	}
	if (posix_disk_space(root, &free_low, &free_high, &total_low, &total_high) == 0)
	{
		unsigned long long available = ((unsigned long long)free_high << 32) | free_low;

		if (available < (unsigned long long)manifest.total + FREE_MARGIN_BYTES)
		{
			snprintf(why, sizeof(why), "NOT ENOUGH ROOM ON THE CARD: %lu MB NEEDED, %llu MB FREE", (manifest.total + FREE_MARGIN_BYTES) >> 20,
				available >> 20);
			goto failed;
		}
	}
	if (!make_folders(platform_data_root(), "mods", 1))
	{
		snprintf(why, sizeof(why), "THE MODS FOLDER COULD NOT BE MADE");
		goto failed;
	}
	/* (a staging folder of the same list goes on; of another is deleted) */
	snprintf(plan_path, sizeof(plan_path), "%s/" PLAN_NAME, stage);
	snprintf(plan, sizeof(plan), "opence-modstore-plan 1\n%s\n", hash_text);
	text = path_kind(stage, NULL) ? file_whole(plan_path, 256, NULL) : NULL;
	if (path_kind(stage, NULL) && (!text || strcmp(text, plan)))
		tree_delete(stage);
	free(text);
	if (!path_kind(stage, NULL) && posix_make_directory(stage) != 0)
	{
		snprintf(why, sizeof(why), "A FOLDER FOR THE DOWNLOAD COULD NOT BE MADE ON THE CARD");
		goto failed;
	}
	if (!file_put(plan_path, plan, strlen(plan)))
	{
		snprintf(why, sizeof(why), "THE CARD COULD NOT BE WRITTEN");
		goto failed;
	}
	progress_set(0, manifest.total);
	for (index = 0; index < manifest.count; index++)
	{
		say("%s %s: FILE %ld OF %ld", update ? "UPDATING" : "DOWNLOADING", entry.name, index + 1, manifest.count);
		if (!download_file(&entry, &manifest.files[index], stage, &done, manifest.total, why, sizeof(why)))
		{
			stopped = cancelled();
			goto failed;
		}
	}
	if (!mod_toml_check(stage, entry.id, why, sizeof(why)))
	{
		tree_delete(stage);
		goto failed;
	}
	/* (the marker: what the store put in the folder, for removing it) */
	capacity = 256 + (size_t)manifest.count * (MAXIMUM_FILE_PATH + 100);
	text = malloc(capacity);
	if (!text)
	{
		snprintf(why, sizeof(why), "OUT OF MEMORY");
		goto failed;
	}
	used = (size_t)snprintf(text, capacity, MARKER_HEADER "\nid %s\nversion %s\nmanifest %s\n", entry.id, entry.version, hash_text);
	for (index = 0; index < manifest.count; index++)
	{
		char file_hash[HASH_BYTES * 2 + 1];

		hex_write(manifest.files[index].hash, HASH_BYTES, file_hash);
		used += (size_t)snprintf(text + used, capacity - used, "file %lu %s %s\n", manifest.files[index].size, file_hash,
			manifest.files[index].path);
	}
	{
		char marker_path[MAXIMUM_PATH];
		int written;

		snprintf(marker_path, sizeof(marker_path), "%s/" MARKER_NAME, stage);
		written = file_put(marker_path, text, used);
		free(text);
		if (!written)
		{
			snprintf(why, sizeof(why), "THE CARD COULD NOT BE WRITTEN");
			goto failed;
		}
	}
	file_remove(plan_path);
	/* (put in place: an old folder of the store's aside first, and back if the new one cannot take its place) */
	if (path_kind(folder, NULL))
	{
		if (path_kind(old, NULL))
			tree_delete(old);
		if (!file_rename(folder, old))
		{
			snprintf(why, sizeof(why), "THE OLD FOLDER COULD NOT BE PUT ASIDE, SO NOTHING WAS CHANGED");
			goto failed;
		}
		if (!file_rename(stage, folder))
		{
			file_rename(old, folder);
			snprintf(why, sizeof(why), "THE NEW FOLDER COULD NOT BE PUT IN PLACE, SO THE OLD ONE WAS KEPT");
			goto failed;
		}
		tree_delete(old);
	}
	else if (!file_rename(stage, folder))
	{
		snprintf(why, sizeof(why), "THE MOD COULD NOT BE PUT IN PLACE");
		goto failed;
	}
	platform_log("mod store: %s %s %s (%ld files, %lu bytes)", update ? "updated" : "installed", entry.id, entry.version,
		manifest.count, done);
	manifest_free(&manifest);
	pthread_mutex_lock(&store.lock);
	store.changed = 1;
	pthread_mutex_unlock(&store.lock);
	states_refresh(job_index_get());
	say("%s %s %s. IT IS ON IN THE MODS LIST", update ? "UPDATED" : "INSTALLED", entry.name, entry.version);
	return;
failed:
	manifest_free(&manifest);
	if (stopped)
		say("STOPPED. WHAT WAS DOWNLOADED IS KEPT, AND DOWNLOAD AGAIN GOES ON FROM THERE");
	else
		say("%s", why);
	platform_log("mod store: %s not installed: %s", entry.id, why);
}

/* the removal of a mod the store installed: its files, as its marker lists them, and nothing else */
static void remove_job(struct entry entry)
{
	char folder[MAXIMUM_PATH], stage[MAXIMUM_PATH], path[MAXIMUM_PATH];
	struct marker marker;
	long index;
	int left = 0;

	if (!mod_folder(entry.id, folder, sizeof(folder)) || !store_folder(entry.id, "tmp", stage, sizeof(stage)))
	{
		say("%s HAS A NAME THE CARD CANNOT HOLD", entry.id);
		return;
	}
	if (!marker_read(folder, &marker, 1) || strcmp(marker.id, entry.id))
	{
		say("%s WAS NOT INSTALLED BY THE STORE, SO IT WAS LEFT ALONE", entry.name);
		return;
	}
	say("REMOVING %s...", entry.name);
	progress_set(0, (unsigned long)marker.files.count);
	for (index = 0; index < marker.files.count; index++)
	{
		char *slash;

		if (snprintf(path, sizeof(path), "%s/%s", folder, marker.files.files[index].path) >= (int)sizeof(path))
			continue;
		if (path_kind(path, NULL) == 1 && !file_remove(path))
			left++;
		/* (and the folders it was in, deepest first, those that are empty now) */
		while ((slash = strrchr(path, '/')) != NULL && strlen(path) > strlen(folder) + 1)
		{
			*slash = 0;
			if (strlen(path) <= strlen(folder) || !file_remove(path))
				break;
		}
		progress_set((unsigned long)index + 1, (unsigned long)marker.files.count);
	}
	manifest_free(&marker.files);
	snprintf(path, sizeof(path), "%s/" MARKER_NAME, folder);
	file_remove(path);
	if (!file_remove(folder))
		left++;
	if (path_kind(stage, NULL))
		tree_delete(stage);
	pthread_mutex_lock(&store.lock);
	store.changed = 1;
	pthread_mutex_unlock(&store.lock);
	states_refresh(job_index_get());
	if (left)
		say("REMOVED %s. FILES OF YOUR OWN IN ITS FOLDER WERE LEFT", entry.name);
	else
		say("REMOVED %s", entry.name);
	platform_log("mod store: %s removed%s", entry.id, left ? " (some files were left)" : "");
}

enum
{
	JOB_STATES = 100,
};

static void *worker_main(void *argument)
{
	int job;
	long index;
	struct entry entry;

	(void)argument;
#ifdef HALO_WIIU
	wiiu_thread_background("mod store");
#endif
	memset(&entry, 0, sizeof(entry));
	pthread_mutex_lock(&store.lock);
	job = store.job;
	index = store.job_index;
	if (index >= 0 && index < store.count)
		entry = store.entries[index];
	pthread_mutex_unlock(&store.lock);
	if (job == _modstore_job_catalog)
		catalog_job();
	else if (job == _modstore_job_install)
		install_job(entry);
	else if (job == _modstore_job_remove)
		remove_job(entry);
	else if (job == JOB_STATES)
		states_refresh(-1);
	pthread_mutex_lock(&store.lock);
	store.running = 0;
	store.job = _modstore_job_none;
	store.job_index = -1;
	bump();
	pthread_mutex_unlock(&store.lock);
	return NULL;
}

static int job_start(int job, long index)
{
	pthread_t thread;
	pthread_attr_t attributes;
	int created;

	pthread_mutex_lock(&store.lock);
	if (store.running)
	{
		pthread_mutex_unlock(&store.lock);
		return 0;
	}
	store.running = 1;
	store.job = job;
	store.job_index = index;
	store.cancel = 0;
	store.done = store.total = 0;
	bump();
	pthread_mutex_unlock(&store.lock);
	pthread_attr_init(&attributes);
	pthread_attr_setstacksize(&attributes, WIIU_STORE_STACK_BYTES);
	created = pthread_create(&thread, &attributes, worker_main, NULL) == 0;
	pthread_attr_destroy(&attributes);
	if (created)
	{
		pthread_detach(thread);
		return 1;
	}
	pthread_mutex_lock(&store.lock);
	store.running = 0;
	store.job = _modstore_job_none;
	snprintf(store.message, sizeof(store.message), "A THREAD FOR THE STORE COULD NOT BE MADE");
	bump();
	pthread_mutex_unlock(&store.lock);
	return 0;
}

/* ---------- public code */

int modstore_configured(void)
{
	unsigned char key[32];
	char url[MAXIMUM_URL];

	return store_address(url, sizeof(url)) && key_get(key);
}

int modstore_phase(void)
{
	int phase;
	char url[MAXIMUM_URL];

	if (!store_address(url, sizeof(url)))
		return _modstore_off;
	pthread_mutex_lock(&store.lock);
	phase = store.phase;
	pthread_mutex_unlock(&store.lock);
	return phase == _modstore_off ? _modstore_idle : phase;
}

int modstore_message(char *text, unsigned long size)
{
	int length;

	if (!size)
		return 0;
	pthread_mutex_lock(&store.lock);
	snprintf(text, size, "%s", store.message);
	pthread_mutex_unlock(&store.lock);
	length = (int)strlen(text);
	return length;
}

unsigned long modstore_generation(void)
{
	unsigned long generation;

	pthread_mutex_lock(&store.lock);
	generation = store.generation;
	pthread_mutex_unlock(&store.lock);
	return generation;
}

void modstore_open(void)
{
	char url[MAXIMUM_URL];
	int ready;

	if (!store_address(url, sizeof(url)))
		return;
	pthread_mutex_lock(&store.lock);
	store.opened = 1;
	ready = store.phase == _modstore_ready;
	pthread_mutex_unlock(&store.lock);
	/* (opened again: the list kept, its states looked at again; a catalog that failed is asked for again) */
	job_start(ready ? JOB_STATES : _modstore_job_catalog, -1);
}

void modstore_refresh(void)
{
	char url[MAXIMUM_URL];

	if (store_address(url, sizeof(url)))
		job_start(_modstore_job_catalog, -1);
}

void modstore_close(void)
{
	pthread_mutex_lock(&store.lock);
	store.opened = 0;
	if (store.running)
		store.cancel = 1;
	pthread_mutex_unlock(&store.lock);
}

long modstore_count(void)
{
	long count;

	pthread_mutex_lock(&store.lock);
	count = store.count;
	pthread_mutex_unlock(&store.lock);
	return count;
}

int modstore_state(long index)
{
	int state = _modstore_mod_available;

	pthread_mutex_lock(&store.lock);
	if (index >= 0 && index < store.count)
		state = store.entries[index].state;
	pthread_mutex_unlock(&store.lock);
	return state;
}

unsigned long modstore_size(long index)
{
	unsigned long size = 0;

	pthread_mutex_lock(&store.lock);
	if (index >= 0 && index < store.count)
		size = store.entries[index].size;
	pthread_mutex_unlock(&store.lock);
	return size;
}

int modstore_field(long index, int field, char *text, unsigned long size)
{
	const char *value = "";
	int found = 0;

	if (!size)
		return 0;
	text[0] = 0;
	pthread_mutex_lock(&store.lock);
	if (index >= 0 && index < store.count)
	{
		const struct entry *entry = &store.entries[index];

		found = 1;
		switch (field)
		{
		case _modstore_field_id: value = entry->id; break;
		case _modstore_field_name: value = entry->name; break;
		case _modstore_field_author: value = entry->author; break;
		case _modstore_field_version: value = entry->version; break;
		case _modstore_field_installed: value = entry->installed; break;
		case _modstore_field_description: value = entry->description; break;
		case _modstore_field_kind: value = entry->kind; break;
		case _modstore_field_licence: value = entry->licence; break;
		case _modstore_field_why: value = entry->why; break;
		default: found = 0; break;
		}
		snprintf(text, size, "%s", value);
	}
	pthread_mutex_unlock(&store.lock);
	return found;
}

int modstore_job(void)
{
	int job;

	pthread_mutex_lock(&store.lock);
	job = store.job == JOB_STATES ? _modstore_job_none : store.job;
	pthread_mutex_unlock(&store.lock);
	return job;
}

long modstore_job_index(void)
{
	long index;

	pthread_mutex_lock(&store.lock);
	index = store.running ? store.job_index : -1;
	pthread_mutex_unlock(&store.lock);
	return index;
}

void modstore_progress(unsigned long *done, unsigned long *total)
{
	pthread_mutex_lock(&store.lock);
	*done = store.done;
	*total = store.total;
	pthread_mutex_unlock(&store.lock);
}

void modstore_cancel(void)
{
	pthread_mutex_lock(&store.lock);
	if (store.running)
		store.cancel = 1;
	pthread_mutex_unlock(&store.lock);
}

int modstore_install(long index)
{
	int state;

	pthread_mutex_lock(&store.lock);
	state = index >= 0 && index < store.count ? store.entries[index].state : -1;
	if (store.phase != _modstore_ready || store.running)
		state = -2;
	pthread_mutex_unlock(&store.lock);
	if (state == -2)
	{
		say("WAIT FOR THE LAST JOB TO END");
		return 0;
	}
	if (state != _modstore_mod_available && state != _modstore_mod_update)
	{
		if (state == _modstore_mod_installed)
			say("THAT VERSION IS ALREADY INSTALLED");
		else if (state == _modstore_mod_local)
			say("A FOLDER OF THAT NAME IS ALREADY IN THE MODS FOLDER AND IS NOT THE STORE'S: IT WAS LEFT ALONE");
		else if (state == _modstore_mod_unplayable)
			say("THIS BUILD CANNOT PLAY THAT MOD");
		else
			say("THERE IS NO SUCH MOD");
		return 0;
	}
	return job_start(_modstore_job_install, index);
}

int modstore_remove(long index)
{
	int state;

	pthread_mutex_lock(&store.lock);
	state = index >= 0 && index < store.count ? store.entries[index].state : -1;
	if (store.phase != _modstore_ready || store.running)
		state = -2;
	pthread_mutex_unlock(&store.lock);
	if (state == -2)
	{
		say("WAIT FOR THE LAST JOB TO END");
		return 0;
	}
	if (state != _modstore_mod_installed && state != _modstore_mod_update)
	{
		say(state == _modstore_mod_local ? "THAT MOD IS YOURS, NOT THE STORE'S: REMOVE ITS FOLDER YOURSELF" : "THAT MOD IS NOT INSTALLED");
		return 0;
	}
	return job_start(_modstore_job_remove, index);
}

int modstore_changed(void)
{
	int changed;

	pthread_mutex_lock(&store.lock);
	changed = store.changed;
	store.changed = 0;
	pthread_mutex_unlock(&store.lock);
	return changed;
}

#else /* HALO_ANDROID, HALO_WEB: the store is not in these builds */

int modstore_configured(void) { return 0; }
int modstore_phase(void) { return _modstore_off; }
int modstore_message(char *text, unsigned long size) { if (size) text[0] = 0; return 0; }
unsigned long modstore_generation(void) { return 0; }
void modstore_open(void) {}
void modstore_refresh(void) {}
void modstore_close(void) {}
long modstore_count(void) { return 0; }
int modstore_state(long index) { (void)index; return 0; }
unsigned long modstore_size(long index) { (void)index; return 0; }
int modstore_field(long index, int field, char *text, unsigned long size) { (void)index; (void)field; if (size) text[0] = 0; return 0; }
int modstore_job(void) { return 0; }
long modstore_job_index(void) { return -1; }
void modstore_progress(unsigned long *done, unsigned long *total) { *done = *total = 0; }
void modstore_cancel(void) {}
int modstore_install(long index) { (void)index; return 0; }
int modstore_remove(long index) { (void)index; return 0; }
int modstore_changed(void) { return 0; }

#endif
