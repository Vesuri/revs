/* bbc_hw.cpp — the shared BBC hardware model behind bus_read/bus_write.
 *
 * Same argument as mos.cpp: the *semantics* of the two VIAs are the machine and are
 * identical on both backends; only the display and audio consequences differ.  So the
 * model lives here once, and a backend overrides only what it genuinely re-hosts.
 *
 * ⭐ WHAT PHASE 2 MEASURED, AND WHY IT IS SHORT.  DumpHwAccesses.java found **19 registers
 * across 4 devices**, and three previously-[ASSUMED] rows turned out wrong: the game's
 * interrupt is a **User VIA T1 timer**, not System VIA vsync, and neither the ADC nor the
 * sound chip is ever addressed directly (both go through the MOS — see mos.cpp).  That is
 * why this file models two timers and a couple of flags rather than a BBC.
 * `docs/static-map.md` §The hardware map has the full inventory.
 *
 * ⭐ THE TWO READS THAT ARE LOAD-BEARING.  Everything else here is a write that the port
 * either records or drops; these two are what the 6502 *blocks on*, and getting them wrong
 * is a hang, not a glitch:
 *
 *   $FE4D  System VIA IFR — bit 1 is the vsync (CA1) flag.  hw_init ($4E11) spins
 *          `BIT $FE4D / BEQ` until it is set, to align the raster-band chain to the frame
 *          before claiming IRQ1V.  A model that never sets it never boots.
 *   $FE6D  User VIA IFR — bit 6 is the T1 timeout.  irq1v_handler ($4E5C) reads it FIRST
 *          and, if clear, chains straight on to the previous IRQ1V handler ($4E59 → an
 *          indirect JMP into MOS ROM that this port has nothing behind).  A model that
 *          never sets it means the game body never runs while everything else looks fine.
 *
 * ⚠ This is deliberately NOT a VIA emulation.  Revs uses two flag bits and a timer latch;
 * modelling shift registers and handshake modes would be inventing behaviour nothing
 * observes.  Where a real VIA is more subtle than this, the comment says so.
 */
#include "platform.h"
#include "bbc_screen.h"
#include "teletext.h"       /* tt_set_active — leaving MODE 7 is a CRTC write, see hwWrite */
#include "platform_c.h"     /* g_irqClobberCount/Which — the interrupt register contract */
#include "probe.h"          /* PROBE_IRQ_*(): what ONE band arm costs (probe.cpp) */
#include "../cpu/cpu.h"

extern "C" void irq1v_handler(void);   /* $4E5C, from the generated transliteration */
extern "C" void FUN_52a4(void);        /* band 4's arm — the ONLY game work in the cycle */

/* ---------------------------------------------------------------------------
   Registers Phase 5 will need, recorded rather than dropped.  Keeping the last
   written value costs one store and turns "the copper shows the wrong colours"
   into a readable number.
   --------------------------------------------------------------------------- */
extern "C" {
volatile uint8_t  g_ulaControl = 0;        /* $FE20 — Video ULA control (mode/flash) */
volatile uint8_t  g_ulaPalette[16] = {0};  /* $FE21 — 16 physical/logical colour pairs */
/* $FE66/$FE67 — the User VIA T1 latch irq1v_handler reloads at the end of every band.
   This IS the raster schedule: each value is how long until the next mode/palette
   change.  Phase 5 turns the sequence into copper WAITs, so capture it now. */
volatile uint8_t  g_userT1LatchLo = 0;
volatile uint8_t  g_userT1LatchHi = 0;

/* ⭐ THE RASTER-BAND RECORD — the display, as the game itself describes it.
 *
 * The 6845 gives the geometry (one static mode: 40x26 cells of 8 lines at $5A80) but the
 * game's *colours and pixel depth* are a function of raster position, rewritten by
 * irq1v_handler once per band: ULA control to $FE20, some subset of the 16 palette
 * entries to $FE21, then the User VIA T1 latch = how long until the next band.  A frame
 * is five bands whose durations sum to 20000 us (one 312.5-line interlace-sync field).
 *
 * So rather than hard-code a band table — which would freeze a *variable* (band 1's
 * duration is the horizon, and it moves with the hills) — the model records what the
 * handler actually wrote, per cycle, and the Amiga backend turns that into copper WAITs.
 * The game stays the source of truth for its own display, which is the whole point of
 * the faithfulness seam.
 *
 * Closing rule: every band arm ends `STX $FE67 / STA $FE66` ($4F01/$4F04), so the write
 * to $FE66 is what completes a record.  The palette snapshot is CUMULATIVE because the
 * ULA's palette RAM is: bands 3 and 4 rewrite only four entries each and inherit the
 * rest from band 2.
 */
volatile uint8_t  g_bandCount = 0;             /* bands recorded this cycle */
volatile uint8_t  g_bandOverflow = 0;          /* more bands than BBC_MAX_BANDS: a finding */
volatile uint16_t g_bandDuration[BBC_MAX_BANDS] = {0};   /* microseconds until the next */
volatile uint8_t  g_bandControl[BBC_MAX_BANDS] = {0};    /* $FE20 during this band */
volatile uint8_t  g_bandState[BBC_MAX_BANDS] = {0};      /* $4F43: which band this IS */
volatile uint8_t  g_bandPalette[BBC_MAX_BANDS][16] = {{0}};

/* ⭐⭐ THE 1 MHz CLOCK BEHIND $FE68 (User VIA T2), Revs's only entropy source.
   Fields, counted here, are the one time base every backend already has: a band cycle is
   one 20000 us field by construction (the five band durations sum to it — see the record
   above).  A backend with something finer overrides Platform::hwMicros(). */
static uint32_t s_fieldMicros = 0;

extern "C" {
/* Proof the model is live, for a probe: a constant $0 and a working counter are
   indistinguishable from the picture, which is exactly how the old model survived. */
unsigned long g_viaT2Reads = 0;
uint8_t       g_viaT2Last  = 0;
}

/* Start of a band cycle: the backend calls this immediately before dispatching the five
   fireIrq1v() bands that make up one field, so the record describes ONE field and a
   half-written cycle can never be read as a whole one. */
void bbc_begin_band_cycle(void)
{
    g_bandCount = 0;
    s_fieldMicros += 20000u;   /* one PAL field of 1 MHz timer ticks */
}

/* Reads of an I/O address the Phase 2 inventory does not list.  A finding, not noise. */
volatile unsigned long g_hwUnknownReads = 0;
volatile uint16_t      g_hwUnknownAddr  = 0;
}

/* ⭐ THE DETERMINISTIC FALLBACK, for a backend with no fine clock and for `REVS_FIXED_RNG`.
   ⚠ Field count ALONE is not usable and that is arithmetic, not taste: 20000 mod 256 = 32, so
   the low five bits of the derived counter never change and `AND #7` (the idle jitter) is a
   constant again — the very bug this file just fixed, wearing a clock's clothes.  So the
   fallback also steps by 251 us per read: coprime with 256, hence all 256 low bytes, and
   deterministic given a fixed sequence of reads.
   ⚠ This one IS correlated with how often the game asks, which a real T2 is not.  That is
   acceptable only where determinism is the explicit goal (a pinned perf run must drive the same
   simulation in every build); it is not the faithful model, and a backend that can read a real
   clock overrides it. */
uint32_t Platform::hwMicros()
{
    return s_fieldMicros + 251u * (uint32_t)g_viaT2Reads;
}

/* --------------------------------------------------------------------------- */
uint8_t Platform::hwRead(uint16_t addr)
{
    switch (addr) {

    /* System VIA IFR.  Bit 1 = vsync (CA1).  Only hw_init's alignment spin reads it.
       A real VIA latches the flag until it is cleared by writing IFR or touching ORA;
       Revs never clears it, so a latch-forever model would make the spin a no-op on
       every later boot.  Modelled as edge-consuming instead: set by the backend's frame
       boundary, cleared by the read that reports it.  Bit 7 (IRQ summary) is set with it
       because a real IFR sets bit 7 whenever any enabled flag is up — `BIT` puts bit 7
       in N, and nothing here branches on N, but keeping it right costs nothing. */
    case 0xFE4D:
        if (vsyncElapsed()) return 0x82;
        return 0x00;

    /* User VIA IFR.  Bit 6 = T1 timeout — the game's own interrupt source.  The value is
       owned by the backend's ISR shim (fireIrq1v), which raises it immediately before
       dispatching the handler; the handler acknowledges it by writing $FE6D back. */
    case 0xFE6D:
        return m_userT1Pending ? 0xC0 : 0x00;

    /* System VIA ACR, read once by hw_init ($4E1B: ORA $FE4B / STA $FE4B) to set the T1
       continuous-interrupt bit without disturbing the rest.  Answering 0 means the
       read-modify-write writes exactly the bit it wanted, which is what it is for. */
    case 0xFE4B:
        return 0x00;

    /* ⭐⭐ User VIA T2 counter, low byte ($FE68 = User VIA base + 8 — the COUNTER, not port B;
       an older comment here had the register wrong and answered a constant $0).
       This is Revs's ONLY entropy source, read at six sites: the gravel/skid trigger
       ($0E7C, CMP #$3F), $274E, the starter's catch delay ($498C, AND $09), the idle-rev
       jitter ($49BD, AND #7), $4C06 and $635F — plus the mirrors' engine shudder in the
       $7B00 overlay ($7FB6).  A constant 0 is not a harmless stub: the engine caught on the
       FIRST crank poll instead of after a random delay, the idle sat at exactly $28 where a
       real BBC reads $2C, and the gravel trigger fired on EVERY call.
       ⭐ THE MODEL IS A CLOCK, NOT A PRNG, and that is measured, not assumed:
       `make refloop --park --via-t2` samples what the real 6502 got at each site, and at
       $635F (32 reads in one loop) 22 of 31 successive samples land EXACTLY on
       "previous value minus the microseconds that elapsed", 24 of 31 within +-2.  T2 free-runs
       down at 1 MHz and keeps counting past its timeout, so the low byte is the elapsed-time
       low byte, negated.  The origin is arbitrary — only 8 bits are ever observed. */
    case 0xFE68: {
        const uint8_t v = (uint8_t)(0u - hwMicros());   /* virtual: the backend's finest clock */
        g_viaT2Last = v;
        g_viaT2Reads++;
        return v;
    }

    /* Read-back of the two IERs.  Revs writes them ($FE4E, $FE6E) but never reads them;
       answer 0 rather than fall into the unknown-read counter if that ever changes. */
    case 0xFE4E:
    case 0xFE6E:
        return 0x00;

    default:
        /* ⚠ Outside the 19-register inventory.  Either the inventory missed something or
           a self-modified address landed here — both worth knowing about. */
        g_hwUnknownAddr = addr;
        g_hwUnknownReads++;
        return 0x00;
    }
}

/* --------------------------------------------------------------------------- */
void Platform::hwWrite(uint16_t addr, uint8_t val)
{
    switch (addr) {

    /* Video ULA control ($FE20): screen mode, flash, cursor width.  Written by
       irq1v_handler once per raster band ($88 for the sky band, $C4 for the road) and
       by OSBYTE 154.  Phase 5 turns the per-band value into a copper BPLCON/palette
       change; for now record the latest. */
    case 0xFE20:
        bbc_ula_control_write(val);   /* the one definition — bbc_screen.h */
        break;

    /* Video ULA palette ($FE21): the high nibble is the logical colour, the low nibble
       the (inverted) physical colour.  Revs rewrites all 16 entries per band, which is
       precisely what makes this a copper job rather than a CPU one. */
    case 0xFE21:
        bbc_ula_palette_write(val);   /* the one definition — bbc_screen.h */
        break;

    /* User VIA T1 counter/latch — the raster schedule (see the extern above).
       ⭐ $FE66 is the LAST write of every band arm ($4F04, after $4F01's $FE67), so it is
       what closes a band record: duration = the latch + 2 for the 6522's own reload
       cycles, which is what makes the five durations sum to exactly one 20000 us field
       instead of missing it by 10.  Everything else about the band — mode and palette —
       is already in g_ulaControl/g_ulaPalette by now, so the record is a snapshot. */
    case 0xFE64: case 0xFE66:
        g_userT1LatchLo = val;
        if (addr == 0xFE66) {
            if (g_bandCount < BBC_MAX_BANDS) {
                unsigned b = g_bandCount;
                g_bandDuration[b] = (uint16_t)(((unsigned)g_userT1LatchHi << 8) |
                                                g_userT1LatchLo) + 2u;
                g_bandControl[b]  = g_ulaControl;
                /* ⭐ WHICH band this is, straight from the game's own counter.  $4F43 is
                   INC'd at $4F07, AFTER this write, so it still holds the state whose arm
                   just ran — i.e. the band identity.  Recording it rather than trusting
                   arrival order is what makes the raster anchor unambiguous when a cycle
                   is dispatched starting from a state other than 0. */
                g_bandState[b]    = mem[0x4F43];
                for (unsigned i = 0; i < 16; i++) g_bandPalette[b][i] = g_ulaPalette[i];
                g_bandCount = (uint8_t)(b + 1);
            } else {
                /* More bands in one field than the five the handler has arms for.  Not
                   absorbed: a sixth band means this model has the cycle wrong. */
                g_bandOverflow++;
            }
        }
        break;
    case 0xFE65: case 0xFE67:
        g_userT1LatchHi = val;
        break;

    /* User VIA IFR write = acknowledge the flagged interrupts.  irq1v_handler does
       `STA $FE6D` with A = $40 to clear its own T1 flag. */
    case 0xFE6D:
        if (val & 0x40) m_userT1Pending = false;
        break;

    /* System VIA IFR write = acknowledge.  Nothing here latches, so nothing to clear. */
    case 0xFE4D:
        break;

    /* ⭐ 6845 CRTC ($FE00 address register / $FE01 data).  hw_init ($4DDD) programs all 14
       registers from the table at $4F0F, and that is the moment the machine STOPS being a
       teletext screen: it is how Revs leaves MODE 7 for the custom race mode, bypassing the
       MOS entirely (which is why no VDU 22 accompanies it).
       ⚠ This matters to the PIXEL, not just to bookkeeping: $7C00-$7FFF is the MODE 7 screen
       AND the dashboard code overlay, time-multiplexed (docs/static-map.md), so a renderer that
       kept treating it as a page would draw executable code as mosaics.  Leaving MODE 7 is a
       hardware event and it is detected as one, rather than trusting the game's $64 flag. */
    case 0xFE00:
    case 0xFE01:
        tt_set_active(0);
        break;

    /* Everything else Revs writes — System VIA T1/T2 and ACR/IER ($FE45/$FE46/$FE47/
       $FE4B/$FE4E), User VIA ACR/IER/ORB ($FE6B/$FE6E/$FE69), the ROM latch — configures
       a chip this port does not have.  Dropped deliberately: the port's own 50 Hz VERTB
       interrupt replaces the timer these registers program.  Not counted as unknown,
       because they ARE in the inventory; they simply have no consequence here. */
    default:
        break;
    }
}

/* --------------------------------------------------------------------------
   The IRQ1V shim.

   ⚠ irq1v_handler ends in RTI, which the transpiler emits as `PLP(); return;` — the C
   return supplies the PC, but the P byte still has to be on the 6502 stack, because a
   real IRQ pushed it.  Call the handler without pushing one and cpu.S walks backwards by
   one byte per frame: 256 frames later the stack wraps into page 1's live data and the
   game corrupts itself long after the code that caused it.  That is exactly the class of
   bug the postmortem calls expensive-to-find, so it is paid for here, once.
   -------------------------------------------------------------------------- */
void Platform::fireIrq1v(void)
{
    /* ⭐ NOT UNTIL THE GAME HAS CLAIMED IRQ1V.  The backend installs its vblank before
       engine_main() runs, so the first interrupts arrive at an engine that has not
       initialised — and irq_band_state ($4F43) starts at 0 in the image, which is a VALID
       band, so the handler happily begins stepping a state machine whose timers, palette
       tables and screen have not been set up.  Measured: the band counter ended at $FE,
       a state the dispatch has no arm for, so the 50 Hz body then never ran again while
       everything else looked healthy.
       On the BBC this cannot happen — the handler is only in the chain after hw_init's
       final `STA $0204` ($4E54) — so the faithful gate is exactly that write. */
    if (mem[0x0204] != 0x5C || mem[0x0205] != 0x4E) return;

    m_userT1Pending = true;      /* $FE6D bit 6: this interrupt is ours, not the MOS's */

    /* ⚠ BEFORE the push, not after: the handler ends in RTI, which pops the P this pushes, so a
       balanced handler leaves S where it was BEFORE the push.  Sampling after it reported an
       imbalance on all 13275 interrupts — the instrument's off-by-one, not the machine's. */
    const uint8_t s0 = cpu.S;

    PUSH(P_pack());              /* what the 6502's IRQ sequence would have pushed */

    /* ⭐⭐ AND WHAT THE MOS'S IRQ ENTRY WOULD HAVE DONE: `STA $FC`.
       irq1v_handler ends `PLA / TAX / LDA $FC / RTI` ($4F0A) — it saves only X for itself
       and gets the INTERRUPTED A back out of $FC, because on a real BBC the OS's interrupt
       entry stashes A there before `JMP (IRQ1V)`.  Nothing here used to write $FC, so it
       held 0 forever and every ISR return silently set A = 0.
       ⚠ On the host that is invisible: tickVBI() fires at the top of renderFrame, where no
       foreground routine is mid-computation.  On the Amiga the VERTB preempts the main loop
       at an arbitrary instruction — including inside the unrolled fill chain in the
       $7B00-$7FFF overlay, whose whole mechanism is that A CARRIES the previous cell's byte
       across elements whose column source is zero ($7C00: `LDY src / BEQ skip / ... /
       skip: STA ($70),Y`).  Zero A mid-chain and every remaining cell of that display line
       is written 0, i.e. a BLACK RUN TO THE RIGHT EDGE — the horizon stripes, on scattered
       lines, on the Amiga only.  A real BBC does not show them (confirmed with
       `make refloop`: band 2 never holds a zero run longer than 3 cells). */
    mem[0x00FC] = cpu.A;

    cpu.I = 1;

    /* The contract measured on hardware: all three come back unchanged.  Checked, not assumed
       — three byte compares per frame, against a class of bug that is otherwise invisible
       until it shows up as a wrong pixel in an unrelated routine. */
    const uint8_t a0 = cpu.A, x0 = cpu.X, y0 = cpu.Y;

    /* ⭐⭐ AND THE C-SIDE STATE THE 6502 DOES NOT HAVE, which is the part a register contract
       cannot cover.  `cpu_unwind` models the ONE routine that returns two levels up ($2F7E's
       TSX/INX/INX/TXS + RTS, the exit from the four unrolled road-span chains): the drop sets the
       flag and the call site of the dropped frame consumes it (src/cpu/cpu.h).  On a 6502 that
       state is the STACK POINTER, saved and restored by the interrupt sequence itself.  Here it is
       a global, and an interrupt can land in the window between the set and the consume — so if
       anything in the handler's own call tree consumed or set it, the interrupted chain would
       either keep plotting past its exit (a run to the RIGHT EDGE in the carried colour: green) or
       exit early (leaving the rest of the line black).  Exactly the residual artefact's two forms.
       Saved and restored here, and COUNTED, because "the handler never uses it" is a claim about
       reachability through 33 sites and one indirect dispatch — not something to assume. */
    const uint8_t unwind0 = cpu_unwind;
    if (unwind0) g_irqUnwindPending++;   /* preempted mid-drop: the window is real, count it */

    /* ⭐ Which band this call is about to service, read BEFORE the handler steps $4F43. */
    const int band = mem[0x4F43];

    PROBE_IRQ_NULL();          /* the control: the same bracket around no work at all */
    PROBE_IRQ_BEGIN();
    irq1v_handler();
    PROBE_IRQ_END(band);

    if (cpu_unwind != unwind0) { g_irqUnwindTouched++; cpu_unwind = unwind0; }
    if (cpu.S != s0) g_irqStackImbalance++;   /* the handler must leave the 6502 stack as it found it */

    uint8_t which = 0;
    if (cpu.A != a0) which |= 1;
    if (cpu.X != x0) which |= 2;
    if (cpu.Y != y0) which |= 4;
    if (which) { g_irqClobberCount++; g_irqClobberWhich |= which; }
}

/* ---------------------------------------------------------------------------
   ⭐⭐ ONE FIELD OF THE BAND CYCLE — and the fast path that skips almost all of it.

   WHAT THE MEASUREMENT SAID (2026-08-17, amiga/band_prof.gdb, a bracket per band arm).
   One field costs 6.1 ms of its 20 ms budget, split:

       band 0   792 us      band 1   735 us      band 2   838 us
       band 3   675 us      band 4  1128 us  (of which $52A4 is 233)
       the fireIrq1v shim, five times over:            ~1720 us

   So 96% of the "50 Hz body" is machinery and 233 us is game work.  The earlier reading of
   this row — "~810 us a call for sixteen palette stores, unexplained" — was an AVERAGE over
   five arms that do different jobs; band 3 writes FOUR palette bytes and still costs 675 us,
   which is what says the cost is per-CALL, not per-store.  An empty bracket on the same path
   reads 33 us, so none of this is the instrument.

   WHY IT CAN BE SKIPPED, AND WHY THAT IS STILL FAITHFUL.  The five interrupts do not draw.
   Each one repaints the Video ULA for the band about to be scanned and reloads User VIA T1
   with that band's duration — a raster split, which on this machine the COPPER executes.
   The port never switches a palette from the CPU: bbc_hw.cpp records what the handler wrote
   and RevsScreen re-emits it as copper WAITs.  So the cycle's whole output is the RECORD, and
   the record is a pure function of five palette tables ($3458/$3468/$3478/$347C), the horizon
   ($4F1F/$4F20) and the state it starts from ($4F43).  Everything else it leaves behind is
   idempotent: $4F21/$4F22 are the horizon remainder (same inputs, same value), $4F43 returns
   to 0, and the 6502 stack balances.  Run it twice on unchanged inputs and the second run is
   observably a no-op — apart from $52A4, which is the game work and therefore always runs.

   MoveHorizon ($4F44) is a MAIN-LOOP routine, so at this framerate the inputs change about
   once every 25 fields and the other 24 re-derive a record byte-for-byte identical to the one
   already sitting in g_band*.

   ⚠ The comparison is the WHOLE 43 bytes, not a hash: a digest that collides here would put
   the wrong palette on the screen for a frame, and 43 byte compares cost ~1% of what they
   save.  ⚠ The cache is seeded invalid and the fast path also demands a cached count of 5 —
   the count RevsScreen requires (g_bandRejects) — so a half-built record can never be
   re-asserted as a whole one.

   `make BANDSKIP=0` is the control, and it is the OLD CODE, not a restructure of it.
   --------------------------------------------------------------------------- */
extern "C" {
volatile unsigned long g_bandSkips = 0;   /* fields whose record was reused */
volatile unsigned long g_bandRuns  = 0;   /* fields that ran the real cycle */
/* BANDCHECK only: predictions of "reuse" made, and how many the real cycle contradicted. */
volatile unsigned long g_bandCheckChecks   = 0;
volatile unsigned long g_bandCheckMismatch = 0;
/* ⭐⭐ THE STIMULUS, counted over the whole run instead of sampled at the end.  The horizon is
   the one input that moves while driving ($4F44 MoveHorizon, a main-loop routine), so it is what
   makes a reuse test non-trivial — and a run with zero here has proved only that a static record
   stays static.  ⚠ It must be a COUNT, not a state read: the first attempt read $61/$63/$3C after
   the run and concluded the car was parked, when in fact those are the values a car has AFTER it
   leaves the track and stalls, i.e. at the end of a run that drove the whole way. */
volatile unsigned long g_bandHorizonMoves  = 0;
}

/* $3458 band 2 (16) · $3468 band 0 (16) · $3478 band 3 (4) · $347C band 4 (4) ·
   the horizon (2) · the entry state (1).  Band 1's palette is a computed constant
   sequence and its mode writes are literals, so neither is an input. */
enum { BAND_INPUT_BYTES = 43 };
static unsigned char s_bandInputs[BAND_INPUT_BYTES];
static bool          s_bandInputsValid = false;
static unsigned char s_bandCachedCount = 0;

static bool band_inputs_unchanged(void)
{
    unsigned char buf[BAND_INPUT_BYTES];
    unsigned n = 0, i;
    for (i = 0; i < 16; i++) buf[n++] = mem[0x3458 + i];
    for (i = 0; i < 16; i++) buf[n++] = mem[0x3468 + i];
    for (i = 0; i < 4;  i++) buf[n++] = mem[0x3478 + i];
    for (i = 0; i < 4;  i++) buf[n++] = mem[0x347C + i];
    buf[n++] = mem[0x4F1F];
    buf[n++] = mem[0x4F20];
    buf[n++] = mem[0x4F43];

    bool same = s_bandInputsValid;
    for (i = 0; i < n; i++) {
        if (buf[i] != s_bandInputs[i]) {
            /* The horizon pair sits at the end, just before the entry state — count it on its
               own, because it is the input that makes this test worth running (see the counter). */
            if (i == BAND_INPUT_BYTES - 3 || i == BAND_INPUT_BYTES - 2) g_bandHorizonMoves++;
            same = false;
            s_bandInputs[i] = buf[i];
        }
    }
    s_bandInputsValid = true;
    return same;
}

unsigned Platform::fireIrq1vField(void)
{
    /* ⚠⚠ BEFORE the IRQ1V gate, not after, and `make determinism` is why: this call does two
       jobs — it zeroes the band record AND advances the 1 MHz field clock behind $FE68, the
       game's only entropy source.  Both backends used to call it unconditionally and let
       fireIrq1v apply the gate internally, so the clock ticked through the pre-claim fields
       too.  Hoisting the gate above it looked like a tidy-up and silently re-seeded the RNG:
       the whole-corpus differential diverged at mem[$0004] within 300 frames. */
    bbc_begin_band_cycle();

    /* The same gate fireIrq1v applies, hoisted out of the loop: until the game has claimed
       IRQ1V there is no cycle to run and no record worth caching. */
    if (mem[0x0204] != 0x5C || mem[0x0205] != 0x4E) return 0;

#ifdef REVS_BAND_CHECK
    /* ⭐⭐ THE ORACLE (`make BANDCHECK=1`), and it exists because the obvious test was blind.
       Dropping the horizon from the digest — a sabotage that must break the picture — PASSED
       the host's 300-frame byte differential, because a host run is only ~36 FIELDS and the
       horizon never moved in it.  A skip that is never wrong on static inputs proves nothing
       about the case the whole change is about.
       So: always run the REAL cycle, and separately ask what the predicate would have decided.
       Whenever it says "reuse", the record the real cycle just built must equal the one it
       would have reused.  Exact, no double-run of $52A4, and it fires on a moving car —
       which on this port means the target, not the host. */
    {
        unsigned char  pCount = s_bandCachedCount;
        unsigned short pDur[BBC_MAX_BANDS];
        unsigned char  pCtl[BBC_MAX_BANDS], pSt[BBC_MAX_BANDS], pPal[BBC_MAX_BANDS][16];
        for (unsigned b = 0; b < BBC_MAX_BANDS; b++) {
            pDur[b] = g_bandDuration[b]; pCtl[b] = g_bandControl[b]; pSt[b] = g_bandState[b];
            for (unsigned c = 0; c < 16; c++) pPal[b][c] = g_bandPalette[b][c];
        }
        const bool predicted = (pCount == 5) && band_inputs_unchanged();

        unsigned n = 0;
        for (int band = 0; band < 8; band++) {
            fireIrq1v(); n++;
            if (mem[0x4F43] == 0) break;
        }
        s_bandCachedCount = g_bandCount;
        g_bandRuns++;

        if (predicted) {
            g_bandCheckChecks++;
            bool bad = (g_bandCount != pCount);
            for (unsigned b = 0; !bad && b < g_bandCount; b++) {
                if (pDur[b] != g_bandDuration[b] || pCtl[b] != g_bandControl[b] ||
                    pSt[b]  != g_bandState[b])   { bad = true; break; }
                for (unsigned c = 0; c < 16; c++)
                    if (pPal[b][c] != g_bandPalette[b][c]) { bad = true; break; }
            }
            if (bad) g_bandCheckMismatch++;
        }
        return n;
    }
#endif

#ifndef REVS_NO_BANDSKIP
    if (s_bandCachedCount == 5 && band_inputs_unchanged()) {
        /* The record in g_band* is still the right answer; only its count was just zeroed. */
        g_bandCount = s_bandCachedCount;

        /* Band 4's arm, and nothing else.  The register setup is the one the twin performs
           at $4EF5 — measured to make no difference to the differential, kept for the same
           reason it is kept there.  A/X/Y are saved around it because on the real path the
           closing RTI restores them (A via $FC) and the measured contract is that all three
           survive an engine-context interrupt. */
        const uint8_t a0 = cpu.A, x0 = cpu.X, y0 = cpu.Y;
        cpu.A = mem[0x347C]; cpu.X = 0xFF; cpu.N = 1; cpu.Z = 0; cpu.C = 1;
        PROBE_PHASE(PROBE_PHASE_BODYARM);
        FUN_52a4();
        PROBE_PHASE(PROBE_PHASE_DRAIN);
        cpu.A = a0; cpu.X = x0; cpu.Y = y0;

        g_bandSkips++;
        return 0;
    }
#endif

    unsigned dispatched = 0;
    /* ⚠ Bounded, because an unbounded loop over a state machine the game can change is how a
       frame gets eaten.  8 = the five real bands plus slack; overrunning drops the rest of
       this field's bands rather than hanging. */
    for (int band = 0; band < 8; band++) {
        fireIrq1v();
        dispatched++;
        if (mem[0x4F43] == 0) break;      /* $4F43 = irq_band_state; 0 = cycle complete */
    }
    s_bandCachedCount = g_bandCount;
    g_bandRuns++;
    return dispatched;
}

/* Default: no display, so every check reports a frame boundary.  A backend with a real
   vblank overrides this. */
bool Platform::vsyncElapsed() { return true; }
