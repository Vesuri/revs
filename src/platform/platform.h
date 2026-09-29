#ifndef PLATFORM_H
#define PLATFORM_H

#if !defined(REVS_PLATFORM_AMIGA)
#include <cstdint>   /* Host build.  The Amiga build has no libstdc++ and gets the
                        integer types from the force-included framework/SASCCompat.h. */
#endif

#include "platform_c.h"   /* MosRegs, and the C bridge the transliteration reaches us through */

/* Abstract platform base — one concrete subclass per target (SDL host, Amiga).
   The global singleton pointer is used by the C bridge layer so that the
   C-compiled 6502 transliteration can reach hardware emulation.

   ⚠ KEEP THIS INTERFACE GENERIC.  On the Atari port this class accumulated a
   dozen game-specific "region X changed" notifications (titleChanged,
   compassChanged, lockonChanged, cockpitDirty, …).  They earned their place —
   each one removed a full-screen re-decode — but they belong in the CONCRETE
   backend's own header, not in the portable base.  When Revs needs a dirty-region
   hook, prefer a single `regionDirty(addr, nCells)` over one method per widget. */

class Platform {
public:
    Platform();
    virtual ~Platform();

    /* ------------------------------------------------------------------ */
    /* Lifecycle                                                          */
    /* ------------------------------------------------------------------ */

    /* Run the game.  Each platform sets up whatever it needs (interrupts,
       display, signal handlers) and then drives the genuine entry chain.
       main() constructs the concrete PlatformClass and calls this; it returns
       when the user quits. */
    virtual void run() = 0;

    /* ------------------------------------------------------------------ */
    /* Frame / interrupt                                                  */
    /* ------------------------------------------------------------------ */

    /* Register the 50 Hz vsync handler (the BBC's System VIA CA1 vsync IRQ on
       the host; the real INTB_VERTB server on the Amiga). */
    virtual void setInterrupt(void (*fn)(void)) = 0;

    virtual int  framesPerSecond() = 0;

    /* Present the offscreen buffer. */
    virtual void renderFrame() = 0;

    /* Pump the OS event loop without rendering — keeps the window manager from
       marking the app unresponsive during spin-waits. */
    virtual void pollEvents() {}

    /* Fire one vsync tick if enough time has accumulated.  Call only from
       frame-boundary spin-waits, never from a raster-position wait (which the
       tick would reset, so the wait could never exit). */
    virtual void tickVBI() {}

    /* ------------------------------------------------------------------ */
    /* Hardware bus — called by bus_read / bus_write in bus.h              */
    /* ------------------------------------------------------------------ */

    /* Read / write a BBC I/O address ($FC00-$FEFF).  Implemented ONCE for both backends
       in src/platform/bbc_hw.cpp — the two VIA flag bits Revs blocks on are the machine,
       not a platform choice.  A backend overrides only to add a display/audio
       consequence, and calls the base first. */
    virtual uint8_t hwRead(uint16_t addr);
    virtual void    hwWrite(uint16_t addr, uint8_t val);

    /* Dispatch the game's own 50 Hz body (irq1v_band_schedule, $4E5C) exactly as a 6502 IRQ
       would — raising the User VIA T1 flag it checks and pushing the P byte its closing
       RTI pulls.  Call this from the backend's vblank, never irq1v_band_schedule directly.
       See bbc_hw.cpp for what goes wrong otherwise. */
    void fireIrq1v();

    /* ⭐⭐ ONE WHOLE FIELD of the band cycle, and the fast path that usually skips it.
       The five interrupts produce a RECORD, not a picture — the copper executes the raster
       splits — and the record is a pure function of five palette tables plus the horizon,
       which the main loop moves about once every 25 fields at this framerate.  So when the
       inputs are unchanged the entire cycle is a no-op apart from $52A4, and this runs that
       alone.  Measured cost of the full cycle: 6.1 ms of a 20 ms field, 96% of it machinery.
       Returns the number of irq1v_band_schedule dispatches it actually made (0 on the fast path).
       `make BANDSKIP=0` is the control — see bbc_hw.cpp for the faithfulness argument. */
    unsigned fireIrq1vField();

    /* Notification that the game wrote an OS vector / page-2 cell
       ($0200-$02FF) — IRQ1V, EVNTV, BRKV etc. */
    virtual void    shadowWrite(uint16_t addr, uint8_t val);

    /* Register a BBC address → C function mapping for interrupt dispatch
       (the game's own IRQ1V / EVNTV handler bodies). */
    virtual void registerVBI(uint16_t /*addr*/, void (* /*fn*/)(void)) {}

    /* Runtime indirect JMP/JSR dispatch (JMP ($xx) vector patterns).  ⚠ On a
       6502 binary-only port this is load-bearing: the entry-point sweep
       (docs/entrypoint-sweep.md) enumerates every such site up front. */
    virtual void indirectJmp(uint16_t addr) { (void)addr; }

    /* A BRK executed at `pc` — a software interrupt through BRKV, not a no-op.  See
       platform_c.h; the default implementation reports it the same way smcUnhandled does. */
    virtual void brk(uint16_t pc);

    /* A self-modifying instruction held a value the transpiler's SMC_SITES table does
       not cover (an unlisted opcode byte, or a patched branch offset pointing outside
       the enclosing routine).  The generated C cannot express what the 6502 would
       execute next, so this is a BUG REPORT, not an event to absorb: the default
       implementation is deliberately noisy, and a backend must not quietly ignore it.
       A silent no-op here reads exactly like a rasteriser that runs and draws nothing.
       docs/transpiler.md §Self-modifying code. */
    virtual void smcUnhandled(uint16_t site, uint16_t value);

    /* ------------------------------------------------------------------ */
    /* MOS calls                                                          */
    /* ------------------------------------------------------------------ */

    /* Service an intercepted MOS entry ($FFCE-$FFF7).  `entry` is the ROM
       address JSR'd; A/X/Y come from the cpu struct and are updated in place.
       Unlike the Atari (whose OS the port simply replaced), Revs runs under the
       MOS: OSBYTE/OSWORD are how it reads the keyboard, the ADC and the disc.

       ⚠ NOT virtual-per-backend by design.  src/platform/mos.cpp implements the
       whole closed surface (4 entries / 17 sites / 8 OSBYTE reason codes, enumerated
       in Phase 2) once, so the host and the target can never disagree about the OS
       itself.  A backend supplies only the five genuinely platform-specific answers
       below.  See docs/bbc-hardware.md §MOS calls.

       ⭐ CPU-FREE BOUNDARY.  The register file crosses as a MosRegs value (in and
       out), never through the global cpu struct — the two bridge functions in
       platform_c.h marshal cpu at the edge for the generated corpus.  This lets a
       native twin's typed wrapper (revs_native_seam.h) issue an OS call without
       touching cpu. */
    virtual MosRegs mosCall(uint16_t entry, MosRegs in);

    /* --- what a backend must answer for the MOS layer --------------------- */

    /* OSBYTE 129 negative INKEY: is the key whose internal number is -(256-x)
       currently held?  `x` is the raw X register, i.e. the 256-n form stored in
       menu_key_tbl ($39E0).  Default: nothing is held. */
    virtual bool keyDown(uint8_t x);

    /* OSBYTE 128 with X=0 — the fire-button/last-channel word.  Only bit 0 is
       ever read (by $168E).  Default: no buttons. */
    virtual uint8_t adcButtons();

    /* OSBYTE 128 with X=1..4 — ⭐ the steering axis.  A full 16-bit uPD7002
       conversion; the game uses only the high byte, biased so $80 is centre.
       Default: $8000, dead centre. */
    virtual uint16_t adcAxis(uint8_t channel);

    /* OSRDCH — one blocking site ($6316, the driver-name line editor).  Must
       always return a real character: signalling ESCAPE instead loops forever.
       Default: CR, which ends the line immediately. */
    virtual uint8_t rdch();

    /* OSBYTE $15 with X=0 — flush the keyboard buffer.  console_io ($6311) does this
       before every field so a keypress left over from the menu cannot be typed into it.
       A no-op only for a backend whose rdch() is not buffered.  Default: nothing. */
    virtual void flushKeyboard();

    /* OSWRCH — VDU output byte.  Default: discarded. */
    virtual void wrch(uint8_t c);

    /* ------------------------------------------------------------------ */
    /* Image loading                                                      */
    /* ------------------------------------------------------------------ */

    /* Load the post-load memory image into mem[].  Returns 0 on OK. */
    virtual int loadImage(const char* path) = 0;

    /* ------------------------------------------------------------------ */
    /* Shared state                                                       */
    /* ------------------------------------------------------------------ */
    bool quit;

    /* ⭐⭐ ELAPSED MICROSECONDS — the clock behind the User VIA's T2 counter ($FE68), which is
       Revs's ONLY source of entropy (docs/bbc-hardware.md §The T2 counter).  It must be a
       CLOCK, not a counter of calls: T2's whole value to the game is that it is decorrelated
       from game code because it runs off the 1 MHz bus, so a value derived from how often the
       game asked would reintroduce exactly the correlation the game is buying its way out of.
       The unit is microseconds; only the low 8 bits of the derived counter are ever observed,
       so wrapping is fine and the origin is arbitrary.
       Default: a field-counted approximation, correct in rate but coarse — a backend with a
       finer clock (a real timer, the beam position) should override it. */
    virtual uint32_t hwMicros();

    /* ⭐⭐ THE SIMULATION CLOCK's two inputs (race_main_loop_core §THE SIMULATION CLOCK,
       docs/faithfulness-seam.md §THE FRAME-RATE-INDEPENDENT SIMULATION).
       simStepTenths — the game time one simulation step covers, in tenths of a millisecond;
       0 selects LEGACY mode (one step per painted frame, the engine's own loop), which is the
       default and what every determinism gate runs.
       simFields — display fields elapsed since the previous call: the wall clock the steps are
       owed against.  Only read when simStepTenths is non-zero. */
    virtual unsigned simStepTenths() { return 0u; }
    virtual unsigned simFields()     { return 0u; }

protected:
    /* Has a display frame boundary passed since the last call?  Backs the System VIA
       vsync flag ($FE4D bit 1) that hw_init's alignment spin blocks on.  Default: yes
       every time (a headless backend has no raster to align to). */
    virtual bool vsyncElapsed();

    /* User VIA T1 timeout latch ($FE6D bit 6) — raised by fireIrq1v(), cleared when the
       handler acknowledges it.  Owned by bbc_hw.cpp. */
    bool m_userT1Pending = false;
};

extern Platform* platform;

#endif /* PLATFORM_H */
