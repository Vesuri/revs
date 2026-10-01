| shape_m68k.s — scale_shape_vectors IN 68000 REGISTERS (Amiga only; `make SHAPEASM=0` is the C control)
|
| This is scale_shape_vectors_core (src/gen/revs_native.c, twin #94): the object's width and five
| halvings into shape_scale_tbl[2..7], then each vector byte of the shape scaled out of that table
| into shape_vertex[x], with its negation at [8 + x].  The C stays the reference: the host runs it,
| and `make SHAPECHECK=1` runs both on the same 64 KB every call, after a fuzzer
| (amiga/shape_check.gdb), and compares all of it and the return value.
|
| ⭐ WHY: single-stepped at ~240 instructions an object (docs/perf-method.md §the object plotter).
| GCC rebuilt the vector table's address from `mem` on every vertex and zero-extended each index
| twice; here the four tables sit in address registers and the cursors in byte-wide data registers.
| ⭐ The rounded extra halving is ((a >> (s - 1)) + 1) >> 1, which equals the C's
| (a >> s) + ((a >> (s - 1)) & 1) for every a below $200.
| ⭐ WHAT IT STORES: every byte the C stores, with the same value.
|
| int scale_shape_m68k(void) -> 1 when a vertex needs eight bits (abandon the object), else 0
| Registers: a0 = mem, a1 = shape_scale_tbl, a2 = shape_vector_tbl, a3 = shape_vertex;
|   d0 = the vertex, d1 = the extra shift - 1 (or -1 for none), d2 = the vector cursor,
|   d3 = its end, d4 = the output cursor, d5 = the vector byte, d6 = its second term's index.

	.equ	Z_WIDTH,    0x2A        | proj_width
	.equ	Z_SHIFT,    0x2B        | proj_width_shift
	.equ	Z_MATHLO,   0x74        | math_lo
	.equ	Z_MATHHI,   0x75        | math_hi
	.equ	Z_OUT,      0x77        | shared_temp_77 — the output cursor
	.equ	Z_VCUR,     0x81        | OBJ_VECTOR_CURSOR (point_delta_lo[1])
	.equ	Z_VEND,     0x8A        | OBJ_VECTOR_END (bearing_lo)
	.equ	VECTORS,    0x4480      | shape_vector_tbl
	.equ	VERTEX,     0x5EF8      | shape_vertex (sixteen entries)
	.equ	SCALE,      0x5FF8      | shape_scale_tbl

| `--defsym SABOTAGE=N` (`make SHAPECHECK=1 SHAPEASM_SABOTAGE=N`): a deliberate defect SHAPECHECK
| must catch.  0 in every real build.
	.ifndef SABOTAGE
	.equ	SABOTAGE, 0
	.endif

	.section .text.scale_shape_m68k,"ax",@progbits
	.even
	.globl	scale_shape_m68k
	.type	scale_shape_m68k, @function
scale_shape_m68k:
	movem.l	d2-d6/a2-a3,-(sp)
	lea	mem,a0
	lea	SCALE(a0),a1
	lea	VECTORS(a0),a2
	lea	VERTEX(a0),a3

	| $202A-$2042 — the width, then five halvings
	move.b	Z_WIDTH(a0),d0
	move.b	d0,2(a1)
	lsr.b	#1,d0
	move.b	d0,3(a1)
	lsr.b	#1,d0
	move.b	d0,4(a1)
	lsr.b	#1,d0
	move.b	d0,5(a1)
	lsr.b	#1,d0
	move.b	d0,6(a1)
	lsr.b	#1,d0
	move.b	d0,7(a1)

	| the extra halving: none at 0, and past 8 places every bit and the rounding carry are gone
	moveq	#0,d1
	move.b	Z_SHIFT(a0),d1
	cmp.b	#8,d1
	bls.s	1f
	moveq	#9,d1
1:	subq.w	#1,d1                       | -1: no extra halving

	moveq	#0,d2
	move.b	Z_VCUR(a0),d2               | $2043 — the vector cursor
	move.b	Z_VEND(a0),d3
	moveq	#0,d4                       | the output cursor
	moveq	#0,d5
	moveq	#0,d6
ss_loop:
	move.b	(a2,d2.w),d5                | $2049
	bmi.s	ss_two
	moveq	#0,d0
	move.b	(a1,d5.w),d0                | $2072 — one term
	tst.w	d1
	bmi.s	ss_store
	bra.s	ss_shift
ss_two:                                 | $204E-$2071 — two terms, and a third with bit 6
	moveq	#7,d0
	and.b	d5,d0
	move.b	(a1,d0.w),d0
	move.b	d0,Z_MATHLO(a0)
	move.b	d5,Z_MATHHI(a0)
	move.b	d5,d6
	lsr.b	#3,d6
	and.w	#7,d6
	add.b	(a1,d6.w),d0                | (d0's upper bytes stay 0: it was an index below 8)
	btst	#6,d5
	beq.s	1f
	add.b	3(a1),d0
1:	tst.w	d1
	bmi.s	ss_store
ss_shift:                               | $2076-$207F — rounded: the closing ADC #0
	lsr.w	d1,d0
	.if SABOTAGE != 1                   | SABOTAGE 1: the rounding bit dropped
	addq.w	#1,d0
	.endif
	lsr.w	#1,d0
ss_store:                               | $2080-$2096
	move.b	d0,(a3,d4.w)
	bmi.s	ss_abandon
	neg.b	d0
	.if SABOTAGE == 2
	move.b	d0,9(a3,d4.w)               | SABOTAGE 2: the negation one entry off
	.else
	move.b	d0,8(a3,d4.w)
	.endif
	addq.b	#1,d4
	addq.b	#1,d2
	cmp.b	d3,d2
	bne.s	ss_loop
	move.b	d4,Z_OUT(a0)
	moveq	#0,d0
	movem.l	(sp)+,d2-d6/a2-a3
	rts
ss_abandon:
	move.b	d4,Z_OUT(a0)
	moveq	#1,d0
	movem.l	(sp)+,d2-d6/a2-a3
	rts
	.size	scale_shape_m68k, .-scale_shape_m68k
