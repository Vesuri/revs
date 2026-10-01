| scan_m68k.s — THE TRANSPOSED SOURCE SCAN IN 68000 REGISTERS (Amiga only; `make SCANASM=0` is the C control)
|
| This is view_scan_all = view_scan_body(0, 79, consume = 1) with the low block's run-B seeding
| (src/gen/revs_native.c), for the default build.  It finds every non-zero source byte of the forty
| $80-spaced blocks at $3000 on the lines the sweep's painters own, consumes it, and appends
| {cell, view_cell_bytes[src]} to that line's event list; then plants each list's $FF sentinel.  The C
| is still the reference: the host runs it, and `make SCANCHECK=1` runs both on the target on the same
| sources and compares every event list, every cursor, the count and all forty blocks.
|
| ⭐ WHAT MAKES IT CHEAP:
|   * the zero path is `move.l (a0)+,d0 / beq.s` — 22 cycles a longword of four lines, unrolled
|     twenty deep and entered by a computed jump at the cell's floor group, so there is no line
|     counter, no trip test and no second cursor;
|   * the floor group's lanes BELOW the floor (the car's interior, which the sweep never consumes and
|     which therefore test non-zero every frame) are masked out of the zero test in a register, so a
|     cell whose only non-zero bytes are the car's costs no call at all;
|   * a hit is not a call: each group has its own out-of-line HIT BLOCK (sc_h<g>_<lane>) in which the
|     lane's line — and so its cursor slot, L*4(a2) — is an assemble-time constant, and which branches
|     straight back to the next group's entry.  The floor group enters its block at the floor's LANE
|     (sc_entry, one word a line), so the lanes below the floor are never looked at and no lane tests
|     the floor.  A zero lane is two instructions.  (Measured before: a `bsr` to one shared routine
|     that saved d1, derived the line from a0 and bumped a line and a slot counter every lane, ~110
|     cycles a hit at ~109 hits a sweep.)
| ⚠ THE FLOOR IS A CORRECTNESS BOUNDARY (s_lowConsume's banner): below it the bytes are live producer
|   state, never consumed and never recorded.  A floor >= 80 scans nothing, exactly as the C's
|   `q < qe` loop does (44 before view_low_build has run: the low block is the chain's, phase 1 ours).
|
| unsigned view_scan_m68k(uint8_t *src, ViewSpan *ev, ViewSpan **evEnd, const uint8_t *floor,
|                         const uint8_t *xlat, const uint8_t *seedHead, const uint8_t *seedNext,
|                         const uint8_t *rstart, uint8_t *seedPos)
|   src      mem + $3000 (view_src_blocks)         ev/evEnd  g_viewEv / g_viewEvEnd
|   floor    s_lowConsume                           xlat      mem + $6000 (view_cell_bytes)
|   seedHead/seedNext  s_lowSeedHead / s_lowSeedNext          rstart    mem + $4400 (view_right_start_src)
|   seedPos  s_lowSeedPos, or 0 when LOWFULLCHECK is off
|   returns  the number of sources found (g_viewEvents' increment)
| Registers:
|   a0 source cursor (the seed loop's scratch)   a1 this cell's block   a2 evEnd   a3 xlat
|   a4 an event cursor    a5 floor cursor    a6 seedHead cursor
|   d0 the longword / scratch   d1 d2 d4 scratch   d3.w a hit's source byte (high byte kept 0)
|   d5.w this cell's floor   d6.b the cell   d7.w found

	.equ	EVSTRIDE, 96                | sizeof g_viewEv[0]
	.equ	LINES,    80
	.equ	CELLS,    40
	.equ	ARGS,     44+4
	.equ	A_SRC,      ARGS+0
	.equ	A_EV,       ARGS+4
	.equ	A_EVEND,    ARGS+8
	.equ	A_FLOOR,    ARGS+12
	.equ	A_XLAT,     ARGS+16
	.equ	A_SEEDHEAD, ARGS+20
	.equ	A_SEEDNEXT, ARGS+24
	.equ	A_RSTART,   ARGS+28
	.equ	A_SEEDPOS,  ARGS+32

| `--defsym SABOTAGE=N` (`make SCANCHECK=1 SCANASM_SABOTAGE=N`): a deliberate defect SCANCHECK must
| catch.  0 in every real build.
	.ifndef SABOTAGE
	.equ	SABOTAGE, 0
	.endif
| `--defsym GROUPS=n` (`make SCANGROUPS=n`): scan lines 0..4n-1 only.  20 (all 80 lines) in every
| real build; fewer is a carve arm that prices the rest (the picture above line 4n-1 is wrong).
	.ifndef GROUPS
	.equ	GROUPS, 20
	.endif

| One lane of a hit block: byte J of the longword a0 has just passed, line L.  d3's high byte is 0.
.macro LANE l, j
	move.b	-4+\j(a0),d3
	beq.s	8f
	.if SABOTAGE != 2
	clr.b	-4+\j(a0)                   | consume (SABOTAGE 2: the source survives)
	.endif
	move.l	\l*4(a2),a4
	move.b	d6,(a4)+
	.if SABOTAGE == 3
	move.b	d3,(a4)+                    | SABOTAGE 3: the raw source, untranslated
	.else
	move.b	(a3,d3.w),(a4)+
	.endif
	move.l	a4,\l*4(a2)
	addq.w	#1,d7
8:
.endm

	.section .text.view_scan_m68k,"ax",@progbits
	.even
	.globl	view_scan_m68k
	.type	view_scan_m68k, @function
view_scan_m68k:
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	A_EVEND(sp),a2
	move.l	A_XLAT(sp),a3

	| every line's cursor to its list's head
	move.l	a2,a0
	move.l	A_EV(sp),a4
	moveq	#LINES-1,d1
1:	move.l	a4,(a0)+
	lea	EVSTRIDE(a4),a4
	dbra	d1,1b

	move.l	A_SRC(sp),a1
	move.l	A_FLOOR(sp),a5
	move.l	A_SEEDHEAD(sp),a6
	moveq	#0,d6
	moveq	#0,d7

sc_cell:
	moveq	#0,d3                       | a hit block's byte index (the seeds dirty it)
	moveq	#0,d5
	move.b	(a5)+,d5                    | the cell's floor: its first consumable line
	moveq	#GROUPS*4,d0
	cmp.w	d0,d5
	bcc.w	sc_seeds                    | $FF: no table yet, nothing is ours
	moveq	#-4,d1
	and.w	d5,d1                       | the floor's group
	lea	(a1,d1.w),a0
	move.l	(a0)+,d0                    | the floor group, its lanes below the floor masked off
	moveq	#3,d2
	and.w	d5,d2
	add.w	d2,d2
	add.w	d2,d2
	and.l	sc_floormask(pc,d2.w),d0    | ⚠ a SPEED mask only: the block is entered AT the floor's
	                                    |   lane, so a wrong mask changes no output (not a sabotage)
	beq.s	1f
	lea	sc_entry(pc),a4
	add.w	d5,d5
	.if SABOTAGE == 1
	and.w	#-8,d5                      | SABOTAGE 1: entered at lane 0 — the car's lanes are consumed
	.endif
	move.w	(a4,d5.w),d5
	jmp	(a4,d5.w)                   | the floor's lane of the floor group's hit block
1:	add.w	d1,d1                       | the next group's entry: 8 bytes of code a group
	.if SABOTAGE == 5
	jmp	sc_groups+16(pc,d1.w)       | SABOTAGE 5: the group after the floor's is skipped
	.else
	jmp	sc_groups+8(pc,d1.w)
	.endif

sc_floormask:
	.long	0xFFFFFFFF, 0x00FFFFFF, 0x0000FFFF, 0x000000FF

| ⚠ exactly 8 bytes a group and sc_seeds straight after: the floor path's computed jump and every hit
|   block's return (sc_groups + 8*(g+1)) both count on it.
sc_groups:
	.irp	g,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19
	.if \g < GROUPS
	move.l	(a0)+,d0
	.if SABOTAGE == 6 && \g == 15
	bra.s	2f                          | SABOTAGE 6: lines 60..63 are never looked at
	.else
	beq.s	2f
	.endif
	bra.w	sc_h\g\()_0
2:
	.endif
	.endr
	| (the entry for a floor in the last group lands here, past the table)

| run B's entry for the lines whose run starts at this cell, after the cell's own sources
sc_seeds:
	moveq	#0,d1
	move.b	(a6)+,d1                    | the first line in this cell's chain
	cmp.b	#0xFF,d1
	beq.s	sc_nextcell
sc_seed:
	move.w	d1,d2
	add.w	d2,d2
	add.w	d2,d2                       | its cursor slot
	move.l	(a2,d2.w),a4
	cmp.b	-2(a4),d6                   | ⚠ an EMPTY list reads the previous list's last slot, which
	beq.s	5f                          |   view_low_build guards with $FF — never a cell.  Else a
	                                    |   real event already starts at this cell: it LOSES (5:)
	move.b	d6,(a4)+
	move.l	A_RSTART(sp),a0
	.if SABOTAGE == 4
	move.b	1(a0,d1.w),(a4)+            | SABOTAGE 4: the NEXT line's entry byte
	.else
	move.b	(a0,d1.w),(a4)+
	.endif
	move.l	a4,(a2,d2.w)
	move.l	A_SEEDPOS(sp),d0
	beq.s	4f
	move.w	d1,d3                       | LOWFULLCHECK only: the seed's slot in its list
	lsl.w	#5,d3
	move.w	d3,d4
	add.w	d3,d3
	add.w	d4,d3                       | line * 96
	move.l	a4,d4
	subq.l	#2,d4
	sub.l	A_EV(sp),d4
	sub.w	d3,d4
	move.l	d0,a0
	lsr.w	#1,d4
	move.b	d4,(a0,d1.w)                | where the seed went
4:	move.l	A_SEEDNEXT(sp),a0
	move.b	(a0,d1.w),d1
	cmp.b	#0xFF,d1
	bne.s	sc_seed
	bra.s	sc_nextcell
| ⚠⚠ the chain enters run B at unit+$05, which consumes the entry cell's source UNREAD
|   (revs_native.c view_low_run §forced): the event there takes the entry byte's colour
5:	move.l	A_RSTART(sp),a0
	move.b	(a0,d1.w),-1(a4)
	bra.s	4b

sc_nextcell:
	lea	0x80(a1),a1
	addq.b	#1,d6
	cmp.b	#CELLS,d6
	bne.w	sc_cell

	| every list's sentinel, where its cursor stopped
	move.l	a2,a0
	moveq	#LINES-1,d1
5:	move.l	(a0)+,a4
	st	(a4)
	dbra	d1,5b

	moveq	#0,d0
	move.w	d7,d0
	movem.l	(sp)+,d2-d7/a2-a6
	rts

| The hit blocks: group g's four lanes, then back to group g+1's entry (g = 19: sc_seeds).  a0 is
| just past the group's longword.  Clobbers d3 (low byte), a4.
	.irp	g,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19
	.if \g < GROUPS
	.irp	j,0,1,2,3
sc_h\g\()_\j:
	LANE	(\g*4+\j),\j
	.endr
	bra.w	sc_groups+8*(\g+1)
	.endif
	.endr

| sc_entry[line]: the offset of that line's lane in its group's hit block — the floor group's way in.
sc_entry:
	.irp	g,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19
	.if \g < GROUPS
	.irp	j,0,1,2,3
	.word	sc_h\g\()_\j - sc_entry
	.endr
	.endif
	.endr
	.size	view_scan_m68k, .-view_scan_m68k
