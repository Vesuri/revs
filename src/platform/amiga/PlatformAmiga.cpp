/* PlatformAmiga — the Amiga backend's machine takeover.  See PlatformAmiga.h for the design rules.
 *
 * It takes the machine over, installs the real VERTB interrupt (the vector is taken over
 * wholesale — graphics.library's own server no longer runs, so WaitTOF() is unavailable),
 * runs Revs::run(), and restores everything.
 *
 * ⚠ This header used to read "day-one bring-up skeleton ... what it does NOT do yet: render
 * the game, emulate BBC hardware, service MOS calls, produce audio".  All four are done and
 * live elsewhere: RevsScreen (display), bbc_hw.cpp (the BBC hardware model behind
 * bus_read/bus_write), mos.cpp (the MOS call layer), RevsAudio + sound.cpp (the MOS sound
 * scheduler onto Paula).  This file is the takeover and the timebase, not a skeleton.
 */
/* ⚠ INCLUDE ORDER IS LOAD-BEARING.  framework/AmigaHardware.h #defines bare register
   names (bplcon0, vposr, dmaconr, …) as offsets, and those collide with the `struct Custom`
   MEMBERS in <hardware/custom.h> — a header the graphics includes pull in.  So: every
   system header FIRST, AmigaHardware.h LAST. */
#include <proto/exec.h>
#include <proto/graphics.h>
#include <exec/execbase.h>
#include <exec/interrupts.h>
#include <exec/nodes.h>
#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>

#include "framework/AmigaHardware.h"
#include "PlatformAmiga.h"
#include "Revs.h"
#include "../probe.h"   /* PROBE_VBI(): advance the phase-bracket beam epoch */
#include "../track.h"      /* circuit selection */
#include "../trackmenu.h"  /* ...and the menu that makes it the PLAYER's */

#include "../../cpu/mem_decl.h"
extern "C" MEM_QUAL uint8_t mem[65536];      // the 6502 RAM image (src/cpu/cpu.c)
extern "C" volatile uint8_t g_keyDown[128];  // RevsInput's rawkey state, for the quit chord
// revs_keys.h: may kbd_test_key answer in line?  Not in an autorun build until its script hands
// over, because every scripted answer must go through keyDown() below to reach the rawkey state.
#ifdef REVS_AUTORUN_BUILD
extern "C" { volatile uint8_t g_keyDirect = 0; }
#else
extern "C" { volatile uint8_t g_keyDirect = 1; }
#endif

// GfxBase is opened in the constructor (GCCRuntime.cpp defines the global).
extern struct GfxBase* GfxBase;

// Custom-register pointers (dmaconPointer, intenaPointer, diwstrtPointer, bplcon0Pointer,
// ciaapraPointer, …) all come from the framework's AmigaHardware.h as macros — do NOT
// redeclare them here.

// ---------------------------------------------------------------------------
// VBI state
// ---------------------------------------------------------------------------
static Revs* s_scene = 0;
static PlatformAmiga* s_platform = 0;   // for the ISR's mouse sampling

// ⚠⚠ EVERY PROBE COUNTER MUST BE LISTED IN amiga/Makefile's PROBE_SYMS.
//
// This build uses -fdata-sections + --gc-sections, so a counter that nothing in the CURRENT
// configuration reads or writes gets its whole section dropped at link time.  The symbol does not
// merely vanish: gdb then resolves the name into .text and prints INSTRUCTION BYTES as a value.
// Measured during scaffolding — a non-FPSCOUNT build reported `painted=1223110688` (0x48E73020,
// which is m68k code inside AmigaHardware::processBlitterQueue).  A zero would have read as "not
// counting"; garbage reads as a measurement.  This is docs/method-lessons.md's "if a probe has
// never fired, suspect the probe" in its worst form: the probe fired and lied.
//
// ⚠ `__attribute__((used, retain))` does NOT fix it — `retain` is IGNORED on this target
// (-Wattributes), and `used` only stops the compiler, not the linker.  The fix is a linker gc
// root: PROBE_SYMS in amiga/Makefile becomes -Wl,--undefined=<sym>, and `make probe-audit` fails
// the build if any listed symbol is missing from the ELF.  Zero runtime cost.
//
// Real 50 Hz PAL vblank counter.  ⭐ EVERY timing measurement in this port is
// denominated in this, never in host wall clock: it is immune to emulator speed and
// to the gdb stub.  (docs/perf-method.md)
extern "C" { volatile uint16_t g_vbiCount = 0; }
#ifdef REVS_TAPTEST
extern "C" void revs_input_tap_test(void);
#endif
extern "C" uint16_t platform_frame_count(void) { return g_vbiCount; }

// Painted-frame counter — the numerator of the ONLY honest framerate figure:
//     FPS = 50 * g_fpsFrames / g_vbiCount
// Defined in every build (so amiga/fps_seg.gdb can always read it) but only incremented
// under FPSCOUNT, which is otherwise a shipping binary.  Never quote a framerate from a
// PROBES build — the probes are what such a build exists to measure.  (docs/perf-method.md)
extern "C" { volatile unsigned long g_fpsFrames = 0; }

#ifdef REVS_FPSCOUNT
/* ⭐⭐ THE FRAMERATE SERIES, SAMPLED BY THE PROGRAM ITSELF — no gdb stop inside the
 * measurement window.
 *
 * ⚠ WHY THIS REPLACES A SEGMENTED gdb SCRIPT.  fps_seg.gdb re-arms a CONDITIONAL
 * breakpoint per segment, so gdb halts the machine at every call to Revs::render to
 * evaluate it.  Measured 2026-08-13, both on the SAME binary: fps_seg reported 1.4 FPS
 * where a single free run of the same build reported 0.43, and on a slower build it
 * reported 0.02 against 0.66 — the instruments disagree by 3x and 30x, in both
 * directions.  Sampling in the ISR and reading the whole series in ONE stop after the run
 * removes the stops from the window entirely.
 *
 * Cost: a mask, a compare and three stores once every 512 vblanks, inside a build that
 * exists only to be measured.  (docs/perf-method.md) */
#define FPS_SERIES_MAX   24u
extern "C" {
volatile unsigned long g_fpsSeries[FPS_SERIES_MAX];
volatile uint16_t      g_fpsSeriesVbi[FPS_SERIES_MAX];
volatile uint8_t       g_fpsSeriesN = 0;
}
#endif

static struct Interrupt  s_vbiServer;
static struct IntVector  s_savedVertb;
static bool              s_vertbTaken  = false;
static uint16_t          s_savedIntena = 0;

// exec puts IntVects[] at ExecBase+84, so VERTB (bit 5) is ExecBase+144 — exactly the
// offset Kickstart's level-3 autovector stub dispatches through.  If this ever fails to
// compile, the vector takeover below needs re-deriving before it is trusted.
static_assert(__builtin_offsetof(struct ExecBase, IntVects) == 84,
              "ExecBase::IntVects moved — re-check the VERTB vector takeover");

static uint32_t vbiHandler()
{
    // ⚠ Clearing the interrupt request is THIS handler's job — exec's server-chain
    // walker used to do it and we replaced it.  Miss this and level 3 re-triggers
    // forever.  No SETCLR bit = clear.
    *intreqPointer = (uint16_t)INTF_VERTB;

    g_vbiCount++;

#ifdef REVS_FPSCOUNT
    // Sample the painted-frame counter every 512 vblanks — a mask and three stores, so the
    // framerate series costs nothing and needs no gdb stop inside the window.  See the
    // g_fpsSeries comment above for the 3x/30x measurement error this replaces.
    if ((g_vbiCount & 511u) == 0u && g_fpsSeriesN < FPS_SERIES_MAX) {
        g_fpsSeries[g_fpsSeriesN]    = g_fpsFrames;
        g_fpsSeriesVbi[g_fpsSeriesN] = g_vbiCount;
        g_fpsSeriesN++;
    }
#endif

    // ⭐ Advance the phase-bracket clock by exactly one display frame.  Without this the
    // brackets time with a counter that wraps every 20 ms and they measure ~4% of a game
    // frame — see beamTick() in src/platform/probe.cpp.  Compiles to nothing without
    // PROBES.  Must stay ABOVE the game body, so a phase opened inside it is timed
    // against the frame it actually ran in.
    PROBE_VBI();

    // ⚠ Work here is capped at ONE FRAME.  Over that and a displayed frame is silently
    // dropped — and the dropped frame (a stall, a 2x animation jump, a copper write
    // landing behind the beam) is what a player reports, not the cost.  Bracket any new
    // ISR-side work with VPOSR/VHPOSR beam-line reads before theorising.
    // ⚠⚠ AND DO NOT DISPATCH THE GAME'S 50 Hz BODY HERE.  This used to be a TODO asking for
    // exactly that; it is settled the other way and the TODO was a trap.  The body DRAWS, so
    // running it in the ISR preempts the main loop's painting at an arbitrary point and gave
    // ~50 scene changes per painted frame.  Revs::vbi() COUNTS the field and the body is
    // drained from main-loop context at the engine's own frame hook ($1701) and frame wait
    // ($1760); `make BODY_IN_ISR=1` restores the old model for A/B only and amiga/fill_catch.gdb
    // detects it (docs/amiga-arch.md).  The copper bitplane POINTER swaps DO belong here and
    // are done, FIRST, in Revs::vbi() — the ordering and its beam measurement are written there.
    // The mouse counter is 8 bits and free-running, so it MUST be sampled every frame:
    // a missed frame loses the delta, and a delta taken across the wrap is a fast flick
    // in the wrong direction.  Two register reads, above the game body so it cannot be
    // starved by an overrunning one.
    /* ⭐ What does the ISR itself cost?  Its time is charged to whatever phase was open when it
       preempted, so the phase table cannot answer it (probe.cpp §two sub-frame timers). */
    /* ⭐⭐ ISRSPLIT: the handler's own sub-brackets (probe.h §ISRSPLIT).  Compiles to nothing
       without `make ISRSPLIT=1`.
       ⚠ MUST START BELOW PROBE_VBI(): that bumps g_beamEpoch by a whole display frame, so a
       bracket straddling it reads ~10 ms of pure instrument artefact. */
    PROBE_ISR_SPLIT(PROBE_ISR_PROLOGUE);
    PROBE_ISR_BEGIN();
    /* The NULL CONTROL, on the same path at the same rate: two consecutive transitions bracket
       nothing at all, so slot 0 IS the floor under every other row (probe.h §ISRSPLIT). */
    PROBE_ISR_SPLIT(PROBE_ISR_NULL);
    PROBE_ISR_SPLIT(PROBE_ISR_MOUSE);
    if (s_platform) s_platform->sampleMouse();

    if (s_scene) s_scene->vbi();
    PROBE_ISR_SPLIT(-1);
    PROBE_ISR_END();

    return 0;
}

// ---------------------------------------------------------------------------
// Embedded boot image (incbin.s)
// ---------------------------------------------------------------------------
extern "C" uint8_t revs_runtime_bin[];
extern "C" uint8_t revs_runtime_bin_end[];

// ---------------------------------------------------------------------------
PlatformAmiga::PlatformAmiga(const char* /*imagePath*/)
{
    // Open graphics.library here so run()'s display takeover can reach GfxBase.  On
    // failure set quit so main() bails instead of dereferencing a null GfxBase.
    GfxBase = (struct GfxBase*)OpenLibrary((CONST_STRPTR)"graphics.library", 33);
    quit = (GfxBase == 0);
}

PlatformAmiga::~PlatformAmiga()
{
    if (GfxBase) { CloseLibrary((struct Library*)GfxBase); GfxBase = 0; }
}

int PlatformAmiga::framesPerSecond() { return 50; }
void PlatformAmiga::setInterrupt(void (*)(void)) {}   // the real VERTB handler owns this
// ⭐⭐ THE GAME'S OWN FRAME WAIT is where the 50 Hz body runs.  This is called from the engine's
// spin at $1760, where the BBC's main loop sits waiting for exactly this interrupt to advance
// $62F7 — so it is the faithful place for the body, and the one place the engine is provably not
// drawing.  See Revs.h (drainTicks) for the measurement that moved it out of the ISR.
void PlatformAmiga::tickVBI()
{
    if (s_scene) s_scene->drainTicks();
}

int PlatformAmiga::loadImage(const char* /*path*/)
{
    // The 6502 image is linked in (incbin.s) rather than loaded from disc, so every
    // build boots the SAME initial state and code path.  It is the POST-UNPACK runtime
    // image: the generated C is a transliteration of the relocated layout and replaces
    // REVS2's unpack stub rather than running it (docs/static-map.md).
    const uint8_t* src = revs_runtime_bin;
    uint32_t n = (uint32_t)(revs_runtime_bin_end - revs_runtime_bin);
    if (n > 65536u) n = 65536u;
    for (uint32_t i = 0; i < n; i++) mem[i] = src[i];
    return 0;
}

// hwRead/hwWrite and mosCall are NOT overridden here.  The BBC hardware model
// (src/platform/bbc_hw.cpp) and the MOS surface (src/platform/mos.cpp) are implemented
// once for both backends, so the host and the target cannot disagree about the machine
// itself.  What this backend supplies is the two things that genuinely differ: a real
// frame boundary, and the input.

bool PlatformAmiga::vsyncElapsed()
{
    // The System VIA vsync flag, taken from the ISR's own counter rather than from a
    // beam read: it is only ever consulted by hw_init's one alignment spin, and that spin
    // must end on a frame boundary the rest of the port agrees with.
    uint16_t now = g_vbiCount;
    if (now == lastVsyncCount) return false;
    lastVsyncCount = now;
    return true;
}

// ⭐⭐ $FE68's clock — see PlatformAmiga.h.  Field count for the coarse part (g_vbiCount is
// the real VERTB's own counter) and the BEAM for the fine part: VHPOSR's low byte is the
// horizontal position in 280 ns units, its high byte the line.  One `move.w` from a register
// that is always moving, which is exactly the property T2 has on a BBC.
// ⚠ Only the low 8 bits of the derived down-counter are ever observed by the game, so the
// approximations here (256 lines per wrap, 4 beam units per microsecond) cost nothing real:
// what matters is the RATE and that nothing correlates it with what the game is doing.
uint32_t PlatformAmiga::hwMicros()
{
#ifdef REVS_FIXED_RNG
    return Platform::hwMicros();     // pinned: a perf run must drive the same simulation
#else
    const uint16_t vh = *vhposrPointer;
    return (uint32_t)g_vbiCount * 20000u + (uint32_t)(vh >> 8) * 64u + (uint32_t)(vh & 0xFFu) / 4u;
#endif
}

// ⭐⭐ THE SIMULATION CLOCK's inputs (docs/faithfulness-seam.md §THE FRAME-RATE-INDEPENDENT SIMULATION).
// The step is chosen by the CPU (user decision): a 68000 steps at 25 Hz — the best a stock
// A500 can display is 25 fps, and a step there costs ~3.6 ms of driving model — and a 68020 or
// better at 50 Hz, one step per field.  `make SIM_STEP_TENTHS=n` overrides it (936 = the engine's
// own 93.6 ms frame, h = 1) and `make SIMLEGACY=1` restores the engine's loop outright.
// The field count is the VERTB ISR's own, so under warp it is EMULATED time and a FIXED_RNG run
// stays deterministic.
unsigned PlatformAmiga::simStepTenths()
{
#if defined(REVS_SIM_LEGACY)
    return 0u;
#elif defined(SIM_STEP_TENTHS)
    return SIM_STEP_TENTHS;
#else
    return (SysBase->AttnFlags & AFF_68020) ? 200u : 400u;
#endif
}

unsigned PlatformAmiga::simFields()
{
    const uint16_t now = g_vbiCount;
    const uint16_t n   = (uint16_t)(now - lastSimFieldCount);
    lastSimFieldCount = now;
    return n;
}

/* ===========================================================================
   OSRDCH — the game's only text input                                (rdch)
   ---------------------------------------------------------------------------
   console_io ($6300) reads a fixed-width field a character at a time: the two
   wing settings ($3C50, two digits each) and the driver names ($66D4, twelve).
   Platform::rdch()'s default answers CR, which ends the field before the player
   can type anything — so on this backend both wing prompts self-answered and the
   wing page looked like a screen that only wanted SPACE, which is how it was
   reported.  It is not an extra SPACE: the page is faithful ($3C6B's
   wait_dismiss_space), it was just untypeable.

   Blocking by contract — mos.cpp: "must always return a real character; signalling
   ESCAPE instead loops forever" — so the wait drives real frames, exactly as the
   engine's own spin-wait hooks do.  Without renderFrame() here the echo of the
   character just typed would never reach the screen and the field would look dead
   while it filled.
   =========================================================================== */
uint8_t PlatformAmiga::rdch()
{
#ifdef REVS_AUTORUN_BUILD
    // ⚠ An unattended run has no typist.  autorun.cpp's script is written against
    // rdch() answering CR immediately (it documents this), and a blocking read here
    // would hang every probe and FPS run at the first wing prompt.
    return 0x0D;
#else
    for (;;) {
        uint8_t ch = input.typedChar();
        if (ch) return ch;
        if (quit) return 0x0D;                 // CTRL + left button: let the field close
        renderFrame();                         // echo what is already typed, wait one field
        pollEvents();
    }
#endif
}

void PlatformAmiga::flushKeyboard()
{
    input.flushTyped();
}

bool PlatformAmiga::keyDown(uint8_t x)
{
#ifdef REVS_AUTORUN_BUILD   // autorun.h — one predicate, not four flags per site
    // Unattended run: the script walks the front end and then holds the throttle, so the
    // measurement window contains the driving loop instead of a menu spin.  ⚠ It overrides
    // the real keyboard on purpose — a measurement must not depend on what is on the desk.
    //
    // ⭐ But it drives the REAL input path rather than short-circuiting it: the script's
    // answer is pushed into the same rawkey state the CIA-A handler writes, and the answer
    // returned is RevsInput's.  So an unattended run that still reaches a race is an
    // end-to-end test of the key map — which is otherwise unverifiable on a headless
    // target, because there is no keyboard to press.
    //
    // ⚠ A STRAIGHT_TO_RACE build stops routing through the script the moment it is done:
    // pressBbcKey() WRITES the rawkey state, so answering `false` for a key would clear
    // one the player is holding.  done() is false forever in a measurement build.
    if (!autoRun.done()) {
        bool held = autoRun.keyDown(x);
        input.pressBbcKey(x, held);
        // ⭐⭐ ...and when it finishes THIS poll, let go of everything.  A release step can only
        // clear the codes the game asks about while it is in force (two polls = two codes), so
        // the script used to hand the keyboard over with 'Q' still down: the engine then saw a
        // gear key held on every frame, $16BD kept $58 negative, and $49D6 took the idle arm —
        // which is why the port's engine never stalled where a real BBC parked in gear does.
        // ⚠ Measurement builds never reach this (done() is false forever there), so it cannot
        // disturb an FPS window; it fires exactly at the straight-to-race handover.
        if (autoRun.done()) input.releaseAllKeys();
#ifdef REVS_KEY_STIM
        // `make KEYSTIM=1`: hold UP (rawkey $4C, the throttle's second slot) from the handover on,
        // so the in-line answer in revs_keys.h is exercised by a headless run.
        if (autoRun.done()) g_keyDown[0x4C] = 1u;
#endif
    }
    if (autoRun.done()) g_keyDirect = 1;    // the script is finished: revs_keys.h may answer
    return input.keyDown(x);
#else
    return input.keyDown(x);
#endif
}

uint8_t  PlatformAmiga::adcButtons()             { return input.buttons(); }
uint16_t PlatformAmiga::adcAxis(uint8_t channel) { return input.axis(channel); }

void PlatformAmiga::renderFrame()
{
#ifdef REVS_CRASHPROBE
    // ⭐ Locate the freeze: how many PAL fields since the previous present?  A crash hold that
    // runs with no render (the bug the fence symptom points to) shows up as one large gap.
    {
        extern volatile unsigned long g_renderGapMax, g_renderGapMaxAt, g_renderStalls;
        static uint16_t s_lastPresent = 0;
        static bool     s_havePresent = false;
        uint16_t nowv = g_vbiCount;
        if (s_havePresent) {
            unsigned gap = (unsigned)((uint16_t)(nowv - s_lastPresent));
            if (gap > g_renderGapMax) { g_renderGapMax = gap; g_renderGapMaxAt = nowv; }
            if (gap >= 25) g_renderStalls++;         // >= ~0.5 s: a visible stall
        }
        s_lastPresent = nowv;
        s_havePresent = true;
    }
#endif
#ifdef REVS_TAPTEST
    revs_input_tap_test();   /* zero-length synthetic taps — RevsInput.cpp §THE PROOF */
#endif
    // Present, then wait for the next real vblank.  The wait is on g_vbiCount (the ISR's
    // own counter), not WaitTOF(): once the VERTB vector is taken over, graphics.library's
    // VERTB server no longer runs, so WaitTOF() would never be signalled.
    if (s_scene) s_scene->render();
    PROBE_PHASE(PROBE_PHASE_SPIN);
    uint16_t start = g_vbiCount;
    while (g_vbiCount == start) { /* spin */ }
}

void PlatformAmiga::pollEvents()
{
    // ⚠ QUIT IS CTRL + LEFT BUTTON, not the bare left button it used to be: the left button
    // is the BRAKE PEDAL now (RevsInput::axis channel 2), so a bare-button quit would end the
    // program the first time the player braked.  Ctrl is not a key the game ever tests, so the
    // chord cannot collide with anything.  Polled from every spin-wait so the player can always
    // abort — including out of a compute stretch that never reaches renderFrame().
    if ((*ciaapraPointer & 0x40u) == 0 && g_keyDown[0x63]) quit = true;
}

// ⭐⭐ THE CIRCUIT MENU.  src/platform/trackmenu.h is the model and `make trackmenu` proves the
// page against a real BBC; what lives here is only the DRIVING of it — the keyboard and the frame
// pump, which is the one thing that cannot be shared with the host build or the differential.
bool PlatformAmiga::runTrackMenu()
{
    // ⭐ Suspend the 50 Hz body for the duration.  engine_main() has not run, so there is no game
    // state to tick, and ~5.5 s of counted fields would hand it a 200-tick backlog (Revs.h).
    Revs::setFrontEnd(true);
    tm_begin(TM_OPTIONS_MAX);

#ifdef REVS_AUTORUN_BUILD
    // ⭐ UNATTENDED: answer the menu instead of skipping it, so the auto path still exercises
    // tm_begin(), the paint, the option→circuit routing and the install — the parts that can be
    // wrong without a keyboard.  `make TRACK=n` therefore now arrives at its circuit the same way
    // a player does.
    //
    // ⚠ AND IT PAINTS NO FRAMES, deliberately.  Every published framerate is `50 *
    // g_fpsFrames / g_vbiCount` over a whole run (docs/perf-method.md), so a handful of cheap
    // teletext frames at the start would inflate it — ~2% on a 200 s window, which is inside the
    // range this project has already been burned by quoting.  A measurement build must not have a
    // front end in its window at all.
    tm_tick(0, TM_TITLE_FIELDS);                  // spend the title dwell in one go
    {
        unsigned opt = tm_option_for_track((unsigned char)REVS_TRACK_DEFAULT);
        if (opt) {
            tm_tick(TM_KEY_OPTION(opt), 1);       // the digit
            tm_tick(0, 1);                        // release — SPACE only counts after one
            tm_tick(TM_KEY_SPACE, 1);             // confirm
        }
    }
#else
    // Interactive: one real display frame per iteration, reading the real keyboard through the
    // real map.  ⚠ Fields, not iterations, drive the dwell: a frame here costs a teletext decode
    // and may span more than one field, and 273 iterations of an unknown length is not 5.45 s.
    uint16_t last = g_vbiCount;
    while (!tm_finished() && !quit) {
        renderFrame();                            // decode the page, then wait one field
        pollEvents();                             // left mouse button still quits

        unsigned keys = 0;
        for (unsigned i = 0; i <= TM_OPTIONS_MAX; i++)
            if (input.keyDown(tm_key_codes[i])) keys |= (1u << i);
        // ...and the title page's skip, which answers to ANY key, not just the menu's seven.
        if (input.anyKeyDown()) keys |= TM_KEY_ANY;

        uint16_t now = g_vbiCount;
        tm_tick(keys, (uint16_t)(now - last));
        last = now;

        // The install is attempted INSIDE the loop so a refusal can put the player back in the
        // menu.  ⚠ revs_track_install() validates before it writes a byte (track.c), so a refused
        // choice leaves mem[] untouched and the next choice installs into a clean image — which is
        // what makes retrying safe at all.
        if (tm_finished()) {
            // ⚠ RECORD THE ASK BEFORE MAKING IT.  g_trackRequested is what distinguishes "this run
            // wanted Silverstone" from "this run wanted Nurburgring and could not have it", and
            // revs_track_boot() used to be the only thing that set it — so when the menu took over
            // installation it read 255 for the whole run and the refusal signal was gone.  That is
            // the same defect track.c's fallback note is about, one layer up.
            g_trackRequested = (unsigned char)tm_track();
            if (!revs_track_install((unsigned char)tm_track())) tm_reject();
        }
    }
#endif

#ifdef REVS_AUTORUN_BUILD
    if (tm_finished()) {
        g_trackRequested = (unsigned char)tm_track();       // see the note in the loop above
        if (!revs_track_install((unsigned char)tm_track())) tm_reject();
    }
#endif

    Revs::setFrontEnd(false);
    Revs::discardPendingTicks();
    return !quit;
}

// ---------------------------------------------------------------------------
void PlatformAmiga::run()
{
    // The scene holds several KB of shadow buffers; keep it in BSS (static), NOT on the
    // stack (where the PlatformAmiga instance lives), to avoid stack overflow.
    static Revs scene;

    // --- takeover: save system state, disable the OS display ------------------
    struct View* savedView = GfxBase->ActiView;
    LoadView(NULL);
    WaitTOF();
    WaitTOF();

    // Disable raster + sprite + copper DMA so old state can't leak through.  Copper DMA
    // is re-enabled below, once OUR list is installed.
    *dmaconPointer = (uint16_t)(DMAF_RASTER | DMAF_SPRITE | DMAF_COPPER);

    // Mask blit-done for the whole window: nothing here consumes it, and every armed one
    // is a pointless level-3 dispatch into graphics.library's queue handler.  INTENA is
    // saved and restored verbatim on the way out (the OS needs blit-done back for QBlit).
    s_savedIntena = (uint16_t)(*intenarPointer);
    *intenaPointer = (uint16_t)INTF_BLIT;    // no SETCLR = disable
    *intreqPointer = (uint16_t)INTF_BLIT;    // drop any already-latched request

    // A KNOWN-BLANK display for the window between here and scene.initialize(): zero
    // bitplanes, whatever the OS left in the window registers.  Nothing can appear —
    // raster DMA is off and BPLCON0 selects no planes.
    //
    // ⚠ THE DISPLAY GEOMETRY IS NOT SET HERE.  It has exactly one owner,
    // RevsScreen::setConstantRegisters(), which writes DIWSTRT/DIWSTOP, DDFSTRT/DDFSTOP,
    // FMODE and BPLCON1/2/3 in one place from the same constants the decode and the copper
    // bands are derived from (src/platform/bbc_screen.h).  Two owners for one write-only
    // register is how a value ends up fixed in the wrong file — and the band WAITs are all
    // relative to VSTRT, so the window and the copper list have to agree by construction,
    // not by two matching literals in two files.
    *bplcon0Pointer = 0x0000;

    // --- take over the whole VERTB vector ------------------------------------
    // Not AddIntServer: exec's iv_Code is the server-chain walker, so overwriting it drops
    // graphics.library / gameport.device / timer.device off the vblank entirely (~3.9% of
    // all wall clock on the Atari port).  iv_Node is cosmetic — it is what OS debug tools
    // report as the vector's owner.
    s_vbiServer.is_Node.ln_Type = NT_INTERRUPT;
    s_vbiServer.is_Node.ln_Pri  = 127;
    s_vbiServer.is_Node.ln_Name = (char*)"Revs VBI";
    s_vbiServer.is_Data = 0;
    s_vbiServer.is_Code = (void(*)())vbiHandler;
    {
        struct IntVector* iv = &SysBase->IntVects[INTB_VERTB];
        Disable();
        s_savedVertb = *iv;
        iv->iv_Data  = 0;
        iv->iv_Code  = (void(*)())vbiHandler;
        iv->iv_Node  = &s_vbiServer.is_Node;
        Enable();
        s_vertbTaken = true;
    }

    // --- bring up the scene --------------------------------------------------
    // Load the faithful boot image into mem[] before anything reads it.  Display DMA
    // stays OFF here: COP1LC still points at the OS LoadView(NULL) copper, so enabling
    // copper DMA now would let the OS copper run through initialize() and intermittently
    // reset our one-time custom-register setup.  initialize() installs our first list
    // (COP1LC = ours) with the copper halted, so there is no race.
    loadImage(0);

    scene.initialize();

    // Real input.  ⚠ AFTER the display takeover and BEFORE Forbid(): OpenResource and the
    // AddICRVector dance are OS calls, and the ICR vector must be ours before the game
    // starts asking which keys are held.
    input.initialize();

    // ⭐⭐ PUBLISH THE SCENE TO THE ISR ONLY ONCE IT IS BUILT — and that means AFTER both
    // initialize() calls, not before them.  The VERTB vector was taken over ~40 lines above,
    // so the handler is already firing at 50 Hz while these run, and both of them make OS
    // calls (AllocMem for ~50 KB of chip RAM, OpenResource, AddICRVector) that comfortably
    // span a vblank.
    //
    // THE BUG THIS FIXES (measured 2026-08-15, and it was the black screen).  RevsScreen::
    // initialize() assigns m_copper / m_ttCopper / m_ttBitmap the instant each allocation
    // returns, ~40 lines before it fills either list in.  A VERTB landing in that window ran
    // vbiUpdate() -> applyMode(), which found tt_active() true, LATCHED m_ttOnScreen = 1 and
    // installed the still-empty MODE 7 list.  initialize() then finished by writing the RACE
    // list and installing it (Revs::initialize) with its four pens deliberately black — and
    // because m_ttOnScreen was already 1, applyMode() never switched again for the whole run.
    // The target sat on a 2-bitplane race list with an all-black palette while every MODE 7
    // probe read healthy: page correct, planes=3, height=250, mode7=1, ttAllocFailed=0.
    // Symptom: a black screen that looks exactly like a hang.
    //
    // ⚠ Not fixed by reordering inside initialize(): "allocate, then build" is unavoidable,
    // so the only durable rule is that the ISR cannot see a half-built scene at all.
    // RevsScreen::vbiUpdate() carries the matching guard on its own side.
    s_scene = &scene;
    s_platform = this;

    // Our list is installed and the constant registers are set — safe to start display
    // DMA.  The copper restarts from COP1LC (ours) at the next vblank.
    *dmaconPointer = (uint16_t)(DMAF_SETCLR | DMAF_MASTER | DMAF_COPPER |
                               DMAF_RASTER | DMAF_SPRITE);

    // --- multitasking off for the duration -----------------------------------
    // Nothing here needs exec's scheduler and we never Wait().  ⚠ Everything between
    // Forbid() and Permit() must be Wait()-free — the WaitTOF() pairs and every library
    // open/close are deliberately outside.
    Forbid();

    // ⭐⭐ CIRCUIT SELECTION, and the position in this sequence is the whole argument.
    //
    // It must be AFTER input.initialize() and the DMA enable (the menu reads the real keyboard and
    // has to be on screen) and BEFORE scene.run() (engine_init reads $5300-$5A25 immediately, so
    // installing later would swap the geometry under a session that had already read it —
    // src/platform/track.h).  Between Forbid() and scene.run() is the only window that is both.
    //
    // ⚠ revs_track_boot() USED TO RUN HERE, ~40 lines earlier, and it no longer does: the menu is
    // now the single installer.  That is not tidying, it is the fix for a real hazard — one boot
    // image installs exactly one circuit (track.h §INSTALLING IS NOT IDEMPOTENT), so a default
    // install followed by the player's choice would leave the default's patch bytes in the engine.
    // An unattended build reaches its `make TRACK=n` circuit through the menu's own auto path.
    if (runTrackMenu()) scene.run();   // returns when the user quits

    Permit();
    input.shutdown();        // hand the SP vector back to keyboard.device
    scene.shutdown();

    // --- restore the system --------------------------------------------------
    // Hand VERTB back BEFORE the LoadView/WaitTOF restore: WaitTOF() is signalled by
    // graphics.library's VERTB server, which only runs again once exec's chain walker is
    // back in the vector.
    if (s_vertbTaken) {
        Disable();
        SysBase->IntVects[INTB_VERTB] = s_savedVertb;
        Enable();
        s_vertbTaken = false;
    }
    s_scene = 0;
    s_platform = 0;

    *dmaconPointer = (uint16_t)(DMAF_COPPER | DMAF_RASTER | DMAF_SPRITE);

    // Put the interrupt enables back exactly as the OS had them.  Drop a latched blit
    // request first so re-enabling can't immediately fire a stale one into its handler.
    *intreqPointer = (uint16_t)INTF_BLIT;
    *intenaPointer = (uint16_t)(INTF_SETCLR | (s_savedIntena & 0x7FFFu));

    LoadView(savedView);
    WaitTOF();
    WaitTOF();
}
