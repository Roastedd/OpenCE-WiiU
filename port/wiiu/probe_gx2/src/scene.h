/*
SCENE.H

The little pictures the tests draw into the 64 x 64 target, and the pixels the CPU looks at afterwards. Positions are
in normalized device coordinates (x right, y up, the top row of the target at y = 1). Keeping them here lets the desktop
test check that every pixel that is looked at lies inside the triangle it belongs to, or outside all of them.
*/

#ifndef HALO_WIIU_GX2_SCENE_H
#define HALO_WIIU_GX2_SCENE_H

#include <stdint.h>

#define SCENE_SIZE 64u

/* the color every test clears to; its four channels are all different, so that the order of the channels in the
buffer can be read off one pixel (readback_detect_order) */
extern const uint8_t scene_clear_rgba[4];

typedef struct scene_point
{
	float x, y;
} scene_point;

/* the target pixel that has this position at its center (floor of the mapped coordinates) */
void scene_pixel_of(scene_point p, unsigned *x, unsigned *y);
/* is the center of pixel (x, y) inside the triangle (either winding)? */
int scene_inside(const scene_point triangle[3], unsigned x, unsigned y);

/* the vertex stream test: a triangle in the lower left half and one in the upper right half; one pixel inside each */
void scene_halves(scene_point vertices[6]);
void scene_halves_sample(unsigned which, unsigned *x, unsigned *y);

/* the index test: twelve vertices, a small triangle in each corner of the target (vertex k belongs to corner k / 3) */
void scene_corners(scene_point vertices[12]);
void scene_corner_sample(unsigned corner, unsigned *x, unsigned *y);
void scene_corner_color(unsigned corner, uint8_t rgba[4]);

/* the generated vertex shader's triangle: the positions the game's vertex shader is given (x, y, z, w) -- it flips y --
and the vertex colors; one pixel inside the drawn triangle and one outside */
void scene_generated(float positions[3][4], float colors[3][4]);
void scene_generated_sample(unsigned *inside_x, unsigned *inside_y, unsigned *outside_x, unsigned *outside_y);
void scene_generated_color(uint8_t rgba[4]);

#endif
