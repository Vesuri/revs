#ifndef BBC_SCREEN_H
#define BBC_SCREEN_H
/* bbc_screen.h — THE DISPLAY MODEL.  One copy, both backends.
 *
 * Everything here is DERIVED from the binary and then confirmed by decoding a real frame
 * out of a running host build (tools/screen_ppm.py; the picture is the steering wheel,
 * the dials, the mirrors and the road, so the geometry is not a guess).  ⚠ What is NOT
 * yet confirmed against a real BBC is the raster PHASE — see kBandAnchorLine below.
 *
 * ── GEOMETRY ────────────────────────────────────────────────────────────────────────
 * hw_init ($4DDD) writes the 6845 from the 14-byte table at $4F0F:
 *
 *     R0 =63  R1 =40  R2 =49  R3 =$24  R4 =38  R5 =0  R6 =26
 *     R7 =32  R8 =1   R9 =7   R10=$67  R11=8   R12/R13=$0B50
 *
 *   R12/R13 * 8            = $5A80   screen base
 *   R1                     = 40      bytes per character row
 *   R6                     = 26      character rows displayed
 *   R9 + 1                 = 8       scan lines per character row  => 208 lines
 *   26 * 320               = 8320    bytes; $5A80 + 8320 = $7B00 exactly, which is where
 *                                    the $7B00-$7FFF code overlay begins.  The frame
 *                                    buffer ends where the code page starts.
 *   R4 + 1 = 39 rows, R8=1 (interlace sync) => 312.5 lines/field = 20000 us
 *   R7 = 32                => vsync at row 32
 *
 * Byte order is the BBC character-cell layout, NOT linear scanlines:
 *
 *     offset = charRow*320 + cell*8 + lineInRow
 *
 * ── PIXELS ──────────────────────────────────────────────────────────────────────────
 * The Video ULA serialises a byte through a shift register, emitting a 4-bit palette
 * index built from bits (7,5,3,1) and shifting left with 1s fed in.  So a pixel's own
 * bits land in index bits 3 and 1, while index bits 2 and 0 hold the FOLLOWING pixels'
 * bits — i.e. they are don't-care.  The game's own palette tables prove it: $3458 writes
 * sixteen entries in four groups of four, {0,1,4,5} {2,3,6,7} {8,9,12,13} {10,11,14,15},
 * which is exactly "bits 3 and 1 significant, bits 2 and 0 free".
 *
 *   MODE 5 ($FE20 = $C4): 4 px/byte, colour = bit(7-p)*2 + bit(3-p)   -> 4 colours
 *   MODE 4 ($FE20 = $88): 8 px/byte, colour = bit(7-p)                -> 2 colours
 *
 * Both read the same 40 bytes per line, so a MODE 5 pixel is simply twice as wide.  The
 * Amiga display is therefore 320x208, two bitplanes, and a MODE 5 byte expands to one
 * byte per plane (four pixels doubled); the nibbles are already the two planes —
 * bits 7-4 are plane 1 and bits 3-0 are plane 0, for the same four pixels.
 *
 * ── COLOUR IS A FUNCTION OF RASTER POSITION ─────────────────────────────────────────
 * There is one static screen mode and five palette/mode bands per field, written by
 * irq1v_handler ($4E5C).  Measured band cycle (durations are the User VIA T1 latch the
 * handler reloads, +2 for the 6522's reload, in microseconds = 1/64 line):
 *
 *   band 0  MODE 4, palette $3468 (all 16: 0-7 blue, 8-15 yellow)     4038 us  63.1 ln
 *   band 1  MODE 5, sixteen identical entries -> flat blue            variable  = SKY
 *   band 2  MODE 5, palette $3458 (black / blue / white / green)      the remainder of
 *                                                                    5438 us  84.9 ln
 *   band 3  four entries $3478: colour 1 -> red                       7682 us 120.0 ln
 *   band 4  four entries $347C: colour 3 -> cyan, then the GAME BODY   2840 us  44.4 ln
 *                                                                   ------------------
 *                                                                    20000 us 312.5 ln
 *
 * ⭐⭐ THE SKY IS A HIDING PLACE FOR LIVE CODE, and that is not a metaphor.  $5E40-$66FF —
 * 5.5 KB of engine variables AND executable routines — sits inside the frame buffer, on
 * display, under band 1.  All sixteen of band 1's palette entries are the same blue, so
 * the running code renders as flat sky whatever its bytes say.  Nothing clears it and
 * nothing can: it is the program.  (Measured here first: the frame-buffer rows over that
 * range are byte-identical to the boot image except where live variables churn, and
 * disasm/listing.txt disassembles 462 instructions inside them.)
 *
 * ⚠ THAT IS ALSO WHY THE RASTER PHASE MATTERS TO THE PIXEL.  Get the band boundaries
 * wrong by a few lines and the port shows the engine's own code as noise where the sky
 * belongs.  The phase below is therefore measured two independent ways.
 *
 * The interval boundaries, in display lines (line 0 = the first displayed line):
 *
 *   band 0   -26.4 ..  18.0    MODE 4 — the two text rows at the top of the screen.  The
 *                              loader blanks the first three character rows (24 lines), so
 *                              the mode switch itself happens over zeroed memory.
 *   band 1    18.0 ..  81.1    the blue sky, and the code hiding in it
 *   band 2    81.1 .. 100.5    horizon + the cars' rear wings (black/blue/white/green)
 *   band 3   100.5 .. 166.1    the track (blue -> RED)
 *   band 4   166.1 .. 286.1    the dashboard (green -> CYAN); runs off the bottom of the
 *                              208-line display at 208 and spends the rest in the blank
 *
 * ⚠ THE LATCH IS PIPELINED, and this is the one place the record needs interpreting: the
 * 6522 reloads T1 from the latch at timeout, so the value the handler writes during band
 * n governs the interval AFTER the next interrupt, i.e. it is band n+1's duration.  The
 * record therefore holds D(n+1) against band n, and the interval [interrupt n, interrupt
 * n+1] is the duration recorded against band n-1.  Recorded (SILVER, level track):
 * 2840 4038 1242 4198 7682 us against bands 4 0 1 2 3, summing to exactly 20000.
 *
 * Two independent confirmations of the phase, which is why it is [DERIVED], not fitted:
 *   1. The CONTENT.  In a decoded race frame the ground (green) begins at line 100 and the
 *      drawn blue sky ends there; band 3 starts at 100.5.  The code region ends at line 80
 *      and band 1 ends at 81.1.  The blank rows are lines 0-23 and band 0 covers 0-18.
 *   2. The reference reconstruction's write-up of the custom mode (a map, never a source —
 *      docs/reference-sources.md) gives the five band heights as 18/64/19/66/41 visible
 *      lines, which is the same table, and names the memory hidden in the sky.
 *
 * Band 2's duration is the HORIZON — `MoveHorizon` ($4F44, called from the main loop at
 * $173F) computes it as $04D8 +/- 64*(a clamped function of the pitch variable $1F), i.e.
 * plus or minus whole scan lines, and band 3 takes the remainder of a fixed $153C.  So the
 * sky/track split MOVES WITH THE HILLS, which is exactly why this model records what the
 * handler wrote rather than freezing a table.
 */

#define BBC_SCREEN_BASE     0x5A80u
#define BBC_SCREEN_CELLS    40u          /* bytes per character row */
#define BBC_SCREEN_ROWS     26u          /* character rows */
#define BBC_SCREEN_LINES    8u           /* scan lines per character row */
#define BBC_SCREEN_BPR      (BBC_SCREEN_CELLS * BBC_SCREEN_LINES)   /* 320 bytes/row */
#define BBC_SCREEN_BYTES    (BBC_SCREEN_ROWS * BBC_SCREEN_BPR)      /* 8320 */
#define BBC_SCREEN_HEIGHT   (BBC_SCREEN_ROWS * BBC_SCREEN_LINES)    /* 208 lines */
#define BBC_SCREEN_WIDTH    320u         /* in MODE 4 pixels; MODE 5 doubles */

/* ULA control values the handler writes ($FE20). */
#define BBC_ULA_MODE4       0x88u
#define BBC_ULA_MODE5       0xC4u

/* One microsecond of User VIA T1 is 1/64 of a 64 us scan line. */
#define BBC_US_PER_LINE     64u

/* ⭐⭐ WHICH DISPLAY LINE A BAND FIRST OWNS: CEILING, never round-to-nearest.
 *
 * A band boundary lands at a fractional line (81.094, 100.5, 166.094 below) because the chain
 * is timed in microseconds, not lines.  The band that STARTS at 81.094 does not own line 81 —
 * line 81 is still the previous band's, because the previous band was already displaying when
 * the timer fired partway through it.  So the first line the new band owns is ceil().
 *
 * [DERIVED, and cross-checked against the reference reconstruction's visible band heights of
 * 18/64/19/66/41 lines, which give boundaries 18 / 82 / 101 / 167.]  ceil reproduces all four
 * exactly.  Round-to-nearest gives 18 / 81 / 101 / 166 — TWO of the four wrong by a line.
 *
 * ⚠ THAT OFF-BY-ONE IS VISIBLE, not academic, and it is why this is a macro in the shared
 * model rather than an expression in one backend.  Line 81 is the last line of the engine code
 * and variables that live inside the frame buffer ($5E40-$66FF) and are invisible ONLY because
 * band 1 maps all sixteen palette entries to the same blue.  Hand line 81 to band 2, whose pen
 * 0 is BLACK, and 248 of its 320 pixels turn into a black bar across the sky.  Measured on the
 * target, 2026-08-14: exactly that bar, and it disappears with ceil.
 *
 * `(us + 63) >> 6` is floor((us+63)/64) = ceil(us/64) for both signs — the >> is an arithmetic
 * shift, so it floors on negatives too, which band 0's pre-display anchor needs.  A shift and
 * not a divide: the 68000 has no 32-bit divide (make muldiv-audit). */
#define BBC_US_TO_FIRST_LINE(us)  (((int)(us) + (int)BBC_US_PER_LINE - 1) >> 6)

/* The band record, filled by src/platform/bbc_hw.cpp (see the long comment there). */
#define BBC_MAX_BANDS       8u

#ifdef __cplusplus
extern "C" {
#endif
extern volatile unsigned char  g_bandCount;
extern volatile unsigned char  g_bandOverflow;
extern volatile unsigned short g_bandDuration[BBC_MAX_BANDS];
extern volatile unsigned char  g_bandControl[BBC_MAX_BANDS];
extern volatile unsigned char  g_bandState[BBC_MAX_BANDS];
extern volatile unsigned char  g_bandPalette[BBC_MAX_BANDS][16];
extern volatile unsigned char  g_ulaControl;
extern volatile unsigned char  g_ulaPalette[16];
/* Fields that ran the real five-interrupt cycle vs fields that reused the record it would
   have rebuilt (bbc_hw.cpp §fireIrq1vField).  ⚠ Read BOTH before believing any measurement of
   the reuse: a skip rate of 0 looks exactly like a correct fast path to every byte-differential
   in this repo, and that is how its first sabotage passed. */
extern volatile unsigned long  g_bandSkips;
extern volatile unsigned long  g_bandRuns;
/* `make BANDCHECK=1` only — the oracle's tally.  Mismatch must be 0 with Checks large. */
extern volatile unsigned long  g_bandCheckChecks;
extern volatile unsigned long  g_bandCheckMismatch;
/* Fields in which the horizon moved — the stimulus that makes a reuse test non-trivial. */
extern volatile unsigned long  g_bandHorizonMoves;
/* Call immediately before dispatching one field's worth of bands, so the record always
   describes a whole field rather than a half-written one. */
void bbc_begin_band_cycle(void);
#ifdef __cplusplus
}
#endif

/* ⭐ THE VIDEO ULA WRITE, AS ONE DEFINITION USED BY TWO CALLERS.
 *
 * Platform::hwWrite's $FE20/$FE21 arms are these two lines, and so is the native twin of
 * irq1v_handler (src/gen/revs_native.c) — which issues 48 palette writes per field and is
 * 51% of the frame, so it reaches the model directly instead of paying a C-bridge call, a
 * virtual dispatch and a 12-case switch per byte.
 *
 * ⚠ It is an INLINE SHARED WITH hwWrite rather than a copy of it, deliberately: two
 * hand-written statements of "what $FE21 means" is exactly how the model and the fast path
 * drift, and nothing in `make validate` would see it (the differential diffs mem[], and the
 * ULA is not in mem[]).  The band record's $FE66 arm, which snapshots g_ulaPalette, stays in
 * hwWrite — it runs once per band, not 16 times.
 * ⚠ It also bypasses the REVS_PROBE_HWTIME counters, which therefore under-count the palette
 * writes in a twin build.  That instrument exists to price ONE access, not to total them. */
#ifdef __cplusplus
extern "C" {
#endif

/* ⭐⭐ AND THE REASON THE FAST PATH STILL ANNOUNCES ITSELF.  `make validate` diffs the
 * hardware writes a twin makes against the ones its 6502 oracle makes, as a sequence
 * (tools/validate_native.c) — which is the only way a display routine's output is visible
 * at all, since the ULA is not in mem[].  A fast path that skipped the trace would be a
 * twin validated on the four bytes it happens to leave in RAM.  So the trace hook lives
 * HERE, in the shared definition, and Platform::hwWrite's $FE20/$FE21 arms therefore do
 * NOT trace (they reach the hardware through these two functions and would double-log) —
 * every other register traces at the hwWrite override.  ⚠ Keep those two halves in step.
 * Host-only: REVS_HW_TRACE is on for the whole host build (which exists for the
 * differential, not for speed) and off on the Amiga, where this compiles to nothing. */
#ifdef REVS_HW_TRACE
void bbc_hw_trace(unsigned short addr, unsigned char val);
#define BBC_HW_TRACE(a, v) bbc_hw_trace((unsigned short)(a), (unsigned char)(v))
#else
#define BBC_HW_TRACE(a, v) ((void)0)
#endif

/* $FE21: high nibble = logical colour, low nibble = the (inverted) physical colour. */
static inline void bbc_ula_palette_write(unsigned char v) {
    BBC_HW_TRACE(0xFE21, v);
    g_ulaPalette[(v >> 4) & 0x0F] = v;
}
/* $FE20: screen mode, flash, cursor width. */
static inline void bbc_ula_control_write(unsigned char v) {
    BBC_HW_TRACE(0xFE20, v);
    g_ulaControl = v;
}
#ifdef __cplusplus
}
#endif

/* ── THE RASTER ANCHOR ───────────────────────────────────────────────────────────────
 * The chain is self-timed, so ONE absolute line fixes all five bands: band 0's interrupt,
 * which lands 26.4 lines BEFORE the first displayed line (inside the previous field's
 * blanking), so that band 1 begins at line 18.  Expressed in 1/64-line units so the
 * fractional part survives, because it is what keeps the accumulated boundaries within a
 * line of the measured ones.
 *
 * ⚠ Deriving this from hw_init's start-up arithmetic instead gives ~78, and that is
 * WRONG — see the two confirmations above.  The suspect step is which edge of vsync
 * latches the System VIA CA1 flag that $4E11 spins on.  Left as a note rather than
 * chased, because the phase is now pinned by content and by the reference; a real-machine
 * beam measurement (docs/bbc-reference-loop.md step 5) would close it properly.
 */
#define BBC_BAND0_ANCHOR_US   (-26 * (int)BBC_US_PER_LINE - 24)   /* -26.375 lines */

#endif /* BBC_SCREEN_H */
