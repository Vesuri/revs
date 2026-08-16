/* revs_native.c — FAITHFUL native twins.
 *
 * Every function here replaces a transliterated one of the same name in revs_gen.c, which
 * keeps its body under a `__t6502` suffix as the validation ORACLE.  `make validate` runs
 * both on the same randomised pre-state and diffs the whole of mem[] plus the registers the
 * fixture declares live; a twin with no fixture FAILS the run rather than passing vacuously.
 *
 * The rules this file lives by are docs/faithfulness-seam.md and docs/phases.md §1a:
 *   - a typed `_core(...)` doing the work, plus the `void <name>(void)` 6502-ABI shim;
 *   - real locals only where the mem[] cell is PROVEN dead, with the proof written down;
 *   - BBC hardware writes are #ifdef-guarded, never deleted — the removed one is a hole in
 *     the differential, not a saved cycle;
 *   - say what the routine COMPUTES.  The instruction-by-instruction record already exists
 *     next door in revs_gen.c and does not need re-narrating.
 *
 * Linked into BOTH backends.  Anything genuinely Amiga-only belongs in
 * src/platform/amiga/revs_native_amiga.cpp instead.
 */
#include "../cpu/cpu.h"
#include "../cpu/bus.h"
#include "revs_decl.h"
#define REVS_MEM_ALIASES
#include "mem.h"
#include "../platform/platform_c.h"
#include "../platform/bbc_screen.h"   /* bbc_ula_palette_write / bbc_ula_control_write */
#include "../platform/probe.h"        /* PROBE_PHASE(): the phase-29 body-arm split */

/* ===========================================================================
   $4E5C  irq1v_handler — THE RASTER-BAND STATE MACHINE, and the 50 Hz body's arm
   ===========================================================================

   WHAT IT COMPUTES.  Revs owns IRQ1V and drives its own display from a User VIA T1
   timer.  One PAL field is FIVE interrupts; each one repaints the Video ULA for the
   band that is about to be scanned out and reloads T1 with how long that band lasts.
   The counter at $4F43 says which band is next, and the last band's arm is also where
   the whole 50 Hz game body runs (FUN_52a4 — physics, opponents, and display lines
   120-143).  So this one function is both the display's colour schedule and the
   simulation's clock, and it is 51% of the port's frame (docs/perf-method.md).

   THE BANDS, in the order the counter walks them:

     0   sky-top     MODE 4, all sixteen palette entries from $3468;  next latch $0FC4
     1   sky         MODE 5, the sixteen entries 3,$13..$F3 (one flat colour);  then the
                     horizon split — band 1 runs for $4F1F/$4F20 microseconds and band 2
                     gets the remainder of a fixed $153C, stashed in $4F21/$4F22.
                     ⭐ A zero-height band 1 (the split underflows) FALLS THROUGH into
                     band 2's arm in the same interrupt, which is how the horizon can sit
                     at the very top of the screen.  Bands 2→3 fall through the same way.
     2   horizon     the four-colour palette at $3458;  latch = the remainder computed above
     3   track       four entries from $3478 (colour 1 → red);  next latch $1E00
     4   dashboard   four entries from $347C (colour 3 → cyan), then FUN_52a4, then the
                     User VIA ORB poke and the wrap back to band 0;  next latch $0B16
     $FF             the arm is skipped; the counter just wraps to 0 and takes band 0's
                     latch.  Any OTHER negative counter does nothing at all.

   WHAT IT LEAVES BEHIND.  In mem[]: the pushed X on the 6502 stack (and back), the
   horizon remainder $4F21/$4F22, and the band counter $4F43.  Nothing else.  In the
   hardware model: $FE6D (the interrupt acknowledge), $FE20/$FE21 (the ULA),
   $FE66/$FE67 (the next band's duration — the write to $FE66 is what closes a band
   record in bbc_hw.cpp) and $FE69 once per field.

   EXIT CONTRACT.  A, X and Y all come back as the interrupted code left them — measured
   on a real BBC over 2858 engine-context interrupts (`make refloop --irq-abi`) and
   asserted at the seam.  A arrives via $FC, which the MOS's own IRQ entry wrote; the
   handler's `PLA/TAX` restores X; Y is never touched.  Flags and S come from the RTI.

   ⭐ WHY THIS IS TWIN #1, AND WHERE THE TIME WAS.  ~80 6502 instructions, of which 48
   are `STA $FE21` in the three palette loops.  The transliteration spent its time on
   per-instruction N/Z bookkeeping against a struct in memory and on a C-bridge +
   virtual-dispatch + switch per hardware byte; none of that is work the BBC did.  The
   twin keeps every hardware write and drops the bookkeeping: the only registers that
   survive the RTI are recomputed at the end, so the intermediate ones are dead.
   =========================================================================== */

/* One palette table → the ULA, in the 6502's order (last entry first).  The order is
   observable: entries share logical colours, so the LAST write to a given high nibble
   wins.  $3468/$3458 are 16 entries, $3478/$347C are 4. */
static void ula_palette_table(unsigned table, int last)
{
    int x;
    for (x = last; x >= 0; x--)
        bbc_ula_palette_write(mem[table + x]);
}

void irq1v_handler(void)
{
    unsigned char state;
    unsigned char latch_lo, latch_hi;   /* what $4F01 writes to $FE66/$FE67 */
    unsigned char pulled;

    /* $4E5C — is this interrupt ours?  User VIA IFR bit 6 is the T1 timeout.  If it is
       clear the MOS's previous IRQ1V handler gets the interrupt, and this one never
       touches the machine.  ⚠ The register state on THAT path is live: the transliteration
       leaves A = 0 from the AND, and the exit is a JMP, not an RTI, so nothing restores it. */
    if ((bus_read(0xFE6D) & 0x40) == 0) {
        cpu.A = 0; cpu.N = 0; cpu.Z = 1;
        platform_indirect_jmp((unsigned short)(mem[MEM_saved_irq1v] |
                                               ((unsigned short)mem[MEM_saved_irq1v + 1] << 8)));
        return;
    }
    bus_write(0xFE6D, 0x40);      /* $4E63 acknowledge our own T1 flag */

    PUSH(cpu.X);                  /* $4E66 TXA/PHA — X is the arms' loop counter */
    cpu.D = 0;                    /* $4E68 CLD */

    /* $4E69 — dispatch on the band counter. */
    state = irq_band_state;
    if (state & 0x80) {
        /* $4E92.  Only $FF is a band; every other negative value leaves the machine
           entirely alone, which is how a half-initialised counter fails safe. */
        if (state != 0xFF) goto rti;
        irq_band_state = 0;                    /* $4E96 INC, and $4F07 INCs it again */
        goto band0_latch;                      /* $4E8C — band 0's timing, no palette */
    }
    switch (state) {
    case 0:  goto band0;
    case 1:  goto band1;
    case 2:  goto band2;
    case 3:  goto band3;
    default: goto band4;                       /* $4E7A: anything above 3 */
    }

band0:  /* $4E7C — the two blanked text rows at the top, in MODE 4 */
    bbc_ula_control_write(BBC_ULA_MODE4);
    ula_palette_table(0x3468, 15);
band0_latch:  /* $4E8C */
    latch_lo = 0xC4; latch_hi = 0x0F;
    goto latch;

band1:  /* $4E9B — the sky: MODE 5 with all sixteen entries the same colour */
    bbc_ula_control_write(BBC_ULA_MODE5);
    {
        unsigned char v = 0x03;
        int i;
        for (i = 0; i < 16; i++) { bbc_ula_palette_write(v); v = (unsigned char)(v + 0x10); }
    }
    /* $4EAA — the horizon split.  MoveHorizon ($4F44) puts band 1's duration in
       $4F1F/$4F20; band 2 takes the remainder of a fixed $153C and it is kept in
       $4F21/$4F22 for band 2's own arm to load.  A borrow (the sky is longer than the
       whole split) means band 2 has no height at all, so its arm runs now. */
    {
        unsigned sky = (unsigned)mem[0x4F1F] | ((unsigned)mem[0x4F20] << 8);
        unsigned rest = (0x153Cu - sky) & 0xFFFFu;
        mem[0x4F21] = (unsigned char)rest;
        mem[0x4F22] = (unsigned char)(rest >> 8);
        latch_lo = mem[0x4F1F]; latch_hi = mem[0x4F20];
        if (sky <= 0x153Cu) goto latch;        /* $4EC1 BCS: no borrow */
    }
    /* fall through — zero-height band 2 */

band2:  /* $4EC3 — the horizon: black / blue / white / green */
    ula_palette_table(0x3458, 15);
    latch_lo = mem[0x4F21]; latch_hi = mem[0x4F22];
    if (latch_hi != 0) goto latch;             /* $4ED4 BNE, on the LDX of $4F22 */
    /* fall through — zero-height band 3 */

band3:  /* $4ED6 — the track: colour 1 becomes red */
    ula_palette_table(0x3478, 3);
    latch_lo = 0x00; latch_hi = 0x1E;
    goto latch;

band4:  /* $4EE7 — the dashboard: colour 3 becomes cyan, and then the GAME RUNS */
    ula_palette_table(0x347C, 3);
    irq_band_state = 0xFF;                     /* $4EF2 STX, X == $FF; $4F07 wraps it to 0 */

    /* FUN_52a4 is an ordinary JSR target, so it is entered with whatever the 6502 state
       was: A = the last palette byte fetched, X = $FF from the DEX that ended the loop,
       N/Z from that DEX, C = 1 from the CMP #3 that dispatched here, Y from the
       interrupted foreground.  Reproduced explicitly because the twin does not otherwise
       maintain the register file.
       ⚑ MEASURED, not assumed: falsifying A, X, or N/Z/C here changes NOTHING in the
       differential (`make validate FN=irq1v`, 128 randomised band-4 cases, with the same
       harness catching a dropped `FUN_52a4()` call at once) — the body reloads all of
       them before use.  Kept anyway: it costs five stores per FIELD, and "the callee does
       not read it today" is a claim about a 400-routine subtree. */
    cpu.A = mem[0x347C]; cpu.X = 0xFF;
    cpu.N = 1; cpu.Z = 0; cpu.C = 1;

    PROBE_PHASE(PROBE_PHASE_BODYARM);
    FUN_52a4();
    PROBE_PHASE(PROBE_PHASE_DRAIN);

    bus_write(0xFE69, 0xFF);                   /* $4EFA User VIA ORB */
    latch_lo = 0x16; latch_hi = 0x0B;

latch:  /* $4F01 — how long until the next band.  $FE66 LAST: bbc_hw.cpp closes the band
           record on it, and the record must already hold this band's mode and palette. */
    bus_write(0xFE67, latch_hi);
    bus_write(0xFE66, latch_lo);
    irq_band_state++;

rti:    /* $4F0A — PLA / TAX / LDA $FC / RTI */
    PULL(pulled);
    cpu.X = pulled;
    cpu.A = mem[0x00FC];
    PLP();
}
