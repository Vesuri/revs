#pragma once
/* shape.h — INPUT-DISTRIBUTION COUNTERS for the render path (Phase 6 item 0, step 2).
 *
 * ⭐ WHY THIS EXISTS AND WHY IT IS NOT A PROFILER.  `probe.h`'s phase brackets say WHERE the
 * frame goes (the dashboard is 36.1%, the road subsystem 40%).  They cannot say WHAT the work
 * is, and `docs/direct-bitplane-plan.md` §6 step 2 / §7 both turn on exactly that: ⚑ the
 * predecessor project's -36% came from input-distribution counters, not from PC sampling.
 *
 * ⚠⚠ **"DASH" IN EVERY NAME HERE IS HISTORICAL AND IT MISLED §8.**  The rasteriser lives in the
 * `$7B00` *dash overlay page*, which is why these counters are called `g_shapeDash*` — but the
 * sweep it instruments paints the **VIEW band, display rows 81..157**, and never the dashboard's
 * rows 158..207.  The span census below measured that raster map directly.  So `$7BE2`'s share of
 * the frame is the ROAD VIEW's, and §8's "make the instruments sprites and the cockpit bitmap goes
 * static" is attached to the wrong routine and must be re-sized against the writers of rows
 * 158..207 before it is scheduled (`docs/direct-bitplane-plan.md` §10h).  The counter names stay:
 * they are in `PROBE_SYMS` and in committed `.gdb` scripts.
 *
 * The one measurement that sizes TWO whole Phase 6 items (§7 dirty flags, §8 hardware sprites)
 * is: **how much of the sweep is actually dirty per frame?**  The `$7B00` overlay is
 * 40 unrolled column units, each opening with its own dirty test (`LDY table,X / BEQ +8`), so
 * the game already carries a per-column mechanism — and if the sweep is already dirty-limited,
 * both of those items shrink from "36% of the frame" to something much smaller.  Until this is
 * counted, "the dashboard is mostly static" is an assumption with a one-command measurement
 * behind it, which this project's own rule calls a to-do rather than a tag.
 *
 * WHAT IS COUNTED, and why it needs no change to any generated code beyond two hooks:
 * `$7BE2` sweeps X from $4F down to $2C over 40 `$80`-spaced column blocks based at $3000
 * (`docs/static-map.md` item 10), and each unit ZEROES the source byte it consumes.  So the
 * before/after difference over that exact rectangle is the number of stores the sweep made,
 * measured without instrumenting the chain itself:
 *
 *   before $1748   -> g_shapeDashDirty   sources pending, and in how many columns
 *   after  ($174B) -> g_shapeDashLeft    sources still pending (the COUNT patch stops early)
 *   consumed = dirty - left
 *
 * ⚠ The rectangle is the sweep's own X range, not the whole block: bytes outside $2C..$4F are
 * never visited and counting them would inflate "dirty" with work the sweep cannot do.
 *
 * Platform-independent by construction — it reads only `mem[]` — so the HOST measures it for
 * free (`make SHAPE=1`, `REVS_SHAPE_WATCH=N`), and the same counters are readable on the target
 * through gdb.  ⚠ Any counter a committed .gdb script reads must go in PROBE_SYMS.
 */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef REVS_SHAPE

/* Per-sweep totals, accumulated.  `Calls` is the divisor for every mean below. */
extern volatile unsigned long g_shapeDashCalls;
extern volatile unsigned long g_shapeDashDirty;    /* sources pending before the sweep   */
extern volatile unsigned long g_shapeDashLeft;     /* ...still pending after it          */
extern volatile unsigned long g_shapeDashCols;     /* columns with >= 1 pending source   */
extern volatile unsigned long g_shapeDashColsLeft; /* ...still pending after the sweep   */
/* The last sweep on its own, so a single gdb stop is a reading rather than an average. */
extern volatile unsigned short g_shapeDashLastDirty;
extern volatile unsigned short g_shapeDashLastLeft;
extern volatile unsigned char  g_shapeDashLastCols;
/* How many of the 40 columns were dirty, as a histogram over sweeps (index = column count
   / 4, so 11 buckets cover 0..40).  A mean hides a bimodal distribution, and "mostly clean
   with an occasional full redraw" is a different design than "always half dirty". */
extern volatile unsigned long g_shapeDashColHist[11];
/* Per column, how many sweeps found it dirty — which columns are the moving instruments. */
extern volatile unsigned long g_shapeDashPerCol[40];
/* Per row of the sweep's X range ($2C..$4F), the same thing: a horizontal strip of dirt is a
   different subject (the mirrors, a moving needle) than a scatter. */
extern volatile unsigned long g_shapeDashPerRow[36];
/* ⭐ UNIT EXECUTIONS — the dirty TESTS, counted at each of the 40 column units' `LDY table,X`.
   Against `dirty` (the stores) this is the whole question: 1440 tests for ~100 stores means the
   dashboard's cost is the SCAN, and no amount of dirty-flag work on top of the game's own tests
   can help; a number close to the store count means the COUNT/START patching already skips the
   clean columns and the cost is elsewhere. */
extern volatile unsigned long g_shapeDashUnits;
extern volatile unsigned short g_shapeDashLastUnits;

void shape_dash_before(void);
void shape_dash_after(void);
void shape_dash_unit(unsigned line);

/* ── ⭐⭐ THE PER-LINE CENSUS — what the sweep would be allowed to SKIP ──────────────────────
 * §7a settled that the sweep's cost is the SCAN (2093 units for ~83 stores) and §7f that no
 * layout change moves it, because every unit must read its own source byte.  The only lever
 * left is running fewer units, and the unit of skipping is a SCAN LINE: with every source on a
 * line zero, every cell of that line takes the carried byte, so the line is one flat run of the
 * background byte — and if it was that same byte last sweep, painting it again changes nothing.
 *
 * Whether that is worth building depends on three counts this measures, per line:
 *   dirty    sources non-zero at sweep entry — what a PRODUCER-maintained dirty flag would see
 *   units    units the line actually ran (the cost)
 *   changed  stores that changed the byte already there — the TRUE redundancy, measured rather
 *            than argued, so the answer does not depend on my reading of the carry semantics
 *
 * ⭐ The pair to compare is `unitsRedundant` (units on lines that changed NOTHING) against
 * `unitsCleanSrc` (units on lines with no dirty source): the first is the prize, the second is
 * what a producer-side flag could actually detect.  If they differ, a dirty flag is not a
 * sufficient predicate and the skip needs more than the producers know.
 * ⚠ One extra mem[] read per unit — a SHAPE build only. */
extern volatile unsigned long g_shapeLineSweeps;
extern volatile unsigned long g_shapeLineVisited;        /* lines that ran >= 1 unit, summed  */
extern volatile unsigned long g_shapeLineRedundant;      /* ...that changed no byte at all    */
extern volatile unsigned long g_shapeLineCleanSrc;       /* ...that had no dirty source       */
extern volatile unsigned long g_shapeLineUnits;          /* units run                         */
extern volatile unsigned long g_shapeLineUnitsRedundant; /* ...on lines that changed nothing  */
extern volatile unsigned long g_shapeLineUnitsCleanSrc;  /* ...on lines with no dirty source  */
extern volatile unsigned long g_shapeLineCleanButChanged;/* clean sources, yet a byte moved    */
extern volatile unsigned long g_shapeLineDirtyNoChange;  /* dirty sources, yet nothing moved   */
/* Per line ($03..$4F), how many sweeps ran it / found it redundant, and the units it ran. */
extern volatile unsigned long g_shapeLinePerVisit[128];
extern volatile unsigned long g_shapeLinePerRedundant[128];
extern volatile unsigned long g_shapeLinePerUnits[128];
/* ⭐ WHERE the producer flag would be WRONG: a line with no dirty source whose sweep still
   changed a byte, counted per line.  The summed count alone cannot say whether the skip is
   unsound everywhere or only where some OTHER routine writes the viewport. */
extern volatile unsigned long g_shapeLinePerCleanChanged[128];
void shape_dash_store(unsigned dst, unsigned value, unsigned line);

/* ⭐⭐ THE SPAN CENSUS — the colour-run shape of a painted line, which is the workload of a
   direct-to-bitplane renderer (the run census above is the workload of a SKIP scheme, a
   different question).  Hooked from `shape_dash_store` / `shape_dash_after`; its result and
   the two independent instruments that corroborate it are at the code in shape.cpp. */
void scout_sweep_end(void);
void scout_report(void);

/* ── ⭐⭐ THE RUN CENSUS — what a SOURCE-EVENT consumer would actually have to visit ─────────
 * `docs/open-work.md` item 1's live sub-lever, and the count that decides whether it gets
 * built.  The per-line census above sized a per-LINE skip, that skip SHIPPED, and it measured
 * -0.4% — because phases 2 and 3 skip zero lines.  The next granularity down is the CELL, and
 * the reason it is not obviously hopeless is `view_consume`'s structure: a non-zero source means
 * "new colour here", a zero source means "same as my left".  So a painted line is a RUN-LENGTH
 * ENCODED colour, the store is IDEMPOTENT wherever the cell already holds that colour, and a
 * road edge that moves one cell changes ONE cell — not the thirty-five to its right, which keep
 * the same run value.
 *
 * ⚠⚠ BUT A CLEAN UNIT'S STORE IS WHAT ERASES LAST FRAME, so "skip the clean units" is not the
 * scheme and this is not the per-line skip again.  A source-event consumer must visit a cell if
 * EITHER
 *   (a) its source is non-zero — it has to be consumed, ZEROED, and the carried byte updated;
 *   (b) its store would change the byte already there — the picture genuinely moves.
 * The prize is the size of that UNION, and (a) ∪ (b) is not (a) ∪ nothing: a cell can change
 * with no event of its own (the byte carried into it moved) and an event can change nothing
 * (it re-states the colour already painted).  So the union is MEASURED, not derived from my
 * reading of the carry semantics — the same discipline the `changed` test above was written
 * with.
 *
 * ⭐ And the union alone does not price it either: finding those cells costs per-RUN set-up, so
 * the census counts CONTIGUOUS runs of must-visit cells as well.  Two runs of two beats four
 * scattered singletons, and forty singletons is the present loop with extra book-keeping.
 * Contiguity needs no screen geometry: cells of one line sit 8 bytes apart in visit order, so
 * `dst == prev + 8` IS "adjacent", and a segment jump would honestly read as a run break.
 * ⚠ The EXTENT (first..last must-visit cell) is counted too, because RoF's third transferable
 * finding was a scan bounded by a producer-known extent (~324 -> 113 ticks) — a cheaper scheme
 * than a run list, and the two are priced against each other here rather than in argument.
 * ⚠ A SHAPE build only: one extra mem[] read per unit, on top of the per-line census's own. */
extern volatile unsigned long g_shapeRunSweeps;
extern volatile unsigned long g_shapeRunStores;    /* cell stores the sweep made (the cost)   */
extern volatile unsigned long g_shapeRunEvents;    /* ...whose source was non-zero (arm 1/2)  */
extern volatile unsigned long g_shapeRunChanged;   /* ...that changed the byte already there  */
extern volatile unsigned long g_shapeRunUnion;     /* ...that are an event OR a change        */
extern volatile unsigned long g_shapeRunRuns;      /* contiguous runs of union cells          */
extern volatile unsigned long g_shapeRunExtent;    /* Σ per-line (last-first+1) union extent  */
extern volatile unsigned long g_shapeRunLinesAny;  /* line paints with >= 1 union cell         */
extern volatile unsigned long g_shapeRunLinesNone; /* ...with none at all (wholly redundant)   */
/* ⭐ A mean hides a bimodal distribution — the per-line census's own lesson — so the union size
   per line paint is a histogram: buckets 0,1,2,3,4,5-8,9-16,17-32,33-40. */
extern volatile unsigned long g_shapeRunUnionHist[9];
extern volatile unsigned long g_shapeRunRunsHist[9];   /* ...and the run count per line paint */
/* Per source line, the union cells it needed — horizon or near field is a different subject. */
extern volatile unsigned long g_shapeRunPerUnion[128];
/* ⚠⚠ THE INSTRUMENT'S OWN STATE, because the event half is LATCHED rather than passed: the
   arm is recorded by `view_consume` and read by the store hook one call later.  If that order
   ever breaks, the union is silently wrong in the flattering direction, so the loss is counted
   and printed — a non-zero value invalidates every event/union number above. */
extern volatile unsigned long g_shapeRunArmLost;

/* ── ⭐⭐ IS THE PRODUCER-SIDE MARKING COMPLETE? ────────────────────────────────────────────
 * The three-part skip needs a per-line DIRTY MARK set by whoever writes a source byte, and the
 * skip is only sound if that mark is set by EVERY writer.  "Which routines write $3000..$43CF"
 * is a question about a large subtree, so it is measured, not read:
 *
 *   shape_mark_source(addr)   a hook at a known writer — "I wrote this source byte"
 *   the SHADOW                a copy of the whole rectangle taken after each sweep; the bytes
 *                             that differ at the next sweep's entry are what was ACTUALLY
 *                             written, whoever wrote them
 *
 * ⭐ So the shadow is the ground truth and needs no hook at all, which is the point: a writer I
 * never found still shows up in it.  `g_shapeMarkUnmarked` counts written-but-unmarked lines and
 * **must read 0 over a long run before any skip is enabled**.  `g_shapeMarkOver` counts the other
 * direction (marked, nothing written) — not a bug, just skip coverage given away.
 * ⚠ 5120 compares per sweep: a SHAPE build only. */
extern volatile unsigned long g_shapeMarkWritten;      /* lines a producer really wrote      */
extern volatile unsigned long g_shapeMarkMarked;       /* lines a hook marked                */
extern volatile unsigned long g_shapeMarkUnmarked;     /* ⚠ written, NOT marked — must be 0  */
extern volatile unsigned long g_shapeMarkOver;         /* marked, nothing written            */
extern volatile unsigned long g_shapeMarkPerUnmarked[128];
void shape_mark_source(unsigned addr);

/* ── ⭐⭐ THE DASH-EDGE WALK (`fill_dash_edge_columns`, phase 18) ─────────────────────────────
 * Phase 18 is 17 ms/frame — the sixth-biggest row in the frame — for a driver whose whole job is
 * TWELVE columns at the two ends of the viewport (`revs_native.c` §$1E15).  Nothing says how many
 * CELLS that is, and the two readings differ by an order of magnitude: twelve columns of a few
 * lines each is a rounding error, twelve columns of forty lines is 480 cells and the row is honest
 * throughput.  Until it is counted, "phase 18 is per-item setup like the rest" is an assumption.
 *
 * ⭐ AND THE ARM SPLIT IS THE ACTIONABLE HALF.  `column_gap_walk` re-reads its three patched
 * operands ($1DD5/$1DDC/$1DE9) out of `mem[]` on EVERY cell and dispatches on them, so a cell's
 * cost depends on which arm it takes:
 *   skip      a non-zero source on the $09 arm — reads the byte, steps the line, stores nothing
 *   table     a non-zero source on the $EF arm — one store into the per-line boundary table
 *   colour    an empty cell — calls surface_colour_at, then stores
 *   fallback  ...of which the surface had no colour and the patched fallback byte was used
 * If `colour` dominates, the cost is surface_colour_at and the operand traffic is noise; if the
 * cells are few and the row is still 17 ms, the cost is per-COLUMN setup and the walk is not the
 * subject at all.  Counted, not argued.
 * ⚠ One increment per cell: a SHAPE build only. */
extern volatile unsigned long g_shapeEdgeCalls;    /* fill_dash_edge_columns calls        */
extern volatile unsigned long g_shapeEdgeWalks;    /* column_gap_walk entries             */
extern volatile unsigned long g_shapeEdgeCells;    /* loop passes = source bytes read     */
extern volatile unsigned long g_shapeEdgeSkip;     /* non-zero source, the $09 arm        */
extern volatile unsigned long g_shapeEdgeTable;    /* non-zero source, the $EF arm        */
extern volatile unsigned long g_shapeEdgeColour;   /* empty cell -> surface_colour_at     */
extern volatile unsigned long g_shapeEdgeFallback; /* ...with no surface colour           */
/* Per walk, the cells it ran, as a histogram (buckets of 8, 16 buckets cover 0..127) — a mean
   hides "two long columns and ten empty ones", which is a different subject than a flat run. */
extern volatile unsigned long g_shapeEdgeHist[16];
void shape_edge_call(void);
void shape_edge_walk(unsigned cells);
void shape_edge_cell(unsigned arm);

/* ── ⭐⭐ THE VIEW SWEEP'S CONSUME ARMS, PER VIEW PHASE ───────────────────────────────────────
 * ⭐ THE QUESTION THIS SETTLES, and it re-prices a whole plan item.  The four view probes put
 * phase 3 (`paint_lines_short`, $7F18) at 98 us per UNIT against phase 1's 15 — 4.4x once the
 * chain-entry brackets are netted out — and the reading on the table was that the premium is the
 * port's SMC machinery, worth ~8-10% of the frame to replace with run descriptors.  Two cheaper
 * explanations were checked first and BOTH failed: the per-run instrument is 2-3% of a row, and
 * `paint_cells`' prologue is ~40 instructions in the objdump, nowhere near the ~1900 cycles the
 * arithmetic demands.  What is left is that phase 3's units are simply DEARER work than phase 1's:
 *
 *   clean   `view_consume` found a zero source and returned the carried byte — no store to the
 *           source, no `view_cell_bytes` lookup.  The cheap arm, and phase 1's sources are ~96%
 *           empty (2093 units a frame for ~83 changing bytes).
 *   dirty   a non-zero source: zero it, then translate through `view_cell_bytes`.  Two extra
 *           mem[] accesses, one of them a WRITE.
 *   forced  the run's first unit, which always takes the translate path.
 *
 * ⭐ And ONE MORE candidate the same hook prices for free: `busSafe`.  A span that fails
 * `view_span_is_ram` routes every cell through `bus_write` — the hardware-range test per cell,
 * which is exactly the per-unit premium being hunted.  If phase 3's runs are not bus-safe, that
 * is the answer and it is a one-line fix, not a rewrite.
 *
 * ⚠ IF THE PREMIUM IS THE DIRTY ARM, IT IS THE GAME'S OWN WORK AND THE DESCRIPTOR REWRITE CANNOT
 * TOUCH IT — price step 2 against this table before writing any of it.
 * ⚠ THE INSTRUMENT'S SELF-CHECK is the sum identity `clean + dirty + forced == units`: every
 * `view_consume` call takes exactly one arm, so a shortfall means a hook is missing an arm and an
 * excess means one is double-counted.  `runs`/`lines` are here too as the CONTROL — the host runs
 * a different trajectory than the target, and a per-phase unit count nothing like the target's
 * 1442/426/282 would invalidate reading any of this across to the Amiga.
 * ⚠ One increment per unit: a SHAPE build only. */
extern volatile unsigned long g_shapeViewUnits[3];  /* view_consume calls, per phase      */
extern volatile unsigned long g_shapeViewClean[3];  /* ...zero source, carried byte        */
extern volatile unsigned long g_shapeViewDirty[3];  /* ...non-zero: zero it + translate    */
extern volatile unsigned long g_shapeViewForced[3]; /* ...the run's first unit             */
extern volatile unsigned long g_shapeViewRuns[3];   /* chain runs entered                  */
extern volatile unsigned long g_shapeViewRunBus[3]; /* ...whose span was NOT bus-safe      */
extern volatile unsigned long g_shapeViewLines[3];  /* scan lines                          */
/* ⭐ THE OTHER SIDE OF THE SUM IDENTITY, counted somewhere else on purpose: the run lengths
   come out of the run set-up's pointer arithmetic and the stop tails out of the stop branch,
   while clean/dirty/forced are counted per `view_consume` CALL.  `clean + dirty + forced ==
   runUnits + stops` therefore compares two independent derivations of the same quantity — an
   identity a misplaced or unreachable hook breaks, which a self-consistent total cannot. */
extern volatile unsigned long g_shapeViewRunUnits[3];
extern volatile unsigned long g_shapeViewStops[3];
void shape_view_phase(int idx);
void shape_view_arm(unsigned arm);
void shape_view_run(unsigned units, int busSafe);
void shape_view_stop(void);
void shape_view_line(void);

/* ── ⭐⭐⭐ THE TAKEOVER'S FLAT-LINE COUNT (`docs/span-render-plan.md` §10p step 1) ──────────
 * ⭐⭐ THE ONE NUMBER THE PHASE-1 TAKEOVER IS SIZED ON, and it costs no emulator run.  §10p
 * puts a flat line at ~980 cycles against ~2300 for a changed one, so the FLAT FRACTION is the
 * whole difference between a -8 ms and a -11 ms result.  CLAUDE.md licenses exactly this: count
 * it on the host, where the view sweep's 36/16/25 line split already reproduces the target's.
 *
 * ⭐ THE PREDICATE IS THE WHOLE OF IT, AND IT IS STATELESS AND EXACT.  `view_consume` is RLE
 * with a DESTRUCTIVE READ — zero source means "same as my left", non-zero means "change to
 * `view_cell_bytes[source]`" AND THEN `*srcp = 0` — so a source is non-zero at line entry **iff
 * a producer wrote it since the last sweep**, and a line is one flat run **iff all forty of its
 * sources are zero**.  No map, no persistence, nothing to get stale.  ⛔ The dirty map this
 * replaces marked on ANY write including a write of zero, so it was CONSERVATIVE where this is
 * exact; it also cost +4.9 ms in the producers (`docs/span-render-plan.md` §10n).
 *
 * ⚠ THE SECOND CONJUNCT IS COUNTED SEPARATELY ON PURPOSE.  A planted stop ends the chain on the
 * unit it sits on, so units from there to 39 keep their sources and the line is not one run —
 * the emitter needs `flat && fullRun`.  §10p's reading of the stop list says `view_stop_from(0)
 * == 40` is a CONSTANT TRUE through all of phase 1, i.e. `full == lines` on phase 1's row, and
 * that is a CLAIM this instrument checks rather than trusts.  If it holds, the `fullRun`
 * conjunct is dead weight in phase 1's predicate and can come out of the inner test.
 *
 * ⭐ THE PER-SWEEP TALLY IS PUBLISHED TOO — min, max and last, flushed at `shape_view_phase(0)`,
 * the one call `view_paint_lines_core` makes per sweep.  ⚠⚠ BUT THE WORRY THAT MOTIVATED IT IS
 * WITHDRAWN, AND THE CORRECTION IS THE USEFUL PART: I wrote that a whole-run ratio "cannot answer
 * the sizing question" because 21-of-36 averaged could be bimodal.  It CAN, because the cost is
 * LINEAR in the flat count — sum over sweeps of `flat*980 + (36-flat)*2300` is
 * `36*2300*N - 1320*Σflat`, which depends on Σflat and on nothing else about its distribution.
 * So the MEAN is a sufficient statistic for the average frame; the min..max spread bounds the
 * WORST FRAME, which is a jitter question and a different one.  ⭐⭐ Ask whether the statistic is
 * sufficient for the cost function before demanding a distribution — a linear cost needs only
 * the total.
 *
 * ⭐⭐⭐ MEASURED (host, `REVS_FIXED_RNG=1`, STRAIGHT_TO_RACE + HOLD_THROTTLE so the car MOVES,
 * 595 sweeps), and all three reads say something:
 *   phase 1: 36 lines/sweep  flat=57%  full == lines  per sweep 0..36, last 21
 *   phase 2: 16 lines/sweep  flat= 0%  full == 0      per sweep 0..0
 *   phase 3: never enters this hook — only `view_paint_lines_core` and `paint_lines_clipped`
 *            call `paint_cells`; `paint_lines_short` drives its own line loop.
 * (1) The 36/16 split reproduces the target's, so the row carries across.  (2) `full == lines`
 * over 21456 phase-1 lines CONFIRMS §10p's claim ⇒ the `fullRun` conjunct is dead weight in
 * phase 1 and the predicate is the forty-source scan alone.  (3) 57% flat sizes the takeover at
 * **-7.8 ms** on the average frame (36 x (0.57*980 + 0.43*2300) = 55.7k cyc = 7.86 ms against the
 * measured 15.68), ranging -4.0 ms on a sweep with nothing flat to -10.7 ms on an all-flat one.
 * ⭐ It also cross-checks the PARKED span census: `SPANFILL=1` found 21 of 36 lines spanned with
 * a dirty-map predicate, and this driving run's last sweep reads 21 — so that 21 was not a
 * parked-car artefact, which was the standing doubt about it.
 *
 * ⛔ AND THE FLAT PREDICATE IS PHASE-1-ONLY.  Not one of 9536 phase-2 lines had forty zero
 * sources, and every one of them had stops planted (2.0 runs/line).  Phase 3's lines visit 11.2
 * of 40 cells, so a forty-cell scan asks the wrong question there even before it is tried.  ⇒
 * phases 2 and 3 are a DRIVER lever (§10p), never a span-fast-path one.
 * ⚠ One forty-byte scan per line: a SHAPE build only, and a COUNT, never a timing.
 * ⚠ Sabotaged four ways before its output was believed — one cell instead of forty reads 96%,
 * the inverted test 0%, a 64-byte stride 0%, and the restored control reproduces 6109 exactly. */
extern volatile unsigned long g_shapeViewFlat[3];      /* lines whose forty sources are all zero */
extern volatile unsigned long g_shapeViewFull[3];      /* lines entered with the stop list empty  */
extern volatile unsigned long g_shapeViewFlatFull[3];  /* ...both: the emitter's own predicate    */
extern volatile unsigned long g_shapeViewFlatLines[3]; /* lines this hook saw (its own control)   */
extern volatile unsigned long g_shapeViewSweeps[3];    /* sweeps in which the phase ran           */
extern volatile unsigned char g_shapeViewFlatMin[3];   /* per-SWEEP flat&&full count: min         */
extern volatile unsigned char g_shapeViewFlatMax[3];   /* ...and max                             */
extern volatile unsigned char g_shapeViewFlatLast[3];  /* ...and the last completed sweep's       */
extern volatile unsigned char g_shapeViewDstFirst[3]; /* lowest DISPLAY line the phase painted */
extern volatile unsigned char g_shapeViewDstLast[3];  /* ...and the highest              */
void shape_view_flat(unsigned line, int fullRun, unsigned screenPtr);

/* ── THE ROAD PASS ($1A20, phase 11) ────────────────────────────────────────────────────────
 * The other half of step 2, and the number that prices direct plotting: how many BYTES of the
 * 8320-byte frame buffer does the road rasteriser actually write per frame?  The decode converts
 * all 8320 whatever happens; if the road writes a few hundred, then a direct plotter plus a
 * dirty-region present is worth far more than making the decode itself faster.
 *
 * Measured by snapshot-and-diff around the call, so no plotter is instrumented and the span
 * chains' self-modifying code is untouched.  ⚠ 8320 bytes of static storage and two passes over
 * the frame buffer per frame: a SHAPE build only, never a perf build. */
extern volatile unsigned long g_shapeRoadCalls;
extern volatile unsigned long g_shapeRoadBytes;   /* frame-buffer bytes the pass changed  */
extern volatile unsigned long g_shapeRoadLines;   /* display lines it touched             */
extern volatile unsigned short g_shapeRoadLastBytes;
extern volatile unsigned short g_shapeRoadLastLines;
extern volatile unsigned char  g_shapeRoadFirstLine;  /* topmost line touched, last call  */
extern volatile unsigned char  g_shapeRoadLastLine;   /* bottommost                       */
void shape_road_before(void);
void shape_road_after(void);

/* ── WHO ACTUALLY DRAWS?  Frame-buffer writes attributed PER MAIN-LOOP PHASE ────────────────
 * ⭐⭐ Because the first answer was wrong: `$1A20` is called "the road rasteriser" everywhere in
 * this project's notes, and the snapshot-diff above says it changes SIX BYTES of the frame buffer
 * per call, at display lines 26..55 — inside the sky band, i.e. engine VARIABLES that happen to
 * live in the frame buffer, not road pixels.  So the plotting is somewhere else, and a plan that
 * hand-writes asm for "the rasteriser" needs to know where before it starts.
 *
 * This is the same snapshot-and-diff, run at every phase boundary and charged to the phase that
 * just closed — the `make refloop --fill` idea (attribute every write to the routine that made it)
 * applied to the port's own main loop.  ⚠ 8320 compares per phase boundary, ~25 boundaries per
 * frame: a HOST instrument.  Do not read a framerate, or a phase share, from a build with it on.
 */
extern volatile unsigned long g_shapePhaseBytes[40];   /* frame-buffer bytes written by phase n */
extern volatile unsigned long g_shapePhaseFrames[40];  /* phase closures that wrote anything    */
extern volatile unsigned char g_shapePhaseFirst[40];   /* topmost display line it ever wrote    */
extern volatile unsigned char g_shapePhaseLast[40];    /* bottommost                            */
void shape_phase_mark(int id);

/* ── ⭐⭐ HOW MUCH OF THE PICTURE CHANGES PER PAINTED FRAME ───────────────────────────────────
 * The number that prices dirty-region drawing, and it is cheap enough to run on the TARGET: one
 * pass over the 8320-byte frame buffer per painted frame, comparing against the state at the
 * previous paint.  `decode()` converts all 8320 bytes every time; if only a few hundred differ,
 * then most of that pass is re-converting bytes that did not move — and a dirty-region decode
 * captures much of what direct plotting would, for far less work.
 *
 * ⚠ The host and the target are DIFFERENT here and both are needed: the host runs the 50 Hz body
 * once per game frame, the target drains ~50 ticks per painted frame and the body DRAWS (display
 * lines 120-143).  So the target's delta is the honest one for the port as it ships. */
extern volatile unsigned long g_shapeFrameCalls;
extern volatile unsigned long g_shapeFrameBytes;   /* changed bytes, summed over paints  */
extern volatile unsigned short g_shapeFrameLast;   /* ...on the most recent paint        */
extern volatile unsigned short g_shapeFrameMax;
extern volatile unsigned char  g_shapeFrameFirstLine;
extern volatile unsigned char  g_shapeFrameLastLine;
extern volatile unsigned short g_shapeFrameLines;  /* display lines changed, last paint  */
void shape_frame_delta(void);

/* ── ⭐⭐⭐ THE TAKEOVER'S READER GATE — POISON THE ROWS IT WANTS TO OWN ──────────────────────
 * `docs/span-render-plan.md` §10p's reader audit asks: **if the takeover writes bitplanes instead
 * of `mem[]` for display rows 81..101, does anything in the game read those `mem[]` bytes back?**
 * §10i's RESULTS rule makes that a WRITTEN READER AUDIT, and a code read cannot close it — the
 * readers include the transliteration an expansion circuit's hook re-enters, which no scan of the
 * native surface can see (CLAUDE.md: "a `region_*` name is SHIPPING code until proven otherwise").
 *
 * ⭐⭐ SO ASK IT AS A MEASUREMENT INSTEAD, AND LET THE GAME ANSWER.  Immediately after the sweep
 * returns, invert every `mem[]` byte of the display lines named by `REVS_FB_POISON=<first>-<last>`.
 * `^= 0xFF` rather than a fixed pattern, so EVERY poisoned byte provably differs from what the
 * sweep just wrote — a constant could coincide with the real pixels.  Then run a fixed trajectory
 * and diff the whole 64 KB against an unpoisoned run: **if every difference lies inside the
 * poisoned rows, nothing in the game read them.**  This covers the transliteration, the track
 * hooks and the 50 Hz body for free, because it asks the machine rather than the source.
 *
 * ⭐ AND IT HAS A BUILT-IN POSITIVE CONTROL, which is why it is trustworthy at all.
 * `update_grip_limits` reads `mem[$713D]` and `mem[$7205]` — both are frame-buffer addresses on
 * DISPLAY LINE 149 (cells 7 and 32: $713D-$5A80 = 18*320 + 7*8 + 5, so row 18 line 5), and it
 * feeds them into the two per-axle grip thresholds.  So `REVS_FB_POISON=149-149` MUST diverge,
 * and a run where it does not is an instrument that measures nothing (`revs_verify_the_instrument`).
 * Poisoning 81..101 and 149..149 in the same session is the control pair.
 *
 * ⚠ The poison does not accumulate: the sweep rewrites those rows every frame, so each frame's
 * readers see exactly one frame's inversion.  ⚠ A SHAPE build only — it corrupts the display by
 * construction, and the host has no renderer to corrupt.
 *
 * ── ⭐⭐⭐ THE ANSWER: PHASE 1'S ROWS HAVE NO READER, AND THE OTHER TWO DO ──────────────────
 * `REVS_FIXED_RNG=1`, diff of the whole 64 KB at frame 300 against an unpoisoned run, with each
 * phase's row range measured by `g_shapeViewDstFirst/Last` rather than assumed:
 *
 *     rows      phase                     differ  inside  OUTSIDE  reader
 *     81..116   1 view_paint_lines_core     1440    1440      0     ⭐ NONE
 *     117..132  2 paint_lines_clipped        644     640      4     plot_line_octant, undo 28..31
 *     133..157  3 paint_lines_short         2796     796   2000     ...undo 32..35 + row 149
 *     149..149  (the positive control)      2325      34   2291     update_grip_limits — it works
 *
 * ⭐ So the takeover may own display lines 81..116 OUTRIGHT: no compatibility write-back, no
 * shadow copy, nothing.  All 1440 poisoned bytes also SURVIVED to the dump, so nothing overwrote
 * those rows after the sweep and no masking can be hiding a reader.
 *
 * ⭐⭐ THE READER IN THE OTHER TWO IS `plot_line_octant`, AND IT IS NOT A PIXEL CONSUMER — it
 * saves the ORIGINAL byte at each address it plots into `MEM_plot_undo_byte` ($0780) so the plot
 * can be undone.  The eight outside bytes are exactly entries 28..35, each the bit-inverse of the
 * reference.  A takeover of phases 2/3 therefore owes the undo table real bytes, not the whole
 * frame buffer — but phase 1 owes nothing, which is where §10p starts.
 *
 * ⛔ AND §10p's ORIGINAL GATE MECHANISM IS RETRACTED: it claimed `column_gap_walk` reads frame
 * buffer pixels.  It does not — it walks the SOURCE blocks at `$3000 + column*$80` and stores to
 * `$0504`/`$4400`, none of them inside `$5A80..$7B40`.  The `NOUNITS=2` observation behind that
 * claim (ph18 5.88 vs 10.18 ms) has a different cause: NOUNITS=2 never CONSUMES the sources, so
 * they stay non-zero and the gap walk — which replaces every ZERO byte — finds fewer to fill.
 * Phase 18 is a PRODUCER for the sweep, not a consumer of the frame buffer. */
extern volatile unsigned long g_shapeFbPoisonBytes;  /* bytes inverted — its own control */
void shape_fb_poison(void);

#define PROBE_SHAPE_DASH_BEFORE()  shape_dash_before()
#define PROBE_SHAPE_DASH_AFTER()   shape_dash_after()
#define PROBE_SHAPE_DASH_UNIT(line) shape_dash_unit(line)
#define PROBE_SHAPE_DASH_STORE(d, v, line) shape_dash_store((d), (v), (line))
#define PROBE_SHAPE_MARK(addr)     shape_mark_source((addr))
#define PROBE_SHAPE_EDGE_CALL()    shape_edge_call()
#define PROBE_SHAPE_EDGE_WALK(c)   shape_edge_walk((c))
#define PROBE_SHAPE_EDGE_CELL(a)   shape_edge_cell((a))
#define PROBE_SHAPE_VIEW_PHASE(i)  shape_view_phase((i))
#define PROBE_SHAPE_VIEW_ARM(a)    shape_view_arm((a))
#define PROBE_SHAPE_VIEW_RUN(n, b) shape_view_run((n), (b))
#define PROBE_SHAPE_VIEW_STOP()    shape_view_stop()
#define PROBE_SHAPE_VIEW_LINE()    shape_view_line()
#define PROBE_SHAPE_VIEW_FLAT(l,f,p) shape_view_flat((l), (f), (p))
#define PROBE_SHAPE_ROAD_BEFORE()  shape_road_before()
#define PROBE_SHAPE_ROAD_AFTER()   shape_road_after()
#define PROBE_SHAPE_PHASE(n)       shape_phase_mark(n)
#define PROBE_SHAPE_FRAME()        shape_frame_delta()
#define PROBE_SHAPE_FB_POISON()    shape_fb_poison()

#else

#define PROBE_SHAPE_EDGE_CALL()    ((void)0)
#define PROBE_SHAPE_EDGE_WALK(c)   ((void)(c))
#define PROBE_SHAPE_EDGE_CELL(a)   ((void)(a))
#define PROBE_SHAPE_VIEW_PHASE(i)  ((void)0)
#define PROBE_SHAPE_VIEW_ARM(a)    ((void)0)
#define PROBE_SHAPE_VIEW_RUN(n, b) ((void)0)
#define PROBE_SHAPE_VIEW_STOP()    ((void)0)
#define PROBE_SHAPE_VIEW_LINE()    ((void)0)
#define PROBE_SHAPE_VIEW_FLAT(l,f,p) ((void)0)
#define PROBE_SHAPE_DASH_BEFORE()  ((void)0)
#define PROBE_SHAPE_DASH_AFTER()   ((void)0)
#define PROBE_SHAPE_FB_POISON()    ((void)0)
#define PROBE_SHAPE_DASH_UNIT(line) ((void)(line))
#define PROBE_SHAPE_DASH_STORE(d, v, line) ((void)0)
#define PROBE_SHAPE_MARK(addr)     ((void)0)
#define PROBE_SHAPE_ROAD_BEFORE()  ((void)0)
#define PROBE_SHAPE_ROAD_AFTER()   ((void)0)
#define PROBE_SHAPE_PHASE(n)       ((void)0)
#define PROBE_SHAPE_FRAME()        ((void)0)

#endif /* REVS_SHAPE */

#ifdef __cplusplus
}
#endif
