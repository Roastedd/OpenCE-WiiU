/*
DIRECTORY_CACHE.H

The names in the directories that the path translation (xbox_files.c, platform_translate_path) looks a path's components up in, read once and kept for a while.
The Xbox's names are not case sensitive, so the translation finds, for each component of a path, the spelling the directory has: it listed the whole directory
(a directory opened, an entry read for each name until the one wanted, a stat of the exact spelling), for every component of every path, every time. On the Wii U's
card each of those calls is a request to the system, some 0.7 ms: a path of three components with a few dozen names in its directories cost some 20 calls before
it was opened. The menus opened a dozen paths in a frame (reports/research/2026-11-09/menulag-result.md).

A directory is read whole the first time a name is wanted from it, and kept DIRECTORY_CACHE_MILLISECONDS, until a file or a directory is made, removed or renamed
through the Xbox file API (directory_cache_flush), or until another directory pushes it out (DIRECTORY_CACHE_DIRECTORIES are kept). A directory with more than
DIRECTORY_CACHE_NAMES names is not kept (the lookup then reads it as it did). What the cache decides is only a name's spelling (a name with no entry is used as it is
given), and the Wii U's card is not case sensitive, so an entry that is out of date - a file made or removed by something that does not tell the cache - can change
a path's spelling and nothing else. The file layer's calls are macros the includer defines; the test (port/wiiu/tests/test_directory_cache.c) defines them as
counting mocks.

    DIRECTORY_CACHE_OPEN(path)                 a handle for the directory, or NULL
    DIRECTORY_CACHE_NEXT(handle, name, size)   the next name (not . and ..): 1, or 0 at the end
    DIRECTORY_CACHE_CLOSE(handle)
    DIRECTORY_CACHE_NOW()                      milliseconds (unsigned long long), a clock that does not go back
*/

#ifndef HALO_DIRECTORY_CACHE_H
#define HALO_DIRECTORY_CACHE_H

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

enum
{
	DIRECTORY_CACHE_DIRECTORIES = 24,
	DIRECTORY_CACHE_NAMES = 4096,
	DIRECTORY_CACHE_BYTES = 262144,
	DIRECTORY_CACHE_MILLISECONDS = 30000,
	DIRECTORY_CACHE_NAME_LENGTH = 1024,
};

struct directory_cache_entry
{
	char *path;
	/* the names, each ended by a zero, in the directory's order */
	char *names;
	unsigned long count;
	unsigned long generation;
	unsigned long long read_at;
	unsigned long long used;
	/* the directory had too many names to keep: it is read as it was (and not again for a while) */
	int too_big;
};

static struct directory_cache_entry directory_cache_entries[DIRECTORY_CACHE_DIRECTORIES];
static unsigned long directory_cache_generation;
static unsigned long long directory_cache_uses;
static pthread_mutex_t directory_cache_lock = PTHREAD_MUTEX_INITIALIZER;

/* a file or a directory was made, removed or renamed: what is kept is read again when it is wanted */
static void directory_cache_flush(void)
{
	__atomic_fetch_add(&directory_cache_generation, 1, __ATOMIC_SEQ_CST);
}

static int directory_cache_equal_ignoring_case(const char *a, const char *b)
{
	for (; *a && *b; a++, b++)
	{
		unsigned char x = (unsigned char)*a, y = (unsigned char)*b;

		if (x >= 'A' && x <= 'Z')
			x = (unsigned char)(x + 32);
		if (y >= 'A' && y <= 'Z')
			y = (unsigned char)(y + 32);
		if (x != y)
			return 0;
	}
	return *a == *b;
}

/* the names in a directory by reading it: the entry's names and count, or too_big */
static int directory_cache_read(char const *directory, struct directory_cache_entry *entry)
{
	void *stream = DIRECTORY_CACHE_OPEN(directory);
	char name[DIRECTORY_CACHE_NAME_LENGTH];
	unsigned long capacity = 4096, used = 0;
	char *names;

	memset(entry, 0, sizeof(*entry));
	if (!stream)
		return 0;
	names = malloc(capacity);
	if (!names)
	{
		DIRECTORY_CACHE_CLOSE(stream);
		return 0;
	}
	while (DIRECTORY_CACHE_NEXT(stream, name, sizeof(name)))
	{
		unsigned long length = (unsigned long)strlen(name) + 1;

		if (entry->count >= DIRECTORY_CACHE_NAMES || used + length > DIRECTORY_CACHE_BYTES)
		{
			entry->too_big = 1;
			break;
		}
		if (used + length > capacity)
		{
			char *grown;

			while (used + length > capacity)
				capacity *= 2;
			grown = realloc(names, capacity);
			if (!grown)
			{
				free(names);
				DIRECTORY_CACHE_CLOSE(stream);
				return 0;
			}
			names = grown;
		}
		memcpy(names + used, name, length);
		used += length;
		entry->count++;
	}
	DIRECTORY_CACHE_CLOSE(stream);
	if (entry->too_big)
	{
		free(names);
		names = NULL;
		entry->count = 0;
	}
	entry->names = names;
	return 1;
}

/* in the names of an entry: whether one is exactly name, and the first that is name in any case */
static int directory_cache_search(struct directory_cache_entry const *entry, char const *name, char *result, unsigned long size, int *exact)
{
	char const *cursor = entry->names;
	unsigned long index;
	int found = 0;

	*exact = 0;
	for (index = 0; index < entry->count; index++)
	{
		unsigned long length = (unsigned long)strlen(cursor) + 1;

		if (directory_cache_equal_ignoring_case(cursor, name) && length <= size)
		{
			if (!found)
			{
				memcpy(result, cursor, length);
				found = 1;
			}
			if (!strcmp(cursor, name))
			{
				*exact = 1;
				break;
			}
		}
		cursor += length;
	}
	return found;
}

/* The directory's name that is name in any case (the first in the directory's order), in result: 1; 0 if the directory has none; -1 if the directory cannot be read or is
too big to keep (the caller reads it as it did). exact: a name that is exactly name is among them. */
static int directory_cache_find(char const *directory, char const *name, char *result, unsigned long size, int *exact)
{
	struct directory_cache_entry fresh;
	unsigned long generation = (unsigned long)__atomic_load_n(&directory_cache_generation, __ATOMIC_SEQ_CST);
	unsigned long long now = DIRECTORY_CACHE_NOW();
	int index, victim = -1, answer;

	*exact = 0;
#ifdef DIRECTORY_CACHE_OFF
	(void)victim;
	(void)index;
	(void)generation;
	(void)now;
	if (!directory_cache_read(directory, &fresh))
		return -1;
	if (fresh.too_big)
		return -1;
	answer = directory_cache_search(&fresh, name, result, size, exact);
	free(fresh.names);
	return answer;
#else
	pthread_mutex_lock(&directory_cache_lock);
	for (index = 0; index < DIRECTORY_CACHE_DIRECTORIES; index++)
	{
		struct directory_cache_entry *entry = &directory_cache_entries[index];

		if (entry->path && !strcmp(entry->path, directory) && entry->generation == generation && now - entry->read_at < DIRECTORY_CACHE_MILLISECONDS)
		{
			if (entry->too_big)
			{
				pthread_mutex_unlock(&directory_cache_lock);
				return -1;
			}
			entry->used = ++directory_cache_uses;
			answer = directory_cache_search(entry, name, result, size, exact);
			pthread_mutex_unlock(&directory_cache_lock);
			return answer;
		}
	}
	pthread_mutex_unlock(&directory_cache_lock);

	/* (read outside the lock: another thread's lookups do not wait for the card) */
	if (!directory_cache_read(directory, &fresh))
		return -1;
	answer = fresh.too_big ? -1 : directory_cache_search(&fresh, name, result, size, exact);
	fresh.path = malloc(strlen(directory) + 1);
	if (!fresh.path)
	{
		free(fresh.names);
		return answer;
	}
	strcpy(fresh.path, directory);
	fresh.generation = generation;
	fresh.read_at = now;
	pthread_mutex_lock(&directory_cache_lock);
	for (index = 0; index < DIRECTORY_CACHE_DIRECTORIES; index++)
	{
		struct directory_cache_entry *entry = &directory_cache_entries[index];

		if (entry->path && !strcmp(entry->path, directory))
		{
			victim = index;
			break;
		}
		if (victim < 0 || (directory_cache_entries[victim].path && (!entry->path || entry->used < directory_cache_entries[victim].used)))
			victim = index;
	}
	if (directory_cache_entries[victim].path)
	{
		free(directory_cache_entries[victim].path);
		free(directory_cache_entries[victim].names);
	}
	fresh.used = ++directory_cache_uses;
	directory_cache_entries[victim] = fresh;
	pthread_mutex_unlock(&directory_cache_lock);
	return answer;
#endif
}

#endif
