| span_pass_m68k.s — ONE SPAN PASS IN 68000 REGISTERS: draw_surface_spans' loop, interp_edge's setup
| and the span walk as one routine (Amiga only; `make SETUPASM=0` is the C control)
|
| This is draw_surface_spans_core's loop and interp_edge_core (src/gen/revs_native.c), instruction for
| instruction in effect, for a pass whose destination draw_surface_spans_core has checked (the patched
| operands in $0300..$0800 — every pass the game makes).  The C is still the reference: the host runs
| it, and `make SETUPCHECK=1` runs both on the target, pass for pass, and compares every byte of
| mem[] (tools: amiga/setupcheck.gdb).
|
| ⭐ WHY IT IS ONE ROUTINE: priced by single-stepping, a real span cost ~450 instructions of which the
| walk was 79; the rest was the setup's C (~246), the bridge that loaded the walk's registers and
| unpacked its results (~59), the per-span entry and write-back (~52) and the entry decode (~23).  Here
| the setup computes each walk input IN the register the walk takes it in, falls into the walk's jump
| table (span_walk_enter), and the loop around it costs no C call per span.
| ⭐ WHAT IT STORES: every cell interp_edge_core stores that anything outside the pass READS, with the
| same value, in the same span, before the cap — the cap can run circuit code, and the next span reads
| the cross-span cells.  `make determinism*` and `validate` run the C, which still stores everything,
| so they are untouched.  What it does NOT write:
|   - C-private scratch: g_spanStepIn / g_spanStepOut / g_spanMarkOn, which every C walk path sets
|     before it reads them;
|   - ⭐ TWELVE 6502 WORKING CELLS NOTHING OUTSIDE THE PASS READS (THE RESULTS RULE,
|     docs/validation-harness.md): span_saved_index $1B, saved_slot_index $45, the three plot
|     pointers' page bytes $71/$73/$8F, math_lo/math_hi $74/$75, SPAN_DX/DY/BLOCK $83-$85,
|     SPAN_YSTEP $87 and bearing_hi $8B.  Here each lives in a register.  READER AUDIT:
|     `make rangeaudit DEFUSE=1` over those cells on all five circuits (2026-09-26) — every read
|     of a value interp_edge / draw_surface_spans / the span plotters left is made by those same
|     routines (all inside this one routine here) or by interp_edge's own neg16_math_noinit; no
|     other routine and no circuit hook reads one.  The hooks that name these cells (math_lo/hi,
|     saved_slot_index, span_saved_index in revs_track_hooks.c) write each before reading it.
|     On the port side the cap's C reads span_swapped and $33/$34, which are still stored.
|     `make SETUPCHECK=1` masks exactly these twelve (span_pass_dead_cells in revs_native.c).
| ⚠ Read LIVE, as the C does: colour_pattern_keep_tbl ($33FC) and every walk input the walk itself
|   reads live (span_walk_m68k.s).  $33FC sits in a page a walk can write.
|
| C is called for two things only: the cap (span_cap_line — it can run a circuit's hook) and an entry
| offset the chain cannot mean (platform_smc_unhandled — never, in the game).
|
| void span_pass_m68k(unsigned far, unsigned near, unsigned style0, uint8_t *dest)
|   far/near: the pass's first endpoint indices (span_index_far / span_index_near, already stored)
|   style0:   the first call's style (row_base_lo[pass]); dest: mem + the patched destination
| Register plan inside a span (a5/a6 are the walk's own and never change):
|   a5 = mem + $628F — the pattern table, and through d16 every cell in $0000..$E28E
|   a6 = mem + $337C — colour_pattern_and_tbl (the walk's), and keep_tbl at +$80
|   a4 = mem + $5EC0 — the four edge tables through d8(a4,Xn), until the walk takes a4 as dest
| The frame: 0(sp) x (far), 1(sp) y (near), 2(sp) $80 = a descending arm, 3(sp) its entry step,
|   4(sp) mem + dest.

	.equ	BASE,       0x628F
	.equ	Z_SAVEDIDX, 0x001B-BASE     | span_saved_index — the near index
	.equ	Z_SWAPPED,  0x001E-BASE     | span_swapped
	.equ	Z_PASS,     0x0027-BASE     | surface_pass_index
	.equ	Z_STYLEBASE,0x0032-BASE     | surface_style_base
	.equ	Z_CAPFILL,  0x0033-BASE     | span_cap_surface_fill
	.equ	Z_CAPOVER,  0x0034-BASE     | span_cap_surface_over
	.equ	Z_SLOTIDX,  0x0045-BASE     | saved_slot_index — the far index
	.equ	Z_ENDIDX,   0x004B-BASE     | span_end_index
	.equ	Z_NEARIDX,  0x004C-BASE     | span_index_near
	.equ	Z_FARIDX,   0x004F-BASE     | span_index_far
	.equ	Z_SPLIT,    0x0050-BASE     | road_split_index
	.equ	Z_CAPPEND,  0x0053-BASE     | span_cap_pending
	.equ	Z_STYLEIDX, 0x0054-BASE     | surface_style_index
	.equ	Z_PTR1LO,   0x0070-BASE     | plot_ptr
	.equ	Z_PTR1HI,   0x0071-BASE
	.equ	Z_PTR2LO,   0x0072-BASE     | plot_ptr2
	.equ	Z_PTR2HI,   0x0073-BASE
	.equ	Z_MATHLO,   0x0074-BASE
	.equ	Z_MATHHI,   0x0075-BASE
	.equ	Z_X77,      0x0077-BASE     | shared_temp_77 — this span's endpoint x
	.equ	Z_X7E,      0x007E-BASE     | shared_temp_7e — the previous span's
	.equ	Z_CURSOR,   0x007F-BASE     | span_line_cursor
	.equ	Z_LINEEND,  0x0082-BASE     | SPAN_LINE_END
	.equ	Z_DX,       0x0083-BASE     | SPAN_DX
	.equ	Z_DY,       0x0084-BASE     | SPAN_DY
	.equ	Z_BLOCK,    0x0085-BASE     | SPAN_BLOCK
	.equ	Z_ARM,      0x0086-BASE     | SPAN_ARM
	.equ	Z_YSTEP,    0x0087-BASE     | SPAN_YSTEP
	.equ	Z_CLIP,     0x0088-BASE     | SPAN_CLIP
	.equ	Z_BEARHI,   0x008B-BASE     | bearing_hi
	.equ	Z_8C,       0x008C-BASE     | shared_temp_8c — the near spans' shared style
	.equ	Z_PTR3LO,   0x008E-BASE     | plot_ptr3
	.equ	Z_PTR3HI,   0x008F-BASE
	.equ	OR_OFF,     0x629C-BASE     | colour_pattern_or_tbl
	.equ	STYLE_TBL,  0x5FD0-BASE     | surface_style_tbl
	.equ	P_KEEP,     0x33FC-0x337C   | colour_pattern_keep_tbl, from a6
	.equ	EDGE,       0x5EC0          | a4's base: every edge table inside d8
	.equ	E_XLO,      0x5E40-EDGE     | edge_x_lo
	.equ	E_XHI,      0x5E90-EDGE     | edge_x_hi
	.equ	E_STYLE,    0x5EE0-EDGE     | edge_style
	.equ	E_Y,        0x5F20-EDGE     | edge_y

	.equ	F_X,    0                   | the frame
	.equ	F_Y,    1
	.equ	F_REV,  2
	.equ	F_STEP, 3
	.equ	F_DEST, 4
	.equ	FRAME,  8
	.equ	ARGS,   FRAME+44+4          | past the frame, the eleven saved registers and the return

| `--defsym SABOTAGE=N` (`make SETUPCHECK=1 SETUPASM_SABOTAGE=N`): a deliberate defect SETUPCHECK must
| catch.  0 in every real build.
	.ifndef SABOTAGE
	.equ	SABOTAGE, 0
	.endif

| One arm's entry: read the phase's offset out of the arm's table, write it over the chain's branch
| operand (a real mem[] store — the C keeps it), decode it to column * 2 + forced, and count the walk's
| lines, n - 1, into d5's upper word: descending n = page - $2F, ascending $44 - page.
| a0 = the phase, d4 = the block.
.macro ARMENTRY tbl, oper, dec, rev
	lea	\tbl-BASE(a5),a4
	moveq	#0,d0
	move.b	(a4,a0.w),d0            | the entry offset for this sub-column phase
	move.b	d0,\oper-\tbl(a4)       | ...over the chain's branch operand
	lea	\dec(pc),a4
	move.b	(a4,d0.w),d0            | -> column * 2 + forced, or $FF: an offset the chain cannot mean
	jbmi	pass_trap
	add.w	d0,d7
	swap	d5
	move.w	d4,d5
	lsr.w	#1,d5                   | block >> 1 = page - $30
	.if \rev == 0
	neg.w	d5
	add.w	#0x13,d5                | $43 - page
	.endif
	swap	d5
.endm

	.section .text.span_pass_m68k,"ax",@progbits
	.even
	.globl	span_pass_m68k
	.type	span_pass_m68k, @function
span_pass_m68k:
	movem.l	d2-d7/a2-a6,-(sp)
	subq.l	#FRAME,sp
	lea	mem+BASE,a5
	lea	mem+0x337C,a6
	lea	EDGE-BASE(a5),a4
	move.b	ARGS+3(sp),F_X(sp)
	move.b	ARGS+7(sp),F_Y(sp)
	move.l	ARGS+12(sp),F_DEST(sp)
	moveq	#0,d3
	move.b	ARGS+11(sp),d3          | the first call's style
	moveq	#1,d4                   | ...and it only publishes the endpoint
	jbra	pass_span

| ----- draw_surface_spans' loop: advance both indices, skip marked points, pick the style -----
pass_next:
	addq.b	#1,F_X(sp)              | $19D7 INX
	addq.b	#1,F_Y(sp)              | $19D8 INY
	moveq	#0,d7
	move.b	F_Y(sp),d7
	cmp.b	Z_ENDIDX(a5),d7         | $19D9 — the walk stops at this half's end index
	jbcc	pass_done
	lea	EDGE-BASE(a5),a4        | (a4 was the walk's destination)
	move.b	E_STYLE(a4,d7.w),d0     | $19DD — fill_line_attr marked it: skip
	jbmi	pass_next
	moveq	#0,d4                   | publishOnly = 0 unless an arm says otherwise
	moveq	#0,d3
	move.b	Z_NEARIDX(a5),d0        | the previous span's near index against the split
	cmp.b	Z_SPLIT(a5),d0
	jbcs	pass_near               | nearer than the split: the shared style
	jbne	pass_beyond
	move.b	E_STYLE-1(a4,d7.w),d0   | exactly at the split
	andi.b	#3,d0
	jbne	pass_class
	move.b	d7,Z_SPLIT(a5)          | $19F1 — the split moves to here
	tst.b	Z_PASS(a5)
	jbne	pass_near
	moveq	#1,d4                   | pass 0 publishes style 0
	jbra	pass_span
pass_beyond:
	move.b	E_STYLE-1(a4,d7.w),d0
	andi.b	#3,d0
	jbne	pass_class
	move.b	Z_PASS(a5),d0
	cmp.b	#1,d0
	jbeq	1f
	cmp.b	#2,d0
	jbne	2f
1:	move.b	d0,d3                   | passes 1 and 2 publish the pass number itself
	moveq	#1,d4
	jbra	pass_span
2:	move.b	Z_STYLEBASE(a5),d3      | passes 0 and 3: the base, drawn
	jbra	pass_span
pass_class:
	add.b	d0,d0
	add.b	d0,d0
	move.b	Z_STYLEBASE(a5),d3
	add.b	d0,d3                   | 4 * class + base...
	scs	d4                      | ...and a carry out of it only publishes
	jbra	pass_span
pass_near:
	move.b	Z_8C(a5),d3

| ----- interp_edge: d3.w = style, d4.b = publishOnly, a4 = the edge tables -----
pass_span:
	moveq	#0,d6
	move.b	F_X(sp),d6              | the far point
	moveq	#0,d7
	move.b	F_Y(sp),d7              | the near point
	move.b	d3,Z_STYLEIDX(a5)
	moveq	#0,d5                   | swapped
	move.b	d5,Z_SWAPPED(a5)

	| 1 — the clip bit, rotated into span_clip's top
	move.b	E_Y(a4,d7.w),d1         | the near point's scan line: lineEnd
	move.b	d1,d0
	subq.b	#1,d0
	cmp.b	#0x4E,d0
	jbcc	1f                      | off the bottom of the view
	move.b	E_XHI(a4,d6.w),d0
	jbpl	2f
	not.b	d0                      | |angle|, near enough for a clip test
2:	cmp.b	#0x14,d0
	jbcc	1f                      | more than $14 off axis
	move.b	Z_CLIP(a5),d2
	lsr.b	#1,d2
	jbra	3f
1:	move.b	Z_CLIP(a5),d2
	.if SABOTAGE != 6
	lsr.b	#1,d2                   | SABOTAGE 6: a clipped point does not shift the history
	.endif
	ori.b	#0x80,d2
3:	move.b	d2,Z_CLIP(a5)

	| 2 — the endpoint, a 10-bit x biased by $80
	move.b	E_XHI(a4,d6.w),d0
	lsl.w	#8,d0
	move.b	E_XLO(a4,d6.w),d0
	movea.w	d0,a0                   | farX (only its low word is ever used)
	lsl.w	#2,d0
	lsr.w	#8,d0                   | bits 6-13 of farX...
	.if SABOTAGE == 1
	add.b	#0x81,d0                | SABOTAGE 1: the endpoint's bias off by one
	.else
	add.b	#0x80,d0                | ...plus $80
	.endif
	move.b	d0,d3                   | x77
	move.b	d3,Z_X77(a5)
	move.b	d1,Z_LINEEND(a5)
	tst.b	d4
	jbne	pass_publish

	| both ends usable?  bit 6 = the previous point was clipped, bit 7 this one
	move.b	Z_X7E(a5),d4            | x7e — the previous span's endpoint
	move.b	Z_CURSOR(a5),d6         | cursor — ...and its scan line
	btst	#6,d2
	jbeq	4f
	tst.b	d2
	jbmi	pass_publish            | neither end is usable
	exg	d3,d4                   | walk from the previous endpoint to this one instead
	.if SABOTAGE != 2
	exg	d1,d6                   | SABOTAGE 2: the scan lines are not swapped with the ends
	.endif
	move.b	d4,Z_X7E(a5)
	move.b	d6,Z_CURSOR(a5)
	move.b	d3,Z_X77(a5)
	move.b	d1,Z_LINEEND(a5)
	moveq	#-1,d5
	move.b	d5,Z_SWAPPED(a5)
4:
	| 3 — the deltas.  span_dy = |lineEnd - cursor|
	move.b	d1,d7
	sub.b	d6,d7                   | lineDelta
	move.b	d7,d6
	jbpl	5f
	neg.b	d6                      | dy ($80 stays $80, as the 6502's does)
5:	move.b	d2,d1                   | clip, from here on
	andi.b	#0xC0,d2
	jbeq	pass_oneclip
	| one of the two ends clipped: dx is the angle difference, normalised left
	moveq	#0,d0
	move.b	Z_FARIDX(a5),d0         | span_index_far — the PREVIOUS far index
	move.b	E_XHI(a4,d0.w),d2
	lsl.w	#8,d2
	move.b	E_XLO(a4,d0.w),d2       | its x
	move.w	a0,d0
	sub.w	d2,d0                   | dxRaw = farX - that
	move.w	d0,d4
	lsr.w	#8,d4                   | the PRE-abs high byte selects the arm...
	.if SABOTAGE != 7
	eor.b	d5,d4                   | ...crossed with the swap (SABOTAGE 7: not)
	.endif
	tst.w	d0
	jbpl	6f
	neg.w	d0                      | |dxRaw|
6:	cmp.w	#0x4000,d0
	jbcc	7f                      | top byte already >= $40: give back two
	add.w	d0,d0
	cmp.w	#0x4000,d0
	jbcs	8f
	.if SABOTAGE == 3
	lsr.b	#2,d6                   | SABOTAGE 3: give back two where it is one
	.else
	lsr.b	#1,d6                   | give back one
	.endif
	jbra	9f
8:	add.w	d0,d0
	jbpl	9f                      | give back none
7:	lsr.b	#2,d6
9:	lsr.w	#8,d0
	move.b	d0,d2                   | dx
	jbra	pass_deltas
pass_oneclip:
	| neither end clipped: dx is how far x moved, and the borrow becomes the arm's top bit
	move.b	d4,d2
	sub.b	d3,d2                   | x7e - x77
	jbcs	10f
	move.b	Z_ARM(a5),d4
	lsr.b	#1,d4
	ori.b	#0x80,d4
	jbra	pass_deltas
10:	neg.b	d2
	move.b	Z_ARM(a5),d4
	lsr.b	#1,d4
pass_deltas:
	| d1 clip, d2 dx, d4 arm, d5 swapped, d6 dy, d7 lineDelta
	move.b	d6,d3                   | dy
	move.b	d4,Z_ARM(a5)
	move.b	d2,d0
	or.b	d3,d0
	jbeq	pass_publish            | no extent
	moveq	#0,d0                   | does the abandon path stamp a surface code?
	andi.b	#0xC0,d1
	jbeq	11f
	move.b	d4,d0
	andi.b	#0x80,d0
11:	move.b	d0,Z_CAPPEND(a5)
	tst.b	d7                      | a zero line delta borrows its direction from the swap
	jbne	12f
	move.b	d5,d7
	not.b	d7
12:
	| 4 — the style record becomes the four column patterns (d2 dx, d3 dy, d4 arm, d7 ystep)
	moveq	#0,d0
	move.b	Z_STYLEIDX(a5),d0
	lea	STYLE_TBL(a5),a1
	moveq	#0,d6
	move.b	(a1,d0.w),d6            | s0
	move.b	d6,(a5)
	move.b	d6,d1
	and.b	P_KEEP(a6),d1
	move.b	d1,OR_OFF(a5)
	addq.b	#1,d0                   | the record index wraps as a byte, as the C's does
	move.b	(a1,d0.w),d1
	move.b	d1,1(a5)
	and.b	P_KEEP+1(a6),d1
	move.b	d1,OR_OFF+1(a5)
	addq.b	#1,d0
	move.b	(a1,d0.w),d1
	move.b	d1,2(a5)
	and.b	P_KEEP+2(a6),d1
	move.b	d1,OR_OFF+2(a5)
	addq.b	#1,d0
	move.b	(a1,d0.w),d5            | s3
	move.b	d5,3(a5)
	move.b	d5,d1
	and.b	P_KEEP+3(a6),d1
	move.b	d1,OR_OFF+3(a5)
	| the two surface classes a cap can stamp
	move.b	Z_PASS(a5),d1
	lsl.b	#3,d1                   | the pass, in bits 3-5
	move.b	d6,d0
	lsr.b	#3,d0
	andi.b	#3,d0
	or.b	d1,d0
	ori.b	#0x40,d0
	move.b	d0,Z_CAPOVER(a5)        | byte 0's colour
	tst.b	d6
	jbne	13f
	.if SABOTAGE != 8
	moveq	#0x55,d6                | an all-zero pattern is substituted (SABOTAGE 8: not)
	move.b	d6,(a5)
	.endif
13:	                                | d6.b is the walk's bh (the 6502's bearing_hi) from here
	move.b	d5,d0
	lsr.b	#1,d0
	andi.b	#1,d0
	tst.b	d5
	jbpl	14f
	addq.b	#2,d0
14:	or.b	d1,d0
	ori.b	#0x80,d0
	move.b	d0,Z_CAPFILL(a5)        | byte 3's fill colour
	| the end markers: on for a solid pattern or a fill class of 3 (the X-major walk reads the bit)
	cmp.b	#0xFF,d6
	jbeq	15f
	andi.b	#3,d0
	cmp.b	#3,d0
	jbne	16f
15:	bset	#16,d6
16:
	| the LAST span of a pass is clamped to the top or the bottom of the view
	move.b	Z_LINEEND(a5),d5
	move.b	F_Y(sp),d0
	addq.b	#1,d0
	cmp.b	Z_ENDIDX(a5),d0
	jbeq	17f
	cmp.b	#0x50,d5
	jbcs	18f
17:	moveq	#0x4F,d5
	tst.b	d7
	jbpl	19f
	moveq	#0,d5
19:	move.b	d5,Z_LINEEND(a5)
18:
	| 6 — the source block and the three screen pages, from the endpoint's x
	move.b	Z_X7E(a5),d0
	sub.b	#0x30,d0
	moveq	#0,d1
	move.b	d0,d1
	lsr.b	#2,d1                   | block
	cmp.b	#0x28,d1
	jbcc	pass_publish            | off the side
	andi.w	#7,d0
	movea.w	d0,a0                   | the sub-column phase, an index from here
	| which of the twelve walks: variant * 16 into d7, the walk's inputs into their registers
	tst.b	d7
	smi	d7
	andi.w	#16,d7                  | the step is -1
	cmp.b	d3,d2
	jbcs	pass_steep              | dx < dy: Y-major
	exg	d2,d3                   | X-major: the DDA adds dy and takes back dx
	tst.b	d4
	exg	d1,d4                   | d4 = the block (exg leaves the arm's flags standing)
	jbmi	pass_srev
	moveq	#0,d1
	move.b	Z_CURSOR(a5),d1         | the start line
	clr.b	F_REV(sp)
	ARMENTRY 0x3E50, 0x2D28, pass_dec_shallow, 0
	btst	#16,d6                  | markers off: the step stays on the way out
	jbeq	pass_ptrs
	cmpi.b	#2,Z_PASS(a5)
	.if SABOTAGE == 4
	jbcc	pass_ptrs               | SABOTAGE 4: the step-in selection inverted
	.else
	jbcs	pass_ptrs               | ascending: the step moves IN for passes 2 and 3
	.endif
	jbra	pass_stepin
pass_srev:
	moveq	#0,d1
	move.b	Z_CURSOR(a5),d1
	move.w	#0x8000,F_REV(sp)       | descending, entry step 0 (for now)
	add.w	#64,d7
	ARMENTRY 0x40D0, 0x2DAB, pass_dec_shallow, 1
	btst	#16,d6
	jbeq	pass_ptrs
	cmpi.b	#2,Z_PASS(a5)
	jbcc	pass_ptrs               | descending: for passes 0 and 1
	moveq	#-1,d0                  | the entry step is the direction: -1 when the step is negative
	btst	#4,d7
	jbne	20f
	moveq	#1,d0
20:	move.b	d0,F_STEP(sp)           | the cap replays this entry step
pass_stepin:
	add.w	#32,d7                  | the step happens on the way IN, and Y is nudged to match
	btst	#4,d7
	jbne	21f
	subq.b	#1,d1
	jbra	pass_ptrs
21:	addq.b	#1,d1
	jbra	pass_ptrs
pass_steep:
	add.w	#128,d7
	tst.b	d4
	exg	d1,d4                   | d4 = the block
	jbmi	pass_trev
	moveq	#0,d1
	move.b	Z_CURSOR(a5),d1         | the start line
	clr.b	F_REV(sp)
	ARMENTRY 0x3ED0, 0x2E2F, pass_dec_steep, 0
	jbra	pass_ptrs
pass_trev:
	moveq	#0,d1
	move.b	Z_CURSOR(a5),d1
	move.w	#0x8000,F_REV(sp)
	add.w	#32,d7
	ARMENTRY 0x3ED8, 0x2EA8, pass_dec_steep, 1

pass_ptrs:
	| the three screen pointers: the page from the block, the low bytes from mem[] as draw_road left them
	move.w	d4,d0
	lsr.b	#1,d0
	add.b	#0x31,d0
	lsl.w	#8,d0                   | (page + 1) << 8
	move.b	Z_PTR3LO(a5),d0
	lea	-BASE(a5),a3
	adda.w	d0,a3
	sub.w	#0x100,d0
	move.b	Z_PTR1LO(a5),d0
	lea	-BASE(a5),a1
	adda.w	d0,a1
	move.b	Z_PTR2LO(a5),d0
	lea	-BASE(a5),a2
	adda.w	d0,a2
	movea.l	F_DEST(sp),a4
	.if SABOTAGE == 5
	addq.b	#1,d1                   | SABOTAGE 5: the start line off by one
	.endif
	jsr	span_walk_enter

	| the write-back: the three pointers' C words (their mem[] page bytes are dead — see the header)
	lea	-BASE(a5),a0
	move.l	a1,d0
	sub.l	a0,d0
	move.w	d0,plot_ptr_v
	move.l	a2,d0
	sub.l	a0,d0
	move.w	d0,plot_ptr2_v
	move.l	a3,d0
	sub.l	a0,d0
	move.w	d0,plot_ptr3_v
	tst.l	d7
	jbne	pass_abandoned
	tst.b	F_REV(sp)
	jbpl	pass_publish            | an ascending walk that ran out caps nothing
	add.b	F_STEP(sp),d1           | $2F12: the descending arms replay the entry step...
	moveq	#0,d0                   | ...and cap the line with X = $2F
	jbra	pass_cap
pass_abandoned:
	tst.b	Z_CAPPEND(a5)
	jbeq	pass_publish
	moveq	#1,d0                   | the abandon path's cap, with X = S + 2
pass_cap:
	move.l	d0,-(sp)
	move.l	d1,-(sp)
	jsr	span_asm_cap
	addq.l	#8,sp

| ----- interp_edge_publish: carry the endpoint forward unless the ends were swapped -----
pass_publish:
	tst.b	Z_SWAPPED(a5)
	jbmi	1f
	move.b	Z_X77(a5),Z_X7E(a5)
	move.b	Z_LINEEND(a5),Z_CURSOR(a5)
1:	move.b	F_Y(sp),Z_NEARIDX(a5)   | $1A18 — only after the call (interp_edge reads the old far)
	move.b	F_X(sp),Z_FARIDX(a5)    | $1A1A
	jbra	pass_next

| An entry offset the chain cannot mean: C reports it (and sets the pointers' C words, which the C's
| walk entry had set by then), then the span publishes — as span_walk_fast_run's early return does.
pass_trap:
	move.l	d4,-(sp)                | the block
	move.l	d7,-(sp)                | variant * 16: which arm
	jsr	span_asm_trap
	addq.l	#8,sp
	jbra	pass_publish

pass_done:
	addq.l	#FRAME,sp
	movem.l	(sp)+,d2-d7/a2-a6
	rts

| The entry offsets each chain recognises (span_entry_decode's switch), as column * 2 + forced; $FF
| is an offset the chain cannot mean.  Shallow $00 is the chain top (column 0, not forced — the same
| entry, since colMark starts at $80 either way).
pass_dec_shallow:
	.byte	0x00,0xFF,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x01,0xFF,0xFF,0xFF,0xFF,0x02,0xFF,0xFF
	.byte	0xFF,0xFF,0xFF,0x03,0xFF,0xFF,0xFF,0xFF,0x04,0xFF,0xFF,0xFF,0xFF,0xFF,0x05,0xFF
	.byte	0xFF,0xFF,0xFF,0x06,0xFF,0xFF,0xFF,0xFF,0xFF,0x07,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
	.byte	0xFF,0xFF,0xFF,0x08,0xFF,0xFF,0xFF,0xFF,0xFF,0x09,0xFF,0xFF,0xFF,0xFF,0x0A,0xFF
	.byte	0xFF,0xFF,0xFF,0xFF,0x0B,0xFF,0xFF,0xFF,0xFF,0x0C,0xFF,0xFF,0xFF,0xFF,0xFF,0x0D
	.byte	0xFF,0xFF,0xFF,0xFF,0x0E,0xFF,0xFF,0xFF,0xFF,0xFF,0x0F,0xFF,0xFF,0xFF,0xFF,0xFF
	.fill	160,1,0xFF
pass_dec_steep:
	.byte	0x01,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x03,0xFF,0xFF,0xFF,0xFF
	.byte	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x05,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
	.byte	0xFF,0x07,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x09,0xFF
	.byte	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x0B,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
	.byte	0xFF,0xFF,0xFF,0xFF,0x0D,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x0F
	.fill	176,1,0xFF
	.even
.Lspan_pass_m68k_end:                     | (.L: the symbol would name the next function)
	.size	span_pass_m68k, .Lspan_pass_m68k_end-span_pass_m68k
