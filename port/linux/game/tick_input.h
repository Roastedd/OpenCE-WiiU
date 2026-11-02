/*
TICK_INPUT.H

The input the game's tick side reads, handed over as a snapshot (the tools repository's
reports/research/2026-11-01/ticks-off-thread.md section 4.4: "the main thread passes raw input only").

Once a frame, before the tick side of the frame runs, the game thread copies what the input layer holds (every gamepad's buttons
and sticks, the abstraction's input state for each local player, the keyboard's controls, the mouse, the keys) with
tick_input_capture. The tick side (player_control.c, director.c, cheats.c, debug_keys.c, game_engine.c, coop_spectate.c) reads the
copy through the tick_input_* functions, which are the input layer's own calls under other names; the game thread's own reads (the
menus, the console) are the input layer's. The frame's tick side then sees what it saw, and a tick thread could run
it without touching the input layer.

With the tick owners' queues off, or outside a capture (tick_input_release), the tick_input_* functions are the input layer's
calls themselves.

Compiled for the Wii U only (HALO_WIIU).
*/

#ifndef __TICK_INPUT_H
#define __TICK_INPUT_H

#ifdef HALO_WIIU

struct gamepad_state;
struct mouse_state;
struct game_input_state;

/* the game thread's: the input of this frame copied (called once, before the tick side runs), and let go of after it */
void tick_input_capture(void);
void tick_input_release(void);

/* the tick side's reads: the input layer's calls of the same names, from the copy */
boolean tick_input_has_gamepad(short gamepad_index);
const struct gamepad_state *tick_input_get_gamepad_state(short gamepad_index);
struct game_input_state *tick_input_abstraction_get_input_state(short local_player_index);
const struct mouse_state *tick_input_get_mouse_state(void);
boolean tick_input_key_is_down(short key_code);
boolean tick_input_abstraction_port_reload(short controller_index);
byte tick_input_abstraction_port_accept(short controller_index);
boolean tick_input_abstraction_port_action_only(short controller_index);
real tick_input_abstraction_port_primary_trigger(short controller_index);

/* what a file of the tick side reads as the input layer's, from the copy (after its includes) */
#define input_has_gamepad tick_input_has_gamepad
#define input_get_gamepad_state tick_input_get_gamepad_state
#define input_abstraction_get_input_state tick_input_abstraction_get_input_state
#define input_get_mouse_state tick_input_get_mouse_state
#define input_key_is_down tick_input_key_is_down
#define input_abstraction_port_reload tick_input_abstraction_port_reload
#define input_abstraction_port_accept tick_input_abstraction_port_accept
#define input_abstraction_port_action_only tick_input_abstraction_port_action_only
#define input_abstraction_port_primary_trigger tick_input_abstraction_port_primary_trigger

#endif /* HALO_WIIU */

#endif /* __TICK_INPUT_H */
