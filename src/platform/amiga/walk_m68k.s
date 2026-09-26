| walk_m68k.s — road_edge_walk's POINT LOOP IN 68000 REGISTERS (Amiga only; `make WALKASM=0` is the C control)
|
| This is road_edge_walk_run (src/gen/revs_native.c) for everything a point does on its way to being
| kept: bearing_to_section (origin 0), emit_edge_bearing and point_distance_hypot, the running nearest,
| project_point (origin 0), the width emitter (emit_width_core, emit_width_m68k.s) and the step to the
| next section.  The C loop is still the reference: the host runs it (make validate, make determinism)
| and `make GEOCHECK=1` runs both on the same 64 KB every walk and compares all of it, the four C words
| the walk writes, and the exit.
|
| ⭐ WHY: single-stepped, a point cost ~200 instructions of C across three calls — every mem[] cell an
| absolute-long operand, a section coordinate assembled with six instructions, project_point's result
| returned through a stack struct, and a movem + argument push around each call.  Here the four bases
| and the three constant words live in registers for the whole walk, every mem[] cell is a (d16,a0)
| operand, and the emitter is entered below its prologue.
| ⭐ WHAT IT STORES: every cell and word the C stores that anything outside the walk READS, with the
| same value.  One deliberate reordering, provably invisible: bearing_to_section's store of
| hypot_min_v is folded into point_distance_hypot's, which always overwrites it before anything reads
| it (the hypot is the pair's only reader and runs in the same point).
| ⭐ EIGHT 6502 WORKING STORES A POINT ARE NOT MADE (THE RESULTS RULE, docs/validation-harness.md):
| point_delta_lo[0]/[2] $80/$82, point_delta_hi[0] $83, point_delta_sign[1] $87, the raw arctan
| shared_temp_7e $7E (three arms), the far hypot arm's math_lo/math_hi and the step's stride in
| math_lo.  READER AUDIT: `make rangeaudit RANGE=0074-0075,007C-0088,008D DEFUSE=1` on all five
| circuits (2026-09-26) — every read of a value bearing_to_section / point_distance_hypot /
| project_point / road_edge_walk_resume left in those cells is made by those same routines, by
| div16by8 inside them, or (the stride) by nothing at all; no circuit hook reads one.  What IS read
| outside, and stays stored: $85 (plot_view_src_line+14), $86 and $88 (interp_edge+172/+26 — the
| first span of the frame inherits the walk's last signs as SPAN_ARM/SPAN_CLIP), $7C/$7D
| (note_object_contact+3 reads the high byte) and $8D (road_edge_start+133, the emitter).
| `make GEOCHECK=1` masks exactly these cells (walk_dead_cells in revs_native.c).
| ⭐ WHAT IT HANDS BACK TO C: the three exits that are not "18 points kept" — a subdivide (clip or
| behind), and the off-axis seam, whose SMC site ($248B) a circuit rewrites into its own hook.  The C
| wrapper (road_edge_walk_run_asm) does those, exactly as the C loop would from the same state.
|
| unsigned road_edge_walk_m68k(unsigned section, unsigned pointCap, unsigned offAxis, unsigned resume)
|   -> bits 0-7 the section byte (the 6502's exit X), bits 8-9 the exit (0 done, 1 subdivide, 2 the
|      off-axis seam)
| Registers for the whole walk (all survive emit_width_core and its C callouts):
|   a0 = mem, a2 = view_origin_16[2] (a word), a3 = mem+$0900 (the section planes: lo +0, hi +$100),
|   a4 = mem+$5E40 (edge_x: lo +0, hi +$50), a5 = mem+$6100 (arctan_table), a6 = car_heading_v,
|   d7 = view_origin_16[1] : view_origin_16[0], d1 = the section byte, zero-extended to a long.
| Per point: d2/d4 the raw ground-plane deltas, d3/d5 the smaller/larger magnitude, d6 the raw arctan,
|   d0 the bearing and then the distance.

	.equ	Z_PITCH,    0x0D            | view_pitch_offset
	.equ	Z_WRAP,     0x0E            | section_wrap_limit
	.equ	Z_CURSOR,   0x12            | edge_cursor
	.equ	Z_NEARSECT, 0x13            | edge_nearest_section
	.equ	Z_PREVSECT, 0x14            | walk_prev_section
	.equ	Z_COUNT42,  0x42            | shared_counter_42 — points kept on this side
	.equ	Z_NEARCUR,  0x5C            | nearest_edge_cursor
	.equ	Z_NEARBHI,  0x5E            | nearest_edge_bearing_hi
	.equ	Z_MATHLO,   0x74
	.equ	Z_MATHHI,   0x75
	.equ	Z_DISTLO,   0x7C            | point_dist
	.equ	Z_DISTHI,   0x7D
	.equ	Z_ARCTAN,   0x7E            | shared_temp_7e — the raw arctan byte, the hypot's arm choice
	.equ	Z_DELTALO,  0x80            | point_delta_lo[3]; _hi at $83, _sign at $86
	.equ	Z_DELTAHI,  0x83
	.equ	Z_DELTASG,  0x86
	.equ	Z_LINE,     0x8D            | projected_line
	.equ	SECT_LO,    0x0900          | section coordinates, lo plane ($0A00 the hi plane)
	.equ	EDGE_XLO,   0x5E40          | edge_x_lo; edge_x_hi at +$50
	.equ	ARCTAN,     0x6100          | arctan_table
	.equ	STEP_TBL,   0x3DD0          | edge_walk_step_tbl
	.equ	ARGS,       44+4            | past the eleven saved registers and the return
	.equ	A_SECTION,  ARGS+3
	.equ	A_CAP,      ARGS+7
	.equ	A_OFFAXIS,  ARGS+11
	.equ	A_RESUME,   ARGS+12

| `--defsym SABOTAGE=N` (`make GEOCHECK=1 WALKASM_SABOTAGE=N`): a deliberate defect GEOCHECK must
| catch.  0 in every real build.
	.ifndef SABOTAGE
	.equ	SABOTAGE, 0
	.endif

| A word's HIGH byte to a mem[] cell: push the word, pop its first byte (a byte pop moves sp by 2).
| 24 cycles against 38 for move/lsr #8/move.
	.macro	STHI reg, dst
	move.w	\reg,-(sp)
	move.b	(sp)+,\dst
	.endm

	.section .text.road_edge_walk_m68k,"ax",@progbits
	.even
	.globl	road_edge_walk_m68k
	.type	road_edge_walk_m68k, @function
road_edge_walk_m68k:
	movem.l	d2-d7/a2-a6,-(sp)
	lea	mem,a0
	lea	SECT_LO(a0),a3
	lea	EDGE_XLO(a0),a4
	lea	ARCTAN(a0),a5
	move.w	view_origin_16+2,d7
	swap	d7
	move.w	view_origin_16,d7
	movea.w	view_origin_16+4,a2
	movea.w	car_heading_v,a6
	moveq	#0,d1
	move.b	A_SECTION(sp),d1
	tst.l	A_RESUME(sp)
	jbne	wk_step                     | a circuit's hook re-enters at $2490: keep, then step

	| ---- $23D8 -> $2147: bearing_to_section — components 0 and 2 of the delta, the ground plane
wk_point:
	lea	(a3,d1.w),a1                | this section's triple
	move.b	0x100(a1),d2
	lsl.w	#8,d2
	move.b	(a1),d2
	sub.w	d7,d2                       | delta 0 = section - view_origin[0]
	move.b	0x102(a1),d4
	lsl.w	#8,d4
	move.b	2(a1),d4
	sub.w	a2,d4                       | delta 2
	move.w	d2,d3
	bpl.s	1f
	neg.w	d3                          | |delta 0|
1:	move.w	d4,d5
	bpl.s	1f
	neg.w	d5                          | |delta 2|
1:	STHI	d2,Z_DELTASG+0(a0)          | (delta_lo[0]/[2] and delta_hi[0]: dead — the header)
	STHI	d5,Z_DELTAHI+2(a0)
	STHI	d4,Z_DELTASG+2(a0)

	| $2187 THE SORT — the smaller magnitude over the larger is the arctan's index
	cmp.w	d3,d5
	bcs.s	wk_arm0                     | |delta 2| < |delta 0|: measured off component 0
	beq.s	wk_diag                     | equal: a 45-degree diagonal

	| $2239 — measured off component 2 (d3 = |delta 0| the smaller, d5 = |delta 2| the larger)
	moveq	#0,d0
	move.w	d3,d0
	lsl.l	#8,d0
	divu.w	d5,d0                       | the TRUE ratio, 0..255 (smaller < larger)
	moveq	#0,d6
	move.b	(a5,d0.w),d6                | the raw arctan (in d6 only: $7E is dead — the header)
	move.w	d6,d0
	lsl.w	#5,d0                       | * 32 — a 16-bit angle
	eor.w	d4,d2                       | the two signs differ?
	bpl.s	1f
	neg.w	d0                          | ...then the other side of the axis
1:	tst.w	d4
	bpl.s	wk_bearing
	add.w	#0x8000,d0                  | the quadrant, from component 2's sign
	bra.s	wk_bearing

	| $21C1 — measured off component 0: the smaller is |delta 2|
wk_arm0:
	exg	d3,d5                       | d3 = the smaller, d5 = the larger, as on the other arm
	moveq	#0,d0
	move.w	d3,d0
	lsl.l	#8,d0
	divu.w	d5,d0
	moveq	#0,d6
	move.b	(a5,d0.w),d6
	move.w	d6,d0
	lsl.w	#5,d0
	eor.w	d2,d4                       | the two signs agree?
	.if SABOTAGE == 1
	bpl.s	1f                          | SABOTAGE 1: negate on the wrong sign test
	.else
	bmi.s	1f
	.endif
	neg.w	d0                          | ...then the other side of the axis
1:	add.w	#0x4000,d0                  | base $40
	tst.w	d2
	bpl.s	wk_bearing
	add.w	#0x8000,d0                  | ...or $C0, from component 0's sign
	bra.s	wk_bearing

	| $220D — the four diagonals on the two signs; $FF says "maximally oblique" to the hypot
wk_diag:
	moveq	#-1,d6
	move.w	#0x2000,d0
	tst.w	d4
	bpl.s	1f
	.if SABOTAGE == 2
	move.w	#0x2000,d0                  | SABOTAGE 2: one diagonal lost
	.else
	move.w	#0x6000,d0
	.endif
1:	tst.w	d2
	bpl.s	wk_bearing
	eor.w	#0xC000,d0                  | $2000 -> $E000, $6000 -> $A000

	| ---- $23C0 emit_edge_bearing: the angle FROM WHERE THE CAR POINTS, at the cursor
wk_bearing:
	move.w	d0,bearing_v
	sub.w	a6,d0                       | - car_heading
	moveq	#0,d2
	move.b	Z_CURSOR(a0),d2
	move.b	d0,(a4,d2.w)                | edge_x_lo[cursor]
	move.w	d0,-(sp)
	move.b	(sp)+,0x50(a4,d2.w)         | edge_x_hi[cursor] (STHI by hand: a macro argument cannot hold commas)

	| ---- $0CA5 point_distance_hypot — two octagonal fits, picked on the raw arctan
	move.w	d5,hypot_max_v
	cmp.b	#0x67,d6
	bcc.s	wk_far
	move.w	d3,d4                       | components far apart: max + min/8
	lsr.w	#3,d4
	move.w	d4,d0
	add.w	d5,d0                       | the distance
	.if SABOTAGE != 3                   | SABOTAGE 3: the near arm's low lane is overwritten
	move.b	d3,d4                       | ⚠ the near arm stores only the HIGH lane of min/8
	.endif
	move.w	d4,hypot_min_v
	bra.s	wk_dist
wk_far:
	lsr.w	#1,d3                       | comparable: min/2 + max - max/8
	move.w	d3,hypot_min_v
	move.w	d5,d4
	lsr.w	#3,d4                       | max/8
	move.w	d3,d0
	add.w	d5,d0
	sub.w	d4,d0                       | the distance (the 6502's math_lo/hi max/8 here: dead)
wk_dist:
	move.b	d0,Z_DISTLO(a0)
	STHI	d0,Z_DISTHI(a0)

	| ---- $23DB the RUNNING NEAREST — one 16-bit compare
	cmp.w	edge_nearest_v,d0
	bhi.s	1f
	move.w	d0,edge_nearest_v
	move.b	Z_COUNT42(a0),Z_NEARSECT(a0)
	move.b	d2,Z_NEARCUR(a0)
	.if SABOTAGE != 4                   | SABOTAGE 4: the nearest point's bearing is not recorded
	move.b	0x50(a4,d2.w),Z_NEARBHI(a0)
	.endif
1:
	| ---- $23FC -> $2287 project_point — component 1, the height, over the distance
	move.b	0x101(a1),d4
	lsl.w	#8,d4
	move.b	1(a1),d4
	move.l	d7,d3
	swap	d3
	sub.w	d3,d4                       | delta 1 = section - view_origin[1] (its sign cell: dead)
	move.w	d4,d3
	bpl.s	1f
	neg.w	d3
1:	lsr.w	#3,d3                       | the height, scaled
	cmp.w	d0,d3
	jbcc	wk_subdiv                   | at or past the far clip: drop, subdivide
	moveq	#0,d5
	move.w	d3,d5
	lsl.l	#8,d5
	divu.w	d0,d5                       | the TRUE ratio, 0..255 (height < distance)
	cmp.w	#0x80,d5
	jbcc	wk_subdiv                   | off the top of the scan-line space
	tst.w	d4
	bmi.s	1f
	add.b	#0x3C,d5                    | above eye level: quotient + 60
	bra.s	2f
1:	moveq	#0x3C,d3                    | below: 60 - quotient
	.if SABOTAGE == 5
	moveq	#0x3D,d3                    | SABOTAGE 5: below eye level one line off
	.endif
	sub.b	d5,d3
	move.b	d3,d5
2:	sub.b	Z_PITCH(a0),d5              | less the frame's smoothed pitch: the scan line
	move.b	d5,Z_LINE(a0)
	jbmi	wk_subdiv                   | behind the camera: subdivide

	| ---- $246A EMIT: the far kerb, the style, the horizon and any corner marker
	jsr	emit_width_core             | a0 = mem, d1 = section

	| ---- $246D past the subdivision floor, has the road swung off the view axis in this step?
	move.b	Z_COUNT42(a0),d2
	cmp.b	Z_NEARSECT(a0),d2
	bls.s	wk_step
	moveq	#0,d3
	move.b	Z_CURSOR(a0),d3
	move.b	0x50(a4,d3.w),d4            | edge_x_hi[here]
	bpl.s	1f
	not.b	d4                          | EOR #$FF: one less in magnitude, all a threshold needs
1:	cmp.b	A_OFFAXIS(sp),d4
	jbcc	wk_seam                     | at or past it: C finishes the seam (and a circuit's hook)

	| ---- $2490 keep the point, then step the section index
wk_step:
	move.b	d1,Z_PREVSECT(a0)
	addq.b	#1,Z_CURSOR(a0)
	move.b	Z_COUNT42(a0),d2
	addq.b	#1,d2
	move.b	d2,Z_COUNT42(a0)
	cmp.b	A_CAP(sp),d2
	bcc.s	wk_done                     | 18 points is the cap
	moveq	#0,d3
	move.b	d2,d3
	lea	STEP_TBL(a0),a1
	move.b	(a1,d3.w),d3                | this point's stride along the section list (never read back)
	move.b	d1,d4
	sub.b	Z_WRAP(a0),d4
	cmp.b	d3,d4
	jbcc	1f                          | (jbcc: sabotage 6 leaves nothing to branch over)
	.if SABOTAGE != 6                   | SABOTAGE 6: the list never wraps
	add.b	#0x78,d1                    | round the 120-byte list
	.endif
1:	sub.b	d3,d1
	jbra	wk_point

wk_done:
	moveq	#0,d0
	move.b	d1,d0
	movem.l	(sp)+,d2-d7/a2-a6
	rts
wk_subdiv:
	move.w	#0x100,d0
	move.b	d1,d0
	movem.l	(sp)+,d2-d7/a2-a6
	rts
wk_seam:
	move.w	#0x200,d0
	move.b	d1,d0
	movem.l	(sp)+,d2-d7/a2-a6
	rts
	.size	road_edge_walk_m68k, .-road_edge_walk_m68k
