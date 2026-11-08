/*
SOCIAL_STORE.H

The friends layer's two files beside config.toml (on the Wii U's card, the app's folder): friends.key, the identity's seed, and friends.txt, the list and the lists
of requests and blocked keys, in text with a version and a check. The file is replaced safely: the new text is written to its own file and read back before the old
one is touched, and the old one is never renamed over; a power loss at any step leaves a whole file. Nothing here is called from the game's thread: the saver does it
(social_front.h).
*/

#ifndef __HALO_LINUX_SOCIAL_STORE_H
#define __HALO_LINUX_SOCIAL_STORE_H

#include "social_core.h"

enum
{
	/* the most a file may be (a hostile or damaged one is not read past it) */
	SOCIAL_STORE_MAXIMUM = 65536,
	SOCIAL_STORE_VERSION = 1,
};

/* file operations, so that the harness can fail one at any step */
struct social_files
{
	void *user;
	/* the size read, or -1 if the file is none or unreadable */
	int (*read_file)(void *user, const char *name, char *buffer, int capacity);
	/* 0 if written whole and closed */
	int (*write_file)(void *user, const char *name, const char *data, int size);
	int (*rename_file)(void *user, const char *from, const char *to);
	int (*remove_file)(void *user, const char *name);
};

/* the real files, in a folder (stdio only: fopen, rename, remove); folder must live as long as files does */
struct social_posix_folder
{
	char path[512];
};
void social_files_posix(struct social_files *files, struct social_posix_folder *folder, const char *path);

/* the text of friends.txt for the engine's state (its circle key, friends, requests sent, those turned away); the length, or -1 if it does not fit */
int social_store_format(const struct social_engine *engine, char *text, int capacity);

/* the text read into an engine that has its identity: friends, lists and settings. 0 ok; 1 it is not a friends file; 2 the check fails (damaged or cut short);
3 a newer version wrote it (the engine is locked out of saving, so that the file stays as it is) */
int social_store_parse(struct social_engine *engine, const char *text, int size);

/* friends.new written and read back, friends.bak and friends.txt made of the old; 0 ok */
int social_store_save(const struct social_files *files, const char *text, int size);

/* friends.txt, else friends.new, else friends.bak: the first that parses (the engine is only touched by one that does); 0 if one did, 1 if there is no file,
2 if there are files and none is any good, 3 a newer version (locked) */
int social_store_load(const struct social_files *files, struct social_engine *engine);

/* friends.key: three lines of comment and the seed in hex */
int social_key_format(const unsigned char *seed, char *text, int capacity);
int social_key_parse(const char *text, int size, unsigned char *seed);

#endif
