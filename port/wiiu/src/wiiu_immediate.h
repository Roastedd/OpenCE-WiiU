/* The vertices of an immediate-mode Begin/End (D3DDevice_Begin, SetVertexData..., End): what a vertex holds is the registers as they were when register 0 was written, but only the
registers written since the Begin go to the draw, so only those are kept, side by side (a vertex is a few cache lines at most: sixteen registers apart were a line each, and the
Espresso waits for every line it writes). A register first written after some vertices were made gives them the value it held before: the vertices are laid out again then. */
#ifndef WIIU_IMMEDIATE_H
#define WIIU_IMMEDIATE_H

#define WIIU_IMMEDIATE_REGISTERS 16

struct wiiu_immediate
{
	/* the registers written since Begin (bit 0, the position, always) */
	unsigned long mask;
	float *vertices; /* count vertices of layout_count registers each, four floats a register, the registers of layout_mask in order */
	unsigned long count, capacity;
	unsigned long layout_mask;
	unsigned layout_count, layout[WIIU_IMMEDIATE_REGISTERS];
};

void wiiu_immediate_begin(struct wiiu_immediate *immediate);
/* a register is about to be written (old_value: what it held); call before the write */
void wiiu_immediate_write(struct wiiu_immediate *immediate, unsigned reg, const float *old_value);
/* a vertex is complete: the registers' values now (0 if memory ran out) */
int wiiu_immediate_emit(struct wiiu_immediate *immediate, const float (*registers)[4]);
/* the vertices as the draw wants them, a vertex after another, each register's four floats in order of register; `out` has room for count * popcount(mask) * 4 floats. Returns how
many registers, which are listed in `registers` */
unsigned wiiu_immediate_pack(const struct wiiu_immediate *immediate, unsigned long count, float *out, unsigned *registers);

#endif
