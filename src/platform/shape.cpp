/* shape.cpp — the input-distribution counters described in shape.h. */
#include <stdio.h>
#include <stdlib.h>
#include "shape.h"
#include "bbc_screen.h"

#ifdef REVS_SHAPE

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

/* One cell store, before it lands.  ⭐ Comparing against what is already there is the whole
   point: it measures the sweep's REDUNDANCY directly instead of deriving it from the carry
   semantics, so the census cannot be wrong in the same way my reasoning could be. */
void shape_dash_store(unsigned dst, unsigned value, unsigned line)
{
    if (mem[dst] != (unsigned char)value) s_lineChanged[line & 0x7Fu]++;
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
                                    if (getenv("REVS_SHAPE_CBC"))
                                        fprintf(stderr, "CBC sweep %lu line $%02X\n",
                                                g_shapeLineSweeps, x); }
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
    line_census_before();
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
    scan(&bytes, &cols, /*credit*/0);
    g_shapeDashLeft     += bytes;
    g_shapeDashColsLeft += cols;
    g_shapeDashLastLeft  = (unsigned short)bytes;
}


/* ── the road pass ------------------------------------------------------------------------- */

extern "C" {
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
