#pragma once
/* shape.h — INPUT-DISTRIBUTION COUNTERS for the render path (Phase 6 item 0, step 2).
 *
 * ⭐ WHY THIS EXISTS AND WHY IT IS NOT A PROFILER.  `probe.h`'s phase brackets say WHERE the
 * frame goes (the dashboard is 36.1%, the road subsystem 40%).  They cannot say WHAT the work
 * is, and `docs/direct-bitplane-plan.md` §6 step 2 / §7 both turn on exactly that: ⚑ the
 * predecessor project's -36% came from input-distribution counters, not from PC sampling.
 *
 * The one measurement that sizes TWO whole Phase 6 items (§7 dirty flags, §8 hardware sprites)
 * is: **how much of the dashboard sweep is actually dirty per frame?**  The `$7B00` overlay is
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
void shape_dash_unit(void);

#define PROBE_SHAPE_DASH_BEFORE()  shape_dash_before()
#define PROBE_SHAPE_DASH_AFTER()   shape_dash_after()
#define PROBE_SHAPE_DASH_UNIT()    shape_dash_unit()

#else

#define PROBE_SHAPE_DASH_BEFORE()  ((void)0)
#define PROBE_SHAPE_DASH_AFTER()   ((void)0)
#define PROBE_SHAPE_DASH_UNIT()    ((void)0)

#endif /* REVS_SHAPE */

#ifdef __cplusplus
}
#endif
