/*
SOCIAL_FAKE.H

A network of fake MQTT brokers and the machines on it, in one process (the friends layer's tests, tools/harness/social.py). Each machine has an engine (social_core.h)
with a monotonic clock of its own, a wall clock that is wrong by hours (the Wii U's is), and a random generator of its own. A broker does what the public ones do:
it drops a connection's publishes past ten a second, can lose, repeat and reorder messages, can limit the topics a client may subscribe to, and can go down. The
whole network runs on one time, advanced in steps (fake_run), so that a test is the same every time.
*/

#ifndef SOCIAL_FAKE_H
#define SOCIAL_FAKE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAKE_MAX_NODES 12
#define FAKE_BROKERS 4
#define FAKE_MAX_PENDING 16384
/* seconds: September 2026 */
#define FAKE_UTC_BASE 1790000000ULL

struct fake_net;

struct fake_node
{
	struct fake_net *net;
	int index;
	int alive;
	struct social_engine *engine;
	struct social_front *front;
	unsigned int mono_offset;
	long long wall_offset;
	int anchored;
	unsigned long long rng;
	unsigned char seed[32];
	int broker_ready[FAKE_BROKERS];
	char subscribed[SOCIAL_MAX_TOPICS][SOCIAL_TOPIC_SIZE];
	int subscribed_count;
	/* publishes in the last second at each broker (the limit) */
	unsigned int window_start[FAKE_BROKERS];
	int window_count[FAKE_BROKERS];
	int published, limited;
	char last_log[8][256];
	int log_count;
	unsigned int updates_per_step;
};

struct fake_message
{
	int used;
	unsigned int due;
	int node, broker, size;
	char topic[SOCIAL_TOPIC_SIZE];
	unsigned char payload[SOCIAL_MAX_WIRE + 64];
};

struct fake_net
{
	unsigned int time;
	struct fake_node nodes[FAKE_MAX_NODES];
	int count;
	struct fake_message pending[FAKE_MAX_PENDING];
	unsigned long long rng;
	int loss_percent, duplicate_percent, jitter_ms, latency_ms;
	int broker_up[FAKE_BROKERS];
	/* the most topics a client may subscribe to at a broker (0: no limit) */
	int subscription_cap[FAKE_BROKERS];
	int publish_limit;
	unsigned int delivered, lost, duplicated, limited;
	int verbose;
	/* the files of each machine, when it has any (a front saves them) */
	struct social_files *files;
	/* told of each message a machine publishes (once, whatever the brokers do with it) */
	void (*tap)(void *user, int node, unsigned int time, const char *topic, const unsigned char *payload, int size);
	void *tap_user;
};

static unsigned long long fake_random(unsigned long long *state)
{
	unsigned long long x = *state;

	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	*state = x;
	return x;
}

static unsigned int fake_now(void *user)
{
	struct fake_node *node = user;

	return node->net->time + node->mono_offset;
}

static unsigned long long fake_utc(void *user, int *anchored)
{
	struct fake_node *node = user;
	long long seconds = (long long)FAKE_UTC_BASE + node->net->time / 1000;

	*anchored = node->anchored;
	return (unsigned long long)(node->anchored ? seconds : seconds + node->wall_offset);
}

static void fake_random_bytes(void *user, unsigned char *bytes, int size)
{
	struct fake_node *node = user;
	int index;

	for (index = 0; index < size; index++)
		bytes[index] = (unsigned char)(fake_random(&node->rng) >> 24);
}

static int fake_ready(void *user)
{
	struct fake_node *node = user;
	int broker, count = 0;

	for (broker = 0; broker < FAKE_BROKERS; broker++)
		count += node->broker_ready[broker] && node->net->broker_up[broker];
	return count;
}

static int fake_topic_subscribed(const struct fake_node *node, int broker, const char *topic)
{
	int cap = node->net->subscription_cap[broker];
	int index;

	for (index = 0; index < node->subscribed_count && (!cap || index < cap); index++)
	{
		if (!strcmp(node->subscribed[index], topic))
			return 1;
	}
	return 0;
}

/* a message into the network from a client (node -1: from outside, with no limit) */
static void fake_deliver_from(struct fake_net *net, int from, int broker, const char *topic, const unsigned char *payload, int size)
{
	int target, slot;

	for (target = 0; target < net->count; target++)
	{
		struct fake_node *node = &net->nodes[target];
		int copies = 1;

		if (!node->alive || !node->broker_ready[broker] || !fake_topic_subscribed(node, broker, topic))
			continue;
		if ((int)(fake_random(&net->rng) % 100) < net->loss_percent)
		{
			net->lost++;
			continue;
		}
		if ((int)(fake_random(&net->rng) % 100) < net->duplicate_percent)
		{
			copies = 2;
			net->duplicated++;
		}
		while (copies--)
		{
			for (slot = 0; slot < FAKE_MAX_PENDING && net->pending[slot].used; slot++)
				;
			if (slot == FAKE_MAX_PENDING)
				return;
			net->pending[slot].used = 1;
			net->pending[slot].node = target;
			net->pending[slot].broker = broker;
			net->pending[slot].due = net->time + (unsigned int)net->latency_ms + (net->jitter_ms ? (unsigned int)(fake_random(&net->rng) % (unsigned long long)net->jitter_ms) : 0);
			net->pending[slot].size = size;
			snprintf(net->pending[slot].topic, sizeof(net->pending[slot].topic), "%s", topic);
			memcpy(net->pending[slot].payload, payload, (size_t)size);
		}
	}
	(void)from;
}

static int fake_publish(void *user, const char *topic, const unsigned char *payload, int size)
{
	struct fake_node *node = user;
	struct fake_net *net = node->net;
	int broker, accepted = 0;

	if (net->tap)
		net->tap(net->tap_user, node->index, net->time, topic, payload, size);
	for (broker = 0; broker < FAKE_BROKERS; broker++)
	{
		if (!node->broker_ready[broker] || !net->broker_up[broker])
			continue;
		/* (a client past the limit has its publishes dropped without a word) */
		if (net->time - node->window_start[broker] >= 1000)
		{
			node->window_start[broker] = net->time;
			node->window_count[broker] = 0;
		}
		if (net->publish_limit && ++node->window_count[broker] > net->publish_limit)
		{
			node->limited++;
			net->limited++;
			accepted = 1;
			continue;
		}
		node->published++;
		fake_deliver_from(net, node->index, broker, topic, payload, size);
		accepted = 1;
	}
	return accepted;
}

static void fake_subscribe(void *user, const char (*topics)[SOCIAL_TOPIC_SIZE], int count)
{
	struct fake_node *node = user;

	if (count > SOCIAL_MAX_TOPICS)
		count = SOCIAL_MAX_TOPICS;
	memset(node->subscribed, 0, sizeof(node->subscribed));
	memcpy(node->subscribed, topics, (size_t)count * SOCIAL_TOPIC_SIZE);
	node->subscribed_count = count;
}

static void fake_log(void *user, const char *line)
{
	struct fake_node *node = user;

	snprintf(node->last_log[node->log_count % 8], sizeof(node->last_log[0]), "%s", line);
	node->log_count++;
	if (node->net->verbose)
		fprintf(stderr, "[%u node %d] social: %s\n", node->net->time, node->index, line);
}

static void fake_start(struct fake_net *net, unsigned long long seed)
{
	int broker;

	memset(net, 0, sizeof(*net));
	net->rng = seed * 2654435761ULL + 88172645463325252ULL;
	net->time = 1000;
	net->latency_ms = 40;
	net->publish_limit = 10;
	for (broker = 0; broker < FAKE_BROKERS; broker++)
		net->broker_up[broker] = 1;
}

/* a machine on the network, with brokers ready; wall_hours is the error of its wall clock */
static struct fake_node *fake_add(struct fake_net *net, int wall_hours, int anchored)
{
	struct fake_node *node = &net->nodes[net->count];
	struct social_env env;
	int broker;

	memset(node, 0, sizeof(*node));
	node->net = net;
	node->index = net->count++;
	node->alive = 1;
	node->rng = fake_random(&net->rng) | 1;
	node->mono_offset = (unsigned int)(fake_random(&net->rng) & 0xFFFFFFFFu);
	node->wall_offset = (long long)wall_hours * 3600;
	node->anchored = anchored;
	for (broker = 0; broker < FAKE_BROKERS; broker++)
		node->broker_ready[broker] = 1;
	memset(&env, 0, sizeof(env));
	env.user = node;
	env.now = fake_now;
	env.utc = fake_utc;
	env.random = fake_random_bytes;
	env.publish = fake_publish;
	env.subscribe = fake_subscribe;
	env.ready = fake_ready;
	env.log = fake_log;
	env.platform = node->index % 2 ? SOCIAL_PLATFORM_WIIU : SOCIAL_PLATFORM_LINUX;
	env.network_version = 41;
	{
		struct social_front_options options;

		memset(&options, 0, sizeof(options));
		node->front = social_front_create(&env, net->files ? &net->files[node->index] : NULL, &options);
		node->engine = social_front_engine(node->front);
	}
	fake_random_bytes(node, node->seed, 32);
	{
		char name[16];

		snprintf(name, sizeof(name), "Node%d", node->index);
		social_set_identity(node->engine, node->seed, name, 0);
	}
	social_set_enabled(node->engine, 1);
	return node;
}

/* a machine that starts again (the process restarted): a new engine and front from the files, the clocks and the network as they were. The identity is read
from friends.key (made from the node's seed the first time) */
static void fake_restart(struct fake_net *net, int index)
{
	struct fake_node *node = &net->nodes[index];
	struct social_env env = node->engine->env;
	struct social_front_options options;
	char name[16];

	social_front_destroy(node->front);
	memset(&options, 0, sizeof(options));
	node->front = social_front_create(&env, net->files ? &net->files[index] : NULL, &options);
	node->engine = social_front_engine(node->front);
	snprintf(name, sizeof(name), "Node%d", index);
	social_front_start(node->front, name, node->seed);
	social_set_enabled(node->engine, 1);
	memset(node->subscribed, 0, sizeof(node->subscribed));
	node->subscribed_count = 0;
}

/* advances the network by ms in steps of 10: due messages are delivered (in time order), then every live machine has its pass */
static void fake_run(struct fake_net *net, unsigned int ms)
{
	unsigned int end = net->time + ms;

	while (net->time < end)
	{
		int index;

		net->time += 10;
		for (;;)
		{
			int best = -1;

			for (index = 0; index < FAKE_MAX_PENDING; index++)
			{
				if (net->pending[index].used && (int)(net->time - net->pending[index].due) >= 0 &&
					(best < 0 || (int)(net->pending[best].due - net->pending[index].due) > 0))
				{
					best = index;
				}
			}
			if (best < 0)
				break;
			net->pending[best].used = 0;
			if (net->nodes[net->pending[best].node].alive)
			{
				net->delivered++;
				social_front_heard(net->nodes[net->pending[best].node].front, net->pending[best].topic, net->pending[best].payload, net->pending[best].size);
			}
		}
		for (index = 0; index < net->count; index++)
		{
			if (net->nodes[index].alive)
				social_front_tick(net->nodes[index].front);
		}
	}
}

#endif
