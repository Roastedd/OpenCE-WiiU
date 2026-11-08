/*
SOCIAL_PARTY.C

Parties of up to eight over the brokers (social_core.h).
*/

#include "../src/p2p_internal.h"
#include "social_core.h"

#include <string.h>

void social_party_update(struct social_engine *engine)
{
	(void)engine;
}

void social_party_shutdown(struct social_engine *engine)
{
	(void)engine;
}

int social_party_topic(const struct social_engine *engine, const char *topic)
{
	(void)engine;
	(void)topic;
	return 0;
}

const unsigned char *social_party_key(const struct social_engine *engine, const char *topic)
{
	(void)engine;
	(void)topic;
	return NULL;
}

int social_party_topic_wanted(const struct social_engine *engine, char *topic)
{
	(void)engine;
	(void)topic;
	return 0;
}

void social_party_heard(struct social_engine *engine, const char *topic, const unsigned char *plain, int size, const struct social_inner *inner)
{
	(void)topic;
	(void)plain;
	(void)size;
	(void)inner;
	engine->statistics.dropped_unknown++;
}

void social_presence_extra(struct social_engine *engine, struct social_presence *presence)
{
	(void)engine;
	(void)presence;
}
