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

#ifdef REVS_SPAN_STATS
#define SPAN_STAT(stmt) do { stmt; } while (0)
#else
#define SPAN_STAT(stmt) ((void)0)
#endif

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

/* ⭐⭐ THE BROADCAST AS A TABLE, because the 68000 cannot make one cheaply.  `0x01010101 * b` is
   not a multiply on this target — GCC synthesises it as moveq/move.b/move.l/lsl.l #8/add.l/swap/
   clr.w/add.l, ~64 cycles, and a fill needs TWO of them (one per plane).  128 cycles a span for a
   value with 256 possible answers: build them once, beside the expansion tables they come from.
   ⚠ The pair is INTERLEAVED — one entry is {lo4, hi4} — so a span pays ONE address computation
   for both planes instead of two indexed longword loads off two different bases. */
static uint32_t s_expand4[256][2];

/* ⭐⭐ THE DIAGNOSTIC COUNTERS ARE A MEASURABLE TAX, SO THEY HAVE A SWITCH (`make SPANSTAT=0`).
   Each one is a `volatile` RMW on an absolute address — 32..50 cycles that cannot be coalesced —
   and there are six of them per span, ~220 cycles, which is 0.65 ms a frame over 21 spans.  That
   is a quarter of what deleting 851 chain units bought, so a span price quoted from a counting
   build is not the shipping price.  Same class as `SND_STAT()` in the VERTB ISR (CLAUDE.md).
   ⚠ The oracles and every committed .gdb script READ these, so the default is ON; SPANSTAT=0 is
   the instrument that prices them and it drops PROBE_SYMS_PLOT with them. */
#ifdef REVS_SPAN_STATS
extern "C" {
volatile unsigned long  g_plotRuns      = 0;
volatile unsigned long  g_plotCells     = 0;
volatile unsigned short g_plotRunsLast  = 0;
volatile unsigned short g_plotCellsLast = 0;
volatile unsigned char  g_plotLineLo    = 0xFF;
volatile unsigned char  g_plotLineHi    = 0;
volatile unsigned long  g_plotNoTarget  = 0;
volatile unsigned long  g_plotChainLines = 0;   /* lines the TAKEOVER painted cell by cell */
}
#endif

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
    /* ⚠ SHIFTS, NOT `0x01010101UL * v`: with a runtime operand that is a 32-bit multiply and the
       68000 has none — the link audit (muldiv-audit) would fail on __mulsi3.  Safe to read the
       expansion tables here: RevsScreen::initialize() fills them, and buildMap() is first reached
       from present(), long after. */
    for (unsigned v = 0; v < 256; v++) {
        uint32_t lo = g_bbcExpandLo[v], hi = g_bbcExpandHi[v];
        lo |= lo << 8;  lo |= lo << 16;
        hi |= hi << 8;  hi |= hi << 16;
        s_expand4[v][0] = lo;
        s_expand4[v][1] = hi;
    }
    s_mapBuilt = 1;
}

extern "C" void revs_plot_target(unsigned char* planeBase)
{
    if (!s_mapBuilt) buildMap();
    s_target = planeBase;
#ifdef REVS_SPAN_STATS
    g_plotRunsLast = 0;
    g_plotCellsLast = 0;
    g_plotLineLo = 0xFF;
    g_plotLineHi = 0;
#endif
}

/* One run: `cells` consecutive cells of one scan line, all the same MODE 5 byte.
   ⚠ The two zero-page bases the chain keeps ($70 for cells 0-31, $72 for 32-39) are $6700 and
   $6800 — 256 bytes apart, which is cell 32 of the SAME line — so a run may cross that seam and
   still be contiguous here.  That collapse is one of the reasons this layout was chosen (§7e). */
extern "C" void revs_plot_run(unsigned short addr, unsigned char value, unsigned short cells)
{
    uint8_t* const base = s_target;
    if (!base) { SPAN_STAT(g_plotNoTarget++); return; }
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
       kPlaneGap is 40, a multiple of 4.
       ⭐⭐⭐ AND THE LOOP'S SHAPE IS THE MEASUREMENT, not a detail.  Written the obvious way —
       a `do { *q1++ = lo4; *q2++ = hi4; n -= 4; } while (n >= 4)` over two `uint32_t*` — GCC
       kept the ENTRY pointers live (the byte tail below recomputes from them), so the loop
       pointer had to live somewhere else and it re-derived it through `d1`/`a3` every turn while
       re-reading lo4, hi4 AND the limit out of three loop-invariant stack slots:
           1: move.l a0,d1 / movea.l d1,a3 / move.l 28(sp),(a3)+ / move.l a3,d1
              move.l 32(sp),(a2)+ / cmpa.l 36(sp),a3 / bne.s 1b
       — 185 cycles per eight bytes written.  Measured on the span path (`SPANFILL=1` minus
       `SPANFILL=2`, parked) that was +5.47 ms/frame for 420 longword pairs; CLAUDE.md's
       frame-slot defect class exactly (the decode's own 38 -> 22 ms).  The fix below makes the
       ADVANCING pointers the only copies: the count is taken apart first, the byte tail
       continues from where the longword loop left off, and nothing else is live across the back
       edge, so lo4/hi4/limit stay in registers — 56 cycles per eight bytes.
       ⚠⚠ AND THAT WAS STILL 4.05 ms, WHICH IS WHY `revs_plot_span` NO LONGER COMES HERE.  Fixing
       the loop left the PREAMBLE — a variable cell count to split, an alignment step, a byte
       tail, two synthesised broadcasts, an eight-register movem — and for an 80-byte span the
       preamble is bigger than the stores.  This generic path is now only the boundary cells and
       the mirror, where `cells` really does vary. */
    {
        const uint32_t lo4 = 0x01010101UL * lo;
        const uint32_t hi4 = 0x01010101UL * hi;
        unsigned lw;

        while (n && (((unsigned long)p1) & 3u)) { *p1++ = lo; *p2++ = hi; n--; }
        lw = n >> 2;
        n &= 3u;
        while (lw--) {
            *(uint32_t*)(void*)p1 = lo4;  p1 += 4;
            *(uint32_t*)(void*)p2 = hi4;  p2 += 4;
        }
        while (n--) { *p1++ = lo; *p2++ = hi; }
    }

#ifdef REVS_SPAN_STATS
    g_plotRuns++;
    g_plotCells += cells;
    g_plotRunsLast++;
    g_plotCellsLast = (unsigned short)(g_plotCellsLast + cells);
    {
        const unsigned char y = s_lineOf[off];
        if (y < g_plotLineLo) g_plotLineLo = y;
        if (y > g_plotLineHi) g_plotLineHi = y;
    }
#endif
}

extern "C" void revs_plot_cell(unsigned short addr, unsigned char value)
{
    revs_plot_run(addr, value, 1);
}

/* ── ⭐⭐⭐ OWNERSHIP: WHICH DISPLAY LINES THE SPAN EMITTER PAINTED ──────────────────────────
   See revs_plot.h.  A source line's forty cells all land on ONE display line, so a span is one
   line and the claim is one byte.  ⚠ Nothing is claimed without a target: `revs_plot_run` drops
   the fill in that case (`g_plotNoTarget`), so the decode must still convert the line. */
extern "C" { unsigned char g_plotOwn[BBC_SCREEN_HEIGHT]; }

/* The header cannot include bbc_screen.h (see its stdint note), so 208 is spelled twice — once
   there as a literal and once here as the constant.  This is the two staying equal. */
typedef char revs_plot_own_size_check[(sizeof g_plotOwn == 208) ? 1 : -1];

/* ⭐⭐⭐ ONE WHOLE DISPLAY LINE, SPECIALISED — AND THE SPECIALISATION *IS* THE MEASUREMENT.
   Routed through the generic `revs_plot_run`, the fill measured +4.05 ms/frame for 1680 bytes
   (`SPANFILL=1` minus `SPANFILL=2`, parked) = ~17 cycles a byte, against a 68000 store floor of
   3.  Everything above the floor was preamble the span's own shape makes dead:

     - a span is ALWAYS forty cells from cell 0 (revs_plot.h), so `cells` was a parameter GCC
       could not fold across the TU boundary.  It is gone from the signature; the count is
       structural and the emitter's `view_stop_from(0) == 40` is what proves it.
     - a row base is `y * 80` off an AllocMem pointer (8-byte aligned, MEMF_CHIP), so BOTH planes
       are longword aligned: no alignment step, no count split, no byte tail.
     - the two planes are ADJACENT — p2 = p1 + 40 — so a display line is ONE 80-byte block:
       forty `lo` bytes then forty `hi`.  Twenty `move.l Dn,(An)+`, straight line, no loop.
     - the broadcasts are a table lookup now (s_expand4), not ~128 cycles of shift synthesis.
     - `off` dies before the first store, so the fill's only live values are the pointer and the
       two longwords — all in the call-clobbered set, so there is no movem and no stack slot.
       (CLAUDE.md: a hot loop's state lives in memory if anything keeps it alive across it.)

   ⚠⚠ movem.l WAS COSTED AND REJECTED, on paper, and the arithmetic is worth keeping because the
   instinct is strong: `movem.l d0-d7/a2-a3,(a0)` writes forty bytes in 8+8*10 = 88 cycles, i.e.
   2.2 cycles a byte against move.l's 3.0.  But the ten registers must be RELOADED with hi4
   between the planes (~40 cycles), the second movem in `d16(An)` costs 12+80, and eight of the
   ten are callee-saved so the function grows a 72/76-cycle movem prologue and epilogue pair:
   220 + 148 = 368 against 240.  ⭐ A uniform fill only beats `move.l` when ONE register load
   serves MANY stores, and a span changes value every line.  The 80-byte block is too short. */
extern "C" void revs_plot_span(unsigned short addr, unsigned char value)
{
    const unsigned off = (unsigned)addr - BBC_SCREEN_BASE;

    if (!s_target) { SPAN_STAT(g_plotNoTarget++); return; }
    if (off >= FB_BYTES) return;

    /* The claim and the counters go FIRST — see above: they are the last readers of `off`. */
    {
        const unsigned char y = s_lineOf[off];
        g_plotOwn[y] = 1;
#ifdef REVS_SPAN_STATS
        g_plotRuns++;
        g_plotCells += BBC_SCREEN_CELLS;
        g_plotRunsLast++;
        g_plotCellsLast = (unsigned short)(g_plotCellsLast + BBC_SCREEN_CELLS);
        if (y < g_plotLineLo) g_plotLineLo = y;
        if (y > g_plotLineHi) g_plotLineHi = y;
#endif
    }

#ifndef REVS_SPAN_NO_FILL
    /* ENDIAN-OK: a uniform-byte broadcast — every byte of each longword is the same, so no byte
       order can tell the difference.  And this is the bitplane buffer, whose bytes the Amiga
       reads as bits; it is never an alias of mem[]. */
    {
        uint32_t* q        = (uint32_t*)(void*)(s_target + s_planeOff[off]);
        const uint32_t lo4 = s_expand4[value][0];
        const uint32_t hi4 = s_expand4[value][1];

        *q++ = lo4; *q++ = lo4; *q++ = lo4; *q++ = lo4; *q++ = lo4;
        *q++ = lo4; *q++ = lo4; *q++ = lo4; *q++ = lo4; *q++ = lo4;
        *q++ = hi4; *q++ = hi4; *q++ = hi4; *q++ = hi4; *q++ = hi4;
        *q++ = hi4; *q++ = hi4; *q++ = hi4; *q++ = hi4; *q++ = hi4;
    }
#else
    /* ⚠⚠ `make SPANFILL=2` — THE PICTURE IS WRONG BY CONSTRUCTION, and this is the DECOMPOSITION
       the §10e checkpoint needs.  The span still CLAIMS its display line (so the chain's forty
       units are still deleted and the decode still leaves the line alone) and only the fill is
       gone.  Differencing this against SPANFILL=1's phase 24 prices the FILL on its own, with no
       bracket inside the sweep; against the plain control it prices the DELETION.
       The claimed lines then show whatever the buffer already held — never quote a picture, and
       never an FPS, from it.  The trajectory is unaffected either way: the visible frame buffer
       is write-only (measured, `make fbwrites --fill-reads`).  Counters stay live so the run
       still reports the same 21 spans / 840 cells as the filling build. */
#endif
}

extern "C" int revs_plot_has_target(void) { return s_target != 0; }

/* ── ⭐⭐⭐ THE TAKEOVER'S OWN LINE (docs/span-render-plan.md §10p step 3b) ────────────────────
   See revs_plot.h for what this is.  The chain, re-expressed against the Amiga's layout: the same
   forty cells, the same RLE, the same translation table — and the destination is the two bitplanes
   instead of forty `mem[]` bytes that a decode then has to convert back.

   ⭐⭐ WHY IT IS ~45 CYCLES A CELL AND NOT ~160.  The chain pays, per cell, a source load at stride
   $80, the translation, a `mem[]` store at stride 8, and then its share of the decode that reads
   that byte back and expands it.  Here the translation and BOTH expansion lookups sit behind the
   `if (s)` — `view_consume`'s zero source means "the byte to my left", so `lo`/`hi` are still the
   ones the last non-zero cell computed.  With two or three runs to a line that is five lookups a
   line, not eighty, and the common cell is a `tst.b d16(a0)` and two `move.b Dn,(An)+`.

   ⚠ THE TWO POINTERS ADVANCE AND NOTHING ELSE IS LIVE ACROSS THE BACK EDGE — the same discipline
   `revs_plot_span`'s note records, and for the same measured reason (CLAUDE.md §a hot loop's state
   lives in memory if anything takes its address).  `srcp` is bumped once per four cells and the
   four source reads are displacements off it, because `d16(An)` is 4 cycles where an `addq`+`lea`
   pair is 8.  Four, not eight: the plane stores want a register-indirect-postincrement that a
   longer unroll starts spilling around.

   ⚠ ENDIAN-OK, and it is not an exception to the `mem[]` rule: every `mem[]` access here is a
   BYTE.  The plane bytes are not `mem[]` at all — they are the bitplane buffer, which the Amiga
   reads as bits — and they are written a byte at a time too, because consecutive cells need not
   share a value.  (The FLAT line is the one that gets longword stores; that is `revs_plot_span`.) */
extern "C" unsigned char revs_plot_chain(unsigned short addr, unsigned char value,
                                         MEM_QUAL uint8_t* srcp,
                                         MEM_QUAL const uint8_t* cellBytes)
{
    const unsigned off = (unsigned)addr - BBC_SCREEN_BASE;
    unsigned byte = value;

    if (!s_target) { SPAN_STAT(g_plotNoTarget++); return (unsigned char)byte; }
    if (off >= FB_BYTES) return (unsigned char)byte;

    /* The claim and the counters first — they are the last readers of `off`, so nothing but the
       three pointers and the two expanded bytes is live over the loop. */
    {
        const unsigned char y = s_lineOf[off];
        g_plotOwn[y] = 1;
#ifdef REVS_SPAN_STATS
        g_plotChainLines++;
        g_plotRuns++;
        g_plotCells += BBC_SCREEN_CELLS;
        g_plotRunsLast++;
        g_plotCellsLast = (unsigned short)(g_plotCellsLast + BBC_SCREEN_CELLS);
        if (y < g_plotLineLo) g_plotLineLo = y;
        if (y > g_plotLineHi) g_plotLineHi = y;
#endif
    }

    {
        uint8_t* p1 = s_target + s_planeOff[off];
        uint8_t* p2 = p1 + kPlaneGap;
        uint8_t  lo = g_bbcExpandLo[byte];
        uint8_t  hi = g_bbcExpandHi[byte];
        unsigned i;

        /* ⚠⚠ `REVS_SPAN_VERIFY` DOES NOT CONSUME — the chain is still running in that build and
           it is the one destructive reader `view_consume`'s RLE allows.  See revs_plot.h. */
#ifdef REVS_SPAN_VERIFY
#define PLOT_CHAIN_CELL(SOFF, DOFF)  do {                                       \
            const uint8_t s_ = srcp[(SOFF)];                                    \
            if (s_) { byte = cellBytes[s_];                                     \
                      lo = g_bbcExpandLo[byte]; hi = g_bbcExpandHi[byte]; }     \
            p1[(DOFF)] = lo; p2[(DOFF)] = hi;                                   \
        } while (0)
#else
#define PLOT_CHAIN_CELL(SOFF, DOFF)  do {                                       \
            const uint8_t s_ = srcp[(SOFF)];                                    \
            if (s_) { srcp[(SOFF)] = 0; byte = cellBytes[s_];                   \
                      lo = g_bbcExpandLo[byte]; hi = g_bbcExpandHi[byte]; }     \
            p1[(DOFF)] = lo; p2[(DOFF)] = hi;                                   \
        } while (0)
#endif

        for (i = 0; i < BBC_SCREEN_CELLS / 4u; i++) {
            PLOT_CHAIN_CELL(0x000, 0);
            PLOT_CHAIN_CELL(0x080, 1);
            PLOT_CHAIN_CELL(0x100, 2);
            PLOT_CHAIN_CELL(0x180, 3);
            srcp += 0x200;
            p1 += 4;
            p2 += 4;
        }
#undef PLOT_CHAIN_CELL
    }
    return (unsigned char)byte;
}

/* Cleared from `view_paint_lines_core`, i.e. once per SWEEP — not per decode.  The crash hold
   renders extra frames with no sweep between them, and those must keep honouring the last
   sweep's spans instead of repainting stale mem[] over them. */
extern "C" void revs_plot_own_reset(void)
{
    unsigned i;
    for (i = 0; i < BBC_SCREEN_HEIGHT; i++) g_plotOwn[i] = 0;
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
