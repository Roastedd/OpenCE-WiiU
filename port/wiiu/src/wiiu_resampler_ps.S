/* Audio-only 48-tap interpolation. Two paired registers preserve the four
   existing accumulation lanes and separately rounded multiply/add steps.
   Ordinary pointer arguments, volatile EABI registers, no stack or calls. */
	.machine "broadway"
	.section .text.wiiu_resampler_ps,"ax",@progbits
	.balign 4
	.global wiiu_resampler_ps_dot48
	.type wiiu_resampler_ps_dot48,@function
wiiu_resampler_ps_dot48:
	lis 12,wiiu_resampler_ps_zero@ha
	lfs 0,wiiu_resampler_ps_zero@l(12)
	lfs 1,wiiu_resampler_ps_zero@l(12)
	li 0,12
	mtctr 0
1:
	psq_l 2,0(4),0,0
	psq_l 3,8(4),0,0
	psq_l 4,0(3),0,0
	psq_l 5,8(3),0,0
	ps_mul 6,4,2
	ps_mul 7,5,3
	ps_add 0,0,6
	ps_add 1,1,7
	addi 3,3,16
	addi 4,4,16
	bdnz 1b
	ps_sum0 0,0,0,0
	ps_sum0 1,1,1,1
	fadds 0,0,1
	stfs 0,0(5)
	blr
	.size wiiu_resampler_ps_dot48,.-wiiu_resampler_ps_dot48

	.global wiiu_resampler_ps_dot48_stereo
	.type wiiu_resampler_ps_dot48_stereo,@function
wiiu_resampler_ps_dot48_stereo:
	lis 12,wiiu_resampler_ps_zero@ha
	lfs 0,wiiu_resampler_ps_zero@l(12)
	lfs 1,wiiu_resampler_ps_zero@l(12)
	lfs 2,wiiu_resampler_ps_zero@l(12)
	lfs 3,wiiu_resampler_ps_zero@l(12)
	li 0,12
	mtctr 0
2:
	psq_l 4,0(5),0,0
	psq_l 5,8(5),0,0
	psq_l 6,0(3),0,0
	psq_l 7,8(3),0,0
	psq_l 8,0(4),0,0
	psq_l 9,8(4),0,0
	ps_mul 10,6,4
	ps_mul 11,7,5
	ps_add 0,0,10
	ps_add 1,1,11
	ps_mul 10,8,4
	ps_mul 11,9,5
	ps_add 2,2,10
	ps_add 3,3,11
	addi 3,3,16
	addi 4,4,16
	addi 5,5,16
	bdnz 2b
	ps_sum0 0,0,0,0
	ps_sum0 1,1,1,1
	ps_sum0 2,2,2,2
	ps_sum0 3,3,3,3
	fadds 0,0,1
	fadds 2,2,3
	stfs 0,0(6)
	stfs 2,4(6)
	blr
	.size wiiu_resampler_ps_dot48_stereo,.-wiiu_resampler_ps_dot48_stereo

	.global wiiu_resampler_ps_blended48
	.type wiiu_resampler_ps_blended48,@function
wiiu_resampler_ps_blended48:
	lis 12,wiiu_resampler_ps_zero@ha
	lfs 0,wiiu_resampler_ps_zero@l(12)
	lfs 1,wiiu_resampler_ps_zero@l(12)
	lfs 12,0(6)
	li 0,12
	mtctr 0
3:
	psq_l 2,0(4),0,0
	psq_l 3,8(4),0,0
	psq_l 4,0(5),0,0
	psq_l 5,8(5),0,0
	ps_mul 4,4,12
	ps_mul 5,5,12
	ps_add 2,2,4
	ps_add 3,3,5
	psq_l 6,0(3),0,0
	psq_l 7,8(3),0,0
	ps_mul 8,6,2
	ps_mul 9,7,3
	ps_add 0,0,8
	ps_add 1,1,9
	addi 3,3,16
	addi 4,4,16
	addi 5,5,16
	bdnz 3b
	ps_sum0 0,0,0,0
	ps_sum0 1,1,1,1
	fadds 0,0,1
	stfs 0,0(7)
	blr
	.size wiiu_resampler_ps_blended48,.-wiiu_resampler_ps_blended48

	.global wiiu_resampler_ps_blended48_stereo
	.type wiiu_resampler_ps_blended48_stereo,@function
wiiu_resampler_ps_blended48_stereo:
	lis 12,wiiu_resampler_ps_zero@ha
	lfs 0,wiiu_resampler_ps_zero@l(12)
	lfs 1,wiiu_resampler_ps_zero@l(12)
	lfs 2,wiiu_resampler_ps_zero@l(12)
	lfs 3,wiiu_resampler_ps_zero@l(12)
	lfs 12,0(7)
	li 0,12
	mtctr 0
4:
	psq_l 4,0(5),0,0
	psq_l 5,8(5),0,0
	psq_l 6,0(6),0,0
	psq_l 7,8(6),0,0
	ps_mul 6,6,12
	ps_mul 7,7,12
	ps_add 4,4,6
	ps_add 5,5,7
	psq_l 6,0(3),0,0
	psq_l 7,8(3),0,0
	psq_l 8,0(4),0,0
	psq_l 9,8(4),0,0
	ps_mul 6,6,4
	ps_mul 7,7,5
	ps_mul 8,8,4
	ps_mul 9,9,5
	ps_add 0,0,6
	ps_add 1,1,7
	ps_add 2,2,8
	ps_add 3,3,9
	addi 3,3,16
	addi 4,4,16
	addi 5,5,16
	addi 6,6,16
	bdnz 4b
	ps_sum0 0,0,0,0
	ps_sum0 1,1,1,1
	ps_sum0 2,2,2,2
	ps_sum0 3,3,3,3
	fadds 0,0,1
	fadds 2,2,3
	stfs 0,0(8)
	stfs 2,4(8)
	blr
	.size wiiu_resampler_ps_blended48_stereo,.-wiiu_resampler_ps_blended48_stereo

	.section .rodata.wiiu_resampler_ps,"a",@progbits
	.balign 4
wiiu_resampler_ps_zero:
	.long 0
