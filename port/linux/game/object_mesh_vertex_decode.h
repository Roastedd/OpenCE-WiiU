#ifndef OBJECT_MESH_VERTEX_DECODE_H
#define OBJECT_MESH_VERTEX_DECODE_H

#include <stdint.h>
#include <string.h>

/* Model GPU streams retain Xbox byte order on Wii U, while CPU-created
 * streams are native. Decode fields without native struct loads or alignment
 * assumptions. Normals and UVs are not needed by the surface/waist reader. */
static uint16_t object_mesh_u16(const unsigned char *p, int little)
{
    return little ? (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8) :
        (uint16_t)((uint16_t)p[1] | (uint16_t)p[0] << 8);
}

static float object_mesh_float(const unsigned char *p, int little)
{
    uint32_t bits = little ? (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24 :
        (uint32_t)p[3] | (uint32_t)p[2] << 8 |
        (uint32_t)p[1] << 16 | (uint32_t)p[0] << 24;
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void object_mesh_vertex_decode(const unsigned char *p, int compressed, int little,
    real_point3d *position, long *node0, long *node1, real *weight)
{
    position->x = object_mesh_float(p, little);
    position->y = object_mesh_float(p + 4, little);
    position->z = object_mesh_float(p + 8, little);
    if (compressed)
    {
        *node0 = p[28] / 3;
        *node1 = p[29] / 3;
        *weight = (real)(int16_t)object_mesh_u16(p + 30, little) * (1.f / 32767.f);
    }
    else
    {
        *node0 = (int16_t)object_mesh_u16(p + 56, little);
        *node1 = (int16_t)object_mesh_u16(p + 58, little);
        *weight = object_mesh_float(p + 60, little);
    }
}

#endif
