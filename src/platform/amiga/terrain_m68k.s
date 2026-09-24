| terrain_m68k.s — THE TERRAIN PAINTER IN 68000 REGISTERS (Amiga only; `make TERRAINASM=0` is the C control)
|
| This is revs_plot_terrain / plot_terrain_line (RevsPlot.cpp) for a whole sweep block: every display
| line is its entry byte up to the first event, then each event's byte up to the next — view_consume's
| RLE, so the output is byte-for-byte the C painter's.  The C is still the reference: `make
| TERRAINCHECK=1` walks every painted line cell by cell against its event list, and `LOWFULLCHECK=1`
| checks the low block against the run painter on a poisoned buffer.
|
| ⭐ WHY IT IS NOT THE C's SHAPE.  The C asks each of ten groups "does a run start here?  does the
| next one start inside?" — two compares and two branches a group even when nothing changes — and a
| group holding an event falls into a byte loop of ~45 instructions.  Here the question is asked once
| per EVENT, never per group:
|   * the groups before an event's group are ONE computed jump into ten unrolled longword pairs.  The
|     jump offset is -(cells to fill), because a group is four cells AND four bytes of code (two
|     two-byte `move.l Dn,(An)+`), so there is no scaling at all;
|   * a group an event splits is painted whole in the OLD colour by that same fill, and then only its
|     tail — the bytes from the event's cell on — is overwritten in the new one: one byte, one word or
|     byte + word per plane.  A second event in the same group just overwrites its own tail, so the
|     C's masked merge (⛔ lost to GCC's register allocation, twice) is not needed at all;
|   * an event on a group boundary costs nothing but its colour lookup — the next fill carries it.
| ⭐ NO PER-LINE ADDRESS LOOKUP.  Both drivers step exactly one display row per sweep line
|   (step_scanline: +1 inside a character row, +$138 across one — MODE 5's layout, i.e. always the
|   next scan line), so the rows of one block are CONTIGUOUS and interleaved: a line's plane-1 row is
|   the previous line's plane-2 row + 40, which is exactly where the previous line's plane-2 pointer
|   stopped.  The C caller proves contiguity at the two ends of the block before calling (a scan-line
|   step cannot move 0 rows, so equal ends mean every step moved 1) and TERRAINCHECK re-proves it on
|   every line.
| ⚠ The event list is ASCENDING (the scan emits a line's events in cell order and the low block's seed
|   is inserted in that same order), so a fill never runs backwards.  A descending list would jump past
|   the fill block; TERRAINCHECK's cell walk relies on the same order.
|
| void terrain_paint_m68k(unsigned lines, uint8_t *plane, uint8_t *own, const uint8_t *bgEnd,
|                         const ViewSpan *ev, const uint32_t *expand4)
|   lines   n >= 1 sweep lines, first downwards (display rows y0 .. y0 + n - 1)
|   plane   display row y0's plane-1 byte 0 (plane 2 at +40, row y0 + 1 at +80)
|   own     &g_plotOwn[y0]
|   bgEnd   &g_viewRowBg[first] + 1 (read pre-decrement: the lines run downwards)
|   ev      &g_viewEv[first][0] (each lower line's list is EVSTRIDE bytes below)
|   expand4 g_bbcExpand4 — {lo4, hi4} per colour byte, 8 bytes an entry
| Registers:
|   a0 the event cursor   a1 plane-1 cursor   a3 plane-2 cursor (a1 + 40)   a2 expand4
|   a4 this line's list   a5 bgEnd cursor     a6 own cursor
|   d0 the event's cell   d1 scratch          d2/d3 the run's lo4/hi4 broadcast
|   d6.w cells painted (a multiple of 4, 0..40) d7.w lines - 1 (dbra)

	.equ	EVSTRIDE, 96                | sizeof g_viewEv[0]: 48 two-byte ViewSpans
	.equ	CELLS,    40                | cells a line = bytes a plane row
	.equ	ARGS,     44+4              | past the eleven saved registers and the return

| `--defsym SABOTAGE=N` (`make TERRAINCHECK=1 TERRAINASM_SABOTAGE=N`): a deliberate defect the
| oracles must catch.  0 in every real build.
	.ifndef SABOTAGE
	.equ	SABOTAGE, 0
	.endif

| Ten groups of four cells: a longword a plane each.  Entered at -(cells to fill) from its end.
.macro FILL10
	.rept	10
	move.l	d2,(a1)+
	move.l	d3,(a3)+
	.endr
.endm

| The line's entry byte / an event's colour byte in d1 -> the run's broadcasts in d2/d3.
.macro COLOUR
	lsl.w	#3,d1
	.if SABOTAGE == 3
	addq.w	#8,d1                       | SABOTAGE 3: every colour is the NEXT byte's
	.endif
	move.l	(a2,d1.w),d2
	move.l	4(a2,d1.w),d3
.endm

	.section .text.terrain_paint_m68k,"ax",@progbits
	.even
	.globl	terrain_paint_m68k
	.type	terrain_paint_m68k, @function
terrain_paint_m68k:
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	ARGS+0(sp),d7
	subq.w	#1,d7
	move.l	ARGS+4(sp),a1
	lea	CELLS(a1),a3
	move.l	ARGS+8(sp),a6
	move.l	ARGS+12(sp),a5
	move.l	ARGS+16(sp),a4
	move.l	ARGS+20(sp),a2

tp_line:
	moveq	#0,d1
	move.b	-(a5),d1                    | the line's entry byte
	COLOUR
	move.b	#1,(a6)+                    | claim the display row
	move.l	a4,a0
	.if SABOTAGE == 5
	lea	-EVSTRIDE+2(a4),a4          | SABOTAGE 5: the next line's list one entry off
	.else
	lea	-EVSTRIDE(a4),a4
	.endif
	moveq	#0,d6

tp_event:
	move.b	(a0)+,d0                    | the next run's first cell, or the $FF sentinel
	cmp.b	#CELLS,d0
	jbcc	tp_eol
	moveq	#3,d1
	add.b	d0,d1
	and.w	#0xFC,d1                    | the fill's end: the event's group, whole if it splits it
	sub.w	d1,d6                       | -(cells to fill)
	jmp	1f(pc,d6.w)
	FILL10
1:
	.if SABOTAGE != 6                   | SABOTAGE 6: the painted count is not advanced
	move.w	d1,d6
	.endif
	moveq	#0,d1
	move.b	(a0)+,d1
	COLOUR
	and.w	#3,d0                       | where in its group the run starts
	jbeq	tp_event                    | on the boundary: the next fill carries it
	subq.w	#2,d0
	bcs.s	tp_k1
	beq.s	tp_k2
	move.b	d2,-1(a1)                   | cell 3 of the group
	move.b	d3,-1(a3)
	jbra	tp_event
tp_k2:
	.if SABOTAGE == 2
	move.w	d3,-2(a1)                   | SABOTAGE 2: cells 2-3 take plane 2's byte on plane 1
	.else
	move.w	d2,-2(a1)                   | cells 2-3
	.endif
	move.w	d3,-2(a3)
	jbra	tp_event
tp_k1:
	move.b	d2,-3(a1)                   | cells 1-3
	move.w	d2,-2(a1)
	.if SABOTAGE != 1                   | SABOTAGE 1: plane 2's cell 1 keeps the old colour
	move.b	d3,-3(a3)
	.endif
	move.w	d3,-2(a3)
	jbra	tp_event

tp_eol:
	moveq	#CELLS,d1
	sub.w	d1,d6
	jmp	2f(pc,d6.w)
	FILL10
2:
	move.l	a3,a1                       | the next display row: right after this one's plane 2
	lea	CELLS(a1),a3
	dbra	d7,tp_line

	movem.l	(sp)+,d2-d7/a2-a6
	rts
	.size	terrain_paint_m68k, .-terrain_paint_m68k
