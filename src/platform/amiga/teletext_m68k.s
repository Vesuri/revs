| teletext_m68k.s — THE MODE 7 ROW PAINTER, IN 68000 REGISTERS
| (Amiga only; `make TTASM=0` is the C control, tt_paint_row in RevsScreen.cpp)
|
| One DISPLAY row from 40 decoded keys (teletext.h: TT_KEY), exactly what tt_paint_row does: every
| PAIR in which either key moved is painted, runs of blank-on-black pairs go to the blitter
| (tt_blit_clear_c), and the keys shadow is updated.  `make TTCHECK=1` compares the whole bitmap
| with the old row loop after every decode, so it gates this file as it gated the C.
|
| ⭐ WHY: single-stepped, the C row loop was ~29 instructions a VISITED pair and the pair painter
| ~170 a glyph pair — its three planes each pay `and` + `eor` with a mask pair built from two
| tables per pair, and GCC kept the row state in the frame.  Here the row state lives in
| registers, and a pair whose two cells share a colour on a black background — plain text and
| most mosaics — goes to one of EIGHT specialised routines where a glyph line is one fetch and
| three plain stores (the idiom of a per-colour font copier): the plane is the glyph where the
| foreground has that bit and zero where it has not.  Every other pair takes the general routine,
| the C painter's `(w & M) ^ C` with all six masks in registers.
|
| unsigned tt_paint_row_m68k(uint8_t* rowTop, uint32_t* keys, const uint32_t* cells, unsigned half)
|   -> cells in moved pairs (2 a pair), as tt_paint_row
| Row loop:  a0 = cells, a1 = keys, a2 = the pair's destination (advances 2 a pair), a6 = the glyph
|   words (g_ttGlyphHi) and a3 = the glyph bytes (g_ttFont), both advanced to the half's first
|   source line; d4 = the pending blitter run's length in pairs (0 = none), d6 = pairs left (dbra,
|   low word) : cells painted (high word), d7 = half << 12.
| Painters: d0/d1 = the two keys, a2 = destination, a4 = glyph A's words, a5 = glyph B's bytes;
|   d2/d3/d5 scratch (the general one saves d4/d6/d7 and uses d0-d7).
| ⚠ BLANK is `code <= $20`, not a table lookup: the generated font's blank glyphs are exactly codes
|   $00-$20 in all three sets (and the decoder never emits a code below $20) — the C painter's
|   g_ttBlank says the same, and TTCHECK compares the result with it.

	.equ	ROWB,    120                | kTtRowBytes: one display line, three interleaved planes
	.equ	GAP,     40                 | kTtPlaneGap
	.equ	BGMASK,  0x380000           | TT_KEY(0, 0, 0, 7): the background bits
	.equ	GLYPH,   0x1FF              | TT_KEY_GLYPH
	.equ	PAIRS,   20                 | TT_COLS / 2
	.equ	F_RUNAT,   0                | frame: the pending blitter run's first word
	.equ	FRAME,     4
	.equ	ARGS,      FRAME+44+4       | past the frame, the eleven saved registers and the return

	.ifndef SABOTAGE
	.equ	SABOTAGE, 0
	.endif

| One plane word of a specialised line: the glyph word (d2) where foreground F has bit P, else 0 (d5).
	.macro	PST f, p, y
	.if SABOTAGE == 1 && \p == 1
	.if (\f >> 2) & 1                   | SABOTAGE 1: plane 1 takes plane 2's bit
	move.w	d2,(\y)*ROWB+(\p)*GAP(a2)
	.else
	move.w	d5,(\y)*ROWB+(\p)*GAP(a2)
	.endif
	.else
	.if (\f >> \p) & 1
	move.w	d2,(\y)*ROWB+(\p)*GAP(a2)
	.else
	move.w	d5,(\y)*ROWB+(\p)*GAP(a2)
	.endif
	.endif
	.endm

| The glyph word of the pair's next source line: A's pre-shifted high byte | B's byte.
	.macro	GW
	move.w	(a4)+,d2
	or.b	(a5)+,d2
	.endm

	.macro	FG_LINE f, y
	GW
	PST	\f, 0, \y
	PST	\f, 1, \y
	PST	\f, 2, \y
	.endm

| Double height: each source line of the half over two display lines — the chip's rule.
	.macro	FG_DLINE f, y
	GW
	PST	\f, 0, 2*\y
	PST	\f, 1, 2*\y
	PST	\f, 2, 2*\y
	.if SABOTAGE == 2 && \y == 4
	PST	\f, 0, 2*\y                 | SABOTAGE 2: the half's last display line is never painted
	PST	\f, 1, 2*\y
	PST	\f, 2, 2*\y
	.else
	PST	\f, 0, 2*\y+1
	PST	\f, 1, 2*\y+1
	PST	\f, 2, 2*\y+1
	.endif
	.endm

	.macro	FG_ROUTINE f
tt_fg\f:
	moveq	#0,d5
	FG_LINE	\f, 0
	FG_LINE	\f, 1
	FG_LINE	\f, 2
	FG_LINE	\f, 3
	FG_LINE	\f, 4
	FG_LINE	\f, 5
	FG_LINE	\f, 6
	FG_LINE	\f, 7
	FG_LINE	\f, 8
	FG_LINE	\f, 9
	bra	.Lnext
	.endm

	.macro	FG_DROUTINE f
tt_fgd\f:
	moveq	#0,d5
	FG_DLINE \f, 0
	FG_DLINE \f, 1
	FG_DLINE \f, 2
	FG_DLINE \f, 3
	FG_DLINE \f, 4
	bra	.Lnext
	.endm

| One plane word of a general line: (glyph & M) ^ C; glyph in d0, temp d1.
	.macro	GPST m, c, p, y
	move.w	d0,d1
	and.w	\m,d1
	eor.w	\c,d1
	move.w	d1,(\y)*ROWB+(\p)*GAP(a2)
	.endm

	.macro	GEN_LINE y
	move.w	(a4)+,d0
	or.b	(a5)+,d0
	GPST	d2, d5, 0, \y
	GPST	d3, d6, 1, \y
	GPST	d4, d7, 2, \y
	.endm

	.macro	GEN_DLINE y
	move.w	(a4)+,d0
	or.b	(a5)+,d0
	GPST	d2, d5, 0, 2*\y
	GPST	d3, d6, 1, 2*\y
	GPST	d4, d7, 2, 2*\y
	GPST	d2, d5, 0, 2*\y+1
	GPST	d3, d6, 1, 2*\y+1
	GPST	d4, d7, 2, 2*\y+1
	.endm

| colour index (the key's bits 16-21) * 12: a row of s_ttMask{Hi,Lo}[64][6]
	.macro	CX12 r, t
	swap	\r
	add.w	\r,\r
	add.w	\r,\r
	move.w	\r,\t
	add.w	\r,\r
	add.w	\t,\r
	.endm

| The general painter's set-up: the six masks into d2-d7 (d4/d6/d7 saved first), then the glyph
| pointers from the keys.
	.macro	GEN_SETUP
	movem.l	d4/d6-d7,-(sp)
	move.l	d0,d2
	CX12	d2, d3
	lea	g_ttMaskHi,a4
	adda.w	d2,a4
	move.l	d1,d2
	CX12	d2, d3
	lea	g_ttMaskLo,a5
	adda.w	d2,a5
	movem.w	(a4),d2-d7
	or.w	(a5)+,d2
	or.w	(a5)+,d3
	or.w	(a5)+,d4
	or.w	(a5)+,d5
	or.w	(a5)+,d6
	or.w	(a5)+,d7
	andi.w	#GLYPH,d0
	lsl.w	#5,d0
	lea	(a6,d0.w),a4
	andi.w	#GLYPH,d1
	lsl.w	#4,d1
	lea	(a3,d1.w),a5
	.endm

	.text
	.even
	.globl	tt_paint_row_m68k
tt_paint_row_m68k:
	movem.l	d2-d7/a2-a6,-(sp)
	lea	-FRAME(sp),sp
	movea.l	ARGS(sp),a2
	movea.l	ARGS+4(sp),a1
	movea.l	ARGS+8(sp),a0
	move.l	ARGS+12(sp),d7
	lea	g_ttGlyphHi,a6
	lea	g_ttFont,a3
	cmpi.l	#2,d7                       | the bottom half starts at source line 5
	bne.s	1f
	lea	10(a6),a6
	addq.l	#5,a3
1:	moveq	#12,d0
	lsl.l	d0,d7                       | half << 12: the key's free bits 12-13
	moveq	#PAIRS-1,d6
	moveq	#0,d4

.Lpair:
	move.l	(a0)+,d0
	or.l	d7,d0
	move.l	(a0)+,d1
	or.l	d7,d1
	cmp.l	(a1)+,d0
	bne.s	.Lmoved_a
	cmp.l	(a1)+,d1
	bne.s	.Lmoved_b
	tst.w	d4                          | an unchanged pair ends a run
	beq.s	.Lnext
	bsr	tt_flush
.Lnext:
	addq.l	#2,a2
	dbra	d6,.Lpair

	tst.w	d4
	beq.s	2f
	bsr	tt_flush
2:	move.l	d6,d0
	clr.w	d0
	swap	d0
	lea	FRAME(sp),sp
	movem.l	(sp)+,d2-d7/a2-a6
	rts

.Lmoved_a:
	addq.l	#4,a1
.Lmoved_b:
	move.l	d0,-8(a1)
	move.l	d1,-4(a1)
	swap	d6
	addq.w	#2,d6
	swap	d6
	move.l	d0,d2                       | blank on black: both codes <= $20, both backgrounds 0
	or.l	d1,d2
	andi.l	#BGMASK,d2
	bne.s	.Lpaint
	moveq	#0x7F,d2
	and.b	d0,d2
	cmpi.b	#0x20,d2
	bhi.s	.Lpaint
	moveq	#0x7F,d2
	and.b	d1,d2
	cmpi.b	#0x20,d2
	bhi.s	.Lpaint
	tst.w	d4                          | ...extends the run (or starts one here)
	bne.s	3f
	move.l	a2,F_RUNAT(sp)
3:	addq.w	#1,d4
	bra.s	.Lnext

.Lpaint:
	tst.w	d4
	beq.s	4f
	bsr	tt_flush
4:	move.l	d0,d2                       | one colour for both cells, on black?
	swap	d2
	move.l	d1,d3
	swap	d3
	cmp.w	d2,d3
	bne.s	.Lgeneral
	cmpi.w	#8,d2
	bcc.s	.Lgeneral
	.if SABOTAGE == 3
	bra.s	.Lgeneral                   | SABOTAGE 3 is a CONTROL: every pair takes the general path
	.endif
	add.w	d2,d2
	add.w	d2,d2
	andi.w	#GLYPH,d0                   | the glyph pointers
	lsl.w	#5,d0
	lea	(a6,d0.w),a4
	andi.w	#GLYPH,d1
	lsl.w	#4,d1
	lea	(a3,d1.w),a5
	tst.l	d7
	bne.s	5f
	jmp	tt_tab_n(pc,d2.w)
5:	jmp	tt_tab_d(pc,d2.w)
.Lgeneral:
	tst.l	d7
	bne	tt_gend
	bra	tt_gen

| The routine tables: one foreground on black each, reached PC-relative (8-bit displacement: they
| must stay right after the dispatch).
	.even
tt_tab_n:
	bra.w	tt_fg0
	bra.w	tt_fg1
	bra.w	tt_fg2
	bra.w	tt_fg3
	bra.w	tt_fg4
	bra.w	tt_fg5
	bra.w	tt_fg6
	bra.w	tt_fg7
tt_tab_d:
	bra.w	tt_fgd0
	bra.w	tt_fgd1
	bra.w	tt_fgd2
	bra.w	tt_fgd3
	bra.w	tt_fgd4
	bra.w	tt_fgd5
	bra.w	tt_fgd6
	bra.w	tt_fgd7

| Flush the pending blank run through the blitter: tt_blit_clear_c(first word, pairs).  C clobbers
| d0/d1/a0/a1, all live here, and keeps d4.  (Called with the frame 4 lower — the return address.)
tt_flush:
	movem.l	d0-d1/a0-a1,-(sp)
	moveq	#0,d0
	move.w	d4,d0
	.if SABOTAGE == 4
	subq.w	#1,d0                       | SABOTAGE 4: a run's last pair is never cleared
	.endif
	move.l	d0,-(sp)
	move.l	F_RUNAT+4+16+4(sp),-(sp)
	jsr	tt_blit_clear_c
	addq.l	#8,sp
	movem.l	(sp)+,d0-d1/a0-a1
	moveq	#0,d4
	rts

	FG_ROUTINE 0
	FG_ROUTINE 1
	FG_ROUTINE 2
	FG_ROUTINE 3
	FG_ROUTINE 4
	FG_ROUTINE 5
	FG_ROUTINE 6
	FG_ROUTINE 7
	FG_DROUTINE 0
	FG_DROUTINE 1
	FG_DROUTINE 2
	FG_DROUTINE 3
	FG_DROUTINE 4
	FG_DROUTINE 5
	FG_DROUTINE 6
	FG_DROUTINE 7

tt_gen:
	GEN_SETUP
	GEN_LINE 0
	GEN_LINE 1
	GEN_LINE 2
	GEN_LINE 3
	GEN_LINE 4
	GEN_LINE 5
	GEN_LINE 6
	GEN_LINE 7
	GEN_LINE 8
	GEN_LINE 9
	movem.l	(sp)+,d4/d6-d7
	bra	.Lnext

tt_gend:
	GEN_SETUP
	GEN_DLINE 0
	GEN_DLINE 1
	GEN_DLINE 2
	GEN_DLINE 3
	GEN_DLINE 4
	movem.l	(sp)+,d4/d6-d7
	bra	.Lnext
