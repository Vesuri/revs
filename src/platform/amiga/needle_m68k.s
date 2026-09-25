| needle_m68k.s — plot_line_octant's DDA FOR THE SPRITE NEEDLES, IN 68000 REGISTERS
| (Amiga only, REVS_NEEDLE_PLANES builds; `make NDLASM=0` is the C control)
|
| This is the `for (;;)` of plot_line_octant_core (src/gen/revs_native.c) on its NEEDLE_PLANES arm,
| where a pixel is an APPEND to g_needlePo/g_needlePix and nothing is stored through the plot
| pointer.  The C loop stays the reference: the host runs it, and `make NDLASMCHECK=1` runs both on
| the same 64 KB every call and compares all of it, the pointer and the whole pixel list.
|
| ⭐ WHY: single-stepped at ~76 instructions a pixel, ~35 pixels a frame — two `switch`es on the
| self-modified opcode bytes in mem[] every pixel, math_lo re-read and math_hi decremented in mem[]
| (both only necessary on the 6502-plot arm, where a pixel can land on them), four scratch cells
| stored per pixel, and the list append's global count loaded and stored per pixel.  Here the octant
| picks one of eight specialised loops ONCE, and every value lives in a register.
| ⭐ WHAT IT STORES: what the C leaves — bearing_lo (the last accumulator), shared_temp_76/77 (the
| last mask index and column), math_hi (the spent counter), plot_ptr_v (C publishes its lanes), the
| list and its count.  math_lo and the delta are read once: on this arm nothing stores to mem[].
|
| unsigned needle_dda_m68k(unsigned x, unsigned y, unsigned acc, unsigned po, unsigned line, unsigned cell)
|   -> the number of pixels that fell off the display (REVS_NEEDLE_OUTSIDE), or $FFFF when the two
|      opcode slots are not one of the eight octants (the C loop runs instead, nothing touched)
| Registers: a0 = mem, a1 = plot_ptr, a2 = &g_needlePo, a3 = &g_needlePix, a4 = the list index,
|   a5 = the display line (low byte), a6 = the cell (low byte); d0 = the DDA accumulator, d1 = x,
|   d2 = y, d3 = the pixel counter (bits 16-23, math_hi) : the increment (bits 0-7, math_lo),
|   d4 = the mask index, d5 = the mask base, d6 = scratch, d7 = the plane offset (`y * 80 + cell`).

	.equ	Z_MATHLO,   0x74
	.equ	Z_MATHHI,   0x75
	.equ	Z_T76,      0x76            | shared_temp_76 — the pixel's mask index
	.equ	Z_T77,      0x77            | shared_temp_77 — the sub-cell column
	.equ	Z_MASKBASE, 0x79            | hypot_min_hi
	.equ	Z_DELTA,    0x83            | point_delta_hi — the DDA's major delta
	.equ	Z_BEARLO,   0x8A            | bearing_lo — the parked accumulator
	.equ	SMC_MAJOR,  0x5220
	.equ	SMC_MINOR,  0x529B
	.equ	LINE_STRIDE, 80             | REVS_NEEDLE_LINE_STRIDE
	.equ	NDL_YN,     208             | REVS_NEEDLE_YN
	.equ	NDL_CELLS,  40              | REVS_NEEDLE_CELLS
	.equ	NDL_MAX,    64              | REVS_NEEDLE_MAX
	.equ	ARGS,       44+4            | past the eleven saved registers and the return
	.equ	OP_DEY, 0x88
	.equ	OP_INY, 0xC8
	.equ	OP_DEX, 0xCA
	.equ	OP_INX, 0xE8

	.ifndef SABOTAGE
	.equ	SABOTAGE, 0
	.endif

| One step: 0 DEY, 1 INY, 2 DEX, 3 INX.  A Y step moves the display line and the plane offset with it.
	.macro	STEP kind
	.if \kind == 0
	subq.b	#1,d2
	subq.w	#1,a5
	sub.w	#LINE_STRIDE,d7
	.endif
	.if \kind == 1
	addq.b	#1,d2
	addq.w	#1,a5
	.if SABOTAGE == 1
	add.w	#LINE_STRIDE+1,d7           | SABOTAGE 1: an INY step lands one cell right
	.else
	add.w	#LINE_STRIDE,d7
	.endif
	.endif
	.if \kind == 2
	subq.b	#1,d1
	.endif
	.if \kind == 3
	addq.b	#1,d1
	.endif
	.endm

| One whole loop, specialised on the octant's two steps.
	.macro	DDA maj, min
	.even
dda_\maj\()_\min:
	add.b	d3,d0                       | $521A: acc += incr
	bcc.s	1f
	sub.b	Z_DELTA(a0),d0              | carry: acc -= delta, and the major step
	STEP	\maj
1:	move.b	d1,d4                       | $5223: the mask index, from x BEFORE its cell carry
	lsr.b	#1,d4
	and.b	#3,d4
	or.b	d5,d4
	move.b	d4,Z_T76(a0)
	tst.b	d1
	bpl.s	3f
	| $522C: x ran below 0.  One cell left — unless that borrowed out of the pointer's low byte, in
	| which case the 6502's re-test ($523D) steps straight back right and only x = 0 survives.
	move.w	a1,d6
	cmp.b	#8,d6
	bcs.s	2f
	moveq	#7,d1
	subq.w	#8,a1
	subq.w	#1,a6
	subq.w	#1,d7
	bra.s	4f
2:	moveq	#0,d1
	bra.s	4f
3:	cmp.b	#8,d1                       | $523D: x ran past 7: one cell right
	bcs.s	4f
	moveq	#0,d1
	addq.w	#8,a1
	addq.w	#1,a6
	.if SABOTAGE != 2
	addq.w	#1,d7
	.endif
4:	move.b	d1,Z_T77(a0)                | $524E
	tst.b	d2                          | $5253: y's character-row carry, which moves the pointer only
	bpl.s	5f
	suba.w	#0x140,a1
	moveq	#7,d2
	bra.s	6f
5:	cmp.b	#8,d2
	bcs.s	6f
	adda.w	#0x140,a1
	moveq	#0,d2
6:	move.w	a5,d6                       | the clip: on the race display, or no sprite can show it
	cmp.b	#NDL_YN,d6
	bcc.s	8f
	move.w	a6,d6
	cmp.b	#NDL_CELLS,d6
	bcc.s	8f
	cmpa.w	#NDL_MAX,a4
	bcc.s	9f                          | the list is full: dropped, and not "outside"
	move.w	a4,d6
	add.w	d6,d6
	move.w	d7,(a2,d6.w)                | g_needlePo[n]
	.if SABOTAGE == 3
	and.w	#3,d4                       | SABOTAGE 3: the mask index loses its base bit
	.else
	and.w	#7,d4
	.endif
	lsl.w	#2,d4
	move.w	d4,(a3,d6.w)                | g_needlePix[n] = (mask & 7) << 2
	addq.w	#1,a4
	bra.s	9f
8:	addq.w	#1,(sp)                     | REVS_NEEDLE_OUTSIDE — counted on the stack
9:	STEP	\min                        | $529B: the every-pixel step
	sub.l	#0x10000,d3                 | $529C: DEC math_hi...
	btst	#23,d3
	.if SABOTAGE == 4
	beq	dda_\maj\()_\min+2          | SABOTAGE 4: the loop re-enters past the accumulator add
	.else
	jbeq	dda_\maj\()_\min            | ...until it goes negative
	.endif
	jbra	dda_exit
	.endm

	.section .text.needle_dda_m68k,"ax",@progbits
	.even
	.globl	needle_dda_m68k
	.type	needle_dda_m68k, @function
needle_dda_m68k:
	movem.l	d2-d7/a2-a6,-(sp)
	lea	mem,a0
	| which of the eight octants: major index * 4 + minor index, over 0 DEY 1 INY 2 DEX 3 INX
	move.b	SMC_MAJOR(a0),d0
	jbsr	op_index
	move.w	d6,d1
	jbmi	dda_refuse
	lsl.w	#2,d1
	move.b	SMC_MINOR(a0),d0
	jbsr	op_index
	tst.w	d6
	jbmi	dda_refuse
	or.w	d6,d1                       | d1 = major*4 + minor
	add.w	d1,d1
	lea	dda_table(pc),a1
	move.w	(a1,d1.w),d6
	jbeq	dda_refuse                  | a same-axis pair: not an octant
	adda.w	d6,a1                       | the loop to enter
	clr.w	-(sp)                       | the outside count, which the loop keeps at (sp)
	move.l	a1,-(sp)                    | ...under the loop's address, which the `rts` below pops
	| the state, from the arguments and mem[]
	.equ	A2ARGS, ARGS+6
	moveq	#0,d1
	move.b	A2ARGS+3(sp),d1             | x
	moveq	#0,d2
	move.b	A2ARGS+7(sp),d2             | y
	moveq	#0,d0
	move.b	A2ARGS+11(sp),d0            | acc
	moveq	#0,d7
	move.w	A2ARGS+14(sp),d7            | po
	movea.w	A2ARGS+18(sp),a5            | line (low byte)
	movea.w	A2ARGS+22(sp),a6            | cell (low byte)
	moveq	#0,d3
	move.b	Z_MATHHI(a0),d3
	swap	d3
	move.b	Z_MATHLO(a0),d3             | counter : increment
	move.b	Z_MASKBASE(a0),d5
	movea.w	plot_ptr_v,a1
	lea	g_needlePo,a2
	lea	g_needlePix,a3
	moveq	#0,d6
	move.b	g_needleCount,d6
	movea.w	d6,a4
	rts                                 | into the octant's loop

dda_refuse:
	move.l	#0xFFFF,d0
	movem.l	(sp)+,d2-d7/a2-a6
	rts

| d0 = an opcode -> d6 = 0..3, or -1
op_index:
	moveq	#0,d6
	cmp.b	#OP_DEY,d0
	beq.s	1f
	moveq	#1,d6
	cmp.b	#OP_INY,d0
	beq.s	1f
	moveq	#2,d6
	cmp.b	#OP_DEX,d0
	beq.s	1f
	moveq	#3,d6
	cmp.b	#OP_INX,d0
	beq.s	1f
	moveq	#-1,d6
1:	rts

dda_exit:
	move.b	d0,Z_BEARLO(a0)
	swap	d3
	move.b	d3,Z_MATHHI(a0)
	move.w	a1,plot_ptr_v
	move.w	a4,d6
	move.b	d6,g_needleCount
	moveq	#0,d0
	move.w	(sp)+,d0                    | the outside count
	movem.l	(sp)+,d2-d7/a2-a6
	rts

| major*4 + minor -> the loop, as an offset from here (0 = not an octant)
dda_table:
	.word	0,               0,               dda_0_2-dda_table, dda_0_3-dda_table
	.word	0,               0,               dda_1_2-dda_table, dda_1_3-dda_table
	.word	dda_2_0-dda_table, dda_2_1-dda_table, 0,             0
	.word	dda_3_0-dda_table, dda_3_1-dda_table, 0,             0

	DDA	0, 2
	DDA	0, 3
	DDA	1, 2
	DDA	1, 3
	DDA	2, 0
	DDA	2, 1
	DDA	3, 0
	DDA	3, 1
	.size	needle_dda_m68k, .-needle_dda_m68k
