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
 * FLAGS — both twins here declare AXY+flags live — and C has no carry or overflow.  Where a
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

/* ===========================================================================
   $4E5C  irq1v_band_schedule — THE RASTER-BAND PALETTE/MODE SCHEDULE
   ===========================================================================

   WHAT IT COMPUTES.  Revs owns IRQ1V and drives its own display from a User VIA T1
   timer.  One PAL field is FIVE interrupts; each one repaints the Video ULA for the
   band that is about to be scanned out and reloads T1 with how long that band lasts.
   irq_band_state says which band is next.  ⭐ THE HANDLER DRAWS NOTHING — that is what
   its 2026-08-17 rename settled (docs/rename.md): the only game work in the whole cycle
   is the `body_tick_xor_anim` call in band 4, 4% of the field.  A BBC has to run a raster
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
     4   dashboard   four entries from $347C (colour 3 → cyan), then body_tick_xor_anim,
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

        /* The horizon split.  MoveHorizon ($4F44) puts band 1's duration in
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

        /* body_tick_xor_anim is an ordinary JSR target, so it is entered with whatever the
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
        body_tick_xor_anim();
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
        ($60) over the store of unit view_stop_a_tbl[line], runs the chain, and composes
        the boundary cell itself out of view_bnd_a_mask/fill.  It then enters chain B at
        view_start_b_tbl[line] for the second run.
     3  $7F18, line $1B..$03 — as phase 2, but BOTH chains get a planted stop and a
        computed start, and the driver steps the scan-line pointers itself.

   view_paint_restore ($7BBF) then puts `STA` back over the three planted RTSs and `CPX`
   back at $7EEE, so the chain leaves no patch behind.  ⚠ It does NOT reset the
   $7D24/$7F24/$7F7D *records* of where it planted, and the drivers skip the re-plant when
   the stop is unchanged — so the first line of a phase can legally run with no stop
   planted at all.  Faithful, and reproduced.

   ⚠ THE CONTROL TABLES OVERLAP THE SOURCE BLOCKS, and that is not a mistake to tidy up:
   view_stop_b_tbl ($3080) is cell column 1's source area, so the chain can ZERO a byte the
   driver is about to read.  Every table read therefore happens exactly where the 6502 did
   it — hoisting one out of the loop changes behaviour.

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

/* The per-scan-line control tables (symbols.csv carries the same names).  Addresses rather
   than mem.h aliases: they are indexed tables, so the twin adds the line itself. */
#define VIEW_SRC_BLOCKS     0x3000u   /* forty $80-spaced source blocks, one per cell column */
#define VIEW_CELL_BYTES     0x6000u   /* source byte -> screen byte */
#define VIEW_STOP_A         0x3150u   /* chain A's stop unit for this line */
#define VIEW_STOP_B         0x3080u   /* chain B's stop unit (and cell column 1's sources) */
#define VIEW_START_B        0x30D0u   /* chain B's entry unit */
#define VIEW_EDGE_INDEX     0x3050u   /* index into the four edge mask/fill tables */
#define VIEW_EDGE_MASK_A    0x3679u
#define VIEW_EDGE_FILL_A    0x3579u
#define VIEW_EDGE_MASK_B    0x36F9u
#define VIEW_EDGE_FILL_B    0x35F9u
#define VIEW_BND_A_MASK     0x38D0u
#define VIEW_BND_A_FILL     0x3350u
#define VIEW_BND_B_MASK     0x3950u
#define VIEW_BND_B_FILL     0x33D0u
#define VIEW_BND_A_SRC      0x0504u   /* per-line source byte for chain A's boundary cell */
#define VIEW_BND_B_SRC      0x4400u   /* ...and chain B's */
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
            int lastSeg = (unit >= 32);
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
            MEM_QUAL unsigned char* segBase = (unit < 32) ? mem + base0 : dp1;
            MEM_QUAL unsigned char* dp      = segBase + (((unsigned)unit & 31u) << 3);
            MEM_QUAL unsigned char* segEnd  = (unit < 32) ? mem + base0 + 256 : dp1 + 64;
            int curUnit = unit;
            int segLimit = (unit < 32) ? 32 : 40;
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
    for (;;) {
        unsigned edge, entry, next;
        int carry_out;

        v->line = (v->line - 1) & 0xFF;

        /* chain A's stop */
        v->cell = mem[VIEW_STOP_A + v->line];
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
        next = step_scanline(&carry_out);
        if (!(next & 7) && carry_out) {
            plot_ptr_lo  = (unsigned char)next;
            plot_ptr2_lo = (unsigned char)next;
        }

        /* chain A: enter at $F1 - view_stop_b_tbl[line], with the boundary cell composed
           from the per-line source byte and the edge tables */
        v->byte = sub_from(0xF1, mem[VIEW_STOP_B + v->line]);
        mem[0x7F68] = (unsigned char)v->byte;
        edge    = mem[VIEW_EDGE_INDEX + v->line];
        v->byte = view_compose(mem[VIEW_BND_A_SRC + v->line],
                               mem[VIEW_EDGE_MASK_A + edge],
                               mem[VIEW_EDGE_FILL_A + edge]);
        v->cell = v->byte;                          /* TAY: N/Z already match */
        if (!view_enter_chain(v, 0x7F67, 0x7F68, 0x7C)) { view_commit(v); return; }
        v->byte = view_compose(v->byte, mem[VIEW_BND_A_MASK + v->line],
                                        mem[VIEW_BND_A_FILL + v->line]);
        REVS_PLOT_CELL(view_screen_addr(MEM_plot_ptr_lo, v->cell), (uint8_t)v->byte);
        bus_write(view_screen_addr(MEM_plot_ptr_lo, v->cell), (uint8_t)v->byte);

        /* chain B: the same again, one page down and with its own tables.  ⚠ the stop is
           re-read here — the chain may have zeroed it (see the header). */
        v->cell = mem[VIEW_STOP_B + v->line];
        if (!view_move_stop(v, v->cell, VIEW_REC_B3, 0x7F7C, 0x7F87, 0x7F88, 0x7E)) {
            view_commit(v);
            return;
        }
        entry   = mem[VIEW_START_B + v->line];
        v->cell = entry;
        mem[0x7F9B] = (unsigned char)entry;
        v->byte = view_compose(mem[VIEW_BND_B_SRC + v->line],
                               mem[VIEW_BND_B_MASK + v->line],
                               mem[VIEW_BND_B_FILL + v->line]);
        v->cell = v->byte;                          /* TAY */
        if (!view_enter_chain(v, 0x7F9A, 0x7F9B, 0x7E)) { view_commit(v); return; }
        math_hi = (unsigned char)v->cell;           /* the chain's cell, parked in scratch */
        edge    = mem[VIEW_EDGE_INDEX + v->line];
        v->byte = view_compose(v->byte, mem[VIEW_EDGE_MASK_B + edge],
                                        mem[VIEW_EDGE_FILL_B + edge]);
        v->cell = math_hi;
        UPD_NZ(v->cell);                            /* the `LDY math_hi` that reloaded it */
        REVS_PLOT_CELL(view_screen_addr(MEM_plot_ptr2_lo, v->cell), (uint8_t)v->byte);
        bus_write(view_screen_addr(MEM_plot_ptr2_lo, v->cell), (uint8_t)v->byte);

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
    v->byte = load_a(OP_RTS);
    mem[VIEW_CHAIN_END] = (unsigned char)v->byte;

    for (;;) {
        v->line = (v->line - 1) & 0xFF;

        v->cell = mem[VIEW_STOP_A + v->line];
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
            v->cell     = mem[VIEW_START_B + v->line];   /* chain B's entry, for the store */
            UPD_NZ(v->cell);                             /* its `LDY` outlives the chain */
            mem[0x7D4D] = (unsigned char)v->cell;
        }

        paint_cells(v, 0, 0, 1);                /* the JSR through view_next_scanline */

        v->byte = view_compose(v->byte, mem[VIEW_BND_A_MASK + v->line],
                                        mem[VIEW_BND_A_FILL + v->line]);
        REVS_PLOT_CELL(view_screen_addr(MEM_plot_ptr_lo, v->cell), (uint8_t)v->byte);
        bus_write(view_screen_addr(MEM_plot_ptr_lo, v->cell), (uint8_t)v->byte);

        v->byte = view_compose(mem[VIEW_BND_B_SRC + v->line],
                               mem[VIEW_BND_B_MASK + v->line],
                               mem[VIEW_BND_B_FILL + v->line]);
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
