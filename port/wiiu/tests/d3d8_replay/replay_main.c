/*
REPLAY_MAIN.C

The Wii U's Direct3D layer replayed on Linux. The layer (port/wiiu/src/d3d8_gx2.c, with its texture cache, xbox_textures_gx2.c, the memory watch and
d3d8_resources.c) is built as the Wii U build builds it, for 32-bit big-endian PowerPC, and run under qemu-ppc -cpu 750 (its dcbz needs the Espresso's
32-byte lines) by a made-up game (replay_game.c) in a made-up world (replay_world.c). The command streams it hands over are read as the GX2 thread reads
them (replay_gx2.c, with the GX2 thread's own derivation, dk_derive.c, and its decals' expansion), and each draw comes out as a line of what the GPU is
given: the targets, the derived state, the shaders' keys, the textures with their contents as the commands made them by then, the uniforms, the
constants, the vertex format, and the bytes of the indices and of every vertex the draw reads, hashed when this thread reads them, as the GPU's copies
are made then. The lines' digest is the run's picture: a change to the layer that keeps the pictures keeps it, whatever the GX2 thread's schedule.
tools/test_wiiu_d3d8_replay.py builds it, checks the digest it recorded under each schedule, and has the faults it must see.

	replay MODE SCHEDULE SEED FRAMES [TRACE]

MODE: sync (gx2_thread 0: each stream run as it is handed over) or async (posted to the queue); SCHEDULE: lag (a stream is run only when the game thread
waits for one: the GX2 thread as far behind as the six slots let it be) or eager (also some at moments chosen by SEED). Writes each line to TRACE, if
named, and to the standard output the lines' digest, the digest of the resources' Lock fields at each present (which the stream's hand-overs number,
so it is the same for a mode, not across them), and what the run counted: the lines, the draws, the log's lines and those that say a state or a target
was found changed with no setter having said so.

What it does not have of the GX2 thread: the GX2 calls, the shader pack, the ring and the static data cache (which copy the bytes hashed here), the GPU
test. What it does not have of the game: anything but the calls, so a draw's pictures are its inputs, not pixels.
*/

#include "replay.h"

#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	FILE *trace = NULL;
	int async, eager;
	unsigned long frames;

	if (argc < 5)
	{
		fprintf(stderr, "usage: %s sync|async lag|eager SEED FRAMES [TRACE]\n", argv[0]);
		return 2;
	}
	async = !strcmp(argv[1], "async");
	eager = !strcmp(argv[2], "eager");
	frames = strtoul(argv[4], NULL, 0);
	if (argc > 5 && !(trace = fopen(argv[5], "w")))
	{
		fprintf(stderr, "replay: cannot write %s\n", argv[5]);
		return 2;
	}
	replay_verbose_log = getenv("REPLAY_LOG") != NULL;
	if (!replay_window_make())
		return 2;
	replay_gx2_start(async, eager, (uint32_t)strtoul(argv[3], NULL, 0), trace);
	replay_game_run(frames);
	halo_switch_renderer_idle();
	replay_gx2_finish();
	if (trace)
		fclose(trace);
	printf("digest %016llx\n", (unsigned long long)replay_gx2_digest());
	printf("locks %016llx\n", (unsigned long long)replay_locks);
	printf("lines %lu draws %lu log %lu states-missed %lu targets-missed %lu most-queued %lu\n", replay_gx2_lines(), replay_gx2_draws(), replay_log_lines,
		replay_states_missed_lines, replay_targets_missed_lines, replay_gx2_most_queued());
	return 0;
}
