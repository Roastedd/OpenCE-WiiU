/*
COLLISION_USAGE.H

header included in hcex build.
*/

#ifndef __COLLISION_USAGE_H
#define __COLLISION_USAGE_H
#pragma once

#include "cseries/cseries.h"
#include <xtl.h>

/* ---------- constants */

enum
{
	_collision_user_unknown,
	_collision_user_ai_look,
	_collision_user_ai_lineofsight,
	_collision_user_ai_comms,
	_collision_user_ai_lineoffire,
	_collision_user_ai_melee,
	_collision_user_aim_assist,
	_collision_user_bipeds,
	_collision_user_melee,
	_collision_user_decals,
	_collision_user_area_damage,
	_collision_user_items,
	_collision_user_observer,
	_collision_user_point_physics,
	_collision_user_projectiles,
	_collision_user_structure_lighting,
	_collision_user_sounds,
	_collision_user_vehicles,
	_collision_user_limp_body_physics,
	_collision_user_objects,
	_collision_user_ui,
	_collision_user_debugging,
	NUMBER_OF_COLLISION_USER_TYPES,
};

enum
{
	_collision_function_vector_structure,
	_collision_function_vector_objects,
	_collision_function_vector_bounds_object,
	_collision_function_vector_intersect_model,
	_collision_function_vector_intersect_bsp_object,
	_collision_function_vector_intersect_bsp_structure,
	_collision_function_sphere_intersect_bsp_object,
	_collision_function_sphere_intersect_bsp_structure,
	NUMBER_OF_COLLISION_FUNCTION_TYPES,
};

enum
{
	MAXIMUM_COLLISION_USER_STACK_DEPTH = 32,
};

/* ---------- macros */

/* ---------- structures */

/* ---------- prototypes/COLLISION_USAGE.C */

/* port: the log of the collision tests is compiled out on the Wii U (HALO_WIIU). Its counts and its periods were globals written by whatever
thread made a test, which the ticks on a thread of their own would share with the frame's code, and only a debug display
(collision_log_render_enable) read them. Each of the tests asks for a count and, with the time shown (collision_log_time, off unless a console command sets it), a clock:
here that is nothing. The stack of users (global_current_collision_users) stays, as each thread's own. */
#ifdef HALO_WIIU
#define collision_log_render() ((void)0)
#define collision_log_start_time(start_time) ((void)((start_time)->QuadPart = 0))
#define collision_log_end_time(collision_function, end_time) ((void)0)
#define collision_log_usage(collision_function) ((void)0)
#define collision_log_display(buffer) ((void)0)
#define collision_log_begin_period(time_period) ((void)0)
#define collision_log_continue_period(time_period) ((void)0)
#define collision_log_end_period() ((void)0)
#endif

void collision_log_initialize(
	void);
void collision_log_enable(
	boolean enable);
#ifndef HALO_WIIU
void collision_log_render(
	void);
void collision_log_start_time(
	LARGE_INTEGER *start_time);
void collision_log_end_time(
	short collision_function,
	__int64 end_time);
void collision_log_usage(
	short collision_function);
void collision_log_display(
	char *buffer);
void collision_log_begin_period(
	short time_period);
void collision_log_continue_period(
	short time_period);
void collision_log_end_period(
	void);
#endif

/* ---------- globals */

extern __thread short global_current_collision_user_depth;
extern __thread short global_current_collision_users[MAXIMUM_COLLISION_USER_STACK_DEPTH];

/* ---------- public code */

#endif // __COLLISION_USAGE_H
