/*
ASYNC_SAVE.H

The background thread a checkpoint's write to the card runs on (source/saved games/game_state_xbox.c has the policy: what is copied, when the
write is waited for). A checkpoint wrote the whole game state (20 MiB) to z:\savegame.bin on the game thread, and on the console that took 1.3 to 1.6 s
with the game standing still; the copy is taken on the game thread (a few frames' worth) and the write is given to this thread. For the Wii U only
(HALO_WIIU, the digest harness's builds too).

Switches (halo/<name> on the console, HALO_<NAME> in the environment of the host runs; absent or -1 is the default):
  async_save.txt         1 or absent: on; 0: the checkpoint is written on the game thread, as it always was
  async_save_verify.txt  1: each checkpoint is checked: the copy against the game state it was taken from, the file against the copy once written, and
                         the copy's CRC is logged (the host tests compare the two paths with it); 0 or absent: not
  async_save_delta.txt   1: a checkpoint writes only the 4 KB blocks of the game state that differ from what the last checkpoint wrote (the file already holds the rest);
                         0 or absent: the whole 20 MiB, as it was
  async_save_piece_kb.txt  the size of the pieces the write is made in (128 by default): the card is not held by one 20 MiB request
  async_save_revert_image.txt  0: a revert reads the 20 MiB back from the card, as it always did; 1 or absent: it takes the game state from the copy of the last checkpoint
                         that is still in memory (the image), which holds the file's bytes (game_state_xbox.c), and does not read the card
*/

#ifndef __ASYNC_SAVE_H
#define __ASYNC_SAVE_H
#include <stddef.h>

#ifdef HALO_WIIU

/* the switches, read once */
int async_save_enabled(void);
int async_save_verify(void);
int async_save_revert_image(void);
int async_save_delta(void);
unsigned long async_save_piece_bytes(void);

/* memcpy of the checkpoint's image on three threads (the game thread waits for all of them) */
void async_save_copy(void *to, const void *from, size_t bytes);

/* makes the thread now (while the heap is whole), so that the first checkpoint does not have to; 0 if it could not be made, and the switch is then off */
int async_save_prepare(void);

/* run(argument) on the thread; 0 if it cannot be (no thread, or one job is already running: the caller waits first and writes itself if it cannot) */
int async_save_start(void (*run)(void *), void *argument);

/* waits for the job running to end: 1 if none is running now, 0 if one still is after timeout_ms (0: as long as it takes) */
int async_save_wait(unsigned long timeout_ms);
int async_save_running(void);

#endif

#endif
