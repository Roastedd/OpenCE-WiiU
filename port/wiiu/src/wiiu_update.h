/*
WIIU_UPDATE.H

The Wii U's self-updater as the rest of the port sees it (wiiu_update.c): the shell starts it before the game and stops it as the
app closes, the renderer draws its panel, and the controllers answer it. Only integers and strings cross here: the controllers'
unit is built with the Xbox's ABI.
*/

#ifndef HALO_WIIU_UPDATE_H
#define HALO_WIIU_UPDATE_H

#ifdef __cplusplus
extern "C" {
#endif

/* At the start (the shell, after it has read the last run's crash note and close trail, before the game): an update or a trial
left half done is finished or undone, and the worker that looks for a new build started. crash_note: the last run left a crash
note. Returns 1 if the files on the card are now another build's, which is being started in place of this one: the game must
not start. */
int wiiu_update_startup(int crash_note);

/* As the app closes, before its sockets are closed: the worker is asked to stop and waited for, up to wait_ms; 1 if it has
stopped (or never ran). */
int wiiu_update_stop(unsigned wait_ms);

/* The renderer's, each frame: 0 with no panel to draw; 1 with one, its lines (at most 10, '\n' between them, 62 characters each)
copied into text when *generation is not the panel's (which it is set to). */
int wiiu_update_panel(char *text, unsigned capacity, unsigned *generation);

/* The controllers', for each pad as it is read (buttons are wiiu_input.h's): 1 if the panel has the buttons, which the game must
then not see (they are kept from it until they are let go after the panel closes). */
int wiiu_update_panel_input(unsigned port, unsigned buttons);

/* The game thread's, as the controllers are read: whether the main menu (main_menu) or any menu (menus) has the screen. */
void wiiu_update_menus(int main_menu, int menus);

#ifdef __cplusplus
}
#endif

#endif
