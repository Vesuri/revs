#pragma once
/* RevsScreen — the BBC display, re-hosted on Amiga bitplanes + a copper list.
 *
 * ⭐ WHAT THIS IS A PORT OF.  The BBC side is ONE static 6845 mode (40x26 character cells
 * of 8 lines at $5A80, 208 lines) whose pixel depth and palette are rewritten five times
 * per field by the game's own timer interrupt.  src/platform/bbc_screen.h derives all of
 * that and is the reference for every constant here; read it first.
 *
 * The mapping:
 *
 *   BBC frame buffer, 40 bytes/line   ->  320x208, TWO bitplanes, interleaved
 *   MODE 5 byte (4 px, 4 colours)     ->  one byte per plane: the byte's high nibble IS
 *                                        plane 2's four pixels and the low nibble IS
 *                                        plane 1's, each doubled to two Amiga pixels
 *   MODE 4 byte (8 px, 2 colours)     ->  the byte into plane 2, zero into plane 1
 *   the five ULA palette bands        ->  copper WAIT + four COLORxx MOVEs each
 *
 * ⭐ Both modes land on the SAME four colour registers, so the copper never touches
 * BPLCON0 and the mode is purely a decode choice:
 *
 *   COLOR00 = BBC logical 0   COLOR01 = logical 2   COLOR02 = logical 8   COLOR03 = logical 10
 *
 * because a MODE 5 pixel's two bits become index bits 3 and 1 (bbc_screen.h), and a MODE 4
 * pixel's single bit becomes index bit 3 — which is COLOR02 with plane 1 zeroed.  That is
 * why the game's palette tables come in groups of four, and it is what lets one 2-bitplane
 * display be faithful to both modes at once.
 *
 * ⚠ DOUBLE BUFFERED, and it has to be: a frame takes far longer than 20 ms to decode
 * (baseline 1.4 FPS), so decoding into the live buffer would show the beam crossing a
 * half-drawn screen.  The POINTER swap happens in the VBI and nowhere else — a torn
 * bitplane pointer garbages the whole viewport for a frame (docs/amiga-lessons.md).
 */
#include "framework/Util.h"

class Bitmap;
class CopperList;
class Sprite;

class RevsScreen {
public:
    /* Allocate the two bitmaps and the copper list, and emit the fixed part of the list.
       Call with display DMA off (PlatformAmiga::run guarantees it). */
    void initialize();
    void shutdown();

    CopperList* copper() const { return m_copper; }

    /* Main-loop context: BBC frame buffer -> the back buffer.  The expensive one. */
    /* ⭐⭐⭐ ONCE PER PAINTED FRAME, and it is NOT a decode any more — it was renamed when the
       frame-buffer conversion stopped running per frame (RevsScreen.cpp at `own_has_gap`).  What
       it does now is: snapshot the game's raster band schedule and turn it into the COPPER's
       palette plan, run the painters that own the dashboard rows, lay down the cockpit's
       playfield — and expand `mem[]` only on a frame where some display line has no painter,
       which is the cold frames after the front end and nothing else. */
    void prepareFrame();

    /* ⭐ The conversion, cell by cell, DIRTY-REGION by default: only cell columns whose eight
       source bytes differ from `shadow` are converted, because only 4.9% of the frame buffer
       changes per painted frame (measured — docs/direct-bitplane-plan.md §7b).  Pass
       shadow/shadowMode null to convert everything; that reference pass is `make DIRTY=0` and
       the REVS_DIRTYCHECK oracle.  Returns cell columns converted, of 1040. */
    /* ⚠ `unsigned char*`, not `uint8_t*`: this header does not pull in <stdint.h> and Util.h
       only defines the typedefs pre-C++11.  Same type on this target either way. */
    unsigned convertRace(unsigned char* dst, unsigned char* shadow, unsigned char* shadowMode);


    /* VBI context only.  Turns the band record the game just wrote (bbc_hw.cpp) into the
       copper's palette bands and per-row mode table, then presents the finished back
       buffer by swapping the bitplane pointers. */
    void vbiUpdate();

    /* Records the beam line at VERTB entry (g_beamEntry*, read by amiga/beam_watch.gdb).
       Two register reads: the ISR comment in PlatformAmiga.cpp asks for exactly this before
       theorising about ISR-side work, and it is what says whether the handler is being
       entered in the blank at all. */
    void noteVbiEntry();

private:
    /* One-time custom registers (FMODE / BPLCON1 / BPLCON2 / BPLCON3), CPU-written with the
       copper halted.  The rule and the reasoning are at the definition: constants go here,
       and only per-frame or per-band state goes in the copper list. */
    void setConstantRegisters();

    /* ── MODE 7, the front end ─────────────────────────────────────────────────────────────
       A SECOND display configuration: 320x250, THREE bitplanes (teletext has eight colours)
       and its own copper list, so the race view's two-plane list and its display DMA cost are
       untouched.  The reasoning is at the constants in the .cpp.  Which of the two is on
       screen follows tt_active() — set by the engine's own VDU 22,7 and cleared when hw_init
       programs the 6845 — because $7C00-$7FFF is the teletext page AND the dashboard code
       overlay, time-multiplexed. */
    void decodeTeletext();          /* main loop: the page -> the 3-plane bitmap */
#ifdef REVS_TT_CHECK
    void ttCheck();                 /* `make TTCHECK=1`: the whole page re-drawn the old way, compared */
    void ttSelfTest();              /* ...and its synthetic double-height pages, once */
#endif
    void buildTeletextCopper();     /* one-time: sprites, playfield, pointers, 8 colours */
    void setDisplayWindow(unsigned height);  /* DIWSTRT/DIWSTOP for 208 or 250 lines */
    /* VBI: hand the display to whichever mode the machine is in now.  Returns non-zero if it
       switched, in which case nothing else this field should touch the other list. */
    int  applyMode();
    /* Main-loop half: capture the band record for the frame being decoded, and turn it into
       the raster plan + the per-line mode table.  Both must describe the SAME game frame as
       the pixels — see the long note at snapshotBands(). */
    void snapshotBands();
    void buildLineModes();
    /* VBI half: the plan -> copper WAITs and COLORxx MOVEs, at the pointer swap. */
    void buildBands();
    void present();

    /* The band record for ONE game frame, copied out of bbc_hw.cpp's live globals (which the
       IRQ1V band cycle rewrites 50 times a second). */
    struct BandSnapshot {
        /* ⚠ FIRST, so it sits at an even address: snapshotBands copies it a longword at a time */
        unsigned char  palette[5][16];
        unsigned char  count;
        unsigned char  state[5];
        unsigned short duration[5];
        unsigned char  control[5];
    };
    /* The raster boundaries derived from it, in display lines, band-state order. */
    struct BandPlan {
        short         line[5];
        unsigned char rec[5];
        unsigned char valid;
    };

    /* ⚠ NO member initialisers, and no constructor: the scene that owns this is a
       function-local `static Revs` in a -nostdlib freestanding build, and a member whose
       initialisation is not constant makes the compiler emit __cxa_guard_acquire, which
       does not exist here (the link fails).  Static storage is zero-initialised, and
       initialize() sets everything that must not start at zero. */
    /* ⭐ SET LAST BY initialize(), AND THE ONLY THING vbiUpdate() MAY TRUST.  A non-null
       m_copper means "the object exists", not "the list has been written" — initialize()
       allocates ~40 lines before it fills either list in, and the VERTB handler is already
       running at 50 Hz by then.  A VERTB in that window used to install an EMPTY MODE 7 list
       and latch m_ttOnScreen, which is what left the target on an all-black race list for a
       whole run (PlatformAmiga::run has the full write-up). */
    unsigned char m_built;
    Bitmap*     m_bitmap[2];
#ifdef REVS_DUAL_PLAYFIELD
    /* ⚠ SINGLE buffered on purpose — that is the point of the layer.  The cockpit is the same
       pixels every frame apart from the mirrors and the gear readout, so there is nothing for a
       second buffer to hide, and a second buffer is exactly what made the car flicker when the
       view sweep claimed these rows: one copy had the body and the other never did. */
    Bitmap*     m_cockpit;
#endif
    CopperList* m_copper;
    /* MODE 7's own pair.  ⚠ SINGLE-buffered, deliberately: the page is static between
       keypresses, so it is redrawn CELL BY CELL only where it CHANGES (decodeTeletext), and a
       real BBC tears here too — the MOS writes screen RAM while the beam is scanning it.  A
       second 250-line 3-plane buffer would cost 30 KB of chip RAM to hide an artefact the
       original hardware shows. */
    Bitmap*     m_ttBitmap;
    CopperList* m_ttCopper;
    unsigned char m_ttOnScreen;      /* which configuration the display is set up for */
    unsigned char m_ttFlashSeen;     /* the flash phase the current picture was drawn in */
    /* ⭐ Which rows decodeTeletext() last drew double-height (bit y = row y).  The one piece of
       cross-row state the row-by-row decode needs: a row that STOPS being double-height hands its
       lower display row back to the row below, which may not itself be dirty, so it must be
       forced.  A row that STARTS double overwrites that display row with its own bottom half and
       needs nothing extra. */
    unsigned long m_ttRowDbl;
    /* One 8-byte all-zero sprite (VSTART == VSTOP == 0), pointed to by all eight channels
       so sprite DMA has somewhere harmless to go.  See initialize(). */
    Sprite*     m_nullSprite;
#ifdef REVS_TYRE_SPRITES
    /* ⭐ THE FRONT-WHEEL DITHER, as two precomputed states per side (RevsTyres.h).  Four images:
       [side][state].  Alternating the animation is a copper SPRxPT repoint and nothing else —
       no CPU touches a pixel of it after the build. */
    Sprite*     m_tyre[2][2];
    bool        m_tyreReady;
#endif
    unsigned    m_back;               /* index of the buffer decode() writes */
    bool        m_ready;              /* the back buffer holds a finished frame */

    /* ⭐ Both written in the MAIN LOOP at the top of decode(), read by the VBI only at the
       swap.  They used to be derived in the VBI from the live record, which meant the palette
       schedule tracked the CURRENT frame while the pixels came from one ~1.1 s older — and
       band 2's boundary is the horizon, so the two disagreeing put sky rows in the wrong
       band's palette (black instead of blue).  snapshotBands() has the measurement. */
    BandSnapshot m_bandSnap;
    BandPlan     m_plan;

    /* Which BBC mode each display line is in, from the band plan.  Written by
       buildLineModes() just before the decode loop that reads it, so the two cannot disagree
       within a frame, and the expensive loop stays branch-free per row.
       ⚠ `aligned(4)` is load-bearing, not a hint: buildLineModes fills this table a LONGWORD at
       a time (208 byte stores measured 1.64 ms/frame — probe.h §DECODESPLIT). */
    unsigned char m_lineMode[208] __attribute__((aligned(4)));

};
