/* shape.cpp — the input-distribution counters described in shape.h. */
#include "shape.h"

#ifdef REVS_SHAPE

extern "C" volatile unsigned char mem[65536];

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

/* One dirty TEST — the `LDY table,X` at the head of any of the 40 column units.  Counted
   separately from the stores because a unit that finds its source zero still ran. */
void shape_dash_unit(void)
{
    g_shapeDashUnits++;
}

void shape_dash_before(void)
{
    unsigned bytes, cols;
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
    scan(&bytes, &cols, /*credit*/0);
    g_shapeDashLeft     += bytes;
    g_shapeDashColsLeft += cols;
    g_shapeDashLastLeft  = (unsigned short)bytes;
}

#endif /* REVS_SHAPE */
