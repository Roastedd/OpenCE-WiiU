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
static struct { int valid; struct social_joined_game game; } joined;
static int joined_reached;
static int joined_listed_invite_locked(unsigned char *hash, unsigned char *token)
{
	if (!joined_reached)
		return 0;
	memset(hash, 0x3C, SOCIAL_HASH_SIZE);
	memset(token, 0xC3, SOCIAL_TOKEN_SIZE);
	return 1;
}
static int lobby_open = 1, lobby_in_progress, lobby_listed = 1, lobby_password;

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
	*has_password = lobby_password;
}
static int p2p_lobby_listed_locked(void)
{
	return lobby_listed;
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
static int in_progress_now, splitscreen_local, server_listening, client_pregame, connection_local, menu_scene;
#define _game_connection_local 0
#define _client_state_pregame 3
static int game_in_progress(void) { return in_progress_now; }
static int game_time_menu_scene(void) { return menu_scene; }
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
	lobby_listed = 1;
	lobby_password = 0;
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
		client_pregame = 1;
		CHECK(scr_social_activity() == SOCIAL_ACTIVITY_SOLO, "a split screen game's lobby");
		client_pregame = 0;
		splitscreen_local = 0;
		server_listening = 0;
		client_pregame = 1;
		CHECK(scr_social_activity() == SOCIAL_ACTIVITY_LOBBY, "a lobby joined");
	}
	else CASE("private")
	{
		/* a public game takes friends by its presence; a private one (not listed in the server browser) or one with a password only by an invitation, which carries the way in */
		hosted_game(2, 16);
		social_place_make(&place);
		CHECK(place.joinable && !place.private_game && place.has_invite, "public: joinable %d private %d", place.joinable, place.private_game);
		lobby_listed = 0;
		social_place_make(&place);
		CHECK(!place.joinable && place.private_game && place.has_invite && place.hosting && place.players == 2 && place.maximum_players == 16,
			"private: joinable %d private %d invite %d", place.joinable, place.private_game, place.has_invite);
		lobby_listed = 1;
		lobby_password = 1;
		social_place_make(&place);
		CHECK(!place.joinable && place.private_game && place.has_invite, "a password: joinable %d private %d", place.joinable, place.private_game);
		lobby_password = 0;
		configured_friends_can_join = 0;
		social_place_make(&place);
		CHECK(!place.joinable && !place.private_game && place.has_invite, "friends cannot join: joinable %d", place.joinable);
		configured_friends_can_join = 1;
		/* a public game with a room left and the lobby open is joinable with the real count and most; a full one is not */
		hosted_game(16, 16);
		lobby_open = 0;
		social_place_make(&place);
		CHECK(!place.joinable && !place.private_game && place.players == 16 && place.maximum_players == 16, "full public: joinable %d", place.joinable);
	}
	else CASE("joined-public")
	{
		/* a public game this machine joined and reached: friends and the party get its way in (map, type, players, most), though this machine hosts nothing */
		memset(&p2p, 0, sizeof(p2p));
		joined.valid = 1;
		snprintf(joined.game.map, sizeof(joined.game.map), "chillout");
		snprintf(joined.game.gametype, sizeof(joined.game.gametype), "CTF");
		joined.game.players = 6;
		joined.game.maximum_players = 16;
		joined.game.engine_type = 2;
		joined.game.open = 1;
		activity_told = SOCIAL_GAME;
		joined_reached = 1;
		social_place_make(&place);
		CHECK(!place.hosting && place.has_invite && place.joinable && !strcmp(place.map, "chillout") && !strcmp(place.gametype, "CTF") && place.players == 6 && place.maximum_players == 16 &&
			place.invite_hash[0] == 0x3C && place.invite_token[0] == 0xC3, "joined: invite %d joinable %d map %s %d of %d", place.has_invite, place.joinable, place.map, place.players, place.maximum_players);
		/* not reached (a game joined by an invitation has no such way in; or the host is not yet reached) */
		joined_reached = 0;
		social_place_make(&place);
		CHECK(!place.has_invite && !place.joinable, "not reached: no way in told");
		joined_reached = 1;
		/* a full game has the way in told, and is not joinable */
		joined.game.players = 16;
		social_place_make(&place);
		CHECK(place.has_invite && !place.joinable && place.players == 16, "a full game: joinable %d", place.joinable);
		joined.game.players = 6;
		joined.game.open = 0;
		social_place_make(&place);
		CHECK(!place.joinable, "a game not taking players");
		joined.game.open = 1;
		configured_friends_can_join = 0;
		social_place_make(&place);
		CHECK(!place.joinable, "friends cannot join");
		configured_friends_can_join = 1;
		/* the menus, and a campaign, tell nothing */
		activity_told = SOCIAL_MENUS;
		social_place_make(&place);
		CHECK(!place.has_invite && !place.joinable, "in the menus");
		activity_told = SOCIAL_SOLO;
		social_place_make(&place);
		CHECK(!place.has_invite && !place.joinable, "solo");
		/* a game it hosts is its own, not the joined one */
		activity_told = SOCIAL_LOBBY;
		hosted_game(2, 8);
		social_place_make(&place);
		CHECK(place.hosting && place.maximum_players == 8 && place.invite_hash[0] != 0x3C, "hosting wins");
		joined.valid = 0;
	}
	else CASE("menus-after-a-game")
	{
		/* the main menu's scene ticks as a game, with the connection the last game left ("local"): that is the menus, not solo (the console, 2026-11-10: a friend saw "online solo" at the main menu after a
		split screen game) */
		memset(&p2p, 0, sizeof(p2p));
		in_progress_now = 1;
		menu_scene = 1;
		connection_local = 1;
		CHECK(scr_social_activity() == SOCIAL_ACTIVITY_MENUS, "the main menu's scene with a local connection: %d", scr_social_activity());
		/* the split screen game's server still there with no lobby of its own: the menus */
		splitscreen_local = 1;
		server_listening = 1;
		CHECK(scr_social_activity() == SOCIAL_ACTIVITY_MENUS, "a leftover split screen server at the main menu: %d", scr_social_activity());
		/* its lobby: solo (never a lobby to join) */
		client_pregame = 1;
		CHECK(scr_social_activity() == SOCIAL_ACTIVITY_SOLO, "its lobby: %d", scr_social_activity());
		client_pregame = 0;
		splitscreen_local = 0;
		server_listening = 0;
		/* a campaign, a map playing alone: solo */
		menu_scene = 0;
		CHECK(scr_social_activity() == SOCIAL_ACTIVITY_SOLO, "a campaign: %d", scr_social_activity());
		/* a game of this machine's with others: a game; hosted or joined; and a lobby before it */
		connection_local = 0;
		CHECK(scr_social_activity() == SOCIAL_ACTIVITY_GAME, "a game with others: %d", scr_social_activity());
		in_progress_now = 0;
		menu_scene = 1;
		server_listening = 1;
		CHECK(scr_social_activity() == SOCIAL_ACTIVITY_LOBBY, "hosting its lobby: %d", scr_social_activity());
		server_listening = 0;
		client_pregame = 1;
		CHECK(scr_social_activity() == SOCIAL_ACTIVITY_LOBBY, "a friend's lobby joined: %d", scr_social_activity());
		client_pregame = 0;
		CHECK(scr_social_activity() == SOCIAL_ACTIVITY_MENUS, "and out of it: %d", scr_social_activity());
	}
	else
		return 2;
	return 0;
}
