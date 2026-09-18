#ifndef REVS_PLOT_H
#define REVS_PLOT_H
/* revs_plot.h — DIRECT-TO-BITPLANE PLOTTING for the 3D view rasteriser ($7BE2).
 *
 * ⭐⭐ WHY THIS EXISTS, AND WHY IT IS RUNS AND NOT CELLS.  `docs/direct-bitplane-plan.md` §7d/§7e:
 * measured on a real BBC, `$7BE2` writes display lines 80..157 — the viewport, not the dashboard —
 * at 2148 stores a frame, and `A` carries between its units, so a cell whose source byte is zero
 * repeats whatever the cell to its LEFT drew.  With ~83 non-zero sources across ~77 scan lines,
 * **a line is one to three RUNS of identical bytes.**
 *
 * In the BBC's layout those runs are strided by 8 and can only be written a byte at a time — which
 * is why the 6502 does exactly that, and why the transliteration inherits ~2100 iterations of a
 * ~30-instruction unit (131 ms, instruction-fetch bound in chip RAM).  In the Amiga's interleaved
 * bitplane layout the same run is CONTIGUOUS, so it is one fill: ~2100 iterations become ~150.
 * ⭐ That is the win.  A plotter that merely mirrored each store into bitplanes would be a LOSS,
 * because the dirty-region decode (§7c) already skips the unchanged cells such a mirror would
 * redundantly convert.
 *
 * ⚠ THE LAYOUT IS TODAY'S, DELIBERATELY (§7e): 320x208, two interleaved planes, plane 1 at +0 and
 * plane 2 at +40, `kRowBytes` 80, same palette, same copper.  Nothing about the display changes —
 * only who writes it — which is what lets `RevsScreen::decode()` stay the ORACLE unmodified.
 *
 * ⚠ Amiga only, and it compiles to nothing everywhere else: `make validate` and `make determinism`
 * run on the host, where the sweep keeps writing mem[] and nothing here is defined.
 */

/* ⚠ NO <stdint.h> HERE.  This header is included by RevsScreen.cpp, which includes the Amiga
   SDK's own headers first, and the freestanding build's compat stdint.h then redeclares int8_t
   against exec/types.h — a hard error.  The prototypes use the underlying types instead, which are
   the same types on this target (m68k ILP32, char 8 / short 16 / long 32). */

/* MEM_QUAL only — a lone #define, so it brings none of the stdint trouble above with it.  The
   chain painter below takes a `mem[]` pointer across the TU boundary and the qualifier has to be
   the same on both sides of it (`make BODY_IN_ISR=1` makes it `volatile`). */
#include "../cpu/mem_decl.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef REVS_DIRECT_PLOT

/* Per painted frame, from RevsScreen: which bitplane buffer the plotter paints into.  Null
   disables plotting for the frame (before the first decode there is no target, and the MODE 7
   front end has no race buffer at all). */
void revs_plot_target(unsigned char* planeBase);

/* ⭐⭐⭐ BOTH PLANE BUFFERS, ONCE AT START-UP — A DIFFERENT CONTRACT FROM `revs_plot_target`.
 * A span painter repaints its whole display line every sweep, so the buffer the next decode will
 * fill is all it needs.  The GLYPH painter below is a DELTA painter: it maintains the ~3.7 bytes a
 * frame that change on rows nothing else on the screen touches, so a byte put into one buffer
 * alone would be missing from the other FOR EVER — the two alternate on screen and no pass
 * repaints them.
 * ⚠⚠ That is the general rule and it is measured, not aesthetic: a mirrored painter whose state
 * is CROSS-FRAME must write BOTH plane buffers; "the back buffer is enough" holds only for a
 * painter that repaints a whole row (CLAUDE.md, docs/span-render-plan.md §11b). */
void revs_plot_planes(unsigned char* planeA, unsigned char* planeB);

/* THE BAND RECORD'S MODE PER BAND, published once per decode by `RevsScreen::buildLineModes()`.
 * `firstLine[n]` is band n's first display line (band 0's is NEGATIVE — it starts before the
 * display) and `mode[n]` is 4 or 5.
 * ⚠⚠ THE GLYPH PAINTER MAY NOT READ `m_lineMode`, WHICH IS WHY THIS EXISTS.  That table has
 * already had the owned lines zeroed (mode 0 = "write nothing") by the time any painter runs, and
 * the flat-band test zeroes more of it besides — while the painter must put the pixels in
 * whatever the palette says today, because on an owned row NOTHING will ever re-expand them from
 * mem[].  A flat band is a palette fact about what is VISIBLE, not about what is stored. */
void revs_plot_bands(const short* firstLine, const unsigned char* mode, unsigned count);

/* One run of `cells` identical MODE 5 bytes starting at BBC frame-buffer address `addr`, i.e.
   addr, addr+8, addr+16, ... — contiguous plane bytes on this side. */
void revs_plot_run(unsigned short addr, unsigned char value, unsigned short cells);

/* A single cell (the phase drivers' composed boundary bytes). */
void revs_plot_cell(unsigned short addr, unsigned char value);

/* ⭐⭐ ONE WHOLE DISPLAY LINE, AND THE PLOTTER NOW *OWNS* IT (docs/span-render-plan.md §10j).
 * Same fill as revs_plot_run, plus a claim: `g_plotOwn[display line] = 1`.  A BBC row base has
 * `(base - $5A80) % 320` in 0..7, so all forty of a source line's cells (`base + i*8`) land on
 * the SAME display line — one span is exactly one line, which is why ownership can be per line
 * and needs no character-row alignment.
 * ⚠ It claims nothing when there is no target: no buffer was painted, so `decode()` must still
 * convert the line out of mem[].
 *
 * ⭐⭐⭐ NO CELL COUNT, AND THAT IS LOAD-BEARING.  A span is a WHOLE display line — forty cells
 * from cell 0 — so the count is structural, not a parameter, and the emitter's
 * `view_stop_from(0) == 40` is its proof.  Passed as an argument it was a value GCC could not
 * fold across the TU boundary, and the generic `revs_plot_run` it forced the call into priced
 * the fill at ~17 cycles a byte against a 3-cycle store floor (RevsPlot.cpp has the decomposition
 * and the rejected movem.l arithmetic). */
void revs_plot_span(unsigned short addr, unsigned char value);

/* ⭐⭐⭐ ...AND THE LINE THAT IS *NOT* ONE FLAT RUN — THE §10p TAKEOVER (step 3b).
 *
 * `revs_plot_span` can only take a line the sweep would have painted in one colour, which is 21 of
 * phase 1's 36.  The other 15 still went through the chain: forty `mem[]` stores, then the decode
 * converting them back out again.  This paints them straight into the two bitplanes instead, which
 * is the whole architecture — **the renderer owns the line, and `mem[]` is not in the path at all.**
 *
 * It IS the chain, in the chain's own terms: forty cells of one source line at `srcp`, `$80` apart,
 * `view_consume`'s RLE (a zero source repeats the byte to its left, a non-zero one is translated
 * through `cellBytes` = `view_cell_bytes` and then ZEROED), and the carried byte comes back so the
 * caller can thread it into the next line exactly as unit 39 would have left it.
 * ⭐ The RLE is what makes it cheap: the two expansion lookups happen once per RUN — two or three
 * times a line — not once per cell.  A cell is then a source test and two byte stores.
 *
 * ⚠⚠ UNDER `REVS_SPAN_VERIFY` IT DOES NOT CONSUME.  The oracle build keeps the chain running so
 * `mem[]` stays a valid reference, and two consumers of one destructive read is one too many: the
 * chain would find zeroed sources and paint a flat line into the very bytes it is being compared
 * against.  The caller drops the return value in that build for the same reason.
 *
 * ⚠ The caller must have checked `revs_plot_has_target()` — there is no buffer to paint into
 * otherwise, and unlike a span there is no cheap way to put the line back. */
unsigned char revs_plot_chain(unsigned short addr, unsigned char value,
                              MEM_QUAL unsigned char* srcp,
                              MEM_QUAL const unsigned char* cellBytes);

/* ⭐⭐ IS THERE A BUFFER TO PAINT INTO THIS FRAME?  Asked ONCE PER SWEEP, not per line — the
 * target is set once per painted frame in `present()` — and the answer is what licenses ownership
 * (`paint_cells`'s `mayOwn`).
 * ⚠⚠ IT IS A CORRECTNESS GATE, NOT AN OPTIMISATION.  A claimed line is painted by the renderer
 * ALONE: the forty chain stores are gone, so `mem[]` no longer holds that line either.  With no
 * target the fill is dropped, and a build that still skipped the chain would leave the line painted
 * by nobody — `g_plotNoTarget` counted exactly that and nothing acted on it.  MODE 7 and the frames
 * either side of it are when this is 0. */
int revs_plot_has_target(void);

/* ⭐⭐⭐ WHICH DISPLAY LINES THE SPAN EMITTER PAINTED THIS SWEEP — the ownership publication, and
 * the thing the scaffold could not express.  `g_plotLineLo/Hi` + whole-character-row skipping
 * cannot: a BBC cell is EIGHT display lines, so converting one cell of a partly-owned row paints
 * frozen mem[] bytes straight over emitted lines.  `RevsScreen::decode()` reads this and zeroes
 * `m_lineMode[y]` for an owned line, which the decode already understands as "write nothing"
 * (revs_expand_cell), skips a fully-owned character row on wholesale (`any`), and re-expands on
 * the frame a line stops being owned (`modeChanged`).
 * ⚠ CLEARED AT THE TOP OF `view_paint_lines_core`, NOT in present() or decode(): the crash hold
 * calls platform_render_frame() extra times with no sweep in between, and clearing per decode
 * would let those repaint stale mem[] over the spans. */
/* ⚠ `aligned(4)` is part of the CONTRACT, not a hint: the decode scans these flags as
 * longwords (probe.h §DECODESPLIT).  It is spelled on the definition too. */
extern unsigned char g_plotOwn[208] __attribute__((aligned(4)));  /* = BBC_SCREEN_HEIGHT */
void revs_plot_own_reset(void);

#ifdef REVS_PLOT_DELTA
/* ⭐⭐⭐ THE DELTA DOMAIN — display lines 0..17 AND 192..207, OWNED OUTRIGHT (§11b/§11c/§11d).
 *
 * WHY THESE 34 ROWS.  The measured writer-set ledger (`make fbwrites FILL=all`) groups all 208
 * display lines by the SET of routines that write them, and these two blocks are where that set
 * is a SINGLE routine at a handful of stores a frame:
 *
 *   0..17    the two MODE 4 text rows                      `vdu_char_emit`, ~3.7 st/f between
 *   192..207 the bottom two dash text rows                  them — and 8 of the 34 have NO writer
 *
 * ⭐⭐⭐ RANK A DOMAIN BY ROWS OWNED / WRITER SET, never by the prize alone: owning a row means
 * retargeting every routine that writes it.
 * ⛔⛔⛔ AND THE STORE RATE IS NOT A TIE-BREAKER, IT IS THE WHOLE DECISION — display lines
 * 158..191, the needles and lamps, were built as a third block of this same domain and measured
 * a NET LOSS (+5.41 ms of phase 32 against the −2.79 ms of decode they own).  ⭐⭐⭐ The rule that
 * falls out, and the one to apply before adding any block here: the BUDGET is the decode the rows
 * delete, ~544 cycles a row (40 cells x 13.6 cyc/mem[] byte, the 6.87 cyc/plane-byte fill rate of
 * §10n), and the COST is ~666 cycles per byte the painter DELIVERS — 261 of plane work and ~405 of
 * getting there — so **a row pays for a delta painter only below ~0.8 delivered bytes a frame.**
 * These rows are at 3.7/34 = 0.11; the needles are at 57.6/34 = 1.69.  §11d has the three-arm
 * table, the two placements that lose by two different mechanisms, and the ONE domain shape that
 * could still collect those rows.
 *
 * HOW IT WORKS, and there are only two moving parts whatever the domain:
 *   1. THE BASE.  `revs_plot_own_reset()` expands all 34 rows of mem[] into BOTH buffers once,
 *      the first sweep it has planes and a band record.  An owned row is nearly all cells that
 *      nothing rewrites within a frame, so a delta painter is only valid once the planes already
 *      hold that static base.  ⛔ This is a buffer-INITIALISATION flag, not a dirty map —
 *      nothing per cell, nothing per frame, nothing the writers maintain (CLAUDE.md §writer-
 *      maintained dirty maps).  `deltaBaseStale` re-lays it only when an owned row's MODE moves.
 *   2. THE DELTA.  `revs_plot_byte` from the writer's own store site, into both buffers.  It
 *      filters by display line, so a writer whose range straddles the edge needs no bounds test
 *      of its own — ⚠ but that filter is NOT free, and paying it on out-of-domain stores is most
 *      of what closed the needle block above.
 *
 * ⚠⚠ BOTH BUFFERS, ALWAYS.  A writer whose erase is cross-frame stateful — `undraw_plot_lines`
 * restores bytes saved when the needle was drawn LAST frame — would otherwise leave the mark from
 * two frames ago standing in the other buffer (§11b fact 1; the glyph domain's sabotage of
 * exactly this fires at 6888).  It holds for `vdu_char_emit` too: a glyph cell is not repainted
 * every frame, so one buffer's copy would never be refreshed.
 *
 * ⭐ AND THE `mem[]` STORES ALL STAY.  Owning a row while still writing mem[] needs NO reader
 * gate — `REVS_FB_POISON` is owed only by the step that DELETES the stores — so `make validate`
 * and every `determinism` trajectory are untouched by this, and `undraw_plot_lines`' own saved
 * bytes keep coming out of mem[].  The prize is the decode's, not the store's: a row the decode
 * stops converting is worth ~0.09 ms and the store flip is a wash (CLAUDE.md, §10n). */
void revs_plot_byte(unsigned short addr, unsigned char value);
#define REVS_PLOT_BYTE(a, v)  revs_plot_byte((unsigned short)(a), (unsigned char)(v))
#ifdef REVS_SPAN_STATS
extern volatile unsigned long g_plotDeltaBytes;   /* bytes mirrored into the two buffers */
extern volatile unsigned long g_plotDeltaBases;   /* full re-expansions of the owned rows */
#endif
#ifdef REVS_PLOT_DELTA_CHECK
/* ⭐⭐ THE STALENESS ORACLE (`make DELTACHECK=1`), and it is the ONE thing a delta painter on an
 * owned row has to prove: that no writer reaches the owned rows unmirrored.  Once per sweep it
 * re-expands mem[] for every block of `kDeltaBlock` and compares against what the two buffers
 * actually hold, so a byte written by a routine with no `REVS_PLOT_BYTE` at its store site shows
 * up as a mismatch on the very next sweep.  ⭐ It is domain-agnostic by construction — widening
 * `kDeltaBlock` widens the oracle with it, which is how the needle block was proven correct
 * before it was closed on cost.
 * ⚠ It cannot be `revs_screen_convert_reference`: that reads `m_lineMode`, which the carve has
 * zeroed on exactly the lines under test, so the reference would be blank there (§10k trap 1).
 * It expands from the band record the painter itself uses. */
extern volatile unsigned long  g_plotDeltaChecks;
extern volatile unsigned long  g_plotDeltaMismatch;
extern volatile unsigned short g_plotDeltaMismatchY;  /* first offending display line */
#endif
#else
#define REVS_PLOT_BYTE(a, v)  ((void)0)
#endif

/* Counters — every one of them in PROBE_SYMS (amiga/Makefile).
   ⚠ `make SPANSTAT=0` compiles them and their updates away: six volatile RMWs a span is ~220
   cycles, and that instrument is how the shipping price is separated from the counting price.
   A .gdb script that reads one must therefore not be run against a SPANSTAT=0 build — the
   probe audit refuses it, because PROBE_SYMS_PLOT goes away with them. */
#ifdef REVS_SPAN_STATS
extern volatile unsigned long g_plotRuns;      /* runs emitted, summed over frames  */
extern volatile unsigned long g_plotCells;     /* cells covered by them             */
extern volatile unsigned short g_plotRunsLast; /* ...on the most recent sweep       */
extern volatile unsigned short g_plotCellsLast;
extern volatile unsigned char  g_plotLineLo;   /* display lines the last sweep painted */
extern volatile unsigned char  g_plotLineHi;
extern volatile unsigned long  g_plotNoTarget; /* runs dropped for want of a buffer */
extern volatile unsigned long  g_plotChainLines; /* lines the §10p takeover painted cell by cell */
/* ⭐⭐ THE RUN CENSUS ON THE TAKEOVER'S OWN LINES (§10p step 3c's sizing number).  `view_consume`
   is RLE — a zero source means "same colour as my left" — so a line's colour RUNS are exactly
   1 + (non-zero sources at cells 1..39), and that is what decides whether the cell loop can
   become a LONGWORD-FILL run loop.  ⚠ It must be counted on the NON-FLAT lines only: the flat
   ones are already `revs_plot_span`'s and would drag the average to 1.0 by construction. */
extern volatile unsigned long  g_plotChainNZ;     /* Σ non-zero sources over those lines */
extern volatile unsigned short g_plotChainNZLast; /* ...on the most recent sweep         */
#endif

#define REVS_PLOT_TARGET(p)     revs_plot_target(p)
#define REVS_PLOT_PLANES(a, b)  revs_plot_planes((a), (b))
#define REVS_PLOT_BANDS(l, m, n) revs_plot_bands((l), (m), (unsigned)(n))
#define REVS_PLOT_RUN(a, v, n)  revs_plot_run((unsigned short)(a), (unsigned char)(v), (unsigned short)(n))
#define REVS_PLOT_SPAN(a, v)    revs_plot_span((unsigned short)(a), (unsigned char)(v))
#define REVS_PLOT_CHAIN(a, v, s, t) \
        revs_plot_chain((unsigned short)(a), (unsigned char)(v), (s), (t))
#define REVS_PLOT_HAS_TARGET()  revs_plot_has_target()
#define REVS_PLOT_OWN_RESET()   revs_plot_own_reset()

#ifdef REVS_SPAN_OWN
/* ⭐⭐⭐ NO MIRRORING IN THE SHIPPING ARM, AND THIS IS THE WHOLE POINT OF `REVS_SPAN_OWN`.
 * `make SPANEMIT=1`'s scaffold mirrored EVERY chain store into the bitplanes on every line the
 * emitter did not paint — which is precisely the ⛔ mirror-each-store plotter shape §10c closed
 * at -9%, and the larger half of its +54 ms (§10L).  Here the two painters are DISJOINT: an
 * owned line comes from its span alone, and every other line comes from mem[] through the decode
 * exactly as it does today.  So PLOT_UNIT and the phase-2/3 boundary cells compile to nothing.
 * ⚠ They must stay LIVE under REVS_SPAN_VERIFY: with no plotting at all the oracle compares
 * decode(new mem[]) against decode(old mem[]) and mismatches ~5950 bytes (§10k trap 1) — a check
 * has to reproduce everything the sweep changed, not just the part under test. */
#define REVS_PLOT_CELL(a, v)    ((void)0)
#else
#define REVS_PLOT_CELL(a, v)    revs_plot_cell((unsigned short)(a), (unsigned char)(v))
#endif

#ifdef REVS_DIRECT_CHECK
/* ⭐⭐ THE ORACLE (§5), bracketing the sweep.  `before` converts mem[] with the SHIPPING decode and
   seeds the plot target with it; `after` converts mem[] again and requires the plotted buffer to be
   identical, over the WHOLE buffer — which checks both that the plotter wrote the right bytes and
   that it wrote nothing anywhere else.  Two full conversions per sweep: diagnostic only, and no
   framerate may be quoted from such a build. */
void revs_plot_check_before(void);
void revs_plot_check_after(void);
extern volatile unsigned long g_plotChecks, g_plotMismatch;
extern volatile unsigned short g_plotMismatchOff;
#define REVS_PLOT_CHECK_BEFORE() revs_plot_check_before()
#define REVS_PLOT_CHECK_AFTER()  revs_plot_check_after()
#else
#define REVS_PLOT_CHECK_BEFORE() ((void)0)
#define REVS_PLOT_CHECK_AFTER()  ((void)0)
#endif

#else   /* !REVS_DIRECT_PLOT */

#define REVS_PLOT_TARGET(p)      ((void)(p))
#define REVS_PLOT_PLANES(a, b)   ((void)0)
#define REVS_PLOT_BANDS(l, m, n) ((void)0)
#define REVS_PLOT_BYTE(a, v)     ((void)0)
#define REVS_PLOT_RUN(a, v, n)   ((void)0)
#define REVS_PLOT_SPAN(a, v)     ((void)0)
#define REVS_PLOT_CHAIN(a, v, s, t)  ((unsigned char)(v))
#define REVS_PLOT_HAS_TARGET()   0
#define REVS_PLOT_CELL(a, v)     ((void)0)
#define REVS_PLOT_OWN_RESET()    ((void)0)
#define REVS_PLOT_CHECK_BEFORE() ((void)0)
#define REVS_PLOT_CHECK_AFTER()  ((void)0)

#endif

#ifdef __cplusplus
}
#endif
#endif /* REVS_PLOT_H */
