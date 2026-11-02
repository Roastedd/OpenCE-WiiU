/*
WIDGETS.H

header included in hcex build.
*/

#ifndef __WIDGETS_H
#define __WIDGETS_H
#pragma once

/* ---------- headers */

#include "cseries/cseries.h"
#include "math/real_math.h"
#include "memory/data.h"

/* ---------- constants */

/* ---------- structures */

struct render_animation;
struct render_lighting;

struct widget_datum
{
	struct datum_header header;
	short type;
	long type_datum_index;
	long next_widget_index;
};

typedef char widget_datum_size_assert[
	sizeof(struct widget_datum) == 0xC ? 1 : -1];

/* ---------- macros */

#define widget_get(widget_index) \
	((struct widget_datum *)datum_get(widget_data, (widget_index)))

/* ---------- prototypes/WIDGETS.C */

void widgets_initialize(
	void);
void widgets_initialize_for_new_map(
	void);
void widgets_dispose_from_old_map(
	void);
void widgets_dispose(
	void);
short tag_group_to_widget_type(
	unsigned long group_tag);
void widgets_new(
	long object_index);
void widgets_delete(
	long object_index);
boolean widgets_need_lighting(
	long widget_index);
void widgets_render(
	long object_index,
	struct render_lighting const *lighting,
	struct render_animation const *animation);
#ifdef HALO_WIIU
/* port: the widgets (the flags, antennas, glows and light volumes an object carries) are made, deleted and stepped by the ticks, and drawn and stepped by
the frame, which with the ticks beside the drawing (tick_overlap.h) are two threads: a lock around each of those, taken while a tick thread exists. The
ticks hold it to make, delete and step the antennas (game_tick), the frame to draw an object's widgets and step the flags. A widget the frame's copy of
the object names that the ticks have let go of since is skipped. */
void widgets_lock(void);
void widgets_unlock(void);
#endif
void widgets_update(
	real delta);

/* ---------- globals */

extern struct data_array *widget_data;

/* ---------- public code */

#endif // __WIDGETS_H
