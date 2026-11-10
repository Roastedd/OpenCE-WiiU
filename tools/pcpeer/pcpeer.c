/*
PCPEER.C

The friends layer as a desktop program with no game: the real social layer (port/linux/game/social_*.c) and the real MQTT code of internet play
(port/linux/src/p2p_signal.c, with p2p_crypto.c and the game's own posix_net.c sockets), built unchanged, run over TCP to the public brokers of
halo/brokers.txt (or the game's defaults). What the game's p2p.c would supply is here in small stand-ins (the clock, the machine's key, name lookup, and the
pass); the glue is social_p2p.inc's, less hosting and joining, which need a game.

	pcpeer --dir <data dir> [--name <name>] [--console <port>] [--brokers <host:port,host:port,...>] [--share offline]

The data dir holds friends.key and friends.txt (the identity and the friends: a new directory is a new identity), brokers.txt (optional: one host:port per
line; the game's four defaults if it is absent) and nothing else. The log goes to standard error. The commands (social_command.h) are lines of text on a TCP
port of this machine only (127.0.0.1): "list", "code", "add <code>" ... (with or without "social " before them), a line a time; the answer follows and, when the
client has closed its sending side, the connection ends. tools/pcpeer/pcpeer_cmd.py does that for one command.

Platform stand-ins are in this file and are NOT the game's: p2p_now, halo_utc_now (the machine's own clock, taken as anchored), p2p_resolve, the machine's key.
*/

#include "platform.h"
#include "posix.h"
#include "port_config.h"
#include "p2p_internal.h"

#include "social_front.h"
#include "social_join.h"
#include "social_command.h"
#include "social_core.h"
#include "halo_port_limits.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* ---------- stand-ins for p2p.c, p2p_lobby.c, port_config.c and the platform layer */

pthread_mutex_t p2p_lock = PTHREAD_MUTEX_INITIALIZER;

static struct
{
	char dir[512];
	char name[64];
	char brokers[512];
	int console_port;
	int share_offline;
} options;

void platform_log(const char *format, ...)
{
	char line[1024];
	struct timeval now;
	struct tm local;
	va_list arguments;
	int length;

	gettimeofday(&now, NULL);
	localtime_r(&now.tv_sec, &local);
	length = snprintf(line, sizeof(line), "%02d:%02d:%02d.%03d ", local.tm_hour, local.tm_min, local.tm_sec, (int)(now.tv_usec / 1000));
	va_start(arguments, format);
	vsnprintf(line + length, sizeof(line) - (size_t)length, format, arguments);
	va_end(arguments);
	fprintf(stderr, "%s\n", line);
	fflush(stderr);
}

/* the milliseconds of a monotonic clock (a long, as GetTickCount is, and not near 0 at the start) */
unsigned long p2p_now(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (unsigned long)(now.tv_sec * 1000UL + (unsigned long)(now.tv_nsec / 1000000L)) + 100000UL;
}

/* the game anchors its clock to a TLS answer's Date; this machine's own clock is kept up by the operating system, so it is taken as the anchor */
unsigned long long halo_utc_now(void)
{
	return (unsigned long long)time(NULL);
}

long halo_utc_offset_seconds(int *anchored)
{
	*anchored = 1;
	return 0;
}

unsigned long p2p_resolve(const char *host)
{
	return posix_resolve_ipv4(host);
}

void p2p_hex(const unsigned char *bytes, int size, char *text)
{
	static const char digits[] = "0123456789abcdef";
	int index;

	for (index = 0; index < size; index++)
	{
		text[index * 2] = digits[bytes[index] >> 4];
		text[index * 2 + 1] = digits[bytes[index] & 15];
	}
	text[size * 2] = 0;
}

static unsigned char machine_seed[P2P_SEED_SIZE];
static unsigned char machine_public[P2P_KEY_SIZE];
static unsigned char machine_signing[P2P_KEY_SIZE];
static unsigned char machine_secret[P2P_KEY_SIZE];

const unsigned char *p2p_identifier(void)
{
	static unsigned char identifier[P2P_IDENTIFIER_SIZE];

	p2p_identifier_for(machine_public, identifier);
	return identifier;
}

const unsigned char *p2p_public_key(void)
{
	return machine_public;
}

void p2p_key_hash(const unsigned char *key, unsigned char *hash)
{
	unsigned char digest[P2P_SHA256_SIZE];

	p2p_sha256(key, P2P_KEY_SIZE, digest);
	memcpy(hash, digest, P2P_KEY_HASH_SIZE);
}

void p2p_identifier_from_hash(const unsigned char *hash, unsigned char *result)
{
	memcpy(result, hash, P2P_IDENTIFIER_SIZE);
	result[0] = (unsigned char)((result[0] & 0xFC) | 0x02);
}

void p2p_identifier_for(const unsigned char *key, unsigned char *result)
{
	unsigned char hash[P2P_KEY_HASH_SIZE];

	p2p_key_hash(key, hash);
	p2p_identifier_from_hash(hash, result);
}

int p2p_shared_secret(const unsigned char *key, unsigned char *shared)
{
	static const unsigned char zero[P2P_KEY_SIZE];

	p2p_x25519(shared, machine_secret, key);
	return !p2p_equal(shared, zero, P2P_KEY_SIZE);
}

void p2p_lobby_slot_topic(const unsigned char *key_hash, char *topic, int size)
{
	char text[2 * P2P_KEY_HASH_SIZE + 1];

	p2p_hex(key_hash, P2P_KEY_HASH_SIZE, text);
	snprintf(topic, (size_t)size, "%s%s", P2P_LOBBY_SLOT_PREFIX, text);
}

/* no game, no tunnel, no WebRTC, no server browser: nothing here is asked for, but p2p_signal.c links to them */
void p2p_lobby_slot_heard(const char *hash_text, const unsigned char *payload, int size, int retained)
{
	(void)hash_text, (void)payload, (void)size, (void)retained;
}

void p2p_lobby_query_heard(void)
{
}

int p2p_peer_offered(const unsigned char *identifier, const unsigned char *public_key, const unsigned char *secret, const struct p2p_candidate *candidates,
	int count, const struct p2p_webrtc *webrtc, int offered)
{
	(void)identifier, (void)public_key, (void)secret, (void)candidates, (void)count, (void)webrtc, (void)offered;
	return 0;
}

int p2p_peer_turned_away(const unsigned char *identifier, int is_host)
{
	(void)identifier, (void)is_host;
	return 1;
}

int p2p_peer_reoffered(const unsigned char *identifier, const unsigned char *secret, const struct p2p_candidate *candidates, int count,
	const struct p2p_webrtc *webrtc)
{
	(void)identifier, (void)secret, (void)candidates, (void)count, (void)webrtc;
	return 0;
}

int p2p_webrtc_ready(void)
{
	return 0;
}

int p2p_webrtc_describe(const unsigned char *identifier, int proven, struct p2p_webrtc *local, struct p2p_candidate *candidates, int maximum_count)
{
	(void)identifier, (void)proven, (void)candidates, (void)maximum_count;
	memset(local, 0, sizeof(*local));
	return 0;
}

void p2p_webrtc_new_request(void)
{
}

/* config.toml: there is none; these are the few settings p2p_signal.c asks for */
const char *config_string(const char *name)
{
	if (!strcmp(name, "network.brokers_file"))
		return "brokers.txt";
	return "";
}

void config_folder(char *path, size_t size)
{
	snprintf(path, size, "%s/", options.dir);
}

char *config_file_read(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	char *data;
	long length;

	if (!file)
		return NULL;
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
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

/* ---------- the friends layer, as social_p2p.inc makes it (less hosting and joining) */

static struct social_front *front;
static struct social_files files;
static struct social_posix_folder folder;
static volatile int quitting;

static unsigned int env_now(void *user)
{
	(void)user;
	return (unsigned int)p2p_now();
}

static unsigned long long env_utc(void *user, int *anchored)
{
	(void)user;
	halo_utc_offset_seconds(anchored);
	return halo_utc_now();
}

static void env_random(void *user, unsigned char *bytes, int size)
{
	(void)user;
	posix_random_bytes(bytes, (posix_ulong)size);
}

static int env_publish(void *user, const char *topic, const unsigned char *payload, int size)
{
	(void)user;
	return p2p_signal_social_publish(topic, payload, size) > 0;
}

static void env_subscribe(void *user, const char (*topics)[SOCIAL_TOPIC_SIZE], int count)
{
	(void)user;
	p2p_signal_social_topics(topics, count);
}

static int env_ready(void *user)
{
	(void)user;
	return p2p_signal_ready_count();
}

static void env_log(void *user, const char *line)
{
	(void)user;
	platform_log("social: %s", line);
}

/* a message the brokers delivered (p2p_signal.c calls this on the p2p thread, here the main one) */
void social_p2p_heard(const char *topic, const unsigned char *payload, int size)
{
	if (front)
		social_front_heard(front, topic, payload, size);
}

/* the join machine: a desktop peer with no game joins nothing and hosts nothing, so a friend's game is never "joinable" from here and begin refuses */
static unsigned int join_now(void *user)
{
	(void)user;
	return (unsigned int)p2p_now();
}

static int join_version(void *user)
{
	(void)user;
	return HALO_PORT_NETWORK_VERSION;
}

static int join_no(void *user)
{
	(void)user;
	return 0;
}

static int join_map_state(void *user, const char *map)
{
	(void)user, (void)map;
	return 2;
}

static int join_in_game_of(void *user, const unsigned char *host_hash)
{
	(void)user, (void)host_hash;
	return 0;
}

static int join_friend_now(void *user, unsigned int friend_id, int *online, struct social_join_target *target)
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

static int join_begin(void *user, const unsigned char *host_hash, const unsigned char *token)
{
	(void)user, (void)host_hash, (void)token;
	return 0;
}

static int join_progress(void *user, int *stage, char *reason, int size)
{
	(void)user;
	*stage = 0;
	if (size > 0)
		reason[0] = 0;
	return 0;
}

static void join_cancel(void *user)
{
	(void)user;
}

static int start_front(void)
{
	struct social_env env;
	struct social_front_options front_options;
	struct social_join_backend backend;
	unsigned char seed[SOCIAL_SEED_SIZE];
	char text[512];
	int written;

	snprintf(text, sizeof(text), "%s/", options.dir);
	memset(&env, 0, sizeof(env));
	env.now = env_now;
	env.utc = env_utc;
	env.random = env_random;
	env.publish = env_publish;
	env.subscribe = env_subscribe;
	env.ready = env_ready;
	env.log = env_log;
	env.platform = SOCIAL_PLATFORM_MACOS;
	env.network_version = HALO_PORT_NETWORK_VERSION;
	social_files_posix(&files, &folder, text);
	memset(&front_options, 0, sizeof(front_options));
	front_options.saver_thread = 1;
	front = social_front_create(&env, &files, &front_options);
	if (!front)
		return 0;
	posix_random_bytes(seed, sizeof(seed));
	written = social_front_start(front, options.name, seed);
	memset(seed, 0, sizeof(seed));
	if (written)
		platform_log("social: friends.key could not be written; the identity lasts this run only");
	social_set_enabled(social_front_engine(front), 1);
	social_engine_set_share(social_front_engine(front), !options.share_offline, SOCIAL_DETAILS_BASIC, 1);
	memset(&backend, 0, sizeof(backend));
	backend.now = join_now;
	backend.version = join_version;
	backend.busy = join_no;
	backend.in_game_of = join_in_game_of;
	backend.map_state = join_map_state;
	backend.friend_now = join_friend_now;
	backend.begin = join_begin;
	backend.progress = join_progress;
	backend.cancel = join_cancel;
	social_join_set_backend(&backend);
	social_global_front = front;
	return 1;
}

/* ---------- the commands, on a local TCP port */

static pthread_mutex_t command_lock = PTHREAD_MUTEX_INITIALIZER;

static void console_answer(int client, const char *line)
{
	static char reply[16384];
	char text[512];
	const char *command = line;
	int length;

	while (*command == ' ')
		command++;
	if (!strncmp(command, "social", 6) && (command[6] == ' ' || !command[6]))
		command += 6;
	while (*command == ' ')
		command++;
	snprintf(text, sizeof(text), "%s", command);
	pthread_mutex_lock(&command_lock);
	reply[0] = 0;
	if (!text[0] || !strcmp(text, "quit"))
	{
		snprintf(reply, sizeof(reply), "%s\n", text[0] ? "stopping" : "empty line; try: help");
		if (text[0])
			quitting = 1;
	}
	else if (!social_command(text, reply, (int)sizeof(reply)))
		snprintf(reply, sizeof(reply), "not a command: %s\n", text);
	pthread_mutex_unlock(&command_lock);
	length = (int)strlen(reply);
	if (length && reply[length - 1] != '\n')
		reply[length++] = '\n', reply[length] = 0;
	platform_log("console: %s -> %d bytes", text, length);
	while (length > 0)
	{
		ssize_t sent = send(client, reply + strlen(reply) - length, (size_t)length, 0);

		if (sent <= 0)
			return;
		length -= (int)sent;
	}
}

static void *console_client(void *argument)
{
	int client = (int)(long)argument;
	char buffer[1024];
	int size = 0;

	for (;;)
	{
		ssize_t count = recv(client, buffer + size, sizeof(buffer) - 1 - (size_t)size, 0);
		char *newline;

		if (count <= 0)
			break;
		size += (int)count;
		buffer[size] = 0;
		while ((newline = strchr(buffer, '\n')) != NULL)
		{
			*newline = 0;
			if (newline > buffer && newline[-1] == '\r')
				newline[-1] = 0;
			console_answer(client, buffer);
			size -= (int)(newline + 1 - buffer);
			memmove(buffer, newline + 1, (size_t)size + 1);
		}
		if (size >= (int)sizeof(buffer) - 1)
			size = 0;
	}
	if (size > 0)
	{
		/* (a last line with no newline, then the end of the client's sending) */
		buffer[size] = 0;
		console_answer(client, buffer);
	}
	close(client);
	return NULL;
}

static void *console_server(void *argument)
{
	int listener = (int)(long)argument;

	for (;;)
	{
		int client = accept(listener, NULL, NULL);
		pthread_t thread;

		if (client < 0)
			continue;
		if (pthread_create(&thread, NULL, console_client, (void *)(long)client) == 0)
			pthread_detach(thread);
		else
			close(client);
	}
	return NULL;
}

static int console_start(int port)
{
	struct sockaddr_in address;
	int listener = socket(AF_INET, SOCK_STREAM, 0);
	int yes = 1;
	pthread_t thread;

	if (listener < 0)
		return 0;
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = htons((unsigned short)port);
	if (bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(listener, 4) != 0)
	{
		platform_log("pcpeer: cannot listen on 127.0.0.1:%d: %s", port, strerror(errno));
		close(listener);
		return 0;
	}
	if (pthread_create(&thread, NULL, console_server, (void *)(long)listener) != 0)
		return 0;
	pthread_detach(thread);
	return 1;
}

/* ---------- the pass (p2p.c's thread, less everything that is the game's) */

static void stop_signal(int number)
{
	(void)number;
	quitting = 1;
}

static void pass(int wait_milliseconds)
{
	enum { MAXIMUM = 16 };
	int read[MAXIMUM], write[MAXIMUM], error[MAXIMUM];
	int read_count = 0, write_count = 0, error_count = 0;

	p2p_signal_select_sets(read, &read_count, write, &write_count, MAXIMUM);
	if (posix_socket_select(read, &read_count, write, &write_count, error, &error_count, 0, wait_milliseconds * 1000, 0) < 0)
		read_count = write_count = 0;
	p2p_signal_update(read, read_count, write, write_count);
	/* social_p2p_tick: the place (a desktop peer in the menus), the brokers asked for, the engine's pass */
	{
		struct social_place place;

		memset(&place, 0, sizeof(place));
		place.activity = SOCIAL_MENUS;
		social_engine_set_place(social_front_engine(front), &place);
	}
	p2p_signal_start();
	social_front_tick(front);
}

int main(int argc, char **argv)
{
	int index;
	char path[600];
	FILE *file;

	snprintf(options.brokers, sizeof(options.brokers), "%s", "");
	options.console_port = 2323;
	for (index = 1; index < argc; index++)
	{
		if (!strcmp(argv[index], "--dir") && index + 1 < argc)
			snprintf(options.dir, sizeof(options.dir), "%s", argv[++index]);
		else if (!strcmp(argv[index], "--name") && index + 1 < argc)
			snprintf(options.name, sizeof(options.name), "%s", argv[++index]);
		else if (!strcmp(argv[index], "--console") && index + 1 < argc)
			options.console_port = atoi(argv[++index]);
		else if (!strcmp(argv[index], "--brokers") && index + 1 < argc)
			snprintf(options.brokers, sizeof(options.brokers), "%s", argv[++index]);
		else if (!strcmp(argv[index], "--share") && index + 1 < argc)
			options.share_offline = !strcmp(argv[++index], "offline");
		else
		{
			fprintf(stderr, "usage: pcpeer --dir <data dir> [--name <name>] [--console <port>] [--brokers host:port,...] [--share offline]\n");
			return 2;
		}
	}
	if (!options.dir[0])
	{
		fprintf(stderr, "pcpeer: --dir is required (a folder of its own: the identity and the friends are kept in it)\n");
		return 2;
	}
	mkdir(options.dir, 0700);
	if (options.brokers[0])
	{
		/* (a list on the command line is written as the folder's brokers.txt, as the card's would be) */
		char *cursor;

		snprintf(path, sizeof(path), "%s/brokers.txt", options.dir);
		file = fopen(path, "w");
		if (file)
		{
			for (cursor = options.brokers; *cursor; cursor++)
				fputc(*cursor == ',' ? '\n' : *cursor, file);
			fputc('\n', file);
			fclose(file);
		}
	}
	signal(SIGTERM, stop_signal);
	signal(SIGINT, stop_signal);
	signal(SIGPIPE, SIG_IGN);
	/* a key for the brokers' client side (the own lobby slot's name); the friends layer's identity is its own (friends.key) */
	posix_random_bytes(machine_seed, sizeof(machine_seed));
	p2p_ed25519_public(machine_seed, machine_signing, machine_secret);
	p2p_x25519(machine_public, machine_secret, NULL);
	if (!start_front())
	{
		platform_log("pcpeer: the friends layer could not start");
		return 1;
	}
	if (!console_start(options.console_port))
		return 1;
	platform_log("pcpeer: pid %d, data dir %s, console 127.0.0.1:%d", (int)getpid(), options.dir, options.console_port);
	while (!quitting)
		pass(50);
	platform_log("pcpeer: stopping; friends told offline");
	social_shutdown(social_front_engine(front));
	/* a few passes to send it, then the connections closed cleanly */
	for (index = 0; index < 10; index++)
		pass(50);
	social_front_save_now(front);
	p2p_signal_lobby_quit();
	social_front_destroy(front);
	return 0;
}
