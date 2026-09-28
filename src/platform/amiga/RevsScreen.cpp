/* RevsScreen — see RevsScreen.h.  Every constant here is derived in
   src/platform/bbc_screen.h; this file only re-hosts it. */
#define ECS_SPECIFIC
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <graphics/display.h>
#include <proto/exec.h>
#include <exec/memory.h>

#include "RevsScreen.h"
#include "framework/AmigaHardware.h"
#include "framework/Bitmap.h"
#include "framework/CopperList.h"
#include "framework/Sprite.h"
#include "../bbc_screen.h"
#include "../teletext.h"           /* the MODE 7 model: the VDU driver's page + the SAA5050 */
#include "../revs_plot.h"        /* the direct-to-bitplane plotter: this is where it is aimed */
#ifdef REVS_TYRE_SPRITES
#include "RevsTyres.h"
#endif
#include "../probe.h"              /* phase brackets: DECODESPLIT's carve, and the cockpit's */
#include "../../gen/mem.h"        /* MEM_<name> offsets — the cockpit layer reads the game's own
                                    silhouette and dash-edge tables, and they deserve names */
#ifdef REVS_PLOT_ONLY
extern "C" volatile unsigned char g_plotLineLo, g_plotLineHi;
#endif
#include "../../cpu/m68k_math.h"   /* the 68000 has NO 32-bit mul/div (make muldiv-audit) */

#include "../../cpu/mem_decl.h"
extern "C" MEM_QUAL uint8_t mem[65536];

/* ---- display geometry ---------------------------------------------------------
   320x208, two bitplanes, interleaved.  kDisplayTop is the raster line the display
   window starts on, so a BBC display line L is Amiga raster line kDisplayTop + L.
   ⚠ Must agree with the DIWSTRT/DIWSTOP PlatformAmiga::run programs. */
static const uint16_t kW          = BBC_SCREEN_WIDTH;
static const uint16_t kH          = BBC_SCREEN_HEIGHT;
/* The ownership scan below walks kH as longword GROUPS; 26 rows x 8 lines is a multiple of
   four, and this is that staying true if the geometry ever changes. */
typedef char revs_screen_height_group_check[(BBC_SCREEN_HEIGHT % 4) == 0 ? 1 : -1];
static const uint8_t  kBP         = 2;
static const uint16_t kDisplayTop = 0x2C;
static const uint16_t kRowBytes   = (kW / 8) * kBP;          /* 80: interleaved */
static const uint16_t kPlaneGap   = (kW / 8);                /* 40: plane stride within a row */
#ifdef REVS_DUAL_PLAYFIELD
/* ⭐ FOUR planes on the display, still TWO per bitmap.  PF1 is planes 1,3 (the terrain's
   double-buffered pair) and PF2 is planes 2,4 (the cockpit's single one), so each bitmap keeps
   the 2-plane interleaved layout every painter in the port already writes — kRowBytes and
   kPlaneGap are unchanged and RevsPlot.cpp needs no edit at all.  The Amiga applies BPL1MOD to
   the ODD planes and BPL2MOD to the EVEN ones, which IS the dual-playfield division, so one
   modulo each covers a bitmap whose two planes are 40 bytes apart.  Makefile §DUALPF. */
static const uint8_t  kDisplayBP  = 4;
#else
static const uint8_t  kDisplayBP  = kBP;
#endif

/* ---- fixed copper-list layout (indices in 32-bit MOVE/WAIT words) -------------
   d[0] is the CopperList ctor's copperWait(16,0) and d[LEN-1] its park.  Unused band
   slots keep the ctor's copper-NOP prefill — that prefill is why a band the game does
   not use costs a wasted copper cycle instead of halting the copper (CopperList.cpp).

   ⚠ THE SPRITE POINTERS COME FIRST because they have the tightest deadline in the list:
   Agnus fetches each channel's control words in the sprite DMA slots near the START of a
   line, so SPRxPT must already be right when it does.  Everything else — BPLCON0, the
   modulos, the bitplane pointers, the top palette — is consumed later in the line or
   later in the frame.  (docs/amiga-lessons.md: SPRxPT is stricter than BPLxPT.) */
#define IDX_SPRITES     1                       /* 8 channels x SPRxPTH/L       (16) */
#define IDX_PLAYFIELD   (IDX_SPRITES + 16)      /* BPLCON0 + BPL1MOD + BPL2MOD   (3) */
#ifdef REVS_DUAL_PLAYFIELD
#define IDX_BPL_WORDS   8                       /* BPL1/3PT (terrain) + BPL2/4PT (cockpit) */
/* COLOR00..03 for PF1, then COLOR09/10/11 for PF2.  COLOR08 is PF2's pen 0, which dual
   playfield never displays (it is the transparency that lets PF1 through), so it is not
   written at all. */
#define PAL_BASE_WORDS  7
#else
#define IDX_BPL_WORDS   4                       /* 2 interleaved planes */
#define PAL_BASE_WORDS  4                       /* COLOR00..03 */
#endif
#ifdef REVS_NEEDLE_PLANES
/* ...then COLOR21 and COLOR25: pen 1 of sprite pairs 2/3 (the rev needle) and 4/5 (the steering
   mark), PER BAND, because the rev needle crosses the band 3/4 boundary at display line 166 and
   its colour is whatever the game's palette makes its BBC pen in each band (revs_plot.h §12d). */
#define PAL_WORDS       (PAL_BASE_WORDS + 2)
#else
#define PAL_WORDS       PAL_BASE_WORDS
#endif
#define IDX_BPL         (IDX_PLAYFIELD + 3)
#define IDX_TOPPAL      (IDX_BPL + IDX_BPL_WORDS)  /* the palette for display line 0 */
#ifdef REVS_TYRE_SPRITES
/* ⭐ COLOR17..19 — the tyre sprites' three pens (sprite pair 0/1 shares 17..19).  Written at the
   TOP of the frame rather than inside a band, because the patch (display lines 130..140) sits
   wholly inside raster band 3 and nothing else on screen uses a sprite: one set of values covers
   every line a sprite is visible on.  Filled in buildBands() from band 3's own palette record, so
   the sprite's colours track the game's palette instead of being baked in. */
#define IDX_SPRPAL      (IDX_TOPPAL + PAL_WORDS)  /* COLOR17..19                  (3) */
#define IDX_BANDS       (IDX_SPRPAL + 3)
#else
#define IDX_BANDS       (IDX_TOPPAL + PAL_WORDS)
#endif
#define BAND_WORDS      (1 + PAL_WORDS)         /* WAIT + the band's palette         */
#define MAX_BANDS       8
#define LIST_LENGTH     (IDX_BANDS + MAX_BANDS * BAND_WORDS + 1)

/* ═══ MODE 7 ═══════════════════════════════════════════════════════════════════════════════
   ⭐ A SECOND, SEPARATE DISPLAY CONFIGURATION — its own bitmap and its own copper list, and
   the race view's is not touched.  Three reasons it is not one shared list:

     1. TELETEXT NEEDS EIGHT COLOURS, so three bitplanes.  Giving the race view a third plane
        it never uses would cost display DMA in the ONE place this port cannot afford it:
        display DMA against a program in chip RAM is already about two thirds of a rendered
        race frame (docs/perf-method.md).  MODE 7 is a static front end with no game work, so
        the extra plane is free there and unaffordable here.
     2. THE GEOMETRY DIFFERS.  40x25 cells of 8x10 is 320x250, against the race view's
        320x208 — a different DIWSTOP, so the two cannot share a window either.
     3. Switching whole lists at a mode change is one COP1LC write; merging them would mean
        rewriting BPLCON0, six pointers and eight colours in the VBI on every frame.

   The cell is 8x10 because 25 rows of the SAA5050's native 10-row cell is 250 lines, which is
   a legal PAL display, while its doubled 12x20 form would need 500 — see the derivation in
   tools/gen_teletext_font.py.  So the chip's own character ROM maps one source row to one scan
   line with no resampling. */
static const uint16_t kTtW        = TT_WIDTH;                    /* 320 */
static const uint16_t kTtH        = TT_HEIGHT;                   /* 250 */
static const uint8_t  kTtBP       = 3;                           /* eight teletext colours */
static const uint16_t kTtRowBytes = (kTtW / 8) * kTtBP;           /* 120: interleaved */
static const uint16_t kTtPlaneGap = (kTtW / 8);                   /* 40: plane stride in a row */

#define IDX_TT_SPRITES   1
#define IDX_TT_PLAYFIELD (IDX_TT_SPRITES + 16)  /* BPLCON0 + BPL1MOD + BPL2MOD    (3) */
#define IDX_TT_BPL       (IDX_TT_PLAYFIELD + 3) /* 3 interleaved planes           (6) */
#define IDX_TT_PAL       (IDX_TT_BPL + 6)       /* COLOR00..07                    (8) */
#define TT_LIST_LENGTH   (IDX_TT_PAL + 8 + 1)

/* The SAA5050's palette, and there is nothing to choose: the chip drives one RGB line per
   primary, so every teletext colour is fully saturated and the pen index IS the colour code
   (bit 0 red, bit 1 green, bit 2 blue).  That is also why the plane-building loop in
   decodeTeletext() can treat a cell's colour as a per-plane bit mask. */
static const uint16_t kTtPalette[8] = {
    0x000, 0xF00, 0x0F0, 0xFF0, 0x00F, 0xF0F, 0x0FF, 0xFFF,
};

/* ---- MODE 5 nibble expansion --------------------------------------------------
   A MODE 5 byte holds four pixels; the high nibble is those four pixels' HIGH bits and
   the low nibble their LOW bits (bbc_screen.h).  So each nibble is already one bitplane's
   four pixels and only has to be doubled to Amiga pixel width.  Two 256-entry tables
   rather than one 16-entry one indexed twice: on a 68000 that is two indexed byte loads
   with no masking or shifting in the inner loop, which is the whole cost of this pass.
   ⚠ Built in initialize(), never lazily — a table built on first use inside a frame is
   the Atari port's 3.6-second freeze (docs/m68k-optimisation.md). */
/* ⭐ NOT static any more: the direct-to-bitplane plotter (RevsPlot.cpp) expands the SAME MODE 5
   bytes, and two definitions of one mapping is how a plotter and its own oracle come to agree on a
   bug.  One definition, shared — the rule bbc_ula_palette_write is under too. */
extern "C" { uint8_t g_bbcExpandHi[256]; uint8_t g_bbcExpandLo[256]; }
#define s_expandHi g_bbcExpandHi
#define s_expandLo g_bbcExpandLo

static uint8_t expandNibble(unsigned n)
{
    uint8_t v = 0;
    if (n & 8) v |= 0xC0;
    if (n & 4) v |= 0x30;
    if (n & 2) v |= 0x0C;
    if (n & 1) v |= 0x03;
    return v;
}

/* ---- colour ------------------------------------------------------------------
   A Video ULA palette byte is (logical colour << 4) | (physical colour EOR 7), and the
   physical nibble's bit 3 is the FLASH flag rather than a colour bit.  Revs never sets
   it (every entry in its tables has a low nibble of 0-7), so it is masked off here and
   noted rather than modelled — a flashing palette entry would need a VBI-toggled colour. */
static uint16_t bbcColour(uint8_t paletteByte)
{
    unsigned phys = (unsigned)((paletteByte & 0x0Fu) ^ 0x07u) & 0x07u;
    return (uint16_t)(((phys & 1u) ? 0x0F00u : 0u) |      /* bit 0 = red   */
                      ((phys & 2u) ? 0x00F0u : 0u) |      /* bit 1 = green */
                      ((phys & 4u) ? 0x000Fu : 0u));      /* bit 2 = blue  */
}

/* ⭐ The four Amiga colour registers ARE four of the BBC's sixteen logical colours:
   a MODE 5 pixel's two bits become palette-index bits 3 and 1 (bits 2 and 0 are the
   neighbouring pixels' bits and are don't-care), and a MODE 4 pixel's single bit becomes
   index bit 3 with plane 1 decoded as zero.  Hence logical 0, 2, 8, 10. */
static const uint8_t kLogicalForPen[4] = { 0, 2, 8, 10 };

/* Bands with a malformed record are counted, never silently accepted: a cycle that is not
   the game's five bands means this model has the display wrong, and a stale copper list
   with the right colours reads exactly like a working one. */
extern "C" { volatile unsigned long g_bandRejects = 0; }

/* ⭐⭐ THE FLAT-BAND SKIP (Phase 6 item 0, step 1 — docs/direct-bitplane-plan.md §4/§4a).
 *
 * A band whose four Amiga colour registers all hold the SAME colour makes its bitplane content
 * unobservable, so decoding those lines produces pixels nobody can see.  The BBC's band 1 is
 * exactly that — sixteen palette entries, all the same blue — and it is 63 of the 208 display
 * lines (18..81, a FIXED extent: band 1's duration is the fixed 4038 us latch, measured
 * 2026-08-16; what moves with the hills is band 2's length).  Those lines are also where 5.5 KB
 * of live engine code and variables sit inside the BBC frame buffer, so what is skipped is
 * precisely the region whose byte content never meant anything as a picture.
 *
 * ⚠ FLATNESS IS DECIDED PER FRAME, from the same snapshot the copper list is built from, which
 * is what makes leaving stale plane bytes safe: if a band stops being flat, or a line moves into
 * a non-flat band, that line is decoded in the very frame the new palette applies.  Deciding it
 * from a literal line range instead would break the moment a circuit hook moved a boundary.
 *
 * Counters, not faith (docs/method-lessons.md): g_decodeFlatLines must read 63 in a race, and 0
 * says the skip never engaged.  `make FLATSKIP=0` is the A/B build. */
extern "C" {
volatile uint16_t g_decodeFlatLines  = 0;    /* lines skipped in the most recent decode */
volatile uint16_t g_decodeFlatBands  = 0;    /* bands found flat in it                  */
}

/* MODE 7 rows converted in the last decodeTeletext() (out of TT_ROWS): 0 on an idle frame, a
   handful on a keypress, all 25 on a flash flip or a full repaint.  In PROBE_SYMS. */
extern "C" { volatile uint16_t g_ttRowsDrawn = 0; }

/* ⭐⭐ THE DIRTY-REGION DECODE (Phase 6 item 0, step 2's payoff — docs/direct-bitplane-plan.md §7b).
 *
 * MEASURED, on the target, car under power: **406 of 8320 frame-buffer bytes change per painted
 * frame (4.9%)**.  The pass converted all 8320 regardless, so ~95% of its 81 ms was re-converting
 * a picture that had not moved.  This compares each 8-byte CELL COLUMN against a shadow of the
 * bytes that produced what is already in the buffer, and converts only the ones that differ.
 *
 * ⚠ THREE THINGS IT HAS TO GET RIGHT, and all three are in the loop below:
 *
 *  1. **The Amiga is DOUBLE buffered, so there are TWO shadows.**  The buffer being written was
 *     last painted TWO decodes ago, not one, and the bytes to compare against are the ones that
 *     produced *its* content.  One shared shadow would leave every other frame a frame stale —
 *     which at ~1 FPS is a second of game time, not a subtle artefact.  s_shadow is indexed by
 *     m_back for exactly that reason.
 *  2. **A line's MODE can change while its bytes do not.**  m_lineMode comes from the per-frame
 *     band snapshot: a line that moves between MODE 5, MODE 4 and flat-and-skipped must be
 *     re-converted even though the source byte is identical.  So the row's eight mode bytes are
 *     diffed against the shadow's too, and a mode change dirties the whole character row.
 *  3. **The compare must be cheaper than the conversion it skips.**  A cell is 8 CONTIGUOUS bytes
 *     in the BBC layout (cell*8 + line), so a clean cell costs two longword loads a side — 2080
 *     longword reads over the buffer, against 8320 byte reads + 16640 table lookups + 16640 byte
 *     stores for the unconditional pass.
 *
 * Counters, not faith: g_decodeCells is cells converted out of 1040 and must read WELL under 1040
 * in a race (a run that reads 1040 every frame has the dirty test failing open, which is exactly
 * as fast as no feature at all and looks identical on screen).  `make DIRTY=0` is the A/B build
 * and it PRINTS its state, because g_decodeCells then reads 1040 by construction.
 * `make DIRTYCHECK=1` is the oracle: every frame, re-decode in full into a scratch copy of the
 * buffer and require byte equality (g_decodeDirtyMismatch == 0). */
extern "C" {
volatile uint16_t g_decodeCells      = 0;    /* cell columns CONVERTED in the last decode (/1040) */
volatile uint16_t g_decodeCellsMax   = 0;    /* the worst frame of the run                        */
volatile unsigned long g_decodeCellsTotal = 0;/* running sum, so a mean is available             */
volatile uint16_t g_decodeFullFrames = 0;    /* decodes that had to convert everything            */
volatile uint16_t g_decodeModeDirty  = 0;    /* rows dirtied by a MODE change, not a byte change  */
volatile unsigned long g_decodeModeLines = 0;/* ...and the LINES those cost, 40 cells each      */
/* ⭐ THE CARVE-OUT'S OWN STATE PRINT (`make SPANFILL=1`): display lines this decode left to the
   span emitter.  An A/B switch must print its own state — a build whose predicate never fires and
   a carve-out that buys nothing read identically without it, which is the mistake `g_spanEmitLines`
   was added to catch on the emitter side.  Declared always so a .gdb script reads a zero. */
volatile uint16_t g_decodeOwnLines = 0;
/* DIRTYCHECK only, but declared always so a .gdb script can read a zero rather than .text. */
volatile unsigned long g_decodeDirtyChecks   = 0;
volatile unsigned long g_decodeDirtyMismatch = 0;
volatile uint16_t      g_decodeDirtyMismatchOff = 0xFFFFu;
/* ⭐ THE COCKPIT LAYER'S OWN STATE PRINT (`make DUALPF=1`).  Declared always so a .gdb script
   reads a zero rather than instruction bytes in a build without it. */
volatile uint16_t      g_cockpitCells      = 0;  /* cells converted into PF2 last decode (/240) */
volatile unsigned long g_cockpitCellsTotal = 0;
volatile unsigned long g_cockpitRuns       = 0;  /* decodes that rebuilt the silhouette table   */
volatile unsigned long g_cockpitFulls      = 0;  /* decodes that had to re-expand all 240 cells */
/* ⚠⚠ THE ONE ASSUMPTION THIS LAYER MAKES, AS A TRIPWIRE.  PF2 has three opaque pens and the
   car was measured to use exactly three (Makefile §DUALPF), so a car pixel at BBC pen 3 is
   stored TRANSPARENT and falls through to PF1.  That is still the right colour today, because
   PF1 holds the same art — it stops being right the moment the terrain painter ignores the
   silhouette, which is the next step.  Must read 0. */
volatile unsigned long g_cockpitPen3       = 0;
/* ⚠⚠ MUST BE 0 — the boundary cells' masks are PIXEL masks ($88/$CC/$EE/$FF and their mirrors,
   symbols.csv), i.e. both of a pixel's two bits move together, which is what lets one AND split a
   cell into "PF1's terrain" and "PF2's dashboard".  A mask whose two nibbles disagree would be a
   PLANE mask instead and the split would be wrong; this counts them rather than assuming. */
volatile unsigned long g_cockpitMaskBad    = 0;
/* ⭐ Set by the ownership walk: every display line's mode is 0, i.e. owned or flat, so the
   conversion has nothing to do this frame.  `g_decodeSkips` counts the frames it saved. */
static unsigned char   g_decodeNothingToDo = 0;
volatile unsigned long g_decodeSkips       = 0;
/* ⭐⭐⭐ THE GATE ON DELETING THE CONVERSION ALTOGETHER (`DECODEHOLES=1`).  A HOLE is a display
   line that is neither OWNED by a painter nor inside a FLAT band — i.e. the one thing the
   conversion still had to do, and the only way `DECODENOCONV` can show stale pixels.  Counted
   rather than assumed, with the LAST frame a hole appeared at: the cold frames at the top of a
   run have no ownership yet, so "holes only before frame N" is the answer that licenses the
   deletion and "holes at frame 3000" is the answer that forbids it.
   ⚠ A check build only — this walk is precisely the ~1 ms the deletion collects. */
volatile unsigned long g_decodeGapFrames   = 0;   /* frames the conversion had to run on   */
volatile unsigned long g_decodeGapLastAt   = 0;   /* g_decodeFrames at the last of them      */
volatile unsigned long g_decodeFrames      = 0;   /* race-view frames this pass has prepared */
/* A silhouette table entry that did not decode to a chain slot: the layer degrades to "no car
   on this line" (PF1 shows, i.e. today's picture) rather than masking the wrong cells. */
volatile unsigned long g_cockpitBadSlot    = 0;
/* `make DUALPFCHECK=1` only, but declared always so a .gdb script reads a zero. */
volatile unsigned long g_cockpitChecks     = 0;
volatile unsigned long g_cockpitMismatch   = 0;
volatile uint16_t      g_cockpitMismatchAt = 0xFFFFu;   /* (line << 8) | cell of the first */
/* ⭐⭐⭐ THE DYNAMIC FOOTPRINT, ENUMERATED RATHER THAN SAMPLED (`make DUALPFCHECK=1`, read with
   amiga/dualpf_dump.gdb).  [line - 117][cell] counts the painted frames in which that cell's
   eight source bytes moved.  Which cells of 117..157 are NOT static decides how much of the car
   PF2 can own and which painters have to be retargeted before `LOWOWN=1` can claim these rows —
   and a 41-frame `fbwrites` census is exactly the instrument CLAUDE.md warns cannot answer it
   (a needle ROTATES; a sample of a moving thing is not its range).
   ⚠ A PRACTICE SESSION STILL CANNOT SEE THE WING MIRRORS — an empty track reflects nothing.
   That one rectangle comes from the game's own six-segment tables, as it does for DASHCHECK. */
volatile unsigned short g_cockChange[41][40];
volatile unsigned long  g_cockChangeFrames = 0;
}

/* The shadow of the frame-buffer bytes that produced each bitplane buffer's current content, in
   the BBC's own layout (row*BPR + cell*8 + line) so a cell is contiguous.  ⚠ 4-byte aligned: the
   compare reads longwords, and on a 68000 an odd address is an address error. */
static uint32_t s_shadow[2][(BBC_SCREEN_BPR * BBC_SCREEN_ROWS) / 4];
/* ⭐ NO "shadow valid" FLAG, and none is needed — which is worth stating so nobody adds one.
   Static storage starts zeroed, so on the first decode every row's shadowed MODE differs from
   the snapshot's and the row converts in full; a row whose mode is 0 throughout is the flat
   band, whose plane bytes are unobservable anyway.  The same argument covers the MODE 7 round
   trip: the front end draws into its own bitmap, so a race buffer's content still matches its
   shadow when the race list comes back, and anything the engine changed in mem[] meanwhile is
   caught by the byte compare like any other change. */
/* ⚠ `aligned(4)` is load-bearing: convertRace compares a character row's eight shadowed modes
   against the live ones as TWO LONGWORDS, and a row starts at line row*8, which is aligned
   whenever the array is.  (probe.h §DECODESPLIT — the per-byte form cost 1.9 ms a frame.) */
static uint8_t  s_shadowMode[2][BBC_SCREEN_HEIGHT] __attribute__((aligned(4)));

/* ⭐ WHAT THE TARGET IS ACTUALLY DISPLAYING, addressable from gdb.  amiga/screen_dump.gdb
   dumps these two blocks out of a running FS-UAE and tools/amiga_ppm.py turns them into a
   picture — the same "look at it" instrument as the host-side dump, but taken on the real
   machine, so a decode or band bug is seen rather than reasoned about.
   ⚠ Both MUST stay listed in PROBE_SYMS (amiga/Makefile): --gc-sections drops an
   unreferenced global and gdb then prints instruction bytes as a value. */
extern "C" {
volatile uint32_t g_screenFrontAddr  = 0;   /* the displayed interleaved bitplane block */
/* PF2's block (`make DUALPF=1`); 0 in a single-playfield build, so a dump script can tell the
   two configurations apart from the target rather than from the flags it thinks it used. */
volatile uint32_t g_screenCockpitAddr = 0;
/* ⭐ The buffer the LAST decode filled — which is the only one that can be compared against the
   current mem[].  g_screenFrontAddr is what the copper is showing, i.e. one decode older, and a
   check that used it read 114 stale pixels and looked like a defect in the layer under test. */
volatile uint32_t g_screenBackAddr    = 0;
volatile uint32_t g_screenCopperAddr = 0;   /* the copper list, incl. the palette bands */
volatile uint16_t g_screenBytes      = 0;   /* size of the bitplane block               */
volatile uint16_t g_screenCopperWords = 0;  /* LIST_LENGTH — so the dump can't go stale  */
/* ⭐ The display's SHAPE, so `amiga/screen_dump.gdb` + `tools/amiga_ppm.py` can decode either
   configuration without being told which.  The port now has two (320x208 two-plane race view,
   320x250 three-plane MODE 7) and a dumper that assumes one would decode the other as garbage
   and read as a render bug — the exact "instrument blind to the case" failure this project has
   hit repeatedly (docs/method-lessons.md). */
volatile uint16_t g_screenPlanes  = 0;      /* bitplanes in the CURRENT configuration */
volatile uint16_t g_screenHeight  = 0;      /* display lines in it */
volatile uint16_t g_screenMode7   = 0;      /* non-zero while the MODE 7 page is on screen */
/* ⚠ MODE 7's bitmap is 30000 bytes of CHIP RAM and its list another 148.  A failed allocation
   must not degrade quietly into "the front end is black", which is indistinguishable from a
   renderer bug — the whole point of the counters in this port.  Non-zero here means the front
   end cannot be displayed at all, and says which half failed. */
volatile uint16_t g_ttAllocFailed = 0;      /* bit 0 = bitmap, bit 1 = copper list */
}

/* ⭐⭐ WHERE THE BEAM ACTUALLY IS when the copper list and the bitplane pointers are rewritten.
 *
 * ⚠ THE RULE (docs/amiga-lessons.md, CLAUDE.md): bitplane POINTER swaps happen in the VBI,
 * never mid-frame.  vbiUpdate() IS called from the VERTB handler, which is why the rule looked
 * satisfied by construction — but "in the VERTB handler" and "in the vertical blank" are the
 * same thing only while the handler is SHORTER THAN A FRAME.  Here it is not: Revs::vbi() runs
 * the game's whole 50 Hz body (band 4 of the IRQ1V cycle is tick_wheel_spin) before it gets to
 * vbiUpdate(), and the body takes hundreds of milliseconds at the current baseline.  So the
 * swap and the band rebuild can land ANYWHERE in a much later field, with the copper already
 * past the words being edited.
 *
 * That cannot be reasoned about from the source and it cannot be seen in a dump either — a
 * copper list dumped at a frame boundary is consistent whatever the beam did while it was
 * being written.  So it is MEASURED: two register reads once per PAINTED frame (~1/s), which
 * is why they are affordable in a build that also quotes a framerate.
 *
 * Read with amiga/beam_watch.gdb.  A display line here means the write raced the beam.
 * ⚠ Must stay in PROBE_SYMS (amiga/Makefile) or --gc-sections drops them and gdb prints
 * instruction bytes as a measurement. */
extern "C" {
volatile uint16_t g_beamPresentLine  = 0;   /* raster line of the most recent present()     */
volatile uint16_t g_beamPresentMin   = 0xFFFF;
volatile uint16_t g_beamPresentMax   = 0;
volatile unsigned long g_beamPresents = 0;  /* presents counted                              */
volatile unsigned long g_beamPresentsLate = 0; /* ...of which the beam was inside the display */
/* And the same question one level up: where is the beam when the VERTB handler is ENTERED?
   It answers whether a swap moved to the TOP of the handler would actually be in the blank —
   an overrunning handler leaves VERTB pending, so the next one is taken the instant interrupts
   are enabled again, which can be mid-display no matter what the code order is. */
volatile uint16_t g_beamEntryLine  = 0;
volatile unsigned long g_beamEntries     = 0;
volatile unsigned long g_beamEntriesLate = 0;
}

/* The per-painted-frame plan/pixel log — see the block in decode() that fills it.  A power of
   two so the wrap is a mask (no __umodsi3), and small enough to print in one gdb loop.
   ⚠ ALL of this is `make FILLWATCH=1` only: the checks it feeds re-read the whole frame buffer. */
#define PLAN_LOG_MAX 64u
#ifdef REVS_FILLWATCH
extern "C" {
volatile uint8_t  g_planLogBand2[PLAN_LOG_MAX];   /* band 2's first display line, from T1     */
volatile uint8_t  g_planLogBand3[PLAN_LOG_MAX];   /* band 3's — where the ground should start  */
volatile uint8_t  g_planLogGroundL[PLAN_LOG_MAX]; /* first green cell, LEFT edge, from pixels  */
volatile uint8_t  g_planLogGroundR[PLAN_LOG_MAX]; /* ...and the RIGHT edge                     */
volatile uint8_t  g_planLogStale[PLAN_LOG_MAX];   /* 1 = the record was rejected this frame    */
volatile unsigned long g_planLogN       = 0;      /* frames logged (index = N & 63)            */
volatile unsigned long g_planStaleFrames = 0;
}
#endif

/* ⭐⭐ DID THE SOURCE MOVE UNDER THE DECODE?  The BBC frame buffer is SINGLE-buffered — it is the
 * game's only screen — and this port has a second, unsynchronised reader: decode() spends ~250 ms
 * walking it in the main loop while the VERTB handler keeps running the game's 50 Hz body.  Any
 * write that lands in a character row the decode has not reached yet is baked into the bitplane
 * buffer and stays on screen for the WHOLE painted frame, clearing only on the next one — which
 * is exactly the reported behaviour of what is left of the artefact.
 *
 * So: a per-character-row checksum taken as the decode reads each row (free — the bytes are in
 * registers anyway), then a second pass afterwards to see which rows changed.  8320 extra byte
 * reads against a 250 ms pass. */
extern "C" {
/* ⭐⭐ CATCH THE BAD FRAME, do not sample for it.  The artefact is rarer than a dozen frames, so
 * dumping frames and looking is a lottery; the invariant is cheap and exact instead.
 *
 * THE INVARIANT.  Band 2 spans the horizon.  Above the ground line — the first line that is green
 * nearly all the way across — every cell of it is SKY, byte $0F (pen 1).  A real BBC agrees: `make
 * refloop` finds band 2's lines completely free of any other value, with the road's vanishing-point
 * triangle only from line 102 down.  So a $00 (black) or $FF (green) cell above the ground line is
 * the artefact, in both of its reported colours, and its run length says how far the bad fill got.
 * (White is excluded deliberately: distant scenery — the marshal's post — is legitimately white.)
 *
 * When it trips, the whole horizon neighbourhood is LATCHED into g_fillEvidence, because the next
 * painted frame overwrites the buffer and the artefact is gone.  amiga/fill_catch.gdb prints it.
 */
/* ⚠ 74..167 = the whole 3D view down to the dashboard, NOT just band 2.  The first version of
 * this stopped at 116 and read clean for hundreds of frames while the artefact sat at display line
 * 125: the horizon MOVES WITH THE HILLS (the user's own screenshot has the ground starting at 118,
 * mine at 100), so a window pinned around one hill's horizon misses the next hill's entirely.
 * Measured from that screenshot: one display line green where the lines above and below it are
 * black — a single line whose fill did not happen, showing the previous frame's colour. */
#define FILL_EV_LO   74u
#define FILL_EV_HI   167u
#ifdef REVS_FILLWATCH
extern "C" {
volatile unsigned long g_fillBadFrames = 0;   /* painted frames that violated the invariant   */
volatile unsigned long g_fillBadFrameN = 0;   /* which painted frame the evidence is from     */
volatile uint16_t g_fillBadLine   = 0xFFFFu;  /* the first offending display line             */
volatile uint8_t  g_fillBadCell   = 0;        /* leftmost offending cell on it                */
volatile uint8_t  g_fillBadValue  = 0;        /* $00 = black run, $FF = green run              */
volatile uint8_t  g_fillBadRun    = 0;        /* how many cells                               */
volatile uint8_t  g_fillGroundLine = 0;       /* the ground line the check used               */
volatile uint8_t  g_fillEvidence[(FILL_EV_HI - FILL_EV_LO) * BBC_SCREEN_CELLS] = {0};

/* ⭐ TWO MORE EXACT CHECKS, because the invariant above only covers the sky side of the horizon and
 * the artefact can sit BELOW the ground line, where green and black are both legitimate shapes and
 * no invariant exists.  So instead of asking "is this frame right?", ask the two questions that
 * have exact answers:
 *
 *  1. CHANGE.  With the car stationary the horizon is identical frame to frame, so a one-frame
 *     outlier stands out: count the cells of lines 74..115 that differ from the previous painted
 *     frame.  A handful is a car or a marshal's post moving; dozens is the artefact.
 *  2. DECODE.  Re-derive what the bitplanes MUST hold for those lines straight from mem[] and
 *     compare with what the decode actually produced.  This is the only check that can see a
 *     source byte changing behind the read, or the expansion writing the wrong plane — i.e. it
 *     separates "the game wrote it" from "we drew it wrong" with no interpretation left over. */
volatile unsigned long g_horizonChangeMax = 0;    /* worst frame-to-frame cell delta seen      */
volatile unsigned long g_horizonChangeBig = 0;    /* frames with a delta over the threshold    */
volatile uint8_t  g_horizonChangeSeries[64] = {0};/* per painted frame, capped at 255          */
volatile unsigned long g_decodeMismatch    = 0;   /* decoded bitplanes != mem[] afterwards     */
volatile uint16_t g_decodeMismatchLine     = 0xFFFFu;

/* ⭐⭐ THE ARTEFACT'S ACTUAL SHAPE, measured off the user's screenshot rather than guessed.
 *
 * Decoding that screenshot line by line: the road's RIGHT edge marches smoothly down the screen
 * (199, 201, 213, 217, 225 px) while its LEFT edge reads 80, then 136, then 186, then 34, then 20.
 * Two consecutive display lines have the left edge tens of pixels too far right, so grass green
 * stands where the road belongs.  That is not a fill running away to the right edge — it is two
 * lines drawn from DIFFERENT GEOMETRY than the lines around them.
 *
 * So the check is an outlier test on the left edge, with the right edge as the control: a line
 * whose leftmost black cell differs from both neighbours by >= 4 cells while the rightmost black
 * cell stays within 2 cells of them.  The control is what separates this from a curve or a crest,
 * where the WHOLE road moves and both edges move together. */
volatile unsigned long g_edgeJumpFrames = 0;
volatile uint16_t g_edgeJumpLine  = 0xFFFFu;
volatile uint8_t  g_edgeJumpPrev  = 0;   /* leftmost black cell on the line above  */
volatile uint8_t  g_edgeJumpHere  = 0;   /* ...on the offending line               */
volatile uint8_t  g_edgeJumpNext  = 0;   /* ...and below                           */
}
#endif  /* REVS_FILLWATCH */

volatile unsigned long g_tearFrames = 0;      /* painted frames whose source moved mid-decode */
volatile unsigned long g_tearRows   = 0;      /* total character rows caught moving           */
volatile uint16_t g_tearRowCount[BBC_SCREEN_ROWS] = {0}; /* per character row, how often      */
volatile uint16_t g_tearLastRow  = 0xFFFFu;
}

/* The raster line, from the two beam registers: VPOSR bit 0 is V8, VHPOSR's high byte is
   V7..V0.  Read VPOSR first — the pair is not atomic, and taking the high bit after the low
   byte can straddle a line-256 crossing. */
static inline uint16_t beamLine()
{
    uint16_t hi = *vposrPointer;
    uint16_t lo = *vhposrPointer;
    return (uint16_t)(((hi & 1u) << 8) | (lo >> 8));
}

/* ---------------------------------------------------------------------------
   ONE-TIME custom registers, written with the CPU.

   ⭐ THE SPLIT, and it is a rule rather than a preference (docs/amiga-lessons.md, and the
   same shape as the Atari port's AmigaHardware::setPlayfield + per-list CopperList::
   setPlayfield): a register that is CONSTANT for the whole run is written ONCE here with
   the CPU; only what genuinely varies down the screen or from frame to frame belongs in
   the copper list.  Re-emitting a constant from the copper every frame spends copper
   cycles — in the display window, the same cycles the bitplane fetch wants — to write a
   value that never changes.  What stays in the list: BPLCON0 + the interleave modulos
   (CopperList::setPlayfield), the bitplane pointers, the eight SPRxPT pairs, and the five
   palette bands.

   ⚠ Called with display DMA OFF and the copper halted (PlatformAmiga::run guarantees it).
   That ordering is load-bearing: with copper DMA on, COP1LC still points at the OS's
   LoadView(NULL) list, and an OS-copper frame landing here would overwrite these.
   --------------------------------------------------------------------------- */
void RevsScreen::setConstantRegisters()
{
    /* ⭐ THE DISPLAY WINDOW AND DATA FETCH — PAL lores 320x208, which is the BBC custom
       mode's exact height (26 character rows of 8 lines; bbc_screen.h).  Derived from kW/kH
       and kDisplayTop rather than written as four magic literals, because every copper band
       WAIT below is relative to kDisplayTop: if the window and the band waits could disagree
       the whole palette schedule slides, and a slid schedule looks like a decode bug.

         DDF: kW/16 = 20 words per line, and in LORES Agnus takes one 16-bit fetch every
       EIGHT colour clocks (hires is every four).  So DDFSTOP = DDFSTRT + 8*(words-1)
       = 0x38 + 8*19 = 0xD0, the standard 320-pixel lores pair.
       ⚠ The four values are CHECKED against that pair below, not just derived.  Getting the
       fetch stride wrong here (2 instead of 8) made Agnus fetch six words a line — colourful
       garbage on the left, black on the right — and NOTHING in the port's instrument set
       noticed: screen_dump.gdb decodes the bitplane BUFFER, and DDF/DIW affect only what
       Agnus scans out of it.  The buffer dump was byte-identical and the bands were at the
       same raster lines, so "verified on the target" was verified by an instrument that
       cannot see this class of bug at all.  Hence the compile-time check: the one place that
       CAN catch it is the build.  (docs/method-lessons.md, and cf. bbc_screen.h's rule that
       a derived number gets confirmed a second way.)

       ⚠ DIWHIGH is deliberately NOT written, and the framework's AmigaHardware::setPlayfield
       deliberately NOT called for this: it hard-codes a DIWHIGH whose VSTOP-high bit belongs
       to the Atari port's 276-line window.  Every field below fits the OCS-compatible 8-bit
       encoding (VSTRT 44, VSTOP 252, HSTRT 0x81, HSTOP 0xC1 + the implicit 256), so the high
       register has nothing to add here and writing that value would push VSTOP off the frame. */
    const uint16_t kDiwStrt = (uint16_t)((kDisplayTop << 8) | 0x81);
    const uint16_t kDiwStop = (uint16_t)((((kDisplayTop + kH) & 0xFF) << 8) | 0xC1);
    const uint16_t kDdfStrt = 0x0038;
    const uint16_t kDdfStop = (uint16_t)(kDdfStrt + 8 * ((kW / 16) - 1));
    static_assert(kDiwStrt == 0x2C81, "DIWSTRT: VSTRT must be 44, HSTRT 0x81");
    static_assert(kDiwStop == 0xFCC1, "DIWSTOP: VSTOP must be 252 = 44+208, HSTOP 0xC1");
    static_assert(kDdfStop == 0x00D0, "DDFSTOP: lores 320px is 0xD0 — check the fetch stride");
    *diwstrtPointer = kDiwStrt;
    *diwstopPointer = kDiwStop;
    *ddfstrtPointer = kDdfStrt;
    *ddfstopPointer = kDdfStop;

    /* Chip-set fetch mode.  0 = the OCS/ECS 16-bit fetch, which is the A500 target.  Write
       it rather than inherit it: on an AGA machine the OS may have left 32/64-bit fetch on,
       and a bitplane block sized for 16-bit fetch then reads garbage past its own end. */
    *fmodePointer   = 0x0000;

    /* No horizontal scroll — both playfields at delay 0. */
    *bplcon1Pointer = 0x0000;

    /* ⚠⚠⚠ THE PLAYFIELD/SPRITE PRIORITY CODES READ THE OTHER WAY ROUND, AND BOTH ARMS HERE HAD
       THEM INVERTED (measured on the target, 2026-09-21 — the tyre sprites appeared only over
       playfield colour 0, which is the signature of a playfield that is in FRONT).
         PFxP = 0   playfield in front of sprite group 0, i.e. IN FRONT OF EVERY SPRITE
         PFxP = N   playfield behind groups 0..N-1, in front of groups N..3
         PFxP = 4   playfield behind all four groups, i.e. EVERY SPRITE IN FRONT
       The cross-check is Intuition, which runs BPLCON2 = $0024 and shows the mouse pointer over
       the screen: $0024 is PF1P = PF2P = 4 = sprites in front.  The old comment here read the
       table upside down (there is no "group 4"), so the value that was supposed to keep the
       unpointed channels off the picture was doing the opposite — harmless only because the null
       sprites are disarmed, which is what actually fixed that artefact. */
    /* ⭐⭐ AND BIT 6, PF2PRI, IS THE DUAL-PLAYFIELD HALF OF THE SAME REGISTER: set = playfield 2
       in front of playfield 1.  PF2 is the COCKPIT and PF1 the terrain, so it must be set — the
       car has to occlude the road, never the other way round.  It is inert in a single-playfield
       build and in MODE 7, which is why it can live here with the other constants. */
#ifdef REVS_DUAL_PLAYFIELD
#define REVS_BPLCON2_PF2PRI 0x0040u
#else
#define REVS_BPLCON2_PF2PRI 0x0000u
#endif

#ifdef REVS_TYRE_SPRITES
    /* ⭐ SPRITES IN FRONT (PF1P = PF2P = 4).  The tyre patch IS a sprite layer and must win the
       priority fight — the whole split (playfield = outline, sprite = pattern) depends on the
       sprite's pixels landing over the playfield's, not under them.  The other six channels
       still point at the 8-byte null sprite, whose VSTART == VSTOP == 0 means they are never
       armed on any line, so nothing else can appear. */
    *bplcon2Pointer = (uint16_t)(0x0024u | REVS_BPLCON2_PF2PRI);
#else
    /* PLAYFIELD IN FRONT OF EVERY SPRITE GROUP — the BBC has no sprite layer, so nothing may
       ever appear over the game.  Belt and braces with the null sprites. */
    *bplcon2Pointer = (uint16_t)(0x0000u | REVS_BPLCON2_PF2PRI);
#endif

    /* ECS Denise border blanking: the area outside the display window renders BLACK instead
       of COLOR00.  Without it the border tracks the copper's current COLOR00 — which this
       port rewrites five times a field — so the surround would flash blue/black/green in
       step with the palette bands.  ⚠ Needs ECSENA (BPLCON0 bit 0), which the copper's
       BPLCON0 MOVE carries as USE_BPLCON3 (CopperList::setPlayfield); BPLCON3 is inert
       without it.  Bits 14-9 (0x0c00) are the BANK/PF2OFx reset field the framework writes.
       ⚠ DIWHIGH is deliberately NOT written: DIWSTRT/DIWSTOP (PlatformAmiga::run) are
       44..252 and 0x81..0x1C1, every field of which fits the OCS-compatible 8-bit encoding,
       and the framework's setPlayfield hard-codes a DIWHIGH whose VSTOP-high bit belongs to
       the Atari port's 276-line window — writing that here would push VSTOP off the bottom
       of the frame. */
    *bplcon3Pointer = (uint16_t)(0x0c00 | BPLCON3_BRDNBLNK | BPLCON3_BRDNTRAN);
}

/* --------------------------------------------------------------------------- */
#ifdef REVS_DUAL_PLAYFIELD
static void revs_cockpit_slot_tables(void);   /* the cockpit layer — defined with the rest of it */
#endif

static void tt_build_tables(void);   /* the MODE 7 painter's tables — see decodeTeletext */
void RevsScreen::initialize()
{
    for (unsigned b = 0; b < 256; b++) {
        s_expandHi[b] = expandNibble(b >> 4);
        s_expandLo[b] = expandNibble(b & 0x0Fu);
    }
    /* Until the first band record arrives, decode everything as MODE 5 — that is four of
       the five bands, and the fifth covers blank rows. */
    for (unsigned y = 0; y < kH; y++) m_lineMode[y] = 5;
#ifdef REVS_DUAL_PLAYFIELD
    revs_cockpit_slot_tables();
#endif

    m_bitmap[0]   = Bitmap::allocate(kW, kH, kBP, /*interleaved*/true);
    m_bitmap[1]   = Bitmap::allocate(kW, kH, kBP, /*interleaved*/true);
    m_copper      = CopperList::allocate(LIST_LENGTH);
    m_nullSprite  = Sprite::allocate(0);
    if (!m_bitmap[0] || !m_bitmap[1] || !m_copper || !m_nullSprite) return;
#ifdef REVS_DUAL_PLAYFIELD
    /* ⚠ MEMF_CLEAR is what makes the layer correct before its first conversion: an all-zero PF2
       is transparent everywhere, so the display is exactly the DUALPF=0 picture until the
       cockpit has something to say.  Bitmap::allocate clears. */
    m_cockpit = Bitmap::allocate(kW, kH, kBP, /*interleaved*/true);
    if (!m_cockpit) return;
    g_screenCockpitAddr = (uint32_t)m_cockpit->data;
#endif

    /* ⭐ BOTH plane buffers to the plot module, once — the glyph domain's delta painter keeps a
       few bytes a frame on rows nothing else repaints, so it must reach both (revs_plot.h).
       ⚠ Here and not in present(): it also builds the 16.6 KB address map, which is free at
       start-up and is a dropped frame inside one. */
    REVS_PLOT_PLANES((unsigned char*)m_bitmap[0]->data, (unsigned char*)m_bitmap[1]->data);
#ifdef REVS_DUAL_PLAYFIELD
    /* the cockpit layer's own plane, for the writers that paint onto it (§12f-iv) */
    revs_plot_cockpit_plane((unsigned char*)m_cockpit->data);
#endif

    /* MODE 7's own configuration.  ⚠ Allocated up front, never on the mode switch: a chip-RAM
       allocation inside a frame is the Atari port's 3.6-second freeze, and a FAILED one at a
       mode switch would blank the front end with no way to attribute it. */
    tt_build_tables();   /* the MODE 7 painter's glyph and mask tables — at start-up, not in a page */
    /* ⚠ The MODE 7 painter blits (tt_blit_clear), and PlatformAmiga::run enables only copper,
       raster and sprite DMA — the blitter's is whatever the OS left, so say it. */
    AmigaHardware::setDMAChannels(DMAF_BLITTER, true);
    m_ttBitmap = Bitmap::allocate(kTtW, kTtH, kTtBP, /*interleaved*/true);
    m_ttCopper = CopperList::allocate(TT_LIST_LENGTH);
    if (!m_ttBitmap) g_ttAllocFailed |= 1u;
    if (!m_ttCopper) g_ttAllocFailed |= 2u;

    setConstantRegisters();

    /* ⭐ ALL EIGHT SPRITE CHANNELS POINTED AT ONE EMPTY SPRITE, every frame.
       The BBC has no sprites and Revs never wants one, but sprite DMA is ON (PlatformAmiga
       ::run) — and a channel whose SPRxPT nothing writes does not sit still: Agnus ADVANCES
       the pointer as it fetches, so after one frame it is walking chip RAM and reading
       whatever it finds as control words.  Symptom on the target: garbage sprites tearing
       across the picture in colours nobody set (COLOR17..31 are still whatever the OS left),
       and with BPLCON2's playfield priority at its reset value they render IN FRONT of the
       game.  Two of the three artefacts this port was showing.
       Sprite::allocate(0) is 8 cleared bytes: control words 0,0 — VSTART == VSTOP == 0, so
       the channel is never armed on any line — followed by the 0,0 terminator.  ⚠ The list
       must re-point all eight EVERY frame, which is exactly what a copper list does; a
       one-time CPU write would hold for the first frame only. */
    for (unsigned s = 0; s < 8; s++)
        m_copper->showSprite(IDX_SPRITES + s * 2, (uint16_t)s, *m_nullSprite);

#ifdef REVS_NEEDLE_PLANES
    /* ⭐ THE NEEDLE IMAGES' CHIP POOL, allocated HERE and never inside a frame (a chip-RAM
       allocation mid-race is a multi-second freeze).  ~233 images over every input the game can
       produce, the largest a 32 x 29 pair: 20 K words covers them with room, and
       `g_needlePoolFull` counts an image that did not fit (it is then not shown). */
    {
        const unsigned long words = 20480u;
        unsigned short* pool = (unsigned short*)AllocMem(words * 2u, MEMF_CHIP | MEMF_CLEAR);
        revs_needle_pool(pool, pool ? words : 0u);
    }
#endif
#ifdef REVS_TYRE_SPRITES
    /* ⭐ TWO CHANNELS, TWO IMAGES EACH.  A channel shows one horizontal position per line and the
       two wheel arches are 304 pixels apart, so one channel cannot carry both sides however the
       images are laid out — channel 0 is the left arch and channel 1 the right.  The ALTERNATION
       is still just a pointer swap, which is the point.
       ⚠ Positions are set ONCE, here: the patch never moves.  DIWSTRT is $2C81, so screen pixel
       (0,0) is hardware (H $81, V 44) — but a SPRITE whose HSTART is $81 lands one lores pixel
       to the RIGHT of the playfield's first pixel, so the base is $80.  Measured on the target
       (2026-09-21): at $81 every sprite pixel pair straddled the MODE 5 pixel boundary, half of
       it over the playfield pixel it was supposed to replace.  ⚠ setX()'s argument is the 9-bit
       HSTART whose LSB is SH0, so ±1 here is one lores pixel, not one hires pixel. */
    m_tyreReady = false;
    for (unsigned side = 0; side < 2u; side++)
        for (unsigned st = 0; st < 2u; st++) {
            Sprite* sp = Sprite::allocate(REVS_TYRE_LINES);
            m_tyre[side][st] = sp;
            if (!sp) continue;
            sp->setX((uint16_t)(0x80u + (side ? REVS_TYRE_R_X : REVS_TYRE_L_X)));
            sp->setY((uint16_t)(kDisplayTop + REVS_TYRE_Y0));
        }
#endif

    /* Per-frame / per-band playfield state: BPLCON0 (plane count + ECSENA) and the
       interleave modulos.  These are the only playfield registers the copper touches —
       everything constant is in setConstantRegisters() above. */
#ifdef REVS_DUAL_PLAYFIELD
    /* ⭐ WRITTEN OUT RATHER THAN setPlayfield()'d, because the geometry is genuinely not the one
       that helper derives: it would compute the modulo for ONE four-plane interleaved bitmap
       (4*40 - 40 = 120), and this display is TWO two-plane ones (2*40 - 40 = 40 each).  The
       bitplane count and DBLPF are the only other difference. */
    {
        uint32_t* const d0 = m_copper->data();
        d0[IDX_PLAYFIELD + 0] = copperMove(bplcon0,
            (uint16_t)((kDisplayBP << PLNCNTSHFT) | DBLPF | USE_BPLCON3));
        d0[IDX_PLAYFIELD + 1] = copperMove(bpl1mod, kPlaneGap);   /* odd planes  = PF1 */
        d0[IDX_PLAYFIELD + 2] = copperMove(bpl2mod, kPlaneGap);   /* even planes = PF2 */
    }
    /* firstBitplane / delta = 2: BPL1PT+BPL3PT off the terrain bitmap, BPL2PT+BPL4PT off the
       cockpit's.  The cockpit's pointers are written ONCE — present() re-points only PF1. */
    m_copper->showBitmap(IDX_BPL,     *m_bitmap[0], 1, 2, 0, 0, kBP);
    m_copper->showBitmap(IDX_BPL + 4, *m_cockpit,   2, 2, 0, 0, kBP);
#else
    m_copper->setPlayfield(IDX_PLAYFIELD, kW, kH, kBP, /*interleaved*/true);
    m_copper->showBitmap(IDX_BPL, *m_bitmap[0], 1, 1, 0, 0, kBP);
#endif

    /* MODE 7's list is fixed, so it is built once here.  ⚠ The RACE list stays the one
       installed at start-up (Revs::initialize) even though the machine boots in MODE 7: the
       first VBI's applyMode() does the switch, which keeps "who installs the copper list" in
       exactly one place instead of two that can disagree. */
    buildTeletextCopper();

    g_screenCopperAddr  = (uint32_t)m_copper->data();
    g_screenCopperWords = LIST_LENGTH;
    g_screenFrontAddr  = (uint32_t)m_bitmap[0]->data;
    g_screenBytes      = (uint16_t)revs_mulu16(kH, kRowBytes);

    /* Start black.  The game's first band cycle installs the real palette; showing black
       until then matches the BBC, which also sets all four colours to black across the
       mode switch so nothing appears while the first frame is being drawn. */
    uint32_t* d = m_copper->data();
    for (unsigned pen = 0; pen < 4; pen++)
        d[IDX_TOPPAL + pen] = copperMove(color00 + (pen << 1), 0x000);
#ifdef REVS_DUAL_PLAYFIELD
    for (unsigned pen = 1; pen < 4; pen++)
        d[IDX_TOPPAL + 3 + pen] = copperMove(color00 + ((8u + pen) << 1), 0x000);
#endif
#ifdef REVS_NEEDLE_PLANES
    d[IDX_TOPPAL + PAL_BASE_WORDS + 0] = copperMove(color00 + (21u << 1), 0x000);
    d[IDX_TOPPAL + PAL_BASE_WORDS + 1] = copperMove(color00 + (25u << 1), 0x000);
#endif

    /* ⭐ LAST LINE OF THE FUNCTION, deliberately: this is what opens the scene to the VERTB
       handler (see m_built in the header, and PlatformAmiga::run for the black screen it
       cost).  Every early `return` above therefore leaves it clear, which is right — a scene
       that failed to allocate must not be driven either. */
    m_built = 1;
}

/* ═══════════════════════════════════════════════════════════════════════════════════════════
   MODE 7 — the front end.  src/platform/teletext.h is the model; this is only the display.
   ═══════════════════════════════════════════════════════════════════════════════════════════ */

/* The display window, which differs between the two configurations: 208 lines for the race
   view, 250 for MODE 7's 25 rows of 10.
   ⚠ THE OCS DIWSTOP ENCODING IS WHY THIS IS ONE FUNCTION AND NOT TWO LITERALS.  DIWSTOP's
   vertical bit 8 is not stored — the hardware supplies it as the COMPLEMENT of bit 7 — so the
   low eight bits are written for both and the two cases land on different sides of 256 without
   any special case: 44+208 = 252 -> $FC (bit 7 set, V8 = 0), 44+250 = 294 -> $26 (bit 7 clear,
   V8 = 1, giving 256+38).  Writing 294 & 0xFF "by accident" is correct here; writing 294
   truncated to a byte anywhere else would not be, which is exactly why it is derived once. */
void RevsScreen::setDisplayWindow(unsigned height)
{
    const unsigned vstop = kDisplayTop + height;
    *diwstrtPointer = (uint16_t)((kDisplayTop << 8) | 0x81);
    *diwstopPointer = (uint16_t)(((vstop & 0xFFu) << 8) | 0xC1);
}

/* The MODE 7 list is FIXED — built once, never rewritten.  Nothing about a teletext display
   varies down the screen: one plane count, one set of pointers (single-buffered), one palette
   of eight.  All the per-frame work is in the bitmap. */
void RevsScreen::buildTeletextCopper()
{
    if (!m_ttCopper || !m_ttBitmap || !m_nullSprite) return;

    for (unsigned s = 0; s < 8; s++)
        m_ttCopper->showSprite(IDX_TT_SPRITES + s * 2, (uint16_t)s, *m_nullSprite);
    m_ttCopper->setPlayfield(IDX_TT_PLAYFIELD, kTtW, kTtH, kTtBP, /*interleaved*/true);
    m_ttCopper->showBitmap(IDX_TT_BPL, *m_ttBitmap, 1, 1, 0, 0, kTtBP);

    /* ⭐ The pen index IS the teletext colour code, so COLORnn = colour n with no mapping
       table — see kTtPalette and the plane-building loop in decodeTeletext(). */
    uint32_t* d = m_ttCopper->data();
    for (unsigned c = 0; c < 8; c++)
        d[IDX_TT_PAL + c] = copperMove(color00 + (c << 1), kTtPalette[c]);
}

/* ⭐⭐ THE PAGE -> THREE BITPLANES, CELL BY CELL, ONLY WHERE THE PICTURE CHANGES.  Main loop.
 *
 * A full page is 1000 cells x 10 lines x 3 planes = 30000 byte stores, and the row-at-a-time loop
 * this replaces measured **~4380 raster lines — ~280 ms — for one** on an A500 (`make TTTIME=1`).
 * It ran on every flash-phase flip (~1.6 a second, the whole page re-dirtied for one flashing
 * prompt), so the idle menu spent ~45% of the CPU repainting an unchanged page, and each new page
 * visibly wiped down the single-buffered bitmap for a quarter of a second.  Three changes:
 *
 *  1. ⭐⭐ THE PAGE ITSELF IS THE DIRTY RECORD, not the writers' marks.  The game pokes screen RAM
 *     directly (teletext.h: $3A65, $65BA, $659A) — `menu_wait_key`'s highlight among them — and
 *     none of those marked a row, so a moved highlight appeared only when the next flash flip
 *     repainted everything: up to 0.64 s late.  Each row's 40 bytes are compared against a shadow
 *     of the bytes the bitmap was drawn from (10 longword compares a row, ~1.3 ms a page, EQUALITY
 *     ONLY so byte order cannot be observed).  `g_ttRowDirty` stays as an extra hint.
 *  2. ⭐ A CHANGED ROW IS DECODED, AND ONLY ITS CHANGED CELLS ARE PAINTED: every DISPLAY cell keeps
 *     the key of what is drawn there (the decoded cell's TT_KEY plus which half of a double-height glyph),
 *     so a highlight repaints its attribute's cells and a flash flip its flashing ones.
 *  3. A flash flip re-decodes only the rows that CARRY a flash code (`s_ttRowFlash`).
 * The cells are painted in PAIRS, a word per plane per line, unrolled with constant displacements,
 * and a pair of blank glyphs is thirty constant stores with no glyph reads.
 *
 * ⚠ The shadows are exact because nothing else writes `m_ttBitmap` (allocated cleared, drawn only
 *   here), which is why the mode switch no longer needs to force a repaint of the whole page.
 * ⚠ `make TTCHECK=1` is the gate: after every decode it repaints the WHOLE page with the old
 *   row loop (`tt_reference_page`) into a scratch buffer and compares all 30000 bytes.  Its scope:
 *   it shares `tt_decode_row` and the font, so it checks what this code SKIPS and how it draws,
 *   never the decode itself — that is `make mode7`'s decoder differential (tools/validate_mode7.c). */
#ifdef REVS_TT_TIME
extern "C" volatile uint16_t g_vbiCount;
/* `make TTTIME=1` — the front end's cost, in RASTER LINES read in-program (`amiga/tttime.gdb`).
   ⚠ In-program because a gdb `finish` through FS-UAE's stub reads the same beam delta for a
   25-row decode as for an empty one. */
extern "C" {
volatile uint32_t g_ttTimeLast = 0, g_ttTimeMax = 0, g_ttTimeTotal = 0, g_ttTimeCalls = 0;
volatile uint16_t g_ttTimeCells = 0;
/* The worst call split: its decode (tt_decode_row_keys) lines, its rows decoded, its cells painted. */
volatile uint32_t g_ttTimeMaxDecode = 0, g_ttTimeMaxRows = 0, g_ttTimeMaxCells = 0;
}
static inline uint32_t tt_beam_lines(void)
{
    uint16_t v0, v1, line;
    do {                                  /* the framework's own beam read (AmigaHardware.cpp) */
        v0   = g_vbiCount;
        line = (uint16_t)(((*vposrPointer & 1u) << 8) | (*vhposrPointer >> 8));
        v1   = g_vbiCount;
    } while (v0 != v1);
    return revs_mulu16(v0, 313u) + line;
}
#endif
extern "C" { volatile uint16_t g_ttCellsDrawn = 0; }   /* cells painted by the last decode */

static uint32_t      s_ttSrc[TT_ROWS * TT_COLS / 4];   /* the page bytes the bitmap shows */
static uint32_t      s_ttKey[TT_ROWS * TT_COLS];       /* per DISPLAY cell: what is drawn there */
static unsigned long s_ttRowFlash = 0;                 /* rows carrying a flash code */
static unsigned char s_ttPrimed   = 0;
/* ⚠ The painter's tables are EXTERNAL (extern "C", g_ names) because the asm row painter
   (teletext_m68k.s, `make TTASM=1`) reads them by name. */
extern "C" {
unsigned char g_ttBlank[TT_SETS * 128u];               /* 1 = the glyph has no set pixel */
/* ⭐ CELLS ARE PAINTED IN PAIRS, ONE WORD PER PLANE PER LINE: a cell is one byte of a plane row,
   so an even column and the next share an aligned word, and pairing halves the store count
   (a 68000 word store costs what a byte store does).  The left cell is the HIGH byte — the
   bitmap is big-endian memory the display reads, not mem[].  Two tables make the pair cheap:
   every glyph line pre-shifted into the high byte (a `lsl.w #8` is 22 cycles a line), and each
   (fg, bg)'s plane masks, high and low. */
uint16_t g_ttGlyphHi[TT_SETS * 128u << TT_GLYPH_SHIFT];
uint16_t g_ttMaskHi[64][6], g_ttMaskLo[64][6];         /* [fg | bg << 3] -> m0 m1 m2 c0 c1 c2 */
}

static void tt_build_tables(void)
{
    for (unsigned gi = 0; gi < TT_SETS * 128u; gi++) {
        const uint8_t* g = &g_ttFont[gi << TT_GLYPH_SHIFT];
        uint8_t any = 0;
        for (unsigned y = 0; y < TT_CELL_H; y++) {
            any |= g[y];
            g_ttGlyphHi[(gi << TT_GLYPH_SHIFT) + y] = (uint16_t)((unsigned)g[y] << 8);
        }
        g_ttBlank[gi] = (unsigned char)(any == 0);
    }
    /* With fg/bg as per-plane 0x00/0xFF masks, plane p is the glyph where the foreground has
       bit p and its complement where the background does: `(bits & (f ^ b)) ^ b` — two
       operations, no branch, and a constant when f == b. */
    for (unsigned cb = 0; cb < 64u; cb++) {
        const unsigned fg = cb & 7u, bg = cb >> 3;
        for (unsigned p = 0; p < 3u; p++) {
            const unsigned f = (fg >> p) & 1u ? 0xFFu : 0u, b = (bg >> p) & 1u ? 0xFFu : 0u;
            g_ttMaskLo[cb][p]      = (uint16_t)(f ^ b);  g_ttMaskHi[cb][p]      = (uint16_t)((f ^ b) << 8);
            g_ttMaskLo[cb][3u + p] = (uint16_t)b;        g_ttMaskHi[cb][3u + p] = (uint16_t)(b << 8);
        }
    }
    for (unsigned i = 0; i < TT_ROWS * TT_COLS; i++) s_ttKey[i] = 0xFFFFFFFFu;  /* no TT_KEY sets bits 22-31 */
}

/* One glyph line of the pair into the three planes; D is the pair's word, W the line's glyph
   word, Y the display line.  kTtPlaneGap and kTtRowBytes are in bytes, D indexes words. */
#define TT_LINE(Y, W)  do {                                                               \
        const uint16_t w_ = (W);                                                          \
        d[(Y) * (kTtRowBytes / 2)]                         = (uint16_t)((w_ & M0) ^ C0); \
        d[(Y) * (kTtRowBytes / 2) + kTtPlaneGap / 2]       = (uint16_t)((w_ & M1) ^ C1); \
        d[(Y) * (kTtRowBytes / 2) + kTtPlaneGap]           = (uint16_t)((w_ & M2) ^ C2); \
    } while (0)
#define TT_GW(I)  ((uint16_t)(h[I] | l[I]))

/* HALF: 0 = a normal glyph, 1/2 = the top/bottom half of a double-height one, each source line
   stretched over two display lines — the chip's rule.
   ⚠⚠ NOINLINE, and that is the whole point: inlined into the row loop GCC addressed the pair as
   `(0,a0,d0.l)`, kept the row offsets in stack slots and SPILLED the glyph word to the frame every
   line — 17 instructions a pair-line, ~60% of them memory operands, single-stepped.  Out of line
   the destination is a plain pointer with constant displacements and the six masks, the glyph
   word and one temporary are exactly d0-d7. */
static __attribute__((noinline))
void tt_paint_pair(uint16_t* d, uint32_t ka, uint32_t kb, unsigned half)
{
    const uint16_t* ma = g_ttMaskHi[TT_KEY_COLOUR(ka)];
    const uint16_t* mb = g_ttMaskLo[TT_KEY_COLOUR(kb)];
    const uint16_t M0 = (uint16_t)(ma[0] | mb[0]), M1 = (uint16_t)(ma[1] | mb[1]);
    const uint16_t M2 = (uint16_t)(ma[2] | mb[2]), C0 = (uint16_t)(ma[3] | mb[3]);
    const uint16_t C1 = (uint16_t)(ma[4] | mb[4]), C2 = (uint16_t)(ma[5] | mb[5]);
    const unsigned ga = TT_KEY_GLYPH(ka), gb = TT_KEY_GLYPH(kb);

    if (g_ttBlank[ga] && g_ttBlank[gb]) {     /* the backgrounds alone: thirty constant stores */
        TT_LINE(0, 0); TT_LINE(1, 0); TT_LINE(2, 0); TT_LINE(3, 0); TT_LINE(4, 0);
        TT_LINE(5, 0); TT_LINE(6, 0); TT_LINE(7, 0); TT_LINE(8, 0); TT_LINE(9, 0);
        return;
    }
    const uint16_t* h = &g_ttGlyphHi[ga << TT_GLYPH_SHIFT];
    const uint8_t*  l = &g_ttFont[gb << TT_GLYPH_SHIFT];
    if (!half) {
        TT_LINE(0, TT_GW(0)); TT_LINE(1, TT_GW(1)); TT_LINE(2, TT_GW(2)); TT_LINE(3, TT_GW(3));
        TT_LINE(4, TT_GW(4)); TT_LINE(5, TT_GW(5)); TT_LINE(6, TT_GW(6)); TT_LINE(7, TT_GW(7));
        TT_LINE(8, TT_GW(8)); TT_LINE(9, TT_GW(9));
    } else {
        const unsigned o = (half - 1u) * (TT_CELL_H / 2u);
        h += o; l += o;
        TT_LINE(0, TT_GW(0)); TT_LINE(1, TT_GW(0)); TT_LINE(2, TT_GW(1)); TT_LINE(3, TT_GW(1));
        TT_LINE(4, TT_GW(2)); TT_LINE(5, TT_GW(2)); TT_LINE(6, TT_GW(3)); TT_LINE(7, TT_GW(3));
        TT_LINE(8, TT_GW(4)); TT_LINE(9, TT_GW(4));
    }
}
#undef TT_GW
#undef TT_LINE

/* ⭐⭐ BLANK-ON-BLACK RUNS GO TO THE BLITTER.  Measured over the COMPETITION=1 menu walk, 74% of the
   pairs a page change paints are two blank glyphs, ~63% of the paint, and on a black background
   every plane word is zero — a D-only blitter clear, which runs BESIDE the CPU (the program is in
   fast RAM).  A run of such pairs along a display row is one blit: 10 lines x 3 interleaved plane
   rows = height 30, `words` wide, modulo the rest of the 40-byte plane row.  The CPU paints the
   other pairs of the row into different words, so the two never write the same word.
   ⚠ DIRECT, NEVER QUEUED: the port masks blit-done (PlatformAmiga::run), so the framework's queue
   would never advance; this waits for the previous blit (AmigaHardware::blitterWait) instead,
   which the CPU's own painting between two runs normally hides.  ⚠ And nothing may read or repaint the bitmap until the last
   blit is done — decodeTeletext and ttCheck call tt_blit_wait() first.
   ⚠ No OwnBlitter(): the port owns the machine in this window and nothing else blits (the
   framework's routines make the same assumption). */
static inline void tt_blit_wait(void) { AmigaHardware::blitterWait(); }
/* ⚠ The wait first is what makes blitterClear take its DIRECT arm: that arm needs the blitter idle
   and the queue empty, and nothing in the port ever queues. */
static void tt_blit_clear(uint8_t* dst, unsigned words)
{
    AmigaHardware::blitterWait();
    AmigaHardware::blitterClear((uint16_t*)(void*)dst, (uint16_t)words,
                                (uint16_t)(TT_CELL_H * kTtBP), (int16_t)(kTtPlaneGap - 2u * words));
}

#ifdef REVS_TT_ASM
/* ⭐ `make TTASM=1` (the DEFAULT): the row painter below in 68000 registers (teletext_m68k.s), with
   one routine per foreground for the pairs that share a colour on black.  This C stays the
   reference and the TTASM=0 control; `make TTCHECK=1` gates either against the old row loop. */
extern "C" unsigned tt_paint_row_m68k(uint8_t* rowTop, uint32_t* keys, const uint32_t* cells, unsigned half);
extern "C" void tt_blit_clear_c(uint8_t* dst, unsigned words) { tt_blit_clear(dst, words); }
#define TT_PAINT_ROW tt_paint_row_m68k
#else
#define TT_PAINT_ROW tt_paint_row
#endif

/* One DISPLAY row from 40 decoded keys (teletext.h: TT_KEY): paint every PAIR in which either key
   moved.  HALF goes into the key's free bits 12-13, so a double-height half is a different key
   from the same glyph drawn normally.  Returns the cells painted.
   ⭐ The unchanged pair is the common case — every pair of every changed row is visited — so it is
   two longword compares and nothing else (reading the fields back out of TtCells was ~32
   instructions a pair, single-stepped). */
static unsigned tt_paint_row(uint8_t* rowTop, uint32_t* keys, const uint32_t* cells, unsigned half)
{
    const uint32_t hk = (uint32_t)half << 12;
    unsigned painted = 0, runAt = 0, runLen = 0;

    for (unsigned col = 0; col < TT_COLS; col += 2u) {
        const uint32_t ka = cells[col] | hk, kb = cells[col + 1u] | hk;
        if (ka == keys[col] && kb == keys[col + 1u]) {
            if (runLen) { tt_blit_clear(rowTop + runAt, runLen); runLen = 0; }
            continue;
        }
        keys[col] = ka; keys[col + 1u] = kb;
        painted += 2u;
        if ((g_ttBlank[TT_KEY_GLYPH(ka)] & g_ttBlank[TT_KEY_GLYPH(kb)])
            && !((ka | kb) & TT_KEY(0u, 0u, 0u, 7u))) {             /* blank on black: a run */
            if (!runLen) runAt = col;
            runLen++;
            continue;
        }
        if (runLen) { tt_blit_clear(rowTop + runAt, runLen); runLen = 0; }
        tt_paint_pair((uint16_t*)(void*)(rowTop + col), ka, kb, half);
    }
    if (runLen) tt_blit_clear(rowTop + runAt, runLen);
    return painted;
}

void RevsScreen::decodeTeletext()
{
    if (!m_ttBitmap) return;
#ifdef REVS_TT_TIME
    const uint32_t t0 = tt_beam_lines();
    uint32_t tDecode = 0;
#endif
    tt_blit_wait();                       /* the last decode's final run, normally long done */

    const unsigned char phase = (unsigned char)tt_flash_phase();
    unsigned long force = g_ttRowDirty;         /* the VDU driver's and *LOAD's marks: a hint */
    g_ttRowDirty = 0;
    if (phase != m_ttFlashSeen) { force |= s_ttRowFlash; m_ttFlashSeen = phase; }
    if (!s_ttPrimed) { force = (1UL << TT_ROWS) - 1UL; s_ttPrimed = 1; }

    /* ENDIAN-OK: equality only — the page is compared against a copy of itself, never read as a
       value, so byte order cannot be observed (the bbc_hw.cpp band-input precedent). */
    const uint32_t* page = (const uint32_t*)(const void*)(mem + TT_SCREEN_BASE);
    uint32_t*       seen = s_ttSrc;
    uint32_t*       keys = s_ttKey;
    uint8_t*      rowTop = (uint8_t*)m_ttBitmap->data;
    const unsigned kRowStride = TT_CELL_H * kTtRowBytes;   /* bytes one cell row occupies */

    unsigned long oldDbl = m_ttRowDbl, newDbl = 0;
    int      hidden  = 0;                /* this row is the bottom half of the one above */
    uint16_t decoded = 0, painted = 0;
    for (unsigned row = 0; row < TT_ROWS;
         row++, page += TT_COLS / 4u, seen += TT_COLS / 4u, keys += TT_COLS, rowTop += kRowStride) {
        const unsigned long bit = 1UL << row;
        /* ⭐ Unrolled: the loop form paid two pointer bumps and a bound test per longword, ~66
           cycles each (2322 instructions a field over the idle page, single-stepped). */
        const int changed =
            page[0] != seen[0] || page[1] != seen[1] || page[2] != seen[2] || page[3] != seen[3] ||
            page[4] != seen[4] || page[5] != seen[5] || page[6] != seen[6] || page[7] != seen[7] ||
            page[8] != seen[8] || page[9] != seen[9];
        typedef char tt_row_is_ten_longs[TT_COLS == 40u ? 1 : -1];
        if (changed)
            for (unsigned i = 0; i < TT_COLS / 4u; i++) seen[i] = page[i];

        /* A row under a double-height row is not displayed — the chip's rule; the row above
           painted this display row with its bottom halves. */
        if (hidden) { hidden = 0; continue; }
        if (!changed && !(force & bit)) {
            if (oldDbl & bit) { newDbl |= bit; hidden = 1; }
            continue;
        }

        const unsigned char* src = mem + TT_SCREEN_BASE + row * TT_COLS;
        uint32_t cells[TT_COLS];
#ifdef REVS_TT_TIME
        const uint32_t td = tt_beam_lines();
#endif
        const int flags = tt_decode_row_keys(src, cells, (int)phase);
#ifdef REVS_TT_TIME
        tDecode += tt_beam_lines() - td;
#endif
        const int dbl   = flags & 1;
        if (flags & 2) s_ttRowFlash |= bit; else s_ttRowFlash &= ~bit;
        decoded++;

        painted += TT_PAINT_ROW(rowTop, keys, cells, dbl ? 1u : 0u);
        if (dbl) {
            newDbl |= bit;
            hidden  = 1;
            if (row + 1u < TT_ROWS)
                painted += TT_PAINT_ROW(rowTop + kRowStride, keys + TT_COLS, cells, 2u);
        } else if (oldDbl & bit) {
            force |= bit << 1;           /* lost double height: the row below shows itself again */
        }
    }

    m_ttRowDbl     = newDbl;
    g_ttRowsDrawn  = decoded;
    g_ttCellsDrawn = painted;
    tt_blit_wait();   /* a decode is FINISHED when its blits are: the timer and the check both mean that */
#ifdef REVS_TT_CHECK
    ttCheck();
#endif
#ifdef REVS_TT_TIME
    if (painted) {
        const uint32_t dt = tt_beam_lines() - t0;
        g_ttTimeLast = dt; g_ttTimeCells = painted; g_ttTimeTotal += dt; g_ttTimeCalls++;
        if (dt > g_ttTimeMax) {
            g_ttTimeMax = dt; g_ttTimeMaxDecode = tDecode; g_ttTimeMaxRows = decoded; g_ttTimeMaxCells = painted;
        }
    }
#endif
}

#ifdef REVS_TT_CHECK
/* ⭐ `make TTCHECK=1` — the REFERENCE: the row loop this file shipped until the cell decode,
   kept verbatim in shape (whole page, every row, double height consumed the chip's way) and run
   into a scratch buffer after every decode, then compared byte for byte with the live bitmap.
   `g_ttCheckMismatch` must read 0 with `g_ttCheckRuns` non-zero (`amiga/ttcheck.gdb`). */
extern "C" {
volatile uint32_t g_ttCheckRuns = 0, g_ttCheckMismatch = 0, g_ttCheckFirst = 0xFFFFFFFFu;
}
static void tt_reference_page(uint8_t* base, int phase)
{
    uint8_t* const limit = base + revs_mulu16(kTtH, kTtRowBytes);
    const unsigned kRowStride = TT_CELL_H * kTtRowBytes;
    unsigned  row    = 0;
    uint8_t*  rowTop = base;
    while (row < TT_ROWS) {
        const unsigned char* src = mem + TT_SCREEN_BASE + row * TT_COLS;
        TtCell cells[TT_COLS];
        const int dbl = tt_decode_row(src, cells, phase);
        const unsigned halves = dbl ? 2u : 1u;
        uint8_t* halfBase = rowTop;
        for (unsigned half = 0; half < halves; half++, halfBase += kRowStride) {
            if (halfBase + kRowStride > limit) break;
            for (unsigned col = 0; col < TT_COLS; col++) {
                const TtCell* c = &cells[col];
                const uint8_t* g = &g_ttFont[((unsigned)c->set * 128u + c->code) << TT_GLYPH_SHIFT];
                const uint8_t f0 = (c->fg & 1u) ? 0xFFu : 0x00u;
                const uint8_t f1 = (c->fg & 2u) ? 0xFFu : 0x00u;
                const uint8_t f2 = (c->fg & 4u) ? 0xFFu : 0x00u;
                const uint8_t b0 = (c->bg & 1u) ? 0xFFu : 0x00u;
                const uint8_t b1 = (c->bg & 2u) ? 0xFFu : 0x00u;
                const uint8_t b2 = (c->bg & 4u) ? 0xFFu : 0x00u;
                uint8_t* dst = halfBase + col;
                for (unsigned y = 0; y < TT_CELL_H; y++) {
                    const uint8_t bits = dbl ? g[half * (TT_CELL_H / 2) + (y >> 1)] : g[y];
                    const uint8_t inv  = (uint8_t)~bits;
                    dst[0]               = (uint8_t)((bits & f0) | (inv & b0));
                    dst[kTtPlaneGap]     = (uint8_t)((bits & f1) | (inv & b1));
                    dst[kTtPlaneGap * 2] = (uint8_t)((bits & f2) | (inv & b2));
                    dst += kTtRowBytes;
                }
            }
        }
        if (dbl) { rowTop += kRowStride; row++; }
        rowTop += kRowStride;
        row++;
    }
}
/* ⭐ THE ORACLE'S OWN STIMULUS, run once before the first real page: the menus never TOGGLE double
   height under unchanged cells, and that is exactly what the key's `half` nibble exists for (a
   sabotage that dropped it survived a whole menu walk).  Teletext's own convention makes it the
   common case — a double-height line is printed twice, on the row and the row below — so a row
   turning double leaves every cell to the right of its $8D decoding the same, and only the half
   says "repaint".  Five pages, each decoded (and checked): plain text twice; the same with $8D on
   both rows; plain again; double height on row 24 (whose bottom half is off the screen); and two
   stacked double rows, the lower one hidden.  The real page is restored afterwards and the page
   compare repaints it. */
static void tt_selftest_page(unsigned which)
{
    static const char kText[] = "DOUBLE HEIGHT AND THE ROW BELOW IT";
    for (unsigned i = 0; i < TT_SCREEN_SIZE; i++) mem[TT_SCREEN_BASE + i] = 0x20u;
    const unsigned rows[2] = { which == 3u ? 24u : 3u, which == 3u ? 24u : 4u };
    for (unsigned r = 0; r < 2u; r++) {
        unsigned char* d = mem + TT_SCREEN_BASE + rows[r] * TT_COLS;
        d[0] = (which == 1u || which == 3u || which == 4u) ? 0x8Du : 0x20u;
        d[1] = 0x83u;                                       /* yellow text */
        for (unsigned x = 0; kText[x] && x + 2u < TT_COLS; x++) d[x + 2u] = (unsigned char)kText[x];
    }
    if (which == 4u) {                                      /* rows 5/6 double too: 4 is hidden */
        for (unsigned x = 0; x < TT_COLS; x++)
            mem[TT_SCREEN_BASE + 5u * TT_COLS + x] = mem[TT_SCREEN_BASE + 6u * TT_COLS + x] =
                mem[TT_SCREEN_BASE + 3u * TT_COLS + x];
    }
}
void RevsScreen::ttSelfTest()
{
    static unsigned char page[TT_SCREEN_SIZE];
    for (unsigned i = 0; i < TT_SCREEN_SIZE; i++) page[i] = mem[TT_SCREEN_BASE + i];
    static const unsigned char kSeq[] = { 0u, 1u, 0u, 3u, 0u, 4u, 1u, 0u };
    for (unsigned i = 0; i < sizeof kSeq; i++) { tt_selftest_page(kSeq[i]); decodeTeletext(); }
    for (unsigned i = 0; i < TT_SCREEN_SIZE; i++) mem[TT_SCREEN_BASE + i] = page[i];
}
void RevsScreen::ttCheck()
{
    static unsigned char selfTested = 0;
    if (!selfTested) { selfTested = 1; ttSelfTest(); return; }   /* the next decode repaints the real page */
    static uint8_t* ref = 0;
    const unsigned n = revs_mulu16(kTtH, kTtRowBytes);
    if (!ref) ref = (uint8_t*)AllocMem(n, MEMF_ANY | MEMF_CLEAR);
    if (!ref) return;
    tt_reference_page(ref, (int)m_ttFlashSeen);
    const uint8_t* live = (const uint8_t*)m_ttBitmap->data;
    for (unsigned i = 0; i < n; i++)
        if (live[i] != ref[i]) {
            if (g_ttCheckFirst == 0xFFFFFFFFu) g_ttCheckFirst = i;
            g_ttCheckMismatch++;
        }
    g_ttCheckRuns++;
}
#endif

/* ⭐ HAND THE DISPLAY TO WHICHEVER MODE THE MACHINE IS IN.  VBI context.
 *
 * The switch is one COP1LC write plus COPJMP1, and a DIW change.  It happens twice a session,
 * so a single glitched field at the transition is acceptable — a real BBC's mode change is far
 * more violent than that.  Returns non-zero when it switched, so the caller does not then go on
 * to rebuild bands into a list that is no longer the active one. */
/* ⭐ THE COPPER'S BAND WORDS ARE MEMOISED TOO (buildBands).  They are a pure function of the band
   snapshot, the plan built from it and the needle pens, and the list keeps its words between
   frames — so the ~60 colour conversions and word stores a presented frame were rebuilding the
   same list (1.5k instructions a frame single-stepped, in the VERTB ISR: wall clock).  Set by
   whatever changes an input: a plan rebuild (buildLineModes — which every snapshot change forces)
   and a switch back to the race list.  prepareFrame writes it only while `m_ready` is clear and
   buildBands reads it only while it is set, so the two never overlap. */
static unsigned char s_copperBandsStale = 1u;

int RevsScreen::applyMode()
{
    const unsigned char want = tt_active() ? 1u : 0u;

    /* ⚠ CROSS-CHECK, not a second source of truth.  `$64` is the GAME's own mode flag, written
       only by $16E1 (clear, entering the race) and $4F3B (set, in irq1v_release).  This port
       derives the mode from HARDWARE events instead — the engine's VDU 22,7 and hw_init's CRTC
       writes — so the two derivations are independent and a disagreement means one of them is
       wrong.  Counted rather than resolved here, because guessing which to believe is how an
       assumption calcifies; the counter says whether there is anything to resolve. */
    if ((unsigned char)((mem[0x64] & 0x80u) ? 1u : 0u) != want) g_ttModeDisagree++;

    if (want == m_ttOnScreen) return 0;
    /* ⚠ VALIDATE BEFORE LATCHING.  Latching m_ttOnScreen first and then bailing on a missing
       bitmap would record a switch that never happened and never retry it — the display would
       stay on the race list for the whole run with no counter moving.  Cost me a target run. */
    if (want && (!m_ttCopper || !m_ttBitmap)) return 0;
    m_ttOnScreen = want;

    setDisplayWindow(want ? kTtH : kH);
    if (want) {
        /* ⭐ No forced repaint: decodeTeletext() compares the page against the bytes the bitmap
           was drawn from, and nothing else writes the bitmap, so whatever the page holds now is
           painted on the next decode and nothing that is already right is painted twice. */
        g_screenCopperAddr  = (uint32_t)m_ttCopper->data();
        g_screenCopperWords = TT_LIST_LENGTH;
        g_screenFrontAddr   = (uint32_t)m_ttBitmap->data;
        g_screenBytes       = (uint16_t)revs_mulu16(kTtH, kTtRowBytes);
        g_screenPlanes      = kTtBP;
        g_screenHeight      = kTtH;
        g_screenMode7       = 1;
        AmigaHardware::setCopperList(*m_ttCopper, /*immediate*/true);
    } else {
        s_copperBandsStale  = 1u;                 /* back on the race list: rebuild its bands */
        g_screenCopperAddr  = (uint32_t)m_copper->data();
        g_screenCopperWords = LIST_LENGTH;
        g_screenFrontAddr   = (uint32_t)m_bitmap[m_back ^ 1u]->data;
        g_screenBytes       = (uint16_t)revs_mulu16(kH, kRowBytes);
        g_screenPlanes      = kDisplayBP;
        g_screenHeight      = kH;
        g_screenMode7       = 0;
        AmigaHardware::setCopperList(*m_copper, /*immediate*/true);
    }
    return 1;
}

void RevsScreen::shutdown()
{
    delete m_ttCopper;    m_ttCopper   = 0;
    delete m_ttBitmap;    m_ttBitmap   = 0;
    delete m_copper;      m_copper     = 0;
    delete m_bitmap[0];   m_bitmap[0]  = 0;
    delete m_bitmap[1];   m_bitmap[1]  = 0;
    delete m_nullSprite;  m_nullSprite = 0;
}

/* ---------------------------------------------------------------------------
   The band snapshot -> the raster PLAN (main loop), then the plan -> the copper (VBI).

   ⭐ WHY IT IS TWO HALVES.  The line boundaries are shared: the per-line MODE table is read
   by decode() in the main loop, and the copper WAITs are written in the VBI at the swap, and
   both must describe the SAME frame.  Deriving them twice from a record that moves 50 times a
   second is how they came apart.  So the arithmetic runs ONCE, in the main loop, into
   m_plan — and the VBI only copies numbers into copper words, which is all an ISR should do.
   (It also removes the old main-loop-reads / VBI-writes race on m_lineMode.)
   --------------------------------------------------------------------------- */
/* ⭐⭐ THE MODE TABLE IS FILLED A LONGWORD AT A TIME, AND THAT IS WORTH 1.3 ms A FRAME.
   Five bands cover all 208 display lines, so the byte-at-a-time fill this replaces ran 208
   times a frame and measured 1.64 ms (~56 cycles a line for one `move.b` — probe.h
   §DECODESPLIT carves it).  A band boundary is not 4-aligned, so head and tail bytes stay
   per-byte and only the middle widens; `m_lineMode` is `aligned(4)`, which is what makes the
   middle a legal `move.l` at all.
   ENDIAN-OK, and it is the documented exception to the mem[]-aliasing rule (CLAUDE.md): all
   four bytes of the written longword are the SAME value, so byte order cannot be observed. */
static inline void revs_fill_modes(unsigned char* p, int a, int b, unsigned char v)
{
    while (a < b && (a & 3)) p[a++] = v;
    int n = (b - a) >> 2;
    if (n > 0) {
        uint32_t w = (uint32_t)v;
        w |= w << 8;
        w |= w << 16;
        uint32_t* q = (uint32_t*)(void*)(p + a);
        a += n << 2;
        do { *q++ = w; } while (--n);
    }
    while (a < b) p[a++] = v;
}

/* ⭐⭐⭐ IS EVERY DISPLAY LINE IN [lo,hi) CLAIMED BY A PAINTER?  This is the whole remaining
   question the frame-buffer conversion answers, and it is asked HERE — from buildLineModes, per
   BAND, while the band's line range is already in registers — because a band whose palette is
   flat need not be asked at all and a band that is wholly owned costs one `cmp.l` per four lines.
   ⚠ It replaces a FIXED cold-frame count, which was wrong: Silverstone's holes stop at decode #2
   but Donington's ran to decode #54, so "convert the first two frames" showed ~45 stale lines for
   ten seconds of a race.  Exactness is cheap here and a guess is not.
   ⚠ ENDIAN-OK: the wide test is an equality against a byte-uniform constant, which has no byte
   order.  `g_plotOwn` is `aligned(4)`, so the leading byte loop is the only alignment handling
   needed. */
static int own_has_gap(unsigned lo, unsigned hi)
{
#ifdef REVS_SPAN_OWN
    const unsigned char* p = g_plotOwn + lo;
    unsigned n = hi - lo;
    while (n && (((unsigned)(p - g_plotOwn)) & 3u)) { if (!*p) return 1; p++; n--; }
    /* ⭐ ONE `and.l` PER FOUR LINES AND ONE COMPARE PER RANGE, no early exit: a gap frame is rare
       (one of ~1200 on Silverstone), so the exit only ever paid a `cmpi.l #imm` per group.  Exact
       because every writer stores 0 or 1 (the reset, the delta template, `= 1` in RevsPlot.cpp
       and terrain_m68k.s's `move.b #1`), so the AND is 0x01010101 iff every byte is 1. */
    uint32_t all = 0x01010101u;
    while (n >= 4u) {
        all &= *(const uint32_t*)(const void*)p;
        p += 4u; n -= 4u;
    }
    if (all != 0x01010101u) return 1;
    while (n) { if (!*p) return 1; p++; n--; }
    return 0;
#else
    (void)lo; (void)hi;
    return 1;                      /* no ownership in this build: everything is the decode's */
#endif
}

/* Set by buildLineModes, read by the conversion below: this frame has at least one display line
   that is neither owned nor flat, so `mem[]` is the only thing that can supply it. */
static unsigned char s_frameHasGap = 1u;

/* ⭐⭐ THE TABLE IS MEMOISED ON THE BAND SNAPSHOT.  `m_lineMode`, `m_plan` and the painters' band
   record are a pure function of `m_bandSnap`, which is the same on nearly every frame; rebuilding
   them anyway single-stepped at 1385 instructions a call (~2 ms/frame — the 208-byte fill and the
   gap test were two thirds of it).  What is NOT a function of the snapshot is ownership, which
   every sweep resets and re-claims, so the gap test still runs every frame — over the non-flat
   ranges the last build recorded.
   ⚠ `s_modesBuilt` must be cleared by anything else that writes `m_lineMode`: the gap frame's
   owned-line mute, the DECODEFULL carve and VIEWCARVE (see revs_line_modes_stale). */
static unsigned char s_bandsChanged = 1u;   /* set by snapshotBands when the record moved */
static unsigned char s_modesBuilt;          /* m_lineMode/m_plan describe m_bandSnap */
static unsigned char s_gapRanges;
static unsigned char s_gapLo[5], s_gapHi[5];
static inline void revs_line_modes_stale(void) { s_modesBuilt = 0u; }

void RevsScreen::buildLineModes()
{
    if (s_modesBuilt && !s_bandsChanged) {
        unsigned char gap = 0u;
        for (unsigned r = 0; r < s_gapRanges; r++)
            if (own_has_gap(s_gapLo[r], s_gapHi[r])) { gap = 1u; break; }
        s_frameHasGap = gap;
        if (gap) { g_decodeGapFrames++; g_decodeGapLastAt = g_decodeFrames; }
        g_decodeFrames++;
        return;
    }
    s_modesBuilt = 0u;

    /* The record must be the game's five bands, identified by the state it wrote them
       under ($4F43): 0,1,2,3 and $FF for the last.  Anything else and the previous
       frame's plan stands — see g_bandRejects. */
    const BandSnapshot& s = m_bandSnap;
    /* ⚠ AND `s_frameHasGap` KEEPS ITS PREVIOUS VALUE ON THIS PATH, deliberately: the previous
       frame's plan stands, so the previous frame's answer to "is every line claimed?" describes
       exactly the modes that are still in the table. */
    if (s.count != 5) { g_bandRejects++; return; }
    unsigned slot[5];
    for (unsigned i = 0; i < 5; i++) slot[i] = 0xFFu;
    for (unsigned i = 0; i < 5; i++) {
        unsigned st = s.state[i];
        if (st == 0xFFu) st = 4;
        if (st > 4 || slot[st] != 0xFFu) { g_bandRejects++; return; }
        slot[st] = i;
    }

    /* ⚠ THE LATCH IS PIPELINED (bbc_screen.h): the duration recorded against band n is
       band n+1's, because the 6522 reloads T1 from the latch only at the NEXT timeout.
       So the interval that starts at band n is the one recorded against band n-1. */
    int startUs = (int)BBC_BAND0_ANCHOR_US;
    unsigned char gap = 0u, ranges = 0u;
    uint16_t flatLines = 0, flatBands = 0;
    unsigned char bandMode[5];

    for (unsigned n = 0; n < 5; n++) {
        unsigned rec  = slot[n];
        unsigned prev = slot[n ? n - 1 : 4];   /* no %: it links __umodsi3 */
        /* us -> display lines, rounded to the nearest.  A shift, not a divide: the
           68000 has no 32-bit divide, and >> on a negative is the floor, which is what
           the pre-display band 0 wants anyway. */
        int nextUs    = startUs + (int)s.duration[prev];
        int lineStart = BBC_US_TO_FIRST_LINE(startUs);
        int lineEnd   = BBC_US_TO_FIRST_LINE(nextUs);

        /* The line-mode table for the part of this band that is on screen. */
        int a = lineStart < 0 ? 0 : lineStart;
        int b = lineEnd > (int)kH ? (int)kH : lineEnd;
        unsigned mode = (s.control[rec] == BBC_ULA_MODE4) ? 4u : 5u;
        /* ⚠ THE PAINTERS GET THIS, THE PRE-FLAT-TEST MODE, DELIBERATELY.  A flat band is a
           PALETTE fact — "nothing here is observable right now" — and skipping the decode under
           it is free because the decode runs again the moment it un-flattens.  An OWNED row is
           never re-expanded, so a painter that wrote nothing under a flat band would lose that
           content permanently. */
        bandMode[n] = (unsigned char)mode;
#ifndef REVS_NO_FLATSKIP
        /* ⭐ Mode 0 = "do not decode these lines at all": all four colour registers of this
           band hold the same colour, so no bitplane content is observable under it.  The
           test is deliberately strict — all four pens, in both BBC modes — even though a
           MODE 4 band can only ever show pens 0 and 2 (decode() writes plane 1 as zero).
           Band 0 is 18 blanked lines, so the finer test would buy nothing and would make
           this depend on the mode as well as the palette.
           ⭐ AND THE TEST IS ON THE PALETTE BYTES, NOT ON FOUR bbcColour() CALLS.  bbcColour is
           `phys = (byte & 7) ^ 7` followed by a BIJECTION from those three bits onto three
           colour nibbles, so `bbcColour(a) == bbcColour(b)` if and only if
           `(a & 7) == (b & 7)` — exactly, not approximately.  Inlined four times a band it was
           twenty `btst`/`ori.w` chains a frame (probe.h §DECODESPLIT). */
        {
            const unsigned p0 = (unsigned)s.palette[rec][kLogicalForPen[0]] & 7u;
            if (p0 == ((unsigned)s.palette[rec][kLogicalForPen[1]] & 7u) &&
                p0 == ((unsigned)s.palette[rec][kLogicalForPen[2]] & 7u) &&
                p0 == ((unsigned)s.palette[rec][kLogicalForPen[3]] & 7u)) {
                mode = 0u;
                if (b > a) { flatBands++; flatLines = (uint16_t)(flatLines + (b - a)); }
            }
        }
#endif
        revs_fill_modes(m_lineMode, a, b, (unsigned char)mode);
        /* ⭐ THE GAP TEST, per band and only where it can matter: a flat band is unobservable and
           an empty range has nothing in it. */
        if (mode != 0u && b > a) {
            s_gapLo[ranges] = (unsigned char)a;
            s_gapHi[ranges] = (unsigned char)b;
            ranges++;
            if (own_has_gap((unsigned)a, (unsigned)b)) gap = 1u;
        }

        m_plan.line[n] = (short)lineStart;
        m_plan.rec[n]  = (unsigned char)rec;

        startUs = nextUs;
    }
    m_plan.valid = 1;
    REVS_PLOT_BANDS(m_plan.line, bandMode, 5);
    g_decodeFlatLines = flatLines;
    g_decodeFlatBands = flatBands;
    s_frameHasGap     = gap;
    s_gapRanges       = ranges;
    s_modesBuilt      = 1u;
    s_copperBandsStale = 1u;                      /* a new plan: the copper's bands follow it */
    if (gap) { g_decodeGapFrames++; g_decodeGapLastAt = g_decodeFrames; }
    g_decodeFrames++;
}

void RevsScreen::buildBands()
{
    if (!m_plan.valid) return;
#ifdef REVS_NEEDLE_PLANES
    static unsigned char s_builtPen[2] = { 0xFFu, 0xFFu };
    if (g_needlePen[0] != s_builtPen[0] || g_needlePen[1] != s_builtPen[1]) {
        s_builtPen[0] = g_needlePen[0];
        s_builtPen[1] = g_needlePen[1];
        s_copperBandsStale = 1u;
    }
#endif
    if (!s_copperBandsStale) return;              /* the list already holds exactly these words */
    s_copperBandsStale = 0u;
    const BandSnapshot& s = m_bandSnap;
    uint32_t* d = m_copper->data();
    unsigned emitted = 0;

    for (unsigned n = 0; n < 5; n++) {
        int lineStart   = (int)m_plan.line[n];
        unsigned rec    = m_plan.rec[n];
        /* A band starting at or before the first displayed line owns the list header (no
           WAIT); the later ones get a WAIT at the end of the previous line.  A band starting
           past the bottom of the display is dropped — its palette is never seen, and the
           cumulative record means the next band that IS seen already carries every entry it
           did not overwrite. */
        uint16_t at = (uint16_t)((lineStart <= 0) ? IDX_TOPPAL
                                                 : IDX_BANDS + emitted * BAND_WORDS);
        if (lineStart > 0) {
            if (lineStart >= (int)kH || emitted >= MAX_BANDS) continue;
            d[at++] = copperWait(kDisplayTop + lineStart - 1, 0xE0);
            emitted++;
        }
        for (unsigned pen = 0; pen < 4; pen++)
            d[at + pen] = copperMove(color00 + (pen << 1),
                                     bbcColour(s.palette[rec][kLogicalForPen[pen]]));
#ifdef REVS_DUAL_PLAYFIELD
        /* ⭐⭐ PF2's THREE OPAQUE PENS, AND THE PERMUTATION IS IN THE REGISTER NUMBERS.  The
           cockpit layer stores BBC pen 0 as PF2 pen 3 and leaves BBC pen 3 transparent, so:
             COLOR09 (PF2 pen 1) = the band's colour for BBC pen 1
             COLOR10 (PF2 pen 2) = the band's colour for BBC pen 2
             COLOR11 (PF2 pen 3) = the band's colour for BBC pen 0
           Written from the SAME band record as COLOR00..03 above, which is what keeps an
           opaque cockpit pixel the exact colour of the PF1 pixel it hides — the property this
           whole step has to preserve (Makefile §DUALPF).  COLOR08 is PF2's transparency and is
           never displayed, so it is never written. */
        d[at + 4] = copperMove(color00 + (9u << 1),
                               bbcColour(s.palette[rec][kLogicalForPen[1]]));
        d[at + 5] = copperMove(color00 + (10u << 1),
                               bbcColour(s.palette[rec][kLogicalForPen[2]]));
        d[at + 6] = copperMove(color00 + (11u << 1),
                               bbcColour(s.palette[rec][kLogicalForPen[0]]));
#endif
#ifdef REVS_NEEDLE_PLANES
        /* ⭐ THE NEEDLE SPRITES' PENS, from the same band record as the playfields — so a needle
           pixel is exactly the colour the BBC's own plotted pixel would have been on this line.
           $FF (no image built yet) leaves the pen black; nothing shows until an image does. */
        {
            unsigned k;
            for (k = 0; k < 2u; k++) {
                const unsigned pen = g_needlePen[k];
                d[at + PAL_BASE_WORDS + k] = copperMove(color00 + ((k ? 25u : 21u) << 1),
                    pen < 4u ? bbcColour(s.palette[rec][kLogicalForPen[pen]]) : 0x000);
            }
        }
#endif
#ifdef REVS_TYRE_SPRITES
        /* ⭐ The tyre patch lives at display lines 130..140, so whichever band covers line 130 is
           the one whose pens the sprites must use.  Taken from the same record as the playfield's
           own COLOR01..03 above, which is what keeps the sprite and the outline underneath it in
           the same palette. */
        if (lineStart <= (int)REVS_TYRE_Y0 &&
            (n == 4u || (int)m_plan.line[n + 1u] > (int)REVS_TYRE_Y0)) {
            /* ⚠⚠ COLOR16 IS $1A0, NOT COLOR17 — COLOR00 is $180 and the registers are two bytes
               apart, so COLOR16 = $180 + 16*2 = $1A0.  Writing the three pens from $1A0 put
               them one register low (sprite colour 2 then read COLOR18, which held PEN 3's
               colour) and the tyre pattern came out GREEN instead of white: measured on the
               target, and the copper dump is the evidence (`$1a0 <- 0f00  $1a2 <- 0fff  $1a4 <-
               00f0` against a band whose pens are black/red/white/green).  COLOR16 itself is
               never displayed for a sprite — index 0 is transparent — so the first sprite pen
               is COLOR17 = $1A2. */
            unsigned pen;
            for (pen = 1; pen < 4u; pen++)
                d[IDX_SPRPAL + pen - 1u] =
                    copperMove(0x1A2 + ((pen - 1u) << 1),      /* COLOR17, 18, 19 */
                               bbcColour(s.palette[rec][kLogicalForPen[pen]]));
        }
#endif
    }

    /* Any band slot the game did not use this frame goes back to a copper NOP, so a
       shrinking band count cannot leave last frame's WAIT and colours behind. */
    for (unsigned i = emitted; i < MAX_BANDS; i++)
        for (unsigned w = 0; w < BAND_WORDS; w++)
            d[IDX_BANDS + i * BAND_WORDS + w] = copperMove(0x1FE, 0);
}

void RevsScreen::present()
{
    if (!m_ready) return;
    /* ⚠ THE ONLY PLACE BITPLANE POINTERS ARE WRITTEN, and it is inside the VBI.  A torn
       pointer garbages the whole viewport for a frame (docs/amiga-lessons.md). */
#ifdef REVS_DUAL_PLAYFIELD
    /* PF1 ONLY.  The cockpit is single buffered, so BPL2PT/BPL4PT were written once in
       initialize() and must not be touched again. */
    m_copper->showBitmap(IDX_BPL, *m_bitmap[m_back], 1, 2, 0, 0, kBP);
#else
    m_copper->showBitmap(IDX_BPL, *m_bitmap[m_back], 1, 1, 0, 0, kBP);
#endif
    g_screenFrontAddr = (uint32_t)m_bitmap[m_back]->data;
    m_back  ^= 1u;
    m_ready  = false;

    /* ⭐⭐⭐ AND *THIS* IS WHERE THE PLOT TARGET IS SET, not at the top of decode().
     *
     * The frame order is: renderFrame() = decode (fills m_bitmap[m_back], m_ready = true) then
     * spin one field, during which THIS runs and flips m_back — and only then do the engine's
     * phases 1..23 and, at phase 24, the view sweep.  So the buffer the sweep must plot into is
     * m_bitmap[m_back] *as it is immediately after the flip*: the one the NEXT decode will fill
     * and the one after that will present.  Aimed from decode() instead, the sweep plotted into
     * the buffer decode had just filled — which this flip then put on screen, so every span was
     * torn into the live display and then discarded by the next decode's buffer swap.
     *
     * ⚠ No present can intervene between the sweep and that next decode: present returns at once
     * unless `m_ready`, and only decode() sets it.  Exactly one present runs per painted frame,
     * here, before any phase the sweep belongs to.
     * ⚠ MODE 7 has no race buffer, so the front end clears the target rather than aiming at a
     * teletext page — and it never reaches here: vbiUpdate(), the only caller, returns first
     * while tt_active(), so every MODE 7 test in this function would be a constant false. */
    REVS_PLOT_TARGET(!m_bitmap[m_back] ? (uint8_t*)0 : (uint8_t*)m_bitmap[m_back]->data);

#ifdef REVS_TYRE_SPRITES
    /* ⭐⭐ THE WHOLE ANIMATION: TWO POINTER WRITES.  No CPU touches a tyre pixel after the build —
       alternating the dither is repointing each channel at the other precomputed image, which is
       what the EOR over 44 bytes of `mem[]` at 50 Hz used to do.  ⚠ In the VBI and FIRST, beside
       the bitplane pointers: Agnus fetches a channel's control words in the sprite DMA slots near
       the START of a line, so SPRxPT has the tightest deadline in the list (docs/amiga-lessons.md).
       ⚠ MODE 7 has no tyres — leave the null sprites in place there. */
    if (m_tyreReady) {
        const unsigned ph = g_tyrePhase & 1u;
        m_copper->showSprite(IDX_SPRITES + 0, 0, *m_tyre[0][ph]);
        m_copper->showSprite(IDX_SPRITES + 2, 1, *m_tyre[1][ph]);
    }
#endif
#ifdef REVS_NEEDLE_PLANES
    /* ⭐⭐ THE TWO NEEDLES: FOUR POINTER WRITES.  The images were chosen at this frame's decode
       tail (revs_needle_paint), so they belong to the buffer just flipped on — the BBC's needle
       and its terrain were one frame buffer, and here they are one flip.  A channel with no
       image shows the empty sprite.  ⚠ MODE 7 has no needles (revs_plot_target(0) clears them). */
    {
        /* ⭐ ONLY THE DATA WORDS.  The MOVE's register words (SPRxPTH/L) were written once by
           initialize()'s showSprite and never change, so a present writes half the words it used
           to — and reads the image table directly rather than through a cross-TU accessor, which
           cost ~100 instructions a present in the VERTB ISR (a permanent wall-clock tax). */
        uint16_t* const w = (uint16_t*)(m_copper->data() + IDX_SPRITES + REVS_NEEDLE_CHANNEL0 * 2u);
        const uint32_t nul = (uint32_t)m_nullSprite->data();
        unsigned k;
        for (k = 0; k < REVS_NEEDLE_CHANNELS; k++) {
            const unsigned short* const img = g_needleShow[k];
            const uint32_t a = img ? (uint32_t)img : nul;
            w[4u * k + 1u] = (uint16_t)(a >> 16);
            w[4u * k + 3u] = (uint16_t)a;
        }
    }
#endif
}

/* ⭐⭐ SNAPSHOT THE BAND RECORD WITH THE FRAME IT DESCRIBES.  Main-loop context, called from
   decode() — and the pairing is the whole point.
 *
 * THE BUG THIS FIXES (measured, 2026-08-14).  The band record is rewritten by the game's own
 * IRQ1V band cycle in EVERY VERTB, 50 times a second.  The frame buffer is decoded once per
 * GAME frame, which is ~1.1 s at the current baseline.  buildBands() used to run in every
 * vbiUpdate(), so the copper's palette schedule was rebuilt ~50-100 times from ever-newer
 * records while the pixels on screen stayed from one much older frame.
 *
 * That is not a cosmetic mismatch, because band 2's boundary IS the horizon: `update_horizon_band`
 * ($4F44) recomputes band 2's duration every game frame as a function of pitch, so the
 * sky/track split MOVES WITH THE HILLS (bbc_screen.h).  Pair frame N's pixels with frame
 * N+k's boundary and the rows in between get the wrong band's palette — and band 2's pen 0 is
 * BLACK where band 1's is blue, so sky rows the game left at byte 0 turn into the horizontal
 * black lines the port was showing, appearing and disappearing frame to frame exactly as
 * reported.  Confirmed on the host, same frame: with the record and the pixels from ONE frame
 * they agree exactly — band 2 spans lines 81.1-100.5 and the frame buffer holds sky ($0F, pen
 * 1) on 81-99 and ground ($FF, pen 3) from 100.
 *
 * ⚠ SINGLE SLOT, and it is safe because of m_ready: decode() writes the snapshot only while
 * m_ready is false, and vbiUpdate() reads it only when m_ready is true.  Do not "optimise"
 * the flag away.
 * ⚠ Rejects a partial cycle rather than storing it — the ISR may be mid-cycle when the main
 * loop gets here, and half a record silently produces a plausible wrong schedule (that was
 * failure 3 in the project's verify-the-instrument list).  The previous snapshot then stands,
 * which is one frame stale in a value that changes slowly. */
void RevsScreen::snapshotBands()
{
    /* ⚠ A rejected record leaves the snapshot as it was, and so leaves s_bandsChanged clear:
       the previous plan stands, exactly as a rebuild from the unchanged snapshot would leave it. */
    s_bandsChanged = 0u;
    if (g_bandCount != 5) { g_bandRejects++; return; }
    unsigned diff = m_bandSnap.count ^ 5u;
    m_bandSnap.count = 5;
    for (unsigned i = 0; i < 5; i++) {
        const unsigned char  st = g_bandState[i];
        const unsigned short du = g_bandDuration[i];
        const unsigned char  co = g_bandControl[i];
        diff |= (unsigned)(m_bandSnap.state[i] ^ st) | (unsigned)(m_bandSnap.duration[i] ^ du) |
                (unsigned)(m_bandSnap.control[i] ^ co);
        m_bandSnap.state[i]    = st;
        m_bandSnap.duration[i] = du;
        m_bandSnap.control[i]  = co;
    }
    /* ⭐ THE PALETTE A LONGWORD AT A TIME: 20 volatile `move.l`s instead of 80 volatile byte
       copies (~400 instructions a frame, single-stepped).  A byte-for-byte COPY, so byte order
       is preserved and never interpreted; both arrays are aligned (bbc_hw.cpp, BandSnapshot). */
    {
        const volatile uint32_t* src = (const volatile uint32_t*)(const volatile void*)g_bandPalette;
        uint32_t* dst = (uint32_t*)(void*)m_bandSnap.palette;
        uint32_t pdiff = 0;
        for (unsigned w = 0; w < 5u * 16u / 4u; w++) {
            const uint32_t v = src[w];
            pdiff |= dst[w] ^ v;
            dst[w] = v;
        }
        s_bandsChanged = (unsigned char)((diff | pdiff) != 0u);
    }
}

void RevsScreen::noteVbiEntry()
{
    const uint16_t line = beamLine();
    g_beamEntryLine = line;
    g_beamEntries++;
    if (line >= kDisplayTop && line < kDisplayTop + kH) g_beamEntriesLate++;
}

void RevsScreen::vbiUpdate()
{
#ifdef REVS_SCREEN_NO_BANDS
    return;
#endif
    /* ⚠ m_built, NOT m_copper: a non-null pointer only means the allocation returned, and
       initialize() fills both lists in long after that.  See the header. */
    if (!m_built || !m_copper) return;

    /* ⭐ THE MODE SWITCH GOES FIRST, before anything reads m_ready or touches a list.  A switch
       returns immediately: the band schedule below belongs to the race list, and rebuilding it
       into a list the copper is no longer running would be invisible until the next race. */
    if (applyMode()) return;
    /* MODE 7 is single-buffered and has no palette bands — the page is blitted in main-loop
       context and there is nothing for the VBI to present. */
    if (tt_active()) return;

    /* ⚠ ONLY when a finished frame is going up.  Rebuilding the bands on a VBI that presents
       nothing would re-introduce exactly the mismatch above, and it would also spend ISR time
       rewriting a schedule for pixels that are not changing. */
    if (!m_ready) return;

    /* ⭐ MEASURE THE BEAM BEFORE TOUCHING THE LIST, not after: what matters is whether the
       copper had already read these words this field, and that is decided by where the beam is
       when the write starts.  See beamLine() above for why this is here at all. */
    const uint16_t line = beamLine();
    g_beamPresentLine = line;
    if (line < g_beamPresentMin) g_beamPresentMin = line;
    if (line > g_beamPresentMax) g_beamPresentMax = line;
    g_beamPresents++;
    /* kDisplayTop..kDisplayTop+kH is the display window; a swap inside it raced the beam. */
    if (line >= kDisplayTop && line < kDisplayTop + kH) g_beamPresentsLate++;

    buildBands();
    present();
}

/* ---------------------------------------------------------------------------
   THE DECODE'S TWO INNER LOOPS.

   ⭐⭐ EVERY PREDICATE IN HERE IS LOOP-INVARIANT, AND THAT IS THE WHOLE POINT.  Written as one
   loop with the tests inside, GCC runs out of address registers and spills `shadowRow` and
   `rowDirty` to the stack, so each of the 760 cells a race frame scans paid `tst.l 48(sp)` +
   `tst.l 52(sp)` — two longword RAM reads on top of the four the comparison actually needs, in a
   loop whose body was ~30 bytes of instruction fetch.  With the decode running under two
   bitplanes' DMA that measured ~212 cycles a clean cell against ~140 nominal, i.e. memory
   traffic IS the cost.  So the row driver classifies the row ONCE and calls a specialisation.

   `uniform` is 5, 4, or 0 = "consult mode[] per line", and it is a COMPILE-TIME CONSTANT at
   every call site: these MUST be always_inline or the mode becomes a memory operand in the
   inner loop (docs/perf-method.md §twins #25-#39).  ⚠ 0 cannot mean "uniformly flat" here —
   a row with all eight lines flat has `any == 0` and never reaches these.
   --------------------------------------------------------------------------- */
#define REVS_DECODE_INLINE static inline __attribute__((always_inline))

/* ONE CELL: eight source bytes -> eight interleaved 2-plane rows. */
REVS_DECODE_INLINE void revs_expand_cell(const uint8_t* s, uint8_t* p,
                                         const unsigned char* mode, int uniform)
{
    /* ⭐ EIGHT ITERATIONS, KNOWN AT COMPILE TIME — unrolled, because the rolled form spent
       `lea 80(a3),a3` + `cmpa.l` + `bne` = 24 of its 76 cycles a line on loop control alone,
       and at ~110 cells a frame that is ~5 ms.  GCC will not unroll it unasked at -O3. */
#pragma GCC unroll 8
    for (unsigned l = 0; l < BBC_SCREEN_LINES; l++, p += kRowBytes) {
        const unsigned m = uniform ? (unsigned)uniform : (unsigned)mode[l];
        if (!uniform && m == 0) continue;       /* flat band: write nothing */
        const uint8_t b = s[l];
        if (m == 5) {
            p[0]      = s_expandLo[b];
            p[kW / 8] = s_expandHi[b];          /* plane 2 = index bit 1 */
        } else {
            /* MODE 4: eight 1-bit pixels, straight into plane 2 so the set pixels land on
               pen 2 = BBC logical colour 8, the ULA's index bit 3. */
            p[0]      = 0;
            p[kW / 8] = b;
        }
    }
}

/* ⭐ THE SHIPPING PATH: a shadow exists and the row's MODE pattern is unchanged, so a cell is
   converted only if one of its eight bytes moved.  No `shadowRow` test and no `rowDirty` test:
   both were loop-invariant values GCC had spilled. */
REVS_DECODE_INLINE unsigned revs_scan_row(const uint8_t* s, uint8_t* p, uint32_t* sh,
                                          const unsigned char* mode, int uniform)
{
    /* ⭐⭐ TWO PASSES, AND THE SPLIT IS THE OPTIMISATION.  Fused, the clean-cell path was 128
       cycles of which only 60 were the four longword loads the comparison actually needs — the
       rest was two branches and a five-pointer loop tail.  Unrolling fixes that, but unrolling a
       loop with the expansion inlined into it multiplies the expansion's code instead.  Separated,
       pass 1 is small enough to unroll for free and pass 2 runs ~2-6 times a row.
       ⚠ Indexing `src[0]/src[1]` off a per-cell base also made GCC invent a second induction
       variable per stream; post-increment walks keep it to two live pointers.  There is no
       early-out lost by loading both halves up front — a CLEAN cell (the ~88% case) compares
       both longwords anyway. */
    uint8_t changed[BBC_SCREEN_CELLS];
    const uint32_t* src = (const uint32_t*)(const void*)s;
    unsigned n = 0;

#pragma GCC unroll 4
    for (unsigned c = 0; c < BBC_SCREEN_CELLS; c++) {
        const uint32_t s0 = *src++;
        const uint32_t s1 = *src++;
        const uint32_t h0 = sh[0];
        const uint32_t h1 = sh[1];
        sh += 2;
        /* ⚠ SPELT AS "dirty is the exception", not `if (clean) continue`.  The two forms are the
           same predicate, but the `continue` form made GCC put the second compare out of line and
           route the COMMON case through two taken `beq.w`s — 84 cycles a clean cell instead of the
           72 the straight-line fall-through costs. */
        if (__builtin_expect(s0 != h0 || s1 != h1, 0)) {
            sh[-2] = s0;
            sh[-1] = s1;
            changed[n++] = (uint8_t)c;
        }
    }

    for (unsigned i = 0; i < n; i++) {
        const unsigned c = changed[i];
        revs_expand_cell(s + c * BBC_SCREEN_LINES, p + c, mode, uniform);
    }
    return n;
}

/* ⭐⭐ ONE DISPLAY LINE, ALL 40 CELLS — what a moved band boundary actually costs.  The horizon
   band slides as the car drives (`update_horizon_band`), so ~1.6 character rows a frame have a
   changed mode pattern; dirtying the whole row for that re-expanded ~64 cells a frame against the
   ~28 that had a byte move, i.e. 70% of the expansion was band movement.  Only the LINES the
   boundary crossed change conversion, and this rewrites exactly those.
   ⚠ `m == 0` writes nothing, which is not a shortcut: a line that went flat keeps whatever pixels
   it had and band 1's sixteen identical palette entries hide them — that IS the display model
   (bbc_screen.h, "the code hiding in the sky"), and the whole-row path did the same. */
REVS_DECODE_INLINE void revs_expand_line(const uint8_t* s, uint8_t* p, unsigned m)
{
    if (m == 0) return;
    for (unsigned c = 0; c < BBC_SCREEN_CELLS; c++, p++, s += BBC_SCREEN_LINES) {
        const uint8_t b = *s;
        if (m == 5) {
            p[0]      = s_expandLo[b];
            p[kW / 8] = s_expandHi[b];
        } else {
            p[0]      = 0;
            p[kW / 8] = b;
        }
    }
}

/* THE FULL PATH: no shadow at all (the oracle and `make DIRTY=0`), or a row whose band schedule
   moved under it.  Rare and not on the critical path, so it keeps the runtime `sh` test. */
REVS_DECODE_INLINE unsigned revs_full_row(const uint8_t* s, uint8_t* p, uint32_t* sh,
                                          const unsigned char* mode, int uniform)
{
    for (unsigned c = 0; c < BBC_SCREEN_CELLS;
         c++, s += BBC_SCREEN_LINES, p++, sh += sh ? BBC_SCREEN_LINES / 4 : 0) {
        if (sh) {
            const uint32_t* const src = (const uint32_t*)(const void*)s;
            sh[0] = src[0];
            sh[1] = src[1];
        }
        revs_expand_cell(s, p, mode, uniform);
    }
    return BBC_SCREEN_CELLS;
}

#ifdef REVS_DUAL_PLAYFIELD
/* ═══ THE COCKPIT LAYER ══════════════════════════════════════════════════════════════════════
   Display lines 117..157 — the car body, the top of the dashboard and the wing mirrors' upper
   half — expanded out of the SAME mem[] bytes the terrain decode reads, into PF2.

   ⭐ WHAT DEFINES "THE CAR": the complement of the game's own two terrain runs.  view_paint_lines
   paints every line of the viewport as a LEFT run and a RIGHT run of cells and leaves the gap
   between them alone, and what splits them is the dashboard silhouette, not the road — so the
   run tables are STATIC DATA in the binary and the cells outside the runs are, by construction,
   exactly the furniture the rasteriser refuses to touch (disasm/symbols.csv, view_run_*).
   Phase 2's lines (117..132) tabulate only the left run's END and mirror it; phase 3's (133..157)
   tabulate three of the four bounds and derive the fourth.  Both are decoded below.

   ⭐ THE RUNS' OWN FIRST AND LAST CELLS STAY ON PF1.  They are COMPOSITES — the rasteriser masks
   the view's pixels and ORs the dashboard's into one byte — so PF1 already holds the finished
   mixture and PF2 has nothing to add.  That is why a CELL-granularity mask is enough here; it
   stops being enough when the terrain stops being clipped, which is when the boundary cell's
   sub-byte phase (view_edge_phase + the four mask/fill tables) has to move to PF2 with it.

   ⭐⭐ AND THE PIXEL REMAP IS TWO COMPLEMENTS.  A MODE 5 byte holds each pixel's low bit in the
   low nibble and its high bit in the high nibble, so s_expandLo[b] IS plane 1 and s_expandHi[b]
   IS plane 2.  The permutation the layer needs — BBC pen 0 -> PF2 pen 3, 1 -> 1, 2 -> 2,
   3 -> transparent — is exactly "new low bit = NOT old high bit, new high bit = NOT old low
   bit", because pen = 2*hi + lo:
        pen 0 (hi0 lo0) -> 3 (hi1 lo1)      pen 2 (hi1 lo0) -> 2 (hi1 lo0)
        pen 1 (hi0 lo1) -> 1 (hi0 lo1)      pen 3 (hi1 lo1) -> 0 (hi0 lo0)
   so the whole conversion is `lo = ~expandHi[b]; hi = ~expandLo[b]` — no third table, no branch
   and no per-pixel work.
   ═══════════════════════════════════════════════════════════════════════════════════════════ */
#define COCK_Y0    117u
#define COCK_Y1    157u
#define COCK_ROW0  (COCK_Y0 / BBC_SCREEN_LINES)                 /* 14: display lines 112..119 */
#define COCK_ROW1  (COCK_Y1 / BBC_SCREEN_LINES)                 /* 19: display lines 152..159 */
#define COCK_ROWS  (COCK_ROW1 - COCK_ROW0 + 1u)
#define COCK_LINES (COCK_Y1 - COCK_Y0 + 1u)
/* Phase 2's lines mirror a single tabulated bound; phase 3's have their own three. */
#define COCK_PHASE3_Y0  133u

/* The rasteriser's stop/start tables hold the LOW BYTE OF A CHAIN SLOT, not a cell — chain A's
   unit k is at $0F + $11*k and chain B's entry point at $05 + $11*k.  Inverted once into two
   lookups so the per-line decode costs a table read: the 68000 has no 32-bit divide and this
   must not reach __udivsi3 (make muldiv-audit). */
static uint8_t s_slotCellA[256];        /* $0F + $11*k -> k, 0xFF if not a slot */
static uint8_t s_slotCellB[256];        /* $05 + $11*k -> k, 0xFF if not a slot */
static uint8_t s_cockRun[COCK_LINES][4];    /* a0, a1, b0, b1 — the two runs, in CELLS */
/* ⚠ uint32_t, and the ⭐ is that it is COMPARED four bytes at a time.  As 48 byte compares
   against m_lineMode this cost 0.8 ms a frame — see revs_cockpit_paint for the whole 2.84 ms
   that "has anything changed?" used to cost.  m_lineMode is aligned(4) and line 112 is a
   multiple of four, so both sides are legal `move.l`s.  ENDIAN-OK: an equality compare of two
   identically-laid-out longwords, never an interpretation of lanes. */
static uint32_t s_cockMode[(COCK_ROWS * BBC_SCREEN_LINES) / 4u];
static unsigned s_cockCells = 0;        /* cells this decode expanded into PF2 */

static void revs_cockpit_slot_tables(void)
{
    unsigned i, k;
    for (i = 0; i < 256u; i++) { s_slotCellA[i] = 0xFFu; s_slotCellB[i] = 0xFFu; }
    /* Chain A carries cells 0..15 and chain B cells 26..39, i.e. fourteen entries; sixteen
       covers both with room to spare and no wrapped value collides inside that range. */
    for (k = 0; k < 16u; k++) {
        s_slotCellA[(0x0Fu + 0x11u * k) & 0xFFu] = (uint8_t)k;
        s_slotCellB[(0x05u + 0x11u * k) & 0xFFu] = (uint8_t)k;
    }
}

/* Rebuild the per-line silhouette from the game's tables.  Returns non-zero if anything moved —
   they are static data, so that is the FIRST call and nothing else, but reading them every
   decode is a handful of table lookups and it removes the question of when they became live
   (they sit in the view blocks' tails, which are other things out of a race). */
static int revs_cockpit_runs(void)
{
    int changed = 0;
    unsigned y;
    for (y = COCK_Y0; y <= COCK_Y1; y++) {
        /* X = $4F..$03 indexes the tables, top line first, and display line = 160 - X. */
        const unsigned X  = 160u - y;
        const unsigned le = s_slotCellA[mem[0x3150u + X]];
        uint8_t* const r  = s_cockRun[y - COCK_Y0];
        uint8_t a0, a1, b0, b1;
        if (le == 0xFFu) {
            g_cockpitBadSlot++;
            a0 = 0; a1 = BBC_SCREEN_CELLS - 1u; b0 = 1; b1 = 0;   /* no car: PF1 keeps the line */
        } else if (y < COCK_PHASE3_Y0) {
            /* Phase 2: the left run starts at cell 0 and the right run is its mirror about
               cell 19.5 (symbols.csv: 5+34 = 6+33 = 39). */
            a0 = 0; a1 = (uint8_t)le;
            b0 = (uint8_t)(BBC_SCREEN_CELLS - 1u - le); b1 = BBC_SCREEN_CELLS - 1u;
        } else {
            const unsigned re = s_slotCellA[mem[0x3080u + X]];
            const unsigned rs = s_slotCellB[mem[0x30D0u + X]];
            if (re == 0xFFu || rs == 0xFFu) {
                g_cockpitBadSlot++;
                a0 = 0; a1 = BBC_SCREEN_CELLS - 1u; b0 = 1; b1 = 0;
            } else {
                a1 = (uint8_t)le;
                b1 = (uint8_t)(26u + re);
                b0 = (uint8_t)(26u + rs);
                a0 = (uint8_t)(BBC_SCREEN_CELLS - 1u - b1);       /* the mirror, not a table */
            }
        }
        if (r[0] != a0 || r[1] != a1 || r[2] != b0 || r[3] != b1) {
            r[0] = a0; r[1] = a1; r[2] = b0; r[3] = b1;
            changed = 1;
        }
    }
    return changed;
}

/* ⭐⭐ WHICH CELLS OF THE LAYER CAN MOVE AT ALL — and the answer is the two EDGE STRIPS.
   Everything between them is the car's body and the top of the dashboard: static art, drawn
   once when the race view is built and never touched again.  What moves inside 117..157 is
   the two WING MIRRORS (their own six-segment tables put them in cells 0..2 and 37..39) and the
   front-wheel DITHER that `tick_wheel_spin` EORs at 50 Hz (cells 0..1 and 38..39, display lines
   133..140, and a sprite once REVS_TYRE_SPRITES is on).  Both live in the same four cells at
   each edge, so the per-frame refresh is eight cells of a line and nothing else.

   ⭐⭐⭐ AND THAT IS THE WHOLE POINT OF THE EXERCISE, NOT A SHORTCUT (user, §12: "the mirrors
   have limited content in them and the gear indicator changes but those are the only, very
   limited, changes that this part of the screen ever should have").  THREE dirty-scan designs
   were built and measured against it first, and every one of them lost to the SCAN, exactly as
   §12c said they would:
       a separate six-row scan                      +11.23 ms of phase 27
       fused into convertRace's scan, one pass      +14.82
       fused, two-pass, car cells only              + 9.81
   convertRace walks 26 rows for 16.7 ms — 0.64 ms a row — so ANY second opinion about which
   cells changed costs more than re-expanding the handful that can.  ⇒ don't detect; know.

   ⚠ THE COMPLETENESS OF THAT CLAIM IS WHAT `make DUALPFCHECK=1` CHECKS, every painted frame,
   over all 41 lines x 40 cells: a car cell that moves and is not refreshed shows up as a
   mismatch on the frame it moves.  Widen the strip if it ever fires; do not assume it. */
/* ⭐⭐⭐ WHAT MOVES INSIDE 117..157, AND WHY IT IS *EXCLUDED* FROM THE LAYER RATHER THAN
   REFRESHED INTO IT.
   Three things in these rows are not static art: the rev-counter / steering mark that
   `plot_line_octant` and `undraw_plot_lines` draw (display lines 129..180 x cells 16..23,
   `make fbwrites`), the front-wheel dither `tick_wheel_spin` EORs at 50 Hz (133..140 x cells
   0..1 and 38..39), and the two WING MIRRORS (154..178 x cells 0..2 and 37..39, from the game's
   own six-segment tables — a practice session never draws one, so no census can see them).

   ⭐⭐⭐ THEY ARE LEFT TRANSPARENT, AND PF1 — WHICH STILL DECODES EVERY CELL — SHOWS THEM.  That
   is EXACTLY RIGHT today and it costs the layer NOTHING per frame, which is the only price this
   step can afford: a small-region re-expansion costs ~143 cycles A BYTE (§12c; the decode's 13.6
   is a WHOLESALE, longword-batched, dirty-skipping rate and does not transfer).  Four designs
   that tried to keep the layer live were built and measured first, and each lost:
       its own six-row dirty scan                      +11.23 ms of phase 27
       fused into convertRace's scan, one pass         +14.82
       fused, two-pass, car cells only                 + 9.81
       no scan, refreshing the two EDGE STRIPS         +20.35   (48 cells = 768 bytes)
   The first three lose to §12c's "the decode is ~99% SCAN" — convertRace walks 26 rows for
   16.7 ms, 0.64 ms a row, so ANY second opinion about which cells changed costs more than
   re-expanding the few that can.  The fourth loses to the byte rate.  ⇒ deliver ZERO bytes a
   frame: paint the static car once and let the dynamic strips come from the layer underneath.

   ⚠⚠ AND THAT IS A STAGING DECISION WITH A NAMED DEBT, not the end state.  It works because PF1
   still holds the whole picture; the moment the terrain painter stops clipping to the silhouette
   (§2a, `LOWOWN=1`) PF1 holds ROAD there instead, and each of these three must become a PF2
   painter — which is what `NEEDLE=1` (§12d, geometry), `TYRESPRITE=1` (sprites) and a mirror
   painter already are, retargeted.  The oracle below is what will say so: it compares the
   COMPOSITE, so it stays green through that change only if the strips keep arriving.

   ⚠ `make DUALPFCHECK=1` IS THE COMPLETENESS GATE, every painted frame over all 41 lines x 40
   cells.  It found this list: the first cut refreshed edge strips only and read 203 mismatches
   at display line 157 cell 17 — the steering mark, which is nowhere near an edge. */
/* ⭐ THE CELLS OF 117..157 THIS LAYER LEAVES TRANSPARENT FOR SOMEONE ELSE.  There used to be a
   needle column here (cells 12..27 of 128..157, the §12d painter's PF1 hole); the needles are
   prerendered SPRITES now (revs_plot.h §12d), in front of both playfields, so the dial art under
   them is ordinary static cockpit and this layer paints it like the rest of the car. */
static const struct { unsigned char y0, y1, c0, c1; } s_cockDyn[] = {
    { 1u, 0u, 0u, 0u },   /* never matches (y0 > y1): keeps the table non-empty in every build */
    /* ⭐⭐ THE FRONT-WHEEL DITHER IS NOT DYNAMIC ANY MORE — `TYRESPRITE=1` makes the wheels a
       SPRITE, so `tick_wheel_spin` no longer EORs `mem[]` (revs_native.c §tick_wheel_spin) and
       the art underneath is static like the rest of the car.  ⇒ the layer PAINTS it, and two of
       the five holes in §2a's entry fee close for nothing.  ⚠ Without the sprite arm they must
       stay transparent, which is also why `LOWOWN=1` #errors unless `TYRESPRITE=1`. */
#ifndef REVS_TYRE_SPRITES
    { 133, 140,  0,  1 }, { 133, 140, 38, 39 },
#endif
    /* ⭐⭐ THE WING MIRRORS ARE NOT A HOLE EITHER: a reflection is cockpit, so it is painted ON
       THIS LAYER — `mirror_draw_car` writes one opaque PF2 byte pair per store through
       `REVS_COCKPIT_BYTE` (§12f-iv), and the static art around it comes from the rebuild like the
       rest of the car.  ⇒ every cell of 117..157 now has exactly one owner, and with the
       needles on sprites no transparent window is left at all. */
};
#define COCK_DYNS (sizeof s_cockDyn / sizeof s_cockDyn[0])

/* ⭐ ONE DISPLAY LINE, ALL 40 CELLS.  LINE-major: the silhouette bounds and the mode are per
   LINE, so a cell-major walk would reload four run bytes and a mode byte for each of a cell's
   eight lines — and under display DMA a chip-RAM access is not cheap (the decode measures ~212
   cycles for a two-longword compare). */
static void revs_cock_line(const uint8_t* base, uint8_t* cock, unsigned char mode, unsigned y)
{
    const uint8_t* const src = base + revs_mulu16((uint16_t)(y >> 3), BBC_SCREEN_BPR) + (y & 7u);
    uint8_t* const q = cock + revs_mulu16((uint16_t)y, kRowBytes);
    const uint8_t* const r = s_cockRun[y - COCK_Y0];
    const unsigned a0 = r[0], a1 = r[1], b0 = r[2], b1 = r[3];
    /* ⭐⭐⭐ AN OWNED LINE IS STILL A RACE-VIEW LINE.  Ownership expresses itself as
       `m_lineMode = 0` — the decode's "write nothing" — so a bare `mode == 5` test reads every
       row §2a claims as "not the race view" and leaves the WHOLE layer transparent: PF2 came
       back all-zero over 117..157 and the cockpit vanished into the road behind it.  The band
       schedule and the ownership map are two different questions and this one needs both. */
#ifdef REVS_SPAN_OWN
    const int live = (mode == 5u) || g_plotOwn[y] != 0u;
#else
    const int live = (mode == 5u);
#endif
    unsigned c, i;
    for (c = 0; c < BBC_SCREEN_CELLS; c++) {
        uint8_t lo = 0, hi = 0;                        /* transparent: PF1 shows through */
        if (live && !((c >= a0 && c <= a1) || (c >= b0 && c <= b1))) {
            int dyn = 0;
            for (i = 0; i < COCK_DYNS; i++)
                if (y >= s_cockDyn[i].y0 && y <= s_cockDyn[i].y1 &&
                    c >= s_cockDyn[i].c0 && c <= s_cockDyn[i].c1) { dyn = 1; break; }
            if (!dyn) {
                const uint8_t b = src[c * 8u];
                lo = (uint8_t)~s_expandHi[b];
                hi = (uint8_t)~s_expandLo[b];
                if (s_expandLo[b] & s_expandHi[b]) g_cockpitPen3++;      /* the tripwire */
            }
        }
        q[c]             = lo;
        q[c + kPlaneGap] = hi;
        s_cockCells++;
    }

    /* ⭐⭐⭐ THE BOUNDARY CELLS — the last thing the terrain painter had to know about the car
       (§12f-ii, and §12's directive: "if the original has complex logic to render only to the
       edges of the car outline, all that can go").  Each run's first and last cell was composed
       as `(source & mask) | fill`: the terrain pixels the mask KEEPS, plus the dashboard's own
       pixels the fill supplies.  Those dash pixels are this layer's, so take them here — from the
       game's own static tables, once, in the same rebuild that found the silhouette — and the
       painter drops four composites, two clip lookups and eight table reads A LINE.
       ⚠ The two CLIP-GATED ones only exist on phase 3's lines: a phase-2 line reaches the screen
       edge on its outside, so its run has no entry (A) / exit (B) composite even though the
       edge-phase table still holds a value for it.  Painting them anyway would punch the
       dashboard's pixels into the grass at the screen edge.
       ⚠ A mask of $FF keeps every pixel, so `dash` is 0 and the cell stays transparent — the
       identity composite needs no special case.
       ⭐ Harmless and identical on the non-LOWOWN arm, which is why there is ONE path: PF1 still
       holds the composed byte there, and an opaque PF2 pixel carrying the same BBC pen shows the
       same colour (COLOR09/10/11 are PF1's pens 1/2/0). */
    if (live) {
        const unsigned X    = 160u - y;
        const unsigned edge = mem[MEM_view_edge_phase + X];
        const int      clip = (y >= COCK_PHASE3_Y0);
        struct { unsigned char cell, mask, fill; } b[4];
        unsigned n = 0;
        if (clip) { b[n].cell = r[0];
                    b[n].mask = mem[MEM_view_left_start_mask + edge];
                    b[n].fill = mem[MEM_view_left_start_fill + edge]; n++; }
        b[n].cell = r[1];
        b[n].mask = mem[MEM_view_left_end_mask + X];
        b[n].fill = mem[MEM_view_left_end_fill + X];  n++;
        b[n].cell = r[2];
        b[n].mask = mem[MEM_view_right_start_mask + X];
        b[n].fill = mem[MEM_view_right_start_fill + X]; n++;
        if (clip) { b[n].cell = r[3];
                    b[n].mask = mem[MEM_view_right_end_mask + edge];
                    b[n].fill = mem[MEM_view_right_end_fill + edge]; n++; }
        for (i = 0; i < n; i++) {
            const unsigned m = b[i].mask, f = b[i].fill;
            const uint8_t  keep = s_expandLo[m];              /* plane bits the terrain keeps */
            const uint8_t  dash = (uint8_t)~keep;             /* ...and the ones the dash owns */
            if (s_expandHi[m] != keep) { g_cockpitMaskBad++; continue; }
            if (b[i].cell >= BBC_SCREEN_CELLS) continue;
            q[b[i].cell]             = (uint8_t)((uint8_t)~s_expandHi[f] & dash);
            q[b[i].cell + kPlaneGap] = (uint8_t)((uint8_t)~s_expandLo[f] & dash);
        }
    }
}

/* THE LAYER, AND IN THE STEADY STATE IT DOES NOTHING AT ALL.  It repaints only when an INPUT to
   the expansion moves — the silhouette table (once, when the race view's blocks go live) or the
   band schedule's mode for one of these lines.  Neither is visible to a byte compare, which is
   why they are tested here rather than inferred. */
/* Set when revs_cockpit_paint re-expands the layer, cleared once the tyre outline has masked the
   tread back out of it.  The outline is idempotent, and nothing else writes the PF2 tread cells
   (display lines 130..140, cells 0-1 / 38-39): the mirrors' PF2 bytes land on 154..178 and the
   layer is its own allocation — so re-masking an unrepainted layer every frame was pure traffic.
   ⚠ revs_plot_cockpit_byte (RevsPlot.cpp) accepts rows 117..157, so it raises this too for a byte
   in the tread rows, even though no current writer lands there. */
extern "C" { unsigned char g_cockOutlineStale = 1u; }
#define s_cockOutlineStale g_cockOutlineStale

static void revs_cockpit_paint(const uint8_t* base, uint8_t* cock, const unsigned char* lineMode)
{
    static unsigned char force = 1;
    static unsigned char poll  = 0;
    const uint32_t* const lw = (const uint32_t*)(const void*)&lineMode[COCK_ROW0 * BBC_SCREEN_LINES];
    unsigned i, y;

    /* ⚠⚠ ASKING "DID ANYTHING CHANGE?" IS NOT FREE, AND IT WAS 2.84 ms/FRAME — with the layer
       delivering ZERO bytes.  41 lines x three mem[] table reads plus 48 byte compares, at what
       a chip-RAM access costs under display DMA.  Both halves are fixed here and the lesson is
       the general one this file keeps meeting: on this machine an ACCESS COUNT is the cost, and
       that applies to a guard exactly as it applies to the work it guards.
       ⭐ The silhouette tables are STATIC DATA in the game binary (symbols.csv, view_run_*), so
       they are re-read once every 64 painted frames rather than every frame — often enough to
       notice the race view's blocks going live, ~1/64 of the price.  `force` keeps the poll
       every frame until the first successful build. */
    if (force || (poll = (unsigned char)((poll + 1u) & 63u)) == 0u) {
        if (revs_cockpit_runs()) { force = 1; g_cockpitRuns++; }
    }
    for (i = 0; i < (COCK_ROWS * BBC_SCREEN_LINES) / 4u; i++)
        if (s_cockMode[i] != lw[i]) { s_cockMode[i] = lw[i]; force = 1; }

    s_cockCells = 0;
    if (!force) return;
    force = 0;
    s_cockOutlineStale = 1u;
    g_cockpitFulls++;
    for (y = COCK_Y0; y <= COCK_Y1; y++) revs_cock_line(base, cock, lineMode[y], y);
}

#endif  /* REVS_DUAL_PLAYFIELD */

/* ---------------------------------------------------------------------------
   THE CONVERSION ITSELF: BBC frame buffer -> one interleaved 2-plane buffer.

   ⭐ CELL-MAJOR WITHIN A CHARACTER ROW, which is what makes the dirty test cheap.  The BBC
   layout is row*320 + cell*8 + line, so a cell's EIGHT SCAN LINES are eight CONTIGUOUS bytes
   — two longwords — while a display line is 40 bytes with a stride of 8.  Comparing per cell
   therefore costs two aligned longword loads a side; comparing per line could only ever be 40
   strided byte loads, which is the same read count as converting.  (mem[] is aligned(4) in
   cpu.c and $5A80, 320 and 8 are all multiples of 4, so every cell address is aligned.)

   `shadow`/`shadowMode` null ⇒ convert everything: that is the reference pass used by
   REVS_DIRTYCHECK and by `make DIRTY=0`.

   ⚠ NOT A WIDE-POINTER ALIAS OF mem[] IN THE SENSE make endian-lint IS ABOUT.  The longwords
   here are never interpreted as a VALUE — they are compared against, and copied to, a shadow
   in the identical byte layout, so any byte order gives the same answer.  Every byte that
   becomes a pixel is read as a byte, and both stores are bytes.  ENDIAN-OK: comparison only.
   --------------------------------------------------------------------------- */
unsigned RevsScreen::convertRace(uint8_t* dst, uint8_t* shadow, unsigned char* shadowMode)
{
#ifdef REVS_TERRAIN_SPANS
    revs_plot_terrain_stale();   /* it can write the terrain painter's rows */
#endif
    const uint8_t* base = (const uint8_t*)mem + BBC_SCREEN_BASE;
    unsigned converted = 0;
    unsigned y = 0;

    /* ⭐⭐ RUNNING POINTERS, NOT `row *` — two `mulu.w` a row is 140 cycles for a walk whose
       stride is a constant.  `y` advances with them, so the three row-relative bases and the
       mode index stay in step by construction. */
    const uint8_t* rowBase  = base;
    uint8_t*       rowDst   = dst;
    uint8_t*       shadowRow = shadow;

    for (unsigned row = 0; row < BBC_SCREEN_ROWS; row++, y += BBC_SCREEN_LINES,
             rowBase += BBC_SCREEN_BPR,
             rowDst  += BBC_SCREEN_LINES * kRowBytes,
             shadowRow = shadowRow ? shadowRow + BBC_SCREEN_BPR : (uint8_t*)0) {
        const unsigned char* const mode = &m_lineMode[y];

        /* ⭐⭐ A CHARACTER ROW'S EIGHT MODES ARE TWO LONGWORDS, AND ALL THREE TESTS BELOW READ
           THEM THAT WAY.  Per byte they were three 8-iteration loops — the OR, the uniformity
           compare and the shadow compare — measured together at ~3 ms a frame on 26 rows
           (probe.h §DECODESPLIT).  `m_lineMode` and `s_shadowMode` are `aligned(4)` and a row
           begins at line row*8, so both halves are legal `move.l`s.
           ENDIAN-OK, three times over and each for its own reason: `m0 | m1` is compared
           against ZERO, the uniformity test compares against a longword whose four bytes are
           the SAME, and the shadow test is an equality compare of two identically-laid-out
           longwords.  None of the three can observe byte order. */
        const uint32_t* const mw = (const uint32_t*)(const void*)mode;
        const uint32_t m0 = mw[0], m1 = mw[1];

        /* ⚠ A MODE CHANGE MUST BE REDECODED EVEN THOUGH NO BYTE MOVED.  m_lineMode comes from
           this frame's band snapshot, so a moved band boundary re-points a line at a different
           conversion (MODE 5 / MODE 4 / skipped) while its source byte is untouched.
           ⭐⭐ BUT ONLY THE LINES IT CROSSED, which is the point of the bitmask: dirtying the row
           charged 40 cells x 8 lines for a boundary that moved one line.  All eight bits set is
           the whole row after all — including a row entering or leaving the flat band — and that
           falls through to the full path below.
           ⭐ The bitmask is built per byte, but only on the frames a boundary actually moved:
           four rows of 26 can straddle one, and a boundary moves on a fraction of frames, so
           the two longword compares answer "nothing moved" for the whole row nearly always. */
        unsigned modeChanged = 0;
        if (shadowMode) {
            uint32_t* const sw = (uint32_t*)(void*)&shadowMode[y];
            if (sw[0] != m0 || sw[1] != m1) {
                for (unsigned l = 0; l < BBC_SCREEN_LINES; l++) {
                    if (shadowMode[y + l] != mode[l]) {
                        modeChanged |= 1u << l;
                        shadowMode[y + l] = mode[l];
                    }
                }
                g_decodeModeDirty++;
            }
        }

        /* Is any line of this row displayed at all?  A character row wholly inside the flat
           blue band is 40 cells nobody can see — and its bytes are engine variables. */
        if (!(m0 | m1)) continue;   /* nothing to draw; the shadow BYTES deliberately stay stale */

#ifdef REVS_PLOT_ONLY
        /* ⭐ THE PLOTTER OWNS THESE LINES.  Under REVS_PLOT_ONLY the view rasteriser no longer
           writes mem[] for its own region, so converting it would paint stale bytes over what the
           plotter drew.  The range is what the last sweep actually PAINTED, not a literal — the
           viewport's extent is data (docs/direct-bitplane-plan.md §7e).  Whole character rows only,
           which is why it is tested here rather than per line; the sweep's region is 77 lines, so
           the rounding costs at most one row at each end.
           ⚠ MEASUREMENT BUILD: vdu_char_def's digits compose against mem[] and are lost with it.
           This exists to PRICE the end state, not to be it. */
        if ((unsigned)g_plotLineHi >= (unsigned)g_plotLineLo &&
            y >= (unsigned)g_plotLineLo && y + BBC_SCREEN_LINES <= (unsigned)g_plotLineHi)
            continue;
#endif

        /* ⭐⭐ CLASSIFY THE ROW ONCE — mode[] is the same for all 40 of its cells.  Five raster
           bands over 26 character rows means at most four rows can straddle a boundary, so in a
           race frame ~17 of the 19 scanned rows take a path with no per-line mode test at all
           (band 0 is MODE 4, bands 2-4 MODE 5, band 1 is the flat sky that `any` already
           skipped).  See revs_scan_row above for why this is worth a switch. */
        /* The wide test is EXACT, not an approximation of the old per-byte loop: `m0 == m1` and
           `m0 == v * 0x01010101` together say every one of the eight bytes equals v, which is
           what "uniform" means.  Byte order is irrelevant — both operands are longwords read
           from the same array with the same layout, so this is an equality compare, never an
           interpretation of lanes.
           ⚠⚠ AND DIRTYCHECK CANNOT GATE THIS BLOCK — dropping the `m0 == m1` guard survives
           31/31 oracle checks with mismatch=0.  The oracle's reference pass is
           convertRace(scratch, 0, 0) over the SAME m_lineMode, so it classifies the row the same
           wrong way and both sides are wrong identically.  That is structural to an in-process
           differential and not a fixture to widen: DIRTYCHECK gates what the DIRTY PASS SKIPS
           (sabotaging the shadow-mode compare below fires it at mismatch=4050), and the gate on
           the classification is the PICTURE — amiga/screen_dump.gdb, or `make DIRTY=0`, which
           takes the mixed path for every row. */
        int uniform = 0;                                 /* 0 = mixed, consult mode[] per line */
        if (m0 == m1) {
            if (m0 == 0x05050505u)      uniform = 5;
            else if (m0 == 0x04040404u) uniform = 4;
        }

        uint32_t* const sh = shadowRow ? (uint32_t*)(void*)shadowRow : (uint32_t*)0;
        const int full = (shadowRow == 0) || modeChanged == 0xFFu;

        switch (uniform) {
        case 5:  converted += full ? revs_full_row(rowBase, rowDst, sh, mode, 5)
                                   : revs_scan_row(rowBase, rowDst, sh, mode, 5);  break;
        case 4:  converted += full ? revs_full_row(rowBase, rowDst, sh, mode, 4)
                                   : revs_scan_row(rowBase, rowDst, sh, mode, 4);  break;
        default: converted += full ? revs_full_row(rowBase, rowDst, sh, mode, 0)
                                   : revs_scan_row(rowBase, rowDst, sh, mode, 0);  break;
        }

        /* The lines a moved boundary re-pointed, for every cell the scan skipped.  Redundant for
           a cell the scan already expanded — the same bytes, written twice — and that is cheaper
           than tracking which. */
        if (!full && modeChanged) {
            for (unsigned l = 0; l < BBC_SCREEN_LINES; l++) {
                if (!(modeChanged & (1u << l))) continue;
                revs_expand_line(rowBase + l, rowDst + revs_mulu16((uint16_t)l, kRowBytes),
                                 mode[l]);
                g_decodeModeLines++;
            }
        }
    }
    return converted;
}

/* ⭐ THE ORACLE HANDLE (docs/direct-bitplane-plan.md §5).  The direct-to-bitplane plotter writes
   no mem[], so `make validate` cannot check it; what CAN is the shipping decode, run over the same
   mem[] state — so it is exposed as a plain C entry point rather than reimplemented anywhere.
   ⚠ A file-static instance pointer, set by decode(): the object is a member of a function-local
   static Revs, and this build has no way to name it otherwise.  Null before the first decode, which
   is exactly when there is no picture to be a reference for. */
static RevsScreen* s_lastDecoded = 0;

extern "C" void revs_screen_convert_reference(uint8_t* dst)
{
    if (s_lastDecoded) s_lastDecoded->convertRace(dst, 0, 0);
}


/* ---------------------------------------------------------------------------
   Main loop: the BBC frame buffer -> the back buffer.
   --------------------------------------------------------------------------- */
void RevsScreen::prepareFrame()
{
#ifdef REVS_SCREEN_NO_DECODE
    m_ready = true;
    return;
#endif
    /* ⭐ MODE 7 IS A DIFFERENT PASS ENTIRELY, and it must come first: the race decode below
       reads the BBC frame buffer $5A80-$7AFF, which out of a race holds nothing it should be
       drawing, while $7C00-$7FFF holds the teletext page instead of the dashboard overlay. */
    if (tt_active()) { decodeTeletext(); return; }
#ifdef REVS_DECODE_SPLIT
    /* ⚠ AFTER the teletext test, and that placement is the measurement.  Bracketed BEFORE it,
       this row read 2.72 ms with calls=345 against 337 frames: the front-end frames at the top of
       the window return early, so a whole `decodeTeletext()` landed in it eight times and was
       amortised over every race frame.  The `phase 27 remainder` it explains was never race
       work. */
    PROBE_PHASE(DEC_PHASE_ENTRY);
#endif

    Bitmap* bm = m_bitmap[m_back];
    if (!bm) return;
    s_lastDecoded = this;
    /* ⚠ THE PLOT TARGET IS NOT SET HERE — see present().  This decode fills m_bitmap[m_back],
       but the sweep that plots into it runs AFTER the flip that present() is about to do, so
       aiming the plotter from here aims it one buffer too early. */

#ifdef REVS_DECODE_SPLIT
    /* ⭐ THE CONTROL FIRST, and its bracket must stay EMPTY (probe.h §DECODESPLIT): phase 52's
       ticks are one transition's own cost, which every other row here has to be corrected by. */
    PROBE_PHASE(DEC_PHASE_NULL);
    PROBE_PHASE(DEC_PHASE_PRE);
#endif
    /* ⭐ FIRST, before a single pixel is decoded: capture the raster schedule that belongs to
       the frame buffer we are about to read.  See snapshotBands() — the pixels and the band
       boundaries have to come from the same game frame or the horizon lands in the wrong
       palette.  It also fills m_lineMode, which the loop below reads per row. */
    snapshotBands();
#ifdef REVS_DECODE_SPLIT
    PROBE_PHASE(DEC_PHASE_MODES);
#endif
    buildLineModes();
#ifdef REVS_DECODE_SPLIT
    PROBE_PHASE(DEC_PHASE_OWN);
#endif

#if defined(REVS_SPAN_OWN) && defined(REVS_DECODE_FULL)
    /* ⭐⭐⭐ THE SPAN EMITTER OWNS THESE DISPLAY LINES — EXPRESSED AS MODE 0, WHICH THE DECODE
     * ALREADY UNDERSTANDS AS "WRITE NOTHING" (revs_expand_cell's `if (!uniform && m == 0)
     * continue`).  Four properties fall out of the existing machinery for free, and each one is
     * a thing the scaffold's `g_plotLineLo/Hi` carve-out could not do:
     *   - PER LINE, not per character row.  A BBC cell is eight display lines, so converting one
     *     cell of a partly-owned row paints frozen mem[] straight over emitted lines.  The
     *     sweep's own region is 77 lines and never row-aligned.
     *   - A fully-owned row costs nothing at all: `any` is the OR of the eight modes, so it is
     *     skipped before a byte is read.
     *   - The frame a line STOPS being owned repaints correctly: mode 0 was written to
     *     `shadowMode`, so the mode-change bitmask fires and `revs_expand_line` re-expands it.
     *     That is the whole owned -> not-owned transition, already written and already tested.
     *   - No change to the hot loops.
     * ⚠ Not compiled under REVS_SPAN_VERIFY: the oracle's reference conversion reads m_lineMode
     * too, so a carved-out mode would blank the reference on exactly the lines under test. */
    {
        /* ⭐⭐ FOUR FLAGS AT A TIME.  The per-line form of this loop measured 1.75 ms/frame —
           ~60 cycles a display line to test one byte (probe.h §DECODESPLIT) — and the flags are
           SPARSE: the sweep owns ~36 of 208 lines, so 43 of the 52 groups are wholly zero and
           cost one `tst.l` between them.  Same idiom as the span scan's group-of-four `or.l`.
           ENDIAN-OK: the wide read is a ZERO TEST, and zero has no byte order.  The per-line
           work re-reads the bytes rather than unpacking the longword, so nothing here depends
           on which end byte 0 sits at.  `g_plotOwn` is `aligned(4)` for this. */
        /* ⭐⭐⭐ AND THE *FULL* GROUP GETS ONE LONGWORD STORE, BECAUSE OWNERSHIP IS RUN-SHAPED.
           The per-line arm is 68 cycles a line — `tst.b (0,a4,d0.l)` / `clr.b (0,a2,d0.l)` with a
           LONG index, plus the counter and the loop — and it ran for all 145 owned lines, which is
           most of this slot's 2.47 ms (probe.h §DECODESPLIT).  But the owned set is BLOCKS
           (0..18, 117..157, 158..191, 192..207), so four adjacent flags are almost always all
           set: test the group against `0x01010101` and zero four modes with a single `move.l`.
           ⚠ ENDIAN-OK twice over: the test is an equality against a byte-uniform constant and the
           store is ZERO, and neither has a byte order.  Both arrays are `aligned(4)`.
           ⚠ The mixed group keeps the byte loop — a block boundary is not 4-aligned (117 and 158
           are not), so there are always a few. */
        const uint32_t* const grp = (const uint32_t*)(const void*)g_plotOwn;
        uint32_t* const       mw  = (uint32_t*)(void*)m_lineMode;
        unsigned owned = 0;
        uint32_t anyMode = 0;
        for (unsigned q = 0; q < kH / 4u; q++) {
            const uint32_t g = grp[q];
            if (g == 0x01010101u) { mw[q] = 0; owned += 4u; continue; }
            if (g) {
                const unsigned y0 = q * 4u;
                for (unsigned y = y0; y < y0 + 4u; y++)
                    if (g_plotOwn[y]) { m_lineMode[y] = 0; owned++; }
            }
            /* ⭐⭐⭐ ...AND THE SAME WALK ANSWERS "IS THERE ANYTHING LEFT TO CONVERT?" FOR ONE
               `or.l` A GROUP.  A line's mode is 0 when it is OWNED (above) or when its band is
               FLAT (buildLineModes' palette test), and mode 0 is the decode's "write nothing" —
               so a frame in which every mode is 0 has `convertRace` walk 26 rows to convert
               NOTHING.  That is now the normal case: 145 of 208 lines are owned and 64 more are
               the flat sky, and the conversion was measured at 2.20 ms to deliver FOUR CELLS.
               ⚠ The full-group arm above `continue`s BEFORE this, which is correct — it has just
               written zero, so it can contribute nothing to the OR.
               ⚠ ENDIAN-OK: an OR of bytes against zero has no byte order. */
            anyMode |= mw[q];
        }
        g_decodeOwnLines = owned;
        g_decodeNothingToDo = (anyMode == 0u);
        revs_line_modes_stale();          /* the carve wrote m_lineMode */
    }
#endif

#ifdef REVS_VIEW_CARVE
    /* ⚠⚠ THE PICTURE IS WRONG BY CONSTRUCTION — A CEILING PROBE, NEVER A SHIPPING ARM.
     * `make VIEWCARVE=<lo>-<hi>` (bare `VIEWCARVE=1` = 117..157).  Claims a block of display
     * lines outright — `m_lineMode = 0` — so phase 27 stops converting them, and nothing
     * paints them instead: they FREEZE at whatever they last held.  That is the point.
     * 117..157 is the view sweep's SHORT phases (phase 2's 117..132 + phase 3's 133..157,
     * measured in §10p step 2); 158..207 is the dashboard and 0..17 the two MODE 4 text rows,
     * i.e. every block of §11's 208-row ledger, priced on ONE scale.
     * The delta on ph27 is the ENTIRE prize of flipping those stores to the bitplanes, because
     * `093e560` established that the store flip itself is a wash (a bitplane pair costs about
     * what the `mem[]` byte cost).  So the decode carve-out is what is left, and it is worth
     * knowing before the painter is written.
     * ⚠ It is a CEILING and not the achievable figure: a real per-run takeover owns two cell
     * RANGES a line, not the line, so the cells between the runs — the dash, the needles —
     * would still have to be converted. */
    for (unsigned y = REVS_VIEW_CARVE_LO; y <= REVS_VIEW_CARVE_HI; y++) m_lineMode[y] = 0;
    revs_line_modes_stale();
#endif

#ifdef REVS_FILLWATCH
    const unsigned long rejects0 = g_bandRejects;

    /* ⭐⭐ ONE LOG LINE PER PAINTED FRAME: does the band boundary AGREE WITH THE PIXELS?
     *
     * The remaining artefact survives a whole PAINTED frame (~1 s, ~50 fields) and clears on the
     * next one — so it is not a beam race any more, it is this frame's DATA: either the pixels or
     * the schedule they are shown under.  Band 3's boundary is where the ground starts, and the
     * game draws the ground's first green row there, so the two are one number measured two ways:
     * `m_plan.line[3]` from the T1 latches, and the topmost right-edge green cell from the frame
     * buffer.  They must match; a disagreement is a wrong-palette strip across the horizon, and
     * that is the artefact.
     *
     * ⚠ Logged rather than counted, because the suspect is snapshotBands() REJECTING a record it
     * caught mid-cycle and leaving the PREVIOUS frame's plan in place ("one frame stale in a
     * value that changes slowly" — which is false at 1 FPS: the horizon moves with the hills and
     * a frame is a second of game time).  A counter cannot show that; the sequence can. */
    {
        const uint8_t* col = (const uint8_t*)mem + BBC_SCREEN_BASE + (BBC_SCREEN_CELLS - 1) * 8;
        unsigned groundR = 0, groundL = 0;
        for (unsigned y = 60; y < 140; y++) {
            const unsigned row = y >> 3, line = y & 7;
            const unsigned off = revs_mulu16((uint16_t)row, BBC_SCREEN_BPR) + line;
            if (!groundR && col[off] == 0xFFu) groundR = y;
            if (!groundL && ((const uint8_t*)mem + BBC_SCREEN_BASE)[off] == 0xFFu) groundL = y;
            if (groundR && groundL) break;
        }
        const unsigned i = g_planLogN & (PLAN_LOG_MAX - 1u);
        g_planLogBand2[i]  = (uint8_t)m_plan.line[2];
        g_planLogBand3[i]  = (uint8_t)m_plan.line[3];
        g_planLogGroundL[i] = (uint8_t)groundL;
        g_planLogGroundR[i] = (uint8_t)groundR;
        g_planLogStale[i]  = (uint8_t)(g_bandRejects != rejects0);   /* this frame's plan is old */
        g_planLogN++;
        if (g_bandRejects != rejects0) g_planStaleFrames++;
    }

    /* ⭐⭐ THE INVARIANT CHECK — see g_fillEvidence above for what it is and why it is exact. */
    {
        const uint8_t* base = (const uint8_t*)mem + BBC_SCREEN_BASE;
        int band2 = m_plan.valid ? (int)m_plan.line[2] : 82;
        if (band2 < 2) band2 = 82;

        /* The ground line: the first line at or below band 2 that is green nearly all the way
           across.  Counted rather than assumed, so a hill or a moved horizon cannot fool it. */
        unsigned ground = 0;
        for (unsigned y = (unsigned)band2; y < FILL_EV_HI && !ground; y++) {
            const unsigned off = revs_mulu16((uint16_t)(y >> 3), BBC_SCREEN_BPR) + (y & 7u);
            unsigned green = 0;
            for (unsigned c = 0; c < BBC_SCREEN_CELLS; c++)
                if (base[off + c * 8u] == 0xFFu) green++;
            if (green >= 30u) ground = y;
        }
        g_fillGroundLine = (uint8_t)ground;

        unsigned badLine = 0, badCell = 0, badRun = 0, badVal = 0;
        for (unsigned y = (unsigned)band2; y < ground && !badRun; y++) {
            const unsigned off = revs_mulu16((uint16_t)(y >> 3), BBC_SCREEN_BPR) + (y & 7u);
            unsigned run = 0, start = 0, val = 0;
            for (unsigned c = 0; c < BBC_SCREEN_CELLS; c++) {
                const uint8_t b = base[off + c * 8u];
                if ((b == 0x00u || b == 0xFFu) && (run == 0 || b == val)) {
                    if (run == 0) { start = c; val = b; }
                    run++;
                } else if (run >= 8u) {
                    break;                     /* a long enough run: report it */
                } else {
                    run = 0;
                }
            }
            if (run >= 8u) { badLine = y; badCell = start; badRun = run; badVal = val; }
        }

        if (badRun) {
            g_fillBadFrames++;
            /* Latch the FIRST one only: the next painted frame overwrites the buffer, and the
               first catch is the one whose neighbourhood is still intact to look at. */
            if (g_fillBadLine == 0xFFFFu) {
                g_fillBadLine  = (uint16_t)badLine;
                g_fillBadCell  = (uint8_t)badCell;
                g_fillBadRun   = (uint8_t)badRun;
                g_fillBadValue = (uint8_t)badVal;
                g_fillBadFrameN = g_planLogN;
                for (unsigned y = FILL_EV_LO; y < FILL_EV_HI; y++) {
                    const unsigned off = revs_mulu16((uint16_t)(y >> 3), BBC_SCREEN_BPR) + (y & 7u);
                    for (unsigned c = 0; c < BBC_SCREEN_CELLS; c++)
                        g_fillEvidence[(y - FILL_EV_LO) * BBC_SCREEN_CELLS + c] = base[off + c * 8u];
                }
            }
        }
    }

#endif  /* REVS_FILLWATCH */

    const uint8_t* base = (const uint8_t*)mem + BBC_SCREEN_BASE;
    uint8_t* dst = (uint8_t*)bm->data;
    g_screenBackAddr = (uint32_t)dst;

#ifdef REVS_FILLWATCH
    /* ⭐ The tear detector's FIRST pass, and it is its own loop rather than a hitch-hiker inside
       the conversion: the dirty-region decode below reads only the cells that CHANGED, so a
       checksum folded into it would stop covering the bytes the second pass re-reads and would
       report every skipped cell as torn — an instrument turned into noise by an optimisation is
       precisely the failure mode docs/method-lessons.md warns about. */
    uint16_t rowSum[BBC_SCREEN_ROWS];
    for (unsigned row = 0; row < BBC_SCREEN_ROWS; row++) {
        const uint8_t* rowBase = base + revs_mulu16((uint16_t)row, BBC_SCREEN_BPR);
        uint16_t sum = 0;
        for (unsigned line = 0; line < BBC_SCREEN_LINES; line++) {
            const uint8_t* s = rowBase + line;
            for (unsigned i = 0; i < BBC_SCREEN_CELLS; i++) {
                sum = (uint16_t)(sum + *s + i);  /* +i so a swap of two cells still differs */
                s += BBC_SCREEN_LINES;
            }
        }
        rowSum[row] = sum;
    }
#endif

    /* ⭐⭐ THE CONVERSION, dirty-region by default (see g_decodeCells above).  s_shadow is
       indexed by m_back because the Amiga is DOUBLE buffered: the bytes this buffer's pixels
       came from are two decodes old, not one. */
#ifdef REVS_DECODE_SPLIT
    PROBE_PHASE(DEC_PHASE_CONVERT);
#endif
    /* ⭐⭐⭐ THE CONVERSION IS SKIPPED OUTRIGHT WHEN EVERY LINE IS OWNED OR FLAT (`DECODESKIP=1`).
       ⚠⚠ AND THE SHADOW STAYS CONSISTENT, WHICH IS THE ONLY THING THIS COULD BREAK.  The shadow
       records what was CONVERTED INTO THIS BUFFER; a skipped frame writes nothing, so what the
       shadow says the buffer holds is still exactly what it holds.  And a line that stops being
       owned makes its mode non-zero again, so `anyMode` is non-zero on that very frame and the
       conversion runs — the mode-change bitmask then sees `s_shadowMode` disagree and re-expands
       the row, which is the owned -> not-owned transition already written and already tested
       (§SPAN_OWN's fourth property). */
#ifndef REVS_DECODE_FULL
    /* ⭐⭐⭐ THE CONVERSION RUNS ONLY WHEN A DISPLAY LINE HAS NO OWNER — WHICH IS THE COLD FRAMES
       AND NOTHING ELSE.  Every line has a painter: 0..18 and 192..207 the glyph delta base,
       83..116 the span sweep, 117..157 the low painter plus the cockpit layer, 158..191 the dash
       base and its rectangles — and the 64-line sky band is FLAT, i.e. its four palette entries
       are equal, so no plane bit in it is observable (it is the engine's own bytes showing
       through screen memory, which is why the picture there was always arbitrary).
       ⚠⚠ THE TEST IS EXACT AND PER FRAME, and a fixed cold-frame count is NOT a substitute:
       measured over five circuits, the gap frames stop at frame 2 on four of them and at frame
       **54** on Donington, where a two-frame guess left ~45 stale lines for ten seconds of race.
       `own_has_gap` costs one `cmp.l` per four lines of a non-flat band (buildLineModes).
       ⚠ `g_decodeGapFrames` / `g_decodeGapLastAt` are the census, always compiled in: a gap
       LATE in a run is a region whose painter stopped painting, and it is the one thing that can
       put stale pixels on the screen.  `make DECODEFULL=1` restores the per-frame conversion. */
    if (s_frameHasGap) {
        /* ⚠ THE OWNED LINES ARE MUTED FIRST, and only on a gap frame.  A painted line must not
           be re-expanded out of `mem[]` — the span sweep's own bytes are a generation ahead of
           it — so ownership is still expressed as mode 0, just no longer every frame. */
#ifdef REVS_SPAN_OWN
        {
            unsigned y;
            for (y = 0; y < kH; y++) if (g_plotOwn[y]) m_lineMode[y] = 0;
            revs_line_modes_stale();      /* next frame restores the painters' modes */
        }
#endif
        g_decodeCells       = (uint16_t)convertRace(dst, 0, 0);
        g_decodeCellsTotal += g_decodeCells;
        g_decodeFullFrames++;
    } else {
        g_decodeCells = 0;
    }
#else
    {
#ifdef REVS_NO_DIRTY
        const unsigned cells = convertRace(dst, 0, 0);
#else
        const unsigned cells = convertRace(dst, (uint8_t*)s_shadow[m_back], s_shadowMode[m_back]);
#endif
        g_decodeCells       = (uint16_t)cells;
        g_decodeCellsTotal += cells;
        if (cells > g_decodeCellsMax) g_decodeCellsMax = (uint16_t)cells;
        if (cells >= BBC_SCREEN_ROWS * BBC_SCREEN_CELLS) g_decodeFullFrames++;
    }
#endif
#ifdef REVS_DECODE_SPLIT
    PROBE_PHASE(DEC_PHASE_POST);       /* the counters and the oracles get their own row */
#endif

#if defined(REVS_DUAL_PLAYFIELD) && !defined(REVS_DUAL_NOCONVERT)
    /* ⭐ THE COCKPIT LAYER.  After the terrain conversion, because the two must describe the
       same mem[] and this is where mem[] is final for the frame. */
    PROBE_PHASE(PROBE_PHASE_COCKPIT);
    revs_cockpit_paint(base, (uint8_t*)m_cockpit->data, m_lineMode);
#ifdef REVS_DECODE_SPLIT
    PROBE_PHASE(DEC_PHASE_POST);
#else
    PROBE_PHASE(PROBE_PHASE_PREPARE);
#endif
    g_cockpitCells       = (uint16_t)s_cockCells;
    g_cockpitCellsTotal += s_cockCells;

#ifdef REVS_DUAL_CHECK
    /* ⭐⭐ THE ORACLE FOR THE DECOMPOSITION, and it runs HERE because here is the only place the
     * three things it relates are simultaneous: mem[], the PF1 buffer this decode just filled,
     * and PF2.  The same check from gdb at a frame boundary read 114 then 38 stale pixels — the
     * displayed buffer is a decode behind mem[], and the layer under test looked guilty for it.
     *
     * COMPOSITE PF2 OVER PF1 AND REQUIRE THE BBC PEN BACK.  Bitwise, so it is two expressions
     * rather than eight pixels: PF2 is opaque wherever either plane bit is set, its pen 1 is
     * `lo & ~hi` and its pen 2 is `hi & ~lo`, and its pen 3 maps to BBC pen 0 = both bits clear.
     *
     * ⚠⚠ AND ITS SCOPE, STATED AT THE ORACLE (docs/validation-harness.md).  It CANNOT falsify
     * the silhouette: PF1 still decodes every cell, so marking a terrain cell as car merely
     * covers a pixel with its own colour and composites back identically.  That is not a gap to
     * widen — it is the step being a pure decomposition, and it is exactly why the mask is
     * gated instead by dumping s_cockRun and diffing it against the game's own tables read out
     * of a real BBC race (tools/revs_dualpf.py, amiga/dualpf_dump.gdb).  What it DOES cover is
     * the remap, the plane offsets, the two bitmap addresses and the per-line region bounds —
     * i.e. everything that would put the car in the wrong place or the wrong colour. */
    {
        const uint8_t* const cbase = (const uint8_t*)mem + BBC_SCREEN_BASE;
        const uint8_t* const p2b   = (const uint8_t*)m_cockpit->data;
        unsigned y;
        g_cockpitChecks++;

        /* The footprint census — its own shadow, so it reports "moved since the LAST painted
           frame" for every cell, independently of anything the layer or the decode believes. */
        {
            static uint32_t seen[41][40][2];
            static int primed = 0;
            for (y = COCK_Y0; y <= COCK_Y1; y++) {
                const unsigned row = y >> 3, ln = y & 7u;
                unsigned c;
                for (c = 0; c < BBC_SCREEN_CELLS; c++) {
                    const uint8_t* const q =
                        cbase + revs_mulu16((uint16_t)row, BBC_SCREEN_BPR) + c * 8u;
                    const uint32_t w0 = ((const uint32_t*)(const void*)q)[0];
                    const uint32_t w1 = ((const uint32_t*)(const void*)q)[1];
                    (void)ln;
                    if (primed && (w0 != seen[y - COCK_Y0][c][0] ||
                                   w1 != seen[y - COCK_Y0][c][1]))
                        g_cockChange[y - COCK_Y0][c]++;
                    seen[y - COCK_Y0][c][0] = w0;
                    seen[y - COCK_Y0][c][1] = w1;
                }
            }
            if (primed) g_cockChangeFrames++;
            primed = 1;
        }
        for (y = COCK_Y0; y <= COCK_Y1; y++) {
            const unsigned off = revs_mulu16((uint16_t)(y >> 3), BBC_SCREEN_BPR) + (y & 7u);
            const unsigned lo  = revs_mulu16((uint16_t)y, kRowBytes);
            const uint8_t* const r = s_cockRun[y - COCK_Y0];
            unsigned c;
            if (m_lineMode[y] != 5u) continue;
            for (c = 0; c < BBC_SCREEN_CELLS; c++) {
                /* ⭐⭐⭐ THE PLACE `mem[]` STOPS BEING THE REFERENCE, which is the whole terrain
                   half of this band.  (The needle column used to be a second one; the needles
                   are sprites now, never in `mem[]` or either plane, so the dial art under them
                   is checked here like the rest of the car.)  PF1's cells inside the two runs are painted
                   by the SPAN SWEEP, which runs after present()'s flip — so the bytes in this
                   buffer describe an earlier `mem[]` than the one being read here.  While the
                   conversion still ran every frame it re-expanded them and hid the skew; with
                   the conversion gone (see it, above) the skew is the design, and comparing them
                   reads a moving road as 3430 wrong pixels.  What this oracle still covers is
                   what it was written for: the CAR — PF2's own cells, painted from the same
                   `mem[]` this decode is reading.
                   ⚠ Measured before narrowing it, so the exclusion is a fact and not a hope:
                   of 3498 mismatching pixels 3430 were inside a run, and every one of the other
                   68 was in the needle column or the tyre-sprite footprint below. */
                if (c >= r[0] && c <= r[1]) continue;
                if (c >= r[2] && c <= r[3]) continue;
#ifdef REVS_TYRE_SPRITES
                /* The tread is masked out of BOTH planes and drawn by a SPRITE, so `mem[]`'s own
                   dither is left in the reference with nothing to match it (RevsTyres.h). */
                if (y >= REVS_TYRE_Y0 && y < REVS_TYRE_Y0 + REVS_TYRE_LINES &&
                    (c <= REVS_TYRE_L_CELL + 1u || c >= REVS_TYRE_R_CELL)) continue;
#endif
                const uint8_t b     = cbase[off + c * 8u];
                const uint8_t p1lo  = dst[lo + c],        p1hi = dst[lo + c + kPlaneGap];
                const uint8_t p2lo  = p2b[lo + c],        p2hi = p2b[lo + c + kPlaneGap];
                const uint8_t clear = (uint8_t)~(p2lo | p2hi);      /* PF2 transparent here */
                const uint8_t gotLo = (uint8_t)((p2lo & ~p2hi) | (clear & p1lo));
                const uint8_t gotHi = (uint8_t)((p2hi & ~p2lo) | (clear & p1hi));
                if (gotLo == s_expandLo[b] && gotHi == s_expandHi[b]) continue;
                g_cockpitMismatch++;
                if (g_cockpitMismatchAt == 0xFFFFu)
                    g_cockpitMismatchAt = (uint16_t)((y << 8) | c);
            }
        }
    }
#endif
#endif

    /* ⭐⭐⭐ §12c — AND THE DYNAMIC RECTANGLES, over the rows the conversion just skipped.
       The bottom band is OWNED, so `convertRace` wrote none of it; what still moves down there is
       six small rectangles (the two needles, the two front-wheel dithers, the two wing mirrors)
       and they are re-expanded here from `mem[]`.  ⚠ HERE, at the end of the decode, for two
       reasons: this is the last point in a painted frame at which `mem[]` is final — the needles
       are `race_main_loop`'s closing draw and `tick_wheel_spin` runs at 50 Hz from the band
       schedule — and `s_target` is this buffer (present() aims it at `m_bitmap[m_back]` and
       decode fills that same one, which is the pairing the comment at present() exists for). */
    /* ⚠ ITS OWN DECODESPLIT SLOT.  The rect pass lives inside the decode's phase, so `ph27`
       alone cannot say whether a move is the conversion the rows delete or the re-expand that
       replaces it — the first A/B of this change read +8.0 ms on that one row with no way to
       attribute it.  `make DECODESPLIT=1` carves the rest of decode() the same way. */
#ifdef REVS_DECODE_SPLIT
    PROBE_PHASE(DEC_PHASE_RECTS);
#endif
    REVS_PLOT_RECTS_RUN();

    /* ⭐⭐⭐ §12d — AND THE TWO DASH NEEDLES: choose each mark's prerendered sprite image (and
       build it, the first time its pixel list is seen).  The list was filled by
       `draw_dash_needles`, `race_main_loop`'s closing draw, so it describes the same game frame
       this decode prepared; `present()` shows the images with that frame's flip. */
    REVS_NEEDLE_PAINT();

#ifdef REVS_TYRE_SPRITES
    /* ⭐ BUILD THE TWO TYRE STATES, ONCE.  Main-loop context, as RevsTyres.h requires: the build
       applies the game's own EOR to the live frame buffer twice (to discover state B and to
       check the period) and restores around it, so the band-4 ISR arm must not run `tick_wheel_
       spin` in the middle of it.  Deferred to here rather than initialize() because the patch
       does not exist until the race view has been painted at least once. */
    /* ⚠ NOT OVER A MODE 7 PAGE.  $7B00-$7FFF is the teletext screen as well as the race view's
       code overlay, and the patch addresses are frame-buffer addresses either way — building
       from a front-end page would fill both sprites with teletext bytes and the report would
       look entirely plausible (a believable `zeroAnim`, `periodBad` 0).
       ⚠⚠ AND THE COLOURS ARE SAMPLED FROM ONE FRAME.  The sweep repaints the arch every frame
       with the off-road colour of wherever the car is, so an image built on grass is wrong on
       gravel.  That is tolerable only because this is a staging step: the arch stops being
       repainted at all once these rows are owned, which is the same commit that has to make the
       playfield colour 0 under the 58 animated pixels `g_tyreZeroAnim` counts. */
    if (!m_tyreReady && !tt_active() &&
        m_tyre[0][0] && m_tyre[0][1] && m_tyre[1][0] && m_tyre[1][1])
        m_tyreReady = revs_tyres_build((uint16_t*)m_tyre[0][0]->data() + 2,
                                       (uint16_t*)m_tyre[0][1]->data() + 2,
                                       (uint16_t*)m_tyre[1][0]->data() + 2,
                                       (uint16_t*)m_tyre[1][1]->data() + 2) != 0;

    /* ⭐⭐⭐ AND THE PLAYFIELD'S HALF OF THE SPLIT, LAST OF ALL: the tread comes OUT of the
       bitplanes, so the sprite is the only layer that draws it (user, 2026-09-21).  It must be
       the last write to these rows — the conversion above re-expands them from `mem[]`, which
       still holds state A's dither, and `REVS_PLOT_RECTS_RUN` re-expands the same two rectangles
       when that feature is on. */
    revs_tyres_outline(dst);
#ifdef REVS_DUAL_PLAYFIELD
    /* ⚠ AND ON PF2 TOO, OR THE SPLIT LEAKS.  The tread cells are car cells, so the cockpit layer
       has just expanded mem[]'s own dither into them; masking it out of PF1 alone would leave
       PF2 drawing it.  Masked on both, a tread pixel is PF2 pen 0 over PF1 pen 0 = COLOR00 —
       the same black the single-playfield build left there — and the sprite supplies the
       pattern. */
    if (s_cockOutlineStale) {                  /* only after the layer was re-expanded */
        revs_tyres_outline((uint8_t*)m_cockpit->data);
        s_cockOutlineStale = 0u;
    }
#endif
#endif
#ifdef REVS_DECODE_SPLIT
    PROBE_PHASE(DEC_PHASE_TAIL);
#endif

#ifdef REVS_DIRTYCHECK
    /* ⭐⭐ THE ORACLE, and it is exact: an UNCONDITIONAL decode of the same frame must produce
     * byte-identical bitplanes.  The scratch starts as a COPY of what the dirty pass produced,
     * so the flat-band lines — which neither pass writes — are equal by construction and any
     * difference is the dirty test wrongly skipping something.  ⚠ It runs the whole reference
     * pass every frame, so this build is far slower than shipping: never quote FPS from it.
     * ⚠⚠ AND THAT IS ITS EXACT SCOPE — SKIPPING, NOT CLASSIFYING.  The reference pass reads the
     * same m_lineMode, so anything convertRace derives FROM m_lineMode (the `uniform` switch, the
     * `any` fast-out's interpretation of a mode byte) is computed identically wrong on both
     * sides and this oracle reads mismatch=0.  Measured both ways: sabotaging the shadow-mode
     * compare fires it at 4050, sabotaging the uniformity guard survives 31/31.  A classification
     * change is gated by the PICTURE (amiga/screen_dump.gdb) or by argument at the code, never
     * here. */
    {
        static uint8_t scratch[BBC_SCREEN_HEIGHT * kRowBytes];
        for (unsigned i = 0; i < sizeof scratch; i++) scratch[i] = dst[i];
        convertRace(scratch, 0, 0);
        g_decodeDirtyChecks++;
        for (unsigned i = 0; i < sizeof scratch; i++) {
            if (scratch[i] != dst[i]) {
                g_decodeDirtyMismatch++;
                if (g_decodeDirtyMismatchOff == 0xFFFFu) g_decodeDirtyMismatchOff = (uint16_t)i;
            }
        }
    }
#endif

#ifdef REVS_FILLWATCH
    /* ⭐ Second pass: the same checksum over the same bytes.  Anything that differs was written
       by the VERTB handler's game body WHILE this decode was reading — a tear that is now baked
       into the bitplanes for the whole painted frame.  See g_tearFrames above. */
    unsigned torn = 0;
    for (unsigned row = 0; row < BBC_SCREEN_ROWS; row++) {
        const uint8_t* rowBase = base + revs_mulu16((uint16_t)row, BBC_SCREEN_BPR);
        uint16_t sum = 0;
        for (unsigned line = 0; line < BBC_SCREEN_LINES; line++) {
            const uint8_t* s = rowBase + line;
            for (unsigned i = 0; i < BBC_SCREEN_CELLS; i++) {
                sum = (uint16_t)(sum + *s + i);
                s += BBC_SCREEN_LINES;
            }
        }
        if (sum != rowSum[row]) {
            torn++;
            g_tearRows++;
            if (g_tearRowCount[row] != 0xFFFFu) g_tearRowCount[row]++;
            g_tearLastRow = (uint16_t)row;
        }
    }
    if (torn) g_tearFrames++;

    /* ---- check 1: how much of the horizon changed since the last painted frame? ---- */
    {
        static uint8_t prev[(FILL_EV_HI - FILL_EV_LO) * BBC_SCREEN_CELLS];
        static int havePrev = 0;
        unsigned changed = 0;
        for (unsigned y = FILL_EV_LO; y < FILL_EV_HI; y++) {
            const unsigned off = revs_mulu16((uint16_t)(y >> 3), BBC_SCREEN_BPR) + (y & 7u);
            uint8_t* p = &prev[(y - FILL_EV_LO) * BBC_SCREEN_CELLS];
            for (unsigned c = 0; c < BBC_SCREEN_CELLS; c++) {
                const uint8_t b = base[off + c * 8u];
                if (havePrev && p[c] != b) changed++;
                p[c] = b;
            }
        }
        havePrev = 1;
        if (changed > g_horizonChangeMax) g_horizonChangeMax = changed;
        if (changed >= 40u) g_horizonChangeBig++;
        g_horizonChangeSeries[(g_planLogN - 1u) & 63u] =
            (uint8_t)(changed > 255u ? 255u : changed);
    }

    /* ---- check 3: the road's LEFT edge, against its own neighbours ---- */
    {
        /* Leftmost / rightmost black cell per line, over the road region only. */
        uint8_t L[FILL_EV_HI - FILL_EV_LO], R[FILL_EV_HI - FILL_EV_LO];
        for (unsigned y = FILL_EV_LO; y < FILL_EV_HI; y++) {
            const unsigned off = revs_mulu16((uint16_t)(y >> 3), BBC_SCREEN_BPR) + (y & 7u);
            unsigned l = 0xFFu, r = 0xFFu;
            for (unsigned c = 0; c < BBC_SCREEN_CELLS; c++)
                if (base[off + c * 8u] == 0x00u) { if (l == 0xFFu) l = c; r = c; }
            L[y - FILL_EV_LO] = (uint8_t)l;
            R[y - FILL_EV_LO] = (uint8_t)r;
        }
        for (unsigned i = 1; i + 1 < (FILL_EV_HI - FILL_EV_LO); i++) {
            if (L[i] == 0xFFu || L[i-1] == 0xFFu || L[i+1] == 0xFFu) continue;
            if (R[i] == 0xFFu || R[i-1] == 0xFFu || R[i+1] == 0xFFu) continue;
            const int dPrev = (int)L[i] - (int)L[i-1], dNext = (int)L[i] - (int)L[i+1];
            const int rPrev = (int)R[i] - (int)R[i-1], rNext = (int)R[i] - (int)R[i+1];
            if (dPrev >= 4 && dNext >= 4 &&                    /* the left edge jumped RIGHT */
                rPrev > -3 && rPrev < 3 && rNext > -3 && rNext < 3) {   /* ...and only it did */
                g_edgeJumpFrames++;
                if (g_edgeJumpLine == 0xFFFFu) {
                    g_edgeJumpLine = (uint16_t)(FILL_EV_LO + i);
                    g_edgeJumpPrev = L[i-1];
                    g_edgeJumpHere = L[i];
                    g_edgeJumpNext = L[i+1];
                    for (unsigned y = FILL_EV_LO; y < FILL_EV_HI; y++) {
                        const unsigned o = revs_mulu16((uint16_t)(y >> 3), BBC_SCREEN_BPR) + (y & 7u);
                        for (unsigned c = 0; c < BBC_SCREEN_CELLS; c++)
                            g_fillEvidence[(y - FILL_EV_LO) * BBC_SCREEN_CELLS + c] = base[o + c * 8u];
                    }
                }
                break;
            }
        }
    }

    /* ---- check 2: do the decoded bitplanes still match mem[] for those lines? ---- */
    for (unsigned y = FILL_EV_LO; y < FILL_EV_HI; y++) {
        if (m_lineMode[y] != 5) continue;
        const unsigned off = revs_mulu16((uint16_t)(y >> 3), BBC_SCREEN_BPR) + (y & 7u);
        const uint8_t* p1 = dst + revs_mulu16((uint16_t)y, kRowBytes);
        const uint8_t* p2 = p1 + (kW / 8);
        for (unsigned c = 0; c < BBC_SCREEN_CELLS; c++) {
            const uint8_t b = base[off + c * 8u];
            if (p1[c] != s_expandLo[b] || p2[c] != s_expandHi[b]) {
                g_decodeMismatch++;
                if (g_decodeMismatchLine == 0xFFFFu) g_decodeMismatchLine = (uint16_t)y;
                break;
            }
        }
    }
#endif  /* REVS_FILLWATCH */

    m_ready = true;
#ifdef REVS_DECODE_SPLIT
    PROBE_PHASE(PROBE_PHASE_PREPARE);   /* what is left on 27 is the switches and render() */
#endif
}
