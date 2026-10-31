/*
The Wii U self-updater's decisions (port/wiiu/src/wiiu_update_core.c) on a desktop: the manifest's checks, the signature, the
journal's text and its two copies, and the swap and the trial with the power cut at every step of them in turn (and again at
every step of the start that resolves them). Run by tools/test_wiiu_update.py, natively and big-endian under qemu-ppc.

    test_update                                   every test
    test_update verify KEYHEX MANIFEST SIGNATURE   "valid" or "invalid" (an OpenSSL signature checked as the console checks it)
    test_update parse MANIFEST                    "ok build N files N" or "refused: why"
*/

#include "wiiu_update_core.h"

#include "monocypher-ed25519.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned checks, failures;

#define CHECK(condition) check((condition) != 0, #condition, __FILE__, __LINE__)

static int check(int ok, const char *what, const char *file, int line)
{
	checks++;
	if (!ok)
	{
		failures++;
		fprintf(stderr, "%s:%d: failed: %s\n", file, line, what);
	}
	return ok;
}

static char *read_whole(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	char *data;
	long length;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (length < 0)
	{
		fclose(file);
		return NULL;
	}
	data = malloc((size_t)length + 1);
	if (data && fread(data, 1, (size_t)length, file) != (size_t)length)
	{
		free(data);
		data = NULL;
	}
	fclose(file);
	if (data)
	{
		data[length] = 0;
		*size = (size_t)length;
	}
	return data;
}

/* ---------- the manifest */

static const char *const hash_a = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
static const char *const hash_b = "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";

static int parses(const char *text, char *reason, size_t capacity)
{
	wiiu_update_manifest manifest;

	return wiiu_update_manifest_parse(text, strlen(text), &manifest, reason, capacity) == 0;
}

/* the good manifest with one line replaced (or a line added at its end) */
static void manifest_with(char *out, size_t capacity, int line, const char *replacement)
{
	const char *lines[] = { "halo-wiiu-manifest 1", "build 124", "tag wiiu-124", "maps c1ea0000", NULL, NULL, NULL };
	char file_a[160], file_b[160];
	size_t used = 0;
	int i;

	snprintf(file_a, sizeof(file_a), "file shaders.pack 2401600 %s", hash_a);
	snprintf(file_b, sizeof(file_b), "file halo.wuhb 14934416 %s", hash_b);
	lines[4] = file_a;
	lines[5] = file_b;
	out[0] = 0;
	for (i = 0; i < 7; i++)
	{
		const char *text = i == line ? replacement : lines[i];

		if (!text)
			continue;
		used += (size_t)snprintf(out + used, capacity - used, "%s\n", text);
	}
}

static void test_manifest(void)
{
	char text[8192], reason[200];
	wiiu_update_manifest manifest;
	static const char *const refused_lines[][2] = {
		/* line, replacement */
		{ "0", "halo-wiiu-manifest 2" },
		{ "0", "halo-wiiu-manifest  1" },
		{ "1", "build 0" },
		{ "1", "build 0124" },
		{ "1", "build 1000000000" },
		{ "1", "build 12a" },
		{ "1", "build -5" },
		{ "1", "build 124 " },
		{ "2", "tag wiiu-125" },
		{ "2", "tag build-124" },
		{ "3", "maps C1EA0000" },
		{ "3", "maps c1ea0000c1ea0000c1ea0000c1ea0000c" },
		{ "3", "maps c1ea_0000" },
		{ "4", "file shaders.pack 2401600" },
		{ "4", "file ../shaders.pack 2401600 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" },
		{ "4", "file data/shaders.pack 2401600 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" },
		{ "4", "file config.toml 10 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" },
		{ "4", "file network.key 32 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" },
		{ "4", "file log.txt 10 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" },
		{ "4", "file halo.wuhb.new 10 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" },
		{ "4", "file .pack 10 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" },
		{ "4", "file Shaders.pack 10 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" },
		{ "4", "file shaders.pack 0 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" },
		{ "4", "file shaders.pack 268435457 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" },
		{ "4", "file shaders.pack 10 0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef" },
		{ "4", "file shaders.pack 10 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde" },
		{ "4", "file halo.wuhb 10 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" },
		{ "4", "extra line" },
		{ "4", "" },
		{ "6", "file other.wuhb 10 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" },
		{ "6", "file maps 10 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" },
	};
	unsigned i;

	manifest_with(text, sizeof(text), -1, NULL);
	CHECK(wiiu_update_manifest_parse(text, strlen(text), &manifest, reason, sizeof(reason)) == 0);
	CHECK(manifest.build == 124 && !strcmp(manifest.maps, "c1ea0000") && manifest.file_count == 2);
	CHECK(!strcmp(manifest.files[0].name, "shaders.pack") && manifest.files[0].size == 2401600 && manifest.files[0].sha256[0] == 0x01 &&
		manifest.files[0].sha256[31] == 0xef);
	CHECK(!strcmp(manifest.files[1].name, "halo.wuhb") && manifest.files[1].size == 14934416 && manifest.files[1].sha256[0] == 0xfe);
	for (i = 0; i < sizeof(refused_lines) / sizeof(*refused_lines); i++)
	{
		manifest_with(text, sizeof(text), atoi(refused_lines[i][0]), refused_lines[i][1]);
		reason[0] = 0;
		if (!CHECK(!parses(text, reason, sizeof(reason)) && reason[0]))
			fprintf(stderr, "  accepted: %s\n", refused_lines[i][1]);
	}
	/* a good manifest broken as a whole: no end of line, a carriage return, a NUL, a control character, too long, nothing */
	manifest_with(text, sizeof(text), -1, NULL);
	CHECK(wiiu_update_manifest_parse(text, strlen(text) - 1, &manifest, reason, sizeof(reason)) != 0);
	text[10] = '\r';
	CHECK(!parses(text, reason, sizeof(reason)));
	manifest_with(text, sizeof(text), -1, NULL);
	text[12] = 0x01;
	CHECK(!parses(text, reason, sizeof(reason)));
	manifest_with(text, sizeof(text), -1, NULL);
	CHECK(wiiu_update_manifest_parse(text, strlen(text) + 1, &manifest, reason, sizeof(reason)) != 0);
	CHECK(wiiu_update_manifest_parse("", 0, &manifest, reason, sizeof(reason)) != 0);
	{
		static char long_text[WIIU_UPDATE_MANIFEST_MOST + 64];

		size_t length;

		manifest_with(long_text, sizeof(long_text), -1, NULL);
		length = strlen(long_text);
		/* (one byte over the bound, in lines a manifest may not have) */
		memset(long_text + length, '\n', WIIU_UPDATE_MANIFEST_MOST + 1 - length);
		long_text[WIIU_UPDATE_MANIFEST_MOST + 1] = 0;
		CHECK(wiiu_update_manifest_parse(long_text, strlen(long_text), &manifest, reason, sizeof(reason)) != 0);
	}
	/* the same file twice, nine files, a release over 512 MB, a release without the bundle */
	snprintf(text, sizeof(text), "halo-wiiu-manifest 1\nbuild 5\ntag wiiu-5\nmaps c1ea0000\nfile a.pack 1 %s\nfile a.pack 1 %s\nfile halo.wuhb 1 %s\n",
		hash_a, hash_a, hash_b);
	CHECK(!parses(text, reason, sizeof(reason)) && strstr(reason, "twice"));
	{
		size_t used = (size_t)snprintf(text, sizeof(text), "halo-wiiu-manifest 1\nbuild 5\ntag wiiu-5\nmaps c1ea0000\n");

		for (i = 0; i < 8; i++)
			used += (size_t)snprintf(text + used, sizeof(text) - used, "file p%u.pack 1 %s\n", i, hash_a);
		snprintf(text + used, sizeof(text) - used, "file halo.wuhb 1 %s\n", hash_b);
		CHECK(!parses(text, reason, sizeof(reason)) && strstr(reason, "more than 8"));
	}
	snprintf(text, sizeof(text), "halo-wiiu-manifest 1\nbuild 5\ntag wiiu-5\nmaps c1ea0000\nfile a.pack 268435456 %s\nfile b.pack 268435456 %s\n"
		"file halo.wuhb 1 %s\n", hash_a, hash_a, hash_b);
	CHECK(!parses(text, reason, sizeof(reason)) && strstr(reason, "512 MB"));
	snprintf(text, sizeof(text), "halo-wiiu-manifest 1\nbuild 5\ntag wiiu-5\nmaps c1ea0000\nfile a.pack 1 %s\n", hash_a);
	CHECK(!parses(text, reason, sizeof(reason)) && strstr(reason, "halo.wuhb"));
	snprintf(text, sizeof(text), "halo-wiiu-manifest 1\nbuild 5\ntag wiiu-5\n");
	CHECK(!parses(text, reason, sizeof(reason)));
	snprintf(text, sizeof(text), "halo-wiiu-manifest 1\nbuild 5\ntag wiiu-5\nmaps x\nfile halo.wuhb 1 %s\n", hash_b);
	CHECK(parses(text, reason, sizeof(reason)));
}

static void test_names(void)
{
	static const char *const allowed[] = { "halo.wuhb", "shaders.pack", "brokers.txt", "hud-2.pack", "a_b.pack", "0.pack" };
	static const char *const refused[] = { "", ".", "..", "../halo.wuhb", "/halo.wuhb", "data", "data/maps", "maps", "menus", "saves", "update",
		"config.toml", "network.key", "log.txt", "log-previous.txt", "crash.txt", "blackbox.txt", "exit-trail.txt", "debug_server.txt",
		"game_core.txt", "overlay.txt", "shader-misses-1.txt", "halo.rpx", "other.wuhb", "halo.wuhb.new", "halo.wuhb.old", "halo.wuhb.part",
		"shaders.pack.bad", "shaders..pack", "shaders.", ".pack", "-x.pack", "Halo.wuhb", "HALO.WUHB", "sh aders.pack", "x\\y.pack",
		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.pack" };
	unsigned i;

	for (i = 0; i < sizeof(allowed) / sizeof(*allowed); i++)
		if (!CHECK(wiiu_update_name_allowed(allowed[i])))
			fprintf(stderr, "  refused: %s\n", allowed[i]);
	for (i = 0; i < sizeof(refused) / sizeof(*refused); i++)
		if (!CHECK(!wiiu_update_name_allowed(refused[i])))
			fprintf(stderr, "  allowed: %s\n", refused[i]);
	CHECK(!wiiu_update_name_allowed(NULL));
}

/* ---------- the signature */

static void test_signature(void)
{
	uint8_t seed[32], secret[64], public_key[32], signature[64], key[32];
	char manifest[2048], hex[65];
	unsigned i;

	for (i = 0; i < 32; i++)
		seed[i] = (uint8_t)(i * 7 + 1);
	crypto_ed25519_key_pair(secret, public_key, seed);
	manifest_with(manifest, sizeof(manifest), -1, NULL);
	crypto_ed25519_sign(signature, secret, (const uint8_t *)manifest, strlen(manifest));
	wiiu_update_hex(public_key, 32, hex);
	CHECK(wiiu_update_key_from_hex(hex, key) == 0 && !memcmp(key, public_key, 32));
	CHECK(wiiu_update_signature_valid(key, signature, 64, manifest, strlen(manifest)));
	/* a byte of the manifest changed, of the signature, the signature's length, another key */
	manifest[20] ^= 1;
	CHECK(!wiiu_update_signature_valid(key, signature, 64, manifest, strlen(manifest)));
	manifest[20] ^= 1;
	signature[5] ^= 0x10;
	CHECK(!wiiu_update_signature_valid(key, signature, 64, manifest, strlen(manifest)));
	signature[5] ^= 0x10;
	CHECK(!wiiu_update_signature_valid(key, signature, 63, manifest, strlen(manifest)));
	CHECK(!wiiu_update_signature_valid(key, signature, 65, manifest, strlen(manifest)));
	CHECK(!wiiu_update_signature_valid(key, signature, 64, manifest, strlen(manifest) - 1));
	key[0] ^= 1;
	CHECK(!wiiu_update_signature_valid(key, signature, 64, manifest, strlen(manifest)));
	/* the key as the build is given it: 64 lowercase hexadecimal digits, no more and no fewer */
	CHECK(wiiu_update_key_from_hex("", key) != 0);
	CHECK(wiiu_update_key_from_hex("0123", key) != 0);
	hex[0] = 'G';
	CHECK(wiiu_update_key_from_hex(hex, key) != 0);
	CHECK(wiiu_update_key_from_hex(NULL, key) != 0);
}

/* ---------- the journal's text */

static void sample_journal(wiiu_update_journal *journal)
{
	memset(journal, 0, sizeof(*journal));
	journal->sequence = 41;
	journal->state = WIIU_UPDATE_TRIAL;
	journal->to_build = 124;
	journal->from_build = 123;
	journal->starts = 1;
	journal->rejected = 120;
	journal->notice = 0;
	journal->has_manifest = 1;
	memset(journal->manifest_sha256, 0xab, 32);
	journal->entry_count = 2;
	snprintf(journal->entries[0].name, sizeof(journal->entries[0].name), "shaders.pack");
	journal->entries[0].size = 2401600;
	journal->entries[0].had = 1;
	memset(journal->entries[0].sha256, 0x11, 32);
	snprintf(journal->entries[1].name, sizeof(journal->entries[1].name), "halo.wuhb");
	journal->entries[1].size = 14934416;
	journal->entries[1].had = 1;
	memset(journal->entries[1].sha256, 0x22, 32);
}

static void test_journal_text(void)
{
	wiiu_update_journal journal, back;
	char text[4096], broken[4096];
	size_t size, cut, at;
	int state;

	for (state = WIIU_UPDATE_IDLE; state <= WIIU_UPDATE_RESTORED; state++)
	{
		sample_journal(&journal);
		journal.state = (wiiu_update_state)state;
		size = wiiu_update_journal_format(&journal, text, sizeof(text));
		CHECK(size > 0);
		CHECK(wiiu_update_journal_parse(text, size, &back) == 0);
		CHECK(!memcmp(&journal, &back, sizeof(journal)));
	}
	sample_journal(&journal);
	journal.has_manifest = 0;
	memset(journal.manifest_sha256, 0, 32);
	journal.entry_count = 0;
	memset(journal.entries, 0, sizeof(journal.entries));
	size = wiiu_update_journal_format(&journal, text, sizeof(text));
	CHECK(size > 0 && wiiu_update_journal_parse(text, size, &back) == 0 && !memcmp(&journal, &back, sizeof(journal)));
	/* a journal cut off anywhere, or with any byte changed, is no journal */
	sample_journal(&journal);
	size = wiiu_update_journal_format(&journal, text, sizeof(text));
	for (cut = 0; cut < size; cut++)
		if (!CHECK(wiiu_update_journal_parse(text, cut, &back) != 0))
			break;
	for (at = 0; at < size; at++)
	{
		memcpy(broken, text, size);
		broken[at] ^= 0x04;
		if (!CHECK(wiiu_update_journal_parse(broken, size, &back) != 0))
		{
			fprintf(stderr, "  byte %lu changed and still read\n", (unsigned long)at);
			break;
		}
	}
	/* too small a buffer writes nothing */
	CHECK(wiiu_update_journal_format(&journal, text, 100) == 0);
	CHECK(wiiu_update_crc32("123456789", 9) == 0xCBF43926u);
}

/* ---------- a folder with the power cut at a chosen step */

typedef struct folder
{
	char root[512];
	/* the mutating operations so far, the one the power is cut at (0: never), and whether it has been */
	unsigned operations, cut_at;
	int cut;
	/* a rename that fails without the power going (0: none) */
	unsigned fail_rename_at, renames;
	unsigned unmounts;
	int unmount_fails;
} folder;

static int mutating(folder *f)
{
	if (f->cut)
		return 0;
	f->operations++;
	if (f->cut_at && f->operations == f->cut_at)
	{
		f->cut = 1;
		return -1; /* this one is cut */
	}
	return 1;
}

static int f_exists(void *context, const char *path)
{
	struct stat information;

	(void)context;
	return stat(path, &information) == 0;
}

static int f_rename(void *context, const char *from, const char *to)
{
	folder *f = context;
	const int go = mutating(f);

	if (go <= 0)
		return -1;
	f->renames++;
	if (f->fail_rename_at && f->renames == f->fail_rename_at)
		return -1;
	/* (as FAT's: a rename never replaces a file) */
	if (f_exists(context, to))
		return -1;
	return rename(from, to);
}

static int f_remove(void *context, const char *path)
{
	folder *f = context;

	if (mutating(f) <= 0)
		return -1;
	return unlink(path);
}

static int f_write(void *context, const char *path, const void *data, size_t size)
{
	folder *f = context;
	const int go = mutating(f);
	FILE *file;
	size_t length = size;

	if (go == 0)
		return -1;
	/* (the write the power is cut at leaves half the file) */
	if (go < 0)
		length = size / 2;
	file = fopen(path, "wb");
	if (!file)
		return -1;
	if (fwrite(data, 1, length, file) != length)
	{
		fclose(file);
		return -1;
	}
	fclose(file);
	return go < 0 ? -1 : 0;
}

static long f_read(void *context, const char *path, void *data, size_t capacity)
{
	FILE *file = fopen(path, "rb");
	size_t got;

	(void)context;
	if (!file)
		return -1;
	got = fread(data, 1, capacity, file);
	fclose(file);
	return (long)got;
}

static int f_flush(void *context)
{
	(void)context;
	return 0;
}

static int f_unmount(void *context)
{
	folder *f = context;

	f->unmounts++;
	return f->unmount_fails ? -1 : 0;
}

static wiiu_update_fs fs_of(folder *f)
{
	wiiu_update_fs fs;

	fs.context = f;
	fs.root = f->root;
	fs.exists = f_exists;
	fs.rename = f_rename;
	fs.remove = f_remove;
	fs.write = f_write;
	fs.read = f_read;
	fs.flush = f_flush;
	return fs;
}

static void put_file(folder *f, const char *relative, const char *content)
{
	char path[700];
	FILE *file;

	snprintf(path, sizeof(path), "%s/%s", f->root, relative);
	file = fopen(path, "wb");
	if (file)
	{
		fputs(content, file);
		fclose(file);
	}
}

/* the file's text, or "" if there is none */
static const char *file_text(folder *f, const char *relative)
{
	static char text[256];
	char path[700];
	FILE *file;
	size_t got;

	snprintf(path, sizeof(path), "%s/%s", f->root, relative);
	file = fopen(path, "rb");
	if (!file)
		return "";
	got = fread(text, 1, sizeof(text) - 1, file);
	fclose(file);
	text[got] = 0;
	return text;
}

static void empty_folder(const char *path)
{
	DIR *directory = opendir(path);
	struct dirent *entry;

	if (!directory)
		return;
	while ((entry = readdir(directory)))
	{
		char child[700];
		struct stat information;

		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
			continue;
		snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
		if (stat(child, &information) == 0 && S_ISDIR(information.st_mode))
		{
			empty_folder(child);
			rmdir(child);
		}
		else
			unlink(child);
	}
	closedir(directory);
}

/* the build in the folder's bundle, as the system would start it: 0 if there is none (the app cannot start) */
static uint32_t bundle_build(folder *f)
{
	const char *text = file_text(f, "halo.wuhb");

	return !strncmp(text, "build=", 6) ? (uint32_t)strtoul(text + 6, NULL, 10) : 0;
}

static const char *base;
static unsigned folder_serial;

/* The card as build 123 left it with build 124 staged: halo.wuhb and shaders.pack change, brokers.txt does not, extra.pack is new in
124 (the folder has none). */
static void staged_folder(folder *f, wiiu_update_journal *journal)
{
	wiiu_update_fs fs;
	wiiu_update_manifest manifest;
	uint8_t unchanged[WIIU_UPDATE_FILES] = { 0, 1, 0, 0 };
	char path[700];

	memset(f, 0, sizeof(*f));
	snprintf(f->root, sizeof(f->root), "%s/card%u", base, folder_serial++);
	mkdir(f->root, 0755);
	empty_folder(f->root);
	snprintf(path, sizeof(path), "%s/update", f->root);
	mkdir(path, 0755);
	put_file(f, "halo.wuhb", "build=123");
	put_file(f, "shaders.pack", "pack-123");
	put_file(f, "brokers.txt", "brokers-1");
	put_file(f, "config.toml", "player's");
	put_file(f, "update/shaders.pack.new", "pack-124");
	put_file(f, "update/extra.pack.new", "extra-124");
	put_file(f, "update/halo.wuhb.new", "build=124");
	memset(&manifest, 0, sizeof(manifest));
	manifest.build = 124;
	manifest.file_count = 4;
	snprintf(manifest.files[0].name, sizeof(manifest.files[0].name), "halo.wuhb");
	snprintf(manifest.files[1].name, sizeof(manifest.files[1].name), "brokers.txt");
	snprintf(manifest.files[2].name, sizeof(manifest.files[2].name), "shaders.pack");
	snprintf(manifest.files[3].name, sizeof(manifest.files[3].name), "extra.pack");
	memset(journal, 0, sizeof(*journal));
	wiiu_update_journal_entries(journal, &manifest, unchanged);
	journal->state = WIIU_UPDATE_STAGED;
	journal->to_build = 124;
	journal->from_build = 123;
	journal->rejected = 7;
	fs = fs_of(f);
	wiiu_update_journal_save(&fs, journal);
}

static wiiu_update_system system_of(folder *f, uint32_t running, int crash_note)
{
	wiiu_update_system system;

	memset(&system, 0, sizeof(system));
	system.running_build = running;
	system.crash_note = crash_note;
	system.unmount = f_unmount;
	system.context = f;
	return system;
}

/* every file as build 123 had it, 124's staged */
static int is_old_build(folder *f)
{
	return !strcmp(file_text(f, "halo.wuhb"), "build=123") && !strcmp(file_text(f, "shaders.pack"), "pack-123") &&
		!strcmp(file_text(f, "extra.pack"), "") && !strcmp(file_text(f, "update/halo.wuhb.new"), "build=124") &&
		!strcmp(file_text(f, "update/shaders.pack.new"), "pack-124") && !strcmp(file_text(f, "update/extra.pack.new"), "extra-124") &&
		!strcmp(file_text(f, "update/halo.wuhb.old"), "") && !strcmp(file_text(f, "update/shaders.pack.old"), "");
}

/* every file as build 124 has it, 123's kept */
static int is_new_build(folder *f)
{
	return !strcmp(file_text(f, "halo.wuhb"), "build=124") && !strcmp(file_text(f, "shaders.pack"), "pack-124") &&
		!strcmp(file_text(f, "extra.pack"), "extra-124") && !strcmp(file_text(f, "update/halo.wuhb.old"), "build=123") &&
		!strcmp(file_text(f, "update/shaders.pack.old"), "pack-123") && !strcmp(file_text(f, "update/halo.wuhb.new"), "") &&
		!strcmp(file_text(f, "update/shaders.pack.new"), "") && !strcmp(file_text(f, "update/extra.pack.new"), "");
}

/* build 123 back after a failed trial: 124's files gone */
static int is_restored(folder *f)
{
	return !strcmp(file_text(f, "halo.wuhb"), "build=123") && !strcmp(file_text(f, "shaders.pack"), "pack-123") &&
		!strcmp(file_text(f, "extra.pack"), "") && !strcmp(file_text(f, "update/halo.wuhb.old"), "") &&
		!strcmp(file_text(f, "update/shaders.pack.old"), "") && !strcmp(file_text(f, "update/halo.wuhb.bad"), "") &&
		!strcmp(file_text(f, "update/shaders.pack.bad"), "") && !strcmp(file_text(f, "update/extra.pack.bad"), "");
}

static int untouched(folder *f)
{
	return !strcmp(file_text(f, "brokers.txt"), "brokers-1") && !strcmp(file_text(f, "config.toml"), "player's");
}

/* What a person does when the power was cut between the bundle's two renames (no halo.wuhb: the app is gone from the menu): PLAYING.md's
recovery, which renames update/halo.wuhb.old back; or the other copy there (.new in a swap, .bad in a restore) into place. */
static void recover_by_hand(folder *f, const char *suffix)
{
	char from[700], to[700];

	snprintf(from, sizeof(from), "%s/update/halo.wuhb.%s", f->root, suffix);
	snprintf(to, sizeof(to), "%s/halo.wuhb", f->root);
	rename(from, to);
}

/* Starts the console (the journal loaded, the start's steps run) with the power cut at the start's step cut_at (0: not cut); a start
that relaunches is followed by the next. Returns the journal as the last start left it. */
static wiiu_update_journal start_console(folder *f, unsigned cut_at, int crash_note, int *relaunches)
{
	wiiu_update_journal journal;
	wiiu_update_fs fs = fs_of(f);
	int round;

	for (round = 0; round < 4; round++)
	{
		wiiu_update_system system;
		int result;

		/* (a start cut between the bundle's renames leaves no app to start: the recovery by hand first) */
		if (!bundle_build(f))
			recover_by_hand(f, "old");
		f->operations = 0;
		f->cut = 0;
		f->cut_at = cut_at;
		wiiu_update_journal_load(&fs, &journal);
		system = system_of(f, bundle_build(f), crash_note);
		result = wiiu_update_recover(&fs, &journal, &system);
		f->cut_at = 0;
		if (f->cut)
		{
			/* the power went during this start: the next one begins again from the card */
			f->cut = 0;
			cut_at = 0;
			continue;
		}
		if (result != WIIU_UPDATE_RELAUNCH)
			break;
		if (relaunches)
			(*relaunches)++;
		crash_note = 0;
	}
	f->cut = 0;
	f->cut_at = 0;
	wiiu_update_journal_load(&fs, &journal);
	return journal;
}

/* how many mutating steps a run takes uncut */
static unsigned commit_steps(void)
{
	folder f;
	wiiu_update_journal journal;
	wiiu_update_fs fs;
	wiiu_update_system system;

	staged_folder(&f, &journal);
	fs = fs_of(&f);
	system = system_of(&f, 123, 0);
	f.operations = 0;
	CHECK(wiiu_update_commit(&fs, &journal, &system) == 1);
	CHECK(is_new_build(&f) && untouched(&f) && f.unmounts == 1);
	CHECK(journal.state == WIIU_UPDATE_TRIAL && journal.starts == 0 && journal.from_build == 123);
	/* (brokers.txt was left out: it did not change; the bundle is last) */
	CHECK(journal.entry_count == 3 && !strcmp(journal.entries[2].name, "halo.wuhb") && !journal.entries[1].had);
	return f.operations;
}

static void test_commit_cut_everywhere(void)
{
	const unsigned steps = commit_steps();
	unsigned cut, second_cut, by_hand_cases = 0, cases = 0;

	CHECK(steps >= 7);
	for (cut = 1; cut <= steps + 1; cut++)
	{
		for (second_cut = 0; second_cut <= 12; second_cut++)
		{
			int forward;

			for (forward = 0; forward < 2; forward++)
			{
				folder f;
				wiiu_update_journal journal;
				wiiu_update_fs fs;
				wiiu_update_system system;
				uint32_t running;

				staged_folder(&f, &journal);
				fs = fs_of(&f);
				system = system_of(&f, 123, 0);
				f.cut_at = cut;
				wiiu_update_commit(&fs, &journal, &system);
				f.cut = 0;
				f.cut_at = 0;
				running = bundle_build(&f);
				if (!running)
				{
					/* (only the moment between halo.wuhb's two renames leaves the folder without it) */
					CHECK(!strcmp(file_text(&f, "update/halo.wuhb.old"), "build=123") && !strcmp(file_text(&f, "update/halo.wuhb.new"), "build=124"));
					recover_by_hand(&f, forward ? "new" : "old");
					by_hand_cases++;
				}
				else if (forward)
					continue;
				journal = start_console(&f, second_cut, 0, NULL);
				running = bundle_build(&f);
				cases++;
				if (running == 124)
				{
					CHECK(is_new_build(&f));
					CHECK(journal.state == WIIU_UPDATE_TRIAL && journal.starts == 1 && journal.to_build == 124 && journal.from_build == 123);
				}
				else if (CHECK(running == 123))
				{
					CHECK(is_old_build(&f));
					CHECK(journal.state == WIIU_UPDATE_STAGED && journal.to_build == 124);
				}
				CHECK(untouched(&f) && journal.rejected == 7);
				if (failures)
				{
					fprintf(stderr, "  the commit cut at step %u, its start at %u (by hand: %s)\n", cut, second_cut, forward ? "forward" : "back");
					return;
				}
			}
		}
	}
	CHECK(by_hand_cases > 0);
	printf("commit cut at each of %u steps, and its start at each of 12: %u cases, %u needing the recovery by hand\n", steps, cases, by_hand_cases);
}

/* a folder where build 124 is on trial (committed from 123) */
static void trial_folder(folder *f, unsigned starts)
{
	wiiu_update_journal journal;
	wiiu_update_fs fs;
	wiiu_update_system system;

	staged_folder(f, &journal);
	fs = fs_of(f);
	system = system_of(f, 123, 0);
	CHECK(wiiu_update_commit(&fs, &journal, &system) == 1);
	journal.starts = starts;
	wiiu_update_journal_save(&fs, &journal);
	f->unmounts = 0;
}

static void test_trial(void)
{
	folder f;
	wiiu_update_journal journal;
	wiiu_update_fs fs;
	int relaunches;
	unsigned cut, cases = 0;

	/* the first start of a trial counts it; a crash note then is the old build's, not counted */
	trial_folder(&f, 0);
	journal = start_console(&f, 0, 1, NULL);
	CHECK(journal.state == WIIU_UPDATE_TRIAL && journal.starts == 1 && is_new_build(&f));
	/* reaching the main menu keeps it; the next start deletes the old build's files */
	fs = fs_of(&f);
	CHECK(wiiu_update_mark_good(&fs, &journal) == 0 && journal.state == WIIU_UPDATE_GOOD);
	journal = start_console(&f, 0, 0, NULL);
	CHECK(journal.state == WIIU_UPDATE_IDLE && journal.rejected == 7 && journal.entry_count == 0);
	CHECK(!strcmp(file_text(&f, "halo.wuhb"), "build=124") && !strcmp(file_text(&f, "update/halo.wuhb.old"), "") &&
		!strcmp(file_text(&f, "update/shaders.pack.old"), "") && untouched(&f));

	/* the old build's files deleted with the power cut at each step: the next start deletes the rest */
	for (cut = 1; cut <= 6; cut++)
	{
		trial_folder(&f, 1);
		fs = fs_of(&f);
		wiiu_update_journal_load(&fs, &journal);
		CHECK(wiiu_update_mark_good(&fs, &journal) == 0);
		journal = start_console(&f, cut, 0, NULL);
		CHECK(journal.state == WIIU_UPDATE_IDLE && !strcmp(file_text(&f, "halo.wuhb"), "build=124") &&
			!strcmp(file_text(&f, "update/halo.wuhb.old"), "") && !strcmp(file_text(&f, "update/shaders.pack.old"), "") && untouched(&f));
	}

	/* a crash before the main menu: the old build is put back, and the new one set aside */
	trial_folder(&f, 1);
	relaunches = 0;
	journal = start_console(&f, 0, 1, &relaunches);
	CHECK(relaunches == 1 && f.unmounts == 1);
	CHECK(is_restored(&f) && untouched(&f));
	CHECK(journal.state == WIIU_UPDATE_IDLE && journal.rejected == 124 && journal.notice == 124);

	/* two starts that never reach the main menu: the same */
	trial_folder(&f, 1);
	journal = start_console(&f, 0, 0, NULL);
	CHECK(journal.state == WIIU_UPDATE_TRIAL && journal.starts == 2 && is_new_build(&f));
	relaunches = 0;
	journal = start_console(&f, 0, 0, &relaunches);
	CHECK(relaunches == 1 && is_restored(&f) && journal.state == WIIU_UPDATE_IDLE && journal.rejected == 124);

	/* the bundle that cannot be unmounted: nothing moves, and the next start tries again */
	trial_folder(&f, 2);
	f.unmount_fails = 1;
	journal = start_console(&f, 0, 0, NULL);
	CHECK(journal.state == WIIU_UPDATE_RESTORING && is_new_build(&f));
	f.unmount_fails = 0;
	journal = start_console(&f, 0, 0, NULL);
	CHECK(is_restored(&f) && journal.state == WIIU_UPDATE_IDLE && journal.rejected == 124);

	/* no unmount on this console at all: the trial goes on */
	trial_folder(&f, 2);
	{
		wiiu_update_system system = system_of(&f, 124, 0);

		fs = fs_of(&f);
		wiiu_update_journal_load(&fs, &journal);
		system.unmount = NULL;
		CHECK(wiiu_update_recover(&fs, &journal, &system) == WIIU_UPDATE_CONTINUE);
		CHECK(journal.state == WIIU_UPDATE_TRIAL && journal.starts == 3 && is_new_build(&f));
	}

	/* the bundle replaced by hand during a trial: the trial is dropped, the files left */
	trial_folder(&f, 1);
	put_file(&f, "halo.wuhb", "build=200");
	journal = start_console(&f, 0, 1, NULL);
	CHECK(journal.state == WIIU_UPDATE_IDLE && !strcmp(file_text(&f, "halo.wuhb"), "build=200"));

	/* the restore with the power cut at each of its steps, and at each step of the start after */
	for (cut = 1; cut <= 12; cut++)
	{
		unsigned second_cut;

		for (second_cut = 0; second_cut <= 12; second_cut++)
		{
			int forward;

			for (forward = 0; forward < 2; forward++)
			{
				uint32_t running;

				trial_folder(&f, 2);
				fs = fs_of(&f);
				wiiu_update_journal_load(&fs, &journal);
				{
					wiiu_update_system system = system_of(&f, 124, 0);

					f.operations = 0;
					f.cut_at = cut;
					wiiu_update_recover(&fs, &journal, &system);
					f.cut = 0;
					f.cut_at = 0;
				}
				running = bundle_build(&f);
				if (!running)
					recover_by_hand(&f, forward ? "bad" : "old");
				else if (forward)
					continue;
				journal = start_console(&f, second_cut, 0, NULL);
				cases++;
				/* the start finishes the restore whichever build it finds: 124 puts 123 back and starts it, 123 is already back */
				CHECK(is_restored(&f) && untouched(&f));
				CHECK(journal.state == WIIU_UPDATE_IDLE && journal.rejected == 124 && journal.notice == 124);
				if (failures)
				{
					fprintf(stderr, "  the restore cut at step %u, the start after it at %u\n", cut, second_cut);
					return;
				}
			}
		}
	}
	printf("restore cut at each of 12 steps, and its start at each of 12: %u cases\n", cases);
}

static void test_commit_refusals(void)
{
	folder f;
	wiiu_update_journal journal;
	wiiu_update_fs fs;
	wiiu_update_system system;

	/* the unmount fails: nothing is moved, the release stays staged */
	staged_folder(&f, &journal);
	fs = fs_of(&f);
	system = system_of(&f, 123, 0);
	f.unmount_fails = 1;
	CHECK(wiiu_update_commit(&fs, &journal, &system) == 0);
	CHECK(is_old_build(&f) && journal.state == WIIU_UPDATE_STAGED);
	wiiu_update_journal_load(&fs, &journal);
	CHECK(journal.state == WIIU_UPDATE_STAGED);

	/* a rename that fails halfway: everything is put back */
	staged_folder(&f, &journal);
	fs = fs_of(&f);
	system = system_of(&f, 123, 0);
	f.fail_rename_at = 4;
	CHECK(wiiu_update_commit(&fs, &journal, &system) == 0);
	CHECK(is_old_build(&f) && journal.state == WIIU_UPDATE_STAGED);

	/* a file not staged, a journal not staged */
	staged_folder(&f, &journal);
	fs = fs_of(&f);
	system = system_of(&f, 123, 0);
	{
		char path[700];

		snprintf(path, sizeof(path), "%s/update/extra.pack.new", f.root);
		unlink(path);
	}
	CHECK(wiiu_update_commit(&fs, &journal, &system) == 0 && f.unmounts == 0 && !strcmp(file_text(&f, "halo.wuhb"), "build=123"));
	journal.state = WIIU_UPDATE_DOWNLOADING;
	CHECK(wiiu_update_commit(&fs, &journal, &system) == 0);

	/* an old file of an earlier update in the way is deleted first */
	staged_folder(&f, &journal);
	fs = fs_of(&f);
	system = system_of(&f, 123, 0);
	put_file(&f, "update/halo.wuhb.old", "build=100");
	CHECK(wiiu_update_commit(&fs, &journal, &system) == 1 && is_new_build(&f));

	/* the parts and staged files discarded: the journal idle, rejected kept */
	staged_folder(&f, &journal);
	fs = fs_of(&f);
	put_file(&f, "update/brokers.txt.part", "half");
	journal.state = WIIU_UPDATE_DOWNLOADING;
	CHECK(wiiu_update_discard(&fs, &journal) == 0 && journal.state == WIIU_UPDATE_IDLE && journal.rejected == 7);
	CHECK(!strcmp(file_text(&f, "update/halo.wuhb.new"), "") && !strcmp(file_text(&f, "update/shaders.pack.new"), "") &&
		!strcmp(file_text(&f, "halo.wuhb"), "build=123"));
	journal.state = WIIU_UPDATE_TRIAL;
	CHECK(wiiu_update_discard(&fs, &journal) != 0);
}

static void test_journal_copies(void)
{
	folder f;
	wiiu_update_journal journal, read_back;
	wiiu_update_fs fs;
	unsigned i;

	staged_folder(&f, &journal);
	fs = fs_of(&f);
	/* each save goes to the other copy, and the newer is read */
	for (i = 0; i < 5; i++)
	{
		journal.starts = i;
		CHECK(wiiu_update_journal_save(&fs, &journal) == 0);
		CHECK(wiiu_update_journal_load(&fs, &read_back) == 1 && read_back.starts == i && read_back.sequence == journal.sequence);
	}
	/* a save cut off leaves the one before */
	f.operations = 0;
	f.cut_at = 1;
	journal.starts = 99;
	CHECK(wiiu_update_journal_save(&fs, &journal) != 0);
	f.cut = 0;
	f.cut_at = 0;
	CHECK(wiiu_update_journal_load(&fs, &read_back) == 1 && read_back.starts == 4);
	/* and the next save, with the next number, goes over the broken copy */
	journal = read_back;
	journal.starts = 5;
	CHECK(wiiu_update_journal_save(&fs, &journal) == 0);
	CHECK(wiiu_update_journal_load(&fs, &read_back) == 1 && read_back.starts == 5);
	/* none at all: idle */
	empty_folder(f.root);
	CHECK(wiiu_update_journal_load(&fs, &read_back) == 0 && read_back.state == WIIU_UPDATE_IDLE);
}

static void test_entries(void)
{
	wiiu_update_manifest manifest;
	wiiu_update_journal journal;
	uint8_t unchanged[WIIU_UPDATE_FILES] = { 1, 1, 0 };

	memset(&manifest, 0, sizeof(manifest));
	manifest.file_count = 3;
	snprintf(manifest.files[0].name, sizeof(manifest.files[0].name), "halo.wuhb");
	snprintf(manifest.files[1].name, sizeof(manifest.files[1].name), "brokers.txt");
	snprintf(manifest.files[2].name, sizeof(manifest.files[2].name), "shaders.pack");
	memset(&journal, 0, sizeof(journal));
	wiiu_update_journal_entries(&journal, &manifest, unchanged);
	/* (the bundle always comes, and last, even marked unchanged) */
	CHECK(journal.entry_count == 2 && !strcmp(journal.entries[0].name, "shaders.pack") && !strcmp(journal.entries[1].name, "halo.wuhb"));
	wiiu_update_journal_entries(&journal, &manifest, NULL);
	CHECK(journal.entry_count == 3 && !strcmp(journal.entries[2].name, "halo.wuhb"));
}

int main(int argc, char **argv)
{
	char temporary[] = "/tmp/opence-update-tests-XXXXXX";

	if (argc == 5 && !strcmp(argv[1], "verify"))
	{
		uint8_t key[32];
		size_t manifest_size = 0, signature_size = 0;
		char *manifest = read_whole(argv[3], &manifest_size), *signature = read_whole(argv[4], &signature_size);
		const int valid = manifest && signature && wiiu_update_key_from_hex(argv[2], key) == 0 &&
			wiiu_update_signature_valid(key, signature, signature_size, manifest, manifest_size);

		printf("%s\n", valid ? "valid" : "invalid");
		free(manifest);
		free(signature);
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "parse"))
	{
		wiiu_update_manifest manifest;
		size_t size = 0;
		char reason[200] = "";
		char *text = read_whole(argv[2], &size);

		if (text && wiiu_update_manifest_parse(text, size, &manifest, reason, sizeof(reason)) == 0)
			printf("ok build %u files %u\n", (unsigned)manifest.build, (unsigned)manifest.file_count);
		else
			printf("refused: %s\n", text ? reason : "no file");
		free(text);
		return 0;
	}
	base = mkdtemp(temporary);
	if (!base)
	{
		perror("mkdtemp");
		return 1;
	}
	test_manifest();
	test_names();
	test_signature();
	test_journal_text();
	test_journal_copies();
	test_entries();
	test_commit_refusals();
	if (!failures)
		test_commit_cut_everywhere();
	if (!failures)
		test_trial();
	empty_folder(base);
	rmdir(base);
	if (failures)
	{
		fprintf(stderr, "%u of %u checks failed\n", failures, checks);
		return 1;
	}
	printf("update core tests passed (%u checks)\n", checks);
	return 0;
}
