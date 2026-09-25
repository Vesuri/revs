| edge_m68k.s — ONE END OF fill_dash_edge_columns IN 68000 REGISTERS (Amiga only; `make EDGEASM=0` is the C control)
|
| This is edge_run_flat (src/gen/revs_native.c): for each column of one end of the viewport, pass B
| copies the column's source bytes into the per-line boundary table (a $55 read as empty, an empty
| cell classified), and pass A fills the NEXT column's own empty cells with their surface colour ($55
| where the colour is 0).  The C stays the reference: the host runs it, and `make EDGECHECK=1` runs
| both on the same 64 KB every frame and compares all of it, the two plot pointers and the exit.
|
| ⭐ WHY: single-stepped at ~4.2k instructions a frame, half of them surface_colour_at_core — every
| per-line table an absolute-long operand off a freshly indexed address, and the whole SlotExit built
| per empty cell.  Here the classifier indexes six tables off one `lea` of the line and loads the
| colour from a register-held base, and the column bookkeeping is written once, at the end.
| ⭐ WHAT IT STORES: every byte the C stores, with the same value.  The per-column bookkeeping —
| EDGE_COLUMN, span_line_cursor, EDGE_BLOCK_START, the three patch bytes, plot_ptr — is rewritten by
| every column and read by nothing inside the run, so only the LAST column's values survive the C;
| the asm writes exactly those, once.
|
| unsigned edge_run_m68k(unsigned startSrc, unsigned firstColumn, unsigned stopColumn, unsigned firstLine)
|   -> bits 0-7 the exit A, bits 8-15 the exit Y (the last column's block start)
| Registers: a0 = mem, a1 = this pass's source block, a2 = mem+startSrc (the boundary table),
|   a3 = mem+line (the classifier's six tables at fixed offsets), a4 = mem+edge_style-1,
|   a5 = mem+surface_colours; d0 = the line, d1 = this column's block start (the loop's end),
|   d2 = the column, d3 = the stop column, d4 = the column's first line, d5 = the exit A,
|   d6 = the classifier's position, d7 = the colour (and, between cells, the block offset).

	.equ	Z_RUNLIMIT, 0x42            | EDGE_RUN_LIMIT = shared_counter_42
	.equ	Z_HORIZON,  0x1F            | horizon_extent
	.equ	Z_ATTR1LIM, 0x29            | line_attr_1_limit
	.equ	Z_ATTR0LIM, 0x2C            | line_attr_0_limit
	.equ	Z_PTR,      0x70            | plot_ptr lo/hi
	.equ	Z_PTR2,     0x72            | plot_ptr2 lo/hi
	.equ	Z_LINECUR,  0x7F            | span_line_cursor
	.equ	Z_BLOCKST,  0x82            | EDGE_BLOCK_START = point_delta_lo[2]
	.equ	Z_COLUMN,   0x85            | EDGE_COLUMN = point_delta_hi[2]
	.equ	LATTR0,     0x0400          | line_attr_0 (line_attr_1 at $0450)
	.equ	LATTR1,     0x0450
	.equ	SEDGE0,     0x0554          | surface_edge_0..3
	.equ	SEDGE1,     0x05A4
	.equ	SEDGE2,     0x0600
	.equ	SEDGE3,     0x0650
	.equ	VLSURF,     0x5F60          | view_line_surface
	.equ	STYLE_PREV, 0x5EDF          | edge_style - 1 (a line_attr entry is an index PLUS ONE)
	.equ	COLOURS,    0x38FC          | surface_colours
	.equ	STARTS,     0x3900          | dash_block_starts
	.equ	BLOCKS,     0x3000          | the source blocks, $80 apart
	.equ	GAP_FALLBACK, 0x1DDC        | the three patch bytes, as pass A leaves them
	.equ	GAP_BRANCH, 0x1DD5
	.equ	GAP_PTR,    0x1DDE
	.equ	ARGS,       40+4            | past the ten saved registers and the return

| `--defsym SABOTAGE=N` (`make EDGECHECK=1 EDGEASM_SABOTAGE=N`): a deliberate defect EDGECHECK must
| catch.  0 in every real build.
	.ifndef SABOTAGE
	.equ	SABOTAGE, 0
	.endif

| surface_colour_at_core's colour for line d0 at position d6, into d7.b.  Uses a3.
	.macro	CLASSIFY
	cmp.b	Z_HORIZON(a0),d0
	.if SABOTAGE == 1
	bcc.s	cl_sky\@                    | SABOTAGE 1: the horizon line itself read as sky
	.else
	bhi.s	cl_sky\@                    | above the horizon: sky
	.endif
	lea	(a0,d0.w),a3
	cmp.b	SEDGE0(a3),d6
	bcc.s	cl_c3\@                     | past the outermost boundary
	cmp.b	SEDGE2(a3),d6
	bcs.s	cl_n2\@
	cmp.b	Z_ATTR1LIM(a0),d0
	bcc.s	cl_c3\@
	moveq	#0x7F,d7
	and.b	LATTR1(a3),d7
	bra.s	cl_attr\@
cl_n2\@:
	cmp.b	SEDGE3(a3),d6
	bcc.s	cl_c0\@
	cmp.b	SEDGE1(a3),d6
	bcs.s	cl_in\@
	cmp.b	Z_ATTR0LIM(a0),d0
	bcc.s	cl_c3\@
	moveq	#0x7F,d7
	and.b	LATTR0(a3),d7
cl_attr\@:
	move.b	(a4,d7.w),d7                | the edge point's style...
	.if SABOTAGE == 4
	and.w	#7,d7                       | SABOTAGE 4: the colour index takes a bit too many
	.else
	and.w	#3,d7                       | ...whose low bits are the colour
	.endif
	move.b	(a5,d7.w),d7
	bra.s	cl_done\@
cl_in\@:
	moveq	#3,d7
	and.b	VLSURF(a3),d7               | inside everything: the line's own background class
	move.b	(a5,d7.w),d7
	bra.s	cl_done\@
cl_sky\@:
	move.b	1(a5),d7
	bra.s	cl_done\@
cl_c3\@:
	move.b	3(a5),d7
	bra.s	cl_done\@
cl_c0\@:
	move.b	(a5),d7
cl_done\@:
	.endm

	.section .text.edge_run_m68k,"ax",@progbits
	.even
	.globl	edge_run_m68k
	.type	edge_run_m68k, @function
edge_run_m68k:
	movem.l	d2-d7/a2-a5,-(sp)
	lea	mem,a0
	moveq	#0,d0
	move.w	ARGS+2(sp),d0               | startSrc
	lea	(a0,d0.l),a2
	move.w	d0,plot_ptr2_v              | $1DEF's seed, as edge_column_pass makes it
	move.b	d0,Z_PTR2(a0)
	lsr.w	#8,d0
	move.b	d0,Z_PTR2+1(a0)
	moveq	#0,d2
	move.b	ARGS+7(sp),d2               | firstColumn
	moveq	#0,d3
	move.b	ARGS+11(sp),d3              | stopColumn
	move.b	d3,Z_RUNLIMIT(a0)
	moveq	#0,d4
	move.b	ARGS+15(sp),d4              | firstLine
	lea	STYLE_PREV(a0),a4
	lea	COLOURS(a0),a5
	moveq	#0,d0                       | the line lives in the low byte; the rest stays 0

er_column:
	lea	(a0,d2.w),a3
	moveq	#0,d1
	move.b	STARTS(a3),d1               | ONE end for both passes (the original's own shape)
	move.w	d2,d7
	lsl.w	#7,d7
	add.w	#BLOCKS,d7                  | this column's block, $3000 + column * $80
	lea	(a0,d7.w),a1
	move.w	d7,d5
	lsr.w	#8,d5                       | the no-iteration exit A: the block pointer's high byte

	| ---- pass B ($1DFA): this column into the per-line boundary table, $55 read as empty
	move.b	d2,d6
	move.b	d4,d0
er_b:
	cmp.b	d1,d0
	jbeq	er_bdone
	move.b	(a1,d0.w),d5
	jbeq	er_bempty
	cmp.b	#0x55,d5
	bne.s	1f
	.if SABOTAGE == 2
	move.b	d5,(a2,d0.w)                | SABOTAGE 2: a $55 copied as itself
	.else
	clr.b	(a2,d0.w)                   | $55 is "empty" in this pass
	.endif
	jbra	er_bnext
1:	move.b	d5,(a2,d0.w)
	jbra	er_bnext
er_bempty:
	CLASSIFY
	move.b	d7,d5
	move.b	d7,(a2,d0.w)                | this pass's fallback IS $00
er_bnext:
	subq.b	#1,d0
	jbra	er_b
er_bdone:

	| ---- pass A ($1E03): the NEXT column fills its own block; non-zero bytes kept
	addq.b	#1,d2
	move.w	d2,d7                       | (d7 is the classifier's too: recompute, never add)
	lsl.w	#7,d7
	add.w	#BLOCKS,d7
	lea	(a0,d7.w),a1
	move.w	d7,d5
	lsr.w	#8,d5
	move.b	d2,d6
	move.b	d4,d0
er_a:
	cmp.b	d1,d0
	jbeq	er_adone
	move.b	(a1,d0.w),d5
	jbne	er_anext                    | the $09 skip
	CLASSIFY
	move.b	d7,d5
	jbne	1f                          | (jbne: sabotage 3 leaves nothing to branch over)
	.if SABOTAGE != 3                   | SABOTAGE 3: a colour-0 cell left 0, not $55
	moveq	#0x55,d5
	.endif
1:	move.b	d5,(a1,d0.w)
er_anext:
	subq.b	#1,d0
	jbra	er_a
er_adone:
	cmp.b	d3,d2
	jbeq	er_last
	.if SABOTAGE != 6                   | SABOTAGE 6: the next column starts at the old line
	move.w	d1,d4                       | pass A's exit line threads to the next column
	.endif
	jbra	er_column

	| ---- the last column's bookkeeping, which is all the C leaves behind
er_last:
	move.b	d2,Z_COLUMN(a0)
	move.b	d4,Z_LINECUR(a0)
	move.b	d1,Z_BLOCKST(a0)
	move.b	#Z_PTR,GAP_PTR(a0)
	move.b	#0x09,GAP_BRANCH(a0)
	move.b	#0x55,GAP_FALLBACK(a0)
	move.w	d2,d7
	lsl.w	#7,d7
	add.w	#BLOCKS,d7                  | the stop column's block: pass A's plot_ptr
	move.w	d7,plot_ptr_v
	move.b	d7,Z_PTR(a0)
	lsr.w	#8,d7
	.if SABOTAGE == 5
	addq.b	#1,d7                       | SABOTAGE 5: the pointer's high lane one page off
	.endif
	move.b	d7,Z_PTR+1(a0)
	moveq	#0,d0
	move.b	d1,d0
	lsl.w	#8,d0
	move.b	d5,d0                       | A | Y << 8
	movem.l	(sp)+,d2-d7/a2-a5
	rts
	.size	edge_run_m68k, .-edge_run_m68k
