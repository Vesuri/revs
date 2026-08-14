/* PlatformAmiga — day-one bring-up skeleton.  See PlatformAmiga.h for the design rules.
 *
 * What it does today: takes the machine over, installs the real VERTB interrupt, runs
 * Revs::run(), restores everything.  What it does NOT do yet: render the game, emulate
 * BBC hardware, service MOS calls, produce audio.  Each of those is a filed phase in
 * docs/phases.md; the seams are here and marked TODO so they can be filled in place.
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

extern "C" volatile uint8_t mem[65536];      // the 6502 RAM image (src/cpu/cpu.c)

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
    // TODO(phase: Amiga backend): dispatch the game's own IRQ1V/EVNTV body here, and do
    // all copper bitplane POINTER swaps here — never mid-frame.
    // The mouse counter is 8 bits and free-running, so it MUST be sampled every frame:
    // a missed frame loses the delta, and a delta taken across the wrap is a fast flick
    // in the wrong direction.  Two register reads, above the game body so it cannot be
    // starved by an overrunning one.
    if (s_platform) s_platform->sampleMouse();

    if (s_scene) s_scene->vbi();

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

bool PlatformAmiga::keyDown(uint8_t x)
{
#if defined(REVS_FPSCOUNT) || defined(REVS_PROBE) || defined(REVS_STRAIGHT_TO_RACE)
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
    }
    return input.keyDown(x);
#else
    return input.keyDown(x);
#endif
}

uint8_t  PlatformAmiga::adcButtons()             { return input.buttons(); }
uint16_t PlatformAmiga::adcAxis(uint8_t channel) { return input.axis(channel); }

void PlatformAmiga::renderFrame()
{
    // Present, then wait for the next real vblank.  The wait is on g_vbiCount (the ISR's
    // own counter), not WaitTOF(): once the VERTB vector is taken over, graphics.library's
    // VERTB server no longer runs, so WaitTOF() would never be signalled.
    if (s_scene) s_scene->render();
    uint16_t start = g_vbiCount;
    while (g_vbiCount == start) { /* spin */ }
}

void PlatformAmiga::pollEvents()
{
    // Left mouse button quits.  Polled from every spin-wait so the player can always
    // abort — including out of a compute stretch that never reaches renderFrame().
    if ((*ciaapraPointer & 0x40u) == 0) quit = true;
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

    s_scene = &scene;
    s_platform = this;
    scene.initialize();

    // Real input.  ⚠ AFTER the display takeover and BEFORE Forbid(): OpenResource and the
    // AddICRVector dance are OS calls, and the ICR vector must be ours before the game
    // starts asking which keys are held.
    input.initialize();

    // Our list is installed and the constant registers are set — safe to start display
    // DMA.  The copper restarts from COP1LC (ours) at the next vblank.
    *dmaconPointer = (uint16_t)(DMAF_SETCLR | DMAF_MASTER | DMAF_COPPER |
                               DMAF_RASTER | DMAF_SPRITE);

    // --- multitasking off for the duration -----------------------------------
    // Nothing here needs exec's scheduler and we never Wait().  ⚠ Everything between
    // Forbid() and Permit() must be Wait()-free — the WaitTOF() pairs and every library
    // open/close are deliberately outside.
    Forbid();

    scene.run();          // returns when the user quits

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
