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
#include "../../cpu/m68k_math.h"   /* the 68000 has NO 32-bit mul/div (make muldiv-audit) */

extern "C" volatile uint8_t mem[65536];

/* ---- display geometry ---------------------------------------------------------
   320x208, two bitplanes, interleaved.  kDisplayTop is the raster line the display
   window starts on, so a BBC display line L is Amiga raster line kDisplayTop + L.
   ⚠ Must agree with the DIWSTRT/DIWSTOP PlatformAmiga::run programs. */
static const uint16_t kW          = BBC_SCREEN_WIDTH;
static const uint16_t kH          = BBC_SCREEN_HEIGHT;
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

/* ---- MODE 5 nibble expansion --------------------------------------------------
   A MODE 5 byte holds four pixels; the high nibble is those four pixels' HIGH bits and
   the low nibble their LOW bits (bbc_screen.h).  So each nibble is already one bitplane's
   four pixels and only has to be doubled to Amiga pixel width.  Two 256-entry tables
   rather than one 16-entry one indexed twice: on a 68000 that is two indexed byte loads
   with no masking or shifting in the inner loop, which is the whole cost of this pass.
   ⚠ Built in initialize(), never lazily — a table built on first use inside a frame is
   the Atari port's 3.6-second freeze (docs/m68k-optimisation.md). */
static uint8_t s_expandHi[256];
static uint8_t s_expandLo[256];

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
         DDF: lores fetch of kW/8 = 40 words starts at 0x38 and stops at 0x38 + 4*(40-1)/2,
       i.e. 0xD0 — the standard 320-pixel pair.
       ⚠ DIWHIGH is deliberately NOT written, and the framework's AmigaHardware::setPlayfield
       deliberately NOT called for this: it hard-codes a DIWHIGH whose VSTOP-high bit belongs
       to the Atari port's 276-line window.  Every field below fits the OCS-compatible 8-bit
       encoding (VSTRT 44, VSTOP 252, HSTRT 0x81, HSTOP 0xC1 + the implicit 256), so the high
       register has nothing to add here and writing that value would push VSTOP off the frame. */
    *diwstrtPointer = (uint16_t)((kDisplayTop << 8) | 0x81);
    *diwstopPointer = (uint16_t)((((kDisplayTop + kH) & 0xFF) << 8) | 0xC1);
    *ddfstrtPointer = 0x0038;
    *ddfstopPointer = (uint16_t)(0x0038 + 2 * ((kW / 16) - 1));

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
}

void RevsScreen::shutdown()
{
    delete m_copper;      m_copper     = 0;
    delete m_bitmap[0];   m_bitmap[0]  = 0;
    delete m_bitmap[1];   m_bitmap[1]  = 0;
    delete m_nullSprite;  m_nullSprite = 0;
}

/* ---------------------------------------------------------------------------
   VBI: the band record -> copper palette bands + the per-line mode table.
   --------------------------------------------------------------------------- */
void RevsScreen::buildBands()
{
    /* The record must be the game's five bands, identified by the state it wrote them
       under ($4F43): 0,1,2,3 and $FF for the last.  Anything else and the previous
       frame's list stands — see g_bandRejects. */
    if (g_bandCount != 5) { g_bandRejects++; return; }
    unsigned slot[5];
    for (unsigned i = 0; i < 5; i++) slot[i] = 0xFFu;
    for (unsigned i = 0; i < 5; i++) {
        unsigned st = g_bandState[i];
        if (st == 0xFFu) st = 4;
        if (st > 4 || slot[st] != 0xFFu) { g_bandRejects++; return; }
        slot[st] = i;
    }

    /* ⚠ THE LATCH IS PIPELINED (bbc_screen.h): the duration recorded against band n is
       band n+1's, because the 6522 reloads T1 from the latch only at the NEXT timeout.
       So the interval that starts at band n is the one recorded against band n-1. */
    int startUs = (int)BBC_BAND0_ANCHOR_US;
    uint32_t* d = m_copper->data();
    unsigned emitted = 0;

    for (unsigned n = 0; n < 5; n++) {
        unsigned rec  = slot[n];
        unsigned prev = slot[n ? n - 1 : 4];   /* no %: it links __umodsi3 */
        /* us -> display lines, rounded to the nearest.  A shift, not a divide: the
           68000 has no 32-bit divide, and >> on a negative is the floor, which is what
           the pre-display band 0 wants anyway. */
        int nextUs    = startUs + (int)g_bandDuration[prev];
        int lineStart = (startUs + (int)BBC_US_PER_LINE / 2) >> 6;
        int lineEnd   = (nextUs  + (int)BBC_US_PER_LINE / 2) >> 6;

        /* The line-mode table for the part of this band that is on screen. */
        int a = lineStart < 0 ? 0 : lineStart;
        int b = lineEnd > (int)kH ? (int)kH : lineEnd;
        unsigned mode = (g_bandControl[rec] == BBC_ULA_MODE4) ? 4u : 5u;
        for (int y = a; y < b; y++) m_lineMode[y] = (unsigned char)mode;

        /* The colours.  A band starting at or before the first displayed line owns the
           list header (no WAIT); the later ones get a WAIT at the end of the previous
           line.  A band starting past the bottom of the display is dropped — its palette
           is never seen, and the cumulative record means the next band that IS seen
           already carries every entry it did not overwrite. */
        uint16_t at = (uint16_t)((lineStart <= 0) ? IDX_TOPPAL
                                                 : IDX_BANDS + emitted * BAND_WORDS);
        if (lineStart > 0) {
            if (lineStart >= (int)kH || emitted >= MAX_BANDS) { startUs = nextUs; continue; }
            d[at++] = copperWait(kDisplayTop + lineStart - 1, 0xE0);
            emitted++;
        }
        for (unsigned pen = 0; pen < 4; pen++)
            d[at + pen] = copperMove(color00 + (pen << 1),
                                     bbcColour(g_bandPalette[rec][kLogicalForPen[pen]]));

        startUs = nextUs;
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
}

void RevsScreen::vbiUpdate()
{
#ifdef REVS_SCREEN_NO_BANDS
    return;
#endif
    if (!m_copper) return;
    buildBands();
    present();
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
    Bitmap* bm = m_bitmap[m_back];
    if (!bm) return;

    /* ⚠ NOT A WIDE-POINTER ALIAS OF mem[].  Every read below is a byte read; the two
       stores are bytes into a bitplane the Amiga reads as bits, so there is no multi-byte
       value whose order could differ between the host and the target (make endian-lint).

       Cost, honestly: 8320 source bytes, two table lookups and two stores each — tens of
       milliseconds on a 7 MHz 68000, i.e. several frames.  That is affordable ONLY because
       the engine currently spends ~700 ms per frame (docs/perf-method.md), and it is the
       obvious asm/dirty-region target the moment the engine stops dominating.  What it buys
       today is a faithful picture from the game's own frame buffer with no changes to the
       game's own code. */
    const uint8_t* base = (const uint8_t*)mem + BBC_SCREEN_BASE;
    uint8_t* dst = (uint8_t*)bm->data;

    unsigned y = 0;
    for (unsigned row = 0; row < BBC_SCREEN_ROWS; row++) {
        const uint8_t* rowBase = base + revs_mulu16((uint16_t)row, BBC_SCREEN_BPR);
        for (unsigned line = 0; line < BBC_SCREEN_LINES; line++, y++) {
            /* One character row is 40 cells of 8 bytes, one byte per scan line, so a
               single display line is 40 bytes with a stride of 8. */
            const uint8_t* s  = rowBase + line;
            uint8_t*       p1 = dst + revs_mulu16((uint16_t)y, kRowBytes);  /* plane 1 = bit 0 */
            uint8_t*       p2 = p1 + (kW / 8);          /* plane 2 = index bit 1 */

            if (m_lineMode[y] == 5) {
                for (unsigned i = 0; i < BBC_SCREEN_CELLS; i++) {
                    uint8_t b = *s; s += BBC_SCREEN_LINES;
                    *p1++ = s_expandLo[b];
                    *p2++ = s_expandHi[b];
                }
            } else {
                /* MODE 4: eight 1-bit pixels, straight into plane 2 so the set pixels
                   land on pen 2 = BBC logical colour 8, which is what the ULA's index
                   bit 3 selects. */
                for (unsigned i = 0; i < BBC_SCREEN_CELLS; i++) {
                    uint8_t b = *s; s += BBC_SCREEN_LINES;
                    *p1++ = 0;
                    *p2++ = b;
                }
            }
        }
    }
    m_ready = true;
}
