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

#ifdef __cplusplus
extern "C" {
#endif

#ifdef REVS_DIRECT_PLOT

/* Per painted frame, from RevsScreen: which bitplane buffer the plotter paints into.  Null
   disables plotting for the frame (before the first decode there is no target, and the MODE 7
   front end has no race buffer at all). */
void revs_plot_target(unsigned char* planeBase);

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
extern unsigned char g_plotOwn[208];   /* = BBC_SCREEN_HEIGHT; checked in RevsPlot.cpp */
void revs_plot_own_reset(void);

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
#endif

#define REVS_PLOT_TARGET(p)     revs_plot_target(p)
#define REVS_PLOT_RUN(a, v, n)  revs_plot_run((unsigned short)(a), (unsigned char)(v), (unsigned short)(n))
#define REVS_PLOT_SPAN(a, v)    revs_plot_span((unsigned short)(a), (unsigned char)(v))
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
#define REVS_PLOT_RUN(a, v, n)   ((void)0)
#define REVS_PLOT_SPAN(a, v)     ((void)0)
#define REVS_PLOT_CELL(a, v)     ((void)0)
#define REVS_PLOT_OWN_RESET()    ((void)0)
#define REVS_PLOT_CHECK_BEFORE() ((void)0)
#define REVS_PLOT_CHECK_AFTER()  ((void)0)

#endif

#ifdef __cplusplus
}
#endif
#endif /* REVS_PLOT_H */
