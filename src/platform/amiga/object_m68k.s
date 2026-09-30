| object_m68k.s — THE OBJECT PLOTTER'S LINE SIDE IN 68000 REGISTERS: plot_view_src_line (Amiga only;
| `make OBJASM=0` is the C control)
|
| This is plot_view_src_line_core (src/gen/revs_native.c, twin #96) with its three shared tails and
| surface_colour_at inlined, statement for statement: ONE COLUMN OF ONE SHAPE EDGE into the view
| source.  The C is still the reference — the host runs it, and `make OBJCHECK=1` runs both on the
| target, call for call, on the same 64 KB (plus a fuzzer first) and compares every byte of mem[]
| and both plot-pointer words (amiga/obj_check.gdb).
|
| ⭐ WHY: single-stepped in the race (docs/open-work.md §2c), the draw is ~1950 instructions an
| object and this routine is 6.2 calls an object at 156 instructions a call, every hot line one
| byte RMW on a zero-page cell — the C's floor.  Here each zero-page value it re-reads lives in a
| register, the fills run on one pointer with a byte index and no range test (the block is
| $3000 + column * $80 with column < $28, always RAM), and the classifier costs no call.
| ⭐ WHAT IT STORES: every cell the C stores, with the same value, in the same order relative to
| the two C callees it makes — so mem[] after every call is what the C leaves and no determinism
| record changes.  The callees stay C: fill_object_gap_core (rare) and the gap walk
| (pvs_asm_gap_walk, which is column_gap_walk_core between the EDGE_COLUMN bump and its undo).
| ⚠ Read LIVE, as the C does: surface_colours ($38FC) sits in a source block's tail, so the
|   classifier reads it per cell rather than once.
|
| void pvs_line_m68k(unsigned mode, unsigned colourSelect)
| Registers: a5 = mem (every cell and table is below $8000, so d16(a5) reaches all of them)
|   a4 = mem + the column's block    d7 = mode    d6 = the column    d5 = blockStart
|   d4 = the composed byte (acc)     d3 = shared_temp_76, then the keep mask    d2 = pixel / blank

	.equ	Z_HORIZ,    0x001F      | horizon_extent
	.equ	Z_LA1LIM,   0x0029      | line_attr_1_limit
	.equ	Z_LA0LIM,   0x002C      | line_attr_0_limit
	.equ	Z_PLOTX,    0x0035      | plot_x
	.equ	Z_TOP,      0x0047      | span_top_line
	.equ	Z_DEFER,    0x0048      | span_defer_pending
	.equ	Z_PTRLO,    0x0070      | plot_ptr
	.equ	Z_PTRHI,    0x0071
	.equ	Z_PTR2LO,   0x0072      | plot_ptr2
	.equ	Z_PTR2HI,   0x0073
	.equ	Z_HALF,     0x0074      | PVS_HALF      (math_lo)
	.equ	Z_76,       0x0076      | shared_temp_76 — the edge colour
	.equ	Z_COLP,     0x0078      | PVS_COLOUR_P  (hypot_min_lo)
	.equ	Z_COL,      0x0079      | PVS_COLOUR    (hypot_min_hi)
	.equ	Z_BYTE,     0x007A      | PVS_BYTE      (hypot_max_lo)
	.equ	Z_MODE,     0x007B      | PVS_MODE      (hypot_max_hi)
	.equ	Z_PREVCOL,  0x007C      | PVS_PREV_COL  (point_dist_lo)
	.equ	Z_KEEP,     0x007D      | PVS_KEEP      (point_dist_hi)
	.equ	Z_7E,       0x007E      | shared_temp_7e — this endpoint's x
	.equ	Z_CURSOR,   0x007F      | span_line_cursor
	.equ	Z_EBLK,     0x0082      | EDGE_BLOCK_START (point_delta_lo[2])
	.equ	Z_EDGEX,    0x0083      | OBJ_EDGE_X     (point_delta_hi[0])
	.equ	Z_ESTYLE,   0x0084      | OBJ_EDGE_STYLE (shared_temp_84)
	.equ	Z_ECOL,     0x0085      | EDGE_COLUMN    (point_delta_hi[2])
	.equ	Z_8C,       0x008C      | shared_temp_8c — the deferred keep mask
	.equ	Z_OTHCOL,   0x008D      | PVS_OTHER_COL  (projected_line)
	.equ	Z_OTHX,     0x008F      | PVS_OTHER_X    (plot_ptr3_hi)
	.equ	GAP_BRANCH, 0x1DD5      | column_gap_walk's patched branch operand ($09 skip / $EF map)
	.equ	GAP_FALLBK, 0x1DDC      | its fallback colour operand
	.equ	GAP_PTR,    0x1DDE      | its store pointer's zero-page number
	.equ	LA0,        0x0400      | line_attr_0
	.equ	LA1,        0x0450      | line_attr_1
	.equ	SE0,        0x0554      | surface_edge_0..3
	.equ	SE1,        0x05A4
	.equ	SE2,        0x0600
	.equ	SE3,        0x0650
	.equ	BLOCKS,     0x3000      | the forty $80-spaced source blocks
	.equ	CPAND,      0x337C      | colour_pattern_and_tbl
	.equ	CPKEEP,     0x33FC      | colour_pattern_keep_tbl
	.equ	SC,         0x38FC      | surface_colours
	.equ	DBS,        0x3900      | dash_block_starts
	.equ	PAM,        0x39D0      | pixel_after_mask_tbl
	.equ	PKO,        0x3FE8      | pixel_keep_others_tbl
	.equ	ESPREV,     0x5EDF      | edge_style - 1 (a line_attr entry is an index PLUS ONE)
	.equ	VLS,        0x5F60      | view_line_surface
	.equ	CPT,        0x628F      | colour_pattern_tbl
	.equ	ARGS,       44          | ten saved registers + the return address

| halve_signed_rounded: an arithmetic halving rounded toward zero, (int8_t)r / 2
	.macro	HALVE r
	tst.b	\r
	bpl.s	1f
	addq.b	#1,\r
1:	asr.b	#1,\r
	.endm

| surface_colour_at (twin #40): line in d0 (a clean word), position in d6 -> the colour in d1.
| Clobbers a0/a1.  surface_colours is read LIVE (it sits in a source block's tail).
	.macro	CLASSIFY
	cmp.b	Z_HORIZ(a5),d0
	bhi.s	cl_sky\@                    | above the horizon: sky
	lea	(a5,d0.w),a0
	cmp.b	SE0(a0),d6
	bcc.s	cl_c3\@                     | past the outermost boundary
	cmp.b	SE2(a0),d6
	bcs.s	cl_n2\@
	cmp.b	Z_LA1LIM(a5),d0
	bcc.s	cl_c3\@
	moveq	#0x7F,d1
	and.b	LA1(a0),d1
	bra.s	cl_attr\@
cl_n2\@:
	cmp.b	SE3(a0),d6
	bcc.s	cl_c0\@
	cmp.b	SE1(a0),d6
	bcs.s	cl_in\@
	cmp.b	Z_LA0LIM(a5),d0
	bcc.s	cl_c3\@
	moveq	#0x7F,d1
	and.b	LA0(a0),d1
cl_attr\@:
	lea	ESPREV(a5),a1
	move.b	(a1,d1.w),d1                | the edge point's style...
	.if SABOTAGE == 2
	and.w	#7,d1                       | SABOTAGE 2: the colour index takes a bit too many
	.else
	and.w	#3,d1                       | ...whose low bits are the colour
	.endif
	lea	SC(a5),a1
	move.b	(a1,d1.w),d1
	bra.s	cl_done\@
cl_in\@:
	moveq	#3,d1
	and.b	VLS(a0),d1                  | inside everything: the line's own background class
	lea	SC(a5),a1
	move.b	(a1,d1.w),d1
	bra.s	cl_done\@
cl_sky\@:
	move.b	SC+1(a5),d1
	bra.s	cl_done\@
cl_c3\@:
	move.b	SC+3(a5),d1
	bra.s	cl_done\@
cl_c0\@:
	move.b	SC(a5),d1
cl_done\@:
	.endm

	.section .text.pvs_line_m68k,"ax",@progbits
	.even
	.globl	pvs_line_m68k
	.type	pvs_line_m68k, @function
pvs_line_m68k:
	movem.l	d2-d7/a2-a5,-(sp)
	lea	mem,a5
	moveq	#0,d7
	move.b	ARGS+3(sp),d7               | mode
	moveq	#3,d0
	and.b	ARGS+7(sp),d0               | colourSelect & 3

	| ---- $1C1C-$1C3D: the entry state
	move.b	d7,Z_MODE(a5)
	move.b	Z_COL(a5),Z_COLP(a5)
	lea	CPT(a5),a0
	move.b	(a0,d0.w),Z_COL(a5)
	move.b	Z_ECOL(a5),Z_PREVCOL(a5)
	moveq	#0x0C,d0
	and.b	Z_ESTYLE(a5),d0
	lsr.b	#2,d0
	move.b	(a0,d0.w),Z_76(a5)
	clr.b	Z_PTRLO(a5)

	| ---- $1C3E-$1C7A: the endpoints
	cmp.b	#1,d7
	bne.s	pl_notone
	move.b	Z_7E(a5),d0
	HALVE	d0
	add.b	Z_PLOTX(a5),d0
	move.b	d0,Z_7E(a5)
	lsr.b	#2,d0
	move.b	d0,Z_ECOL(a5)
	bra.s	pl_other
pl_notone:
	move.b	Z_OTHCOL(a5),Z_ECOL(a5)
	move.b	Z_OTHX(a5),Z_7E(a5)
	tst.b	d7
	bne.s	pl_endpts
pl_other:
	move.b	Z_EDGEX(a5),d0
	HALVE	d0
	add.b	Z_PLOTX(a5),d0
	.if SABOTAGE == 1
	addq.b	#1,d0                       | SABOTAGE 1: the other endpoint one pixel right
	.endif
	move.b	d0,Z_OTHX(a5)
	lsr.b	#2,d0
	move.b	d0,Z_OTHCOL(a5)
pl_endpts:

	| ---- $1C7B-$1C88: the pointer, $3000 + column * $80, and the screen-half bit
	moveq	#0,d6
	move.b	Z_ECOL(a5),d6
	move.b	Z_HALF(a5),d0
	add.b	d0,d0
	cmp.b	#0x14,d6
	bcs.s	1f
	addq.b	#1,d0
1:	move.b	d0,Z_HALF(a5)
	moveq	#0,d0
	move.w	d6,d0
	lsl.w	#7,d0
	add.w	#BLOCKS,d0
	move.w	d0,plot_ptr_v
	move.b	d0,Z_PTRLO(a5)
	move.w	d0,d1
	lsr.w	#8,d1
	move.b	d1,Z_PTRHI(a5)

	| ---- $1C89-$1C9D: off the right of the viewport
	cmp.b	#0x28,d6
	bcs.s	pl_onview
	cmp.b	#1,d7
	jbeq	pl_ret
	cmp.b	#0x28,Z_PREVCOL(a5)
	jbcc	pl_ret
	move.b	#0x28,Z_ECOL(a5)
	jbra	pl_closegap
pl_onview:
	lea	(a5,d0.l),a4                | the column's block
	moveq	#0,d5
	move.b	Z_TOP(a5),d5                | blockStart = max(span_top_line, dash_block_starts[col])
	lea	DBS(a5),a0
	move.b	(a0,d6.w),d0
	cmp.b	d0,d5
	bcc.s	1f
	move.b	d0,d5
1:	move.b	d5,Z_EBLK(a5)

	| ---- $1C9E-$1CA9: a run with no height
	cmp.b	Z_CURSOR(a5),d5
	bcs.s	pl_height
	cmp.b	#1,d7
	jbeq	pl_ret
	jbra	pl_prevcol
pl_height:

	| ---- $1CAA-$1CC2: style bit 4 re-picks the colour on a closing pass off the screen half
	move.b	Z_ESTYLE(a5),d0
	btst	#4,d0
	beq.s	pl_pix
	tst.b	d7
	beq.s	pl_pix
	move.b	Z_HALF(a5),d1
	eor.b	d7,d1
	btst	#0,d1
	beq.s	pl_pix
	moveq	#3,d1
	and.b	d0,d1
	lea	CPT(a5),a0
	move.b	(a0,d1.w),Z_76(a5)
pl_pix:

	| ---- $1CC3-$1CD0: the pixel is the endpoint's low two bits; cut the edge colour to it
	moveq	#3,d2
	and.b	Z_7E(a5),d2
	lea	PKO(a5),a0
	move.b	(a0,d2.w),d1                | d1 = pixel_keep_others_tbl[pixel]
	move.b	d1,d0
	not.b	d0
	move.b	Z_76(a5),d3
	and.b	d0,d3
	move.b	d3,Z_76(a5)

	| ---- $1CD1-$1D43: compose the cell, per mode
	cmp.b	#1,d7
	jbeq	pl_mode1
	jbhi	pl_mode2

	| mode 0: the closing arm's own pass
	lea	CPAND(a5),a0
	move.b	(a0,d2.w),d0
	and.b	Z_COLP(a5),d0               | half0
	move.b	d0,Z_HALF(a5)
	lea	CPKEEP(a5),a0
	move.b	(a0,d2.w),d4
	and.b	Z_COL(a5),d4
	or.b	d0,d4
	and.b	d1,d4
	or.b	d3,d4                       | acc
	move.b	d4,Z_BYTE(a5)
	tst.b	Z_DEFER(a5)
	beq.s	pl_m0plain
	move.b	Z_8C(a5),d0                 | $1CEE — merge mode 1's deferred byte
	clr.b	Z_DEFER(a5)
	move.b	d0,Z_KEEP(a5)
	not.b	d0
	and.b	d0,d4
	jbra	pl_samecol
pl_m0plain:
	cmp.b	Z_OTHCOL(a5),d6             | $1CFD — both ends in one column: hand the byte on
	bne.s	1f
	move.b	d4,Z_COL(a5)
	jbra	pl_prevcol
1:	tst.b	d4                          | $1D0A
	bne.s	2f
	moveq	#0x55,d4
2:	moveq	#0,d0                       | $1DE5 — the PLAIN fill, cursor down to above blockStart
	move.b	Z_CURSOR(a5),d0
3:	move.b	d4,(a4,d0.w)
	subq.b	#1,d0
	cmp.b	d5,d0
	bne.s	3b
	jbra	pl_prevcol

pl_mode1:                               | open the span
	lea	CPAND(a5),a0
	move.b	(a0,d2.w),d0                | $1D17
	move.b	d0,Z_KEEP(a5)
	not.b	d0
	and.b	Z_COL(a5),d0
	and.b	d1,d0
	or.b	d3,d0
	move.b	d0,d4
	bra.s	pl_samecol

pl_mode2:                               | close the span, merging mode 1's deferred mask
	lea	PAM(a5),a0
	move.b	(a0,d2.w),d0
	or.b	Z_8C(a5),d0                 | $1D34
	move.b	d0,Z_KEEP(a5)
	not.b	d0
	and.b	Z_COLP(a5),d0
	and.b	d1,d0
	or.b	d3,d0
	move.b	d0,d4
	bra.s	pl_rmw

	| $1D25-$1D33 — both endpoints in one column: defer the byte and paint nothing
pl_samecol:
	cmp.b	Z_OTHCOL(a5),d6
	bne.s	pl_rmw
	move.b	Z_KEEP(a5),d0
	move.b	d4,Z_COL(a5)
	move.b	d0,Z_8C(a5)
	move.b	Z_DEFER(a5),d0
	lsr.b	#1,d0
	or.b	#0x80,d0
	move.b	d0,Z_DEFER(a5)
	jbra	pl_ret

	| ---- $1D44-$1D6E: the READ-MODIFY-WRITE fill, which consults the road
pl_rmw:
	move.b	d4,Z_BYTE(a5)
	clr.b	Z_8C(a5)
	move.b	Z_KEEP(a5),d3               | keep
	move.b	d4,d2                       | $1D57 — the blank cell's fill
	bne.s	1f
	moveq	#0x55,d2
1:	moveq	#0,d0
	move.b	Z_CURSOR(a5),d0
pl_rloop:
	move.b	(a4,d0.w),d1
	cmp.b	#0x55,d1
	beq.s	pl_rblank
	tst.b	d1
	beq.s	pl_classify                 | out of line: keeps the loop's branches short
pl_merge:                               | $1D60 — keep the cell's other pixels, add the byte
	and.b	d3,d1
	or.b	d4,d1
	jbne	4f                         | (jbne: sabotage 3 leaves nothing to branch over)
	.if SABOTAGE != 3                   | SABOTAGE 3: a composed 0 stored as 0, not $55
	moveq	#0x55,d1
	.endif
4:	move.b	d1,(a4,d0.w)
pl_rnext:
	subq.b	#1,d0
	cmp.b	d5,d0
	bne.s	pl_rloop
	jbra	pl_rdone
pl_rblank:
	move.b	d2,(a4,d0.w)                | a $55 cell takes the byte
	bra.s	pl_rnext

	| $1D5D — an untouched cell first takes the surface's colour (surface_colour_at, twin #40)
pl_classify:
	CLASSIFY
	jbra	pl_merge
pl_rdone:

	| ---- $1D6F-$1D7B: every mode but 1 also closes the column's own gaps
	cmp.b	#1,d7
	jbeq	pl_ret
	| column_gap_walk (twin #42's walk) on the NEXT column, from span_line_cursor down to
	| blockStart.  ⭐ The object path meets it in the configuration fill_dash_edge_columns' pass A
	| leaves — branch $09 (a non-zero cell is skipped) and the store through plot_ptr itself (an
	| empty cell takes the surface's colour, or the fallback, in place) — so that shape runs here,
	| and any other patch state goes to the C walk.  Both are exact, so the choice cannot show.
	cmp.b	#0x09,GAP_BRANCH(a5)
	jbne	pl_gapc
	cmp.b	#Z_PTRLO,GAP_PTR(a5)
	jbne	pl_gapc
	addq.b	#1,d6                       | EDGE_COLUMN + 1 (the C bumps the cell and restores it)
	cmp.b	#0x28,d6
	jbcc	pl_prevcol                  | $1DAF — no source column there: nothing is written
	moveq	#0,d0
	move.w	d6,d0
	lsl.w	#7,d0
	add.w	#BLOCKS,d0                  | $1DB5 — plot_ptr = the next column's block
	move.w	d0,plot_ptr_v
	move.b	d0,Z_PTRLO(a5)
	lea	(a5,d0.l),a4
	lsr.w	#8,d0
	move.b	d0,Z_PTRHI(a5)
	move.b	Z_PTR2HI(a5),d1             | plot_ptr2 is marshalled IN, as the C walk does
	lsl.w	#8,d1
	move.b	Z_PTR2LO(a5),d1
	move.w	d1,plot_ptr2_v
	move.b	GAP_FALLBK(a5),d2
	moveq	#0,d0
	move.b	Z_CURSOR(a5),d0
pl_gloop:
	cmp.b	d5,d0
	beq.s	pl_gdone
	tst.b	(a4,d0.w)
	beq.s	pl_gempty                   | $1DD4 BNE +$09 — a painted cell is skipped
pl_gnext:
	subq.b	#1,d0
	bra.s	pl_gloop
pl_gdone:
	jbra	pl_prevcol
pl_gempty:                              | $1DD6 — out of line: keeps the skip loop's branches short
	CLASSIFY
	tst.b	d1
	.if SABOTAGE != 5                   | SABOTAGE 5: a colourless cell stored as 0, not the fallback
	bne.s	1f
	move.b	d2,d1
	.endif
1:	move.b	d1,(a4,d0.w)
	jbra	pl_gnext
pl_gapc:
	move.l	d5,-(sp)
	move.l	d7,-(sp)
	jsr	pvs_asm_gap_walk
	addq.l	#8,sp

	| $1D7C-$1D85 — a previous column off the viewport means "no gap at all"
pl_prevcol:
	cmp.b	#0x28,Z_PREVCOL(a5)
	bcs.s	pl_closegap
	st	Z_PREVCOL(a5)
	| $1D86-$1D93 — CLC/SBC: the gap is column - previous - 1; zero or negative, no fill
pl_closegap:
	move.b	Z_ECOL(a5),d0
	sub.b	Z_PREVCOL(a5),d0
	.if SABOTAGE != 4                   | SABOTAGE 4: the gap one column wider (SEC, not CLC)
	subq.b	#1,d0
	.endif
	beq.s	pl_ret
	bmi.s	pl_ret
	and.l	#0xFF,d0
	move.l	d0,-(sp)
	jsr	fill_object_gap_core
	addq.l	#4,sp
pl_ret:
	movem.l	(sp)+,d2-d7/a2-a5
	rts
	.size	pvs_line_m68k, .-pvs_line_m68k
