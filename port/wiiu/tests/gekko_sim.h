/*
GEKKO_SIM.H

A small instruction simulator for exactly the instructions the Broadway kernels (port/wiiu/src/bw) use, to run their real machine code on
the host. QEMU does not emulate paired singles, so the kernels cannot run under qemu-ppc as they are; this runs the code the assembler
made (tools/bw_object_image.py puts it in a C file), word by word, on a 32-bit big-endian host (qemu-ppc), where an address of the
guest is an address of the host and the data are in the byte order the kernels read.

It checks the kernel's data flow, branches and operand order against the models and the C: that the assembly is what the model
says. It does not check what the hardware does that the manuals leave open (a subnormal in a paired operation, a NaN's payload through
a paired operation): the console's self-test does. It enforces the contract the kernels state: only volatile registers (r0, r3 to r12,
f0 to f13, cr0 to cr1) written, nothing of the link register or the stack touched, and, to catch a kernel that reads the second half
of a register a scalar instruction wrote or an lfs loaded, those halves are poisoned with a NaN.

Not a general PowerPC simulator: an instruction it does not know stops the test.
*/

#ifndef GEKKO_SIM_H
#define GEKKO_SIM_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct bw_relocation { unsigned offset; char kind; };
struct bw_function { const char *name; unsigned entry; };
struct bw_object
{
	const char *name;
	const unsigned *code;
	unsigned words;
	const unsigned char *constants;
	unsigned constant_bytes;
	const struct bw_relocation *relocations;
	unsigned relocation_count;
	const struct bw_function *functions;
	unsigned function_count;
};

struct gekko
{
	uint32_t r[32];
	float ps0[32], ps1[32];
	uint32_t cr;		/* cr0 in the top four bits */
	uint32_t gqr[8];
	uint32_t gpr_written, fpr_written, cr_written;
	unsigned long executed;
};

static float gekko_poison(void)
{
	const uint32_t bits = 0x7fabcdefu;
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static void gekko_stop(const char *what, uint32_t word, unsigned index)
{
	printf("gekko_sim: %s (instruction %08x at word %u)\n", what, (unsigned)word, index);
	exit(2);
}

static void gekko_set_cr(struct gekko *g, unsigned field, int lt, int gt, int eq, int un)
{
	const unsigned shift = 28 - 4 * field;

	g->cr = (g->cr & ~(0xfu << shift)) | ((uint32_t)((lt << 3) | (gt << 2) | (eq << 1) | un) << shift);
	g->cr_written |= 1u << field;
}

static int gekko_cr_bit(const struct gekko *g, unsigned bit)
{
	return (int)((g->cr >> (31 - bit)) & 1u);
}

static void gekko_gpr(struct gekko *g, unsigned index, uint32_t value)
{
	g->r[index] = value;
	g->gpr_written |= 1u << index;
}

static void gekko_fpr(struct gekko *g, unsigned index, float p0, float p1)
{
	g->ps0[index] = p0;
	g->ps1[index] = p1;
	g->fpr_written |= 1u << index;
}

/* the quantized load and store: GQR type 0 (float) and 7 (signed 16 bits, with a scale of 0), the two the kernels use */
static float gekko_load_quantized(const struct gekko *g, unsigned index, uintptr_t address, unsigned item, unsigned word, unsigned instruction)
{
	const uint32_t type = (g->gqr[index] >> 16) & 7u, scale = (g->gqr[index] >> 24) & 0x3fu;

	if (type == 0 && scale == 0)
		return ((const float *)address)[item];
	if (type == 7 && scale == 0)
		return (float)((const int16_t *)address)[item];
	gekko_stop("a quantization the simulator does not do", instruction, word);
	return 0.0f;
}

/* runs the code from word entry until the return (blr); returns the instructions run */
static unsigned long gekko_run(struct gekko *g, const unsigned *code, unsigned words, unsigned entry)
{
	unsigned pc = entry;
	uint32_t link = 0xfeedf00du;

	g->executed = 0;
	for (;;)
	{
		const uint32_t w = pc < words ? code[pc] : 0;
		const unsigned op = w >> 26, rd = (w >> 21) & 31, ra = (w >> 16) & 31, rb = (w >> 11) & 31, rc = (w >> 6) & 31;
		const int32_t simm = (int16_t)(w & 0xffff);
		const uint32_t base = ra ? g->r[ra] : 0;
		unsigned next = pc + 1;

		if (pc >= words)
			gekko_stop("ran off the code", w, pc);
		if (++g->executed > 100000)
			gekko_stop("did not return", w, pc);
		switch (op)
		{
		case 14:	/* addi, li */
			gekko_gpr(g, rd, base + (uint32_t)simm);
			break;
		case 15:	/* addis, lis */
			gekko_gpr(g, rd, base + ((uint32_t)simm << 16));
			break;
		case 7:		/* mulli */
			gekko_gpr(g, rd, (uint32_t)((int32_t)g->r[ra] * simm));
			break;
		case 11:	/* cmpi */
			{
				const int32_t a = (int32_t)g->r[ra];

				gekko_set_cr(g, (w >> 23) & 7, a < simm, a > simm, a == simm, 0);
			}
			break;
		case 10:	/* cmpli */
			{
				const uint32_t a = g->r[ra], b = w & 0xffff;

				gekko_set_cr(g, (w >> 23) & 7, a < b, a > b, a == b, 0);
			}
			break;
		case 32:	/* lwz */
			gekko_gpr(g, rd, *(const uint32_t *)(uintptr_t)(base + (uint32_t)simm));
			break;
		case 36:	/* stw */
			*(uint32_t *)(uintptr_t)(base + (uint32_t)simm) = g->r[rd];
			break;
		case 40:	/* lhz */
			gekko_gpr(g, rd, *(const uint16_t *)(uintptr_t)(base + (uint32_t)simm));
			break;
		case 48:	/* lfs: ps0 is loaded; whether ps1 is is not something a kernel may rely on */
			gekko_fpr(g, rd, *(const float *)(uintptr_t)(base + (uint32_t)simm), gekko_poison());
			break;
		case 52:	/* stfs */
			*(float *)(uintptr_t)(base + (uint32_t)simm) = g->ps0[rd];
			break;
		case 56:	/* psq_l */
			{
				const unsigned weight = (w >> 15) & 1, index = (w >> 12) & 7;
				const int32_t displacement = (int32_t)((w & 0xfff) << 20) >> 20;
				const uintptr_t address = (uintptr_t)(base + (uint32_t)displacement);
				const float first = gekko_load_quantized(g, index, address, 0, pc, w);

				gekko_fpr(g, rd, first, weight ? 1.0f : gekko_load_quantized(g, index, address, 1, pc, w));
			}
			break;
		case 60:	/* psq_st (float stores only) */
			{
				const unsigned weight = (w >> 15) & 1, index = (w >> 12) & 7;
				const int32_t displacement = (int32_t)((w & 0xfff) << 20) >> 20;
				float *address = (float *)(uintptr_t)(base + (uint32_t)displacement);

				if (((g->gqr[index]) & 0x3f07u) != 0)
					gekko_stop("a store quantization the simulator does not do", w, pc);
				address[0] = g->ps0[rd];
				if (!weight)
					address[1] = g->ps1[rd];
			}
			break;
		case 16:	/* bc */
			{
				const unsigned bo = rd, bi = ra;
				const int taken = (bo & 0x10) || (gekko_cr_bit(g, bi) == (int)((bo >> 3) & 1));

				if ((bo & 0x10) == 0 && !(bo & 4))
					gekko_stop("a counting branch", w, pc);
				if ((w & 3) != 0)
					gekko_stop("an absolute or linking branch", w, pc);
				if (taken)
					next = pc + (unsigned)((int32_t)(int16_t)(w & 0xfffc) / 4);
			}
			break;
		case 18:	/* b */
			if ((w & 3) != 0)
				gekko_stop("an absolute or linking branch", w, pc);
			next = pc + (unsigned)(((int32_t)((w & 0x03fffffc) << 6)) >> 8);
			break;
		case 19:
			switch ((w >> 1) & 0x3ff)
			{
			case 16:	/* blr */
				if (rd != 20)
					gekko_stop("a conditional blr", w, pc);
				return g->executed;
			case 449:	/* cror */
				{
					const int bit = gekko_cr_bit(g, ra) | gekko_cr_bit(g, rb);

					g->cr = (g->cr & ~(1u << (31 - rd))) | ((uint32_t)bit << (31 - rd));
					g->cr_written |= 1u << (rd / 4);
				}
				break;
			default:
				gekko_stop("an unknown branch-unit instruction", w, pc);
			}
			break;
		case 21:	/* rlwinm (slwi and srwi) */
			{
				const unsigned shift = rb, mb = rc, me = (w >> 1) & 31;
				const uint32_t source = g->r[rd];
				const uint32_t rotated = shift ? (source << shift) | (source >> (32 - shift)) : source;
				uint32_t mask = 0;
				unsigned bit;

				for (bit = mb;; bit = (bit + 1) & 31)
				{
					mask |= 0x80000000u >> bit;
					if (bit == me)
						break;
				}
				if (w & 1)
					gekko_stop("rlwinm.", w, pc);
				gekko_gpr(g, ra, rotated & mask);
			}
			break;
		case 31:
			switch ((w >> 1) & 0x3ff)
			{
			case 266:	/* add */
				gekko_gpr(g, rd, g->r[ra] + g->r[rb]);
				break;
			case 0:		/* cmpw */
				{
					const int32_t a = (int32_t)g->r[ra], b = (int32_t)g->r[rb];

					gekko_set_cr(g, (w >> 23) & 7, a < b, a > b, a == b, 0);
				}
				break;
			case 32:	/* cmplw */
				{
					const uint32_t a = g->r[ra], b = g->r[rb];

					gekko_set_cr(g, (w >> 23) & 7, a < b, a > b, a == b, 0);
				}
				break;
			case 922:	/* extsh */
				gekko_gpr(g, ra, (uint32_t)(int32_t)(int16_t)g->r[rd]);
				break;
			case 444:	/* or, mr */
				gekko_gpr(g, ra, g->r[rd] | g->r[rb]);
				break;
			case 467:	/* mtspr: GQR5's user alias, as the kernel's set-up writes it */
				{
					const unsigned spr = ra | (rb << 5);

					if (spr != 901)
						gekko_stop("an spr the kernel may not write", w, pc);
					g->gqr[5] = g->r[rd];
				}
				break;
			case 339:	/* mfspr: the quantization registers' user aliases */
				{
					const unsigned spr = ra | (rb << 5);

					if (spr < 896 || spr > 903)
						gekko_stop("an spr the kernel may not read", w, pc);
					gekko_gpr(g, rd, g->gqr[spr - 896]);
				}
				break;
			default:
				gekko_stop("an unknown integer instruction", w, pc);
			}
			break;
		case 59:
			{
				const float a = g->ps0[ra], b = g->ps0[rb], c = g->ps0[rc];

				switch ((w >> 1) & 31)
				{
				case 20: gekko_fpr(g, rd, a - b, gekko_poison()); break;	/* fsubs */
				case 21: gekko_fpr(g, rd, a + b, gekko_poison()); break;	/* fadds */
				case 25: gekko_fpr(g, rd, a * c, gekko_poison()); break;	/* fmuls */
				default: gekko_stop("an unknown single-precision instruction", w, pc);
				}
			}
			break;
		case 63:
			switch ((w >> 1) & 0x3ff)
			{
			case 0:		/* fcmpu */
				{
					const float a = g->ps0[ra], b = g->ps0[rb];
					const int un = a != a || b != b;

					gekko_set_cr(g, (w >> 23) & 7, !un && a < b, !un && a > b, !un && a == b, un);
				}
				break;
			case 40:	/* fneg */
				gekko_fpr(g, rd, -g->ps0[rb], gekko_poison());
				break;
			default:
				gekko_stop("an unknown double-precision instruction", w, pc);
			}
			break;
		case 4:
			{
				const float a0 = g->ps0[ra], a1 = g->ps1[ra], b0 = g->ps0[rb], b1 = g->ps1[rb], c0 = g->ps0[rc], c1 = g->ps1[rc];

				switch ((w >> 1) & 0x3ff)
				{
				case 528: gekko_fpr(g, rd, a0, b0); goto paired_done;	/* ps_merge00 */
				case 560: gekko_fpr(g, rd, a0, b1); goto paired_done;	/* ps_merge01 */
				case 592: gekko_fpr(g, rd, a1, b0); goto paired_done;	/* ps_merge10 */
				case 624: gekko_fpr(g, rd, a1, b1); goto paired_done;	/* ps_merge11 */
				case 72: gekko_fpr(g, rd, b0, b1); goto paired_done;	/* ps_mr */
				default: break;
				}
				switch ((w >> 1) & 31)
				{
				case 10: gekko_fpr(g, rd, a0 + b1, c1); break;		/* ps_sum0 */
				case 11: gekko_fpr(g, rd, c0, a0 + b1); break;		/* ps_sum1 */
				case 12: gekko_fpr(g, rd, a0 * c0, a1 * c0); break;	/* ps_muls0 */
				case 13: gekko_fpr(g, rd, a0 * c1, a1 * c1); break;	/* ps_muls1 */
				case 20: gekko_fpr(g, rd, a0 - b0, a1 - b1); break;	/* ps_sub */
				case 21: gekko_fpr(g, rd, a0 + b0, a1 + b1); break;	/* ps_add */
				case 25: gekko_fpr(g, rd, a0 * c0, a1 * c1); break;	/* ps_mul */
				default: gekko_stop("an unknown paired-single instruction", w, pc);
				}
			paired_done:
				break;
			}
			break;
		default:
			gekko_stop("an unknown instruction", w, pc);
		}
		pc = next;
	}
	(void)link;
}

/* a call: the registers as the EABI passes them, run, and the contract checked */
struct gekko_image
{
	unsigned *code;
	unsigned words;
	unsigned char *constants;
};

/* puts an object's code in memory with its constants and applies the relocations (addresses of the host, which are the guest's) */
static void gekko_load(struct gekko_image *image, const struct bw_object *object)
{
	unsigned index;

	image->words = object->words;
	image->code = (unsigned *)malloc(object->words * 4);
	image->constants = (unsigned char *)malloc(object->constant_bytes + 16);
	memcpy(image->code, object->code, object->words * 4);
	memcpy(image->constants, object->constants, object->constant_bytes);
	for (index = 0; index < object->relocation_count && object->constant_bytes; index++)
	{
		const uint32_t address = (uint32_t)(uintptr_t)image->constants;
		const unsigned word = object->relocations[index].offset / 4;
		const uint16_t half = object->relocations[index].kind == 'H' ? (uint16_t)((address + 0x8000u) >> 16) : (uint16_t)(address & 0xffffu);

		image->code[word] = (image->code[word] & 0xffff0000u) | half;
	}
}

static void gekko_start(struct gekko *g, const uint32_t gqr0, const uint32_t gqr5)
{
	unsigned index;

	memset(g, 0, sizeof(*g));
	for (index = 0; index < 8; index++)
		g->gqr[index] = 0;
	g->gqr[0] = gqr0;
	g->gqr[5] = gqr5;
	for (index = 0; index < 32; index++)
	{
		g->r[index] = 0x5a5a0000u + index;
		g->ps0[index] = g->ps1[index] = gekko_poison();
	}
}

/* after a run: only the registers the EABI lets a function use were written, and the ones the caller needs are as they were */
static void gekko_check_contract(const struct gekko *g, const char *name)
{
	const uint32_t allowed_gprs = 1u | (0x3ffu << 3);	/* r0, r3 to r12 */
	const uint32_t allowed_fprs = 0x3fffu;				/* f0 to f13 */

	if (g->gpr_written & ~allowed_gprs)
	{
		printf("gekko_sim: %s wrote registers it may not: gpr mask %08x\n", name, (unsigned)(g->gpr_written & ~allowed_gprs));
		exit(2);
	}
	if (g->fpr_written & ~allowed_fprs)
	{
		printf("gekko_sim: %s wrote registers it may not: fpr mask %08x\n", name, (unsigned)(g->fpr_written & ~allowed_fprs));
		exit(2);
	}
	if (g->cr_written & ~3u)
	{
		printf("gekko_sim: %s wrote condition register fields it may not: %02x\n", name, (unsigned)(g->cr_written & ~3u));
		exit(2);
	}
}

#endif
