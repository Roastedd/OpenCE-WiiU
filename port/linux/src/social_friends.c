/*
SOCIAL_FRIENDS.C

Requests, answers, hello and remove between friends (social_core.h).
*/

#include "p2p_internal.h"
#include "social_core.h"

#include <string.h>

void social_friends_update(struct social_engine *engine)
{
	(void)engine;
}

int social_requests_pending(const struct social_engine *engine)
{
	(void)engine;
	return 0;
}

struct social_friend *social_direct_sender(struct social_engine *engine, struct social_friend *friend, const unsigned char *tag, const unsigned char **key)
{
	int index;

	if (friend)
	{
		if (!friend->pair_ready || !friend->has_circle || memcmp(tag, friend->tag_in, SOCIAL_TAG_SIZE))
			return NULL;
		*key = friend->pair_key;
		return friend;
	}
	for (index = 0; index < SOCIAL_MAX_FRIENDS; index++)
	{
		struct social_friend *candidate = &engine->friends[index];

		if (candidate->used && candidate->pair_ready && !memcmp(tag, candidate->tag_in, SOCIAL_TAG_SIZE))
		{
			*key = candidate->pair_key;
			return candidate;
		}
	}
	return NULL;
}

void social_request_heard(struct social_engine *engine, const unsigned char *plain, int size, const struct social_inner *inner)
{
	(void)plain;
	(void)size;
	(void)inner;
	engine->statistics.dropped_unknown++;
}

void social_direct_heard(struct social_engine *engine, struct social_friend *friend, int on_inbox, const unsigned char *plain, int size,
	const struct social_inner *inner)
{
	(void)friend;
	(void)on_inbox;
	(void)plain;
	(void)size;
	(void)inner;
	engine->statistics.dropped_unknown++;
}
