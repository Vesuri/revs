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

/* One cell store, before it lands.  ⭐ Comparing against what is already there is the whole
   point: it measures the sweep's REDUNDANCY directly instead of deriving it from the carry
   semantics, so the census cannot be wrong in the same way my reasoning could be. */
void shape_dash_store(unsigned dst, unsigned value, unsigned line)
{
    const unsigned x = line & 0x7Fu;
    const int changed = (mem[dst] != (unsigned char)value);
    if (changed) s_lineChanged[x]++;
    run_census_store(x, dst, changed);
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

void shape_view_phase(int idx) { s_shapeViewPhase = (idx >= 0 && idx < 3) ? idx : 0; }

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

void shape_phase_mark(int id)
{
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
