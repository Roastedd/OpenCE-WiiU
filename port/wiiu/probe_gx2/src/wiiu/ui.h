/*
UI.H

Rectangles, pictures and text for the probe's pages, drawn with one pair of shaders (the "ui" GSH: a textured, tinted
quad) so that what is on the screen has been drawn by the same pipeline the tests are judged with. Quads are collected
and drawn together by ui_flush; positions are pixels of the view (top left origin) that ui_begin was told about.
*/

#ifndef HALO_WIIU_GX2_UI_H
#define HALO_WIIU_GX2_UI_H

#include "gx.h"

/* loads the shaders and makes the buffers; 0 with the reason in error */
int ui_init(char *error, size_t error_size);
void ui_shutdown(void);
int ui_ready(void);

/* the start of a frame: waits for the GPU to finish with the last frame's vertices and takes them over */
void ui_frame(void);
/* the size in pixels of the viewport the following quads are placed in */
void ui_begin(float view_width, float view_height);
/* texture NULL for a flat color; u and v in 0..1; blend 1 for alpha blending */
void ui_quad(float x, float y, float w, float h, const GX2Texture *texture, float u0, float v0, float u1, float v1, const float tint[4], int blend);
void ui_rect(float x, float y, float w, float h, const float color[4]);
void ui_flush(void);

/* the pipeline's pieces, for a test that draws with the same shaders */
const GX2VertexShader *ui_vertex_shader(void);
const GX2PixelShader *ui_pixel_shader(void);

#endif
