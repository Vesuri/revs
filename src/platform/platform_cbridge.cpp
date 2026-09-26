/* C bridge — implements the C-callable interface declared in platform_c.h by
   forwarding to the Platform C++ singleton.  Compiled as C++ so it can see
   platform.h; the symbols are exported with C linkage so the generated C
   translation units link against them unmangled. */

#include "platform.h"
#include "platform_c.h"
#include "probe.h"
#include "../cpu/cpu.h"   /* the global cpu struct — this file marshals it across the MOS seam */

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

/* The cpu-marshalling bridge the GENERATED corpus uses.  The transpiler emits
   `platform_mos_call(entry)` for every JSR into the MOS block, in both the oracle
   and the transliteration, so this signature is fixed: it reads the register file
   out of the global cpu struct, runs the (cpu-free) dispatcher, and writes the exit
   file back.  The dispatcher touches A/X/Y and C only, so nothing else in cpu moves. */
void platform_mos_call(uint16_t entry) {
    if (!platform) return;
    MosRegs in = { cpu.A, cpu.X, cpu.Y, cpu.C };
    MosRegs out = platform->mosCall(entry, in);
    cpu.A = out.a; cpu.X = out.x; cpu.Y = out.y; cpu.C = out.c;
}

/* The typed bridge the native twins' wrappers use — no cpu on either side, so a
   twin can issue an OS call without deciding what the register file's flags are. */
MosRegs platform_mos_call_typed(uint16_t entry, MosRegs in) {
    if (!platform) return in;
    return platform->mosCall(entry, in);
}

int platform_key_down(uint8_t code) {
    return platform ? (platform->keyDown(code) ? 1 : 0) : 0;
}

uint8_t platform_adc_buttons(void) {
    return platform ? platform->adcButtons() : 0x00;
}

uint16_t platform_adc_axis(uint8_t channel) {
    return platform ? platform->adcAxis(channel) : 0x8000;
}

void platform_brk(uint16_t pc) {
    if (platform) platform->brk(pc);
}

void platform_smc_unhandled(uint16_t site, uint16_t value) {
    if (platform) platform->smcUnhandled(site, value);
}

/* Deliberately NOT a Platform virtual, for the same reason as platform_bad_region_entry
   below: it is a diagnostic on the generated code, identical on every backend.  The Amiga
   has no filesystem to write 64 KB to and no reference to compare against, so the whole
   body is host-only. */
#ifndef REVS_PLATFORM_AMIGA
void revs_host_mem_snapshot_at(uint16_t pc);
#endif
void platform_mem_snapshot_at(uint16_t pc) {
#ifdef REVS_PLATFORM_AMIGA
    (void)pc;
#else
    revs_host_mem_snapshot_at(pc);
#endif
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
   The port broke that contract for A and it cost a visible artefact: irq1v_band_schedule restores A
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

unsigned platform_sim_step_tenths(void) {
    return platform ? platform->simStepTenths() : 0u;
}

unsigned platform_sim_fields(void) {
    return platform ? platform->simFields() : 0u;
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

/* ⭐ The User VIA IFR ($FE6D bit 6), for the irq1v_band_schedule fixture.  It is the FIRST thing
   the handler reads and a clear bit means "not our interrupt", so the handler chains
   straight out to the MOS and does nothing else.  Left at 0 the differential would run
   thousands of cases against one three-instruction path and report a confident PASS —
   the vacuous-green failure mode, wearing a plausible input.  Both the twin and its oracle
   see the same answer because both go through this one platform. */
static int g_headlessT1Pending = 0;
/* ⭐ $FE68 (User VIA T2 counter low) under the TEST platform.  Revs's only entropy source, and
   the headless backend answered a constant 0 — which is deterministic (so diff_run works) but
   pins the engine's two luck tests to one arm each: `VIA & starter_random_mask` is always 0, so
   the starter always catches, and `VIA & 7` always adds nothing.  Twins #85/#84 read this
   register, so the fixture sets it per case and BOTH arms get exercised.  Real runs never touch
   this hook (the shipping backends override hwRead with the clock model in bbc_hw.cpp). */
static unsigned char g_headlessViaT2 = 0;
/* ⭐ …and whether OSBYTE 129 answers "held" under the TEST platform.  Same argument: the
   default is "no key is ever down", which makes update_engine_revs' STARTER arm — everything
   past `JSR kbd_test_key / BEQ` — unreachable from any fixture.  Twin #85's fixture toggles it,
   and the sabotage that ignores starter_random_mask is what proved the hole was real. */
static int g_headlessKeyDown = 0;
/* ⚠⚠ A SINGLE all-or-nothing ANSWER IS A COVERAGE HOLE, and `make validate` found it twice.
   read_driving_controls tests SIX different key codes and its interesting arms are the ones
   where exactly ONE of them is down — steering left OR right, throttle OR brake, gear up OR
   down.  With one global answer the fixture could only ever produce "none" or "all", so the
   both-keys arm ran and the one-key arms never did: the sabotage "the key direction is not
   compared with the current sign" survived 5000 cases.  Mode 2 answers for one code only. */
static int g_headlessKeyMode = 0;      /* 0 none, 1 every key, 2 only g_headlessKeyCode, 3 the held-set, 4 the clock schedule,
                                          5 the per-code POLL-COUNT schedule */
static unsigned char g_headlessKeyCode = 0;
/* Mode 4, the CLOCK SCHEDULE: which single code reports held is a function of the tick clock, so a
   multi-frame poll loop (menu_wait_key) can be driven phase by phase.  keyDown reports code held iff
   code == g_headlessKeySchedule[min(mem[g_headlessClockAddr], n-1)].  Because the clock cell lives in
   mem[] and diff_run resets mem[] from `pre` before each model run, both models see the identical
   phase sequence.  A schedule slot of 0x01 (never a menu key, never queried by the poll dispatcher)
   means "nothing held this phase" -> the loop redraws. */
static unsigned char g_headlessKeySchedule[16];
static int g_headlessKeyScheduleN = 0;
/* Mode 3, the held-SET: an arbitrary set of negative-INKEY codes reported down at once.  Mode 2's
   single code cannot cover shift_key_commands, whose interesting arms need SHIFT ($FF) held AND a
   specific scan-table key held together (and the pause path needs $A6 held on top). */
static unsigned char g_headlessKeySet[256];
/* Mode 5, the POLL-COUNT schedule: the answer for a code is a function of HOW MANY TIMES that code
   has been polled, not of the tick clock.  Mode 4 cannot drive the dismiss-key waiters
   (wait_dismiss_space / wait_dismiss_key, $34D0/$34D2) at all: they spin on kbd_test_key without
   ever reaching a frame hook, so mem[g_headlessClockAddr] never advances and the wait for SPACE to
   be RELEASED never ends.  Here each code carries a 32-bit mask, bit i = "held on this code's i'th
   poll", saturating on bit 31 — so a mask with bit 31 set is a key that ends up held forever, which
   is what makes a poll loop terminate by construction.
   ⚠⚠ The poll counters are PROCESS state, not mem[], so diff_run must re-arm them before EACH
   model or the twin inherits the oracle's poll positions and reads a different key sequence.
   platform_test_key_poll_rearm() is that hook, and it is called from both halves of diff_run. */
static unsigned int  g_headlessKeyPollMask[256];
static unsigned char g_headlessKeyPolls[256];
/* ⭐⭐ THE OSRDCH SCHEDULE.  Platform::rdch() defaults to CR, which ends console_io's line the
   instant it starts — a default-answering test backend, and the whole line editor (the printable
   range test, the DELETE arm, the field-full bell) would then be an arm no fixture ever runs.
   A test gives the character SEQUENCE the typist produces; past its end the answer is CR, so any
   schedule terminates.  ⚠⚠ Like the poll counters this cursor is PROCESS state, so diff_run
   re-arms it before EACH model (platform_test_rdch_rearm). */
enum { RDCH_MAX = 64 };
static unsigned char g_headlessRdch[RDCH_MAX];
static int           g_headlessRdchN   = 0;
static int           g_headlessRdchPos = 0;

/* ...and the same for the analogue axes: Platform's default answers dead centre, which pins
   adc_read's magnitude to 0 and makes its dead-zone compare and the joystick's whole pedal arm
   unreachable (two more surviving sabotages). */
static unsigned short g_headlessAdcAxis    = 0x8000;
static unsigned char  g_headlessAdcButtons = 0x00;

/* ⭐⭐ THE HARDWARE-WRITE TRACE, and it is not an extra: without it the differential is
   BLIND to the whole output of a routine whose job is writing hardware.
   Found by sabotage, 2026-08-16: twin #1 is irq1v_band_schedule, which writes the Video ULA and
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
    bool keyDown(uint8_t x) override {
        if (g_headlessKeyMode == 4) {
            int i = g_headlessKeyScheduleN ? (int)mem[g_headlessClockAddr] : 0;
            if (i >= g_headlessKeyScheduleN) i = g_headlessKeyScheduleN - 1;
            if (i < 0) i = 0;
            return g_headlessKeyScheduleN && x == g_headlessKeySchedule[i];
        }
        if (g_headlessKeyMode == 5) {
            unsigned n = g_headlessKeyPolls[x];
            if (n < 31u) g_headlessKeyPolls[x] = (unsigned char)(n + 1u);
            else n = 31u;
            return ((g_headlessKeyPollMask[x] >> n) & 1u) != 0u;
        }
        if (g_headlessKeyMode == 3) return g_headlessKeySet[x] != 0;
        if (g_headlessKeyMode == 2) return x == g_headlessKeyCode;
        return g_headlessKeyDown != 0;
    }
    uint8_t rdch() override {
        if (g_headlessRdchPos < g_headlessRdchN) return g_headlessRdch[g_headlessRdchPos++];
        return 0x0Du;                        /* out of script: CR ends the line */
    }
    uint8_t  adcButtons() override { return g_headlessAdcButtons; }
    uint16_t adcAxis(uint8_t) override { return g_headlessAdcAxis; }
    uint8_t hwRead(uint16_t addr) override {
        if (addr == 0xFE6D && g_headlessT1Pending) return 0xC0;
        if (addr == 0xFE68) return g_headlessViaT2;
        /* $FE4D bit 1 is the vsync flag hw_init's alignment spin blocks on.  Answering 0
           here would hang BOTH models forever (twin #219's fixture found it), so defer to
           the hardware model, which reports the field as elapsed. */
        if (addr == 0xFE4D) return Platform::hwRead(addr);
        return 0x00;
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

/* What $FE68 answers under the test platform — see g_headlessViaT2. */
void platform_test_via_t2(unsigned char v) { g_headlessViaT2 = v; }

/* Whether OSBYTE 129 reports the key held — see g_headlessKeyDown. */
void platform_test_key_down(int on) {
    g_headlessKeyDown = on ? 1 : 0;
    g_headlessKeyMode = on ? 1 : 0;
}

/* ...and the one-key mode: ONLY this internal key number answers held.  See g_headlessKeyMode. */
void platform_test_key_only(unsigned char code) {
    g_headlessKeyMode = 2;
    g_headlessKeyCode = code;
    g_headlessKeyDown = 1;
}

/* Held-SET mode (mode 3): start empty, then add each code that should report held. */
void platform_test_key_set_clear(void) {
    g_headlessKeyMode = 3;
    for (int i = 0; i < 256; i++) g_headlessKeySet[i] = 0;
}
void platform_test_key_set_add(unsigned char code) {
    g_headlessKeyMode = 3;
    g_headlessKeySet[code] = 1;
}

/* Clock-schedule mode (mode 4): the code held is chosen by the tick clock, one entry per phase.
   Pair with platform_test_tick_clock(1)/platform_test_clock_addr(); a slot of 0x01 = nothing held. */
void platform_test_key_schedule(const unsigned char* codes, int n) {
    g_headlessKeyMode = 4;
    if (n < 0) n = 0;
    if (n > 16) n = 16;
    g_headlessKeyScheduleN = n;
    for (int i = 0; i < n; i++) g_headlessKeySchedule[i] = codes[i];
}

/* Poll-count mode (mode 5): clear every mask, then give each interesting code its schedule.
   A mask of 0 is a key never held; bit 31 set is a key held from its 31st poll onward. */
void platform_test_key_poll_clear(void) {
    g_headlessKeyMode = 5;
    for (int i = 0; i < 256; i++) { g_headlessKeyPollMask[i] = 0u; g_headlessKeyPolls[i] = 0; }
}
void platform_test_key_poll_set(unsigned char code, unsigned int mask) {
    g_headlessKeyMode = 5;
    g_headlessKeyPollMask[code] = mask;
    g_headlessKeyPolls[code] = 0;
}
/* ⚠⚠ Rewind every code to its first poll.  diff_run calls this before EACH model run: the counters
   live outside mem[], so without it the native twin starts where the oracle stopped. */
void platform_test_key_poll_rearm(void) {
    for (int i = 0; i < 256; i++) g_headlessKeyPolls[i] = 0;
}

/* The OSRDCH script: the characters console_io's line editor reads, in order. */
void platform_test_rdch_seq(const unsigned char* chars, int n) {
    if (n < 0) n = 0;
    if (n > RDCH_MAX) n = RDCH_MAX;
    g_headlessRdchN = n;
    g_headlessRdchPos = 0;
    for (int i = 0; i < n; i++) g_headlessRdch[i] = chars[i];
}
/* ⚠⚠ Rewind the script to its first character — diff_run calls this before EACH model. */
void platform_test_rdch_rearm(void) { g_headlessRdchPos = 0; }

/* What ADVAL answers under the test platform: the 16-bit axis (only its high byte is used by
   adc_read) and the fire-button word. */
void platform_test_adc(unsigned short axis, unsigned char buttons) {
    g_headlessAdcAxis    = axis;
    g_headlessAdcButtons = buttons;
}

} /* extern "C" */
