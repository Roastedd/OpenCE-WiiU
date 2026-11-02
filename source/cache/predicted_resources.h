/*
PREDICTED_RESOURCES.H

header included in hcex build.
*/

#ifndef __PREDICTED_RESOURCES_H
#define __PREDICTED_RESOURCES_H
#pragma once

/* ---------- constants */

enum
{
	_predicted_resource_bitmap,
	_predicted_resource_sound
};

/* ---------- macros */

/* ---------- structures */

struct predicted_resource
{
	short type;
	short resource_index;
	long tag_index;
};

/* ---------- prototypes/PREDICTED_RESOURCES.C */

void predicted_resources_precache(struct tag_block *predicted_resources);
#ifdef HALO_WIIU
/* port: a bitmap's or a sound wave's ask of the texture or sound cache from tick code, queued for the game thread with the
tick owners' queues on (port/linux/game/tick_owners.h) and made at once otherwise */
struct bitmap_data;
struct sound_permutation;
void predicted_resources_request_bitmap(struct bitmap_data *bitmap);
void predicted_resources_request_sound(struct sound_permutation *permutation);
#endif

/* ---------- globals */

/* ---------- public code */

#endif // __PREDICTED_RESOURCES_H
