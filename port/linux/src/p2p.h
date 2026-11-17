/*
P2P.H

Internet play (p2p.c): machines that shared an invite reach each other's
system link games as if they were on one LAN. xnet.c routes the game's
traffic for them through here; sdl_platform.c passes invite links in and
out through the clipboard.

Addresses and ports are in network byte order.
*/

#ifndef __HALO_LINUX_P2P_H
#define __HALO_LINUX_P2P_H

/* starts internet play, if network.online is set, when the game starts
its networking; local_address is the address the game's sockets are
reached at (network.address, else 127.0.0.1) */
void p2p_initialize(unsigned long local_address);

/* on the desktop, before anything else: if this process was started with
an invite link (halo://join/...) and another copy of the game is running,
passes the link to it and returns nonzero (this one should quit) */
int p2p_hand_off_invite(void);

/* joins the game an invite link or code leads to; text may hold other
words around it. Returns nonzero if it held an invite */
int p2p_join_invite(const char *text);

/* this machine's identifier, which its XNADDR carries (6 bytes) */
const unsigned char *p2p_identifier(void);
/* the address the game reaches the machine with this identifier at, if it
is an internet play peer (reached yet or not) */
int p2p_peer_address(const unsigned char *identifier, unsigned long *address);
/* the ping to the machine with this identifier, in milliseconds, if the
tunnel reaches it; -1 if not, or not timed yet */
int p2p_peer_ping(const unsigned char *identifier);

/* a destination the game sends to or connects to (stream: a TCP socket;
socket: the game's UDP socket connected to it, else -1): 1 if it is a peer's
address, rewritten to the local address standing in for it; -1 if it is a
peer's (or was) but the peer cannot be reached now (the game's traffic must
not go to the address itself); 0 if it is not a peer's */
int p2p_outgoing(int stream, int socket, unsigned long *address, unsigned short *port);
/* a source the game received from, accepted from or is connected to: if
it is one standing in for a peer, the peer's address */
int p2p_incoming(int stream, unsigned long *address, unsigned short *port);
/* the local addresses standing in for this port (a broadcast's) on every
peer; returns their count */
int p2p_broadcast_targets(unsigned short port, unsigned long *addresses, unsigned short *ports, int maximum_count);
/* a datagram the game sends from a socket with the local port source_port
(bound: not 0) to address and port, sealed onto the tunnel at once (no
stand-in carries it; one is made for the answers when they come): 1 if it
is a peer's address (sent, or lost as a datagram may be); -1 if it is a
peer's (or was) but the peer cannot be reached now; 0 if it is not a
peer's */
int p2p_send_datagram(unsigned short source_port, unsigned long address, unsigned short port, const void *data,
	int size);
/* the same for a broadcast to port: to every peer the tunnel reaches;
returns their count */
int p2p_broadcast_datagram(unsigned short source_port, unsigned short port, const void *data, int size);

/* the game's socket has this local port: bound, given one, or listening
(stream and listening: it is hosting). Peers reach only these ports (a
stream's only while it listens), and datagram ports it sent them from */
void p2p_socket_port(int socket, int stream, int listening, unsigned short port);
/* a socket that peers are not to reach (bound to 127.0.0.1 alone) has this
local port: traffic from it is not a peer's whose stand-in had the port */
void p2p_port_taken(int stream, unsigned short port);
/* the game closes a socket, a datagram one of this local port (0: none, or
a stream's) */
void p2p_socket_closed(int socket, unsigned short datagram_port);
/* the game connected a datagram socket to address and port (as it named
them, a peer's or not): what peers send it in the process (below) is only
that one's, as the system passes a connected socket only its peer's */
void p2p_socket_connected(int socket, unsigned long address, unsigned short port);

/* ---- in the process (network.tunnel_in_process): the peers' datagrams and
connections are handed to the game's sockets here, not through stand-in
sockets of their own (a UDP socket for each port of each peer, and two
sockets for each connection), so a host's sockets do not grow with its
peers (the Wii U gives a program 27). Nothing another machine sees changes.
xnet.c asks these before the system's sockets; each takes the p2p lock, or
the inboxes' own */

/* whether internet play hands the game its peers' traffic in the process */
int p2p_in_process(void);
/* the oldest datagram a peer sent the game's datagram socket, waiting in
its inbox: copied (as much as length takes), with the peer's address and
port; its whole size (more than length: cut short), or -1 if none waits */
int p2p_datagram_receive(int socket, void *buffer, int length, unsigned long *address, unsigned short *port);
/* how many datagrams wait in the socket's inbox, and through bytes (if not
NULL) their bytes, as Winsock's FIONREAD counts them */
int p2p_datagram_waiting(int socket, int *bytes);
/* whether a connection a peer opened to this listening socket of the
game's waits to be taken */
int p2p_stream_waiting(int listening_socket);
/* takes the oldest: the number the game knows it by (no system socket has
it, p2p_stream_socket), the peer's address and port; -1 if none waits */
int p2p_stream_accept(int listening_socket, unsigned long *address, unsigned short *port);
/* whether the number is a connection taken in the process (one closed, or
not known, too: none of them is the system's) */
int p2p_stream_socket(int socket);
/* such a connection's calls, as Winsock's, a failure the negative of its
Winsock error: bytes read (0: the peer closed it, or it ended), bytes taken
(as many as the tunnel's window has room for), and closing it */
int p2p_stream_receive(int socket, void *buffer, int length);
int p2p_stream_send(int socket, const void *buffer, int length);
int p2p_stream_close(int socket);
/* whether reads and sends wait (they do not by default, as an accepted
socket of a listening socket that does not block) */
int p2p_stream_set_blocking(int socket, int blocking);
/* what a select would say of it: something to read (or its end), room to
send; 0, or the negative of the error if the number is none */
int p2p_stream_ready(int socket, int *readable, int *writeable);
/* ... and of each of a select's list of the game's sockets (list 0 read, 1
write, 2 error): such a connection's, and in the read list a datagram
socket's inbox and a listening socket's connections waiting, into ready
(one byte each). Whether any is ready; -1 if a connection's number is none */
int p2p_select_ready(const int *sockets, int count, int list, unsigned char *ready);
/* the bytes waiting to be read */
int p2p_stream_waiting_bytes(int socket);
/* its addresses: the peer's (peer), or this end's (the listening port, on
the address the game's stand-ins have) */
int p2p_stream_address(int socket, int peer, unsigned long *address, unsigned short *port);

/* what internet play has done since the game started, for the netcode's
"net:" lines (network_distributed.c) */
struct p2p_statistics
{
	/* the machines the tunnel reaches (not the server browser's probes) */
	int peers;
	/* the tunnel's packets to and from every peer, sealed, and their bytes;
	sends the system refused (the packet is lost) */
	unsigned long packets_sent;
	unsigned long bytes_sent;
	unsigned long packets_received;
	unsigned long bytes_received;
	unsigned long send_failures;
	/* in the process: datagrams handed to the game's sockets, and lost (a
	full inbox, no socket of the game's with the port, or a socket connected
	elsewhere); the connections the game holds */
	unsigned long datagrams_delivered;
	unsigned long datagrams_lost;
	int streams;
};
void p2p_statistics(struct p2p_statistics *statistics);
/* ... and the game's own traffic with other machines, which xnet.c counts
at the game's Winsock calls (not its own client's over the loopback):
datagrams and their bytes each way, its connections' bytes each way, and
its sends the system refused (the datagram lost), the last one's Winsock
error */
struct xnet_statistics
{
	unsigned long datagrams_sent;
	unsigned long datagram_bytes_sent;
	unsigned long datagrams_received;
	unsigned long datagram_bytes_received;
	unsigned long stream_bytes_sent;
	unsigned long stream_bytes_received;
	unsigned long send_failures;
	int last_send_error;
};
void xnet_statistics(struct xnet_statistics *statistics);

/* text for the clipboard (a new invite link), once; NULL if none. Called
from the main thread */
const char *p2p_take_clipboard_text(void);

/* whether a game this machine hosts may be joined from the internet (an
invite, Discord): the PC menus' Create Game > Internet (the default), not
> LAN */
void p2p_set_hosting_allowed(int allowed);
/* the invite link of the game this machine hosts (empty, 0: none, or LAN
only) */
int p2p_invite_link(char *link, int size);
/* an invite (a link or a code, or any text holding one) as the log shows
it: its first digits only, since players post their logs and an invite lets
anyone join; the dedicated server's log, its operator's, has it whole */
const char *p2p_invite_log_text(const char *invite, char *text, int size);

/* whether the game this machine's server holds is a local one (split screen: it takes no connection from another machine). A local game is never hosted for the internet,
listed, shown to friends as a game to join or given an invite, whatever allowed hosting (p2p_set_hosting_allowed) before. The server sets it as it is made, before it listens,
and clears it as it goes */
void p2p_set_game_local(int local);

/* the hosted game's players and the most it takes, which Discord shows
(0, 0: not hosting; until the game says, the machines the tunnel reaches
are shown). The game's server calls it as they change (calling it with the
same counts again costs little) */
void p2p_set_game_player_counts(int count, int maximum);

/* ---- the server browser's public games (p2p_lobby.c) */

/* whether the game this machine hosts for the internet (not LAN) is listed
in everyone's server browser: Create Game's PUBLIC; a private game is
reached only by its invite. Going private makes a new invite, if the old one
was listed */
void p2p_set_hosting_public(int public);
/* the game hosted as last told (p2p_set_game_listing), for the friends layer: its map and type (printable ASCII), its engine, whether it takes players
(open) and is under way, and whether it has a password. Under p2p_lock, on the p2p thread */
void p2p_lobby_game_details(char *map, int map_size, char *gametype, int gametype_size, int *engine_type, int *open, int *in_progress, int *has_password);
/* whether the game hosted is listed in the server browser (public, with the lobby on): friends see a game that is not, or that has a password, only by an invitation. Under p2p_lock, on the p2p thread */
int p2p_lobby_listed_locked(void);

/* whether the game this machine hosts is listed in everyone's server
browser (PUBLIC, and network.public_lobby): the text chat is off in one a
player hosts (a dedicated server's follows network.text_chat) */
int p2p_hosting_listed(void);
/* the password of the game hosted for the internet (Server Setup's
PASSWORD; NULL or empty: none): its listing lets only those who know it
join from the server browser (its invite link still joins it). Setting or
changing it makes a new invite, if the old one was listed. It takes a few
milliseconds (the password's key) */
void p2p_set_hosting_password(const char *password);
/* the hosted game's details as listed (printable ASCII is kept; NULL leaves
one as it was): the game's server calls it as they change (calling it with
the same again costs little) */
void p2p_set_game_listing(const char *name, const char *map, const char *gametype, int engine_type, int kind,
	int open, int in_progress, int has_teams);
/* the server browser: while on, the public games are gathered (asked for
when turned on, and by p2p_lobby_refresh) */
void p2p_lobby_browse(int on);
void p2p_lobby_refresh(void);

enum
{
	P2P_LISTING_NAME_SIZE = 32,
	P2P_LISTING_MAP_SIZE = 32,
	P2P_LISTING_GAMETYPE_SIZE = 24,
	/* an invite link's text (P2P_LINK_SIZE) */
	P2P_LISTING_INVITE_SIZE = 77,
	/* a password's game's: its host's key and its key's hash, and its token
	sealed (P2P_KEY_SIZE, P2P_KEY_HASH_SIZE, P2P_SEALED_TOKEN_SIZE) */
	P2P_LISTING_SIGNING_KEY_SIZE = 32,
	P2P_LISTING_KEY_HASH_SIZE = 16,
	P2P_LISTING_SEALED_TOKEN_SIZE = 56,
	/* a master server's word on a listed server (master_list.c) */
	P2P_LISTING_PUBLISHER_SIZE = 32,
	P2P_LISTING_REGION_SIZE = 16,
	/* the masters that list it, by their key ids (a Discord link's pass is
	shown from one of them: player_link.c) */
	P2P_LISTING_MAXIMUM_MASTERS = 4,
	P2P_LISTING_MASTER_KEY_ID_SIZE = 8,
};

struct p2p_listing
{
	/* (empty for a locked one until p2p_listing_unlock opens it) */
	char invite[P2P_LISTING_INVITE_SIZE];
	/* the host's (its XNADDR's abEnet once reached) */
	unsigned char identifier[6];
	char name[P2P_LISTING_NAME_SIZE + 1];
	char map[P2P_LISTING_MAP_SIZE + 1];
	char gametype[P2P_LISTING_GAMETYPE_SIZE + 1];
	unsigned char player_count, maximum_player_count, engine_type;
	/* the game's kind: an OpenCE gametype's mode, and Hardcore
	(port/linux/game/game_modes.h's game_modes_kind) */
	unsigned char kind;
	unsigned char open, in_progress, has_teams;
	/* joining it failed this run (p2p_lobby_mark_failed) */
	unsigned char failed;
	/* milliseconds, -1 if not known */
	short ping;
	/* its host is being probed for the ping now (a ping known stays), and
	whether Refresh asked for it (until that probe ends) */
	unsigned char probing, refreshing;
	/* it has a password: its invite is sealed with it (p2p_listing_unlock) */
	unsigned char locked;
	unsigned char key_hash[P2P_LISTING_KEY_HASH_SIZE];
	unsigned char signing_key[P2P_LISTING_SIGNING_KEY_SIZE];
	unsigned char sealed_token[P2P_LISTING_SEALED_TOKEN_SIZE];
	/* a dedicated server's, on a master server's word (its listing's own
	flag, which its host sets, is not taken: a game the brokers alone list
	is shown as the players'); listed by the master servers
	(master_list.c): by how many, its publisher's name and its region
	(empty: none said) */
	unsigned char dedicated;
	unsigned char master;
	unsigned char master_count;
	char publisher[P2P_LISTING_PUBLISHER_SIZE + 1];
	char region[P2P_LISTING_REGION_SIZE + 1];
	/* the network version its host plays (another's cannot be joined) */
	unsigned short network_version;
	/* the key ids of the masters that listed it this time the browser was
	open (master_list.c): the masters its server publishes to, of those this
	machine asks */
	unsigned char master_key_ids[P2P_LISTING_MAXIMUM_MASTERS][P2P_LISTING_MASTER_KEY_ID_SIZE];
	unsigned char master_key_id_count;
};
/* joins a public game the server browser found (p2p_join_invite with its
invite), remembering its host: nonzero if it held an invite */
int p2p_join_listing(const struct p2p_listing *listing);
/* whether this machine is in the public game it joined from the server
browser: the tunnel still reaches its host, and no other invite was joined
since. Such a game has no text chat, whatever its host says
(port/linux/game/network_chat.c), unless it is a dedicated server's: */
int p2p_joined_listed_host(void);
/* whether the public game joined (p2p_joined_listed_host) is a dedicated
server's that a master server lists: an approved operator's, whose chat
follows its network.text_chat (a listing's own dedicated flag, which any
host may set, is not enough) */
int p2p_joined_listed_dedicated(void);
/* the key ids of the masters that listed the game being joined, if it was
joined from the server browser (master_key_ids): how many, 0 if not known (an
invite's, a LAN's) */
int p2p_joined_listing_masters(unsigned char (*key_ids)[P2P_LISTING_MASTER_KEY_ID_SIZE], int maximum);
/* a copy of the public games found, in the order shown (the most players
first; then those not failed, the open ones, by name); returns their count */
int p2p_lobby_games(struct p2p_listing *games, int maximum_count);
/* the games the server browser shows, in its order (its sort, after its
tab and Filters): their hosts are probed in that order, those not shown
last */
void p2p_lobby_shown(const struct p2p_listing *games, int count);
/* the master servers' (port/linux/src/master_list.c, server_master.c): a
fresh signed listing of the game this machine hosts for the internet, as
the brokers get it (0 if it hosts none, or none public); a master's entry's
listing taken into the server browser's games (its signature checked) with
the master's word on it, 1 if it was; and the masters' lists fetched again
(each game's count of them starts over) */
int p2p_lobby_host_listing(unsigned char *bytes, int size);
/* network.public_address as last looked up (p2p.c), "a.b.c.d:port": 0 if it
is unset or cannot be looked up (an IPv6 one, which the tunnel does not use) */
int p2p_public_address(char *text, int size);
int p2p_lobby_take_master_listing(const unsigned char *payload, int size, const char *publisher, const char *region,
	int network_version_shown, int dedicated, const unsigned char *master_key_id);
void p2p_lobby_masters_heard(void);
/* how many hosts list a game of a newer network version, which is not
shown (an old build sees none of the games) */
int p2p_lobby_newer_games(void);
/* a game the browser could not join: kept, marked failed, for this run */
void p2p_lobby_mark_failed(const unsigned char *identifier);
/* whether a signalling broker answers (the player-hosted games' lists come
through them) */
int p2p_lobby_brokers_reached(void);

/* ---- what the server browser shows of the games found, and in what order:
its source (its top-left tab), Filters and the column titles (the menus'
network.browser_* settings). Pure, over a copy of the games (the harness
tests them) */

/* network.browser_source */
enum
{
	P2P_LOBBY_SOURCE_ALL,
	P2P_LOBBY_SOURCE_DEDICATED,
	P2P_LOBBY_SOURCE_PLAYERS,
};
/* network.browser_maps: games on maps this machine lacks shown, not, or
those it could download as it joins */
enum
{
	P2P_LOBBY_MAPS_YES,
	P2P_LOBBY_MAPS_NO,
	P2P_LOBBY_MAPS_DOWNLOADABLE,
};
/* what a game's map is to this machine */
enum
{
	P2P_LOBBY_MAP_INSTALLED,
	P2P_LOBBY_MAP_DOWNLOADABLE,
	P2P_LOBBY_MAP_MISSING,
};
/* network.browser_sort's orders (a column title's), each in its own first
direction: dedicated servers first, then the most players, then by name
(servers); the most players; by name, map or type (A to Z); those with a
password first */
enum
{
	P2P_LOBBY_SORT_SERVERS,
	P2P_LOBBY_SORT_PLAYERS,
	P2P_LOBBY_SORT_NAME,
	P2P_LOBBY_SORT_MAP,
	P2P_LOBBY_SORT_TYPE,
	P2P_LOBBY_SORT_LOCKED,
	NUMBER_OF_P2P_LOBBY_SORTS,
};
struct p2p_lobby_choices
{
	int source;
	int show_empty, show_full;
	/* one game engine's only (1 CTF, 2 Slayer, 3 Oddball, 4 King, 5 Race),
	0 any */
	int engine_type;
	int maps;
};
/* the games the choices show, kept at the front in their order; returns
how many. map_state tells what a game's map is to this machine (NULL: every
map installed). A game of another network version is kept: shown, greyed,
not joinable */
int p2p_lobby_filter(struct p2p_listing *games, int count, const struct p2p_lobby_choices *choices,
	int (*map_state)(const char *map));
/* the games in the order's (reverse: the other way), those of another
network version last; equal ones keep their order */
void p2p_lobby_sort(struct p2p_listing *games, int count, int order, int reverse);
/* network.browser_sort's text: an order's name, "-" before it reversed
("-players"); an unknown one is servers' */
int p2p_lobby_sort_parse(const char *text, int *reverse);
void p2p_lobby_sort_text(int order, int reverse, char *text, int size);

/* the join of an invite (or a listing) under way: its stage, and once it
failed why, as a word ("" while it goes on): "no_answer" (the host never
answered), "no_route" (it answered, and the tunnel never reached it); or
the host's game refused it (p2p_join_refused): "full", "closed", "banned",
"password", "version" or "token" (its invite was replaced). Returns the
stage */
enum
{
	P2P_JOIN_NONE,
	/* asking the brokers for the host */
	P2P_JOIN_ASKING,
	/* the host answered: the tunnel being made (hole punching) */
	P2P_JOIN_ANSWERED,
	/* the tunnel reaches the host: the game's own join follows */
	P2P_JOIN_CONNECTED,
	P2P_JOIN_FAILED,
};
int p2p_join_progress(int *stage, char *reason, int size);
/* gives up the join under way (an invite's host being asked, or its tunnel being made); nothing is under way after it */
void p2p_join_cancel(void);
/* the host of the game joined refused it (the game's rejection code:
network_game_protocol.h's _rejection_code_*), the game's client says */
void p2p_join_refused(int rejection_code);
/* a locked game's invite (into its invite), if the password is its own:
FALSE if not; TRUE for a game with none. It takes a few milliseconds */
int p2p_listing_unlock(struct p2p_listing *listing, const char *password);

/* the sizes of a Discord user's id and name as kept (with their end), and
the text kept of either as told: only the characters allowed (digits in an
id; letters, digits, "_", "." and "-" in a name), no longer than that */
enum
{
	P2P_DISCORD_ID_SIZE = 24,
	P2P_DISCORD_NAME_SIZE = 40,
};
void p2p_discord_sanitize(char *destination, int size, const char *source, int name);
/* the Discord user signed in to the client on this machine, as its
READY told (empty if none, or internet play is off) */
void p2p_discord_identity(char *id, int id_size, char *name, int name_size);
/* this machine's hardware id, as hex (empty if none), a host told it when
joining (a keyed hash of what the machine is known by: p2p.c); and the hex
kept of one told: lowercase hex digits only, P2P_HARDWARE_ID_BYTES' worth */
enum
{
	P2P_HARDWARE_ID_BYTES = 16,
	P2P_HARDWARE_ID_SIZE = 2 * P2P_HARDWARE_ID_BYTES + 1,
};
void p2p_hardware_id(char *hex, int size);
void p2p_hardware_id_sanitize(char *destination, int size, const char *source);

/* the real address (network byte order) of the internet play peer of this
virtual address (network byte order): where its packets come from; 0 if
it is no peer's */
unsigned long p2p_peer_endpoint_address(unsigned long virtual_address);

/* ---- map sharing between the players of a game (port/linux/game/map_share.c)

The players of an internet game reach its host alone. To share a map they
reach each other too: a host tells a player the key hash of another, which
answers holders of the invite it joined with as its host does (only while
answering), and the player asks it for a session as it asked the host. */

enum
{
	/* the hash of a machine's public key, which reaches it */
	P2P_PLAYER_KEY_HASH_SIZE = 16,
	/* the port each machine takes the others' map sharing connections on:
	its game listens there without hosting */
	P2P_MAP_SHARE_PORT = 5153,
};

/* the identifier (6 bytes) and key hash of the internet play peer of this
virtual address (network byte order): 0 if it is no peer's */
int p2p_peer_key_hash(unsigned long virtual_address, unsigned char *identifier, unsigned char *key_hash);
/* the binding of the internet play tunnel's session with the peer of this
virtual address (network byte order), for a Discord link's pass
(player_link_binding of the session's secret, which both ends share and
nobody else has); 0 if it is no peer's */
int p2p_peer_binding(unsigned long virtual_address, unsigned char *binding);
/* whether this machine, a player of an internet game (not its host),
answers the game's other players: holders of the invite it joined with */
void p2p_set_answering_players(int on);
/* asks for a session with another player of the internet game this
machine joined, whose key has this hash (as the game's host told), if there
is none: nonzero if there is one, or it is asked for (for some seconds, or
until reached; asked again, it is asked for anew) */
int p2p_reach_player(const unsigned char *key_hash);
/* whether the tunnel reaches the machine with this identifier, and the
virtual address (network byte order) the game reaches it at */
int p2p_peer_reached(const unsigned char *identifier, unsigned long *address);
/* this network's NAT gives each destination its own port and no router
forwards the tunnel's: only games behind more lenient NATs can be reached */
int p2p_nat_strict(void);

/* BLAKE2b with a 32-byte digest (Monocypher's): a map's pieces
(map_share.c) */
void p2p_blake2b_256(const void *data, unsigned long size, unsigned char *digest);

#endif
