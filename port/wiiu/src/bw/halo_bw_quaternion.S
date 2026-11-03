/* quaternion_decompress_8byte (source/models/model_animations.c): four signed 16-bit samples to a quaternion, adapted from the
 * bundle's wii_broadway_quaternion.S (the Broadway assembly bundle of 2026-11-01, CC0, see README.md here).
 *
 * Only the load is here. The bundle's blend (quaternions_interpolate_and_normalize) normalises with frsqrte and Newton steps
 * in double precision, which does not give the float the game's 1 / sqrtf (wiiu_sqrtf.c, correctly rounded) gives, and the rest
 * of it is scalar work the compiler does as well: the blend stays the C function.
 *
 * Needs the signed 16-bit load of a quantization register: GQR5 (user alias 901) = 0x00070007's load half, type 7 and scale
 * 0 (Nintendo's context setup and libogc's CAST_Init do it; whether the Wii U's threads have it is read at the start and said in
 * the log). Anything else returns 0 with nothing written, and the C runs. The bundle read GQR5 as SPR 917, which is the
 * supervisor register: the Wii U's user programs have the aliases at 896 to 903 (the audio resampler reads GQR0 as 896).
 *
 *   int halo_bw_asm_quaternion_load(const void *compressed (r3), float *quaternion (r4));
 * The samples times binary32(1/32767) (0x38000100), each rounded once as the C's (float)short * scale is.
 */
    .machine "broadway"
    .section .rodata.halo_bw_qscale,"a",@progbits
    .balign 4
.Lscale:
    .long 0x38000100

    .section .text.halo_bw_quaternion,"ax",@progbits
    .balign 4
    .global halo_bw_range_quaternion_begin
halo_bw_range_quaternion_begin:
    .global halo_bw_asm_quaternion_load
    .type halo_bw_asm_quaternion_load,@function
halo_bw_asm_quaternion_load:
    mfspr       5,901
    srwi        5,5,16
    cmpwi       5,7
    bne         .Lload_scalar
    lis         5,.Lscale@ha
    lfs         0,.Lscale@l(5)
    psq_l       1,0(3),0,5          /* i, j */
    psq_l       2,4(3),0,5          /* k, w */
    ps_muls0    1,1,0
    ps_muls0    2,2,0
    psq_st      1,0(4),0,0
    psq_st      2,8(4),0,0
    li          3,1
    blr
.Lload_scalar:
    li          3,0
    blr
    .size halo_bw_asm_quaternion_load,.-halo_bw_asm_quaternion_load
    .global halo_bw_range_quaternion_end
halo_bw_range_quaternion_end:
