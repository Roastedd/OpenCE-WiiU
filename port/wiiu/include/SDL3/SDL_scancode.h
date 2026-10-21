/* SDL3/SDL_scancode.h, as the Wii U build has it: port/linux/src/sdl_platform.h sizes its key table by SDL's scancode count (512). The Wii U
reads no keyboard; the table is the platform layer's, kept for the declarations shared with the Linux builds. */
#ifndef HALO_WIIU_SDL_SCANCODE_SHIM_H
#define HALO_WIIU_SDL_SCANCODE_SHIM_H
#define SDL_SCANCODE_COUNT 512
#endif
