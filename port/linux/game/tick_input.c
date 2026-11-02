/*
TICK_INPUT.C

The snapshot of the input the tick side reads (tick_input.h).
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "input/input.h"
#include "input/input_abstraction.h"
#include "networking/network_connection.h" /* MAXIMUM_NUMBER_OF_LOCAL_PLAYERS */
#include "tick_owners.h"
#include "tick_input.h"

#ifdef HALO_WIIU

/* (tick_input.h's names are macros for the tick side's files; here the input layer's own are called) */
#undef input_has_gamepad
#undef input_get_gamepad_state
#undef input_abstraction_get_input_state
#undef input_get_mouse_state
#undef input_key_is_down
#undef input_abstraction_port_reload
#undef input_abstraction_port_accept
#undef input_abstraction_port_action_only
#undef input_abstraction_port_primary_trigger

enum
{
	/* the keys, and the four that are not in the list (shift, control, windows, alt) */
	TICK_INPUT_KEY_COUNT = NUMBER_OF_KEYS + 5,
	/* (struct game_input_state and struct mouse_state, whose definitions are the input layer's and player_control.c's) */
	TICK_INPUT_STATE_BYTES = 0x1C,
	TICK_INPUT_MOUSE_BYTES = 0x10,
};

struct tick_input_snapshot
{
	boolean has_gamepad[MAXIMUM_GAMEPADS];
	boolean has_gamepad_state[MAXIMUM_GAMEPADS];
	struct gamepad_state gamepads[MAXIMUM_GAMEPADS];
	boolean has_input_state[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];
	/* (words, for the structs' alignment) */
	unsigned long input_states[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS][TICK_INPUT_STATE_BYTES / 4];
	boolean has_mouse;
	unsigned long mouse[TICK_INPUT_MOUSE_BYTES / 4];
	byte keys[TICK_INPUT_KEY_COUNT];
	boolean port_reload[MAXIMUM_GAMEPADS];
	byte port_accept[MAXIMUM_GAMEPADS];
	boolean port_action_only[MAXIMUM_GAMEPADS];
	real port_primary_trigger[MAXIMUM_GAMEPADS];
};

/* the copy, aligned as the input layer's structs are */
static struct tick_input_snapshot tick_input_snapshot;
/* whether the tick side reads the copy: set by a capture, until its release */
static boolean tick_input_captured = FALSE;

void tick_input_capture(
	void)
{
	short index;
	struct mouse_state const *mouse;

	if (!TICK_OWNERS_FEATURE(TICK_FEATURE_INPUT))
		return;

	csmemset(&tick_input_snapshot, 0, sizeof(tick_input_snapshot));
	for (index = 0; index < MAXIMUM_GAMEPADS; index++)
	{
		struct gamepad_state const *gamepad = input_get_gamepad_state(index);

		tick_input_snapshot.has_gamepad[index] = input_has_gamepad(index);
		if (gamepad)
		{
			tick_input_snapshot.has_gamepad_state[index] = TRUE;
			tick_input_snapshot.gamepads[index] = *gamepad;
		}
		tick_input_snapshot.port_reload[index] = input_abstraction_port_reload(index);
		tick_input_snapshot.port_accept[index] = input_abstraction_port_accept(index);
		tick_input_snapshot.port_action_only[index] = input_abstraction_port_action_only(index);
		tick_input_snapshot.port_primary_trigger[index] = input_abstraction_port_primary_trigger(index);
	}
	for (index = 0; index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; index++)
	{
		struct game_input_state *input_state = input_abstraction_get_input_state(index);

		if (input_state)
		{
			tick_input_snapshot.has_input_state[index] = TRUE;
			csmemcpy(tick_input_snapshot.input_states[index], input_state, TICK_INPUT_STATE_BYTES);
		}
	}
	mouse = input_get_mouse_state();
	if (mouse)
	{
		tick_input_snapshot.has_mouse = TRUE;
		csmemcpy(tick_input_snapshot.mouse, mouse, TICK_INPUT_MOUSE_BYTES);
	}
	for (index = 0; index < TICK_INPUT_KEY_COUNT; index++)
	{
		/* (NUMBER_OF_KEYS is no key: the codes past it are the four that stand for a pair of keys, and the input layer halts on it) */
		if (index != NUMBER_OF_KEYS)
			tick_input_snapshot.keys[index] = (byte)input_key_is_down(index);
	}
	tick_input_captured = TRUE;

	return;
}

void tick_input_release(
	void)
{
	tick_input_captured = FALSE;

	return;
}

static boolean tick_input_reads_copy(
	void)
{
	return tick_input_captured && TICK_OWNERS_FEATURE(TICK_FEATURE_INPUT);
}

boolean tick_input_has_gamepad(
	short gamepad_index)
{
	if (tick_input_reads_copy())
		return gamepad_index >= 0 && gamepad_index < MAXIMUM_GAMEPADS && tick_input_snapshot.has_gamepad[gamepad_index];

	return input_has_gamepad(gamepad_index);
}

const struct gamepad_state *tick_input_get_gamepad_state(
	short gamepad_index)
{
	if (tick_input_reads_copy())
	{
		return gamepad_index >= 0 && gamepad_index < MAXIMUM_GAMEPADS && tick_input_snapshot.has_gamepad_state[gamepad_index] ?
			&tick_input_snapshot.gamepads[gamepad_index] : NULL;
	}

	return input_get_gamepad_state(gamepad_index);
}

struct game_input_state *tick_input_abstraction_get_input_state(
	short local_player_index)
{
	if (tick_input_reads_copy())
	{
		return local_player_index >= 0 && local_player_index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS &&
			tick_input_snapshot.has_input_state[local_player_index] ?
			(struct game_input_state *)tick_input_snapshot.input_states[local_player_index] : NULL;
	}

	return input_abstraction_get_input_state(local_player_index);
}

const struct mouse_state *tick_input_get_mouse_state(
	void)
{
	if (tick_input_reads_copy())
		return tick_input_snapshot.has_mouse ? (struct mouse_state const *)tick_input_snapshot.mouse : NULL;

	return input_get_mouse_state();
}

boolean tick_input_key_is_down(
	short key_code)
{
	if (tick_input_reads_copy() && key_code >= 0 && key_code < TICK_INPUT_KEY_COUNT && key_code != NUMBER_OF_KEYS)
		return tick_input_snapshot.keys[key_code];

	return input_key_is_down(key_code);
}

boolean tick_input_abstraction_port_reload(
	short controller_index)
{
	if (tick_input_reads_copy())
		return controller_index >= 0 && controller_index < MAXIMUM_GAMEPADS && tick_input_snapshot.port_reload[controller_index];

	return input_abstraction_port_reload(controller_index);
}

byte tick_input_abstraction_port_accept(
	short controller_index)
{
	if (tick_input_reads_copy())
		return controller_index >= 0 && controller_index < MAXIMUM_GAMEPADS ? tick_input_snapshot.port_accept[controller_index] : 0;

	return input_abstraction_port_accept(controller_index);
}

boolean tick_input_abstraction_port_action_only(
	short controller_index)
{
	if (tick_input_reads_copy())
		return controller_index >= 0 && controller_index < MAXIMUM_GAMEPADS && tick_input_snapshot.port_action_only[controller_index];

	return input_abstraction_port_action_only(controller_index);
}

real tick_input_abstraction_port_primary_trigger(
	short controller_index)
{
	if (tick_input_reads_copy())
		return controller_index >= 0 && controller_index < MAXIMUM_GAMEPADS ? tick_input_snapshot.port_primary_trigger[controller_index] : 0.f;

	return input_abstraction_port_primary_trigger(controller_index);
}

#else /* HALO_WIIU */

typedef int tick_input_not_for_this_platform;

#endif
