/* port/linux/src/directory_cache.h on its own, over a file layer that counts: a directory opened, an entry read.
   cc -std=gnu11 -pthread -Iport/linux/src port/wiiu/tests/test_directory_cache.c   (-DDIRECTORY_CACHE_OFF: the lookups that read the directory each time, which the checks
   below must find: the negative control of tools/test_wiiu_directory_cache.py) */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- a file layer of directories in memory */

#define MOCK_DIRECTORIES 64
#define MOCK_NAMES 5000

static struct
{
	char path[64];
	int count;
	char *names[MOCK_NAMES];
	int readable;
} mock_directories[MOCK_DIRECTORIES];
static int mock_directory_count;
static unsigned long mock_opens, mock_entries;
static unsigned long long mock_clock;
static pthread_mutex_t mock_lock = PTHREAD_MUTEX_INITIALIZER;

struct mock_stream
{
	int directory;
	int next;
};

static int mock_find(char const *path)
{
	int index;

	for (index = 0; index < mock_directory_count; index++)
	{
		if (!strcmp(mock_directories[index].path, path))
			return index;
	}
	return -1;
}

static int mock_make(char const *path)
{
	int index = mock_find(path);

	if (index < 0)
	{
		index = mock_directory_count++;
		snprintf(mock_directories[index].path, sizeof(mock_directories[index].path), "%s", path);
	}
	mock_directories[index].readable = 1;
	return index;
}

static void mock_add(char const *path, char const *name)
{
	int index = mock_make(path);

	mock_directories[index].names[mock_directories[index].count++] = strdup(name);
}

static void mock_clear(char const *path)
{
	int index = mock_make(path);

	while (mock_directories[index].count)
		free(mock_directories[index].names[--mock_directories[index].count]);
}

static void *mock_open(char const *path)
{
	int index = mock_find(path);
	struct mock_stream *stream;

	pthread_mutex_lock(&mock_lock);
	mock_opens++;
	pthread_mutex_unlock(&mock_lock);
	if (index < 0 || !mock_directories[index].readable)
		return NULL;
	stream = calloc(1, sizeof(*stream));
	stream->directory = index;
	return stream;
}

static int mock_next(void *handle, char *name, unsigned long size)
{
	struct mock_stream *stream = handle;

	if (stream->next >= mock_directories[stream->directory].count)
		return 0;
	pthread_mutex_lock(&mock_lock);
	mock_entries++;
	pthread_mutex_unlock(&mock_lock);
	snprintf(name, size, "%s", mock_directories[stream->directory].names[stream->next++]);
	return 1;
}

static void mock_close(void *handle)
{
	free(handle);
}

#define DIRECTORY_CACHE_OPEN(path) mock_open(path)
#define DIRECTORY_CACHE_NEXT(handle, name, size) mock_next((handle), (name), (size))
#define DIRECTORY_CACHE_CLOSE(handle) mock_close(handle)
#define DIRECTORY_CACHE_NOW() mock_clock
#include "directory_cache.h"

/* ---------- the checks */

static int failures;

#define CHECK(condition, ...) do { if (!(condition)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int lookup(char const *directory, char const *name, char *result, int *exact)
{
	return directory_cache_find(directory, name, result, 256, exact);
}

static void reset(void)
{
	mock_opens = mock_entries = 0;
}

static void test_spelling(void)
{
	char result[256];
	int exact;

	mock_add("/d", "maps");
	mock_add("/d", "Saved");
	mock_add("/d", "SAVED");
	mock_add("/d", "saved");
	mock_add("/d", "z.txt");
	CHECK(lookup("/d", "SAVED", result, &exact) == 1 && !strcmp(result, "Saved") && exact == 1, "an exact name among others: %s %d", result, exact);
	CHECK(lookup("/d", "sAvEd", result, &exact) == 1 && !strcmp(result, "Saved") && exact == 0, "the first spelling in the directory's order: %s %d", result, exact);
	CHECK(lookup("/d", "Z.TXT", result, &exact) == 1 && !strcmp(result, "z.txt") && exact == 0, "a name in another case: %s %d", result, exact);
	CHECK(lookup("/d", "nothing", result, &exact) == 0, "a name the directory does not have");
	CHECK(lookup("/d", "map", result, &exact) == 0 && lookup("/d", "mapss", result, &exact) == 0, "a name that is a part of one");
	CHECK(lookup("/missing", "x", result, &exact) == -1, "a directory that is not there");
}

static void test_one_read_serves_every_lookup(void)
{
	char result[256];
	int exact, index;

	reset();
	for (index = 0; index < 50; index++)
	{
		char name[16];

		snprintf(name, sizeof(name), "n%d", index % 7);
		lookup("/d", name, result, &exact);
		lookup("/d", "saved", result, &exact);
	}
	/* the directory was read before (test_spelling) and is kept: no open, no entry read at all for a hundred lookups */
	CHECK(mock_opens == 0, "a hundred lookups opened %lu directories", mock_opens);
	CHECK(mock_entries == 0, "a hundred lookups read %lu entries", mock_entries);
}

static void test_a_change_and_the_time_make_it_read_again(void)
{
	char result[256];
	int exact;

	reset();
	lookup("/d", "saved", result, &exact);
	CHECK(mock_opens == 0, "kept");
	mock_add("/d", "NewFile");
	directory_cache_flush();
	CHECK(lookup("/d", "newfile", result, &exact) == 1 && !strcmp(result, "NewFile"), "a file made is found after the flush");
	CHECK(mock_opens == 1 && mock_entries == 6, "the flush made one read of six entries: %lu %lu", mock_opens, mock_entries);
	reset();
	mock_clock += DIRECTORY_CACHE_MILLISECONDS - 1;
	lookup("/d", "saved", result, &exact);
	CHECK(mock_opens == 0, "kept until the time is up");
	mock_clock += 2;
	lookup("/d", "saved", result, &exact);
	CHECK(mock_opens == 1, "read again when the time is up: %lu", mock_opens);
}

static void test_many_directories_push_the_oldest_out(void)
{
	char result[256], path[32];
	int exact, index;

	mock_clock += 10;
	for (index = 0; index < DIRECTORY_CACHE_DIRECTORIES + 4; index++)
	{
		snprintf(path, sizeof(path), "/many/%d", index);
		mock_clear(path);
		mock_add(path, "file");
		lookup(path, "FILE", result, &exact);
	}
	reset();
	lookup("/many/0", "file", result, &exact);
	CHECK(mock_opens == 1, "the oldest was pushed out: %lu", mock_opens);
	reset();
	snprintf(path, sizeof(path), "/many/%d", DIRECTORY_CACHE_DIRECTORIES + 3);
	lookup(path, "file", result, &exact);
	CHECK(mock_opens == 0, "the newest is kept: %lu", mock_opens);
}

static void test_a_directory_with_too_many_names_is_read_as_it_was(void)
{
	char result[256], name[16];
	int exact, index;

	mock_clear("/big");
	for (index = 0; index < DIRECTORY_CACHE_NAMES + 10; index++)
	{
		snprintf(name, sizeof(name), "f%d", index);
		mock_add("/big", name);
	}
	CHECK(lookup("/big", "f5", result, &exact) == -1, "not kept, and the caller told to read it");
	reset();
	CHECK(lookup("/big", "f6", result, &exact) == -1 && mock_opens == 0, "and not read again for each lookup (the cache remembers it is too big): %lu opens", mock_opens);
}

static void *hammer(void *argument)
{
	char result[256], name[16];
	int exact, index;

	for (index = 0; index < 20000; index++)
	{
		snprintf(name, sizeof(name), "F%d", index % 40);
		if (lookup("/threads", name, result, &exact) != 1 || strcasecmp(result, name))
			failures++;
		if ((index % 500) == 0 && argument)
			directory_cache_flush();
	}
	return NULL;
}

static void test_threads(void)
{
	pthread_t threads[4];
	char name[16];
	int index;

	mock_clear("/threads");
	for (index = 0; index < 40; index++)
	{
		snprintf(name, sizeof(name), "f%d", index);
		mock_add("/threads", name);
	}
	for (index = 0; index < 4; index++)
		pthread_create(&threads[index], NULL, hammer, index & 1 ? (void *)1 : NULL);
	for (index = 0; index < 4; index++)
		pthread_join(threads[index], NULL);
	CHECK(failures == 0, "lookups from four threads with flushes between them went wrong");
}

int main(void)
{
	test_spelling();
	test_one_read_serves_every_lookup();
	test_a_change_and_the_time_make_it_read_again();
	test_many_directories_push_the_oldest_out();
	test_a_directory_with_too_many_names_is_read_as_it_was();
	test_threads();
	printf("%d failures\n", failures);
	return failures ? 1 : 0;
}
