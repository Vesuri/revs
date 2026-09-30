/* shape.cpp — the input-distribution counters described in shape.h. */
#include "shape.h"

#ifdef REVS_SHAPE

/* ⚠⚠ INSIDE the guard AND host-only, and the second half was learned the hard way: the
   m68k-amiga cross toolchain has no <stdio.h>, and this file is in the Amiga source list
   unconditionally, so an include out here breaks `amiga/make` outright — and an include inside
   `REVS_SHAPE` alone breaks `make SHAPE=1`, which is the very build `amiga/view_census.gdb` and
   `amiga/run_census.gdb` document as their own.  A counter file readable on the target must not
   need a host library to compile; the one diagnostic that wants stdio is a host debug aid and is
   guarded to match. */
#ifndef REVS_PLATFORM_AMIGA
#include <stdio.h>
#include <stdlib.h>
#endif
#include "bbc_screen.h"
#include "view_span.h"      /* ⭐ the Stage A oracle compares against view_span_line */

#include "../cpu/m68k_math.h"
#include "../cpu/mem_decl.h"
#include "../gen/mem.h"
extern "C" MEM_QUAL unsigned char mem[65536];

/* The dashboard sweep's own geometry, from docs/static-map.md item 10 — 40 column blocks
   $80 apart based at $3000, swept with X from $4F down to $2C ($7BE2 loads X = $4F, the
   tail at $7EEE compares X against $2C).  Named here rather than inlined so a future
   disassembly correction has one place to land. */
#define DASH_BLOCK_BASE   0x3000u
#define DASH_BLOCK_STRIDE 0x0080u
#define DASH_COLUMNS      40u
#define DASH_X_LO         0x2Cu
#define DASH_X_HI         0x4Fu          /* inclusive */

extern "C" {
volatile unsigned long g_shapeDashCalls     = 0;
volatile unsigned long g_shapeDashDirty     = 0;
volatile unsigned long g_shapeDashLeft      = 0;
volatile unsigned long g_shapeDashCols      = 0;
volatile unsigned long g_shapeDashColsLeft  = 0;
volatile unsigned short g_shapeDashLastDirty = 0;
volatile unsigned short g_shapeDashLastLeft  = 0;
volatile unsigned char  g_shapeDashLastCols  = 0;
volatile unsigned long g_shapeDashColHist[11] = {0};
volatile unsigned long g_shapeDashPerCol[DASH_COLUMNS] = {0};
volatile unsigned long g_shapeDashPerRow[DASH_X_HI - DASH_X_LO + 1] = {0};
volatile unsigned long g_shapeDashUnits = 0;
volatile unsigned short g_shapeDashLastUnits = 0;
}

/* One scan of the sweep's rectangle: total non-zero sources, and how many columns hold at
   least one.  `perCol` non-zero means "also credit each dirty column", which only the
   before-scan wants. */
static void scan(unsigned* bytesOut, unsigned* colsOut, int credit)
{
    unsigned bytes = 0, cols = 0;
    for (unsigned k = 0; k < DASH_COLUMNS; k++) {
        const unsigned base = DASH_BLOCK_BASE + k * DASH_BLOCK_STRIDE;
        unsigned n = 0;
        for (unsigned x = DASH_X_LO; x <= DASH_X_HI; x++)
            if (mem[base + x]) {
                n++;
                if (credit) g_shapeDashPerRow[x - DASH_X_LO]++;
            }
        if (n) {
            cols++;
            if (credit) g_shapeDashPerCol[k]++;
        }
        bytes += n;
    }
    *bytesOut = bytes;
    *colsOut  = cols;
}

/* Per-sweep working state: filled by the unit/store hooks, rolled up in shape_dash_after. */
static unsigned short s_lineUnits[128];
static unsigned short s_lineChanged[128];
static unsigned short s_lineDirty[128];

/* One dirty TEST — the `LDY table,X` at the head of any of the 40 column units.  Counted
   separately from the stores because a unit that finds its source zero still ran. */
void shape_dash_unit(unsigned line)
{
    g_shapeDashUnits++;
    s_lineUnits[line & 0x7Fu]++;
}

/* ── the run census (shape.h) ────────────────────────────────────────────────────────── */

extern "C" {
volatile unsigned long g_shapeRunSweeps = 0;
volatile unsigned long g_shapeRunStores = 0;
volatile unsigned long g_shapeRunEvents = 0;
volatile unsigned long g_shapeRunChanged = 0;
volatile unsigned long g_shapeRunUnion = 0;
volatile unsigned long g_shapeRunRuns = 0;
volatile unsigned long g_shapeRunExtent = 0;
volatile unsigned long g_shapeRunLinesAny = 0;
volatile unsigned long g_shapeRunLinesNone = 0;
volatile unsigned long g_shapeRunUnionHist[9] = {0};
volatile unsigned long g_shapeRunRunsHist[9] = {0};
volatile unsigned long g_shapeRunPerUnion[128] = {0};
volatile unsigned long g_shapeRunArmLost = 0;
}

/* THE LATCH.  `VIEW_UNIT` calls the unit hook, then `view_consume` (which reports its arm),
   then the store hook — so the arm of the store about to land is the last one reported.  The
   unit hook clears it to 0xFF so a stop unit's consume (which has no store) cannot leak its arm
   into the next unit's store, and a store that finds 0xFF counts itself lost instead of guessing
   clean. */
static unsigned char s_armLatch = 0xFFu;

/* Per line paint, in visit order.  `s_runPrevDst` is the last union cell's address + 1 so that
   zero means "no union cell yet" without stealing an address. */
static unsigned s_runFirstDst[128];
static unsigned s_runPrevDst[128];
static unsigned short s_runStores[128];
static unsigned short s_runEvents[128];
static unsigned short s_runUnion[128];
static unsigned short s_runRuns[128];

static unsigned hist_bucket(unsigned n)
{
    if (n <= 4u) return n;                  /* 0,1,2,3,4 exactly — the interesting end */
    if (n <= 8u) return 5u;
    if (n <= 16u) return 6u;
    if (n <= 32u) return 7u;
    return 8u;
}

static void run_census_store(unsigned x, unsigned dst, int changed)
{
    const unsigned char arm = s_armLatch;
    int isEvent;
    s_armLatch = 0xFFu;
    if (arm == 0xFFu) { g_shapeRunArmLost++; isEvent = 0; }
    else              isEvent = (arm != 0u);        /* 1 = non-zero source, 2 = forced entry */
    s_runStores[x]++;
    if (isEvent) s_runEvents[x]++;
    if (!isEvent && !changed) return;               /* a cell the scheme could skip entirely */
    s_runUnion[x]++;
    if (!s_runPrevDst[x]) { s_runFirstDst[x] = dst; s_runRuns[x]++; }
    else if (dst != s_runPrevDst[x] - 1u + 8u) s_runRuns[x]++;
    s_runPrevDst[x] = dst + 1u;
}

static void run_census_before(void)
{
    unsigned x;
    for (x = 0; x < 128; x++) {
        s_runFirstDst[x] = 0; s_runPrevDst[x] = 0;
        s_runStores[x] = 0; s_runEvents[x] = 0; s_runUnion[x] = 0; s_runRuns[x] = 0;
    }
    s_armLatch = 0xFFu;
}

static void run_census_after(void)
{
    unsigned x;
    g_shapeRunSweeps++;
    for (x = 0; x < 128; x++) {
        if (!s_runStores[x]) continue;              /* the sweep never stored on this line */
        g_shapeRunStores += s_runStores[x];
        g_shapeRunEvents += s_runEvents[x];
        g_shapeRunChanged += s_lineChanged[x];
        g_shapeRunUnion += s_runUnion[x];
        g_shapeRunRuns += s_runRuns[x];
        g_shapeRunPerUnion[x] += s_runUnion[x];
        g_shapeRunUnionHist[hist_bucket(s_runUnion[x])]++;
        g_shapeRunRunsHist[hist_bucket(s_runRuns[x])]++;
        if (s_runUnion[x]) {
            g_shapeRunLinesAny++;
            /* the extent a producer-known bound would scan: first..last union cell inclusive */
            g_shapeRunExtent += ((s_runPrevDst[x] - 1u) - s_runFirstDst[x]) / 8u + 1u;
        } else {
            g_shapeRunLinesNone++;
        }
    }
}


/* ── ⭐⭐ THE SPAN CENSUS: THE COLOUR-RUN SHAPE OF ONE PAINTED LINE ──────────────────────────
   The run census above counts the cells that CHANGED, which sizes a SKIP scheme.  This counts
   how many contiguous COLOUR RUNS the sweep paints, which is a different question and the one
   `docs/span-render-plan.md` turns on: a direct-to-bitplane renderer does not store cells,
   it emits SPANS, so the run list — not the cell count — is its workload.

   ⭐ WHAT IT ANSWERED (driving, $63 = $2C..$32, 295 sweeps): 2155 stores a sweep are
   **144 solid runs of 13.9 cells + 146 mixed runs of 1.04** — i.e. the road scene is a span
   list of ~1.9 solid spans and ~1.9 individual boundary cells per painted line, and 93% of
   all painted bytes are one of the FOUR solid MODE 5 values.  That is what makes the direct
   renderer a trapezoid fill instead of a region problem.

   ⚠ Believe it because it agrees with two instruments it shares no code with: line paints
   77.26 against the phase table's independently counted 36+16+25 = 77, and stores 2155 against
   its independently counted 1442+426+282 = 2150 units.  ⚠⚠ THAT AGREEMENT IS THE WHOLE
   VERIFICATION, AND THE FIRST VERSION FAILED IT QUIETLY: hooked only from VIEW_UNIT it read
   2089 against 2150, because `paint_lines_short` stores its three CHAIN-BOUNDARY cells through
   REVS_PLOT_CELL directly — and those are exactly the composed edge bytes, so the miss landed
   entirely on the number the span model cares about (mixed runs read 94 instead of 146, a 36%
   under-count).  A 3% gap in a total was a 36% error in a component; chase a percent that does
   not close.  ⚠⚠ And it must be read from a MOVING car — a parked scene paints a tidy, tiny
   span list and every conclusion drawn from it is about a scene nobody drives through, so the
   report prints `road_speed` beside its numbers.  The shape is trajectory-robust: the race
   proper reads 2148 / 280 runs / 143 solid / 137 mixed. */
extern "C" {
volatile unsigned long g_scoutSweeps  = 0;
volatile unsigned long g_scoutPaints  = 0;   /* line paints with >= 1 store           */
volatile unsigned long g_scoutStores  = 0;   /* cell stores (the cost today)          */
volatile unsigned long g_scoutRuns    = 0;   /* contiguous equal-value runs           */
volatile unsigned long g_scoutRunHist[17];   /* runs per line paint, 0..15, 16 = more */
volatile unsigned long g_scoutLenHist[9];    /* run length in cells: 1,2,3,4,5-8,9-16,17-24,25-32,33+ */
volatile unsigned long g_scoutGapCells = 0;  /* cells NOT covered by the 40 (unpainted) */
volatile unsigned long g_scoutValues[256];   /* distinct painted byte values          */
volatile unsigned long g_scoutSolid   = 0;   /* stores of a SOLID MODE 5 byte (00/0F/F0/FF) */
volatile unsigned long g_scoutMixed   = 0;   /* stores of a MIXED byte (a boundary inside a cell) */
volatile unsigned long g_scoutSolidRuns = 0; /* runs whose value is solid                 */
volatile unsigned long g_scoutMixedRuns = 0; /* runs whose value is mixed                 */
/* ⭐ WHAT THE SWEEP DOES NOT OWN.  Only 27 of a line's 40 cells are stored, so 1001 cells a
   sweep keep last frame's bytes — and a direct renderer has to know whether that is the SKY
   beyond the road's ends (which it must leave alone, because some other pass owns it) or
   INTERIOR cells (which would mean the span list is not contiguous and the whole trapezoid
   model is wrong).  first/last give the extent, gaps counts the holes inside it. */
volatile unsigned long g_scoutFirstSum = 0;  /* sum of the first painted cell per line paint */
volatile unsigned long g_scoutLastSum  = 0;  /* ...and the last                              */
volatile unsigned long g_scoutInterior = 0;  /* cells inside [first,last] that were NOT stored */
volatile unsigned long g_scoutCellHist[40];  /* per cell, how many line paints stored it     */
volatile unsigned long g_scoutFullLines = 0; /* line paints covering all 40 cells             */
/* ⭐ THE RASTER MAP — cells stored per DISPLAY LINE, which is what a direct renderer has to
   own.  `offset = charRow*320 + cell*8 + lineInRow` (bbc_screen.h), so the display line is
   charRow*8 + lineInRow and this profile says exactly which rows the sweep paints and how
   wide.  A row the sweep never touches is owned by some other pass and must stay that way. */
volatile unsigned long g_scoutRowCells[208];
volatile unsigned long g_scoutRowRuns[208];
/* ⭐⭐ WHO WRITES THE SOURCE BLOCKS — the one thing the span model cannot assume.  The sweep
   expands road surface AND object pixels (cars, markers, trees) out of the same 40 blocks, so
   a renderer that emits spans from `surface_edge` alone would drop every object.  Attributing
   source-cell writes to the PHASE that made them sizes the composited object layer:
   phases 14..17 are the object plotters, 18 the dash-edge colour fill, 11 draw_road. */
volatile unsigned long g_scoutSrcByPhase[40];
}

/* ⭐ A MODE 5 byte holds four 2-bit pixels bit-interleaved (px0 = bits 7,3; px1 = 6,2;
   px2 = 5,1; px3 = 4,0), so exactly four of the 256 values paint a cell in ONE colour:
   $00 -> 0, $0F -> 1, $F0 -> 2, $FF -> 3.  Anything else puts a colour BOUNDARY inside the
   cell, which is the only thing a span filler cannot emit as a solid word. */
static int scout_is_solid(unsigned char v)
{
    return v == 0x00u || v == 0x0Fu || v == 0xF0u || v == 0xFFu;
}
static unsigned char s_scPrevVal[128];
static signed   char s_scPrevCell[128];
static unsigned char s_scRuns[128];
static unsigned char s_scStores[128];
static unsigned char s_scRunLen[128];
static unsigned char s_scRow[128];
static unsigned char s_scFirst[128];
static unsigned char s_scLast[128];

static unsigned scout_len_bucket(unsigned n)
{
    if (n <= 4u) return n - 1u;
    if (n <= 8u) return 4u;
    if (n <= 16u) return 5u;
    if (n <= 24u) return 6u;
    if (n <= 32u) return 7u;
    return 8u;
}

static void scout_close_run(unsigned x)
{
    if (s_scRunLen[x]) { g_scoutRuns++; g_scoutLenHist[scout_len_bucket(s_scRunLen[x])]++;
                         g_scoutRowRuns[s_scRow[x]]++;
                         if (scout_is_solid(s_scPrevVal[x])) g_scoutSolidRuns++;
                         else                                g_scoutMixedRuns++;
                         s_scRuns[x]++; s_scRunLen[x] = 0; }
}

static void scout_close_line(unsigned x)
{
    scout_close_run(x);
    if (s_scStores[x]) {
        g_scoutFirstSum += s_scFirst[x];
        g_scoutLastSum  += s_scLast[x];
        {   const unsigned extent = (unsigned)s_scLast[x] - (unsigned)s_scFirst[x] + 1u;
            if (extent > s_scStores[x]) g_scoutInterior += extent - s_scStores[x];
            if (s_scStores[x] >= 40u)   g_scoutFullLines++; }
        g_scoutPaints++;
        g_scoutStores += s_scStores[x];
        g_scoutRunHist[s_scRuns[x] > 16u ? 16u : s_scRuns[x]]++;
        if (s_scStores[x] < 40u) g_scoutGapCells += 40u - s_scStores[x];
    }
    s_scRuns[x] = 0; s_scStores[x] = 0; s_scPrevCell[x] = -1;
}

static void scout_store(unsigned dst, unsigned value, unsigned x)
{
    /* ⚠⚠ `revs_divu16`/`revs_modu16`, NOT `/` and `%`: a bare `off % BBC_SCREEN_BPR` promotes
       to int and emits `__umodsi3`, which the 68000 does not have — `amiga/make`'s muldiv-audit
       fails the link (CLAUDE.md §NEVER emit a 32-bit software mul/div).  `off` is < 8320, so a
       32/16 DIVU is exact. */
    unsigned off  = (dst - BBC_SCREEN_BASE) & 0xFFFFu;
    unsigned cell = (off >= BBC_SCREEN_BYTES) ? 0xFFu
                  : (unsigned)(revs_modu16(off, BBC_SCREEN_BPR) / 8u);
    if (off < BBC_SCREEN_BYTES) {
        const unsigned row = (unsigned)revs_divu16(off, BBC_SCREEN_BPR) * 8u + (off & 7u);
        if (row < 208u) { g_scoutRowCells[row]++; s_scRow[x] = (unsigned char)row; }
    }
    /* the sweep walks cells left to right, so a non-advancing cell is a NEW line paint */
    if (s_scPrevCell[x] >= 0 && (int)cell <= s_scPrevCell[x]) scout_close_line(x);
    if (s_scRunLen[x] && (unsigned char)value != s_scPrevVal[x]) scout_close_run(x);
    s_scPrevVal[x]  = (unsigned char)value;
    s_scPrevCell[x] = (signed char)cell;
    if (cell < 40u) {
        g_scoutCellHist[cell]++;
        /* seed on the line paint's FIRST store — a zero-initialised `first` would silently
           report cell 0 as the extent's start for every line. */
        if (!s_scStores[x]) { s_scFirst[x] = (unsigned char)cell; s_scLast[x] = (unsigned char)cell; }
        else if (cell < s_scFirst[x]) s_scFirst[x] = (unsigned char)cell;
        else if (cell > s_scLast[x])  s_scLast[x]  = (unsigned char)cell;
    }
    s_scRunLen[x]++;
    s_scStores[x]++;
    g_scoutValues[(unsigned char)value]++;
    if (scout_is_solid((unsigned char)value)) g_scoutSolid++; else g_scoutMixed++;
}

void scout_sweep_end(void)
{
    g_scoutSweeps++;
    for (unsigned x = 0; x < 128u; x++) scout_close_line(x);
}

void scout_report(void)
{
#ifndef REVS_PLATFORM_AMIGA
    const unsigned long n = g_scoutSweeps ? g_scoutSweeps : 1u;
    unsigned distinct = 0;
    for (unsigned i = 0; i < 256u; i++) if (g_scoutValues[i]) distinct++;
    printf("SCOUT sweeps=%lu  line-paints/sweep=%lu.%02lu  stores/sweep=%lu.%02lu  "
                "RUNS/sweep=%lu.%02lu  cells/run=%lu.%02lu  runs/paint=%lu.%02lu  "
                "distinct-bytes=%u  unpainted-cells/sweep=%lu.%02lu\n",
                g_scoutSweeps,
                g_scoutPaints / n, (g_scoutPaints * 100 / n) % 100,
                g_scoutStores / n, (g_scoutStores * 100 / n) % 100,
                g_scoutRuns / n,   (g_scoutRuns * 100 / n) % 100,
                g_scoutRuns ? g_scoutStores / g_scoutRuns : 0,
                g_scoutRuns ? (g_scoutStores * 100 / g_scoutRuns) % 100 : 0,
                g_scoutPaints ? g_scoutRuns / g_scoutPaints : 0,
                g_scoutPaints ? (g_scoutRuns * 100 / g_scoutPaints) % 100 : 0,
                distinct,
                g_scoutGapCells / n, (g_scoutGapCells * 100 / n) % 100);
    /* ⚠⚠ THE PARKED-CAR TRAP, same guard the run census carries: a static scene paints a
       tidy, tiny run list and every conclusion drawn from it is about a scene nobody drives
       through.  road_speed ($63) must be non-zero for these numbers to mean anything. */
    printf("SCOUT   road_speed $63=%02X (must be non-zero: a parked scene is a different "
           "workload)\n", mem[MEM_road_speed]);
    printf("SCOUT   solid/mixed: stores %lu.%02lu solid + %lu.%02lu mixed per sweep; "
           "runs %lu.%02lu solid + %lu.%02lu mixed\n",
           g_scoutSolid / n, (g_scoutSolid * 100 / n) % 100,
           g_scoutMixed / n, (g_scoutMixed * 100 / n) % 100,
           g_scoutSolidRuns / n, (g_scoutSolidRuns * 100 / n) % 100,
           g_scoutMixedRuns / n, (g_scoutMixedRuns * 100 / n) % 100);
    printf("SCOUT   extent: first=%lu.%02lu last=%lu.%02lu  interior-gaps/sweep=%lu.%02lu  "
           "full-40 line paints=%lu.%02lu\n",
           g_scoutPaints ? g_scoutFirstSum / g_scoutPaints : 0,
           g_scoutPaints ? (g_scoutFirstSum * 100 / g_scoutPaints) % 100 : 0,
           g_scoutPaints ? g_scoutLastSum / g_scoutPaints : 0,
           g_scoutPaints ? (g_scoutLastSum * 100 / g_scoutPaints) % 100 : 0,
           g_scoutInterior / n, (g_scoutInterior * 100 / n) % 100,
           g_scoutFullLines / n, (g_scoutFullLines * 100 / n) % 100);
    printf("SCOUT   source-block cells written per phase (phase:cells/sweep):");
    for (unsigned i = 0; i < 40u; i++) if (g_scoutSrcByPhase[i] / n)
        printf(" %u:%lu", i, g_scoutSrcByPhase[i] / n);
    printf("\nSCOUT   per-display-line cells/sweep (row 0..207, 16 per line):");
    for (unsigned i = 0; i < 208u; i++) {
        if (!(i % 16u)) printf("\nSCOUT     row %3u:", i);
        printf(" %2lu", g_scoutRowCells[i] / n);
    }
    printf("\nSCOUT   per-display-line runs/sweep (row 0..207):");
    for (unsigned i = 0; i < 208u; i++) {
        if (!(i % 16u)) printf("\nSCOUT     row %3u:", i);
        printf(" %2lu", g_scoutRowRuns[i] / n);
    }
    printf("\nSCOUT   per-cell coverage (line paints storing cell 0..39):");
    for (unsigned i = 0; i < 40u; i++) printf(" %lu", g_scoutCellHist[i] / n);
    printf("\nSCOUT   runs-per-line-paint histogram (0..15,16+):");
    for (unsigned i = 0; i < 17u; i++) printf(" %lu", g_scoutRunHist[i]);
    printf("\nSCOUT   run-length histogram (1,2,3,4,5-8,9-16,17-24,25-32,33+):");
    for (unsigned i = 0; i < 9u; i++) printf(" %lu", g_scoutLenHist[i]);
    printf("\nSCOUT   painted byte values:");
    for (unsigned i = 0; i < 256u; i++) if (g_scoutValues[i])
        printf(" %02X:%lu", i, g_scoutValues[i] / n);
    printf("\n");
#endif
}

/* ── ⭐⭐⭐ THE STAGE A ORACLE: CAN THE SPAN RECORD REPRODUCE THE SWEEP'S PICTURE? ──────────
   The one question the replacement renderer turns on (docs/span-render-plan.md §10e/§10i), and
   it is answerable on the HOST with no emulator run: for every cell the sweep stores, compare
   the byte it stores against `view_span_line`'s prediction for that (line, cell).

   THE CLASSIFICATION IS THE MEASUREMENT, not the match rate.  A disagreement is not
   automatically a defect — the span list is the line's INTERIOR and two other things paint
   into the same forty cells — so each miss is attributed by what the sweep's own consume arm
   says about that cell's source byte:

     match         the prediction is what the sweep painted.  Stage A owns this cell outright.
     event-miss    the source was NON-ZERO, i.e. a producer composed this cell: a boundary at
                   pixel precision, or an object (car, tree, marker).  ⭐ These are §10b's
                   overlay and their COUNT is its size — the number to hold against the
                   census's independently measured ~1.9 mixed cells a line.
     carry-miss    the source was ZERO, so `view_consume` carried the byte from the cell to its
                   left.  ⚠⚠ THIS IS THE KILL SIGNAL: a zero source means the picture there is
                   pure RLE background, which is exactly what the classifier claims to know.
                   A carry-miss that is not downstream of an event-miss on the same line means
                   the interval structure is WRONG, and the offender list is what says how.

   ⚠ The prediction is rebuilt lazily on the first store of each line rather than hooked into
   the sweep's per-line prologue, so the sweep's hot code is untouched and phases 2 and 3 —
   which enter mid-chain with no prologue at all — are covered on the same terms as phase 1.
   The four `surface_edge` buffers and `horizon_extent` are written by the producers BEFORE the
   sweep and by nothing during it, so a lazy read is the same read.

   ⭐⭐ SABOTAGED SIX WAYS (250-frame driving window; control MATCH 92.56%, carry-miss 12719):

     breakpoints -1 cell        79.01%   75944   6.0x
     colour ignores `position`  83.79%   53010   4.2x
     classifier read on line^1  82.24%   62191   4.9x
     edge_3 buffer dropped      88.84%   30313   2.4x
     breakpoints +3 cells       88.31%   27176   2.1x
     breakpoints +1 cell        92.29%   12719   1.0x  <- argued, see below

   ⚠⚠ The +1 arm prints a carry-miss BYTE-IDENTICAL to the control, which is CLAUDE.md's named
   tell for a stale object file.  It is not one (MATCH and runs/line both moved, and the build
   rm's its objects), and the asymmetry against the -1 arm is the proof: `surface_colour_at_core`
   selects on `position >= edge`, so an interval starts AT `e`.  Shifting +1 therefore
   misclassifies exactly cell `e` — the BOUNDARY cell, which a producer writes at pixel
   precision — so the damage is charged to event-miss (1.40 -> 1.47/line, +1326, against the
   1427 cells MATCH lost).  Shifting -1 corrupts cell `e-1`, an ordinary interior cell nobody
   writes, and carry-miss goes 6x.  ⇒ the oracle sees interval defects; the +1 direction is
   simply hidden behind the overlay the renderer has to paint anyway.  */
extern "C" {
volatile unsigned long g_spanPredLines   = 0;   /* line paints a prediction was built for  */
volatile unsigned long g_spanPredSpans   = 0;   /* runs in those predictions               */
volatile unsigned long g_spanPredCells   = 0;   /* cells compared                          */
volatile unsigned long g_spanPredMatch   = 0;
volatile unsigned long g_spanPredEventMiss = 0;
volatile unsigned long g_spanPredCarryMiss = 0;
volatile unsigned long g_spanPredEventHit  = 0; /* matched anyway, though a producer wrote it */
volatile unsigned long g_spanPredMixed     = 0; /* misses whose stored byte is a MIXED cell  */
volatile unsigned long g_spanPredLost      = 0; /* the arm latch was lost (see s_armLatch)   */
volatile unsigned long g_spanPredSpanHist[VIEW_SPAN_MAX + 1] = {0};
/* ⭐⭐⭐ THE COMPOSITE MODEL — the cell counters above say the representation HOLDS; these say
   the PAINTER is exact.  `revs_plot_spans` fills the <=5 runs and then lets each event byte carry
   rightwards over the cells the fill got wrong, so its answer at cell c is
       carrySeen ? lastEventByte : spanColour(c)
   and that is what `g_spanPredComposite` compares against every byte the chain stores.  Cells at
   or after the line's first event match BY CONSTRUCTION (the overlay writes the chain's own byte),
   so what this actually measures is the region BEFORE the first event — which is exactly the
   place a wrong interval structure would show and the place the cell census cannot separate.
   ⚠⚠ g_spanPredCompMiss MUST BE 0.  A single mismatch is a cell the painter would get wrong with
   no producer byte to cover for it, and Stage A does not ship until it is zero.
   ⭐⭐⭐ IT IS 0 — 0 of 1 280 208 cells over a 250-frame driving window, and the overlay costs
   1.95 writes a line against the <=5 longword runs that cover all forty cells.  So the painter is
   EXACT, not approximate: `revs_plot_spans` needs no fallback arm and no per-cell compare.
   ⚠ Its sabotage control is free and was measured: model the three CHAIN-BOUNDARY stores as clean
   cells (drop arm 3) and MISS reads 30 197 with 22 580 of them DOWNSTREAM — one unmodelled carry
   update poisons every zero-source cell to its right, which is why the miss count landed within
   1% of the carry-miss count and looked like a defect in the representation.  ⭐ That is the
   general shape: in an RLE stream a missing EVENT and a wrong INTERVAL are indistinguishable by
   count, and only the arm split (`composite misses by arm`) separates them. */
volatile unsigned long g_spanPredComposite = 0;   /* cells the composite model predicted      */
volatile unsigned long g_spanPredCompMiss  = 0;   /* ...and got wrong.  ⚠⚠ MUST BE 0          */
volatile unsigned long g_spanPredCompUp    = 0;   /* of those, upstream of every event        */
volatile unsigned long g_spanPredOverlay   = 0;   /* cells the overlay must WRITE (its cost)  */
}

/* The offender list — ⭐ the whole point of recording it is that a COUNT cannot tell an
   off-by-one boundary from a car: (line, cell, predicted, painted) can. */
#define SPAN_PRED_OFFENDERS 12u
struct SpanPredOffender { unsigned char line, cell, pred, got, kind; };
static SpanPredOffender s_spOff[SPAN_PRED_OFFENDERS];
static unsigned s_spOffN = 0;
/* ⚠ The composite needs its OWN slots: the shared list fills up with the first twelve carry/event
   misses ever seen, so a composite offender never reached it and the first diagnosis had to be
   made from counts alone.  An instrument whose interesting case cannot be printed is half an
   instrument. */
static SpanPredOffender s_spCompOff[SPAN_PRED_OFFENDERS];
static unsigned s_spCompOffN = 0;
static unsigned long s_spCompMissArm[5] = {0};   /* by arm, slot 4 = latch lost */

static unsigned char s_spPredRow[VIEW_SPAN_CELLS];
static unsigned      s_spPredLine = 0xFFFFu;
/* the composite model's per-line state: the byte the last event on this line left carrying */
static unsigned      s_spCarry    = 0x100u;       /* >0xFF = no event on this line yet */

static void span_pred_line(unsigned line)
{
    ViewSpan spans[VIEW_SPAN_MAX];
    const unsigned n = view_span_line((unsigned char)line, spans);
    unsigned i, c;
    for (i = 0; i < n; i++) {
        const unsigned end = (i + 1u < n) ? spans[i + 1].start : VIEW_SPAN_CELLS;
        for (c = spans[i].start; c < end; c++) s_spPredRow[c] = spans[i].colour;
    }
    s_spPredLine = line;
    s_spCarry    = 0x100u;
    g_spanPredLines++;
    g_spanPredSpans += n;
    g_spanPredSpanHist[n > VIEW_SPAN_MAX ? VIEW_SPAN_MAX : n]++;
}

/* `arm` is `view_consume`'s own report for THIS cell, read out of the run census's latch
   before it is consumed: 0 = zero source, 1 = non-zero source, 2 = forced entry, $FF = lost —
   plus 3, which the CHAIN-BOUNDARY hook supplies for itself (shape.h). */
#define SPAN_ARM_EVENT(a) ((a) == 1u || (a) == 2u || (a) == 3u)
static void span_pred_store(unsigned dst, unsigned value, unsigned x, unsigned char arm)
{
    const unsigned off = (dst - BBC_SCREEN_BASE) & 0xFFFFu;
    unsigned cell;
    unsigned char pred;
    if (off >= BBC_SCREEN_BYTES) return;
    cell = (unsigned)(revs_modu16(off, BBC_SCREEN_BPR) / 8u);
    if (cell >= VIEW_SPAN_CELLS) return;
    if (x >= 80u) return;                      /* not a line the surface record covers */
    if (x != s_spPredLine) span_pred_line(x);

    pred = s_spPredRow[cell];

    /* ── the composite model (see the counters) — asked BEFORE the cell-wise classification so
          the two instruments stay independent of each other's early returns. ── */
    {
        unsigned upstream;
        unsigned char want;
        /* ⚠ THE EVENT IS APPLIED FIRST.  The overlay writes the producer's composed byte AT the
           event cell and only then carries it rightwards, so an event cell's answer is that byte
           — not the carry from its left.  (Ordering this the other way round made every event
           cell predict its left neighbour and read 10.6% miss.)  ⇒ event cells match BY
           CONSTRUCTION and what this measures is every OTHER cell, which is the point. */
        if (SPAN_ARM_EVENT(arm)) s_spCarry = (unsigned char)value;
        upstream = (s_spCarry > 0xFFu);
        want     = upstream ? pred : (unsigned char)s_spCarry;
        g_spanPredComposite++;
        if (want != (unsigned char)value) {
            g_spanPredCompMiss++;
            if (upstream) g_spanPredCompUp++;
            s_spCompMissArm[arm < 4u ? arm : 4u]++;
            if (s_spCompOffN < SPAN_PRED_OFFENDERS) {
                SpanPredOffender* o = &s_spCompOff[s_spCompOffN++];
                o->line = (unsigned char)x; o->cell = (unsigned char)cell;
                o->pred = want; o->got = (unsigned char)value; o->kind = 2u;
            }
        }
        /* the overlay's true write traffic: every cell whose answer the RUN FILL did not
           already put there — this is what the painter costs beyond its <=5 longword runs. */
        if (want != pred) g_spanPredOverlay++;
    }

    g_spanPredCells++;
    if (pred == (unsigned char)value) {
        g_spanPredMatch++;
        if (SPAN_ARM_EVENT(arm)) g_spanPredEventHit++;
        return;
    }
    if (!scout_is_solid((unsigned char)value)) g_spanPredMixed++;
    if (arm == 0xFFu)                  g_spanPredLost++;
    else if (SPAN_ARM_EVENT(arm))      g_spanPredEventMiss++;
    else {
        g_spanPredCarryMiss++;
        if (s_spOffN < SPAN_PRED_OFFENDERS) {
            SpanPredOffender* o = &s_spOff[s_spOffN++];
            o->line = (unsigned char)x; o->cell = (unsigned char)cell;
            o->pred = pred; o->got = (unsigned char)value; o->kind = 0u;
        }
        return;
    }
    /* keep a couple of event misses too, so the list shows both shapes side by side */
    if (s_spOffN < SPAN_PRED_OFFENDERS && (s_spOffN & 3u) != 3u) {
        SpanPredOffender* o = &s_spOff[s_spOffN++];
        o->line = (unsigned char)x; o->cell = (unsigned char)cell;
        o->pred = pred; o->got = (unsigned char)value; o->kind = 1u;
    }
}

void span_pred_report(void)
{
#ifndef REVS_PLATFORM_AMIGA
    const unsigned long n = g_spanPredLines ? g_spanPredLines : 1u;
    const unsigned long c = g_spanPredCells ? g_spanPredCells : 1u;
    unsigned i;
    printf("SPANPRED line-paints=%lu  runs/line=%lu.%02lu  cells=%lu  "
           "MATCH=%lu.%02lu%%  event-miss=%lu.%02lu/line  carry-miss=%lu  lost=%lu\n",
           g_spanPredLines,
           g_spanPredSpans / n, (g_spanPredSpans * 100 / n) % 100,
           g_spanPredCells,
           g_spanPredMatch * 100 / c, (g_spanPredMatch * 10000 / c) % 100,
           g_spanPredEventMiss / n, (g_spanPredEventMiss * 100 / n) % 100,
           g_spanPredCarryMiss, g_spanPredLost);
    printf("SPANPRED   of the matches, %lu were cells a producer had written; %lu of the "
           "misses are MIXED bytes (a boundary inside the cell)\n",
           g_spanPredEventHit, g_spanPredMixed);
    printf("SPANPRED  ⭐ COMPOSITE (what revs_plot_spans would paint): cells=%lu  MISS=%lu"
           " (upstream-of-every-event %lu)  overlay writes=%lu.%02lu/line\n",
           g_spanPredComposite, g_spanPredCompMiss, g_spanPredCompUp,
           g_spanPredOverlay / n, (g_spanPredOverlay * 100 / n) % 100);
    printf("SPANPRED   composite misses by arm (0=clean 1=source 2=forced 3=edge 4=lost): "
           "%lu %lu %lu %lu %lu  offenders:",
           s_spCompMissArm[0], s_spCompMissArm[1], s_spCompMissArm[2], s_spCompMissArm[3],
           s_spCompMissArm[4]);
    for (i = 0; i < s_spCompOffN; i++)
        printf(" X(l=%u,c=%u,want=%02X,got=%02X)", s_spCompOff[i].line, s_spCompOff[i].cell,
               s_spCompOff[i].pred, s_spCompOff[i].got);
    printf("\n");
    printf("SPANPRED   runs-per-line histogram (0..%u):", VIEW_SPAN_MAX);
    for (i = 0; i <= VIEW_SPAN_MAX; i++) printf(" %lu", g_spanPredSpanHist[i]);
    printf("\nSPANPRED   first offenders (kind C=carry E=event X=COMPOSITE):");
    for (i = 0; i < s_spOffN; i++)
        printf(" %c(l=%u,c=%u,pred=%02X,got=%02X)", "CEX"[s_spOff[i].kind],
               s_spOff[i].line, s_spOff[i].cell, s_spOff[i].pred, s_spOff[i].got);
    printf("\nSPANPRED   road_speed $63=%02X (a parked scene is a different workload)\n",
           mem[MEM_road_speed]);
#endif
}

/* One cell store, before it lands.  ⭐ Comparing against what is already there is the whole
   point: it measures the sweep's REDUNDANCY directly instead of deriving it from the carry
   semantics, so the census cannot be wrong in the same way my reasoning could be. */
static void dash_store_common(unsigned dst, unsigned value, unsigned line, unsigned spanArm)
{
    const unsigned x = line & 0x7Fu;
    const int changed = (mem[dst] != (unsigned char)value);
    /* ⚠ BEFORE run_census_store, which consumes the latch. */
    const unsigned char arm = s_armLatch;
    if (changed) s_lineChanged[x]++;
    run_census_store(x, dst, changed);
    scout_store(dst, value, x);
    span_pred_store(dst, value, x, spanArm > 0xFFu ? arm : (unsigned char)spanArm);
}

void shape_dash_store(unsigned dst, unsigned value, unsigned line)
{
    dash_store_common(dst, value, line, 0x100u);      /* whatever the latch says */
}

/* ⭐ The chain boundary (shape.h): the driver composed this byte itself, so the latch describes
   someone else's cell.  ⚠ The RUN CENSUS still sees the real latch — its published 426/32/16 is a
   different question and must not move underneath a diagnosis of the span model. */
void shape_dash_store_edge(unsigned dst, unsigned value, unsigned line)
{
    dash_store_common(dst, value, line, 3u);
}

/* ── the per-line census (shape.h) ---------------------------------------------------------- */

extern "C" {
volatile unsigned long g_shapeLineSweeps = 0;
volatile unsigned long g_shapeLineVisited = 0;
volatile unsigned long g_shapeLineRedundant = 0;
volatile unsigned long g_shapeLineCleanSrc = 0;
volatile unsigned long g_shapeLineUnits = 0;
volatile unsigned long g_shapeLineUnitsRedundant = 0;
volatile unsigned long g_shapeLineUnitsCleanSrc = 0;
volatile unsigned long g_shapeLineCleanButChanged = 0;
volatile unsigned long g_shapeLinePerCleanChanged[128];
volatile unsigned long g_shapeLineDirtyNoChange = 0;
volatile unsigned long g_shapeLinePerVisit[128] = {0};
volatile unsigned long g_shapeLinePerRedundant[128] = {0};
volatile unsigned long g_shapeLinePerUnits[128] = {0};
}

/* The sweep's whole X range is $03..$4F — phase 1 paints $4F..$2C, phases 2 and 3 the rest —
   so the census covers all of it, not just DASH_X_LO..DASH_X_HI. */
#define DASH_LINE_LO 0x03u
#define DASH_LINE_HI 0x4Fu

/* ⭐ THE SECOND HALF OF THE PREDICATE.  A line with no dirty source still paints its BACKGROUND
   byte into all forty cells, and that byte is surface_colours[view_line_surface[line] & 3] —
   which the road pass rewrites every frame.  So "no producer wrote my sources" does NOT mean
   "my picture is unchanged", and this array is what separates the two. */
static unsigned char s_lineBg[128];
static unsigned char s_lineBgSeen[128];
volatile unsigned long g_shapeCleanChangedBgMoved = 0;
volatile unsigned long g_shapeCleanChangedBgSame  = 0;
volatile unsigned long g_shapeCleanChangedBgSamePerLine[128];
/* ⭐⭐ THE PREDICATE A PRODUCER-SIDE SKIP WOULD ACTUALLY USE, and it is three-part, not one:
   a line may be skipped only if (1) no producer wrote any of its forty sources since the last
   sweep, (2) its BACKGROUND byte — surface_colours[view_line_surface & 3] — is the one it was
   painted with, and (3) the last paint was itself FLAT.  (3) is the one the first reading of
   this census missed: a line whose sources were dirty LAST frame carries road pixels that this
   frame's flat repaint has to erase, so "clean now" alone licenses a stale line. */
static unsigned char s_lineWasFlat[128];
volatile unsigned long g_shapeSkippablePredicate = 0;   /* lines the 3-part test would skip  */
volatile unsigned long g_shapeSkippableUnits     = 0;
volatile unsigned long g_shapeSkippableWrong     = 0;   /* ...that DID need a changed byte   */

/* ── marking completeness (shape.h) --------------------------------------------------------- */

extern "C" {
volatile unsigned long g_shapeMarkWritten  = 0;
volatile unsigned long g_shapeMarkMarked   = 0;
volatile unsigned long g_shapeMarkUnmarked = 0;
volatile unsigned long g_shapeMarkOver     = 0;
volatile unsigned long g_shapeMarkPerUnmarked[128];
}

/* ⭐⭐⭐ THE TWO NUMBERS THAT DECIDE WHETHER A PRODUCER-EMITTED EVENT LIST CAN BEAT THE
   TRANSPOSED SCAN (CLAUDE.md §price a skip scheme with TWO numbers).  The scan's cost is a
   fixed ~800 longword tests a sweep plus one body per EVENT; a producer-emitted list pays one
   body per STORE.  So the whole question is the ratio of producer STORES to real EVENTS, and
   nothing in the project had ever counted the first one.  Host only, SHAPE only. */
volatile unsigned long g_shapeSrcStores  = 0;   /* source-block bytes a producer WROTE  */
volatile unsigned long g_shapeSrcEvents  = 0;   /* ...bytes that actually CHANGED       */
volatile unsigned long g_shapeSrcSweeps  = 0;

static unsigned char s_lineMarked[128];                 /* set by the writer hooks       */
static unsigned char s_srcShadow[DASH_COLUMNS][128];    /* the rectangle after last sweep */
static unsigned char s_srcShadowSeen;

/* A hook at a known writer.  Anything outside the forty source blocks, or outside the sweep's
   own line range, is not a source byte and is ignored — the blocks are $80 apart but only
   $03..$4F of each is ever painted. */
void shape_mark_source(unsigned addr)
{
    unsigned off, line;
    addr &= 0xFFFFu;
    if (addr < DASH_BLOCK_BASE) return;
    off = addr - DASH_BLOCK_BASE;
    if (off >= DASH_COLUMNS * DASH_BLOCK_STRIDE) return;
    line = off & (DASH_BLOCK_STRIDE - 1u);
    if (line < DASH_LINE_LO || line > DASH_LINE_HI) return;
    s_lineMarked[line] = 1;
    g_shapeSrcStores++;
}

/* The ground truth: which lines changed between the end of the last sweep and the start of this
   one, whoever changed them.  Run before the sweep consumes anything. */
static void mark_census(void)
{
    unsigned x, k;
    unsigned char written[128];
    for (x = 0; x < 128; x++) written[x] = 0;
    for (k = 0; k < DASH_COLUMNS; k++) {
        const unsigned base = DASH_BLOCK_BASE + k * DASH_BLOCK_STRIDE;
        for (x = DASH_LINE_LO; x <= DASH_LINE_HI; x++)
            if (mem[base + x] != s_srcShadow[k][x]) written[x] = 1;
    }
    if (s_srcShadowSeen) {
        /* the changed-BYTE count is exactly the event population the scan must find */
        unsigned changed = 0;
        for (k = 0; k < DASH_COLUMNS; k++) {
            const unsigned base = DASH_BLOCK_BASE + k * DASH_BLOCK_STRIDE;
            for (x = DASH_LINE_LO; x <= DASH_LINE_HI; x++)
                if (mem[base + x] != s_srcShadow[k][x]) changed++;
        }
        g_shapeSrcEvents += changed;
        g_shapeSrcSweeps++;
        if ((g_shapeSrcSweeps % 256u) == 0u) {
            const unsigned long n = g_shapeSrcSweeps;
            printf("SRCSTORES  %lu sweeps: producer stores %lu.%02lu/sweep, real events "
                   "%lu.%02lu/sweep, ratio %lu.%02lu:1  (the scan pays ~800 longword tests "
                   "+ 1 body per EVENT; a producer list pays 1 body per STORE)\n",
                   n,
                   g_shapeSrcStores / n, (g_shapeSrcStores * 100 / n) % 100,
                   g_shapeSrcEvents / n, (g_shapeSrcEvents * 100 / n) % 100,
                   g_shapeSrcEvents ? g_shapeSrcStores / g_shapeSrcEvents : 0u,
                   g_shapeSrcEvents ? (g_shapeSrcStores * 100 / g_shapeSrcEvents) % 100 : 0u);
            fflush(stdout);
        }
        for (x = DASH_LINE_LO; x <= DASH_LINE_HI; x++) {
            if (written[x]) {
                g_shapeMarkWritten++;
                if (!s_lineMarked[x]) { g_shapeMarkUnmarked++; g_shapeMarkPerUnmarked[x]++; }
            } else if (s_lineMarked[x]) {
                g_shapeMarkOver++;
            }
            if (s_lineMarked[x]) g_shapeMarkMarked++;
        }
    }
    for (x = 0; x < 128; x++) s_lineMarked[x] = 0;
}

/* Re-take the shadow once the sweep has consumed (and zeroed) what it was going to. */
static void mark_snapshot(void)
{
    unsigned k, x;
    for (k = 0; k < DASH_COLUMNS; k++) {
        const unsigned base = DASH_BLOCK_BASE + k * DASH_BLOCK_STRIDE;
        for (x = 0; x < 128; x++) s_srcShadow[k][x] = mem[base + x];
    }
    s_srcShadowSeen = 1;
}

static void line_census_before(void)
{
    unsigned x, k;
    for (x = 0; x < 128; x++) { s_lineUnits[x] = 0; s_lineChanged[x] = 0; s_lineDirty[x] = 0; }
    for (k = 0; k < DASH_COLUMNS; k++) {
        const unsigned base = DASH_BLOCK_BASE + k * DASH_BLOCK_STRIDE;
        for (x = DASH_LINE_LO; x <= DASH_LINE_HI; x++)
            if (mem[base + x]) s_lineDirty[x]++;
    }
}

static void line_census_after(void)
{
    unsigned x;
    g_shapeLineSweeps++;
    for (x = DASH_LINE_LO; x <= DASH_LINE_HI; x++) {
        if (!s_lineUnits[x]) continue;                  /* the sweep never reached this line */
        g_shapeLineVisited++;
        g_shapeLineUnits += s_lineUnits[x];
        g_shapeLinePerVisit[x]++;
        g_shapeLinePerUnits[x] += s_lineUnits[x];
        if (!s_lineChanged[x]) {
            g_shapeLineRedundant++;
            g_shapeLineUnitsRedundant += s_lineUnits[x];
            g_shapeLinePerRedundant[x]++;
        }
        if (!s_lineDirty[x]) {
            g_shapeLineCleanSrc++;
            g_shapeLineUnitsCleanSrc += s_lineUnits[x];
            if (s_lineChanged[x]) { unsigned char bg =
                                        mem[MEM_surface_colours
                                            + (mem[MEM_view_line_surface + x] & 3)];
                                    if (s_lineBgSeen[x] && bg == s_lineBg[x])
                                        { g_shapeCleanChangedBgSame++;
                                          g_shapeCleanChangedBgSamePerLine[x]++; }
                                    else
                                        g_shapeCleanChangedBgMoved++;
                                    g_shapeLineCleanButChanged++;
                                    g_shapeLinePerCleanChanged[x]++;
#ifndef REVS_PLATFORM_AMIGA
                                    if (getenv("REVS_SHAPE_CBC"))
                                        fprintf(stderr, "CBC sweep %lu line $%02X\n",
                                                g_shapeLineSweeps, x);
#endif
                                  }
        } else if (!s_lineChanged[x]) {
            g_shapeLineDirtyNoChange++;
        }
        {
            unsigned char bg =
                mem[MEM_surface_colours + (mem[MEM_view_line_surface + x] & 3)];
            if (!s_lineDirty[x] && s_lineBgSeen[x] && bg == s_lineBg[x] && s_lineWasFlat[x]) {
                g_shapeSkippablePredicate++;
                g_shapeSkippableUnits += s_lineUnits[x];
                if (s_lineChanged[x]) g_shapeSkippableWrong++;
            }
            s_lineWasFlat[x] = (unsigned char)(s_lineDirty[x] == 0);
            s_lineBg[x] = bg;
            s_lineBgSeen[x] = 1;
        }
    }
}

void shape_dash_before(void)
{
    unsigned bytes, cols;
    mark_census();
    line_census_before();
    run_census_before();
    scan(&bytes, &cols, /*credit*/1);
    /* The unit count belongs to the sweep that just ENDED, so take its DELTA before this one
       runs — the cumulative total's low word would be a running sum, not a reading. */
    {
        static unsigned long prevUnits = 0;
        g_shapeDashLastUnits = (unsigned short)(g_shapeDashUnits - prevUnits);
        prevUnits = g_shapeDashUnits;
    }
    g_shapeDashCalls++;
    g_shapeDashDirty += bytes;
    g_shapeDashCols  += cols;
    g_shapeDashLastDirty = (unsigned short)bytes;
    g_shapeDashLastCols  = (unsigned char)cols;
    g_shapeDashColHist[cols / 4u]++;
}

void shape_dash_after(void)
{
    unsigned bytes, cols;
    line_census_after();
    run_census_after();       /* ⚠ after line_census_after: both read s_lineChanged */
    scan(&bytes, &cols, /*credit*/0);
    g_shapeDashLeft     += bytes;
    g_shapeDashColsLeft += cols;
    g_shapeDashLastLeft  = (unsigned short)bytes;
    mark_snapshot();
    scout_sweep_end();
}


/* ── the road pass ------------------------------------------------------------------------- */

extern "C" {
/* ── THE DASH-EDGE WALK (phase 18) — src/platform/shape.h has what the arms mean ─────────── */
volatile unsigned long g_shapeEdgeCalls = 0;
volatile unsigned long g_shapeEdgeWalks = 0;
volatile unsigned long g_shapeEdgeCells = 0;
volatile unsigned long g_shapeEdgeSkip = 0;
volatile unsigned long g_shapeEdgeTable = 0;
volatile unsigned long g_shapeEdgeColour = 0;
volatile unsigned long g_shapeEdgeFallback = 0;
volatile unsigned long g_shapeEdgeHist[16] = { 0 };

void shape_edge_call(void) { g_shapeEdgeCalls++; }

void shape_edge_walk(unsigned cells)
{
    g_shapeEdgeWalks++;
    g_shapeEdgeHist[cells > 127 ? 15 : cells / 8]++;
}

/* The arm codes are shape.h's four, in the order the walk tests them. */
void shape_edge_cell(unsigned arm)
{
    g_shapeEdgeCells++;
    switch (arm) {
        case 0: g_shapeEdgeSkip++;     break;
        case 1: g_shapeEdgeTable++;    break;
        case 2: g_shapeEdgeColour++;   break;
        case 3: g_shapeEdgeColour++; g_shapeEdgeFallback++; break;
        default: break;
    }
}

/* ── THE VIEW SWEEP'S CONSUME ARMS (phases 1/2/3) — shape.h carries the question ─────────── */
volatile unsigned long g_shapeViewUnits[3]  = { 0, 0, 0 };
volatile unsigned long g_shapeViewClean[3]  = { 0, 0, 0 };
volatile unsigned long g_shapeViewDirty[3]  = { 0, 0, 0 };
volatile unsigned long g_shapeViewForced[3] = { 0, 0, 0 };
volatile unsigned long g_shapeViewRuns[3]   = { 0, 0, 0 };
volatile unsigned long g_shapeViewRunBus[3] = { 0, 0, 0 };
volatile unsigned long g_shapeViewLines[3]  = { 0, 0, 0 };
volatile unsigned long g_shapeViewRunUnits[3] = { 0, 0, 0 };
volatile unsigned long g_shapeViewStops[3]  = { 0, 0, 0 };

/* ⚠ Its own index, NOT probe.h's g_viewPhaseIdx: the phase brackets are a PROBES build and this
   is measured on the host, where they are compiled out.  Set at the same three call sites. */
static int s_shapeViewPhase = 0;

/* ── the flat-line count's per-sweep tally (shape.h §THE TAKEOVER'S FLAT-LINE COUNT) ─────── */
volatile unsigned long g_shapeViewFlat[3]      = { 0, 0, 0 };
volatile unsigned long g_shapeViewFull[3]      = { 0, 0, 0 };
volatile unsigned long g_shapeViewFlatFull[3]  = { 0, 0, 0 };
volatile unsigned long g_shapeViewFlatLines[3] = { 0, 0, 0 };
volatile unsigned long g_shapeViewSweeps[3]    = { 0, 0, 0 };
volatile unsigned char g_shapeViewFlatMin[3]   = { 0xFFu, 0xFFu, 0xFFu };
volatile unsigned char g_shapeViewFlatMax[3]   = { 0, 0, 0 };
volatile unsigned char g_shapeViewFlatLast[3]  = { 0, 0, 0 };
volatile unsigned long g_shapeFbPoisonBytes    = 0;
volatile unsigned char g_shapeViewDstFirst[3]  = { 0xFFu, 0xFFu, 0xFFu };
volatile unsigned char g_shapeViewDstLast[3]   = { 0, 0, 0 };

static unsigned char s_flatThisSweep[3];
static unsigned char s_linesThisSweep[3];

void shape_view_phase(int idx)
{
    /* ⭐ THE SWEEP BOUNDARY, and there is exactly one: `view_paint_lines_core` sets phase 0 once
       per sweep before any line runs, and phases 1 and 2 only ever follow it within the same
       sweep.  So publishing the previous sweep's tally here is a complete, non-overlapping
       partition — see shape.h on why a whole-run ratio cannot size the takeover. */
    if (idx == 0) {
        for (unsigned p = 0; p < 3u; p++) {
            if (!s_linesThisSweep[p]) continue;      /* the phase did not run in that sweep */
            g_shapeViewSweeps[p]++;
            g_shapeViewFlatLast[p] = s_flatThisSweep[p];
            if (s_flatThisSweep[p] < g_shapeViewFlatMin[p])
                g_shapeViewFlatMin[p] = s_flatThisSweep[p];
            if (s_flatThisSweep[p] > g_shapeViewFlatMax[p])
                g_shapeViewFlatMax[p] = s_flatThisSweep[p];
            s_flatThisSweep[p] = 0;
            s_linesThisSweep[p] = 0;
        }
    }
    s_shapeViewPhase = (idx >= 0 && idx < 3) ? idx : 0;
}

/* The forty-source test, at the point the emitter would ask it: after the line has been stepped
   and its background byte resolved, before any unit runs.  `fullRun` is the caller's
   `view_stop_from(0) == 40` — counted apart from `flat` so §10p's "no planted stops in phase 1"
   claim is checked rather than trusted. */
void shape_view_flat(unsigned line, int fullRun, unsigned screenPtr)
{
    const int p = s_shapeViewPhase;
    unsigned k;
    int flat = 1;

    /* ⚠ `+ line` UNMASKED, exactly as `paint_cells` computes `srcp` — a `& 0x7F` here would be
       a no-op while `line < 128` and would silently read a DIFFERENT byte than the consumer if it
       ever were not, which is the one way this count could be wrong and look fine. */
    for (k = 0; k < 40u; k++)
        if (mem[MEM_view_src_blocks + (k << 7) + line]) { flat = 0; break; }

    /* ⭐ WHICH DISPLAY LINES THIS PHASE OWNS — the takeover's reader gate needs the row
       range, not the source-block line index, because the poison test and `plot_line_octant`'s
       undo table both speak display lines.  Same arithmetic as view_dst_line. */
    {   unsigned off = (screenPtr - BBC_SCREEN_BASE) & 0xFFFFu;
        if (off < BBC_SCREEN_BYTES) {
            unsigned row = off / BBC_SCREEN_BPR, sub = off % BBC_SCREEN_BPR;
            if (sub < BBC_SCREEN_LINES) {
                unsigned d = row * BBC_SCREEN_LINES + sub;
                if (d < g_shapeViewDstFirst[p]) g_shapeViewDstFirst[p] = (unsigned char)d;
                if (d > g_shapeViewDstLast[p])  g_shapeViewDstLast[p]  = (unsigned char)d;
            }
        }
    }

    g_shapeViewFlatLines[p]++;
    s_linesThisSweep[p]++;
    if (flat)            g_shapeViewFlat[p]++;
    if (fullRun)         g_shapeViewFull[p]++;
    if (flat && fullRun) { g_shapeViewFlatFull[p]++; s_flatThisSweep[p]++; }
}

/* ⭐⭐⭐ THE TAKEOVER'S READER GATE (shape.h §THE TAKEOVER'S READER GATE).  Invert every mem[]
   byte of the display lines `REVS_FB_POISON=<first>-<last>` names, right after the sweep wrote
   them.  Diff a fixed trajectory's 64 KB against an unpoisoned run: if every difference lies
   inside the poisoned rows, nothing in the game read them back.
   ⭐ An A/B switch must PRINT its own state (CLAUDE.md) — a build that parsed nothing reads
   exactly like a game that ignores the rows, which is the answer we are trying to establish. */
void shape_fb_poison(void)
{
    static int parsed = 0;
    static int first = -1, last = -1;

    if (!parsed) {
        parsed = 1;
        if (const char* e = getenv("REVS_FB_POISON")) {
            char* p = 0;
            long a = strtol(e, &p, 0);
            long b = (p && *p == '-') ? strtol(p + 1, 0, 0) : a;
            if (a >= 0 && b >= a && b < (long)BBC_SCREEN_HEIGHT) {
                first = (int)a; last = (int)b;
                printf("SHAPE fb-poison ARMED: display lines %d..%d, %d bytes/frame, ^= 0xFF\n",
                            first, last, (last - first + 1) * 40);
            } else {
                printf("SHAPE fb-poison REFUSED '%s' — want <first>-<last> within 0..%u\n",
                            e, (unsigned)BBC_SCREEN_HEIGHT - 1u);
            }
        } else {
            printf("SHAPE fb-poison off (set REVS_FB_POISON=<first>-<last> to arm)\n");
        }
        fflush(stdout);
    }

    if (first < 0) return;

    for (int d = first; d <= last; d++) {
        /* The BBC layout: a character row's eight scan lines are 1 byte apart inside an 8-byte
           cell, and the rows BBC_SCREEN_BPR apart.  Same arithmetic as view_dst_line, inverted. */
        unsigned base = BBC_SCREEN_BASE
                      + (unsigned)(d / BBC_SCREEN_LINES) * BBC_SCREEN_BPR
                      + (unsigned)(d % BBC_SCREEN_LINES);
        for (unsigned cell = 0; cell < 40u; cell++) {
            mem[base + cell * 8u] ^= 0xFFu;
            g_shapeFbPoisonBytes++;
        }
    }
}

/* 0 = clean (zero source, carried byte), 1 = dirty (zero it + translate), 2 = forced. */
void shape_view_arm(unsigned arm)
{
    s_armLatch = (unsigned char)arm;
    const int p = s_shapeViewPhase;
    g_shapeViewUnits[p]++;
    switch (arm) {
        case 0: g_shapeViewClean[p]++;  break;
        case 1: g_shapeViewDirty[p]++;  break;
        case 2: g_shapeViewForced[p]++; break;
        default: break;
    }
}

void shape_view_run(unsigned units, int busSafe)
{
    g_shapeViewRuns[s_shapeViewPhase]++;
    g_shapeViewRunUnits[s_shapeViewPhase] += units;
    if (!busSafe) g_shapeViewRunBus[s_shapeViewPhase]++;
}

void shape_view_stop(void) { g_shapeViewStops[s_shapeViewPhase]++; }

void shape_view_line(void) { g_shapeViewLines[s_shapeViewPhase]++; }

volatile unsigned long g_shapeRoadCalls = 0;
volatile unsigned long g_shapeRoadBytes = 0;
volatile unsigned long g_shapeRoadLines = 0;
volatile unsigned short g_shapeRoadLastBytes = 0;
volatile unsigned short g_shapeRoadLastLines = 0;
volatile unsigned char  g_shapeRoadFirstLine = 0;
volatile unsigned char  g_shapeRoadLastLine  = 0;
}

static unsigned char s_fbSnap[BBC_SCREEN_BYTES];

void shape_road_before(void)
{
    for (unsigned i = 0; i < BBC_SCREEN_BYTES; i++)
        s_fbSnap[i] = mem[BBC_SCREEN_BASE + i];
}

void shape_road_after(void)
{
    unsigned bytes = 0, lines = 0, first = 0xFFu, last = 0;
    /* Walk by DISPLAY LINE, not by address, so "lines touched" is a picture fact rather than a
       memory one — the BBC layout puts one character row's eight lines 320 bytes apart with a
       stride of 8 (bbc_screen.h). */
    for (unsigned row = 0; row < BBC_SCREEN_ROWS; row++) {
        for (unsigned line = 0; line < BBC_SCREEN_LINES; line++) {
            const unsigned off = row * BBC_SCREEN_BPR + line;
            unsigned n = 0;
            for (unsigned c = 0; c < BBC_SCREEN_CELLS; c++) {
                const unsigned o = off + c * BBC_SCREEN_LINES;
                if (mem[BBC_SCREEN_BASE + o] != s_fbSnap[o]) n++;
            }
            if (n) {
                const unsigned y = row * BBC_SCREEN_LINES + line;
                bytes += n;
                lines++;
                if (first == 0xFFu) first = y;
                last = y;
            }
        }
    }
    g_shapeRoadCalls++;
    g_shapeRoadBytes += bytes;
    g_shapeRoadLines += lines;
    g_shapeRoadLastBytes = (unsigned short)bytes;
    g_shapeRoadLastLines = (unsigned short)lines;
    g_shapeRoadFirstLine = (unsigned char)(first == 0xFFu ? 0 : first);
    g_shapeRoadLastLine  = (unsigned char)last;
}

/* ── per-phase frame-buffer attribution --------------------------------------------------- */

extern "C" {
volatile unsigned long g_shapePhaseBytes[40]  = {0};
volatile unsigned long g_shapePhaseFrames[40] = {0};
volatile unsigned char g_shapePhaseFirst[40]  = {0};
volatile unsigned char g_shapePhaseLast[40]   = {0};
}

static unsigned char s_phaseSnap[BBC_SCREEN_BYTES];
static int s_phaseOpen = -1;

/* Diff the 40 `$80`-spaced source blocks against the last phase boundary and re-baseline in the
   same pass, charging every changed cell to the phase that has just ENDED.  Host SHAPE build
   only: 5120 compares a boundary. */
static unsigned char s_scoutSrcSnap[DASH_COLUMNS][DASH_BLOCK_STRIDE];
static int s_scoutSrcPrevPhase = -1;
static void scout_src_boundary(int id)
{
    unsigned long changed = 0;
    for (unsigned k = 0; k < DASH_COLUMNS; k++) {
        const unsigned base = DASH_BLOCK_BASE + k * DASH_BLOCK_STRIDE;
        for (unsigned x = 0; x < DASH_BLOCK_STRIDE; x++) {
            const unsigned char v = mem[base + x];
            if (v != s_scoutSrcSnap[k][x]) { s_scoutSrcSnap[k][x] = v; changed++; }
        }
    }
    if (s_scoutSrcPrevPhase >= 0 && s_scoutSrcPrevPhase < 40)
        g_scoutSrcByPhase[s_scoutSrcPrevPhase] += changed;
    s_scoutSrcPrevPhase = (id >= 0 && id < 40) ? id : -1;
}

void shape_phase_mark(int id)
{
    scout_src_boundary(id);
    unsigned bytes = 0, first = 0xFFu, last = 0;
    /* ONE pass: compare and re-baseline in the same walk, so a boundary costs 8320 reads and
       only as many writes as there were changes. */
    for (unsigned row = 0; row < BBC_SCREEN_ROWS; row++) {
        for (unsigned line = 0; line < BBC_SCREEN_LINES; line++) {
            const unsigned off = row * BBC_SCREEN_BPR + line;
            unsigned n = 0;
            for (unsigned c = 0; c < BBC_SCREEN_CELLS; c++) {
                const unsigned o = off + c * BBC_SCREEN_LINES;
                const unsigned char v = mem[BBC_SCREEN_BASE + o];
                if (v != s_phaseSnap[o]) { s_phaseSnap[o] = v; n++; }
            }
            if (n) {
                const unsigned y = row * BBC_SCREEN_LINES + line;
                bytes += n;
                if (first == 0xFFu) first = y;
                last = y;
            }
        }
    }
    if (s_phaseOpen >= 0 && s_phaseOpen < 40 && bytes) {
        g_shapePhaseBytes[s_phaseOpen] += bytes;
        g_shapePhaseFrames[s_phaseOpen]++;
        if (g_shapePhaseFirst[s_phaseOpen] == 0 ||
            first < g_shapePhaseFirst[s_phaseOpen])
            g_shapePhaseFirst[s_phaseOpen] = (unsigned char)first;
        if (last > g_shapePhaseLast[s_phaseOpen])
            g_shapePhaseLast[s_phaseOpen] = (unsigned char)last;
    }
    s_phaseOpen = id;
}

/* ── the per-paint delta ------------------------------------------------------------------- */

extern "C" {
volatile unsigned long g_shapeFrameCalls = 0;
volatile unsigned long g_shapeFrameBytes = 0;
volatile unsigned short g_shapeFrameLast = 0;
volatile unsigned short g_shapeFrameMax  = 0;
volatile unsigned char  g_shapeFrameFirstLine = 0;
volatile unsigned char  g_shapeFrameLastLine  = 0;
volatile unsigned short g_shapeFrameLines = 0;
}

static unsigned char s_paintSnap[BBC_SCREEN_BYTES];

void shape_frame_delta(void)
{
    unsigned bytes = 0, lines = 0, first = 0xFFu, last = 0;
    for (unsigned row = 0; row < BBC_SCREEN_ROWS; row++) {
        for (unsigned line = 0; line < BBC_SCREEN_LINES; line++) {
            const unsigned off = row * BBC_SCREEN_BPR + line;
            unsigned n = 0;
            for (unsigned c = 0; c < BBC_SCREEN_CELLS; c++) {
                const unsigned o = off + c * BBC_SCREEN_LINES;
                const unsigned char v = mem[BBC_SCREEN_BASE + o];
                if (v != s_paintSnap[o]) { s_paintSnap[o] = v; n++; }
            }
            if (n) {
                const unsigned y = row * BBC_SCREEN_LINES + line;
                bytes += n; lines++;
                if (first == 0xFFu) first = y;
                last = y;
            }
        }
    }
    g_shapeFrameCalls++;
    g_shapeFrameBytes += bytes;
    g_shapeFrameLast = (unsigned short)bytes;
    if (bytes > g_shapeFrameMax) g_shapeFrameMax = (unsigned short)bytes;
    g_shapeFrameLines = (unsigned short)lines;
    g_shapeFrameFirstLine = (unsigned char)(first == 0xFFu ? 0 : first);
    g_shapeFrameLastLine  = (unsigned char)last;
}

#endif /* REVS_SHAPE */
