/*
SCREENS_VIEW.H

The GamePad's social view made from the social model (screens_view.c, no game types). What the caller gives: the model, the tab and the quick chat page, the page's four lines, the toast
state and the clock, and whether the provider can act on a friend or on the bots; what it gets: the view (halo_social.h) and the keys of the friends the rows show. The chat's lines
are the caller's to put in the view afterwards.
*/

#ifndef HALO_SCREENS_VIEW_H
#define HALO_SCREENS_VIEW_H

#include "halo_social.h"
#include "screens.h"

struct screens_view_input
{
	struct screens_model const *model;
	int tab;
	char const *tiles[HALO_SOCIAL_TILES];
	char const *page_name;
	struct screens_toast_state const *toasts;
	unsigned long now;
	/* the provider has friends at all (an empty list says why), can join or invite a friend, can add and drop bots */
	int has_friends;
	int can_friend_action;
	int can_bots;
};

void screens_view_make(struct halo_social_view *view, struct screens_view_input const *input, long *row_keys);

#endif
