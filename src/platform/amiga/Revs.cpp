/* Revs — the Amiga application/scene.  See Revs.h; this is the bring-up skeleton. */
#include "Revs.h"
#include "PlatformAmiga.h"
#include "framework/AmigaHardware.h"
#include "framework/CopperList.h"
#include "../bbc_screen.h"
#include "../teletext.h"      // tt_tick_flash — the SAA5050 flash phase is a per-FIELD counter
#include "RevsAudio.h"        // the SN76489 the MOS drives, re-hosted on Paula

extern "C" volatile uint16_t g_vbiCount;
extern "C" volatile unsigned long g_fpsFrames;
extern "C" volatile uint8_t mem[65536];   // the 6502 RAM image (src/cpu/cpu.c)
extern "C" void engine_main(void);        // $63BD, the transpiled engine entry

// ⭐⭐ DOES THE 50 Hz BODY WRITE THE FILL CHAIN'S INPUTS?  (`make ISRWATCH=1`)
//
// The remaining artefact is a green or black run to the right edge of the horizon that lasts one
// PAINTED frame.  The game's renderer paints runs in a single pass and never clears (the plotter
// at $2F45 reads the destination and merges when it is non-zero), so a wrong byte survives until
// the game next paints that area — which is exactly one Revs frame.  Two inputs decide a run:
//
//   $3000-$43FF  the per-cell COLUMN SOURCES the $7C00 chain consumes (a zero means "same as the
//                previous cell", which is how A carries a colour along the line)
//   $7B00-$7FFF  the chain ITSELF — 42 patched opcode slots, one per span end ($91 STA vs $60 RTS)
//
// On a real BBC the T1 interrupt is raster-scheduled and cannot land where it would matter.  Here
// the whole band cycle plus the game body fires in one burst at the top of the frame, so it CAN
// preempt the main loop inside the chain.  If the body writes either range, that is the mechanism;
// if it writes neither, the hypothesis is dead and the search moves on.  Measured, not argued.
//
// ⚠ Costs ~11 KB of extra reads per field, so it is a build flag, not standing instrumentation.
// ⭐ MEASURED FIRST TIME ROUND: over 1809 fields the body wrote NEITHER range — so a preempted
// column source and a preempted span-end opcode are both out.  The watch then widened, because the
// renderer is full of SELF-MODIFIED OPERANDS and they are all in the engine's own code region:
// $193E opens with `STA $1970`, patching the destination of the `STA $0400,Y` at $196F, and $19AF
// patches $2F4F/$2F50/$2F91/$2F92 — the operand bytes of the span plotters' own stores.  Preempt
// the main loop between a patch and the loop that uses it and the resumed loop writes a run of the
// right colour to the WRONG address, or the wrong colour to the right one.
//
// So the range is now the whole engine image below the frame buffer, by 256-byte block, and the
// answer is read against disasm/listing.txt: a block that is code and moves during the body is a
// shared SMC site.  ⚠ Sampled every 8th field — 37 KB of extra reads is over half the CPU at 50 Hz
// and the question is "ever", not "how often".
#ifdef REVS_ISRWATCH
// ⭐⭐ THE RANGE MATTERS, and the first two versions of this had it wrong: the game's live
// variables — INCLUDING the road edge lists at $5E40/$5E90/$5F20 that the rasteriser draws from —
// live INSIDE the frame buffer, in the sky region ($5E40-$66FF, invisible because band 1 maps all
// sixteen palette entries to the same blue).  A watch that stopped at the frame buffer's base was
// blind to exactly the state the artefact is about.  So: $1200 up to the end of the screen.
#define ISRWATCH_LO    0x1200u
#define ISRWATCH_BLKS  105u                  // $1200-$7AFF: engine image AND the frame buffer
extern "C" {
volatile unsigned long g_isrWroteSrc = 0;    // fields in which the body wrote $3000-$43FF
volatile unsigned long g_isrWroteOvl = 0;    // ...or the $7B00-$7FFF overlay code
volatile unsigned long g_isrWroteCode = 0;   // ...or anything in $1200-$59FF
volatile unsigned long g_isrSamples = 0;
volatile uint8_t  g_isrSrcBlocks[20] = {0};  // which 256-byte block of the sources, ever
volatile uint8_t  g_isrOvlBlocks[5]  = {0};
volatile uint8_t  g_isrCodeBlocks[ISRWATCH_BLKS] = {0};
/* ⭐⭐ AND ZERO PAGE, BYTE BY BYTE — the range the first two passes did not cover and the one that
   matters most.  The road plotters keep ALL their live state there: the destination pointers
   ($70-$73), the last column and step ($82/$83), the pixel bytes ($85/$8A/$8B), the cursors
   ($12/$1F/$50/$51).  If the 50 Hz body writes any of those, then an interrupt taken inside the
   plotter resumes it with a different destination or a different end column — one display line
   whose fill stops early or runs long, which is EXACTLY what the reported artefact is (measured
   from the user's screenshot: a single line green where the lines above and below are black).
   ⚠ On a real BBC the same body writes the same zero page, so this is not "the game is broken":
   there the main loop is vsync-locked, so the band-4 interrupt lands at the SAME point in the
   drawing sequence every field.  Here a frame takes ~50 fields, so it lands everywhere. */
volatile uint8_t  g_isrZpWrites[256] = {0};
volatile unsigned long g_isrZpFields = 0;
}

static uint16_t blockSum(unsigned addr)
{
    uint16_t s = 0;
    for (unsigned i = 0; i < 256; i++) s = (uint16_t)(s + mem[addr + i] + i);
    return s;
}
#endif

// ⭐ Fields counted by the ISR, run by drainTicks() from main-loop context.  volatile: the ISR
// writes it and the main loop reads it, and 8 bits so a 68000 load/store of it is atomic — a
// 16-bit counter would need the interrupt masked around the decrement.
static volatile uint8_t s_pendingTicks = 0;
// Re-entrancy guard: the body itself can reach a spin-wait hook (that is what drives frames), and
// draining from inside the body would run the 50 Hz chain nested inside itself.
static bool s_inBody = false;
// ⭐ The port's own front end (the circuit menu) is up: count no fields.  volatile — the ISR reads
// it, main-loop context writes it.  See Revs.h §setFrontEnd for why this is not "pause the game".
static volatile bool s_frontEnd = false;
extern "C" {
volatile unsigned long g_bodyTicksDropped = 0;  // the cap hit: game time slowed, and we say so
volatile unsigned long g_bodyDrains       = 0;  // drain calls that ran at least one tick
volatile unsigned long g_bodyTicks        = 0;  // ticks run in total
volatile uint8_t       g_bodyPending      = 0;  // ...and what is queued right now
}

void Revs::runBandCycle()
{
    // ⚠ Bounded, because an unbounded loop over a state machine the game can change is how a
    // frame gets eaten.  8 = the five real bands plus slack; overrunning drops the rest of this
    // field's bands rather than hanging.
    bbc_begin_band_cycle();
    for (int band = 0; band < 8; band++) {
        platform->fireIrq1v();
        if (mem[0x4F43] == 0) break;      // $4F43 = irq_band_state; 0 = cycle complete
    }
}

void Revs::setFrontEnd(bool on)
{
    s_frontEnd = on;
    /* Whatever slipped through before the flag was seen is not the game's time either. */
    if (on) s_pendingTicks = 0;
}

void Revs::discardPendingTicks()
{
    s_pendingTicks = 0;
    g_bodyPending  = 0;
}

void Revs::drainTicks()
{
    if (s_inBody) return;
    s_inBody = true;
    unsigned ran = 0;
    while (s_pendingTicks) { s_pendingTicks--; runBandCycle(); ran++; }
    s_inBody = false;
    if (ran) { g_bodyDrains++; g_bodyTicks += ran; }
    g_bodyPending = s_pendingTicks;
}

void Revs::initialize()
{
    // ⚠ Install the copper list while the copper is halted (display DMA is off at this
    // point — PlatformAmiga::run() guarantees it).  Installing into a running copper is
    // how the Atari port lost its one-time register setup to stray OS-copper frames.
    screen.initialize();
    if (screen.copper()) AmigaHardware::setCopperList(*screen.copper());
    // Paula and the chip-RAM waveforms.  Before the first VERTB, because revs_audio_vbi() is
    // what ticks the MOS's sound scheduler and it does nothing until this has run.
    revs_audio_init();
}

void Revs::shutdown()
{
    revs_audio_shutdown();
    screen.shutdown();
}

void Revs::render()
{
    // ⭐ DRAIN THE PENDING 50 Hz TICKS HERE FIRST, before a single pixel is read.  This is the
    // engine's own frame hook ($1701, the top of the main loop): the previous iteration's drawing
    // is finished and this iteration's has not started, so it is one of exactly two points where
    // running the game body cannot change the scene underneath either the rasteriser or the decode.
    // (The other is the frame-wait spin at $1760 — PlatformAmiga::tickVBI.)
    drainTicks();

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

    // ⭐ The teletext flash phase, and it belongs HERE because it is measured in DISPLAY FIELDS,
    // not game frames: on the SAA5050 the flash is generated by the chip from the field rate, so
    // it must keep running at 50 Hz whatever the port's framerate is doing.  Three instructions.
    // ⚠ Without this the phase is stuck at 0 and the front end's flashing "PRESS" prompt is
    // simply never drawn — which looks exactly like a missing glyph rather than a missing tick.
    tt_tick_flash();

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

    // ⭐ SOUND, and it belongs here for the same reason as the flash phase above: the MOS
    // schedules sound on the System VIA's 100 Hz timer, NOT on vsync, so it has to keep its own
    // rate whatever the port's framerate is doing — two ticks per field.  It is also why this sits
    // AFTER the copper work and BEFORE the body: a waveform switch busy-waits ~7 rasterlines for
    // Paula's DMA restart (RevsAudio.h), and nothing that waits on the beam may precede the
    // copper writes (docs/amiga-lessons.md).
    revs_audio_vbi();

    // ⚠ Bounded, because Rule 5 caps ISR work at one frame and an unbounded loop over a
    // state machine the game can change is how an ISR eats every frame.  8 = the five
    // real bands plus slack; overrunning it drops the rest of this frame's bands rather
    // than the whole display.
#ifdef REVS_ISRWATCH
    static uint16_t code0[ISRWATCH_BLKS], ovl0[5];
    const bool watch = ((g_vbiCount & 7u) == 0u);
    if (watch) {
        for (unsigned b = 0; b < ISRWATCH_BLKS; b++) code0[b] = blockSum(ISRWATCH_LO + (b << 8));
        for (unsigned b = 0; b < 5; b++)             ovl0[b]  = blockSum(0x7B00 + (b << 8));
    }
    /* Zero page is cheap enough to check EVERY field, and it is the interesting one. */
    static uint8_t zp0[256];
    for (unsigned i = 0; i < 256; i++) zp0[i] = mem[i];
#endif

    /* ⭐ NOTHING BELOW HERE WHILE THE CIRCUIT MENU IS UP — see Revs.h §setFrontEnd.  It guards
       both models: under BODY_IN_ISR the body would RUN, before engine_main has initialised a
       thing, which is worse than queueing it. */
    if (s_frontEnd) return;

#ifdef REVS_BODY_IN_ISR
    /* The old model, kept for A/B measurement only: run the body here, in the ISR, where it
       preempts the main loop's drawing at an arbitrary point.  `make BODY_IN_ISR=1` restores it,
       and amiga/fill_catch.gdb then shows the edge jumps and the decode mismatches come back. */
    Revs::runBandCycle();
#else
    /* ⭐ COUNT the field; do not run the body here.  See Revs.h for the measurement behind this.
       Capped: if the main loop ever stops reaching a drain point, ticks must not accumulate
       without bound — dropping them slows game time, which is visible and debuggable, where an
       unbounded counter would eventually run thousands of ticks in one burst. */
    if (s_pendingTicks < 200u) s_pendingTicks++;
    else                       g_bodyTicksDropped++;
#endif

#ifdef REVS_ISRWATCH
    if (watch) {
        unsigned hitSrc = 0, hitOvl = 0, hitCode = 0;
        for (unsigned b = 0; b < ISRWATCH_BLKS; b++) {
            if (blockSum(ISRWATCH_LO + (b << 8)) == code0[b]) continue;
            hitCode++;
            g_isrCodeBlocks[b] = 1;
            /* $3000-$43FF is the column-source array, inside this range: keep the two answers
               separate, because "the body wrote a source" and "the body wrote code" are
               different findings. */
            const unsigned addr = ISRWATCH_LO + (b << 8);
            if (addr >= 0x3000u && addr < 0x4400u) { hitSrc++; g_isrSrcBlocks[(addr - 0x3000u) >> 8] = 1; }
        }
        for (unsigned b = 0; b < 5; b++)
            if (blockSum(0x7B00 + (b << 8)) != ovl0[b]) { hitOvl++; g_isrOvlBlocks[b] = 1; }
        if (hitSrc)  g_isrWroteSrc++;
        if (hitOvl)  g_isrWroteOvl++;
        if (hitCode) g_isrWroteCode++;
        g_isrSamples++;
    }
    {
        unsigned zpHits = 0;
        for (unsigned i = 0; i < 256; i++)
            if (mem[i] != zp0[i]) { g_isrZpWrites[i] = 1; zpHits++; }
        if (zpHits) g_isrZpFields++;
    }
#endif
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
