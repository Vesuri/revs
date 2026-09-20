/* RevsScreen — see RevsScreen.h.  Every constant here is derived in
   src/platform/bbc_screen.h; this file only re-hosts it. */
#define ECS_SPECIFIC
#include <hardware/custom.h>
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
#ifdef REVS_DECODE_SPLIT
#include "../probe.h"              /* DECODESPLIT: the three spare phase ids carve decode() */
#endif
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
#define IDX_BPL         (IDX_PLAYFIELD + 3)     /* 2 interleaved planes          (4) */
#define IDX_TOPPAL      (IDX_BPL + 4)           /* COLOR00..03 for display line 0 (4) */
#define IDX_BANDS       (IDX_TOPPAL + 4)
#define BAND_WORDS      5                       /* WAIT + COLOR00..03                */
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

    /* ⭐ PLAYFIELD IN FRONT OF EVERY SPRITE GROUP.  PFxP = 4 means "the playfield is behind
       sprite groups 0..3 and in front of groups 4..", i.e. in front of all four — the BBC
       has no sprite layer, so nothing may ever appear over the game.  The reset value 0 is
       the OPPOSITE (playfield behind every group), which is what let the unpointed sprite
       channels paint over the picture.  Belt and braces with the null sprites: the sprites
       are disarmed AND would lose the priority fight if they weren't. */
    *bplcon2Pointer = 0x0024;

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
void RevsScreen::initialize()
{
    for (unsigned b = 0; b < 256; b++) {
        s_expandHi[b] = expandNibble(b >> 4);
        s_expandLo[b] = expandNibble(b & 0x0Fu);
    }
    /* Until the first band record arrives, decode everything as MODE 5 — that is four of
       the five bands, and the fifth covers blank rows. */
    for (unsigned y = 0; y < kH; y++) m_lineMode[y] = 5;

    m_bitmap[0]   = Bitmap::allocate(kW, kH, kBP, /*interleaved*/true);
    m_bitmap[1]   = Bitmap::allocate(kW, kH, kBP, /*interleaved*/true);
    m_copper      = CopperList::allocate(LIST_LENGTH);
    m_nullSprite  = Sprite::allocate(0);
    if (!m_bitmap[0] || !m_bitmap[1] || !m_copper || !m_nullSprite) return;

    /* ⭐ BOTH plane buffers to the plot module, once — the glyph domain's delta painter keeps a
       few bytes a frame on rows nothing else repaints, so it must reach both (revs_plot.h).
       ⚠ Here and not in present(): it also builds the 16.6 KB address map, which is free at
       start-up and is a dropped frame inside one. */
    REVS_PLOT_PLANES((unsigned char*)m_bitmap[0]->data, (unsigned char*)m_bitmap[1]->data);

    /* MODE 7's own configuration.  ⚠ Allocated up front, never on the mode switch: a chip-RAM
       allocation inside a frame is the Atari port's 3.6-second freeze, and a FAILED one at a
       mode switch would blank the front end with no way to attribute it. */
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

    /* Per-frame / per-band playfield state: BPLCON0 (plane count + ECSENA) and the
       interleave modulos.  These are the only playfield registers the copper touches —
       everything constant is in setConstantRegisters() above. */
    m_copper->setPlayfield(IDX_PLAYFIELD, kW, kH, kBP, /*interleaved*/true);
    m_copper->showBitmap(IDX_BPL, *m_bitmap[0], 1, 1, 0, 0, kBP);

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

/* ⭐ THE PAGE -> THREE BITPLANES.  Main-loop context, ROW BY ROW, only where the page changed.
 *
 * A full 25-row redraw is 1000 cells x 10 lines x 3 planes = 30000 byte stores, so redrawing
 * unconditionally would drag the front end down re-drawing a page that has not moved.  Every
 * writer of the page marks its row in g_ttRowDirty (teletext.h) instead, and this converts only
 * the marked rows — an unchanged frame does no work and reads no screen RAM.  A flash-phase flip
 * re-dirties the whole page: a static front end can afford the occasional full redraw, and it
 * keeps the flashing "PRESS" prompt correct with no per-row flash bookkeeping.
 *
 * ⭐ The row and half base addresses are walked with a running pointer (rowTop += kRowStride), so
 * there is no per-row / per-scanline multiply in the addressing.
 * ⚠ NOT a widening cast on mem[] — it must never be aliased as a 16- or 32-bit pointer
 * (make endian-lint). */
void RevsScreen::decodeTeletext()
{
    if (!m_ttBitmap) return;

    const unsigned char phase = (unsigned char)tt_flash_phase();
    unsigned long dirty = g_ttRowDirty;
    if (phase != m_ttFlashSeen) { dirty = (1UL << TT_ROWS) - 1UL; m_ttFlashSeen = phase; }
    g_ttRowDirty = 0;
    if (!dirty) { g_ttRowsDrawn = 0; return; }

    uint8_t* const base  = (uint8_t*)m_ttBitmap->data;
    uint8_t* const limit = base + revs_mulu16(kTtH, kTtRowBytes);
    const unsigned kRowStride = TT_CELL_H * kTtRowBytes;   /* bytes one cell row occupies */

    unsigned long dblRows = m_ttRowDbl;
    uint16_t drawn = 0;

    unsigned  row    = 0;
    uint8_t*  rowTop = base;              /* == base + row*kRowStride, kept additively */
    while (row < TT_ROWS) {
        const unsigned long bit = 1UL << row;
        if (!(dirty & bit)) { rowTop += kRowStride; row++; continue; }

        const unsigned char* src =
            (const unsigned char*)(const void*)(mem + TT_SCREEN_BASE + row * TT_COLS);
        TtCell cells[TT_COLS];
        const int dbl = tt_decode_row(src, cells, (int)phase);

        /* A row that just LOST double-height must repaint the row below it: that display row was
           this row's bottom half and the row below may not be dirty on its own account.  A row
           that just GAINED it overwrites that display row with its own bottom half here. */
        if ((dblRows & bit) && !dbl) dirty |= (bit << 1);
        if (dbl) dblRows |= bit; else dblRows &= ~bit;

        /* A double-height row draws the TOP halves on this display row and the BOTTOM halves on
           the next; the source row below is then not displayed — the chip's rule. */
        const unsigned halves = dbl ? 2u : 1u;
        uint8_t* halfBase = rowTop;
        for (unsigned half = 0; half < halves; half++, halfBase += kRowStride) {
            if (halfBase + kRowStride > limit) break;
            for (unsigned col = 0; col < TT_COLS; col++) {
                const TtCell* c = &cells[col];
                const uint8_t* g = &g_ttFont[((unsigned)c->set * 128u + c->code)
                                             << TT_GLYPH_SHIFT];
                /* ⭐ Colour becomes a per-plane BIT MASK, which is what makes this three
                   stores and no branches: pen index == teletext colour code, so plane p's byte
                   is the glyph where the foreground has bit p set and its complement where the
                   background does. */
                const uint8_t f0 = (c->fg & 1u) ? 0xFFu : 0x00u;
                const uint8_t f1 = (c->fg & 2u) ? 0xFFu : 0x00u;
                const uint8_t f2 = (c->fg & 4u) ? 0xFFu : 0x00u;
                const uint8_t b0 = (c->bg & 1u) ? 0xFFu : 0x00u;
                const uint8_t b1 = (c->bg & 2u) ? 0xFFu : 0x00u;
                const uint8_t b2 = (c->bg & 4u) ? 0xFFu : 0x00u;

                uint8_t* dst = halfBase + col;
                for (unsigned y = 0; y < TT_CELL_H; y++) {
                    /* Double height stretches each source row over two display lines: the top
                       half of the glyph on the first row, the bottom half on the second. */
                    const uint8_t bits = dbl ? g[half * (TT_CELL_H / 2) + (y >> 1)] : g[y];
                    const uint8_t inv  = (uint8_t)~bits;
                    dst[0]               = (uint8_t)((bits & f0) | (inv & b0));
                    dst[kTtPlaneGap]     = (uint8_t)((bits & f1) | (inv & b1));
                    dst[kTtPlaneGap * 2] = (uint8_t)((bits & f2) | (inv & b2));
                    dst += kTtRowBytes;   /* one scan line: the `lea 120(a1),a1` step */
                }
            }
        }
        drawn++;

        if (dbl) {
            dblRows &= ~(bit << 1);   /* the consumed bottom-half row is not itself double */
            rowTop  += kRowStride;
            row++;
        }
        rowTop += kRowStride;
        row++;
    }

    m_ttRowDbl    = dblRows;
    g_ttRowsDrawn = drawn;
}

/* ⭐ HAND THE DISPLAY TO WHICHEVER MODE THE MACHINE IS IN.  VBI context.
 *
 * The switch is one COP1LC write plus COPJMP1, and a DIW change.  It happens twice a session,
 * so a single glitched field at the transition is acceptable — a real BBC's mode change is far
 * more violent than that.  Returns non-zero when it switched, so the caller does not then go on
 * to rebuild bands into a list that is no longer the active one. */
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
        /* Force a full redraw: the page has to be re-blitted into a buffer that may hold the
           last front end, and the dirty set would otherwise say "unchanged". */
        tt_mark_all_dirty();
        m_ttFlashSeen = 0xFFu;
        g_screenCopperAddr  = (uint32_t)m_ttCopper->data();
        g_screenCopperWords = TT_LIST_LENGTH;
        g_screenFrontAddr   = (uint32_t)m_ttBitmap->data;
        g_screenBytes       = (uint16_t)revs_mulu16(kTtH, kTtRowBytes);
        g_screenPlanes      = kTtBP;
        g_screenHeight      = kTtH;
        g_screenMode7       = 1;
        AmigaHardware::setCopperList(*m_ttCopper, /*immediate*/true);
    } else {
        g_screenCopperAddr  = (uint32_t)m_copper->data();
        g_screenCopperWords = LIST_LENGTH;
        g_screenFrontAddr   = (uint32_t)m_bitmap[m_back ^ 1u]->data;
        g_screenBytes       = (uint16_t)revs_mulu16(kH, kRowBytes);
        g_screenPlanes      = kBP;
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

void RevsScreen::buildLineModes()
{
    /* The record must be the game's five bands, identified by the state it wrote them
       under ($4F43): 0,1,2,3 and $FF for the last.  Anything else and the previous
       frame's plan stands — see g_bandRejects. */
    const BandSnapshot& s = m_bandSnap;
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

        m_plan.line[n] = (short)lineStart;
        m_plan.rec[n]  = (unsigned char)rec;

        startUs = nextUs;
    }
    m_plan.valid = 1;
    REVS_PLOT_BANDS(m_plan.line, bandMode, 5);
    g_decodeFlatLines = flatLines;
    g_decodeFlatBands = flatBands;
}

void RevsScreen::buildBands()
{
    if (!m_plan.valid) return;
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
    m_copper->showBitmap(IDX_BPL, *m_bitmap[m_back], 1, 1, 0, 0, kBP);
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
     * teletext page. */
    REVS_PLOT_TARGET(tt_active() || !m_bitmap[m_back] ? (uint8_t*)0
                                                      : (uint8_t*)m_bitmap[m_back]->data);
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
    if (g_bandCount != 5) { g_bandRejects++; return; }
    m_bandSnap.count = 5;
    for (unsigned i = 0; i < 5; i++) {
        m_bandSnap.state[i]    = g_bandState[i];
        m_bandSnap.duration[i] = g_bandDuration[i];
        m_bandSnap.control[i]  = g_bandControl[i];
        for (unsigned c = 0; c < 16; c++)
            m_bandSnap.palette[i][c] = g_bandPalette[i][c];
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
void RevsScreen::decode()
{
#ifdef REVS_SCREEN_NO_DECODE
    m_ready = true;
    return;
#endif
    /* ⭐ MODE 7 IS A DIFFERENT PASS ENTIRELY, and it must come first: the race decode below
       reads the BBC frame buffer $5A80-$7AFF, which out of a race holds nothing it should be
       drawing, while $7C00-$7FFF holds the teletext page instead of the dashboard overlay. */
    if (tt_active()) { decodeTeletext(); return; }

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

#ifdef REVS_SPAN_OWN
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
        const uint32_t* const grp = (const uint32_t*)(const void*)g_plotOwn;
        unsigned owned = 0;
        for (unsigned q = 0; q < kH / 4u; q++) {
            if (!grp[q]) continue;
            const unsigned y0 = q * 4u;
            for (unsigned y = y0; y < y0 + 4u; y++)
                if (g_plotOwn[y]) { m_lineMode[y] = 0; owned++; }
        }
        g_decodeOwnLines = owned;
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
#ifdef REVS_NO_DIRTY
    const unsigned cells = convertRace(dst, 0, 0);
#else
    const unsigned cells = convertRace(dst, (uint8_t*)s_shadow[m_back], s_shadowMode[m_back]);
#endif
#ifdef REVS_DECODE_SPLIT
    PROBE_PHASE(PROBE_PHASE_DECODE);   /* the remainder goes back on the original row */
#endif
    g_decodeCells       = (uint16_t)cells;
    g_decodeCellsTotal += cells;
    if (cells > g_decodeCellsMax) g_decodeCellsMax = (uint16_t)cells;
    if (cells >= BBC_SCREEN_ROWS * BBC_SCREEN_CELLS) g_decodeFullFrames++;

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
    REVS_PLOT_RECTS();
#ifdef REVS_DECODE_SPLIT
    PROBE_PHASE(PROBE_PHASE_DECODE);
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
}
