/*
WIIU_AUTO_CONTINUE.C

See wiiu_auto_continue.h.
*/

#include "wiiu_auto_continue.h"

void wiiu_auto_continue_state_init(struct wiiu_auto_continue_state *state, int enabled, int crash_note)
{
	state->menu_polls = 0;
	state->button_seen = 0;
	state->crash_note = crash_note != 0;
	state->enabled = enabled != 0;
	state->finished = 0;
	state->reason = WIIU_AUTO_CONTINUE_NO_REASON;
	if (!state->enabled)
	{
		state->finished = 1;
		state->reason = WIIU_AUTO_CONTINUE_OFF;
	}
	else if (state->crash_note)
	{
		state->finished = 1;
		state->reason = WIIU_AUTO_CONTINUE_CRASH_NOTE;
	}
}

int wiiu_auto_continue_step(struct wiiu_auto_continue_state *state, unsigned buttons, int main_menu)
{
	if (state->finished)
		return WIIU_AUTO_CONTINUE_WAIT;
	if (buttons)
		state->button_seen = 1;
	if (state->button_seen)
	{
		state->finished = 1;
		state->reason = WIIU_AUTO_CONTINUE_BUTTON;
		return WIIU_AUTO_CONTINUE_CANCELLED;
	}
	if (!main_menu)
		return WIIU_AUTO_CONTINUE_WAIT;
	if (++state->menu_polls < WIIU_AUTO_CONTINUE_SETTLE_POLLS)
		return WIIU_AUTO_CONTINUE_WAIT;
	state->finished = 1;
	state->reason = WIIU_AUTO_CONTINUE_DONE;
	return WIIU_AUTO_CONTINUE_GO;
}

char const *wiiu_auto_continue_reason_text(int reason)
{
	switch (reason)
	{
		case WIIU_AUTO_CONTINUE_OFF: return "game.wiiu_auto_continue is off";
		case WIIU_AUTO_CONTINUE_CRASH_NOTE: return "the last run left a crash note";
		case WIIU_AUTO_CONTINUE_BUTTON: return "a controller button was held or pressed";
		case WIIU_AUTO_CONTINUE_DONE: return "done";
		default: return "";
	}
}

#ifndef WIIU_AUTO_CONTINUE_TEST

#include "wiiu_shell.h"

int config_boolean(const char *name);
/* menu_functions.c: Campaign > Continue on player 1's last save, without the error sound when there is none; 1 if the game is going on with it, 0 if there is nothing to continue */
int port_auto_continue(void);

static struct wiiu_auto_continue_state auto_state;
static int auto_crash_note;
static int auto_started;
static volatile unsigned auto_buttons;

/* (the settings are read once the game is running: the first poll of a controller) */
static void auto_begin(void)
{
	if (auto_started)
		return;
	auto_started = 1;
	wiiu_auto_continue_state_init(&auto_state, config_boolean("game.wiiu_auto_continue"), auto_crash_note);
	if (auto_state.crash_note && auto_state.enabled)
		wiiu_log("Auto continue: not this run: %s", wiiu_auto_continue_reason_text(auto_state.reason));
	else if (auto_state.enabled)
		wiiu_log("Auto continue: on (game.wiiu_auto_continue): the last campaign save is continued once the main menu is up; hold B at the start to cancel");
}

void wiiu_auto_continue_start(int crash_note)
{
	auto_crash_note = crash_note != 0;
}

/* (the controllers are read on the game thread; a pad's buttons are noted for the first controller's next poll) */
void wiiu_auto_continue_pad(unsigned port, unsigned buttons)
{
	(void)port;
	auto_begin();
	if (!auto_state.finished && buttons)
		__atomic_fetch_or(&auto_buttons, buttons, __ATOMIC_RELAXED);
}

void wiiu_auto_continue_poll(int main_menu)
{
	unsigned buttons;

	auto_begin();
	if (auto_state.finished)
		return;
	buttons = __atomic_exchange_n(&auto_buttons, 0u, __ATOMIC_RELAXED);
	switch (wiiu_auto_continue_step(&auto_state, buttons, main_menu))
	{
		case WIIU_AUTO_CONTINUE_CANCELLED:
			wiiu_log("Auto continue: cancelled: %s", wiiu_auto_continue_reason_text(auto_state.reason));
			break;
		case WIIU_AUTO_CONTINUE_GO:
			if (port_auto_continue())
				wiiu_log("Auto continue: the main menu is up: going on with the last campaign save (Campaign > Continue)");
			else
				wiiu_log("Auto continue: nothing done: there is no valid campaign save to continue");
			break;
		default:
			break;
	}
}

#endif
