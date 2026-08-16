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
#include "../platform/shape.h"        /* PROBE_SHAPE_DASH_UNIT(): the §7a unit counter */
#include "../platform/revs_plot.h"    /* REVS_PLOT_*: the direct-to-bitplane run plotter */

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

/* ===========================================================================
   $7BE2  dashboard_sweep — THE 40-UNIT COLUMN CHAIN, AND ITS 42 PATCH SITES
   ===========================================================================

   WHAT IT COMPUTES.  The dashboard and the wing mirrors are not drawn where they
   are computed: every producer writes a *source byte* into one of forty $80-spaced
   blocks at $3000..$4380, indexed by screen column, and this routine is the single
   consumer that turns those into screen bytes.  One "unit" is one (column, cell)
   pair — read the source, and if it is non-zero clear it and translate it through
   the glyph table at $6000; either way store the byte that is now in A.  Forty
   units per column, walking the column downwards, and A carries between them, so a
   cell whose source is zero repeats whatever the cell above it drew.  That is the
   whole drawing model, and it is why the chain has to run in order.

   THREE PHASES, and they differ only in how far down the column the chain runs:

     1  $7BE2, X = $4F..$2C — the full forty units, looping through $7EF3 (which
        advances $70/$71 and $72/$73 to the next column) until X reaches $2C.
     2  $7D13, X = $2B..$1C — the column is shorter here, so the driver plants an
        RTS ($60) over the `STA (zp),Y` of unit `$3150,X`, runs the chain, and
        composes the boundary byte itself out of $38D0/$3350.  It then enters chain
        B at the unit named by `$30D0,X` for the lower half.
     3  $7F18, X = $1B..$03 — as phase 2, but BOTH chains get a planted stop
        ($3150,X and $3080,X) and both get a computed start, and the driver walks
        the screen pointer itself rather than through $7EF3.

   $7BBF then puts `STA` back over the three planted RTSs and `CPX` back at $7EEE,
   so the chain leaves no patch behind.  ⚠ It does NOT reset the $7D24/$7F24/$7F7D
   *records* of where it planted, and the drivers' `CPY $7D24 / BEQ` skips the
   re-plant when the row is unchanged — so the first column of a phase can legally
   run with no stop planted at all.  Faithful, and reproduced.

   ⭐ SHAPE, MEASURED (docs/direct-bitplane-plan.md §7a): 2093 units run per sweep
   and about 83 of them change a byte, i.e. 96% of the work is a dirty test that
   finds nothing.  That 96% is the GAME's algorithm and the twin keeps every bit of
   it — deleting the scan is a representation change (a dirty list, or sprites) and
   is tracked separately.  What the twin drops is the interpreter: the unrolled
   chain became forty copies of `LDY` + N/Z bookkeeping + a `switch` over a
   self-modified opcode byte + a region-dispatch prologue, none of which the 6502
   paid for.  Here it is one indexed loop over a regular structure:

       unit i:  source block $3000 + $80*i,  screen offset 8*i,
                base pointer $70 for i < 32 and $72 for i >= 32,
                opcode slot at unit_addr(i) + $0F.

   ⚠ ONLY 29 OF THE 40 SLOTS ARE PATCHABLE, and that is a proof, not a choice: every
   writer patches only the LOW byte of its store, so it can reach one page.  Chain
   A's unit 15 slot ($7D0E) and chain B's first ten ($7D65..$7DFE) are in page $7D,
   which no writer addresses — they are plain stores and the twin must not dispatch
   on them (a randomised fixture puts garbage there, and the oracle stores anyway).

   EXIT CONTRACT.  A = $E0 and X = 3 from $7BBF/$7FAC; Y is the row phase 3 last
   composed at; the flags are live too (C from `CPX #3`, V from phase 3's
   `SEC / SBC $3080,X`), so the fixture declares AXY+flags and the twin computes
   them.  The chain's own intermediate flags are dead — every one of the four call
   sites sets N/Z with an `AND`/`CPX` before the next branch — which is why the unit
   loop keeps no flags at all.
   =========================================================================== */

/* The forty unit addresses, in the order the chain runs them.  Chain A is sixteen
   17-byte units from $7C00; chain A's tail `JMP $7D56` makes chain B's twenty-four
   the same sweep. */
#define DASH_UNIT_ADDR(i)  ((uint16_t)((i) < 16 ? 0x7C00 + 0x11 * (i)          \
                                                : 0x7D56 + 0x11 * ((i) - 16)))

/* The opcode slot of each unit, or 0 for the eleven no writer can reach (page $7D).
   ⚠ A TABLE, not `unit_addr(i) + $0F` recomputed: the oracle's slot address is a compile-
   time constant in every one of its forty copies, so a twin that derives it per unit hands
   back the arithmetic it saved.  Measured — the first version of this twin computed it and
   came out SLOWER than the transliteration (docs/perf-method.md). */
static const uint16_t g_dashSlot[40] = {
    0x7C0F, 0x7C20, 0x7C31, 0x7C42, 0x7C53, 0x7C64, 0x7C75, 0x7C86,
    0x7C97, 0x7CA8, 0x7CB9, 0x7CCA, 0x7CDB, 0x7CEC, 0x7CFD, 0,
    0,      0,      0,      0,      0,      0,      0,      0,
    0,      0,      0x7E0F, 0x7E20, 0x7E31, 0x7E42, 0x7E53, 0x7E64,
    0x7E75, 0x7E86, 0x7E97, 0x7EA8, 0x7EB9, 0x7ECA, 0x7EDB, 0x7EEC
};

/* Is `dst` a slot a writer pinned to `page` could name?  The generated oracle spells
   this as an explicit case list; both come from the same operand-encoding argument. */
static int dash_is_slot(uint16_t dst, unsigned page)
{
    int i;
    if ((dst >> 8) != page) return 0;
    for (i = 0; i < 40; i++)
        if (g_dashSlot[i] == dst) return 1;
    return 0;
}

/* A `STA $91` / `STA $60` whose own operand byte was patched.  Returns 0 (and traps)
   for a low byte that is not a slot boundary — planting mid-instruction would leave
   the real slot reading $91 and diverge from the 6502 silently. */
static int dash_plant(uint16_t site, uint16_t opnd, unsigned page)
{
    uint16_t dst = (uint16_t)(mem[opnd] | (mem[opnd + 1] << 8));
    if (!dash_is_slot(dst, page)) { platform_smc_unhandled(site, dst); return 0; }
    bus_write(dst, cpu.A);
    return 1;
}

static void dash_chain(int unit, int forced, int advance_first);

/* A `JSR` into the middle of a chain.  The two legal entry offsets are the unit start
   and unit+$05 — the latter skips the dirty test and uses the Y the caller just set up
   with a `TAY` (docs/static-map.md §Open items 10).  Returns 0 if it trapped. */
static int dash_call(uint16_t site, uint16_t opnd, unsigned page)
{
    uint16_t t = (uint16_t)(mem[opnd] | (mem[opnd + 1] << 8));
    int i;
    if ((t >> 8) == page)
        for (i = 0; i < 40; i++) {
            uint16_t u = DASH_UNIT_ADDR(i);
            if (t == u)                { dash_chain(i, 0, 0); return 1; }
            if (t == (uint16_t)(u + 5)) { dash_chain(i, 1, 0); return 1; }
        }
    platform_smc_unhandled(site, t);
    return 0;
}

/* The chain itself, $7BF7-$7F16.  Runs units `unit`..39, then the $7EEE tail, which
   either returns or steps to the next column and starts over at unit 0.
     forced         entered at unit+$05: no dirty test, cpu.Y is the glyph index
     advance_first  entered at $7EF3 (the JSRs from $7BF1 and $7D37), so the screen
                    pointer moves and $7BF7's prologue runs before any unit
   A, X and Y are the 6502's; the flags in between are dead (see the header). */
/* ⭐⭐ THE RUN ACCUMULATOR (Amiga only — revs_plot.h).  `A` carries between units, so consecutive
   cells of a scan line usually hold the SAME byte; in the Amiga's bitplane layout those cells are
   contiguous, so a run is one fill instead of N stores.  This does not change a single mem[] byte
   or a single branch of the chain — it only notices, as the chain runs, that the byte it is about
   to store is the byte it stored to the cell on the left.
   ⚠ A run never spans a scan line: the unit loop ends at 40 and each line re-enters it. */
#ifdef REVS_DIRECT_PLOT
#define PLOT_DECL()   unsigned runAddr = 0, runVal = 0, runLen = 0
#define PLOT_UNIT(dd, aa)  do {                                                     \
        if (runLen && (unsigned)(uint8_t)(aa) == runVal) runLen++;                  \
        else { if (runLen) REVS_PLOT_RUN(runAddr, runVal, runLen);                  \
               runAddr = (dd); runVal = (unsigned)(uint8_t)(aa); runLen = 1; }      \
    } while (0)
#define PLOT_FLUSH()  do { if (runLen) { REVS_PLOT_RUN(runAddr, runVal, runLen); runLen = 0; } \
                      } while (0)
#else
#define PLOT_DECL()   ((void)0)
#define PLOT_UNIT(dd, aa) ((void)0)
#define PLOT_FLUSH()  ((void)0)
#endif

static void dash_chain(int unit, int forced, int advance_first)
{
    unsigned a = cpu.A, x = cpu.X, y = cpu.Y;
    PLOT_DECL();

    for (;;) {
        int i;

        if (advance_first) {
            /* $7EF3 — next column.  Every eighth step crosses a character row, which
               costs $138: `CLC / ADC #$38` and two `ADC #1`s, carry and all.
               ⚠ THE MACROS, NOT PLAIN C, and this is a measured correction rather than
               caution: V and C are the two flags the chain can leak to its caller (every
               call site's next instruction is an `AND`/`LDA`/`CPX`, which rewrites N and
               Z but not V), and the ADCs here are the only thing in the whole chain that
               writes V.  Written as plain arithmetic they diverged on exactly the paths
               where a trap cut the driver short before its own `SBC` reset V.  Also
               decimal mode, which plain C cannot express at all. */
            LDY(mem[0x0070]);
            INY();
            TYA();
            AND(0x07);
            if (!cpu.Z) {                        /* $7EF9 BEQ $7F02 — inside a row */
                mem[0x0070] = cpu.Y;
                mem[0x0072] = cpu.Y;
            } else {
                TYA();
                CLC();
                ADC(0x38);
                mem[0x0070] = cpu.A;
                mem[0x0072] = cpu.A;
                LDA(mem[0x0071]);
                ADC(0x01);
                mem[0x0071] = cpu.A;
                ADC(0x01);
                mem[0x0073] = cpu.A;
            }
            /* $7BF7 — the column's background byte, two bits of $5F60,X through $38FC. */
            a = mem[0x38FC + (mem[0x5F60 + x] & 3)];
            advance_first = 0; unit = 0; forced = 0;
        }

        /* ⭐ THE 2093-UNIT LOOP, and everything in it is a running pointer.  The unit is
           the whole cost of the routine and the only thing that made it worth a twin, so
           the source address, the destination address and the opcode slot all step by a
           constant instead of being derived from `i`.
           ⚠ Hoisting the two destination bases out of the loop is safe by CONSTRUCTION,
           not by luck: the chain writes only its own source blocks ($3000-$43CF) and
           `base + row` with base walking up from $6700, so nothing it does can reach
           $70-$73 and change the pointer under itself. */
        {
            unsigned base0 = mem[0x0070] | ((unsigned)mem[0x0071] << 8);
            unsigned base1 = mem[0x0072] | ((unsigned)mem[0x0073] << 8);
            unsigned p = 0x3000u + ((unsigned)unit << 7) + x;
            unsigned d = (unit < 32) ? base0 + ((unsigned)unit << 3)
                                     : base1 + (((unsigned)unit - 32) << 3);
            const uint16_t* sp = &g_dashSlot[unit];

            for (i = unit; i < 40; i++) {
                unsigned slot;

                PROBE_SHAPE_DASH_UNIT();
                if (forced) {                       /* only ever the FIRST unit of a call */
                    forced = 0;
                    mem[p] = 0;                     /* $7C05 LDA #0 / STA table,X */
                    a = mem[0x6000 + y];
                } else {
                    unsigned src = mem[p];          /* $7C00 LDY table,X / BEQ */
                    if (src) { mem[p] = 0; a = mem[0x6000 + src]; }
                }

                slot = *sp++;                       /* $7C0F STA (zp),Y — or a planted RTS */
                if (slot) {
                    unsigned op = mem[slot];
                    if (op != 0x91) {
                        y = ((unsigned)i << 3) & 0xFF;   /* $7C0D LDY #<row> ran first */
                        if (op != 0x60) platform_smc_unhandled(slot, op);
                        PLOT_FLUSH();
                        goto done;
                    }
                }
                PLOT_UNIT(d, a);
#ifndef REVS_PLOT_ONLY
                bus_write((uint16_t)d, (uint8_t)a);
#endif
                p += 0x80;
                d += 8;
                if (i == 31) d = base1;             /* chain B's last eight use $72/$73 */
            }
            y = 0x38;                               /* unit 39's row, had the chain not stopped */
            PLOT_FLUSH();
        }

        /* $7EEE — the sweep's own terminator, itself an opcode slot. */
        {
            unsigned op = mem[0x7EEE];
            if (op == 0x60) goto done;
            if (op != 0xE0) { platform_smc_unhandled(0x7EEE, op); goto done; }
        }
        cpu.X = (uint8_t)x;                         /* the `CPX #$2C` writes C, so it is */
        CPX(0x2C);                                  /* the macro here too */
        if (cpu.Z) goto done;                       /* $7EF0 BEQ $7F17 */
        DEX();                                      /* $7EF2 */
        x = cpu.X;
        advance_first = 1;
    }
done:
    PLOT_FLUSH();       /* belt and braces: a leaked run would paint the NEXT call's line */
    cpu.A = (uint8_t)a; cpu.X = (uint8_t)x; cpu.Y = (uint8_t)y;
}

/* $7BBF — un-plant everything the sweep planted.  The three recorded low bytes are
   copied into the restoring stores' own operands first; that is why the records at
   $7D24/$7F24/$7F7D survive the call. */
static void dash_restore(void)
{
    mem[0x7BD4] = mem[0x7D24];
    mem[0x7BD7] = mem[0x7F24];
    mem[0x7BDA] = mem[0x7F7D];
    LDA(0x91);
    if (!dash_plant(0x7BD3, 0x7BD4, 0x7C)) return;
    if (!dash_plant(0x7BD6, 0x7BD7, 0x7C)) return;
    if (!dash_plant(0x7BD9, 0x7BDA, 0x7E)) return;
    LDA(0xE0);
    mem[0x7EEE] = cpu.A;
}

/* $7F18 — phase 3.  Both chains stop early and both start late, and the pointer walk
   is inline here rather than in $7EF3. */
static void dash_phase3(void)
{
    for (;;) {
        DEX();
        LDY(mem[0x3150 + cpu.X]);
        CPY(mem[0x7F24]);
        if (!cpu.Z) {                               /* $7F1F BEQ $7F31 */
            LDA(0x91);
            if (!dash_plant(0x7F23, 0x7F24, 0x7C)) return;
            mem[0x7F2F] = cpu.Y;
            mem[0x7F24] = cpu.Y;
            LDA(0x60);
            if (!dash_plant(0x7F2E, 0x7F2F, 0x7C)) return;
        }
        /* $7F31 — the same column step as $7EF3, but the `BCC $7F51` means a carry out
           of the high byte falls into the STY pair and re-writes $70/$72 with Y. */
        LDY(mem[0x0070]);
        INY();
        TYA();
        AND(0x07);
        if (cpu.Z) {                                /* $7F37 BNE $7F4D — inside a row */
            TYA();
            CLC();
            ADC(0x38);
            mem[0x0070] = cpu.A;
            mem[0x0072] = cpu.A;
            LDA(mem[0x0071]);
            ADC(0x01);
            mem[0x0071] = cpu.A;
            ADC(0x01);
            mem[0x0073] = cpu.A;
            if (!cpu.C) goto stepped;               /* $7F4B BCC $7F51 */
        }
        mem[0x0070] = cpu.Y;                        /* $7F4D */
        mem[0x0072] = cpu.Y;
stepped:
        LDA(0xF1);                               /* $7F51 chain A's start unit */
        SEC();
        SBC(mem[0x3080 + cpu.X]);
        mem[0x7F68] = cpu.A;
        LDY(mem[0x3050 + cpu.X]);
        LDA(mem[0x0504 + cpu.X]);
        AND(mem[0x3679 + cpu.Y]);
        ORA(mem[0x3579 + cpu.Y]);
        TAY();
        if (!dash_call(0x7F67, 0x7F68, 0x7C)) return;
        AND(mem[0x38D0 + cpu.X]);
        ORA(mem[0x3350 + cpu.X]);
        REVS_PLOT_CELL(ZP_IND_Y(0x70), cpu.A);
        bus_write(ZP_IND_Y(0x70), cpu.A);

        LDY(mem[0x3080 + cpu.X]);                   /* $7F72 chain B's stop unit */
        CPY(mem[0x7F7D]);
        if (!cpu.Z) {
            LDA(0x91);
            if (!dash_plant(0x7F7C, 0x7F7D, 0x7E)) return;
            mem[0x7F88] = cpu.Y;
            mem[0x7F7D] = cpu.Y;
            LDA(0x60);
            if (!dash_plant(0x7F87, 0x7F88, 0x7E)) return;
        }
        LDY(mem[0x30D0 + cpu.X]);                   /* $7F8A chain B's start unit */
        mem[0x7F9B] = cpu.Y;
        LDA(mem[0x4400 + cpu.X]);
        AND(mem[0x3950 + cpu.X]);
        ORA(mem[0x33D0 + cpu.X]);
        TAY();
        if (!dash_call(0x7F9A, 0x7F9B, 0x7E)) return;
        math_hi = cpu.Y;
        LDY(mem[0x3050 + cpu.X]);
        AND(mem[0x36F9 + cpu.Y]);
        ORA(mem[0x35F9 + cpu.Y]);
        LDY(math_hi);
        REVS_PLOT_CELL(ZP_IND_Y(0x72), cpu.A);
        bus_write(ZP_IND_Y(0x72), cpu.A);

        CPX(0x03);
        if (cpu.Z) break;                           /* $7FAE BEQ $7FB3 */
    }
    dash_restore();
}

/* $7D13 — phase 2.  Its first act is to plant an RTS at $7EEE, which is what turns the
   chain from "loop over every column" into "run once and return". */
static void dash_phase2(void)
{
    LDA(0x60);
    mem[0x7EEE] = cpu.A;
    for (;;) {
        DEX();
        LDY(mem[0x3150 + cpu.X]);
        CPY(mem[0x7D24]);
        if (!cpu.Z) {
            LDA(0x91);
            if (!dash_plant(0x7D23, 0x7D24, 0x7C)) return;
            mem[0x7D2F] = cpu.Y;
            mem[0x7D24] = cpu.Y;
            LDA(0x60);
            if (!dash_plant(0x7D2E, 0x7D2F, 0x7C)) return;
            LDY(mem[0x30D0 + cpu.X]);
            mem[0x7D4D] = cpu.Y;
        }
        dash_chain(0, 0, 1);                        /* $7D37 JSR $7EF3 */
        AND(mem[0x38D0 + cpu.X]);
        ORA(mem[0x3350 + cpu.X]);
        REVS_PLOT_CELL(ZP_IND_Y(0x70), cpu.A);
        bus_write(ZP_IND_Y(0x70), cpu.A);
        LDA(mem[0x4400 + cpu.X]);
        AND(mem[0x3950 + cpu.X]);
        ORA(mem[0x33D0 + cpu.X]);
        TAY();
        if (!dash_call(0x7D4C, 0x7D4D, 0x7E)) return;
        CPX(0x1C);
        if (cpu.Z) break;                           /* $7D51 BNE $7D18 */
    }
    dash_phase3();
}

void dashboard_sweep(void)
{
    REVS_PLOT_CHECK_BEFORE();
    /* $7BE2 — $70/$71 at $6700 and $72/$73 at $6800, one screen page apart, then
       column $4F.  The `JSR $7EF3` steps to $6701 before the first unit runs. */
    LDA(0x00);
    mem[0x0070] = cpu.A;
    mem[0x0072] = cpu.A;
    LDX(0x67);
    mem[0x0071] = cpu.X;
    INX();
    mem[0x0073] = cpu.X;
    LDX(0x4F);
    dash_chain(0, 0, 1);
    dash_phase2();
    REVS_PLOT_CHECK_AFTER();
}
