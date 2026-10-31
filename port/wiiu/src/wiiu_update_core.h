/*
WIIU_UPDATE_CORE.H

The decisions of the Wii U's self-updater (wiiu_update.c is its Cafe OS half): what a release's manifest says and whether its
signature is the owner's, and the journal that lets an update be put in place, tried, kept or taken back however the console
is stopped on the way. Plain C with no clock, no thread and no system call of its own: the files are reached through the
caller's functions (wiiu_update_fs), so that a desktop can test every step, and a power cut at any of them.

A release is the files the app owns beside its data (halo.wuhb, shaders.pack, brokers.txt), listed in a manifest signed with the
owner's Ed25519 key:

	halo-wiiu-manifest 1
	build 124
	tag wiiu-124
	maps c1ea0000
	file shaders.pack 2401600 <SHA-256, 64 hex digits>
	file halo.wuhb 14934416 <SHA-256>

"maps" is the format of the maps the build reads (the rebase of tools/wiiu/convert_maps.py): a release whose maps differ from
this build's is not offered, since the player's converted maps would not load. The signature
(halo-wiiu-manifest.txt.sig) is the 64 bytes of `openssl pkeyutl -sign -rawin` over the manifest's bytes. TLS carries the
files but does not vouch for them: the console's entropy is weak (wiiu_mbedtls.c), and only the signature and the hashes say
that the files are the owner's.

The files go to update/<name>.part as they arrive, become update/<name>.new once whole and checked, and are put in place by
renames: each current file to update/<name>.old, then the new one to its place, halo.wuhb last. The journal
(update/state.txt and update/state2.txt, written in turn, the newer whole one counts) says where that is, so that the next start
finishes or undoes a swap that was cut off: the build that is running says which (halo.wuhb is renamed last, so a running new
build means every file before it is in place, and a running old build means the swap did not get that far). The new build is
then on trial until it reaches its main menu; a trial that crashes, or two starts that never reach the menu, put the old build's
files back the same way and set the new build aside.
*/

#ifndef HALO_WIIU_UPDATE_CORE_H
#define HALO_WIIU_UPDATE_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIIU_UPDATE_MANIFEST_NAME "halo-wiiu-manifest.txt"
#define WIIU_UPDATE_SIGNATURE_NAME "halo-wiiu-manifest.txt.sig"
/* the bundle the system runs: renamed last, so that its build tells how far a swap got */
#define WIIU_UPDATE_BUNDLE "halo.wuhb"
#define WIIU_UPDATE_FOLDER "update"

/* bounds of what a manifest may say (a release is three files, about 18 MB) */
#define WIIU_UPDATE_MANIFEST_MOST 4096u
#define WIIU_UPDATE_FILES 8u
#define WIIU_UPDATE_NAME_BYTES 48u
#define WIIU_UPDATE_MAPS_BYTES 33u
#define WIIU_UPDATE_FILE_MOST (256u * 1024u * 1024u)
#define WIIU_UPDATE_TOTAL_MOST (512u * 1024u * 1024u)
#define WIIU_UPDATE_BUILD_MOST 999999999u

/* the longest path the files are given (the card's folder is 31 characters) */
#define WIIU_UPDATE_PATH_BYTES 320u

typedef struct wiiu_update_file
{
	char name[WIIU_UPDATE_NAME_BYTES];
	uint32_t size;
	uint8_t sha256[32];
} wiiu_update_file;

typedef struct wiiu_update_manifest
{
	uint32_t build;
	char maps[WIIU_UPDATE_MAPS_BYTES];
	uint32_t file_count;
	wiiu_update_file files[WIIU_UPDATE_FILES];
} wiiu_update_manifest;

/* ---------- the manifest and its signature */

/* Reads a manifest: 0 and *manifest filled, or -1 with why in reason. Every line is checked (its keyword, its number of fields,
the digits of its numbers, its bounds); a line of any other kind, a file named twice, a name that leaves the app's folder or
is one of the player's, and a release without halo.wuhb are refused. */
int wiiu_update_manifest_parse(const void *text, size_t size, wiiu_update_manifest *manifest, char *reason, size_t reason_size);

/* 1 if a release may own a file of this name: a plain name in the app's folder ([a-z0-9][a-z0-9._-]*, no "..", not ending in a dot),
none of the names the updater gives its own files (.part, .new, .old, .bad, .tmp), and none of the player's or the shell's files
(config.toml, network.key, the logs, the crash notes, the black box, the switches in halo/, data and the folders). */
int wiiu_update_name_allowed(const char *name);

/* the 32 bytes of a key written as 64 hexadecimal digits; 0 on success */
int wiiu_update_key_from_hex(const char *hex, uint8_t key[32]);

/* 1 if signature (exactly 64 bytes) is the key's Ed25519 signature of the message */
int wiiu_update_signature_valid(const uint8_t key[32], const void *signature, size_t signature_size, const void *message,
	size_t message_size);

/* ---------- the journal */

typedef enum wiiu_update_state
{
	WIIU_UPDATE_IDLE,
	/* a release's files arriving as update/<name>.part (kept across HOME and closes, resumed if the same manifest is found again) */
	WIIU_UPDATE_DOWNLOADING,
	/* every file of the release whole and checked, as update/<name>.new */
	WIIU_UPDATE_STAGED,
	/* the files being put in place: build to_build in place of from_build */
	WIIU_UPDATE_SWAPPING,
	/* to_build in place and not yet at its main menu; starts counts its starts */
	WIIU_UPDATE_TRIAL,
	/* to_build reached its main menu: from_build's files (update/<name>.old) are deleted at the next start */
	WIIU_UPDATE_GOOD,
	/* from_build's files being put back in place of to_build's (which go to update/<name>.bad) */
	WIIU_UPDATE_RESTORING,
	/* from_build's files are back: to_build's are deleted at the next start (the restore's last step: a restore whose files are
	gone is no longer one the journal could tell from a folder something else changed) */
	WIIU_UPDATE_RESTORED,
} wiiu_update_state;

/* A file of the release the journal is about. had says whether the folder had a file of that name when the swap began: a
file new in the release has no old copy to keep, nor to put back. */
typedef struct wiiu_update_entry
{
	char name[WIIU_UPDATE_NAME_BYTES];
	uint32_t size;
	uint8_t sha256[32];
	uint8_t had;
} wiiu_update_entry;

typedef struct wiiu_update_journal
{
	uint32_t sequence;
	wiiu_update_state state;
	uint32_t to_build;
	uint32_t from_build;
	uint32_t starts;
	/* a build whose trial failed: never offered again (a later one is) */
	uint32_t rejected;
	/* a build set aside that the player has not been told of yet (shown at the main menu) */
	uint32_t notice;
	/* the manifest the files are from (DOWNLOADING, STAGED): the same manifest found again resumes them */
	uint8_t manifest_sha256[32];
	uint8_t has_manifest;
	/* in the order they are put in place: halo.wuhb last */
	uint32_t entry_count;
	wiiu_update_entry entries[WIIU_UPDATE_FILES];
} wiiu_update_journal;

const char *wiiu_update_state_name(wiiu_update_state state);

/* The journal as text, with its sequence number and a CRC-32 of everything before the last line; the length, or 0 if it does
not fit. */
size_t wiiu_update_journal_format(const wiiu_update_journal *journal, char *out, size_t capacity);

/* 0 if text is a whole journal (its check matches, every field in bounds), and *journal; -1 if not */
int wiiu_update_journal_parse(const void *text, size_t size, wiiu_update_journal *journal);

/* the journal's entries for a manifest: its files but those already in place (unchanged has a 1 for each the caller found the
same as the manifest's: halo.wuhb never is), halo.wuhb last */
void wiiu_update_journal_entries(wiiu_update_journal *journal, const wiiu_update_manifest *manifest, const uint8_t *unchanged);

/* ---------- the files */

/* The file operations the journal's steps are made of, with the app's folder (root). Paths are whole paths. Each returns 0 on
success; write makes the file (or empties it), writes it, flushes it to the card and closes it, so that what it wrote is there
after a power cut. */
typedef struct wiiu_update_fs
{
	void *context;
	const char *root;
	int (*exists)(void *context, const char *path);
	int (*rename)(void *context, const char *from, const char *to);
	int (*remove)(void *context, const char *path);
	int (*write)(void *context, const char *path, const void *data, size_t size);
	/* the bytes read (at most capacity), or -1 if there is no such file */
	long (*read)(void *context, const char *path, void *data, size_t capacity);
	/* the folder's entries (the renames) on the card */
	int (*flush)(void *context);
} wiiu_update_fs;

enum wiiu_update_place
{
	WIIU_UPDATE_CURRENT, /* <root>/<name> */
	WIIU_UPDATE_PART,    /* <root>/update/<name>.part */
	WIIU_UPDATE_NEW,
	WIIU_UPDATE_OLD,
	WIIU_UPDATE_BAD,
};

/* where a file of the release is, in a place; 0 on success */
int wiiu_update_path(const wiiu_update_fs *fs, const char *name, enum wiiu_update_place place, char *out, size_t capacity);

/* The journal: the newer of the two copies whose check matches; 1 if one was read, 0 if there is none (*journal is then idle). */
int wiiu_update_journal_load(const wiiu_update_fs *fs, wiiu_update_journal *journal);

/* Writes the journal with the next sequence number, over the older copy, so that a write cut off leaves the newer one whole. */
int wiiu_update_journal_save(const wiiu_update_fs *fs, wiiu_update_journal *journal);

/* How far one file of a swap has got (restore: the old build's file coming back), from which of its places have a file. */
typedef enum wiiu_update_step
{
	WIIU_STEP_NOT_STARTED,
	WIIU_STEP_HALF, /* the current file set aside, the incoming one not yet in its place: the folder has no file of that name */
	WIIU_STEP_DONE,
	WIIU_STEP_INCONSISTENT, /* no state the swap goes through: something else has moved the files */
} wiiu_update_step;

wiiu_update_step wiiu_update_step_of(const wiiu_update_fs *fs, const wiiu_update_entry *entry, int restore);

/* Takes every file of the journal's swap the rest of the way (in order), or back to where it began (in the reverse order); 0
when every file is there, -1 at the first rename that fails or a file in no state of the swap. */
int wiiu_update_swap_finish(const wiiu_update_fs *fs, const wiiu_update_journal *journal, int restore);
int wiiu_update_swap_undo(const wiiu_update_fs *fs, const wiiu_update_journal *journal, int restore);

/* What the journal's steps need of the system: the build running (HALO_BUILD_NUMBER), whether the last run left a crash note, and
the unmounting of the running bundle (halo.wuhb is mounted while the app runs; RPXLoadingModule lets go of it), 0 on success. */
typedef struct wiiu_update_system
{
	uint32_t running_build;
	int crash_note;
	int (*unmount)(void *context);
	void (*log)(void *context, const char *text);
	void *context;
} wiiu_update_system;

enum
{
	WIIU_UPDATE_CONTINUE = 0, /* the game starts */
	WIIU_UPDATE_RELAUNCH = 1, /* the folder now holds another build: start it, not the game in memory */
};

/* At the start, before the game: a swap left half done is finished or undone (by the build running), a trial counted, a failed
one taken back (the old build's files put back, the new build rejected and noticed), the old files of a kept build deleted.
Returns WIIU_UPDATE_CONTINUE or WIIU_UPDATE_RELAUNCH, or -1 if the journal could not be followed (the files are left as they
are, and said in the log). */
int wiiu_update_recover(const wiiu_update_fs *fs, wiiu_update_journal *journal, const wiiu_update_system *system);

/* The staged release put in place (the journal STAGED, every update/<name>.new checked by the caller): the running bundle
unmounted, the files swapped, halo.wuhb last, the trial begun. Returns 1 when the new build is in place (start it), 0 when
nothing was changed (the journal is STAGED again: why is in the log), -1 when a rename failed and could not be undone (the
journal says SWAPPING, which the next start resolves). */
int wiiu_update_commit(const wiiu_update_fs *fs, wiiu_update_journal *journal, const wiiu_update_system *system);

/* The trial is over: the build reached its main menu. */
int wiiu_update_mark_good(const wiiu_update_fs *fs, wiiu_update_journal *journal);

/* The release's leftovers in the update folder deleted (the parts, the staged files, the files set aside), the journal idle. */
int wiiu_update_discard(const wiiu_update_fs *fs, wiiu_update_journal *journal);

/* ---------- small things the tests and the system side share */

uint32_t wiiu_update_crc32(const void *data, size_t size);
void wiiu_update_hex(const uint8_t *bytes, size_t count, char *out);

#ifdef __cplusplus
}
#endif

#endif
