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

/* ⭐⭐ BOTH PLANE BUFFERS, for the delta painter below — and it is a different thing from
   `s_target`.  `s_target` is "the buffer the next decode will fill", aimed once per painted frame
   from present(); a painter that repaints a whole display line needs nothing else.  The glyph
   painter maintains only the few bytes a frame that CHANGE, so a byte it puts in one buffer alone
   would be missing from the other for ever (revs_plot.h). */
static uint8_t* s_plane[2];
#ifdef REVS_PLOT_TEXT
/* ⛔ A buffer-INITIALISATION flag, NOT a dirty map — nothing per cell, nothing the writers
   maintain.  Declared up here because `revs_plot_target` clears it when the race view leaves the
   screen, and that is far above the glyph domain's own section. */
static uint8_t s_textBased;
#endif

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
volatile unsigned long  g_plotChainNZ     = 0;  /* ...and their non-zero sources = runs - 1 */
volatile unsigned short g_plotChainNZLast = 0;
#ifdef REVS_PLOT_TEXT
volatile unsigned long  g_plotTextBytes = 0;    /* glyph bytes mirrored into both buffers */
volatile unsigned long  g_plotTextBases = 0;    /* full re-expansions of the 34 owned rows */
#endif
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
#ifdef REVS_PLOT_TEXT
    /* ⚠ No target means the race view is not on screen (MODE 7, `tt_active()`), and MODE 7 is the
       same chip memory time-multiplexed — so the glyph domain's static base is gone and the next
       sweep must lay it down again.  One byte, in the VBI, which is where the fact is known. */
    if (!planeBase) s_textBased = 0;
#endif
#ifdef REVS_SPAN_STATS
    g_plotRunsLast = 0;
    g_plotCellsLast = 0;
    g_plotChainNZLast = 0;
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
/* ⭐ ALIGNED, and load-bearing: the decode tests these 208 flags FOUR AT A TIME (a group-of-
   four `tst.l`, the same idiom as the span scan), which is only a legal `move.l` because of
   this attribute.  208 separate byte tests measured 1.75 ms/frame — probe.h §DECODESPLIT. */
extern "C" { unsigned char g_plotOwn[BBC_SCREEN_HEIGHT] __attribute__((aligned(4))); }

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

/* ── the band record, as the painters need it ─────────────────────────────────────────────── */
/* Five entries at most, published once per decode by `RevsScreen::buildLineModes()` — the ONE
   thing a painter cannot get from `m_lineMode` (revs_plot.h says why: the carve has zeroed its
   own lines there, and the flat-band test zeroes more). */
static short   s_bandFirst[5];
static uint8_t s_bandMode[5];
static uint8_t s_bandCount;

extern "C" void revs_plot_bands(const short* firstLine, const unsigned char* mode, unsigned count)
{
    unsigned n;
    if (count > 5u) count = 5u;
    for (n = 0; n < count; n++) { s_bandFirst[n] = firstLine[n]; s_bandMode[n] = mode[n]; }
    s_bandCount = (uint8_t)count;
}

/* Which MODE display line `y` is stored in.  Backwards over five entries: the bands are in line
   order, so the first one that starts at or before `y` is the one `y` is in.  ⚠ Band 0 starts
   BEFORE the display (BBC_BAND0_ANCHOR_US is negative), hence the signed compare. */
static unsigned plotModeOf(unsigned y)
{
    unsigned n = s_bandCount;
    while (n--)
        if ((int)y >= (int)s_bandFirst[n]) return s_bandMode[n];
    return 5u;   /* no record yet — MODE 5 is the race view's own mode */
}

extern "C" void revs_plot_planes(unsigned char* planeA, unsigned char* planeB)
{
    s_plane[0] = planeA;
    s_plane[1] = planeB;
    /* ⭐ Build the address map HERE, not on first use.  This runs from
       `RevsScreen::initialize()`, after the expansion tables are filled and long before the
       display is up, which is the only place 16.6 KB of table fill is free — and it retires the
       lazy build the geometry comment above warns about. */
    if (!s_mapBuilt) buildMap();
}

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

   ⛔⛔ AND WIDENING THE STORES IS CLOSED — BOTH SHAPES, ON ARITHMETIC, BEFORE ANY EMULATOR RUN.
   The instinct is strong and it is wrong, so the numbers are kept.  `g_plotChainNZ` is the census
   that prices it: `view_consume` is RLE, so a line's colour RUNS are exactly `1 + nonzero
   sources`, and the target reports **5.12 nonzero sources over 1277 takeover lines** ⇒ 6.1 runs
   in forty cells, a 6.5-cell mean run.  Per line the loop below is ten groups of 210 cycles.
     - A per-RUN loop: 6.5 cells is far too short to amortise a run prologue, and §10n's
       ⛔ index→pointer result prices one at up to 402 cycles — 6.1 of those exceed the whole loop.
     - A GROUP-OF-FOUR-CELLS longword fill (four consecutive cells' plane bytes ARE adjacent, even
       though their sources are 128 apart and can never be widened): a uniform group falls from
       210 to 132 cycles (60 for the four-way `or.b` test, 10 for the branch, 28 for two `move.l`,
       34 tail) — but a MIXED group rises to 316, because the group test is pure overhead on top
       of the per-cell tests it failed to replace.  At 0.51 changes per group, ~6 groups are
       uniform and ~4 mixed: 6(132) + 4(316) = 2056 against 2100.  **Zero.**
   ⭐⭐ THE GENERAL FORM, and it is the two-number rule's sibling: A COARSE TEST ONLY PAYS IF IT
   REPLACES THE FINE ONES, never if the fine ones still run on its failing arm — so the break-even
   is set by how often the coarse test SUCCEEDS, not by how much the wide store saves.  Here the
   floor is the scan: forty source reads and branches at stride $80 is 880 of the 2100 cycles, and
   no store shape touches it.  ⇒ The remaining lever on this function is a GROUP OF FOUR **LINES**
   (source stride along the line axis is 1, so one `move.l` tests four lines at one cell — the
   trick `view_group_sources` already uses and VIEW_SCAN_LANE already verified on the target),
   which needs eight live destination pointers and is therefore a register-pressure question, not
   an arithmetic one.  Not attempted: extending ownership to phases 2 and 3 buys 0.119 ms of
   decode per display line MEASURED, and is ~5x larger.

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
#ifdef REVS_SPAN_STATS
        /* ⭐⭐ The run census (§10p step 3c's sizing number) — a PLAIN local summed once at the
           end, not a volatile RMW per cell: six volatile RMWs a span is ~220 cycles (§SPANSTAT)
           and this one sits in the innermost loop of the whole renderer. */
        unsigned nz = 0;
#define CHAIN_NZ()   (nz++)
#else
#define CHAIN_NZ()   ((void)0)
#endif

        /* ⚠⚠ `REVS_SPAN_VERIFY` DOES NOT CONSUME — the chain is still running in that build and
           it is the one destructive reader `view_consume`'s RLE allows.  See revs_plot.h. */
#ifdef REVS_SPAN_VERIFY
#define PLOT_CHAIN_CELL(SOFF, DOFF)  do {                                       \
            const uint8_t s_ = srcp[(SOFF)];                                    \
            if (s_) { CHAIN_NZ(); byte = cellBytes[s_];                         \
                      lo = g_bbcExpandLo[byte]; hi = g_bbcExpandHi[byte]; }     \
            p1[(DOFF)] = lo; p2[(DOFF)] = hi;                                   \
        } while (0)
#else
#define PLOT_CHAIN_CELL(SOFF, DOFF)  do {                                       \
            const uint8_t s_ = srcp[(SOFF)];                                    \
            if (s_) { CHAIN_NZ(); srcp[(SOFF)] = 0; byte = cellBytes[s_];       \
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
#undef CHAIN_NZ
#ifdef REVS_SPAN_STATS
        g_plotChainNZ += nz;
        g_plotChainNZLast = (unsigned short)(g_plotChainNZLast + nz);
#endif
    }
    return (unsigned char)byte;
}

#ifdef REVS_PLOT_TEXT
/* ── ⭐⭐⭐ THE GLYPH DOMAIN — display lines 0..17 AND 192..207 ────────────────────────────────
   Why these 34 rows, what the two moving parts are and why the mem[] stores all stay: revs_plot.h.
   Here is only the mechanism. */

/* The two blocks, [first, end).  ⭐ Per DISPLAY LINE, not per character row (CLAUDE.md): 0..17 is
   the two MODE 4 text rows plus the two lines of row 2 that the band boundary at 18.0 puts above
   the sky, and 192..207 is the bottom two character rows of the dashboard. */
static const unsigned char kTextBlock[2][2] = { { 0u, 18u }, { 192u, 208u } };

static uint8_t s_baseMode[5];      /* the band record the base was laid down under */
static short   s_baseFirst[5];
static uint8_t s_baseCount;

static int textLineOwned(int y)
{
    return (y >= (int)kTextBlock[0][0] && y < (int)kTextBlock[0][1]) ||
           (y >= (int)kTextBlock[1][0] && y < (int)kTextBlock[1][1]);
}

/* One owned display line, all forty cells, into BOTH buffers — the BASE.  A dash row is ~38.4 of
   its 40 cells static cockpit that no routine ever rewrites, so the delta painter is only valid
   once the planes already hold them.
   ⚠ Shifts, never a multiply: `row * 320` and `y * 80` with a runtime operand emit __mulsi3 and
   the 68000 has none (CLAUDE.md, and `make muldiv-audit` fails the link). */
static void plotTextBaseRow(unsigned y, unsigned mode)
{
    const unsigned row    = y >> 3;
    const unsigned rowOff = (y << 6) + (y << 4);                       /* y * 80  */
    MEM_QUAL uint8_t* src = mem + BBC_SCREEN_BASE + ((row << 8) + (row << 6)) + (y & 7);
    uint8_t* a = s_plane[0] + rowOff;
    uint8_t* b = s_plane[1] + rowOff;
    unsigned c;
    if (mode == 4u) {
        for (c = 0; c < BBC_SCREEN_CELLS; c++, src += BBC_SCREEN_LINES) {
            const uint8_t v = *src;
            a[c] = 0; a[c + kPlaneGap] = v;
            b[c] = 0; b[c + kPlaneGap] = v;
        }
    } else {
        for (c = 0; c < BBC_SCREEN_CELLS; c++, src += BBC_SCREEN_LINES) {
            const uint8_t v = *src;
            const uint8_t lo = g_bbcExpandLo[v], hi = g_bbcExpandHi[v];
            a[c] = lo; a[c + kPlaneGap] = hi;
            b[c] = lo; b[c + kPlaneGap] = hi;
        }
    }
}

/* Has the geometry moved under the base?  A MODE change or a different band count invalidates it
   outright.  A moved BOUNDARY only matters if it crossed an owned block — and the one boundary
   that moves is the horizon (band 2/3, around line 81..101), seventy lines from either block, so
   this normally accepts the move and keeps the base.  ⚠ Bands 0/1 (18.0) and 3/4 (166.1) are
   FIXED — bands 2+3 sum to a constant $153C — which is why both blocks can be owned at all
   (docs/span-render-plan.md §11b). */
static int textBaseStale(void)
{
    unsigned n;
    if (s_bandCount != s_baseCount) return 1;
    for (n = 0; n < s_bandCount; n++) {
        if (s_bandMode[n] != s_baseMode[n]) return 1;
        if (s_bandFirst[n] == s_baseFirst[n]) continue;
        if (textLineOwned(s_bandFirst[n]) || textLineOwned(s_baseFirst[n])) return 1;
        s_baseFirst[n] = s_bandFirst[n];   /* outside the domain: nothing owned depends on it */
    }
    return 0;
}

/* ⚠⚠ SABOTAGED FIVE WAYS, AND FOUR FIRED — the fifth is written down here because it is the one
   worth knowing.  Deleting the DELTA's mirror (mismatch 6720), swapping lo/hi in the base
   (196606), aiming the delta at one buffer only (6888) and expanding a MODE 4 row as MODE 5
   (42658) all fail loudly.  Deleting the BASE's SECOND buffer survives 497/497 at mismatch 0, and
   the reason is sequencing, not a fixture gap: the claim needs `s_bandCount`, which only a decode
   publishes, and the sweep runs after the decode — so by the first claim BOTH buffers have had a
   full unowned decode and already hold these rows.  It becomes load-bearing on the MODE 7 ROUND
   TRIP, where the buffers are reused for a teletext page while the claim from the last race sweep
   still stands: the first decode back skips the owned rows in whichever buffer it gets, so the
   re-base must lay down both.  A `STRAIGHT_TO_RACE` oracle never leaves the race and so cannot
   reach it — which is a statement of the oracle's scope, not a reason to drop the write.
   ⭐ The SIBLING case is what settles it (CLAUDE.md): the DELTA's two-buffer write is the same
   claim and it fires at 6888. */
static void plotTextBase(void)
{
    unsigned blk, y, n;
    for (blk = 0; blk < 2u; blk++)
        for (y = kTextBlock[blk][0]; y < kTextBlock[blk][1]; y++)
            plotTextBaseRow(y, plotModeOf(y));
    for (n = 0; n < 5u; n++) { s_baseMode[n] = s_bandMode[n]; s_baseFirst[n] = s_bandFirst[n]; }
    s_baseCount = s_bandCount;
    s_textBased = 1;
#ifdef REVS_SPAN_STATS
    g_plotTextBases++;
#endif
}

#ifdef REVS_PLOT_TEXT_CHECK
extern "C" {
volatile unsigned long  g_plotTextChecks    = 0;
volatile unsigned long  g_plotTextMismatch  = 0;
volatile unsigned short g_plotTextMismatchY = 0;   /* (y << 8) | cell of the FIRST mismatch */
}

/* ⭐⭐ THE STALENESS ORACLE — see revs_plot.h.  Called at the TOP of the sweep's own_reset, before
   any re-base, so what it compares is the accumulated delta state: a byte written into these 34
   rows by anybody but `vdu_char_emit` shows up here on the very next sweep. */
static void plotTextCheck(void)
{
    unsigned blk, y, c;
    if (!s_textBased) return;
    g_plotTextChecks++;
    for (blk = 0; blk < 2u; blk++)
        for (y = kTextBlock[blk][0]; y < kTextBlock[blk][1]; y++) {
            const unsigned mode   = plotModeOf(y);
            const unsigned row    = y >> 3;
            const unsigned rowOff = (y << 6) + (y << 4);
            MEM_QUAL uint8_t* src = mem + BBC_SCREEN_BASE + ((row << 8) + (row << 6)) + (y & 7);
            for (c = 0; c < BBC_SCREEN_CELLS; c++, src += BBC_SCREEN_LINES) {
                const uint8_t v  = *src;
                const uint8_t lo = (mode == 4u) ? (uint8_t)0 : g_bbcExpandLo[v];
                const uint8_t hi = (mode == 4u) ? v           : g_bbcExpandHi[v];
                if (s_plane[0][rowOff + c] != lo || s_plane[0][rowOff + c + kPlaneGap] != hi ||
                    s_plane[1][rowOff + c] != lo || s_plane[1][rowOff + c + kPlaneGap] != hi) {
                    if (!g_plotTextMismatch)
                        g_plotTextMismatchY = (unsigned short)((y << 8) | c);
                    g_plotTextMismatch++;
                }
            }
        }
}
#endif /* REVS_PLOT_TEXT_CHECK */

/* ⭐⭐⭐ THE DELTA.  "This BBC frame-buffer byte just became `value`" — one line of one cell, into
   both buffers' planes.  Called from `vdu_char_emit_core`'s single store site, which is where
   every race-view glyph, space and text-script character reaches the screen.
   ⚠ It hooks THAT call site and explicitly NOT `seam_write`: growing that header choke point made
   164 inlined copies of a marking leaf and cost +4.9 ms in the producers (CLAUDE.md). */
extern "C" void revs_plot_byte(unsigned short addr, unsigned char value)
{
    const unsigned off = (unsigned)addr - BBC_SCREEN_BASE;
    unsigned po;
    uint8_t lo, hi;
    if (!s_textBased || off >= (unsigned)FB_BYTES) return;   /* based implies the map is built */
    if (!textLineOwned(s_lineOf[off])) return;               /* not ours — the decode has it */
    po = s_planeOff[off];
    if (plotModeOf(s_lineOf[off]) == 4u) { lo = 0; hi = value; }
    else { lo = g_bbcExpandLo[value]; hi = g_bbcExpandHi[value]; }
    s_plane[0][po] = lo;  s_plane[0][po + kPlaneGap] = hi;
    s_plane[1][po] = lo;  s_plane[1][po + kPlaneGap] = hi;
#ifdef REVS_SPAN_STATS
    g_plotTextBytes++;
#endif
}
#endif /* REVS_PLOT_TEXT */

/* Cleared from `view_paint_lines_core`, i.e. once per SWEEP — not per decode.  The crash hold
   renders extra frames with no sweep between them, and those must keep honouring the last
   sweep's spans instead of repainting stale mem[] over them. */
extern "C" void revs_plot_own_reset(void)
{
    unsigned i;
    for (i = 0; i < BBC_SCREEN_HEIGHT; i++) g_plotOwn[i] = 0;
#ifdef REVS_PLOT_TEXT
    /* ⭐ THE CLAIM, and it is asserted HERE rather than in present() for a hard reason: basing 34
       rows is ~10 000 stores, and work in the vblank ISR is capped at one frame (CLAUDE.md).
       This is main-loop context (phase 24), once per sweep, and the decode reads the claim on the
       frame after — which is the same one-sweep lag the span emitter's claim already has. */
    if (s_bandCount && s_plane[0] && s_plane[1]) {
#ifdef REVS_PLOT_TEXT_CHECK
        plotTextCheck();
#endif
        if (!s_textBased || textBaseStale()) plotTextBase();
        for (i = kTextBlock[0][0]; i < kTextBlock[0][1]; i++) g_plotOwn[i] = 1;
        for (i = kTextBlock[1][0]; i < kTextBlock[1][1]; i++) g_plotOwn[i] = 1;
    }
#endif
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
