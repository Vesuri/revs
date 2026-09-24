| emit_width_m68k.s — emit_edge_width_offset IN 68000 REGISTERS (Amiga only; `make GEOASM=0` is the C control)
|
| This is emit_edge_width_offset_core (src/gen/revs_native.c) for the two calls road_edge_walk makes,
| both with firstScoringPoint = 3 and entry V = 0, and both reading only the exit V.  The C core is
| still the reference: the host runs it (make validate), and `make GEOCHECK=1` runs both on the target
| on the same 64 KB every call and compares all of it and the V.
|
| ⭐ WHY: single-stepped at ~131 instructions a call, 27 calls a frame — a frame pointer, stack spills,
| a six-byte struct returned through memory and a byte-by-byte replay of the 6502's V.  Here every
| value is in a register, the struct is one byte in d0, and V is the `add.w`'s own overflow flag: the
| 6502's V off its high-byte ADC (carry in from the low byte) IS the signed overflow of the 16-bit add.
| ⭐ WHAT IT STORES: every cell the C stores, with the same value — shared_temp_77/76, math_lo/hi,
| edge_opp_x, edge_style, edge_y, horizon_extent/index — so mem[] is byte-identical after every call.
| C is called for the three rare arms only: a point beside the car, where the width takes the 6502's
| own float computation (emit_width_far); a corner marker (emit_width_marker); and a circuit that has
| patched the $261A horizon store (emit_width_c runs the whole C core — decided at entry, before any
| store, so the two can never interleave).
|
| unsigned emit_width_m68k(unsigned sectionByte)  -> the exit V, 0 or 1
| Registers: a0 = mem, a1 table scratch, d1 = the section byte, d2 = the masked flags, d3 scratch
|   (the feature, then k, then the edge word), d4 = the style, d5 = the offset, d6 = V out.

	.equ	Z_CURSOR,   0x12            | edge_cursor
	.equ	Z_HORIZON,  0x1F            | horizon_extent
	.equ	Z_TRACKDIR, 0x25            | track_direction
	.equ	Z_COUNT42,  0x42            | shared_counter_42 — points emitted on this side
	.equ	Z_SIDE,     0x49            | road_side_index
	.equ	Z_HORIDX,   0x51            | horizon_index
	.equ	Z_MATHLO,   0x74
	.equ	Z_MATHHI,   0x75
	.equ	Z_TEMP76,   0x76
	.equ	Z_TEMP77,   0x77
	.equ	Z_DISTLO,   0x7C            | point_dist
	.equ	Z_DISTHI,   0x7D
	.equ	Z_LINE,     0x8D            | projected_line
	.equ	SECT_FLAGS, 0x0702          | section_flags (a byte past $77 wraps back by 120)
	.equ	SIDE_MASK,  0x306C          | edge_side_flag_mask
	.equ	STYLE_FEAT, 0x306E          | edge_style_by_feature
	.equ	WIDTH_K,    0x3076          | edge_width_shift_tbl
	.equ	EDGE_XLO,   0x5E40          | edge_x_lo; from it: x_hi +$50, opp_lo +$10, opp_hi +$60
	.equ	EDGE_STYLE, 0x5EE0          | edge_style; edge_y at +$40
	.equ	SMC_HOOK,   0x261A          | the horizon store pair: STA $1F ($85) .. STY ($84) unpatched
	.equ	ARGS,       24+4            | past the six saved registers and the return

| `--defsym SABOTAGE=N` (`make GEOCHECK=1 GEOASM_SABOTAGE=N`): a deliberate defect GEOCHECK must
| catch.  0 in every real build.
	.ifndef SABOTAGE
	.equ	SABOTAGE, 0
	.endif

	.section .text.emit_width_m68k,"ax",@progbits
	.even
	.globl	emit_width_m68k
	.type	emit_width_m68k, @function
emit_width_m68k:
	movem.l	d2-d6/a2,-(sp)
	lea	mem,a0
	moveq	#0,d1
	move.b	ARGS+3(sp),d1               | the section byte
	cmp.b	#0x85,SMC_HOOK(a0)
	jbne	ew_c                        | a circuit's own horizon hook: the C core, whole
	cmp.b	#0x84,SMC_HOOK+2(a0)
	jbne	ew_c
	moveq	#0,d6                       | V = the entry V unless the width add runs

	| $2565-$257E — the point's feature bits, masked to this side's, and the style they select
	move.w	d1,d0
	cmp.b	#0x78,d0
	bcs.s	1f
	sub.w	#0x78,d0                    | past the 120-byte list: the same table wrapped
1:	lea	SECT_FLAGS(a0),a1
	move.b	(a1,d0.w),d2
	moveq	#0,d0
	move.b	Z_SIDE(a0),d0
	lea	SIDE_MASK(a0),a1
	and.b	(a1,d0.w),d2
	move.b	d2,Z_TEMP77(a0)
	moveq	#7,d3
	and.w	d2,d3                       | the feature
	lea	STYLE_FEAT(a0),a1
	move.b	(a1,d3.w),d4
	move.b	d4,Z_TEMP76(a0)

	| $2580 — nothing but the style for a side's first three points
	cmp.b	#3,Z_COUNT42(a0)
	jbcs	ew_style

	| $2589-$25A9 — the half-width: 2^(22-k) / point_dist in one DIVU (edge_width_offset_for)
	lea	WIDTH_K(a0),a1
	move.b	(a1,d3.w),d3                | k (a table byte a circuit could rewrite)
	moveq	#0,d0
	move.b	Z_DISTHI(a0),d0
	lsl.w	#8,d0
	move.b	Z_DISTLO(a0),d0             | point_dist
	cmp.b	#15,d3
	bhi.s	ew_far
	moveq	#64,d5
	lsr.w	d3,d5
	cmp.w	d5,d0
	bls.s	ew_far                      | dist <= 64 >> k: the quotient would not fit
	move.l	#0x400000,d5
	lsr.l	d3,d5
	.if SABOTAGE == 1
	add.l	d0,d5                       | SABOTAGE 1: the quotient one high (dividend + divisor)
	.endif
	divu.w	d0,d5                       | the offset, 0..$FFFF
	bra.s	ew_sign
ew_far:
	movem.l	d1/a0,-(sp)                 | C scratch registers we still need
	move.l	d3,-(sp)
	move.l	d0,-(sp)
	jsr	emit_width_far              | the 6502's float, for the points beside the car
	addq.l	#8,sp
	movem.l	(sp)+,d1/a0
	move.l	d0,d5

	| $25AB-$25BE — which way it points: side 0/1 as a sign bit, EORed with the direction
ew_sign:
	move.b	Z_TRACKDIR(a0),d0
	tst.b	Z_SIDE(a0)
	beq.s	2f
	eor.b	#0x80,d0
2:	tst.b	d0
	jbpl	3f
	.if SABOTAGE != 2                   | SABOTAGE 2: the offset is never negated
	neg.w	d5
	.endif
3:	move.b	d5,Z_MATHLO(a0)
	move.w	d5,d0
	lsr.w	#8,d0
	move.b	d0,Z_MATHHI(a0)

	| $25C0-$25D2 — the far kerb: this point's angle plus the offset.  V is the 16-bit add's.
	moveq	#0,d0
	move.b	Z_CURSOR(a0),d0
	lea	EDGE_XLO(a0),a1
	move.b	0x50(a1,d0.w),d3
	lsl.w	#8,d3
	move.b	(a1,d0.w),d3                | edge_x
	add.w	d5,d3
	svs	d6
	.if SABOTAGE == 3
	moveq	#0,d6                       | SABOTAGE 3: V is dropped
	.endif
	neg.b	d6                          | $FF -> 1
	move.b	d3,0x10(a1,d0.w)            | edge_opp_x_lo
	lsr.w	#8,d3
	move.b	d3,0x60(a1,d0.w)            | edge_opp_x_hi

	| $25D3-$25FB — a corner marker, if the point carries one
	move.b	d2,d3
	and.b	#0x18,d3
	beq.s	ew_style
	movem.l	d1/a0,-(sp)
	moveq	#0,d0
	move.w	d5,d0
	move.l	d0,-(sp)
	moveq	#0,d0
	move.b	d2,d0
	move.l	d0,-(sp)
	jsr	emit_width_marker
	addq.l	#8,sp
	movem.l	(sp)+,d1/a0

	| $25FD-$260B — the style: 2 on an odd section byte, else the feature's
ew_style:
	moveq	#0,d0
	move.b	Z_CURSOR(a0),d0
	btst	#0,d1
	beq.s	4f
	moveq	#2,d4
4:	lea	EDGE_STYLE(a0),a1
	.if SABOTAGE == 4
	addq.b	#1,d4                       | SABOTAGE 4: every style one off
	.endif
	move.b	d4,(a1,d0.w)
	| $260D-$261D — the scan line, and the horizon if it reaches past everything before it
	move.b	Z_LINE(a0),d3
	move.b	d3,0x40(a1,d0.w)            | edge_y
	cmp.b	#0x50,d3
	bcc.s	ew_done                     | at or past the top of the 80-line space: sky
	cmp.b	Z_HORIZON(a0),d3
	bcs.s	ew_done
	move.b	d3,Z_HORIZON(a0)
	.if SABOTAGE != 5                   | SABOTAGE 5: the horizon point is not recorded
	move.b	d0,Z_HORIDX(a0)
	.endif
ew_done:
	moveq	#0,d0
	move.b	d6,d0
	movem.l	(sp)+,d2-d6/a2
	rts

ew_c:
	move.l	d1,-(sp)
	jsr	emit_width_c
	addq.l	#4,sp
	movem.l	(sp)+,d2-d6/a2
	rts
	.size	emit_width_m68k, .-emit_width_m68k
