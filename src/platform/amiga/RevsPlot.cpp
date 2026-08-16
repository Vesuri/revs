/* RevsPlot — the direct-to-bitplane path for the 3D view rasteriser.  See revs_plot.h for what
   this is and why it is runs rather than cells; docs/direct-bitplane-plan.md §7d/§7e for the
   measurements that chose it.  Amiga-only and unvalidated by construction: the oracle is
   RevsScreen::decode(), not `make validate` (§5). */

#include "../revs_plot.h"

#ifdef REVS_DIRECT_PLOT

/* ⚠ NO <stdint.h>: every C++ TU here is force-included with framework/SASCCompat.h, which
   already typedefs int8_t/uint8_t — and as `char`, not `signed char`, so the compat stdint.h
   redeclares them incompatibly and the build fails.  The fixed-width names below come from it. */
#include "../bbc_screen.h"
#include "../../cpu/mem_decl.h"

extern "C" MEM_QUAL uint8_t mem[65536];

/* The shipping decode's own MODE 5 nibble tables and its full-convert entry point.  ⭐ SHARED, not
   re-derived: two definitions of the same expansion is how a plotter and its oracle agree on a bug
   (the "one definition" rule this project applies to bbc_ula_palette_write). */
extern "C" uint8_t g_bbcExpandLo[256], g_bbcExpandHi[256];
extern "C" void revs_screen_convert_reference(uint8_t* dst);

/* ── geometry ─────────────────────────────────────────────────────────────────────────────
   The BBC frame buffer is row*320 + cell*8 + line; the Amiga buffer is y*80 + cell, with plane 2
   forty bytes after plane 1.  The map between them is a pure function of the address, so it is a
   TABLE: one indexed word load per run, against a division per run if it were computed.  16.6 KB,
   built once — never lazily inside a frame (docs/m68k-optimisation.md: a table built on first use
   is the Atari port's 3.6-second freeze). */
static const unsigned kPlaneGap = BBC_SCREEN_WIDTH / 8;              /* 40 */
static const unsigned kRowBytes = (BBC_SCREEN_WIDTH / 8) * 2;        /* 80, interleaved */
#define FB_BYTES (BBC_SCREEN_BPR * BBC_SCREEN_ROWS)                  /* 8320 */

static uint16_t s_planeOff[FB_BYTES];
/* ⚠ The display line as a TABLE too, not `planeOff / kRowBytes`: a 32-bit divide emits __udivsi3
   and the 68000 has none — the link audit catches it, which is how this line exists. */
static uint8_t  s_lineOf[FB_BYTES];
static uint8_t  s_mapBuilt;
static uint8_t* s_target;

extern "C" {
volatile unsigned long  g_plotRuns      = 0;
volatile unsigned long  g_plotCells     = 0;
volatile unsigned short g_plotRunsLast  = 0;
volatile unsigned short g_plotCellsLast = 0;
volatile unsigned char  g_plotLineLo    = 0xFF;
volatile unsigned char  g_plotLineHi    = 0;
volatile unsigned long  g_plotNoTarget  = 0;
}

static void buildMap()
{
    unsigned off = 0;
    for (unsigned row = 0; row < BBC_SCREEN_ROWS; row++)
        for (unsigned cell = 0; cell < BBC_SCREEN_CELLS; cell++)
            for (unsigned line = 0; line < BBC_SCREEN_LINES; line++, off++) {
                const unsigned y = row * BBC_SCREEN_LINES + line;
                s_planeOff[off] = (uint16_t)(y * kRowBytes + cell);
                s_lineOf[off]   = (uint8_t)y;
            }
    s_mapBuilt = 1;
}

extern "C" void revs_plot_target(unsigned char* planeBase)
{
    if (!s_mapBuilt) buildMap();
    s_target = planeBase;
    g_plotRunsLast = 0;
    g_plotCellsLast = 0;
    g_plotLineLo = 0xFF;
    g_plotLineHi = 0;
}

/* One run: `cells` consecutive cells of one scan line, all the same MODE 5 byte.
   ⚠ The two zero-page bases the chain keeps ($70 for cells 0-31, $72 for 32-39) are $6700 and
   $6800 — 256 bytes apart, which is cell 32 of the SAME line — so a run may cross that seam and
   still be contiguous here.  That collapse is one of the reasons this layout was chosen (§7e). */
extern "C" void revs_plot_run(unsigned short addr, unsigned char value, unsigned short cells)
{
    uint8_t* const base = s_target;
    if (!base) { g_plotNoTarget++; return; }
    const unsigned off = (unsigned)addr - BBC_SCREEN_BASE;
    if (off >= FB_BYTES || !cells) return;

    uint8_t* p1 = base + s_planeOff[off];
    uint8_t* p2 = p1 + kPlaneGap;
    const uint8_t lo = g_bbcExpandLo[value];
    const uint8_t hi = g_bbcExpandHi[value];
    unsigned n = cells;

    /* ENDIAN-OK: a uniform-byte broadcast — all four bytes of each longword are the same, so no
       byte order can tell the difference.  This is not an alias of mem[]; it is the bitplane
       buffer, whose bytes the Amiga reads as bits.  ⚠ Both planes share the alignment because
       kPlaneGap is 40, a multiple of 4. */
    while (n && (((unsigned long)p1) & 3u)) { *p1++ = lo; *p2++ = hi; n--; }
    if (n >= 4) {
        const uint32_t lo4 = 0x01010101UL * lo;
        const uint32_t hi4 = 0x01010101UL * hi;
        uint32_t* q1 = (uint32_t*)(void*)p1;
        uint32_t* q2 = (uint32_t*)(void*)p2;
        do { *q1++ = lo4; *q2++ = hi4; n -= 4; } while (n >= 4);
        p1 = (uint8_t*)(void*)q1;
        p2 = (uint8_t*)(void*)q2;
    }
    while (n--) { *p1++ = lo; *p2++ = hi; }

    g_plotRuns++;
    g_plotCells += cells;
    g_plotRunsLast++;
    g_plotCellsLast = (unsigned short)(g_plotCellsLast + cells);
    {
        const unsigned char y = s_lineOf[off];
        if (y < g_plotLineLo) g_plotLineLo = y;
        if (y > g_plotLineHi) g_plotLineHi = y;
    }
}

extern "C" void revs_plot_cell(unsigned short addr, unsigned char value)
{
    revs_plot_run(addr, value, 1);
}

#ifdef REVS_DIRECT_CHECK
/* ⭐⭐ THE ORACLE.  Seed the target with what the shipping decode makes of mem[] BEFORE the sweep,
   let the sweep plot over it, then convert mem[] again and require the whole buffer to match.
   Comparing the WHOLE buffer, not just the plotted lines, is deliberate: it catches a plotter that
   writes the right pixels in the wrong place as well as one that writes the wrong pixels. */
static uint8_t s_ref[BBC_SCREEN_HEIGHT * 80];

extern "C" {
volatile unsigned long  g_plotChecks      = 0;
volatile unsigned long  g_plotMismatch    = 0;
volatile unsigned short g_plotMismatchOff = 0xFFFF;
}

extern "C" void revs_plot_check_before(void)
{
    if (!s_target) return;
    revs_screen_convert_reference(s_ref);
    for (unsigned i = 0; i < sizeof s_ref; i++) s_target[i] = s_ref[i];
}

extern "C" void revs_plot_check_after(void)
{
    if (!s_target) return;
    revs_screen_convert_reference(s_ref);
    g_plotChecks++;
    for (unsigned i = 0; i < sizeof s_ref; i++) {
        if (s_ref[i] != s_target[i]) {
            g_plotMismatch++;
            if (g_plotMismatchOff == 0xFFFFu) g_plotMismatchOff = (unsigned short)i;
        }
    }
}
#endif /* REVS_DIRECT_CHECK */

#endif /* REVS_DIRECT_PLOT */
