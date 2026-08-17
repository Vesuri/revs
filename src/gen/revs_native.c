/* revs_native.c — FAITHFUL native twins.
 *
 * Every function here replaces a transliterated one of the same name in revs_gen.c, which
 * keeps its body under a `__t6502` suffix as the validation ORACLE.  `make validate` runs
 * both on the same randomised pre-state and diffs the whole of mem[] plus the registers the
 * fixture declares live; a twin with no fixture FAILS the run rather than passing vacuously.
 *
 * ⭐⭐ HOW A TWIN IS WRITTEN — the checklist is docs/faithfulness-seam.md §Writing one, and it
 * is a REQUIREMENT, not a style preference:
 *   - real C: named locals, `for`/`if`/`switch` instead of `goto`, no LDA/STA/TAY chains;
 *   - a typed `_core(...)` that takes its inputs as arguments, plus the `void <name>(void)`
 *     6502-ABI shim that marshals mem[]/cpu into it;
 *   - mem.h names (`plot_ptr_lo`, `band1_duration_lo`, …) for every cell that has one, and a
 *     symbols.csv row for every one that does not — an unnamed hex address in a twin is a
 *     naming pass that was skipped, and docs/rename.md is where it gets queued;
 *   - comments that say what the routine COMPUTES.  The instruction-by-instruction record
 *     already exists next door in revs_gen.c and does not need re-narrating;
 *   - real locals only where the mem[] cell is PROVEN dead, with the proof written down;
 *   - BBC hardware writes are #ifdef-guarded, never deleted — the removed one is a hole in
 *     the differential, not a saved cycle.  (`make validate` diffs the hardware-write
 *     SEQUENCE, so a dropped $FE69 poke is a FAIL, and bbc_hw.cpp turns $FE20/$FE21/$FE66
 *     into the copper's band records on the Amiga: they are not dead stores there either.)
 *
 * ⚠ THE ONE PLACE 6502 MACROS SURVIVE, AND WHY.  A twin's exit contract can include the
 * FLAGS — every twin here declares AXY(+S)+flags live — and C has no carry or overflow.  Where a
 * flag genuinely leaves the routine the arithmetic goes through a small named helper
 * (`load_a`, `adc_step`, `sub_from`, `view_compose`) that wraps cpu.h's macro, so the
 * semantics are the 6502's by construction, decimal mode included (the fixture randomises
 * it).  The macro is INSIDE the helper; the caller reads as C.  Everywhere else the flags
 * are dead and there are no macros at all.
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
   The flag-carrying primitives.  These exist so that no other line in this file
   has to be written in 6502; see the header.
   =========================================================================== */

/* A = value, with N and Z from it.  Used where a value reaches A and an SMC trap can then
   exit the routine with both still live. */
static unsigned load_a(uint8_t value)
{
    LDA(value);
    return cpu.A;
}

/* a + addend + carry_in, setting C and V.  C chains (the 16-bit pointer step adds three
   times) and V is the one flag the cell chain can leak to its caller — nothing else in it
   writes V at all. */
static unsigned adc_step(unsigned a, uint8_t addend, int carry_in)
{
    cpu.A = (uint8_t)a;
    cpu.C = (uint8_t)(carry_in != 0);
    ADC(addend);
    return cpu.A;
}

/* value - subtrahend with the borrow clear (SEC/SBC), setting C and V. */
static unsigned sub_from(unsigned value, uint8_t subtrahend)
{
    cpu.A = (uint8_t)value;
    cpu.C = 1;
    SBC(subtrahend);
    return cpu.A;
}

/* value - subtrahend - !carry_in, setting C and V — the second half of a 16-bit subtract,
   where the borrow has to come from the low half's own SBC. */
static unsigned sbc_step(unsigned value, uint8_t subtrahend, int carry_in)
{
    cpu.A = (uint8_t)value;
    cpu.C = (uint8_t)(carry_in != 0);
    SBC(subtrahend);
    return cpu.A;
}

/* ===========================================================================
   $4E5C  irq1v_band_schedule — THE RASTER-BAND PALETTE/MODE SCHEDULE
   ===========================================================================

   WHAT IT COMPUTES.  Revs owns IRQ1V and drives its own display from a User VIA T1
   timer.  One PAL field is FIVE interrupts; each one repaints the Video ULA for the
   band that is about to be scanned out and reloads T1 with how long that band lasts.
   irq_band_state says which band is next.  ⭐ THE HANDLER DRAWS NOTHING — that is what
   its 2026-08-17 rename settled (docs/rename.md): the only game work in the whole cycle
   is the `tick_wheel_spin` call in band 4, 4% of the field.  A BBC has to run a raster
   split on the CPU for want of a copper; this port hands the same schedule to the copper
   and reuses the record when nothing in it changed.

   THE BANDS, in the order the counter walks them:

     0   sky-top     MODE 4, all sixteen palette entries from $3468;  next latch $0FC4
     1   sky         MODE 5, the sixteen entries 3,$13..$F3 (one flat colour);  then the
                     horizon split — band 1 runs for band1_duration and band 2 gets the
                     remainder of a fixed $153C, stashed in band2_duration.
                     ⭐ A zero-height band 1 (the split underflows) FALLS THROUGH into
                     band 2's arm in the same interrupt, which is how the horizon can sit
                     at the very top of the screen.  Bands 2→3 fall through the same way.
     2   horizon     the four-colour palette at $3458;  latch = the remainder computed above
     3   track       four entries from $3478 (colour 1 → red);  next latch $1E00
     4   dashboard   four entries from $347C (colour 3 → cyan), then tick_wheel_spin,
                     then the User VIA ORB poke and the wrap back to band 0;  latch $0B16
     $FF             the arm is skipped; the counter just wraps to 0 and takes band 0's
                     latch.  Any OTHER negative counter does nothing at all.

   WHAT IT LEAVES BEHIND.  In mem[]: the pushed X on the 6502 stack (and back),
   band2_duration and irq_band_state.  Nothing else.  In the hardware model: $FE6D (the
   interrupt acknowledge), $FE20/$FE21 (the ULA), $FE66/$FE67 (the next band's duration —
   the write to $FE66 is what closes a band record in bbc_hw.cpp) and $FE69 once per field.

   EXIT CONTRACT.  A, X and Y all come back as the interrupted code left them — measured
   on a real BBC over 2858 engine-context interrupts (`make refloop --irq-abi`) and
   asserted at the seam.  A arrives via mos_irq_a, which the MOS's own IRQ entry wrote;
   the `PLA/TAX` restores X; Y is never touched.  Flags and S come from the RTI.

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

/* The interrupt is not ours: hand it to whoever owned IRQ1V before us.
   ⚠ The register state on this path is live and is NOT the exit contract above — the exit
   is a JMP, not an RTI, and the routine arrives here with A = 0 from the IFR test. */
static void irq1v_chain_on(void)
{
    cpu.A = 0; cpu.N = 0; cpu.Z = 1;
    platform_indirect_jmp((unsigned short)(mem[MEM_saved_irq1v] |
                                           ((unsigned short)mem[MEM_saved_irq1v + 1] << 8)));
}

/* PLA / TAX / LDA $FC / RTI — the exit contract, and the only place it is spelled out. */
static void irq1v_return(void)
{
    unsigned char pulled;
    PULL(pulled);
    cpu.X = pulled;
    cpu.A = mos_irq_a;
    PLP();
}

void irq1v_band_schedule(void)
{
    unsigned latch;               /* microseconds until the next band interrupt */
    unsigned char state;

    /* Is this interrupt ours?  User VIA IFR bit 6 is the T1 timeout. */
    if ((bus_read(0xFE6D) & 0x40) == 0) {
        irq1v_chain_on();
        return;
    }
    bus_write(0xFE6D, 0x40);      /* acknowledge our own T1 flag */

    PUSH(cpu.X);                  /* X is the arms' loop counter, restored before the RTI */
    cpu.D = 0;                    /* CLD */

    state = irq_band_state;
    if (state >= 0x80) {
        /* Only $FF is a band; every other negative value leaves the machine entirely
           alone, which is how a half-initialised counter fails safe. */
        if (state != 0xFF) {
            irq1v_return();
            return;
        }
        irq_band_state = 0;       /* and the tail INCs it again, so band 1 comes next */
        latch = 0x0FC4;           /* band 0's timing without band 0's palette */
    } else switch (state) {

    case 0:     /* the two blanked text rows at the top, in MODE 4 */
        bbc_ula_control_write(BBC_ULA_MODE4);
        ula_palette_table(0x3468, 15);
        latch = 0x0FC4;
        break;

    case 1: {   /* the sky: MODE 5 with all sixteen entries the same colour */
        unsigned sky, rest;
        unsigned char entry = 0x03;
        int i;
        bbc_ula_control_write(BBC_ULA_MODE5);
        for (i = 0; i < 16; i++) { bbc_ula_palette_write(entry); entry = (unsigned char)(entry + 0x10); }

        /* The horizon split.  update_horizon_band ($4F44) puts band 1's duration in
           band1_duration from MAIN-LOOP context; band 2 takes the remainder of a fixed
           $153C and it is kept in band2_duration for band 2's own arm to load.  A borrow
           (the sky longer than the whole split) means band 2 has no height at all, so its
           arm has to run in this same interrupt. */
        sky  = band1_duration_lo | ((unsigned)band1_duration_hi << 8);
        rest = (0x153Cu - sky) & 0xFFFFu;
        band2_duration_lo = (unsigned char)rest;
        band2_duration_hi = (unsigned char)(rest >> 8);
        if (sky <= 0x153Cu) {
            latch = sky;
            break;
        }
    }   /* fall through — zero-height band 2 */

    case 2:     /* the horizon: black / blue / white / green */
        ula_palette_table(0x3458, 15);
        latch = band2_duration_lo | ((unsigned)band2_duration_hi << 8);
        if (band2_duration_hi != 0)
            break;
        /* fall through — zero-height band 3 */

    case 3:     /* the track: colour 1 becomes red */
        ula_palette_table(0x3478, 3);
        latch = 0x1E00;
        break;

    default:    /* band 4 (and anything above 3): the dashboard, and then the GAME RUNS */
        ula_palette_table(0x347C, 3);
        irq_band_state = 0xFF;     /* the tail's INC wraps it to 0 */

        /* tick_wheel_spin is an ordinary JSR target, so it is entered with whatever the
           6502 state was: A = the last palette byte fetched, X = $FF from the DEX that
           ended the loop, N/Z from that DEX, C = 1 from the CMP #3 that dispatched here,
           Y from the interrupted foreground.  Reproduced explicitly because the twin does
           not otherwise maintain the register file.
           ⚑ MEASURED, not assumed: falsifying A, X or N/Z/C here changes NOTHING in the
           differential (`make validate FN=irq1v`, 128 randomised band-4 cases, with the
           same harness catching a dropped call at once) — the body reloads all of them
           before use.  Kept anyway: it costs five stores per FIELD, and "the callee does
           not read it today" is a claim about a 400-routine subtree. */
        cpu.A = mem[0x347C]; cpu.X = 0xFF;
        cpu.N = 1; cpu.Z = 0; cpu.C = 1;

        PROBE_PHASE(PROBE_PHASE_BODYARM);
        tick_wheel_spin();
        PROBE_PHASE(PROBE_PHASE_DRAIN);

        bus_write(0xFE69, 0xFF);   /* User VIA ORB, once per field */
        latch = 0x0B16;
        break;
    }

    /* How long until the next band.  $FE66 LAST: bbc_hw.cpp closes the band record on it,
       and the record must already hold this band's mode and palette. */
    bus_write(0xFE67, (uint8_t)(latch >> 8));
    bus_write(0xFE66, (uint8_t)latch);
    irq_band_state++;
    irq1v_return();
}

/* ===========================================================================
   $7BE2  view_paint_lines — THE 3D VIEWPORT RASTERISER, ONE SCAN LINE PER CHAIN
   ===========================================================================

   WHAT IT COMPUTES.  The viewport is not drawn where it is computed: the producers
   ($24F6 → $1A20) write a *source byte* into one of forty $80-spaced blocks at
   $3000..$4380 — one block per CELL COLUMN, each indexed by scan line — and this routine
   is the single consumer that turns those into screen bytes.  It paints ONE SCAN LINE per
   chain, top down, forty cells across:

       plot_ptr  = $6700 = BBC_SCREEN_BASE + 10*320, i.e. character row 10 / cell 0 /
                   line 0 = DISPLAY LINE 80, stepped +1 within a character row and +$139
                   across one (view_next_scanline, $7EF3);
       plot_ptr2 = $6800 = plot_ptr + 256 = cell 32 of the same line, because a line is
                   40 cells x 8 = 320 bytes and cannot be reached from one base.

   One "unit" is one cell: read the source, and if it is non-zero clear it and translate it
   through view_cell_bytes ($6000); either way store the byte that is now carried.  The
   carried byte flows LEFT TO RIGHT, so a cell whose source is zero repeats whatever the
   cell to its left drew — which is also why one corrupt byte gives a run to the right edge
   of a line (docs/bbc-reference-loop.md).  That is the whole drawing model, and it is why
   the chain has to run in order.  Confirmed on a real BBC: stores cover display lines
   80..157 and buckets 88..111 are full at 320 = 8 lines x 40 cells (`make fbwrites`).

   THREE PHASES, differing only in how much of the line is painted:

     1  $7BE2, line $4F..$2C — the full forty cells, looping through view_next_scanline
        until the line counter reaches $2C.
     2  $7D13, line $2B..$1C — the painted run is shorter, so the driver plants an RTS
        ($60) over the store of unit view_run_left_end[line], runs the chain, and composes
        the boundary cell itself out of view_left_end_mask/fill.  It then enters chain B at
        view_run_right_start[line] for the second run.
     3  $7F18, line $1B..$03 — as phase 2, but BOTH chains get a planted stop and a
        computed start, and the driver steps the scan-line pointers itself.

   view_paint_restore ($7BBF) then puts `STA` back over the three planted RTSs and `CPX`
   back at $7EEE, so the chain leaves no patch behind.  ⚠ It does NOT reset the
   $7D24/$7F24/$7F7D *records* of where it planted, and the drivers skip the re-plant when
   the stop is unchanged — so the first line of a phase can legally run with no stop
   planted at all.  Faithful, and reproduced.

   ⚠ THE CONTROL TABLES LIVE INSIDE THE SOURCE BLOCKS, and that is not a mistake to tidy up.
   A block's live source span is offsets dash_block_starts[col]..$4F, so the rest of each $80
   is dead — the tails ($50-$7F) once copy_dash_data has moved them to $7B00, and the offsets
   below the start.  Three of the four tables sit in tails; view_run_right_end ($3080) sits in
   column 1's below-the-start region, and since column 1 starts at offset $1B while the driver
   indexes that table only over phase 3's lines 3..$1B, the two readings collide in EXACTLY ONE
   byte: $309B, at phase 3's topmost line.  That is why the chain can zero a byte the driver is
   about to read, and why every table read has to happen exactly where the 6502 did it —
   hoisting one out of the loop changes behaviour.

   ⭐ SHAPE, MEASURED (docs/direct-bitplane-plan.md §7a): 2093 units run per sweep and
   about 83 of them change a byte, i.e. 96% of the work is a dirty test that finds nothing.
   That 96% is the GAME's algorithm and the twin keeps every bit of it — deleting the scan
   is a representation change (a producer-maintained dirty mask, or sprites) and is tracked
   separately.  What the twin drops is the interpreter: the unrolled chain became forty
   copies of `LDY` + N/Z bookkeeping + a `switch` over a self-modified opcode byte + a
   region-dispatch prologue, none of which the 6502 paid for.  Here it is one indexed loop
   over a regular structure:

       unit i:  source block $3000 + $80*i,  screen offset 8*i,
                base pointer plot_ptr for i < 32 and plot_ptr2 for i >= 32,
                opcode slot g_viewSlot[i].

   ⚠ ONLY 29 OF THE 40 SLOTS ARE PATCHABLE, and that is a proof, not a choice: every
   writer patches only the LOW byte of its store, so it can reach one page.  Chain A's unit
   15 slot ($7D0E) and chain B's first ten ($7D65..$7DFE) are in page $7D, which no writer
   addresses — they are plain stores and the twin must not dispatch on them (a randomised
   fixture puts garbage there, and the oracle stores anyway).

   EXIT CONTRACT.  A = $E0 and the line counter = 3 from $7BBF/$7FAC; Y is the cell offset
   the last chain stopped at; the flags are live too (C from `CPX #3`, V from phase 3's
   `SEC / SBC`), so the fixture declares AXY+flags and the twin computes them.  The chain's
   own intermediate flags are dead — every one of the four call sites sets N/Z with an
   `AND`/`CPX` before the next branch — which is why the unit loop keeps no flags at all.
   =========================================================================== */

/* ⭐⭐ The per-scan-line control tables, and what they MEAN (symbols.csv carries the same names
   and the full derivation).  Addresses rather than mem.h aliases: they are indexed tables, so
   the twin adds the line itself.

   Every line is painted as TWO RUNS of cells — the LEFT run in chain A's cells 0-15 and the
   RIGHT run in chain B's cells 16-39 — and what splits them is the DASHBOARD, not the road.
   That silhouette is fixed furniture, which is why every table here is static data in the
   binary and nothing in the engine writes it.  At the bottom line (X=3) the left run is cells
   5-6 and the right run cells 33-34: the two gaps between the tyres and the dash.

   ⭐ THE TWO RUNS ARE EXACT MIRRORS about cell 19.5, and the code lives off it: the left run's
   start is not tabulated at all — it is $F1 - VIEW_RUN_R_END (5+34 = 6+33 = 39) — and the four
   mask/fill pairs come in the mirrored diagonal, left-START with right-END on the pixel-phase
   tables and left-END with right-START on the per-line ones. */
#define VIEW_SRC_BLOCKS     0x3000u   /* forty $80-spaced source blocks, one per cell column */
#define VIEW_CELL_BYTES     0x6000u   /* source byte -> screen byte */
#define VIEW_RUN_L_END      0x3150u   /* where the LEFT run stops: a chain-A slot's low byte */
#define VIEW_RUN_R_END      0x3080u   /* where the RIGHT run stops — and $F1 minus it is where
                                         the LEFT run starts.  ⚠ Also cell column 1's source
                                         area; the two readings share exactly one byte, $309B
                                         at line $1B, because a block's live source span is
                                         offsets dash_block_starts[col]..$4F and column 1's
                                         start is $1B, while the driver indexes this table only
                                         over phase 3's lines 3..$1B. */
#define VIEW_RUN_R_START    0x30D0u   /* where the RIGHT run starts: a chain-B unit+$05 entry */
#define VIEW_EDGE_PHASE     0x3050u   /* the dash edge's sub-byte PIXEL PHASE, 0-6; one value
                                         serves both runs because they mirror */
#define VIEW_L_START_MASK   0x3679u   /* by phase: the LEFT run's first cell */
#define VIEW_L_START_FILL   0x3579u
#define VIEW_R_END_MASK     0x36F9u   /* by phase: the RIGHT run's last cell (its mirror) */
#define VIEW_R_END_FILL     0x35F9u
#define VIEW_L_END_MASK     0x38D0u   /* by line: the LEFT run's last cell */
#define VIEW_L_END_FILL     0x3350u
#define VIEW_R_START_MASK   0x3950u   /* by line: the RIGHT run's first cell (its mirror) */
#define VIEW_R_START_FILL   0x33D0u
#define VIEW_L_START_SRC    0x0504u   /* the LEFT run's first cell's source byte, per line —
                                         both of these are produced by the body's 18th call,
                                         fill_dash_edge_columns */
#define VIEW_R_START_SRC    0x4400u   /* ...and the RIGHT run's first cell's */
#define VIEW_LINE_SURFACE   0x5F60u   /* per-line surface index, 2 bits, into surface_colours */
#define SURFACE_COLOURS_TBL 0x38FCu

/* The SMC records: where each driver last planted its RTS.  They outlive the call. */
#define VIEW_REC_A2         0x7D24u   /* phase 2, chain A */
#define VIEW_REC_A3         0x7F24u   /* phase 3, chain A */
#define VIEW_REC_B3         0x7F7Du   /* phase 3, chain B */
#define VIEW_CHAIN_END      0x7EEEu   /* the sweep's terminator, itself an opcode slot */

#define OP_STA_IND_Y        0x91u
#define OP_RTS              0x60u
#define OP_CPX_IMM          0xE0u

/* The forty unit addresses, in the order the chain runs them.  Chain A is sixteen 17-byte
   units from $7C00 (cells 0-15); its tail `JMP $7D56` makes chain B's twenty-four (cells
   16-39) part of the same pass. */
#define VIEW_UNIT_ADDR(i)  ((uint16_t)((i) < 16 ? 0x7C00 + 0x11 * (i)          \
                                                : 0x7D56 + 0x11 * ((i) - 16)))

/* The opcode slot of each unit, as a POINTER INTO mem[], or NULL for the eleven no writer
   can reach (page $7D).
   ⚠ A TABLE, not `VIEW_UNIT_ADDR(i) + $0F` recomputed: the oracle's slot address is a
   compile-time constant in every one of its forty copies, so a twin that derives it per
   unit hands back the arithmetic it saved.  Measured — the first version of this twin
   computed it and came out SLOWER than the transliteration (docs/perf-method.md).
   ⭐ And a POINTER rather than the address: `mem[]` is a fixed global, so `&mem[$7C0F]` is a
   link-time constant, and the unit loop's opcode fetch becomes `move.l (a3)+,a0 / move.b
   (a0),d0` instead of a 32-bit `lea mem` plus a long-indexed load — per unit, 2093 times a
   frame.  The address form is recovered where it is wanted (once per plant) by subtracting
   `mem`, which costs nothing outside the loop. */
static MEM_QUAL unsigned char* const g_viewSlotP[40] = {
    mem + 0x7C0F, mem + 0x7C20, mem + 0x7C31, mem + 0x7C42,
    mem + 0x7C53, mem + 0x7C64, mem + 0x7C75, mem + 0x7C86,
    mem + 0x7C97, mem + 0x7CA8, mem + 0x7CB9, mem + 0x7CCA,
    mem + 0x7CDB, mem + 0x7CEC, mem + 0x7CFD, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    mem + 0x7E0F, mem + 0x7E20, mem + 0x7E31, mem + 0x7E42,
    mem + 0x7E53, mem + 0x7E64, mem + 0x7E75, mem + 0x7E86,
    mem + 0x7E97, mem + 0x7EA8, mem + 0x7EB9, mem + 0x7ECA,
    mem + 0x7EDB, mem + 0x7EEC
};

/* The three values the chain and its drivers thread through each other — the 6502's A, X
   and Y under the names of what they actually hold.  Everything else is a plain local. */
typedef struct {
    unsigned byte;   /* A: the pixel byte the chain carries left to right */
    unsigned line;   /* X: the scan line being painted */
    unsigned cell;   /* Y: the cell's byte offset within the line, or a glyph index */
} ViewState;

/* Publish the state as the 6502 register file.  EVERY exit from the routine goes through
   here, including the SMC trap exits, because the fixture declares A, X and Y live. */
static void view_commit(const ViewState* v)
{
    cpu.A = (uint8_t)v->byte;
    cpu.X = (uint8_t)v->line;
    cpu.Y = (uint8_t)v->cell;
}

/* Can all forty cells off this base ($00..$138 from it) be stored without the bus?  True
   for every base the frame buffer can hold, and the unit loop hoists it out of the scan. */
static int view_span_is_ram(unsigned base)
{
    return (base + 40 * 8) <= BBC_IO_LO;
}

/* The screen address a boundary store lands on: one of the two pointers, plus the cell. */
static uint16_t view_screen_addr(unsigned zp, unsigned cell)
{
    return (uint16_t)((mem[zp] | (mem[zp + 1] << 8)) + cell);
}

/* (source AND mask) OR fill — one boundary cell.  Through the macros because N and Z from
   the `ORA` are live if the JSR that follows traps. */
static unsigned view_compose(unsigned source, unsigned mask, unsigned fill)
{
    LDA(source);
    AND(mask);
    ORA(fill);
    return cpu.A;
}

/* ⭐⭐ ADDRESS -> UNIT IN ONE LOAD, instead of a forty-entry search.
   The drivers ask three questions about an address they computed: is it a unit START (a JSR
   into the chain), is it unit+$05 (the entry that skips the dirty test and uses the caller's
   cell index), and is it a unit's opcode SLOT (a plant target).  All three were linear
   searches over the forty units, and between them they run ~190 times a frame — measured as
   most of the per-line DRIVER half of phase 24 (docs/perf-method.md §the unit loop's bytes).
   These two tables answer them in one indexed load, and they are BUILT FROM the same
   VIEW_UNIT_ADDR and g_viewSlotP the rest of the file uses, so the layout has one source of
   truth and there is no second copy to keep in step.
   ⚠ The one-answer-per-address form is only equivalent to the oracle's first-match search
   because no address is both a unit start and another unit's +$05: chain A's units sit at
   offset 0 mod 17 from $7C00 and chain B's at 2 mod 17 (342 = 17*20 + 2), while a +$05 entry
   is 5 or 7 mod 17.  The builder ASSERTS it rather than trusting the arithmetic. */
#define VIEW_LOW_PAGE   0x7Cu                /* the chain occupies $7C, $7D and $7E */
#define VIEW_LOW_PAGES  3
static unsigned char g_viewUnitOf[VIEW_LOW_PAGES][256];  /* unit+1, +$80 = the unit+$05 entry */
static unsigned char g_viewSlotOf[VIEW_LOW_PAGES][256];  /* unit+1 for an opcode slot */
static int g_viewTablesBuilt;
unsigned long g_viewTableCollisions;         /* must stay 0; see the assertion above */

static void view_build_tables(void)
{
    int i;
    for (i = 0; i < 40; i++) {
        uint16_t start = VIEW_UNIT_ADDR(i);
        unsigned char* cell;
        int k;
        for (k = 0; k < 2; k++) {                    /* the unit start, then unit+$05 */
            uint16_t a = (uint16_t)(start + (k ? 5 : 0));
            cell = &g_viewUnitOf[(a >> 8) - VIEW_LOW_PAGE][a & 0xFF];
            if (*cell) g_viewTableCollisions++;
            *cell = (unsigned char)(i + 1 + (k ? 0x80 : 0));
        }
        if (g_viewSlotP[i]) {
            unsigned a = (unsigned)(g_viewSlotP[i] - mem);
            cell = &g_viewSlotOf[(a >> 8) - VIEW_LOW_PAGE][a & 0xFF];
            if (*cell) g_viewTableCollisions++;
            *cell = (unsigned char)(i + 1);
        }
    }
    g_viewTablesBuilt = 1;
}

/* Is `page` one of the three the chain lives in?  Anything else cannot name a unit at all. */
static int view_low_page(unsigned page)
{
    return page >= VIEW_LOW_PAGE && page < VIEW_LOW_PAGE + VIEW_LOW_PAGES;
}

/* Is `dst` a slot a writer pinned to `page` could name?  The generated oracle spells this
   as an explicit case list; both come from the same operand-encoding argument. */
static int view_is_slot(uint16_t dst, unsigned page)
{
    if ((dst >> 8) != page || !view_low_page(page)) return 0;
    return g_viewSlotOf[page - VIEW_LOW_PAGE][dst & 0xFF] != 0;
}

/* ⭐⭐ WHERE THE PLANTED STOPS ARE, TRACKED INSTEAD OF RE-READ PER UNIT.
   45% of the unit loop was the SMC check — two loads and two compares per cell to ask whether
   this unit's store had been overwritten with an `RTS`.  The answer changes only when a driver
   PLANTS, and every plant in the sweep goes through view_plant below, so it is tracked: one
   scan of the forty slots when the sweep starts, and an update at each plant.  The unit loop
   then runs to a precomputed stop and tests nothing at all.
   ⚠ THE SCAN IS NOT OPTIONAL and the tracking cannot replace it: a slot can hold garbage that
   no plant of ours put there (`make validate`'s illegal cases poison the page between calls),
   so each sweep starts from what is REALLY in the page.
   ⚠ Sound only because the chain cannot modify its own page: it stores to $3000-$43CF and to
   base + cell*8, and the sweep's pointers span $6700-$737D — below $7C00.  Same construction
   argument as the hoisted destination bases, and the same one that lets the plant tracking
   survive a whole sweep.
   The list is unit indices, ascending, with a 40 sentinel — normally one entry (phases 2 and 3
   plant one stop per chain) and none at all through phase 1's 1440 units. */
static unsigned char g_viewStopList[41];
static int g_viewStopN;

/* This unit's slot no longer holds `STA (zp),Y`. */
static void view_stop_note(int unit)
{
    int i, j;
    for (i = 0; i < g_viewStopN; i++) {
        if (g_viewStopList[i] == unit) return;             /* already known */
        if (g_viewStopList[i] > unit) break;
    }
    for (j = g_viewStopN; j > i; j--) g_viewStopList[j] = g_viewStopList[j - 1];
    g_viewStopList[i] = (unsigned char)unit;
    g_viewStopList[++g_viewStopN] = 40;
}

/* ...and it does again. */
static void view_stop_forget(int unit)
{
    int i, j;
    for (i = 0; i < g_viewStopN; i++)
        if (g_viewStopList[i] == unit) {
            for (j = i; j < g_viewStopN - 1; j++) g_viewStopList[j] = g_viewStopList[j + 1];
            g_viewStopList[--g_viewStopN] = 40;
            return;
        }
}

/* Rebuild the list from the page itself.  Once per sweep. */
static void view_stops_rescan(void)
{
    int i;
    g_viewStopN = 0;
    g_viewStopList[0] = 40;
    for (i = 0; i < 40; i++)
        if (g_viewSlotP[i] && *g_viewSlotP[i] != OP_STA_IND_Y) view_stop_note(i);
}

/* The first unit at or after `unit` whose store has been overwritten, or 40 for none. */
static int view_stop_from(int unit)
{
    int i;
    for (i = 0; i < g_viewStopN; i++)
        if (g_viewStopList[i] >= unit) return g_viewStopList[i];
    return 40;
}

/* The unit an already-validated slot address belongs to. */
static int view_unit_of_slot(uint16_t dst)
{
    return (int)g_viewSlotOf[(dst >> 8) - VIEW_LOW_PAGE][dst & 0xFF] - 1;
}

/* Plant `opcode` over the store of the unit named by the operand cell at `opnd`.  Returns
   0 (and traps) for a low byte that is not a slot boundary — planting mid-instruction would
   leave the real slot reading `STA` and diverge from the 6502 silently.
   ⚠ `opcode` lands in the carried byte, and N/Z with it: a trap exits right here. */
static int view_plant(ViewState* v, uint16_t site, uint16_t opnd, unsigned page, uint8_t opcode)
{
    uint16_t dst = (uint16_t)(mem[opnd] | (mem[opnd + 1] << 8));
    v->byte = load_a(opcode);
    if (!view_is_slot(dst, page)) { platform_smc_unhandled(site, dst); return 0; }
    bus_write(dst, (uint8_t)v->byte);
    /* the only writer of an opcode slot during a sweep, so the stop list stays exact */
    if (opcode == OP_STA_IND_Y) view_stop_forget(view_unit_of_slot(dst));
    else                        view_stop_note(view_unit_of_slot(dst));
    return 1;
}

/* Is the planted stop already where we want it?  ⚠ The 6502 asks this with a `CPY`, which
   writes C as well as Z — and C is live if the plant that follows traps out of the routine,
   so the comparison has to be the 6502's rather than C's `==`.  (Found by sabotage: written
   as `==` this passed the legal cases and failed 35 of the illegal ones.) */
static int stop_unchanged(unsigned stop, unsigned recorded)
{
    cpu.Y = (uint8_t)stop;
    CPY(recorded);
    return cpu.Z;
}

/* Move a chain's planted RTS to unit `stop`, unless the record says it is already there.
   `rec` is that record, `restoreSite`/`plantSite` the two driver stores whose operands are
   themselves patched.  Returns 0 if either plant trapped. */
static int view_move_stop(ViewState* v, unsigned stop, uint16_t rec,
                          uint16_t restoreSite, uint16_t plantSite, uint16_t plantOpnd,
                          unsigned page)
{
    if (stop_unchanged(stop, mem[rec])) return 1;   /* nothing to re-plant */
    if (!view_plant(v, restoreSite, rec, page, OP_STA_IND_Y)) return 0;
    mem[plantOpnd] = (unsigned char)stop;
    mem[rec]       = (unsigned char)stop;
    return view_plant(v, plantSite, plantOpnd, page, OP_RTS);
}

static void paint_cells(ViewState* v, int unit, int forced, int advance_first);

/* A `JSR` into the middle of a chain.  The two legal entry offsets are the unit start and
   unit+$05 — the latter skips the dirty test and uses the cell index the caller just
   computed (docs/static-map.md §Open items 10).  Returns 0 if it trapped. */
static int view_enter_chain(ViewState* v, uint16_t site, uint16_t opnd, unsigned page)
{
    uint16_t target = (uint16_t)(mem[opnd] | (mem[opnd + 1] << 8));
#ifdef REVS_VIEWP3_NOCHAIN
    /* ⭐ `make VIEWP3=3` — THE PICTURE IS WRONG BY CONSTRUCTION.  Every computed chain entry is
       validated and then NOT RUN, so the difference against the shipping row prices the chain runs
       the drivers of phases 2 and 3 make — the four-way split's biggest claim, re-measured with an
       instrument that has no bracket in it at all. */
    if ((target >> 8) == page && view_low_page(page)
        && g_viewUnitOf[page - VIEW_LOW_PAGE][target & 0xFF]) return 1;
#endif
    if ((target >> 8) == page && view_low_page(page)) {
        unsigned char u = g_viewUnitOf[page - VIEW_LOW_PAGE][target & 0xFF];
        if (u) {
            paint_cells(v, (u & 0x7F) - 1, (u & 0x80) != 0, 0);
            return 1;
        }
    }
    platform_smc_unhandled(site, target);
    return 0;
}

/* Step both screen pointers to the next scan line: +1 inside a character row, +$139 to
   cross into the next one.  Returns the incremented low byte; `*carry_out` reports the
   carry off the high byte, which is the odd tail phase 3 spells as a `BCC`.
   ⚠ The adds go through adc_step because V and C are the two flags that can leave the
   chain, and because the routine can be entered with decimal mode set. */
static unsigned step_scanline(int* carry_out)
{
    unsigned next = (plot_ptr_lo + 1) & 0xFF;

    UPD_NZ(next & 7);                            /* the `TYA / AND #7` — N/Z can outlive us */
    if (carry_out) *carry_out = 0;
    if (next & 7) {                              /* still inside this character row */
        plot_ptr_lo  = (unsigned char)next;
        plot_ptr2_lo = (unsigned char)next;
        return next;
    }
    plot_ptr_lo  = (unsigned char)adc_step(next, 0x38, 0);
    plot_ptr2_lo = plot_ptr_lo;
    plot_ptr_hi  = (unsigned char)adc_step(plot_ptr_hi, 0x01, cpu.C);
    plot_ptr2_hi = (unsigned char)adc_step(plot_ptr_hi, 0x01, cpu.C);
    if (carry_out) *carry_out = cpu.C;
    return next;
}

/* ⭐⭐ THE RUN ACCUMULATOR (Amiga only — revs_plot.h).  The carried byte usually repeats,
   and in the Amiga's bitplane layout consecutive cells of a scan line are contiguous, so a
   run is one fill instead of N stores.  This changes no mem[] byte and no branch — it only
   notices, as the chain runs, that the byte it is about to store is the byte it stored to
   the cell on its left.  ⚠ A run never spans a scan line: the unit loop ends at 40. */
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

/* ⭐ `make VIEWSPLIT=1 PROBES=1` — phase 24 split into the unit loop (30) and the per-line
   drivers (whatever is left in 24), with an EMPTY bracket (31) at the same rate as the
   instrument's own control.  See src/platform/probe.h; a measurement build only. */
#if defined(REVS_VIEWSPLIT) && defined(REVS_PROBE)
#define VIEWSPLIT_DECL()         const int viewPhase = probe_phase_current()
#define VIEWSPLIT_UNITS_BEGIN()  do { PROBE_PHASE(PROBE_PHASE_VIEWCTL);                 \
                                      PROBE_PHASE(PROBE_PHASE_VIEWUNITS); } while (0)
#define VIEWSPLIT_UNITS_END()    PROBE_PHASE(viewPhase)
#else
#define VIEWSPLIT_DECL()         ((void)0)
#define VIEWSPLIT_UNITS_BEGIN()  ((void)0)
#define VIEWSPLIT_UNITS_END()    ((void)0)
#endif

/* ⭐ `make VIEWP3=1 PROBES=1` — phase 3's driver, split into its four pieces plus a control at the
   same rate.  See src/platform/probe.h §PROBE_PHASE_P3_*; a measurement build only. */
#if defined(REVS_VIEWP3) && defined(REVS_PROBE)
#define VIEWP3_PHASE(id)  PROBE_PHASE(id)
#else
#define VIEWP3_PHASE(id)  ((void)0)
#endif

/* One unit's SOURCE half: consume the byte and carry it on, with no store.  The chain does
   this BEFORE it looks at its opcode slot, so a planted `RTS` still consumes the source of the
   unit it stops on — which is why the stop tail needs the same three lines the loop runs.
     forced   the unit+$05 entry: no dirty test, and the carried byte comes from the caller's
              cell index instead of the source byte */
static unsigned view_consume(MEM_QUAL unsigned char* srcp, unsigned byte, int forced,
                             unsigned cell)
{
    if (forced) {
        *srcp = 0;
        return mem[VIEW_CELL_BYTES + cell];
    }
    {
        /* the dirty test: zero = same as my left.  ⚠ `unsigned char`, not `unsigned`: the byte
           load then sets the flags the branch wants, where a zero-extended long costs an extra
           `tst.l` in the 2093-unit loop. */
        unsigned char source = *srcp;
        if (source) {
            *srcp = 0;
            return mem[VIEW_CELL_BYTES + source];
        }
    }
    return byte;
}

/* The chain itself, $7BF7-$7F16.  Runs units `unit`..39 of the current line, then the
   $7EEE tail, which either returns or steps to the next line and starts over at unit 0.
     forced         entered at unit+$05: no dirty test, v->cell is the glyph index
     advance_first  entered at view_next_scanline (the JSRs from $7BF1 and $7D37), so the
                    pointers move and the line's background byte is loaded before any unit */
static void paint_cells(ViewState* v, int unit, int forced, int advance_first)
{
    /* ⚠ THE THREE THREADED VALUES BECOME LOCALS FOR THE DURATION, and that is a 68000
       requirement, not tidiness: this is 30% of the port's frame, and `v->byte` inside the
       2093-iteration loop is a memory operand gcc cannot keep in a register.  They are
       written back at every exit — `done:` is the only label in this file. */
    unsigned byte = v->byte, line = v->line, cell = v->cell;
    PLOT_DECL();
    VIEWSPLIT_DECL();

    for (;;) {
        if (advance_first) {
            PROBE_VIEW_LINE();
            step_scanline((int*)0);
            /* the line's background byte: two bits of the per-line surface index */
            byte = mem[SURFACE_COLOURS_TBL + (mem[VIEW_LINE_SURFACE + line] & 3)];
            advance_first = 0; unit = 0; forced = 0;
        }

        /* ⭐ THE 2093-UNIT LOOP, and everything in it is a running pointer.  The unit is
           the whole cost of the routine and the only reason it is worth a twin, so the
           source and destination addresses step by a constant instead of being derived from
           the cell index, and the opcode slot is not consulted at all (view_stop_from).
           ⚠ Hoisting the two destination bases out of the loop is safe by CONSTRUCTION,
           not by luck: the chain writes only its own source blocks ($3000-$43CF) and
           `base + cell*8` inside the frame buffer, so nothing it does can reach plot_ptr
           and move the pointer under itself. */
        {
            unsigned base0 = plot_ptr_lo  | ((unsigned)plot_ptr_hi  << 8);
            unsigned base1 = plot_ptr2_lo | ((unsigned)plot_ptr2_hi << 8);
            MEM_QUAL unsigned char* srcp = mem + VIEW_SRC_BLOCKS
                                               + ((unsigned)unit << 7) + line;
            MEM_QUAL unsigned char* const dp1 = mem + base1;
            int lastSeg;
            /* ⭐ THE SEGMENT AS A POINTER END, not an `i == 31` test inside the loop.  Cells
               0-31 come off plot_ptr and 32-39 off plot_ptr2 because 40 x 8 = 320 bytes does
               not fit one page, and the old form asked `i == 31?` and `i < 40?` separately in
               every one of the 2093 units — two compares and two branches for a boundary that
               is crossed twice a line.  One `dp != segEnd` covers both, and the cell index the
               stop path wants comes back out of the pointer: `(dp - segBase) & $FF` is `i * 8`
               in BOTH segments, because segment 1 starts at cell 32, i.e. offset 256 = 0 mod
               256.  ⚠ Segment 0 ends at base0 + 256, NOT at plot_ptr2: the 6502 switches
               pointers on the cell INDEX, so a plot_ptr2 that is not plot_ptr + 256 must still
               paint cells 0-31 off plot_ptr. */
            /* ⭐⭐ ...AND WHEN THE TWO POINTERS ARE ONE PAGE APART, ONE SEGMENT INSTEAD OF TWO.
               plot_ptr2 is plot_ptr + 256 for every line the routine itself steps (the prologue
               seeds it that way and view_next_scanline adds the same delta to both), so cell i
               lands on base0 + i*8 across the WHOLE line and the run 0..39 is contiguous.  Then
               the segment crossing — a second trip round the outer loop, a second stop lookup, a
               second run set-up — is not needed at all.
               ⭐ It is worth a branch because the per-RUN cost is what this routine is made of:
               measured at ~280 us a run against ~5.5 us a unit (docs/perf-method.md), so the 36
               crossings phase 1 makes per frame cost more than all 1440 of its units.
               ⚠ The general path stays, and not defensively: `paint_lines_short` steps the two
               pointers itself and its odd carry tail can store a low byte to plot_ptr only, so
               the two CAN drift — and the 6502 switches on the cell INDEX, so cells 0-31 must
               then still come off plot_ptr.  The condition is the whole difference. */
            const int oneSeg = (base1 == base0 + 256u);
            MEM_QUAL unsigned char* segBase = (unit < 32 || oneSeg) ? mem + base0 : dp1;
            MEM_QUAL unsigned char* dp      = segBase + (((unsigned)unit & 31u) << 3)
                                            + ((oneSeg && unit >= 32) ? 256u : 0u);
            MEM_QUAL unsigned char* segEnd  = oneSeg    ? mem + base0 + 320
                                            : (unit < 32) ? mem + base0 + 256 : dp1 + 64;
            int curUnit = unit;
            int segLimit = (unit < 32 && !oneSeg) ? 32 : 40;
            lastSeg = (unit >= 32) || oneSeg;
            /* ⭐⭐ THE PLANTED STOP, LOOKED UP ONCE — see view_stop_from.  40 means "none in
               this chain run", which is every one of phase 1's lines. */
            const int stopUnit = view_stop_from(unit);
            /* ⭐ THE BUS'S HARDWARE-RANGE TEST, HOISTED TO ONE CHECK PER SCAN LINE.  A cell
               store is `STA ($70),Y`, so the transliteration cannot know statically that it
               misses the $FC00-$FEFF I/O window and pays the test 2093 times a frame.  Here
               the whole line's span IS known — 40 cells from base0/base1 — so one check
               licenses plain `mem[]` stores for the line.  ⚠ NOT deleted: if a base ever did
               reach the window the else arm still routes to the platform, which is the one
               thing a "provably RAM" comment on its own could get silently wrong.
               ⭐ Inverting this flag PASSES all 700 fixture cases, and that is the proof rather
               than a fixture gap: with a RAM address the two arms do the same store, so they
               can only differ for $FC00-$FEFF — which this routine's own seeding of $70-$73
               cannot reach.  gcc specialises the unit loop on the flag, so the shipping path
               carries no test at all. */
            const int busSafe = view_span_is_ram(base0) && view_span_is_ram(base1);

            VIEWSPLIT_UNITS_BEGIN();
            for (;;) {
                /* the run ends at the segment's last cell, or on the planted stop */
                const int stopHere = (stopUnit >= curUnit && stopUnit < segLimit);
                MEM_QUAL unsigned char* runEnd =
                    stopHere ? dp + ((unsigned)(stopUnit - curUnit) << 3) : segEnd;
                PROBE_VIEW_RUN((unsigned)(runEnd - dp) >> 3);

#ifdef REVS_NO_UNIT_LOOP
                /* ⭐ `make NOUNITS=2` — the loop does not run AT ALL, so phase 24 is the
                   per-line DRIVERS alone.  Picture wrong by construction, as above. */
                {
                    unsigned n = (unsigned)(runEnd - dp) >> 3;
                    srcp += n << 7;
                    dp = runEnd;
                }
#endif
                while (dp != runEnd) {
                    PROBE_SHAPE_DASH_UNIT(line);
#ifdef REVS_NO_UNIT_WORK
                    /* ⭐ `make NOUNITS=1` — the unit loop keeps its ITERATIONS and loses its
                       memory work (no source read, no consume, no store).  The picture is wrong
                       by construction; the point is a decomposition no bracket can give, because
                       bracketing 118 chain runs a frame costs more than the thing it measures.
                       phase 24 with this on = the drivers plus the bare loop.
                       ⚠ It also stops ZEROING the sources, and the control tables overlap the
                       source blocks ($3080 is column 1's), so the drivers' own workload shifts:
                       treat 1 and 2 as indicative and `NOUNITS=3` — which keeps the consume and
                       drops only the store — as the clean one. */
                    srcp += 0x80;
                    dp += 8;
                    continue;
#endif
                    byte = view_consume(srcp, byte, forced, cell);
                    forced = 0;
#ifdef REVS_NO_UNIT_STORE
                    /* ⭐ `make NOUNITS=3` — everything but the STORE, so the sources are still
                       consumed and the drivers see the workload they really have. */
                    srcp += 0x80;
                    dp += 8;
                    continue;
#endif
                    PLOT_UNIT((unsigned)(dp - mem), byte);
#ifndef REVS_PLOT_ONLY
                    PROBE_SHAPE_DASH_STORE((unsigned)(dp - mem), byte, line);
                    if (busSafe) *dp = (unsigned char)byte;
                    else         bus_write((uint16_t)(dp - mem), (uint8_t)byte);
#endif
                    srcp += 0x80;
                    dp += 8;
                }

                if (stopHere) {
                    /* the unit the stop sits on: its source is consumed, its store is not.
                       ⚠ The slot byte is read AFTER the consume, in the 6502's order — the
                       source blocks and the chain's own page cannot overlap, so the value is
                       the same either way, but the order is not something to have to argue. */
                    MEM_QUAL unsigned char* slot = g_viewSlotP[stopUnit];
                    unsigned char op;
                    PROBE_SHAPE_DASH_UNIT(line);
                    PROBE_VIEW_UNITS(1);                      /* the stop's own unit: consumed */
                    byte = view_consume(srcp, byte, forced, cell);
                    cell = (unsigned)(dp - segBase) & 0xFF;   /* its `LDY #<cell*8>` ran */
                    op = *slot;
                    if (op != OP_RTS) platform_smc_unhandled((uint16_t)(slot - mem), op);
                    PLOT_FLUSH();
                    goto done;
                }
                if (lastSeg) break;
                segBase  = dp1;                         /* cells 32-39 live in the next page */
                dp       = dp1;
                segEnd   = dp1 + 64;
                curUnit  = 32;
                segLimit = 40;
                lastSeg  = 1;
            }
            VIEWSPLIT_UNITS_END();
            cell = 0x38;                            /* unit 39's cell, had the chain not stopped */
            PLOT_FLUSH();
        }

        /* $7EEE — the sweep's own terminator, itself an opcode slot. */
        {
            unsigned op = mem[VIEW_CHAIN_END];
            if (op == OP_RTS) goto done;
            if (op != OP_CPX_IMM) { platform_smc_unhandled(VIEW_CHAIN_END, op); goto done; }
        }
        /* `CPX #$2C` — the last full-width line.  Its C is live. */
        cpu.X = (uint8_t)line;
        CPX(0x2C);
        if (cpu.Z) goto done;
        line = (line - 1) & 0xFF;
        advance_first = 1;
    }
done:
    VIEWSPLIT_UNITS_END();      /* the planted-RTS exit leaves the bracket open otherwise */
    v->byte = byte;
    v->line = line;
    v->cell = cell;
}

/* $7BBF — un-plant everything the sweep planted.  The three recorded low bytes are copied
   into the restoring stores' own operands first; that is why the records survive the call. */
static void unplant_stops(ViewState* v)
{
    mem[0x7BD4] = mem[VIEW_REC_A2];
    mem[0x7BD7] = mem[VIEW_REC_A3];
    mem[0x7BDA] = mem[VIEW_REC_B3];
    if (!view_plant(v, 0x7BD3, 0x7BD4, 0x7C, OP_STA_IND_Y)) return;
    if (!view_plant(v, 0x7BD6, 0x7BD7, 0x7C, OP_STA_IND_Y)) return;
    if (!view_plant(v, 0x7BD9, 0x7BDA, 0x7E, OP_STA_IND_Y)) return;
    v->byte = load_a(OP_CPX_IMM);
    mem[VIEW_CHAIN_END] = (unsigned char)v->byte;
}

/* $7F18 — phase 3.  Both chains stop early and both start late, and the scan-line step is
   inline here rather than reached through the chain's own entry. */
static void paint_lines_short(ViewState* v)
{
    PROBE_PHASE(PROBE_PHASE_VIEWP3);        /* one transition a sweep — src/platform/probe.h §33 */
    PROBE_VIEW_PHASE(2);
    for (;;) {
        unsigned edge, entry, next;
        int carry_out;

        PROBE_VIEW_LINE();
        VIEWP3_PHASE(PROBE_PHASE_VIEWCTL);       /* the control: an empty bracket, opened and closed */
        VIEWP3_PHASE(PROBE_PHASE_VIEWP3);
        v->line = (v->line - 1) & 0xFF;

#ifdef REVS_VIEWP3_EMPTY
        /* ⭐ `make VIEWP3=2` — THE PICTURE IS WRONG BY CONSTRUCTION.  Phase 3's line loop keeps its
           25 iterations and loses its entire body, so `phase 34 with this on` is the loop and
           nothing else.  It exists because the four-way split above prices the two chain entries at
           1.4 ms a line and NOTHING in the generated code for them is 10 000 cycles — so the
           question "is that time in this body at all, or is it interrupt time landing in whichever
           bracket is open?" has to be answered before any of it is optimised. */
        cpu.X = (uint8_t)v->line;
        CPX(0x03);
        if (cpu.Z) break;
        continue;
#endif

        /* chain A's stop */
        VIEWP3_PHASE(PROBE_PHASE_P3_STOPA);
        v->cell = mem[VIEW_RUN_L_END + v->line];
        if (!view_move_stop(v, v->cell, VIEW_REC_A3, 0x7F23, 0x7F2E, 0x7F2F, 0x7C)) {
            view_commit(v);
            return;
        }

        /* the scan-line step, with phase 3's tail: a carry off the high byte makes it store
           the un-crossed low byte after all.
           ⚠ THIS TAIL IS UNTESTED AND UNREACHABLE, and it is recorded rather than trusted:
           deleting it passes all 700 fixture cases (sabotage S2, 2026-08-17), because the
           high byte only carries out of $FF and the pointer lives at $67..$7A.  Kept because
           the 6502 has it; do not read the green as coverage. */
        VIEWP3_PHASE(PROBE_PHASE_VIEWP3);
        next = step_scanline(&carry_out);
        if (!(next & 7) && carry_out) {
            plot_ptr_lo  = (unsigned char)next;
            plot_ptr2_lo = (unsigned char)next;
        }
        VIEWP3_PHASE(PROBE_PHASE_P3_CHAINA);

        /* chain A: enter at $F1 - view_run_right_end[line], with the boundary cell composed
           from the per-line source byte and the edge tables */
        v->byte = sub_from(0xF1, mem[VIEW_RUN_R_END + v->line]);
        mem[0x7F68] = (unsigned char)v->byte;
        edge    = mem[VIEW_EDGE_PHASE + v->line];
        v->byte = view_compose(mem[VIEW_L_START_SRC + v->line],
                               mem[VIEW_L_START_MASK + edge],
                               mem[VIEW_L_START_FILL + edge]);
        v->cell = v->byte;                          /* TAY: N/Z already match */
        if (!view_enter_chain(v, 0x7F67, 0x7F68, 0x7C)) { view_commit(v); return; }
        v->byte = view_compose(v->byte, mem[VIEW_L_END_MASK + v->line],
                                        mem[VIEW_L_END_FILL + v->line]);
        REVS_PLOT_CELL(view_screen_addr(MEM_plot_ptr_lo, v->cell), (uint8_t)v->byte);
        bus_write(view_screen_addr(MEM_plot_ptr_lo, v->cell), (uint8_t)v->byte);

        /* chain B: the same again, one page down and with its own tables.  ⚠ the stop is
           re-read here — the chain may have zeroed it (see the header). */
        VIEWP3_PHASE(PROBE_PHASE_P3_STOPB);
        v->cell = mem[VIEW_RUN_R_END + v->line];
        if (!view_move_stop(v, v->cell, VIEW_REC_B3, 0x7F7C, 0x7F87, 0x7F88, 0x7E)) {
            view_commit(v);
            return;
        }
        VIEWP3_PHASE(PROBE_PHASE_P3_CHAINB);
        entry   = mem[VIEW_RUN_R_START + v->line];
        v->cell = entry;
        mem[0x7F9B] = (unsigned char)entry;
        v->byte = view_compose(mem[VIEW_R_START_SRC + v->line],
                               mem[VIEW_R_START_MASK + v->line],
                               mem[VIEW_R_START_FILL + v->line]);
        v->cell = v->byte;                          /* TAY */
        if (!view_enter_chain(v, 0x7F9A, 0x7F9B, 0x7E)) { view_commit(v); return; }
        math_hi = (unsigned char)v->cell;           /* the chain's cell, parked in scratch */
        edge    = mem[VIEW_EDGE_PHASE + v->line];
        v->byte = view_compose(v->byte, mem[VIEW_R_END_MASK + edge],
                                        mem[VIEW_R_END_FILL + edge]);
        v->cell = math_hi;
        UPD_NZ(v->cell);                            /* the `LDY math_hi` that reloaded it */
        REVS_PLOT_CELL(view_screen_addr(MEM_plot_ptr2_lo, v->cell), (uint8_t)v->byte);
        bus_write(view_screen_addr(MEM_plot_ptr2_lo, v->cell), (uint8_t)v->byte);

#if defined(REVS_VIEWCAL) && defined(REVS_PROBE)
        /* ⭐ `make VIEWCAL=1` — 14 000 known cycles in their own bracket, at this line's rate.  The
           measuring stick for every other row in this routine; see src/platform/probe.h. */
        PROBE_PHASE(PROBE_PHASE_CAL);
        {   /* N x 14 000 cycles — the row must scale linearly in N (probe.h) */
            int burns = REVS_VIEWCAL;
            while (burns-- > 0) probe_burn_cycles();
        }
#endif

        /* `CPX #3` — the last line of the viewport.  Its C is part of the exit contract. */
        cpu.X = (uint8_t)v->line;
        CPX(0x03);
        if (cpu.Z) break;
    }
    unplant_stops(v);
}

/* $7D13 — phase 2.  Its first act is to plant an RTS at $7EEE, which is what turns the
   chain from "loop over every line" into "run once and return". */
static void paint_lines_clipped(ViewState* v)
{
    PROBE_PHASE(PROBE_PHASE_VIEWP2);        /* one transition a sweep — src/platform/probe.h §33 */
    PROBE_VIEW_PHASE(1);                    /* its lines are counted in paint_cells, which it enters
                                               through view_next_scanline once per line */
    v->byte = load_a(OP_RTS);
    mem[VIEW_CHAIN_END] = (unsigned char)v->byte;

    for (;;) {
        v->line = (v->line - 1) & 0xFF;

        v->cell = mem[VIEW_RUN_L_END + v->line];
        if (!stop_unchanged(v->cell, mem[VIEW_REC_A2])) {
            if (!view_plant(v, 0x7D23, VIEW_REC_A2, 0x7C, OP_STA_IND_Y)) {
                view_commit(v);
                return;
            }
            mem[0x7D2F]      = (unsigned char)v->cell;
            mem[VIEW_REC_A2] = (unsigned char)v->cell;
            if (!view_plant(v, 0x7D2E, 0x7D2F, 0x7C, OP_RTS)) {
                view_commit(v);
                return;
            }
            v->cell     = mem[VIEW_RUN_R_START + v->line];   /* chain B's entry, for the store */
            UPD_NZ(v->cell);                             /* its `LDY` outlives the chain */
            mem[0x7D4D] = (unsigned char)v->cell;
        }

        paint_cells(v, 0, 0, 1);                /* the JSR through view_next_scanline */

        v->byte = view_compose(v->byte, mem[VIEW_L_END_MASK + v->line],
                                        mem[VIEW_L_END_FILL + v->line]);
        REVS_PLOT_CELL(view_screen_addr(MEM_plot_ptr_lo, v->cell), (uint8_t)v->byte);
        bus_write(view_screen_addr(MEM_plot_ptr_lo, v->cell), (uint8_t)v->byte);

        v->byte = view_compose(mem[VIEW_R_START_SRC + v->line],
                               mem[VIEW_R_START_MASK + v->line],
                               mem[VIEW_R_START_FILL + v->line]);
        v->cell = v->byte;                          /* TAY */
        if (!view_enter_chain(v, 0x7D4C, 0x7D4D, 0x7E)) { view_commit(v); return; }

        cpu.X = (uint8_t)v->line;
        CPX(0x1C);
        if (cpu.Z) break;
    }
    paint_lines_short(v);
}

/* The idiomatic core: paint the viewport from `firstLine` downwards, both pointers seeded
   one page apart at `screenBase`.  Everything above is reachable only from here. */
static void view_paint_lines_core(unsigned screenBase, unsigned firstLine)
{
    ViewState v;

    PROBE_VIEW_PHASE(0);
    if (!g_viewTablesBuilt) view_build_tables();
    view_stops_rescan();          /* what is REALLY in the page, before any plant of ours */

    plot_ptr_lo  = (unsigned char)screenBase;
    plot_ptr2_lo = (unsigned char)screenBase;
    plot_ptr_hi  = (unsigned char)(screenBase >> 8);
    plot_ptr2_hi = (unsigned char)((screenBase >> 8) + 1);

    v.byte = (unsigned char)screenBase;   /* the `LDA #0` that seeded both low bytes */
    v.line = firstLine;
    v.cell = cpu.Y;                       /* untouched until the first chain sets it */
    UPD_NZ(firstLine);                    /* `LDX #$4F` is the prologue's last flag write */

    paint_cells(&v, 0, 0, 1);
    paint_lines_clipped(&v);
    view_commit(&v);
}

/* The 6502-ABI shim.  $6700/$6800 is character row 10 of the frame buffer — display line
   80 — and $4F is the first scan line painted. */
void view_paint_lines(void)
{
    REVS_PLOT_CHECK_BEFORE();
    view_paint_lines_core(0x6700u, 0x4Fu);
    REVS_PLOT_CHECK_AFTER();
}

/* ===========================================================================
   $16DC  race_main_loop — THE RACE
   ===========================================================================

   WHAT IT COMPUTES.  Nothing itself: it is the driver.  One call runs a whole driving
   session — practice, a qualifying lap or a race — and returns to the front end
   (`wait_flag_05F4`, $6563) when that session is over or the player has asked for the pits.
   Three nested things are going on, and the transliteration next door hides all three
   behind sixteen labels and a `goto` out of the tail into the middle of the prologue:

     1  ONCE PER SESSION ($16DC-$16E6).  Program the display hardware, put character output
        on the race view's own plotter, build the $7B00 dashboard overlay out of the block
        tails, and paint the viewport once so the first frame has something under it.
     2  ONCE PER (RE)START ($16EE-$16FE).  Reset the session state, in three nested depths —
        see RestartDepth below — then clear state_flags and scale the wing settings.
     3  ONCE PER FRAME ($1701-$17B7).  The body: 24 calls that simulate and draw, in a flat
        sequence (that flatness is what PROBE_PHASE 1..24 exploits).  Then the tail, which
        is the session's state machine and the only interesting control flow in the routine.

   ⭐ THE TAIL, WHICH IS THE POINT.  It answers one question per frame — does the race go on?
   Four things can say no, and each has its own answer:

     * A CRASH (crash_flag, set for one frame by the body's 23rd call).  The tail clears it,
       holds the picture for 100 fields = 2 seconds, and then asks where to resume.  It
       resumes for a practice lap, for qualifying, or for a NOVICE race, and ends the session
       for an Amateur or Professional one — you are out of the race.
     * SHIFT+f0, "return to pits" ($C0 in state_flags).  Honoured only when the wheels are
       still (wheel_spin_rate); at speed the request is simply cleared and the race goes on.
       Honoured, it exits with bit 6 still set, which is what makes wait_flag_05F4 re-enter
       this routine after the wing-settings menu instead of returning to the front end.
     * ANY OTHER shifted command that leaves state_flags positive — SHIFT+f4's quit — ends
       the session.
     * session_end_countdown reaching zero: the time or lap limit was passed N frames ago and
       the car has been coasting to the line ever since.

     Ending a session is not just leaving the loop: sound off, the "please wait" message, and
     finish_race races the remaining drivers to the finish so the results table is complete.

   ⚠ THE TWO PORT SEAMS ARE IN HERE, and they are the reason this routine matters far beyond
   its own cost.  Both were the transpiler's hooks (PRE_INSN_HOOKS / SPINWAIT_HOOKS) and are
   now spelled out, because the oracle's copies are no longer the code that runs:

     * platform_render_frame() at the TOP of the frame loop, not at the frame wait.  The wait
       is CONDITIONAL — no crash means no wait — so a paint hooked to it would stop counting
       frames on the ordinary path and the framerate would read as a rendering drop
       (docs/perf-method.md §Rule 3).  One hook here means exactly one painted frame per game
       frame, always.
     * platform_tick_vbi() inside the field_countdown wait, and NOT render_frame: on the Amiga
       the 50 Hz body runs in the real VERTB ISR and this loop is preempted, so tickVBI is a
       no-op there and the wait ends on its own; on the headless host, which has no
       preemption, tickVBI is the only thing that advances the interrupt.

   NO HARDWARE WRITES.  Every $FCxx-$FExx access in the race belongs to hw_init and
   irq1v_band_schedule; this routine reaches the machine only through those two calls, so
   there is no #ifdef-guarded poke here and nothing was dropped for the Amiga.

   EXIT CONTRACT: whatever irq1v_release ($4F23) leaves, since that is the last call before
   the RTS.  The one caller does `BIT $05F4` next and consumes no register.

   ⚠⚠ NO FIXTURE, AND `make determinism` IS THE GATE.  The oracle cannot be run against this
   twin on randomised memory — the frame body always runs at least once and it is the whole
   engine.  The full reasoning is beside NATIVE_FUNCS in tools/transpile.py; the practical
   consequence is that every change in here must be followed by `make determinism`, which
   drives 300 frames of exactly this loop and byte-compares all 64 KB.
   =========================================================================== */

/* How much of the session reset a restart re-runs.  The 6502 expresses this as three branch
   targets INSIDE the prologue ($16EE, $16F3, $16F6) that the tail jumps back to, so the
   depths are nested by construction: each entry point falls through into the next. */
typedef enum {
    RESTART_NONE = 0,   /* $16F9 — back from the pits: keep the session exactly as it was */
    RESTART_LATE,       /* $16F6 — rebuild the player's car and the driver tables only */
    RESTART_MID,        /* $16F3 — and zero $00-$68 plus $6280-$62FF: a fresh lap */
    RESTART_FULL        /* $16EE — and reset the player's race clock: a fresh session */
} RestartDepth;

/* What the tail decided about this frame. */
typedef enum {
    LOOP_NEXT_FRAME,    /* $17B7 — round again */
    LOOP_RESTART,       /* leave the frame loop and re-run the reset to `g_restartDepth` */
    LOOP_FINISHED       /* $17BA — the session is over; leave the routine */
} LoopVerdict;

/* A JSR is handed the whole register file, and a callee may branch on the flags before it
   reloads anything.  These three exist so that the argument passing is visibly the 6502's —
   the macro is inside, the call site reads as C — and so that nothing else in this routine
   has to mention a register at all. */
static void arg_a(uint8_t v) { LDA(v); }
static void arg_x(uint8_t v) { LDX(v); }
static void arg_y(uint8_t v) { LDY(v); }

/* $16E9's `BIT $05F4` — bit 6 of state_flags lands in V.  Through the macro rather than as a
   plain mask because the test leaves N and V set across the calls that follow it, and "no
   callee reads them" is a claim about a 400-routine subtree, not something to assume here. */
static int state_flags_bit6(void)
{
    BIT(state_flags);
    return cpu.V;
}

/* $1765-$1771 — WHERE DOES AN INTERRUPTED SESSION RESUME?  Asked after a crash, and again
   after a quit, and the three answers are the three restart depths.  A practice lap and a
   qualifying session simply begin again; so does a Novice race, which is how the beginner
   class cannot be knocked out.  Anything else has really finished. */
static LoopVerdict race_resume_point(RestartDepth* depth)
{
    if (load_a(qualify_minutes) & 0x80) {           /* practice: untimed, never over */
        *depth = RESTART_FULL;
        return LOOP_RESTART;
    }
    if (!(load_a(session_is_race) & 0x80)) {        /* qualifying, not the race proper */
        *depth = RESTART_MID;
        return LOOP_RESTART;
    }
    if (load_a(race_class) == 0) {                  /* Novice */
        *depth = RESTART_LATE;
        return LOOP_RESTART;
    }
    return LOOP_FINISHED;
}

/* $1773-$178D — THE SESSION IS OVER (unless it is a practice lap).  Reached three ways: a
   crash in an Amateur or Professional race, a shifted quit key, and session_end_countdown
   running out.  Silence the sound, then let the remaining twenty drivers finish the race so
   the results are real, and leave a state_flags value the front end can read: $20 if the
   session simply ended, whatever the key wrote if it was negative. */
static LoopVerdict race_session_end(RestartDepth* depth)
{
    sound_stop_all();

    /* $1776's `BMI $1765` re-asks the resume question — and the only test between here and
       there is the one on qualify_minutes that is being repeated, which nothing since has
       written (sound_stop_all touches sound state only).  So a practice lap resumes at
       RESTART_FULL and the re-ask is that, spelled out. */
    if (load_a(qualify_minutes) & 0x80) {
        *depth = RESTART_FULL;
        return LOOP_RESTART;
    }

    arg_x(0x30);                  /* the token for the "please wait" message */
    print_message_pair();
    finish_race();                   /* race the remaining drivers to the finish */

    if (!(load_a(state_flags) & 0x80)) {
        arg_a(0x20);
        state_flags = cpu.A;      /* $178D's BNE is unconditional: $20 is never zero */
    }
    return LOOP_FINISHED;
}

/* $174B-$17B7 — the frame's verdict.  Everything above is called from here. */
static LoopVerdict race_frame_tail(RestartDepth* depth)
{
    /* Re-arm the raster band cycle if the field it was counting has finished.  A counter
       still inside 0..3 means the frame overran its own field, and it is left alone. */
    if (irq_band_state & 0x80)
        irq_band_state++;

    if (load_a(crash_flag) != 0) {
        crash_flag++;                 /* straight back to zero: the crash is handled here */
        /* 100 fields = two seconds of holding the picture.  ⚠ The `LDA #$9C` is kept rather
           than folded into the store, because the value stays in A across the wait below and
           the port's interrupt seam publishes A into mos_irq_a on every field — the oracle's
           store-immediate peephole cannot see that reader. */
        arg_a(0x9C);
        field_countdown = cpu.A;
        do {
            /* ⭐ THE ENGINE'S ONE TRUE FRAME WAIT.  tick_wheel_spin INCs field_countdown
               once per PAL field, so on the Amiga the VERTB ISR ends this on its own and
               platform_tick_vbi() is a no-op; on the host it IS the interrupt. */
            PROBE_PHASE(0);
            platform_tick_vbi();
            platform_poll_events();
        } while (load_a(field_countdown) & 0x80);

        {
            LoopVerdict v = race_resume_point(depth);
            if (v != LOOP_FINISHED) return v;
        }
        return race_session_end(depth);
    }

    /* The in-race command keys.  Y is the index of the last entry of shift_key_tbl. */
    arg_y(0x0B);
    shift_key_commands();

    if (load_a(state_flags) != 0) {
        if (!(cpu.A & 0x80))                       /* $1799: a positive request quits */
            return race_session_end(depth);
        AND(0x40);                                 /* $179B — and it writes A */
        if (cpu.Z)
            return LOOP_FINISHED;                  /* SHIFT+f0 alone: leave for the pits */
        if (load_a(wheel_spin_rate) == 0)
            return LOOP_FINISHED;                  /* stopped: the pit request is granted */
        arg_a(0x00);
        state_flags = 0;                           /* moving: refuse it and drive on */
    }
    /* Either way A is now 0 — from the cell that tested zero, or from the `LDA #0` above. */

    /* The session's own countdown.  Non-zero means the limit was already passed and the car
       is coasting; the frame it would reach zero is the frame the session ends. */
    arg_x(session_end_countdown);
    if (cpu.X != 0) {
        DEX();
        if (cpu.X == 0)
            return race_session_end(depth);
        session_end_countdown = cpu.X;
    }

    engine_sound_update();        /* the fourth and last note step of the frame */
    draw_dash_needles();
    return LOOP_NEXT_FRAME;
}

/* The idiomatic core.  `depth` is how much of the session state the FIRST pass resets, which
   is the only thing the 6502 prologue decides before the loop starts. */
static void race_main_loop_core(RestartDepth depth)
{
    for (;;) {
        LoopVerdict verdict;

        /* ---- the session reset, nested: FULL falls into MID falls into LATE ---- */
        if (depth >= RESTART_FULL) {
            arg_x(0x00);              /* driver 0 = the player */
            clear_race_clock();
        }
        if (depth >= RESTART_MID)
            reset_driving_variables();
        if (depth >= RESTART_LATE)
            build_player_car();

        arg_a(0x00);
        state_flags = 0;
        scale_wing_settings();                   /* scale the wing settings for the new session */

        /* ---- one pass = one game frame ---- */
        do {
            /* ⭐ THE PORT'S PAINT HOOK — see the header for why it is here and not at the
               frame wait.  Its own phase, because renderFrame() spins for the field and
               that spin must not land in whatever phase was open across the loop seam. */
            PROBE_PHASE(PROBE_PHASE_FRAMEWAIT);
            PROBE_SHAPE_PHASE(PROBE_PHASE_FRAMEWAIT);
            platform_render_frame();

            PROBE_PHASE(1);  PROBE_SHAPE_PHASE(1);  tick_race_timers();
            PROBE_PHASE(2);  PROBE_SHAPE_PHASE(2);  draw_starting_lights();
            PROBE_PHASE(3);  PROBE_SHAPE_PHASE(3);  read_driving_controls();
            PROBE_PHASE(4);  PROBE_SHAPE_PHASE(4);  apply_driving_model();
            PROBE_PHASE(5);  PROBE_SHAPE_PHASE(5);  build_track_geometry();
            PROBE_PHASE(6);  PROBE_SHAPE_PHASE(6);  place_player_in_section();
            PROBE_PHASE(7);  PROBE_SHAPE_PHASE(7);  advance_player_section();
            PROBE_PHASE(8);  PROBE_SHAPE_PHASE(8);  update_lap_timers();
            PROBE_PHASE(9);  PROBE_SHAPE_PHASE(9);  engine_sound_update();
            PROBE_PHASE(10); PROBE_SHAPE_PHASE(10); clear_surface_buffers();
            PROBE_SHAPE_ROAD_BEFORE();
            PROBE_PHASE(11); PROBE_SHAPE_PHASE(11); draw_road();
            PROBE_SHAPE_ROAD_AFTER();
            PROBE_PHASE(12); PROBE_SHAPE_PHASE(12); engine_sound_update();
            PROBE_PHASE(13); PROBE_SHAPE_PHASE(13); fill_line_surface();
            PROBE_PHASE(14); PROBE_SHAPE_PHASE(14); build_road_sign();
            arg_x(0x17);                                   /* $172B: the object slot count */
            PROBE_PHASE(15); PROBE_SHAPE_PHASE(15); draw_track_object();
            PROBE_PHASE(16); PROBE_SHAPE_PHASE(16); draw_corner_markers();
            PROBE_PHASE(17); PROBE_SHAPE_PHASE(17); move_and_draw_cars();
            PROBE_PHASE(18); PROBE_SHAPE_PHASE(18); fill_dash_edge_columns();
            PROBE_PHASE(19); PROBE_SHAPE_PHASE(19); mirrors_update();
            PROBE_PHASE(20); PROBE_SHAPE_PHASE(20); engine_sound_update();
            PROBE_PHASE(21); PROBE_SHAPE_PHASE(21); update_horizon_band();
            PROBE_PHASE(22); PROBE_SHAPE_PHASE(22); process_car_contact();
            PROBE_PHASE(23); PROBE_SHAPE_PHASE(23); check_crash();
            PROBE_SHAPE_DASH_BEFORE();
            PROBE_PHASE(24); PROBE_SHAPE_PHASE(24); view_paint_lines();
            PROBE_SHAPE_DASH_AFTER();
            /* ⭐⭐ Phase 32 exists so that phase 24 means ONLY the view sweep.  Without it the
               tail's three JSRs were charged to the rasteriser — 11 ms of its 82. */
            PROBE_PHASE(PROBE_PHASE_VIEWTAIL);

            verdict = race_frame_tail(&depth);
        } while (verdict == LOOP_NEXT_FRAME);

        if (verdict == LOOP_FINISHED)
            break;
    }

    /* $17BA — out.  A = $80 tells copy_dash_data to stow the $7B00 overlay back into the
       block tails it was assembled from, so the page can be MODE 7 screen memory again. */
    arg_a(0x80);
    copy_dash_data();
    irq1v_release();
}

/* The 6502-ABI shim.  The only decision the prologue makes is how much to reset: bit 6 of
   state_flags is set when wait_flag_05F4 is re-entering the race after the pit-lane
   wing-settings menu, and then the session state must survive untouched. */
void race_main_loop(void)
{
    hw_init();

    arg_a(0x00);
    text_out_via_mos = 0;         /* character output goes to the race view's own plotter */
    copy_dash_data();             /* A = 0: BUILD the $7B00 overlay from the block tails */
    view_paint_lines();

    race_main_loop_core(state_flags_bit6() ? RESTART_NONE : RESTART_FULL);
}

/* ===========================================================================
   $24F6  build_track_geometry — THE FRAME'S ROAD GEOMETRY  (twin #4)
   ---------------------------------------------------------------------------
   The view pipeline's FIRST producer, and the fifth call of the frame.  It turns the track
   ahead into the two 40-point edge lists everything downstream reads: road_edge_start emits
   the nearest point of each side, then one road_edge_walk per side climbs the section list
   into the distance, and the three lines at the end record where the road's HORIZON came out
   — its scan line (horizon_extent), which point it was (horizon_index), and how wide the road
   still looks there (horizon_half_width).

   ⭐ The routine itself is 84 bytes of driver: every edge point is projected by the walk, so
   nothing here is arithmetic.  What it does own is the four SEEDS that decide the shape of
   both walks — the "no nearest point yet" pair and the 13-section subdivision floor — and the
   frame's horizon record.  There are no hardware writes and no $FC00-$FEFF access at all: the
   whole routine lives in RAM, so the transpiler was already routing it straight to mem[]
   and the twin removes interpreter, not bus calls.

   ⚠ SELF-MODIFYING, twice, and both sites belong to the expansion circuits: $2538 and $2542
   are rewritten by each circuit's ModifyGameCode, so the bytes below are Silverstone's and
   the other four circuits take the hook arms.  docs/static-map.md §Open items.
   =========================================================================== */

/* The road-geometry pass's shared arrays (symbols.csv holds the evidence for each name).
   ⭐ edge_x is an ANGLE, not a column: bearing_to_section is an arctan and emit_edge_bearing
   stores `bearing - car_heading`, so an edge point is an azimuth relative to where the car is
   pointing and interp_edge is what turns one into a screen column. */
#define EDGE_Y_TBL       0x5F20u   /* edge_y      — per edge point: the scan line it projects to */
#define EDGE_X_HI_TBL    0x5E90u   /* edge_x_hi   — ...and the high byte of its angle */
#define EDGE_HALF        0x0028u   /* 40 — the stride between the two road sides' halves */
#define SECTION_LO_TBL   0x0900u   /* section_coord_lo — 40 sections x 3 bytes, + two scratch slots */
#define SECTION_HI_TBL   0x0A00u   /* section_coord_hi */
#define SECTION_MID      0x00FAu   /*   ...the triple road_edge_walk interpolates midpoints into */
#define SECTION_NEAR     0x00FDu   /*   ...and the one road_edge_start stages the near point in */
#define WALK_STEP_TBL    0x3DD0u   /* edge_walk_step_tbl — 18 entries, one per emitted point */
#define CAR_SEGMENT_TBL  0x06E8u   /* car_segment */
#define PLAYER_CAR       0x17u     /* slot 23 — the player's own car */

/* value >> 1, with C from the bit shifted out.  The last operation in the routine, so its
   C/N/Z are the flags the caller sees. */
static unsigned lsr_a(unsigned value)
{
    cpu.A = (uint8_t)value;
    LSR_A();
    return cpu.A;
}

/* `value >= limit`, spelled as the 6502's CMP so that the comparison's own C/N/Z are left
   behind.  ⚠ NOT decoration: every SMC site in these two routines is an EXIT, so a clamp
   test three lines earlier is the last thing that touched the flags on that path, and a
   plain C `>=` reads the same and validates differently. */
static int cmp_ge(unsigned value, uint8_t limit)
{
    cpu.A = (uint8_t)value;
    CMP(limit);
    return cpu.C;
}

/* max(value, floor), via the same CMP.  Used where the floor's own `LDA #imm` flags are
   provably overwritten before anything reads them (draw_road's two clamps). */
static unsigned clamp_up_to(unsigned value, uint8_t floor)
{
    return cmp_ge(value, floor) ? value : floor;
}

/* Y = value, then the 6502's CPY.  Used where the compare is the last thing to touch the
   flags before an exit, and Y is live across it too. */
static int cpy_eq(uint8_t value, uint8_t limit)
{
    cpu.Y = value;
    CPY(limit);
    return cpu.Z;
}

/* ++mem[cell], leaving N and Z from the result.  Pure RAM by construction here, so unlike
   the transliteration's INC_M it does not pay a bus_write range test. */
static void inc_mem(unsigned cell)
{
    uint8_t v = (uint8_t)(mem[cell] + 1);
    mem[cell] = v;
    UPD_NZ(v);
}

/* One 16-bit section coordinate.  Each section owns three of them at +0/+1/+2, and the index
   is a BYTE — the two scratch slots live at $FA and $FD, past the 120 real bytes. */
static unsigned section_word(unsigned byteIndex)
{
    unsigned i = byteIndex & 0xFFu;
    return (unsigned)mem[SECTION_LO_TBL + i] | ((unsigned)mem[SECTION_HI_TBL + i] << 8);
}

/* ===========================================================================
   $3450  abs8 — |A|  (twin #12)
   ---------------------------------------------------------------------------
   Eight bytes and 21 callers, with one trap in them: the `BPL` at $3450 tests the CALLER's
   N flag, not bit 7 of A.  Real callers have just computed A so the two agree; a randomised
   pre-state decorrelates them, and the 6502 follows N.  $80 negates to itself.
   =========================================================================== */
void abs8(void)
{
    if (!cpu.N)
        return;
    /* $3452-$3455 EOR #$FF / CLC / ADC #1.  Its C, V, N and Z are the routine's exit flags —
       C set means the value was 0, V set means it was $80 — so the negate goes through the
       6502 add rather than a unary minus. */
    adc_step((unsigned)(cpu.A ^ 0xFFu), 0x01, 0);
}

/* ===========================================================================
   $254A  road_edge_side — WHICH ROAD SIDE, AND WHICH WAY ROUND IT  (twin #11)
   ---------------------------------------------------------------------------
   Called twice a frame, with $00 and $80, and it EORs that against track_direction — so the
   two calls pick OPPOSITE sides whichever way round the circuit the car is going.  What it
   hands road_edge_walk is three things:

     cpu.X                the byte index into section_coord_lo/hi the walk starts from:
                          section_cursor for one side, section_cursor + 120 for the other
     section_wrap_limit   what the walk's step wraps against, 0 or 120 — this is the cell
                          that makes the two sides traverse the section list in OPPOSITE
                          directions off the same cursor
     road_side_index      0 or 1, which emit_edge_width_offset uses to pick the road's width
                          record and the sign of the offset

   No arithmetic, no hardware, and the two arms differ only in three constants.
   =========================================================================== */

typedef struct {
    uint8_t sectionIndex;   /* the walk's starting byte index into section_coord_lo/hi */
    uint8_t wrapLimit;      /* -> section_wrap_limit */
    uint8_t side;           /* -> road_side_index */
} RoadSide;

static RoadSide road_edge_side_core(uint8_t sideSelect, uint8_t cursor, uint8_t direction)
{
    RoadSide r;
    if ((sideSelect ^ direction) & 0x80u) {          /* $254C-$254E EOR / BPL */
        /* ⭐ The `CLC / ADC #$78` at $2551 is the ONLY thing in the routine that writes V, and
           nothing overwrites it before the RTS — so the far-side arm leaks the overflow of
           cursor + 120 to the caller and the add has to go through the 6502's own. */
        r.sectionIndex = (uint8_t)adc_step(cursor, 0x78, 0);
        r.wrapLimit    = 0x78;
        r.side         = 1;
    } else {
        r.sectionIndex = cursor;
        r.wrapLimit    = 0x00;
        r.side         = 0;
    }
    return r;
}

/* The whole mem[] effect: pick the side and publish the two cells the walk reads.  Shared by
   the 6502-ABI shim and by build_track_geometry, which calls the cores directly. */
static RoadSide road_edge_side_apply(uint8_t sideSelect)
{
    RoadSide r = road_edge_side_core(sideSelect, section_cursor, track_direction);
    section_wrap_limit = r.wrapLimit;
    road_side_index    = r.side;
    return r;
}

void road_edge_side(void)
{
    RoadSide r = road_edge_side_apply(cpu.A);

    cpu.X = r.sectionIndex;
    /* $255F-$2562 `LDA #0 / ROL A`: the side index reaches A through the carry the two arms
       set, and that rotate's own flags are the exit contract — Z means side 0, C is always
       clear, N always clear.  V is not touched anywhere in the routine. */
    cpu.A = r.side;
    cpu.C = 0;
    cpu.N = 0;
    cpu.Z = (uint8_t)(r.side == 0);
}

/* ===========================================================================
   $22FF  road_edge_start — THE NEAR EDGE POINTS  (twin #9)
   ---------------------------------------------------------------------------
   build_track_geometry's first call, and the only part of the road pipeline that REUSES last
   frame's work.  Slots 0..5 of each 40-point half are the edge points beside and behind the
   car — both walks start at slot 6 — and they are far too close to the camera to re-derive
   from the section list every frame.  So:

     1. if the car has crossed a section since the last pass, slide those slots up by one
        (shift_near_edge_points) and clear the pending flag;
     2. bail out entirely when near_edge_first or near_edge_last reads 6, the "nothing to
        rebuild" sentinel;
     3. re-base the slots that did survive, in PAIRS (slot and slot+40), by the same camera
        delta integrate_car_position adds to the heading — that is rebase_edge_point, and it
        also keeps the running horizon maximum;
     4. and then build the ONE genuinely new point: work out which track-file segment it comes
        from, stage its coordinate triple in the scratch section slot, take its bearing, store
        the angle into both halves and — for the near half only — project it to a scan line
        and offer that to the horizon.
     5. Finally hand the next frame the slot below this one, and stop a stale
        horizon_index_prev from letting the road reach past line 7.

   ⚠ SELF-MODIFYING at $231A, and it is a BRANCH OFFSET rather than an opcode: Silverstone's
   $0F skips the re-base pair for the slot the cursor already sits on, and every expansion
   circuit rewrites it to $00, which is a branch to the next instruction — i.e. re-base that
   slot too.  Those are the only two bytes any circuit writes (`make track-patch`), so any
   other offset traps instead of being guessed at.

   No hardware writes and no $FC00-$FEFF access: the whole routine is RAM.
   =========================================================================== */

/* $231A — see above.  Returns 1 when the branch is taken to $232B, i.e. the re-base pair is
   skipped for this slot; sets *trapped when the bytes are neither recognised shape, and the
   caller must then return exactly where the 6502 would.

   ⚠ The OPCODE is tested whether the branch would be taken or not.  A byte that is not a BEQ
   at all is an instruction the model cannot execute, so it traps on the FIRST iteration
   regardless of the comparison — checking it only on the equal path let the twin re-base four
   slots the oracle never reached (case 4 of the first run). */
static int rebase_takes_branch(int equal, int* trapped)
{
    if (mem[0x231A] != 0xF0) {                       /* not a BEQ at all */
        platform_smc_unhandled(0x231A, mem[0x231A]);
        *trapped = 1;
        return 0;
    }
    if (!equal) return 0;                            /* the branch simply is not taken */
    if (mem[0x231B] == 0x0F) return 1;               /* BEQ $232B — skip the pair */
    if (mem[0x231B] == 0x00) return 0;               /* BEQ $231C — fall through, re-base it */
    platform_smc_unhandled(0x231A, (uint16_t)(0x231C + (int)(int8_t)mem[0x231B]));
    *trapped = 1;
    return 0;
}

static void road_edge_start_core(uint8_t nearSlotCount,   /* 6 — also the "nothing to do" mark */
                                 uint8_t halfStride,      /* $28 = 40 */
                                 uint8_t scratchSection,  /* $FD */
                                 uint8_t pointLimit,      /* $3C = 60, one past slot 5 + 40 */
                                 uint8_t staleHorizonCap) /* 7 */
{
    /* $22FF-$2309 — the section step has not been accounted for yet.  A comes back 0 on both
       arms (either the flag WAS 0, or the explicit store made it so), which matters because
       the SMC trap below exits with A live. */
    if (near_edge_scroll_pending != 0) {
        shift_near_edge_points();
        near_edge_scroll_pending = 0;
    }
    cpu.A = 0;

    /* $230C-$2316 — the two sentinels.  near_edge_first == 6 means nothing survived and
       nothing is owed; near_edge_last == 6 means there is a new point but nothing to re-base. */
    if (cpy_eq(near_edge_first, nearSlotCount))
        return;

    if (!cpy_eq(near_edge_last, nearSlotCount)) {
        /* $2318-$232E — RE-BASE the surviving slots, each as a pair 40 apart so the two
           halves of the edge arrays stay in step. */
        unsigned slot = near_edge_last;
        for (;;) {
            int trapped = 0, skip;

            cpu.Y = (uint8_t)slot;
            CPY(near_edge_cursor);                   /* $2318, and the SMC branch's own flags */
            skip = rebase_takes_branch(cpu.Z, &trapped);
            if (trapped)
                return;

            if (!skip) {
                math_lo = (uint8_t)slot;             /* $231C STY $74 */
                cpu.Y   = (uint8_t)(slot + halfStride);
                rebase_edge_point();
                cpu.Y   = math_lo;
                rebase_edge_point();
            }

            slot = (slot + 1) & 0xFFu;               /* $232B-$232E INY / CPY #6 / BCC */
            if (slot >= nearSlotCount)
                break;
        }
    }

    /* $2330-$235C — WHICH TRACK-FILE SEGMENT does the new near point come from?  As many
       segments back from the player's own as there are near slots left to fill, eight bytes
       to a segment, wrapped on the circuit's length in whichever direction the car is going. */
    unsigned span      = (((nearSlotCount - near_edge_cursor) & 0xFFu) << 3) & 0xFFu;
    unsigned playerSeg = mem[CAR_SEGMENT_TBL + PLAYER_CAR];
    unsigned segIndex;

    /* ⚠ EVERY add and subtract from here on goes through the 6502's own, because V escapes:
       the routine's last flag-setting operation is a CMP, which does not write V, so whatever
       the final ADC/SBC left is what the caller sees.  (The first version used plain C here
       and failed on V alone.) */
    if (track_direction & 0x80u) {                   /* $2338-$234C, running the other way */
        math_lo  = (uint8_t)span;                    /* $233C, and the store is observable */
        segIndex = sub_from(adc_step(playerSeg, 0x08, 0), math_lo);
        if (!cpu.C)                                  /* wrapped back past segment zero */
            segIndex = adc_step(segIndex, segment_count_x8, 0);
    } else {                                         /* $234F-$2358 */
        segIndex = adc_step(span, playerSeg, 0);
        if (cmp_ge(segIndex, segment_count_x8))
            segIndex = sub_from(segIndex, segment_count_x8);
    }
    near_segment_index = (uint8_t)segIndex;

    /* $2360-$23A9 — the new point, TWICE: once at its own slot and once 40 up, which is what
       keeps the two halves parallel.  $3C = 60 is one past slot+40 for every legal slot, so
       the loop normally runs exactly twice, and only the first pass is under 40 and therefore
       gets a projected scan line at all.  The second reads the segment's SECOND triple. */
    unsigned point   = near_edge_cursor;
    unsigned segByte = segIndex;
    for (;;) {
        shared_counter_42 = (uint8_t)point;

        cpu.X = scratchSection;
        cpu.Y = (uint8_t)segByte;
        load_section_triple();                       /* track file -> the scratch section slot */
        bearing_to_section();                        /* ...and its bearing from the camera */

        /* $236C-$2376 — going the other way round, the point belongs to the OTHER half. */
        cpu.Y = (uint8_t)(track_direction & 0x80u ? (point ^ halfStride) : point);
        emit_edge_bearing();                         /* edge_x[Y] = bearing - car_heading */

        if (point < halfStride) {                    /* $2379 CPX #$28 */
            cpu.X = scratchSection;
            project_point();
            unsigned line = projected_line;
            mem[EDGE_Y_TBL + point]              = (uint8_t)line;
            mem[EDGE_Y_TBL + halfStride + point] = (uint8_t)line;

            /* $238C-$2398 — the frame's HORIZON is the largest projected scan line, ties
               broken in favour of the higher point index. */
            if (line > horizon_extent ||
                (line == horizon_extent && point >= horizon_index)) {
                horizon_extent = (uint8_t)line;
                horizon_index  = (uint8_t)point;
            }
        }
        cpu.X = (uint8_t)point;                      /* $2377 / $2382 LDX $42 */

        unsigned next = adc_step(point, halfStride, 0);   /* $239B, and its V reaches the exit */
        if (next >= pointLimit)                      /* $239E CMP #$3C */
            break;
        point   = next;
        segByte = adc_step(near_segment_index, 0x03, 0);  /* $23A3 — the segment's 2nd triple */
    }

    /* $23AC-$23B8 — hand the next frame the slot below this one, then make sure a stale
       horizon_index_prev cannot leave the road reaching further up than line 7. */
    cpu.X = (uint8_t)((near_edge_cursor - 1) & 0xFFu);
    clamp_near_edge_cursor();

    /* ⭐ The CMP is the routine's LAST flag-setting operation and A = 7 is its exit value, so
       this one has to be spelled as the 6502's compare. */
    if (!cmp_ge(staleHorizonCap, horizon_index_prev))
        horizon_extent = staleHorizonCap;
}

void road_edge_start(void)
{
    road_edge_start_core(0x06, (uint8_t)EDGE_HALF, (uint8_t)SECTION_NEAR, 0x3C, 0x07);
}

/* ===========================================================================
   $23D2  road_edge_walk — ONE ROAD SIDE, FROM THE CURSOR INTO THE DISTANCE  (twin #10)
   ---------------------------------------------------------------------------
   The road pipeline's real walk, run once per side.  Starting from the section byte index
   road_edge_side chose, it emits up to 18 edge points, stepping further along the section
   list for each one (edge_walk_step_tbl) so the far half of the road costs almost nothing.
   Every point is three things: its bearing from the camera turned into an angle relative to
   the car (emit_edge_bearing_at_cursor), a perspective divide to a scan line
   (project_point), and a second angle offset by the road's width there
   (emit_edge_width_offset, which is also where the frame's corner markers come from).

   Two things make it more than a loop:

     * THE RUNNING NEAREST.  Whenever a point's scaled distance beats edge_nearest_lo/hi it
       becomes project_point's far clip, and the section count at that moment becomes the
       SUBDIVISION FLOOR — the walk will not interpolate before it.
     * SUBDIVISION.  A step that lands behind the camera or past the clip, or one where the
       road has swung more than $14 off the view axis in a single point, is replaced by three
       quarter-way midpoints staged in the scratch section triple; one of them is emitted and
       the side ends there.  A midpoint is not allowed to append a corner marker, which is
       what the marker_count save/restore around the call is for.

   ⚠ SELF-MODIFYING at $248B: Silverstone's `BCS $24B8 / JMP $2403` (stop, or subdivide) is
   rewritten by every expansion circuit into a single JMP into its own hook block.
   ⚠ In the transliteration the body is the multi-entry region `region_23d8` rather than one
   function, because $2490 is both a branch target and a container split; the oracle for this
   twin is the $23D2 stub plus that whole region.
   =========================================================================== */

/* $2477-$2489 — |edge_x_hi[…]| against the off-axis threshold, spelled with the 6502's
   LDA/BPL/EOR/CMP because both exits below inherit A and the compare's flags.  Note EOR #$FF
   rather than a true negate: one less in magnitude, which is all a threshold needs. */
static int angle_off_axis(unsigned addr, uint8_t threshold)
{
    unsigned a = load_a(mem[addr]);
    if (cpu.N)
        a = load_a((uint8_t)(a ^ 0xFFu));
    return cmp_ge(a, threshold);
}

/* $2403-$2469 — the step was too coarse.  Interpolate three quarter-way midpoints between
   the section point the walk came from and this one, stage them in the scratch triple, and
   emit THAT point instead; then the side is finished either way.  Emits nothing at all when
   this was the side's very first point — the road starts behind the camera. */
static void road_edge_walk_subdivide(unsigned section, uint8_t midSlot)
{
    if (load_a(shared_counter_42) == 0)              /* $2403-$2407 */
        return;

    unsigned prev = walk_prev_section;
    int i;

    math_hi        = 0;                              /* $2408 — the midpoint slot counter */
    shared_temp_77 = (uint8_t)section;               /* $240E — the section index, parked */

    for (i = 0; i < 3; i++) {                        /* the triple's three components */
        unsigned here    = (section + i) & 0xFFu;
        unsigned there   = (prev + i) & 0xFFu;
        unsigned base    = section_word(there);

        /* $2410-$241C — the 16-bit gap, and its high byte's sign is what the shifts need. */
        math_lo = (uint8_t)sub_from(mem[SECTION_LO_TBL + here], mem[SECTION_LO_TBL + there]);
        unsigned deltaHi = sbc_step(mem[SECTION_HI_TBL + here],
                                    mem[SECTION_HI_TBL + there], cpu.C);
        unsigned delta   = ((deltaHi << 8) | math_lo) & 0xFFFFu;

        /* $241F-$242A — two ARITHMETIC shifts right, i.e. a quarter of the signed gap.  The
           sign has to be rotated in twice, so the 6502 stashes it on the STACK across the
           first pair of RORs — and the byte that push leaves at $01xx is state the
           differential compares, which is the only reason it is spelled out here. */
        cpu.C = cpu.N;
        PHP();
        PLP();
        unsigned quarter = ((delta >> 2) | ((delta & 0x8000u) ? 0xC000u : 0u)) & 0xFFFFu;
        unsigned mid     = (base + quarter) & 0xFFFFu;

        math_lo        = (uint8_t)quarter;           /* $2425 — after the two RORs */
        shared_temp_76 = (uint8_t)(quarter >> 8);    /* $242B */

        mem[SECTION_LO_TBL + midSlot + i] = (uint8_t)mid;
        mem[SECTION_HI_TBL + midSlot + i] = (uint8_t)(mid >> 8);

        if (i < 2) {                                 /* $2445-$244B, skipped on the last pass */
            math_hi        = (uint8_t)(i + 1);
            shared_temp_77 = (uint8_t)(section + i + 1);
        }
    }

    /* $2450-$2467 — the midpoint's own angle and projection.  Past the clip it contributes
       nothing; otherwise it gets its width offset with the marker list frozen. */
    cpu.X = midSlot;
    emit_edge_bearing_at_cursor();
    project_point();
    if (cpu.C)
        return;

    cpu.X              = walk_prev_section;
    marker_count_saved = marker_count;               /* $245C — no corner marker for a midpoint */
    emit_edge_width_offset();
    marker_count       = (uint8_t)load_a(marker_count_saved);
    inc_mem(MEM_edge_cursor);                        /* $2467, and its N/Z are the exit flags */
}

static void road_edge_walk_core(uint8_t firstPoint, uint8_t sectionIndex,
                                uint8_t midSlot,      /* $FA */
                                uint8_t pointCap,     /* $12 = 18 points */
                                uint8_t offAxis)      /* $14 */
{
    unsigned section = sectionIndex;

    edge_cursor       = firstPoint;                  /* $23D2 */
    shared_counter_42 = 0;                           /* $23D6 — points emitted so far */

    for (;;) {
        /* $23D8 — this point's angle, and how far away it is.  A comes back as the high byte
           of the distance point_distance_hypot ($0CA5) left in point_dist_lo/hi. */
        cpu.X = (uint8_t)section;
        emit_edge_bearing_at_cursor();
        unsigned distHi = cpu.A;

        /* $23DB-$23FA — the RUNNING NEAREST, which is also project_point's far clip and the
           floor below which the walk refuses to subdivide. */
        if (distHi < edge_nearest_hi ||
            (distHi == edge_nearest_hi && edge_nearest_lo >= point_dist_lo)) {
            edge_nearest_hi         = (uint8_t)distHi;
            edge_nearest_lo         = point_dist_lo;
            edge_nearest_section    = shared_counter_42;
            nearest_edge_cursor     = edge_cursor;
            nearest_edge_bearing_hi = mem[EDGE_X_HI_TBL + edge_cursor];
        }

        /* $23FC-$2401 — project it.  Carry = past the far clip, N = behind the camera. */
        cpu.X = (uint8_t)section;
        project_point();
        if (cpu.C || cpu.N) {
            road_edge_walk_subdivide(section, midSlot);
            return;
        }

        /* $246A — EMIT: the point's second angle, and any corner marker it carries. */
        emit_edge_width_offset();

        /* $246D-$248F — past the subdivision floor, has the road swung more than $14 off the
           view axis in this one step?  If so, subdivide — unless the point BEFORE it was
           already out there, in which case the side is done. */
        if (cmp_ge(shared_counter_42, edge_nearest_section) && !cpu.Z) {
            unsigned here = edge_cursor;
            cpu.Y = (uint8_t)here;                                   /* $2475 LDY $12 */
            if (angle_off_axis(EDGE_X_HI_TBL + here, offAxis)) {
                int prevFar = angle_off_axis((EDGE_X_HI_TBL - 1) + here, offAxis);

                /* ⚠ SMC $248B-$248F — see the header.  Both arms are EXITS, so A and the
                   compare's flags from angle_off_axis are what the caller sees. */
                if (mem[0x248B] == 0xB0 && mem[0x248D] == 0x4C) {    /* unpatched: Silverstone */
                    if (!prevFar)
                        road_edge_walk_subdivide(section, midSlot);
                    return;
                }
                if (mem[0x248B] == 0x4C) {                           /* a circuit's own JMP */
                    uint16_t target = (uint16_t)(mem[0x248C] | (mem[0x248D] << 8));
                    if (target >= 0x5300 && target <= 0x5A25) revs_track_hook(target);
                    else                                      platform_smc_unhandled(0x248B, target);
                    return;
                }
                platform_smc_unhandled(0x248B, mem[0x248B]);
                return;
            }
        }

        /* $2490-$24B5 — keep the point, then step the section index.  18 points is the cap;
           section_wrap_limit is what sends the two sides opposite ways round the list. */
        walk_prev_section = (uint8_t)section;
        edge_cursor++;
        shared_counter_42++;

        unsigned emitted = shared_counter_42;
        cpu.Y = (uint8_t)emitted;
        CPY(pointCap);                               /* $2498, and its flags reach the exit */
        if (cpu.C)
            return;

        unsigned step = mem[WALK_STEP_TBL + emitted];
        math_lo = (uint8_t)step;                     /* $249F — observable */

        unsigned from = section;
        if (!cmp_ge((section - section_wrap_limit) & 0xFFu, (uint8_t)step))
            from = (section + 0x78) & 0xFFu;         /* $24A9 — round the 120-byte list */
        section = (from - step) & 0xFFu;
    }
}

void road_edge_walk(void)
{
    road_edge_walk_core(cpu.A, cpu.X, (uint8_t)SECTION_MID, 0x12, 0x14);
}

/* $2505-$250C and $2513-$251A — ONE ROAD SIDE.  road_edge_side picks which side and which
   traversal direction the walk uses (A=0 and A=$80 are opposites whatever the car's
   direction bit holds); the walk then emits that side's points from `firstPoint` upward. */
static void road_side_walk(uint8_t sideSelect, uint8_t firstPoint)
{
    RoadSide side = road_edge_side_apply(sideSelect);
    road_edge_walk_core(firstPoint, side.sectionIndex, (uint8_t)SECTION_MID, 0x12, 0x14);
}

/* $253B-$2549 — HOW WIDE IS THE ROAD AT THE HORIZON?  The two sides' x at the horizon point,
   differenced and halved: half the apparent road width, which $1FE4 reads to decide how much
   of the distance is worth drawing.  Entered with the horizon point in Y and returning with
   the halved value in A, because both are part of the routine's exit contract.

   ⚠ SMC $2542-$2545: an expansion circuit replaces the `JSR abs8 / LSR A` pair with a call of
   its own plus a NOP, so on those circuits the width is NOT halved. */
static void horizon_half_width_at(unsigned horizonPoint)
{
    sub_from(mem[EDGE_X_HI_TBL + horizonPoint],
             mem[EDGE_X_HI_TBL + EDGE_HALF + horizonPoint]);

    if (mem[0x2542] == 0x20 && mem[0x2545] == 0x4A) {           /* unpatched: Silverstone */
        abs8();
        horizon_half_width = (uint8_t)lsr_a(cpu.A);
    } else if (mem[0x2542] == 0x20 && mem[0x2545] == 0xEA) {    /* a circuit's own call */
        uint16_t target = (uint16_t)(mem[0x2543] | (mem[0x2544] << 8));
        if (target == 0x3450)                       abs8();
        else if (target >= 0x5300 && target <= 0x5A25) revs_track_hook(target);
        else { platform_smc_unhandled(0x2542, target); return; }
        NOP();
        horizon_half_width = cpu.A;
    } else {
        platform_smc_unhandled(0x2542, mem[0x2542]);
    }
}

/* `firstPoint` per side: the cursor each walk starts from.  They are 6 and $2E = 6 + 40 — the
   same offset into each half of the 2x40 edge arrays, which is what makes the two lists
   parallel and lets everything downstream address a side by adding 40. */
static void build_track_geometry_core(uint8_t firstPointSide0, uint8_t firstPointSide1)
{
    horizon_extent = 0;              /* $24F6: the road reaches nowhere until a walk says so */
    road_edge_start();               /* the nearest point of each side, and last frame's clamp */

    edge_nearest_hi = 0xFF;          /* no nearest point yet: the first one always wins */
    edge_nearest_section = 0x0D;     /* ...and do not subdivide before section 13 */

    road_side_walk(0x00, firstPointSide0);
    edge_end_side0 = edge_cursor;    /* where side 0 stopped, for draw_road to pair up */
    road_side_walk(0x80, firstPointSide1);

    /* $251D-$2529 — WHICH POINT IS THE HORIZON?  The walks record it as an index into
       whichever half they were writing, so fold it back into 0..39 and keep it for next
       frame's road_edge_start, which clamps the horizon down when it climbed too far. */
    unsigned horizonPoint = horizon_index;
    if (cmp_ge(horizonPoint, 0x28)) {
        horizonPoint = sub_from(horizonPoint, 0x28);
        horizon_index = (uint8_t)horizonPoint;
    }
    /* The `TAY` at $2528 — and it has to happen HERE, not at the tail that reads it: both
       SMC sites below are exits, and on the trap path Y is already the horizon point.  (The
       first version set it after the dispatch and 99 of 400 fixture cases said so.) */
    arg_y((uint8_t)horizonPoint);
    horizon_index_prev = (uint8_t)horizonPoint;

    /* $252B-$2533 — and the horizon can never be the top line of the 80-line space: $4E is
       as far as the road is allowed to reach, because line $4F is the sky's. */
    unsigned horizonLine = horizon_extent;
    if (cmp_ge(horizonLine, 0x4F)) {
        horizonLine = load_a(0x4E);
        horizon_extent = 0x4E;
    }

    /* $2535-$253A — the horizon's line is written into BOTH sides' edge_y at the horizon
       point, so a span walk that reaches it from either side stops on the same line.
       ⚠ SMC $2538: on an expansion circuit the second store is a call to the circuit's own
       code instead, which is why the bytes are dispatched rather than assumed. */
    mem[EDGE_Y_TBL + horizonPoint] = (uint8_t)horizonLine;
    if (mem[0x2538] == 0x99) {                                  /* unpatched: Silverstone */
        mem[EDGE_Y_TBL + EDGE_HALF + horizonPoint] = (uint8_t)horizonLine;
    } else if (mem[0x2538] == 0x20) {
        uint16_t target = (uint16_t)(mem[0x2539] | (mem[0x253A] << 8));
        if (target >= 0x5300 && target <= 0x5A25) revs_track_hook(target);
        else { platform_smc_unhandled(0x2538, target); return; }
    } else {
        platform_smc_unhandled(0x2538, mem[0x2538]);
        return;
    }

    horizon_half_width_at(horizonPoint);
}

/* The 6502-ABI shim.  Both walk cursors are constants in the 6502; they are arguments here
   because they are the one thing that decides which half of the edge arrays each side owns. */
void build_track_geometry(void)
{
    build_track_geometry_core(0x06, 0x2E);
}

/* ===========================================================================
   $1A20  draw_road — THE ROAD RASTERISER  (twin #5)
   ---------------------------------------------------------------------------
   The view pipeline's SECOND producer, and the eleventh call of the frame: it consumes
   build_track_geometry's two edge lists and produces the per-scan-line data view_paint_lines
   turns into screen bytes.  ⭐ It writes SIX visible frame-buffer bytes in a whole frame
   (measured, `make fbwrites`) — the name says rasteriser, and what it really fills are the
   forty $80-spaced source blocks at $3000, the four surface_edge buffers, line_attr_0/1 and
   view_line_surface.  Nothing here draws.

   The body is four passes over the same two edge lists, and the four are one shape:

       for each of the two road sides:            fill_line_attr    — line -> edge point
       for each of the two sides, twice:          draw_surface_spans — the spans themselves
       for each of the two road sides:            mark_line_surfaces — line -> surface class

   with three cells carrying the pass's identity into the callees: surface_style_base steps
   $00 -> $08 -> $10 -> $1C (which of the four style records the span uses), shared_temp_8c
   is the style for everything nearer than the split, and road_split_index is where near
   becomes far.  ⚠ road_split_index is REREAD before every use below and that is deliberate:
   both fill_line_attr ($19A2) and draw_surface_spans ($19F1) write it, so a local copy would
   be a different program.  horizon_index is reread for the same reason — nothing in this
   subtree writes it today, but that is a claim about a large subtree and the cell is one byte.

   No hardware writes and no $FC00-$FEFF access: RAM only, like its sibling above.
   =========================================================================== */

/* $1A98 twice — the third stage, whose return value is the scan line at which that side's
   line_attr buffer stops being valid.  surface_colour_at reads exactly that: at or past
   the limit, the line is sky. */
static uint8_t mark_side_surfaces(uint8_t surfaceClass)
{
    arg_a(road_split_index);
    arg_x(surfaceClass);
    mark_line_surfaces();
    return cpu.Y;
}

/* $19AF x4 — one span pass.  `firstPoint` is where in the edge list the pass starts; the
   pass number selects both the paired-index offset and which surface_edge buffer the spans
   land in. */
static void surface_pass(uint8_t pass, uint8_t firstPoint)
{
    arg_y(pass);
    arg_a(firstPoint);
    draw_surface_spans();
}

/* The two side cursors are arguments; horizon_index and road_split_index deliberately are NOT.
   The cursors are written only by build_track_geometry and its walk, so they cannot change under
   this routine — but road_split_index is written by two of the callees below and horizon_index is
   read four separate times by the 6502, so both are read from mem[] at every use. */
static void draw_road_core(uint8_t endCursorFar, uint8_t endCursorNear)
{
    plot_ptr_lo = 0x80;              /* $1A20: every span plotter stores through ($70),Y */

    /* $1A24-$1A30 — the FAR half of the road.  The split is the horizon point in the 40..79
       half, but never nearer than point $31: the four passes below all measure "near" and
       "far" against it, and letting it come closer than that inverts them. */
    unsigned farBase = adc_step(horizon_index, 0x28, 0);
    road_split_index = (uint8_t)clamp_up_to(farBase, 0x31);

    plot_ptr2_lo = 0;                /* the second screen pointer, for a span that crosses a page */
    shared_temp_8e = 0;              /* ⚠ no reader in this pipeline — see symbols.csv */

    /* Side 1 (the 40..79 half): its line map, then its two span passes. */
    arg_a(0x00);                     /* the low byte of line_attr_0 — A patches the store */
    arg_x((uint8_t)farBase);
    arg_y(endCursorFar);
    fill_line_attr();

    surface_style_base = 0x00;
    surface_pass(0, road_split_index);

    surface_style_base = 0x08;
    shared_temp_8c  = 0x00;
    /* ⚠ Pass 1 does not go through surface_pass, and the difference is the ORDER: the 6502
       loads Y first and computes the base LAST, so it is the addition's flags — not a plain
       `LDA`'s — that reach the callee.  The base is also RECOMPUTED rather than farBase
       reused, because that is what the 6502 does. */
    arg_y(1);
    adc_step(horizon_index, 0x28, 0);
    draw_surface_spans();

    line_attr_0_limit = mark_side_surfaces(0x04);

    /* $1A60-$1A69 — and the NEAR half, whose split is the horizon point itself, floored at
       point 9 for the same reason.  (The 6502's `TAX` here is overwritten two instructions
       later by `LDX $51` with nothing reading X in between, so it is not reproduced.) */
    unsigned nearBase = horizon_index;
    road_split_index = (uint8_t)clamp_up_to(nearBase, 0x09);

    arg_a(0x50);                     /* ...and this is the low byte of line_attr_1 */
    arg_x((uint8_t)nearBase);
    arg_y(endCursorNear);
    fill_line_attr();

    shared_temp_8c  = 0x1C;
    surface_style_base = 0x10;
    surface_pass(2, horizon_index);

    surface_style_base = 0x1C;
    surface_pass(3, road_split_index);

    line_attr_1_limit = mark_side_surfaces(0x14);
}

/* The 6502-ABI shim.  draw_road takes no arguments — the frame's geometry reaches it entirely
   through the edge lists and the three cursor cells — and leaves A, X and the flags wherever
   its last callee left them, which is why the core does not touch them after the call. */
void draw_road(void)
{
    draw_road_core(edge_cursor, edge_end_side0);
}

/* ===========================================================================
   $46A1  apply_driving_model — THE PLAYER CAR'S PHYSICS  (twin #6)
   ---------------------------------------------------------------------------
   The body's 4th call, and nothing else in the frame writes the car's motion.  136 bytes of
   DRIVER over fifteen sub-models, so — like twins #4 and #5 — what it buys is the naming, not
   milliseconds (docs/faithfulness-seam.md §8).  What the routine itself owns is three things:

     1. THE SPEED SPLIT.  car_speed_lo/hi is element 9 of the driving model's 16-bit state
        vector and is SIGNED; the rest of the game only ever reads its magnitude, so this
        routine takes abs16 of it once a frame and publishes road_speed (the integer part) and
        road_speed_frac (the fraction).  wheel_spin_rate — the one cell that means "the car is
        moving" — is road_speed, or the fraction's top nibble when the integer part came out
        zero, so that a crawling car still turns its front wheels.

     2. THE HAND-INTEGRATED ACCUMULATOR.  model_accum_lo/hi is element 8 of the same vector,
        and it is the only element this routine integrates itself.  The sequence is deliberate
        and looks wrong until you read it twice: the entry value is saved, stage_accum_delta subtracts a
        scaled velocity from the accumulator, the next four sub-models therefore run against
        the OFFSET value, and only then is the entry value restored and the frame's real
        increment (model_accum_delta_lo/hi, 1.5x what stage_accum_delta removed) added.

     3. THE OFF-POWER GATE.  Once drive_state reaches 2 — crashed or spinning, the value
        check_crash writes — elements 5..7 of the state vector are forced to zero instead of
        being integrated.

   ⭐ ALL FIFTEEN SUB-MODELS NOW HAVE NAMES (2026-08-17, the queue's "eleven of fifteen callees
   are still FUN_xxxx"), and read in order the chain is legible: the car's angles, a rotation of
   the world-frame pair 0/1 into the car-frame pair 8/9, the accumulator offset, the grip
   limits, the engine, the two axles' slip sound with two steering rotations between them, the
   load terms, then — past the off-power gate — drag, a second rotation, the rate integrator,
   the heading integrator and the camera.  ⚠ Every name is [INFERRED] from what the routine
   COMPUTES; what the fifteen state elements MEAN physically is still open (docs/rename.md).

   No hardware writes and no $FC00-$FEFF access in the driver itself.
   =========================================================================== */

#define MODEL_STATE_LO   0x62D0u   /* the driving model's 16-bit state vector, low bytes */
#define MODEL_STATE_HI   0x62E0u   /* ...and high bytes; element i is +i in each */

/* ⚠ Every model cell below is read from mem[] at the point of use and never cached in a local:
   any of the fifteen sub-models can write any of them, and stage_accum_delta in particular is *supposed* to
   change model_accum under the four calls that follow it. */
static void apply_driving_model_core(uint8_t posLo, uint8_t posHi)
{
    /* $46A1 — the car's body angles, computed from where the car actually is. */
    arg_a(posHi);
    arg_x(posLo);
    compute_car_angles();
    rotate_state_0_into_8();

    /* $46AE — the accumulator's entry value, for the restore at $46DF. */
    model_accum_entry_lo = model_accum_lo;
    model_accum_entry_hi = model_accum_hi;

    /* $46B8-$46CD — the speed split.  abs16_math negates (math_lo, A) in place when A is
       negative, so math_lo has to be re-read after the call, not before.  Every register and
       flag this block leaves is dead: stage_accum_delta opens with `LDA` and `LDY #$58`. */
    math_lo = car_speed_lo;
    load_a(car_speed_hi);
    abs16_math();
    road_speed      = cpu.A;
    road_speed_frac = math_lo;
    wheel_spin_rate = road_speed ? road_speed : (uint8_t)(math_lo & 0xF0);

    /* $46CF-$46DA — the four sub-models that run against the OFFSET accumulator.  stage_accum_delta is
       what offsets it, and what leaves model_accum_delta_lo/hi behind. */
    stage_accum_delta();
    update_grip_limits();
    update_engine_revs();
    arg_x(0x01);
    update_slip_sound();

    /* $46DF-$46F5 — restore, then apply the frame's real increment as one 16-bit add. */
    model_accum_lo = model_accum_entry_lo;
    model_accum_hi = model_accum_entry_hi;
    model_accum_lo = (uint8_t)adc_step(model_accum_lo, model_accum_delta_lo, 0);
    model_accum_hi = (uint8_t)adc_step(model_accum_hi, model_accum_delta_hi, cpu.C);

    /* $46F8-$4703 — and the sub-models that want the accumulator at its new value.  Each of the
       two rotations ends in model_integrate_element, on element 8 and on element $0A. */
    rotate_accum_by_steer();
    arg_x(0x00);
    update_slip_sound();
    rotate_pair_a_by_steer();
    damp_and_derive_loads();

    /* $4706-$4717 — off power: elements 5..7 are zeroed rather than integrated.  The loop's
       exit registers (X = $FF, A = 0, N set) are dead — apply_drag_terms opens with `LDA`. */
    if (cmp_ge(drive_state, 0x02)) {
        int element;
        for (element = 7; element >= 5; element--) {
            mem[MODEL_STATE_LO + element] = 0;
            mem[MODEL_STATE_HI + element] = 0;
        }
    }

    /* $4719-$4725 — the tail.  integrate_car_position is what advances car_heading_lo/hi, so
       the car has not actually moved until the second-to-last call of the chain. */
    apply_drag_terms();
    rotate_state_6_into_3();
    integrate_state_rates();
    integrate_car_position();
    update_camera_and_drive_state();
}

/* The 6502-ABI shim.  The player's own position is the routine's one input — it reaches the
   6502 in A and X — and A, X, Y and the flags come back from update_camera_and_drive_state untouched. */
void apply_driving_model(void)
{
    apply_driving_model_core(car_heading_lo, car_heading_hi);
}

/* ===========================================================================
   $2AD1  draw_track_object — ONE OBJECT SLOT ONTO THE SCREEN  (twin #7)
   ---------------------------------------------------------------------------
   The body's 15th call, entered with an object SLOT index in X.  There are 24 slots and they
   hold both the other cars and the road signs, which is why the main loop can reuse this call
   for slot $17 — the sign build_road_sign has just assembled.

   What it computes is the object plotter's four-cell argument block:

       plot_shape    the low nibble of the slot's own car_flags_shape byte
       plot_x      4 x (the object's distance ahead of the player), biased by $50
       plot_line   object_line[slot]        — projected when the slot was filled
       proj_width    object_width[slot]      — likewise

   and then calls plot_object.  Two things stop it early: a NEGATIVE car_flags_shape byte, which
   is how an empty slot is spelled, and a distance whose high byte falls outside $E0..$1F —
   more than $2000 either side of the player is off the screen entirely.

   ⚠ THE EXIT CONTRACT IS `LDX saved_slot_index`, ON ALL THREE PATHS, and it is not the slot
   just drawn.  Entered at $2ACB (the other entry, one instruction earlier) the routine writes
   that cell itself; entered at $2AD1 the way the main loop does it, the cell still holds
   whatever the previous owner left, so X on the way out is a value from another subsystem.
   Six routines share the cell — docs/rename.md.

   No hardware writes: the plotter's stores are RAM, and the transpiler was already routing
   this routine's own accesses straight to mem[].
   =========================================================================== */

#define CAR_FLAGS_SHAPE      0x018Cu   /* per slot: flags, with the object's shape in bits 0-3 */
#define OBJECT_BEARING_LO    0x0380u   /* per slot: 16-bit track position, low byte */
#define OBJECT_BEARING_HI    0x0398u   /* ...and high byte */
#define OBJECT_LINE       0x03B0u   /* per slot: screen cell column */
#define OBJECT_WIDTH     0x03C8u   /* per slot: screen width */

/* (hi : math_lo) << 1, returning the new high byte — the 6502's `ASL math_lo / ROL A`.  Run
   twice, it is the x4 that turns a distance into a scan line, and math_lo is left holding the
   scaled low byte because the plotter has no use for it. */
static unsigned shift_pair_left(unsigned hi)
{
    cpu.A = (uint8_t)hi;
    ASL_M(MEM_math_lo);
    ROL_A();
    return cpu.A;
}

static void draw_track_object_core(uint8_t slot)
{
    uint8_t flags = mem[CAR_FLAGS_SHAPE + slot];

    if (flags & 0x80) {
        /* $2AD4 — an empty slot.  Nothing is drawn, but A is still the flag byte at the
           tail, so the 6502's `LDA` is reproduced even though its N/Z are overwritten. */
        cpu.A = flags;
    } else {
        plot_shape = (uint8_t)(flags & 0x0F);

        /* How far ahead of the player the object sits, as one 16-bit subtract.  The low
           byte's only reader is the x4 below, but it goes through math_lo because the
           plotter's setup shares that cell. */
        math_lo = (uint8_t)sub_from(mem[OBJECT_BEARING_LO + slot], car_heading_lo);
        unsigned deltaHi = sbc_step(mem[OBJECT_BEARING_HI + slot], car_heading_hi, cpu.C);

        /* $2AE7-$2AF1 — the visibility window, and the two arms are not symmetric because
           the 6502 tests the sign first: behind the player it wants >= $E0, ahead of it
           < $20.  On the reject path A is the high byte and C is that CMP's own carry. */
        int visible = (deltaHi & 0x80) ? cmp_ge(deltaHi, 0xE0)
                                       : !cmp_ge(deltaHi, 0x20);
        if (visible) {
            unsigned row = shift_pair_left(shift_pair_left(deltaHi));
            plot_x = (uint8_t)adc_step(row, 0x50, 0);
            /* The column's own `LDA` flags are dead — the width's LDA one instruction later
               rewrites N and Z, and nothing between them branches. */
            plot_line = mem[OBJECT_LINE + slot];
            proj_width  = (uint8_t)load_a(mem[OBJECT_WIDTH + slot]);
            plot_object();
        }
    }

    arg_x(saved_slot_index);
}

/* The 6502-ABI shim.  The slot arrives in X; everything else the routine needs is in mem[]. */
void draw_track_object(void)
{
    draw_track_object_core(cpu.X);
}

/* ===========================================================================
   $1E15  fill_dash_edge_columns — THE VIEW/DASHBOARD SEAM  (twin #8)
   ---------------------------------------------------------------------------
   The body's 18th call, and the smallest driver in the frame: 35 bytes, two calls of
   fill_edge_column_run.  What it makes happen is that the road view MEETS the front tyres and
   the dashboard without a gap — the columns at the two ends of the viewport still have source
   bytes left at zero after draw_road, and this pass fills each of them with the colour
   surface_colour_at gives for that scan line.

   The two passes are the two ends of the viewport:

       columns 3..6    from scan line $1B, boundary table view_left_start_src  ($0504)
       columns $1A..22 from scan line $2B, boundary table view_right_start_src ($4400)

   ⭐ AND THAT SECOND ARGUMENT IS WHY THE CALL MATTERS DOWNSTREAM.  plot_ptr2 is what
   fill_edge_column_run patches its store through on alternate passes, so each run writes the
   run's FIRST cell into one of those two per-scan-line tables instead of into the column's own
   $80-byte source block.  view_paint_lines then composes each row's leading edge cell out of
   exactly those bytes: this call is their only producer.

   No hardware writes; the whole subtree lives in RAM.

   ⚠ ONE SABOTAGE HERE CANNOT FAIL, AND IT IS A PROPERTY OF THE CALLEE, NOT A FIXTURE GAP:
   the ORDER of the three register loads below is not observable.  fill_edge_column_run stows
   A, X and Y into $42, $85 and $7F before touching any of them, and the first thing that reads
   a flag in the whole subtree ($1DB3) comes after $1DF5's own `LDA` has reset N and Z — so no
   incoming flag survives to be wrong.  Swapping WHICH register carries which value does fail,
   as it must; swapping the order does not.
   =========================================================================== */

#define VIEW_LEFT_START_SRC   0x0504u   /* per scan line: the LEFT run's first source byte */
#define VIEW_RIGHT_START_SRC  0x4400u   /* ...and the RIGHT run's */

/* One end of the viewport.  `stopColumn` is exclusive of the plot_ptr2 half of the run and
   inclusive of the plot_ptr half — fill_edge_column_run walks two columns per iteration and
   tests the second one against it. */
static void edge_column_pass(uint16_t startSrc, uint8_t firstColumn, uint8_t stopColumn,
                             uint8_t firstLine)
{
    plot_ptr2_hi = (uint8_t)(startSrc >> 8);
    plot_ptr2_lo = (uint8_t)startSrc;
    arg_y(firstLine);
    arg_x(firstColumn);
    arg_a(stopColumn);
    fill_edge_column_run();
}

/* The two boundary tables are the arguments because they are the one thing a change of view
   representation moves (docs/direct-bitplane-plan.md §7a); the column and line numbers are
   the viewport's own geometry and stay immediates. */
static void fill_dash_edge_columns_core(uint16_t leftStartSrc, uint16_t rightStartSrc)
{
    edge_column_pass(leftStartSrc,  0x03, 0x06, 0x1B);
    edge_column_pass(rightStartSrc, 0x1A, 0x22, 0x2B);
}

/* The 6502-ABI shim.  No inputs at all — every value is an immediate in the original — and
   A, X, Y and the flags come back from the second fill_edge_column_run. */
void fill_dash_edge_columns(void)
{
    fill_dash_edge_columns_core(VIEW_LEFT_START_SRC, VIEW_RIGHT_START_SRC);
}

/* ===========================================================================
   $0C47  div16by8 — THE ENGINE'S DIVIDE  (twin #13)
   ---------------------------------------------------------------------------
   mul8's opposite number, and the one function project_point and bearing_to_section call:
   unsigned (A : math_lo) / shared_temp_76, quotient back in math_lo, remainder in A.  Every
   edge point of every frame goes through it — bearing_to_section divides the smaller
   camera-relative delta by the larger to index the arctan table, project_point divides the
   point's distance by the normalised far clip — so it runs a few hundred times a frame off
   three call sites and nothing else in the engine uses it.

   Eight unrolled restoring steps.  Four things about its contract are worth stating because
   three of them are what the twin has to reproduce and the fourth is why the callers are safe:

     * C IS ALWAYS CLEAR ON EXIT.  The closing `ROL math_lo` ($0CA2) inserts the eighth
       quotient bit and shifts out the ZERO the opening `ASL math_lo` put in — never a quotient
       bit.  N and Z come from the finished quotient, from that same ROL.
     * THE EIGHTH STEP DOES NOT RESTORE ($0C9E-$0CA2 computes the quotient bit with a bare CMP
       and no SBC), so the remainder handed back is the true remainder PLUS the divisor whenever
       the quotient is odd.  That is a deliberate cycle saving in the original, not a bug: no
       caller reads the remainder.
     * V is left by whichever of the first SEVEN steps subtracted last — which is the one place
       this twin still needs the 6502, see below.
     * A divisor of 0 makes every step "fit" and returns $FF; a dividend whose high byte is >=
       the divisor overflows the 8-bit quotient silently.  Neither reaches here from the game:
       both callers normalise the divisor left until bit 7 is set, and both branch to their own
       degenerate arm when the two magnitudes come out equal.  The fixture feeds both anyway.

   ⭐ WHAT THE TWIN CHANGES.  The 6502 shifts a byte pair — `ASL math_lo / ROL A` — because it
   has no wider register; the 68000 shifts the whole 16-bit remainder:dividend word in one
   `add.w`, and the quotient bits accumulate in the low half as the dividend bits leave the top.
   That is the entire byte-at-a-time chain gone, and with it ~56 interpreted instructions worth
   of per-instruction flag bookkeeping per call.  There are no hardware writes and no
   $FC00-$FEFF access: the routine is four zero-page cells, so the transpiler was already
   routing it straight to mem[] and what the twin removes is interpreter.

   ⚠ WHY NOT `DIVU.W`, WHICH IS EXACTLY THIS OPERATION.  The exit V flag.  DIVU hands back the
   quotient and the true remainder in one 140-cycle instruction, but V here belongs to the LAST
   of the seven conditional subtracts, and recovering which step that was needs the quotient's
   lowest set bit above bit 0 plus a SECOND divide to get that step's partial remainder — 2x
   DIVU plus a bit scan, measurably no faster than the loop below, and only valid on the
   `dividendHi < divisor` path.  ⭐ The unlock is upstream, not here: all four exit flags are
   dead at all three call sites (bearing_to_section's `LDA #0` and project_point's `LDA math_lo
   / CMP #$80` overwrite N, Z and C, and nothing reads V), so once project_point and
   bearing_to_section are twins themselves the flags become internal and a single DIVU is
   provably enough.  Worth ~1% of the frame, i.e. under the noise floor — docs/perf-method.md.
   =========================================================================== */

typedef struct { uint8_t quotient, remainder; } Div16By8;

/* `dividendHi` arrives in A and is the top half of the 16-bit numerator; `dividendLo` is
   math_lo, which the loop consumes bit by bit and hands back as the quotient. */
static Div16By8 div16by8_core(uint8_t dividendHi, uint8_t dividendLo, uint8_t divisor)
{
    /* remainder in the high byte, dividend-becoming-quotient in the low byte — the pair the
       6502 keeps in A and math_lo, here shifted as ONE word. */
    uint16_t work = (uint16_t)(((uint16_t)dividendHi << 8) | dividendLo);
    Div16By8 r;
    int step;

    for (step = 0; step < 8; step++) {
        /* The bit leaving the top of the word is the remainder's ninth bit.  The 6502 keeps it
           in C and takes it as "the divisor fits" without comparing at all ($0C4A `BCS`),
           which is right: a nine-bit remainder always exceeds an eight-bit divisor. */
        int ninthBit = (work & 0x8000u) != 0;
        work = (uint16_t)(work << 1);

        if (ninthBit || (work >> 8) >= divisor) {
            /* Steps 1..7 restore; the eighth deliberately does not (see the header).  The
               subtract goes through the 6502's own SBC because its V is the routine's exit V
               and because decimal mode changes the result byte — the fixture randomises D. */
            if (step < 7)
                work = (uint16_t)((work & 0x00FFu) | ((unsigned)sub_from(work >> 8, divisor) << 8));
            work |= 1u;              /* the quotient bit, carried up the low half by the next shift */
        }
    }

    r.quotient  = (uint8_t)work;
    r.remainder = (uint8_t)(work >> 8);
    return r;
}

/* The 6502-ABI shim.  The dividend arrives split between A and math_lo and the divisor in
   shared_temp_76 — all three are the callers' own scratch cells, so they are arguments here
   and mem[] sees only the quotient. */
void div16by8(void)
{
    Div16By8 r = div16by8_core(cpu.A, math_lo, shared_temp_76);

    math_lo = r.quotient;
    /* $0CA2 `ROL math_lo`, the routine's last instruction: N and Z from the finished quotient,
       and C from the zero the opening ASL inserted — clear on every path through the loop. */
    UPD_NZ(r.quotient);
    cpu.C = 0;
    cpu.A = r.remainder;
}
