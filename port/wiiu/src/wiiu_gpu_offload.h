/*
WIIU_GPU_OFFLOAD.H

The switches of halo/gpu_offload.txt (a number; 0, no file or a negative number: none on, the frames drawn as without the file). Each bit is a way of taking work off the
game thread's drawing or off the GX2 thread; none of them is a GPU feature of its own, because the study that made them
(reports/research/2026-11-03/gpu-offload.md) found none that pays here.

  bit 1 (1)  blends: the interpolation's blends (render_interpolation.c) with the square roots left out (render_interpolation_fast.h): 2.3 ms of a fight's drawing
             in the game thread's time becomes about 1.4. The poses differ by a few units in a float's last place.
  bit 2 (2)  census: the GX2 thread times what it does for each draw (the state derivation, the shader keys, the decals' expansion, the rest of the draw's set-up and
             the GX2 calls) and counts how many draws repeat an earlier draw of the frame (the same state, shaders and textures; the same mesh): a log line every
             five seconds ("gpu offload: ..."). It changes nothing that is drawn; its timers cost about 0.1 ms a frame.
  bit 3 (4)  check: with bit 1, every blend is made both ways and the largest difference said every 600 frames ("gpu offload check: ..."). For the host's runs; it doubles
             the blends' cost.
*/
#ifndef WIIU_GPU_OFFLOAD_H
#define WIIU_GPU_OFFLOAD_H

#define WIIU_GPU_OFFLOAD_BLENDS 1u
#define WIIU_GPU_OFFLOAD_CENSUS 2u
#define WIIU_GPU_OFFLOAD_CHECK 4u
#define WIIU_GPU_OFFLOAD_KNOWN (WIIU_GPU_OFFLOAD_BLENDS | WIIU_GPU_OFFLOAD_CENSUS | WIIU_GPU_OFFLOAD_CHECK)

/* the bits a file's text gives: its first number if it is a positive one, bits not known left out; 0 for anything else */
unsigned wiiu_gpu_offload_parse(const char *text);

/* the bits, read from the card once (the first call; the log says them) */
unsigned wiiu_gpu_offload_bits(void);

#endif
