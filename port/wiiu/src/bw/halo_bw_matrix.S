/* Matrix arithmetic adapted from the read-only 360 Wii donor, through the Broadway assembly bundle of 2026-11-01
 * (CC0, see LICENSES/CC0-1.0.txt and README.md here).
 * 52-byte engine matrix: scale, three basis vectors, position. No vertex work.
 * Exact input/output aliasing is supported. Uses volatile EABI registers only.
 *
 * Changed from the bundle for the Wii U: every multiply and add is separate and rounded to single precision, in the order
 * of the C function it replaces (matrix4x3_multiply, source/math/matrix_math.c). The bundle's ps_madds0 and fmadds fuse the
 * two and round once, which gives other bits than the C and than the PCs, so they are not used.
 *
 *   void halo_bw_asm_matrix4x3_multiply(const float *a (r3), const float *b (r4), float *out (r5));
 *
 * Element k of the 12 after the scale is at byte 4 + 4k. For each row r of b, column c of the three:
 *   value = b[3r] * a[c] + b[3r+1] * a[3+c] + b[3r+2] * a[6+c]      (added left to right)
 * and in the last row, value = value * a.scale + a[9+c]. The columns 0 and 1 are one pair, the column 2 is single.
 * a is read into registers before anything is written; each row of b is read before its row of out is written, and a's
 * position (row 3) is read after rows 0 to 2 of out are written, which do not touch it: out may be a or b.
 */
    .machine "broadway"
    .section .text.halo_bw_matrix,"ax",@progbits
    .balign 4

    .macro ROW_VALUE xy, z
    psq_l       8,\xy(4),0,0        /* (Bx, By) */
    lfs         9,\z(4)             /* Bz */
    ps_muls0    10,2,8              /* (a0*Bx, a1*Bx) */
    ps_muls1    12,3,8              /* (a3*By, a4*By) */
    ps_add      10,10,12
    ps_muls0    12,4,9              /* (a6*Bz, a7*Bz) */
    ps_add      10,10,12
    fmuls       11,5,8              /* a2*Bx */
    ps_merge11  13,8,8              /* By */
    fmuls       12,6,13             /* a5*By */
    fadds       11,11,12
    fmuls       12,7,9              /* a8*Bz */
    fadds       11,11,12
    .endm

    .macro ROW xy, z
    ROW_VALUE   \xy, \z
    psq_st      10,\xy(5),0,0
    stfs        11,\z(5)
    .endm

    .global halo_bw_asm_matrix4x3_multiply
    .type halo_bw_asm_matrix4x3_multiply,@function
halo_bw_asm_matrix4x3_multiply:
    lfs         0,0(3)              /* a.scale */
    lfs         1,0(4)              /* b.scale */
    fmuls       1,0,1
    stfs        1,0(5)
    psq_l       2,4(3),0,0          /* a0, a1 */
    lfs         5,12(3)             /* a2 */
    psq_l       3,16(3),0,0         /* a3, a4 */
    lfs         6,24(3)             /* a5 */
    psq_l       4,28(3),0,0         /* a6, a7 */
    lfs         7,36(3)             /* a8 */
    ROW         4,12
    ROW         16,24
    ROW         28,36
    ROW_VALUE   40,48
    ps_muls0    10,10,0             /* value * a.scale */
    fmuls       11,11,0
    psq_l       12,40(3),0,0        /* a9, a10 */
    lfs         13,48(3)            /* a11 */
    ps_add      10,10,12
    fadds       11,11,13
    psq_st      10,40(5),0,0
    stfs        11,48(5)
    blr
    .size halo_bw_asm_matrix4x3_multiply,.-halo_bw_asm_matrix4x3_multiply
