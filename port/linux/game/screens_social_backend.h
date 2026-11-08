/*
SCREENS_SOCIAL_BACKEND.H

The friends layer (social.h, social_join.h) as the social screens' backend table (screens.h's struct screens_provider): the friends, requests and blocked lists, the party, the
toasts (a friend online, a request, an invitation, the party's events, the results of what the player asked), the answers to an invitation and the actions on a friend or a
party member. The roster is not here: that is the game's own (menu_screens.inl), which asks this file only which of its players are friends or party members.

No game types: the harness builds this file alone against the friends layer (tools/harness/tests/screens_social_check.c). What only the game knows (a clock, the network version)
comes through screens_social_backend_hooks.
*/

#ifndef HALO_SCREENS_SOCIAL_BACKEND_H
#define HALO_SCREENS_SOCIAL_BACKEND_H

#include "screens.h"

struct screens_social_hooks
{
	/* milliseconds, any origin (the toasts' holds and spacing); NULL: toasts without holds */
	unsigned long (*milliseconds)(void);
	/* the network version this build plays (a friend of another is NEWER BUILD or OLDER BUILD); NULL: not told */
	int (*network_version)(void);
};

void screens_social_backend_hooks(struct screens_social_hooks const *hooks);

/* what has been told and seen forgotten (tests: the layer the table reads is another one, or the game started again) */
void screens_social_backend_reset(void);

/* the table; a build sets it with screens_backend_set once the friends layer is on (menu_screens.inl) */
struct screens_provider const *screens_social_backend(void);

/* FRIEND and PARTY (SCREENS_ROW_FRIEND, SCREENS_ROW_PARTY) for a player of the game's roster, by the name: an online friend that is in a lobby or a game (and, when both
sides name the map, the same map), a party member that is online. The game does not know a player's friend code, so a stranger who has copied a friend's name looks like the
friend: the tag is a hint, and nothing is decided by it. */
unsigned short screens_social_roster_flags(char const *name, char const *map);

/* the friend of a roster row (by the same rule), for an action on it: the friend's id, or 0 */
unsigned int screens_social_roster_friend(char const *name, char const *map);

/* a line for the screen the join is shown on: nonzero (and the words in line) while a join the player asked for goes on or has just failed; 0 when there is none */
int screens_social_join_line(char *line, int size);

/* how many a screen is asked about (tests) */
long screens_social_backend_changes(void);

/* the words of a text in upper case (the screens' own), into a buffer; the length */
int screens_social_upper(char *destination, int size, char const *source);

#endif
