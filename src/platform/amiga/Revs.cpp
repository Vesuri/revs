/* Revs — the Amiga application/scene.  See Revs.h; this is the bring-up skeleton. */
#include "Revs.h"
#include "PlatformAmiga.h"
#include "framework/AmigaHardware.h"
#include "framework/CopperList.h"
#include "../bbc_screen.h"

extern "C" volatile uint16_t g_vbiCount;
extern "C" volatile unsigned long g_fpsFrames;
extern "C" volatile uint8_t mem[65536];   // the 6502 RAM image (src/cpu/cpu.c)
extern "C" void engine_main(void);        // $63BD, the transpiled engine entry


void Revs::initialize()
{
    // ⚠ Install the copper list while the copper is halted (display DMA is off at this
    // point — PlatformAmiga::run() guarantees it).  Installing into a running copper is
    // how the Atari port lost its one-time register setup to stray OS-copper frames.
    screen.initialize();
    if (screen.copper()) AmigaHardware::setCopperList(*screen.copper());
}

void Revs::shutdown()
{
    screen.shutdown();
}

void Revs::render()
{
#ifdef REVS_FPSCOUNT
    // ⭐ The framerate numerator: exactly one increment per PAINTED frame, and nothing
    // else.  Keep it that way — the moment this build reads a chip register or multiplies,
    // it stops being the honest baseline.  (docs/perf-method.md)
    g_fpsFrames++;
#endif
    // ⭐ The BBC frame buffer -> the back bitplane buffer.  Main-loop context: the
    // POINTER swap that presents it happens in vbi(), never here.
    screen.decode();
}

void Revs::vbi()
{
    // ⭐ THE GAME'S 50 Hz BODY.  On the BBC this is a USER VIA T1 interrupt, not vsync
    // (docs/static-map.md §The interrupt) — and it is not one interrupt per frame.
    // irq1v_handler ($4E5C) is a RASTER-BAND STATE MACHINE: it walks irq_band_state
    // 0→1→2→3→4→0, rewriting the Video ULA mode and palette for each horizontal band of
    // the screen and reloading T1 ($FE66/$FE67) with the delay to the next one.  Only the
    // last band does the actual game work (FUN_52a4 at $4EF5).
    //
    // So one Amiga VERTB must drive a WHOLE band cycle, not one band: dispatch one band
    // per interrupt and the simulation would tick at 10 Hz while everything else looked
    // right.  perf-method.md is explicit that the 50 Hz sim tick is not negotiable.
    //
    // ⚠ APPROXIMATION, and a deliberate one: the bands all fire here at the top of the frame
    // instead of at their scheduled raster positions, so the palette WRITES collapse into one
    // point in time.  What keeps that faithful is that they are only a RECORD (bbc_hw.cpp
    // captures the T1 latch per band); the schedule itself is re-emitted as copper WAITs by
    // RevsScreen, which is where a raster-position palette change belongs.  So the collapse
    // costs nothing as long as the record is snapshot with the frame it describes.
    //
    // ⚠ Bounded, because Rule 5 caps ISR work at one frame and an unbounded loop over a
    // state machine the game can change is how an ISR eats every frame.  8 = the five
    // real bands plus slack; overrunning it drops the rest of this frame's bands rather
    // than the whole display.
    // Where is the beam right now?  Two register reads, and the answer decides whether the
    // present/band rebuild below can legally happen at all (RevsScreen.cpp, beamLine()).
    screen.noteVbiEntry();

    // ⭐⭐ THE COPPER WORK COMES FIRST, IN THE BLANK — and this ORDER is the whole point.
    //
    // "In the VERTB handler" and "in the vertical blank" are the same thing only while the
    // handler is shorter than the blank.  The game body below is not: the band cycle ends in
    // FUN_52a4, the 50 Hz simulation, and it runs for MILLISECONDS.  With vbiUpdate() after
    // it, every band rebuild and every bitplane-pointer swap landed at raster line 46..149 —
    // MEASURED, 49 of 49 presents inside the 44..251 display window (amiga/beam_watch.gdb).
    // The copper has already executed the words being rewritten by then, so a rebuilt WAIT
    // whose line is behind the beam blocks the copper until the NEXT field and every band
    // after it is skipped: the horizon keeps the previous band's pen 0 (BLACK) or pen 3
    // (GREEN) all the way down.  That is the "black or green fill run at the horizon" this
    // port was showing, and it is a raster race, not a fill bug.
    //
    // Presenting BEFORE the body costs one field of latency in a frame that takes ~50 of
    // them, and it puts the writes where the rule says they go: g_beamPresentsLate must stay
    // 0.  It is also why the band record has to be SNAPSHOT in decode() (snapshotBands) —
    // the record the loop below writes belongs to the frame the main loop has not decoded
    // yet, not to the pixels going up here.
    screen.vbiUpdate();

    // ⚠ Bounded, because Rule 5 caps ISR work at one frame and an unbounded loop over a
    // state machine the game can change is how an ISR eats every frame.  8 = the five
    // real bands plus slack; overrunning it drops the rest of this frame's bands rather
    // than the whole display.
    bbc_begin_band_cycle();
    for (int band = 0; band < 8; band++) {
        platform->fireIrq1v();
        if (mem[0x4F43] == 0) break;      // $4F43 = irq_band_state; 0 = cycle complete
    }
}

void Revs::run()
{
    // ⭐ THE GENUINE ENTRY CHAIN.  $63BD is the unpack stub's closing JMP target; the
    // engine never returns from it, so this call IS the game.  Frames are pumped from
    // inside it by the transpiler's hook at the top of the main loop ($1701 →
    // platform_render_frame), and quit is polled from the frame wait ($1760).
    //
    // ⚠ Not $1200: that is the loader stub, which overwrites itself.  And the image this
    // runs against is the POST-unpack one (docs/static-map.md).
    engine_main();

    // Reaching here means the engine returned, which it is not supposed to do.  Fall back
    // to pumping frames so the machine stays ours and the run is still readable from gdb
    // rather than dropping through into a restored-but-unentered OS.
    while (!platform->quit) {
        platform->renderFrame();
        platform->pollEvents();
    }
}
