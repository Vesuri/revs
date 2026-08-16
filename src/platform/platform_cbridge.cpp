/* C bridge — implements the C-callable interface declared in platform_c.h by
   forwarding to the Platform C++ singleton.  Compiled as C++ so it can see
   platform.h; the symbols are exported with C linkage so the generated C
   translation units link against them unmangled. */

#include "platform.h"
#include "platform_c.h"
#include "probe.h"

extern "C" {

#ifdef REVS_PROBE_HWTIME
/* ⭐ HOW MANY HARDWARE ACCESSES DOES A FIELD COST?  The 50 Hz body is 51% of the frame and
   ~10.5 ms of every 20 ms tick (docs/perf-method.md), and its five band arms are mostly
   `STA $FE21` — one 4-cycle instruction on a 6502, a virtual call plus a 40-case switch here.
   Counting them turns "the handler is slow" into microseconds per access, which is the number
   that says whether the fix is a faster seam or less work. */
extern "C" {
volatile unsigned long g_probeHwWrites = 0;
volatile unsigned long g_probeHwReads  = 0;
/* g_probeHwTicks — the per-access cost — is defined in probe.cpp beside the clock that fills it. */
}
#endif

uint8_t platform_hw_read(uint16_t addr) {
#ifdef REVS_PROBE_HWTIME
    g_probeHwReads++;
#endif
    return platform ? platform->hwRead(addr) : 0;
}


void platform_hw_write(uint16_t addr, uint8_t val) {
#ifdef REVS_PROBE_HWTIME
    g_probeHwWrites++;
    probe_hw_begin();
    if (platform) platform->hwWrite(addr, val);
    probe_hw_end();
    return;
#endif
    if (platform) platform->hwWrite(addr, val);
}

void platform_shadow_write(uint16_t addr, uint8_t val) {
    if (platform) platform->shadowWrite(addr, val);
}

int platform_load_image(const char* path) {
    return platform ? platform->loadImage(path) : -1;
}

void platform_register_vbi(uint16_t addr, void (*fn)(void)) {
    if (platform) platform->registerVBI(addr, fn);
}

void platform_indirect_jmp(uint16_t addr) {
    if (platform) platform->indirectJmp(addr);
}

void platform_mos_call(uint16_t entry) {
    if (platform) platform->mosCall(entry);
}

void platform_brk(uint16_t pc) {
    if (platform) platform->brk(pc);
}

void platform_smc_unhandled(uint16_t site, uint16_t value) {
    if (platform) platform->smcUnhandled(site, value);
}

/* Deliberately NOT a Platform virtual: this is a property of the generated code, identical
   on every backend, and adding an interface method for it would imply a backend could
   sensibly differ about it.  Counted rather than aborted so a headless run reports it in
   one gdb read instead of dying with no state to inspect. */
unsigned long g_badRegionCount = 0;
uint16_t      g_badRegionEntry = 0;

void platform_bad_region_entry(uint16_t region, uint16_t entry) {
    g_badRegionCount++;
    g_badRegionEntry = entry;
}

/* ⭐ THE INTERRUPT REGISTER CONTRACT, asserted at the seam.
   Measured on real hardware with `make refloop --irq-abi`: over 2858 interrupts taken while
   the ENGINE was running, A, X and Y were preserved EVERY time.  (Interrupts taken during MOS
   code do clobber all three, which is why that measurement has to be filtered to the engine —
   unfiltered it reports "everything is clobbered" and is useless as a contract.)
   The port broke that contract for A and it cost a visible artefact: irq1v_handler restores A
   from $FC, nothing wrote $FC, so every ISR return zeroed A — invisible on the host, where the
   ISR fires at a controlled point, and a black run to the right edge on the Amiga, where a
   real VERTB preempts the fill chain mid-line.  So the contract is now CHECKED rather than
   assumed, on both backends, because the next violation of it will look like something else
   entirely too. */
unsigned long g_irqClobberCount = 0;
uint8_t       g_irqClobberWhich = 0;   /* bit 0 = A, bit 1 = X, bit 2 = Y */

/* ⭐ The same contract for the state the 6502 keeps in its STACK POINTER and this port keeps in a
   C global: `cpu_unwind`, the two-level-RTS flag (src/cpu/cpu.h).  An interrupt taken between the
   drop and the consume must not disturb it — on the 6502 it cannot, because there the state is S
   and the interrupt sequence saves it.  Pending counts how often an interrupt landed inside that
   window at all (the window is real, so this is expected to be non-zero); Touched counts the ones
   where the handler's own call tree actually moved the flag, which is the bug and must stay 0.
   Imbalance is the matching check for the 6502 stack itself. */
unsigned long g_irqUnwindPending  = 0;
unsigned long g_irqUnwindTouched  = 0;
unsigned long g_irqStackImbalance = 0;

void platform_render_frame(void) {
    if (platform) platform->renderFrame();
}

void platform_tick_vbi(void) {
    if (platform) platform->tickVBI();
}

void platform_poll_events(void) {
    if (platform) platform->pollEvents();
}

} /* extern "C" */

/* ---------------------------------------------------------------------------
   Test-only headless platform for the native-twin validation harness
   (tools/validate_native.c).  The harness links the platform objects but never
   opens a window.  None of this is referenced by the real game build.

   ⚠ Keep this minimal and DETERMINISTIC.  Anything the harness needs the
   platform to emulate (a MOS call a twin makes, a VIA timer a twin polls) must
   be modelled here identically for BOTH the twin and its __t6502 oracle, or the
   differential is comparing two different machines.  See
   docs/validation-harness.md.
   --------------------------------------------------------------------------- */
#include "../cpu/mem_decl.h"
extern MEM_QUAL uint8_t mem[65536];   /* the 6502 RAM image (src/cpu/cpu.c) */

/* Opt-in for frame-wait fixtures: when nonzero, tickVBI advances the BBC's
   100 Hz system clock low byte so a frame-driven twin and its oracle both make
   progress instead of spinning forever on a never-changing counter.  Default OFF
   so every pure-mem[] test is unaffected. */
static int g_headlessTickClock = 0;
static uint16_t g_headlessClockAddr = 0x0292;   /* MOS TIME low byte — confirm */

/* ⭐ The User VIA IFR ($FE6D bit 6), for the irq1v_handler fixture.  It is the FIRST thing
   the handler reads and a clear bit means "not our interrupt", so the handler chains
   straight out to the MOS and does nothing else.  Left at 0 the differential would run
   thousands of cases against one three-instruction path and report a confident PASS —
   the vacuous-green failure mode, wearing a plausible input.  Both the twin and its oracle
   see the same answer because both go through this one platform. */
static int g_headlessT1Pending = 0;

/* ⭐⭐ THE HARDWARE-WRITE TRACE, and it is not an extra: without it the differential is
   BLIND to the whole output of a routine whose job is writing hardware.
   Found by sabotage, 2026-08-16: twin #1 is irq1v_handler, which writes the Video ULA and
   the User VIA T1 latch and leaves only four bytes in mem[].  Two deliberate defects —
   the horizon-split comparison off by one, and the wrong band's T1 latch — both produced
   a byte-identical mem[] and a confident PASS over 25 628 cases.  A mem[]-only diff can
   never see them: the ULA is not in mem[].
   So every hardware write goes into this log, and diff_run compares the two runs' logs as
   a SEQUENCE (order matters — the last write to a palette slot wins, and $FE66 closes a
   band record). */
enum { HWLOG_MAX = 8192 };
extern "C" {
uint16_t g_hwLogAddr[HWLOG_MAX];
uint8_t  g_hwLogVal[HWLOG_MAX];
unsigned g_hwLogN = 0;
unsigned g_hwLogOverflow = 0;

#ifdef REVS_HW_TRACE
/* Called from bbc_screen.h's ULA inlines — the one path that reaches the hardware model
   without going through hwWrite (a native twin's fast path).  See the comment there. */
void bbc_hw_trace(unsigned short addr, unsigned char val) {
    if (g_hwLogN < HWLOG_MAX) { g_hwLogAddr[g_hwLogN] = addr; g_hwLogVal[g_hwLogN] = val; g_hwLogN++; }
    else g_hwLogOverflow++;
}
#endif
}

namespace {
struct HeadlessPlatform : Platform {
    void    run() override {}
    void    setInterrupt(void (*)(void)) override {}
    int     framesPerSecond() override { return 50; }
    void    renderFrame() override {}
    void    tickVBI() override { if (g_headlessTickClock) mem[g_headlessClockAddr]++; }
    int     loadImage(const char*) override { return -1; }
    uint8_t hwRead(uint16_t addr) override {
        return (addr == 0xFE6D && g_headlessT1Pending) ? 0xC0 : 0x00;
    }
    void hwWrite(uint16_t addr, uint8_t val) override {
        /* ⚠ NOT $FE20/$FE21: those reach the model through bbc_screen.h's inlines, which
           trace themselves so a twin's fast path is covered too.  Logging here as well
           would double every ULA write on the oracle's side only. */
        if (addr != 0xFE20 && addr != 0xFE21) {
            if (g_hwLogN < HWLOG_MAX) { g_hwLogAddr[g_hwLogN] = addr; g_hwLogVal[g_hwLogN] = val; g_hwLogN++; }
            else g_hwLogOverflow++;
        }
        Platform::hwWrite(addr, val);      /* the real model still runs */
    }
};
} /* namespace */

extern "C" {

void platform_test_init_headless(void) {
    if (!platform) platform = new HeadlessPlatform();
}

/* Enable/disable the clock-advancing tick for frame-wait twin validation. */
void platform_test_tick_clock(int on) { g_headlessTickClock = on ? 1 : 0; }
void platform_test_clock_addr(uint16_t a) { g_headlessClockAddr = a; }

/* Raise/lower the User VIA T1 timeout flag the IRQ1V handler dispatches on. */
void platform_test_t1_pending(int on) { g_headlessT1Pending = on ? 1 : 0; }

} /* extern "C" */
