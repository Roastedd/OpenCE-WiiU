/* the name cache (port/linux/game/name_cache.c): an answer comes back as it was stored, for the same kind, tag and name (case as the kind compares it), a miss for any other,
and nothing survives a map. */
#include "name_cache.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main(void)
{
	short r = -7;
	char long_name[80];

	CHECK(!halo_name_cache_lookup(HALO_NAME_CACHE_ANIMATION, 5, "stand idle", &r));
	halo_name_cache_store(HALO_NAME_CACHE_ANIMATION, 5, "stand idle", 12);
	CHECK(halo_name_cache_lookup(HALO_NAME_CACHE_ANIMATION, 5, "stand idle", &r) && r == 12);
	/* animations and markers compare without case, nodes with it */
	CHECK(halo_name_cache_lookup(HALO_NAME_CACHE_ANIMATION, 5, "STAND Idle", &r) && r == 12);
	halo_name_cache_store(HALO_NAME_CACHE_NODE, 5, "bip01 spine", 3);
	CHECK(halo_name_cache_lookup(HALO_NAME_CACHE_NODE, 5, "bip01 spine", &r) && r == 3);
	CHECK(!halo_name_cache_lookup(HALO_NAME_CACHE_NODE, 5, "BIP01 spine", &r));
	/* another tag, another kind, another name */
	CHECK(!halo_name_cache_lookup(HALO_NAME_CACHE_ANIMATION, 6, "stand idle", &r));
	CHECK(!halo_name_cache_lookup(HALO_NAME_CACHE_MARKER, 5, "stand idle", &r));
	CHECK(!halo_name_cache_lookup(HALO_NAME_CACHE_ANIMATION, 5, "stand idle2", &r));
	/* a name of none found is an answer */
	halo_name_cache_store(HALO_NAME_CACHE_MARKER, 9, "muzzle", -1);
	CHECK(halo_name_cache_lookup(HALO_NAME_CACHE_MARKER, 9, "muzzle", &r) && r == -1);
	/* a name too long for an entry is never kept, and is never wrong */
	memset(long_name, 'a', sizeof(long_name) - 1);
	long_name[sizeof(long_name) - 1] = 0;
	halo_name_cache_store(HALO_NAME_CACHE_ANIMATION, 5, long_name, 4);
	CHECK(!halo_name_cache_lookup(HALO_NAME_CACHE_ANIMATION, 5, long_name, &r));
	/* a thousand names against 512 entries: every answer that comes back is the one stored for that name */
	{
		int i, wrong = 0;
		char name[32];

		for (i = 0; i < 1000; i++)
		{
			snprintf(name, sizeof(name), "animation %d", i);
			halo_name_cache_store(HALO_NAME_CACHE_ANIMATION, 20, name, (short)i);
		}
		for (i = 0; i < 1000; i++)
		{
			snprintf(name, sizeof(name), "animation %d", i);
			if (halo_name_cache_lookup(HALO_NAME_CACHE_ANIMATION, 20, name, &r) && r != i)
				wrong++;
		}
		CHECK(wrong == 0);
	}
	halo_name_cache_new_map();
	CHECK(!halo_name_cache_lookup(HALO_NAME_CACHE_NODE, 5, "bip01 spine", &r));
	CHECK(!halo_name_cache_lookup(HALO_NAME_CACHE_MARKER, 9, "muzzle", &r));
	if (!failures)
		printf("name cache: all checks passed\n");
	return failures != 0;
}
