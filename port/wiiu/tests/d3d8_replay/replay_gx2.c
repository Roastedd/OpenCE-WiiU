/*
REPLAY_GX2.C

The GX2 thread of the Direct3D layer's replay: the command streams read as wiiu_gx2.c's run_stream and draw read them, as far as what the GPU is given
goes, and each draw, clear, present and visibility test written as a line of it. A draw's line has its targets, the state, shader keys, textures and
uniforms dk_derive.c derives (the GX2 thread's own derivation, linked as it is), the constants and the vertex format the commands set, and the bytes of
the indices and of every vertex it reads, hashed at the moment this thread reads them, as the GPU's copies are made then; a texture is its image's
contents as the commands made them by then. The lines' digest is the run's picture.

What this does not have of wiiu_gx2.c: the GX2 calls, the shader pack, the ring and the static data cache (which copy the same bytes), the GPU test.
The decals' expansion is wiiu_gx2.c's own (decals_expand, taken from the source by tools/test_wiiu_d3d8_replay.py into replay_decals.inc).
*/

#include "replay.h"
#include "dk_shaders.h"
#include "shader_key_wire.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "replay_decals.inc"

#define COMPOSITE_LIMIT 32

static struct
{
	int async, eager;
	uint32_t seed;
	FILE *trace;
	uint32_t ran; /* streams run as they were handed over (gx2_thread 0) */
	struct dk_surface color, depth;
	struct dk_vertex_format format;
	int format_set;
	float constants[192][4];
	uint64_t images[DK_TEXTURE_LIMIT];
	struct dk_command_composite composites[COMPOSITE_LIMIT];
	unsigned composite_count;
	unsigned long frame, draw, lines, draws;
	uint64_t digest;
	unsigned idle_pauses;
	uint32_t most_queued; /* the most streams posted and not yet run at once */
} gx;

/* what the streams hold, counted as they are read (replay_gx2_totals: the game digest's per-pass counts); nothing in a line or a digest */
static unsigned long totals[REPLAY_TOTAL_COUNT];

void replay_gx2_totals(unsigned long out[REPLAY_TOTAL_COUNT])
{
	memcpy(out, totals, sizeof(totals));
}

static void line(const char *format, ...) __attribute__((format(printf, 1, 2)));

static void line(const char *format, ...)
{
	char text[2048];
	va_list arguments;
	int length;

	va_start(arguments, format);
	length = vsnprintf(text, sizeof(text), format, arguments);
	va_end(arguments);
	if (length < 0)
		return;
	if ((unsigned)length >= sizeof(text))
		length = sizeof(text) - 1;
	gx.digest = replay_hash(gx.digest, text, (unsigned long)length);
	gx.digest = replay_hash(gx.digest, "\n", 1);
	gx.lines++;
	if (gx.trace)
		fprintf(gx.trace, "%s\n", text);
}

/* a line for the trace only, not the digest (REPLAY_VIEWS: where each draw and clear lands in its targets, which the test of a lowered step of dynamic
resolution reads; the digest of a run is the same with or without it) */
static void view_line(const char *format, ...) __attribute__((format(printf, 1, 2)));

static void view_line(const char *format, ...)
{
	va_list arguments;

	if (!gx.trace || !getenv("REPLAY_VIEWS"))
		return;
	va_start(arguments, format);
	vfprintf(gx.trace, format, arguments);
	va_end(arguments);
	fputc('\n', gx.trace);
}

static void view_targets(const char *what, const int32_t rectangle[4])
{
	const struct dk_surface *surfaces[2] = { &gx.color, &gx.depth };
	unsigned i;

	for (i = 0; i < 2; i++)
		if (surfaces[i]->kind != DK_SURFACE_NONE)
			view_line("view %s frame %lu kind %u pixels %ux%u active %ux%u rectangle %d %d %d %d", what, gx.frame, (unsigned)surfaces[i]->kind, (unsigned)surfaces[i]->pixel_width,
				(unsigned)surfaces[i]->pixel_height, (unsigned)surfaces[i]->active_width, (unsigned)surfaces[i]->active_height, (int)rectangle[0],
				(int)rectangle[1], (int)rectangle[2], (int)rectangle[3]);
}

static uint64_t hash_of(const void *data, unsigned long size)
{
	return replay_hash(REPLAY_HASH_INIT, data, size);
}

/* a guest address as the trace has it: its offset in the window, or the stream's (inline data) */
static unsigned long address_name(const void *address)
{
	return platform_is_contiguous(address) ? (unsigned long)address - PLATFORM_CONTIGUOUS_BASE : 0xffffffffUL;
}

static void surface_text(char *out, size_t size, const struct dk_surface *surface)
{
	snprintf(out, size, "%lx/%u/%ux%u/%ux%u", (unsigned long)surface->data, (unsigned)surface->kind, (unsigned)surface->width, (unsigned)surface->height,
		(unsigned)surface->pixel_width, (unsigned)surface->pixel_height);
}

static const struct dk_command_composite *composite_find(uint32_t data)
{
	unsigned index;

	for (index = 0; index < gx.composite_count; index++)
		if (gx.composites[index].data == data)
			return &gx.composites[index];
	return NULL;
}

static void composite_receive(const struct dk_command_composite *command)
{
	unsigned index;

	for (index = 0; index < gx.composite_count && gx.composites[index].data != command->data; index++)
		;
	if (index == gx.composite_count)
	{
		if (gx.composite_count == COMPOSITE_LIMIT)
			index = 0;
		else
			gx.composite_count++;
	}
	gx.composites[index] = *command;
	line("composite %lx %ux%u levels %u: %016llx", (unsigned long)command->data, (unsigned)command->width, (unsigned)command->height, (unsigned)command->levels,
		(unsigned long long)hash_of(command->level_data, sizeof(command->level_data)));
}

/* a stage as the GPU samples it: an image's contents, a target's surface, or a composite's levels, and the sampler */
static void stage_text(char *out, size_t size, const struct dk_stage_texture *stage)
{
	const uint64_t sampler = hash_of(&stage->sampler, sizeof(stage->sampler));

	if (stage->id)
		snprintf(out, size, "i%u:%016llx:%016llx", (unsigned)stage->id, (unsigned long long)(stage->id < DK_TEXTURE_LIMIT ? gx.images[stage->id] : 0),
			(unsigned long long)sampler);
	else if (stage->target && stage->composite)
	{
		const struct dk_command_composite *composite = composite_find(stage->target);

		snprintf(out, size, "c%lx/%u:%016llx:%016llx", (unsigned long)stage->target, (unsigned)stage->composite,
			(unsigned long long)(composite ? hash_of(composite->level_data, sizeof(composite->level_data)) : 0), (unsigned long long)sampler);
	}
	else if (stage->target)
		snprintf(out, size, "t%lx:%016llx", (unsigned long)stage->target, (unsigned long long)sampler);
	else
		snprintf(out, size, "-");
}


/* ---------- the shader keys of the draws (GAME_DIGEST_KEYS=file)

Every draw the layer derives has the keys the console's GX2 thread looks up in shaders.pack (dk_shader_for_draw: the same dk_derive_*_key). With the
variable set, each key not yet written is appended to that file as the missing-shader capture writes it on the card (wiiu_missing_keys.c: "P" and the
256-byte pixel key as hex, "V", the program's hash, its packed attribute mask, its instruction count and the words), so that tools/wiiu/key_diff.py can
tell which keys a map's draws need that the pack lacks, before the map is played on a console. Nothing is written, and the lines of the digest do not
change, when the variable is not set. */

#define KEYS_SLOTS 65536u
#define KEYS_PROGRAM_LIMIT 2048u

static struct
{
	int tried;
	FILE *file;
	uint64_t *seen;
	unsigned long pixel_keys, vertex_keys, unknown_programs;
	struct
	{
		uint64_t hash;
		uint32_t *words;
		unsigned long count;
	} programs[KEYS_PROGRAM_LIMIT];
	unsigned program_count;
	struct nv2a_pixel_shader_key last_pixel;
	struct dk_vertex_key last_vertex;
	int last_valid;
} keys;

static int keys_enabled(void)
{
	if (!keys.tried)
	{
		/* (REPLAY_KEYS_FILE is the older name of the same file, from the split screen runs) */
		const char *path = getenv("GAME_DIGEST_KEYS") && *getenv("GAME_DIGEST_KEYS") ? getenv("GAME_DIGEST_KEYS") : getenv("REPLAY_KEYS_FILE");

		keys.tried = 1;
		if (path && *path)
		{
			keys.file = fopen(path, "w");
			keys.seen = calloc(KEYS_SLOTS, sizeof(uint64_t));
			if (!keys.file || !keys.seen)
			{
				if (keys.file)
					fclose(keys.file);
				keys.file = NULL;
			}
		}
	}
	return keys.file != NULL;
}

/* a program the game made (dk_shader_program_note's arguments): kept by hash so that a vertex key's line can have its words */
void replay_keys_program(uint64_t program_hash, const uint32_t *words, unsigned long count);

void replay_keys_program(uint64_t program_hash, const uint32_t *words, unsigned long count)
{
	unsigned index;

	if (!keys_enabled() || !words || !count || count > 136)
		return;
	for (index = 0; index < keys.program_count; index++)
		if (keys.programs[index].hash == program_hash)
			return;
	if (keys.program_count == KEYS_PROGRAM_LIMIT)
		return;
	keys.programs[keys.program_count].words = malloc(count * 4 * sizeof(uint32_t));
	if (!keys.programs[keys.program_count].words)
		return;
	memcpy(keys.programs[keys.program_count].words, words, count * 4 * sizeof(uint32_t));
	keys.programs[keys.program_count].hash = program_hash;
	keys.programs[keys.program_count].count = count;
	keys.program_count++;
}

/* whether this identity is new (and now remembered): an open-addressed set of 64-bit hashes, as the pack's own lookups tell keys apart */
static int keys_new(uint64_t identity)
{
	unsigned slot;

	if (!identity)
		identity = 1;
	for (slot = (unsigned)(identity % KEYS_SLOTS); keys.seen[slot]; slot = (slot + 1) % KEYS_SLOTS)
		if (keys.seen[slot] == identity)
			return 0;
	keys.seen[slot] = identity;
	return 1;
}

/* the frame the draws now belong to (the sweep, game_digest_sweep.c, logs its stops by it, so that a key is told by the stop that made it) */
unsigned long replay_keys_frame(void);

unsigned long replay_keys_frame(void)
{
	return gx.frame;
}

static unsigned long keys_frame_written = ~0ul;

/* the digest harness's (game_digest_sweep.c): the code that last set a pixel shader; the D3D8 replay has none */
extern unsigned long game_digest_last_pixel_site(void) __attribute__((weak));

static void keys_frame_comment(void)
{
	/* (before every new key: the frame, and the code that set the shader the key is of) */
	if (game_digest_last_pixel_site)
		fprintf(keys.file, "# frame %lu site 0x%lx\n", gx.frame, game_digest_last_pixel_site());
	else
		fprintf(keys.file, "# frame %lu\n", gx.frame);
	keys_frame_written = gx.frame;
}

static void keys_note(const struct dk_vertex_key *vertex, const struct nv2a_pixel_shader_key *pixel)
{
	if (!keys_enabled())
		return;
	if (!keys.last_valid || memcmp(pixel, &keys.last_pixel, sizeof(*pixel)))
	{
		unsigned char wire[SHADER_KEY_WIRE_BYTES];
		unsigned index;

		shader_key_write(pixel, wire);
		if (keys_new(shader_key_hash(SHADER_KEY_HASH_INIT, wire, sizeof(wire)) ^ 0x5049585Full))
		{
			keys_frame_comment();
			fputs("P ", keys.file);
			for (index = 0; index < sizeof(wire); index++)
				fprintf(keys.file, "%02x", wire[index]);
			fputc('\n', keys.file);
			keys.pixel_keys++;
		}
		keys.last_pixel = *pixel;
	}
	if (!keys.last_valid || memcmp(vertex, &keys.last_vertex, sizeof(*vertex)))
	{
		if (keys_new(shader_key_hash(SHADER_KEY_HASH_INIT, (const unsigned char *)&vertex->program_hash, sizeof(vertex->program_hash)) * 31u + vertex->packed_mask))
		{
			unsigned index, word;

			for (index = 0; index < keys.program_count && keys.programs[index].hash != vertex->program_hash; index++)
				;
			keys_frame_comment();
			if (index < keys.program_count)
			{
				fprintf(keys.file, "V %08lx%08lx %lu %lu", (unsigned long)(vertex->program_hash >> 32), (unsigned long)(vertex->program_hash & 0xffffffffu),
					(unsigned long)vertex->packed_mask, keys.programs[index].count);
				for (word = 0; word < keys.programs[index].count * 4; word++)
					fprintf(keys.file, " %08lx", (unsigned long)keys.programs[index].words[word]);
				fputc('\n', keys.file);
				keys.vertex_keys++;
			}
			else
			{
				fprintf(keys.file, "# a vertex key whose program was never noted: %016llx mask %lu\n", (unsigned long long)vertex->program_hash,
					(unsigned long)vertex->packed_mask);
				keys.unknown_programs++;
			}
		}
		keys.last_vertex = *vertex;
	}
	keys.last_valid = 1;
	fflush(keys.file);
}

/* Every vertex key the game can ask for, at the end of a run (GAME_DIGEST_KEYS): the game makes all of its vertex programs when it starts (the 67 of
rasterizer_xbox_vertex_shaders.c, and the loading screen's), each with the attributes its declaration packs, and a draw from immediate-mode vertices
asks for a program with none packed. So the keys the console can meet are the programs' (hash, packed mask) and (hash, 0), whether or not a run drew
with them. Written with "#" comments saying which: key_diff.py reads them like the others. */
unsigned long d3d8_dk_vertex_shader_count(void);
int d3d8_dk_vertex_program_by_id(unsigned long id, uint64_t *program_hash, const uint32_t **instructions, unsigned long *instruction_count);
unsigned long d3d8_dk_vertex_program_packed_mask_by_id(unsigned long id);

void replay_keys_finish(void)
{
	unsigned long id, count = d3d8_dk_vertex_shader_count(), written = 0;
	unsigned variant;

	if (!keys_enabled() || !getenv("GAME_DIGEST_KEYS_ALL_VERTEX"))
		return;
	for (id = 1; id < count; id++)
	{
		uint64_t program_hash;
		const uint32_t *words;
		unsigned long instruction_count, word, mask = d3d8_dk_vertex_program_packed_mask_by_id(id);

		if (!d3d8_dk_vertex_program_by_id(id, &program_hash, &words, &instruction_count) || !instruction_count || instruction_count > 136)
			continue;
		for (variant = 0; variant < 2; variant++)
		{
			unsigned long use = variant ? 0 : mask;

			if (variant && !mask)
				continue;
			if (!keys_new(shader_key_hash(SHADER_KEY_HASH_INIT, (const unsigned char *)&program_hash, sizeof(program_hash)) * 31u + use))
				continue;
			fprintf(keys.file, "V %08lx%08lx %lu %lu", (unsigned long)(program_hash >> 32), (unsigned long)(program_hash & 0xffffffffu), use, instruction_count);
			for (word = 0; word < instruction_count * 4; word++)
				fprintf(keys.file, " %08lx", (unsigned long)words[word]);
			fputc('\n', keys.file);
			written++;
		}
	}
	fprintf(keys.file, "# vertex programs listed at the end: %lu of %lu ids, %lu keys\n", count ? count - 1 : 0, count, written);
	fflush(keys.file);
}

/* wiiu_gx2.c's draw, as far as what the GPU reads goes */
static void draw(const struct dk_command_draw *command)
{
	struct dk_derive *derive = dk_derive_get();
	const unsigned char *inline_data = (const unsigned char *)&command->streams[command->stream_count];
	const uint32_t primitive = command->primitive & ~DK_DRAW_RANGE_LATER;
	const int range_later = (command->primitive & DK_DRAW_RANGE_LATER) != 0;
	int32_t vertex_offset = command->vertex_offset;
	uint32_t lowest = 0, highest = 0;
	char streams[512], color[96], depth[96], stages[4][96];
	size_t used = 0;
	unsigned stream, slot = 0, stage;
	uint64_t indices = 0;

	gx.draw++;
	gx.draws++;
	totals[REPLAY_TOTAL_DRAWS]++;
	totals[REPLAY_TOTAL_INDICES] += command->count;
	if (command->index_address)
		totals[REPLAY_TOTAL_INDEXED]++;
	if (command->primitive & DK_DRAW_DECALS)
		totals[REPLAY_TOTAL_DECAL_DRAWS]++;
	if (gx.color.kind == DK_SURFACE_NONE && gx.depth.kind == DK_SURFACE_NONE)
	{
		line("draw %lu.%lu: no target", gx.frame, gx.draw);
		return;
	}
	if (range_later)
	{
		const uint16_t *list = command->index_address == DK_INDEX_INLINE ? (const uint16_t *)(const void *)(inline_data + ((command->inline_bytes + 3u) & ~3u)) :
			(const uint16_t *)(uintptr_t)command->index_address;

		wiiu_index_range(list, command->count, &lowest, &highest);
		vertex_offset = -(int32_t)lowest;
	}
	if (!gx.format_set)
	{
		line("draw %lu.%lu: no vertex format", gx.frame, gx.draw);
		return;
	}
	streams[0] = 0;
	for (stream = 0; stream < DK_STREAM_COUNT; stream++)
	{
		const unsigned char *source;
		uint32_t size;

		if (!((gx.format.stream_mask >> stream) & 1))
			continue;
		if (slot >= command->stream_count)
			break;
		if (range_later)
		{
			const uint32_t stride = command->streams[slot][1];

			size = stride ? stride * (highest - lowest + 1) : 64u;
			source = (const unsigned char *)(uintptr_t)(command->streams[slot][0] + stride * lowest);
		}
		else
		{
			size = command->streams[slot][1];
			source = command->inline_bytes ? inline_data : (const unsigned char *)(uintptr_t)command->streams[slot][0];
		}
		used += (size_t)snprintf(streams + used, sizeof(streams) - used, " s%u@%lx+%lu:%016llx", stream, command->inline_bytes ? 0xffffffffUL : address_name(source),
			(unsigned long)size, (unsigned long long)hash_of(source, size));
		if (used >= sizeof(streams))
			used = sizeof(streams) - 1;
		slot++;
	}
	if (command->index_address)
	{
		const unsigned char *source = command->index_address == DK_INDEX_INLINE ? inline_data + ((command->inline_bytes + 3u) & ~3u) :
			(const unsigned char *)(uintptr_t)command->index_address;

		indices = hash_of(source, command->count * 2);
	}
	surface_text(color, sizeof(color), &gx.color);
	surface_text(depth, sizeof(depth), &gx.depth);
	view_targets("draw", dk_derive_state(derive)->viewport);
	for (stage = 0; stage < 4; stage++)
		stage_text(stages[stage], sizeof(stages[stage]), &dk_derive_textures(derive)->stages[stage]);
	keys_note((const struct dk_vertex_key *)dk_derive_vertex_key(derive), (const struct nv2a_pixel_shader_key *)dk_derive_pixel_key(derive));
	line("draw %lu.%lu: prim %u count %u offset %d indices %016llx%s; targets %s %s; state %016llx vertex key %016llx pixel key %016llx; textures %s %s %s %s; "
		"vertex parameters %016llx pixel parameters %016llx constants %016llx format %016llx",
		gx.frame, gx.draw, (unsigned)primitive, (unsigned)command->count, (int)vertex_offset, (unsigned long long)indices, streams, color, depth,
		(unsigned long long)hash_of(dk_derive_state(derive), sizeof(struct dk_draw_state)),
		(unsigned long long)hash_of(dk_derive_vertex_key(derive), sizeof(struct dk_vertex_key)),
		(unsigned long long)hash_of(dk_derive_pixel_key(derive), sizeof(struct nv2a_pixel_shader_key)),
		stages[0], stages[1], stages[2], stages[3],
		(unsigned long long)hash_of(dk_derive_vertex_parameters(derive), sizeof(struct dk_vertex_parameters)),
		(unsigned long long)hash_of(dk_derive_pixel_parameters(derive), sizeof(struct dk_pixel_parameters)),
		(unsigned long long)hash_of(gx.constants, sizeof(gx.constants)), (unsigned long long)hash_of(&gx.format, sizeof(gx.format)));
}


static void clear(const struct dk_command_clear *command)
{
	char color[96], depth[96];
	uint64_t rectangles = hash_of(command->rectangles, (unsigned long)command->rectangle_count * 4 * sizeof(uint32_t));

	surface_text(color, sizeof(color), &gx.color);
	surface_text(depth, sizeof(depth), &gx.depth);
	{
		unsigned rectangle;

		for (rectangle = 0; rectangle < command->rectangle_count; rectangle++)
			view_targets("clear", (const int32_t *)(const void *)command->rectangles[rectangle]);
	}
	line("clear %lu.%lu: flags %x color %g %g %g %g depth %g stencil %u, %u rectangles %016llx; targets %s %s", gx.frame, gx.draw, (unsigned)command->flags,
		(double)command->color[0], (double)command->color[1], (double)command->color[2], (double)command->color[3], (double)command->depth, (unsigned)command->stencil,
		(unsigned)command->rectangle_count, (unsigned long long)rectangles, color, depth);
}

/* an image's contents: its shape and its texels as the command reads them now (not where they are: a decoded texture's buffer is the heap's) */
static void texture_make(const struct dk_command_texture *command)
{
	const uint32_t shape[8] = { command->kind, command->format, command->width, command->height, command->depth, command->levels, command->face_bytes,
		command->source_bytes };
	uint64_t contents = hash_of(shape, sizeof(shape));

	if (command->id >= DK_TEXTURE_LIMIT)
		return;
	if (command->source_bytes)
		contents = replay_hash(contents, (const void *)(uintptr_t)command->source, command->source_bytes);
	gx.images[command->id] = contents;
}

static void texture_rows(const struct dk_command_texture_rows *command)
{
	if (command->id >= DK_TEXTURE_LIMIT)
		return;
	gx.images[command->id] = replay_hash(gx.images[command->id], &command->level, 3 * sizeof(uint32_t));
	gx.images[command->id] = replay_hash(gx.images[command->id], (const void *)(uintptr_t)command->source, command->source_bytes);
}

/* wiiu_gx2.c's run_stream */
static void run_stream(const unsigned char *at, const unsigned char *end)
{
	while (at + sizeof(struct dk_command_header) <= end)
	{
		const struct dk_command_header *header = (const struct dk_command_header *)at;

		if (header->size < sizeof(*header) || at + header->size > end)
		{
			line("a command of %u bytes runs past the stream's end", (unsigned)header->size);
			break;
		}
		switch (header->type)
		{
		case DK_COMMAND_TARGETS:
			gx.color = ((const struct dk_command_targets *)header)->color;
			gx.depth = ((const struct dk_command_targets *)header)->depth;
			break;
		case DK_COMMAND_CLEAR:
			clear((const struct dk_command_clear *)header);
			break;
		case DK_COMMAND_PRESENT:
		{
			char back[96];

			surface_text(back, sizeof(back), &((const struct dk_command_present *)header)->back_buffer);
			line("present %lu: %s", gx.frame, back);
			gx.frame++;
			gx.draw = 0;
			break;
		}
		case DK_COMMAND_CONSTANTS:
		{
			const struct dk_command_constants *constants = (const struct dk_command_constants *)header;

			totals[REPLAY_TOTAL_CONSTANT_ROWS] += constants->count;

			if (constants->first < 192 && constants->count && constants->first + constants->count <= 192)
				memcpy(gx.constants[constants->first], constants->data, constants->count * 4 * sizeof(float));
			break;
		}
		case DK_COMMAND_VERTEX_FORMAT:
			totals[REPLAY_TOTAL_FORMATS]++;
			gx.format = ((const struct dk_command_vertex_format *)header)->format;
			gx.format_set = 1;
			break;
		case DK_COMMAND_TEXTURE:
			texture_make((const struct dk_command_texture *)header);
			break;
		case DK_COMMAND_TEXTURE_ROWS:
			texture_rows((const struct dk_command_texture_rows *)header);
			break;
		case DK_COMMAND_TEXTURE_FREE:
			if (((const struct dk_command_texture_free *)header)->id < DK_TEXTURE_LIMIT)
				gx.images[((const struct dk_command_texture_free *)header)->id] = 0;
			break;
		case DK_COMMAND_COMPOSITE:
			composite_receive((const struct dk_command_composite *)header);
			break;
		case DK_COMMAND_STATE_WRITES:
			totals[REPLAY_TOTAL_STATE_WRITES] += ((const struct dk_command_state_writes *)header)->count;
			dk_derive_writes(dk_derive_get(), (const struct dk_command_state_writes *)header);
			break;
		case DK_COMMAND_DRAW_FACTS:
			totals[REPLAY_TOTAL_FACTS]++;
			if (header->size >= sizeof(struct dk_command_draw_facts))
				dk_derive_facts(dk_derive_get(), &((const struct dk_command_draw_facts *)header)->facts);
			break;
		case DK_COMMAND_DRAW:
			if (dk_derive_pending(dk_derive_get()))
				dk_derive_run(dk_derive_get());
			if (((const struct dk_command_draw *)header)->primitive & DK_DRAW_DECALS)
			{
				const struct dk_command_draw *expanded = decals_expand((const struct dk_command_draw *)header);

				if (expanded)
					draw(expanded);
				else
					line("draw %lu.%lu: decals not expanded", gx.frame, gx.draw);
			}
			else
				draw((const struct dk_command_draw *)header);
			break;
		case DK_COMMAND_VISIBILITY_BEGIN:
			line("visibility begin %lu.%lu", gx.frame, gx.draw);
			break;
		case DK_COMMAND_VISIBILITY_END:
			line("visibility end %lu.%lu: %u", gx.frame, gx.draw, (unsigned)((const struct dk_command_visibility_end *)header)->index);
			break;
		case DK_COMMAND_MARKER:
		{
			char name[DK_MARKER_NAME_SIZE + 1];

			memcpy(name, ((const struct dk_command_marker *)header)->name, DK_MARKER_NAME_SIZE);
			name[DK_MARKER_NAME_SIZE] = 0;
			line("marker %lu.%lu: %s", gx.frame, gx.draw, name);
			break;
		}
		default:
			/* (the commands the layer no longer writes: STATE, SHADERS, SHADER_KEYS, the parameters, TEXTURES) */
			line("command %u", (unsigned)header->type);
			break;
		}
		at += header->size;
	}
}

/* the oldest stream posted, run and retired: 0 if none was waiting */
static int run_one(void)
{
	unsigned slot;
	uint32_t used, number;

	if (dk_stream_queue.posted - dk_stream_queue.taken > gx.most_queued)
		gx.most_queued = dk_stream_queue.posted - dk_stream_queue.taken;
	if (!stream_queue_take(&dk_stream_queue, &slot, &used, &number))
		return 0;
	run_stream((const unsigned char *)dk_stream_buffer(slot), (const unsigned char *)dk_stream_buffer(slot) + used);
	stream_queue_retire(&dk_stream_queue);
	return 1;
}

void replay_gx2_start(int async, int eager, uint32_t seed, FILE *trace)
{
	memset(&gx, 0, sizeof(gx));
	gx.async = async;
	gx.eager = eager;
	gx.seed = seed ? seed : 1;
	gx.trace = trace;
	gx.digest = REPLAY_HASH_INIT;
}

void replay_gx2_moment(void)
{
	unsigned count;

	if (!gx.async || !gx.eager)
		return;
	/* (a moment in eight: up to three streams run) */
	if (replay_random(&gx.seed) % 8)
		return;
	for (count = replay_random(&gx.seed) % 4; count; count--)
		run_one();
}

void replay_gx2_finish(void)
{
	if (gx.async)
		while (run_one())
			;
}

uint64_t replay_gx2_digest(void)
{
	return gx.digest;
}

unsigned long replay_gx2_lines(void)
{
	return gx.lines;
}

unsigned long replay_gx2_draws(void)
{
	return gx.draws;
}

unsigned long replay_gx2_most_queued(void)
{
	return gx.most_queued;
}

/* ---------- what the layer asks of wiiu_gx2.c */

int host_gx2_stream_slots(void)
{
	return 6;
}

int host_gx2_async(void)
{
	return gx.async;
}

void host_gx2_submit(unsigned int commands, unsigned int size)
{
	const unsigned char *at = (const unsigned char *)(uintptr_t)commands;

	run_stream(at, at + size);
	gx.ran++;
}

/* (as wiiu_gx2.c: with the queue, the last stream run; without, everything handed over) */
unsigned int host_gx2_retired(void)
{
	return gx.async ? stream_queue_retired(&dk_stream_queue) : 0xffffffffu;
}

void host_gx2_idle(void)
{
	if (gx.async)
		while (run_one())
			;
}

/* the game thread waits for the GX2 thread: one stream is run. A wait with nothing posted would never end */
void host_gx2_pause(void)
{
	if (!gx.async)
		return;
	if (run_one())
	{
		gx.idle_pauses = 0;
		return;
	}
	if (++gx.idle_pauses > 1000000)
	{
		fprintf(stderr, "replay: the game thread waits for a stream that was never posted\n");
		exit(4);
	}
}

int host_gx2_visibility_enabled(void)
{
	return 0;
}

unsigned int host_gx2_visibility(unsigned int index)
{
	(void)index;
	return 0;
}

void host_gx2_window(uint32_t base, uint32_t size)
{
	(void)base;
	(void)size;
}
