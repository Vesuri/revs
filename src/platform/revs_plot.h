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
 * only who writes it — which is what lets `RevsScreen::convertRace()` stay the ORACLE unmodified.
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
#include "view_span.h"      /* ViewSpan — revs_plot_spans's whole argument (Stage A) */

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

/* ⭐⭐⭐ §12f-iv — ONE BYTE ONTO THE COCKPIT'S PLAYFIELD, for a writer inside display lines
   117..157 whose content genuinely moves: the wing mirrors.  See RevsPlot.cpp for why these do
   NOT go through the PF1 delta domain (its base and its oracle both re-expand `mem[]`, which is
   frozen for the terrain cells on those rows once §2a owns them).
   ⚠ A row outside the layer is ignored, so a writer that straddles the edge — `mirror_draw_car`
   runs from display line 154 down to 178 — needs no bounds test of its own. */
#if defined(REVS_PLATFORM_AMIGA) && defined(REVS_DUAL_PLAYFIELD)
#define REVS_COCKPIT_Y0  117u
#define REVS_COCKPIT_Y1  157u
void revs_plot_cockpit_plane(unsigned char* plane);
void revs_plot_cockpit_byte(unsigned short addr, unsigned char value);
#define REVS_COCKPIT_BYTE(a, v)  revs_plot_cockpit_byte((unsigned short)(a), (unsigned char)(v))
extern volatile unsigned long g_cockpitDeltaBytes;
#else
#define REVS_COCKPIT_BYTE(a, v)  ((void)0)
#endif

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

/* ⭐⭐⭐ ...AND THE SAME LINE PAINTED FROM THE GAME'S OWN ROAD RECORD — STAGE A.
 *
 * ⛔⛔⛔ PARKED ON COST — READ THIS BEFORE THE DESIGN ARGUMENT BELOW, WHICH IS THE ONE THE
 * MEASUREMENT REFUTED.  Built, exact, oracle green, and **+3.48 ms/frame against
 * `revs_plot_chain`** — the loss is on the arm that WORKS: a wholesale group is 236 cycles and the
 * four chain cells it replaces are 184.  The coarse test does NOT replace the fine ones, because
 * the group must still SCAN the four sources to prove they are zero and that scan costs what the
 * per-cell source test cost.  The full arithmetic, the free-painter bound and what survives are at
 * the definition in RevsPlot.cpp; the verdict is `docs/span-render-plan.md` §10q.
 * ⇒ Nothing calls this unless `make SPANPAINT=1` is on.  Do not revive it to serve step 3b.
 *
 * `revs_plot_chain` is the chain rewritten; this is the chain REPLACED.  Its extra argument is the
 * line's colour structure straight out of `surface_edge_0..3[line]` + `surface_colour_at` (see
 * view_span.h): a line's colour is piecewise constant with at most four breakpoints, so at most
 * FIVE spans cover all forty cells, and the span list is a pure function of the per-scan-line road
 * record the producers already wrote.  The forty source bytes then supply only the cells the
 * classifier cannot express — the MIXED bytes with a road boundary INSIDE the cell.
 *
 * ⭐⭐⭐ WHAT THE SPAN LIST BUYS IS A COARSE TEST THAT ACTUALLY REPLACES THE FINE ONES, which is
 * the one thing `revs_plot_chain`'s own group-of-four arithmetic could not have (RevsPlot.cpp:
 * "a MIXED group rises to 316, because the group test is pure overhead on top of the per-cell
 * tests it failed to replace").  Here a group of four cells is filled with ONE longword pair when
 * the carried byte already equals the span colour and no boundary falls inside the group — and
 * those two facts come from the span record, not from testing the four cells.  The per-cell arm is
 * byte for byte `revs_plot_chain`'s, so a slow group costs what it always cost.
 *
 * ⭐⭐ IT IS EXACT, NOT APPROXIMATE, AND THAT IS MEASURED: the host oracle (`make SHAPE=1`, the
 * composite model in shape.cpp) compares this exact rule against every byte the chain stores and
 * reads **0 misses in 1 280 208 cells** over a 250-frame driving window, with the overlay writing
 * 1.95 cells a line.  So there is no fallback arm and no per-cell compare against the fill.
 * ⚠ The rule that makes it exact: an event byte is written AT its cell and CARRIED rightwards —
 * `view_consume`'s RLE — and the three CHAIN-BOUNDARY bytes the drivers compose themselves are
 * events too.  Modelling those as ordinary cells read 2.36% miss (commit e7f359e).
 *
 * ⚠ Same contract as the chain otherwise: it consumes the sources (not under `REVS_SPAN_VERIFY`),
 * it claims the display line, and it returns the carried byte for the caller to thread on.
 * `nSpans` is `view_span_line`'s return value; `spans[0].colour` is the line background. */
unsigned char revs_plot_spans(unsigned short addr, const ViewSpan* spans, unsigned nSpans,
                              MEM_QUAL unsigned char* srcp,
                              MEM_QUAL const unsigned char* cellBytes);

#ifdef REVS_TERRAIN_SPANS
/* ⭐⭐⭐ THE TERRAIN PAINTER — the whole display line from its SPAN RECORD plus an EVENT LIST,
 * and the routine that retires `revs_plot_chain`'s forty-cell walk.
 *
 * It paints sweep lines `first` DOWN TO `last` from the record `view_own_full` published — the
 * per-line frame-buffer address and surface byte, plus the event list `view_scan_events` found by
 * its TRANSPOSED scan.  A line is its surface colour up to the first event and each event's byte
 * up to the next, which is `view_consume`'s RLE exactly, so the output is byte-for-byte the
 * chain's in ~3.5 longword runs instead of 40 cells.
 *
 * ⭐ ONE CALL A SWEEP, not one a line: the carve ladder priced a per-line cross-TU call at 107 us
 * a line, more than the filling itself.
 * ⚠ It claims the display lines and it does NOT consume: the scan is the destructive reader now.
 * ⛔ It deliberately does not use `view_span_line` — see the note at the definition. */
void revs_plot_terrain(unsigned first, unsigned last);
#endif

#ifdef REVS_LOW_OWN
/* ⭐⭐⭐ §2a — THE LOW BLOCK'S LINE, CLAIMED, AND ITS PLANE BYTE FOR CELL 0.
 *
 * Display lines 117..157 are the view sweep's own, and it is the ONLY writer of them: the real
 * BBC's census (`make fbwrites FILL=117-207`, span-render-plan §12a) names `view_cell_chain_a`,
 * `view_cell_chain_b_mid` and the two `view_paint_lines_*` drivers and nothing else, once
 * `tick_wheel_spin` has become a sprite and the needles have become geometry.  So the block can
 * be owned by RETARGETING one writer — no mirror, no delta, no undo list.
 *
 * ⭐ This is the per-LINE half: claim the display line and hand back `&plane1[cell 0]`, from which
 * cell `c` is `+c` and the second plane `+REVS_PLOT_PLANE_GAP`.  The sweep's own cell loop then
 * writes the two plane bytes INLINE — a cross-TU call inside a writer's loop is an aliasing
 * barrier and measured +3.45 ms (§11e), which is why only the line setup comes through here.
 * ⚠ It returns 0 when there is no target (MODE 7 and the frames either side of it), and the
 * caller must then paint NOTHING and the line stays unclaimed — the decode covers it, exactly as
 * `revs_plot_terrain`'s `!s_target` return does for 81..116.
 * ⚠ The rows are MODE 5 by construction: 117..157 sits inside ONE band, the same band whose
 * upper half (102..116) the terrain painter has been expanding as MODE 5 since §11.  `make
 * LOWOWNCHECK=1` is what proves it, per cell, against the run's own colour walk. */
unsigned char* revs_plot_low_line(unsigned short addr);
#endif

/* ⭐ The second plane's displacement inside one interleaved display line, for a painter that
   walks cells as BYTES.  Spelled once, here, so the needle block and the low block cannot
   disagree about it (they are the same 40). */
#define REVS_PLOT_PLANE_GAP  40u

/* The MODE 5 byte -> plane byte expansion tables, published by `RevsScreen::initialize()`.  A
   painter that writes the planes from a colour byte needs them; nothing else does. */
extern unsigned char g_bbcExpandLo[256], g_bbcExpandHi[256];
/* ⭐ THE SAME TWO BYTES BROADCAST ACROSS A LONGWORD, one interleaved {lo4, hi4} entry per BBC
   colour byte — built once beside the expansion tables (RevsPlot.cpp §the broadcast as a table)
   because `0x01010101 * b` is ~64 cycles of shift synthesis on a 68000 and a fill needs two. */
extern unsigned long g_bbcExpand4[256][2];

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
 * frozen mem[] bytes straight over emitted lines.  `RevsScreen::prepareFrame()` reads this and zeroes
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

#ifdef REVS_PLOT_RECTS
/* ⭐⭐⭐ §12c — THE DYNAMIC RECTANGLES, the third ownership mechanism and the one that collects
 * the rows §11d closed.  Re-expands a handful of small, FIXED rectangles of `mem[]` into the back
 * buffer once per painted frame, so a writer's price to an owned block becomes the AREA it can
 * reach rather than the number of stores it makes.  The needles fire 57.6 times a frame into 291
 * bytes; as a per-byte delta that is ~5.4 ms of "getting there" and as a rectangle it is ~0.56.
 * ⚠ Call it once per PAINTED FRAME and after the decode — `tick_wheel_spin` runs at 50 Hz from
 * the band schedule and `draw_dash_needles` is `race_main_loop`'s last drawing call, so anything
 * earlier publishes a frame-old dashboard.  The table, and why the wing mirrors are in it though
 * no practice measurement can see them, are at the definition (RevsPlot.cpp). */
void revs_plot_rects(void);
/* ⚠⚠⚠ `_RUN`, AND THE SUFFIX IS LOAD-BEARING — A CALL MACRO MUST NEVER SHARE ITS NAME WITH A
   FEATURE MACRO.  This one was spelled `REVS_PLOT_RECTS()`, and the no-op in the #else branch
   below DEFINED that name, so every `#ifdef REVS_PLOT_RECTS` in RevsPlot.cpp read TRUE in a
   build with the feature OFF.  The visible cost was the shipping dashboard: `kDeltaBlock` took
   its three-block form, display lines 158..191 were CLAIMED with no painter behind them, and the
   two dash needles were frozen at whatever the base laid down.  The measured cost was worse — an
   A/B whose two arms BOTH owned those rows, and the conclusion drawn from its +0.07 ms went into
   three documents.  `make macro-lint` is the guard.  (docs/span-render-plan.md §12c) */
#define REVS_PLOT_RECTS_RUN()  revs_plot_rects()
#ifdef REVS_SPAN_STATS
extern volatile unsigned long g_plotRectPasses;
#endif
#else
#define REVS_PLOT_RECTS_RUN()  ((void)0)
#endif

#ifdef REVS_NEEDLE_PLANES
/* ⭐⭐⭐ §12d — THE TWO DASH NEEDLES ARE PRERENDERED HARDWARE SPRITES.  The user's directive:
 *
 *     "The needle should be drawn either to the second playfield (the cockpit) or to sprites.
 *      In no case should it have anything to do with the terrain rendering."  ...  "Both can and
 *      should even use prerendered sprites."
 *
 * WHAT GOES.  `plot_line_octant` drew each pixel with a read-modify-write through `($70),Y` and
 * an undo list `undraw_plot_lines` replayed next frame; the port's first replacement painted the
 * pixels into both PF1 buffers and erased them by copying a cached clean cockpit back over a
 * bounding rectangle, which put a needle-shaped hole in the terrain's playfield (cells 12..27 of
 * display lines 128..157) and a special case in every terrain painter that crossed it.
 *
 * WHAT REPLACES IT.  The DDA still runs in the game's own twin — it is the game's line — and its
 * per-pixel action is still an APPEND to `g_needlePo`/`g_needlePix`.  The renderer turns a mark's
 * pixel list into a SPRITE IMAGE the first time it sees that list, with the position built into
 * the image's own control words, and keeps it (RevsPlot.cpp §the needle sprites): there are 118
 * distinct rev-needle images and 115 steering-mark images over every input the game can produce
 * (enumerated on the host, docs/perf-method.md), the widest 32 pixels and the tallest 29 lines.
 * After that a frame costs a key compare (the DDA's entry state — the walk itself is skipped on a
 * hit, see the image cache below) and three sprite-pointer writes; no plane byte is touched,
 * nothing is erased, and neither playfield knows the needles exist.  The rev needle is
 * sprite pair 2/3 and the steering mark pair 4/5; the tyres keep 0/1.
 *
 * ⚠ The list is filled by `draw_dash_needles`, `race_main_loop`'s closing draw, and consumed at
 * `decode()`'s tail; the chosen images are shown by `present()` with the frame's own buffer flip,
 * so the needles on screen always belong to the terrain on screen. */
#define REVS_NEEDLE_MAX   64u
#define REVS_NEEDLE_MARKS  4u
/* THE LIST IS PLANE OFFSETS (`y * 80 + cell`), which the DDA carries beside the plot pointer by
   ±1 and ±80 — a sprite render reads the position straight back out of it. */
#define REVS_NEEDLE_LINE_STRIDE  80u   /* one display line of the interleaved plane PAIR */
#define REVS_NEEDLE_PLANE_GAP    40u   /* ...and the second plane's displacement within it */
extern unsigned short g_needlePo[REVS_NEEDLE_MAX];     /* `y * 80 + cell` of the pixel */
/* The game's mask index 0..7, scaled by the pixel table's entry size (4). */
extern unsigned short g_needlePix[REVS_NEEDLE_MAX];
extern unsigned char  g_needleCount;
extern unsigned char  g_needleMarkAt[REVS_NEEDLE_MARKS];   /* first pixel of each mark */
extern unsigned char  g_needleMarks;
/* ⚠⚠ ALL MUST READ 0.  `Outside` is a pixel off the display (it cannot be in a sprite);
   `MaskBad` is the game's two mask tables not being pixel-aligned, which is what licenses
   reading a byte OR as one pixel's colour; `Overflow` is a line longer than the list;
   `PoolFull` an image the chip pool had no room for (that mark is not shown); `TooWide` an
   image wider than a sprite pair; `ColourBad` a mark whose pixels are not all one colour. */
#ifdef REVS_SPAN_STATS
#define REVS_NEEDLE_OUTSIDE()  (g_needleOutside++)
extern volatile unsigned long  g_needleOutside;
extern volatile unsigned long  g_needleMaskBad;
extern volatile unsigned long  g_needleOverflow;
extern volatile unsigned long  g_needlePaints;
extern volatile unsigned long  g_needleRenders;   /* sprite images built — ~233 a session */
extern volatile unsigned long  g_needleHits;      /* marks shown from the cache, DDA skipped */
extern volatile unsigned long  g_needlePoolFull;
extern volatile unsigned long  g_needleTooWide;
extern volatile unsigned long  g_needleColourBad;
extern volatile unsigned short g_needlePixLast;
#else
#define REVS_NEEDLE_OUTSIDE()  ((void)0)
#endif
#ifdef REVS_NEEDLE_CHECK
/* `make NEEDLECHECK=1` — every shown image against the pixel list it was chosen for, bit for
   bit (RevsPlot.cpp §ndlSpriteCheck).  `g_needleSpriteMismatch` must be 0. */
extern volatile unsigned long  g_needleSpriteChecks;
extern volatile unsigned long  g_needleSpriteMismatch;
#endif

/* Drop the pixel the DDA just computed onto the list.  Two stores and a bound test — the whole
   of what a needle pixel costs the game side. */
#define REVS_NEEDLE_PIXEL(po, m)                                             \
    do {                                                                     \
        const unsigned rnp_ = g_needleCount;                                 \
        if (rnp_ < REVS_NEEDLE_MAX) {                                        \
            g_needlePo[rnp_]   = (unsigned short)(po);                       \
            g_needlePix[rnp_]  = (unsigned short)(((m) & 7u) << 2);          \
            g_needleCount      = (unsigned char)(rnp_ + 1u);                 \
        }                                                                    \
    } while (0)

/* Where the plotter's (cell base, entry scan line) lands: the plane offset it walks from, plus
   the display line and cell it has to clip against.  ONCE per mark. */
void revs_needle_origin(unsigned short addr, unsigned char scanLine,
                        unsigned short* po, unsigned char* line, unsigned char* cell);
/* The clip: anywhere on the race display.  A sprite has no column to stay inside, so the only
   bound left is that `po` must name a real pixel — the old 12..27 x 128..191 column dropped the
   steering mark's pixels beyond cell 27 at full lock (the enumeration reaches cells 7..32). */
#define REVS_NEEDLE_Y0        0u
#define REVS_NEEDLE_YN      208u
#define REVS_NEEDLE_C0        0u
#define REVS_NEEDLE_CELLS    40u
#define REVS_NEEDLE_CLEAR()  (g_needleCount = 0, g_needleMarks = 0)
/* Open a mark — one `plot_line_octant` call, one sprite image. */
#define REVS_NEEDLE_MARK()                                                   \
    do {                                                                     \
        const unsigned rnm_ = g_needleMarks;                                 \
        if (rnm_ < REVS_NEEDLE_MARKS) {                                      \
            g_needleMarkAt[rnm_] = g_needleCount;                            \
            g_needleMarks        = (unsigned char)(rnm_ + 1u);               \
        }                                                                    \
    } while (0)
/* ⭐⭐ THE IMAGE CACHE IS KEYED ON THE DDA'S ENTRY STATE, NOT ON ITS OUTPUT.  A mark's pixel list
   is a pure function of what `plot_line_octant` reads before its loop — the plot pointer, the two
   step opcodes, the column, the delta, the increment, the counter, the scan line and the mask
   base — so those words name the image exactly.  On a HIT the DDA does not run: the mark shows
   its cached image and the loop's five exit cells are replayed from the entry (RevsNdlExit), so
   no reader of zero page can tell.  On a MISS the DDA fills the list as before and
   `revs_needle_exit` records what it left, for the entry `revs_needle_paint` then builds. */
typedef struct {
    unsigned short ptr;                      /* plot_ptr_v */
    unsigned char  bearing, t76, t77, count; /* bearing_lo, shared_temp_76/77, math_hi */
} RevsNdlExit;
int  revs_needle_lookup(unsigned k0, unsigned k1, unsigned k2, RevsNdlExit* ex);
void revs_needle_exit(const RevsNdlExit* ex);
void revs_needle_paint(void);
#define REVS_NEEDLE_PAINT()  revs_needle_paint()
/* The screen's side: the chip pool the images live in (allocated once, at start-up — never inside
   a frame), the image each of the four needle channels shows (0 = none), and the BBC pen each
   mark is drawn in, which `buildBands` turns into the sprite pair's colour per raster band. */
#define REVS_NEEDLE_CHANNEL0  2u        /* channels 2..5: two marks, a sprite pair each */
#define REVS_NEEDLE_CHANNELS  4u
void revs_needle_pool(unsigned short* pool, unsigned long words);
const unsigned short* revs_needle_sprite(unsigned k);
extern unsigned char g_needlePen[2];
#else
#define REVS_NEEDLE_PIXEL(a, m)  ((void)0)
#define REVS_NEEDLE_MARK()       ((void)0)
#define REVS_NEEDLE_OUTSIDE()    ((void)0)
#define REVS_NEEDLE_CLEAR()      ((void)0)
#define REVS_NEEDLE_PAINT()      ((void)0)
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
#define REVS_PLOT_SPANS(a, sp, n, s, t) \
        revs_plot_spans((unsigned short)(a), (sp), (n), (s), (t))
#define REVS_PLOT_TERRAIN(f, l)  revs_plot_terrain((unsigned)(f), (unsigned)(l))
#define REVS_PLOT_HAS_TARGET()  revs_plot_has_target()
#ifdef REVS_LOW_OWN
#define REVS_PLOT_LOW_LINE(a)   revs_plot_low_line((unsigned short)(a))
#else
#define REVS_PLOT_LOW_LINE(a)   ((unsigned char*)0)
#endif
#define REVS_PLOT_OWN_RESET()   revs_plot_own_reset()
#ifdef REVS_LOW_FULL_CHECK
/* ⭐⭐ `make LOWFULLCHECK=1` — the low block through the full-width painter against the run painter
   it replaced, compared as the PLAYER SEES IT: PF1 wherever PF2 is transparent (RevsPlot.cpp). */
void revs_plot_low_snap(void);
void revs_plot_low_compare(void);
#define REVS_PLOT_LOW_SNAP()     revs_plot_low_snap()
#define REVS_PLOT_LOW_COMPARE()  revs_plot_low_compare()
#else
#define REVS_PLOT_LOW_SNAP()     ((void)0)
#define REVS_PLOT_LOW_COMPARE()  ((void)0)
#endif

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
/* ⚠ THE SECOND FALLBACK THIS MACRO NEEDS, and the host build is the only thing that reads it:
   the dual-playfield definitions live inside `REVS_DIRECT_PLOT`, so a `mirror_draw_car_core`
   store site compiled WITHOUT the direct plotter — i.e. every host build, i.e. `make validate`
   and `make determinism` — saw no declaration at all.  A missing no-op here is a broken host
   build, not a missing feature. */
#define REVS_COCKPIT_BYTE(a, v)  ((void)0)
#define REVS_PLOT_RUN(a, v, n)   ((void)0)
#define REVS_PLOT_SPAN(a, v)     ((void)0)
#define REVS_PLOT_CHAIN(a, v, s, t)  ((unsigned char)(v))
#define REVS_PLOT_SPANS(a, sp, n, s, t)  ((unsigned char)0)
#define REVS_PLOT_TERRAIN(f, l)  ((void)0)
#define REVS_PLOT_LOW_LINE(a)    ((unsigned char*)0)
#define REVS_PLOT_HAS_TARGET()   0
#define REVS_PLOT_CELL(a, v)     ((void)0)
#define REVS_PLOT_OWN_RESET()    ((void)0)
#define REVS_PLOT_LOW_SNAP()     ((void)0)
#define REVS_PLOT_LOW_COMPARE()  ((void)0)
#define REVS_PLOT_RECTS_RUN()    ((void)0)
#define REVS_NEEDLE_PIXEL(a, m)  ((void)0)
#define REVS_NEEDLE_MARK()       ((void)0)
#define REVS_NEEDLE_OUTSIDE()    ((void)0)
#define REVS_NEEDLE_CLEAR()      ((void)0)
#define REVS_NEEDLE_PAINT()      ((void)0)
#define REVS_PLOT_CHECK_BEFORE() ((void)0)
#define REVS_PLOT_CHECK_AFTER()  ((void)0)

#endif

#ifdef __cplusplus
}
#endif
#endif /* REVS_PLOT_H */
