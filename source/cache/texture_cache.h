/*
TEXTURE_CACHE.H

header included in hcex build.
*/

#ifndef __TEXTURE_CACHE_H
#define __TEXTURE_CACHE_H
#pragma once

/* ---------- constants */

/* ---------- macros */

/* ---------- structures */

struct bitmap_data;

/* ---------- prototypes/TEXTURE_CACHE.C */

void *_texture_cache_bitmap_get_hardware_format(
	struct bitmap_data *bitmap,
	boolean block,
	boolean load);
void *texture_cache_bitmap_load(
	struct bitmap_data *bitmap);
void texture_cache_bitmap_delete(
	struct bitmap_data *bitmap);
void texture_cache_new(
	void);
void texture_cache_close(
	void);
void texture_cache_delete(
	void);
void texture_cache_open(
	void);
void texture_cache_idle(
	void);
/* port: the pool for the performance overlay (port/wiiu/src/wiiu_perf.h): the KiB the cache holds, of them the KiB used in the last two frames, and its size */
boolean texture_cache_usage(
	unsigned long *resident_kib,
	unsigned long *hot_kib,
	unsigned long *capacity_kib);
void texture_cache_return_memory(
	void);
void texture_cache_flush(
	void);
void *texture_cache_steal_memory(
	long size);
void texture_cache_debug_render(
	void);

/* ---------- globals */

/* ---------- public code */

#endif // __TEXTURE_CACHE_H
