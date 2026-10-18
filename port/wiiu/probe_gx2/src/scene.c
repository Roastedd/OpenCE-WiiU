#include "scene.h"

const uint8_t scene_clear_rgba[4] = { 51, 102, 153, 204 };

void scene_pixel_of(scene_point p, unsigned *x, unsigned *y)
{
	float fx = (p.x + 1.0f) * 0.5f * (float)SCENE_SIZE;
	float fy = (1.0f - p.y) * 0.5f * (float)SCENE_SIZE;

	*x = fx < 0.0f ? 0u : fx >= (float)SCENE_SIZE ? SCENE_SIZE - 1 : (unsigned)fx;
	*y = fy < 0.0f ? 0u : fy >= (float)SCENE_SIZE ? SCENE_SIZE - 1 : (unsigned)fy;
}

int scene_inside(const scene_point t[3], unsigned x, unsigned y)
{
	float px = ((float)x + 0.5f) / (float)SCENE_SIZE * 2.0f - 1.0f;
	float py = 1.0f - ((float)y + 0.5f) / (float)SCENE_SIZE * 2.0f;
	float e0 = (t[1].x - t[0].x) * (py - t[0].y) - (t[1].y - t[0].y) * (px - t[0].x);
	float e1 = (t[2].x - t[1].x) * (py - t[1].y) - (t[2].y - t[1].y) * (px - t[1].x);
	float e2 = (t[0].x - t[2].x) * (py - t[2].y) - (t[0].y - t[2].y) * (px - t[2].x);

	return (e0 > 0 && e1 > 0 && e2 > 0) || (e0 < 0 && e1 < 0 && e2 < 0);
}

void scene_halves(scene_point v[6])
{
	/* lower left half of the square (bottom left, bottom right, top left), then the upper right half */
	v[0].x = -0.9f; v[0].y = -0.9f;
	v[1].x = 0.9f;  v[1].y = -0.9f;
	v[2].x = -0.9f; v[2].y = 0.9f;
	v[3].x = 0.9f;  v[3].y = 0.9f;
	v[4].x = -0.9f; v[4].y = 0.9f;
	v[5].x = 0.9f;  v[5].y = -0.9f;
}

void scene_halves_sample(unsigned which, unsigned *x, unsigned *y)
{
	*x = which ? 48u : 16u;
	*y = which ? 16u : 48u;
}

static void corner_center(unsigned corner, float *ox, float *oy)
{
	*ox = (corner & 1) ? 0.5f : -0.5f;
	*oy = (corner & 2) ? -0.5f : 0.5f;
}

void scene_corners(scene_point v[12])
{
	unsigned c;

	for (c = 0; c < 4; c++)
	{
		float ox, oy;

		corner_center(c, &ox, &oy);
		v[c * 3 + 0].x = ox - 0.35f; v[c * 3 + 0].y = oy - 0.35f;
		v[c * 3 + 1].x = ox + 0.35f; v[c * 3 + 1].y = oy - 0.35f;
		v[c * 3 + 2].x = ox;         v[c * 3 + 2].y = oy + 0.35f;
	}
}

void scene_corner_sample(unsigned corner, unsigned *x, unsigned *y)
{
	float ox, oy;
	scene_point p;

	corner_center(corner, &ox, &oy);
	p.x = ox;
	p.y = oy - 0.1f;
	scene_pixel_of(p, x, y);
}

void scene_corner_color(unsigned corner, uint8_t rgba[4])
{
	static const uint8_t colors[4][4] = { { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 }, { 255, 255, 0, 255 } };

	rgba[0] = colors[corner & 3][0];
	rgba[1] = colors[corner & 3][1];
	rgba[2] = colors[corner & 3][2];
	rgba[3] = colors[corner & 3][3];
}

void scene_generated(float p[3][4], float c[3][4])
{
	static const float positions[3][4] = { { -0.9f, -0.9f, 0.5f, 1.0f }, { 0.9f, -0.9f, 0.5f, 1.0f }, { -0.9f, 0.9f, 0.5f, 1.0f } };
	unsigned i, k;

	for (i = 0; i < 3; i++)
	{
		for (k = 0; k < 4; k++)
		{
			p[i][k] = positions[i][k];
			c[i][k] = k == 3 ? 1.0f : 0.25f * (float)(k + 1);
		}
	}
}

void scene_generated_color(uint8_t rgba[4])
{
	rgba[0] = 64;  /* 0.25 */
	rgba[1] = 128; /* 0.5 */
	rgba[2] = 191; /* 0.75 */
	rgba[3] = 255;
}

void scene_generated_sample(unsigned *inside_x, unsigned *inside_y, unsigned *outside_x, unsigned *outside_y)
{
	/* the vertex shader flips y: the triangle drawn has its corners at the top left, the top right and the bottom left */
	*inside_x = 12;
	*inside_y = 12;
	*outside_x = 52;
	*outside_y = 52;
}
