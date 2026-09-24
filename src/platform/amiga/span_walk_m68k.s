| span_walk_m68k.s — THE SPAN WALK IN 68000 REGISTERS (Amiga only; `make SPANASM=0` is the C control)
|
| This is span_walk_fast + fast_plot (src/gen/revs_native.c) for the spans span_walk_fast_ok has
| already proved safe, with the walk's whole state in registers — which GCC cannot do, because the
| walk holds ~22 live values and GCC spills 94-137 stack operands whatever the C shape
| (docs/perf-method.md §the span walk in 68000 asm).  The C function it replaces is still the
| reference: the host runs it, and `make WALKCHECK=1` runs both on the target, span for span,
| and compares every byte they write.
|
| ⭐ WHAT MAKES IT SMALL, all of it DERIVED at the C (span_walk_fast's banner) and gated by WALKCHECK:
|   * the DDA carry into every add is 0 on this path, so the DDA is add.b / bcc / sub.b;
|   * the eight columns are UNROLLED, so which plotter, which pointer and which pattern byte a
|     column uses are constants in the instruction — a column that does not plot is two
|     instructions, and the first line's computed entry is a jump table;
|   * one of the two Y steps is always 0 and the other is +-1 (the C caller only sends such a
|     span here), so there are twelve routines: shallow {fwd,rev} x {step in,step out} x {+1,-1}
|     and steep {fwd,rev} x {+1,-1}, all generated from the macros below.
| ⚠ dash_block_starts ($3900+block) and the three pattern tables are read LIVE from mem[] on every
|   use, exactly as the C does: they sit inside the pages this walk writes, and a store can land on
|   them.  Everything else provably cannot move under the walk (the guard), so it lives in registers.
|
| Registers (the C bridge in revs_native.c, span_walk_asm, loads them):
|   in   d1.w  y (upper byte of the word 0)        d2.b  the DDA addend      d3.b  its subtrahend
|        d4    block (zero-extended)               d5    lineEnd.b | (lines - 1) << 16
|        d6    bh.b | markOn << 16                 d7.w  variant * 16 + startCol * 2 + forced
|        a1/a2/a3  mem + plot_ptr / plot_ptr2 / plot_ptr3          a4  mem + the destination
|   out  d1.b  y   d4.b block   a1-a3 the pointers   d7.l 1 = abandoned (y reached lineEnd), 0 = ran out
|   clobbers d0, d5, a0; preserves a5/a6 (a5 may be GCC's frame pointer) and d6 (bits 17-31 unused)
|   working: d0.b the DDA accumulator, d0 bit 8 = "this half-line has plotted" (colMark != $80),
|            d7 scratch, a0 = mem + $3900 + block, a5 = mem + $628F, a6 = mem + $337C.

	.equ	MEM_DBS,   0x3900          | dash_block_starts
	.equ	MEM_PAT,   0x628F          | colour_pattern_tbl
	.equ	MEM_AND,   0x337C          | colour_pattern_and_tbl
	.equ	OR_OFF,    0x629C - 0x628F | colour_pattern_or_tbl, from a5

| `--defsym SABOTAGE=N` (`make WALKCHECK=1 SPANASM_SABOTAGE=N`): a deliberate defect WALKCHECK must
| catch.  0 in every real build.
	.ifndef SABOTAGE
	.equ	SABOTAGE, 0
	.endif

.macro STEPY dir
	.if \dir > 0
	addq.b	#1,d1
	.else
	subq.b	#1,d1
	.endif
.endm

| One plot: fast_plot's body.  `cell` is the pointer the cell is read and merged through, `line` the
| one the bearing byte is copied through, `col` the column 0..3.  The common case — an empty cell —
| runs straight through; a non-empty one goes to the cold code in subsection 1.
.macro PLOT cell, line, col, inmode, dir, abandon
	.if \inmode
	STEPY	\dir                    | the step on the way IN
	.endif
	cmp.b	d5,d1
	.if SABOTAGE == 5
	jbeq	50f                     | SABOTAGE 5: y == lineEnd does not abandon
	.else
	jbeq	\abandon                | the span's end line: abandon, C writes back and caps
	.endif
50:
	move.b	d4,(a4,d1.w)            | dest[y] = block — which source block feeds this line
	.if SABOTAGE == 3
	.ifc \cell,a1
	move.b	(a2,d1.w),d7            | SABOTAGE 3: plotter 2 reads its cell through p2
	.else
	move.b	(\cell,d1.w),d7
	.endif
	.else
	move.b	(\cell,d1.w),d7
	.endif
	jbne	51f
	move.b	\col(a5),d7             | an empty cell takes the pattern whole
54:	move.b	d7,(\cell,d1.w)
	move.b	d6,(\line,d1.w)         | ...and the line copy gets bearing_hi
	.if \inmode == 0
	STEPY	\dir                    | the step on the way OUT
	.endif
59:
	.subsection 1
51:	cmp.b	#0x2C,d1                | a filled cell on the block's first line is left alone:
	bcc.s	52f                     |   y < $2C && y <= dash_block_starts[block]
	cmp.b	(a0),d1
	.if SABOTAGE == 1
	bcc.s	52f                     | SABOTAGE 1: the first-line test off by one (y >= , not y >)
	.else
	bhi.s	52f
	.endif
	.if \inmode == 0
	STEPY	\dir
	.endif
	jbra	59b
52:
	.if SABOTAGE != 4
	cmp.b	#0x55,d7                | "all four columns" reads as empty...
	bne.s	53f
	moveq	#0,d7
53:
	.endif
	and.b	\col(a6),d7
	or.b	\col+OR_OFF(a5),d7
	jbne	54b
	moveq	#0x55,d7                | ...and an empty result is substituted back
	jbra	54b
	.subsection 0
.endm

| The end/half markers: a half-line that plotted nothing is terminated with $FF — span_end_marker_body.
.macro MARKER ptr
	bclr	#8,d0                   | colMark = $80, testing what it was
	.if SABOTAGE != 2
	bne.s	69f                     | this half plotted: no terminator
	.endif
	btst	#16,d6                  | markers switched off for this span
	beq.s	69f
	cmp.b	#0x2C,d1
	bcc.s	61f
	cmp.b	(a0),d1
	bls.s	69f
61:	st	(\ptr,d1.w)
69:
.endm

| The block is a BYTE and wraps (a reverse span that starts low steps it below 0 — a third of them
| on Silverstone), so a0 must follow the wrap: dash_block_starts[block] is mem[$3900 + (uint8_t)block].
.macro BLOCKSTEP rev
	.if \rev
	subq.l	#1,a0                   | (an address-register op leaves the flags alone)
	subq.b	#1,d4
	bcc.s	65f
	.if SABOTAGE == 7
	lea	0(a0),a0                | SABOTAGE 7: the pointer does not follow the block's wrap
	.else
	lea	256(a0),a0              | 0 -> $FF
	.endif
65:
	.else
	addq.l	#1,a0
	addq.b	#1,d4
	bcc.s	65f
	lea	-256(a0),a0             | $FF -> 0
65:
	.endif
.endm

.macro LINESTEP rev
	.if \rev
	lea	-256(a1),a1
	lea	-256(a2),a2
	lea	-256(a3),a3
	.else
	lea	256(a1),a1
	lea	256(a2),a2
	lea	256(a3),a3
	.endif
	BLOCKSTEP \rev
.endm

| X-major column i: only a carry lands a pixel.  _f<i> is the computed entry's "plot the first
| column whole" label.
.macro SCOL name, i, col, cell, line, inmode, dir
\name\()_c\i:
	add.b	d2,d0
	jbcc	\name\()_n\i
	sub.b	d3,d0
\name\()_f\i:
	ori.w	#0x100,d0               | colMark = this column
	PLOT	\cell, \line, \col, \inmode, \dir, \name\()_abandon
\name\()_n\i:
.endm

| Y-major column i: plot, and plot again, until the DDA carries.
.macro TCOL name, i, col, cell, line, dir
\name\()_c\i:
\name\()_f\i:
70:	PLOT	\cell, \line, \col, 0, \dir, \name\()_abandon
	add.b	d2,d0
	jbcc	70b
	sub.b	d3,d0
.endm

| Plotter 1 merges its cell through p2 (a2) and copies through p1 (a1); plotter 2 through p1 and p3.
| Forward, columns 0-3 are plotter 1 and 4-7 plotter 2, column = i & 3; reverse, the other way
| round with column = 3 - (i & 3).
.macro SHALLOW name, rev, inmode, dir
\name\()_line:
	andi.w	#0x00FF,d0              | a new line: colMark = $80
	.if SABOTAGE == 6
	addq.b	#1,d0                   | SABOTAGE 6: a carry-in of 1 into each later line's first add
	.endif
	.if \rev
	SCOL	\name, 0, 3, a1, a3, \inmode, \dir
	SCOL	\name, 1, 2, a1, a3, \inmode, \dir
	SCOL	\name, 2, 1, a1, a3, \inmode, \dir
	SCOL	\name, 3, 0, a1, a3, \inmode, \dir
	MARKER	a1
	BLOCKSTEP 1
	SCOL	\name, 4, 3, a2, a1, \inmode, \dir
	SCOL	\name, 5, 2, a2, a1, \inmode, \dir
	SCOL	\name, 6, 1, a2, a1, \inmode, \dir
	SCOL	\name, 7, 0, a2, a1, \inmode, \dir
	MARKER	a2
	.else
	SCOL	\name, 0, 0, a2, a1, \inmode, \dir
	SCOL	\name, 1, 1, a2, a1, \inmode, \dir
	SCOL	\name, 2, 2, a2, a1, \inmode, \dir
	SCOL	\name, 3, 3, a2, a1, \inmode, \dir
	MARKER	a2
	BLOCKSTEP 0
	SCOL	\name, 4, 0, a1, a3, \inmode, \dir
	SCOL	\name, 5, 1, a1, a3, \inmode, \dir
	SCOL	\name, 6, 2, a1, a3, \inmode, \dir
	SCOL	\name, 7, 3, a1, a3, \inmode, \dir
	MARKER	a1
	.endif
	LINESTEP \rev
	sub.l	#0x10000,d5             | lines left, in d5's upper word
	jbcc	\name\()_line
	moveq	#0,d7
	jbra	span_asm_exit
\name\()_abandon:
	moveq	#1,d7
	jbra	span_asm_exit
.endm

.macro STEEP name, rev, dir
\name\()_line:
	.if SABOTAGE == 6
	addq.b	#1,d0
	.endif
	.if \rev
	TCOL	\name, 0, 3, a1, a3, \dir
	TCOL	\name, 1, 2, a1, a3, \dir
	TCOL	\name, 2, 1, a1, a3, \dir
	TCOL	\name, 3, 0, a1, a3, \dir
	BLOCKSTEP 1
	TCOL	\name, 4, 3, a2, a1, \dir
	TCOL	\name, 5, 2, a2, a1, \dir
	TCOL	\name, 6, 1, a2, a1, \dir
	TCOL	\name, 7, 0, a2, a1, \dir
	.else
	TCOL	\name, 0, 0, a2, a1, \dir
	TCOL	\name, 1, 1, a2, a1, \dir
	TCOL	\name, 2, 2, a2, a1, \dir
	TCOL	\name, 3, 3, a2, a1, \dir
	BLOCKSTEP 0
	TCOL	\name, 4, 0, a1, a3, \dir
	TCOL	\name, 5, 1, a1, a3, \dir
	TCOL	\name, 6, 2, a1, a3, \dir
	TCOL	\name, 7, 3, a1, a3, \dir
	.endif
	LINESTEP \rev
	sub.l	#0x10000,d5
	jbcc	\name\()_line
	moveq	#0,d7
	jbra	span_asm_exit
\name\()_abandon:
	moveq	#1,d7
	jbra	span_asm_exit
.endm

.macro ENTRIES name
	.word	\name\()_c0-span_asm_table, \name\()_f0-span_asm_table
	.word	\name\()_c1-span_asm_table, \name\()_f1-span_asm_table
	.word	\name\()_c2-span_asm_table, \name\()_f2-span_asm_table
	.word	\name\()_c3-span_asm_table, \name\()_f3-span_asm_table
	.word	\name\()_c4-span_asm_table, \name\()_f4-span_asm_table
	.word	\name\()_c5-span_asm_table, \name\()_f5-span_asm_table
	.word	\name\()_c6-span_asm_table, \name\()_f6-span_asm_table
	.word	\name\()_c7-span_asm_table, \name\()_f7-span_asm_table
.endm

	.section .text.span_walk_m68k,"ax",@progbits
	.even
	.globl	span_walk_m68k
	.type	span_walk_m68k, @function
span_walk_m68k:
	movem.l	a5-a6,-(sp)
	lea	mem+MEM_PAT,a5
	lea	mem+MEM_AND,a6
	bsr.s	span_walk_enter
	movem.l	(sp)+,a5-a6
	rts

| ⭐ The walk proper, for a caller that already holds a5 = mem + $628F and a6 = mem + $337C — the C
| bridge above, and span_pass_m68k (span_pass_m68k.s), which falls into it straight out of the setup.
| Registers as in the header; it returns with rts (the walk's last act is `rts` from span_asm_exit).
	.globl	span_walk_enter
span_walk_enter:
	add.w	d7,d7
	lea	span_asm_table(pc),a0
	move.w	(a0,d7.w),d7
	pea	(a0,d7.w)               | the entry column, returned to by the rts below
	moveq	#0,d0
	move.b	d4,d0
	lea	mem+MEM_DBS,a0
	adda.l	d0,a0                   | a0 = &dash_block_starts[block]
	moveq	#0,d0
	sub.b	d3,d0                   | acc = -subtrahend: the first carry lands the first pixel
	rts

span_asm_exit:
	rts

| variant = shallow: rev * 4 + stepIn-mode * 2 + (step < 0);  steep: 8 + rev * 2 + (step < 0)
span_asm_table:
	ENTRIES	sfo_p
	ENTRIES	sfo_n
	ENTRIES	sfi_p
	ENTRIES	sfi_n
	ENTRIES	sro_p
	ENTRIES	sro_n
	ENTRIES	sri_p
	ENTRIES	sri_n
	ENTRIES	tf_p
	ENTRIES	tf_n
	ENTRIES	tr_p
	ENTRIES	tr_n

	SHALLOW	sfo_p, 0, 0, 1
	SHALLOW	sfo_n, 0, 0, -1
	SHALLOW	sfi_p, 0, 1, 1
	SHALLOW	sfi_n, 0, 1, -1
	SHALLOW	sro_p, 1, 0, 1
	SHALLOW	sro_n, 1, 0, -1
	SHALLOW	sri_p, 1, 1, 1
	SHALLOW	sri_n, 1, 1, -1
	STEEP	tf_p, 0, 1
	STEEP	tf_n, 0, -1
	STEEP	tr_p, 1, 1
	STEEP	tr_n, 1, -1

	.subsection 1
span_walk_m68k_end:                     | subsection 1 (the cold plot arms) is laid out last
	.subsection 0
	.size	span_walk_m68k, span_walk_m68k_end-span_walk_m68k
