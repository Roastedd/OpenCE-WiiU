/*
WIIU_AUTO_CONTINUE.H

game.wiiu_auto_continue (config.toml, off by default): once the main menu has the screen at the start of a run, the game goes on with the last
campaign save as if Campaign > Continue had been chosen (menu_functions.c's campaign_continue). It does nothing, and says why in the log, when:
a controller button was held or pressed before it was due (holding B as the game starts cancels it, and so does a press while the menu comes up),
the last run left a crash note, or there is no save to continue. It happens at most once a run.

The decision is plain C (wiiu_auto_continue_step), tested on a desktop (tools/test_wiiu_auto_continue.py); wiiu_auto_continue.c is the console's half.
*/
#ifndef WIIU_AUTO_CONTINUE_H
#define WIIU_AUTO_CONTINUE_H

#ifdef __cplusplus
extern "C" {
#endif

/* the polls of the first controller (about 60 a second) the main menu must have had the screen for before the game goes on: the menu is drawn by then */
#define WIIU_AUTO_CONTINUE_SETTLE_POLLS 90u

enum wiiu_auto_continue_action
{
	WIIU_AUTO_CONTINUE_WAIT,       /* nothing yet */
	WIIU_AUTO_CONTINUE_GO,         /* go on with the save now */
	WIIU_AUTO_CONTINUE_CANCELLED   /* decided not to, for good (the reason is in the state) */
};

enum wiiu_auto_continue_reason
{
	WIIU_AUTO_CONTINUE_NO_REASON,
	WIIU_AUTO_CONTINUE_OFF,         /* game.wiiu_auto_continue is false */
	WIIU_AUTO_CONTINUE_CRASH_NOTE,  /* the last run left a crash note */
	WIIU_AUTO_CONTINUE_BUTTON,      /* a controller button was held or pressed */
	WIIU_AUTO_CONTINUE_DONE         /* it has gone on (or tried to) once */
};

struct wiiu_auto_continue_state
{
	unsigned menu_polls;     /* polls of the first controller with the main menu up */
	int button_seen;         /* a button on any controller, at any poll so far */
	int crash_note;
	int enabled;
	int finished;
	int reason;
};

void wiiu_auto_continue_state_init(struct wiiu_auto_continue_state *state, int enabled, int crash_note);
/* one poll of the first controller: buttons is all the buttons held on any controller at this poll (0 for none), main_menu whether the main menu has the screen */
int wiiu_auto_continue_step(struct wiiu_auto_continue_state *state, unsigned buttons, int main_menu);
char const *wiiu_auto_continue_reason_text(int reason);

#ifndef WIIU_AUTO_CONTINUE_TEST
/* The shell's, at the start, once the last run's crash note has been read */
void wiiu_auto_continue_start(int crash_note);
/* The controller layer's, for each controller as it is read (before the updater's panel takes its buttons) and for the first, with whether the main menu has the screen */
void wiiu_auto_continue_pad(unsigned port, unsigned buttons);
void wiiu_auto_continue_poll(int main_menu);
#endif

#ifdef __cplusplus
}
#endif

#endif
