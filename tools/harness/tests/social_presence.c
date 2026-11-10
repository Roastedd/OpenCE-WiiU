#include "harness.h"

/* What the friends layer is told of this machine's game, from the game's side and the platform layer's: the code that makes the place friends see (social_p2p.inc's social_place_make with
internet play's hosting test, p2p.c's hosting_wanted) and the activity the game tells (menu_screens.inl's scr_social_activity), on stand-ins for the engine's world. A split screen game
(a game on this machine, bots or no bots) has a server that listens as any does; it must never be hosted, shown as joinable or given a player count of the tunnel. */

#include "social_core.h"

enum
{
	P2P_MAXIMUM_PEERS = 127,
};
struct peer
{
	int used, connected, is_host;
};
/* the parts of internet play's state the code under test reads */
static struct
{
	int hosting_socket, hosting, hosting_lan_only, game_local, has_token;
	unsigned char token[SOCIAL_TOKEN_SIZE];
	int game_player_count, game_player_maximum;
	struct peer peers[P2P_MAXIMUM_PEERS];
} p2p;
static int configured_friends_can_join = 1;
static int activity_told;
static int lobby_open = 1, lobby_in_progress;

static int connected_player_count(void)
{
	int count = 0, index;

	for (index = 0; index < P2P_MAXIMUM_PEERS; index++)
		count += p2p.peers[index].used && p2p.peers[index].connected && !p2p.peers[index].is_host;
	return count;
}
static int social_activity_get(void)
{
	return activity_told;
}
static void p2p_lobby_game_details(char *map, int map_size, char *gametype, int gametype_size, int *engine_type, int *open, int *in_progress, int *has_password)
{
	snprintf(map, (size_t)map_size, "bloodgulch");
	snprintf(gametype, (size_t)gametype_size, "Slayer");
	*engine_type = 1;
	*open = lobby_open;
	*in_progress = lobby_in_progress;
	*has_password = 0;
}
static int config_boolean(char const *name)
{
	(void)name;
	return configured_friends_can_join;
}
static unsigned char const *p2p_public_key(void)
{
	static unsigned char key[32] = { 1 };

	return key;
}
static void p2p_key_hash(unsigned char const *key, unsigned char *hash)
{
	memcpy(hash, key, SOCIAL_HASH_SIZE);
}

/* the game's side: what the activity is told from */
static int in_progress_now, splitscreen_local, server_listening, client_pregame, connection_local;
#define _game_connection_local 0
#define _client_state_pregame 3
static int game_in_progress(void) { return in_progress_now; }
static int game_connection(void) { return connection_local ? _game_connection_local : 2; }
static int network_game_is_splitscreen_local(void) { return splitscreen_local; }
static void *global_network_game_client_get(void) { static int client; return client_pregame ? &client : NULL; }
static void *global_network_game_server_get(void) { static int server; return server_listening ? &server : NULL; }
static short network_game_client_get_state(void *client, short *data) { (void)client; (void)data; return client_pregame ? _client_state_pregame : -1; }

#include "presence_code.inc"

static void hosted_game(int players, int maximum)
{
	memset(&p2p, 0, sizeof(p2p));
	p2p.hosting_socket = 7;
	p2p.hosting = 1;
	p2p.has_token = 1;
	p2p.token[0] = 9;
	p2p.game_player_count = players;
	p2p.game_player_maximum = maximum;
	activity_told = SOCIAL_LOBBY;
	lobby_open = 1;
	lobby_in_progress = 0;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	struct social_place place;

	CASE("hosted-16")
	{
		/* a game hosted for friends with 16 places: the players the game says and its most; joinable */
		hosted_game(3, 16);
		social_place_make(&place);
		CHECK(place.hosting && place.joinable && place.activity == SOCIAL_LOBBY, "hosting %d joinable %d activity %d", place.hosting, place.joinable, place.activity);
		CHECK(place.players == 3 && place.maximum_players == 16, "%d of %d", place.players, place.maximum_players);
		CHECK(!strcmp(place.map, "bloodgulch") && place.has_invite, "map and invite");
		lobby_in_progress = 1;
		activity_told = SOCIAL_GAME;
		hosted_game(16, 16);
		lobby_open = 0;
		social_place_make(&place);
		CHECK(place.players == 16 && place.maximum_players == 16 && !place.joinable, "a full game: %d of %d, joinable %d", place.players, place.maximum_players, place.joinable);
	}
	else CASE("not-said-yet")
	{
		/* before the game has said how many places it has: not the tunnel's 128, and not joinable on a guess */
		hosted_game(0, 0);
		p2p.peers[0].used = p2p.peers[0].connected = 1;
		p2p.peers[1].used = p2p.peers[1].connected = 1;
		social_place_make(&place);
		CHECK(place.maximum_players == 0 && place.players == 3 && !place.joinable, "%d of %d joinable %d", place.players, place.maximum_players, place.joinable);
	}
	else CASE("local-game")
	{
		/* a split screen game of one player and three bots: its server listens (hosting_socket) and internet play was allowed before; told as solo by the game */
		hosted_game(4, 128);
		p2p.game_local = 1;
		CHECK(!hosting_wanted(), "a local game is not hosted for the internet");
		p2p.hosting = 1;
		activity_told = SOCIAL_SOLO;
		social_place_make(&place);
		CHECK(!place.hosting && !place.joinable && !place.has_invite && place.players == 0 && place.maximum_players == 0, "hosting %d joinable %d players %d of %d", place.hosting,
			place.joinable, place.players, place.maximum_players);
		/* ... and if the game had told a lobby or a game (it did, as "GAME"), the platform layer still does not make it a place to join */
		activity_told = SOCIAL_GAME;
		social_place_make(&place);
		CHECK(!place.hosting && !place.joinable && !place.has_invite, "told as a game: hosting %d joinable %d", place.hosting, place.joinable);
	}
	else CASE("hosting-wanted")
	{
		memset(&p2p, 0, sizeof(p2p));
		p2p.hosting_socket = 4;
		CHECK(hosting_wanted(), "a game that listens, internet allowed, is hosted");
		p2p.hosting_lan_only = 1;
		CHECK(!hosting_wanted(), "a LAN game is not");
		p2p.hosting_lan_only = 0;
		p2p.hosting_socket = -1;
		CHECK(!hosting_wanted(), "no game listening");
		p2p.hosting_socket = 4;
		p2p.game_local = 1;
		CHECK(!hosting_wanted(), "a local game is not");
	}
	else CASE("activity")
	{
		/* the game's word: menus; a lobby of a hosted game; a game of it; a campaign; and a split screen game, which is solo in its lobby and in play, bots or no bots */
		memset(&p2p, 0, sizeof(p2p));
		CHECK(scr_social_activity() == SOCIAL_ACTIVITY_MENUS, "menus");
		server_listening = 1;
		CHECK(scr_social_activity() == SOCIAL_ACTIVITY_LOBBY, "a hosted lobby");
		in_progress_now = 1;
		CHECK(scr_social_activity() == SOCIAL_ACTIVITY_GAME, "a hosted game");
		connection_local = 1;
		CHECK(scr_social_activity() == SOCIAL_ACTIVITY_SOLO, "a campaign");
		connection_local = 0;
		splitscreen_local = 1;
		CHECK(scr_social_activity() == SOCIAL_ACTIVITY_SOLO, "a split screen game in play (the server is a network server's, as any hosted game's)");
		in_progress_now = 0;
		CHECK(scr_social_activity() == SOCIAL_ACTIVITY_SOLO, "a split screen game's lobby");
		splitscreen_local = 0;
		server_listening = 0;
		client_pregame = 1;
		CHECK(scr_social_activity() == SOCIAL_ACTIVITY_LOBBY, "a lobby joined");
	}
	else
		return 2;
	return 0;
}
