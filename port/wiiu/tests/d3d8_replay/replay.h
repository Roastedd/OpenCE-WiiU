/* The Direct3D layer's replay on Linux (replay_main.c): what its three parts share - the made-up world (replay_world.c), the GX2 thread's reading of
the streams (replay_gx2.c) and the made-up game (replay_game.c). */
#ifndef D3D8_REPLAY_H
#define D3D8_REPLAY_H

#include "xgpu.h"
#include "sdl_platform.h"
#include "touch_menu.h"
#include "port_config.h"
#include "posix.h"
#include "hud_hires.h"
#include "menu_files.h"
#include "text_hires.h"
#include "dk_commands.h"
#include "dk_derive.h"
#include "dk_textures.h"
#include "wiiu_indices.h"
#include "wiiu_stream_queue.h"

#include <coreinit/time.h>
#include <stdint.h>
#include <stdio.h>

/* the tag cache's place in the window (physical_memory_get_tag_cache_base_address), and the text atlas's size */
#define REPLAY_TAG_CACHE_OFFSET 0x06000000UL
#define REPLAY_ATLAS_SIZE 128

uint64_t replay_hash(uint64_t hash, const void *data, unsigned long size);
#define REPLAY_HASH_INIT 14695981039346656037ULL
uint32_t replay_random(uint32_t *state);

/* replay_world.c */
int replay_window_make(void);
extern unsigned long replay_log_lines, replay_states_missed_lines, replay_targets_missed_lines;
extern int replay_verbose_log;
extern unsigned long replay_hud_placeholder_data, replay_hud_override_address;
void replay_hud_placeholder_move(unsigned long data);
/* (hud_hires.h has it from the change that made the texture cache remember a draw's textures; the replay of the layer before it has it from here) */
unsigned long hud_hires_placeholder_serial(void);
extern unsigned long replay_menu_data, replay_menu_serial;
extern unsigned long replay_atlas_data;
extern long replay_screen_height;
extern uint64_t replay_locks;
void replay_atlas_draw(uint32_t *seed);

/* replay_gx2.c: how the GX2 thread runs (gx2_thread 0: each stream as it is handed over; else posted to the queue and run when the game thread waits for one,
or also at moments of the schedule's own) and what it saw */
void replay_gx2_start(int async, int eager, uint32_t seed, FILE *trace);
/* a moment the GX2 thread may have caught up at (the eager schedule runs some of what is posted) */
void replay_gx2_moment(void);
void replay_gx2_finish(void);
uint64_t replay_gx2_digest(void);
unsigned long replay_gx2_lines(void);
unsigned long replay_gx2_draws(void);
/* what the streams held so far: draws, their indices (or vertices), indexed draws, decal draws, state writes (index, value pairs), draw facts,
constant rows, vertex formats set; none of it is in a line or a digest */
enum { REPLAY_TOTAL_DRAWS, REPLAY_TOTAL_INDICES, REPLAY_TOTAL_INDEXED, REPLAY_TOTAL_DECAL_DRAWS, REPLAY_TOTAL_STATE_WRITES, REPLAY_TOTAL_FACTS,
	REPLAY_TOTAL_CONSTANT_ROWS, REPLAY_TOTAL_FORMATS, REPLAY_TOTAL_COUNT };
void replay_gx2_totals(unsigned long out[REPLAY_TOTAL_COUNT]);
unsigned long replay_gx2_most_queued(void);

/* replay_game.c */
void replay_game_run(unsigned long frames);

/* d3d8_gx2.c's: the queue and its buffers, and what the game calls besides Direct3D (the game's units have these from halo_linux_source_fixups.h, or name
them where they call them) */
extern struct stream_queue dk_stream_queue;
uint32_t *dk_stream_buffer(unsigned slot);
long halo_screen_width(void);
long halo_screen_commit(void);
void halo_screen_ui_offset(unsigned char centered);
void halo_texture_clip(unsigned char clip, float low, float high);
void halo_screen_warp_blend(unsigned char on);
void halo_render_marker(char const *name);
void halo_stream_endian_forget(void);
void halo_switch_renderer_idle(void);
void wiiu_index_buffer_renew(D3DIndexBuffer *buffer, unsigned long bytes);
void wiiu_decal_vertices(const unsigned char *color_bytes, const void *vertices, long count);
/* (the game's cache/physical_memory_map.c, and Custom Edition's tag cache: replay_world.c's here) */
void *physical_memory_get_tag_cache_base_address(void);
void *halo_custom_edition_tag_cache(void);

#endif
