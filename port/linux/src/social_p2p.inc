/*
SOCIAL_P2P.INC

The friends layer in the game's process (this file is included at the end of p2p.c, which is one of the platform layer's units: the engine itself is in port/linux/game): the one social_front (social_front.h) that social.h's calls reach, wired to internet play's brokers (p2p_signal.c) and the p2p
thread's pass (p2p.c), to the clock (halo_utc_now), to the log ("social: ..." lines), and to the files beside config.toml (friends.key, friends.txt).

It is off unless the player turns it on: the config key social.enabled, or the card file social.txt (a 1 turns it on, a 0 off, whichever the config says): it is new,
and untried on the console. Off, nothing here runs but one check: no thread, no socket, no file, no subscription, and p2p.c's pass calls nothing.

The p2p thread is the engine's thread. A thread of its own reads the files at the start (the card is slow) and then the engine is let loose; the saver thread writes them.
*/

#include "../game/social_front.h"
#include "../game/social_join.h"

typedef char social_topic_count_assert[SOCIAL_MAX_TOPICS == P2P_SOCIAL_MAXIMUM_TOPICS ? 1 : -1];
typedef char social_topic_size_assert[SOCIAL_TOPIC_SIZE == P2P_SOCIAL_TOPIC_SIZE ? 1 : -1];

extern struct social_front *social_global_front;

#ifdef HALO_WIIU
int wiiu_in_foreground(void);
#endif

enum
{
	_social_off,
	_social_loading,
	_social_running,
};

static struct
{
	volatile int state;
	struct social_front *front;
	struct social_files files;
	struct social_posix_folder folder;
	int quit_registered;
	/* what the game last said it is doing (social_set_activity) */
	volatile int activity;
} social;

/* ---------- the environment */

static unsigned int social_env_now(void *user)
{
	(void)user;
	return (unsigned int)p2p_now();
}

static unsigned long long social_env_utc(void *user, int *anchored)
{
	(void)user;
	halo_utc_offset_seconds(anchored);
	return halo_utc_now();
}

static void social_env_random(void *user, unsigned char *bytes, int size)
{
	(void)user;
	posix_random_bytes(bytes, (posix_ulong)size);
}

static int social_env_publish(void *user, const char *topic, const unsigned char *payload, int size)
{
	(void)user;
	return p2p_signal_social_publish(topic, payload, size) > 0;
}

static void social_env_subscribe(void *user, const char (*topics)[SOCIAL_TOPIC_SIZE], int count)
{
	(void)user;
	p2p_signal_social_topics(topics, count);
}

static int social_env_ready(void *user)
{
	(void)user;
	return p2p_signal_ready_count();
}

static void social_env_log(void *user, const char *line)
{
	(void)user;
	platform_log("social: %s", line);
}

#ifdef HALO_WIIU
static int social_front_foreground(void *user)
{
	(void)user;
	return wiiu_in_foreground();
}
#endif

static int social_platform_code(void)
{
#if defined(HALO_WIIU)
	return SOCIAL_PLATFORM_WIIU;
#elif defined(HALO_ANDROID)
	return SOCIAL_PLATFORM_ANDROID;
#elif defined(HALO_WEB)
	return SOCIAL_PLATFORM_WEB;
#elif defined(_WIN32)
	return SOCIAL_PLATFORM_WINDOWS;
#elif defined(__APPLE__)
	return SOCIAL_PLATFORM_MACOS;
#else
	return SOCIAL_PLATFORM_LINUX;
#endif
}

/* ---------- joining (social_join.h): what the join machine asks of internet play, on the game's thread */

static unsigned int social_join_now(void *user)
{
	(void)user;
	return (unsigned int)p2p_now();
}

static int social_join_version(void *user)
{
	(void)user;
	return HALO_PORT_NETWORK_VERSION;
}

static int social_join_busy(void *user)
{
	int busy;

	(void)user;
	pthread_mutex_lock(&p2p_lock);
	busy = p2p.hosting || joined_host() != NULL;
	pthread_mutex_unlock(&p2p_lock);
	return busy || social_join_hook_busy();
}

static int social_join_in_game_of(void *user, const unsigned char *host_hash)
{
	int together;

	(void)user;
	pthread_mutex_lock(&p2p_lock);
	together = joined_host() != NULL && !memcmp(p2p.join_host_hash, host_hash, SOCIAL_HASH_SIZE);
	pthread_mutex_unlock(&p2p_lock);
	return together;
}

static int social_join_map_state(void *user, const char *map)
{
	(void)user;
	return social_join_hook_map_state(map);
}

/* the friend as the snapshot last had it (the game thread's own copy) */
static int social_join_friend_now(void *user, unsigned int friend_id, int *online, struct social_join_target *target)
{
	static struct social_snapshot snapshot;
	int index;

	(void)user;
	*online = 0;
	social_snapshot_get(&snapshot);
	for (index = 0; index < snapshot.friend_count; index++)
	{
		if (snapshot.friends[index].id == friend_id)
		{
			*online = snapshot.friends[index].online;
			if (snapshot.friends[index].online && snapshot.friends[index].joinable)
			{
				*target = snapshot.friends[index].join;
				return 1;
			}
			return 0;
		}
	}
	return 0;
}

static int social_join_begin(void *user, const unsigned char *host_hash, const unsigned char *token)
{
	char link[P2P_LINK_SIZE];

	(void)user;
	memcpy(link, "halo://join/", 12);
	social_hex(host_hash, SOCIAL_HASH_SIZE, link + 12);
	social_hex(token, SOCIAL_TOKEN_SIZE, link + 12 + 2 * SOCIAL_HASH_SIZE);
	return p2p_join_invite(link);
}

static int social_join_progress(void *user, int *stage, char *reason, int size)
{
	(void)user;
	return p2p_join_progress(stage, reason, size);
}

static int social_join_game_ready(void *user, const unsigned char *host_hash)
{
	(void)user;
	return social_join_hook_game_ready(host_hash);
}

static void social_join_cancel_backend(void *user)
{
	(void)user;
	p2p_join_cancel();
}

static int social_join_strict_nat(void *user)
{
	(void)user;
	return p2p_nat_strict();
}

/* ---------- starting */

/* whether it is on: the card file social.txt in the config's folder says (1 or 0), else the config key */
static int social_switched_on(const char *folder)
{
	char path[640];
	FILE *file;
	int value = -1;

	snprintf(path, sizeof(path), "%ssocial.txt", folder);
	file = fopen(path, "r");
	if (file)
	{
		if (fscanf(file, "%d", &value) != 1)
			value = -1;
		fclose(file);
	}
	if (value == 0 || value == 1)
		return value;
	return config_boolean("social.enabled");
}

static int social_details_from_text(const char *text)
{
	if (text && !strcmp(text, "none"))
		return SOCIAL_DETAILS_NONE;
	if (text && !strcmp(text, "full"))
		return SOCIAL_DETAILS_FULL;
	return SOCIAL_DETAILS_BASIC;
}

static void social_atexit(void);

static void *social_loader(void *argument)
{
	struct social_env env;
	struct social_front_options options;
	struct social_front *front;
	unsigned char seed[SOCIAL_SEED_SIZE];
	char folder[512];
	int written;

	(void)argument;
	config_folder(folder, sizeof(folder));
	if (!social_switched_on(folder))
	{
		social.state = _social_off;
		return NULL;
	}
	memset(&env, 0, sizeof(env));
	env.now = social_env_now;
	env.utc = social_env_utc;
	env.random = social_env_random;
	env.publish = social_env_publish;
	env.subscribe = social_env_subscribe;
	env.ready = social_env_ready;
	env.log = social_env_log;
	env.platform = social_platform_code();
	env.network_version = HALO_PORT_NETWORK_VERSION;
	social_files_posix(&social.files, &social.folder, folder);
	memset(&options, 0, sizeof(options));
	options.saver_thread = 1;
#ifdef HALO_WIIU
	options.foreground = social_front_foreground;
#endif
	front = social_front_create(&env, &social.files, &options);
	if (!front)
	{
		platform_log("social: out of memory; friends are off");
		social.state = _social_off;
		return NULL;
	}
	/* a new identity, if there is none, from the machine's random bytes and the time of this moment */
	posix_random_bytes(seed, sizeof(seed));
	{
		unsigned int now = (unsigned int)p2p_now();
		int index;

		for (index = 0; index < 4; index++)
			seed[index] ^= (unsigned char)(now >> (8 * index));
	}
	written = social_front_start(front, config_string("social.name"), seed);
	memset(seed, 0, sizeof(seed));
	if (written)
		platform_log("social: friends.key could not be written; the identity lasts this run only");
	social_set_enabled(social_front_engine(front), 1);
	social_engine_set_share(social_front_engine(front), config_boolean("social.share_status"), social_details_from_text(config_string("social.share_details")),
		config_boolean("social.requests"));
	{
		struct social_join_backend backend;

		memset(&backend, 0, sizeof(backend));
		backend.now = social_join_now;
		backend.version = social_join_version;
		backend.busy = social_join_busy;
		backend.in_game_of = social_join_in_game_of;
		backend.map_state = social_join_map_state;
		backend.friend_now = social_join_friend_now;
		backend.begin = social_join_begin;
		backend.progress = social_join_progress;
		backend.game_ready = social_join_game_ready;
		backend.cancel = social_join_cancel_backend;
		backend.strict_nat = social_join_strict_nat;
		social_join_set_backend(&backend);
	}
	social.front = front;
	if (!social.quit_registered)
	{
		social.quit_registered = 1;
		atexit(social_atexit);
	}
	__sync_synchronize();
	social_global_front = front;
	social.state = _social_running;
	return NULL;
}

void social_p2p_initialize(void)
{
	pthread_t thread;

	if (social.state != _social_off)
		return;
	social.state = _social_loading;
	if (pthread_create(&thread, NULL, social_loader, NULL) != 0)
	{
		social.state = _social_off;
		return;
	}
	pthread_detach(thread);
}

int social_p2p_wanted(void)
{
	return social.state == _social_running;
}

void social_p2p_heard(const char *topic, const unsigned char *payload, int size)
{
	if (social.state == _social_running)
		social_front_heard(social.front, topic, payload, size);
}

/* what the machine is doing and could be joined at: the game's word (social_set_activity), and what internet play knows of the game hosted. Under p2p_lock */
static void social_place_make(struct social_place *place)
{
	memset(place, 0, sizeof(*place));
	place->activity = (unsigned char)(social.activity ? social.activity : SOCIAL_MENUS);
	if (p2p.hosting && p2p.has_token && place->activity != SOCIAL_SOLO)
	{
		int count, maximum, engine_type, open, in_progress, has_password;

		hosted_player_counts(&count, &maximum);
		p2p_lobby_game_details(place->map, sizeof(place->map), place->gametype, sizeof(place->gametype), &engine_type, &open, &in_progress, &has_password);
		place->hosting = 1;
		/* (a game being set up is a lobby, one going is a game) */
		if (place->activity == SOCIAL_MENUS || place->activity == SOCIAL_LOBBY || place->activity == SOCIAL_BUSY)
			place->activity = in_progress ? SOCIAL_GAME : SOCIAL_LOBBY;
		place->players = (unsigned char)(count < 0 ? 0 : count > 255 ? 255 : count);
		place->maximum_players = (unsigned char)(maximum < 0 ? 0 : maximum > 255 ? 255 : maximum);
		place->engine_type = (unsigned char)engine_type;
		place->private_game = (unsigned char)has_password;
		place->has_invite = 1;
		p2p_key_hash(p2p_public_key(), place->invite_hash);
		memcpy(place->invite_token, p2p.token, SOCIAL_TOKEN_SIZE);
		/* (friends can join an internet game that takes players, unless the player turned that off) */
		place->joinable = (unsigned char)(open && config_boolean("social.friends_can_join"));
	}
}

void social_p2p_tick(void)
{
	struct social_place place;

	if (social.state != _social_running)
		return;
	social_place_make(&place);
	social_engine_set_place(social_front_engine(social.front), &place);
	/* (the brokers are asked for; the pass of the engine) */
	p2p_signal_start();
	social_front_tick(social.front);
}

void social_p2p_quit(void)
{
	if (social.state == _social_running)
		social_shutdown(social_front_engine(social.front));
}

/* the process ends: friends are told offline at once (the p2p thread is waiting for the lock, or in its select) */
static void social_atexit(void)
{
	pthread_mutex_lock(&p2p_lock);
	social_p2p_quit();
	pthread_mutex_unlock(&p2p_lock);
}

void social_set_activity(int activity)
{
	social.activity = activity;
}
