/*
WIIU_UPDATE_CORE.C

The self-updater's decisions (wiiu_update_core.h): the manifest and its signature, the journal, the swap's steps and what the
next start does about a swap or a trial that a power cut, a crash or a hang interrupted. Plain C over the caller's file
functions; tested on the desktop (tools/test_wiiu_update.py), every step of a swap cut off in turn.
*/

#include "wiiu_update_core.h"

#include "monocypher-ed25519.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---------- small things */

uint32_t wiiu_update_crc32(const void *data, size_t size)
{
	const unsigned char *bytes = data;
	uint32_t crc = 0xFFFFFFFFu;
	size_t i;
	int bit;

	for (i = 0; i < size; i++)
	{
		crc ^= bytes[i];
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return ~crc;
}

void wiiu_update_hex(const uint8_t *bytes, size_t count, char *out)
{
	static const char digits[] = "0123456789abcdef";
	size_t i;

	for (i = 0; i < count; i++)
	{
		out[i * 2] = digits[bytes[i] >> 4];
		out[i * 2 + 1] = digits[bytes[i] & 15];
	}
	out[count * 2] = 0;
}

static int hex_digit(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

/* exactly count bytes as lowercase hexadecimal digits (what sha256sum and the release script write) */
static int parse_hex(const char *text, size_t length, uint8_t *out, size_t count)
{
	size_t i;

	if (length != count * 2)
		return -1;
	for (i = 0; i < count; i++)
	{
		const int high = hex_digit((unsigned char)text[i * 2]), low = hex_digit((unsigned char)text[i * 2 + 1]);

		if (high < 0 || low < 0)
			return -1;
		out[i] = (uint8_t)(high << 4 | low);
	}
	return 0;
}

/* a decimal number with no sign, no leading zero and no more than most */
static int parse_number(const char *text, size_t length, uint32_t most, uint32_t *value)
{
	uint32_t result = 0;
	size_t i;

	if (!length || length > 10 || (length > 1 && text[0] == '0'))
		return -1;
	for (i = 0; i < length; i++)
	{
		const unsigned char c = (unsigned char)text[i];

		if (c < '0' || c > '9')
			return -1;
		/* (the 32-bit bound before the multiplication, then the caller's) */
		if (result > (0xFFFFFFFFu - (c - '0')) / 10u)
			return -1;
		result = result * 10u + (c - '0');
	}
	if (result > most)
		return -1;
	*value = result;
	return 0;
}

static void say(char *reason, size_t capacity, const char *format, ...) __attribute__((format(printf, 3, 4)));

static void say(char *reason, size_t capacity, const char *format, ...)
{
	va_list arguments;

	if (!reason || !capacity)
		return;
	va_start(arguments, format);
	vsnprintf(reason, capacity, format, arguments);
	va_end(arguments);
}

/* A line's fields, split at single spaces: a line with two spaces together, or one at either end, is no line of a manifest. */
#define FIELDS_MOST 6

typedef struct fields
{
	const char *text[FIELDS_MOST];
	size_t length[FIELDS_MOST];
	unsigned count;
} fields;

static int split(const char *line, size_t length, fields *out)
{
	size_t start = 0, i;

	out->count = 0;
	if (!length || line[0] == ' ' || line[length - 1] == ' ')
		return -1;
	for (i = 0; i <= length; i++)
	{
		if (i < length && line[i] != ' ')
			continue;
		if (i == start || out->count == FIELDS_MOST)
			return -1;
		out->text[out->count] = line + start;
		out->length[out->count] = i - start;
		out->count++;
		start = i + 1;
	}
	return 0;
}

static int field_is(const fields *line, unsigned index, const char *word)
{
	return index < line->count && line->length[index] == strlen(word) && !memcmp(line->text[index], word, line->length[index]);
}

/* ---------- names */

int wiiu_update_name_allowed(const char *name)
{
	const size_t length = name ? strlen(name) : 0;
	const char *dot;
	size_t i;

	if (!length || length >= WIIU_UPDATE_NAME_BYTES)
		return 0;
	if (!((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= '0' && name[0] <= '9')))
		return 0;
	for (i = 1; i < length; i++)
	{
		const char c = name[i];

		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
			return 0;
	}
	/* one dot at most, with something after it: no "..", no name of the updater's own (halo.wuhb.new), no hidden suffix */
	dot = strchr(name, '.');
	if (dot && (strchr(dot + 1, '.') || !dot[1]))
		return 0;
	/* What a release owns: the bundle, the brokers' list, and packs of data (shaders.pack, and whatever pack a later release adds).
	Anything else in the folder is the player's or the shell's (config.toml, network.key, the logs and notes, the switches in .txt
	files, data/, menus/), and a release never touches it. */
	if (!strcmp(name, WIIU_UPDATE_BUNDLE) || !strcmp(name, "brokers.txt"))
		return 1;
	return dot && !strcmp(dot, ".pack");
}

/* ---------- the manifest */

int wiiu_update_manifest_parse(const void *data, size_t size, wiiu_update_manifest *manifest, char *reason, size_t reason_size)
{
	const char *text = data;
	size_t at = 0, i;
	unsigned line_number = 0;
	uint64_t total = 0;
	int have_bundle = 0;

	memset(manifest, 0, sizeof(*manifest));
	if (!text || !size || size > WIIU_UPDATE_MANIFEST_MOST)
	{
		say(reason, reason_size, "the manifest is %lu bytes (1 to %u)", (unsigned long)size, (unsigned)WIIU_UPDATE_MANIFEST_MOST);
		return -1;
	}
	if (text[size - 1] != '\n')
	{
		say(reason, reason_size, "the manifest does not end with a line's end");
		return -1;
	}
	/* (printable ASCII and line ends only: no NUL, no carriage return, no control character a name could hide behind) */
	for (i = 0; i < size; i++)
	{
		const unsigned char c = (unsigned char)text[i];

		if (c != '\n' && (c < 0x20 || c > 0x7E))
		{
			say(reason, reason_size, "the manifest has a byte %02x at %lu", (unsigned)c, (unsigned long)i);
			return -1;
		}
	}
	while (at < size)
	{
		const char *line = text + at;
		const size_t length = (size_t)((const char *)memchr(line, '\n', size - at) - line);
		fields f;

		at += length + 1;
		line_number++;
		if (split(line, length, &f) != 0)
		{
			say(reason, reason_size, "line %u is not fields with single spaces between them", line_number);
			return -1;
		}
		/* the lines in their order: the format, the build, its tag, the maps, then the files */
		if (line_number == 1)
		{
			if (f.count != 2 || !field_is(&f, 0, "halo-wiiu-manifest") || !field_is(&f, 1, "1"))
			{
				say(reason, reason_size, "the first line is not \"halo-wiiu-manifest 1\"");
				return -1;
			}
		}
		else if (line_number == 2)
		{
			if (f.count != 2 || !field_is(&f, 0, "build") ||
				parse_number(f.text[1], f.length[1], WIIU_UPDATE_BUILD_MOST, &manifest->build) != 0 || !manifest->build)
			{
				say(reason, reason_size, "line 2 is not \"build <1 to %u>\"", (unsigned)WIIU_UPDATE_BUILD_MOST);
				return -1;
			}
		}
		else if (line_number == 3)
		{
			char tag[24];

			snprintf(tag, sizeof(tag), "wiiu-%u", (unsigned)manifest->build);
			if (f.count != 2 || !field_is(&f, 0, "tag") || !field_is(&f, 1, tag))
			{
				say(reason, reason_size, "line 3 is not \"tag %s\"", tag);
				return -1;
			}
		}
		else if (line_number == 4)
		{
			if (f.count != 2 || !field_is(&f, 0, "maps") || f.length[1] >= WIIU_UPDATE_MAPS_BYTES)
			{
				say(reason, reason_size, "line 4 is not \"maps <format>\"");
				return -1;
			}
			for (i = 0; i < f.length[1]; i++)
			{
				const char c = f.text[1][i];

				if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
				{
					say(reason, reason_size, "the maps' format has a character outside a-z, 0-9 and -");
					return -1;
				}
			}
			memcpy(manifest->maps, f.text[1], f.length[1]);
			manifest->maps[f.length[1]] = 0;
		}
		else
		{
			wiiu_update_file *file;
			uint32_t index;

			if (f.count != 4 || !field_is(&f, 0, "file"))
			{
				say(reason, reason_size, "line %u is not \"file <name> <size> <sha256>\"", line_number);
				return -1;
			}
			if (manifest->file_count == WIIU_UPDATE_FILES)
			{
				say(reason, reason_size, "the manifest has more than %u files", (unsigned)WIIU_UPDATE_FILES);
				return -1;
			}
			file = &manifest->files[manifest->file_count];
			if (f.length[1] >= sizeof(file->name))
			{
				say(reason, reason_size, "line %u: the name is longer than %u characters", line_number, (unsigned)(sizeof(file->name) - 1));
				return -1;
			}
			memcpy(file->name, f.text[1], f.length[1]);
			file->name[f.length[1]] = 0;
			if (!wiiu_update_name_allowed(file->name))
			{
				say(reason, reason_size, "line %u: a release may not own a file named \"%s\"", line_number, file->name);
				return -1;
			}
			for (index = 0; index < manifest->file_count; index++)
			{
				if (!strcmp(manifest->files[index].name, file->name))
				{
					say(reason, reason_size, "line %u: %s is named twice", line_number, file->name);
					return -1;
				}
			}
			if (parse_number(f.text[2], f.length[2], WIIU_UPDATE_FILE_MOST, &file->size) != 0 || !file->size)
			{
				say(reason, reason_size, "line %u: the size of %s is not 1 to %u", line_number, file->name, (unsigned)WIIU_UPDATE_FILE_MOST);
				return -1;
			}
			if (parse_hex(f.text[3], f.length[3], file->sha256, sizeof(file->sha256)) != 0)
			{
				say(reason, reason_size, "line %u: the SHA-256 of %s is not 64 lowercase hexadecimal digits", line_number, file->name);
				return -1;
			}
			total += file->size;
			if (total > WIIU_UPDATE_TOTAL_MOST)
			{
				say(reason, reason_size, "the release is more than %u MB", (unsigned)(WIIU_UPDATE_TOTAL_MOST >> 20));
				return -1;
			}
			if (!strcmp(file->name, WIIU_UPDATE_BUNDLE))
				have_bundle = 1;
			manifest->file_count++;
		}
	}
	if (line_number < 4)
	{
		say(reason, reason_size, "the manifest ends after %u lines", line_number);
		return -1;
	}
	if (!have_bundle)
	{
		say(reason, reason_size, "the release has no %s", WIIU_UPDATE_BUNDLE);
		return -1;
	}
	return 0;
}

int wiiu_update_key_from_hex(const char *hex, uint8_t key[32])
{
	return hex ? parse_hex(hex, strlen(hex), key, 32) : -1;
}

int wiiu_update_signature_valid(const uint8_t key[32], const void *signature, size_t signature_size, const void *message,
	size_t message_size)
{
	uint8_t copy[64];

	if (!key || !signature || signature_size != sizeof(copy) || (!message && message_size))
		return 0;
	memcpy(copy, signature, sizeof(copy));
	return crypto_ed25519_check(copy, key, message, message_size) == 0;
}

/* ---------- the journal's text */

static const char *const state_names[] = { "idle", "downloading", "staged", "swapping", "trial", "good", "restoring", "restored" };

const char *wiiu_update_state_name(wiiu_update_state state)
{
	return (unsigned)state < sizeof(state_names) / sizeof(*state_names) ? state_names[state] : "?";
}

/* appends to the text, never past its end; the length so far, or capacity once it no longer fits */
static size_t put(char *out, size_t capacity, size_t used, const char *format, ...) __attribute__((format(printf, 4, 5)));

static size_t put(char *out, size_t capacity, size_t used, const char *format, ...)
{
	va_list arguments;
	int written;

	if (used >= capacity)
		return capacity;
	va_start(arguments, format);
	written = vsnprintf(out + used, capacity - used, format, arguments);
	va_end(arguments);
	if (written < 0 || (size_t)written >= capacity - used)
		return capacity;
	return used + (size_t)written;
}

size_t wiiu_update_journal_format(const wiiu_update_journal *journal, char *out, size_t capacity)
{
	char hex[65];
	size_t used = 0;
	uint32_t i;

	if (!capacity || journal->entry_count > WIIU_UPDATE_FILES)
		return 0;
	used = put(out, capacity, used, "halo-wiiu-journal 1\nsequence %u\nstate %s\nto %u\nfrom %u\nstarts %u\nrejected %u\nnotice %u\n",
		(unsigned)journal->sequence, wiiu_update_state_name(journal->state), (unsigned)journal->to_build, (unsigned)journal->from_build,
		(unsigned)journal->starts, (unsigned)journal->rejected, (unsigned)journal->notice);
	if (journal->has_manifest)
	{
		wiiu_update_hex(journal->manifest_sha256, 32, hex);
		used = put(out, capacity, used, "manifest %s\n", hex);
	}
	else
		used = put(out, capacity, used, "manifest -\n");
	for (i = 0; i < journal->entry_count; i++)
	{
		const wiiu_update_entry *entry = &journal->entries[i];

		wiiu_update_hex(entry->sha256, 32, hex);
		used = put(out, capacity, used, "entry %u %s %u %s\n", entry->had ? 1u : 0u, entry->name, (unsigned)entry->size, hex);
	}
	if (used >= capacity)
		return 0;
	used = put(out, capacity, used, "check %08x\n", (unsigned)wiiu_update_crc32(out, used));
	return used >= capacity ? 0 : used;
}

int wiiu_update_journal_parse(const void *data, size_t size, wiiu_update_journal *journal)
{
	static const char *const numbers[] = { "sequence", NULL, "to", "from", "starts", "rejected", "notice" };
	const char *text = data;
	size_t at = 0, check_at = 0;
	unsigned line_number = 0;
	int checked = 0;
	wiiu_update_journal result;

	memset(&result, 0, sizeof(result));
	if (!text || !size || size > 4096 || text[size - 1] != '\n')
		return -1;
	while (at < size)
	{
		const char *line = text + at;
		const char *end = memchr(line, '\n', size - at);
		const size_t length = (size_t)(end - line);
		fields f;

		check_at = at;
		at += length + 1;
		line_number++;
		if (checked || split(line, length, &f) != 0)
			return -1;
		if (line_number == 1)
		{
			if (f.count != 2 || !field_is(&f, 0, "halo-wiiu-journal") || !field_is(&f, 1, "1"))
				return -1;
		}
		else if (line_number <= 8)
		{
			/* sequence, state, to, from, starts, rejected, notice: each once, in this order */
			const unsigned index = line_number - 2;

			if (f.count != 2)
				return -1;
			if (index == 1)
			{
				unsigned state;

				if (!field_is(&f, 0, "state"))
					return -1;
				for (state = 0; state < sizeof(state_names) / sizeof(*state_names); state++)
					if (field_is(&f, 1, state_names[state]))
						break;
				if (state == sizeof(state_names) / sizeof(*state_names))
					return -1;
				result.state = (wiiu_update_state)state;
			}
			else
			{
				uint32_t *targets[] = { &result.sequence, NULL, &result.to_build, &result.from_build, &result.starts, &result.rejected,
					&result.notice };

				if (!field_is(&f, 0, numbers[index]) || parse_number(f.text[1], f.length[1], 0xFFFFFFFFu, targets[index]) != 0)
					return -1;
			}
		}
		else if (line_number == 9)
		{
			if (f.count != 2 || !field_is(&f, 0, "manifest"))
				return -1;
			if (!field_is(&f, 1, "-"))
			{
				if (parse_hex(f.text[1], f.length[1], result.manifest_sha256, 32) != 0)
					return -1;
				result.has_manifest = 1;
			}
		}
		else if (field_is(&f, 0, "entry"))
		{
			wiiu_update_entry *entry;

			if (f.count != 5 || result.entry_count == WIIU_UPDATE_FILES || f.length[2] >= WIIU_UPDATE_NAME_BYTES)
				return -1;
			entry = &result.entries[result.entry_count];
			if (!field_is(&f, 1, "0") && !field_is(&f, 1, "1"))
				return -1;
			entry->had = field_is(&f, 1, "1");
			memcpy(entry->name, f.text[2], f.length[2]);
			entry->name[f.length[2]] = 0;
			if (!wiiu_update_name_allowed(entry->name) || parse_number(f.text[3], f.length[3], WIIU_UPDATE_FILE_MOST, &entry->size) != 0 ||
				parse_hex(f.text[4], f.length[4], entry->sha256, 32) != 0)
			{
				return -1;
			}
			result.entry_count++;
		}
		else if (field_is(&f, 0, "check"))
		{
			uint8_t crc[4];

			if (f.count != 2 || parse_hex(f.text[1], f.length[1], crc, 4) != 0 ||
				((uint32_t)crc[0] << 24 | (uint32_t)crc[1] << 16 | (uint32_t)crc[2] << 8 | crc[3]) != wiiu_update_crc32(text, check_at))
			{
				return -1;
			}
			checked = 1;
		}
		else
			return -1;
	}
	if (!checked || line_number < 10)
		return -1;
	*journal = result;
	return 0;
}

void wiiu_update_journal_entries(wiiu_update_journal *journal, const wiiu_update_manifest *manifest, const uint8_t *unchanged)
{
	uint32_t i;
	int pass;

	journal->entry_count = 0;
	/* (the bundle last: its build is what tells the next start how far a swap got) */
	for (pass = 0; pass < 2; pass++)
	{
		for (i = 0; i < manifest->file_count; i++)
		{
			const wiiu_update_file *file = &manifest->files[i];
			const int bundle = !strcmp(file->name, WIIU_UPDATE_BUNDLE);
			wiiu_update_entry *entry;

			if (bundle != (pass == 1) || (!bundle && unchanged && unchanged[i]))
				continue;
			entry = &journal->entries[journal->entry_count++];
			memset(entry, 0, sizeof(*entry));
			snprintf(entry->name, sizeof(entry->name), "%s", file->name);
			entry->size = file->size;
			memcpy(entry->sha256, file->sha256, sizeof(entry->sha256));
		}
	}
}

/* ---------- the journal's files */

int wiiu_update_path(const wiiu_update_fs *fs, const char *name, enum wiiu_update_place place, char *out, size_t capacity)
{
	static const char *const suffixes[] = { "", ".part", ".new", ".old", ".bad" };
	int written;

	if ((unsigned)place > WIIU_UPDATE_BAD)
		return -1;
	if (place == WIIU_UPDATE_CURRENT)
		written = snprintf(out, capacity, "%s/%s", fs->root, name);
	else
		written = snprintf(out, capacity, "%s/" WIIU_UPDATE_FOLDER "/%s%s", fs->root, name, suffixes[place]);
	return written > 0 && (size_t)written < capacity ? 0 : -1;
}

static int journal_slot_path(const wiiu_update_fs *fs, uint32_t sequence, char *out, size_t capacity)
{
	const int written = snprintf(out, capacity, "%s/" WIIU_UPDATE_FOLDER "/%s", fs->root, sequence & 1u ? "state.txt" : "state2.txt");

	return written > 0 && (size_t)written < capacity ? 0 : -1;
}

int wiiu_update_journal_load(const wiiu_update_fs *fs, wiiu_update_journal *journal)
{
	char path[WIIU_UPDATE_PATH_BYTES];
	char text[4096];
	wiiu_update_journal copies[2];
	int valid[2] = { 0, 0 };
	int slot;

	memset(journal, 0, sizeof(*journal));
	for (slot = 0; slot < 2; slot++)
	{
		long size;

		if (journal_slot_path(fs, (uint32_t)slot, path, sizeof(path)) != 0)
			continue;
		size = fs->read(fs->context, path, text, sizeof(text));
		/* (a copy in the wrong slot is one this code did not write) */
		valid[slot] = size > 0 && wiiu_update_journal_parse(text, (size_t)size, &copies[slot]) == 0 &&
			(copies[slot].sequence & 1u) == (uint32_t)slot;
	}
	if (!valid[0] && !valid[1])
		return 0;
	slot = valid[0] && valid[1] ? (copies[1].sequence > copies[0].sequence ? 1 : 0) : valid[1] ? 1 : 0;
	*journal = copies[slot];
	return 1;
}

int wiiu_update_journal_save(const wiiu_update_fs *fs, wiiu_update_journal *journal)
{
	char path[WIIU_UPDATE_PATH_BYTES];
	char text[4096];
	size_t size;

	journal->sequence++;
	size = wiiu_update_journal_format(journal, text, sizeof(text));
	if (!size || journal_slot_path(fs, journal->sequence, path, sizeof(path)) != 0)
		return -1;
	return fs->write(fs->context, path, text, size);
}

/* ---------- the swap's steps */

/* A forward swap sets the current file aside as .old and brings .new in; a restore sets the current (the new build's) aside as
.bad and brings .old back. In a restore every entry has a current file (the forward swap brought one in), and brings one back
only if the folder had one before. */
typedef struct swap_paths
{
	char current[WIIU_UPDATE_PATH_BYTES];
	char incoming[WIIU_UPDATE_PATH_BYTES];
	char aside[WIIU_UPDATE_PATH_BYTES];
	int had;
	int brings;
} swap_paths;

static int swap_paths_of(const wiiu_update_fs *fs, const wiiu_update_entry *entry, int restore, swap_paths *paths)
{
	paths->had = restore ? 1 : entry->had != 0;
	paths->brings = restore ? entry->had != 0 : 1;
	if (wiiu_update_path(fs, entry->name, WIIU_UPDATE_CURRENT, paths->current, sizeof(paths->current)) != 0 ||
		wiiu_update_path(fs, entry->name, restore ? WIIU_UPDATE_OLD : WIIU_UPDATE_NEW, paths->incoming, sizeof(paths->incoming)) != 0 ||
		wiiu_update_path(fs, entry->name, restore ? WIIU_UPDATE_BAD : WIIU_UPDATE_OLD, paths->aside, sizeof(paths->aside)) != 0)
	{
		return -1;
	}
	return 0;
}

static wiiu_update_step step_of(const wiiu_update_fs *fs, const swap_paths *paths)
{
	const int current = fs->exists(fs->context, paths->current) != 0;
	const int incoming = paths->brings && fs->exists(fs->context, paths->incoming) != 0;
	const int aside = paths->had && fs->exists(fs->context, paths->aside) != 0;

	if (paths->had && paths->brings)
	{
		if (current && incoming && !aside)
			return WIIU_STEP_NOT_STARTED;
		if (!current && incoming && aside)
			return WIIU_STEP_HALF;
		if (current && !incoming && aside)
			return WIIU_STEP_DONE;
	}
	else if (paths->brings)
	{
		if (!current && incoming)
			return WIIU_STEP_NOT_STARTED;
		if (current && !incoming)
			return WIIU_STEP_DONE;
	}
	else if (paths->had)
	{
		if (current && !aside)
			return WIIU_STEP_NOT_STARTED;
		if (!current && aside)
			return WIIU_STEP_DONE;
	}
	return WIIU_STEP_INCONSISTENT;
}

wiiu_update_step wiiu_update_step_of(const wiiu_update_fs *fs, const wiiu_update_entry *entry, int restore)
{
	swap_paths paths;

	if (swap_paths_of(fs, entry, restore, &paths) != 0)
		return WIIU_STEP_INCONSISTENT;
	return step_of(fs, &paths);
}

int wiiu_update_swap_finish(const wiiu_update_fs *fs, const wiiu_update_journal *journal, int restore)
{
	uint32_t i;

	for (i = 0; i < journal->entry_count; i++)
	{
		swap_paths paths;
		wiiu_update_step step;

		if (swap_paths_of(fs, &journal->entries[i], restore, &paths) != 0)
			return -1;
		step = step_of(fs, &paths);
		if (step == WIIU_STEP_INCONSISTENT)
			return -1;
		if (step == WIIU_STEP_NOT_STARTED && paths.had && fs->rename(fs->context, paths.current, paths.aside) != 0)
			return -1;
		if (step != WIIU_STEP_DONE && paths.brings && fs->rename(fs->context, paths.incoming, paths.current) != 0)
			return -1;
	}
	return 0;
}

int wiiu_update_swap_undo(const wiiu_update_fs *fs, const wiiu_update_journal *journal, int restore)
{
	uint32_t i;

	for (i = journal->entry_count; i-- > 0;)
	{
		swap_paths paths;
		wiiu_update_step step;

		if (swap_paths_of(fs, &journal->entries[i], restore, &paths) != 0)
			return -1;
		step = step_of(fs, &paths);
		if (step == WIIU_STEP_INCONSISTENT)
			return -1;
		if (step == WIIU_STEP_DONE && paths.brings && fs->rename(fs->context, paths.current, paths.incoming) != 0)
			return -1;
		if (step != WIIU_STEP_NOT_STARTED && paths.had && fs->rename(fs->context, paths.aside, paths.current) != 0)
			return -1;
	}
	return 0;
}

/* ---------- the next start */

static void tell(const wiiu_update_system *system, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void tell(const wiiu_update_system *system, const char *format, ...)
{
	char text[240];
	va_list arguments;

	if (!system->log)
		return;
	va_start(arguments, format);
	vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	system->log(system->context, text);
}

/* deletes the entries' files in a place, if they are there (a file that cannot be deleted is left: the next start tries again) */
static void remove_place(const wiiu_update_fs *fs, const wiiu_update_journal *journal, enum wiiu_update_place place)
{
	char path[WIIU_UPDATE_PATH_BYTES];
	uint32_t i;

	for (i = 0; i < journal->entry_count; i++)
		if (wiiu_update_path(fs, journal->entries[i].name, place, path, sizeof(path)) == 0 && fs->exists(fs->context, path))
			fs->remove(fs->context, path);
}

/* the journal back to idle, keeping what it remembers of rejected builds and notices */
static void go_idle(wiiu_update_journal *journal)
{
	journal->state = WIIU_UPDATE_IDLE;
	journal->to_build = journal->from_build = journal->starts = 0;
	journal->has_manifest = 0;
	memset(journal->manifest_sha256, 0, sizeof(journal->manifest_sha256));
	journal->entry_count = 0;
}

/* The trial failed: the old build's files are put back (the running bundle unmounted first), and the new build is rejected once
the old one runs again (the journal stays RESTORING until then, so that a cut on the way is finished at the next start). */
static int restore_now(const wiiu_update_fs *fs, wiiu_update_journal *journal, const wiiu_update_system *system)
{
	if (!system->unmount || system->unmount(system->context) != 0)
	{
		tell(system, "Update: the running bundle could not be unmounted: build %u's files are not put back yet", (unsigned)journal->from_build);
		return -1;
	}
	if (wiiu_update_swap_finish(fs, journal, 1) != 0)
	{
		tell(system, "Update: build %u's files could not all be put back (the next start tries again)", (unsigned)journal->from_build);
		return -1;
	}
	if (fs->flush)
		fs->flush(fs->context);
	tell(system, "Update: build %u's files are back in place of build %u's: starting it", (unsigned)journal->from_build, (unsigned)journal->to_build);
	return WIIU_UPDATE_RELAUNCH;
}

/* the restore's last step: the set-aside build's files deleted (those that cannot be are tried again at the next start) */
static int restored(const wiiu_update_fs *fs, wiiu_update_journal *journal)
{
	remove_place(fs, journal, WIIU_UPDATE_BAD);
	go_idle(journal);
	return wiiu_update_journal_save(fs, journal) == 0 ? WIIU_UPDATE_CONTINUE : -1;
}

int wiiu_update_recover(const wiiu_update_fs *fs, wiiu_update_journal *journal, const wiiu_update_system *system)
{
	const uint32_t running = system->running_build;

	switch (journal->state)
	{
	case WIIU_UPDATE_IDLE:
	case WIIU_UPDATE_DOWNLOADING:
	case WIIU_UPDATE_STAGED:
		return WIIU_UPDATE_CONTINUE;

	case WIIU_UPDATE_SWAPPING:
		/* halo.wuhb is renamed last: the new build running means the files before it are in place, the old one that it never got there */
		if (running == journal->to_build)
		{
			if (wiiu_update_swap_finish(fs, journal, 0) != 0)
			{
				tell(system, "Update: the swap to build %u was cut off and cannot be finished: the files are left as they are", (unsigned)running);
				return -1;
			}
			tell(system, "Update: the swap to build %u was cut off after the bundle: finished; its trial begins", (unsigned)running);
			journal->state = WIIU_UPDATE_TRIAL;
			journal->starts = 1;
			return wiiu_update_journal_save(fs, journal) == 0 ? WIIU_UPDATE_CONTINUE : -1;
		}
		if (running == journal->from_build)
		{
			if (wiiu_update_swap_undo(fs, journal, 0) != 0)
			{
				tell(system, "Update: the swap from build %u was cut off and cannot be undone: the files are left as they are", (unsigned)running);
				return -1;
			}
			tell(system, "Update: the swap to build %u was cut off before the bundle: undone; it stays downloaded", (unsigned)journal->to_build);
			journal->state = WIIU_UPDATE_STAGED;
			return wiiu_update_journal_save(fs, journal) == 0 ? WIIU_UPDATE_CONTINUE : -1;
		}
		tell(system, "Update: the journal swaps build %u to %u, and build %u is running: the files are left as they are", (unsigned)journal->from_build,
			(unsigned)journal->to_build, (unsigned)running);
		go_idle(journal);
		return wiiu_update_journal_save(fs, journal) == 0 ? WIIU_UPDATE_CONTINUE : -1;

	case WIIU_UPDATE_TRIAL:
		if (running != journal->to_build)
		{
			tell(system, "Update: build %u was on trial and build %u is running: the trial is dropped", (unsigned)journal->to_build, (unsigned)running);
			go_idle(journal);
			return wiiu_update_journal_save(fs, journal) == 0 ? WIIU_UPDATE_CONTINUE : -1;
		}
		/* (a crash note counts once the new build has run: the one at the trial's first start is the old build's) */
		if (journal->starts >= 2 || (journal->starts >= 1 && system->crash_note))
		{
			tell(system, "Update: build %u %s: build %u's files are put back", (unsigned)running,
				journal->starts >= 2 ? "has started twice without reaching the main menu" : "crashed before it reached the main menu",
				(unsigned)journal->from_build);
			if (!system->unmount)
			{
				tell(system, "Update: the bundle cannot be unmounted on this console: build %u stays", (unsigned)running);
				journal->starts++;
				return wiiu_update_journal_save(fs, journal) == 0 ? WIIU_UPDATE_CONTINUE : -1;
			}
			journal->state = WIIU_UPDATE_RESTORING;
			if (wiiu_update_journal_save(fs, journal) != 0)
				return -1;
			return restore_now(fs, journal, system);
		}
		journal->starts++;
		tell(system, "Update: build %u is on trial, start %u (it is kept once it reaches the main menu)", (unsigned)running, (unsigned)journal->starts);
		return wiiu_update_journal_save(fs, journal) == 0 ? WIIU_UPDATE_CONTINUE : -1;

	case WIIU_UPDATE_GOOD:
		if (running == journal->to_build)
		{
			remove_place(fs, journal, WIIU_UPDATE_OLD);
			remove_place(fs, journal, WIIU_UPDATE_BAD);
			tell(system, "Update: build %u is kept; build %u's files are deleted", (unsigned)running, (unsigned)journal->from_build);
		}
		else
			tell(system, "Update: build %u was kept and build %u is running: its files are left as they are", (unsigned)journal->to_build,
				(unsigned)running);
		go_idle(journal);
		return wiiu_update_journal_save(fs, journal) == 0 ? WIIU_UPDATE_CONTINUE : -1;

	case WIIU_UPDATE_RESTORING:
		if (running == journal->from_build)
		{
			/* (the restore is recorded as done before the new build's files are deleted: once they are gone its steps cannot be read) */
			if (wiiu_update_swap_finish(fs, journal, 1) != 0)
			{
				tell(system, "Update: build %u's files cannot all be put back: the files are left as they are", (unsigned)running);
				return -1;
			}
			journal->state = WIIU_UPDATE_RESTORED;
			journal->rejected = journal->to_build;
			journal->notice = journal->to_build;
			if (wiiu_update_journal_save(fs, journal) != 0)
				return -1;
			tell(system, "Update: build %u is back; build %u is set aside and not offered again", (unsigned)running, (unsigned)journal->to_build);
			return restored(fs, journal);
		}
		if (running == journal->to_build)
			return restore_now(fs, journal, system);
		tell(system, "Update: the journal puts build %u back, and build %u is running: the files are left as they are", (unsigned)journal->from_build,
			(unsigned)running);
		go_idle(journal);
		return wiiu_update_journal_save(fs, journal) == 0 ? WIIU_UPDATE_CONTINUE : -1;

	case WIIU_UPDATE_RESTORED:
		return restored(fs, journal);
	}
	return -1;
}

int wiiu_update_commit(const wiiu_update_fs *fs, wiiu_update_journal *journal, const wiiu_update_system *system)
{
	char path[WIIU_UPDATE_PATH_BYTES];
	uint32_t i;

	if (journal->state != WIIU_UPDATE_STAGED || !journal->entry_count ||
		strcmp(journal->entries[journal->entry_count - 1].name, WIIU_UPDATE_BUNDLE))
	{
		tell(system, "Update: nothing staged to put in place (the journal is %s)", wiiu_update_state_name(journal->state));
		return 0;
	}
	for (i = 0; i < journal->entry_count; i++)
	{
		const wiiu_update_entry *entry = &journal->entries[i];
		int place;

		if (wiiu_update_path(fs, entry->name, WIIU_UPDATE_NEW, path, sizeof(path)) != 0 || !fs->exists(fs->context, path))
		{
			tell(system, "Update: %s is not staged", entry->name);
			return 0;
		}
		/* (a file of an earlier swap in a place this one uses would read as a step of it) */
		for (place = WIIU_UPDATE_OLD; place <= WIIU_UPDATE_BAD; place++)
		{
			if (wiiu_update_path(fs, entry->name, (enum wiiu_update_place)place, path, sizeof(path)) != 0)
				return 0;
			if (fs->exists(fs->context, path) && (fs->remove(fs->context, path) != 0 || fs->exists(fs->context, path)))
			{
				tell(system, "Update: %s is in the way and cannot be deleted", path);
				return 0;
			}
		}
	}
	for (i = 0; i < journal->entry_count; i++)
	{
		wiiu_update_entry *entry = &journal->entries[i];

		entry->had = wiiu_update_path(fs, entry->name, WIIU_UPDATE_CURRENT, path, sizeof(path)) == 0 && fs->exists(fs->context, path);
	}
	journal->state = WIIU_UPDATE_SWAPPING;
	journal->from_build = system->running_build;
	journal->starts = 0;
	if (wiiu_update_journal_save(fs, journal) != 0)
	{
		tell(system, "Update: the journal cannot be written: nothing is changed");
		journal->state = WIIU_UPDATE_STAGED;
		return 0;
	}
	if (!system->unmount || system->unmount(system->context) != 0)
	{
		tell(system, "Update: the running bundle cannot be unmounted: nothing is changed");
		journal->state = WIIU_UPDATE_STAGED;
		wiiu_update_journal_save(fs, journal);
		return 0;
	}
	if (wiiu_update_swap_finish(fs, journal, 0) != 0)
	{
		if (wiiu_update_swap_undo(fs, journal, 0) == 0)
		{
			tell(system, "Update: a file could not be put in place: everything is put back as it was");
			journal->state = WIIU_UPDATE_STAGED;
			wiiu_update_journal_save(fs, journal);
			return 0;
		}
		tell(system, "Update: a file could not be put in place, nor put back: the next start sorts it out");
		return -1;
	}
	if (fs->flush)
		fs->flush(fs->context);
	journal->state = WIIU_UPDATE_TRIAL;
	journal->starts = 0;
	/* (a journal that cannot be written now still says SWAPPING, which the new build's start takes as done) */
	if (wiiu_update_journal_save(fs, journal) != 0)
		tell(system, "Update: build %u is in place, and its journal could not be written (the next start finishes it)", (unsigned)journal->to_build);
	else
		tell(system, "Update: build %u is in place of build %u: its trial begins at its start", (unsigned)journal->to_build, (unsigned)journal->from_build);
	return 1;
}

int wiiu_update_mark_good(const wiiu_update_fs *fs, wiiu_update_journal *journal)
{
	if (journal->state != WIIU_UPDATE_TRIAL)
		return 0;
	journal->state = WIIU_UPDATE_GOOD;
	return wiiu_update_journal_save(fs, journal);
}

int wiiu_update_discard(const wiiu_update_fs *fs, wiiu_update_journal *journal)
{
	if (journal->state != WIIU_UPDATE_IDLE && journal->state != WIIU_UPDATE_DOWNLOADING && journal->state != WIIU_UPDATE_STAGED)
		return -1;
	remove_place(fs, journal, WIIU_UPDATE_PART);
	remove_place(fs, journal, WIIU_UPDATE_NEW);
	go_idle(journal);
	return wiiu_update_journal_save(fs, journal);
}
