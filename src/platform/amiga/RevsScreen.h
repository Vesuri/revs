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

class RevsScreen {
public:
    /* Allocate the two bitmaps and the copper list, and emit the fixed part of the list.
       Call with display DMA off (PlatformAmiga::run guarantees it). */
    void initialize();
    void shutdown();

    CopperList* copper() const { return m_copper; }

    /* Main-loop context: BBC frame buffer -> the back buffer.  The expensive one. */
    void decode();

    /* VBI context only.  Turns the band record the game just wrote (bbc_hw.cpp) into the
       copper's palette bands and per-row mode table, then presents the finished back
       buffer by swapping the bitplane pointers. */
    void vbiUpdate();

private:
    void buildBands();
    void present();

    /* ⚠ NO member initialisers, and no constructor: the scene that owns this is a
       function-local `static Revs` in a -nostdlib freestanding build, and a member whose
       initialisation is not constant makes the compiler emit __cxa_guard_acquire, which
       does not exist here (the link fails).  Static storage is zero-initialised, and
       initialize() sets everything that must not start at zero. */
    Bitmap*     m_bitmap[2];
    CopperList* m_copper;
    unsigned    m_back;               /* index of the buffer decode() writes */
    bool        m_ready;              /* the back buffer holds a finished frame */

    /* Which BBC mode each display line is in, from the band record.  Written in the VBI,
       read by decode() in the main loop — a one-frame-stale row is invisible (it changes
       only when the horizon moves) and it keeps the expensive loop branch-free per row. */
    unsigned char m_lineMode[208];
};
