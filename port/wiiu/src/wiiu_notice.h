/*
WIIU_NOTICE.H

Three small messages to the player, drawn by the renderer's panel (wiiu_gx2.c) over the game's picture, none of which needs the game's own
screens or toasts (screens.enable) or any of the menus' widgets:

- the slow SD card message (halo/card_check.txt, default 1): a short write and read on the card once the main menu is up, off the game thread; a card
  under 4 MB/s write or 8 MB/s read gets a plain-words panel in the main menu, once a run. The speeds are kept for the detailed overlay;
- the saving mark (halo/saving_mark.txt, default 1): "Saving..." in a corner while a checkpoint or Save and Quit write runs and a second after;
- the loading bar (halo/loading_bar.txt, default 1): "Loading..." and a bar on the black frame of a campaign level load, from the stage reached.

The numbers are single words read and written by different threads; nothing here is a lock the renderer waits for.
*/

#ifndef WIIU_NOTICE_H
#define WIIU_NOTICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- the card */

/* The slow-card limits, MB/s (1 MB = 1048576 bytes) */
#define NOTICE_SLOW_WRITE_MBS 4.0f
#define NOTICE_SLOW_READ_MBS 8.0f

/* whether a card of these speeds is slow (either speed 0 or less: not measured, not slow) */
int wiiu_notice_card_is_slow(float write_mbs, float read_mbs);
/* the message for a slow card, lines at most 62 characters with '\n' between them */
void wiiu_notice_card_text(char *out, unsigned capacity, float write_mbs, float read_mbs);
/* the benchmark itself on a folder: a 2 MB file in 128 KB pieces is written, read back and removed. 1 if it ran (the speeds set), 0 if the file could not be made */
int wiiu_notice_card_bench(const char *folder, float *write_mbs, float *read_mbs);
/* the measured speeds if the benchmark has run (1), else 0 */
int wiiu_notice_card_speeds(float *write_mbs, float *read_mbs);

/* The game thread's, as the controllers are read: whether the main menu is up. The first time it is, the benchmark is started on a thread of its own. */
void wiiu_notice_menus(int main_menu);

/* ---------- what the renderer draws */

/* Each present: 0 with no panel to show; 1 with one, its lines copied into text and *bar set to the bar's percent (-1: no bar) when *generation is not the panel's
(which it is set to; generations of this module have the top bit set, the updater's do not). */
int wiiu_notice_panel(char *text, unsigned capacity, unsigned *generation, int *bar);
/* Each present: whether "Saving..." is shown (a checkpoint's or Save and Quit's write is running, or ended less than a second ago) */
int wiiu_notice_saving(void);

/* ---------- the loading bar (the game thread's) */

/* A campaign level load begins (the black frame): the bar starts at 0 */
void wiiu_loading_begin(void);
/* the stage reached, percent 0 to 100 (it does not go back); the bar is presented at once if the last frame was shown a quarter of a second ago or more */
void wiiu_loading_stage(int percent);
/* the cache file's copy (game_precache_new_map's loop, which presents its own frames): fraction 0 to 1 of the first part of the bar */
void wiiu_loading_precache(float fraction);
/* the load is done */
void wiiu_loading_end(void);

#ifdef __cplusplus
}
#endif

#endif
