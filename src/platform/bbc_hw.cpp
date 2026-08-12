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
#include "../cpu/cpu.h"

extern "C" void irq1v_handler(void);   /* $4E5C, from the generated transliteration */

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
/* Reads of an I/O address the Phase 2 inventory does not list.  A finding, not noise. */
volatile unsigned long g_hwUnknownReads = 0;
volatile uint16_t      g_hwUnknownAddr  = 0;
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

    /* User VIA port B ($FE68) — the BBC user port, read six times by FUN_635D.  Nothing
       is wired to it here.  ⚠ Listed rather than left to the unknown counter because
       "unknown" should mean "not in the Phase 2 inventory", and this is. */
    case 0xFE68:
        return 0x00;

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
        g_ulaControl = val;
        break;

    /* Video ULA palette ($FE21): the high nibble is the logical colour, the low nibble
       the (inverted) physical colour.  Revs rewrites all 16 entries per band, which is
       precisely what makes this a copper job rather than a CPU one. */
    case 0xFE21:
        g_ulaPalette[(val >> 4) & 0x0F] = val;
        break;

    /* User VIA T1 counter/latch — the raster schedule (see the extern above). */
    case 0xFE64: case 0xFE66:
        g_userT1LatchLo = val;
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
    PUSH(P_pack());              /* what the 6502's IRQ sequence would have pushed */
    cpu.I = 1;
    irq1v_handler();
}

/* Default: no display, so every check reports a frame boundary.  A backend with a real
   vblank overrides this. */
bool Platform::vsyncElapsed() { return true; }
