/* What the kernels' start-up needs of the CPU, as leaf functions the start-up runs under an exception guard (wiiu_bw_kernels.c): the log says
 * what a core does when a paired-single instruction, a read of the quantization registers' user aliases or a write of GQR5's is not
 * allowed, instead of the run ending there. Written for this port; not from the bundle.
 *
 *   void halo_bw_asm_probe_ps(const float *two (r3), float *out (r4));      out = two + two, as a pair
 *   void halo_bw_asm_read_gqr(unsigned *eight (r3));                       GQR0 to GQR7, their user aliases 896 to 903
 *   void halo_bw_asm_set_gqr5(unsigned value (r3));                        GQR5 (901) made value, only if halo/broadway.txt asks
 */
    .machine "broadway"
    .section .text.halo_bw_probe,"ax",@progbits
    .balign 4
    .global halo_bw_range_probe_begin
halo_bw_range_probe_begin:

    .global halo_bw_asm_probe_ps
    .type halo_bw_asm_probe_ps,@function
halo_bw_asm_probe_ps:
    psq_l       0,0(3),0,0
    ps_add      1,0,0
    psq_st      1,0(4),0,0
    blr
    .size halo_bw_asm_probe_ps,.-halo_bw_asm_probe_ps

/* the user aliases of the eight quantization registers (896 to 903), for the log:
 *   void halo_bw_asm_read_gqr(unsigned *eight (r3)); */
    .global halo_bw_asm_read_gqr
    .type halo_bw_asm_read_gqr,@function
halo_bw_asm_read_gqr:
    mfspr       0,896
    stw         0,0(3)
    mfspr       0,897
    stw         0,4(3)
    mfspr       0,898
    stw         0,8(3)
    mfspr       0,899
    stw         0,12(3)
    mfspr       0,900
    stw         0,16(3)
    mfspr       0,901
    stw         0,20(3)
    mfspr       0,902
    stw         0,24(3)
    mfspr       0,903
    stw         0,28(3)
    blr
    .size halo_bw_asm_read_gqr,.-halo_bw_asm_read_gqr

/* GQR5 (user alias 901) made the signed 16-bit load, type 7 and scale 0 for loads and stores alike, as libogc's CAST_Init and Nintendo's
 * context setup make it: only if halo/broadway.txt asks (3), on the thread that will call the quaternion kernel (the register is per thread),
 * and only to make the register the kernel checks for; whether a user program may write the alias is not known: the start-up's exception
 * guard catches a refusal and the log says it.
 *   void halo_bw_asm_set_gqr5(unsigned value (r3)); */
    .global halo_bw_asm_set_gqr5
    .type halo_bw_asm_set_gqr5,@function
halo_bw_asm_set_gqr5:
    mtspr       901,3
    blr
    .size halo_bw_asm_set_gqr5,.-halo_bw_asm_set_gqr5
    .global halo_bw_range_probe_end
halo_bw_range_probe_end:
