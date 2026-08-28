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
#include "../cpu/m68k_math.h"   /* revs_mulu16: MULU.W, the 68000 op mul8 stands in for */
#include "revs_decl.h"
#define REVS_MEM_ALIASES
#include "mem.h"
#include "revs_native_seam.h"
#include "../platform/platform_c.h"
#include "../platform/bbc_screen.h"   /* bbc_ula_palette_write / bbc_ula_control_write */
#include "../platform/probe.h"        /* PROBE_PHASE(): the phase-29 body-arm split */
#include "../platform/shape.h"        /* PROBE_SHAPE_DASH_UNIT(): the §7a unit counter */
#include "../platform/revs_plot.h"    /* REVS_PLOT_*: the direct-to-bitplane run plotter */

/* The object/slot-writer chain's exit ABI — A/X/Y + N/Z/V/C returned by value so a core stays
   cpu-free; the thin shim (or a caller whose own exit ABI is this) replays it onto cpu. */

/* The object plotter (twin #94), defined far below but called from race_main_loop_core with
   the object slot count.  The main loop reaches it through the core, not the 6502-ABI shim. */
SlotExit draw_track_object_core(uint8_t slot, uint8_t entryY, uint8_t entryV, uint8_t entryC);

/* The rest of the frame body's steps, all defined far below.  race_main_loop_core reaches each
   through its core so the whole hot path is core-to-core with no 6502-ABI shim hops. */
static void read_driving_controls_core(void);
void apply_driving_model_core(uint8_t posLo, uint8_t posHi);
void build_track_geometry_core(uint8_t firstPointSide0, uint8_t firstPointSide1);
void draw_road_core(uint8_t endCursorFar, uint8_t endCursorNear);
static void build_road_sign_core(void);

/* ===========================================================================
   The flag-carrying primitives.  These exist so that no other line in this file
   has to be written in 6502; see the header.

   ⚠⚠ ALWAYS_INLINE IS LOAD-BEARING HERE, NOT A HINT.  Each of these is a few instructions
   wrapping one cpu.h macro, and GCC leaves them OUT OF LINE at -O3 because they write the
   global `cpu` and have many callers.  In a routine that is nearly all arithmetic that costs a
   `jsr` plus a `movem.l d2-d7,-(sp)` pair PER SUBTRACT, and it is how twins #14/#15 first
   measured 349 painted frames against a 383 control — a twin SLOWER than the transliteration
   it replaced, because the transliteration expands the same macro inline at every site.
   Inlining them recovered 349 -> 371 for those two twins and 383 -> 388 for the corpus that
   was already here (docs/perf-method.md §twins #14/#15).
   ⭐ So: read the objdump for `jsr <sub_from>` before believing any arithmetic twin is fast.
   =========================================================================== */

/* A = value, with N and Z from it.  Used where a value reaches A and an SMC trap can then
   exit the routine with both still live. */

/* a + addend + carry_in, setting C and V.  C chains (the 16-bit pointer step adds three
   times) and V is the one flag the cell chain can leak to its caller — nothing else in it
   writes V at all. */

/* a + addend + carry_in as a VALUE ONLY — the ADC counterpart of sbc_value below, and it
   exists for the same reason: a 16-bit add's low half feeds nothing but the high half's
   carry, so paying cpu.h's five flag stores for it is paying for nothing.  ⚠ Decimal mode is
   honoured, because D changes the RESULT BYTE and not merely the flags. */


/* a - m - !carry_in as a VALUE + borrow-out — the SBC counterpart of adc_value, for the same
   reason (a multi-byte subtract's low halves feed only the next half's borrow).  ⚠ Decimal mode
   is honoured because D changes the RESULT BYTE.  On the 6502 the CARRY out of an SBC is the
   BINARY borrow even in decimal mode (only the accumulator digits are corrected), so .carry is
   computed from the plain subtraction in both branches. */

/* V for ONE add, replayed from its operands — the ADC counterpart of the SBC overflow replay, and it
   exists for the same reason: mul8's exit V is the V of the LAST add in an eight-step chain,
   so the twin computes that one add's overflow instead of the seven dead ones. */

/* V for ONE subtract, replayed from its operands — the SBC counterpart of adc_overflow.
   SBC computes A + ~M + C, so its overflow is ((A^M) & (A^result))>>7 (the two operands
   differ in sign and the result took the sign of M).  Used where a converted subtract's V is
   the only flag that escapes the routine. */

/* value - subtrahend with the borrow clear (SEC/SBC), setting C and V. */

/* value - subtrahend - !carry_in, setting C and V — the second half of a 16-bit subtract,
   where the borrow has to come from the low half's own SBC. */

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

   WHAT IT LEAVES BEHIND.  In mem[]: the pushed X on the 6502 stack (and back) and
   irq_band_state.  band2_duration is no longer in mem[] — it was relocated to a native
   wide value (band2_duration_v, below) by the wide-value cleanup, mechanism (B).  Nothing
   else in mem[].  In the hardware model: $FE6D (the
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

/* ⭐ WIDE-VALUE CLEANUP, mechanism (B): band2_duration ($4F21/$4F22) relocated out of mem[]
   into this native uint16_t.  It is genuine cross-interrupt state — band 1's arm computes it
   (the horizon split remainder) and band 2's arm, a LATER T1 interrupt of the same field,
   loads it — so a file-scope static that persists exactly as the two mem[] bytes did is the
   faithful storage.  Grep confirms this function (and its __t6502 validation oracle, which
   still uses mem[]) are the ONLY readers/writers, so no de-transliteration was needed.
   ⚠ The oracle still writes mem[$4F21/$4F22]; the validate fixture set_ignore's those cells,
   and det_compare.py skips them (they are no longer game state).  set_band2_duration_v below
   lets the fixture seed the standalone band-2 arm to match the oracle's pinned input. */
static uint16_t band2_duration_v;

/* Native-only test hook: seed the relocated value so the validate fixture can drive the
   band-2 consumer arm on a known input (its producer arm ran in an earlier interrupt).
   Unreferenced on the Amiga build → dropped by --gc-sections. */
void set_band2_duration_v(uint16_t v) { band2_duration_v = v; }

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
        band2_duration_v = (uint16_t)rest;
        if (sky <= 0x153Cu) {
            latch = sky;
            break;
        }
    }   /* fall through — zero-height band 2 */

    case 2:     /* the horizon: black / blue / white / green */
        ula_palette_table(0x3458, 15);
        latch = band2_duration_v;
        if ((band2_duration_v >> 8) != 0)
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
        /* chain A enters at $F1 - view_run_right_end[line] (SEC/SBC).  N/Z/C are recomputed
           downstream, but this subtract's V reaches view_paint_lines' exit on paths where
           nothing below rewrites it — replay just that flag (cpu otherwise untouched). */
        v->byte = (uint8_t)(0xF1 - mem[VIEW_RUN_R_END + v->line]);
        cpu.V = sbc_overflow(0xF1, mem[VIEW_RUN_R_END + v->line], 1);
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
void view_paint_lines_core(unsigned screenBase, unsigned firstLine, uint8_t entryCell)
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
    v.cell = entryCell;                   /* the 6502's entry Y — but the first chain overwrites
                                             it before any read, so it is a dead seed (a fixture
                                             sabotage forcing it to 0 changes nothing over 700
                                             cases); threaded in only to mirror the entry ABI */
    UPD_NZ(firstLine);                    /* `LDX #$4F` is the prologue's last flag write */

    paint_cells(&v, 0, 0, 1);
    paint_lines_clipped(&v);
    view_commit(&v);
}

/* The 6502-ABI shim.  $6700/$6800 is character row 10 of the frame buffer — display line
   80 — and $4F is the first scan line painted. */

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

/* What the tail decided about this frame. */

/* A JSR is handed the whole register file, and a callee may branch on the flags before it
   reloads anything.  These three exist so that the argument passing is visibly the 6502's —
   the macro is inside, the call site reads as C — and so that nothing else in this routine
   has to mention a register at all. */
void arg_a(uint8_t v) { LDA(v); }
static void arg_x(uint8_t v) { LDX(v); }
static void arg_y(uint8_t v) { LDY(v); }

/* $16E9's `BIT $05F4` — bit 6 of state_flags lands in V.  Through the macro rather than as a
   plain mask because the test leaves N and V set across the calls that follow it, and "no
   callee reads them" is a claim about a 400-routine subtree, not something to assume here. */
int state_flags_bit6(void)
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

#if defined(REVS_CRASHPROBE) && defined(REVS_PLATFORM_AMIGA)
/* ⭐ Attribute the ~5s crash freeze across race_frame_tail (the hold + body) and
   race_main_loop_core (the reset).  s_crashBodyStartVbi is stamped after each present. */
extern volatile uint16_t      g_vbiCount;
extern volatile unsigned long g_resetFieldsMax, g_resetFieldsLast, g_resetCount;
static unsigned s_crashBodyStartVbi;   /* g_vbiCount right after the last present */
#endif

/* $174B-$17B7 — the frame's verdict.  Everything above is called from here. */
static LoopVerdict race_frame_tail(RestartDepth* depth)
{
    /* Re-arm the raster band cycle if the field it was counting has finished.  A counter
       still inside 0..3 means the frame overran its own field, and it is left alone. */
    if (irq_band_state & 0x80)
        irq_band_state++;

    if (load_a(crash_flag) != 0) {
        /* ⭐ SHOW THE FENCE BEFORE THE HOLD.  check_crash() drew the fence into the view source
           and view_paint_lines() has already painted it into the frame buffer this frame — but
           the next present is not until the top of the frame loop, AFTER the 2 s hold below AND
           the session reset.  On the BBC the framebuffer IS the screen, so the fence is visible
           the instant it is drawn and stays up for the whole hold; here the decode-to-bitplanes
           step needs a present to reproduce that.  Without this one call the crash graphic never
           appears: the user sees the pre-crash frame frozen for the whole ~3 s sequence.  This
           touches no game state (host renderFrame() is a no-op), so the determinism gates stay
           byte-identical; it only makes the already-painted fence visible during the hold.
           ⚠ Amiga-only: on the host renderFrame() fires tickVBI() (bbc_hw.cpp), which advances
           the sim clock — an extra present there would move the trajectory the determinism gates
           pin.  The present is a display concern and the host is headless, so it belongs here. */
#if defined(REVS_PLATFORM_AMIGA)
        platform_render_frame();
#endif
        crash_flag++;                 /* straight back to zero: the crash is handled here */
        /* 100 fields = two seconds of holding the picture.  ⚠ The `LDA #$9C` is kept rather
           than folded into the store, because the value stays in A across the wait below and
           the port's interrupt seam publishes A into mos_irq_a on every field — the oracle's
           store-immediate peephole cannot see that reader. */
        arg_a(0x9C);
        field_countdown = cpu.A;
#if defined(REVS_CRASHPROBE) && defined(REVS_PLATFORM_AMIGA)
        /* ⭐ THE ~5s CRASH-FREEZE MEASUREMENT.  The hold is meant to last 100 field-countdown
           INCs = 2 s.  Snapshot the wall clock (g_vbiCount, one per real PAL field), the fields
           actually DRAINED (g_bodyTicks, = the INCs), and the fields DROPPED (g_bodyTicksDropped)
           across the hold.  Faithful: vbi delta ~100, ticks ~100, drops ~0.  Drain-starved:
           vbi delta >> 100 with drops > 0 — the theorem that a field cycle costs > 20 ms. */
        {
            extern volatile unsigned long g_bodyTicks, g_bodyTicksDropped;
            extern volatile unsigned long g_crashHolds, g_crashHoldVbi, g_crashHoldVbiMax,
                                          g_crashHoldTicks, g_crashHoldDrops, g_crashBodyFieldsMax;
            unsigned vbi0   = g_vbiCount;
            unsigned long tk0 = g_bodyTicks, dr0 = g_bodyTicksDropped;
            unsigned bodyD  = (unsigned)(uint16_t)(vbi0 - s_crashBodyStartVbi);
            if (bodyD > g_crashBodyFieldsMax) g_crashBodyFieldsMax = bodyD;
#endif
        do {
            /* ⭐ THE ENGINE'S ONE TRUE FRAME WAIT.  tick_wheel_spin INCs field_countdown
               once per PAL field, so on the Amiga the VERTB ISR ends this on its own and
               platform_tick_vbi() is a no-op; on the host it IS the interrupt. */
            PROBE_PHASE(0);
            platform_tick_vbi();
            platform_poll_events();
        } while (load_a(field_countdown) & 0x80);
#if defined(REVS_CRASHPROBE) && defined(REVS_PLATFORM_AMIGA)
            unsigned vbiD = (unsigned)((g_vbiCount - vbi0) & 0xFFFFu);
            g_crashHolds++;
            g_crashHoldVbi   += vbiD;
            if (vbiD > g_crashHoldVbiMax) g_crashHoldVbiMax = vbiD;
            g_crashHoldTicks += g_bodyTicks        - tk0;
            g_crashHoldDrops += g_bodyTicksDropped - dr0;
        }
#endif

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
void race_main_loop_core(RestartDepth depth)
{
    for (;;) {
        LoopVerdict verdict;

#if defined(REVS_CRASHPROBE) && defined(REVS_PLATFORM_AMIGA)
        unsigned resetStartVbi = g_vbiCount;
        extern volatile unsigned long g_resetSplit[4];
        unsigned rs = g_vbiCount;
#define RESET_SPLIT(i) do { unsigned n = g_vbiCount; g_resetSplit[i] = (unsigned)(uint16_t)(n - rs); rs = n; } while (0)
#else
#define RESET_SPLIT(i) ((void)0)
#endif
        /* ---- the session reset, nested: FULL falls into MID falls into LATE ---- */
        if (depth >= RESTART_FULL) {
            arg_x(0x00);              /* driver 0 = the player */
            clear_race_clock();
        }
        RESET_SPLIT(0);
        if (depth >= RESTART_MID)
            reset_driving_variables();
        RESET_SPLIT(1);
        if (depth >= RESTART_LATE)
            build_player_car();
        RESET_SPLIT(2);

        arg_a(0x00);
        state_flags = 0;
        scale_wing_settings();                   /* scale the wing settings for the new session */
        RESET_SPLIT(3);
#undef RESET_SPLIT

#if defined(REVS_CRASHPROBE) && defined(REVS_PLATFORM_AMIGA)
        if (depth >= RESTART_LATE) {             /* a real reset ran, not RESTART_NONE */
            unsigned d = (unsigned)(uint16_t)(g_vbiCount - resetStartVbi);
            g_resetFieldsLast = d;
            g_resetCount++;
            if (d > g_resetFieldsMax) g_resetFieldsMax = d;
        }
#endif
        /* ---- one pass = one game frame ---- */
        do {
            /* ⭐ THE PORT'S PAINT HOOK — see the header for why it is here and not at the
               frame wait.  Its own phase, because renderFrame() spins for the field and
               that spin must not land in whatever phase was open across the loop seam. */
            PROBE_PHASE(PROBE_PHASE_FRAMEWAIT);
            PROBE_SHAPE_PHASE(PROBE_PHASE_FRAMEWAIT);
            platform_render_frame();
#if defined(REVS_CRASHPROBE) && defined(REVS_PLATFORM_AMIGA)
            s_crashBodyStartVbi = g_vbiCount;    /* fields from here to the hold = the body cost */
#endif

            PROBE_PHASE(1);  PROBE_SHAPE_PHASE(1);  tick_race_timers();
            PROBE_PHASE(2);  PROBE_SHAPE_PHASE(2);  draw_starting_lights();
            PROBE_PHASE(3);  PROBE_SHAPE_PHASE(3);  read_driving_controls_core();
            PROBE_PHASE(4);  PROBE_SHAPE_PHASE(4);  apply_driving_model_core(car_heading_lo, car_heading_hi);
            PROBE_PHASE(5);  PROBE_SHAPE_PHASE(5);  build_track_geometry_core(0x06, 0x2E);
            PROBE_PHASE(6);  PROBE_SHAPE_PHASE(6);  place_player_in_section();
            PROBE_PHASE(7);  PROBE_SHAPE_PHASE(7);  advance_player_section();
            PROBE_PHASE(8);  PROBE_SHAPE_PHASE(8);  update_lap_timers();
            PROBE_PHASE(9);  PROBE_SHAPE_PHASE(9);  engine_sound_update();
            PROBE_PHASE(10); PROBE_SHAPE_PHASE(10); clear_surface_buffers();
            PROBE_SHAPE_ROAD_BEFORE();
            PROBE_PHASE(11); PROBE_SHAPE_PHASE(11); draw_road_core(edge_cursor, edge_end_side0);
            PROBE_SHAPE_ROAD_AFTER();
            PROBE_PHASE(12); PROBE_SHAPE_PHASE(12); engine_sound_update();
            PROBE_PHASE(13); PROBE_SHAPE_PHASE(13); fill_line_surface();
            PROBE_PHASE(14); PROBE_SHAPE_PHASE(14); build_road_sign_core();
            /* $172B: the object slot count is the starting slot */
            PROBE_PHASE(15); PROBE_SHAPE_PHASE(15);
            {   /* race_main_loop_core is cpu (NATIVE_FUNCS driver) — marshal the typed exit */
                SlotExit e = draw_track_object_core(0x17, cpu.Y, cpu.V, cpu.C);
                cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
                cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
            }
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
#define EDGE_OPP_X_LO    0x5E50u   /* edge_opp_x_lo — the OPPOSITE boundary's angle at that point */
#define EDGE_OPP_X_HI    0x5EA0u   /* edge_opp_x_hi */
#define EDGE_STYLE_TBL   0x5EE0u   /* edge_style  — which surface style the span there uses */
#define SECTION_FLAGS    0x0702u   /* section_flags — per section byte: its feature bits */
#define SECTION_FLAGS_W  0x068Au   /*   ...the same table at -$78, for a byte index past 120 */
#define EDGE_SIDE_MASK   0x306Cu   /* edge_side_flag_mask  — 2, by road side */
#define EDGE_STYLE_SEL   0x306Eu   /* edge_style_tbl       — 8, by the feature bits */
#define EDGE_WIDTH_SHIFT 0x3076u   /* edge_width_shift_tbl — 8, likewise */
#define TRACK_SEGMENT_LO 0x5900u   /* track_segment_lo — the TRACK FILE's 8-byte segment records */
#define TRACK_SEGMENT_HI 0x5300u   /* track_segment_hi */
#define EDGE_HALF        0x0028u   /* 40 — the stride between the two road sides' halves */
#define SECTION_LO_TBL   0x0900u   /* section_coord_lo — 40 sections x 3 bytes, + two scratch slots */
#define SECTION_HI_TBL   0x0A00u   /* section_coord_hi */
#define SECTION_SIDE1    0x0078u   /* +$78: the OPPOSITE road edge's parallel section list
                                      (section byte cursor 0..$77 for side 0, +$78 for side 1) */
#define SECTION_MID      0x00FAu   /*   ...the triple road_edge_walk interpolates midpoints into */
#define SECTION_NEAR     0x00FDu   /*   ...and the one road_edge_start stages the near point in */
#define WALK_STEP_TBL    0x3DD0u   /* edge_walk_step_tbl — 18 entries, one per emitted point */
#define CAR_SEGMENT_TBL  0x06E8u   /* car_segment */
#define PLAYER_CAR       0x17u     /* slot 23 — the player's own car */

/* `value >= limit`, spelled as the 6502's CMP so that the comparison's own C/N/Z are left
   behind.  ⚠ NOT decoration: every SMC site in these two routines is an EXIT, so a clamp
   test three lines earlier is the last thing that touched the flags on that path, and a
   plain C `>=` reads the same and validates differently. */

/* max(value, floor), via the same CMP.  Used where the floor's own `LDA #imm` flags are
   provably overwritten before anything reads them (draw_road's two clamps). */
static unsigned clamp_up_to(unsigned value, uint8_t floor)
{
    return cmp_ge(value, floor) ? value : floor;
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
   TWINS #16-#24 — THE REST OF THE ROAD-GEOMETRY PASS
   ---------------------------------------------------------------------------
   With these nine, the whole call tree under build_track_geometry is real C: 19 routines,
   776 6502 instructions, no transliteration left anywhere in it.  They divide into three
   groups, and only the last two carry arithmetic worth compressing:

     THE NEAR-SLOT BOOKKEEPING — shift_near_edge_points, clamp_near_edge_window and
     clamp_near_edge_cursor.  Slots 0..5 of each 40-point half are the edge points beside and
     behind the car; these three slide them along when the car crosses a section and keep the
     [near_edge_first, near_edge_last] window and near_edge_cursor consistent afterwards.
     Run at most once a frame.

     THE PER-POINT PRIMITIVES — rebase_edge_point, load_section_triple, emit_edge_bearing and
     emit_edge_bearing_at_cursor.  Small, and twinned because leaving one transliterated leaf
     inside a loop is what made twins #9/#10 driver-shaped in the first place.

     THE TWO THAT COMPUTE — point_distance_hypot and emit_edge_width_offset.  Both are byte
     chains standing in for 16-bit operations the 68000 has: a shift-and-add distance
     approximation, and a variable-count 16-bit shift the 6502 has to spell as a loop.

   ⭐ $2145 and $2285 are deliberately NOT twinned (see transpile.py) — each is one `LDY #0`
   falling into a body that already is one, so their oracles would call the native code and
   the fixtures would be vacuous.  What changed instead is that nothing here calls them: every
   caller in this subtree is a twin now and passes the view origin as an ARGUMENT.
   =========================================================================== */

/* The two coordinate transforms (twins #14/#15), defined further down the file.  Everything
   in this pass reaches them through the cores, never through the 6502-ABI shims. */
void bearing_to_section_core(uint8_t sectionByte, uint8_t origin);

/* project_point's outputs, made explicit so a sibling core takes them as values instead of
   reading the 6502's exit flags back out: `line` is the projected scan line (the 6502 left it
   in A), `clip` is the drop flag (the far clip or the >$80 quotient — C set on the 6502) and
   `behind` is bit 7 of the surviving line (N on the 6502), which the road-edge walk folds into
   a subdivide.  `behind` is meaningful only when !clip; it is 0 on a clipped return.  The
   mantissa/exponent project_point also produces stay in proj_width / proj_width_shift, shared
   state the same way a mem[] cell is. */
ProjPoint project_point_core(uint8_t sectionByte, uint8_t origin);

/* The driving model's two pure-binary scale helpers (defined with damp_and_derive_loads),
   used by stage_accum_delta above them in the file. */
static uint16_t model_scale16(uint16_t value, uint8_t scale);
static uint16_t model_mul_1_5(uint16_t value);

/* draw_road's three producers (twins #26/#28/#29), defined much further down — the road pass
   reaches them through the cores, not the 6502-ABI shims. */
SlotExit mark_line_surfaces_core(uint8_t surfaceClass, uint8_t firstPoint, int entryV);
void draw_surface_spans_core(uint8_t pass, uint8_t firstPoint);
SlotExit fill_line_attr_core(uint8_t bufferLow, uint8_t endCursor, uint8_t firstPoint,
                                    int entryC, int entryV);
SlotExit fill_edge_column_run_core(uint8_t firstColumn, uint8_t stopColumn,
                                          uint8_t firstLine, uint8_t entryV);
static SlotExit plot_object_core(uint8_t slot, uint8_t entryY, uint8_t entryV);   /* SlotExit: top of file */
SlotExit plot_view_src_line_core(uint8_t mode, uint8_t colourSelect);

/* The exit flags of a 16-bit binary add, returned by value so a core stays cpu-free; a shim
   (or a caller whose own exit ABI is this add's) replays them onto the cpu. */

/* update_engine_revs' escaping registers: the tail add's exit A/flags (every arm ends in
   engine_note_only), plus X and Y, which take path-dependent values.  Returned by value so the
   core stays cpu-free; the shim replays them.  EngineRegs is the starter poll's escaping X/Y. */

/* update_camera_and_drive_state's escaping registers: the final car_speed_scaled add's exit
   A/flags, plus X (= player_car) and Y (= car_section_cursor).  Returned by value; shim replays. */

/* apply_driving_model's sub-models (twins #58-#86), all defined further down.  It reaches
   every one through its core so the whole chain is one native call sequence, not shim hops. */
static void compute_car_angles_core(uint8_t headingHi, uint8_t headingLo);
static void rotate_state_pair_core(uint8_t dest, uint8_t source, uint8_t mode);
static void stage_accum_delta_core(void);
void update_grip_limits_core(void);
EngineExit update_engine_revs_core(uint8_t carryIn, uint8_t entryY);
static void update_slip_sound_core(uint8_t axle, uint8_t ambientY);
AddFlags rotate_accum_by_steer_core(void);
AddFlags rotate_pair_a_by_steer_core(void);
static void damp_and_derive_loads_core(void);
static void apply_drag_terms_core(void);
AddFlags integrate_state_rates_core(void);
AddFlags integrate_car_position_core(void);
CameraExit update_camera_and_drive_state_core(void);

/* `value >= limit` through the 6502's CPX, which also leaves X = value.  The near-slot clamps
   below end on one of these, so the compare's own C/N/Z are their exit flags. */

/* ===========================================================================
   $12DC  clamp_near_edge_cursor — WHICH NEAR SLOT DOES THE NEXT FRAME REBUILD?  (twin #18)
   ---------------------------------------------------------------------------
   Handed a candidate slot, which it steps up by one first.  Two independent clamps:

     * the window's TOP.  If candidate+1 is below near_edge_last, the window shrinks to it —
       there is no point promising to rebuild slots that no longer need it.
     * the CURSOR itself.  The candidate must land in [near_edge_first, 5], and both ways out
       of that range snap to 5, not to the boundary that was crossed.

   ⚠ The exit flags are the second clamp's `CPX #6`, and on the path that fires it the `LDX #5`
   AFTER the compare overwrites N and Z while leaving C — which is why both steps go through
   cpx_ge/arg_x rather than a C conditional.
   =========================================================================== */
void clamp_near_edge_cursor_core(uint8_t candidate)
{
    unsigned slot = (candidate + 1u) & 0xFFu;         /* $12DC INX */

    if (!cpx_ge(slot, near_edge_last))                /* $12DD-$12E1 */
        near_edge_last = (uint8_t)slot;

    slot = (slot - 1u) & 0xFFu;                       /* $12E3 DEX */
    if (!cpx_ge(slot, near_edge_first)) {             /* $12E4-$12E8 — below the window */
        slot = 5;
        arg_x(5);
    }
    if (cpx_ge(slot, 6)) {                            /* $12EA-$12EE — past the last near slot */
        slot = 5;
        arg_x(5);
    }
    near_edge_cursor = (uint8_t)slot;                 /* $12F0 STX */
}


/* ===========================================================================
   $12C8  clamp_near_edge_window — RE-OPEN THE WINDOW BY ONE SLOT  (twin #17)
   ---------------------------------------------------------------------------
   The other half of a section step: near_edge_last moves up one, held inside
   [near_edge_first, 6], and then the cursor clamp above runs on near_edge_cursor + 1.  (The
   6502 spells that as `LDX near_edge_cursor / INX` falling into clamp_near_edge_cursor's own
   `INX`, so the cursor really is stepped TWICE before the first compare.)
   =========================================================================== */
void clamp_near_edge_window_core(uint8_t nearSlots)   /* 6 — one past the last near slot */
{
    unsigned last = (near_edge_last + 1u) & 0xFFu;    /* $12C8-$12CA */

    if (cpx_ge(last, nearSlots)) {                    /* $12CB-$12CF */
        last = nearSlots;
        arg_x(nearSlots);
    }
    if (!cpx_ge(last, near_edge_first)) {             /* $12D1-$12D5 */
        last = near_edge_first;
        arg_x(near_edge_first);
    }
    near_edge_last = (uint8_t)last;                   /* $12D7 */

    clamp_near_edge_cursor_core((uint8_t)(near_edge_cursor + 1u));   /* $12D9-$12DB */
}


/* ===========================================================================
   $12A0  shift_near_edge_points — THE CAR CROSSED A SECTION  (twin #16)
   ---------------------------------------------------------------------------
   Slides the near slots of BOTH 40-point halves up by one — entry i becomes entry i+1 for
   i = 4..0 and i = $2C..$28 — which frees slot 0 and slot $28 for the point road_edge_start
   is about to build, and keeps the two halves in step.  All three per-point arrays move
   together: the azimuth's two bytes and the scan line.

   Then the window is re-opened: near_edge_first becomes 6 - near_edge_shift, i.e. as many
   slots as the car has just stepped over, and the clamps above tidy up the rest.

   ⚠ The `SEC / SBC near_edge_shift` is the last thing in the routine to write V — the clamps
   that follow it are all CPX, which does not — but the ONLY caller (road_edge_start $2309) reads
   near_edge_first with an immediate CMP #6 before touching V, so that overflow is a DEAD byproduct
   (dropped from the fixture mask, not reproduced).  A does stay the computed near_edge_first across
   both clamps (they work in X), so it is kept live.  The subtract runs D=0 (the road pass is never
   decimal — static-map.md §Decimal mode), so it is a plain binary `6 - near_edge_shift`.
   =========================================================================== */
uint8_t shift_near_edge_points_core(uint8_t topSlot,    /* $2C — slot 4 of the far half */
                                           uint8_t wrapSlot,   /* $28 — where the far half starts */
                                           uint8_t lowTop,     /* 5  — ...and the near half's top */
                                           uint8_t nearSlots)  /* 6 */
{
    unsigned slot = topSlot;

    for (;;) {                                        /* $12A2-$12BB */
        mem[EDGE_X_LO_TBL + slot + 1] = mem[EDGE_X_LO_TBL + slot];
        mem[EDGE_X_HI_TBL + slot + 1] = mem[EDGE_X_HI_TBL + slot];
        mem[EDGE_Y_TBL    + slot + 1] = mem[EDGE_Y_TBL    + slot];

        if (slot == wrapSlot)                         /* the far half is done — cross over */
            slot = lowTop;
        if (slot == 0)                                /* $12BA DEX / $12BB BPL */
            break;
        slot--;
    }

    near_edge_first = (uint8_t)(nearSlots - near_edge_shift);          /* $12BD-$12C2 SEC/SBC, D=0 */
    clamp_near_edge_window_core(nearSlots);                            /* $12C4 — works in X, leaves A */
    return near_edge_first;   /* A stays near_edge_first across the clamps: the routine's exit A */
}


/* ===========================================================================
   $0BA2  rebase_edge_point — ONE SURVIVING POINT ONTO THIS FRAME'S CAMERA  (twin #19)
   ---------------------------------------------------------------------------
   An edge point is stored relative to the camera — its azimuth is measured from where the car
   points, its scan line from where the camera looks — so a point kept from last frame is
   wrong by exactly one frame of camera motion.  This corrects both halves of that in one go:
   the azimuth by the frame's heading step (the same 16-bit value integrate_heading adds to
   car_heading), the scan line by view_pitch_delta.  The point's style byte is cleared, since
   nothing has claimed the span yet, and the re-based scan line is offered to the frame's
   horizon in passing.

   The routine answers entirely in mem[] (the two edge cells, the scan line and the horizon
   pair); its sole caller — road_edge_start's re-base loop — reads no exit register or flag, so
   the subtracts' flags are dead (fixture: live=S) and they are plain 16-bit binary on the render
   path (docs/static-map.md §Decimal mode).
   =========================================================================== */
void rebase_edge_point_core(uint8_t slot)
{
    mem[EDGE_STYLE_TBL + slot] = 0;                                     /* $0BA2-$0BA4 */

    /* $0BA7-$0BB6 — the point's stored azimuth, less this frame's heading step (16-bit). */
    uint16_t az = (uint16_t)(((unsigned)mem[EDGE_X_LO_TBL + slot] | ((unsigned)mem[EDGE_X_HI_TBL + slot] << 8))
                           -  ((unsigned)heading_step_lo | ((unsigned)heading_step_hi << 8)));
    mem[EDGE_X_LO_TBL + slot] = (uint8_t)az;
    mem[EDGE_X_HI_TBL + slot] = (uint8_t)(az >> 8);

    /* $0BBA-$0BC0 — and its scan line, less the frame's pitch delta. */
    uint8_t line = (uint8_t)(mem[EDGE_Y_TBL + slot] - view_pitch_delta);
    mem[EDGE_Y_TBL + slot] = line;

    if (line >= horizon_extent) {                                      /* $0BC3-$0BC9 CMP (D-blind) */
        horizon_extent = line;
        horizon_index  = slot;
    }
}


/* ===========================================================================
   $1208  load_section_triple — TRACK FILE -> A LIVE SECTION SLOT  (twin #20)
   ---------------------------------------------------------------------------
   A track-file segment is an 8-byte record (which is why the wrap point is segment_count_x8),
   and fields 1..3 of it are the segment's three 16-bit coordinates.  This copies that triple
   into one of the 40+2 live section slots, whose two arrays road_edge_start, road_edge_walk,
   bearing_to_section and project_point all read.

   ⚠ Neither index wraps: the 6502 addresses these as absolute,X and absolute,Y, so a byte
   index of $FE really does reach three bytes past the end of the array rather than back to
   the start.  The two halves are copied in the 6502's order (all three low bytes, then all
   three high), which matters only if source and destination overlap — and the near-point
   scratch slot at $FD is close enough to the end of the live array that they can.
   =========================================================================== */
void load_section_triple_core(uint8_t destSection, uint8_t segmentByte)
{
    /* $1208-$122B — copy the three 16-bit coordinates of the segment (skipping its first byte,
       the length) into the scratch section triple.  The 6502 reads the segment high byte one
       past the triple at $1226, but that only set flags the callers do not read, so it is gone. */
    uint8_t*       dstLo = &mem[SECTION_LO_TBL + destSection];
    uint8_t*       dstHi = &mem[SECTION_HI_TBL + destSection];
    const uint8_t* srcLo = &mem[TRACK_SEGMENT_LO + segmentByte + 1];
    const uint8_t* srcHi = &mem[TRACK_SEGMENT_HI + segmentByte + 1];

    for (int i = 0; i < 3; i++) {
        dstLo[i] = srcLo[i];
        dstHi[i] = srcHi[i];
    }
}


/* ===========================================================================
   $0CA5  point_distance_hypot — HOW FAR AWAY IS THIS POINT?  (twin #22)
   ---------------------------------------------------------------------------
   The distance from the camera to the point bearing_to_section has just transformed, from the
   two ground-plane magnitudes that routine sorted into hypot_min and hypot_max.  It is an
   octagonal approximation to sqrt(min^2 + max^2) — and it is TWO of them, picked on the raw
   arctan byte the bearing left in shared_temp_7e, i.e. on the ANGLE between the components:

     under $67   the components are far apart -> max + min/8      (error under 3% there)
     $67 and up  they are comparable          -> max*7/8 + min/2  (which is the 45-degree case)

   ⭐ WHAT THE TWIN CHANGES.  Every one of those terms is a 16-bit shift the 6502 has to spell
   as `LSR hi / ROR A` pairs — nine of them in the far arm — and the 68000 does each in one
   `lsr.w`.  The whole byte-at-a-time chain and its per-instruction flag bookkeeping go; what
   is left is three shifts, an add and a subtract.

   ⚠ hypot_min is shifted IN PLACE and does not survive the call.  That is not a scratch
   detail to tidy away — it is output, and the differential compares it.  ⚠⚠ And the two arms
   write DIFFERENT AMOUNTS of it: the near arm's >>3 keeps the low byte in A the whole way and
   only ever stores the high one, so hypot_min_lo comes back UNCHANGED there, where the far
   arm's >>1 is a read-modify-write of both.  Storing both on both arms is byte-exact
   arithmetic and a differential failure — 829 of 2000 cases, all on the near arm.
   =========================================================================== */

static PointDist point_distance_hypot_core(uint8_t angle, uint16_t minMag, uint16_t maxMag)
{
    PointDist r;

    r.maxEighth = 0;

    /* The 6502 spells every term as `LSR hi / ROR A` byte pairs and adds them with ADC/SBC
       chains; on the render path D is always 0 (docs/static-map.md §Decimal mode), so each is
       a plain binary 16-bit shift/add/subtract, wrapping mod 2^16 exactly as the byte chain did. */
    if (angle < 0x67) {                               /* $0CA5-$0CA9 — the components diverge */
        r.farArm = 0;
        r.min    = (uint16_t)(minMag >> 3);           /* $0CAB-$0CB5 */
        r.dist   = (uint16_t)(r.min + maxMag);        /* $0CB6-$0CB9 — max + min/8 */
        return r;
    }

    /* $0CC2-$0CF0 — components comparable: max*7/8 + min/2, computed as (min/2 + max) - max/8. */
    r.farArm    = 1;
    r.min       = (uint16_t)(minMag >> 1);            /* $0CC2-$0CC4 */
    r.maxEighth = (uint16_t)(maxMag >> 3);            /* $0CC6-$0CD5 */
    r.dist      = (uint16_t)(r.min + maxMag - r.maxEighth);
    return r;
}

/* The 6502-ABI shim: the two magnitudes and the angle are bearing_to_section's own cells, and
   the distance plus the shifted minimum are what the rest of the pass reads. */
uint8_t point_distance_hypot_apply(void)
{
    GEO_COUNT(g_geoHypot);
    PointDist d = point_distance_hypot_core(
                      shared_temp_7e,
                      (uint16_t)(hypot_min_lo | ((unsigned)hypot_min_hi << 8)),
                      (uint16_t)(hypot_max_lo | ((unsigned)hypot_max_hi << 8)));

    hypot_min_hi = (uint8_t)(d.min >> 8);
    if (d.farArm) {
        hypot_min_lo = (uint8_t)d.min;                /* $0CC4 ROR — the near arm has no store */
        math_lo = (uint8_t)(d.maxEighth >> 8);        /* ⚠ the HIGH byte, in math_lo */
        math_hi = (uint8_t)d.maxEighth;
    }
    point_dist_lo = (uint8_t)d.dist;
    point_dist_hi = (uint8_t)(d.dist >> 8);
    return (uint8_t)(d.dist >> 8);       /* the distance high byte — the callers' live output */
}


/* ===========================================================================
   $23C0  emit_edge_bearing — THE POINT'S ANGLE, RELATIVE TO THE CAR  (twin #21)
   ---------------------------------------------------------------------------
   bearing_to_section leaves an absolute bearing; an edge point stores the angle FROM WHERE
   THE CAR IS POINTING, so this is the one subtraction between them, written into both bytes
   of edge_x at the given slot.  It then falls into point_distance_hypot, which is why every
   caller gets the point's distance as well as its angle out of one call.

   The subtract's own flags are all dead — the hypot's opening `LDA / CMP` overwrites N, Z and
   C and both of its exits overwrite V — so it is a value chain, not a flag chain.
   =========================================================================== */
uint8_t emit_edge_bearing_core(uint8_t slot)
{
    /* $23C0-$23CC — the point's angle FROM WHERE THE CAR POINTS: bearing - car_heading, one
       16-bit subtract (binary on the render path — docs/static-map.md §Decimal mode). */
    uint16_t rel = (uint16_t)(((unsigned)bearing_lo | ((unsigned)bearing_hi << 8))
                            -  ((unsigned)car_heading_lo | ((unsigned)car_heading_hi << 8)));
    mem[EDGE_X_LO_TBL + slot] = (uint8_t)rel;
    mem[EDGE_X_HI_TBL + slot] = (uint8_t)(rel >> 8);

    return point_distance_hypot_apply();     /* $23CF JMP — the point's distance high byte in A */
}


/* ===========================================================================
   $23BB  emit_edge_bearing_at_cursor — ...FOR THE POINT THE WALK IS ON  (twin #23)
   ---------------------------------------------------------------------------
   Five bytes: take the section's bearing from the camera, then emit it at edge_cursor.  It
   exists because road_edge_walk always wants both together, where road_edge_start picks the
   slot itself and calls the two halves separately.
   =========================================================================== */
uint8_t emit_edge_bearing_at_cursor_core(uint8_t sectionByte)
{
    bearing_to_section_core(sectionByte, 0);        /* $23BB -> $2145: origin 0 = the camera */
    return emit_edge_bearing_core(edge_cursor);     /* $23BE — at the cursor point; A = its distance */
}


/* ===========================================================================
   $2565  emit_edge_width_offset — THE OTHER SIDE OF THE ROAD, AND THE MARKERS  (twin #24)
   ---------------------------------------------------------------------------
   The last thing road_edge_walk does with a point it has decided to keep, and it produces
   three separate things out of one lookup:

     1. THE OPPOSITE BOUNDARY.  The point's section byte carries feature bits; masked by the
        bits that belong to this road side, their low three select a width EXPONENT, and
        proj_width (the reciprocal-table mantissa project_point just left) shifted by the
        difference is how wide the road looks HERE.  Added to — or subtracted from, depending
        on which side and which way round the circuit — the point's own azimuth, that is
        edge_opp_x, the angle of the far kerb.
     2. THE STYLE.  edge_style_tbl's entry for the same feature bits, or a flat 2 on an odd
        section byte, becomes the point's edge_style: which surface draw_road paints there.
     3. A CORNER MARKER, when the masked bits include either of the $18 pair and the frame has
        fewer than three already.  Bit 0 halves the marker's offset from its edge point.

   ⭐ WHAT THE TWIN CHANGES.  The width shift is a VARIABLE 16-bit shift, and the 6502 has to
   run it as a loop of `ASL A / ROL math_hi` (or `LSR / ROR`) one place per iteration, up to
   255 times.  The 68000 shifts a word by a register in one instruction, so the loop becomes a
   shift and a range test — the one place in this pass where the twin does asymptotically less
   work than the 6502 rather than the same work with less bookkeeping.

   ⚠ The first THREE points of a side get no width offset at all ($2580 CMP #3): they are the
   ones beside the car, where the perspective divide has nothing useful to say.
   ⚠ SELF-MODIFYING at $261A: all four expansion circuits replace the horizon store pair with
   `JMP $56AF` (`make track-patch`), so the bytes are dispatched rather than assumed.
   =========================================================================== */

/* $2596-$25A7 — proj_width shifted by `steps`, as a 16-bit value.  The 6502's loop shifts one
   place per iteration and the count is a signed byte, so a count of 16 or more shifts every
   bit out; that is a range test here, not 240 iterations. */
static unsigned width_shifted(uint8_t mantissa, uint8_t steps)
{
    if (steps == 0)                       return mantissa;              /* $2597 BEQ */
    if (steps < 0x80u)                                                  /* $2599 BPL — left */
        return steps >= 16 ? 0u : (((unsigned)mantissa << steps) & 0xFFFFu);
    {   /* the right arm counts UP to zero, so 256 - steps places */
        unsigned places = 0x100u - steps;
        return places >= 16 ? 0u : ((unsigned)mantissa >> places);
    }
}

/* $25D9-$25FB — append this point to the frame's corner-marker list, which draw_corner_markers
   consumes and zeroes.  At most three, and a midpoint never gets one (road_edge_walk saves and
   restores marker_count around the call that could). */
static void append_corner_marker(uint8_t flags, unsigned offset)
{
    unsigned slot = marker_count;

    if (slot >= 0x03u)                                /* $25DB CPY #3 / BCS — at most three */
        return;                                       /* (Y and its C are dead: the caller reloads Y) */

    mem[MARKER_EDGE_IDX  + slot] = edge_cursor;
    mem[MARKER_FLAGS_TBL + slot] = flags;

    if (flags & 0x01u) {                              /* $25E9-$25EF — a half-width marker */
        offset  >>= 1;                                /* ...halved in math_lo/hi themselves */
        math_hi   = (uint8_t)(offset >> 8);
        math_lo   = (uint8_t)offset;
    }

    mem[MARKER_OFF_LO + slot] = (uint8_t)offset;
    mem[MARKER_OFF_HI + slot] = (uint8_t)(offset >> 8);
    inc_mem(MEM_marker_count);
}

/* $1B29-$1B6F — one corner marker's projection, the wide-value half of draw_corner_markers.
   The 6502 carries the 16-bit offset and its sum in the math_lo/math_hi byte lanes, threading
   carry with ASL/ROL; here both are ordinary 16-bit expressions.  The offset is doubled onto
   the edge point's azimuth; the summed high byte off the ends of $18..$E8 (the near/far wrap)
   selects DRAW, anything inside it SKIP.  On the draw path plot_x is that azimuth's top byte
   re-centred on $50, and proj_width is |high byte of the offset << 3|. */
void draw_corner_marker_core(uint8_t offLo, uint8_t offHi, uint16_t edgeX,
                             uint8_t edgeY, CornerMarker *out)
{
    uint16_t q = (uint16_t)(((offHi << 8) | offLo) << 1);   /* $1B31 ASL / $1B32 ROL — doubled */
    uint16_t p = (uint16_t)(q + edgeX);                     /* $1B36-$1B3F — 16-bit add */
    uint8_t  phi = (uint8_t)(p >> 8);

    if (phi < 0x18u || phi >= 0xE8u) {                      /* $1B41-$1B47 */
        uint16_t r = (uint16_t)(q << 2);                    /* $1B5B loop — offset << 3 */
        uint8_t  rh = (uint8_t)(r >> 8);
        out->draw      = 1;
        out->plotX     = (uint8_t)((p >> 6) + 0x50u);       /* $1B49-$1B52 — (P<<2) hi byte + $50 */
        out->plotLine  = edgeY;                             /* $1B57 — edge_y[marker_edge_index] */
        out->projWidth = (rh & 0x80u) ? (uint8_t)(0u - rh)  /* $1B62-$1B6B — |hi byte of offset<<3| */
                                      : rh;
        out->mathLo    = (uint8_t)r;
        out->mathHi    = rh;
        out->temp76    = (uint8_t)(p << 2);                 /* $76 after the two ROLs */
    } else {
        out->draw   = 0;
        out->mathLo = (uint8_t)q;                           /* the doubled offset, never re-shifted */
        out->mathHi = (uint8_t)(q >> 8);
        out->temp76 = (uint8_t)p;                           /* $1B3A — P low byte */
    }
}

/* Exit ABI of emit_edge_width_offset.  X passes through the caller's; A = the point's scan line
   (the CMP at each exit sets A to it); Y = edge_cursor; V is the width ADC's overflow when the
   scoring branch ran, else the entry V; N/Z/C are the last CMP's on that exit path. */

WidthExit emit_edge_width_offset_core(uint8_t sectionByte, uint8_t firstScoringPoint,
                                             uint8_t entryV)
{
    unsigned feature, offset = 0, line;
    uint8_t  flags, style;
    uint8_t  vOut = entryV;              /* V survives from entry unless the width ADC rewrites it */

    /* $2565-$257E — the point's feature bits, masked down to this road side's, and the two
       table entries they select.  The section-flags array is addressed twice over: $0702 for a
       byte index inside the 120-byte list and $068A (the same table, less 120) past it. */
    flags = (uint8_t)(mem[(sectionByte >= 0x78) ? SECTION_FLAGS_W + sectionByte
                                                : SECTION_FLAGS   + sectionByte]
                      & mem[EDGE_SIDE_MASK + road_side_index]);
    shared_temp_77 = flags;
    feature        = flags & 0x07u;
    style          = mem[EDGE_STYLE_SEL + feature];
    shared_temp_76 = style;

    /* $2580-$2586 — nothing but the style for the first three points of the side.  (The `CMP`
       here is a mid-routine branch; its flags are overwritten before any exit, so a plain >=.) */
    if (shared_counter_42 >= firstScoringPoint) {
        int negate;

        /* $2589-$25A9 — the apparent half-width here: project_point's mantissa, shifted by its
           exponent less this feature's own.  Binary render path (docs/static-map.md §Decimal
           mode), so the shift count is a plain subtract. */
        uint8_t steps = (uint8_t)(proj_width_shift - mem[EDGE_WIDTH_SHIFT + feature] - 1u);

        offset  = width_shifted(proj_width, steps);
        math_hi = (uint8_t)(offset >> 8);
        math_lo = (uint8_t)offset;

        /* $25AB-$25BE — and which way it points.  Road side 0 or 1 becomes a sign bit, EORed
           with the direction the car is going round the circuit, so the two boundaries stay on
           opposite sides of the road however the walk is traversing the section list. */
        negate = (uint8_t)((road_side_index ? 0x80u : 0x00u) ^ track_direction) >= 0x80u;
        if (negate) {                                 /* $25B3-$25BE — 16-bit two's-complement negate */
            offset  = (unsigned)(uint16_t)(0u - offset);
            math_lo = (uint8_t)offset;
            math_hi = (uint8_t)(offset >> 8);
        }

        /* $25C0-$25D2 — the far kerb's azimuth: this point's angle plus that offset (16-bit add). */
        {
            unsigned slot = edge_cursor;
            unsigned base = (unsigned)mem[EDGE_X_LO_TBL + slot]
                          | ((unsigned)mem[EDGE_X_HI_TBL + slot] << 8);
            unsigned res  = (base + offset) & 0xFFFFu;
            mem[EDGE_OPP_X_LO + slot] = (uint8_t)res;
            mem[EDGE_OPP_X_HI + slot] = (uint8_t)(res >> 8);
            /* ⚠ V escapes: the HIGH half's ADC is the last thing in the routine to write V —
               everything after it is CMP/CPY, which do not — so the caller gets its overflow.
               Replayed from the operands (565 of 2000 cases differ on V alone otherwise). */
            unsigned carryLo = (mem[EDGE_X_LO_TBL + slot] + (offset & 0xFFu)) > 0xFFu;
            vOut = adc_overflow(mem[EDGE_X_HI_TBL + slot], (uint8_t)(offset >> 8), carryLo);
        }

        /* $25D3-$25FB — and a corner marker, if the point carries one. */
        if (flags & 0x18u)
            append_corner_marker(flags, offset);
    }

    /* $25FD-$260B — THE STYLE.  An odd section byte is always style 2; an even one takes the
       feature's own.  (The `TXA / AND #1` is the only use of the section index down here.)
       Y exits = edge_cursor from here on (the $2603 LDY), so horizon_index below uses it. */
    mem[EDGE_STYLE_TBL + edge_cursor] = (sectionByte & 1u) ? 0x02u : style;

    /* $260D-$261D — the point's scan line, and the frame's horizon if it reaches further than
       anything before it.  $50 is the top of the 80-line space: a point at or past it is sky.
       Each exit is a CMP, so A = line and N/Z/C are that CMP's (V untouched — still vOut). */
    line = projected_line;
    mem[EDGE_Y_TBL + edge_cursor] = (uint8_t)line;
    {
        uint8_t d = (uint8_t)(line - 0x50u);          /* CMP #$50 */
        if (line >= 0x50u) {
            WidthExit e = { (uint8_t)line, edge_cursor,
                            (uint8_t)((d >> 7) & 1u), (uint8_t)(line == 0x50u), vOut, 1u };
            return e;
        }
    }
    {
        uint8_t d = (uint8_t)(line - horizon_extent); /* CMP horizon_extent */
        if (line < horizon_extent) {
            WidthExit e = { (uint8_t)line, edge_cursor,
                            (uint8_t)((d >> 7) & 1u), 0u, vOut, 0u };
            return e;
        }
        /* line >= horizon_extent: this point IS the new horizon (or a circuit hook owns it). */
        if (mem[0x261A] == 0x85 && mem[0x261C] == 0x84) {       /* unpatched: Silverstone */
            uint8_t z = (uint8_t)(d == 0u);           /* the CMP's Z, before the store moves it */
            horizon_extent = (uint8_t)line;
            horizon_index  = edge_cursor;
            {   WidthExit e = { (uint8_t)line, edge_cursor,
                                (uint8_t)((d >> 7) & 1u), z, vOut, 1u };
                return e;
            }
        }
        /* ⚠ SMC/hook seam: the JMP transfers to the circuit's own code, which the 6502 reaches
           with A = line and the CMP horizon_extent flags live (C=1 here), so re-establish that
           entry ABI before dispatching.  The hook runs to its own RTS and owns the EXIT state —
           hand cpu back verbatim (documented cpu exception, like cluster 5's per-circuit hooks). */
        cpu.Y = edge_cursor;                          /* $2603 LDY $12, live into the JMP */
        cpu.A = (uint8_t)line;
        cpu.N = (uint8_t)((d >> 7) & 1u);
        cpu.Z = (uint8_t)(d == 0u);
        cpu.C = 1u;
        cpu.V = vOut;
        if (mem[0x261A] == 0x4C) {                              /* a circuit's own JMP */
            uint16_t target = (uint16_t)(mem[0x261B] | (mem[0x261C] << 8));
            if (target >= 0x5300 && target <= 0x5A25) revs_track_hook(target);
            else                                      platform_smc_unhandled(0x261A, target);
        } else {
            platform_smc_unhandled(0x261A, mem[0x261A]);
        }
        { WidthExit e = { cpu.A, cpu.Y, cpu.N, cpu.Z, cpu.V, cpu.C }; return e; }
    }
}


/* ===========================================================================
   $3450  abs8 — |A|  (twin #12)
   ---------------------------------------------------------------------------
   Eight bytes and 21 callers, with one trap in them: the `BPL` at $3450 tests the CALLER's
   N flag, not bit 7 of A.  Real callers have just computed A so the two agree; a randomised
   pre-state decorrelates them, and the 6502 follows N.  $80 negates to itself.
   =========================================================================== */
/* $3452-$3455 EOR #$FF / CLC / ADC #1 — negate `a` as (~a)+1, computing the add's own exit
   flags directly: C set iff the value was 0 (~a+1 carries only from $FF), V set iff it was
   $80 (~a == $7F is the one operand whose +1 signed-overflows), N/Z from the result.  cpu-free:
   the whole result-plus-flags is returned by value, so a native caller that has already decided
   the value is negative calls this directly instead of routing A and its sign N through cpu. */
static AddFlags negate8(uint8_t a)
{
    uint8_t  inverted = (uint8_t)(a ^ 0xFFu);
    unsigned sum      = (unsigned)inverted + 1u;
    uint8_t  result   = (uint8_t)sum;
    AddFlags f;
    f.hi       = result;
    f.carry    = (sum > 0xFFu);
    f.overflow = (inverted == 0x7Fu);
    f.neg      = (result >> 7) & 1u;
    f.zero     = (result == 0);
    return f;
}

/* 6502-ABI shim: the value is in A and its SIGN is the caller's N (the $3450 `BPL` tests N, not
   bit 7 of A — a third of the fixture's cases decorrelate them).  Positive: RTS, A and every
   flag left alone.  Negative: negate, leaving A and the negate's full N/Z/V/C. */
void abs8(void)
{
    if (!cpu.N)
        return;
    AddFlags f = negate8(cpu.A);
    cpu.A = f.hi;
    cpu.C = f.carry;
    cpu.V = f.overflow;
    cpu.N = f.neg;
    cpu.Z = f.zero;
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
RoadSide road_edge_side_apply(uint8_t sideSelect)
{
    RoadSide r = road_edge_side_core(sideSelect, section_cursor, track_direction);
    section_wrap_limit = r.wrapLimit;
    road_side_index    = r.side;
    return r;
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

void road_edge_start_core(uint8_t nearSlotCount,   /* 6 — also the "nothing to do" mark */
                                 uint8_t halfStride,      /* $28 = 40 */
                                 uint8_t scratchSection,  /* $FD */
                                 uint8_t pointLimit,      /* $3C = 60, one past slot 5 + 40 */
                                 uint8_t staleHorizonCap) /* 7 */
{
    /* $22FF-$2309 — fold in a pending scroll of the near edge points before anything reads them. */
    if (near_edge_scroll_pending != 0) {
        shift_near_edge_points_core(0x2C, halfStride, 0x05, nearSlotCount);
        near_edge_scroll_pending = 0;
    }

    /* $230C-$2316 — the two sentinels.  near_edge_first == 6 means nothing survived and
       nothing is owed; near_edge_last == 6 means there is a new point but nothing to re-base. */
    if (near_edge_first == nearSlotCount)
        return;

    if (near_edge_last != nearSlotCount) {
        /* $2318-$232E — RE-BASE the surviving slots, each as a pair 40 apart so the two
           halves of the edge arrays stay in step.  POST-tested (INY / CPY #6 / BCC): the body
           runs once before the count is checked, so a near_edge_last past the count still
           re-bases that one wild slot and then wraps 8-bit up through slot 5. */
        unsigned slot = near_edge_last;
        do {
            int trapped = 0;
            int skip = rebase_takes_branch(slot == near_edge_cursor, &trapped);  /* $2318 SMC BEQ */
            if (trapped)
                return;

            if (!skip) {
                math_lo = (uint8_t)slot;                        /* $231C STY $74 — observable */
                rebase_edge_point_core((uint8_t)(slot + halfStride));
                rebase_edge_point_core((uint8_t)slot);
            }
            slot = (slot + 1) & 0xFFu;                          /* $232B-$232E INY / CPY #6 / BCC */
        } while (slot < nearSlotCount);
    }

    /* $2330-$235C — WHICH TRACK-FILE SEGMENT does the new near point come from?  As many
       segments back from the player's own as there are near slots left to fill, eight bytes
       to a segment, wrapped on the circuit's length in whichever direction the car is going.
       (The road pass runs with D=0, so these are ordinary 8-bit wrapping adds.) */
    unsigned span      = (((nearSlotCount - near_edge_cursor) & 0xFFu) << 3) & 0xFFu;
    unsigned playerSeg = mem[CAR_SEGMENT_TBL + PLAYER_CAR];
    unsigned segIndex;

    if (track_direction & 0x80u) {                   /* $2338-$234C, running the other way */
        math_lo = (uint8_t)span;                     /* $233C, and the store is observable */
        unsigned base = (playerSeg + 0x08u) & 0xFFu;
        segIndex = (base - span) & 0xFFu;
        if (base < span)                             /* wrapped back past segment zero */
            segIndex = (segIndex + segment_count_x8) & 0xFFu;
    } else {                                         /* $234F-$2358 */
        segIndex = (span + playerSeg) & 0xFFu;
        if (segIndex >= (unsigned)segment_count_x8)
            segIndex = (segIndex - segment_count_x8) & 0xFFu;
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

        load_section_triple_core(scratchSection, (uint8_t)segByte);  /* track file -> scratch */
        bearing_to_section_core(scratchSection, 0);  /* ...and its bearing from the camera */

        /* $236C-$2376 — going the other way round, the point belongs to the OTHER half. */
        unsigned edgeSlot = (track_direction & 0x80u) ? (point ^ halfStride) : point;
        emit_edge_bearing_core((uint8_t)edgeSlot);   /* edge_x[Y] = bearing - car_heading */

        if (point < halfStride) {                    /* $2379 CPX #$28 */
            project_point_core(scratchSection, 0);    /* origin 0 = the camera */
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

        unsigned next = (point + halfStride) & 0xFFu;     /* $239B */
        if (next >= pointLimit)                      /* $239E CMP #$3C */
            break;
        point   = next;
        segByte = (near_segment_index + 0x03u) & 0xFFu;   /* $23A3 — the segment's 2nd triple */
    }

    /* $23AC-$23B8 — hand the next frame the slot below this one, then make sure a stale
       horizon_index_prev cannot leave the road reaching further up than line 7. */
    clamp_near_edge_cursor_core((uint8_t)((near_edge_cursor - 1) & 0xFFu));

    if (staleHorizonCap < horizon_index_prev)        /* $23B4 CMP; A = 7 caps the horizon */
        horizon_extent = staleHorizonCap;
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
/* Returns the section byte the 6502 leaves in X at each exit — road_edge_walk's exit-ABI
   register, which build_track_geometry ($24F6) reads back as its own exit X (live=AXY).
   Early return = the caller's section ($2407 RTS, X untouched); the midpoint-clip exit = the
   midpoint slot ($2450 LDX #$FA); the normal exit = walk_prev_section ($245A LDX). */
static uint8_t road_edge_walk_subdivide(unsigned section, uint8_t midSlot)
{
    GEO_COUNT(g_geoSubdiv);
    if (load_a(shared_counter_42) == 0)              /* $2403-$2407 */
        return (uint8_t)section;

    unsigned prev = walk_prev_section;
    int i;

    math_hi        = 0;                              /* $2408 — the midpoint slot counter */
    shared_temp_77 = (uint8_t)section;               /* $240E — the section index, parked */

    for (i = 0; i < 3; i++) {                        /* the triple's three components */
        unsigned here    = (section + i) & 0xFFu;
        unsigned there   = (prev + i) & 0xFFu;
        unsigned base    = section_word(there);

        /* $2410-$241C — the 16-bit gap as one signed subtract (D=0 on the road pass —
           static-map.md §Decimal mode).  Its high byte's sign is what the shifts need. */
        uint16_t here16  = (uint16_t)(((uint16_t)mem[SECTION_HI_TBL + here]  << 8)
                                      | mem[SECTION_LO_TBL + here]);
        uint16_t there16 = (uint16_t)(((uint16_t)mem[SECTION_HI_TBL + there] << 8)
                                      | mem[SECTION_LO_TBL + there]);
        unsigned delta   = (uint16_t)(here16 - there16);
        math_lo = (uint8_t)delta;                    /* $2410 — the low byte, parked */

        /* $241F-$242A — a quarter of the signed gap: an arithmetic shift right by two.  The
           6502 stashes the high byte's sign on the stack across the first RORs, but that PHP
           byte is dead the instant its own PLP pops it (nothing reads the restored flags), so
           the push/pop computes nothing — its only trace is the $01FF residue the fixture
           ignores as dead stack (same as the bearing leaves). */
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
    emit_edge_bearing_at_cursor_core(midSlot);
    if (project_point_core(midSlot, 0).clip)
        return midSlot;                              /* $2450 LDX #$FA left the midpoint slot in X */

    marker_count_saved = marker_count;               /* $245C — no corner marker for a midpoint */
    emit_edge_width_offset_core(walk_prev_section, 0x03, 0u);   /* mem-only here; exit V is dead */
    marker_count       = (uint8_t)load_a(marker_count_saved);
    inc_mem(MEM_edge_cursor);                        /* $2467, and its N/Z are the exit flags */
    return (uint8_t)walk_prev_section;               /* $245A LDX $0014 */
}

/* Returns the section byte the 6502 leaves in X ($24F6 reads it back as build_track_geometry's
   exit X; live=AXY).  The cap/off-axis/hook exits leave `section` in X ($24B4 TAX and the
   $2475-$248F arm carry it); the two subdivide exits inherit subdivide's exit X. */
uint8_t road_edge_walk_core(uint8_t firstPoint, uint8_t sectionIndex,
                                uint8_t midSlot,      /* $FA */
                                uint8_t pointCap,     /* $12 = 18 points */
                                uint8_t offAxis)      /* $14 */
{
    unsigned section = sectionIndex;

    edge_cursor       = firstPoint;                  /* $23D2 */
    shared_counter_42 = 0;                           /* $23D6 — points emitted so far */

    for (;;) {
        GEO_POINT();   /* one edge point visited on this side */
        /* $23D8 — this point's angle, and how far away it is.  A comes back as the high byte
           of the distance point_distance_hypot ($0CA5) left in point_dist_lo/hi. */
        unsigned distHi = emit_edge_bearing_at_cursor_core((uint8_t)section);

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

        /* $23FC-$2401 — project it.  clip = past the far clip, behind = below the camera. */
        {
            ProjPoint p = project_point_core((uint8_t)section, 0);
            if (p.clip || p.behind)
                return road_edge_walk_subdivide(section, midSlot);
        }

        /* $246A — EMIT: the point's second angle, and any corner marker it carries. */
        emit_edge_width_offset_core((uint8_t)section, 0x03, 0u);   /* mem-only here; exit V dead */

        /* $246D-$248F — past the subdivision floor, has the road swung more than $14 off the
           view axis in this one step?  If so, subdivide — unless the point BEFORE it was
           already out there, in which case the side is done. */
        if (shared_counter_42 > edge_nearest_section) {              /* $2471 BEQ/$2473 BCC */
            unsigned here = edge_cursor;                             /* $2475 LDY $12 */
            if (angle_off_axis(EDGE_X_HI_TBL + here, offAxis)) {
                int prevFar = angle_off_axis((EDGE_X_HI_TBL - 1) + here, offAxis);

                /* ⚠ SMC $248B-$248F — see the header.  The unpatched arm just exits in mem[];
                   a circuit's own JMP is real 6502 code that READS the registers, so before
                   dispatching to it re-establish the entry ABI: A + N/Z/C are angle_off_axis's
                   CMP #$14 result (left in cpu by the helper), Y = edge_cursor (the $2475 LDY),
                   X = section.  After the hook runs it owns the exit. */
                if (mem[0x248B] == 0xB0 && mem[0x248D] == 0x4C) {    /* unpatched: Silverstone */
                    if (!prevFar)
                        return road_edge_walk_subdivide(section, midSlot);
                    return (uint8_t)section;                         /* $248B BCS $24B8, X=section */
                }
                cpu.Y = (uint8_t)here;
                cpu.X = (uint8_t)section;
                if (mem[0x248B] == 0x4C) {                           /* a circuit's own JMP */
                    uint16_t target = (uint16_t)(mem[0x248C] | (mem[0x248D] << 8));
                    if (target >= 0x5300 && target <= 0x5A25) revs_track_hook(target);
                    else                                      platform_smc_unhandled(0x248B, target);
                    return cpu.X;                                    /* the hook owns the exit X */
                }
                platform_smc_unhandled(0x248B, mem[0x248B]);
                return cpu.X;
            }
        }

        /* $2490-$24B5 — keep the point, then step the section index.  18 points is the cap;
           section_wrap_limit is what sends the two sides opposite ways round the list. */
        walk_prev_section = (uint8_t)section;
        edge_cursor++;
        shared_counter_42++;

        unsigned emitted = shared_counter_42;
        if (emitted >= pointCap)                     /* $2498 CPY #$12 — 18 points is the cap */
            return (uint8_t)section;                 /* $24B4 TAX left the section byte in X */

        unsigned step = mem[WALK_STEP_TBL + emitted];
        math_lo = (uint8_t)step;                     /* $249F — observable */

        unsigned from = section;
        if (((section - section_wrap_limit) & 0xFFu) < (uint8_t)step)
            from = (section + 0x78) & 0xFFu;         /* $24A9 — round the 120-byte list */
        section = (from - step) & 0xFFu;
    }
}


/* $2505-$250C and $2513-$251A — ONE ROAD SIDE.  road_edge_side picks which side and which
   traversal direction the walk uses (A=0 and A=$80 are opposites whatever the car's
   direction bit holds); the walk then emits that side's points from `firstPoint` upward. */
static uint8_t road_side_walk(uint8_t sideSelect, uint8_t firstPoint)
{
    GEO_SIDE_SET(sideSelect ? 1 : 0);
    RoadSide side = road_edge_side_apply(sideSelect);
    return road_edge_walk_core(firstPoint, side.sectionIndex, (uint8_t)SECTION_MID, 0x12, 0x14);
}

/* $253B-$2549 — HOW WIDE IS THE ROAD AT THE HORIZON?  The two sides' x at the horizon point,
   differenced and halved: half the apparent road width, which $1FE4 reads to decide how much
   of the distance is worth drawing.  Entered with the horizon point in Y and returning with
   the halved value in A, because both are part of the routine's exit contract.

   ⚠ SMC $2542-$2545: an expansion circuit replaces the `JSR abs8 / LSR A` pair with a call of
   its own plus a NOP, so on those circuits the width is NOT halved. */
static void horizon_half_width_at(unsigned horizonPoint)
{
    /* $253B — the two sides' x at the horizon point, differenced.  D=0 on the geometry path
       (static-map §Decimal mode), so this is a plain 8-bit subtract, and its own sign (bit 7)
       is what abs8 negated on.  The subtract's C/V/Z are dead (abs8 overwrote them when it
       negated, and on the keep path they reach build_track_geometry's exit UNREAD); cpu.A is
       not read after this routine either — horizon_half_width_at is the last call in the core. */
    uint8_t diff = (uint8_t)(mem[EDGE_X_HI_TBL + horizonPoint] -
                             mem[EDGE_X_HI_TBL + EDGE_HALF + horizonPoint]);
    uint8_t mag  = (diff & 0x80u) ? negate8(diff).hi : diff;    /* |diff| — cpu-free abs8 */

    /* ⚠ Exit cpu.A IS part of build_track_geometry's ABI (its fixture compares A): it is the
       half-width on both compute arms, diff on the trap fall-through, the hook's own A on the
       circuit arm.  The abs8 cpu round-trip is gone; only that one exit-register write remains. */
    if (mem[0x2542] == 0x20 && mem[0x2545] == 0x4A) {           /* unpatched: Silverstone */
        horizon_half_width = (uint8_t)(mag >> 1);               /* $2549 LSR A — half the width */
        cpu.A = horizon_half_width;
    } else if (mem[0x2542] == 0x20 && mem[0x2545] == 0xEA) {    /* a circuit's own call + NOP */
        uint16_t target = (uint16_t)(mem[0x2543] | (mem[0x2544] << 8));
        if (target == 0x3450) {
            horizon_half_width = mag;               /* the abs8 hook, then NOP (no halving) */
            cpu.A = horizon_half_width;
        } else if (target >= 0x5300 && target <= 0x5A25) {
            /* Circuit-hook seam: the hook READS A and its sign N, so re-establish the 6502
               entry ABI before dispatching, then hand its own exit A back verbatim. */
            cpu.A = diff;
            cpu.N = (diff >> 7) & 1u;
            revs_track_hook(target);
            horizon_half_width = cpu.A;
        } else { cpu.A = diff; platform_smc_unhandled(0x2542, target); return; }
    } else {
        cpu.A = diff;
        platform_smc_unhandled(0x2542, mem[0x2542]);
    }
}

/* `firstPoint` per side: the cursor each walk starts from.  They are 6 and $2E = 6 + 40 — the
   same offset into each half of the 2x40 edge arrays, which is what makes the two lists
   parallel and lets everything downstream address a side by adding 40. */
void build_track_geometry_core(uint8_t firstPointSide0, uint8_t firstPointSide1)
{
    GEO_COUNT(g_geoFrames);
    horizon_extent = 0;              /* $24F6: the road reaches nowhere until a walk says so */
    /* the nearest point of each side, and last frame's clamp */
    GEO_PHASE(GEO_PHASE_START);
    road_edge_start_core(0x06, (uint8_t)EDGE_HALF, (uint8_t)SECTION_NEAR, 0x3C, 0x07);

    edge_nearest_hi = 0xFF;          /* no nearest point yet: the first one always wins */
    edge_nearest_section = 0x0D;     /* ...and do not subdivide before section 13 */

    GEO_PHASE(GEO_PHASE_WALK0);
    road_side_walk(0x00, firstPointSide0);
    edge_end_side0 = edge_cursor;    /* where side 0 stopped, for draw_road to pair up */
    GEO_PHASE(GEO_PHASE_WALK1);
    /* road_edge_walk leaves the last section byte in X ($24B4 TAX / the off-axis arm), and it
       sits in the register untouched through the horizon-fold tail below to become this
       routine's own exit X ($24F6, live=AXY).  Model that by parking it now. */
    cpu.X = road_side_walk(0x80, firstPointSide1);
    GEO_PHASE(GEO_PHASE_TAIL);

    /* $251D-$2529 — WHICH POINT IS THE HORIZON?  The walks record it as an index into
       whichever half they were writing, so fold it back into 0..39 and keep it for next
       frame's road_edge_start, which clamps the horizon down when it climbed too far. */
    unsigned horizonPoint = horizon_index;
    if (cmp_ge(horizonPoint, 0x28)) {
        /* $251D: guarded by cmp_ge above, so the subtract never borrows — a plain 8-bit
           fold back into 0..39 (D=0 on the geometry path).  Its flags are dead: overwritten
           by the cmp_ge at $252B and by horizon_half_width_at below. */
        horizonPoint = (unsigned)(uint8_t)(horizonPoint - 0x28);
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
    GEO_PHASE(5);   /* reopen the enclosing phase: its remainder is the driver + the return */
}

/* The 6502-ABI shim.  Both walk cursors are constants in the 6502; they are arguments here
   because they are the one thing that decides which half of the edge arrays each side owns. */

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
static SlotExit mark_side_surfaces(uint8_t surfaceClass, int entryV)
{
    return mark_line_surfaces_core(surfaceClass, road_split_index, entryV);
}

/* $19AF x4 — one span pass.  `firstPoint` is where in the edge list the pass starts; the
   pass number selects both the paired-index offset and which surface_edge buffer the spans
   land in. */
static void surface_pass(uint8_t pass, uint8_t firstPoint)
{
    draw_surface_spans_core(pass, firstPoint);
}

/* The two side cursors are arguments; horizon_index and road_split_index deliberately are NOT.
   The cursors are written only by build_track_geometry and its walk, so they cannot change under
   this routine — but road_split_index is written by two of the callees below and horizon_index is
   read four separate times by the 6502, so both are read from mem[] at every use. */
void draw_road_core(uint8_t endCursorFar, uint8_t endCursorNear)
{
    ROAD_COUNT(g_roadFrames);
    plot_ptr_lo = 0x80;              /* $1A20: every span plotter stores through ($70),Y */

    /* $1A24-$1A30 — the FAR half of the road.  The split is the horizon point in the 40..79
       half, but never nearer than point $31: the four passes below all measure "near" and
       "far" against it, and letting it come closer than that inverts them. */
    /* ⚠ STAYS adc_step: on the fixture's SMC-early-return path fill_line_attr never reaches
       edge_x_offscreen and the mark walk is skipped, so this add's V is draw_road's exit V and
       the differential compares it (28/200 when it was a plain `+`). */
    unsigned farBase = adc_step(horizon_index, 0x28, 0);
    road_split_index = (uint8_t)clamp_up_to(farBase, 0x31);

    plot_ptr2_lo = 0;                /* the second screen pointer, for a span that crosses a page */
    plot_ptr3_lo   = 0;              /* the third screen pointer's low byte — road_span_plot_2 stores through it */

    /* Side 1 (the 40..79 half): its line map, then its two span passes.  $00 is the low byte
       of line_attr_0 (it patches the store), endCursorFar the stop cursor, farBase the start. */
    ROAD_PHASE(ROAD_PHASE_FILL);
    /* draw_road discards fill_line_attr's returned A/X/Y — it consumes road_split_index (a mem
       cell) via surface_pass/mark below.  But on the SMC-early-return path fill_line_attr leaves
       C and V UNTOUCHED (only the $1943 DEY runs, touching N/Z/Y), so its exit V/C are the ones
       it was called with — here farBase's ADC flags, which are draw_road's own exit V/C when the
       walk is skipped (see the farBase comment above).  Pass them live so the trap path echoes
       them back faithfully. */
    (void)fill_line_attr_core(0x00, endCursorFar, (uint8_t)farBase, cpu.C, cpu.V);

    ROAD_PHASE(ROAD_PHASE_SPANS);
    surface_style_base = 0x00;
    surface_pass(0, road_split_index);

    surface_style_base = 0x08;
    shared_temp_8c  = 0x00;
    /* ⚠ Pass 1 does not go through surface_pass: its base is RECOMPUTED rather than farBase
       reused, because that is what the 6502 does (draw_surface_spans recomputes its own start
       flags from firstPoint, so only the value matters). */
    draw_surface_spans_core(1, (uint8_t)(horizon_index + 0x28));   /* base only; the spans walk
                                                                      overwrites this add's flags */

    /* ⚠ SEAM (like the two fill_line_attr calls above): mark_line_surfaces_core is cpu-free, so
       draw_road threads the 6502 flag chain by hand.  The far mark's entry V is the live cpu.V
       here (the near fill below reads cpu.C/cpu.V, exactly as the 6502 left them after this
       walk), and its full exit state is marshalled back into cpu so that read sees HEAD's bytes. */
    ROAD_PHASE(ROAD_PHASE_MARK);
    {
        SlotExit m = mark_side_surfaces(0x04, cpu.V);
        line_attr_0_limit = m.y;
        cpu.A = m.a; cpu.X = m.x; cpu.Y = m.y;
        cpu.N = m.n; cpu.Z = m.z; cpu.V = m.v; cpu.C = m.c;
    }

    /* $1A60-$1A69 — and the NEAR half, whose split is the horizon point itself, floored at
       point 9 for the same reason.  (The 6502's `TAX` here is overwritten two instructions
       later by `LDX $51` with nothing reading X in between, so it is not reproduced.) */
    ROAD_PHASE(11);                  /* the near-half clamp: enclosing-phase remainder */
    unsigned nearBase = horizon_index;
    road_split_index = (uint8_t)clamp_up_to(nearBase, 0x09);

    /* ...and $50 is the low byte of line_attr_1, endCursorNear the stop, nearBase the start.
       C/V passed live for the same trap-path reason as the far half above. */
    ROAD_PHASE(ROAD_PHASE_FILL);
    (void)fill_line_attr_core(0x50, endCursorNear, (uint8_t)nearBase, cpu.C, cpu.V);

    ROAD_PHASE(ROAD_PHASE_SPANS);
    shared_temp_8c  = 0x1C;
    surface_style_base = 0x10;
    surface_pass(2, horizon_index);

    surface_style_base = 0x1C;
    surface_pass(3, road_split_index);

    /* The near mark's exit IS draw_road's exit (nothing after it touches A/X/Y/flags), so its
       SlotExit is marshalled into cpu here and the shim leaves cpu alone.  Its entry V is the
       live cpu.V — the near fill's exit V threaded through the V-transparent span passes. */
    ROAD_PHASE(ROAD_PHASE_MARK);
    {
        SlotExit m = mark_side_surfaces(0x14, cpu.V);
        line_attr_1_limit = m.y;
        cpu.A = m.a; cpu.X = m.x; cpu.Y = m.y;
        cpu.N = m.n; cpu.Z = m.z; cpu.V = m.v; cpu.C = m.c;
    }
    ROAD_PHASE(11);                  /* reopen the enclosing phase: its remainder is the return */
}

/* The 6502-ABI shim.  draw_road takes no arguments — the frame's geometry reaches it entirely
   through the edge lists and the three cursor cells — and leaves A, X and the flags wherever
   its last callee left them, which the core sets by marshalling the near mark's SlotExit into
   cpu at that call site (see above); the shim itself adds nothing. */

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


/* ⚠ Every model cell below is read from mem[] at the point of use and never cached in a local:
   any of the fifteen sub-models can write any of them, and stage_accum_delta in particular is *supposed* to
   change model_accum under the four calls that follow it. */
void apply_driving_model_core(uint8_t posLo, uint8_t posHi)
{
    /* $46A1 — the car's body angles, computed from where the car actually is. */
    compute_car_angles_core(posHi, posLo);
    rotate_state_pair_core(8u, 0u, 0xC0u);   /* rotate_state_0_into_8 */

    /* $46AE — the accumulator's entry value, for the restore at $46DF. */
    model_accum_entry_lo = model_accum_lo;
    model_accum_entry_hi = model_accum_hi;

    /* $46B8-$46CD — the speed split.  |car_speed| as a 16-bit sign-magnitude value: abs16_math
       negates the (hi:lo) pair in place when hi is negative (D=0), and is a no-op otherwise.
       road_speed = |hi|, road_speed_frac = |lo|.  Every register and flag this block leaves is
       dead: stage_accum_delta opens with `LDA` and `LDY #$58`. */
    math_lo = car_speed_lo;
    if (car_speed_hi & 0x80u) {                         /* negative: neg16_math */
        uint16_t v = (uint16_t)(0u - (uint16_t)(((uint16_t)car_speed_hi << 8) | car_speed_lo));
        math_hi         = car_speed_hi;                 /* neg16_math writes math_hi = the original hi.
                                                           Faithful but dead scratch: stage_accum_delta
                                                           overwrites math_hi before any read, so a sabotage
                                                           of THIS byte alone is unobservable (kept per the
                                                           scratch-write rule; road_speed/math_lo below ARE live). */
        math_lo         = (uint8_t)v;                   /* the negated low byte */
        road_speed      = (uint8_t)(v >> 8);            /* the negated high byte */
    } else {                                            /* non-negative: abs16_math is a no-op */
        road_speed      = car_speed_hi;                 /* math_lo/math_hi untouched */
    }
    road_speed_frac = math_lo;
    wheel_spin_rate = road_speed ? road_speed : (uint8_t)(math_lo & 0xF0);

    /* $46CF-$46DA — the four sub-models that run against the OFFSET accumulator.  stage_accum_delta is
       what offsets it, and what leaves model_accum_delta_lo/hi behind. */
    stage_accum_delta_core();
    update_grip_limits_core();
    /* update_engine_revs consumes the caller's CARRY (the coast arm's ADC #7, $49A6) and preserves
       its ENTRY Y on the off-power arms — that Y is the ANDed surface bytes update_grip_limits left
       at $4C46.  Its EXIT Y is live: it stays the ambient Y that update_slip_sound's OSBYTE 21
       (sound_stop_channel, $0E6B) passes to the MOS, so replay the engine's exit registers here as
       the shim does, before the sound call reads them. */
    {
        EngineExit ee = update_engine_revs_core(cpu.C,
                            (uint8_t)(surface_change_0 & surface_change_1));
        cpu.A = ee.tail.hi; cpu.C = ee.tail.carry; cpu.V = ee.tail.overflow;
        cpu.N = ee.tail.neg; cpu.Z = ee.tail.zero;
        cpu.X = ee.x; cpu.Y = ee.y;
    }
    update_slip_sound_core(0x01, cpu.Y);   /* the engine's exit Y is the ambient Y its OSBYTE 21 logs */

    /* $46DF-$46F5 — restore the entry accumulator, then apply the frame's real increment as one
       16-bit add (D=0 on the driving path — static-map.md §Decimal mode).  The add's exit flags
       are dead: rotate_accum_by_steer_core opens with LDA. */
    {
        uint16_t accum = (uint16_t)(((uint16_t)model_accum_entry_hi << 8) | model_accum_entry_lo)
                       + (uint16_t)(((uint16_t)model_accum_delta_hi  << 8) | model_accum_delta_lo);
        model_accum_lo = (uint8_t)accum;
        model_accum_hi = (uint8_t)(accum >> 8);
    }

    /* $46F8-$4703 — and the sub-models that want the accumulator at its new value.  Each of the
       two rotations ends in model_integrate_element, on element 8 and on element $0A. */
    rotate_accum_by_steer_core();
    /* MOS-seam replay: rotate_accum_by_steer ends in model_integrate_element on element 8, leaving
       Y = 8 (its last apply_angle_term source index).  update_slip_sound's silence arm reaches
       OSBYTE 21 (sound_stop_channel) with Y still holding it — a dead input the MOS ignores, but
       the real 6502 passes it, so it is reconstructed here now the core no longer leaves it in cpu. */
    cpu.Y = 8u;
    update_slip_sound_core(0x00, cpu.Y);
    rotate_pair_a_by_steer_core();
    damp_and_derive_loads_core();

    /* $4706-$4717 — off power: elements 5..7 are zeroed rather than integrated.  The loop's
       exit registers (X = $FF, A = 0, N set) are dead — apply_drag_terms opens with `LDA`. */
    if (drive_state >= 0x02u) {                         /* $4706 CMP #2 (unsigned; flags dead) */
        int element;
        for (element = 7; element >= 5; element--) {
            mem[MODEL_STATE_LO + element] = 0;
            mem[MODEL_STATE_HI + element] = 0;
        }
    }

    /* $4719-$4725 — the tail.  integrate_car_position is what advances car_heading_lo/hi, so
       the car has not actually moved until the second-to-last call of the chain. */
    apply_drag_terms_core();
    rotate_state_pair_core(3u, 6u, 0x40u);   /* rotate_state_6_into_3 */
    integrate_state_rates_core();
    integrate_car_position_core();
    /* apply_driving_model's exit A/X/Y/flags ARE update_camera_and_drive_state's (its last call);
       replay them into cpu so the shim returns them untouched. */
    {
        CameraExit ce = update_camera_and_drive_state_core();
        cpu.A = ce.acc.hi; cpu.C = ce.acc.carry; cpu.V = ce.acc.overflow;
        cpu.N = ce.acc.neg; cpu.Z = ce.acc.zero;
        cpu.X = ce.x; cpu.Y = ce.y;
    }
}

/* The 6502-ABI shim.  The player's own position is the routine's one input — it reaches the
   6502 in A and X — and A, X, Y and the flags come back from update_camera_and_drive_state untouched. */

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

/* Exit ABI is the full register+flag set (SlotExit).  A, V and C differ per path; X, N and Z
   are the closing `arg_x` (LDX saved_slot_index).  Entry Y passes through every path (nothing
   writes it before arg_x), and entry V/C pass through the empty-slot path — so all three are
   inputs. */
SlotExit draw_track_object_core(uint8_t slot, uint8_t entryY, uint8_t entryV, uint8_t entryC)
{
    uint8_t flags = mem[CAR_FLAGS_SHAPE + slot];
    uint8_t a, v = entryV, c = entryC;          /* y passes through untouched (= entryY) */

    if (flags & 0x80) {
        /* $2AD4 — an empty slot.  Nothing is drawn, but A is still the flag byte at the
           tail, so the 6502's `LDA` is reproduced even though its N/Z are overwritten. */
        a = flags;
    } else {
        plot_shape = (uint8_t)(flags & 0x0F);

        /* How far ahead of the player the object sits, as one 16-bit subtract.  The low
           byte's only reader is the x4 below, but it goes through math_lo because the
           plotter's setup shares that cell.  The high SBC's N/Z/C are dead (the visibility
           CMP recomputes them), but its V ESCAPES: CMP leaves V alone, so on the not-visible
           path the subtract's V is the routine's exit V.  Replay it from the high byte. */
        uint16_t bearing = (uint16_t)(((uint16_t)mem[OBJECT_BEARING_HI + slot] << 8) |
                                      mem[OBJECT_BEARING_LO + slot]);
        uint16_t heading = (uint16_t)(((uint16_t)car_heading_hi << 8) | car_heading_lo);
        uint16_t delta   = (uint16_t)(bearing - heading);
        math_lo = (uint8_t)delta;                       /* the plotter's setup shares this cell */
        uint8_t  deltaHi = (uint8_t)(delta >> 8);
        { uint8_t hiM = (uint8_t)(bearing >> 8), hiS = (uint8_t)(heading >> 8);
          v = (uint8_t)((((hiM ^ hiS) & (hiM ^ deltaHi)) >> 7) & 1u); }

        /* $2AE7-$2AF1 — the visibility window, and the two arms are not symmetric because
           the 6502 tests the sign first: behind the player it wants >= $E0, ahead of it
           < $20.  On the reject path A is the high byte and C is that CMP's own carry. */
        uint8_t limit = (deltaHi & 0x80u) ? 0xE0u : 0x20u;
        a = deltaHi;                                    /* CMP leaves A = the high byte */
        c = (uint8_t)(deltaHi >= limit);                /* ...and C = its own carry */
        int visible = (deltaHi & 0x80u) ? (deltaHi >= 0xE0u) : (deltaHi < 0x20u);
        if (visible) {
            unsigned row = shift_pair_left(shift_pair_left(deltaHi));
            plot_x = (uint8_t)(row + 0x50);             /* carry-in 0 */
            /* N/Z/C are dead (the LDAs below rewrite N/Z, nothing reads C), but V ESCAPES on
               the drawn path: this ADC's V is the routine's exit V unless plot_object writes
               it.  Feed it in as plot_object's entry V and take plot_object's exit back. */
            v = adc_overflow((uint8_t)row, 0x50, 0);
            /* The column's own `LDA` flags are dead — the width's LDA one instruction later
               rewrites N and Z, and nothing between them branches. */
            plot_line   = mem[OBJECT_LINE + slot];
            proj_width  = mem[OBJECT_WIDTH + slot];
            SlotExit po = plot_object_core(slot, entryY, v);
            a = po.a; entryY = po.y; v = po.v; c = po.c;   /* X/N/Z are the arg_x below */
        }
    }

    /* $2B0A — arg_x(saved_slot_index): LDX sets X and its N/Z. */
    uint8_t ssi = saved_slot_index;
    SlotExit e = { a, ssi, entryY, (uint8_t)((ssi >> 7) & 1u), (uint8_t)(ssi == 0u), v, c };
    return e;
}

/* The 6502-ABI shim.  The slot arrives in X; everything else the routine needs is in mem[]. */

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


/* One end of the viewport.  `stopColumn` is exclusive of the plot_ptr2 half of the run and
   inclusive of the plot_ptr half — fill_edge_column_run walks two columns per iteration and
   tests the second one against it. */
static SlotExit edge_column_pass(uint16_t startSrc, uint8_t firstColumn, uint8_t stopColumn,
                                 uint8_t firstLine)
{
    plot_ptr2_hi = (uint8_t)(startSrc >> 8);
    plot_ptr2_lo = (uint8_t)startSrc;
    /* entry V is dead on this path (fill_edge_column_run's own note): pass 0. */
    return fill_edge_column_run_core(firstColumn, stopColumn, firstLine, 0u);
}

/* The two boundary tables are the arguments because they are the one thing a change of view
   representation moves (docs/direct-bitplane-plan.md §7a); the column and line numbers are
   the viewport's own geometry and stay immediates. */
SlotExit fill_dash_edge_columns_core(uint16_t leftStartSrc, uint16_t rightStartSrc)
{
    edge_column_pass(leftStartSrc,  0x03, 0x06, 0x1B);
    /* the second pass's exit is the routine's — the first's is overwritten by it. */
    return edge_column_pass(rightStartSrc, 0x1A, 0x22, 0x2B);
}

/* The 6502-ABI shim.  No inputs at all — every value is an immediate in the original — and
   A, X, Y and the flags come back from the second fill_edge_column_run. */

/* ===========================================================================
   $18EA  copy_dash_data — THE SECOND UNPACK / STOW  (twin #115)
   ---------------------------------------------------------------------------
   The whole call tree is this one routine — it has no JSR of its own; it just moves bytes.
   race_main_loop calls it twice per race:

       A = $00  ASSEMBLE the $7B00-$7FFF overlay (the view rasteriser, the wing mirrors and
                the dashboard bitmap) out of the live TAILS of the 41 $80-spaced source
                blocks at $3000, which is why that page is $00 in every static image;
       A = $80  STOW the (by now heavily self-modified) overlay back into those same tails
                before the page goes back to being MODE 7 screen memory.

   Both directions are the same block walk; only the copy direction flips, so it is one loop
   with the store gated on bit 7 of A.  The two zero-page pointers are seeded from
   dash_ptr_init ($192F): $70/$71 walks UP the source blocks (+$80 per block) and $72/$73
   descends through the overlay page ($7FB0 down to $7768).

   ⚠ TWO 6502 idioms are load-bearing and kept faithfully:
     * Y and the per-block byte count are 8-bit and the loop tests Y AFTER decrementing it,
       so the block's start offset is a sentinel that is NOT itself copied, and an out-of-range
       start would wrap Y through all 256 offsets.  A `do { } while (y != start)` over uint8_t
       reproduces both exactly.
     * The forward copy in the original writes src->dst and then reads that byte straight back
       to store dst->src — a provably identical value into the cell it just came from, so the
       twin drops the redundant readback (it cannot change mem[]: src and dst never alias, the
       block ranges being $3000-$444F and $7768-$7FFF).

   No hardware writes and no callees — the whole thing is RAM.  Addresses are masked to 16 bits
   so the faithful Y-wrap can never index past mem[]; with real data (block starts all < $4F)
   the copy provably stays inside $3000-$7FFF and never reaches the hardware window.
   =========================================================================== */

#define DASH_PTR_INIT     0x192Fu   /* 4-byte seed: src lo/hi ($3000) then dst lo/hi ($7FB0) */
#define DASH_BLOCK_COUNT  0x29u     /* 41 blocks */
#define DASH_BLOCK_TOP    0x4Fu     /* a block's live data always ENDS at offset $4F */

void copy_dash_data_core(uint8_t dirFlag)
{
    /* Direction decided ONCE, not per byte: assemble reads a source block and writes the
       overlay; stow does the reverse.  The block-side pointer walks UP the $80-spaced blocks
       and the page-side pointer descends; only their read/write roles depend on the flag. */
    const int stow = (dirFlag & 0x80) != 0;

    uint16_t block = (uint16_t)(mem[DASH_PTR_INIT + 0] | (mem[DASH_PTR_INIT + 1] << 8));
    uint16_t page  = (uint16_t)(mem[DASH_PTR_INIT + 2] | (mem[DASH_PTR_INIT + 3] << 8));

    uint8_t bytes = 0;
    for (uint8_t b = 0; b < DASH_BLOCK_COUNT; b++) {
        const uint8_t *from = &mem[stow ? page  : block];
        uint8_t       *to   = &mem[stow ? block : page];

        /* Copy offsets $4F down to the block's start offset + 1 (the 6502 tests Y after
           decrementing, so the start is a sentinel and is not itself copied).  ⚠ The sentinel
           is RE-READ from mem[] each pass, not cached: block 18's source is $3900 — the
           dash_block_starts table itself — so in stow mode this very copy overwrites the table,
           including its own sentinel cell mid-descent, exactly as `CMP $3900,X` sees it.  Y and
           the byte count are 8-bit and wrap as the 6502 register does. */
        bytes = 0;
        uint8_t y = DASH_BLOCK_TOP;
        do {
            to[y] = from[y];
            bytes++;
            y--;
        } while (y != mem[DASH_BLOCK_STARTS + b]);

        page  -= bytes;      /* page descended past the bytes just moved (16-bit) */
        block += 0x80u;      /* next $80-spaced source block                       */
    }

    /* Leave the zero-page scratch exactly as the 6502 did.  Nothing outside the routine reads
       these, but the differential compares all of mem[]. */
    plot_ptr_lo     = (uint8_t)block;
    plot_ptr_hi     = (uint8_t)(block >> 8);
    plot_ptr2_lo    = (uint8_t)page;
    plot_ptr2_hi    = (uint8_t)(page >> 8);
    shared_temp_76  = bytes;
}

/* The 6502-ABI shim.  A carries the direction in on entry (and is stashed into math_lo ($74),
   the routine's direction flag).  On exit A/X/Y and the flags carry the tail arithmetic:
   `LDA $70 / ADC #$80` leaves A = final src low byte with the add's V; the block loop closes on
   `CPX #$29` (X = $29, N=0 Z=1 C=1); Y is the last block's start offset the inner loop stopped on. */

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


/* `dividendHi` arrives in A and is the top half of the 16-bit numerator; `dividendLo` is
   math_lo, which the loop consumes bit by bit and hands back as the quotient. */
static Div16By8 div16by8_core(uint8_t dividendHi, uint8_t dividendLo, uint8_t divisor)
{
    GEO_COUNT(g_geoDiv);
    /* remainder in the high byte, dividend-becoming-quotient in the low byte — the pair the
       6502 keeps in A and math_lo, here shifted as ONE word. */
    uint16_t work = (uint16_t)(((uint16_t)dividendHi << 8) | dividendLo);
    Div16By8 r;
    int step;

    /* ⭐ Only the LAST restoring subtract's V leaves this routine, so the loop computes VALUES
       only and the one flag is replayed from its operands afterwards.  That is the whole
       difference between ~900 instructions a call and this: seven times five cpu-struct stores
       plus seven V computations, for flags the algorithm never reads.  `lastMinuend` is what the
       replay needs (the divisor and the borrow are the same on every pass). */
    int     didSubtract = 0;
    uint8_t lastMinuend = 0;

    r.overflow = 0;
    r.setV     = 0;                  /* no subtract ran -> the 6502 leaves V alone (shim honours) */

    for (step = 0; step < 8; step++) {
        /* The bit leaving the top of the word is the remainder's ninth bit.  The 6502 keeps it
           in C and takes it as "the divisor fits" without comparing at all ($0C4A `BCS`),
           which is right: a nine-bit remainder always exceeds an eight-bit divisor. */
        int ninthBit = (work & 0x8000u) != 0;
        work = (uint16_t)(work << 1);

        if (ninthBit || (work >> 8) >= divisor) {
            /* Steps 1..7 restore; the eighth deliberately does not (see the header).  This is a
               plain binary subtract: D = 0 on the render/geometry path (docs/static-map.md
               §Decimal mode), so restoring division is just `remainder -= divisor`. */
            if (step < 7) {
                uint8_t result;
                lastMinuend = (uint8_t)(work >> 8);
                didSubtract = 1;
                result = (uint8_t)(lastMinuend - divisor);
                work   = (uint16_t)((work & 0x00FFu) | ((unsigned)result << 8));
            }
            work |= 1u;              /* the quotient bit, carried up the low half by the next shift */
        }
    }

    /* $0C47's exit V, and the reason it is a `DIVU.W` blocker: it belongs to whichever of the
       seven subtracts ran last.  If none ran, the 6502 left V alone and so does this (setV=0).
       SBC overflow: V = ((a ^ m) & (a ^ (a-m))) bit 7, with carry set (no borrow-in).  Returned
       by value — the one escaping flag — so the core touches no cpu; the shim replays it. */
    if (didSubtract) {
        uint8_t res = (uint8_t)(lastMinuend - divisor);
        r.overflow = (uint8_t)((((lastMinuend ^ divisor) & (lastMinuend ^ res)) >> 7) & 1u);
        r.setV     = 1;
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
    if (r.setV) cpu.V = r.overflow;     /* the last subtract's V; untouched when none ran */
}

/* ===========================================================================
   TWIN #14, $2145/$2147 bearing_to_section — THE BEARING
   TWIN #15, $2285/$2287 project_point      — THE PERSPECTIVE DIVIDE
   ---------------------------------------------------------------------------
   The road pass's two coordinate transforms, and every edge point in the frame goes through
   both: bearing_to_section turns a track section's position into an ANGLE measured from the
   view origin, project_point turns the same section's height into a SCAN LINE.  They are
   taken together because they are very nearly one routine twice over — the same opening
   subtract, the same normalise-and-divide, the same pair of entry points — and because
   between them they call exactly ONE function, div16by8, which is already real C (twin #13).

   ⭐ THAT CALLEE SET IS WHY THESE ARE WORTH TWINNING AT ALL.  Twins #4-#12 were DRIVERS: short
   bodies over long transliterated subtrees, and deleting their interpreter collected nothing
   because the interpreter was never where their time was (docs/faithfulness-seam.md §8).
   These two have no transliterated subtree left underneath them, so every instruction the
   interpreter was running for them is an instruction this file now owns.

   ⭐ TWO ENTRY POINTS EACH, AND THE SECOND ONE IS AN ORIGIN.  $2145 and $2285 are two bytes
   long — `LDY #0` — and fall into $2147 / $2287, which subtract view_origin[Y].  Y is a byte
   offset into a STRIDE-SIX array of three-component positions, so Y = 0 is the camera and
   Y = 6 is the road sign's own displaced viewpoint (build_road_sign is the only caller that
   passes it, at $4CED and $4D1A; build_sign_origin is what fills that second row).  Ghidra
   split each pair into two functions and the transliteration kept them that way.

   ⭐ ONE DELTA VECTOR, THREE PARALLEL ARRAYS.  Both routines' opening is the same three
   instructions per component, and the cells line up: low bytes at $80-$82, the magnitude's
   high bytes at $83-$85, the RAW signed high bytes at $86-$88, all indexed by component 0..2.
   bearing_to_section fills components 0 and 2 — the ground plane, whose ratio is the bearing —
   and project_point fills component 1, the height.  Those are point_delta_lo / _hi / _sign
   now; before this twin they were nine bare zero-page addresses, one of which ($0088) carried
   a name belonging to an unrelated owner.

   ⚠ THE ARCTAN SCALE IS THE SAME IN BOTH ARMS, and symbols.csv used to say it was not.  Both
   have exactly three LSR/ROR pairs, so both scale the table byte by 32 and 45 degrees is
   $1FE0 either way.  What differs is the octant: the quadrant base, and which way the negate
   goes.  Counted out of the listing — the prose had been read many times and was still wrong.

   ⚠ DECIMAL MODE REACHES EVERY ONE OF THESE SUBTRACTS, so the abs, the negate and both
   closing adjustments go through the 6502's own ADC/SBC.  The fixtures randomise D for the
   same reason twin #13's does, and that is also where the exit V comes from.

   ⭐ WHAT THIS UNLOCKS AND DELIBERATELY DOES NOT TAKE.  div16by8's header records that its
   exit V is dead at all three of its call sites; those three sites are now both in this file,
   so the DIVU.W replacement it describes is provably legal.  It is NOT taken here — it is a
   separate and separately-measurable change, and these fixtures still compare V, so taking it
   means relaxing them in the same commit.
   =========================================================================== */

#define POINT_DELTA_LO    0x0080u  /* point_delta_lo[0..2]   — camera-relative delta, low byte */
#define POINT_DELTA_HI    0x0083u  /* point_delta_hi[0..2]   — ...its magnitude's high byte */
#define POINT_DELTA_SIGN  0x0086u  /* point_delta_sign[0..2] — ...and the raw high byte, the sign */
#define ARCTAN_TABLE      0x6100u  /* arctan_table — atan(i/256) with 45 degrees at $FF */
#define RECIP_TABLE_BIAS  0x6180u  /* reciprocal_table reached biased: entry i = $8000/(i+$80) */


/* One component of the camera-relative delta: the section coordinate minus the view origin,
   split into its sign (the high byte of the signed difference) and its magnitude (the absolute
   value of that difference).  ⚠ The 6502 has the component baked into the address (`LDA
   $0902,X`), so unlike section_word's index this one does NOT wrap at 8 bits — the scratch slot
   $FD plus component 2 is $09FF, still inside the table.  The game runs this in binary mode, so
   the sign is bit 7 of the true two's-complement high byte. */
static ViewDelta view_delta(uint8_t sectionByte, unsigned component, uint8_t origin)
{
    int section = (int)mem[SECTION_LO_TBL + sectionByte + component]
                | ((int)mem[SECTION_HI_TBL + sectionByte + component] << 8);
    int viewpt  = (int)mem[VIEW_ORIGIN_LO + origin + component]
                | ((int)mem[VIEW_ORIGIN_HI + origin + component] << 8);
    uint16_t  diff = (uint16_t)(section - viewpt);
    ViewDelta d;

    d.rawHi = (uint8_t)(diff >> 8);
    d.mag   = (diff & 0x8000u) ? (uint16_t)(-(int)diff) : diff;   /* |section - viewpoint| */
    return d;
}

/* Shift the larger magnitude left until the bit leaving its high byte is a 1, taking the
   smaller one with it ONE PLACE FEWER — that spare place is the headroom the 8-bit quotient
   needs — and hand back the high byte with the bit rotated back in: the divisor div16by8
   wants, normalised so bit 7 is set.  The larger's high byte never goes back to memory (the
   6502 keeps it in A for the whole loop); its low byte and both of the smaller's do.
   $21BD-$21C6, $2235-$223E and $22C5-$22CF are all this same idiom.

   ⚠ A larger of 0 would spin here exactly as the 6502 does.  It cannot happen: a zero larger
   means both ground magnitudes are zero, which is the equal case and never reaches an arm,
   and project_point's far clip rejects every point when point_dist is 0. */
static uint8_t normalise_for_divide(uint8_t* largerLo, uint8_t largerHi,
                                    uint16_t* smaller, unsigned* shifts)
{
    unsigned hi = largerHi;
    unsigned lo = *largerLo;

    *shifts = 0;
    for (;;) {
        unsigned out = (hi >> 7) & 1u;                    /* ASL lo / ROL hi */
        hi = ((hi << 1) | ((lo >> 7) & 1u)) & 0xFFu;
        lo = (lo << 1) & 0xFFu;
        if (out)
            break;
        *smaller = (uint16_t)(*smaller << 1);
        (*shifts)++;
    }
    *largerLo = (uint8_t)lo;
    return (uint8_t)((hi >> 1) | 0x80u);                  /* ROR A, with the 1 that fell out */
}

/* $220D-$2234 — the four 45-degree diagonals, on the two sign bits.  Reached three ways
   (equal magnitudes, or either arm's dividend catching up with its divisor).  shared_temp_7e =
   $FF says "maximally oblique" to the point_distance_hypot that runs next.  The quadrant is the
   two ground-plane sign bits: component 0 -> bit 1, component 2 -> bit 0. */
static void bearing_diagonal(void)
{
    static const uint8_t diagonal[4] = { 0x20u, 0x60u, 0xE0u, 0xA0u };
    unsigned quadrant = ((mem[POINT_DELTA_SIGN + 0] & 0x80u) ? 2u : 0u)
                      | ((mem[POINT_DELTA_SIGN + 2] & 0x80u) ? 1u : 0u);

    shared_temp_7e = 0xFFu;                         /* $220D */
    bearing_lo     = 0x00u;                         /* $2211 */
    bearing_hi     = diagonal[quadrant];
}

/* $21C1-$220C and $2239-$2284 — the two arms, which differ in three things and nothing else:
   which component is the divisor, which sign byte picks the quadrant base, and which way the
   negate goes.  Arm A measures off component 0 (base $40/$C0, negate when the two signs
   AGREE); arm B measures off component 2 (base $00/$80, negate when they DIFFER).  Together
   they are an octant decomposition of a full turn. */
static void bearing_arm(unsigned largerComponent, unsigned smallerComponent,
                        uint8_t quadrantBase, int negateWhenSignsAgree)
{
    uint8_t  largerLo = mem[POINT_DELTA_LO + largerComponent];
    uint16_t smaller  = (uint16_t)(((unsigned)mem[POINT_DELTA_HI + smallerComponent] << 8)
                                   | mem[POINT_DELTA_LO + smallerComponent]);
    unsigned shifts;
    uint8_t  divisor;

    divisor = normalise_for_divide(&largerLo, mem[POINT_DELTA_HI + largerComponent],
                                  &smaller, &shifts);
    mem[POINT_DELTA_LO + largerComponent]  = largerLo;
    mem[POINT_DELTA_LO + smallerComponent] = (uint8_t)smaller;
    mem[POINT_DELTA_HI + smallerComponent] = (uint8_t)(smaller >> 8);

    shared_temp_76 = divisor;                       /* $21C7 / $223F */
    math_lo        = (uint8_t)smaller;              /* $21C9 / $2241 */

    /* $21CD / $2245 — a dividend half that has caught the divisor would overflow the 8-bit
       quotient, and is the 45-degree case by another road.  After the sort the dividend high
       byte is <= the divisor, so this is the only way it reaches it. */
    if ((uint8_t)(smaller >> 8) == divisor) {
        bearing_diagonal();
        return;
    }

    /* $22DA — the divide, a proper fraction (dividend hi < divisor) so an 8-bit quotient: one
       DIVU.W where the 6502 spent a seven-step restoring loop. */
    uint8_t quotient  = (uint8_t)revs_divu16(smaller, divisor);
    math_lo           = quotient;
    uint8_t rawArctan = mem[ARCTAN_TABLE + quotient];
    shared_temp_7e    = rawArctan;                  /* how oblique — the hypot's segment split */

    /* $21E1-$21EA / $2259-$2262 — the table byte * 32 into a 16-bit angle (the 6502 does it as
       three LSR/ROR pairs of {rawArctan:0}, i.e. a 16-bit >> 3). */
    unsigned angle = (unsigned)rawArctan * 32u;

    /* $21EC / $2264 — the negate that puts the angle on the right side of its axis.  The two
       arms sweep opposite ways round, which is why the test is inverted between them. */
    int signsDiffer = ((mem[POINT_DELTA_SIGN + 0] ^ mem[POINT_DELTA_SIGN + 2]) & 0x80u) != 0;
    if (negateWhenSignsAgree ? !signsDiffer : signsDiffer)
        angle = (unsigned)(-(int)angle) & 0xFFFFu;

    /* $21FF / $2277 — and the quadrant the octant sits in, from the LARGER component's sign,
       added into the angle's high byte. */
    uint8_t base = (mem[POINT_DELTA_SIGN + largerComponent] & 0x80u)
                     ? (uint8_t)(quadrantBase + 0x80u) : quadrantBase;
    bearing_lo = (uint8_t)angle;
    bearing_hi = (uint8_t)((angle >> 8) + base);
}

void bearing_to_section_core(uint8_t sectionByte, uint8_t origin)
{
    GEO_COUNT(g_geoBearing);
    /* $2147-$2185 — components 0 and 2 of the delta: the ground plane. */
    ViewDelta d0 = view_delta(sectionByte, 0, origin);
    ViewDelta d2 = view_delta(sectionByte, 2, origin);

    mem[POINT_DELTA_LO   + 0] = (uint8_t)d0.mag;
    mem[POINT_DELTA_HI   + 0] = (uint8_t)(d0.mag >> 8);
    mem[POINT_DELTA_SIGN + 0] = d0.rawHi;
    mem[POINT_DELTA_LO   + 2] = (uint8_t)d2.mag;
    mem[POINT_DELTA_HI   + 2] = (uint8_t)(d2.mag >> 8);
    mem[POINT_DELTA_SIGN + 2] = d2.rawHi;

    /* $2187-$2191 THE SORT.  The divide wants a proper fraction, so the smaller magnitude
       becomes the dividend and the larger the divisor.  point_distance_hypot reads the same
       two pairs afterwards as its min and max, UNSHIFTED — the normalise below only touches
       the point_delta cells, never these. */
    {
        int d2Smaller = d2.mag <  d0.mag;
        int equal     = d2.mag == d0.mag;

        if (d2Smaller) {
            hypot_min_hi = (uint8_t)(d2.mag >> 8);
            hypot_min_lo = (uint8_t)d2.mag;
            hypot_max_lo = (uint8_t)d0.mag;
            hypot_max_hi = (uint8_t)(d0.mag >> 8);
            bearing_arm(0, 2, 0x40u, 1);            /* $21C1 — measured off component 0 */
        } else {
            hypot_min_hi = (uint8_t)(d0.mag >> 8);
            hypot_min_lo = (uint8_t)d0.mag;
            hypot_max_lo = (uint8_t)d2.mag;
            hypot_max_hi = (uint8_t)(d2.mag >> 8);
            if (equal)
                bearing_diagonal();                 /* $21B8 — the two are the same length */
            else
                bearing_arm(2, 0, 0x00u, 0);        /* $2239 — measured off component 2 */
        }
    }
}

ProjPoint project_point_core(uint8_t sectionByte, uint8_t origin)
{
    /* The 6502 dropped a point by returning with carry SET (the far clip's $22BC SEC, or the
       >$80 quotient); both exits are this same "clipped" answer.  behind is 0 on a drop — the
       caller's BPL is behind a taken BCS, so it is never read here. */
    static const ProjPoint PROJ_CLIPPED = { 0, 1, 0 };

    GEO_COUNT(g_geoProject);

    /* $2287-$22AE — component 1 of the delta, the HEIGHT, and the only component that is
       scaled on the way in: >> 3 as a 16-bit pair before anything looks at it. */
    ViewDelta d      = view_delta(sectionByte, 1, origin);
    uint16_t  height = (uint16_t)(d.mag >> 3);
    unsigned  shifts;
    uint8_t   divisor, distLo, quotient, lineByte;

    mem[POINT_DELTA_SIGN + 1] = d.rawHi;
    mem[POINT_DELTA_LO   + 1] = (uint8_t)height;
    mem[POINT_DELTA_HI   + 1] = (uint8_t)(height >> 8);

    /* $22B0-$22BD THE FAR CLIP — the scaled height against point_dist, which
       point_distance_hypot filled in for THIS point a moment ago, so it is a vertical
       field-of-view test and not a comparison with a stale distance.  Height at or beyond the
       distance drops the point. */
    if (height >= (uint16_t)(((unsigned)point_dist_hi << 8) | point_dist_lo))
        return PROJ_CLIPPED;                        /* $22BC SEC — "drop this point" */

    /* $22BE-$22D8 — normalise the DISTANCE until its top bit falls out, taking the height
       with it one place fewer, and record the pair the road's apparent width is made of:
       reciprocal_table's entry for the normalised distance (a MANTISSA) in proj_width and the
       shift count (its EXPONENT) in proj_width_shift.  Neither is read again here — both are
       for emit_edge_width_offset and the object slot writer.
       ⚠ point_dist_lo is shifted IN PLACE and does not survive; point_dist_hi does, because
       the 6502 keeps it in A for the whole loop. */
    distLo  = point_dist_lo;
    divisor = normalise_for_divide(&distLo, point_dist_hi, &height, &shifts);
    point_dist_lo = distLo;

    mem[POINT_DELTA_LO + 1] = (uint8_t)height;
    mem[POINT_DELTA_HI + 1] = (uint8_t)(height >> 8);

    shared_temp_76   = divisor;
    proj_width_shift = (uint8_t)shifts;
    proj_width       = mem[RECIP_TABLE_BIAS + divisor];

    /* $22DA-$22E1 — the perspective divide itself: the shifted height over the normalised
       distance, one DIVU.W.  A DIVIDE, not a multiply — the reciprocal above is for the width. */
    unsigned q = revs_divu16(height, divisor);
    math_lo    = (uint8_t)q;

    /* $22E3-$22E7 — a quotient past $80 is off the top of the 0..79 scan-line space, and leaves
       by the same drop door as the far clip.  ⚠ Tested on the FULL quotient, not its low byte:
       a point right at the far-clip boundary divides to 256+ (line 128+, off screen), which the
       6502's restoring divide saturated to a byte >= $80 before comparing.  DIVU keeps the true
       value, so the >= $80 test must see it too — the low byte alone could wrap below $80 and
       fail to drop an off-screen point. */
    if (q >= 0x80u)
        return PROJ_CLIPPED;
    quotient = (uint8_t)q;

    /* $22E9-$22FD — 60 either side of the camera's eye level, less the frame's smoothed pitch,
       and that is the scan line.  bit 7 of the height sign chooses above/below. */
    if (mem[POINT_DELTA_SIGN + 1] & 0x80u)
        lineByte = (uint8_t)(0x3Cu - quotient);     /* $22ED — below: 60 - quotient */
    else
        lineByte = (uint8_t)(quotient + 0x3Cu);     /* $22F5 — above: quotient + 60 */

    lineByte       = (uint8_t)(lineByte - view_pitch_offset);
    projected_line = lineByte;
    /* The point survived the clip; "behind the camera" is bit 7 of the final line, which the
       6502 left in N and the caller reads to fold back to a subdivide. */
    return (ProjPoint){ lineByte, 0, (lineByte & 0x80u) != 0 };
}

/* The 6502-ABI shims.  ⚠ Only the BODIES are twinned: $2145 and $2285 stay transliterated,
   because each is a single `LDY #0` that falls into the body, so a twin of them would run the
   same core the oracle does and the fixture would compare native against native — a fixture
   that passes vacuously (docs/validation-harness.md).  Two generated LDY macros is the right
   price for keeping both oracles real.

   X is the section's byte index into section_coord_lo/hi; Y is the view origin's byte offset,
   0 for the camera and 6 for the road sign's viewpoint. */


/* ===========================================================================
   $2B26-$2FFF  THE SPAN RASTERISER — twins #25-#39
   ---------------------------------------------------------------------------
   Everything draw_road reaches below its three stages, and the one part of the view pipeline
   that was still transliterated after twins #16-#24 closed the geometry pass.  Read as one
   subsystem it is a Bresenham span painter with an unusual amount of machinery around it:

     interp_edge            picks the arm, builds the colour patterns and the two surface
                            codes, and plants SEVEN self-modified bytes in the four arms
                            and the two plotters
     draw_span_*_fwd/rev    four arms = {X-major, Y-major} x {ascending, descending}, each an
                            eight-column chain UNROLLED once and entered partway through
     road_span_plot / _2    the leaf that actually merges one column's pixels into a buffer
     span_end_marker_p1/p2  the $FF terminator that closes a run, and itself an opcode slot

   ⭐ WHAT THE SELF-MODIFICATION IS FOR, because it is not obfuscation and the twins have to
   model all of it:
     * the Y-STEP slots ($2F47/$2F60/$2F89/$2FA2/$2F18) hold INY, DEY or NOP — a span walks up
       the screen, down it, or stays on one line, and the direction is a per-span value;
     * the DESTINATION operands ($2F4F/$2F50, $2F91/$2F92) name one of the four surface_edge
       buffers, so one plotter serves all four passes;
     * the ENTRY OFFSETS ($2D28/$2DAB/$2E2F/$2EA8) are a computed jump into the middle of an
       unrolled chain — "start at sub-column k", the standard way to run a partial unrolled
       loop without a counter;
     * and the two END MARKERS are switched between `CPX #$80` and `RTS`, i.e. the whole
       routine is turned off, when the run needs no terminator.

   ⚠ THE NINE BYTES AT $80-$88 ARE A SECOND TENANT of point_delta_lo/hi/sign (see
   docs/rename.md): to build_track_geometry they are a camera-relative delta vector, to this
   pass they are DDA state.  The windows never overlap — draw_road runs after
   build_track_geometry has finished — and the defines below are what make the code readable.
   =========================================================================== */

#define SPAN_LINE_END  0x0082u   /* point_delta_lo[2]   — the scan line the span stops at */
#define SPAN_DX        0x0083u   /* point_delta_hi[0]   — the DDA's major delta */
#define SPAN_DY        0x0084u   /* point_delta_hi[1]   — ...and its minor delta */
#define SPAN_BLOCK     0x0085u   /* point_delta_hi[2]   — the source block, 0..$2C */
#define SPAN_ARM       0x0086u   /* point_delta_sign[0] — bit 7 picks ascending or descending */
#define SPAN_YSTEP     0x0087u   /* point_delta_sign[1] — which way the plotters step Y */
#define SPAN_CLIP      0x0088u   /* point_delta_sign[2] — two-bit rolling clip history */

#define COLOUR_PATTERN     0x628Fu   /* colour_pattern_tbl     — 4 bytes, the span's pixels */
#define COLOUR_PATTERN_OR  0x629Cu   /* colour_pattern_or_tbl  — ...masked to this column */
#define SURFACE_STYLE_TBL  0x5FD0u   /* surface_style_tbl — 4 bytes per style record */
#define SPAN_PAIR_OFFSET   0x30FCu   /* span_pair_offset_tbl — by pass, the paired-index gap */
#define ROW_BASE_HI        0x2B1Eu   /* row_base_hi — by pass, the surface_edge buffer */
#define ROW_BASE_LO        0x2B22u   /* row_base_lo */

/* The five Y-step slots and the two end-marker opcode slots, by address. */
#define SLOT_STEP_P1_IN    0x2F47u
#define SLOT_STEP_P1_OUT   0x2F60u
#define SLOT_STEP_P2_IN    0x2F89u
#define SLOT_STEP_P2_OUT   0x2FA2u
#define SLOT_STEP_CAP      0x2F18u
#define SLOT_MARKER_P1     0x2FC0u
#define SLOT_MARKER_P2     0x2FD7u

/* The patched destination operands: the low/high byte pair each plotter stores through. */
#define OPERAND_DEST_P1_LO 0x2F4Fu
#define OPERAND_DEST_P1_HI 0x2F50u
#define OPERAND_DEST_P2_LO 0x2F91u
#define OPERAND_DEST_P2_HI 0x2F92u

#define OP_INY 0xC8u
#define OP_DEY 0x88u
#define OP_NOP 0xEAu

/* One Y-step slot.  Three opcodes are legal and anything else is a byte the model cannot
   execute, so it traps exactly where the transliteration does — which is an EXIT, and the
   caller has to take it as one.  Steps *y (INY/DEY/NOP); returns 0 on the trap, *y untouched. */
static int span_step_y(unsigned slot, uint8_t *y)
{
    switch (mem[slot]) {
    case OP_DEY: (*y)--; return 1;
    case OP_INY: (*y)++; return 1;
    case OP_NOP:         return 1;
    default:     platform_smc_unhandled(slot, mem[slot]); return 0;
    }
}

/* ---------------------------------------------------------------------------
   $0E40  abs16_math  (twin #25)
   ---------------------------------------------------------------------------
   |math_lo:A| in place: the 16-bit value's low byte lives in math_lo and its high byte
   arrives (and leaves) in A.  Twenty-one callers across the engine; interp_edge's is the
   one in this subsystem.

   ⚠⚠ IT BRANCHES ON THE CALLER'S N, not on bit 7 of A — the same trap abs8 has.  Every real
   caller has just computed A, so the two agree there and nowhere else; `if (A & 0x80)` fails
   a randomised pre-state, and decimal mode decorrelates them even for a freshly computed
   value (docs/faithfulness-seam.md).
   ⚠ It BRANCHES ON THE CALLER'S N and then falls into neg16_math (twin #48), which is the
   function this now calls — nothing is duplicated.
   --------------------------------------------------------------------------- */
void abs16_math(void)
{
    if (!cpu.N) return;             /* $0E40 BPL — caller's N is the value's sign; positive: A kept */

    /* Negate (cpu.A : math_lo) in place — the high byte the caller is holding is the value's high.
       D = 0 on every caller (docs/static-map.md §Decimal mode), so a plain 16-bit negate.
       $0E42 (neg16_math, the init entry abs16_math falls into) first PARKS the caller's high byte
       in math_hi, and the negate leaves it there — so math_hi holds the PRE-negate high byte on
       exit, which the pure-C twin reproduces because callers see that cell. */
    uint8_t high = cpu.A;
    uint16_t v = (uint16_t)(0u - (uint16_t)(((uint16_t)high << 8) | math_lo));
    math_hi = high;
    math_lo = (uint8_t)v;
    cpu.A   = (uint8_t)(v >> 8);
}

/* ---------------------------------------------------------------------------
   $2FEE  road_span_advance  (twin #26)
   ---------------------------------------------------------------------------
   "Has this span walked off the top of its source block?"  Returns nothing but the CARRY:
   set while the scan line is still PAST the block's first line, clear the moment it reaches
   it.  Both plotters consult it before merging a pixel into an occupied cell, and a clear
   carry there ends the column.

   ⚠ Its exit N/Z come from reloading X, not from the compare — the compare's own N/Z are
   dead by then, and no caller reads them (both plotters and both markers consult only the
   CARRY).  A and X are preserved — the whole point of the 6502's math_lo/math_hi round trip,
   which the pure-C core simply does not need — so the routine is a pure predicate on Y.
   --------------------------------------------------------------------------- */
int road_span_advance_core(uint8_t y)
{
    /* CMP then a CLC on equal: carry SET while the scan line is still past the block's first
       line, CLEAR the moment it reaches (or precedes) it.  C = (Y > dash_block_starts[block]). */
    return y > mem[DASH_BLOCK_STARTS + mem[SPAN_BLOCK]];
}


/* ---------------------------------------------------------------------------
   $2F12-$2F44  the run's SURFACE CAP
   ---------------------------------------------------------------------------
   Not a function of its own in the disassembly — it is the tail two of the four arms fall
   into, and the plotters' abort path jumps to it.  It stamps ONE view_line_surface entry:
   the class of the scan line the run ended on, which view_paint_lines later turns into that
   line's background colour.

   Which of the two codes it writes is the walk direction (span_swapped), and a descending
   walk steps back a line first and gives up if that line already carries a class.  Past 45
   degrees off the section (view_yaw_offset >= $28) the low two bits are left alone; below it
   they are cleared unless they are already 3.

   ⚠ X is CLOBBERED here — $2F2E loads view_yaw_offset into it — and Y by the descending
   arm's step.  Both are live at the plotters' exit, so both are part of the contract.
   --------------------------------------------------------------------------- */
/* $2F23's per-circuit "is this scan line already classified?" test — the one operation in the
   surface cap whose answer genuinely leaves an external routine.  Silverstone reads
   view_line_surface directly ($2F23 == LDA abs,Y); an expansion circuit runs its own hook,
   whose Z flag is the answer.  This is the sanctioned flag-escape seam: the cpu.h read is
   isolated in a named helper right at the external call.  Returns 1 when Z is set (the line is
   still empty — go on and stamp it), 0 when clear (already classified — leave it), -1 on a trap. */
static int span_cap_line_slot_z(uint8_t y)
{
    if (mem[0x2F23] == 0xB9) {                       /* unpatched: Silverstone, LDA $5F60,Y */
        return mem[VIEW_LINE_SURFACE + y] == 0;      /* Z set iff the entry is still zero */
    }
    if (mem[0x2F23] == 0x20) {                        /* an expansion circuit's own hook (JSR) */
        uint16_t hook = (uint16_t)(mem[0x2F24] | (mem[0x2F25] << 8));
        if (hook >= 0x5300 && hook <= 0x5A25) {
            /* Reproduce the 6502 register context $2F19-$2F22 hands the hook: A = span_swapped
               (from the LDA the BMI branched on), Y = this scan line (after the DEY), N/Z from
               that DEY.  The hook is circuit code that reads them and answers through Z. */
            cpu.A = span_swapped; cpu.Y = y;
            cpu.N = (y >> 7) & 1; cpu.Z = (y == 0);
            revs_track_hook(hook);
            return cpu.Z ? 1 : 0;
        }
        platform_smc_unhandled(0x2F23, hook); return -1;
    }
    platform_smc_unhandled(0x2F23, mem[0x2F23]); return -1;
}

static void span_cap_line(uint8_t y)
{
    unsigned code;

    if (span_swapped & 0x80u) {          /* $2F19-$2F1B: the walk ran the other way (BMI) */
        y--;
        int z = span_cap_line_slot_z(y);
        if (z <= 0) return;              /* trapped, or the line already has a class — leave it */
        code = span_cap_surface_b;
    } else {
        code = span_cap_surface_a;
    }

    if (y >= 0x50u) return;              /* $2F2A CPY #$50 — off the bottom of the view */

    if (view_yaw_offset >= 0x28u) {      /* $2F31 CPX #$28 with X = view_yaw_offset */
        /* $2F35-$2F3F — keep a class of 3, flatten anything else to a multiple of 4. */
        if ((code & 0x03u) != 0x03u) code &= 0xFCu;
    }
    mem[VIEW_LINE_SURFACE + y] = (uint8_t)code;
}

/* $2F7E — the plotter has reached the span's end line.  `TSX/INX/INX/TXS` drops the ARM's
   return address as well as this one, so the RTS below lands back in interp_edge and the
   whole eight-column chain is abandoned.  The model keeps return addresses on the C stack,
   so the drop is a flag (cpu.h's UNWIND) that every caller in the chain consults.
   ⚠ X really is clobbered by the idiom — it comes back as S+2 — and the differential sees it. */
static void span_abandon_chain(uint8_t y)
{
    UNWIND_SET();                        /* the TSX/INX/INX/TXS two-level return, modelled */
    if (span_cap_pending != 0) span_cap_line(y);
}

/* Did the leaf just abandon the chain?  The arms consume the flag exactly as the generated
   corpus does, so a native arm and a transliterated one behave identically. */
#define span_chain_abandoned()  UNWIND_TAKEN()

/* ---------------------------------------------------------------------------
   $2F45  road_span_plot  (twin #27)  and  $2F87  road_span_plot_2  (twin #28)
   ---------------------------------------------------------------------------
   THE LEAF OF THE WHOLE VIEW PIPELINE: one column of one span, merged into one buffer cell.
   The two are the same routine against different pointers — that is the only difference —
   so they share a core and differ by a descriptor.

   WHAT ONE CALL DOES:
     1. steps Y by the span's direction (an opcode slot), and gives up on the entire chain if
        that lands on the span's end line;
     2. records which SOURCE BLOCK feeds this scan line, in the pass's own surface_edge
        buffer (the store's address is the patched operand pair);
     3. merges the column's pixels into the cell: an EMPTY cell takes the style's pattern
        whole, an occupied one keeps the other columns' bits and takes this column's from
        colour_pattern_or_tbl.  $55 is the "all four columns lit" value and is treated as
        empty on the way in and substituted back on the way out, so it never reads as zero;
     4. copies the span's bearing high byte alongside, through the second pointer;
     5. steps Y again and returns with the carry clear.

   ⭐ A cell that is occupied AND past its block's first scan line ends the column instead
   (road_span_advance is the test) — that is how a span stops where the previous one already
   painted.

   ⚠ Both stores go through bus_read/bus_write and that is not an oversight: the pointer is a
   pre-state value in the fixture, so it can name the hardware window, and `make validate`
   diffs the hardware-write SEQUENCE.  Three accesses per call is not where the road pass's
   time is (docs/perf-method.md).
   --------------------------------------------------------------------------- */

const SpanPlotter SPAN_PLOT_1 = {
    SLOT_STEP_P1_IN, SLOT_STEP_P1_OUT, OPERAND_DEST_P1_LO, OPERAND_DEST_P1_HI,
    MEM_plot_ptr2_lo, MEM_plot_ptr_lo
};
const SpanPlotter SPAN_PLOT_2 = {
    SLOT_STEP_P2_IN, SLOT_STEP_P2_OUT, OPERAND_DEST_P2_LO, OPERAND_DEST_P2_HI,
    MEM_plot_ptr_lo, MEM_plot_ptr3_lo
};

/* ⭐⭐ ALWAYS_INLINE, and it is worth 4% of the frame.  The descriptor is a compile-time
   constant at both call sites, so inlining turns every `p->slot` from a memory operand into
   an immediate and the whole struct disappears; left out of line GCC passes a pointer and
   re-loads five fields per call, in the routine that runs eight times per scan line.
   Same rule as REVS_FLAG_OP above — measured, not assumed (docs/perf-method.md).

   Pure C: no cpu struct.  The DDA accumulator is the `accumulator` parameter (it is only ever
   handed straight back to the caller — the 6502 parked it in bearing_lo across the call, but
   nothing here reads it, so the spill is gone).  `*y` is the scan-line counter, stepped in
   place; `*carry` is the DDA carry threaded column-to-column (in, and out); `*abandoned` tells
   the caller this span hit its predecessor and the chain has been unwound.

   Carry out: 0 on a normal exit; on either Y-step SMC-trap the pre-step compare's carry stands
   (that is what the 6502's CPY/CMP left in C when the trapping slot never ran); unchanged on
   the entry-step trap.  On the abandon path the carry, y and accumulator are all dead. */
static inline __attribute__((always_inline))
void span_plot_core(const SpanPlotter* p, uint8_t accumulator, uint8_t column,
                    uint8_t *y, unsigned *carry, int *abandoned)
{
    unsigned cellAddr, cell, a, preC;

    ROAD_COUNT(g_roadCols);               /* one column of one span — the view pipeline's leaf */
    (void)accumulator;
    *abandoned = 0;
    if (!span_step_y(p->stepIn, y)) return;        /* entry slot trapped: carry_in stands */
    if (*y == mem[SPAN_LINE_END]) { span_abandon_chain(*y); *abandoned = 1; return; }

    /* Which source block feeds this scan line, into the pass's surface_edge buffer. */
    mem[(uint16_t)((mem[p->destLo] | (mem[p->destHi] << 8)) + *y)] = mem[SPAN_BLOCK];

    cellAddr = (unsigned)(mem[p->cellPtr] | (mem[(uint8_t)(p->cellPtr + 1)] << 8)) + *y;
    cell     = mem[(uint16_t)cellAddr];

    if (cell == 0) {
        a    = mem[COLOUR_PATTERN + column];        /* an empty cell takes the pattern whole */
        preC = (*y >= mem[SPAN_LINE_END]);          /* CPY(SPAN_LINE_END): y != end here, so y > end */
    } else {
        if (*y < 0x2Cu && !road_span_advance_core(*y)) {   /* reached the block's first line */
            span_step_y(p->stepOut, y);      /* road_span_advance left C=0, and a trap here keeps it */
            *carry = 0u;
            return;
        }
        preC = (cell >= 0x55u);                     /* CMP(0x55): carry survives to the exit trap */
        a    = (cell == 0x55u) ? 0u : cell;         /* "all four columns" reads as empty */
        a    = (a & mem[COLOUR_PATTERN_AND + column]) | mem[COLOUR_PATTERN_OR + column];
        if (a == 0) a = 0x55u;                      /* ...and is substituted back */
    }

    mem[(uint16_t)cellAddr] = (uint8_t)a;
    mem[(uint16_t)((mem[p->linePtr] | (mem[(uint8_t)(p->linePtr + 1)] << 8)) + *y)] = bearing_hi;

    *carry = span_step_y(p->stepOut, y) ? 0u : preC;
}

void road_span_plot(void)
{
    uint8_t y = cpu.Y; unsigned carry = cpu.C; int ab;
    span_plot_core(&SPAN_PLOT_1, cpu.A, cpu.X, &y, &carry, &ab);
    cpu.Y = y; cpu.C = carry ? 1 : 0;               /* A echoes the accumulator unchanged */
}
void road_span_plot_2(void)
{
    uint8_t y = cpu.Y; unsigned carry = cpu.C; int ab;
    span_plot_core(&SPAN_PLOT_2, cpu.A, cpu.X, &y, &carry, &ab);
    cpu.Y = y; cpu.C = carry ? 1 : 0;
}

/* ---------------------------------------------------------------------------
   $2FC0  span_end_marker_p1  (twin #29)  and  $2FD7  span_end_marker_p2  (twin #30)
   ---------------------------------------------------------------------------
   Close a run: write the $FF terminator the view rasteriser reads as "no more spans on this
   line" through one of the two pointers, then reset X to $80.  Called once after each group
   of four columns.

   ⚠⚠ THE FIRST BYTE IS AN OPCODE SLOT, so the routine has two shapes: `CPX #$80` (do the
   work) and `RTS` (do nothing at all).  interp_edge chooses between them at $2CAA/$2CB4, and
   the RTS form leaves X, A and every flag untouched — which is why it cannot be modelled as
   an `if` around the body.

   ⭐ X is the "the column ran clean to its end" mark: the arms load 0..3 into it before each
   plot and only the untouched $80 gets a terminator.  A is preserved across the store by the
   TAX/TXA pair, which is also what makes X's exit value $80 rather than the pattern byte.
   --------------------------------------------------------------------------- */
static void span_end_marker(unsigned slot, unsigned ptr, uint8_t y,
                            uint8_t *colMark, unsigned *carry)
{
    switch (mem[slot]) {
    case OP_RTS:      return;                    /* switched off: colMark, A, carry untouched */
    case OP_CPX_IMM:  break;
    default:          platform_smc_unhandled(slot, mem[slot]); return;
    }

    if (*colMark == 0x80u) {                     /* the column never plotted anything */
        int atEnd = 1;
        if (y < 0x2Cu) atEnd = road_span_advance_core(y);   /* still past the first line */
        if (atEnd)
            mem[(uint16_t)((mem[ptr] | (mem[(uint8_t)(ptr + 1)] << 8)) + y)] = 0xFFu;
    }
    *colMark = 0x80u;
    *carry   = 0u;
}

void span_end_marker_p1(void)
{
    uint8_t colMark = cpu.X; unsigned carry = cpu.C;
    span_end_marker(SLOT_MARKER_P1, MEM_plot_ptr_lo, cpu.Y, &colMark, &carry);
    cpu.X = colMark; cpu.C = carry ? 1 : 0;
}
void span_end_marker_p2(void)
{
    uint8_t colMark = cpu.X; unsigned carry = cpu.C;
    span_end_marker(SLOT_MARKER_P2, MEM_plot_ptr2_lo, cpu.Y, &colMark, &carry);
    cpu.X = colMark; cpu.C = carry ? 1 : 0;
}

/* ---------------------------------------------------------------------------
   $2D17 draw_span_shallow_fwd  (twin #31)   $2D9A draw_span_shallow_rev  (#32)
   $2E20 draw_span_steep_fwd    (twin #33)   $2E99 draw_span_steep_rev    (#34)
   ---------------------------------------------------------------------------
   THE SPAN WALK ITSELF, and the four are one algorithm with two independent choices:

     X-MAJOR ("shallow", dx >= dy)   one pixel per column; the DDA adds dy and a carry is
                                     what moves the pixel to the next scan line
     Y-MAJOR ("steep",   dx <  dy)   the column REPEATS until the DDA carries, which is how
                                     a steep span paints several lines of the same column
     ASCENDING / DESCENDING          columns 0..3 or 3..0, the three screen pointers and the
                                     source-block index stepped up or down, and the bound
                                     plot_ptr2_hi == $44 or == $2F

   One iteration of the loop is EIGHT columns: four through road_span_plot into one buffer,
   then the source block steps, then four through road_span_plot_2 into the other.  The
   shallow arms close each half with its end marker; the steep arms have none.  The two
   descending arms fall into the surface cap at $2F12 when the walk finishes.

   ⭐⭐ THE ENTRY IS A COMPUTED JUMP INTO THE MIDDLE OF THE UNROLLED CHAIN.  The sub-column
   phase (math_hi & 7) indexes the arm's entry-offset table, the byte is written over the
   chain's own `BCC` operand, and the `CLC` in front of that branch makes it unconditional:
   "start at sub-column k".  It is the standard way to run a partial unrolled loop without a
   counter, and it applies to the FIRST iteration only — every later one starts at the top.

   ⚠ DECLARED COVERAGE LIMIT, and it is derived rather than assumed: the twin recognises the
   chain top and the sixteen (shallow) or eight (steep) slot offsets, which is exactly the set
   the four tables in the image hold.  Those tables are static — `LDA table,X` at $2D17 /
   $2D9A / $2E20 / $2E99 are the only references to them in the whole listing, and although
   they sit inside the $80-spaced source blocks they sit in the blocks' TAILS (offset $50 and
   $58), above the $50 scan lines the span plotters can reach through ($70),Y.  Anything else
   goes to platform_smc_unhandled, exactly as an unrecognised opcode slot does.
   --------------------------------------------------------------------------- */

/* Where an entry offset may land, relative to the chain's own base.  Both shallow arms share
   one pair of tables and both steep arms share one: the two mirrors have identical layouts. */
static const uint8_t SHALLOW_DDA_OFF[8] = { 0x02, 0x0D, 0x18, 0x23, 0x33, 0x3E, 0x49, 0x54 };
static const uint8_t SHALLOW_COL_OFF[8] = { 0x08, 0x13, 0x1E, 0x29, 0x39, 0x44, 0x4F, 0x5A };
static const uint8_t STEEP_COL_OFF[8]   = { 0x00, 0x0B, 0x16, 0x21, 0x2E, 0x39, 0x44, 0x4F };


static const SpanArm ARM_SHALLOW_FWD = { 0x3E50u, 0x2D28u, 0x2D29u, SPAN_DY, SPAN_DX, 0, 0, 0x44u };
static const SpanArm ARM_SHALLOW_REV = { 0x40D0u, 0x2DABu, 0x2DACu, SPAN_DY, SPAN_DX, 1, 0, 0x2Fu };
static const SpanArm ARM_STEEP_FWD   = { 0x3ED0u, 0x2E2Fu, 0x2E30u, SPAN_DX, SPAN_DY, 0, 1, 0x44u };
static const SpanArm ARM_STEEP_REV   = { 0x3ED8u, 0x2EA8u, 0x2EA9u, SPAN_DX, SPAN_DY, 1, 1, 0x2Fu };

/* Decode a patched entry offset into "start at column c", plus whether the DDA test for that
   first column is skipped (the offset named its `LDX #k` slot) and whether the chain's own
   top runs first.  Returns 0 for an offset the chain cannot mean. */
static int span_entry_decode(const SpanArm* arm, uint8_t offset,
                             int* column, int* forced, int* runTop)
{
    int i;

    *column = 0; *forced = 0; *runTop = 0;

    if (arm->steep) {
        for (i = 0; i < 8; i++)
            if (offset == STEEP_COL_OFF[i]) { *column = i; *forced = 1; return 1; }
    } else {
        if (offset == 0x00) { *runTop = 1; return 1; }     /* the `LDX #$80` at the top */
        for (i = 0; i < 8; i++) {
            if (offset == SHALLOW_COL_OFF[i]) { *column = i; *forced = 1; return 1; }
            if (offset == SHALLOW_DDA_OFF[i]) { *column = i; return 1; }
        }
    }
    /* ⚠ The trap reports the computed TARGET, not the offset byte — that is what the
       transliteration's switch has in hand at the same point, and the harness diffs it. */
    platform_smc_unhandled(arm->operand - 1, (uint16_t)(arm->base + (int8_t)offset));
    return 0;
}

/* $2F12-$2F18 — the two descending arms' shared exit: replay the plotters' Y step once more
   (the opcode is copied out of road_span_plot's own entry slot) and cap the run's last line. */
static void span_walk_cap(uint8_t y)
{
    mem[SLOT_STEP_CAP] = mem[SLOT_STEP_P1_IN];   /* copy the plotter's own Y-step opcode */
    if (!span_step_y(SLOT_STEP_CAP, &y)) return;
    span_cap_line(y);
}

/* ⭐ THE LEAVES OF THE SPAN WALK, all pure C now — no cpu register or flag carries state across
   a statement here or in span_walk below.  These three adapters just pick the plotter/marker
   descriptor from the arm's direction and relay span_walk's own locals by pointer; the leaf
   cores (span_plot_core / span_end_marker / span_walk_cap) own the actual work.  Their 6502-ABI
   shims (road_span_plot, span_end_marker_p1, …) still exist for the draw_span_*__t6502 oracles
   to call, so this seam changes no drawn byte.
   What span_walk needs BACK from a plot is three facts: the scan line the plotter stepped to
   (y), whether the chain abandoned, and the plotter's EXIT CARRY — which is 0 on the ordinary
   path but not on the block-first-line / trapped-step exits, and the DDA feeds it straight into
   the next add (the 6502 did `ADC` right after the plot with the plotter's C still live). */
static void sw_plot(int usePlot2, uint8_t acc, uint8_t column,
                    uint8_t *y, int *abandoned, unsigned *carry)
{
    span_plot_core(usePlot2 ? &SPAN_PLOT_2 : &SPAN_PLOT_1, acc, column, y, carry, abandoned);
    /* The abandon path set the two-level-return flag (so the draw_span oracle sees it too);
       clear it here, once per plot, exactly as the oracle's `if (UNWIND_TAKEN()) return` does. */
    if (*abandoned) (void)span_chain_abandoned();
}

/* The half/end markers test colMark against $80 ("this column plotted nothing") and reset it,
   so colMark is in/out; the marker's exit carry is the carry-in to the next column's DDA add
   (0 when it did work, unchanged when its slot is RTS), so it is threaded too. */
static void sw_marker(int p2, uint8_t *colMark, uint8_t y, unsigned *carry)
{
    span_end_marker(p2 ? SLOT_MARKER_P2 : SLOT_MARKER_P1,
                    p2 ? MEM_plot_ptr2_lo : MEM_plot_ptr_lo, y, colMark, carry);
}

/* The descending arms' shared exit: cap the last line at the scan line y left off on. */
static void sw_walk_cap(uint8_t y) { span_walk_cap(y); }

/* Inlined so `arm->rev` and `arm->steep` become constants and the four specialisations lose
   the direction tests from their inner loops.  The DDA is a plain binary fixed-point walk:
   `acc` is the fractional accumulator (a byte), `carry` its carry between columns, both pure
   C.  ⭐ This is legal because the render path is only ever entered with D=0 (docs/static-map
   §Decimal mode: no SED on build_track_geometry→draw_road), so the 6502 byte adc/sbc here
   computed nothing a binary add/subtract does not. */
static inline __attribute__((always_inline))
void span_walk(const SpanArm *arm, uint8_t phase, uint8_t startLine)
{
    int col, forced, runTop, first = 1;
    uint8_t y = startLine;              /* the scan line the plotters step through Y */
    uint8_t colMark = 0x80u;            /* "this column has plotted nothing yet" (shallow) */
    uint8_t acc;
    unsigned carry;
    int abandoned;

    /* Read this sub-column phase's entry offset and write it over the chain's branch operand.
       ⚠ That store is a real mem[] write and the differential sees it, so it stays even
       though the twin then decodes the offset rather than executing it. */
    mem[arm->operand] = mem[arm->table + phase];

    /* The accumulator starts at MINUS the delta the DDA gives back, so the first carry is
       what lands the first pixel:  ~sub + 1 == -sub. */
    acc   = (uint8_t)(0u - (unsigned)mem[arm->subtrahend]);
    carry = 0;

    if (!span_entry_decode(arm, mem[arm->operand], &col, &forced, &runTop)) return;

    for (;;) {
        ROAD_COUNT(g_roadSpanLines);        /* one DDA scan line of this span */
        int startCol   = first ? col : 0;
        int force      = first && forced;   /* a computed entry plots its first column whole */
        int midAllowed = (startCol < 4);    /* ...and skips the half boundary if it is past it */
        int i;

        /* ⚠ The shallow arms only.  A steep arm reaches its own top with the column mark
           holding the last column it plotted, and nothing reads it before the next set — so
           resetting it there would be observationally identical, and the sabotage that does
           it PASSES.  The shallow arms are different because their end markers test it. */
        if (!arm->steep && (!first || runTop)) colMark = 0x80u;

        for (i = startCol; i < 8; i++) {
            int column   = arm->rev ? 3 - (i & 3) : (i & 3);
            int usePlot2 = arm->rev ? (i < 4) : (i >= 4);

            /* The half boundary: close the first buffer's run and step the source block.
               Skipped when the entry landed past it, which is the whole point of the
               computed entry. */
            if (i == 4 && midAllowed) {
                if (!arm->steep) sw_marker(arm->rev ? 0 : 1, &colMark, y, &carry);
                mem[SPAN_BLOCK] = (uint8_t)(mem[SPAN_BLOCK] + (arm->rev ? -1 : 1));
            }

            if (arm->steep) {
                /* Y-major: the same column, again and again, until the DDA carries.  The plot
                   comes first, and its exit carry (normally 0) is the carry-in to the adc — so
                   the line's CPX carry-in is overwritten before it is used, as on the 6502. */
                for (;;) {
                    sw_plot(usePlot2, acc, (uint8_t)column, &y, &abandoned, &carry);
                    if (abandoned) return;
                    { unsigned s = (unsigned)acc + mem[arm->addend] + carry;
                      acc = (uint8_t)s; carry = s >> 8; }
                    if (carry) break;
                }
                { int d = (int)acc - mem[arm->subtrahend] - (int)(1u - carry);
                  acc = (uint8_t)d; carry = (d >= 0); }
            } else {
                /* X-major: one column per step, and only a carry lands a pixel.  The very
                   first column of a computed entry is plotted unconditionally. */
                if (force) force = 0;
                else {
                    { unsigned s = (unsigned)acc + mem[arm->addend] + carry;
                      acc = (uint8_t)s; carry = s >> 8; }
                    if (!carry) continue;
                    { int d = (int)acc - mem[arm->subtrahend] - (int)(1u - carry);
                      acc = (uint8_t)d; carry = (d >= 0); }
                }
                colMark = (uint8_t)column;
                sw_plot(usePlot2, acc, (uint8_t)column, &y, &abandoned, &carry);
                if (abandoned) return;
                /* carry is now the plotter's exit carry, the carry-in to the next column's adc */
            }
        }

        if (!arm->steep) sw_marker(arm->rev ? 1 : 0, &colMark, y, &carry);

        /* One scan line down (or up): the three screen pointers and the source block move
           together, and plot_ptr2_hi is the one the bound is measured on. */
        if (arm->rev) { plot_ptr2_hi--; plot_ptr_hi--; plot_ptr3_hi--; mem[SPAN_BLOCK]--; }
        else          { plot_ptr2_hi++; plot_ptr_hi++; plot_ptr3_hi++; mem[SPAN_BLOCK]++; }

        if (plot_ptr2_hi == arm->bound) break;                  /* CPX #bound, on Z */
        carry = arm->rev ? 0u : (plot_ptr2_hi >= arm->bound);   /* the CPX carry into next line
                                                                   ($2E1A/$2F0F clear it for rev) */
        first = 0;
    }

    if (arm->rev) sw_walk_cap(y);       /* $2D9A's `JMP $2F12`, and $2E99's fall-through */
}

void draw_span_shallow_fwd(void) { span_walk(&ARM_SHALLOW_FWD, cpu.X, cpu.Y); }
void draw_span_shallow_rev(void) { span_walk(&ARM_SHALLOW_REV, cpu.X, cpu.Y); }
void draw_span_steep_fwd(void)   { span_walk(&ARM_STEEP_FWD,   cpu.X, cpu.Y); }
void draw_span_steep_rev(void)   { span_walk(&ARM_STEEP_REV,   cpu.X, cpu.Y); }

/* ---------------------------------------------------------------------------
   $2B26  interp_edge  (twin #35)
   ---------------------------------------------------------------------------
   THE SPAN RASTERISER'S SETUP, and the routine that decides everything the four arms and the
   two plotters then do.  draw_surface_spans calls it once per span with the span's two
   endpoints: X names the point in one edge half, Y the point in the other, A the surface
   style, and the CARRY means "no span — just publish this endpoint and return".

   WHAT IT COMPUTES, in order:

     1. THE CLIP BIT.  One bit per call, rotated into span_clip's top: set when the endpoint
        is off the bottom of the view or more than $14 off axis.  Bit 7 is this call's, bit 6
        the previous call's, so the pair says whether the SPAN has two usable ends — which is
        the whole reason the cell is a shift register rather than a flag.

     2. THE ENDPOINT.  edge_x_hi:edge_x_lo shifted left twice with $80 added to the high byte
        (shared_temp_77), and the point's scan line (span_line_end).  Both are published into
        shared_temp_7e / span_line_cursor on the way out, so the NEXT span starts where this
        one ended.

     3. THE TWO DELTAS.  span_dy is |end line - start line|.  span_dx is either the difference
        between the two endpoints' angles, normalised left until its top bit pair is in range
        (and span_dy shifted down to match), or — when one end was clipped — simply the
        difference between the two x positions.  Their comparison picks X-major or Y-major.

     4. THE PIXELS.  Four bytes of the style record become colour_pattern_tbl and, masked,
        colour_pattern_or_tbl; the two per-line surface codes are composed from them and the
        pass number; and bearing_hi — the byte the plotters copy alongside every pixel — is
        the record's first byte, with $00 read as "all four columns" ($55).

     5. THE SELF-MODIFICATION.  Five Y-step slots, two end-marker opcode slots.  The step
        direction comes from the sign of span_ystep; the end markers are switched OFF when the
        pattern is solid ($FF) or the far surface code's low bits are 3, because a solid run
        needs no terminator.  ⭐ The `LDA $2F60 / STA $2F47` shuffle at $2CC5 MOVES the step
        from the exit slot to the entry slot for one arm's worth of walking, which is how the
        first plotted column lands on the right line.

     6. THE SCREEN POINTERS.  All three come from the endpoint's high byte: (x - $30) >> 2 is
        the source block, >> 3 plus $30 is the page, and plot_ptr3 is one page above.  A block
        index of $28 or more means the span is off the side and the routine publishes and
        returns.

   ⭐ THE CARRY IS AN ARGUMENT, NOT A FLAG.  The 6502 does PHP on entry purely to capture the
   caller's carry ("publish this endpoint without drawing"), and the twin takes it as the
   `publishOnly` parameter instead — so nothing in this routine reads or writes a cpu flag
   except the ONE irreducible handoff to the span plotters, which are separate 6502-ABI twins
   that read the entry sub-column phase in X and the start line in Y.  The 6502 also captured
   the caller's N/Z/V in that same PHP byte, but they are dead the instant the first internal
   test overwrites them, so the twin ignores them and its exit registers/flags are ALL dead
   (validate_native.c declares interp_edge LIVE_NONE).  The only mem[] the removed PHP would
   have written is one 6502-stack byte the oracle still writes; the fixture ignores it.
   --------------------------------------------------------------------------- */

/* The far/near endpoint indices interp_edge hands back to its caller.  The 6502 left them in
   X and Y ($2D05/$2D08) — not a computed result but the caller's OWN input indices, which the
   convention keeps in place so the caller can step them.  The native caller
   (draw_surface_spans_core) tracks x/y in its own locals and IGNORES this return; only the
   transliterated oracle (draw_surface_spans__t6502) does INX/INY on them, so the interp_edge
   SHIM marshals these two fields back into cpu.X/cpu.Y for that oracle's benefit. */

/* $2B26's three exits all run the same tail: publish this endpoint for the next span unless the
   endpoints were swapped, then report the two indices. */
static EdgeIndices interp_edge_publish(void)
{
    EdgeIndices r;
    if (!(span_swapped & 0x80u)) {          /* $2CFC — not swapped: carry the endpoint forward */
        shared_temp_7e   = shared_temp_77;
        span_line_cursor = mem[SPAN_LINE_END];
    }
    r.farIdx  = saved_slot_index;           /* $2D05 — the caller's far index */
    r.nearIdx = span_saved_index;           /* $2D08 — ...and the near one, for its INX/INY */
    return r;
}

EdgeIndices interp_edge_core(uint8_t styleIndex, uint8_t farPoint, uint8_t nearPoint,
                                    int publishOnly)
{
    unsigned x;
    int i;

    ROAD_COUNT(g_roadSpans);                /* one span pair handed to the rasteriser */
    surface_style_index = styleIndex;
    span_swapped        = 0;

    /* 1 — the clip bit, rotated into span_clip's top: set when the endpoint is off the bottom
       of the view or more than $14 off axis. */
    {
        uint8_t clipBit;
        uint8_t line = (uint8_t)(mem[EDGE_Y_TBL + nearPoint] - 1u);
        if (line >= 0x4Eu) {
            clipBit = 1;
        } else {
            uint8_t angle = mem[EDGE_X_HI_TBL + farPoint];
            if (angle & 0x80u) angle ^= 0xFFu;      /* |angle|, near enough for a clip test */
            clipBit = (angle >= 0x14u) ? 1u : 0u;
        }
        mem[SPAN_CLIP] = (uint8_t)((clipBit << 7) | (mem[SPAN_CLIP] >> 1));
    }

    /* 2 — the endpoint, as a 10-bit x biased by $80 in the high byte. */
    x = (unsigned)((mem[EDGE_X_HI_TBL + farPoint] << 8) | mem[EDGE_X_LO_TBL + farPoint]);
    shared_temp_77 = (uint8_t)(((x << 2) >> 8) + 0x80u);
    mem[SPAN_LINE_END] = mem[EDGE_Y_TBL + nearPoint];
    saved_slot_index   = farPoint;
    span_saved_index   = nearPoint;
    if (publishOnly) return interp_edge_publish();

    /* Both ends have to be usable.  Bit 6 clear means the PREVIOUS point was on screen and
       this one starts a span; bit 6 set with bit 7 set means neither is. */
    if (mem[SPAN_CLIP] & 0x40u) {
        if (mem[SPAN_CLIP] & 0x80u) return interp_edge_publish();
        /* $2B69 — walk from the previous endpoint to this one instead. */
        { uint8_t px = shared_temp_7e, pl = span_line_cursor;
          shared_temp_7e     = shared_temp_77;
          span_line_cursor   = mem[SPAN_LINE_END];
          shared_temp_77     = px;
          mem[SPAN_LINE_END] = pl; }
        span_swapped--;                     /* $FF */
    }

    /* 3 — the two deltas.  span_dy is |end line - start line|. */
    mem[SPAN_YSTEP] = (uint8_t)(mem[SPAN_LINE_END] - span_line_cursor);
    { uint8_t dy = mem[SPAN_YSTEP];
      if (dy & 0x80u) dy = (uint8_t)(0u - dy);
      mem[SPAN_DY] = dy; }

    if (mem[SPAN_CLIP] & 0xC0u) {
        /* Both ends on screen: dx is the angle difference (a plain signed 16-bit subtract of
           the two endpoints' x), normalised left until its top byte is below $40, with span_dy
           shifted down by as much.  The PRE-abs high byte selects the plotter arm. */
        /* ⚠ The 6502 loads the two point indices into Y and X ($2B91) and does this in
           byte-pair arithmetic; both registers are reloaded before anything reads them (the
           phase into X at $2C92, the start line into Y at $2C93), so the twin uses a uint16_t. */
        uint16_t vSaved = (uint16_t)((mem[EDGE_X_HI_TBL + saved_slot_index] << 8)
                                     | mem[EDGE_X_LO_TBL + saved_slot_index]);
        uint16_t vFar   = (uint16_t)((mem[EDGE_X_HI_TBL + span_index_far] << 8)
                                     | mem[EDGE_X_LO_TBL + span_index_far]);
        uint16_t dxRaw  = (uint16_t)(vSaved - vFar);
        uint8_t  armHi  = (uint8_t)(dxRaw >> 8);           /* pre-abs high byte → arm select */
        uint16_t adx    = (dxRaw & 0x8000u) ? (uint16_t)(0u - dxRaw) : dxRaw;

        int giveBack = 2;
        if ((adx >> 8) < 0x40u) {
            adx <<= 1;
            if ((adx >> 8) >= 0x40u) giveBack = 1;
            else { adx <<= 1; giveBack = ((adx >> 8) & 0x80u) ? 2 : 0; }
        }
        math_lo = (uint8_t)adx;             /* the 6502's ASL_M leaves the shifted low byte here */
        while (giveBack--) mem[SPAN_DY] >>= 1;

        mem[SPAN_DX]  = (uint8_t)(adx >> 8);
        mem[SPAN_ARM] = (uint8_t)(armHi ^ span_swapped);
    } else {
        /* One end clipped: dx is just how far the x moved, and the subtract's borrow (carry)
           becomes span_arm's new top bit (the arm-select).  The x-move is negated to |dx| when
           that borrow occurred. */
        uint8_t carry = (shared_temp_7e >= shared_temp_77) ? 1u : 0u;
        uint8_t dxv   = (uint8_t)(shared_temp_7e - shared_temp_77);
        mem[SPAN_ARM] = (uint8_t)((carry << 7) | (mem[SPAN_ARM] >> 1));
        if (!carry) dxv = (uint8_t)(0u - dxv);
        mem[SPAN_DX]  = dxv;
    }
    if (mem[SPAN_DX] == 0 && mem[SPAN_DY] == 0) return interp_edge_publish();  /* no extent */

    /* Does the abandon path stamp a surface code?  Only when both ends were usable. */
    if (mem[SPAN_CLIP] & 0xC0u)
        span_cap_pending = (uint8_t)(mem[SPAN_ARM] & 0x80u);
    else
        span_cap_pending = (uint8_t)(mem[SPAN_CLIP] & 0xC0u);

    /* A zero line delta borrows its direction from the swap flag. */
    if (mem[SPAN_YSTEP] == 0)
        mem[SPAN_YSTEP] = (uint8_t)(span_swapped ^ 0xFFu);

    /* 5a — the Y step the plotters take on the way OUT; the entry slots stay NOP for now. */
    { uint8_t step = (mem[SPAN_YSTEP] & 0x80u) ? OP_DEY : OP_INY;
      mem[SLOT_STEP_P1_OUT] = step;
      mem[SLOT_STEP_P2_OUT] = step;
      mem[SLOT_STEP_P1_IN]  = OP_NOP;
      mem[SLOT_STEP_P2_IN]  = OP_NOP; }

    /* 4 — the style record becomes this span's four column patterns. */
    for (i = 0; i < 4; i++) {
        uint8_t pat = mem[SURFACE_STYLE_TBL + (uint8_t)(surface_style_index + i)];
        mem[COLOUR_PATTERN + i]    = pat;
        mem[COLOUR_PATTERN_OR + i] = (uint8_t)(pat & mem[COLOUR_PATTERN_KEEP + i]);
    }

    math_lo = (uint8_t)(surface_pass_index << 3);        /* the pass, in bits 3-5 */
    span_cap_surface_a = (uint8_t)(((mem[COLOUR_PATTERN] >> 3) & 3) | math_lo | 0x40u);

    if (mem[COLOUR_PATTERN] == 0) mem[COLOUR_PATTERN] = 0x55u;
    bearing_hi = mem[COLOUR_PATTERN];

    { uint8_t p3 = mem[COLOUR_PATTERN + 3];
      uint8_t code = (uint8_t)((p3 >> 1) & 1u);
      if (p3 & 0x80u) code |= 2u;                        /* $2C4C BIT — bit 7 through N */
      span_cap_surface_b = (uint8_t)(code | 0x80u | math_lo); }

    /* The LAST span of a pass is clamped to the top or the bottom of the view rather than to
       its own end line, so the walk always terminates on a real scan line. */
    if ((uint8_t)(span_saved_index + 1) == span_end_index || mem[SPAN_LINE_END] >= 0x50u)
        mem[SPAN_LINE_END] = (mem[SPAN_YSTEP] & 0x80u) ? 0x00u : 0x4Fu;

    /* 6 — the three screen pointers and the source block, all from the endpoint's x. */
    math_hi = (uint8_t)(shared_temp_7e - 0x30u);
    { uint8_t block = (uint8_t)(math_hi >> 2);
      mem[SPAN_BLOCK] = block;
      if (block >= 0x28u) return interp_edge_publish();   /* off the side */
      /* The three screen pages, from the endpoint's block: (block >> 1) + $30, and plot_ptr3
         one page above. */
      { uint8_t page = (uint8_t)((block >> 1) + 0x30u);
        plot_ptr_hi  = page;
        plot_ptr2_hi = page;
        plot_ptr3_hi = (uint8_t)(page + 1u); } }

    /* The span walk takes the entry sub-column phase and the start line as plain C arguments. */
    uint8_t phase     = (uint8_t)(math_hi & 7u);   /* the sub-column phase, into the entry tables */
    uint8_t startLine = span_line_cursor;

    if (mem[SPAN_DX] >= mem[SPAN_DY]) {
        /* X-MAJOR.  A solid run needs no terminator, so the two end markers are switched off
           by planting RTS over their first byte. */
        int wantMarkers = (mem[COLOUR_PATTERN] == 0xFFu)
                       || ((span_cap_surface_b & 3u) == 3u);
        if (!wantMarkers) {
            mem[SLOT_MARKER_P2] = OP_RTS;
            mem[SLOT_MARKER_P1] = OP_RTS;
        } else {
            mem[SLOT_MARKER_P2] = OP_CPX_IMM;
            mem[SLOT_MARKER_P1] = OP_CPX_IMM;
            /* $2CBC — for two of the four passes the step has to happen on the way IN
               instead, and Y is nudged to match.  The 6502 does CMP #2 / ROR A / EOR span_arm
               and branches on the result's bit 7. */
            uint8_t pv  = surface_pass_index;
            uint8_t cin = (pv >= 0x02u) ? 1u : 0u;                  /* CMP #2 carry */
            uint8_t sel = (uint8_t)(((cin << 7) | (pv >> 1)) ^ mem[SPAN_ARM]);
            if (sel & 0x80u) {
                uint8_t step = mem[SLOT_STEP_P1_OUT];
                mem[SLOT_STEP_P1_IN]  = step;
                mem[SLOT_STEP_P2_IN]  = step;
                mem[SLOT_STEP_P1_OUT] = OP_NOP;
                mem[SLOT_STEP_P2_OUT] = OP_NOP;
                if (mem[SPAN_YSTEP] & 0x80u) startLine++; else startLine--;
            }
        }
        if (mem[SPAN_ARM] & 0x80u) span_walk(&ARM_SHALLOW_REV, phase, startLine);
        else                       span_walk(&ARM_SHALLOW_FWD, phase, startLine);
    } else {
        if (mem[SPAN_ARM] & 0x80u) span_walk(&ARM_STEEP_REV, phase, startLine);
        else                       span_walk(&ARM_STEEP_FWD, phase, startLine);
    }

    return interp_edge_publish();
}

/* The 6502-ABI shim.  A is the style record's index, X the endpoint in one edge half, Y the
   endpoint in the other, and the CARRY is "publish this endpoint without drawing a span".  On
   exit the 6502 leaves the far/near indices in X/Y (the oracle's INX/INY read them), so marshal
   the returned pair back there. */

/* ---------------------------------------------------------------------------
   $1933 edge_x_offscreen (twin #36) and $193E fill_line_attr (twin #37)
   ---------------------------------------------------------------------------
   draw_road's FIRST stage, run once per road side: it turns the side's edge points into a
   PER-SCAN-LINE MAP.  line_attr_0 and line_attr_1 hold, for every scan line, the index of the
   edge point that covers it — which is exactly what surface_colour_at reads them for.

   The walk climbs the edge list from the horizon point; each point fills every line from the
   previous point's line down to its own.  Three things end a point instead:
     * a line at or past $50 — off the bottom, so the point is CLAMPED (its edge_y is pulled
       back to the cursor) and the walk finishes with the remaining lines filled from a MARKED
       index, whose bit 7 also terminates the loop on the next increment;
     * a point whose line has not moved, when the previous point was off axis;
     * a point that would fill upward rather than down.
   The last two set bit 7 of the point's edge_style, and the tail then advances
   road_split_index past every marked point.

   ⚠ TWO SELF-MODIFYING SITES.  $1946 is a per-circuit extent — Silverstone calls
   edge_x_offscreen, an expansion circuit calls its own hook instead.  $1970 is the store's
   own low operand byte, which is how ONE loop serves both line_attr buffers: draw_road passes
   $00 or $50 in A and the routine writes it into the instruction.
   --------------------------------------------------------------------------- */

/* $1933 — "is edge point X more than $14 off the road centre?"  Computes
   (edge_x_hi[X] + $14) >= $28 and rolls that answer into shared_temp_76's top bit; the cell's
   previous top bit slides down to bit 6, which fill_line_attr reads back as the PREVIOUS
   point's answer.
   ⚠ This is a FLAG PRODUCER — its whole output is a mem write plus four escaping flags, so it
   returns them BY VALUE (the caller replays them; the shim marshals them into cpu).  The add's
   value/overflow come from the pure adc_value/adc_overflow helpers, which honour decimal mode
   exactly as the ADC macro does (the fixture randomises D); the CMP is always binary and the ROR
   is spelled out by hand.  Escapes: A = the add's sum (draw_road never reads it, but the
   standalone fixture compares it); V = the add's signed overflow (draw_road's exit V on the mark
   path); C = the byte rolled OUT (old bit 0); N = the rolled-in answer; Z = "cell now zero". */

EdgeOffFlags edge_x_offscreen_core(uint8_t pointX)
{
    uint8_t edgeHi = mem[EDGE_X_HI_TBL + pointX];
    uint8_t sum    = adc_value(edgeHi, 0x14u, 0).val;    /* + $14 (decimal-aware value) */
    uint8_t v      = adc_overflow(edgeHi, 0x14u, 0);     /* ...its overflow, replayed pure */
    uint8_t carry  = (uint8_t)(sum >= 0x28u);            /* CMP #$28 — always binary */

    uint8_t old  = shared_temp_76;                       /* ROR shared_temp_76 */
    uint8_t newv = (uint8_t)((carry << 7) | (old >> 1));
    shared_temp_76 = newv;

    EdgeOffFlags e = { sum, v, (uint8_t)(old & 1u), carry, (uint8_t)(newv == 0) };
    return e;
}

/* The 6502-ABI shim: the edge point index arrives in X (unchanged to exit). */

#define LINE_ATTR_OPERAND 0x1970u   /* the patched low byte of `STA line_attr,Y` */

/* Idiomatic C: the walk runs on local variables and plain math.  The routine's flags DO leave
   it (the differential checks A/X/Y and every flag), but they are all recovered at the end from
   the walk's final state — no cpu.h operation drives the body.  The two genuine flag PRODUCERS
   it leans on stay as helpers: edge_x_offscreen_core (its V escapes) and the roll it performs.

   Two escaping flags need explaining:
     · V — set only by edge_x_offscreen_core and by the "previous point off axis?" BIT test, both
       reads of shared_temp_76's bit 6/7.  We mirror BIT's V by hand and take the ADD's V from
       the helper, tracking the last one in `vFlag`.
     · C — the tail leaves it set (its CPX) unless it returns on the very first LDA, in which case
       C is whatever the WALK left.  Every completed iteration ends on a fill or a skip, and both
       leave carry set, so the walk's exit carry is 1 whenever any iteration ran; only a walk that
       breaks on its first step (a start index already at/over $80, which draw_road never passes)
       carries the SMC helper's carry through.  `completedAny` distinguishes the two. */
SlotExit fill_line_attr_core(uint8_t bufferLow, uint8_t endCursor, uint8_t firstPoint,
                                    int entryC, int entryV)
{
    mem[LINE_ATTR_OPERAND] = bufferLow;      /* $0400 or $0450 — the store's own operand */
    span_end_index = endCursor;

    uint8_t onePastLast = (uint8_t)(endCursor - 1);   /* $1943 DEY — one past this half's last point */
    math_hi = onePastLast;

    /* Exit ABI for the SMC-trap early return below: the $1943 DEY leaves Y and its N/Z, X keeps
       the start index, and A/C/V are untouched (A = the entry buffer-low, C/V the caller's). */
    SlotExit trapExit = { bufferLow, firstPoint, onePastLast,
                          (uint8_t)((onePastLast >> 7) & 1u), (uint8_t)(onePastLast == 0),
                          (uint8_t)entryV, (uint8_t)entryC };

    /* $1946 — Silverstone's own `JSR edge_x_offscreen`, or a circuit's hook in its place.  Both
       are 6502-ABI shims that read the start index in X, so seed it — the one genuine hook-seam
       cpu access left in this routine. */
    cpu.X = firstPoint;
    if (mem[0x1946] == 0x20) {
        uint16_t target = (uint16_t)(mem[0x1947] | (mem[0x1948] << 8));
        if (target == 0x1933) edge_x_offscreen();
        else if (target >= 0x5300 && target <= 0x5A25) revs_track_hook(target);
        else { platform_smc_unhandled(0x1946, target); return trapExit; }
    } else {
        platform_smc_unhandled(0x1946, mem[0x1946]);
        return trapExit;
    }

    uint8_t x = cpu.X;                        /* a circuit hook may have moved the start index */
    uint8_t y = horizon_extent;              /* $1949/$1977 — the first scan line to fill from */
    span_line_cursor = y;

    int vFlag = cpu.V;                        /* last V produced by the SMC helper (edge_x_offscreen) */
    int smcCarry = cpu.C;                     /* ...and its carry, the walk's exit carry if nothing runs */
    int completedAny = 0;

    for (;;) {
        int clamped;
        uint8_t fillDownTo = 0, storeVal;

        x = (uint8_t)(x + 1);                /* $1979 — the next edge point */
        if (x & 0x80u) break;                /* a marked index (bit 7): the side is finished */
        clamped = (x >= onePastLast);        /* $197C CPX — walked off the end of the half */

        if (!clamped) {
            /* $194E — re-test this point's angle only when the last one was off axis. */
            if (shared_temp_76 & 0x80u) { vFlag = edge_x_offscreen_core(x).v; }

            uint8_t ptLine = mem[EDGE_Y_TBL + x];     /* the scan line this point projects to */
            if (ptLine >= 0x50u) {
                clamped = 1;                          /* $195A — off the bottom of the screen */
            } else {
                /* $195C BIT — its V escapes, so mirror it; bit 7 = "previous point off axis". */
                int prevOffAxis = (shared_temp_76 & 0x80u) != 0;
                vFlag = (shared_temp_76 >> 6) & 1u;
                if (prevOffAxis && ptLine == mem[EDGE_Y_TBL + x + 1]) {
                    /* previous point off axis + this one on the SAME line ⇒ nothing to add */
                    mem[EDGE_STYLE_TBL + x] |= 0x80u; completedAny = 1; continue;
                }
                if (ptLine >= span_line_cursor) {
                    /* a point that would fill UPWARD from the cursor adds nothing either */
                    mem[EDGE_STYLE_TBL + x] |= 0x80u; completedAny = 1; continue;
                }
                fillDownTo = ptLine;                  /* $1969 — fill down to this point's line */
            }
        }

        if (clamped) {
            /* $1980 — pull the point's line back to the cursor, mark the index, and fill
               everything that is left down to line 0. */
            uint8_t ey = mem[EDGE_Y_TBL + x];
            if (!(ey & 0x80u) && ey >= span_line_cursor)
                mem[EDGE_Y_TBL + x] = span_line_cursor;
            x |= 0x80u;                        /* mark the index; bit 7 ends the walk */
            fillDownTo = 0x00u;
        }

        /* $1969 — every line from the cursor down to fillDownTo names this point (index x). */
        storeVal = x;
        mem[SPAN_LINE_END] = fillDownTo;
        {
            /* The store operand cannot move under us: line_attr is $04xx and the operand $1970,
               so compute the base once. */
            uint16_t base = (uint16_t)(mem[LINE_ATTR_OPERAND]
                                       | (mem[LINE_ATTR_OPERAND + 1] << 8));
            while (y != fillDownTo) {
                ROAD_COUNT(g_roadFillLines);   /* one scan line named in the line->point map */
                bus_write((uint16_t)(base + y), storeVal);
                y = (uint8_t)(y - 1);
            }
        }
        span_line_cursor = y;
        completedAny = 1;
    }

    int walkCarry = completedAny ? 1 : smcCarry;   /* the walk's exit carry (see the header) */

    /* $1996 — leave road_split_index past any point the walk had to mark. */
    uint8_t sx = road_split_index;
    uint8_t tailStyle;
    int exitViaCpx = 0;
    int tailLooped = 0;                             /* a CPX left carry clear and we continued */
    for (;;) {
        tailStyle = mem[EDGE_STYLE_TBL + sx];       /* $1998 LDA */
        if (!(tailStyle & 0x80u)) { exitViaCpx = 0; break; }
        sx = (uint8_t)(sx + 1);                     /* $199D INX */
        if (sx >= (uint8_t)math_hi) { exitViaCpx = 1; break; }   /* $199E CPX, carry set */
        tailLooped = 1;                             /* $19A0 BCC — that CPX's carry (0) is now live */
    }
    road_split_index = sx;

    /* Exit ABI — the differential compares A/X/Y and every flag, so hand back exactly what the
       6502 leaves at $19A4: X and A from the tail, Y from the walk's final cursor, V carried
       from the last BIT / edge_x_offscreen, and N/Z/C from whichever tail op ended it. */
    SlotExit e;
    e.a = tailStyle;
    e.x = sx;
    e.y = y;
    e.v = (uint8_t)vFlag;
    if (exitViaCpx) {
        uint8_t diff = (uint8_t)(sx - (uint8_t)math_hi);
        e.n = (uint8_t)((diff >> 7) & 1u);
        e.z = (uint8_t)(sx == (uint8_t)math_hi);
        e.c = 1;                                     /* CPX carry set (sx >= math_hi) */
    } else {
        e.n = 0;                                     /* the tail exits with bit 7 of A clear */
        e.z = (uint8_t)(tailStyle == 0);
        /* the tail's LDA leaves carry untouched: it is the last CPX's (0) if the loop ran,
           else the carry the walk itself left. */
        e.c = (uint8_t)(tailLooped ? 0 : walkCarry);
    }
    return e;
}

/* The 6502-ABI shim.  A is the target buffer's low byte, Y the side's end cursor, X the point
   the walk starts from; entry C/V are echoed on the SMC-trap path. */

/* ---------------------------------------------------------------------------
   $19AF  draw_surface_spans  (twin #38)
   ---------------------------------------------------------------------------
   draw_road's SECOND stage, run four times.  It walks the two edge halves in lockstep — index
   i in one, i + span_pair_offset_tbl[pass] in the other — and hands each pair to interp_edge
   as a span.  Before the walk it patches both plotters' destination operands with this pass's
   surface_edge buffer, which is how one span plotter serves all four passes.

   ⭐ THE STYLE PER SPAN IS THE WHOLE POINT OF THE ROUTINE.  Spans NEARER than
   road_split_index all take shared_temp_8c whole; spans beyond it take 4 * (the point's own
   surface class) + surface_style_base.  The boundary case — the walk arriving exactly at
   road_split_index — either moves the split to here and publishes without drawing, or falls
   into the near arm, depending on the pass.  ⚠ The CARRY is an argument to interp_edge and
   means "publish this endpoint, draw nothing": it is set on the first call of every pass and
   on the boundary arms, and clear everywhere else.
   --------------------------------------------------------------------------- */
void draw_surface_spans_core(uint8_t pass, uint8_t firstPoint)
{
    surface_pass_index = pass;
    span_index_near    = firstPoint;

    /* $19B3 — a pass that starts at or past its own last point draws nothing. */
    if (firstPoint >= span_end_index) return;

    /* $19B8 — the far endpoint is the near one plus this pass's paired offset. */
    uint8_t offset  = mem[SPAN_PAIR_OFFSET + pass];
    span_index_far  = (uint8_t)(firstPoint + offset);

    /* $19BD..$19CE — patch both span plotters' store operands with this pass's edge buffer. */
    mem[OPERAND_DEST_P1_HI] = mem[OPERAND_DEST_P2_HI] = mem[ROW_BASE_HI + pass];
    uint8_t styleLo = mem[ROW_BASE_LO + pass];
    mem[OPERAND_DEST_P1_LO] = mem[OPERAND_DEST_P2_LO] = styleLo;

    /* x/y are the 6502's X/Y across the walk: the far and near endpoint indices interp_edge
       takes as arguments. */
    uint8_t x = span_index_far;
    uint8_t y = span_index_near;

    /* $19D4 — publish the first endpoint ("record it, draw nothing"). */
    interp_edge_core(styleLo, x, y, 1);

    for (;;) {
        x = (uint8_t)(x + 1);            /* $19D7 INX */
        y = (uint8_t)(y + 1);            /* $19D8 INY */

        /* $19D9 CPY — the walk stops when it reaches this half's end index. */
        if (y >= span_end_index) return;

        /* $19DD/$19E0 — fill_line_attr marks (bit 7) the points the walk must skip. */
        uint8_t style = mem[EDGE_STYLE_TBL + y];
        if (style & 0x80u) continue;

        /* Pick this span's style byte and whether interp_edge draws it or only records the
           endpoint (its `publishOnly` argument — the carry the 6502 handed over). */
        uint8_t near = span_index_near;   /* $19E2 — the previous span's near index */
        uint8_t styleArg;
        int publishOnly;

        if (near < road_split_index) {                /* $19E4/$19E6 — nearer than the split */
            styleArg    = shared_temp_8c;             /* one shared style, whole */
            publishOnly = 0;
        } else if (near == road_split_index) {        /* $19E8 — exactly at the split */
            uint8_t klass = (uint8_t)(mem[EDGE_STYLE_TBL + y - 1] & 0x03u);
            if (klass != 0) {                         /* $19EF — a real class: 4*class + base */
                unsigned sum = (unsigned)(klass << 2) + surface_style_base;
                styleArg    = (uint8_t)sum;
                publishOnly = (sum > 0xFFu);
            } else {
                road_split_index = y;                 /* $19F1 — the split moves to here */
                if (surface_pass_index == 0) {        /* $19F4/$19F6 — pass 0 publishes style 0 */
                    styleArg    = 0;
                    publishOnly = 1;
                } else {                              /* other passes take the shared style */
                    styleArg    = shared_temp_8c;
                    publishOnly = 0;
                }
            }
        } else {                                      /* $19E8 BNE — beyond the split */
            uint8_t klass = (uint8_t)(mem[EDGE_STYLE_TBL + y - 1] & 0x03u);
            if (klass != 0) {                         /* $1A02 — 4*class + base */
                unsigned sum = (unsigned)(klass << 2) + surface_style_base;
                styleArg    = (uint8_t)sum;
                publishOnly = (sum > 0xFFu);
            } else if (surface_pass_index == 1 || surface_pass_index == 2) {
                /* $1A06/$1A0A — passes 1 and 2 publish the pass number itself. */
                styleArg    = surface_pass_index;
                publishOnly = 1;                      /* CMP #pass ⇒ pass == pass sets carry */
            } else {                                  /* $1A0E — passes 0 and 3: base (4*0 + base) */
                unsigned sum = (unsigned)surface_style_base;
                styleArg    = (uint8_t)sum;
                publishOnly = (sum > 0xFFu);          /* base is a byte, so always 0 */
            }
        }

        /* $1A15 — interp_edge reads span_index_far (the PREVIOUS far, not x) internally, so the
           two cells are only advanced AFTER the call. */
        interp_edge_core(styleArg, x, y, publishOnly);
        span_index_near = y;             /* $1A18 */
        span_index_far  = x;             /* $1A1A */
    }
}

/* The 6502-ABI shim.  Y is the pass number, A the first edge index of the pass. */

/* ---------------------------------------------------------------------------
   $1A98  mark_line_surfaces  (twin #39)
   ---------------------------------------------------------------------------
   draw_road's THIRD stage, run once per road side: it stamps each edge point's surface class
   onto the SCAN LINE that point projects to, in view_line_surface — the array view_paint_lines
   reads for a line's background colour.

   A point is rejected when its line is off the bottom, when fill_line_attr marked it, or when
   either of its two boundaries is more than $14 off axis.  A run of marked points is skipped
   in one go.  An entry that is already there wins unless it belongs to a different class and
   the sign test at $1AF4 says this one is nearer.

   ⭐ IT RETURNS THE LIMIT, in Y: edge_y at road_split_index plus one, i.e. the scan line at
   which this side's line_attr buffer stops being valid.  draw_road stores it as
   line_attr_0_limit / line_attr_1_limit.
   ⚠ And the whole walk is SKIPPED once view_yaw_offset reaches $28 — 45 degrees off the
   section — with only that limit computed.
   --------------------------------------------------------------------------- */
#define EDGE_OPP_X_HI_TBL 0x5EA0u   /* edge_opp_x_hi — the point's other boundary */

SlotExit mark_line_surfaces_core(uint8_t surfaceClass, uint8_t firstPoint, int entryV)
{
    /* Live A: every arm leaves a different byte in it, so it is threaded and returned.  The
       pre-loop LDA seeds it, so a walk that never runs a body leaves view_yaw_offset in A. */
    uint8_t a = view_yaw_offset;
    /* Live V: the LAST +$14 the walk performs owns the exit V (INY/CPX/LDX/LDY leave V alone);
       a walk that runs no add leaves the caller's entry V untouched. */
    int v = entryV;

    mem[SPAN_CLIP] = surfaceClass;            /* ⚠ a THIRD tenant of $88 — docs/rename.md */
    math_hi        = firstPoint;              /* the walk index, and the loop's own cursor */
    span_end_index--;

    /* $1B00 CMP #$28 — the whole walk is skipped once view_yaw_offset is >= 45 deg off axis. */
    if (view_yaw_offset < 0x28u) for (;;) {
        /* $1B05 — the loop is entered at its own bottom test; X comes from math_hi, never from
           the class the caller passed in.  Its CPX carry is the loop's only exit. */
        uint8_t x = math_hi;
        if (x >= span_end_index) break;

        ROAD_COUNT(g_roadMarkPts);        /* one edge point examined for its surface class */
        {
            uint8_t y = mem[EDGE_Y_TBL + x];
            if (y < 0x50u) {
                /* A holds the style byte; bit 7 set means fill_line_attr already marked it. */
                a = mem[EDGE_STYLE_TBL + x];
                if (!(a & 0x80u)) {
                    uint8_t other;                /* the point's OTHER boundary, order per side */
                    if (mem[SPAN_CLIP] == 0x14u) {
                        other = mem[EDGE_X_HI_TBL + x];
                        a     = mem[EDGE_OPP_X_HI_TBL + x];
                    } else {
                        other = mem[EDGE_OPP_X_HI_TBL + x];
                        a     = mem[EDGE_X_HI_TBL + x];
                    }
                    shared_temp_77 = other;       /* a real mem[] store the differential sees */
                    /* Both +$14 are plain binary adds: D=0 on the road pass (docs/static-map.md
                       §Decimal mode — no SED site is on draw_road), so a uint8_t add is exact.
                       Only the escaping V is replayed, via adc_overflow. */
                    v = adc_overflow(a, 0x14u, 0);
                    a = (uint8_t)(a + 0x14u);
                    if (!(a & 0x80u)) {
                        v = adc_overflow(other, 0x14u, 0);
                        a = (uint8_t)(other + 0x14u);
                        if (a & 0x80u) {
                            int stamp;
                            uint8_t entry;

                            /* $1AD8 — skip a whole run of points fill_line_attr marked; X and
                               math_hi advance together, but y (this point's line) is unchanged. */
                            while (mem[EDGE_STYLE_TBL + x + 1] & 0x80u) {
                                x++;
                                math_hi++;
                                if (x >= span_end_index) break;
                            }

                            /* An entry already on this line wins, unless it belongs to another
                               class and the sign test says this point is the nearer one.  In the
                               not-stamped arm A holds the same byte the 6502's ROR/EOR left. */
                            entry   = mem[VIEW_LINE_SURFACE + y];
                            math_lo = entry;      /* a real mem[] store the differential sees */
                            if (entry == 0) {
                                stamp = 1;
                            } else {
                                uint8_t masked = (uint8_t)(entry & 0x1Cu);
                                if (masked == mem[SPAN_CLIP]) {
                                    stamp = 1;
                                } else {
                                    uint8_t carryIn = (masked >= mem[SPAN_CLIP]) ? 0x80u : 0x00u;
                                    a = (uint8_t)((uint8_t)(carryIn | (masked >> 1)) ^ entry);
                                    stamp = !(a & 0x80u);
                                }
                            }
                            if (stamp) {
                                a = (uint8_t)((mem[EDGE_STYLE_TBL + x] & 0x03u) | mem[SPAN_CLIP]);
                                mem[VIEW_LINE_SURFACE + y] = a;
                            }
                        }
                    }
                }
            }
        }
        math_hi++;                            /* $1B03 */
    }

    /* $1B0B — the limit, and the routine's real return value.  A stays live (last byte above),
       X ends at road_split_index, and Y — the scan-line limit — is what siblings want.  C is
       set on every exit (the CPX/CMP that got us here); INY sets N/Z from the incremented Y. */
    {
        uint8_t x = road_split_index;
        uint8_t y = (uint8_t)(mem[EDGE_Y_TBL + x] + 1u);
        SlotExit e;
        e.a = a;
        e.x = x;
        e.y = y;
        e.n = (uint8_t)((y >> 7) & 1u);
        e.z = (uint8_t)(y == 0);
        e.v = (uint8_t)(v & 1);
        e.c = 1;
        return e;
    }
}

/* The 6502-ABI shim.  X is the surface class to OR in, A the first edge index; Y comes back
   as the scan line at which this side's line_attr buffer stops being valid. */

/* ===========================================================================
   TWINS #40-#43 — THE VIEW/DASHBOARD SEAM'S OWN CALLEES
   ---------------------------------------------------------------------------
   With these four, the call tree under fill_dash_edge_columns (twin #8) has no
   transliteration left in it:

     $1DEF  fill_edge_column_run   walk a RUN of view source columns, two passes per column
     $1DA6  fill_column_gaps       ...the three-register PATCH, then the walk
     $1DAF  column_gap_walk        ...the walk itself: fill this column's empty source bytes
     $1E9E  surface_colour_at      which track surface is at (line, position), as a colour

   WHAT THE SUBSYSTEM COMPUTES.  draw_road leaves a source byte at ZERO wherever no span
   covered it, and view_paint_lines reads a zero as "same byte as the cell to my left".  At
   the two ends of the viewport — beside the front tyres and up against the dashboard — that
   is wrong: there is no cell to the left, so the gap has to be filled with the colour of
   whatever surface the road actually has at that point.  This pass walks each end column from
   its block's first scan line down to the cursor and substitutes surface_colour_at's answer
   for every zero it finds.

   ⭐ AND EACH COLUMN IS WALKED TWICE, THROUGH TWO DIFFERENT POINTERS, which is the whole
   reason fill_column_gaps is self-modifying rather than parameterised:

     pass B (plot_ptr2, offset $EF, fallback $00)  writes the per-scan-line boundary table
            view_left_start_src / view_right_start_src, and maps a $55 source byte to 0
     pass A (plot_ptr,  offset $09, fallback $55)  writes the column's own $80-byte source
            block at $3000 + column*$80, and leaves a non-zero byte alone

   The three registers ARE the patch: X is the store's zero-page pointer NUMBER ($1DDE), Y a
   BRANCH OFFSET that picks which arm a non-zero source byte takes ($1DD5), and A the fallback
   colour immediate ($1DDC).  All three sites are declared in transpile.py.

   ⚠ $1DAF has a second caller outside this tree ($1D77, the other column filler), which
   enters the walk WITHOUT patching — i.e. it runs on whatever the last fill_column_gaps left
   behind.  That is why the walk decodes the three bytes out of mem[] instead of taking them as
   arguments, and why it is a twin of its own rather than fill_column_gaps' loop body.
   =========================================================================== */

#define EDGE_RUN_LIMIT     0x0042u   /* shared_counter_42 — the column the run stops at */
#define EDGE_COLUMN        0x0085u   /* point_delta_hi[2] as this pass's column cursor */
#define EDGE_BLOCK_START   0x0082u   /* point_delta_lo[2] — dash_block_starts[column] */
#define SURFACE_EDGE_0     0x0554u   /* the four per-scan-line surface boundary buffers */
#define SURFACE_EDGE_1     0x05A4u
#define SURFACE_EDGE_2     0x0600u
#define SURFACE_EDGE_3     0x0650u
#define LINE_ATTR_0        0x0400u   /* per scan line: which edge point covers it, side 0 */
#define LINE_ATTR_1        0x0450u   /* ...and side 1 */
#define EDGE_STYLE_PREV    0x5EDFu   /* edge_style - 1: a line_attr entry is an index PLUS ONE */

#define GAP_PTR_OPERAND    0x1DDEu   /* the store's zero-page pointer number */
#define GAP_BRANCH_OPERAND 0x1DD5u   /* the non-zero-source arm's branch offset */
#define GAP_FALLBACK       0x1DDCu   /* the colour substituted for a zero surface_colour_at */

/* ⭐ ONE range test per column instead of one per cell (CLAUDE.md §bus_read/bus_write).  The
   walk touches at most $80 bytes above a pointer it reads once, so the hardware window can be
   ruled out for the whole column — but the else arm stays, because under a randomised fixture
   a pointer really can land in SHEILA. */
static int pointer_is_ram(unsigned base)
{
    return base < 0xFB01u;
}

/* The 16-bit pointer a zero-page PAIR holds — the address an `STA (zp),Y` resolves through,
   before Y is added. */



/* ===========================================================================
   $1E9E  surface_colour_at — WHICH SURFACE IS AT (LINE, POSITION)?  (twin #40)
   ---------------------------------------------------------------------------
   The road renderer's colour decision, and the only routine that reads all four
   surface_edge buffers together.  Given a scan line in Y and a position along it in
   EDGE_COLUMN, it walks the boundaries outward and returns the surface's colour byte:

     line past horizon_extent        → surface_colours[1], the off-road colour
     position at or past edge 0      → surface_colours[3]
     ...past edge 2                  → the second road side's line_attr, or [3] past its limit
     ...past edge 3                  → surface_colours[0]
     ...past edge 1                  → the first road side's line_attr, ditto
     inside every boundary           → view_line_surface[line], the line's background

   The two line_attr arms end the same way: the attribute byte is an edge-point index PLUS
   ONE (hence EDGE_STYLE_PREV), its style's low two bits pick the colour, and X is left
   holding that style — which is why X is part of the exit contract and not scratch.

   ⚠ EVERY EXIT'S CARRY IS THE LAST COMPARE'S, and the caller's next instruction is a store,
   not a branch — so the carry is only observable through the differential.  It is 1 on every
   arm reached by a taken BCS and 0 on the two that fall through, which is what the compares
   below reproduce rather than compute.
   =========================================================================== */

/* Build surface_colour_at's exit: A = colour with N/Z from it, Y = the scan line, V unchanged
   from entry, X and C are per-arm.  (Nothing in the routine writes V, and CPY/CPX/CMP never do,
   so V is always the caller's.) */
static SlotExit surf_exit(uint8_t colour, uint8_t x, uint8_t line, uint8_t entryV, uint8_t c)
{
    SlotExit e = { colour, x, line, (uint8_t)((colour >> 7) & 1u),
                   (uint8_t)(colour == 0u), entryV, c };
    return e;
}

static SlotExit surface_colour_at_core(uint8_t line, uint8_t position,
                                       uint8_t entryX, uint8_t entryV)
{
    unsigned attr;
    uint8_t  c;

    /* $1E9E — nothing above the horizon has a surface; that is sky.  CPY leaves C = line >=
       horizon_extent; the taken (BCS+BNE) arm needs line > it, and its exit C is 1. */
    if (line > horizon_extent)
        return surf_exit(mem[SURFACE_COLOURS_TBL + 1], entryX, line, entryV, 1u);

    /* $1EA8-$1EBC — the four boundaries, outermost first.  A taken BCS exits with C = 1. */
    if (position >= mem[SURFACE_EDGE_0 + line])
        return surf_exit(mem[SURFACE_COLOURS_TBL + 3], entryX, line, entryV, 1u);

    if (position >= mem[SURFACE_EDGE_2 + line]) {
        if (line >= line_attr_1_limit)                     /* cpy_ge taken: C = 1 */
            return surf_exit(mem[SURFACE_COLOURS_TBL + 3], entryX, line, entryV, 1u);
        attr = mem[LINE_ATTR_1 + line];
        c    = 0u;                                         /* the cpy_ge fell through: C = 0 */
    } else if (position >= mem[SURFACE_EDGE_3 + line]) {
        return surf_exit(mem[SURFACE_COLOURS_TBL + 0], entryX, line, entryV, 1u);
    } else if (position >= mem[SURFACE_EDGE_1 + line]) {
        if (line >= line_attr_0_limit)
            return surf_exit(mem[SURFACE_COLOURS_TBL + 3], entryX, line, entryV, 1u);
        attr = mem[LINE_ATTR_0 + line];
        c    = 0u;
    } else {
        /* $1EBE — inside everything: the line's own background class.  The last CMP (edge 1)
           fell through, so exit C = 0; X = the surface class. */
        attr = mem[VIEW_LINE_SURFACE + line];
        return surf_exit(mem[SURFACE_COLOURS_TBL + (attr & 3u)], (uint8_t)(attr & 3u),
                         line, entryV, 0u);
    }

    /* $1EDC — the attribute is an edge-point index + 1; its style's low bits are the colour.
       ⚠ The 6502's first `TAX` (the masked index) is NOT reproduced: the second one below
       always overwrites X before anything can read it, so that intermediate value is dead.
       Verified by sabotage — dropping the mask HERE passes 2000 cases, while dropping it on
       the table index one line down fails, which is the pair that proves which one matters.
       X = the surface class; C = 0, carried from the cpy_ge that fell through above. */
    attr = mem[EDGE_STYLE_PREV + (attr & 0x7Fu)];
    return surf_exit(mem[SURFACE_COLOURS_TBL + (attr & 3u)], (uint8_t)(attr & 3u),
                     line, entryV, c);
}

/* Apply surface_colour_at's full exit ABI to the cpu — the shim body, parametrised by the scan
   line.  The two still-cpu-based callers (column_gap_walk, plot_view_src_line) route through it
   so the native path leaves exactly the cpu state the ORACLE gets via the shim; returns the
   colour byte for their own use.  (A documented cross-call boundary until they too convert.) */
uint8_t surface_colour_apply(uint8_t line)
{
    SlotExit e = surface_colour_at_core(line, mem[EDGE_COLUMN], cpu.X, cpu.V);
    cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
    return e.a;
}

/* The 6502-ABI shim.  Y is the scan line and EDGE_COLUMN the position; A comes back as the
   colour, X as the surface class on the two arms that compute one. */

/* ===========================================================================
   $1DAF  column_gap_walk — FILL ONE COLUMN'S EMPTY SOURCE BYTES  (twin #41)
   ---------------------------------------------------------------------------
   Walks EDGE_COLUMN's source block from span_line_cursor down to (not including)
   dash_block_starts[column], replacing every ZERO byte with surface_colour_at's answer for
   that scan line.  Which pointer it stores through, what a NON-ZERO byte does and what a zero
   colour becomes are all read out of the three patch bytes — see the header above.

   ⚠ The block address is computed, not tabulated: plot_ptr = $3000 + column*$80, spelled by
   the 6502 as (column + $60) >> 1 with the shifted-out bit rotated back into the low byte.
   The ADC that does it is the last thing to write V, and V is live at every exit.
   =========================================================================== */

SlotExit column_gap_walk_core(uint8_t entryX, uint8_t entryY, uint8_t entryV)
{
    unsigned column = mem[EDGE_COLUMN];
    unsigned storePtr;
    uint8_t fallback, offset;
    uint8_t x = entryX;          /* surface_colour_at's escaping class, threaded across the walk */
    uint8_t v;                   /* the entry ADC #$60 overflow, live at every exit below */
    uint8_t a;                   /* the 6502's A: the last cell handled, or the dead LSR result */
    uint8_t y;                   /* the scan-line cursor */

    /* $1DAF-$1DB3 — there are only $28 source columns; above that there is nothing to fill.
       cmp_ge left A = column and the CMP #$28 flags; C = 1 on the taken (>=) arm. */
    if (column >= 0x28u) {
        uint8_t d = (uint8_t)(column - 0x28u);
        SlotExit e = { (uint8_t)column, entryX, entryY, (uint8_t)((d >> 7) & 1u),
                       (uint8_t)(column == 0x28u), entryV, 1u };
        return e;
    }

    /* $1DB5-$1DBE — plot_ptr = view_src_blocks + column * $80.  The 6502 spells this as
       (column + $60) then LSR/ROR to spread one bit into the low byte's top; the whole thing is
       just $3000 + column*$80 as a 16-bit value.  The LSR leaves A = (column+$60)>>1 = plot_ptr's
       high byte, which is the routine's exit A on the (fixture-unreachable) no-iteration path.
       The `ADC #$60` is the last op to write V and nothing below it does (the loop is all
       LDA/CMP), so its overflow reaches every exit. */
    { uint16_t plot_ptr = (uint16_t)(0x3000u + column * 0x80u);
      plot_ptr_hi = (uint8_t)(plot_ptr >> 8);
      plot_ptr_lo = (uint8_t)plot_ptr;
      a = plot_ptr_hi; }
    v = adc_overflow((uint8_t)column, 0x60u, 0);

    /* ⚠⚠ NOTHING IN THIS LOOP IS HOISTED, AND THAT IS MEASURED RATHER THAN CAUTIOUS.  The
       walk's own stores can land on the cells that drive it: a boundary-table pointer of
       $005D (a real randomised case, 2 of 1200) makes the run cover $0082 and $0085, i.e. the
       loop's end line and the column it is filling, and the 6502 re-reads both every pass.
       The three patch bytes at $1DD5/$1DDC/$1DDE are reachable the same way.  What IS hoisted
       is the hardware-window test, which collapses to one comparison per store instead of a
       bus_read/bus_write dispatch (CLAUDE.md §bus_read/bus_write). */
    y = span_line_cursor;
    for (;;) {
        /* CPY #EDGE_BLOCK_START — the loop test.  On the equal exit its N/Z/C (0/1/1) are the
           routine's exit flags; its carry (y >= end) is the trap path's exit C otherwise. */
        uint8_t end   = mem[EDGE_BLOCK_START];
        uint8_t loopC = (uint8_t)(y >= end);
        if (y == end) {
            SlotExit e = { a, x, y, 0u, 1u, v, 1u };
            return e;
        }

        unsigned srcBase = zp_pointer(MEM_plot_ptr_lo);
        uint8_t  line    = y;
        uint8_t  src;

        offset   = mem[GAP_BRANCH_OPERAND];
        fallback = mem[GAP_FALLBACK];
        storePtr = mem[GAP_PTR_OPERAND];

        src = seam_read((srcBase + line) & 0xFFFFu, pointer_is_ram(srcBase));
        a   = src;                                    /* LDA (plot_ptr),Y */

        if (src != 0) {
            /* $1DD4 — the patched branch: skip the cell, or map it into the table. */
            if (offset == 0x09u) { y = (uint8_t)(line - 1); continue; }   /* $1DDF */
            /* ⚠ THE TRAP BELONGS HERE, not at the top: the branch is only reached once a
               non-zero source byte is found, so a column of zeroes never executes it and an
               unmodelled offset must leave A, Y and the flags as this LDA left them. */
            if (offset != 0xEFu) {
                platform_smc_unhandled(0x1DD4, (uint16_t)(0x1DD6 + (int8_t)offset));
                SlotExit e = { src, x, line, (uint8_t)((src >> 7) & 1u),
                               0u /* src != 0 */, v, loopC };
                return e;
            }
            /* $1DC5 — the boundary-table pass: "all four columns" reads as empty. */
            uint8_t stored;
            if (src == 0x55u) { a = 0u; stored = 0u; }    /* CMP #$55 Z: LDA #0 */
            else              { stored = src; }
            { unsigned altBase = zp_pointer(MEM_plot_ptr2_lo);
              seam_write((altBase + line) & 0xFFFFu, pointer_is_ram(altBase), stored); }
            y = (uint8_t)(line - 1);
            continue;
        }

        /* $1DD6 — an empty cell takes the surface's colour, or the fallback if it has none.
           surface_colour_at's class escapes in X; its colour byte is A. */
        { SlotExit sc = surface_colour_at_core(line, mem[EDGE_COLUMN], x, v);
          x = sc.x;
          a = sc.a ? sc.a : fallback; }               /* colour, or load_a(fallback) if 0 */
        { unsigned storeBase = zp_pointer(storePtr);
          seam_write((storeBase + line) & 0xFFFFu, pointer_is_ram(storeBase), a); }
        y = (uint8_t)(line - 1);
    }
}


/* ===========================================================================
   $1DA6  fill_column_gaps — THE PATCH, THEN THE WALK  (twin #42)
   ---------------------------------------------------------------------------
   Three stores and a fall-through.  It exists because the walk is one routine serving two
   passes: this entry is what turns the registers into the walk's configuration.

   ⚠ THE FIXTURE FOR THIS ONE COVERS THE MAPPING, NOT THE WALK ($1DAF has its own): what can
   be wrong here is which register lands in which patch byte, and swapping any two of them
   fails at once.  The ORDER of the three stores is NOT observable — see twin #8's header.
   =========================================================================== */

SlotExit fill_column_gaps_core(uint8_t pointer, uint8_t branchOffset,
                                      uint8_t fallback, uint8_t entryV)
{
    mem[GAP_PTR_OPERAND]    = pointer;        /* $1DA6 — STA (zp),Y's own zero-page number */
    mem[GAP_BRANCH_OPERAND] = branchOffset;   /* $1DA9 — which arm a non-zero byte takes */
    mem[GAP_FALLBACK]       = fallback;       /* $1DAC — the colour a zero surface becomes */
    /* $1DAF — falls straight through into the walk, and the fall-through leaves the registers
       exactly as they entered: X = pointer, Y = branchOffset, V unchanged.  So the walk's own
       three inputs are those, spelled here as values rather than read back from cpu. */
    return column_gap_walk_core(pointer, branchOffset, entryV);
}


/* ===========================================================================
   $1DEF  fill_edge_column_run — ONE RUN OF END COLUMNS  (twin #43)
   ---------------------------------------------------------------------------
   Entered with X = the first column, A = the column to stop at and Y = the scan line the
   first column's walk starts from.  Per iteration it walks column N through plot_ptr2 (into
   the per-line boundary table) and column N+1 through plot_ptr (into the column's own source
   block), so a run of K iterations touches columns X..X+K-1 one way and X+1..X+K the other.

   ⭐ THE START LINE IS NOT RE-SUPPLIED PER COLUMN, and that is the loop's real subtlety: Y
   comes back from the walk holding the line it stopped at, and the next iteration stores THAT
   as the next column's start line.  So each column's walk begins where its neighbour's ended,
   which is what makes the filled region follow the dashboard's diagonal edge.
   =========================================================================== */

SlotExit fill_edge_column_run_core(uint8_t firstColumn, uint8_t stopColumn,
                                          uint8_t firstLine, uint8_t entryV)
{
    unsigned column = firstColumn;
    uint8_t  y      = firstLine;   /* the scan-line cursor, threaded from each walk's exit */
    uint8_t  v      = entryV;      /* the walk's entry V — DEAD here (see below), carried anyway */
    SlotExit e2 = { 0u, 0u, 0u, 0u, 0u, 0u, 0u };  /* the run's last walk = the routine's exit */

    mem[EDGE_RUN_LIMIT] = stopColumn;         /* $1DEF */

    /* ⭐ THE WALK'S ENTRY V NEVER SURFACES on this path: every column here is < $28, so the walk
       always takes its normal arm — which recomputes V from `column + $60` — and its early-return
       arm (the only exit that would pass entry V through) is unreachable. */
    do {
        mem[EDGE_COLUMN]      = (uint8_t)column;                       /* $1DF1 */
        span_line_cursor      = y;                                     /* $1DF3 */
        mem[EDGE_BLOCK_START] = mem[DASH_BLOCK_STARTS + column];       /* $1DF5-$1DF8 */

        /* $1DFA — this column into the per-line boundary table, $55 mapped to empty.  Its exit
           line is discarded: both walks in an iteration start from the SAME span_line_cursor. */
        { SlotExit e1 = fill_column_gaps_core(MEM_plot_ptr2_lo, 0xEFu, 0x00u, v);
          v = e1.v; }

        /* $1E03 — and the NEXT column into its own source block, non-zero bytes kept.  THIS
           walk's exit line is what the next column's walk starts from ($1DF3 next iteration). */
        mem[EDGE_COLUMN] = (uint8_t)(column + 1);
        e2 = fill_column_gaps_core(MEM_plot_ptr_lo, 0x09u, 0x55u, v);
        y  = e2.y;
        v  = e2.v;

        column = mem[EDGE_COLUMN];
    } while (column != mem[EDGE_RUN_LIMIT]);                           /* $1E0E-$1E12 */

    /* $1E0E LDX EDGE_COLUMN; CPX EDGE_RUN_LIMIT — X = column (now == stopColumn), and the equal
       compare leaves N=0, Z=1, C=1.  A, Y and V are the second walk's, untouched by the CPX. */
    { SlotExit e = { e2.a, (uint8_t)column, y, 0u, 1u, e2.v, 1u }; return e; }
}

/* The 6502-ABI shim.  X is the first column, A the stop column, Y the first start line; X
   comes back as the column the run stopped at and Y as the last walk's end line. */

/* ===========================================================================
   TWINS #44-#49 — THE ENGINE'S MULTIPLY, AND THE NEGATE BESIDE IT
   ---------------------------------------------------------------------------
   The first group of apply_driving_model's callee tree, and the one place in this project
   where the 68000 replaces an algorithm rather than an interpreter:

     $0C02  mul8_noinit    the 8x8 shift-and-add itself — ONE `mulu.w` here
     $0C00  mul8           ...with the multiplicand taken from A
     $0DBF  mul8_accum     (shared_temp_76:math_lo) x math_hi >> 8 — a 16x8 fixed-point step
     $0DB3  mul16_by_pi    x4, then x $C9/256 — i.e. multiply a 16-bit angle by pi
     $0E42  neg16_math     negate (math_hi:math_lo), the high byte leaving in A
     $0E44  neg16_math_noinit  ...the same without parking A in math_hi first

   ⭐⭐ WHY THIS ONE IS DIFFERENT.  Every twin since #14 has confirmed that being real C buys
   nothing on its own — the win is ALGORITHMIC COMPRESSION.  `mul8` is the case the rule was
   waiting for: **28 call sites, the most-called routine in the engine**, and its body is eight
   unrolled iterations of `BCC` / `CLC` / `ADC` / `ROR A` / `ROR math_lo` — about 40 6502
   instructions, each of which the transliteration wraps in flag bookkeeping, standing in for
   one `MULU.W`.  Nothing here is a driver.

   ⚠⚠ AND THE EXIT CONTRACT IS NOT "THE PRODUCT", which is what makes the compression legal
   rather than approximate.  Checked over all 65536 operand pairs (the twin's own arithmetic
   against a replay of the 6502):
     * A = the product's HIGH byte, math_lo = its LOW byte — but N and Z come from the closing
       `ROR math_lo`, i.e. from the LOW byte, not from A;
     * C is 0 on EVERY input.  The eight `ROR math_lo`s shift the multiplier out completely, so
       the last carry-out is a bit that has already been consumed;
     * V is the V of the LAST `ADC`, which happens at the multiplier's top set bit, where the
       accumulator holds (addend x (multiplier mod 2^k)) >> k.  `adc_overflow` replays exactly
       that one add.  With a zero multiplier no add runs at all and the caller's V survives.
   ⚠ Decimal mode is not "a flag detail" here: D changes the RESULT BYTE of every `ADC`, so the
   routine stops being a multiply.  The bit-for-bit replay below is kept for it.  The engine
   never sets D; a randomised fixture does.
   =========================================================================== */

/* $0C02  mul8_noinit — the 8x8 multiply, math_lo x math_hi.  The binary path is one MULU.W (see
   the header); decimal mode still runs the 6502's own shift-and-add, because D changes the RESULT
   byte of every ADC.  Exit: A = product high, math_lo = product low; N/Z from math_lo, C = 0,
   V = the last ADD's.  This is a KEPT shim — generated code and the mul8_accum family call it. */
/* The 8x8 product plus the ONE escaping flag: the V of the last shift-and-add.  `setV` is 0 for a
   zero multiplier (no ADC ran, so the 6502 leaves the caller's V — the shim must not overwrite it). */

static Mul8 mul8_noinit_core(uint8_t multiplier, uint8_t addend)
{
    /* The 8x8 shift-and-add, simulated exactly as $0C02-$0C46 so the escaping V is the real V of
       the LAST ADC — a closed form for the accumulator before that add is too fragile to trust
       (it must reproduce the 6502's ADC overflow bit for all 65536 pairs, not merely the product).
       math_lo (multiplier) is shifted out a bit at a time; on each set bit math_hi (addend) is
       added, and the {A:math_lo} pair rotates right.  Product high ends in A, low in m. */
    Mul8 r;
    uint8_t A = 0, m = multiplier, C = 0;
    r.v = 0; r.setV = 0;

    for (int i = 0; i < 8; i++) {
        if (i == 0) { C = m & 1u; m >>= 1; }        /* $0C04 LSR math_lo (seeds the first bit) */
        /* iters 1..7 reuse the C left by the previous ROR math_lo */
        if (C) {                                     /* BCC skips the add when the bit is clear */
            unsigned s   = (unsigned)A + addend;     /* $0C08-$0C09 CLC; ADC math_hi (carry-in 0) */
            uint8_t  res = (uint8_t)s;
            r.v   = (uint8_t)((~(A ^ addend) & (A ^ res)) >> 7) & 1u;
            r.setV = 1;
            C = (uint8_t)(s > 0xFFu);
            A = res;
        }
        { uint8_t cin = C; C = A & 1u; A = (uint8_t)((cin << 7) | (A >> 1)); }  /* ROR A */
        { uint8_t cin = C; C = m & 1u; m = (uint8_t)((cin << 7) | (m >> 1)); }  /* ROR math_lo */
    }

    r.product = (uint16_t)((A << 8) | m);
    return r;
}

void mul8_noinit(void)
{
    /* $0C02 math_lo x math_hi -> A:math_lo.  The engine only ever multiplies in BINARY — none
       of the 8 SED sites reach here (docs/static-map.md §Decimal mode) — so this is one 16-bit
       product.  Exit: A = product high, math_lo = product low; N/Z from math_lo, C = 0, V = the
       final shift-and-add's. */
    Mul8 r = mul8_noinit_core(math_lo, math_hi);

    math_lo = (uint8_t)r.product;
    cpu.A   = (uint8_t)(r.product >> 8);   /* the closing ROR is on math_lo — NOT on A */
    cpu.N   = (uint8_t)((math_lo >> 7) & 1u);
    cpu.Z   = (uint8_t)(math_lo == 0);
    cpu.C   = 0;                          /* provably 0 for every operand pair */
    if (r.setV) cpu.V = r.v;              /* escaping V; caller's V survives a zero multiplier */
}

/* $0C00  mul8 — mul8_noinit with the multiplicand taken from A (a store, so no flags). */
void mul8(void) { math_lo = cpu.A; mul8_noinit(); }

/* ---------------------------------------------------------------------------
   $0DBF  mul8_accum — THE 16x8 FIXED-POINT STEP  (twin #46)
   ---------------------------------------------------------------------------
   Multiplies the 16-bit value (shared_temp_76 : math_lo) by math_hi and keeps the top 16 bits
   of the 24-bit result: the low product's HIGH byte is added into the high product, which is
   the ordinary way to spell a x.8 fixed-point multiply on a machine with an 8x8 multiplier.

   ⚠ Its exit flags are the closing ADD's, or the `INC math_hi`'s on the carry path — so N and
   Z describe math_lo on one path and math_hi on the other.  A KEPT shim (generated code and the
   mul16_by_pi / scale16_by_y twins call it); D is honoured through mul8_noinit. */
/* mul8_accum's exit ABI: A + N/Z/C/V.  N/Z come from math_lo on the no-carry path and from the
   INCremented math_hi on the carry path — reconstructed in the shim from this typed result. */

Mul8AccumExit mul8_accum_core(void)
{
    uint8_t hiOperand = shared_temp_76, mathHi = math_hi;

    /* $0DBF — math_lo x math_hi, the LOW half.  Only its product-high byte is used. */
    Mul8   r1      = mul8_noinit_core(math_lo, mathHi);
    uint8_t lowHigh = (uint8_t)(r1.product >> 8);
    shared_temp_77  = lowHigh;                       /* $0DC2 */

    /* $0DC4-$0DC6 — shared_temp_76 x math_hi, the HIGH half. */
    Mul8   r2   = mul8_noinit_core(hiOperand, mathHi);
    math_lo     = (uint8_t)r2.product;              /* the second product's low byte */
    math_hi     = (uint8_t)(r2.product >> 8);       /* $0DC9 */

    /* $0DCB-$0DD0 CLC/ADC — the low product's high byte into the high product's low byte. */
    uint8_t  a = lowHigh, m = math_lo;
    unsigned sum = (unsigned)a + m;
    uint8_t  res = (uint8_t)sum;
    math_lo = res;

    Mul8AccumExit e;
    e.a = res;
    e.c = (uint8_t)(sum > 0xFFu);
    e.v = (uint8_t)(((~(a ^ m) & (a ^ res)) >> 7) & 1u);
    if (sum > 0xFFu) {
        math_hi = (uint8_t)(math_hi + 1u);          /* $0DD4 INC math_hi — carry path */
        e.n = (uint8_t)((math_hi >> 7) & 1u);       /* N/Z now describe math_hi */
        e.z = (uint8_t)(math_hi == 0);
    } else {
        e.n = (uint8_t)((res >> 7) & 1u);           /* N/Z describe math_lo */
        e.z = (uint8_t)(res == 0);
    }
    return e;
}

void mul8_accum(void)
{
    Mul8AccumExit e = mul8_accum_core();
    cpu.A = e.a; cpu.N = e.n; cpu.Z = e.z; cpu.C = e.c; cpu.V = e.v;
}

/* ---------------------------------------------------------------------------
   $0DB3  mul16_by_pi — A 16-BIT ANGLE TIMES PI  (twin #47)
   ---------------------------------------------------------------------------
   Shifts (A : math_lo) left twice, parks the high byte where mul8_accum wants it, seeds the
   multiplier with $C9 and falls into mul8_accum.  ⭐ $C9/256 = 0.785 = pi/4 to three figures,
   and 4 x pi/4 = pi — so what compute_car_angles gets back is its angle multiplied by pi
   [INFERRED from the constant; the x4 and the multiply are [DERIVED]].  A KEPT shim; A is high. */

/* ---------------------------------------------------------------------------
   $0E42 / $0E44  neg16_math — NEGATE (math_hi : math_lo)  (twins #48, #49)
   ---------------------------------------------------------------------------
   Two's-complement negate of the 16-bit accumulator.  ⚠ The high byte comes back in A and is
   NOT written to math_hi — the caller decides whether to keep it — and the second subtract's
   N/V/Z/C are the exit flags.  $0E42 parks A in math_hi first (so it negates the value the
   caller is holding); $0E44 negates what is already in the pair.  abs16_math falls into $0E42.
   D is always 0 on every path that reaches here (driving-model / steering / render callers —
   docs/static-map.md §Decimal mode), so these are plain 16-bit negates.  Both are KEPT shims. */
void neg16_math_noinit(void)
{
    uint16_t v = (uint16_t)(0u - (uint16_t)(((uint16_t)math_hi << 8) | math_lo));
    math_lo = (uint8_t)v;               /* $0E44-$0E49 — low byte written back */
    cpu.A   = (uint8_t)(v >> 8);        /* $0E4B-$0E4E — high byte escapes in A, math_hi kept */
}

void neg16_math(void)
{
    math_hi = cpu.A;                    /* $0E42 */
    neg16_math_noinit();
}

/* ===========================================================================
   TWINS #50-#57 — THE DRIVING MODEL'S 16-BIT ARITHMETIC
   ---------------------------------------------------------------------------
   The layer between the multiply and the sub-models: everything that reads or writes the
   model's 16-bit state vector as a NUMBER rather than as physics.

     $0DD7  mul16_signed             16x16 -> 16 with the sign carried in a byte
     $4753  scale16_by_y             |x| * Y >> 8, sign restored — the model's scale operation
     $4765  mul16_by_1_5             x * 1.5, arithmetic (a shift and an add)
     $47E5  model_integrate_element   element X += element 14
     $48A0  add_signed_into_element   element Y += ±(math_hi:math_lo)
     $4874  apply_angle_term          element x car angle -> element, storing or accumulating
     $486D  apply_angle_term_at       ...the same with the source element taken from $7F
     $0E50  kbd_test_key              OSBYTE 129 on one negative INKEY code

   ⭐ mul16_signed IS THE SECOND REAL COMPRESSION IN THIS TREE.  The 6502 spells a 16x16
   multiply as three 8x8 products accumulated by hand across five scratch cells; what it
   computes, derived from that accumulation and checked against it, is exactly

       result = p2 + ((p1 + p3 + $80) >> 8),   p1 = hi*angleLo, p2 = hi*angleHi, p3 = lo*angleHi

   i.e. the true product MINUS its lowest cross term (lo*angleLo, which the routine never
   forms) plus a half-count of rounding.  Three `MULU.W`s and two adds here.
   ⚠ The five scratch cells are part of the contract, not workspace: shared_temp_76/77,
   hypot_min_lo, math_lo and math_hi all keep values the differential compares, so the twin
   writes what the 6502 left there even where its own arithmetic did not need it.
   =========================================================================== */

/* mul16_signed's two operands sit in the point_delta scratch window with ASYMMETRIC sign
   conventions (see the routine note): the MULTIPLICAND is a plain two's-complement value,
   the MULTIPLIER is a car-angle coefficient whose sign is packed in bit 0 of its low byte. */
#define MUL_SRC_LO     0x0080u   /* point_delta_lo[0] — multiplicand low  (two's complement) */
#define MUL_SRC_HI     0x0081u   /* point_delta_lo[1] — multiplicand high; bit 7 is its sign */
#define MUL_TERM_LO    0x0082u   /* point_delta_lo[2] — multiplier low; the car angle, bit 0 = SIGN */
#define MUL_TERM_HI    0x0083u   /* point_delta_hi[0] — multiplier high (heading_sin/heading_cos) */
#define MUL_SIGN       0x0079u   /* hypot_min_hi — product-sign accumulator (bit 7) + apply_angle_term's store/accumulate mode (bit 6) */
#define MODEL_TERM     0x007Cu   /* point_dist_lo — the destination element index */
#define MODEL_SRC_SLOT 0x007Fu   /* span_line_cursor — apply_angle_term_at's source element */

/* ---------------------------------------------------------------------------
   $0DD7  mul16_signed — THE SIGNED 16x16 MULTIPLY  (twin #50)
   ---------------------------------------------------------------------------
   ⚠ ASYMMETRIC sign conventions on the two operands.  The MULTIPLICAND (MUL_SRC_HI:LO) is a
   plain two's-complement value — made positive up front, its sign flipped into MUL_SIGN bit 7.
   The MULTIPLIER (MUL_TERM_HI:LO) is a car-angle coefficient (heading_sin/heading_cos) whose
   sign lives in BIT 0 of its LOW byte, which flips MUL_SIGN again.  It keeps the top 16 bits of
   the three cross products (dropping the lowest, lo*lo), and the tail falls into abs16_math, so
   the banked sign is applied to the result on the way out.

   ⚠ Its exit flags are `BIT MUL_SIGN`'s, not the arithmetic's: N = bit 7, V = bit 6, Z from
   A AND MUL_SIGN — and then the negate's own flags on the negative path.  C is the last
   ADC's and survives the BIT.

   NOTE: this is the faithful $0DD7 twin, kept as the validation oracle's counterpart and for
   any 6502-ABI caller.  apply_angle_term (its only real caller) no longer routes through it —
   see apply_angle_term_body, which folds the same arithmetic into plain 16-bit C.  The three
   cross products are plain 16-bit multiplies (revs_mulu16); D = 0 on every path that reaches the
   real caller (docs/static-map.md §Decimal mode), and the fixture pins it. */
void mul16_signed(void)
{
    unsigned p1, p2, p3, mid, low, result;
    uint8_t  angleLo = mem[MUL_TERM_LO], angleHi = mem[MUL_TERM_HI];
    uint8_t  sourceLo, sourceHi;

    /* $0DD7-$0DEC — |source|, with the sign recorded.  Plain 16-bit two's-complement negate
       (D = 0 on every path that reaches the real caller, docs/static-map.md §Decimal mode). */
    if (mem[MUL_SRC_HI] & 0x80u) {
        unsigned neg = (0x10000u - (((unsigned)mem[MUL_SRC_HI] << 8) | mem[MUL_SRC_LO]))
                       & 0xFFFFu;
        mem[MUL_SRC_LO] = (uint8_t)neg;
        mem[MUL_SRC_HI] = (uint8_t)(neg >> 8);
        mem[MUL_SIGN]  ^= 0x80u;
    }
    /* $0DEE-$0DF8 — and the multiplier's own sign, which lives in bit 0 of its low byte. */
    if (angleLo & 1u) mem[MUL_SIGN] ^= 0x80u;

    sourceLo = mem[MUL_SRC_LO];
    sourceHi = mem[MUL_SRC_HI];

    /* $0DFA-$0E36 — three 8x8 products, accumulated.  The flags of the multiplies themselves
       are all overwritten by the closing adds, so these go through the value-only entry. */
    p1 = revs_mulu16(angleLo, sourceHi);
    p2 = revs_mulu16(angleHi, sourceHi);
    p3 = revs_mulu16(angleHi, sourceLo);

    /* $0E05-$0E20 — the accumulation.  Plain binary 16-bit adds (D = 0 on the real caller's
       path, docs/static-map.md §Decimal mode); the byte truncations of the carried-up high
       halves are kept exactly as the 6502 does them. */
    { unsigned s1, s2, s3, s4;
      uint8_t  c1, c2, c3, c4;

      s1  = (unsigned)(uint8_t)p1 + 0x80u;                /* $0E05-$0E0A */
      low = (uint8_t)s1;  c1 = (uint8_t)(s1 > 0xFFu);
      s2  = (unsigned)(uint8_t)p2 + (uint8_t)((p1 >> 8) + c1);  /* $0E17-$0E1C */
      mid = (uint8_t)s2;  c2 = (uint8_t)(s2 > 0xFFu);
      hypot_min_lo   = (uint8_t)((p2 >> 8) + c2);         /* $78 — $0E15 then $0E1E's INC */
      shared_temp_76 = (uint8_t)low;                      /* $0E0A */
      shared_temp_77 = (uint8_t)mid;                      /* $0E1C */
      math_hi        = (uint8_t)(p3 >> 8);                /* $0E2B */

      /* $0E2D-$0E3A — the two closing adds.  Their C is the only flag of theirs that escapes,
         and it is what decides whether $78 is INCed. */
      s3      = (unsigned)(uint8_t)p3 + (uint8_t)low;
      c3      = (uint8_t)(s3 > 0xFFu);
      s4      = (unsigned)math_hi + (uint8_t)mid + c3;
      math_lo = (uint8_t)s4;  c4 = (uint8_t)(s4 > 0xFFu);
      cpu.C   = c4;                                       /* survives the BIT below */
      if (c4) hypot_min_lo++;
      result  = hypot_min_lo;
      cpu.A   = (uint8_t)result; }

    /* $0E3C-$0E3E — and the sign byte decides the exit flags AND whether to negate. */
    BIT(mem[MUL_SIGN]);
    abs16_math();
}

/* ---------------------------------------------------------------------------
   $4753  scale16_by_y — |x| * Y >> 8, SIGN RESTORED  (twin #51)
   ---------------------------------------------------------------------------
   The model's scale operation: take the caller's 16-bit value in (A : math_lo), scale it by
   the unsigned byte in Y, and give it its sign back.

   ⚠ PHP/PLP, and both halves matter.  The sign is the CALLER's N — the same contract abs16_math
   has — so it has to survive the multiply, and the 6502 parks it on the stack.  The push leaves
   a byte in the stack page that the differential compares, so the pair is reproduced rather
   than replaced by a saved C variable.
   --------------------------------------------------------------------------- */
void scale16_by_y(void)
{
    uint8_t scale = cpu.Y;          /* the multiplier byte */
    /* cpu.A holds the value's high byte on entry ($4753). */
    PHP();                          /* $4753 — the caller's N, which is the value's sign */
    abs16_math();                   /* $4754 */
    shared_temp_76 = cpu.A;         /* $4757 */
    math_hi        = scale;         /* $4759 */
    mul8_accum();                   /* $475B */
    cpu.A = math_hi;                /* $475E — a value only: the LDA's own N/Z are dead here,
                                       because the PLP on the next line overwrites them.  Proved
                                       by sabotage: swapping these two lines passes 3000 cases,
                                       while KEEPING the load's flags across the PLP fails. */
    PLP();                          /* $4760 */
    abs16_math();                   /* $4761 */
}

/* ---------------------------------------------------------------------------
   $4765  mul16_by_1_5 — x + x/2, ARITHMETIC  (twin #52)
   ---------------------------------------------------------------------------
   (A : math_lo) = (math_hi : math_lo) * 1.5, with the halving SIGNED — the 6502 seeds the
   rotate's carry from the value's own bit 7 instead of clearing it, which is a one-instruction
   arithmetic shift right.  ⚠ PHA/PLA, so there is a stack residue here too.
   --------------------------------------------------------------------------- */
void mul16_by_1_5(void)
{
    /* (A:math_lo) = (math_hi:math_lo) * 1.5 = x + x/2, the halving SIGNED (the 6502 seeds the
       rotate from bit 7 instead of clearing it — an arithmetic shift right).  PHA/PLA leave the
       x/2 high byte in the stack page, which the differential compares, so it is written back. */
    uint8_t hiIn = math_hi, loIn = math_lo;

    /* $4765-$476B — x/2 high byte: arithmetic shift right of math_hi, and the bit0 it rotates
       down into the low half. */
    uint8_t hiHalf   = (uint8_t)((hiIn >> 1) | (hiIn & 0x80u));
    int     midCarry = hiIn & 1u;

    mem[0x0100u + cpu.S] = hiHalf;                      /* $476C PHA — the stack residue */

    /* $476D-$4773 — x/2 low (loIn rotated right through midCarry) + x low, carry-in clear. */
    { uint8_t  loHalf = (uint8_t)(((unsigned)midCarry << 7) | (loIn >> 1));
      unsigned sum    = (unsigned)loHalf + loIn;
      math_lo = (uint8_t)sum;
      cpu.C   = (uint8_t)(sum > 0xFFu);
      /* $4775 PLA (hiHalf back into A), $4776 ADC — x/2 high + x high + carry; its flags exit. */
      { unsigned hs = (unsigned)hiHalf + hiIn + cpu.C;
        uint8_t  hr = (uint8_t)hs;
        cpu.A = hr;
        cpu.V = (uint8_t)(((~(hiHalf ^ hiIn) & (hiHalf ^ hr)) >> 7) & 1u);
        cpu.C = (uint8_t)(hs > 0xFFu);
        cpu.N = (uint8_t)((hr >> 7) & 1u);
        cpu.Z = (uint8_t)(hr == 0);
      }
    }
}

/* ---------------------------------------------------------------------------
   $47E5  model_integrate_element — ELEMENT X += ELEMENT 14  (twin #53)
   ---------------------------------------------------------------------------
   One 16-bit add over the state vector.  Element 14 is the per-frame delta the sub-models
   above have been accumulating into, so this is the model's integration step; the two
   rotations ($47A5/$47C5) each end with one.
   --------------------------------------------------------------------------- */
static AddFlags add16_flags(uint8_t ah, uint8_t mh, unsigned sum)
{
    uint8_t  hr = (uint8_t)(sum >> 8);
    AddFlags f;
    f.hi       = hr;
    f.carry    = (uint8_t)(sum > 0xFFFFu);
    f.overflow = (uint8_t)(((~(ah ^ mh) & (ah ^ hr)) >> 7) & 1u);
    f.neg      = (uint8_t)((hr >> 7) & 1u);
    f.zero     = (uint8_t)(hr == 0);
    return f;
}

AddFlags model_integrate_element_core(uint8_t slot)
{
    /* $47E5 — element[slot] += element[14], one 16-bit binary add (D=0 on the driving-model
       path, docs/static-map.md §Decimal mode); the HIGH add's flags are the exit flags, returned
       for the shim / caller to replay. */
    uint8_t  ah = mem[MODEL_STATE_HI + slot], mh = mem[MODEL_STATE_HI + 14];
    unsigned sum = (unsigned)(((uint16_t)ah << 8) | mem[MODEL_STATE_LO + slot])
                 + (unsigned)(((uint16_t)mh << 8) | mem[MODEL_STATE_LO + 14]);
    mem[MODEL_STATE_LO + slot] = (uint8_t)sum;
    mem[MODEL_STATE_HI + slot] = (uint8_t)(sum >> 8);
    return add16_flags(ah, mh, sum);
}

/* ---------------------------------------------------------------------------
   $48A0  add_signed_into_element — ELEMENT Y += ±(math_hi : math_lo)  (twin #54)
   ---------------------------------------------------------------------------
   ⚠⚠ IT BRANCHES ON THE CALLER'S N, like abs8 and abs16_math: a set N means "subtract this
   instead", spelled as a negate followed by the same add.  A twin that tests bit 7 of anything
   it can see is wrong, and a fixture that leaves N correlated with the value cannot tell.
   --------------------------------------------------------------------------- */
static void add_signed_into_element_core(uint8_t slot, uint8_t signByte)
{
    /* The term is math_hi : math_lo.  $48A0 BMI branches on the caller's sign: a SET sign bit
       (negative) adds it as-is, a CLEAR one negates it first ("subtract this instead").  D = 0 on
       this driving-model path (docs/static-map.md §Decimal mode), so plain 16-bit arithmetic. */
    uint16_t term = (uint16_t)(((uint16_t)math_hi << 8) | math_lo);
    if (!(signByte & 0x80u)) term = (uint16_t)(0u - term);      /* $48A0-$48A5 */

    uint16_t sum = (uint16_t)((((uint16_t)mem[MODEL_STATE_HI + slot] << 8)
                               | mem[MODEL_STATE_LO + slot]) + term);   /* $48A7-$48B5 */
    mem[MODEL_STATE_LO + slot] = (uint8_t)sum;
    mem[MODEL_STATE_HI + slot] = (uint8_t)(sum >> 8);
}

/* ---------------------------------------------------------------------------
   $4874 / $486D  apply_angle_term — ONE STATE ELEMENT THROUGH ONE CAR ANGLE  (twins #55, #56)
   ---------------------------------------------------------------------------
   The model's rotation primitive: multiply state element `source` by car angle `angle` and
   either STORE the product into element `dest` or ADD it there — bit 6 of MUL_SIGN, which
   the caller sets along with the sign, is what chooses, and the add path is literally
   add_signed_into_element's tail.

   $486D is the same routine entered with the source element in MODEL_SRC_SLOT and A carrying
   the sign/mode byte, which is what the two four-call rotations ($48B9, $48C1) use so they can
   step source and destination independently.

   ⭐ apply_angle_term is the ONLY caller of the $0DD7 signed multiply, so the multiply is folded
   in here as plain 16-bit C rather than going through the mul16_signed twin's 6502 register
   round-trip.  The one thing that must stay byte-exact is what that multiply COMPUTES, and it is
   not a full 16x16: it keeps the top 16 bits of only the three HIGH cross products and drops the
   lowest (srcLo*termLo), rounding with +$0080 — i.e. product = termHi*srcHi + ((termLo*srcHi +
   termHi*srcLo + $80) >> 8), taken mod 65536.  The operands are signed asymmetrically: the
   multiplicand (model element) is two's-complement; the multiplier (car angle) is sign-MAGNITUDE
   with its sign in bit 0 of the low byte — that bit stays in the magnitude, exactly as the 6502
   multiply used the whole byte.  D = 0 throughout the model path (docs/static-map.md §Decimal
   mode), so every add here is plain binary.
   --------------------------------------------------------------------------- */
/* The pure-C body: multiply element `source` by car angle `angle` and store or accumulate into
   element `dest` (= mem[MODEL_TERM]).  Plain 16-bit C on mem[] — touches no cpu state at all.
   Its exit registers/flags are DEAD in the real program (its only callers are the driving-model
   rotations, whose own exit registers are overwritten the instant apply_driving_model returns to
   them), so the twin does not reconstruct them and every fixture below verifies the RESULT. */
static void apply_angle_term_body(uint8_t angle, uint8_t source)
{
    /* $4876-$4888 — the two operands.  MUL_SIGN was seeded by the caller with the mode byte:
       bit 7 the starting sign, bit 6 the store/accumulate select. */
    int16_t  src16 = (int16_t)(uint16_t)((mem[MODEL_STATE_HI + source] << 8)
                                         | mem[MODEL_STATE_LO + source]);
    uint8_t  termLo = mem[CAR_ANGLE_LO + angle];
    uint8_t  termHi = mem[CAR_ANGLE_HI + angle];
    uint8_t  mode   = mem[MUL_SIGN];

    /* $0DD7-$0DF8 — the sign accumulates: a negative multiplicand flips it, and the multiplier's
       bit-0 sign flips it again.  The multiplicand is taken to its magnitude for the product. */
    int      negative = (mode >> 7) & 1;
    uint16_t srcMag;
    if (src16 < 0) { srcMag = (uint16_t)(-src16); negative ^= 1; }
    else             srcMag = (uint16_t)src16;
    if (termLo & 1u) negative ^= 1;

    /* $0DFA-$0E3A — the three high cross products, dropping srcLo*termLo, rounded with +$80. */
    uint8_t  srcLo = (uint8_t)srcMag, srcHi = (uint8_t)(srcMag >> 8);
    unsigned p1 = (unsigned)termLo * srcHi;
    unsigned p2 = (unsigned)termHi * srcHi;
    unsigned p3 = (unsigned)termHi * srcLo;
    uint16_t mag = (uint16_t)(p2 + ((p1 + p3 + 0x80u) >> 8));

    /* $0E3C-$0E4E — apply the banked sign to the 16-bit magnitude. */
    uint16_t product = negative ? (uint16_t)(0u - mag) : mag;

    /* $488F-$489E — store the product into element `dest`, or accumulate into it. */
    uint8_t  dest  = mem[MODEL_TERM];
    uint16_t value = product;
    if (mode & 0x40u) {
        value = (uint16_t)(value + (uint16_t)((mem[MODEL_STATE_HI + dest] << 8)
                                              | mem[MODEL_STATE_LO + dest]));
    }
    mem[MODEL_STATE_LO + dest] = (uint8_t)value;
    mem[MODEL_STATE_HI + dest] = (uint8_t)(value >> 8);
}

static void apply_angle_term_core(uint8_t dest, uint8_t angle, uint8_t source)
{
    mem[MODEL_TERM] = dest;                     /* $4874 */
    apply_angle_term_body(angle, source);
}

static void apply_angle_term_at_core(uint8_t mode, uint8_t angle)
{
    uint8_t source = mem[MODEL_SRC_SLOT];       /* $486D LDY — a local; exit Y is dead here */
    mem[MUL_SIGN] = mode;                     /* $486F */
    apply_angle_term_body(angle, source);       /* $4871 JMP $4876 */
}

/* ---------------------------------------------------------------------------
   $0E50  kbd_test_key — IS THIS KEY DOWN?  (twin #57)
   ---------------------------------------------------------------------------
   OSBYTE 129 with a negative INKEY code in X and $FF in Y: the MOS answers X = $FF when that
   key is being held.  The routine's whole output is the CPX #$FF result — Z set means the key
   IS down (the callers read it as "pressed": throttle key down -> full throttle).  Returns 1
   when the key is down.  Kept as a twin because the driving model's starter poll goes through
   it, and because the MOS call has to stay a MOS call.
   --------------------------------------------------------------------------- */
/* OSBYTE 129 (INKEY): the OSBYTE number in A, the negative key code in X and a $FF time-limit in
   Y; the MOS answers X = Y = $FF when that key is held.  engine_starter_poll reads the raw exit
   registers, so this returns them typed; kbd_test_key_core keeps the boolean the other callers want. */
MosRegs kbd_test_key_regs(uint8_t keyCode)
{
    return mos_osbyte(0x81u, keyCode, 0xFFu);   /* $0E50-$0E54 */
}

int kbd_test_key_core(uint8_t keyCode)
{
    /* The $0E50 routine leaves A/X/Y as the INKEY call did (X=Y=$FF held, $00 not; A = the OSBYTE
       number), and its callers on the driving-control path let those escape as the chain's exit
       X/Y.  Replay them so the cpu-free wrapper's dropped residue is reconstructed — faithful,
       since every JSR to this routine in the oracle leaves exactly these.  The boolean return is
       the CPX #$FF result the flag-testing callers want. */
    MosRegs r = kbd_test_key_regs(keyCode);
    cpu.A = r.a; cpu.X = r.x; cpu.Y = r.y;
    return r.x == 0xFFu;                             /* $0E57 CPX #$FF — Z set (key down) is the output */
}

/* The 6502-ABI shims. */
void add_signed_into_element(void) { add_signed_into_element_core(cpu.Y, cpu.N ? 0x80u : 0x00u); }
void apply_angle_term(void)      { apply_angle_term_core(cpu.A, cpu.X, cpu.Y); }
void apply_angle_term_at(void)   { apply_angle_term_at_core(cpu.A, cpu.X); }

/* ===========================================================================
   TWINS #58-#66 — THE DRIVING MODEL'S ROTATIONS AND INTEGRATIONS
   ---------------------------------------------------------------------------
   apply_driving_model's third group, and the one that finally says what the model DOES: the
   layer above the 16-bit arithmetic, where the state vector is treated as vectors and rates
   rather than as numbers.

     $4729 stage_accum_delta      the midpoint offset — accumulator -= v, delta = 1.5v
     $47A5 rotate_accum_by_steer  the (8, 9) pair turned by the steering angle
     $47C5 rotate_pair_a_by_steer ...and the (10, 12) pair, with the opposite pair of modes
     $47F9 damp_and_derive_loads  elements 10..13 decayed by 4, then loads 6 and 7 rebuilt
     $48C7 rotate_state_pair      THE 2x2 ROTATION — four apply_angle_term_at calls
     $48B9 rotate_state_0_into_8  ...entered for (source 0, dest 8, mode $C0)
     $48C1 rotate_state_6_into_3  ...and for (source 6, dest 3, mode $40)
     $48EF integrate_car_position the camera triple, at 24-bit precision, plus the heading
     $4937 integrate_state_rates  elements 3/4/5 integrated into 0/1/2, also at 24 bits

   ⭐ EVERY LEAF UNDERNEATH THESE WAS ALREADY A TWIN (#50-#57), so there is no interpreter
   left below this line and the compression here is structural rather than arithmetic: two
   nested `ROR` loops over four state elements become one shift, a hand-unrolled 24-bit
   doubling becomes `wide <<= n`, and the four-call rotation becomes four named calls whose
   arguments are visible instead of three registers stepped between them.

   ⚠⚠ $48EF's FIRST ADD HAS NO `CLC` — its carry comes from the `ROL` immediately above it, so
   the doubling and the add are one 24-bit operation.  A twin that starts the add with a clear
   carry is wrong exactly half the time, and nothing but a differential would say so.
   ⚠ THE LOOP EXIT REGISTERS ARE PART OF THE CONTRACT.  Four of these routines end by falling
   out of a `DEX`/`DEY` loop, so X and Y leave holding the value that failed the test ($FF,
   $FE) and the differential compares them; the twins set them explicitly where the C loop has
   no equivalent, with the instruction that wrote them named.
   =========================================================================== */

#define VIEW_ORIGIN_FRAC 0x62B1u  /* view_origin_frac[0..2] — the camera's sub-byte remainder */
#define MODEL_STATE_FRAC 0x62AEu  /* model_state_frac[0..2] — elements 0..2 at 24 bits */
#define MODEL_ROT_MODE   0x0088u  /* point_delta_sign[2] — here the rotation's sign/mode byte */
#define STEER_ANGLE      2u       /* element 2 (steer_angle) of the heading_sin/heading_cos/steer array */

/* ---------------------------------------------------------------------------
   $4729  stage_accum_delta — THE MIDPOINT OFFSET  (twin #58)
   ---------------------------------------------------------------------------
   Scales element 2 (the heading step) by $58/$100, SUBTRACTS that from the accumulator, and
   parks 1.5x it in model_accum_delta.  The four sub-models that run next therefore see the
   accumulator at x - v while apply_driving_model restores x and adds +1.5v afterwards — the
   shape of a midpoint integration, and the reason this routine looks like it corrupts state.

   ⭐ WRITTEN AS PLAIN 16-BIT SIGNED C.  The 6502 scaled through scale16_by_y (a PHP/PLP sign
   dance) and took 1.5x through mul16_by_1_5 (a PHA/PLA one); with D = 0 — the driving model's
   real precondition (docs/static-map.md §Decimal mode) — both are just model_scale16 and
   model_mul_1_5 on 16-bit words, so this twin is arithmetic on locals.  Its exit registers,
   arithmetic scratch ($74-$77) and stack residue ($01FF) are all dead: apply_driving_model's
   next act is `update_grip_limits`, which opens with `LDA #0`.  The fixture verifies the four
   output cells (model_accum and model_accum_delta) only.
   --------------------------------------------------------------------------- */
static void stage_accum_delta_core(void)
{
    /* $4729-$4736 — scale the heading step (state element 2) by $58/256, keeping its sign. */
    uint16_t step   = (uint16_t)(heading_step_lo | (heading_step_hi << 8));
    uint16_t scaled = model_scale16(step, 0x58u);

    /* $4738-$4746 — the accumulator loses the scaled term for the next four sub-models. */
    uint16_t accum  = (uint16_t)((model_accum_lo | (model_accum_hi << 8)) - scaled);
    model_accum_lo = (uint8_t)accum;
    model_accum_hi = (uint8_t)(accum >> 8);

    /* $4749-$4750 — and 1.5x what was removed is parked as the midpoint delta. */
    uint16_t delta = model_mul_1_5(scaled);
    model_accum_delta_lo = (uint8_t)delta;
    model_accum_delta_hi = (uint8_t)(delta >> 8);
}

/* ---------------------------------------------------------------------------
   $47A5 / $47C5  the two INCREMENTAL ROTATIONS BY THE STEERING ANGLE  (twins #59, #60)
   ---------------------------------------------------------------------------
   Both are the same three steps over a different pair of elements: element 14 takes one
   component times the steering angle, the OTHER component accumulates the second product, and
   model_integrate_element then advances the first component by element 14.  That is a rotation
   applied one frame at a time — the small-angle form, where sin(theta) ~ theta and the cosine
   term is left as 1.

   The mode bytes are the whole difference: $80/$40 for the (8, 9) pair and $00/$C0 for the
   (10, 12) one, i.e. the two products swap signs between the two rotations, which is what
   makes one turn the opposite way from the other.
   --------------------------------------------------------------------------- */
AddFlags rotate_accum_by_steer_core(void)
{
    /* $47A5-$47AF — element 14 = -(element 9 * steer):  bit 7 negates, bit 6 clear stores. */
    mem[MUL_SIGN] = 0x80u;
    apply_angle_term_core(14, STEER_ANGLE, 9);
    /* $47B2-$47BC — element 9 += element 8 * steer:  bit 6 set accumulates instead. */
    mem[MUL_SIGN] = 0x40u;
    apply_angle_term_core(9, STEER_ANGLE, 8);
    /* $47BF-$47C1 — and element 8 advances by the delta just built; its add's flags are the
       exit flags (element/index 8 in X, last source 8 in Y — replayed at the shim). */
    return model_integrate_element_core(8);
}

AddFlags rotate_pair_a_by_steer_core(void)
{
    /* $47C5-$47CF — element 14 = +(element 12 * steer), stored. */
    mem[MUL_SIGN] = 0x00u;
    apply_angle_term_core(14, STEER_ANGLE, 12);
    /* $47D2-$47DC — element 12 -= element 10 * steer. */
    mem[MUL_SIGN] = 0xC0u;
    apply_angle_term_core(12, STEER_ANGLE, 10);
    /* $47DF-$47E1 — element 10 advances; its add's flags are the exit flags (index 10 in X,
       last source 10 in Y — replayed at the shim). */
    return model_integrate_element_core(10);
}

/* ---------------------------------------------------------------------------
   $47F9  damp_and_derive_loads — THE SUSPENSION DECAY AND THE TWO LOADS  (twin #61)
   ---------------------------------------------------------------------------
   Three things, in order:

     1. element 5 = (element 10 - element 11) * $4E/$100 — the DIFFERENCE of the two damped
        quantities, which is the only place a difference of them is taken.
     2. elements 10..13 halved TWICE, arithmetically (>> 1 with the sign preserved).  A
        per-frame decay of four: these are the model's transient terms and this is their damping.
     3. elements 6 and 7 rebuilt from the damped pairs, each as
        ((1.5 * odd) + even) * $CD/$100, doubled.  Load 7 comes from the (12, 13) pair and
        load 6 from the (10, 11) one.

   Finally the high byte of element 7 is copied to wheel_load, which is the one value
   update_grip_limits reads out of this routine.

   ⭐ WRITTEN AS PLAIN 16-BIT BINARY C.  The 6502 spelled every step as byte-pair ADC/SBC/ROR
   chains, but with D = 0 those are just `+`/`-`/arithmetic-`>>` on 16-bit words, so this twin is
   real arithmetic on local variables — no cpu struct, no flag helpers.  The whole driving model
   runs with D = 0 (docs/static-map.md §Decimal mode: not one of the eight SED sites is on this
   path); make determinism-drive is the empirical backstop.

   ⭐ AND IT LEAVES NOTHING IN THE CPU.  The 6502's exit A/X/C, its zero-page arithmetic scratch
   ($74-$78) and its stack residue ($01FF) are all dead here: apply_driving_model's next act is
   `LDA drive_state` and both of its arms reload the registers, and apply_drag_terms opens with
   `LDA`.  The only outputs are the state-vector elements and wheel_load — which is what the
   fixture verifies, ignoring the register spill (as road_span_advance does).
   --------------------------------------------------------------------------- */

/* scale16_by_y ($4753) and mul16_by_1_5 ($4765) as pure 16-bit math — D = 0 only. */
static uint16_t model_scale16(uint16_t value, uint8_t scale)
{
    int      v   = (int16_t)value;
    unsigned mag = (v < 0) ? (unsigned)(-v) : (unsigned)v;   /* abs16_math */
    unsigned p   = (mag * (unsigned)scale) >> 8;             /* |v| * scale, keep the top 16 bits */
    return (v < 0) ? (uint16_t)(-(int)p) : (uint16_t)p;      /* ...sign restored */
}

static uint16_t model_mul_1_5(uint16_t value)
{
    int v = (int16_t)value;
    return (uint16_t)(v + (v >> 1));                         /* v + arithmetic v/2 */
}

/* One element of the model's 16-bit state vector, little-endian across the LO/HI halves. */
static uint16_t model_state_get(uint8_t i)
{
    return (uint16_t)(mem[MODEL_STATE_LO + i] | (mem[MODEL_STATE_HI + i] << 8));
}

static void model_state_put(uint8_t i, uint16_t v)
{
    mem[MODEL_STATE_LO + i] = (uint8_t)v;
    mem[MODEL_STATE_HI + i] = (uint8_t)(v >> 8);
}

static void damp_and_derive_loads_core(void)
{
    uint8_t slot;

    /* 1. $47F9-$4812 — element 5 = (element 10 - element 11) * $4E/$100. */
    model_state_put(5, model_scale16((uint16_t)(model_state_get(10) - model_state_get(11)),
                                     0x4Eu));

    /* 2. $4815-$482A — elements 10..13 halved arithmetically, twice. */
    for (slot = 10; slot <= 13; slot++) {
        int16_t e = (int16_t)model_state_get(slot);
        e = (int16_t)(e >> 1);
        e = (int16_t)(e >> 1);
        model_state_put(slot, (uint16_t)e);
    }

    /* 3. $482C-$4864 — load 7 from the (12, 13) pair, then load 6 from the (10, 11) pair,
       each ((1.5 * odd) + even) * $CD/$100, then doubled. */
    for (slot = 2; slot != 0xFEu; slot = (uint8_t)(slot - 2)) {
        uint16_t sum  = (uint16_t)(model_mul_1_5(model_state_get(11 + slot))
                                   + model_state_get(10 + slot));
        uint16_t load = (uint16_t)(model_scale16(sum, 0xCDu) << 1);
        model_state_put((uint8_t)(6 + slot / 2), load);
    }

    /* $4866-$4869 — the one value update_grip_limits reads out of here. */
    wheel_load = mem[MODEL_STATE_HI + 7];
}

/* ---------------------------------------------------------------------------
   $48C7  rotate_state_pair — THE 2x2 ROTATION  (twins #62, #63, #64)
   ---------------------------------------------------------------------------
   Four apply_angle_term_at calls that turn the (source, source + 1) pair through car angles 1
   and 0 into the (dest, dest + 1) pair:

       dest     =  source * angle1  +/- (source + 1) * angle0
       dest + 1 =  (source + 1) * angle1  -/+ source * angle0

   The two entries above it are the arguments: $48B9 rotates elements 0/1 into 8/9 with mode
   $C0 and $48C1 rotates 6/7 into 3/4 with mode $40 — mode bit 6 makes the second and fourth
   calls ACCUMULATE onto the first and third, and the `EOR #$80` on the fourth is the sign flip
   that makes the four products a rotation rather than four independent scalings.

   ⚠ A FOURTH TENANT of $0088, which is point_delta_sign[2] to build_track_geometry, a clip
   history to the span rasteriser and a surface class to mark_line_surfaces (docs/rename.md).
   Here it is where the mode byte lives across the four calls, because A is needed for it.
   --------------------------------------------------------------------------- */
static void rotate_state_pair_core(uint8_t dest, uint8_t source, uint8_t mode)
{
    mem[MODEL_SRC_SLOT]  = source;              /* $48C7 */
    mem[MODEL_TERM]      = dest;                /* $48C9 */
    mem[MODEL_ROT_MODE]  = mode;                /* $48CB */

    /* $48CD-$48D1 — dest = source * angle 1, stored positive. */
    apply_angle_term_at_core(0x00u, 1u);
    /* $48D4-$48D9 — dest += (source + 1) * angle 0, signed by the mode byte. */
    mem[MODEL_SRC_SLOT]++;
    apply_angle_term_at_core(mem[MODEL_ROT_MODE], 0u);
    /* $48DC-$48E1 — dest + 1 = (source + 1) * angle 1, stored positive. */
    mem[MODEL_TERM]++;
    apply_angle_term_at_core(0x00u, 1u);
    /* $48E4-$48EB — dest + 1 += source * angle 0 with the OPPOSITE sign. */
    mem[MODEL_SRC_SLOT]--;
    apply_angle_term_at_core((uint8_t)(mem[MODEL_ROT_MODE] ^ 0x80u), 0u);
    /* $48E4's DEX leaves X = 0, but the exit registers are DEAD (the only callers, $46A8/$471C in
       apply_driving_model, overwrite A/N/Z and reload X/Y before any read; $48C7 is never JSR'd)
       and every fixture here is result-only — so nothing to replay. */
}

/* ---------------------------------------------------------------------------
   $48EF  integrate_car_position — THE CAMERA, AT 24 BITS  (twin #65)
   ---------------------------------------------------------------------------
   Element 1 is added (doubled) into view_origin component 2 and element 0 into component 0,
   each as a 24-bit add through view_origin_frac — the camera moves by fractions of a unit per
   frame, so the remainder has to be carried or a slow car never moves at all.  Then the
   heading advances by element 2.

   ⚠⚠ THE FIRST ADD HAS NO `CLC`: $490D's carry is the one the `ROL shared_temp_76` above it
   left, i.e. the doubling's own carry out.  The doubling and the add are ONE 24-bit operation.
   ⚠ The name's second half is a misnomer worth keeping in mind — what $4927 advances is the
   HEADING, not a position (disasm/symbols.csv).
   --------------------------------------------------------------------------- */
AddFlags integrate_car_position_core(void)
{
    uint8_t slot;

    /* $48EF-$4925 — element 1 into component 2, then element 0 into component 0.  All binary
       24-bit adds: D = 0 on the driving-model path (docs/static-map.md §Decimal mode).  The
       loop's own A / flags are dead — overwritten next pass, and by the heading add at exit. */
    for (slot = 1; slot != 0xFFu; slot--) {
        unsigned comp = (unsigned)slot * 2u;            /* Y = 2 then 0, stepped by two */
        uint8_t  lo   = mem[MODEL_STATE_LO + slot];     /* $48F7-$48FA */
        uint8_t  hi   = mem[MODEL_STATE_HI + slot];     /* $48FC */
        uint8_t  ext  = (uint8_t)((hi & 0x80u) ? 0xFFu : 0x00u);  /* $48FF-$4901 sign extend */
        uint32_t doubled, sum;

        /* $4903-$4908 — element[slot] sign-extended to 24 bits and doubled; its three bytes stay
           live in mem, and the carry OUT of the top bit (ext>>7) feeds bit 0 of the add. */
        math_lo        = (uint8_t)(lo << 1);
        math_hi        = (uint8_t)((hi << 1) | (lo >> 7));
        shared_temp_76 = (uint8_t)((ext << 1) | (hi >> 7));
        doubled = ((uint32_t)shared_temp_76 << 16) | ((uint32_t)math_hi << 8) | math_lo;

        /* $490A-$491F — add it into the 24-bit view component (FRAC:LO:HI); the top carry-out is
           dead (the loop's exit flags are overwritten by the heading add below). */
        sum = (((uint32_t)mem[VIEW_ORIGIN_HI + comp] << 16)
             | ((uint32_t)mem[VIEW_ORIGIN_LO + comp] << 8)
             |  mem[VIEW_ORIGIN_FRAC + comp])
            + doubled + (ext >> 7);
        mem[VIEW_ORIGIN_FRAC + comp] = (uint8_t)sum;
        mem[VIEW_ORIGIN_LO + comp]   = (uint8_t)(sum >> 8);
        mem[VIEW_ORIGIN_HI + comp]   = (uint8_t)(sum >> 16);
    }
    /* $4922/$4923's two DEYs leave Y = $FE and $4924's DEX leaves X = $FF (replayed at the shim). */

    /* $4927-$4934 — and the heading advances by element 2, the frame's heading step.  The HIGH
       add's A / N / V / Z / C are this routine's exit flags, returned for the shim to replay. */
    { uint8_t  hc = car_heading_hi, hm = heading_step_hi;
      unsigned h  = (unsigned)(((uint16_t)hc << 8) | car_heading_lo)
                  + (unsigned)(((uint16_t)hm << 8) | heading_step_lo);
      car_heading_lo = (uint8_t)h;
      car_heading_hi = (uint8_t)(h >> 8);
      return add16_flags(hc, hm, h);
    }
}

/* ---------------------------------------------------------------------------
   $4937  integrate_state_rates — ELEMENTS 3/4/5 ARE THE RATES OF 0/1/2  (twin #66)
   ---------------------------------------------------------------------------
   For X = 2, 1, 0: element 3+X is shifted left three places (FIVE for X = 2) into a 24-bit
   value and added into element X, with the sub-byte remainder carried in model_state_frac.
   So elements 3/4/5 are the rates of 0/1/2 and this is the integrator that applies them — and
   the odd shift on X = 2 is a per-axis scale factor, four times the other two.

   The shift's own carry out is discarded: $495E clears it before the add.
   --------------------------------------------------------------------------- */
AddFlags integrate_state_rates_core(void)
{
    uint8_t slot;
    AddFlags f = { 0, 0, 0, 0, 0 };

    /* All binary 24-bit adds: D = 0 on the driving-model path (docs/static-map.md §Decimal
       mode).  The LAST pass (slot 0) leaves A / C / V live; the DEX below rewrites N / Z. */
    for (slot = 2; slot != 0xFFu; slot--) {
        uint8_t  lo    = mem[MODEL_STATE_LO + 3 + slot];        /* $493D-$4940 */
        uint8_t  hi    = mem[MODEL_STATE_HI + 3 + slot];        /* $4942 */
        uint8_t  ext   = (uint8_t)((hi & 0x80u) ? 0xFFu : 0x00u);  /* $4945-$4947 */
        unsigned shift = (slot == 2u) ? 5u : 3u;                /* $4949-$494F */
        unsigned wide  = (((unsigned)ext << 16) | ((unsigned)hi << 8) | lo) << shift;
        uint8_t  ah = mem[MODEL_STATE_HI + slot], mh, hr;
        uint32_t sum;

        math_lo        = (uint8_t)wide;                         /* $4951-$4959 (live in mem) */
        math_hi        = (uint8_t)(wide >> 8);
        shared_temp_76 = (uint8_t)(wide >> 16);
        mh = shared_temp_76;

        /* $495B-$4971 — add the low 24 bits of the shifted rate into element X (FRAC:LO:HI); the
           shift's own carry out was cleared ($495E CLC), so carry-in is 0. */
        sum = (((uint32_t)ah << 16)
             | ((uint32_t)mem[MODEL_STATE_LO + slot] << 8)
             |  mem[MODEL_STATE_FRAC + slot])
            + (wide & 0xFFFFFFu);
        hr = (uint8_t)(sum >> 16);
        mem[MODEL_STATE_FRAC + slot] = (uint8_t)sum;
        mem[MODEL_STATE_LO + slot]   = (uint8_t)(sum >> 8);
        mem[MODEL_STATE_HI + slot]   = hr;
        /* the HIGH byte add's A / C / V — live only from the last pass (slot 0). */
        f.hi       = hr;
        f.carry    = (uint8_t)(sum > 0xFFFFFFu);
        f.overflow = (uint8_t)(((~(ah ^ mh) & (ah ^ hr)) >> 7) & 1u);
    }
    /* N / Z are NOT the add's — the shim replays $4974's DEX (X: 0 -> $FF). */
    return f;
}

/* The 6502-ABI shims. */
void stage_accum_delta(void)      { stage_accum_delta_core(); }
void damp_and_derive_loads(void)  { damp_and_derive_loads_core(); }
void rotate_state_pair(void)      { rotate_state_pair_core(cpu.A, cpu.Y, cpu.X); }
void rotate_state_0_into_8(void)  { rotate_state_pair_core(8u, 0u, 0xC0u); }
void rotate_state_6_into_3(void)  { rotate_state_pair_core(3u, 6u, 0x40u); }

/* ===========================================================================
   TWINS #67-#78 — THE SLIP/SOUND CLUSTER
   ---------------------------------------------------------------------------
   apply_driving_model's fourth group: how the engine decides a wheel is sliding, what it does
   to the model when it is, and the tyre squeal that comes out of it.  ⭐ It is the only group
   in this tree that reaches the MOS, so `make sound` is a second gate for it — what the twins
   have to preserve is not a number but the SEQUENCE of OS calls.

     $4779 update_slip_sound   the whole decision, run once per axle (X = 1 then 0)
     $4A91 check_wheel_slip    the magnitude, the limit, and one bit of history
     $4AF7 clamp_slip_to_grip  ...and what is done to the model once it HAS been slipping
     $4B42 store_slip_clamped_off_throttle
     $4B47 store_slip_clamped   three entries of one store: clamp-unless-throttle, clamp,
     $4B51 store_slip_signed    and the re-sign-and-store tail they share
     $4B61 slip_magnitude      |element Y| << 5, clamped to $7F
     $4B88 derive_slip_reference  the reference term, and the carry that DECLINES
     $0B4A sound_queue         an 8-byte MOS SOUND control block, then OSWORD 7
     $0B47 sound_queue_default ...with the amplitude taken from sound_volume
     $0B6E sound_osword        the OSWORD tail both sound entries share
     $0E5A sound_stop_channel  OSBYTE 21 on buffer X|4 — buffers 4..7 ARE the sound channels

   ⭐ WHAT THE CLUSTER COMPUTES, now that it reads as C.  Per axle: negate the accumulator,
   shift it left five into element 10, and compare a cheap hypotenuse of elements 10 and 12
   (max + min/2, the same alpha-max-plus-beta-min the road pass uses) against grip_limit.  One
   bit of the answer is `ROR`ed into slip_flags, so the squeal answers to the last TWO frames
   rather than to this one — and if the shift SATURATED, that alone counts as a slip whatever
   the limit says ($4AAE-$4AB2, which is the sign-comparison nobody would guess from the name).

   ⚠ SIX OF THESE HAD NO NAME.  The reading behind each is in disasm/symbols.csv; two things
   the rename queue had recorded wrongly and this pass corrected are worth repeating here:
   $0B46 is a SPARE BYTE where X is parked across the OSWORD, not self-modifying code, and
   derive_slip_reference's two arms were written down the wrong way round (pedal_mode == 1 is
   the throttle, so `LDY pedal_mode / DEY / BEQ` takes the THROTTLE to the gear-based arm).
   =========================================================================== */

#define SLIP_MAG_LO      0x008Eu  /* plot_ptr3_lo — here slip_magnitude's low byte  */
#define SLIP_MAG_HI      0x008Fu  /* plot_ptr3_hi — ...and its high byte (docs/rename.md) */
#define SLIP_SIGN        0x0079u  /* hypot_min_hi — here the sign byte abs16_math branches on */
#define SLIP_OUT_INDEX   0x0078u  /* hypot_min_lo — here WHICH element the store lands in */
#define SLIP_REV_TERM    0x003Du  /* still unnamed: update_engine_revs' second rev-derived
                                     term, which only a twin of that routine can settle
                                     (docs/rename.md) */
#define SOUND_CHAN_STATE 0x62BDu  /* sound_chan_state[0..3], one byte per MOS sound channel */

/* ---------------------------------------------------------------------------
   $4B61  slip_magnitude — |ELEMENT Y| << 5, CLAMPED  (twin #67)
   ---------------------------------------------------------------------------
   ⚠ THE CLAMP LEAVES STATE BEHIND.  When the high byte goes negative the routine bails out
   with $7F, and both Y (wherever the loop stopped) and SLIP_MAG_LO (part-shifted) keep the
   values that moment left — so a twin that computes the saturated result in one step and
   tidies up afterwards is wrong, and the differential says so.
   --------------------------------------------------------------------------- */
static void slip_magnitude_core(uint8_t slot)
{
    /* $4B61 — |element[slot]| << 5, saturated to $7F.  D = 0 on the driving path
       (docs/static-map.md §Decimal mode); exit registers/flags are dead (both callers overwrite
       A immediately), so the whole product is mem[]: SLIP_MAG_HI, and SLIP_MAG_LO left where the
       loop stopped.  ⚠ the clamp leaves state behind — on overflow the routine bails with $7F and
       SLIP_MAG_LO keeps its part-shifted value — so the shift loop is reproduced step for step. */
    uint16_t v = (uint16_t)(((uint16_t)mem[MODEL_STATE_HI + slot] << 8)  /* $4B61-$4B66 */
                            | mem[MODEL_STATE_LO + slot]);
    uint8_t  count;

    if (v & 0x8000u) v = (uint16_t)(0x10000u - v);              /* $4B69-$4B74 — |x| */

    /* $4B77-$4B86 — five doublings, or as many as fit before the high byte goes negative.  Only
       the FINAL SLIP_MAG_LO/HI are observed, so the value shifts in one uint16_t and is stored
       once: on overflow SLIP_MAG_LO keeps its part-shifted low byte, exactly as the 6502's
       per-iteration ASL of the memory cell leaves it. */
    for (count = 5u; ; count--) {
        v = (uint16_t)(v << 1);                 /* ASL SLIP_MAG_LO / ROL A */
        if (v & 0x8000u) break;                 /* $4B7C BMI → $4B84 (ROL bit 7 = clamp) */
        if (count == 1u) break;                 /* $4B7E DEY / $4B7F BNE */
    }
    mem[SLIP_MAG_LO] = (uint8_t)v;                              /* $4B79/$4B81 */
    mem[SLIP_MAG_HI] = (v & 0x8000u) ? 0x7Fu : (uint8_t)(v >> 8);  /* $4B81, clamp = $7F */
}

/* ---------------------------------------------------------------------------
   $4B51 / $4B47 / $4B42  the three entries of ONE STORE  (twins #68, #69, #70)
   ---------------------------------------------------------------------------
   store_slip_signed re-signs (A : math_lo) on bit 7 of the sign byte and puts it in model_state
   element 10 + slip_out_index.  store_slip_clamped is that with the value first held down to
   slip_magnitude's, and the third entry skips the clamp while the throttle is down — i.e. ON
   the throttle the caller's value goes through unclamped, which is what lets wheelspin exceed
   what the grip limit would otherwise allow.
   --------------------------------------------------------------------------- */
/* The store takes a 16-bit value as (valueHi : math_lo) — the 6502's own split, and math_lo is an
   observed output — and writes it, re-signed, into model_state element 10 + slip_out_index.  The
   exit ABI (A = the stored low byte, Y = the element index, N/Z from that byte, V = bit 6 of the
   sign byte, C per the entry/clamp compare) is reconstructed in the shims, not here. */
void store_slip_signed_core(uint8_t valueHi)
{
    uint8_t lo = math_lo;
    uint8_t hi = valueHi;
    if (mem[SLIP_SIGN] & 0x80u) {                   /* $4B51 BIT N — bit 7 means "negative" */
        /* $4B53 abs16_math negates (hi : math_lo); neg16_math first parks the pre-negate high byte
           in math_hi, and the negate leaves it there.  D = 0 on the slip path — a plain negate. */
        uint16_t v = (uint16_t)(0u - (uint16_t)(((uint16_t)hi << 8) | lo));
        math_hi = hi;
        lo = (uint8_t)v;
        hi = (uint8_t)(v >> 8);
        math_lo = lo;
    }
    uint8_t y = mem[SLIP_OUT_INDEX];                /* $4B56 */
    mem[MODEL_STATE_HI + 10 + y] = hi;              /* $4B58 */
    mem[MODEL_STATE_LO + 10 + y] = lo;             /* $4B5B-$4B5D — A = math_lo, then stored */
}

void store_slip_clamped_core(uint8_t valueHi)
{
    uint8_t hi = valueHi;
    if (hi >= mem[SLIP_MAG_HI]) {                   /* $4B47 CMP / $4B49 BCC — at/over it: clamp */
        math_lo = mem[SLIP_MAG_LO];                 /* $4B4B-$4B4D */
        hi = mem[SLIP_MAG_HI];                      /* $4B4F */
    }
    store_slip_signed_core(hi);
}

void store_slip_clamped_off_throttle_core(uint8_t valueHi)
{
    if (pedal_mode == 1u) {                         /* $4B42 LDY/DEY/BEQ — the throttle skips clamp */
        store_slip_signed_core(valueHi);
        return;
    }
    store_slip_clamped_core(valueHi);
}

/* ---------------------------------------------------------------------------
   $4B88  derive_slip_reference — THE REFERENCE TERM, AND THE CARRY  (twin #71)
   ---------------------------------------------------------------------------
   Sets up everything the store above needs — which element (slip_out_index = X + 2), what sign
   (SLIP_SIGN), and the value (A : math_lo) — and answers with a CARRY: set means it declined,
   which is the one bit both its callers branch on.

   Two arms, and which is which is the thing to get right: pedal_mode == 1 is the throttle, so
   `LDY pedal_mode / DEY / BEQ` sends the THROTTLE to the gear-based arm at $4BAF (where only
   the driven axle, X == 1, is answered at all — the other gets a bare `SEC`), while OFF the
   throttle the reference is the car's own speed, |element 9| << 5, against grip_limit[X],
   three-quartered for the axle that is not X == 1.  Either way the term is multiplied by
   pedal_amount and, on the throttle, halved.
   --------------------------------------------------------------------------- */
/* derive_slip_reference answers with a small pair: the reference term's high byte (its low byte is
   left in math_lo, the 6502's own split) and whether it DECLINED — the carry the 6502 returned. */

SlipRef derive_slip_reference_core(uint8_t axle)
{
    /* $4B88-$4B8C — the element store_slip_signed will write.  On the throttle's declined arm the
       result high byte is axle + 2 (it was the 6502's exit A there). */
    uint8_t element = (uint8_t)(axle + 2u);
    mem[SLIP_OUT_INDEX] = element;

    uint8_t ref;
    if (pedal_mode == 1u) {                                     /* $4B8E-$4B90 DEY/BEQ */
        /* ON THE THROTTLE — $4BAF-$4BBA. */
        if (axle != 1u) { SlipRef r = { element, 1 }; return r; } /* $4BB1 → $4BCD: declined */
        mem[SLIP_SIGN] = (uint8_t)(gear_index - 1u);             /* $4BB3-$4BB6 */
        ref = mem[SLIP_REV_TERM];                                /* $4BBA */
    } else {
        /* OFF THE THROTTLE — $4B93-$4BAC. */
        slip_magnitude_core(9);                                  /* |car_speed| << 5 */
        mem[SLIP_SIGN] = (uint8_t)(car_speed_hi ^ 0x80u);        /* $4B98-$4B9B */
        ref = mem[MEM_grip_limit + axle];                       /* $4B9F */
        if (axle != 1u) {                                        /* $4BA2-$4BA4 CPX #1/BEQ */
            /* $4BA6-$4BAB — three-quarters of the limit: (g/2 + g)/2, each add truncated to 8
               bits exactly as the 6502's LSR/ADC/LSR does (the ADC carry-out is dropped by LSR). */
            uint8_t g = mem[MEM_grip_limit + axle];
            ref = (uint8_t)(((uint8_t)((g >> 1) + g)) >> 1);
        }
    }

    /* $4BBC-$4BC0 — reference x pedal_amount, a plain 8x8 product (D = 0 on the slip path), and on
       the throttle it is halved.  hi = product high, math_lo = product low; declined = 0 (CLC). */
    math_hi = ref;                                              /* $4BBC */
    uint16_t product = (uint16_t)revs_mulu16(ref, pedal_amount);  /* $4BBE-$4BC0 */
    if (pedal_mode == 1u) product >>= 1;                         /* $4BC3-$4BC9 throttle */
    math_lo = (uint8_t)product;
    SlipRef r = { (uint8_t)(product >> 8), 0 };                  /* $4BCB CLC — accepted */
    return r;
}

/* ---------------------------------------------------------------------------
   $4A91  check_wheel_slip — IS THIS AXLE SLIDING?  (twin #72)
   ---------------------------------------------------------------------------
   ⚠⚠ TWO NON-OBVIOUS THINGS, both of which a plain reading of the name would miss.

   (a) THE PHP/PLP CARRIES ONE BIT ACROSS THE ARITHMETIC.  `ORA` answers "is the accumulator
   zero at all", and that answer has to survive a negate and five shifts, so the 6502 parks it
   on the stack.  The push leaves a byte the differential compares, so the pair is reproduced.

   (b) A SATURATED SHIFT IS A SLIP.  If the shifted high byte has the SAME sign as the original
   (they should differ — the value was negated first), the shift overflowed, and $4AB2 goes
   straight to the `ROR` with C already set.  That is the whole of $4AAE-$4AB2 and it is a
   second, independent slip test hiding inside the first.

   Otherwise: the two magnitudes are combined as max + min/2 — alpha-max-plus-beta-min, the
   same cheap hypotenuse the road pass uses — and compared against grip_limit.  ⚠ EQUAL is not
   over: `BNE` past the `CLC` means only a strictly greater magnitude sets the bit.
   --------------------------------------------------------------------------- */
static void check_wheel_slip_core(uint8_t axle)
{
    /* $4A91-$4A99 — is the accumulator zero at all?  (the 6502 parks the answer on the stack
       to survive the negate and five shifts; a local carries it here.) */
    uint16_t accum = (uint16_t)(((uint16_t)model_accum_hi << 8) | model_accum_lo);
    int accumZero = (accum == 0u);

    /* $4A9A-$4AA8 — -model_accum << 5, the low byte into element 10 + axle later, the high now. */
    uint16_t shifted   = (uint16_t)((uint16_t)(0u - accum) << 5);
    uint8_t  shiftedHi = (uint8_t)(shifted >> 8);
    uint8_t  shiftedLo = (uint8_t)shifted;
    mem[MODEL_STATE_HI + 10 + axle] = shiftedHi;

    /* $4AAC-$4AB2 — a SATURATED SHIFT IS A SLIP.  The negate should have flipped the sign, so if
       the shifted high byte still agrees in sign with the pre-negate value the shift overflowed;
       that goes straight to the history roll with the over-limit bit already set.  A zero
       accumulator skips the test entirely. */
    if (!accumZero && ((shiftedHi ^ model_accum_hi) & 0x80u) == 0u) {
        mem[MEM_slip_flags + axle] = (uint8_t)(0x80u | (mem[MEM_slip_flags + axle] >> 1));
        return;
    }

    mem[MODEL_STATE_LO + axle + 10] = shiftedLo;    /* $4AB4-$4AB6 */

    /* $4AB9 — derive_slip_reference sets SLIP_SIGN / SLIP_OUT_INDEX / (hi : math_lo) and answers
       whether it declined.  Its hi : math_lo feed the store below, so they are not disturbed
       between the two calls. */
    SlipRef sr = derive_slip_reference_core(axle);

    uint8_t ref;
    if (sr.declined) {
        /* $4ABE-$4ACC — it declined, so element 12 is cleared and the reference is element 10's
           own magnitude.  ⚠ BOTH indices are absolute 12 here, not 12 + axle — and it provably
           makes no difference: this arm needs pedal_mode == 1 AND X != 1, and X is 0 or 1, so X
           is 0 and `+ 12` IS `+ 12 + axle` (the absolute operands are the 6502 saving bytes). */
        mem[MODEL_STATE_LO + 12] = 0u;
        mem[MODEL_STATE_HI + 12] = 0u;
        uint8_t hi10 = mem[MODEL_STATE_HI + 10];
        ref = (uint8_t)((hi10 & 0x80u) ? (0u - hi10) : hi10);   /* |element 10 high| */
    } else {
        store_slip_clamped_off_throttle_core(sr.hi); /* $4ACF — consumes derive's hi : math_lo */
        /* $4AD2-$4AEB — max + min/2 over the two elements' magnitudes (the cheap hypotenuse). */
        uint8_t m12 = mem[MODEL_STATE_HI + 12 + axle];
        m12 = (uint8_t)((m12 & 0x80u) ? (0u - m12) : m12);
        uint8_t m10 = mem[MODEL_STATE_HI + 10 + axle];
        m10 = (uint8_t)((m10 & 0x80u) ? (0u - m10) : m10);
        ref = (uint8_t)((m10 >= m12) ? (m10 + (m12 >> 1))       /* halve the smaller */
                                     : ((m10 >> 1) + m12));
    }

    /* $4AED-$4AF3 — over the limit?  EQUAL is not over.  Roll the one-bit answer into the two
       frames of slip history. */
    int over = (ref > mem[MEM_grip_limit + axle]);
    mem[MEM_slip_flags + axle] = (uint8_t)((over ? 0x80u : 0u) | (mem[MEM_slip_flags + axle] >> 1));
}

/* ---------------------------------------------------------------------------
   $4AF7  clamp_slip_to_grip — WHAT SLIPPING DOES TO THE MODEL  (twin #73)
   ---------------------------------------------------------------------------
   Run once update_slip_sound has seen slip in either of the last two frames: element 12 + X is
   zeroed, element 10 + X is held at grip_limit_alt[X] (twice, by two different routes — first
   unconditionally through store_slip_clamped, then again against derive_slip_reference's own
   term), and on the throttle the driven axle's element 10 is zeroed outright.  So the grip
   limit is not advisory: this is where the model is forced back inside it.
   --------------------------------------------------------------------------- */
static void clamp_slip_to_grip_core(uint8_t axle)
{
    mem[MODEL_STATE_HI + 12 + axle] = 0u;                       /* $4AF7-$4AF9 */
    mem[MODEL_STATE_LO + 12 + axle] = 0u;                       /* $4AFC */

    slip_magnitude_core(8);                                     /* $4AFF-$4B01 — |model_accum| */
    mem[SLIP_SIGN] = (uint8_t)(model_accum_hi ^ 0x80u);         /* $4B04-$4B09 */
    math_lo = 0x00u;                                            /* $4B0B-$4B0D */
    mem[SLIP_OUT_INDEX] = axle;                                 /* $4B12 */
    store_slip_clamped_core(mem[MEM_grip_limit_alt + axle]);    /* $4B0F/$4B14 */

    SlipRef sr = derive_slip_reference_core(axle);              /* $4B17 */
    if (sr.declined) return;                                    /* $4B1A BCS — it declined */

    if (sr.hi < mem[MEM_grip_limit_alt + axle]) {              /* $4B1C CMP / $4B1F BCC → $4B3E */
        store_slip_clamped_off_throttle_core(sr.hi);
        return;
    }

    /* $4B21-$4B3C — over the second threshold. */
    math_lo = 0x00u;
    store_slip_clamped_off_throttle_core(mem[MEM_grip_limit_alt + axle]);  /* $4B24/$4B28 */
    if (pedal_mode != 1u) return;                              /* $4B2B-$4B2E BNE — off throttle */
    /* $4B30-$4B32 — ⭐ DEAD AS A DECISION, and worth knowing.  Reaching here needs
       derive_slip_reference to have ACCEPTED (declined 0) with pedal_mode == 1, and its throttle
       arm only accepts for axle == 1 — so `CPX #0` can never be equal and this branch never
       taken.  Kept because it is what the 6502 does; a sabotage that deletes it survives the
       differential, which is the evidence for the claim rather than a gap in the fixture. */
    if (axle == 0u) return;
    mem[MODEL_STATE_HI + 10 + axle] = 0u;                      /* $4B34-$4B36 */
    mem[MODEL_STATE_LO + 10 + axle] = 0u;                      /* $4B39 */
}

/* ---------------------------------------------------------------------------
   $0B6E / $0B4A / $0B47  the MOS SOUND path  (twins #74, #75, #76)
   ---------------------------------------------------------------------------
   sound_queue fills in one field of one 8-byte MOS SOUND control block and hands the block to
   OSWORD 7.  The slot the caller names picks the block TWO on from the base (`A << 3` then
   `+ $10`), Y is the amplitude, and the CHANNEL is read back out of the block's own first byte
   so sound_chan_state can be marked busy — the block, not the caller, is what says which
   channel this is.  ⭐ X is the low byte of the OSWORD block address and `LDY #$0B` its high
   byte, which is why $0B00 is where these blocks have to live.

   ⚠ The caller's X is parked in sound_saved_x across the call and restored by sound_osword.
   That byte is a spare, NOT a self-modified operand — no instruction covers $0B46.
   ⚠ sound_queue's `ADC #$10` leaves C and V live all the way to the exit: nothing below it
   writes either flag.
   --------------------------------------------------------------------------- */
MosRegs sound_osword_core(uint8_t oswordNum, uint8_t blockLow)
{
    /* OSWORD `oswordNum` on the block at $0B00 + blockLow; Y ($0B) is that address's high byte
       ($0B70-$0B73).  Returns the MOS's exit registers; the caller's X (in sound_saved_x) is
       restored by the exit ABI, not here.  mos_osword also leaves cpu.A/X/Y set, which the
       sound_osword / sound_queue shims read back as their exit A/Y. */
    return mos_osword(oswordNum, blockLow, 0x0Bu);
}

/* Returns the OSWORD's exit Y — begin_spin threads it out as the spin's yScale residue. */
uint8_t sound_queue_core(uint8_t slot, uint8_t amplitude, uint8_t savedX)
{
    sound_saved_x = savedX;                                     /* $0B4A STX */

    /* $0B4D-$0B53 — the slot picks a control block ((slot << 3) + $10).  The add's C and V leak to
       the exit (reconstructed by sound_queue_block_cv); D = 0 here so it is a plain binary add. */
    uint8_t blockLow = (uint8_t)((uint8_t)(slot << 3) + 0x10u);
    unsigned block = 0x0B00u + blockLow;

    mem[block + 2] = amplitude;                                 /* $0B54-$0B55 — the AMPLITUDE field */

    uint8_t chan = (uint8_t)(mem[block] & 3u);                  /* $0B58-$0B5D — the CHANNEL */
    mem[SOUND_CHAN_STATE + chan] = 0x07u;                       /* $0B5E-$0B60 — the OSWORD 7 marker */
    return sound_osword_core(0x07u, blockLow).y;               /* $0B63 — OSWORD 7 (SOUND) */
}

/* ---------------------------------------------------------------------------
   $0E5A  sound_stop_channel — SILENCE ONE CHANNEL  (twin #77)
   ---------------------------------------------------------------------------
   Clears sound_chan_state[X] and flushes the MOS buffer X|4, because buffers 4..7 ARE the four
   sound channels.  Does nothing at all if the channel was already marked idle, which is what
   keeps update_slip_sound from issuing an OSBYTE every frame the car is not sliding.
   ⚠ X after the OSBYTE is whatever the MOS left, and the `AND #$FB` is applied to THAT.
   --------------------------------------------------------------------------- */
uint8_t sound_stop_channel_core(uint8_t chan, uint8_t ambientY)
{
    /* The caller's A is preserved (PHA/PLA) in the shim, not here — that is exit ABI.  Returns the
       exit X: the channel on the idle flow-through, and (MOS X & ~4) on the active path. */
    if (mem[SOUND_CHAN_STATE + chan] == 0u)             /* $0E5B-$0E5E — already idle? do nothing */
        return chan;                                    /* X flows through unchanged */
    mem[SOUND_CHAN_STATE + chan] = 0u;                  /* $0E60-$0E62 */
    /* MOS ABI: OSBYTE 21 flushes buffer chan|4 (buffers 4..7 ARE the four sound channels); it
       preserves X and Y, and the buffer bit is taken back off.  ⚠ Y is a dead input the MOS
       ignores, but the real 6502 hands it whatever was ambient at the call site and the
       differential logs it — so the caller threads that ambient Y in for fidelity (it is NOT
       cpu-sourced here).  ⚠ The `& ~4` is applied to the MOS's returned X, not the saved channel
       — faithful, though invisible (X is preserved). */
    MosRegs r = mos_call(0xFFF4u, 0x15u, (uint8_t)(chan | 4u), ambientY);   /* $0E65-$0E6B */
    return (uint8_t)(r.x & 0xFBu);                      /* $0E6E-$0E71 */
}

/* ---------------------------------------------------------------------------
   $4779  update_slip_sound — THE TYRE SQUEAL  (twin #78)
   ---------------------------------------------------------------------------
   Called twice per frame, X = 1 then X = 0 — once per axle.  Three outcomes:

     drive_state >= 2 (not under power)   silence channel 3 and do nothing else
     slip in either of the last 2 frames  clamp_slip_to_grip, and START the squeal if
                                          channel 3 is not already playing
     no slip                              silence channel 3, but only on the frames where
                                          bit 1 of loop_counter is clear — a two-frame
                                          hysteresis that stops the squeal chattering

   ⭐ The squeal is queued at amplitude 1 on sound slot 3, and the guard is
   sound_chan_state[3]: the MOS is asked once, not once per frame.
   --------------------------------------------------------------------------- */
static void update_slip_sound_core(uint8_t axle, uint8_t ambientY)
{
    if (drive_state < 0x02u) {                       /* $4779-$477D CMP #2 / BCS → the silence arm */
        check_wheel_slip_core(axle);                 /* $477F */
        if (mem[MEM_slip_flags + axle] & 0xC0u) {    /* $4782-$4787 — slip in the last TWO frames */
            clamp_slip_to_grip_core(axle);
            if (mem[SOUND_CHAN_STATE + 3] == 0u) {   /* $4798-$479B — already playing? */
                /* $479D-$47A1 — slot 3, amplitude 1.  The 6502 still holds the axle in X here,
                   so that is what sound_queue parks in sound_saved_x. */
                sound_queue_core(0x03u, 0x01u, axle);
            }
            return;
        }
        if ((loop_counter & 0x02u) != 0u) return;    /* $4789-$478D — the two-frame hysteresis */
    }
    sound_stop_channel_core(3u, ambientY);           /* $478F-$4791 — Y flows through to the OSBYTE 21 */
}

/* The 6502-ABI shims — they reconstruct each routine's exit registers/flags from the cpu-free
   core's typed outputs and the mem[] state, and hold the MOS-boundary marshalling.  The stores
   share an exit: A = the stored low byte (LDA math_lo), Y = the element (LDY SLIP_OUT_INDEX),
   N/Z from that low byte, V = bit 6 of SLIP_SIGN (BIT), C is the entry-value clamp compare. */
void slip_magnitude(void)       { slip_magnitude_core(cpu.Y); }

void store_slip_exit_abi(uint8_t sign)
{
    cpu.A = math_lo;                                 /* $4B5B LDA math_lo — the stored low byte */
    cpu.Y = mem[SLIP_OUT_INDEX];                     /* $4B56 LDY SLIP_OUT_INDEX */
    cpu.N = (uint8_t)(math_lo >> 7);                 /* LDA math_lo sets N/Z */
    cpu.Z = (uint8_t)(math_lo == 0u);
    cpu.V = (uint8_t)((sign >> 6) & 1u);             /* $4B51 BIT SLIP_SIGN sets V = bit 6 */
}





void check_wheel_slip(void)     { check_wheel_slip_core(cpu.X); }   /* result-only */
void clamp_slip_to_grip(void)   { clamp_slip_to_grip_core(cpu.X); } /* result-only */

/* The $0B4D `ADC #$10` block-index carry and overflow — a plain binary add (D = 0), so C is the
   unsigned carry and V the signed overflow.  begin_spin threads these out as its exit C/V; the
   exit ABI below replays them into cpu. */
typedef struct { uint8_t c, v; } BlockCV;
static BlockCV sound_queue_block_cv(uint8_t slot)
{
    unsigned s   = (uint8_t)(slot << 3);
    unsigned sum = s + 0x10u;
    BlockCV r;
    r.c = (uint8_t)(sum > 0xFFu);
    r.v = (uint8_t)(((~(s ^ 0x10u)) & (s ^ sum) & 0x80u) ? 1u : 0u);
    return r;
}

/* Reconstruct sound_queue / sound_queue_default's exit: A/Y left by OSWORD 7 on the $0Bxx block
   (reason code 7 in A, block high byte $0B in Y — both constants at this call site now the cpu-free
   wrapper no longer leaves them behind), X restored from sound_saved_x (its N/Z the exit), and the
   block-index ADD's C and V ($0B4D ADC #$10). */
void sound_queue_exit_abi(uint8_t slot)
{
    BlockCV cv = sound_queue_block_cv(slot);
    cpu.C = cv.c;
    cpu.V = cv.v;
    cpu.A = 0x07u;                                   /* OSWORD reason code, preserved through the call */
    cpu.Y = 0x0Bu;                                   /* $0B — the sound block's high byte */
    cpu.X = sound_saved_x;                           /* $0B73 LDX sound_saved_x (inside sound_osword) */
    cpu.N = (uint8_t)(sound_saved_x >> 7);
    cpu.Z = (uint8_t)(sound_saved_x == 0u);
}





void update_slip_sound(void)    { update_slip_sound_core(cpu.X, cpu.Y); } /* result-only */

/* ===========================================================================
   TWINS #79-#86 — THE EIGHT SUB-MODELS, and with them the whole of
   apply_driving_model's tree
   ---------------------------------------------------------------------------
   The four groups before this one were the tree's PLUMBING — the multiply, the 16-bit
   arithmetic, the rotations and integrations, the slip/sound cluster.  These eight are the
   parts that talk to the rest of the engine, which is why they came last and why the naming
   pass mattered most here: three of the eight had no name, and nine of the cells they read
   had none either (all of docs/rename.md's "DRIVING MODEL's unnamed callees" entry).

     $0D01 compute_car_angles     the heading -> the sin/cos pair every rotation resolves
                                  through: TWO polynomial evaluations of one pi-scaled angle,
                                  the second on its reflection about pi/2
     $4610 scale_by_track_gradient  a caller's byte x the track's gradient at position Y
     $49CE update_engine_revs     the starter poll, the rev model, a four-segment power curve
                                  and the stall
     $4BCF update_grip_limits     the two per-axle grip thresholds
     $4C65 apply_drag_terms       two speed-dependent terms into state elements 6 and 7
     $4DC9 begin_spin
     $4DCB begin_spin_from_a      the car loses control
     $44EA update_camera_and_drive_state  the biggest routine in the tree, four jobs in one

   ⭐⭐ WHAT THE GROUP MADE LEGIBLE, in the order it surprised:

   1. `compute_car_angles` IS A SINE AND A COSINE, computed as ONE polynomial run twice.  The
      heading is multiplied by pi ($C9/256 = pi/4, shifted twice), then a cubic-ish term
      ($AB x h^3) is subtracted for the small-angle arm and a quadratic used for the large one,
      and the second pass runs on $C900 - h — the reflection that turns sin into cos.  Bit 6 of
      the heading's high byte decides WHICH element each pass writes, and the two sign bits the
      tail ORs into bit 0 are bit7(h) and bit7(h) XOR bit6(h): the quadrant, spelled in two
      instructions.
   2. ⚠ `compute_car_angles` HAS FIVE BYTES OF DEAD CODE, $0D21-$0D25: `BCC $0D27` at $0D1D and
      `BCS $0D4F` at $0D1F are together unconditional, so the low-byte tie-break under them can
      never run.  Reproduced anyway (it costs nothing and the differential would not see it
      either way), but named here so nobody re-derives it.
   3. ⚠⚠ `update_engine_revs` CONSUMES THE CALLER'S CARRY.  The coast arm's `ADC #7` at $49A6
      is reached through six instructions that write no carry at all, so what it adds is
      7 + whatever C apply_driving_model left in `stage_accum_delta`'s wake.  That is not a
      readable design and it is exactly what a randomised differential catches.
   4. `update_grip_limits` GIVES THE TWO AXLES OPPOSITE SIGNS of the load term: $4C52's
      `ADC $78,X` reaches hypot_min_lo for axle 0 and hypot_min_hi for axle 1, and those two
      cells hold -(load) and +(load) from $4BE1-$4BE8.  One `,X` on a zero-page address is the
      whole of the front/rear split.
   5. ⚠⚠ AND THE CHANGED-SURFACE ARM IS DEAD ON THIS RELEASE.  `surface_change_0`/`_1` are
      $00 in disasm/revs_runtime.bin and mid-race on both Silverstone and Brands, nothing in
      the image writes them, and no circuit patches the operands — so `grip_disturbance` is
      always 0, `grip_limit_base_alt_tbl` is never read and the unprompted `begin_spin` never
      fires.  The twin keeps all of it and the FIXTURE FORCES the arm, because randomised
      memory reaches $FF in both bytes once in 65536.  (MEASURED 2026-08-18; the addresses sit
      inside the dashboard bitmap the second unpack drops at $70DB-$7813, which is worth one
      reference-loop check before calling it dead for good.)
   6. ⚠ TWO THINGS HERE CANNOT BE SABOTAGED, and both are properties of the code rather than
      holes in the fixture (docs/validation-harness.md §FIFTEENTH):
        * the `AND #$FE` in BOTH of `compute_car_angles`' arms is defensive — the value comes
          out of an `ASL` in one and out of `0 - (an ASL result)` in the other, so bit 0 is
          already 0.  Checked on both, which is what separates it from a coverage hole;
        * `update_engine_revs`' power curve is CONTINUOUS at all three breakpoints ($BA at the
          first, $B6 at the second, $A2 at the third), so moving one by one changes nothing.
          The curve is covered by sabotages that move a segment's OFFSET or SLOPE instead.
   =========================================================================== */

#define SECTION_DIR_IX 0x0700u   /* per live section, its index into the three pages above */
#define SECTION_CRD_LO 0x0900u   /* section_coord_lo / _hi — the live section geometry */
#define SECTION_CRD_HI 0x0A00u
#define CAR_STATE_1    0x0164u   /* per-driver; the camera adds a gradient-scaled copy */
#define CAR_SPEED_SCL  0x0150u   /* per-driver speed in the AI's units */
#define GEAR_REV_RATIO 0x5A06u   /* TRACK FILE: revs per unit road speed, by gear_index */
#define GEAR_TORQUE    0x5A0Du   /* TRACK FILE: the per-gear torque multiplier */
#define WING_GRIP      0x62A8u   /* the two per-wing downforce coefficients */
#define GRIP_LIMIT     0x62AAu   /* the two per-axle thresholds check_wheel_slip compares to */
#define GRIP_LIMIT_ALT 0x62ACu   /* ...and the second threshold beside them */
#define GRIP_BASE      0x4C61u   /* the constant in each axle's threshold, $35/$35 */
#define GRIP_BASE_ALT  0x4C63u   /* ...and its changed-surface replacement, $19/$1A */
#define VIA_T1_LOW     0xFE68u   /* User VIA T1 counter low — the engine's randomness */

/* ---------------------------------------------------------------------------
   $0D01  compute_car_angles — THE SIN/COS PAIR  (twin #79)
   ---------------------------------------------------------------------------
   Takes the player's heading in A (high) and X (low) and leaves car_angle elements 0 and 1 —
   the pair every `apply_angle_term` in the tree multiplies a state element by.  One angle
   evaluation, run twice: first on h = heading x pi, then on its reflection $C900 - h, with bit
   6 of the heading's high byte choosing which element gets which.  The tail ORs the quadrant's
   two sign bits into bit 0 of each low byte, which is where the sign lives for this pair.

   ⭐ WRITTEN AS PLAIN 16-BIT C.  Every ADC/SBC/ROR byte-pair chain here is ordinary binary
   arithmetic with D = 0 (the driving model's precondition — docs/static-map.md §Decimal mode),
   the three `mul8` steps are `(a * b) >> 8`, and the two loop passes become an explicit
   two-iteration loop over the reflected angle.  The five dead bytes at $0D21 (group header,
   item 2) simply do not appear.  The routine leaves only car_angle[0]/[1]; its exit registers
   and its arithmetic scratch ($42, $74-$79, $7B) are dead — apply_driving_model's next call,
   rotate_state_0_into_8, opens by reloading X, Y and A — so the fixture verifies the four
   output bytes only.
   --------------------------------------------------------------------------- */
static void compute_car_angles_core(uint8_t headingHi, uint8_t headingLo)
{
    /* $0D01-$0D0C — h = heading x pi, as a 16-bit value ($C9/256 x 4 = pi to three figures). */
    uint16_t scaled = (uint16_t)(((headingHi << 8) | headingLo) << 2);
    uint16_t h      = (uint16_t)(((uint32_t)scaled * 0xC9u) >> 8);

    /* $0D0E-$0D19 — bit 6 of the heading's high byte decides which element the first pass
       writes; the second pass takes the other. */
    int elem[2];
    elem[0] = (headingHi & 0x40u) ? 1 : 0;
    elem[1] = elem[0] ^ 1;

    for (int pass = 0; pass < 2; pass++) {
        uint8_t lo, hi;
        if ((uint8_t)(h >> 8) < 0x7Au) {
            /* $0D27-$0D4C — the SMALL-ANGLE arm: (h - ($AB/256) h^3) doubled.  Three
               multiplies by h's high byte, the last of them a 16x8 fixed-point step. */
            uint8_t  hh   = (uint8_t)(h >> 8);
            uint16_t cube = (uint16_t)(0xABu * hh);              /* $AB * h_hi */
            cube = (uint16_t)((cube >> 8) * hh);                 /* x h_hi */
            cube = (uint16_t)(((uint32_t)cube * hh) >> 8);       /* x h_hi, >> 8 */
            uint16_t res = (uint16_t)((h - cube) << 1);          /* the sine, doubled */
            lo = (uint8_t)(res & 0xFEu);   /* bit 0 belongs to the sign the tail ORs in */
            hi = (uint8_t)(res >> 8);
        } else {
            /* $0D4F-$0D7C — the LARGE-ANGLE arm: d = $C900 - h, then -(2 x d x d_hi >> 8),
               SATURATED to -2 ($FFFE) when that doubled term is zero (the negate does not
               borrow). */
            uint16_t d  = (uint16_t)(0xC900u - h);               /* the reflection */
            uint16_t dd = (uint16_t)(((uint32_t)d * (uint8_t)(d >> 8)) >> 8);
            uint16_t d2 = (uint16_t)(dd << 1);
            uint16_t res = (uint16_t)(0u - d2);                  /* negate */
            lo = (uint8_t)(res & 0xFEu);
            hi = (uint8_t)(res >> 8);
            if (d2 == 0u) { lo = 0xFEu; hi = 0xFFu; }            /* the saturation */
        }
        mem[CAR_ANGLE_LO + elem[pass]] = lo;
        mem[CAR_ANGLE_HI + elem[pass]] = hi;

        h = (uint16_t)(0xC900u - h);   /* $0D85-$0D90 — reflect about pi/2 for the second pass */
    }

    /* $0D97-$0DB2 — the quadrant, as two sign bits: bit 7 of the heading's high byte for
       element 0, bit 7 XOR bit 6 for element 1. */
    if (headingHi & 0x80u)                    mem[CAR_ANGLE_LO + 0] |= 0x01u;
    if (((headingHi << 1) ^ headingHi) & 0x80u) mem[CAR_ANGLE_LO + 1] |= 0x01u;
}

/* ---------------------------------------------------------------------------
   $4610  scale_by_track_gradient — A x THE TRACK'S GRADIENT  (twin #80)
   ---------------------------------------------------------------------------
   A x |track_dir_1[Y]| / 256, re-signed by track_dir_1[Y] EOR track_direction.  Both of
   update_camera_and_drive_state's camera terms go through it: the yaw-derived one and the
   player's own car_state_1, which is what makes an across-track offset raise the camera on a
   banked section (the CAMBER reading in docs/rename.md).

   ⭐ WRITTEN AS PLAIN C.  The 6502 carried the EOR's sign across the abs8+multiply on the stack
   (PHP $4617 / PLP $4621) only because abs8 branches on the CALLER's N; in C the two signs are
   just two bytes.  |grad| ≤ 0x80 and value ≤ 0xFF, so the product's high byte is ≤ 0x7F — bit 7
   is always clear — which is why the re-signed high byte's bit 7 IS the exit N in both sign arms
   (the negative arm sets N from -(high); the positive arm's restored EOR status has N = eor.7 = 0
   = high.7).  Returns that re-signed high byte; math_lo/math_hi are set as the oracle does.  The
   full exit flag ABI (N/Z/C/V, with C/V passing through the caller's in the positive arm) is
   replayed in the shim; the two native callers here consume the returned byte directly. */
static uint8_t scale_by_track_gradient_core(uint8_t value, uint8_t index)
{
    uint8_t gradByte = mem[TRACK_DIR_1 + index];                        /* $4612/$4618 */
    uint8_t eor      = (uint8_t)(gradByte ^ track_direction);           /* $4614 — the re-sign */
    uint8_t mag      = (gradByte & 0x80u)                               /* $461B abs8: |grad| */
                       ? (uint8_t)(-(int)gradByte) : gradByte;
    math_hi = value;                                                    /* $4610 (scratch) */
    /* $461E — |gradient| x value >> 8.  D = 0 on the camera path (docs/static-map.md
       §Decimal mode), so a plain 16-bit multiply; mag·value ≤ 0x7F80 fits. */
    unsigned p = revs_mulu16(mag, value);
    math_lo = (uint8_t)p;                                               /* low byte, never re-signed */
    uint8_t high = (uint8_t)(p >> 8);                                   /* ≤ 0x7F */
    return (eor & 0x80u) ? (uint8_t)(-(int)high) : high;                /* $4622 abs8: re-sign high */
}

/* ---------------------------------------------------------------------------
   $4DC9 / $4DCB  begin_spin — THE CAR LOSES CONTROL  (twins #81, #82)
   ---------------------------------------------------------------------------
   Seeds the two decaying counters from a severity (road_speed at the $4DC9 entry), nudges the
   frame's heading increment by $80 and halves it, marks drive_state as not-under-power and
   queues sound slot 4 — the same slot check_crash's crash arm queues.  It is the milder
   sibling of that arm: nothing here stops the engine or clears the model.
   --------------------------------------------------------------------------- */
static SpinExit begin_spin_from_a_core(uint8_t severity, uint8_t savedX)
{
    spin_countdown = (uint8_t)(severity >> 1);  /* $4DCC — severity / 2 */
    spin_shake     = (uint8_t)(severity >> 2);  /* $4DCE-$4DCF — ...and / 4 */
    drive_state    = (uint8_t)(drive_state + 1u);  /* $4DD1 — mark not-under-power */
    /* $4DD4 SEC / ROR heading_step_lo — nudge the heading increment by $80 and halve it (C in = 1
       sets bit 7; the ROR's own carry-out is dead, overwritten by the sound tail below). */
    heading_step_lo = (uint8_t)((heading_step_lo >> 1) | 0x80u);
    /* $4DD7-$4DD9 LDA #4 / JSR sound_queue_default — slot 4 at sound_volume.  This is the last thing
       begin_spin does, so its exit ABI IS sound_queue_default's; the caller's X (untouched here) is
       what sound_queue parks in sound_saved_x.  Return the residue the update_camera caller reads:
       the OSWORD's exit Y and the block ADD's C/V; the begin_spin shims replay the full exit ABI. */
    SpinExit e;
    e.y = sound_queue_core(0x04u, sound_volume, savedX);
    { BlockCV cv = sound_queue_block_cv(0x04u); e.c = cv.c; e.v = cv.v; }
    return e;
}

/* ---------------------------------------------------------------------------
   $4C65  apply_drag_terms — TWO SPEED-DEPENDENT TERMS  (twin #83)
   ---------------------------------------------------------------------------
   The first sub-model past the off-power gate.  Term one: |model_accum_entry_hi| floored at
   road_speed (and DOUBLED while grip_disturbance is non-zero), squared against the same
   magnitude, added into state element 6.  Term two: (road_speed x wing_drag_coeff + 8) times
   term one's pre-square magnitude, added into element 7.  Both grow with speed, both take
   their SIGN from a cell rather than from themselves — element 6's from the accumulator's
   entry value, element 7's from car_speed_hi — because add_signed_into_element branches on the
   caller's N.

   ⚠ It reads the accumulator as it was on ENTRY to apply_driving_model, not as this frame's
   sub-models left it.
   --------------------------------------------------------------------------- */
static void apply_drag_terms_core(void)
{
    /* $4C65-$4C6A — |model_accum_entry_hi| (abs8 on its own bit 7; $80 stays $80). */
    uint8_t entry = model_accum_entry_hi;
    uint8_t magnitude = (entry & 0x80u)          /* $4C6A — the magnitude, before the floor */
                        ? (uint8_t)(-(int)entry) : entry;
    math_hi = magnitude;

    /* $4C6B-$4C77 — floored at the road speed, doubled on a disturbed surface. */
    uint8_t term1 = magnitude;
    if (term1 < road_speed)     term1 = road_speed;
    if (grip_disturbance != 0u) term1 = (uint8_t)(term1 << 1);
    shared_temp_77 = term1;                      /* $4C77 — kept for term two */

    /* $4C79-$4C7C — term1 squared against the pre-floor magnitude. */
    { unsigned p = revs_mulu16(term1, magnitude);
      math_lo = (uint8_t)p;
      math_hi = (uint8_t)(p >> 8); }

    /* $4C7E-$4C82 — into element 6, with model_accum_entry_hi's bit 7 as the sign. */
    add_signed_into_element_core(6u, model_accum_entry_hi);

    /* $4C85-$4C92 — (road_speed x wing_drag_coeff) + 8, high byte held for the fixed-point step. */
    { unsigned p = revs_mulu16(wing_drag_coeff, road_speed);
      math_lo        = (uint8_t)p;
      shared_temp_76 = (uint8_t)((p >> 8) + 0x08u);   /* ADC #$08 (its carry-out is dropped) */
    }

    /* $4C94-$4C98 — (shared_temp_76 : math_lo) x term1 >> 8 (x.8 fixed point). */
    { uint8_t  m       = term1;
      unsigned lowProd = revs_mulu16(math_lo, m);
      uint8_t  lowHigh = (uint8_t)(lowProd >> 8);
      unsigned result  = revs_mulu16(shared_temp_76, m) + lowHigh;
      shared_temp_77 = lowHigh;                  /* mul8_accum's scratch residue */
      math_hi = (uint8_t)(result >> 8);
      math_lo = (uint8_t)result; }

    /* $4C9B-$4CA0 — into element 7, with car_speed_hi's bit 7 as the sign. */
    add_signed_into_element_core(7u, car_speed_hi);
}

/* ---------------------------------------------------------------------------
   $4BCF  update_grip_limits — THE TWO PER-AXLE THRESHOLDS  (twin #84)
   ---------------------------------------------------------------------------
   Rebuilt every frame from three things: the load term damp_and_derive_loads left in
   wheel_load (only while the BRAKE is down — on the throttle or coasting the term is 0), the
   road speed through wing_grip_coeff, and the two surface bytes.

   ⭐ THE FRONT/REAR SPLIT IS ONE `,X`.  $4BE1-$4BE8 stores the shifted load term in
   hypot_min_hi and its NEGATIVE in hypot_min_lo, and $4C52's `ADC $78,X` picks between them by
   axle — so the load shifts grip onto one axle and off the other, which is what weight
   transfer looks like.

   ⚠⚠ The changed-surface arm ($4C06-$4C21) is dead on this release; see the group header.
   Kept whole, hardware read included, and the fixture forces it.

   ⭐ WRITTEN AS PLAIN 8/16-BIT C.  With D = 0 (docs/static-map.md §Decimal mode) the byte-pair
   arithmetic is ordinary binary: the load-term shift is an arithmetic `>> 3`, `mul8` is
   `(a * b) >> 8`, and abs8 is a sign test on car_speed_hi.  The load term lives in two locals
   rather than in hypot_min_lo/hi, and the surface-AND in one rather than shared_temp_77 — all
   of which are dead scratch here (update_engine_revs, the next call, opens with `LDA
   engine_running`).  The outputs are grip_disturbance, grip_limit[0..1] and grip_limit_alt[0..1]
   plus whatever begin_spin writes; the fixture verifies those. */
void update_grip_limits_core(void)
{
    int axle;

    /* $4BD1 — LDY pedal_mode.  Dead: the only thing that could read it before the axle loop's own
       LDY ($4C46) is begin_spin's sound call, but that reaches OSWORD 7 which loads its own Y (the
       control-block pointer) before any read — so nothing observes this Y. */

    /* $4BCF-$4BE8 — the load term (only while braking), and its negative for the other axle.
       The three LSRs sign-extend wheel_load, i.e. an arithmetic `>> 3`. */
    int8_t load = 0;
    if (pedal_mode == 0u)                                  /* 0 = the brake */
        load = (int8_t)((int8_t)wheel_load >> 3);
    uint8_t loadForAxle[2];
    loadForAxle[0] = (uint8_t)(-(int)load);               /* axle 0 gets -(load)... */
    loadForAxle[1] = (uint8_t)load;                       /* ...axle 1 gets +(load) */

    /* $4BEE-$4C24 — has the surface changed?  $FF in EITHER surface byte opens the (dead-on-
       this-release) disturbance arm; $FF in BOTH also swaps in the alternate grip base below.
       The begin_spin test reads grip_disturbance as it was BEFORE this frame's write. */
    uint8_t surfaceBoth = (uint8_t)(surface_change_0 & surface_change_1);   /* $4BF0-$4BF6 */
    uint8_t oldDisturb  = grip_disturbance;
    uint8_t newDisturb  = 0u;
    if (surface_change_0 == 0xFFu || surface_change_1 == 0xFFu) {
        newDisturb = (uint8_t)((((unsigned)bus_read(VIA_T1_LOW) * road_speed) >> 8) & 0x07u);
        if (newDisturb == 0u) newDisturb = 1u;            /* $4C0F — never 0 once the arm runs */
        if (oldDisturb == 0u && drive_state == 0u && (section_jump_history & 0x80u)) {
            /* begin_spin ($4DC9) is reached with X still holding the disturbance value, which its
               sound_queue parks in sound_saved_x ($0B46) — passed explicitly as begin_spin's savedX. */
            begin_spin_from_a_core(road_speed, newDisturb);    /* $4C21 */
        }
    }
    grip_disturbance = newDisturb;                         /* $4C24 */

    /* $4C26-$4C5E — axle 1 then axle 0.  Each threshold is a saturated speed term through the
       wing's downforce, re-signed by the car's direction, plus a base and this axle's load. */
    for (axle = 1; axle >= 0; axle--) {
        uint8_t speed = road_speed;
        if (speed >= 0x35u) speed = 0x35u;                /* the speed term saturates */
        uint8_t term = (uint8_t)(((unsigned)mem[WING_GRIP + axle] * speed) >> 8);
        if (car_speed_hi & 0x80u)                         /* abs8: sign from car_speed_hi */
            term = (uint8_t)(-(int)term);

        uint8_t base = (surfaceBoth == 0xFFu)             /* $4C4A — BOTH surface bytes $FF */
                       ? mem[GRIP_BASE_ALT + axle]
                       : (uint8_t)(term + mem[GRIP_BASE + axle]);
        uint8_t limit = (uint8_t)(base + loadForAxle[axle]);
        mem[GRIP_LIMIT + axle]     = limit;               /* $4C54 */
        mem[GRIP_LIMIT_ALT + axle] = (uint8_t)(((unsigned)limit * 0xF3u) >> 8);  /* $4C57-$4C5A */
    }

    /* $4C46 — each axle iteration reloads Y from the ANDed surface bytes (0xFF in the both-$FF arm,
       which is the same value), and mul8 preserves it, so the loop's last pass (axle 0) leaves
       Y = surfaceBoth as the routine's exit Y.  That Y survives update_engine_revs and is what
       update_slip_sound's OSBYTE 21 logs — the one escaping register.  It is NOT part of this
       result-only fixture, so it is reconstructed by the caller that consumes it (the shim, and
       apply_driving_model_core at the update_slip_sound seam) rather than written here. */
}

/* ---------------------------------------------------------------------------
   $49CE  update_engine_revs — THE ENGINE  (twin #85)
   ---------------------------------------------------------------------------
   Four things in one routine, and which one runs is decided in the first eight bytes:

     the STARTER POLL ($4978) with the engine stopped — the T key, and a 1-in-8 chance a frame
       from the User VIA timer (1-in-32 after a crash: check_crash raises starter_random_mask);
     the COAST ARM ($499F) whenever the car is not under power, in the pits, or the gears were
       shifted this frame — revs creep up by 7 toward pedal_amount on the throttle, or fall by
       $0C to an idle floor of $28, plus 0..7 of timer jitter;
     the REV MODEL ($49DF) — road speed scaled by gear_rev_ratio_tbl, x2 or x4 depending on
       whether the first doubling overflowed (the PHP at $49E8 is what remembers that), with
       the gear-change rev drop layered on top through engine_revs_prev;
     the TAIL ($4A48) — a FOUR-SEGMENT piecewise power curve in the clamped revs, times
       gear_torque_tbl, into engine_torque; and engine_note_target = revs + $19.

   ⚠ THE STALL: below 3 revs the routine INCs engine_running from $FF to 0 and jumps into the
   coast arm's tail, so the next frame takes the starter poll instead.
   ⚠⚠ `ADC #7` at $49A6 adds the CALLER'S CARRY — see the group header, item 3.
   ⚠ $4A37 stores the UNCLAMPED revs and clamps only the copy the power curve reads, so
   engine_revs can exceed $AA even though the curve never sees more than that.
   --------------------------------------------------------------------------- */

/* $4A87-$4A90 — the tail EVERY arm reaches, with A carrying the torque to store.
   ⚠ The off-power arms jump to $4A87, not to $4A7F: they store a zero torque and never touch
   math_lo/math_hi at all.  Getting that boundary wrong is a twin that zeroes the arithmetic
   window on seven paths out of eight, which is exactly what the differential said. */
static AddFlags engine_note_only(uint8_t torque)
{
    /* $4A89-$4A8E — engine_revs + $19.  This is the RTS tail on every arm, so the ADD's
       N/V/Z/C are the routine's exit flags and A is its exit value: return them (D=0). */
    unsigned sum = (unsigned)engine_revs + 0x19u;
    uint8_t a;
    AddFlags f;
    engine_torque = torque;                             /* $4A87 */
    a = (uint8_t)sum;
    engine_note_target = a;
    f.hi = a;
    f.carry = (uint8_t)(sum > 0xFFu);
    f.overflow = (uint8_t)(((~(engine_revs ^ 0x19u) & (engine_revs ^ a)) >> 7) & 1u);
    f.neg = (uint8_t)((a >> 7) & 1u);
    f.zero = (uint8_t)(a == 0);
    return f;
}

/* $4A7F-$4A90 — the curve's output through the gear's torque multiplier, then that tail.
   Reached only from the power curve. */
static AddFlags engine_torque_and_note(uint8_t curve, uint8_t gear)
{
    unsigned p;
    math_hi = curve;                                    /* $4A7F */
    /* $4A81-$4A84 — curve x gear torque >> 8; only the high byte is used (engine_note_only takes
       it as the torque), the multiply's flags are dead, and the rev model is always D=0. */
    p = revs_mulu16(mem[GEAR_TORQUE + gear], math_hi);
    math_lo = (uint8_t)p;
    return engine_note_only((uint8_t)(p >> 8));
}

/* $49BB-$49C7 — the revs land as `base` plus 0..7 of User VIA jitter.  Three arms reach it. */
static void engine_revs_from(uint8_t base)
{
    uint8_t jitter = (uint8_t)(bus_read(VIA_T1_LOW) & 0x07u);       /* $49BD-$49C1 */
    uint8_t a      = (uint8_t)(jitter + base);                      /* $49C3 — ADC (D=0) */
    math_lo = base;                                                 /* $49BB */
    /* The ADD's exit A/flags are ALWAYS overwritten by a following engine_note_only (every arm
       that reaches this later reruns that tail), so only the two rev cells escape. */
    engine_revs      = a;                                           /* $49C5 */
    engine_revs_prev = a;                                           /* $49C7 */
}

/* $499F-$49B9 — the COAST ARM: revs creep up by 7 toward pedal_amount on the throttle, or fall
   by $0C to an idle floor of $28.  ⚠⚠ `ADC #7` adds the CALLER'S CARRY — nothing between the
   entry and it writes C.  See the group header, item 3. */
static uint8_t engine_coast_arm(uint8_t carryIn)
{
    uint8_t a = engine_revs;                                        /* $499F */
    uint8_t x = (uint8_t)(pedal_mode - 1);       /* $49A1-$49A3 — LDX pedal_mode; DEX (X escapes) */
    if (x == 0) {                                /* $49A4 — pedal_mode == 1: on the throttle */
        a = (uint8_t)(a + 0x07u + (carryIn ? 1u : 0u));  /* $49A6 — ADC adds the CALLER'S carry (D=0) */
        if (a < pedal_amount && a < 0x8Cu) {     /* $49A8-$49AE — creeping up, still in range */
            engine_revs      = a;                /* $49C5 — an arm that reaches engine_note_only next,
                                                    so the CMP flags here are dead */
            engine_revs_prev = a;
            return x;
        }
    }
    if (a >= 0x2Au) a = (uint8_t)(a - 0x0Cu);   /* $49B2-$49B5 — fall $0C toward idle (flags dead) */
    else            a = 0x28u;                          /* $49B9 — ...or sit on the floor */
    engine_revs_from(a);
    return x;
}

/* $4993-$499B — the engine catches: the luck mask back to 7, engine_running to $FF.  A is
   untouched, which is what the arm below hands to engine_revs_from. */
static void engine_catches(void)
{
    starter_random_mask = 0x07u;                                    /* $4993-$4995 LDX #7 */
    engine_running      = 0xFFu;                    /* $4997-$4999 LDX #$FF — leaves X=$FF; the
                                                       starter poll replays that as its exit X */
}

/* $4978-$499B — the STARTER POLL, which is where the whole routine goes while the engine is
   stopped.  The T key alone is not enough: without it a car in gear and rolling push-starts,
   and with it the engine catches only on a frame the User VIA timer allows
   (1-in-8 normally, 1-in-32 after a crash — check_crash raises starter_random_mask). */
static EngineRegs engine_starter_poll(void)
{
    EngineRegs r;
    /* $4978-$497D — is the T key held?  kbd_test_key_regs does OSBYTE 129 (documented MOS
       exception) and hands back the MOS's answer registers; X = $FF when the key is down. */
    MosRegs kr = kbd_test_key_regs(0xDCu);
    int keyDown = (kr.x == 0xFFu);
    if (!keyDown) {                              /* $497D BNE — T NOT held: the push-start path */
        uint8_t y = (uint8_t)(gear_index - 1);   /* $497F-$4980 — LDY gear_index; DEY (Y escapes) */
        if (y != 0 && road_speed != 0) {         /* $4982-$4986 — in gear and rolling */
            engine_catches();                    /* leaves X = $FF */
            engine_revs_from(road_speed);           /* A is still road_speed past catches */
            r.x = 0xFFu; r.y = y;
            return r;
        }
        engine_revs      = 0x00u;                                  /* $4988-$498A */
        engine_revs_prev = 0x00u;
        r.x = kr.x;                              /* MOS ABI — X as OSBYTE 129 left it */
        r.y = y;
        return r;
    }
    {
        uint8_t luck = (uint8_t)(bus_read(VIA_T1_LOW) & starter_random_mask);  /* $498C-$498F */
        r.x = kr.x;                              /* MOS ABI — X as OSBYTE 129 left it */
        r.y = kr.y;                              /* MOS ABI — Y untouched in this arm */
        if (luck == 0) { engine_catches(); r.x = 0xFFu; }          /* $4991 — no luck this frame */
        engine_revs_from(luck);
        return r;
    }
}

EngineExit update_engine_revs_core(uint8_t carryIn, uint8_t entryY)
{
    EngineExit e;
    uint8_t gear;
    uint8_t a;
    int segment0;

    if (engine_running == 0) {                                     /* $49CE-$49D0 → $4978 */
        EngineRegs r = engine_starter_poll();
        e.tail = engine_note_only(0x00u);                          /* $49C9-$49CB */
        e.x = r.x; e.y = r.y;
        return e;
    }
    if (drive_state != 0) {                                         /* $49D2-$49D4 → $499F */
        e.x = engine_coast_arm(carryIn);
        e.y = entryY;                                    /* coast arm never touches Y */
        e.tail = engine_note_only(0x00u);
        return e;
    }
    if (gear_change_flag & 0x80u) {                                 /* $49D6-$49D8 → $499D */
        gear_change_rev_drop = gear_change_flag;                    /* $499D */
        e.x = engine_coast_arm(carryIn);
        e.y = entryY;
        e.tail = engine_note_only(0x00u);
        return e;
    }
    {
        uint8_t y0 = (uint8_t)(gear_index - 1);     /* $49DB-$49DC — LDY gear_index; DEY (Y escapes) */
        if (y0 == 0) {                                             /* $49DD — gear 1: the pits */
            e.x = engine_coast_arm(carryIn);
            e.y = 0x00u;                            /* Y = gear_index - 1 = 0 */
            e.tail = engine_note_only(0x00u);
            return e;
        }
        e.y = y0;   /* $49DC leaves Y = gear_index-1; the rev-drop block may overwrite it */
    }

    /* $49DF-$4A00 — the rev model.  A 16-bit (road_speed:frac) is shifted left once, then again
       only if it stayed non-negative; the PHP at $49E8 remembers that "stayed non-negative", and
       the guarded shift is applied ONCE MORE after the gear ratio — so the scaling is x2 or x4. */
    gear = gear_index;                                             /* $49EF LDX gear_index — exit X */
    {
        uint16_t v = (uint16_t)((((uint16_t)road_speed << 8) | road_speed_frac) << 1); /* $49E5-$49E7 */
        int stayedPos = (v & 0x8000u) == 0;                         /* $49E8 PHP — ROL A's N */
        uint16_t p, w;
        if (stayedPos) v = (uint16_t)(v << 1);                      /* $49E9 BMI */
        math_hi = (uint8_t)(v >> 8);                                /* $49EE */
        /* $49F2-$49F5 — that high byte x the gear's rev ratio (D=0, multiply flags dead). */
        p = (uint16_t)revs_mulu16(mem[GEAR_REV_RATIO + gear], math_hi);
        w = (uint16_t)(p << 1);                                     /* $49F8-$49FA */
        if (!stayedPos) w = (uint16_t)(w << 1);                     /* $49FC BPL — the OPPOSITE guard:
                                                    the two guarded shifts straddle the multiply,
                                                    exactly one fires (the PLP restores $49E8's N) */
        math_lo = (uint8_t)w;
        a       = (uint8_t)(w >> 8);
    }

    /* $4A01-$4A35 — the gear-change rev drop, if a shift armed it: only on the throttle, only
       nearly stopped, and only once the starting lights are done (or on one $3F-frame phase of
       them).  When it applies, the revs come from engine_revs_prev decaying by 2 a frame.
       ⚠ the block also sets the escaping Y (e.y): road_speed / start_light_state provisionally,
       then 0 when it disarms; only the !disarm arm keeps start_light_state. */
    if (gear_change_rev_drop & 0x80u) {                            /* $4A01-$4A03 BIT/BPL */
        int disarm = 1;
        if ((uint8_t)(pedal_mode - 1) == 0) {       /* $4A05-$4A08 Y=pedal_mode-1; BNE — on the throttle */
            e.y = road_speed;                                     /* $4A0A */
            if (road_speed < 0x16u) {                             /* $4A0C-$4A0E — nearly stopped */
                int compare = 0;
                e.y = start_light_state;                          /* $4A10 */
                if (!(start_light_state & 0x80u)) compare = 1;    /* $4A12 — lights are done */
                else if (start_light_state == 0xA0u &&            /* $4A14-$4A16 */
                         (uint8_t)(loop_counter & 0x3Fu) >= 0x35u)/* $4A18-$4A20 (a preserved: PHA/PLA) */
                    compare = 1;
                if (compare && a < engine_revs_prev) {            /* $4A22-$4A24 */
                    a = engine_revs_prev;                         /* $4A2C — revs decay from prev */
                    if (engine_revs_prev >= 0x6Cu) {              /* $4A2E-$4A30 */
                        a = (uint8_t)(engine_revs_prev - 0x02u);  /* $4A32 (flags dead) */
                        engine_revs_prev = a;                     /* $4A35 */
                    }
                    disarm = 0;
                }
            }
        }
        if (disarm) { e.y = 0x00u; gear_change_rev_drop = 0x00u; } /* $4A26-$4A28 */
    }

    /* $4A37-$4A45 — the revs land, then the CURVE'S input is clamped and the stall tested.
       ⚠ engine_revs itself keeps the UNCLAMPED value: only the copy the curve reads is held to $AA. */
    engine_revs = a;                                               /* $4A37 — the UNCLAMPED value */
    if (a >= 0xAAu) a = 0xAAu;                                     /* $4A39-$4A3B — the curve's input clamps */
    e.x = gear;                                                    /* $4A3D — exit X = gear_index */
    if (a < 0x03u) {                                              /* $4A3F-$4A41 */
        inc_mem(MEM_engine_running);                               /* $4A43 — $FF -> 0: STALL */
        e.tail = engine_note_only(0x00u);                         /* $4A45 JMP $49C9 */
        return e;
    }

    /* $4A48-$4A7D — the power curve: four straight segments in (revs - $42), breaking at
       $11, $15 and $1A above it, each one a shift and an add.  The result carries on to
       engine_torque_and_note, which reloads the flags — so only the VALUE matters (D=0). */
    {
        uint8_t x = (uint8_t)(a - 0x42u);                         /* $4A48-$4A49 */
        if (x & 0x80u) segment0 = 1;                              /* $4A4B BMI — below $42 */
        else segment0 = (x < 0x11u);                              /* $4A4D-$4A4F */
        if (segment0) {
            a = (uint8_t)((uint8_t)(x << 1) + 0x98u);             /* $4A51-$4A53 */
        } else {
            x = (uint8_t)(x - 0x11u);                             /* $4A58-$4A59 */
            if (x < 0x04u) {                                      /* $4A5B-$4A5D */
                a = (uint8_t)((uint8_t)(x ^ 0xFFu) + 0xBBu);      /* $4A5F-$4A62 */
            } else {
                x = (uint8_t)(x - 0x04u);                         /* $4A66-$4A67 */
                if (x < 0x05u) {                                  /* $4A69-$4A6B */
                    a = (uint8_t)((uint8_t)((uint8_t)(x << 2) ^ 0xFFu) + 0xB7u);  /* $4A6D-$4A72 */
                } else {
                    x = (uint8_t)(x - 0x05u);                     /* $4A76-$4A77 */
                    a = (uint8_t)((uint8_t)((uint8_t)(x << 1) ^ 0xFFu) + 0xA3u);  /* $4A79-$4A7D */
                }
            }
        }
    }
    e.tail = engine_torque_and_note(a, gear);
    return e;
}

/* ---------------------------------------------------------------------------
   $44EA  update_camera_and_drive_state — THE LAST SUB-MODEL  (twin #86)
   ---------------------------------------------------------------------------
   294 bytes and four jobs, in this order:

     (a) with drive_state non-zero it does nothing but DEC spin_shake twice and jump to (c);
     (b) camera_pitch_bias, a signed $FB..3 counter: +1 a frame under power, -1 braking, and
         settling toward 0 in neutral or coasting;
     (c) THE SECTION YAW.  A cheap atan2 over the section's own direction vector — the
         smaller-magnitude component scaled by 0.375 and folded through three saved flags —
         minus car_heading_hi, giving section_yaw and its folded magnitude view_yaw_offset;
         then the frame's view_pitch_offset as a first-order low pass over the yaw term,
         grip_disturbance, camera_pitch_bias and spin_shake, and view_pitch_delta from it;
     (d) DRIVE_STATE ITSELF, from spin_countdown stepped -4 a frame with a saturating jump to
         $C8; and finally the camera, which is the section's own coordinate 1 plus a
         gradient-scaled car_state_1 plus $AC (nominal eye height), and car_speed_scaled.

   ⚠⚠ THE THREE PHPs at $453C/$4540/$4546 ARE PULLED IN REVERSE, and two of the three exist
   only to steer an abs8 that branches on the caller's N.  The pushed bytes are part of the
   differential; so are $45E5/$45E9's, which carry two carries past an intervening add.
   ⚠ SMC $45CB: every expansion circuit replaces the first `ASL A / ROL shared_temp_77` pair
   with a JSR into its own hook, so on those circuits the camera's high byte is built by the
   circuit's code instead.
   --------------------------------------------------------------------------- */
CameraExit update_camera_and_drive_state_core(void)
{
    CameraExit e = {{0,0,0,0,0},0,0};
    uint8_t dirIndex;
    uint8_t yScale;         /* Y carried $452F→$45D8; a spin's MOS sound may overwrite it (see below) */

    if (drive_state != 0) {                             /* $44EA-$44EC */
        spin_shake = (uint8_t)(spin_shake - 1u);        /* $44EE — DEC, flags dead (yaw reloads) */
        spin_shake = (uint8_t)(spin_shake - 1u);        /* $44F0 */
    } else {
        spin_shake     = 0x00u;                         /* $44F5 — both zeroed (drive_state==0) */
        spin_countdown = 0x00u;

        /* $44F9-$452A — camera_pitch_bias, a signed $FB..3 counter: +1 a frame under power,
           -1 braking, and settling toward 0 in neutral or coasting.  Every register here is
           dead by the yaw: merge below (A/X/Y are all reloaded), so it is plain byte math. */
        uint8_t bias = camera_pitch_bias;               /* $44FB */
        int up = 0, down = 0, settle = 0;
        if (gear_index == 0) settle = 1;                /* $44FC-$44FE */
        else if (pedal_mode & 0x80u) settle = 1;        /* $4500-$4502 — coasting */
        else if (pedal_mode == 0) {                     /* $4504 — the brake */
            if (road_speed != 0) down = 1;              /* $450C-$450E */
            else settle = 1;
        } else {                                        /* on the throttle */
            if (engine_torque != 0) up = 1;             /* $4506-$4508 — pulling */
            else settle = 1;
        }
        if (settle) {                                   /* $4510-$4515 — drift back to centre */
            if (bias == 0) goto yaw;                    /* already centred: nothing to store */
            if (!(bias & 0x80u)) down = 1;              /* positive: step down */
            else { bias++; up = 1; }                    /* $4515 — negative: +2 */
        }
        if (up) {                                       /* $4516-$451F */
            bias++;
            if (!(bias & 0x80u)) {                      /* positive: CPY #$04 (carry dead here) */
                if (bias >= 0x04u) bias = 0x03u;        /* clamped to +3 */
            }
        } else if (down) {                              /* $4521-$4528 */
            bias--;
            if (bias & 0x80u) {                         /* negative: CPY #$FB (carry dead here) */
                if (!(bias >= 0xFBu)) bias = 0xFBu;     /* ...and to -5 */
            }
        }
        camera_pitch_bias = bias;                       /* $452A */
    }

yaw:
    /* $452D-$4568 — the section yaw.  A cheap atan2 over the section's direction vector: the
       smaller-magnitude ground-plane component, scaled by 0.375 and folded through three saved
       signs, minus car_heading_hi.  The three 6502 PHPs each carry ONE flag past the scaling —
       here they are plain C: n1 the octant's sign, n2 component 2's sign, c3 "near the diagonal".
       Every intermediate flag is dead (the folds below overwrite them), so the scaling is binary. */
    dirIndex = mem[SECTION_DIR_IX + car_section_cursor];  /* $452D-$452F — Y stays dirIndex to $457F */
    yScale   = dirIndex;                                /* the scale index at $45D8, unless a spin overwrites it */
    shared_temp_76 = view_pitch_offset;                 /* $4531-$4534 — last frame's pitch */
    {
        uint8_t dir0 = mem[TRACK_DIR_0 + dirIndex];
        uint8_t dir2 = mem[TRACK_DIR_2 + dirIndex];
        int n1 = ((dir0 ^ dir2) & 0x80u) != 0;         /* $453C PHP (1) — the octant's sign */
        int n2 = (dir2 & 0x80u) != 0;                  /* $4540 PHP (2) — component 2's sign */
        uint8_t comp = (dir2 & 0x80u) ? (uint8_t)(0u - dir2) : dir2;  /* $4541 abs |c2| */
        int c3 = (comp >= 0x3Cu);                      /* $4546 PHP (3) — CMP #$3C */
        uint8_t a, sy, fold;
        if (comp >= 0x3Cu)                             /* $4547 BCC — off the diagonal: use |c0| */
            comp = (dir0 & 0x80u) ? (uint8_t)(0u - dir0) : dir0;
        math_lo = comp;                                /* $454F */
        a = (uint8_t)((uint8_t)(comp >> 1) + comp);    /* $4552-$4553 — 1.5x */
        a = (uint8_t)(a >> 2);                          /* $4555-$4556 — ...so 0.375x */
        if (!c3) a ^= 0x3Fu;                            /* $4558 BCS — complement in the octant */
        if (n2)  a ^= 0x80u;                            /* $455D BPL — the half turn */
        if (n1)  a = (uint8_t)(0u - a);                 /* $4562 abs8 driven by (1)'s sign */
        sy = (uint8_t)(a - car_heading_hi);            /* $4565-$4566 */
        section_yaw = sy;                              /* $4568 */

        /* $456A-$4574 — folded to 0..$3F about $40 (the subtract's N is the first test). */
        fold = sy;
        if (sy & 0x80u) fold ^= 0xFFu;                 /* $456A BPL — N from the subtract */
        if (fold >= 0x40u) fold ^= 0x7Fu;              /* $4570 BCC */
        view_yaw_offset = fold;                        /* $4574 */
    }

    /* $4577-$4599 — the frame's pitch: 1.5 x the yaw's complement through the track gradient,
       plus four terms, then halved with its sign preserved.  Every add's flags are dead until
       the last, whose sign steers the signed halving. */
    {
        uint8_t base = (uint8_t)(view_yaw_offset ^ 0x3Fu);  /* $4577 */
        uint8_t a;
        int neg;
        math_lo = base;                                /* $4579 */
        a = (uint8_t)((uint8_t)(base >> 1) + base);    /* $457C-$457D — 1.5x */
        a = scale_by_track_gradient_core(a, dirIndex); /* $457F — Y is still dirIndex */
        a = (uint8_t)(a + grip_disturbance);           /* $4582-$4583 */
        a = (uint8_t)(a + camera_pitch_bias);          /* $4585-$4586 */
        a = (uint8_t)(a + spin_shake);                 /* $4589-$458A */
        a = (uint8_t)(a + view_pitch_offset);          /* $458C-$458D */
        neg = (a & 0x80u) != 0;                         /* $458F-$4592 — C = sign(A) */
        a = (uint8_t)((a >> 1) | (neg ? 0x80u : 0u));  /* $4593 ROR — signed halving */
        view_pitch_offset = a;                         /* $4594 */
        view_pitch_delta  = (uint8_t)(a - shared_temp_76);  /* $4596-$4599 */
    }

    /* $459B-$45C9 — drive_state.  spin_countdown steps -4 a frame and SATURATES to $C8; the
       sum with drive_state picks between the three values it can take.
       ⚠⚠ The $45CB SMC dispatch just past this block RETURNS on an unrecognised opcode, and on
       that exit (a real, compared path — 1 fixture case in 10 randomises the SMC bytes) the
       routine's live A/X/Y/N/Z/C/V are exactly what this block leaves.  So the block builds a
       provisional exit `preSmc`: A = driveNew, X = car_section_cursor (untouched to $45D3),
       Y = yScale (dirIndex, or the spin's MOS Y), N/Z from driveNew, and C/V per arm.  The
       countdown arm's spin queues a MOS sound (begin_spin_from_a) that leaves the MOS's own Y —
       captured in yScale — and its own C/V, read back as a documented MOS boundary. */
    shared_temp_77 = 0x00u;                             /* $459B-$459D */
    uint8_t driveNew, cArm, vArm;
    {
        uint8_t sub  = (uint8_t)(spin_countdown - 0x04u);   /* $459F-$45A3 SBC (D=0) */
        uint8_t vSub = (uint8_t)((((spin_countdown ^ 0x04u) &
                                   (spin_countdown ^ sub)) >> 7) & 1u);
        uint8_t a    = vSub ? 0xC8u : sub;              /* $45A4 — the step overflowed: saturate */
        unsigned s;
        uint8_t r, cAdd, vAdd, nAdd, zAdd;
        spin_countdown = a;                             /* $45A8 */
        s    = (unsigned)a + drive_state;               /* $45AA-$45AB ADC (D=0) */
        r    = (uint8_t)s;
        cAdd = (uint8_t)(s > 0xFFu);
        vAdd = (uint8_t)((((~(a ^ drive_state)) & (a ^ r)) >> 7) & 1u);
        nAdd = (uint8_t)((r >> 7) & 1u);
        zAdd = (uint8_t)(r == 0);
        /* ⚠ $45B1's `BPL` keeps the sum; a NEGATIVE sum falls THROUGH to the countdown arm,
           so it is reached two ways, not one. */
        if (zAdd || (!vAdd && nAdd)) {                  /* $45AD BEQ, or $45B1 BPL not taken */
            uint8_t absSpin;                            /* $45B3-$45B5 LDA/abs8 |spin_countdown| */
            uint8_t absV;                               /* abs8's V (its ADC #1 when it negates); C is dead (CMP overwrites) */
            if (a & 0x80u) {                            /* abs8 ran: EOR #$FF / CLC / ADC #1 */
                uint8_t  inv = (uint8_t)(a ^ 0xFFu);
                unsigned as  = (unsigned)inv + 1u;
                absSpin = (uint8_t)as;
                absV = (uint8_t)((((~(inv ^ 0x01u)) & (inv ^ (uint8_t)as)) >> 7) & 1u);
            } else {                                    /* abs8 no-op: V survives from the ADC */
                absSpin = a;
                absV = vAdd;
            }
            if (absSpin >= 0x05u) {                     /* $45B7 CMP #5 → C=1 */
                SpinExit se = begin_spin_from_a_core(absSpin, car_section_cursor);  /* $45B9 (X = car_section_cursor) */
                yScale = se.y;                          /* the spin's sound OSWORD left this in the MOS's Y */
                cArm = se.c; vArm = se.v;               /* begin_spin's exit C/V (the block ADD's, LDA #1 leaves them) */
                driveNew = 0x01u;                        /* $45BD */
            } else {                                     /* $45B7 CMP #5 → C=0 */
                driveNew = 0x00u;                        /* $45C3 */
                cArm = 0u;                               /* CMP set C=0 (absSpin < 5) */
                vArm = absV;                             /* V survives LDA #0 */
            }
        } else if (vAdd) {
            driveNew = 0x7Fu;                            /* $45AF BVS → $45C7 */
            cArm = cAdd; vArm = vAdd;                     /* C/V from the ADD survive LDA #$7F */
        } else {
            driveNew = r;                                /* keep the sum */
            cArm = cAdd; vArm = vAdd;
        }
    }
    drive_state = driveNew;                             /* $45C9 */
    CameraExit preSmc;                                  /* the exit the $45CB SMC trap returns */
    preSmc.acc.hi       = driveNew;
    preSmc.acc.carry    = cArm;
    preSmc.acc.overflow = vArm;
    preSmc.acc.neg      = (uint8_t)((driveNew >> 7) & 1u);
    preSmc.acc.zero     = (uint8_t)(driveNew == 0u);
    preSmc.x            = car_section_cursor;           /* X unchanged since $452D */
    preSmc.y            = yScale;                       /* Y = dirIndex, or the spin's MOS Y */

    /* $45CB-$45D1 — driveNew x4 into shared_temp_76 with the overflow in shared_temp_77, i.e. the
       camera term's high byte.  The first ASL/ROL pair is the per-circuit hook site. */
    {
        uint8_t camA = driveNew, carry;
        if (mem[0x45CB] == 0x0A && mem[0x45CC] == 0x26) {   /* unpatched: Silverstone — inline ASL/ROL */
            carry = (uint8_t)(camA >> 7);
            camA  = (uint8_t)(camA << 1);
            shared_temp_77 = (uint8_t)((shared_temp_77 << 1) | carry);
        } else if (mem[0x45CB] == 0x20) {
            uint16_t target = (uint16_t)(mem[0x45CC] | (mem[0x45CD] << 8));
            if (target >= 0x5300 && target <= 0x5A25) {
                cpu.A = camA;                            /* SMC/hook boundary — the circuit code runs on cpu.A */
                revs_track_hook(target);
                camA = cpu.A;
            } else { platform_smc_unhandled(0x45CB, target); return preSmc; }
        } else {
            platform_smc_unhandled(0x45CB, mem[0x45CB]); return preSmc;
        }
        carry = (uint8_t)(camA >> 7);                    /* $45CF-$45D0 — second ASL/ROL (always) */
        camA  = (uint8_t)(camA << 1);
        shared_temp_77 = (uint8_t)((shared_temp_77 << 1) | carry);
        shared_temp_76 = camA;                           /* $45D1 */
    }

    /* $45D3-$45FB — the camera: the section's coordinate 1, the player's gradient-scaled
       car_state_1, and $AC of nominal eye height, as one 16-bit add with two carries saved
       past the term in between. */
    uint8_t playerCar = player_car;                    /* $45D3 — exit X */
    /* ⚠⚠ yScale IS NOT dirIndex ANY MORE ON ONE PATH.  The spin arm above reaches begin_spin_from_a,
       which queues a MOS SOUND — and sound_osword leaves the MOS's own Y behind.  So this call
       scales by whatever table entry Y now points at, and a twin that "knew" the index was
       still the section's differed in one case in six. */
    uint8_t scaled = scale_by_track_gradient_core(mem[CAR_STATE_1 + playerCar], yScale);  /* $45D5-$45D8 */
    if (scaled & 0x80u) shared_temp_77 = (uint8_t)(shared_temp_77 - 1u);  /* $45DB DEC_M — sign-extend it */
    uint8_t secCursor = car_section_cursor;             /* $45DF — Y for the section coords + exit */
    {
        uint8_t scaledLow = scaled;                    /* the gradient-scaled car_state_1 low byte */
        uint8_t secLo = mem[SECTION_CRD_LO + 1 + secCursor];
        uint8_t secHi = mem[SECTION_CRD_HI + 1 + secCursor];
        unsigned t1 = (unsigned)scaledLow + secLo;     /* $45E1-$45E2 */
        uint8_t  carryA = (uint8_t)(t1 > 0xFFu);       /* $45E5 PHP (a) */
        unsigned t2 = (unsigned)(uint8_t)t1 + 0xACu;   /* $45E6-$45E7 — nominal eye height */
        uint8_t  carryB = (uint8_t)(t2 > 0xFFu);       /* $45E9 PHP (b) */
        unsigned t3 = (unsigned)(uint8_t)t2 + shared_temp_76;   /* $45EA-$45EB */
        uint8_t  carry76 = (uint8_t)(t3 > 0xFFu);
        mem[VIEW_ORIGIN_LO + 1] = (uint8_t)t3;         /* $45ED */
        /* $45EF-$45FB — the high byte: secHi + shared_temp_77 + the three saved low-byte
           carries.  $45EF's and $45F6's own carry-outs are discarded by the PLPs (they are
           bits past the 16-bit result), so the whole high byte is one truncating sum. */
        mem[VIEW_ORIGIN_HI + 1] =
            (uint8_t)(secHi + shared_temp_77 + carry76 + carryB + carryA);  /* $45FB */
    }

    /* $45FE-$460C — car_speed_scaled = road_speed x ($21/256 + 2).  The final add's A/N/V/Z/C
       are the routine's exit register/flags (X is still player_car), so they are replayed (D=0). */
    math_hi = road_speed;                               /* $45FE-$4600 */
    {
        uint16_t p   = (uint16_t)revs_mulu16(0x21u, math_hi);   /* $4602-$4604 — $21 x road_speed */
        uint8_t  phi = (uint8_t)(p >> 8);              /* only the high byte is used */
        unsigned sum;
        uint8_t  res;
        math_lo = (uint8_t)p;
        math_hi = (uint8_t)(math_hi << 1);             /* $4607 ASL_M — road_speed x 2 */
        sum = (unsigned)phi + math_hi;                 /* $4609-$460A */
        res = (uint8_t)sum;
        mem[CAR_SPEED_SCL + playerCar] = res;          /* $460C */
        e.acc.hi       = res;
        e.acc.carry    = (uint8_t)(sum > 0xFFu);
        e.acc.overflow = (uint8_t)(((~(phi ^ math_hi) & (phi ^ res)) >> 7) & 1u);
        e.acc.neg      = (uint8_t)((res >> 7) & 1u);
        e.acc.zero     = (uint8_t)(res == 0);
        e.x = playerCar;
        e.y = secCursor;
    }
    return e;
}

/* The 6502-ABI shims. */
void compute_car_angles(void)            { compute_car_angles_core(cpu.A, cpu.X); }
void scale_by_track_gradient(void)
{
    /* Replay the $4610 exit ABI.  Positive arm ($4622 abs8 not taken): flags are the restored
       EOR status — N = 0, Z = (eor == 0), C/V PASS THROUGH the caller's (the EOR touches neither,
       the PHP/PLP carried them intact).  Negative arm: flags from abs8 negating the high byte —
       N/Z from -(high), C = (high == 0) = Z, V = 0 (high ≤ 0x7F is never $80). */
    uint8_t callerC = cpu.C, callerV = cpu.V;
    uint8_t eor = (uint8_t)(mem[TRACK_DIR_1 + cpu.Y] ^ track_direction);
    uint8_t a = scale_by_track_gradient_core(cpu.A, cpu.Y);
    cpu.A = a;
    if (eor & 0x80u) {
        cpu.N = (uint8_t)((a >> 7) & 1u);
        cpu.Z = (uint8_t)(a == 0);
        cpu.C = (uint8_t)(a == 0);
        cpu.V = 0u;
    } else {
        cpu.N = 0u;
        cpu.Z = (uint8_t)(eor == 0);
        cpu.C = callerC;
        cpu.V = callerV;
    }
}
/* begin_spin's exit ABI is sound_queue_default's: A/Y are left in cpu by the OSWORD (via mos_call
   inside the core), and sound_queue_exit_abi replays X/N/Z (from sound_saved_x) and the block C/V. */
void begin_spin(void)                    { begin_spin_from_a_core(road_speed, cpu.X); sound_queue_exit_abi(0x04u); }
void begin_spin_from_a(void)             { begin_spin_from_a_core(cpu.A, cpu.X);      sound_queue_exit_abi(0x04u); }
void apply_drag_terms(void)              { apply_drag_terms_core(); }

/* ===========================================================================
   TWINS #87-#92 — THE ROAD SIGN, and the OBJECT SLOT WRITER underneath it
   ---------------------------------------------------------------------------
   The first of the three trees the campaign has left, and the smallest: six C functions,
   252 bytes of 6502, with every arithmetic leaf underneath them already a twin (#11-#24).

     $4CA4 build_road_sign       the body's 14th call — one sign into object slot $17
     $4D21 build_sign_origin     one component of the sign's own view origin
     $2A76 write_object_slot     THE object-slot writer, shared with the car projector
     $2AA6 reject_object_slot    ...and its reject arm, which marks the slot empty
     $2AAD store_object_flags    the one store both arms end on
     $2AB3 note_object_contact   "is this object close enough to be a collision candidate?"

   ⭐⭐ WHAT THE GROUP MADE LEGIBLE — four things, in the order they surprised:

   1. A ROAD SIGN IS PROJECTED FROM ITS OWN VIEWPOINT, NOT THE CAMERA'S.  view_origin has a
      stride of six because there are two origins, and build_sign_origin is the only writer of
      the second: for each of the three components it takes the sign's own signed offset byte,
      scales it up (x64 for the two ground-plane components, x16 for the height) and SUBTRACTS
      it from the camera's component.  bearing_to_section and project_point then run with Y = 6,
      and every other caller in the engine uses Y = 0 — which is what the two entry points of
      each of those routines are FOR.
   2. THE SIGN TABLES ARE FOUR ROWS OF SIXTEEN, and they sit in the gaps of the two track
      pages.  sign_offset_0/_1/_2 are $53D0/$53F0/$53E0 — the tail of track_segment_hi, past
      the last segment record — and sign_shape_segment is $59EA, which lands exactly between
      segment_data and segment_count_x8.  Silverstone's own bytes prove the layout: the sixteen
      $59EA entries are ASCENDING segment indices ($03 $10 $19 $2C … $B8) once the low three
      bits are masked off, and sign_offset_1 is $08 in nine of sixteen entries — the signs are
      all at about the same height.
   3. ⚠⚠ ALL FIVE OF THOSE LOADS ARE PER-CIRCUIT SMC ($4CC0 $4CC8 $4CD0 $4CD6 $4CE0, one
      extent each in `make track-smc`).  The opcode stays `LDA abs,X`; each circuit's
      ModifyGameCode rewrites the two operand bytes, so the twin reads the table base out of
      mem[] every time and cannot bake $53D0 in.  What it CAN do is hoist the hardware-window
      test to the base instead of paying it per byte — the transliteration routes all five
      through bus_read.
   4. THE SIGN ADVANCES BY WALKING OFF THE SIDE OF THE VIEW.  The sign to show is the high
      nibble of the player's own segment record, and $4CB2 compares it with the one shown last
      frame: on a match the number is INCREMENTED (`ADC #0` with the CMP's carry, then masked to
      a nibble), so an unchanged segment shows the NEXT sign.  Which one sticks is decided at
      the other end of the routine — sign_last_index is only updated once the sign's bearing is
      more than $40 away from where the car is pointing, i.e. once it has left the view.

   ⭐ AND ONE THING THE GROUP CORRECTED.  object_width's symbols.csv row said "shifted by
   proj_width_shift - $09 places"; the `DEX` at $2A8A makes it - $0A, and the sign of that
   difference is the direction.  Fixed in the same commit.

   No hardware writes anywhere in the group: signs live entirely in RAM.
   =========================================================================== */

#define VIEW_ORIGIN_STRIDE   0x06u     /* view_origin_lo/_hi: origin 0 = camera, 6 = the sign */
#define SIGN_OFFSET_2_SITE   0x4CC0u   /* the `LDA sign_offset_2,X` whose operand a circuit */
#define SIGN_OFFSET_1_SITE   0x4CC8u   /* ...rewrites.  ⚠ The engine loads component 2 first, */
#define SIGN_OFFSET_0_SITE   0x4CD0u   /* ...then 1, then 0 — the order the sites are in. */
#define SIGN_SHAPE_SITE      0x4CD6u   /* `LDA sign_shape_segment,X` for the SHAPE nibble */
#define SIGN_SEGMENT_SITE    0x4CE0u   /* ...and again for the SEGMENT the sign is anchored to */
#define SIGN_SLOT            0x17u     /* the object slot every sign is built into */
#define SIGN_SCRATCH_SECTION 0xFDu     /* the live-section slot its coordinate triple goes to */

/* One of the five per-circuit table loads.  The opcode is `LDA abs,X` on every circuit
   (`make track-smc`: `sig:0,BD@1,2`), so only the base varies — and the hardware-window test
   goes on the BASE, once, instead of on every byte the way bus_read does.  Sets *trapped when
   the byte at the site is not that opcode at all, which is the same hard trap the
   transliteration takes, and the caller must then return without touching A. */
static uint8_t sign_table_byte(uint16_t site, uint8_t index, int* trapped)
{
    uint16_t base;

    if (mem[site] != 0xBDu) {                  /* not `LDA abs,X` — a shape we cannot execute */
        platform_smc_unhandled(site, mem[site]);
        *trapped = 1;
        return 0;
    }
    base = (uint16_t)(mem[site + 1] | ((unsigned)mem[site + 2] << 8));
    return seam_read((unsigned)(uint16_t)(base + index), pointer_is_ram(base));
}

/* ---------------------------------------------------------------------------
   $4D21  build_sign_origin — ONE COMPONENT OF THE SIGN'S VIEWPOINT  (twin #88)
   ---------------------------------------------------------------------------
   view_origin[6 + c] = view_origin[c] - (signed `offset` << (8 - shift)).

   The 6502 spells that as a 24-bit logical shift: the sign extension in shared_temp_76, the
   value in A and a zero low byte in math_lo, shifted right `shift` times by LSR/ROR/ROR.  For
   any shift under 9 — and the three call sites pass 2, 4 and 2 — the middle and low bytes are
   exactly the signed 16-bit `offset x 256` shifted arithmetically, which is what the routine is
   computing; the top byte only supplies the sign bits.  ⭐ THE TWIN DOES IT IN ONE SHIFT, which
   is this group's only algorithmic compression.

   ⚠ `shift` ARRIVING AS 0 MEANS 256, not "no shift": the DEY is at the BOTTOM of the loop.
   Reproduced (the result is then 0) rather than special-cased away.

   The component index is shared_temp_77, which the caller seeds and this routine DECs — so
   three calls walk components 2, 1, 0.  math_lo, math_hi and shared_temp_76 are left as
   scratch, and the second subtract's flags are the routine's exit flags.
   --------------------------------------------------------------------------- */

SignOriginExit build_sign_origin_core(uint8_t offset, uint8_t shift)
{
    uint32_t staged;
    unsigned places;
    uint8_t  component;

    /* $4D21-$4D2B — (sign : value : 0), the sign taken from bit 7 of `offset`.  The 6502 staged
       it through PHA/PLA only to test its sign across the two zeroing stores; the pushed byte at
       $0100+S is dead stack residue (nothing reads it back), so the twin stages it in a local and
       reads bit 7 directly.  det_compare skips $01B8..$01FF and the fixture ignores $01FF for the
       residue the oracle still leaves there. */
    math_lo        = 0x00u;
    shared_temp_76 = 0x00u;
    staged = ((uint32_t)offset << 8);
    if (offset & 0x80u) { staged |= 0xFF0000u; shared_temp_76 = 0xFFu; }

    /* $4D2D-$4D33 — LSR / ROR / ROR, `shift` times, feeding zeros in at the top. */
    places = shift ? shift : 256u;
    staged = (places >= 24u) ? 0u : (staged >> places);
    shared_temp_76 = (uint8_t)(staged >> 16);             /* the loop shifts it too */
    math_lo        = (uint8_t)staged;
    math_hi        = (uint8_t)(staged >> 8);              /* $4D35 */

    /* $4D37-$4D4C — subtract it from the camera's own component, into origin 6, as ONE 16-bit
       subtract.  Y is left as the component index; the high SBC's flags are the routine's exit
       flags, replayed from the high byte (6502 Z is the high byte alone, not the word). */
    component = shared_temp_77;
    shared_temp_77 = (uint8_t)(component - 1u);

    uint16_t wm   = (uint16_t)(((uint16_t)mem[VIEW_ORIGIN_HI + component] << 8) |
                               mem[VIEW_ORIGIN_LO + component]);
    uint16_t ws   = (uint16_t)(((uint16_t)math_hi << 8) | math_lo);
    uint16_t diff = (uint16_t)(wm - ws);
    mem[VIEW_ORIGIN_LO + VIEW_ORIGIN_STRIDE + component] = (uint8_t)diff;
    uint8_t hiR = (uint8_t)(diff >> 8);
    mem[VIEW_ORIGIN_HI + VIEW_ORIGIN_STRIDE + component] = hiR;

    uint8_t hiM = (uint8_t)(wm >> 8);
    SignOriginExit e;
    e.a = hiR;
    e.y = component;
    e.c = (wm >= ws) ? 1u : 0u;                          /* no borrow out of the word */
    e.v = (uint8_t)((((hiM ^ math_hi) & (hiM ^ hiR)) >> 7) & 1u);
    e.n = (hiR >> 7) & 1u;
    e.z = (hiR == 0u);
    return e;
}

/* ---------------------------------------------------------------------------
   $2AB3  note_object_contact — IS THIS OBJECT A COLLISION CANDIDATE?  (twin #92)
   ---------------------------------------------------------------------------
   Runs point_distance_hypot for the point just transformed and, if the distance fits in a byte
   AND is at or under the caller's threshold in Y, records the object as THE frame's contact
   candidate: contact_pending goes non-zero, contact_distance takes the distance and
   contact_slot the slot number.  process_car_contact is the consumer — it clears the flag,
   scales the impact as ($25 - contact_distance) x 2 and takes the car from contact_slot.

   ⭐ ONE CANDIDATE PER FRAME, LAST WRITER WINS, and the threshold is the caller's business:
   the car projector enters at $2AB1 with a fixed $25, while build_road_sign picks $25 or $50 on
   how far off-heading the sign is.  ⚠ contact_pending is DECremented, not set — so a frame in
   which two objects qualify leaves it at $FE, and process_car_contact's test is `non-zero`.

   object_dist_hi is written unconditionally, before either test, and $29FB is its only reader.
   --------------------------------------------------------------------------- */

ContactExit note_object_contact_core(uint8_t threshold, uint8_t entryC)
{
    ContactExit e;
    point_distance_hypot_apply();                        /* $2AB3 */

    uint8_t distHi = point_dist_hi;                      /* $2AB6-$2AB8 — LDA sets A/N/Z */
    object_dist_hi = distHi;
    e.y = threshold;                                     /* Y is the caller's, not reloaded */
    if (distHi != 0u) {                                  /* further away than $FF */
        e.a = distHi; e.n = (distHi >> 7) & 1u; e.z = 0u; e.c = entryC;
        return e;
    }

    uint8_t distLo = point_dist_lo;                      /* CPY $2ABC — Y(threshold) - point_dist_lo */
    uint8_t cmp    = (uint8_t)(threshold - distLo);
    if (threshold < distLo) {                            /* ...or further than the threshold */
        e.a = distHi;                                    /* A is still 0 from the LDA above */
        e.n = (cmp >> 7) & 1u; e.z = (cmp == 0u); e.c = 0u;
        return e;
    }

    contact_pending  = (uint8_t)(contact_pending - 1u);  /* DEC $2AC0 (N/Z here are dead) */
    contact_distance = distLo;                            /* $2AC2-$2AC4 */
    uint8_t slot     = shared_counter_42;                 /* $2AC6-$2AC8 — LDA sets A/N/Z */
    contact_slot     = slot;
    e.a = slot; e.n = (slot >> 7) & 1u; e.z = (slot == 0u); e.c = 1u;   /* passed the C test */
    return e;
}

/* ---------------------------------------------------------------------------
   $2A76  write_object_slot — THE PROJECTION'S RESULT INTO AN OBJECT SLOT  (twin #89)
   $2AA6  reject_object_slot                                               (twin #90)
   $2AAD  store_object_flags                                               (twin #91)
   ---------------------------------------------------------------------------
   Entered straight off project_point with the projected scan line in A and its carry in C, and
   the slot number in shared_counter_42.  Three fields land: object_line (the line, less one),
   object_width (the apparent width, rescaled) and the low nibble of car_flags_shape (the
   shape).  Bit 7 of car_flags_shape is the SLOT-EMPTY mark, and the reject arm is the only
   thing that sets it — draw_track_object reads exactly that bit to skip a slot.

   ⭐ THE WIDTH RESCALE IS AN EXPONENT CORRECTION.  project_point leaves a mantissa in
   proj_width and the number of places it had to shift to normalise in proj_width_shift; the
   slot wants the value at a fixed scale, so this shifts it by proj_width_shift - $0A places,
   LEFT when that is positive and RIGHT when it is negative.  ⚠ Both loops test X at the BOTTOM,
   so an exponent outside +-8 walks up to 255 places and lands on 0 — reproduced, not clamped.

   ⚠ Two rejects, and they are NOT the same test: C set out of project_point (the point is
   behind the near clip) rejects, and so does a projected line of 0, because the `SBC #1` then
   goes negative.  Everything else is drawn.
   --------------------------------------------------------------------------- */
/* The slot-writer chain's exit ABI.  All three fixtures compare A/X/Y + N/Z/V/C, so the core
   returns every escaping value and the thin shim replays it into cpu; pass-through registers
   (entry X/V/C on the reject arms) travel in as args and back out unchanged. */
/* SlotExit is declared up by plot_object_core's forward declaration. */

static void store_object_flags_core(uint8_t y, uint8_t a)
{
    mem[CAR_FLAGS_SHAPE + y] = a;                        /* $2AAD — STA touches no flag/register */
}

RejectExit reject_object_slot_core(void)
{
    uint8_t y = shared_counter_42;                       /* $2AA6 */
    uint8_t a = (uint8_t)(mem[CAR_FLAGS_SHAPE + y] | 0x80u);   /* ORA #$80 — the slot-empty mark */
    store_object_flags_core(y, a);
    RejectExit r = { a, y };                             /* exit: A=a, Y=y, N=1, Z=0 (bit7 set) */
    return r;
}

SlotExit write_object_slot_core(uint8_t projectedLine, uint8_t entryX,
                                       uint8_t entryV, uint8_t entryC)
{
    unsigned width;
    int      places;
    uint8_t  slot = shared_counter_42;                   /* $2A76 */
    SlotExit e;
    e.x = entryX; e.v = entryV; e.c = entryC; e.y = slot;

    if (entryC) {                                        /* $2A78 BCS — behind the near clip */
        RejectExit r = reject_object_slot_core();
        e.a = r.a; e.y = r.y; e.n = (r.a >> 7) & 1u; e.z = (r.a == 0u);
        return e;                                        /* X/V/C pass through unchanged */
    }

    uint8_t line = (uint8_t)(projectedLine - 0x01u);     /* $2A7A-$2A7B  SEC/SBC #1 */
    uint8_t sbcV = sbc_overflow(projectedLine, 0x01u, 1);/* SBC's V/C — exit flags on the */
    uint8_t sbcC = (projectedLine >= 0x01u) ? 1u : 0u;   /* reject-line arm (ORA writes neither) */
    if (line & 0x80u) {                                  /* $2A7D BMI — a projected line of 0 */
        RejectExit r = reject_object_slot_core();
        e.a = r.a; e.y = r.y; e.n = (r.a >> 7) & 1u; e.z = (r.a == 0u);
        e.v = sbcV; e.c = sbcC;                          /* the SBC's V/C survive to this exit */
        return e;                                        /* X passes through unchanged */
    }
    mem[OBJECT_LINE + slot] = line;                      /* $2A7F */

    /* $2A82-$2A99 — the exponent correction.  X carries the count and its own sign picks the
       direction (kept in a signed int); both loops end with X at 0, and so does places==0.  The
       subtract's own N/Z die at the DEX, but its V is the drawn path's exit V. */
    uint8_t  xc     = (uint8_t)((uint8_t)(proj_width_shift - 0x09u) - 1u);   /* SBC / TAX / DEX */
    uint8_t  exitC  = (proj_width_shift >= 0x09u) ? 1u : 0u;   /* the SBC's C — exit C when places==0 */
    uint8_t  shiftV = sbc_overflow(proj_width_shift, 0x09u, 1);
    uint8_t  exitX  = xc;
    width  = proj_width;                                 /* $2A88 */
    places = (int)(int8_t)xc;
    /* ⚠ EACH LOOP'S LAST SHIFT LEAVES ITS BIT IN C, AND THAT C IS THE ROUTINE'S EXIT C —
       nothing between here and the RTS writes it.  The twin replays that one bit from the
       count instead of running the shift a bit at a time (698/4000 failed the first time). */
    if (places < 0) {                                    /* $2A8F — LSR A / INX */
        unsigned n = (unsigned)(uint8_t)(-places);
        exitC = (uint8_t)((n <= 8u) ? ((width >> (n - 1u)) & 1u) : 0u);
        width = (n >= 8u) ? 0u : (width >> n);
        exitX = 0;
    } else if (places > 0) {                             /* $2A95 — ASL A / DEX */
        unsigned n = (unsigned)places;
        exitC = (uint8_t)((n <= 8u) ? ((width >> (8u - n)) & 1u) : 0u);
        width = (n >= 8u) ? 0u : (uint8_t)(width << n);
        exitX = 0;
    }
    mem[OBJECT_WIDTH + slot] = (uint8_t)width;           /* $2A99 */

    /* $2A9C-$2AA3 — keep the surviving flag bits, drop this shape in the low nibble, store.
       The ORA's N/Z are the routine's exit flags. */
    uint8_t flagsA = (uint8_t)((mem[CAR_FLAGS_SHAPE + slot] & 0x70u) | plot_shape);
    store_object_flags_core(slot, flagsA);               /* $2AA3 JMP */
    e.a = flagsA; e.x = exitX; e.y = slot;
    e.n = (flagsA >> 7) & 1u; e.z = (flagsA == 0u);
    e.v = shiftV; e.c = exitC;
    return e;
}

/* ---------------------------------------------------------------------------
   $4CA4  build_road_sign — ONE SIGN INTO OBJECT SLOT $17  (twin #87)
   ---------------------------------------------------------------------------
   The body's 14th call, and the body's 15th (draw_track_object) draws what it leaves.  In
   order: pick the sign, build its viewpoint, take its shape and its anchor segment, bear and
   project it from that viewpoint, and write the slot.
   --------------------------------------------------------------------------- */
static void build_road_sign_core(void)
{
    int     trapped = 0;
    uint8_t signIndex, tableByte, threshold;

    /* $4CA4-$4CBB — which sign.  The player's segment record carries it in its high nibble;
       an unchanged segment shows the NEXT sign (see the group header, item 4).  A and X both
       carry the index out of this block (LDA/…/TAX), which is the state a table-load trap
       reports. */
    {
        uint8_t seg    = mem[CAR_SEGMENT_TBL + player_car];
        uint8_t nibble = (uint8_t)(mem[TRACK_SEGMENT_HI + seg] >> 4);   /* LSR A x4 */
        saved_slot_index = nibble;
        if (nibble == sign_last_index)                   /* $4CB5 — the same sign again */
            nibble = (uint8_t)((nibble + 1u) & 0x0Fu);   /* ADC #0 (C=1 from the equal CMP), AND #$0F */
        signIndex = nibble;                              /* TAX leaves it in X too; X is dead on exit */
    }

    /* $4CBC-$4CD5 — the sign's own view origin, components 2, 1, 0.  shared_temp_77 is the
       component cursor build_sign_origin walks down, so the ORDER of these three carries the
       meaning, and the shifts differ: x64 across the ground plane, x16 up.  (The 6502 kept the
       shift in Y so a table-load trap could report it; the shift is now an explicit argument and
       a trap records only mem[], which build_road_sign is compared on.) */
    shared_temp_77 = 0x02u;                              /* the component cursor: 2, then 1, then 0 */
    tableByte = sign_table_byte(SIGN_OFFSET_2_SITE, signIndex, &trapped);
    if (trapped) return;
    build_sign_origin_core(tableByte, 0x02u);            /* x64 across the ground plane */
    tableByte = sign_table_byte(SIGN_OFFSET_1_SITE, signIndex, &trapped);
    if (trapped) return;
    build_sign_origin_core(tableByte, 0x04u);            /* x16 up */
    tableByte = sign_table_byte(SIGN_OFFSET_0_SITE, signIndex, &trapped);
    if (trapped) return;
    build_sign_origin_core(tableByte, 0x02u);

    /* $4CD6-$4CDE — the shape: the low three bits of the sign's table byte, plus 7.  The
       object plotter's shapes 7..14 are the signs. */
    tableByte = sign_table_byte(SIGN_SHAPE_SITE, signIndex, &trapped);
    if (trapped) return;
    plot_shape = (uint8_t)((tableByte & 0x07u) + 0x07u);   /* carry-in 0; flags dead */

    /* $4CE0-$4CEA — ...and the SEGMENT it is anchored to, in the same byte's top five bits (a
       multiple of 8, which is what a segment index is), into a scratch live section. */
    tableByte = sign_table_byte(SIGN_SEGMENT_SITE, signIndex, &trapped);
    if (trapped) return;
    uint8_t segByte  = (uint8_t)(tableByte & 0xF8u);     /* the segment index (a multiple of 8) */
    uint8_t scratchX = SIGN_SCRATCH_SECTION;             /* X carries the section index to the slot write */
    load_section_triple_core(scratchX, segByte);

    /* $4CEB-$4CF9 — the bearing, FROM THE SIGN'S OWN ORIGIN, into slot $17. */
    bearing_to_section_core(scratchX, VIEW_ORIGIN_STRIDE);
    mem[OBJECT_BEARING_LO + SIGN_SLOT] = bearing_lo;
    mem[OBJECT_BEARING_HI + SIGN_SLOT] = bearing_hi;

    /* $4CFA-$4D08 — how far off the car's heading the sign is.  Past $40 it has left the view,
       and THAT is what commits the sign number for the next frame.  abs8 here branches on bit 7
       of (bearing_hi - car_heading_hi) — the caller's N equals bit 7 — and $80 negates to $80. */
    uint8_t off    = (uint8_t)(bearing_hi - car_heading_hi);
    uint8_t absOff = (off & 0x80u) ? (uint8_t)(-off) : off;
    if (cmp_ge(absOff, 0x40u))                           /* the sign has left the view */
        sign_last_index = saved_slot_index;

    /* $4D09-$4D1F — the contact threshold widens for a sign well off to the side, then the
       projection and the slot write.  The CMP #$6E's carry is note_object_contact's entry C. */
    uint8_t offHeadingC = cmp_ge(absOff, 0x6Eu) ? 1u : 0u;
    threshold = offHeadingC ? 0x50u : 0x25u;
    shared_counter_42 = SIGN_SLOT;
    note_object_contact_core(threshold, offHeadingC);    /* exit dead here (build_road_sign is mem-only) */
    { ProjPoint p = project_point_core(scratchX, VIEW_ORIGIN_STRIDE);   /* $4D1B */
      write_object_slot_core(p.line, scratchX, /*V dead here*/ 0u, p.clip); }   /* $4D1E — exit dead */
}

/* The 6502-ABI shims. */
void build_road_sign(void)      { build_road_sign_core(); }
void store_object_flags(void)   { store_object_flags_core(cpu.Y, cpu.A); }

/* ===========================================================================
   TWINS #93-#95 — THE OBJECT PLOTTER'S SHAPE SIDE
   ---------------------------------------------------------------------------
   draw_track_object (twin #7) does nothing but decide WHERE an object goes; these three are
   what draws it, and together they are a small vector-shape rasteriser:

     $1FB4 plot_object           the driver: colours, the width's scale, the shape's tables
     $202A scale_shape_vectors   the shape's vertex offsets, scaled to this object's WIDTH
     $209A plot_shape_edges      ...walked as EDGES, each one a filled vertical span

   ⭐⭐ WHAT THE GROUP MADE LEGIBLE — five things, in the order they surprised:

   1. **AN OBJECT IS A VECTOR SHAPE, NOT A SPRITE.**  There are ten of them (indices 0..9, plus
      a two-part case below), each a run of `shape_vector_tbl` bytes and a run of five parallel
      `shape_edge_*` columns.  Nothing in the binary holds an object BITMAP: every car, sign and
      marker is drawn from these two lists at whatever size the perspective divide asked for.
   2. **A VERTEX BYTE IS A SUM OF POWERS OF TWO OF THE OBJECT'S WIDTH.**  `scale_shape_vectors`
      fills `shape_scale_tbl[2..7]` with the width halved five times, and then each vector byte
      picks entries out of it: a byte under $80 is one entry, a byte over $80 is TWO (bits 0-2
      and bits 3-5) plus a third half-width when bit 6 is set.  So the shape is stored as
      FRACTIONS of its own size and there is no multiply anywhere in the pass.
   3. **`shape_vertex` IS SIXTEEN ENTRIES THAT LOOK LIKE EIGHT.**  The routine writes the scaled
      value at index 0..7 and its NEGATION at index 8..15 — `$5EF8` and `$5F00` are one array,
      which is why nothing in the engine appears to read `$5F00`.  The edge columns then index
      0..15, i.e. they name a vertex offset AND its sign in one byte.
   4. ⚠ **THE PASS REJECTS ITSELF WHEN A VERTEX WOULD NOT FIT IN SEVEN BITS** ($2085's
      `EOR #$FF / BPL`), and plot_object's `BCS` right after the call is what abandons the whole
      object.  An object too close to the camera is simply not drawn.
   5. ⭐ **SHAPE 9 IS DRAWN TWICE, AND THE SECOND PASS USES THE UNCLAMPED INDEX.**  $1FFC clamps
      the shape to 9, but the loop at $2027 re-enters BELOW the clamp with the raw `plot_shape`
      in X — so a shape of $0A draws shape 9 and then shape $0A, gated on `track_direction`
      being positive.  `shape_vector_start` has eleven entries for exactly that reason.

   6. ⚠⚠ **SHAPE 9 IS A MARKER, NOT A SHAPE — and `plot_shape` = 9 HANGS THE 6502.**  Item 5's
      loop re-enters below the clamp, so a shape of exactly 9 writes 9 into
      `object_shape_clamped` on every pass and $2021's `CMP #$09` never stops agreeing:
      with `track_direction` positive the routine never returns.  It is unreachable in the game
      — cars take shapes 0/1/2/4 ($2A32/$2A3B/$2A46/$29F6), corner markers 6 ($1B6F), and a
      sign's `(size & 7) + 7` misses 9 on every circuit (Silverstone's sixteen give 7/8/10/11/12)
      — so 9 means "the stand-in has been drawn", never a shape.  MEASURED 2026-08-18: the twin
      and the transliteration hang identically on it, which is how the fixture found it.
   7. ⚠⚠ **THE SHAPE TABLES LIVE IN THE UNUSED TAILS OF THE VIEW SOURCE BLOCKS.**  $3550, $35D0,
      $3650, $36D0 and $3750 are offset $50 inside blocks 10..14 of the forty $80-spaced blocks
      at $3000, and `dash_block_starts` says a block's data always ENDS at offset $4F — so each
      column has exactly 48 table entries and index 48 is the next block's data, which
      plot_view_src_line paints over.  That is a real constraint on the walk, not a curiosity:
      it is why every entry's control bits matter and why the fixture has to bound the tables.

   ⚠ ONE PER-CIRCUIT SMC SITE, $1FE9: Silverstone reads `horizon_extent` into X and an
   expansion circuit plants `LDX #imm` in its place (`make track-smc`).  Both arms are kept.
   ⚠ AND ONE SIDE EFFECT WORTH NAMING: $1FC3 writes $F0 into `surface_colours[2]` — the ROAD's
   own colour table — after copying it, so the copy and the original differ from here on.
   draw_corner_markers does the same thing at $1B26 and $1B76.

   ⚠⚠ THE WHOLE PASS IS A SECOND TENANT OF THE point_delta WINDOW ($0080-$008F) — see
   docs/rename.md.  The `OBJ_*` defines below are the pass's own names for those cells and the
   comment on each says whose they are the rest of the time.
   =========================================================================== */

#define COLOUR_PATTERN_TBL  0x628Fu   /* colour_pattern_tbl — plot_view_src_line's four */
#define CAR_ORDER_TBL       0x013Cu   /* car_order */
#define SHAPE_VECTOR_TBL    0x4480u   /* shape_vector_tbl */
#define SHAPE_SCALE_TBL     0x5FF8u   /* shape_scale_tbl[2..7] = the width halved five times */
#define SHAPE_VERTEX        0x5EF8u   /* shape_vertex[0..7], and their negations at [8..15] */
#define SHAPE_VECTOR_START  0x3CDDu   /* shape_vector_start[n], and [n+1] is n's end */
#define SHAPE_EDGE_START    0x3CD0u   /* shape_edge_start[n] */
#define SHAPE_EDGE_LINE_0   0x3550u   /* the five per-edge columns: two vertex indices for the */
#define SHAPE_EDGE_LINE_1   0x35D0u   /* ...span's two scan lines, two for its two x offsets, */
#define SHAPE_EDGE_X_0      0x3650u   /* ...and a style byte.  ⚠ The closing arm at $2117 reads */
#define SHAPE_EDGE_X_1      0x36D0u   /* ...X_1 as a STYLE and LINE_0 as an X — see the note on */
#define SHAPE_EDGE_STYLE    0x3750u   /* ...shape_edge_x_1 in disasm/symbols.csv. */

/* The object pass's own names for the point_delta window it borrows (docs/rename.md). */
#define OBJ_VECTOR_CURSOR   0x0081u   /* point_delta_lo[1] — the shape's vector cursor */
#define OBJ_VECTOR_END      0x008Au   /* bearing_lo        — one past its last vector */
#define OBJ_EDGE_X          0x0083u   /* point_delta_hi[0] — the edge's x, into the plotter */
#define OBJ_EDGE_STYLE      0x0084u   /* point_delta_hi[1] — ...and its style byte */

/* ---------------------------------------------------------------------------
   $202A  scale_shape_vectors — THE SHAPE AT THIS OBJECT'S SIZE  (twin #94)
   ---------------------------------------------------------------------------
   Fills shape_vertex[0..7] with the shape's vertex offsets scaled to proj_width, and
   [8..15] with their negations.  Returns with C SET when one of them would not fit in seven
   bits, which is plot_object's signal to abandon the object.

   ⚠ proj_width_shift's extra halving is `LSR / DEX / BNE` and the closing `ADC #0` ROUNDS off
   the last bit shifted out — so the twin keeps the carry, not just the value.
   --------------------------------------------------------------------------- */
/* Exit ABI is the full register+flag set (SlotExit).  ⚠ V can carry the ENTRY V all the way to
   the abandon exit — a one-term vector with no extra shift never writes V before the reject — so
   the caller's V is an input. */
static SlotExit scale_shape_vectors_core(uint8_t entryV)
{
    int      i;
    unsigned a;
    uint8_t  y = mem[OBJ_VECTOR_CURSOR];             /* $2043 — the shape's vector cursor */
    uint8_t  v = entryV;

    /* $202A-$2042 — the width, then five halvings into shape_scale_tbl[2..7]. */
    a = proj_width;
    mem[SHAPE_SCALE_TBL + 2] = (uint8_t)a;
    for (i = 3; i <= 7; i++) { a >>= 1; mem[SHAPE_SCALE_TBL + i] = (uint8_t)a; }

    shared_temp_77 = 0x00u;                          /* the output cursor */

    for (;;) {
        uint8_t vec = mem[SHAPE_VECTOR_TBL + y];     /* $2049 */
        uint8_t x;

        if (vec & 0x80u) {
            /* $204E-$2071 — a TWO-TERM vector: scale[bits 0-2] + scale[bits 3-5], and a third
               half-width when bit 6 is set.  math_lo/math_hi are left as scratch. */
            math_lo = mem[SHAPE_SCALE_TBL + (vec & 0x07u)];
            math_hi = vec;
            a = (uint8_t)(mem[SHAPE_SCALE_TBL + ((vec >> 3) & 0x07u)] + math_lo);   /* flags dead */
            v = (math_hi >> 6) & 1u;                 /* BIT math_hi — only its V survives */
            if (v) {
                uint8_t m = mem[SHAPE_SCALE_TBL + 3];
                v = adc_overflow((uint8_t)a, m, 0);  /* $206C ADC — its V stays live to the exits */
                a = (uint8_t)(a + m);
            }
        } else {
            a = mem[SHAPE_SCALE_TBL + vec];          /* $2072 — one term */
        }

        /* $2076-$207F — plot_object's extra halving, rounded by the closing `ADC #0`. */
        if (proj_width_shift != 0u) {
            unsigned n     = proj_width_shift;
            uint8_t  carry = 0;
            do { carry = (uint8_t)(a & 1u); a >>= 1; } while (--n != 0u);
            v = adc_overflow((uint8_t)a, 0x00u, carry);   /* $207E ADC #0 — V live to the exits */
            a = (uint8_t)(a + carry);                /* rounds off the last bit shifted out */
        }

        /* $2080-$2096 — store the scaled offset and its negation; reject if it needs eight bits. */
        x = shared_temp_77;
        mem[SHAPE_VERTEX + x] = (uint8_t)a;
        uint8_t eor = (uint8_t)(a ^ 0xFFu);          /* EOR #$FF -> N=0, Z=(eor==0) */
        if (a & 0x80u) {                             /* $2087 — over $7F, abandon the object */
            SlotExit e = { eor, x, y, 0u, (uint8_t)(eor == 0u), v, 1u };   /* SEC */
            return e;
        }
        v = adc_overflow(eor, 0x01u, 0);             /* $208A ADC #1 — V is the loop-end exit V */
        uint8_t neg = (uint8_t)(eor + 0x01u);        /* $2089-$208A — the negation, (a^$FF)+1 */
        mem[SHAPE_VERTEX + 8 + x] = neg;
        shared_temp_77 = (uint8_t)(x + 1u);          /* INC shared_temp_77 */
        y = (uint8_t)(y + 1u);                       /* INY */
        if (y == mem[OBJ_VECTOR_END]) {              /* $2094 CPY OBJ_VECTOR_END */
            SlotExit e = { neg, x, y, 0u, 1u, v, 0u };   /* CPY equal: N=0 Z=1; CLC every vertex fitted */
            return e;
        }
    }
}

/* ---------------------------------------------------------------------------
   $209A  plot_shape_edges — THE SHAPE'S EDGES, AS FILLED SPANS  (twin #95)
   ---------------------------------------------------------------------------
   Walks the shape's edge list from shape_edge_start.  Each edge is a vertical span: two vertex
   offsets give its two scan lines (clamped below at $4F and above at object_line_ceiling, i.e.
   the horizon), two more give the x offsets at its ends, and the style byte carries both the
   colour and the walk's own control bits.  plot_view_src_line draws it, in up to three modes:

     Y = 1   open the span — it derives and remembers this end's column
     Y = 2   close it
     Y = 0   the closing arm at $2117, which takes its endpoints from the NEXT edge

   ⭐ THE STYLE BYTE'S TOP TWO BITS ARE THE WALK'S CONTROL FLOW, and they mean different things
   on the two paths.  On a REJECTED edge (either scan line off the top, or the top line at or
   past the bottom) bit 7 says "keep skipping" and bit 6 says "the shape ends here".  On a drawn
   edge bit 7 sends it to the closing arm and bit 6, tested after the close, ends the shape.
   --------------------------------------------------------------------------- */
/* Exit ABI is the full register+flag set (SlotExit): both callers (its own fixture, and
   plot_object which reads exit Y and V) compare it all.  No entry register is read — Y is
   seeded from plot_ptr3_lo, A/X are written before use — so the core takes no arguments. */
static SlotExit plot_shape_edges_core(void)
{
    uint8_t a = 0, x = 0, n = 0, z = 0, v = 0, c = 0;
    uint8_t y = plot_ptr3_lo;                             /* $209A — the shape's first edge */

    for (;;) {
        int rejected = 0;

        /* $209C-$20A6 — per-edge state: the running colour and the deferred-byte pair. */
        hypot_min_hi        = mem[SURFACE_COLOURS_TBL];
        span_defer_pending  = 0x00;
        shared_temp_8c      = 0x00;

        /* $20A7-$20B8 — the span's BOTTOM line, clamped to $4F. */
        x = mem[SHAPE_EDGE_LINE_0 + y];
        { uint8_t vtx = mem[SHAPE_VERTEX + x];            /* $20AE CLC/ADC plot_line */
          unsigned s = (unsigned)vtx + plot_line;
          a = (uint8_t)s;
          n = (uint8_t)((a >> 7) & 1u);
          /* On the BMI reject path ($20B0) this ADC's V AND C are the routine's exit flags —
             the skip loop below writes neither, so keep them live in v/c. */
          v = adc_overflow(vtx, plot_line, 0);
          c = (uint8_t)(s > 0xFFu); }
        if (n) rejected = 1;
        if (!rejected) {
            if (a >= 0x50u) a = 0x4Fu;                    /* CMP #$50; BCS clamps to $4F */
            span_line_cursor = a;

            /* $20BA-$20D4 — ...and its TOP line, floored at the horizon. */
            x = mem[SHAPE_EDGE_LINE_1 + y];
            { uint8_t vtx = mem[SHAPE_VERTEX + x];        /* $20C1 CLC/ADC plot_line */
              a = (uint8_t)(vtx + plot_line);
              n = (uint8_t)((a >> 7) & 1u);
              /* V escapes on the no-height reject path ($20D1 BCS); C there comes from the
                 $20CF CMP below, so only V needs replaying (the ADC's own C is dead). */
              v = adc_overflow(vtx, plot_line, 0); }
            if (n || !(a >= object_line_ceiling))
                a = object_line_ceiling;
            /* $20CF CMP span_line_cursor — its carry is the exit C on the no-height path. */
            c = (uint8_t)(a >= span_line_cursor);
            if (c) rejected = 1;                          /* the span has no height */
            else   span_top_line = a;
        }

        if (rejected) {
            /* $210C-$2115 — walk past this edge, and past every edge whose bit 7 says the
               skip continues.  Bit 6 ends the shape on either path.  X and V/C are the
               reject point's; the skip loop leaves all three untouched, so they exit as-is. */
            for (;;) {
                uint8_t style        = mem[SHAPE_EDGE_STYLE + y];
                int     keepSkipping = (style >> 7) & 1u;         /* the LDA's own N */
                a = (uint8_t)(style & 0x40u);                     /* AND #$40 */
                n = 0;
                z = (uint8_t)(a == 0);
                if (!z) { SlotExit e = { a, x, y, n, z, v, c }; return e; }  /* bit 6 — end */
                y = (uint8_t)(y + 1u);                            /* INY */
                if (!keepSkipping) break;
            }
            continue;
        }

        /* $20D5-$20F0 — the edge's two x offsets and its style, then open the span. */
        x = mem[SHAPE_EDGE_X_0 + y];
        shared_temp_7e = mem[SHAPE_VERTEX + x];
        x = mem[SHAPE_EDGE_X_1 + y];
        mem[OBJ_EDGE_X] = mem[SHAPE_VERTEX + x];
        mem[OBJ_EDGE_STYLE] = mem[SHAPE_EDGE_STYLE + y];
        span_saved_index = y;
        /* mode 1 — open, with the edge's own style.  plot_view_src_line is cpu-free; take its
           A/X/Y/N/Z into our locals (its V/C are dropped, exactly as its shim leaves cpu.V/C). */
        { SlotExit e = plot_view_src_line_core(0x01u, mem[OBJ_EDGE_STYLE]);
          a = e.a; x = e.x; y = e.y; n = e.n; z = e.z; }

        for (;;) {
            /* $20F1 BIT obj_edge_style — N from bit 7, V from bit 6, Z from A & style. */
            n = (uint8_t)(mem[OBJ_EDGE_STYLE] >> 7);
            v = (uint8_t)((mem[OBJ_EDGE_STYLE] >> 6) & 1u);
            z = (uint8_t)((a & mem[OBJ_EDGE_STYLE]) == 0);
            if (n) {
                /* $2117-$2142 — THE CLOSING ARM.  The span is closed against the NEXT edge's
                   columns, which is why this arm reads shape_edge_x_1 as a style and
                   shape_edge_line_0 as an x offset. */
                y = (uint8_t)(span_saved_index + 1u);
                span_saved_index = y;
                x = mem[SHAPE_EDGE_X_0 + y];
                mem[OBJ_EDGE_X] = mem[SHAPE_VERTEX + x];
                mem[OBJ_EDGE_STYLE] = mem[SHAPE_EDGE_X_1 + y];
                { SlotExit e = plot_view_src_line_core(0x00u, mem[OBJ_EDGE_STYLE]);
                  a = e.a; x = e.x; y = e.y; n = e.n; z = e.z; }
                y = span_saved_index;
                x = mem[SHAPE_EDGE_LINE_0 + y];
                mem[OBJ_EDGE_X] = mem[SHAPE_VERTEX + x];
                mem[OBJ_EDGE_STYLE] = mem[SHAPE_EDGE_STYLE + y];
                { SlotExit e = plot_view_src_line_core(0x00u, mem[OBJ_EDGE_STYLE]);
                  a = e.a; x = e.x; y = e.y; n = e.n; z = e.z; }
                continue;
            }
            { SlotExit e = plot_view_src_line_core(0x02u, 0x00u);   /* $20F5 — close it */
              a = e.a; x = e.x; y = e.y; n = e.n; z = e.z; }
            /* $20FB BIT obj_edge_style again — bit 6 (V) ends the shape.  C is still the
               no-height CMP's 0, untouched since (the plots drop it). */
            n = (uint8_t)(mem[OBJ_EDGE_STYLE] >> 7);
            v = (uint8_t)((mem[OBJ_EDGE_STYLE] >> 6) & 1u);
            z = (uint8_t)((a & mem[OBJ_EDGE_STYLE]) == 0);
            if (v) { SlotExit e = { a, x, y, n, z, v, c }; return e; }  /* bit 6: shape ends */
            y = span_saved_index;
            break;
        }
        y = (uint8_t)(y + 1u);                            /* $2102 — the next edge */
    }
}

/* ---------------------------------------------------------------------------
   $1FB4  plot_object — THE OBJECT PLOTTER  (twin #93)
   ---------------------------------------------------------------------------
   Entered with the object's SLOT in X and its four-cell argument block already set by
   draw_track_object: plot_x, plot_line, proj_width and plot_shape.
   --------------------------------------------------------------------------- */
/* Exit ABI is the full register+flag set (SlotExit).  Entry Y and V flow through the SMC-trap
   exit (which writes neither); the normal exits carry plot_shape_edges' exit Y/V, which that
   routine now returns by value. */
static SlotExit plot_object_core(uint8_t slot, uint8_t entryY, uint8_t entryV)
{
    int     i;
    uint8_t v = entryV;

    /* $1FB4-$1FC5 — this object's four MODE 5 colour patterns, taken from the ROAD's own
       surface colours; the source's entry 2 is then forced to $F0 (see the group header).
       ⚠ The DEX/BPL's final X ($FF) is dead — $1FE0's `LDX #0` rewrites it before any read. */
    math_lo = slot;
    for (i = 3; i >= 0; i--)
        mem[COLOUR_PATTERN_TBL + i] = mem[SURFACE_COLOURS_TBL + i];
    mem[SURFACE_COLOURS_TBL + 2] = 0xF0u;

    /* $1FC6-$1FDD — pattern 1 is the object's OWN colour: a car takes it from its slot number,
       slots $14..$16 from the car behind's position in the order, and the road sign ($17)
       keeps the road's.  ⚠ The `LDA $38FC,X` here is fused into its store: A and its flags
       are dead, because $1FDE's `LDX #0` rewrites N and Z and $1FE2 rewrites A. */
    if (slot != 0x17u) {
        uint8_t sel = (slot >= 0x14u) ? mem[CAR_ORDER_TBL + car_behind] : slot;
        mem[COLOUR_PATTERN_TBL + 1] = mem[SURFACE_COLOURS_TBL + (sel & 0x03u)];
    }

    /* $1FDE-$1FF9 — the width's scale, and the ceiling the spans may not rise above.  A wide
       (near) object gets the whole viewport; a narrow one is stopped at the horizon.  Under
       $40 the width is quadrupled and the extra two places handed to scale_shape_vectors. */
    proj_width_shift = 0x00u;
    uint8_t ceiling  = 0x00u;                        /* LDX #0 — the default ceiling */
    {
        uint8_t pw = proj_width, hw = horizon_half_width;
        if (pw < hw) {                               /* CMP; BCS skips the narrow-object arm */
            uint8_t op = mem[0x1FE9];
            if (op == 0xA6u)      ceiling = horizon_extent;   /* unpatched: LDX horizon_extent */
            else if (op == 0xA2u) ceiling = mem[0x1FEA];      /* a circuit's own `LDX #imm` */
            else {
                /* SMC trap — a COMPARED return.  A and its N/Z come from the CMP just made,
                   C=0 (pw < hw), X=0, and entry Y/V pass through untouched. */
                platform_smc_unhandled(0x1FE9, op);
                uint8_t d = (uint8_t)(pw - hw);
                SlotExit e = { pw, 0x00u, entryY,
                               (uint8_t)((d >> 7) & 1u), (uint8_t)(pw == hw), v, 0u };
                return e;
            }
        }
    }
    object_line_ceiling = ceiling;
    if (proj_width < 0x40u) {                         /* under $40: quadruple it, two extra places */
        proj_width       = (uint8_t)(proj_width << 2);
        proj_width_shift = 0x02u;
    }

    /* $1FFA-$2000 — the shape, clamped to 9. */
    uint8_t shapeIdx = (plot_shape >= 0x0Au) ? 0x09u : plot_shape;

    /* $2002-$2028 — and draw it.  ⭐ The loop re-enters HERE, below the clamp, so a shape over
       9 draws shape 9 and then its own (unclamped) index (group header, item 5). */
    for (;;) {
        object_shape_clamped   = shapeIdx;
        mem[OBJ_VECTOR_CURSOR]  = mem[SHAPE_VECTOR_START + shapeIdx];
        mem[OBJ_VECTOR_END]     = mem[SHAPE_VECTOR_START + 1 + shapeIdx];
        plot_ptr3_lo            = mem[SHAPE_EDGE_START + shapeIdx];

        SlotExit sv = scale_shape_vectors_core(v);
        if (sv.c) return sv;                          /* a vertex did not fit — its exit is ours */

        SlotExit pse = plot_shape_edges_core();       /* its exit Y and V escape this routine */
        uint8_t peY = pse.y;
        v = pse.v;

        /* $201E-$2028 — LDA object_shape_clamped; CMP #9.  Shape 9 loops to draw the unclamped
           index too; otherwise the object is done. */
        uint8_t osc  = object_shape_clamped;          /* CMP #9 sets A=osc, N/Z/C */
        uint8_t oscN = (uint8_t)(((uint8_t)(osc - 0x09u) >> 7) & 1u);
        uint8_t oscC = (uint8_t)(osc >= 0x09u);
        if (osc != 0x09u) {                           /* not shape 9 — the object is done */
            SlotExit e = { osc, plot_shape, peY, oscN, 0u, v, oscC };
            return e;
        }
        uint8_t td = track_direction;                 /* LDA track_direction — N/Z */
        if (td & 0x80u) {                             /* $2027 — negative: stop */
            SlotExit e = { td, plot_shape, peY, 1u, (uint8_t)(td == 0u), v, oscC };
            return e;
        }
        shapeIdx = plot_shape;                         /* re-enter with the UNCLAMPED index */
    }
}

/* The 6502-ABI shims. */
void plot_object(void)          { SlotExit e = plot_object_core(cpu.X, cpu.Y, cpu.V);
                                  cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
                                  cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c; }
void scale_shape_vectors(void)  { SlotExit e = scale_shape_vectors_core(cpu.V);
                                  cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
                                  cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c; }
void plot_shape_edges(void)     { SlotExit e = plot_shape_edges_core();
                                  cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
                                  cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c; }

/* ===========================================================================
   TWINS #96-#97 — THE OBJECT PLOTTER'S LINE SIDE, and with it the whole of
   draw_track_object's tree
   ---------------------------------------------------------------------------
     $1C1C plot_view_src_line   THE line plotter: one edge of a shape into the view source
     $1E38 fill_object_gap      ...and the columns BETWEEN two edges, filled solid

   These are what plot_shape_edges (twin #95) calls three times per edge, and they are the only
   part of the object pipeline that touches the frame buffer's source blocks.  ⚠ Ghidra called
   $1C1C `project_geometry`; nothing here projects, and the name was corrected before this twin.

   ⭐⭐ WHAT THE GROUP MADE LEGIBLE — six things, in the order they surprised:

   1. **A SHAPE EDGE IS DRAWN AS A COLUMN, NOT AS A LINE.**  Each call paints ONE column of the
      view source — `plot_ptr` is `$3000 + column x $80` and the walk is `DEY` from
      span_line_cursor down to the run's top — and the "line" between two edges is made by
      fill_object_gap filling every column in between with a single byte.  There is no Bresenham
      in the object plotter at all: the slope lives in the two endpoints' columns and the gap
      fill closes the difference.
   2. ⭐ **THE THREE MODES ARE AN OPEN/CLOSE PAIR WITH A DEFERRED BYTE BETWEEN THEM.**  Mode 1
      derives its own endpoint from shared_temp_7e, composes the column's byte, and — when the
      two endpoints land in the SAME column — stows the byte's keep-mask in shared_temp_8c,
      arms span_defer_pending and returns without painting.  Mode 2 then ORs
      pixel_after_mask_tbl into that mask, so the two ends of a one-column span are merged into
      one write instead of fighting over it.  Mode 0 is the closing arm's, and consumes the same
      pair from the other side.
   3. **THE COLOUR IS PER-PIXEL, AND THE MODE 5 BYTE IS ASSEMBLED FROM THREE MASK TABLES.**
      pixel_keep_others_tbl clears the one pixel being written, colour_pattern_and_tbl keeps the
      byte's other pixels and colour_pattern_keep_tbl selects what this pixel contributes.  The
      pixel index is the low two bits of the endpoint's x — i.e. the sub-byte part of the
      coordinate IS the pixel number, with no shifting.
   4. ⚠⚠ **AN UNTOUCHED SOURCE BYTE IS FILLED FROM THE ROAD, NOT LEFT ALONE.**  $1D5D calls
      surface_colour_at for any cell reading $00, so an object drawn over unpainted road paints
      the road's own surface colour first and then its own pixel over it.  And $55 is the
      "written but empty" sentinel both ways: a cell holding $55 is treated as blank, and a
      composed byte of 0 is stored AS $55 so the next pass does not mistake it for untouched.
   5. ⚠ **`$1D86` IS `CLC / SBC`, NOT `SEC / SBC`** — the gap width is
      `column - previous column - 1`, one less than it looks.  A width of 0 or negative skips the
      gap fill entirely, which is what makes a one-column edge cost one write.
   6. ⚠ **fill_object_gap WALKS TWO COLUMNS AT A TIME, BACKWARDS.**  Its `DEX / DEX` steps the
      count by two and `DEC plot_ptr_hi` steps the pointer by $100 — which is exactly two of the
      $80-spaced blocks — so each iteration writes a PAIR through plot_ptr and plot_ptr2, and an
      odd width finishes with the single-column tail at $1E98.  The pointer is biased by
      `$7F - span_line_cursor` so that a Y of $7F is the run's bottom line; every write therefore
      lands inside `[span_top_line, span_line_cursor]` and never in the block's tail — which is
      the only reason the shape tables that live in those tails survive (twin #95, item 7).

   ⚠ ONE THING HERE CANNOT BE SABOTAGED, and it is a property of the code: $1C1E's `LDX` is not
   observable.  It reads PVS_COLOUR only to store it in PVS_COLOUR_P, and both X and the LDX's
   N/Z are dead three instructions later (`AND #3 / TAX` rewrites X, the `LDA` before it rewrites
   the flags) — so writing the same byte without going through X changes nothing.  What DOES fail,
   as it must, is capturing PVS_COLOUR_P *after* the new colour has been chosen: 5982 of 6000
   cases.  (docs/validation-harness.md §FIFTEENTH — a surviving sabotage that is no change at all.)

   No hardware writes: the whole pass is RAM.
   ⚠⚠ Like twins #93-#95 this is a SECOND TENANT of the point_delta window; the `PVS_*` defines
   name the cells for this pass and say whose they are the rest of the time (docs/rename.md).
   =========================================================================== */

#define PIXEL_KEEP_OTHERS   0x3FE8u   /* pixel_keep_others_tbl */
#define PIXEL_AFTER_MASK    0x39D0u   /* pixel_after_mask_tbl */
#define GAP_TOP_TBL         0x3F4Fu   /* object_gap_top_tbl */
#define VIEW_SRC_PAGE       0x30u     /* the forty $80-spaced source blocks start at $3000 */
#define SRC_CELL_BLANK      0x55u     /* the "written but empty" sentinel */

#define PVS_MODE      0x007Bu   /* hypot_max_hi   — the entry mode, 0, 1 or 2 */
#define PVS_BYTE      0x007Au   /* hypot_max_lo   — the composed MODE 5 byte */
#define PVS_KEEP      0x007Du   /* point_dist_hi  — ...and the mask that keeps the rest of a cell */
#define PVS_PREV_COL  0x007Cu   /* point_dist_lo  — the column the PREVIOUS call painted */
#define PVS_COLOUR    0x0079u   /* hypot_min_hi   — this call's colour byte */
#define PVS_COLOUR_P  0x0078u   /* hypot_min_lo   — ...and the previous call's */
#define PVS_HALF      0x0074u   /* math_lo        — bit 0: is the column past the screen's middle */
#define PVS_OTHER_COL 0x008Du   /* projected_line — the OTHER endpoint's column */
#define PVS_OTHER_X   0x008Fu   /* plot_ptr3_hi   — ...and its x, at 6.2 fixed point */
#define PVS_GAP_COL   0x0075u   /* math_hi        — fill_object_gap's own column cursor */
#define PVS_GAP_FLOOR 0x0086u   /* point_delta_sign[0] — ...and its safe write cursor */

/* Halve a signed 6.2 coordinate the way $1C42/$1C66 do: arithmetically, and ROUNDED — the
   negative arm is `SEC / ROR / ADC #0`, which is a divide by two that rounds toward zero. */
static unsigned halve_signed_rounded(uint8_t value)
{
    if (value & 0x80u) {
        /* $1C42 negative arm: SEC / ROR A / ADC #0 — a >>1 that rounds toward zero.  D=0 on the
           object path (static-map §Decimal mode), so the ADC is a plain +.  Return value only:
           the caller (derive_endpoint) overwrites A and every flag before reading anything. */
        uint8_t rotated = (uint8_t)(0x80u | (value >> 1));   /* carry-in was 1 */
        return (uint8_t)(rotated + (value & 1u));            /* + the bit ROR shifted out */
    }
    return value >> 1u;                                      /* $1C48 positive arm: LSR A */
}

/* $1C4E / $1C72 — ...then bias it by plot_x and split it into a column (>> 2) and the x itself. */
static void derive_endpoint(uint8_t halved, uint16_t xCell, uint16_t colCell)
{
    /* halved + plot_x (carry-in 0, D=0) is the endpoint's x; the two LSRs give its column.
       The 6502 leaves this in A with dead flags — the caller reads mem[EDGE_COLUMN] next and
       overwrites A before anything reads it, so nothing here escapes but the two mem cells. */
    uint8_t px   = (uint8_t)(halved + plot_x);
    mem[xCell]   = px;
    mem[colCell] = (uint8_t)(px >> 2);               /* LSR A; LSR A */
}

/* ---------------------------------------------------------------------------
   $1E38  fill_object_gap — THE COLUMNS BETWEEN TWO EDGES  (twin #97)
   ---------------------------------------------------------------------------
   Fills `width` columns to the LEFT of the column in EDGE_COLUMN with one byte — the previous
   call's colour, or the blank sentinel if that was 0 — over the run's own line range.  See the
   group header, item 6, for the pair walk and the pointer bias.
   --------------------------------------------------------------------------- */
void fill_object_gap(void);   /* the shim — plot_view_src_line's close_gap arm calls it below */

/* ⚠ V and C reach the exit UNREAD (see plot_view_src_line's close_gap header), so the fixture
   drops them and this returns only the live A/X/Y/N/Z (v/c filled for completeness, uncompared). */
SlotExit fill_object_gap_core(uint8_t width)
{
    unsigned bias;
    uint8_t a, x, y, n = 0, z = 0;

    /* $1E38-$1E3F — the fill byte: the previous call's colour, or the blank sentinel if it was 0. */
    uint8_t fillByte = mem[PVS_COLOUR_P];
    if (fillByte == 0u) fillByte = SRC_CELL_BLANK;
    shared_temp_76 = fillByte;

    /* $1E40-$1E4A — the pointer bias, and the floor a column falls back to.  D=0 on the object
       path (static-map §Decimal mode): plain 8-bit -/+.  The SEC/SBC's borrow feeds the ADC. */
    unsigned biasCarry = (0x7Fu >= span_line_cursor) ? 1u : 0u;   /* SEC/SBC: C = no borrow */
    a              = (uint8_t)(0x7Fu - span_line_cursor);
    mem[PVS_HALF]  = a;                           /* ⚠ math_lo, reused: here the BIAS */
    bias           = a;
    a              = (uint8_t)(bias + span_top_line + biasCarry);
    mem[PVS_GAP_FLOOR] = a;

    /* $1E4B-$1E66 — the two pointers, one block apart, both biased down by `bias`. */
    mem[PVS_GAP_COL] = mem[EDGE_COLUMN];
    {
        uint8_t  sum      = (uint8_t)(mem[EDGE_COLUMN] + 0x5Fu);            /* column + $5F (D=0) */
        uint8_t  lsrCarry = (uint8_t)(sum & 1u);                            /* LSR A -> C */
        uint8_t  ptrHi    = (uint8_t)(sum >> 1u);                           /* LSR A */
        unsigned lowBase  = lsrCarry ? 0x80u : 0x00u;                       /* LDA #0 / ROR A */
        int      carry;
        plot_ptr_hi  = ptrHi;
        plot_ptr2_hi = ptrHi;
        /* SEC/SBC: C = no borrow.  (`>=` vs `>` can only differ at lowBase==bias, which never
           happens: lowBase is 0 or $80 and bias = $7F - line with line a view scan line ≤ $4F,
           so bias ∈ [$30,$7F] — the equality is unreachable by construction.) */
        carry = (lowBase >= (unsigned)bias) ? 1 : 0;
        a = (uint8_t)(lowBase - (unsigned)bias);
        plot_ptr_lo  = a;
        plot_ptr2_lo = (uint8_t)(a ^ 0x80u);                               /* EOR #$80 */
        if (plot_ptr2_lo & 0x80u)                                          /* $1E63 BPL */
            plot_ptr2_hi = (uint8_t)(plot_ptr2_hi - 1u);
        if (!carry) {                                                      /* $1E67 BCS */
            plot_ptr_hi  = (uint8_t)(plot_ptr_hi  - 1u);
            plot_ptr2_hi = (uint8_t)(plot_ptr2_hi - 1u);
        }
    }

    x = width;
    for (;;) {
        uint8_t c;
        /* $1E6D-$1E85 — this column pair's top line: the table's entry when it is at or below
           span_top_line, else the safe floor.  ⚠ The cursor steps back TWO columns. */
        y = mem[PVS_GAP_COL];
        a = mem[GAP_TOP_TBL + y];
        y = (uint8_t)(y - 2u);                        /* DEY; DEY */
        mem[PVS_GAP_COL] = y;
        c = (uint8_t)(a >= span_top_line);            /* CMP: leaves A; only C is read here */
        if (c) {
            a = (uint8_t)(a + mem[PVS_HALF] + 1u);    /* + bias + carry (=1 here), D=0 */
            y = a;                                    /* TAY */
            n = (uint8_t)((a >> 7) & 1u);
            if (n) {                                  /* $1E7D BPL — off the run entirely */
                uint8_t t = (uint8_t)(x - 0x02u);     /* CPX #$02 */
                c = (uint8_t)(x >= 0x02u);
                n = (uint8_t)((t >> 7) & 1u);
                z = (uint8_t)(t == 0u);
                if (!c) { SlotExit e = { a, x, y, n, z, 0u, c }; return e; }   /* $1E83 */
                goto step;                            /* $1E93 */
            }
        } else {
            y = mem[PVS_GAP_FLOOR];                   /* $1E84 (its N/Z are dead) */
        }

        /* $1E86-$1E9D — the writes: a PAIR of columns while two are left, the single tail when
           only one is. */
        a = shared_temp_76;
        c = (uint8_t)(x >= 0x02u);                    /* CPX #$02 (its N/Z here are dead) */
        {
            unsigned base  = zp_pointer(MEM_plot_ptr_lo);
            unsigned base2 = zp_pointer(MEM_plot_ptr2_lo);
            int      ram   = pointer_is_ram(base), ram2 = pointer_is_ram(base2);
            if (!c) {
                do {
                    seam_write((base + y) & 0xFFFFu, ram, a);
                    y = (uint8_t)(y + 1u);            /* INY */
                    n = (uint8_t)((y >> 7) & 1u);
                    z = (uint8_t)(y == 0u);
                } while (!n);
                { SlotExit e = { a, x, y, n, z, 0u, c }; return e; }        /* $1E9D */
            }
            do {
                seam_write((base  + y) & 0xFFFFu, ram,  a);
                seam_write((base2 + y) & 0xFFFFu, ram2, a);
                y = (uint8_t)(y + 1u);                /* INY */
                n = (uint8_t)((y >> 7) & 1u);
                z = (uint8_t)(y == 0u);
            } while (!n);
        }

    step:
        x = (uint8_t)(x - 2u);                        /* DEX; DEX — Z is from the second */
        n = (uint8_t)((x >> 7) & 1u);
        z = (uint8_t)(x == 0u);
        if (z) { SlotExit e = { a, x, y, n, z, 0u, 0u }; return e; }        /* $1E93 */
        plot_ptr_hi  = (uint8_t)(plot_ptr_hi  - 1u);  /* $1E69 — back two columns */
        plot_ptr2_hi = (uint8_t)(plot_ptr2_hi - 1u);
    }
}

/* ---------------------------------------------------------------------------
   $1C1C  plot_view_src_line — ONE COLUMN OF ONE SHAPE EDGE  (twin #96)
   ---------------------------------------------------------------------------
   `mode` arrives in Y (0, 1 or 2 — see the group header, item 2) and the colour selector in A.
   --------------------------------------------------------------------------- */
SlotExit plot_view_src_line_core(uint8_t mode, uint8_t colourSelect)
{
    unsigned pixel;
    uint8_t  edgeCol, blockStart, acc = 0;
    /* The escaping registers, tracked as locals so the body is cpu-free.  A is `acc` (the 6502's
       working byte); x/y are the exit X/Y; n/z the exit N/Z.  V and C reach every exit UNREAD
       (see the close_gap header) and are dropped from the fixture mask, so they are not tracked. */
    uint8_t  x = 0, y, n = 0, z = 0;

    /* $1C1C-$1C3D — the entry state.  The colour this call chooses becomes the NEXT call's
       "previous", which is how a span's two ends agree on a byte.  Y carries `mode` from here
       down to the mode dispatch; nothing below reassigns it until then. */
    y                 = mode;
    mem[PVS_MODE]     = mode;
    mem[PVS_COLOUR_P] = mem[PVS_COLOUR];                             /* $1C1E-$1C21 */
    mem[PVS_COLOUR]   = mem[COLOUR_PATTERN_TBL + (colourSelect & 0x03u)];
    mem[PVS_PREV_COL] = mem[EDGE_COLUMN];
    shared_temp_76    = mem[COLOUR_PATTERN_TBL + ((mem[OBJ_EDGE_STYLE] & 0x0Cu) >> 2)];
    plot_ptr_lo       = 0x00u;

    /* $1C3E-$1C7A — the endpoints.  Mode 1 derives its OWN from shared_temp_7e and then the
       other; mode 0 takes the saved pair and re-derives the other over it; mode 2 takes the
       saved pair and derives nothing. */
    if (mode == 1) {
        derive_endpoint((uint8_t)halve_signed_rounded(shared_temp_7e),
                        MEM_shared_temp_7e, EDGE_COLUMN);
        derive_endpoint((uint8_t)halve_signed_rounded(mem[OBJ_EDGE_X]),
                        PVS_OTHER_X, PVS_OTHER_COL);
    } else {
        mem[EDGE_COLUMN] = mem[PVS_OTHER_COL];
        shared_temp_7e   = mem[PVS_OTHER_X];
        if (mode == 0)
            derive_endpoint((uint8_t)halve_signed_rounded(mem[OBJ_EDGE_X]),
                            PVS_OTHER_X, PVS_OTHER_COL);
    }

    /* $1C7B-$1C88 — the target pointer, $3000 + column x $80, and the screen-half bit.  The
       column is unmodified from here until the gap walk, so read it once.  ⚠ the ADC's own V/C
       reach the exit unread (dropped from the mask); only its SUM matters — plot_ptr_hi and the
       A the 6502 leaves in it, which IS the exit A of the two off-view arms below. */
    edgeCol       = mem[EDGE_COLUMN];
    mem[PVS_HALF] = (uint8_t)((mem[PVS_HALF] << 1) | (edgeCol >= 0x14u ? 1u : 0u));
    plot_ptr_lo   = (uint8_t)((edgeCol & 1u) << 7);
    plot_ptr_hi   = (uint8_t)((edgeCol >> 1) + VIEW_SRC_PAGE);
    acc           = plot_ptr_hi;                    /* A = plot_ptr_hi, live at the exits below */

    /* $1C89-$1C9D — off the right of the viewport, or the run's top line. */
    if (edgeCol >= 0x28u) {
        /* $1D94-$1DA5 — mode 1 gives up; the others clamp the column to $28 and still close
           the gap behind them.  CPX left X = edgeCol; A is still plot_ptr_hi. */
        x = edgeCol;
        if (mode == 1) {                            /* CPY #1: mode==1 -> result 0 */
            n = 0; z = 1;
            { SlotExit e = { acc, x, y, n, z, 0, 0 }; return e; }
        }
        {
            uint8_t prevCol = mem[PVS_PREV_COL];
            if (prevCol >= 0x28u) {                 /* CMP #$28 taken (>=): C=1 */
                uint8_t r = (uint8_t)(prevCol - 0x28u);
                acc = prevCol;                      /* CMP left A = PVS_PREV_COL */
                n = (uint8_t)((r >> 7) & 1u); z = (uint8_t)(r == 0u);
                { SlotExit e = { acc, x, y, n, z, 0, 0 }; return e; }
            }
        }
        mem[EDGE_COLUMN] = 0x28u;
        goto close_gap;
    }
    x = edgeCol;                                    /* the fall-through CPX also left X = edgeCol */
    blockStart = span_top_line;
    if (blockStart < mem[DASH_BLOCK_STARTS + edgeCol])              /* clamp to the block top */
        blockStart = mem[DASH_BLOCK_STARTS + edgeCol];
    mem[EDGE_BLOCK_START] = blockStart;

    /* $1C9E-$1CA9 — a run with no height. */
    if (blockStart >= span_line_cursor) {
        acc = blockStart;                           /* CMP left A = blockStart */
        if (mode == 1) {                            /* CPY #1: mode==1 -> result 0 */
            n = 0; z = 1;
            { SlotExit e = { acc, x, y, n, z, 0, 0 }; return e; }
        }
        goto prev_col;
    }

    /* $1CAA-$1CC2 — the style's bit 4 re-picks the colour, but only on a closing pass whose
       parity disagrees with the screen half.  ⚠ mode 0 (Y == 0) is kept out by the TYA/BEQ. */
    if ((mem[OBJ_EDGE_STYLE] & 0x10u) != 0 &&
        mode != 0 &&
        ((mode ^ mem[PVS_HALF]) & 0x01u) != 0)
        shared_temp_76 = mem[COLOUR_PATTERN_TBL + (mem[OBJ_EDGE_STYLE] & 0x03u)];

    /* $1CC3-$1CD0 — the PIXEL is the low two bits of the endpoint's x, and the edge colour is
       cut down to that pixel's own bits. */
    pixel          = shared_temp_7e & 0x03u;
    shared_temp_76 = (uint8_t)(shared_temp_76 & (uint8_t)~mem[PIXEL_KEEP_OTHERS + pixel]);

    /* $1CD1-$1D43 — compose the cell, per mode.  `acc` is the composed byte (the 6502's A) as
       it flows across the shared same-column / read-modify-write tails. */
    if (mode == 0) {
        /* ---- mode 0: the closing arm's own pass ---- */
        uint8_t half0 = (uint8_t)(mem[COLOUR_PATTERN_AND + pixel] & mem[PVS_COLOUR_P]);
        mem[PVS_HALF] = half0;                    /* ⚠ math_lo again, here a partial byte */
        acc = (uint8_t)(((mem[PVS_COLOUR] & mem[COLOUR_PATTERN_KEEP + pixel]) | half0)
                        & mem[PIXEL_KEEP_OTHERS + pixel]) | shared_temp_76;
        mem[PVS_BYTE] = acc;

        if (span_defer_pending != 0) {
            /* $1CEE — mode 1 left a byte for us: merge it and take the shared tail. */
            span_defer_pending = 0x00u;
            mem[PVS_KEEP]      = shared_temp_8c;
            acc                = (uint8_t)(~shared_temp_8c & acc);
            goto same_column_test;
        }
        x = edgeCol;                                         /* CPX left X = edgeCol */
        if (edgeCol == mem[PVS_OTHER_COL]) {                 /* $1CFD */
            /* both ends in one column: hand the byte on and paint nothing.  load_a's A/N/Z
               die at close_gap, so only the store survives. */
            mem[PVS_COLOUR] = mem[PVS_BYTE];
            goto prev_col;
        }
        acc = acc ? acc : SRC_CELL_BLANK;                    /* $1D0A */
        /* $1DE5-$1DEE — the PLAIN fill: no read, no surface colour, just the byte.  The
           hardware-window test is hoisted onto the POINTER, one per run. */
        {
            unsigned base = zp_pointer(MEM_plot_ptr_lo);
            int      ram  = pointer_is_ram(base);
            uint8_t  line = span_line_cursor;
            while (line != mem[EDGE_BLOCK_START]) {
                seam_write((base + line) & 0xFFFFu, ram, acc);
                line--;
            }
            y = mem[EDGE_BLOCK_START];            /* the fill leaves Y at the stop line */
        }
        goto prev_col;
    }
    if (mode == 1) {
        /* ---- mode 1: open the span ---- */
        {
            uint8_t pat = mem[COLOUR_PATTERN_AND + pixel];   /* $1D17 */
            mem[PVS_KEEP] = pat;
            acc = (uint8_t)(((uint8_t)~pat & mem[PVS_COLOUR] & mem[PIXEL_KEEP_OTHERS + pixel])
                            | shared_temp_76);
        }
    same_column_test:
        x = edgeCol;                                         /* CPX left X = edgeCol */
        if (edgeCol == mem[PVS_OTHER_COL]) {                 /* $1D25 */
            /* $1D2B — one column for both ends: DEFER, and remember the carry in bit 7.  The
               ROR carries in the compare's C, which is 1 on the equal (>=) arm; the exit A is
               PVS_KEEP (load_a) and N/Z come from the ROR result. */
            uint8_t prev = span_defer_pending;
            uint8_t res  = (uint8_t)((prev >> 1) | 0x80u);   /* carry-in = 1 -> bit 7 set */
            mem[PVS_COLOUR] = acc;
            shared_temp_8c  = mem[PVS_KEEP];
            acc             = mem[PVS_KEEP];                 /* load_a left A = PVS_KEEP */
            span_defer_pending = res;
            n = (uint8_t)(res >> 7);                         /* = 1 */
            z = (uint8_t)(res == 0);                         /* = 0 */
            { SlotExit e = { acc, x, y, n, z, 0, 0 }; return e; }
        }
        /* not the same column: fall through to the read-modify-write fill */
    } else {
        /* ---- mode 2: close the span, merging mode 1's deferred mask ---- */
        uint8_t keep = (uint8_t)(shared_temp_8c | mem[PIXEL_AFTER_MASK + pixel]);  /* $1D34 */
        mem[PVS_KEEP] = keep;
        acc = (uint8_t)(((uint8_t)~keep & mem[PVS_COLOUR_P] & mem[PIXEL_KEEP_OTHERS + pixel])
                        | shared_temp_76);
    }

    /* $1D44-$1D6E — the READ-MODIFY-WRITE fill, which is the one that consults the road.  `acc`
       is the composed byte on entry and the last cell written on exit (mode 1's return needs
       that in A). */
    mem[PVS_BYTE]  = acc;
    shared_temp_8c = 0x00u;
    {
        uint8_t line = span_line_cursor;
        while (line != mem[EDGE_BLOCK_START]) {
            /* ⚠ NOT HOISTED, for column_gap_walk's reason: the run can cover the cells that
               drive it ($0082 is the end line and $0085 the column), so the pointer, the end
               line and the column are all re-read every pass. */
            unsigned base = zp_pointer(MEM_plot_ptr_lo);
            int      ram  = pointer_is_ram(base);
            unsigned cell = (base + line) & 0xFFFFu;
            uint8_t  src  = seam_read(cell, ram);
            if (src == 0) {
                /* $1D5D — an untouched cell takes the surface's colour, then merges.  Only the
                   colour it returns is used; surface_colour_at's own exit X/V do not survive
                   (the CPX at $1D6F overwrites X, V/C are dropped), and its colour never depends
                   on the entry X/V, so passing the current x is harmless. */
                acc = surface_colour_at_core(line, mem[EDGE_COLUMN], x, 0u).a;
                acc = (uint8_t)((acc & mem[PVS_KEEP]) | mem[PVS_BYTE]);   /* $1D60 */
                if (acc == 0) acc = SRC_CELL_BLANK;
            } else if (src == SRC_CELL_BLANK) {
                acc = mem[PVS_BYTE] ? mem[PVS_BYTE] : SRC_CELL_BLANK;     /* $1D57 */
            } else {
                acc = (uint8_t)((src & mem[PVS_KEEP]) | mem[PVS_BYTE]);   /* $1D60 */
                if (acc == 0) acc = SRC_CELL_BLANK;
            }
            seam_write(cell, ram, acc);
            line--;
        }
        y = mem[EDGE_BLOCK_START];
        /* acc holds the last cell written — mode 1 returns it in A */
    }

    /* $1D6F-$1D7B — every mode but 1 also closes the column's own gaps. */
    x = mem[PVS_MODE];                            /* CPX left X = PVS_MODE on both arms */
    if (mem[PVS_MODE] == 0x01u) {                 /* CPX #1: equal -> return */
        n = 0; z = 1;
        { SlotExit e = { acc, x, y, n, z, 0, 0 }; return e; }
    }
    mem[EDGE_COLUMN] = (uint8_t)(mem[EDGE_COLUMN] + 1u);
    {
        /* column_gap_walk's exit X/Y survive to close_gap (which sets only A/N/Z), so marshal
           them into the tracked registers.  V in is a byproduct (dropped). */
        SlotExit cg = column_gap_walk_core(x, y, 0u);
        x = cg.x; y = cg.y;
    }
    mem[EDGE_COLUMN] = (uint8_t)(mem[EDGE_COLUMN] - 1u);

prev_col:
    /* $1D7C-$1D85 — a previous column off the viewport is treated as "no gap at all". */
    if (mem[PVS_PREV_COL] >= 0x28u) mem[PVS_PREV_COL] = 0xFFu;

close_gap:
    /* $1D86-$1D93 — CLC/SBC: the gap is `column - previous - 1` (D=0 on the object path).  Zero
       or negative, no fill.  Only N and Z are read (the branch just below); the subtract's V and
       C reach the routine's exit UNREAD — every caller ($20F1/$20F5) opens BIT before touching a
       flag — so they are dropped from the fixture mask, not reproduced.  X is set only AFTER the
       exit test, so the return leaves whatever X the path already held. */
    {
        uint8_t gap = (uint8_t)(mem[EDGE_COLUMN] - mem[PVS_PREV_COL] - 1u);
        acc = gap;
        n   = (uint8_t)((gap >> 7) & 1u);
        z   = (uint8_t)(gap == 0u);
    }
    if (z || n) { SlotExit e = { acc, x, y, n, z, 0, 0 }; return e; }
    x = acc;                                      /* TAX before fill_object_gap */
    /* fill_object_gap's own exit A/X/Y/N/Z are the routine's (V/C unread here). */
    return fill_object_gap_core(x);
}

/* The 6502-ABI shims. */

/* ===========================================================================
   TWINS #98-#114 — THE DRIVING CONTROLS, and with them the last of the campaign's trees
   ---------------------------------------------------------------------------
   Seventeen C functions, ~700 bytes: everything `read_driving_controls` reaches.  It is one
   cluster rather than seventeen decisions because the chain is spliced together by TAIL JUMPS
   across four regions —

     $1579 read_driving_controls → $1EE9 steer_assist_dispatch → $15F4 steer_demand_from_slip
        → $160D steer_demand_store → $1EFA steer_apply_with_assist → $1F08 apply_steering_assist
        → $1612 apply_steer_demand → $162D clamp_and_store_steer_angle → the throttle and gears

   — so the "routine" the listing splits into eight is really ONE pass with eight entry points.
   Under it: $1F9B limit_steer_demand, $63C5 poll_steering_assist, $503F adc_read, and the text
   path $42D0 draw_gear_indicator → $508C vdu_char_wide / $5092 vdu_char_def → $509D
   vdu_char_emit → $50FA mode5_addr_for_cell → $50FC mode5_addr.

   ⭐⭐ WHAT THE GROUP MADE LEGIBLE — seven things, in the order they surprised:

   1. **THE JOYSTICK'S STEERING IS SQUARED.**  $1591-$1593 stores the centred reading in math_hi
      and then calls `mul8` with the SAME value still in A, so the demand is `reading x reading`.
      That is the non-linear response an analogue stick needs and it costs one instruction.
   2. **COMPUTER ASSISTED STEERING IS TWO DIFFERENT ASSISTS, chosen by how hard you are asking.**
      A demand under 5 goes to `steer_demand_from_slip`, which just cancels the car's own slip
      (model_state element $0A, quartered, and never more than the lock already applied); a
      bigger one goes to `apply_steering_assist`, which reads a TRACK EDGE ahead of the car and
      steers toward it.  ⭐ Which edge is the whole of the look-ahead: slot `$32` or slot `$0A` of
      edge_x, picked on the demand's direction.
   3. **THE ASSIST'S GAIN FALLS WITH SPEED AND IS CAPPED BY THE CORNER.**  `$3C - road_speed`
      doubled plus `$20` is the gain, floored at `$20`; the live section's own curvature
      (`section_flags & $7F`, clamped to 2..7, shifted up four) caps it.  So the assist helps
      most at low speed and is deliberately weak through a tight corner.
   4. ⚠⚠ **`poll_steering_assist` PRESERVES A ACROSS ITSELF, and both callers depend on it.**  It
      is `PHA … PLA`, and the `CMP #5` at $1EF3 is therefore comparing the CALLER'S demand, not
      the assist setting.  Reading it the other way makes the whole dispatch look like nonsense.
   5. **THE ASSIST LAMP IS FOUR SCREEN BYTES, written by that same routine** — $77DB, $77DC,
      $77E3 and $77E4 take `steering_assist_flag` shifted right 0..3 places, which is dark at 0
      and four lit pixels at $80.  So "read the setting" and "draw the setting" are one call.
   6. **THE GEAR DIGIT IS ONE CHARACTER DRAWN TWICE.**  `draw_gear_indicator` calls
      `vdu_char_wide` with shared_temp_77 = $22 and then = $FF; the first cell takes the
      character's left four pixels, the second its right four, and `vdu_char_column` INCs itself
      between them.  A MODE 5 byte is four 2-bit pixels, so an 8-pixel MOS character has to
      become two bytes — and this is where the dashboard's double-width text comes from.
   7. ⚠⚠ **`char_row_addr_lo`'s ENTRIES 8..15 ARE `pixel_keep_others_tbl`.**  The two tables
      overlap at $3FE8, so `mode5_addr` returns a wrong low byte for character rows 8..15 and can
      only legally be asked for rows 0..7 and 16..31.  [INFERRED] the overlap is deliberate and
      records which rows the text path owns; the road view owns the middle of the screen.

   ⚠⚠ **AND ONE PER-CIRCUIT SMC SITE THAT IS THE SQUARING ITSELF**, $1593: Silverstone's
   `JSR mul8` is what an expansion circuit replaces with its own hook, so the twin dispatches on
   the operands instead of baking the call.  MEASURED as a real difference, not a precaution — a
   randomised pre-state traps 255 times in 256, and the fixture reported exactly the joystick
   arm's 2448 of 5000 cases wrong until this arm existed.

   ⚠ Four MOS calls in the group and every one of them can clobber A, X and Y with no hint in
   the listing (docs/bbc-reference-loop.md): OSBYTE $80 in `adc_read` and in the joystick
   gear-change poll, OSWORD 10 in `vdu_char_emit`, OSWRCH in `vdu_char_def`'s text arm.

   ⚠⚠ **THE GROUP BOUGHT THREE MORE HARNESS HOOKS, all three from surviving sabotages, and all
   three the same shape as the sub-models' `platform_test_key_down`** — a test backend answering
   one DEFAULT for a whole input class (docs/validation-harness.md §FIFTEENTH):

     * `platform_test_key_only` — the backend answered the same thing for every key code, so the
       fixture could only produce "no key down" or "ALL SEVEN down".  Every interesting arm here
       is a ONE-KEY arm (steer left or right, throttle or brake, gear up or down), and "the key
       direction is not compared with the current sign" survived 5000 cases because
       `STEER_KEYS` was only ever 0 or 3.
     * `platform_test_adc` — `Platform::adcAxis` answers dead centre, which pins `adc_read`'s
       magnitude to 0.  Its dead-zone compare and the joystick's whole x1.5 pedal arm were
       unreachable; two sabotages survived, one of them a dropped ASL carry that is a REAL defect
       class this group already had twice.
     * and `gear_key_latch` forced to 0 — not a hook but the same lesson: a random byte is 0 once
       in 256, so the gear-shift body ran in 20 of 5000 cases and both of its wrap arms survived.
   ⚠⚠ The steering chain is a SECOND TENANT of math_lo/math_hi/shared_temp_76 — the `STEER_*`
   defines below are its own names for them (docs/rename.md).
   =========================================================================== */

#define STEER_SIGN     0x0074u   /* math_lo        — the demand's sign byte; bit 0 = negative */
#define STEER_DEMAND   0x0075u   /* math_hi        — ...and its magnitude */
#define STEER_KEYS     0x0076u   /* shared_temp_76 — 0 none, 1 or 2 one way, 3 both keys down */
#define ASSIST_LAMP_0  0x77DBu   /* the four dashboard screen bytes poll_steering_assist lights */
#define ASSIST_LAMP_1  0x77DCu
#define ASSIST_LAMP_2  0x77E3u
#define ASSIST_LAMP_3  0x77E4u
#define OPTION_FLAGS   0x05F5u   /* state_flags + 1: bit 7 selects the JOYSTICK input path */
#define SECTION_CURVE  0x0701u   /* section_curve — field 1 of the per-section record */
#define SLIP_MAG_LO_10 0x62DAu   /* slip_magnitude_lo / _hi — model_state element $0A */
#define SLIP_MAG_HI_10 0x62EAu
#define GEAR_CHAR_TBL  0x3779u   /* gear_char_tbl — 'R' 'N' '1'..'5' 'P' */
#define VDU_CHAR_BLOCK 0x62C3u   /* vdu_char_block — the OSWORD 10 block */
#define CHAR_ROW_LO    0x3FE0u   /* char_row_addr_lo — ⚠ entries 8..15 are pixel_keep_others_tbl */
#define CHAR_ROW_HI    0x3B06u   /* char_row_addr_hi */

static void steer_apply_with_assist_core(void);
static void apply_steer_demand_core(uint8_t signByte);
static void clamp_and_store_steer_angle_core(uint8_t a);

/* Typed results for the text/screen-address cluster's cpu-free cores. */
uint8_t vdu_char_emit_core(void);                  /* returns the block char left in A */
void adc_read(void);   /* the 6502-ABI shim; the two driver callers below still enter it that way */
void draw_gear_indicator(void);   /* likewise: read_pedals_and_gears enters it via the shim */

/* ---------------------------------------------------------------------------
   $50FC  mode5_addr — THE SCREEN ADDRESS OF A CHARACTER CELL  (twins #113, #114)
   ---------------------------------------------------------------------------
   plot_ptr = char_row_addr[Y >> 3] + A x 2, and Y comes back as the scan line within that
   character row.  $50FA is the entry that multiplies a character COLUMN by four first, so the
   pair together computes `column x 8` — one MODE 5 cell.
   --------------------------------------------------------------------------- */
Mode5Addr mode5_addr_core(uint8_t quarterOffset, uint8_t y)
{
    /* $50FC-$5118 — plot_ptr = char_row_base + (quarterOffset x 2).  The 6502 does it as
       ASL plot_ptr_lo / ROL A to spread the doubled offset over the pair, then a two-byte
       ADC of the row base; D = 0 on the dashboard-text path (docs/static-map.md §Decimal
       mode: a screen address is never computed in BCD), so this is a plain 16-bit add.
       The second add's C and V are dead at every caller — nothing writes either flag after
       mode5_addr returns, so they propagate as the whole text path's exit C/V, but the game
       reads only plot_ptr / X / A / Y; the fixture drops V and C for this cluster. */
    unsigned row = y >> 3;                              /* Y selects the character row */
    uint16_t base = (uint16_t)(((uint16_t)mem[CHAR_ROW_HI + row] << 8) | mem[CHAR_ROW_LO + row]);
    uint16_t addr = (uint16_t)(base + ((unsigned)quarterOffset << 1));
    plot_ptr_lo = (uint8_t)addr;
    plot_ptr_hi = (uint8_t)(addr >> 8);

    /* $5119-$511F — the line within the row is the low three bits; it leaves in both A and Y
       with N always clear (the value is < 8).  row leaves in X. */
    Mode5Addr r;
    r.row  = (uint8_t)row;
    r.line = (uint8_t)(y & 0x07u);
    return r;
}

Mode5Addr mode5_addr_for_cell_core(uint8_t column, uint8_t y)
{
    /* $50FA — a character COLUMN is four quarter-cells wide (ASL A / ASL A), then mode5_addr. */
    return mode5_addr_core((uint8_t)(column << 2), y);
}

/* ---------------------------------------------------------------------------
   $509D  vdu_char_emit — ONE CHARACTER INTO THE DASHBOARD  (twin #112)
   $508C  vdu_char_wide                                     (twin #110)
   $5092  vdu_char_def                                      (twin #111)
   ---------------------------------------------------------------------------
   OSWORD 10 hands back the character's 8x8 bitmap; shared_temp_77 then says which HALF of it
   this cell carries ($00 = no expansion, bit 7 clear = the left four pixels, set = the right
   four shifted up), and the eight bytes go into the screen BOTTOM-UP.
   --------------------------------------------------------------------------- */
uint8_t vdu_char_wide_core(uint8_t ch)
{
    mem[VDU_CHAR_BLOCK] = ch;                          /* $508C — shared_temp_77 is the caller's */
    return vdu_char_emit_core();                       /* the block byte comes back live */
}

/* $5092 vdu_char_def — the OSWRCH-path branch and its BIT flags live in the SHIM (they are
   6502-ABI reconstruction, not computation); the core is only the bitmap-emit arm. */
uint8_t vdu_char_def_core(uint8_t ch)
{
    mem[VDU_CHAR_BLOCK] = ch;                          /* $5096 */
    shared_temp_77      = 0x00u;                       /* ...and no half-width expansion */
    return vdu_char_emit_core();
}

uint8_t vdu_char_emit_core(void)
{
    /* $50A1-$50A9 — OSWORD 10 fills the 8-row bitmap into the block at $62C3.
       MOS ABI — documented cpu exception: OSWORD wants the parameter-block pointer in X/Y and
       the reason code in A, and clobbers X/Y.  The routine's $509D/$50EF PUSH/PULL of the
       caller's X/Y is reconstructed by each shim (and the i=5 fixture ignores the two stack
       residue bytes the transliterated oracle leaves at $01FE/$01FF). */
    mos_osword(0x0Au, 0xC3u, 0x62u);   /* param block at $62C3, reason code 10 */

    /* $50AA-$50C5 — half-width expansion (rows 1..8): shared_temp_77 zero leaves the glyph
       whole; bit 7 clear keeps only the top nibble, set brings the bottom nibble up. */
    if (shared_temp_77 != 0) {
        int leftHalf = (shared_temp_77 & 0x80u) == 0;
        int i;
        for (i = 8; i >= 1; i--) {
            uint8_t row = mem[VDU_CHAR_BLOCK + i];
            mem[VDU_CHAR_BLOCK + i] = leftHalf ? (uint8_t)(row & 0xF0u)
                                               : (uint8_t)(row << 4);
        }
    }

    /* $50C6-$50EA — blit the eight rows BOTTOM-UP; stepping off the top of a character row
       backs plot_ptr up one MODE-5 character row ($0140 bytes) and resets the line to 7. */
    Mode5Addr m = mode5_addr_for_cell_core(mem[0x62CCu], mem[0x62CDu]);  /* column, row */
    uint8_t line = m.line;
    {
        int i;
        for (i = 8; i >= 1; i--) {
            unsigned base = zp_pointer(MEM_plot_ptr_lo);
            seam_write((base + line) & 0xFFFFu, pointer_is_ram(base),
                       mem[VDU_CHAR_BLOCK + i]);
            line = (uint8_t)(line - 1);                /* DEY */
            if (line & 0x80u) {                        /* $50D7 BPL — off the top of the row */
                /* $50D9-$50E4 — plot_ptr -= $0140 (a plain 16-bit subtract; its flags are
                   dead — line is reset just below and the exit A/N/Z come from the block). */
                uint16_t p = (uint16_t)((((uint16_t)plot_ptr_hi << 8) | plot_ptr_lo) - 0x0140u);
                plot_ptr_lo = (uint8_t)p;
                plot_ptr_hi = (uint8_t)(p >> 8);
                line = 0x07u;
            }
        }
    }

    mem[0x62CCu] = (uint8_t)(mem[0x62CCu] + 1);        /* $50EB — the next cell along */
    return mem[VDU_CHAR_BLOCK];                         /* $50F2 — the character comes back live */
}

/* ---------------------------------------------------------------------------
   $3D50  print_spaces — `count` SPACES THROUGH THE VDU CHAR PATH  (twin #148)
   Each space goes through the same dispatch vdu_char_def uses: OSWRCH when
   text_out_via_mos bit 7 is set (X/Y ambient), otherwise the bitmap emitter.
   The 6502 counts down and tests AFTER the decrement, so a count of 0 prints
   256 — a do-while.  The loop counter was math_lo ($74); it is a local now (the
   reader-nativization step of the wide-value cleanup), with the cell's exit value
   (0) still written so the routine stays byte-exact until math_lo is relocated.
   Exit A = the space byte ($20); the caller's exit N/Z come from the final
   DEC to zero (N=0, Z=1) — three callers branch on that Z.
   --------------------------------------------------------------------------- */
uint8_t print_spaces_core(uint8_t count, uint8_t x, uint8_t y)
{
    uint8_t c = count;
    do {
        if (text_out_via_mos & 0x80u)          /* $5092 BIT/BMI — OSWRCH path */
            mos_oswrch(0x20u, x, y);
        else
            vdu_char_def_core(0x20u);          /* the MODE-5 bitmap emitter */
    } while (--c != 0);                         /* $3D59 DEC math_lo / BNE — post-tested */
    /* The 6502 counted down in math_lo ($74), so it exits holding 0; the counter is a local
       now, but keep that scratch residue until math_lo is relocated wholesale (the reader
       campaign nativizes readers first, then removes the cell) — keeps the byte-exact match. */
    math_lo = 0x00u;
    return 0x20u;                               /* both paths return the character in A */
}

/* ---------------------------------------------------------------------------
   $7B4A  draw_starting_lights — WALK THE LIGHT SEQUENCE, PAINT THE COLUMN  (twin #149)
   Lives in the $7B00 overlay.  Does nothing outside the race proper (session_is_race
   positive) or once the lights are dark (state 0).  Otherwise it advances
   start_light_state through its sequence and lays a light column straight into
   view_src_blocks column 37 (the screen's right edge): ten rows of $F0, then the
   arm's pattern into the middle six, XORing a second value between rows.
     • state $80  — top of the sequence: force $F0 ("all lit") until the engine
                    catches (engine_running bit 7), else hold; pattern $80.
     • state $A0  — hold 64 frames (loop_counter AND $3F); when it lapses, release
                    to $28 with the green pattern $F2/$05, otherwise pattern $A5/$77.
     • otherwise  — step the state down by one; a still-negative result keeps the
                    amber pattern ($80 at/above $C0, else $A5/$77); a positive result
                    uses the green pattern $F2/$05.
   The XOR value lived in math_lo ($74) across the fill loop; it is a local now (the
   reader-nativization step of the wide-value cleanup), with the cell's exit value
   still written so the routine stays byte-exact until math_lo is relocated.
   Returns the painted pattern (the byte the 6502 PHA'd), or -1 on the early exits
   so the shim can reproduce that push's stack residue; exit regs/flags are dead at
   the sole (native) caller, race_main_loop.
   --------------------------------------------------------------------------- */
int draw_starting_lights_core(void)
{
    /* $42C0 = view_src_blocks column 37, offset $40 — the ten-row light column. */
    const uint16_t light_col = (uint16_t)(VIEW_SRC_BLOCKS + 37u * 0x80u + 0x40u);

    if (!(session_is_race & 0x80u)) return -1;      /* $7B4A — race only */
    uint8_t state = start_light_state;              /* $7B4E */
    if (state == 0) return -1;                      /* $7B50 — lights dark */

    uint8_t new_state = state;
    uint8_t pattern, eor;

    if (state == 0x80u) {                           /* $7B54 — top of the sequence */
        if (engine_running & 0x80u) new_state = 0xF0u;  /* $7B58 — force $F0 until engine catches */
        pattern = 0x80u; eor = 0x00u;               /* $7B5E/$7B60 */
    } else if (state == 0xA0u) {                    /* $7B64 CPX #$A0 */
        if ((loop_counter & 0x3Fu) != 0) {          /* $7B75 — still holding (64-frame dwell) */
            pattern = 0xA5u; eor = 0x77u;           /* $7B6F/$7B71 */
        } else {
            new_state = 0x28u;                      /* $7B7B — release */
            pattern = 0xF2u; eor = 0x05u;           /* $7B7D/$7B7F — green */
        }
    } else {
        uint8_t dec = (uint8_t)(state - 1u);        /* $7B68 DEX */
        new_state = dec;
        if (state & 0x80u) {                        /* the DEX result stays negative here */
            if (dec >= 0xC0u) { pattern = 0x80u; eor = 0x00u; }  /* $7B6D BCS -> $7B5E */
            else              { pattern = 0xA5u; eor = 0x77u; }  /* $7B6F */
        } else {
            pattern = 0xF2u; eor = 0x05u;           /* $7B7D — positive state, green */
        }
    }

    start_light_state = new_state;                  /* $7B81 */
    math_lo = eor;                                  /* $7B84 — the 6502 parked Y here across the fill */

    { int i;
      for (i = 9; i >= 0; i--) mem[light_col + i] = 0xF0u;   /* $7B8A — clear ten rows */
      uint8_t a = pattern;
      for (i = 5; i >= 0; i--) {                    /* $7B93 — pattern into the middle six */
          mem[light_col + 2 + i] = a;
          a ^= eor;                                 /* $7B96 EOR math_lo */
      }
    }
    return (int)pattern;
}

/* ---------------------------------------------------------------------------
   $4F44  update_horizon_band — MOVE THE HORIZON WITH THE HILLS  (twin #150)

   The body's 21st call.  Band 1 is the sky; its duration is where the sky/track
   split sits, and moving that split IS "moving the horizon".  The routine takes
   $3C - horizon_extent, clamps it (floor $F5 on the negative side; a per-circuit
   SMC compare+clamp on the positive side), then forms band 1's 16-bit duration as

        0x04D8 + (clamp << 6)          with clamp SIGN-EXTENDED into bits 15..14

   and stores it in band1_duration_lo/hi.

   ⭐ Wide-value cleanup: the 6502 built  clamp << 6 + sign-extend  as a PHP/PLP-
   threaded pair of RORs across the byte lanes math_hi:A — the archetypal byte-lane
   carry idiom.  As a wide value it is one expression: (clamp << 6) is a 16-bit
   left shift, and the saved carry c0 rotated into the top is just a sign extension
   (c0 => +0xC000).  math_hi keeps its 6502 exit value — the high byte of the sign-
   extended clamp<<6 — so the cell stays byte-pinned until math_hi ($75) is finally
   relocated to a wide value; nothing here reads it back except the high-byte add,
   which the wide `r` already accounts for.

   Returns 0 on the completed path, with the 16-bit duration in *r_out and the
   6502 math_hi exit value in *mathhi_out; returns -1 if a per-circuit SMC site is
   unrecognised (it has already been reported).  Result-only: exit A/X/Y/flags are
   dead at both callers (the race body flows straight to the 22nd call). */
int update_horizon_band_core(uint16_t *r_out, uint8_t *mathhi_out)
{
    uint8_t a = (uint8_t)(0x3Cu - horizon_extent);   /* $4F44 SEC; SBC horizon_extent */
    uint8_t v;      /* the clamped extent that gets shifted into the duration */
    int      c0;    /* carry threaded into the sign-extension: 1 => extend $C000 */

    if (a & 0x80u) {                     /* $4F49 BMI: the difference went negative */
        if (a >= 0xF5u) { v = a;     c0 = 1; }   /* $4F4B CMP #$F5; BCS: already >= floor */
        else            { v = 0xF5u; c0 = 1; }   /* $4F4F LDA #$F5; SEC: clamp up to the floor */
    } else {                             /* $4F54 positive side: per-circuit SMC ceiling */
        if (mem[0x4F54] != 0xC9u) {      /* SMC opcode (unpatched: Silverstone CMP #imm) */
            platform_smc_unhandled(0x4F54, mem[0x4F54]); return -1;
        }
        if (a < mem[0x4F55]) { v = a; c0 = 0; }  /* $4F56 BCC: below the ceiling, keep it */
        else {                                   /* $4F58 SMC clamp value (unpatched: LDA #imm) */
            if (mem[0x4F58] != 0xA9u) {
                platform_smc_unhandled(0x4F58, mem[0x4F58]); return -1;
            }
            v = mem[0x4F59]; c0 = 0;             /* $4F5A CLC: clamp down to the per-circuit value */
        }
    }

    /* clamp << 6, sign-extended (c0 rotated into bits 15..14 by the 6502's ROR pair). */
    *mathhi_out = (uint8_t)((c0 ? 0xC0u : 0x00u) | (v >> 2));   /* high byte of (v<<6), extended */
    *r_out      = (uint16_t)(0x04D8u + ((unsigned)v << 6) + (c0 ? 0xC000u : 0u));
    return 0;
}

/* ---------------------------------------------------------------------------
   $42D0  draw_gear_indicator — THE GEAR, DOUBLE WIDTH  (twin #109)
   --------------------------------------------------------------------------- */
uint8_t draw_gear_indicator_core(void)
{
    mem[0x62CCu]   = 0x22u;                             /* $42D0 — column $22 */
    shared_temp_77 = 0x22u;                             /* bit 7 clear: the LEFT four pixels */
    mem[0x62CDu]   = 0xD7u;                             /* scan line $D7 = character row 26 */
    uint8_t glyph  = mem[GEAR_CHAR_TBL + gear_index];  /* $42DC/$42DE — the gear's glyph */
    uint8_t block  = vdu_char_wide_core(glyph);        /* left half; returns the block byte */
    shared_temp_77 = 0xFFu;                            /* $42E4 bit 7 set: the RIGHT four pixels */
    /* ⚠ the RIGHT half re-emits the block byte the first emit left ($62C3, live), NOT the glyph. */
    return vdu_char_wide_core(block);                  /* exit A/N/Z come from this second emit */
}

/* ---------------------------------------------------------------------------
   $503F  adc_read — ONE ANALOGUE AXIS, CENTRED  (twin #108)
   ---------------------------------------------------------------------------
   OSBYTE $80 (ADVAL) with the channel in X.  Returns the DISTANCE from centre in A, the
   direction in X (1 positive, 0 negative) and C set when that distance is at least $0A — the
   dead zone both callers test.
   --------------------------------------------------------------------------- */
AdcRead adc_read_core(uint8_t channel)
{
    /* $503F — OSBYTE $80 (ADVAL) with the channel in X; the reading's high byte comes back in Y.
       ADVAL's entry-Y is don't-care and the differential does not compare it for A=$80
       (validate_native.c), so 0 is passed. */
    MosRegs r80 = mos_call(0xFFF4u, 0x80u, channel, 0u);
    uint8_t reading = r80.y;                            /* $5044 */

    /* $5047 — recentre on $80 (adding $80 with no carry-in just flips bit 7, i.e. ^ $80), then
       fold to a magnitude and a direction bit.  The recentre's C/V/Z are dead; only bit 7 (the
       sign) is read.  The dead-zone CMP #$0A at $504F rebuilds N/Z/C in the shim. */
    uint8_t centred = (uint8_t)(reading + 0x80u);
    AdcRead r;
    if (centred & 0x80u) {                             /* $504A BPL fails — negative side */
        r.mag = (uint8_t)(centred ^ 0xFFu);
        r.dir = 0x00u;                                 /* direction: 0 = negative */
    } else {
        r.mag = centred;
        r.dir = 0x01u;                                 /* direction: 1 = positive */
    }
    r.reading = reading;                               /* exit Y — the ADVAL high byte the shim replays */
    return r;
}

/* ---------------------------------------------------------------------------
   $63C5  poll_steering_assist — THE LAMP AND THE SETTING  (twin #107)
   ---------------------------------------------------------------------------
   See the group header, items 4 and 5: A is preserved, X comes back as the flag (with its Z)
   and C as bit 7 of track_direction.
   --------------------------------------------------------------------------- */
void poll_steering_assist_core(void)
{
    /* $63C5-$63D7 — light the four assist lamps from the flag.  A is the caller's (the 6502 saves
       it round this with PHA/PLA); the exit ABI (A preserved, X = flag with its N/Z, C = bit 7 of
       track_direction) is a leaf output the callers read directly, so the shim reconstructs it. */
    uint8_t flag = steering_assist_flag;
    mem[ASSIST_LAMP_2] = flag;                         /* $77E3 */
    mem[ASSIST_LAMP_3] = (uint8_t)(flag >> 1);         /* $77E4 */
    mem[ASSIST_LAMP_1] = (uint8_t)(flag >> 2);         /* $77DC */
    mem[ASSIST_LAMP_0] = (uint8_t)(flag >> 3);         /* $77DB */
}

/* ---------------------------------------------------------------------------
   $1F9B  limit_steer_demand — NEVER MORE LOCK THAN THE DRIVER ASKED FOR  (twin #106)
   --------------------------------------------------------------------------- */
uint8_t limit_steer_demand_core(uint8_t a, int carryIn)
{
    /* $1F9B — when the computed demand overshot the driver's own lock (carry in), pin it back to
       the driver's angle: park |steer_angle_lo| (bit 0 cleared) as the sign byte and return
       steer_angle_hi as the demand.  Otherwise keep what was computed. */
    if (!carryIn) return a;                            /* $1F9B BCC */
    mem[STEER_SIGN] = (uint8_t)(steer_angle_lo & 0xFEu);
    return steer_angle_hi;
}

/* ---------------------------------------------------------------------------
   $15F4  steer_demand_from_slip — CANCEL THE SLIP  (twin #99)
   $160D  steer_demand_store                          (twin #100)
   --------------------------------------------------------------------------- */
static void steer_demand_store_core(uint8_t a)
{
    mem[STEER_DEMAND] = a;                             /* $160D */
    steer_apply_with_assist_core();
}

static void steer_demand_from_slip_core(void)
{
    /* $15F4-$1600 — take |slip_magnitude| (element $0A) as a 16-bit value, its low nibble-masked
       low byte parked in STEER_SIGN.  D = 0 on the steering path (docs/static-map.md §Decimal
       mode), so the old abs16_math is a plain two's-complement negate. */
    uint8_t lo = (uint8_t)(mem[SLIP_MAG_LO_10] & 0xF0u);
    uint8_t hi = mem[SLIP_MAG_HI_10];
    mem[STEER_SIGN] = lo;                              /* $15F4 */
    if (hi & 0x80u) {                                  /* $15FE BPL — negative: |value| */
        /* abs16_math parks the pre-negate high byte in math_hi (= STEER_DEMAND); that write is
           dead — steer_demand_store overwrites STEER_DEMAND before any exit — but replay it so the
           full-mem[] oracle diff holds at every intermediate the harness could sample. */
        mem[STEER_DEMAND] = hi;
        uint16_t neg = (uint16_t)(0u - (uint16_t)(((uint16_t)hi << 8) | lo));
        lo = (uint8_t)neg;
        hi = (uint8_t)(neg >> 8);
        mem[STEER_SIGN] = lo;
    }
    /* $1601-$1606 — quarter the 16-bit magnitude (hi : STEER_SIGN). */
    unsigned mag = (((unsigned)hi << 8) | lo) >> 2;
    uint8_t a = (uint8_t)(mag >> 8);
    mem[STEER_SIGN] = (uint8_t)mag;
    /* $1607 CMP — carry (demand >= driver's own angle) feeds the limiter. */
    a = limit_steer_demand_core(a, a >= steer_angle_hi);
    steer_demand_store_core(a);
}

/* ---------------------------------------------------------------------------
   $1F08  apply_steering_assist — COMPUTER ASSISTED STEERING  (twin #105)
   ---------------------------------------------------------------------------
   The group header's items 2 and 3 are what this computes.  Entered two ways: at $1F08 from
   steer_assist_dispatch, which derives the look-ahead selector from the demand's own direction,
   and at $1F11 from steer_apply_with_assist, which already has it in A.
   --------------------------------------------------------------------------- */
static void assist_from_selector(uint8_t selector);

static void apply_steering_assist_core(void)
{
    /* $1F08-$1F10 — the look-ahead selector: 3 when the demand's sign byte is even, 2 when odd. */
    assist_from_selector((mem[STEER_SIGN] & 0x01u) ? 0x02u : 0x03u);
}

/* `selector` arrives in A on the 6502 (2 or 3); only "== 2" is tested.  Pure C throughout — the
   whole steering path runs with D = 0 (docs/static-map.md §Decimal mode), so every 6502 math
   helper this used to call (abs16_math, mul8_accum, neg16_math_noinit) is just plain binary
   16-bit arithmetic.  The scratch cells $74-$77 are written to the SAME final values the
   transliterated oracle leaves, so make validate's full-mem[] diff still holds. */
static void assist_from_selector(uint8_t selector)
{
    /* $1F11-$1F18 — which track edge to steer at: selector 2 → the far slot $32, else close $0A. */
    uint8_t edgeSlot = (selector == 0x02u) ? 0x32u : 0x0Au;

    /* $1F19-$1F2E — |steering angle| as a 16-bit value (sign in bit 0 of the low byte). */
    uint8_t angLo = steer_angle_lo;
    uint8_t angHi = steer_angle_hi;
    if (angLo & 0x01u) {                               /* $1F1E LSR / $1F22 BCC — negative: flip */
        uint16_t neg = (uint16_t)(0u - (uint16_t)(((uint16_t)angHi << 8) | angLo));
        angLo = (uint8_t)neg;
        angHi = (uint8_t)(neg >> 8);
    }
    mem[STEER_KEYS] = angLo;                            /* $1F1C/$1F29 — |angle| low, read as bias low */

    /* $1F30-$1F39 — +1 in the high byte, less 2 for the far slot's own look-ahead. */
    uint8_t biasHi = (uint8_t)(angHi + 1u);            /* $1F30-$1F31 */
    if (edgeSlot == 0x32u) biasHi = (uint8_t)(biasHi - 2u);   /* $1F33-$1F37 far slot only */
    shared_temp_77 = biasHi;                           /* $1F39 — bias high (overwritten below) */

    /* $1F3B-$1F49 — the track edge less that bias, as a 16-bit magnitude; its sign is kept to
       re-sign the result at the very end (the 6502 parks it with PHP; a local carries it). */
    uint16_t edge = (uint16_t)(((uint16_t)mem[EDGE_X_HI_TBL + edgeSlot] << 8) | mem[EDGE_X_LO_TBL + edgeSlot]);
    uint16_t bias = (uint16_t)(((uint16_t)biasHi << 8) | mem[STEER_KEYS]);
    uint16_t diff = (uint16_t)(edge - bias);
    int diffNegative = (diff & 0x8000u) != 0u;         /* $1F48 PHP — the subtract's sign */
    uint16_t absDiff = diffNegative ? (uint16_t)(0u - diff) : diff;  /* $1F49 abs16 */
    mem[STEER_SIGN] = (uint8_t)absDiff;                /* math_lo — |edge diff| low */
    mem[STEER_KEYS] = (uint8_t)(absDiff >> 8);         /* $1F4C — |edge diff| high (unused past here) */

    /* $1F4E-$1F77 — the GAIN: falls with road_speed, floored at zero, doubled, +$20, then capped
       by the live section's curvature. */
    uint8_t sec    = car_section_cursor;               /* $1F4E LDY $22 */
    uint8_t diff3c = (uint8_t)(0x3Cu - road_speed);    /* $1F50-$1F53 */
    uint8_t gain   = (diff3c & 0x80u) ? 0u : diff3c;   /* $1F55 BPL — floor at zero */
    /* ⚠ `ADC #$20` with NO `CLC` — the doubling's own carry (bit 7 of gain) is part of the sum. */
    uint8_t gainVal = (uint8_t)((uint8_t)(gain << 1) + 0x20u + (gain >> 7));   /* $1F59-$1F5C */
    uint8_t curve  = mem[SECTION_CURVE + sec] & 0x7Fu;
    if (curve >= 0x40u) curve = 0x02u;                 /* $1F63 */
    if (curve >= 0x08u) curve = 0x07u;                 /* $1F69 */
    curve = (uint8_t)(curve << 4);                     /* ×16 */
    if (curve >= gainVal) gainVal = curve;             /* $1F73-$1F77 cap the gain */
    mem[STEER_DEMAND] = gainVal;

    /* $1F79 — (|edge diff| × gain) >> 8, a 16×8 fixed-point step (the old mul8_accum: keep the top
       16 bits of the 24-bit product).  Its low sub-product's high byte is the residue the routine
       leaves in shared_temp_77. */
    shared_temp_77 = (uint8_t)(revs_mulu16((uint8_t)absDiff, gainVal) >> 8);   /* mul8_accum's $0DC2 */
    uint16_t prod = (uint16_t)(revs_mulu16(absDiff, gainVal) >> 8);

    /* $1F7C-$1F88 — re-sign the product by the $1F48 subtract's sign, then clear bit 0. */
    uint16_t signedProd = diffNegative ? (uint16_t)(0u - prod) : prod;   /* $1F7E PLP / $1F7F abs16 */
    uint8_t lo = (uint8_t)signedProd & 0xFEu;          /* $1F84-$1F88 */
    uint8_t hi = (uint8_t)(signedProd >> 8);           /* $1F82 */

    /* $1F8A-$1F93 — and by the steering's own sign: negate unless bit 0 of steer_angle_lo is set. */
    if ((steer_angle_lo & 0x01u) == 0u) {              /* $1F8A LSR / $1F8E BCS */
        uint16_t neg = (uint16_t)(0u - (uint16_t)(((uint16_t)hi << 8) | lo));   /* $1F90 neg16 */
        lo = (uint8_t)neg;
        hi = (uint8_t)(neg >> 8);
    }
    mem[STEER_SIGN]   = lo;
    mem[STEER_DEMAND] = hi;

    apply_steer_demand_core(steer_angle_lo);           /* $1F95 */
}

/* ---------------------------------------------------------------------------
   $1EE9  steer_assist_dispatch      (twin #103)  — the JOYSTICK path's fork
   $1EFA  steer_apply_with_assist    (twin #104)  — the KEYBOARD path's
   --------------------------------------------------------------------------- */
static void steer_assist_dispatch_core(uint8_t demand)
{
    poll_steering_assist_core();                       /* $1EE9 — lamps; A (= demand) survives it */
    if (steering_assist_flag == 0) { clamp_and_store_steer_angle_core(demand); return; }  /* no assist */
    if ((track_direction >> 7) & 1u) { clamp_and_store_steer_angle_core(demand); return; }  /* other way */
    if (demand >= 0x05u) { apply_steering_assist_core(); return; }   /* $1EF3 CMP — enough demand */
    steer_demand_from_slip_core();
}

static void steer_apply_with_assist_core(void)
{
    poll_steering_assist_core();                       /* $1EFA — lamps */
    if (steering_assist_flag != 0 && ((track_direction >> 7) & 1u) == 0u && mem[STEER_KEYS] != 0) {
        assist_from_selector(mem[STEER_KEYS]);         /* $1F03 → $1F11, selector in A */
        return;
    }
    apply_steer_demand_core(steer_angle_lo);           /* $1F95 */
}

/* ---------------------------------------------------------------------------
   $1612  apply_steer_demand            (twin #101)
   $162D  clamp_and_store_steer_angle   (twin #102)
   ---------------------------------------------------------------------------
   ⚠ $162D falls straight into the THROTTLE and GEAR halves of read_driving_controls, so both of
   these end by running that code — the listing's split at $1612 is an artefact of $1F95 and
   $1EEE jumping into the middle of one routine.
   --------------------------------------------------------------------------- */
static void read_pedals_and_gears(void);

static void apply_steer_demand_core(uint8_t signByte)
{
    /* $1612-$161B — 16-bit subtract (steer_angle_hi : signByte) - (STEER_DEMAND : STEER_SIGN),
       low into STEER_SIGN, high into `hi`.  D = 0 on the steering path (docs/static-map.md
       §Decimal mode), so plain 16-bit arithmetic. */
    uint16_t diff = (uint16_t)((((uint16_t)steer_angle_hi << 8) | signByte)
                             - (((uint16_t)mem[STEER_DEMAND] << 8) | mem[STEER_SIGN]));
    mem[STEER_SIGN] = (uint8_t)diff;
    uint8_t hi = (uint8_t)(diff >> 8);

    if (hi >= 0xC8u) {                                 /* $161C CMP/BCS — past the half turn: fold */
        uint16_t neg = (uint16_t)(0u - (uint16_t)(((uint16_t)hi << 8) | mem[STEER_SIGN]));  /* $1620 */
        mem[STEER_DEMAND] = (uint8_t)(neg >> 8);
        mem[STEER_SIGN]   = (uint8_t)((uint8_t)neg ^ 0x01u);   /* $1625-$1629 flip which way */
        hi = mem[STEER_DEMAND];                        /* $162B — clamp reads the magnitude */
    }
    clamp_and_store_steer_angle_core(hi);
}

static void clamp_and_store_steer_angle_core(uint8_t a)
{
    /* $162D CMP #$91 — the lock stop.  Its carry ESCAPES: nothing on the keyboard-and-no-input
       path through read_pedals_and_gears writes C again, so it leaks out through that routine's
       no_key exit and IS the chain's exit C at every caller (steer_demand_from_slip,
       steer_assist_dispatch, steer_apply_with_assist compare it; clamp's own fixture drops it).
       ⚠ This write is LOAD-BEARING but its VALUE is not harness-distinguishable: removing the line
       fails ~260 cases (the cpu-free core prefix leaves C undefined where the transliterated ORACLE
       prefix set it via CMP, so the leaked exit C diverges), yet ANY value here passes — once an
       oracle enters a native shim the whole downstream (this core included) IS native, so the C it
       writes is applied identically to both models and cancels.  The faithful value is the real
       6502's CMP #$91 result, `(a >= 0x91u)`; the harness cannot police it, so keep it correct here. */
    cpu.C = (a >= 0x91u);
    if (a >= 0x91u) a = 0x91u;
    steer_angle_hi = a;
    steer_angle_lo = mem[STEER_SIGN];
    read_pedals_and_gears();                           /* $162D falls into the pedals/gears tail */
}

/* $163B-$16DB — the rest of read_driving_controls, reached only by falling out of the steering.
   Not a 6502 routine of its own; kept as a function so the two entries above can share it. */
static void read_pedals_and_gears(void)
{
    uint8_t mode, amount, delta;

    /* $163B-$1684 — THROTTLE and BRAKE into pedal_mode / pedal_amount.  Once the session is
       over ($000F non-zero) the car drives itself: mode $80, amount revs/4 + 5.  Only the mode
       and amount VALUES landing at have_pedal matter from this section; the pedal carry is the
       one flag it leaves — mirrored so it leaks faithfully to no_key on the joystick gear path
       (nothing there overwrites it, which is why clamp's own CMP #$91 carry can survive to the
       exit on the session-over path).  The clamp fixture drops V and C, but determinism reads the
       live path, so the escaping carry is kept at the real 6502 value by argument. */
    if (session_end_countdown == 0) {                  /* $163B — session still running */
        if (mem[OPTION_FLAGS] & 0x80u) {               /* $163F BIT/BMI — joystick */
            AdcRead a = adc_read_core(0x02u);          /* $1644 — channel 2 */
            int outside = (a.mag >= 0x0Au);            /* $504F dead-zone carry (leaks to no_key) */
            cpu.C = outside;
            if (outside) {                             /* $1649 — outside the dead zone */
                uint8_t mag = a.mag;                    /* $164B — scale the reading up x1.5 */
                mem[STEER_SIGN] = (uint8_t)(mag >> 1);
                /* ⚠ `ADC $74` with no `CLC` — the doubling's own carry ($164F ASL) is in the
                   sum: (mag<<1) + (mag>>1) + bit7(mag).  bit7(mag) is provably 0 here (adc_read
                   folds both sides of centre to a magnitude in 0..$7F), so the ASL never carries;
                   the term is kept as the faithful idiom.  Only this add's OWN carry is read (the
                   in-range test just below) — and the doubled sum CAN exceed $FF (mag=$7F → $13D). */
                unsigned sum = (unsigned)(uint8_t)(mag << 1) + mem[STEER_SIGN] + (mag >> 7);
                if (sum <= 0xFFu) {                     /* $1652 — the sum didn't overflow */
                    uint8_t hi = ((uint8_t)sum >= 0xFAu);   /* $1654 CMP #$FA (carry leaks to no_key) */
                    cpu.C = hi;
                    if (!hi) { mode = a.dir; amount = (uint8_t)sum; goto have_pedal; }  /* in range */
                }
                /* $1658 CPX #0 — the sign is unsigned so this carry is ALWAYS set, and it is the
                   routine's LIVE exit carry (it leaks through the gear tail to no_key). */
                cpu.C = 1;
                if (a.dir == 0x00u) { mode = 0x00u; amount = 0xFAu; goto have_pedal; }  /* $1674 full brake */
                mode = 0x01u; amount = 0xFFu; goto have_pedal;                          /* $1665 full throttle */
            }
        } else {
            if (kbd_test_key_core(0xAEu)) { mode = 0x01u; amount = 0xFFu; goto have_pedal; }  /* $165E throttle */
            if (kbd_test_key_core(0xBEu)) { mode = 0x00u; amount = 0xFAu; goto have_pedal; }  /* $166B brake */
        }
    }
    mode = 0x80u;                                      /* $1678 — nobody is driving */
    amount = (uint8_t)((uint8_t)(engine_revs >> 2) + 0x05u);   /* self-drive: revs/4 + 5 */

have_pedal:
    pedal_mode   = mode;                               /* $1681 */
    pedal_amount = amount;

    /* $1685-$16DB — the GEARS.  One shift per key press, latched in gear_key_latch.
       ⚠ BIT's V (bit 6 of OPTION_FLAGS) is a LIVE EXIT flag: the no_key and latch-held returns
       set no V of their own, so it leaks out of the routine. */
    cpu.V = (uint8_t)((mem[OPTION_FLAGS] >> 6) & 1u);  /* $1685 BIT — V escapes */
    if (mem[OPTION_FLAGS] & 0x80u) {                   /* $1685 BMI — joystick */
        /* $168A — ADVAL 0, the stick buttons; the fire-button bits come back in X, and the MOS's
           exit X/Y escape through the no_key return (see there).  Replay them now the cpu-free
           wrapper no longer leaves them behind. */
        MosRegs b = mos_call(0xFFF4u, 0x80u, 0x00u, 0u);
        cpu.X = b.x; cpu.Y = b.y;
        uint8_t buttons = b.x;
        if ((buttons & 0x01u) == 0) goto no_key;       /* $1691 — no fire button (carry leaks in) */
        cpu.Y = (uint8_t)(pedal_mode - 1);             /* $1696 LDY/DEY — Y escapes to the held return */
        if (pedal_mode != 0x01u) goto shift_up;        /* not braking: shift up */
        /* ⚠ CMP's C (pedal_amount >= $C8) is a LIVE exit flag — it leaks through the shift path
           to the latch-held return, which sets no carry of its own. */
        { uint8_t hard = (pedal_amount >= 0xC8u);      /* $169D CMP #$C8 */
          cpu.C = hard;
          if (hard) goto shift_down; }                 /* $169F — hard brake: shift down */
        goto shift_up;
    }
    { int up = kbd_test_key_core(0x9Fu); cpu.C = up;   /* $16A3 — gear up (carry leaks) */
      if (up) goto shift_up; }
    { int dn = kbd_test_key_core(0xEFu); cpu.C = dn;   /* $16AA — gear down (carry leaks) */
      if (dn) goto shift_down; }

no_key:
    gear_key_latch = 0x00u;                            /* $16B1 — release the latch */
    cpu.A = 0x00u; cpu.N = 0; cpu.Z = 1;               /* exit A/N/Z (compared through the clamp tail);
                                                          X and Y here came from a shared MOS call. */
    return;

shift_up:
    delta = 0xFFu;                                     /* $16B7 — one gear up (adds -1) */
    goto shift;
shift_down:
    delta = 0x01u;                                     /* $16BB — one gear down (adds +1) */
shift:
    gear_change_flag = (uint8_t)(gear_change_flag - 1);  /* $16BD */
    /* $16C1 — latch still held from last frame?  X = the latch and N/Z from it escape on the held
       return, as does A = the delta.  On the continue path draw_gear_indicator overwrites them. */
    cpu.X = gear_key_latch;
    cpu.N = (uint8_t)(gear_key_latch >> 7); cpu.Z = (gear_key_latch == 0);
    cpu.A = delta;
    if (gear_key_latch != 0) return;                   /* still held from last frame */
    gear_key_latch = delta;
    /* $16C5 — apply the gear delta ($FF down-one / $01 up-one) to gear_index.  The clamps below
       are value tests; draw_gear_indicator (the tail) overwrites the flags. */
    { uint8_t g = (uint8_t)(delta + gear_index);       /* carry-in 0 */
      if (g == 0xFFu)      g = 0x00u;                  /* below reverse: stay in reverse */
      else if (g == 0x07u) g = 0x06u;                 /* above top: stay in top */
      gear_index = g; }                                /* $16D6 */
    draw_gear_indicator();                             /* cluster-2 driver, still 6502-ABI */
}

/* ---------------------------------------------------------------------------
   $1579  read_driving_controls — STEERING, THROTTLE, BRAKE, GEARS  (twin #98)
   ---------------------------------------------------------------------------
   The body's 3rd call.  Steering first, through whichever of the two input paths $05F5 selects,
   and the group header's item 1 is the joystick one's whole non-linearity.
   --------------------------------------------------------------------------- */
static void read_driving_controls_core(void)
{
    mem[STEER_KEYS]   = 0x00u;                          /* $1579 */
    mem[STEER_SIGN]   = 0x00u;
    gear_change_flag  = 0x00u;

    int amplifyPressed = kbd_test_key_core(0x9Du);     /* $1581-$1584 — the steering-amplify key */

    if (mem[OPTION_FLAGS] & 0x80u) {                   /* $1589 BIT/BMI — joystick */
        /* $158C-$15B2 — THE JOYSTICK.  The reading is SQUARED (header item 1), then quartered
           into a 16-bit demand unless the amplify key is down, and the axis sign carries into
           STEER_SIGN bit 0. */
        AdcRead a = adc_read_core(0x01u);              /* magnitude and its direction bit */
        uint8_t dir      = a.dir;
        uint8_t demandHi = a.mag;
        mem[STEER_DEMAND] = demandHi;
        /* ⚠⚠ $1593 IS A PER-CIRCUIT SMC EXTENT, and it is the squaring itself: Silverstone's
           `JSR mul8` is what an expansion circuit replaces with its own hook.  The twin has to
           dispatch on the operands rather than bake the call — a randomised pre-state took the
           trap 255 times in 256 and returned, which is exactly the 2448-of-5000 the fixture
           reported before this arm existed. */
        if (mem[0x1593] != 0x20u) { platform_smc_unhandled(0x1593, mem[0x1593]); return; }
        {
            uint16_t target = (uint16_t)(mem[0x1594] | ((unsigned)mem[0x1595] << 8));
            if (target == 0x0C00u) {
                /* $0C00 mul8 — the joystick reading squared, a plain 8x8 product (D = 0 on
                   the steering path).  demandHi = product high, math_lo = product low. */
                unsigned product = revs_mulu16(demandHi, math_hi);
                math_lo  = (uint8_t)product;
                demandHi = (uint8_t)(product >> 8);
            }
            else if (target >= 0x5300u && target <= 0x5A25u) {
                /* the circuit's own hook squares the reading; it works through the 6502 ABI, so
                   hand it A and take the result back — a documented track-hook cpu boundary. */
                cpu.A = demandHi;
                revs_track_hook(target);
                demandHi = cpu.A;
            }
            else { platform_smc_unhandled(0x1593, target); return; }
        }
        if (!amplifyPressed) {                         /* $15A9 PLP — amplify NOT down: quarter */
            unsigned demand = ((unsigned)demandHi << 8) | mem[STEER_SIGN];
            demand >>= 2;
            demandHi = (uint8_t)(demand >> 8);
            mem[STEER_SIGN] = (uint8_t)demand;
        }
        mem[STEER_DEMAND] = demandHi;
        mem[STEER_SIGN] = (uint8_t)((mem[STEER_SIGN] & 0xFEu) | dir);   /* sign into bit 0 */
        steer_assist_dispatch_core(demandHi);
        return;
    }

    /* $15B3-$15F3 — THE KEYBOARD.  Two keys into STEER_KEYS (1, 2 or 3), a fixed demand of 3,
       and the amplify key replaces it with 1 or 0 plus a sign byte of $80. */
    if (kbd_test_key_core(0xA9u)) mem[STEER_KEYS] = 0x02u;
    if (kbd_test_key_core(0xA8u)) mem[STEER_KEYS] = (uint8_t)(mem[STEER_KEYS] + 1);   /* INC */
    mem[STEER_DEMAND] = 0x03u;
    if (!amplifyPressed) {                             /* $15C7 PLP — amplify NOT down */
        /* $15CE-$15DA — demand is 1 when the wheel is barely off centre (steer_angle_hi <= 2),
           else 0; sign byte $80. */
        mem[STEER_DEMAND] = (0x02u >= steer_angle_hi) ? 0x01u : 0x00u;
        mem[STEER_SIGN]   = 0x80u;
    }

    {
        uint8_t keys = mem[STEER_KEYS];                /* $15DF */
        if (keys == 0x00u) { steer_demand_from_slip_core(); return; }
        if (keys == 0x03u) { read_pedals_and_gears(); return; }   /* both keys: no steering */
        if (((keys ^ steer_angle_lo) & 0x01u) == 0x00u) {         /* $15E8 — already this way */
            steer_apply_with_assist_core(); return;
        }
    }
    /* $15EE — flip (math_hi:math_lo); high byte becomes the stored demand (D = 0 here). */
    {
        uint16_t v = (uint16_t)(0u - (uint16_t)(((uint16_t)math_hi << 8) | math_lo));
        math_lo = (uint8_t)v;
        steer_demand_store_core((uint8_t)(v >> 8));
    }
}

/* The 6502-ABI shims. */
void read_driving_controls(void)        { read_driving_controls_core(); }
void steer_demand_from_slip(void)       { steer_demand_from_slip_core(); }
void steer_demand_store(void)           { steer_demand_store_core(cpu.A); }
void apply_steer_demand(void)           { apply_steer_demand_core(cpu.A); }
void clamp_and_store_steer_angle(void)  { clamp_and_store_steer_angle_core(cpu.A); }
void steer_assist_dispatch(void)        { steer_assist_dispatch_core(cpu.A); }
void steer_apply_with_assist(void)      { steer_apply_with_assist_core(); }
void apply_steering_assist(void)        { apply_steering_assist_core(); }
/* limit is a leaf: on the carry path it returns steer_angle_hi with that value's N/Z, C and V
   unchanged from entry; on the no-carry path A and every flag are the caller's. */
/* poll is a leaf: A is preserved, X comes back as the flag (with its N/Z), C as bit 7 of
   track_direction; V is untouched. */
/* $50FC mode5_addr / $50FA mode5_addr_for_cell — plot_ptr is the side effect; the scan line
   within the row comes back in A and Y (N clear, the value is < 8) and the row in X.  The
   fixture drops V and C for this cluster (the second add's flags are dead at every caller). */

/* $509D vdu_char_emit / $508C vdu_char_wide — OSWORD (inside the core) clobbers X/Y, but the
   6502 preserves the caller's X/Y across these routines ($509D/$50EF PUSH/PULL), so each shim
   saves and restores them.  Exit A/N/Z come from the block byte the emit leaves live at $62C3.
   (The i=5 fixture ignores $01FE/$01FF, where the transliterated emit oracle's PUSH/PULL of
   X/Y leaves residue this shim does not write.) */

/* $5092 vdu_char_def — the OSWRCH-path branch is 6502-ABI reconstruction, so it lives here. */

/* $42D0 draw_gear_indicator — exit A/N/Z from the second emit, X forced to $FF ($42E4, then
   emit-preserved), Y preserved from entry (both emits preserve it). */

/* $503F adc_read — magnitude in A, sign in X, the dead-zone carry rebuilt from CMP #$0A
   ($504F).  Y is left as the OSBYTE reading the core's MOS call returned; V is dropped. */
/* ===========================================================================
   THE LATE MISC TREES  (twins #116-#125, user 2026-08-21)
   ---------------------------------------------------------------------------
   Everything still transliterated in the call trees of scale_wing_settings,
   compute_segment_scale, place_player_in_section, process_car_contact and
   tick_wheel_spin.  Every arithmetic LEAF they reach (mul8, abs8, abs16_math,
   sound_queue_default) is already a twin, so these are the drivers and the small
   helpers around them.  D = 0 on every one of these paths (docs/static-map.md
   §Decimal mode: none of the eight SED sites is here), so the ADC/SBC byte
   arithmetic is plain binary and the twins spell it as such.  Where a value
   crosses into a shared tail (car_gap_tail — now native twin #137 — FUN_1c0b, FUN_11be) or a
   native leaf with a live-flag input (abs8, abs16_math), the seam reconstructs
   exactly the cpu inputs that leaf reads — nothing more.
   =========================================================================== */

#define CAR_STATE_2        0x0178u   /* per-car; the other of place_player's two outputs */
#define CAR_FLAGS_0        0x0114u   /* per-car flag byte; spin_car_out marks it */
#define CAR_SEG_OFFSET     0x0880u   /* per-car offset within the current segment */
#define TRACK_SCALE        0x5A14u   /* TRACK FILE: per-track scale factor */
#define SEGMENT_SCALE      0x5FB0u   /* per-segment scaled output */
#define WING_GRIP_BASE_TBL 0x0BA0u   /* per-wing base downforce, 0 = rear, 1 = front */
#define WHEEL_SPIN_XOR_A   0x52F6u   /* the two XOR masks the wheel-spin flicker rolls in */
#define WHEEL_SPIN_XOR_B   0x52FBu

/* ---------------------------------------------------------------------------
   $0B77  scale_wing_settings  (twin #116)
   ---------------------------------------------------------------------------
   Turns the two pit-menu wing settings into the downforce/drag coefficients the
   driving model reads.  Per wing: grip = ((base * (setting*4)) >> 8) + $5A.  Drag
   folds both wings: ((3*rear + front) / 2) + $3C — accumulated as the 6502 does it,
   where the ASL feeds its own carry (bit 7 of rear) into the first ADD, a faithful
   8-bit quirk with no 16-bit equivalent.
   --------------------------------------------------------------------------- */
void scale_wing_settings(void)
{
    /* X walks 1 then 0: front wing (index 1) then rear wing (index 0). */
    for (int i = 1; i >= 0; i--) {
        uint8_t  setting4 = (uint8_t)(mem[MEM_wing_setting_front + i] << 2);   /* setting * 4 */
        unsigned prod     = revs_mulu16(mem[WING_GRIP_BASE_TBL + i], setting4);
        mem[MEM_wing_grip_coeff + i] = (uint8_t)((prod >> 8) + 0x5A);
    }

    uint8_t  rear  = wing_setting_rear;
    uint8_t  front = wing_setting_front;
    unsigned c     = (unsigned)(rear >> 7);                          /* ASL A: carry = bit 7 */
    unsigned acc   = (unsigned)(uint8_t)(rear << 1) + rear + c;      /* ADC rear  */
    acc = ((acc & 0xFF) + front + (acc >> 8)) & 0xFF;               /* ADC front (carry dropped) */
    c   = acc & 1;                                                   /* LSR A: carry = bit 0 */
    acc = ((acc >> 1) + 0x3C + c) & 0xFF;                           /* ADC #$3C */
    wing_drag_coeff = (uint8_t)acc;
}

/* ---------------------------------------------------------------------------
   $44C6  compute_segment_scale  (twin #117)
   ---------------------------------------------------------------------------
   Scales every segment's raw datum by the track scale.  X selects the track's scale
   byte.  Per segment (top down): drop the datum's low two bits; when the datum's bit 1
   is clear, take the rounded x.8 product with the scale, otherwise pass the shifted
   datum through; the datum's bit 0 is rotated back into bit 7 as the rounding bit.
   ⚠ SMC $44D5-$44D7: the segment-data base address is patched per circuit by
   ModifyGameCode; the guard reproduces the oracle's opcode check and trap.
   --------------------------------------------------------------------------- */
void compute_segment_scale(void)
{
    uint8_t scale = mem[TRACK_SCALE + cpu.X];
    mem[MEM_track_scale_saved] = scale;         /* held for reuse; read back at $6396 */
    math_hi = scale;

    if (mem[0x44D5] != 0xB9) { platform_smc_unhandled(0x44D5, mem[0x44D5]); return; }
    uint16_t base = (uint16_t)(mem[0x44D6] | (mem[0x44D7] << 8));

    for (int y = segment_count_x8 >> 3; y >= 0; y--) {
        uint8_t  datum   = mem[(uint16_t)(base + y)];
        unsigned round   = datum & 1;                       /* bit 0: PHP'd rounding bit */
        uint8_t  shifted = (uint8_t)(datum >> 2);           /* two LSRs */
        uint8_t  a = ((datum >> 1) & 1)                     /* bit 1 set: pass through halved */
                     ? shifted
                     : (uint8_t)(revs_mulu16(shifted, scale) >> 8);   /* else rounded x.8 */
        mem[SEGMENT_SCALE + y] = (uint8_t)((round << 7) | (a & 0x7F));  /* ASL; PLP; ROR: bit7<-bit0 */
    }
}

/* ---------------------------------------------------------------------------
   $4687  section_angle_curve  (twin #118)  — returns A
   ---------------------------------------------------------------------------
   A piecewise remap of a section angle into a steering-feel curve: shallow angles are
   amplified 6x, mid angles 4x with an offset, steep angles flatten to a linear tail.
   --------------------------------------------------------------------------- */
static uint8_t section_angle_curve_core(uint8_t a)
{
    if (a >= 0x2E) return (uint8_t)(a + 0xBE);       /* steep: shallow linear tail */
    if (a >= 0x1A) return (uint8_t)(a * 4 + 0x34);   /* mid: 4x + offset */
    return (uint8_t)(a * 6);                          /* shallow: 6x */
}
void section_angle_curve(void) { cpu.A = section_angle_curve_core(cpu.A); }

/* ---------------------------------------------------------------------------
   $4676  scale_angle_in_section  (twin #119)  — returns A
   ---------------------------------------------------------------------------
   Curves the angle, then folds Y and edge_nearest_lo through it as two x.8 multiplies:
   A = ((edge_nearest_lo * ((Y * curve(A)) >> 8)) >> 8).
   --------------------------------------------------------------------------- */
static uint8_t scale_angle_in_section_core(uint8_t a, uint8_t y)
{
    uint8_t curve = section_angle_curve_core(a);
    uint8_t p1    = (uint8_t)(revs_mulu16(y, curve) >> 8);
    return (uint8_t)(revs_mulu16(edge_nearest_lo, p1) >> 8);
}
void scale_angle_in_section(void) { cpu.A = scale_angle_in_section_core(cpu.A, cpu.Y); }

/* ---------------------------------------------------------------------------
   $1FA8  record_section_jump  (twin #120)
   ---------------------------------------------------------------------------
   Rolls one bit into a per-frame history: a 1 iff the caller's carry is set AND this
   car's offset within its segment has reached 3.  Carry in, and the bit rotated OUT
   of the history byte comes back out in carry.
   --------------------------------------------------------------------------- */
static int record_section_jump_core(int carry_in, uint8_t x)
{
    int     bit_in = carry_in ? (mem[CAR_SEG_OFFSET + x] >= 3) : 0;   /* BCC / CMP #3 */
    uint8_t old    = section_jump_history;
    section_jump_history = (uint8_t)((old >> 1) | (bit_in << 7));      /* ROR */
    return old & 1;                                                    /* carry out */
}
void record_section_jump(void) { cpu.C = record_section_jump_core(cpu.C, cpu.X); }

/* ---------------------------------------------------------------------------
   $4626  place_player_in_section  (twin #121)
   ---------------------------------------------------------------------------
   Derives the two per-car placement bytes (car_state_1/car_state_2) from the nearest
   road-edge bearing relative to the current section's yaw.  It folds that relative
   angle through scale_angle_in_section twice — once with weight $BA into car_state_1,
   once with weight $88 into car_state_2 — flipping sign by track direction and by a
   quadrant flag ($0043), and feeds record_section_jump the change since last frame.

   The abs8 and the SMC hook are native leaves with live-flag INPUTS, so the seam sets
   exactly the cpu registers each reads (A and its sign N; abs8's threaded carry).
   ⚠ SMC $462B-$462D: Silverstone calls abs8; an expansion circuit runs its own hook.
   --------------------------------------------------------------------------- */
void place_player_in_section(void)
{
    /* Relative angle of the nearest edge bearing to the section's yaw.  D=0, so a plain 8-bit
       subtract whose bit 7 is the sign abs8 negates on; a circuit hook instead READS the value
       and its sign N via cpu, so that seam re-establishes them. */
    uint8_t rel = (uint8_t)(nearest_edge_bearing_hi - section_yaw);   /* SEC; SBC */

    if (mem[0x462B] != 0x20) { platform_smc_unhandled(0x462B, mem[0x462B]); return; }
    uint8_t mag;
    {
        uint16_t hook = (uint16_t)(mem[0x462C] | (mem[0x462D] << 8));
        if (hook == 0x3450)                          mag = (rel & 0x80u) ? negate8(rel).hi : rel;
        else if (hook >= 0x5300 && hook <= 0x5A25) {
            cpu.A = rel; cpu.N = (rel >> 7) & 1u;    /* the circuit hook reads A and its sign */
            revs_track_hook(hook);
            mag = cpu.A;
        }
        else { platform_smc_unhandled(0x462B, hook); return; }
    }

    /* Quadrant flag: bit 7 of $0043 records whether |rel| reached a quarter turn ($40). */
    int quad_c = (mag >= 0x40);                       /* CMP #$40 */
    mem[0x0043] = (uint8_t)((quad_c << 7) | (mem[0x0043] >> 1));   /* ROR $0043; N = quad_c */
    if (quad_c) mag = (uint8_t)((mag ^ 0x7F) + 1);   /* BMI arm: reflect past the quarter turn */

    /* $4639 PHA: the routine parks this magnitude on the 6502 stack across the two
       sub-calls, then pulls it back for the second fold.  The value is genuinely written
       to page 1 and lives there below SP until the next frame overwrites it, so the twin
       uses the real stack — determinism is byte-exact over $0100-$01FF. */
    cpu.A = mag; PHA();                               /* push V1 (folded magnitude) */

    /* First fold: weight $BA, then flip if the nearest edge is far enough along ($28). */
    uint8_t a = scale_angle_in_section_core(mag, 0xBA);
    if (nearest_edge_cursor >= 0x28) a ^= 0xFF;       /* CPX #$28; BCC skip; EOR #$FF */

    /* |a| with sign taken from track_direction bit 7.  On the keep arm abs8 leaves C untouched,
       so the SBC below sees the CPX #$28 carry; on the negate arm it sees the negate's own C. */
    uint8_t x = player_car;
    int     neg = (track_direction & 0x80) != 0;      /* BIT track_direction — the value's sign */
    uint8_t placed;
    int     placedC;
    if (neg) { AddFlags f = negate8(a); placed = f.hi; placedC = f.carry; }
    else     { placed = a; placedC = (nearest_edge_cursor >= 0x28); }

    cpu.A = placed; PHA();                            /* $464E push V2 (placed) — stack residue */

    /* Change since last frame -> record_section_jump's carry (SBC borrow = !C). */
    unsigned diff = (unsigned)placed - mem[CAR_STATE_2 + x] - (placedC ? 0u : 1u);   /* SBC */
    uint8_t d = (uint8_t)diff;
    if (diff & 0x100) d ^= 0xFF;                       /* BCC (borrow): EOR #$FF -> |diff| */
    record_section_jump_core(d >= 0x16, x);           /* CMP #$16 */

    PLA();                                            /* $465B pull V2 */
    mem[CAR_STATE_2 + x] = cpu.A;                      /* car_state_2[X] = placed */

    PLA();                                            /* $465F pull V1 back into A */
    uint8_t folded = cpu.A;
    /* Second fold: weight $88, sign from the quadrant flag. */
    uint8_t b = scale_angle_in_section_core((uint8_t)((folded ^ 0xFF) + 0x41), 0x88);  /* EOR;ADC #$41 */
    b = (uint8_t)(b << 2);                             /* ASL; ASL */
    if (!(mem[0x0043] & 0x80)) b ^= 0xFF;             /* BIT $0043; BPL: EOR #$FF */
    mem[CAR_STATE_1 + x] = b;
}

/* ---------------------------------------------------------------------------
   $52A4  tick_wheel_spin  (twin #122)
   ---------------------------------------------------------------------------
   One PAL field of the wheel-spin flicker.  Advances a field counter and a rate
   accumulator (road_speed + $30); on the accumulator's carry, and only while
   wheel_spin_rate is non-zero, it XORs the wheel graphics in the dashboard overlay.
   Every $6Exx/$6Fxx/$70xx store is a frame-buffer write.
   --------------------------------------------------------------------------- */
void tick_wheel_spin(void)
{
    field_countdown++;

    unsigned s1  = (unsigned)road_speed + 0x30;                      /* CLC; ADC #$30 */
    unsigned acc = (s1 & 0xFF) + wheel_spin_accum + (s1 >> 8);       /* ADC accum, carry threaded */
    wheel_spin_accum = (uint8_t)acc;
    if (!(acc & 0x100)) return;              /* no carry this field */
    if (wheel_spin_rate == 0) return;        /* spin disabled */

    for (int x = 4; x >= 0; x--) {
        mem[0x6FC0 + x] ^= mem[WHEEL_SPIN_XOR_A + x];
        mem[0x70F8 + x] ^= mem[WHEEL_SPIN_XOR_B + x];
        if (x < 3) {                                     /* CPX #3; BCS skips this pair */
            mem[0x6E85 + x] ^= 0xF0;
            uint8_t r = (uint8_t)(mem[0x6FBD + x] ^= 0xF0);
            if (r != 0) continue;                        /* BNE: skip the lower pair */
        }
        mem[0x6E8A + x] ^= 0xC0;
        mem[0x6FB2 + x] ^= 0x30;
    }
}

/* ---------------------------------------------------------------------------
   $11AB  spin_car_out  (twin #123)
   ---------------------------------------------------------------------------
   Flags car X as spun out.  For a real car slot (X < $14) it folds the low seven bits
   of car_state_2 into car_flags_0 with the spin marker $45, stamps $91 into the page-1
   status array, then runs the shared crash tail (FUN_11be).  Scenery slots do nothing.
   --------------------------------------------------------------------------- */
void spin_car_out(void)
{
    uint8_t x = cpu.X;
    if (x >= 0x14) { FUN_11cd(); return; }        /* not a car slot: shared no-op tail */
    mem[CAR_FLAGS_0 + x] = (uint8_t)((mem[CAR_STATE_2 + x] & 0x7F) | 0x45);
    mem[0x0100 + x]      = 0x91;
    FUN_11be();                                   /* shared crash tail, indexed by X */
}

/* ---------------------------------------------------------------------------
   $1BB9  process_car_contact  (twin #124)
   ---------------------------------------------------------------------------
   Resolves the frame's car-vs-car (or car-vs-scenery) contact.  From the closing
   distance it builds an impact magnitude (floored at 5, doubled); a hard hit in a race
   spins the other car out; the slower of the two cars is credited some speed; and the
   shared crash tail (FUN_1c0b) gets a signed heading kick plus a queued crash sound.
   The PHP/PLP saving the heading-difference sign across the speed logic becomes one
   local carried into abs16_math's N.
   --------------------------------------------------------------------------- */
void process_car_contact(void)
{
    if (contact_pending == 0) { FUN_1c1b(); return; }        /* no contact this frame */
    contact_pending = 0x00;
    shared_temp_76 = (uint8_t)((shared_temp_76 >> 1) | 0x80);   /* SEC; ROR $76 */

    /* Impact from closing distance: $25 - distance, floored at 5, doubled. */
    unsigned d      = 0x25u - contact_distance;              /* SEC; SBC */
    uint8_t  impact = (d & 0x100) ? 0x05 : (uint8_t)d;       /* BCC: floor at 5 */
    uint8_t  impact2 = (uint8_t)(impact << 1);

    uint8_t x = contact_slot;
    uint8_t y = player_car;
    cpu.X = x;   /* $1BD0 LDX contact_slot — X survives to FUN_1c0b's sound save ($0B46) */

    /* Hard hit during the race: spin the other car out. */
    if (impact2 >= 0x28 && (session_is_race & 0x80)) { cpu.X = x; spin_car_out(); }

    /* Heading difference between the two objects, x4; its sign steers abs16_math below. */
    uint8_t  hd4     = (uint8_t)(((unsigned)mem[OBJECT_BEARING_HI + x] - car_heading_hi) << 2);
    int      hd_sign = (hd4 & 0x80) != 0;                    /* PHP: N of the <<2 result */

    /* Speed credit: the slower car gets a nudge; scenery slots (X >= $14) are skipped. */
    uint8_t speedY = mem[CAR_SPEED_SCL + y];
    uint8_t M      = speedY;                                 /* the mul8 multiplicand */
    if (x < 0x14) {
        uint8_t speedX = mem[CAR_SPEED_SCL + x];
        if (speedY >= speedX) {                              /* BCS: credit from speed[Y] */
            M = (uint8_t)(speedY + 0x0C);                    /* ADC #$0B with carry(1) */
            mem[CAR_SPEED_SCL + x] = M;
        } else if (speedX != 0) {                            /* BNE: use speed[X] as is */
            M = speedX;
        } else {                                             /* speed[X] == 0: seed it to $0B */
            M = 0x0B;                                         /* 0 + $0B + carry(0) */
            mem[CAR_SPEED_SCL + x] = M;
        }
    }

    /* impact * speed, clamped to $10, negated by the heading sign -> heading kick. */
    unsigned prod = revs_mulu16(M, impact2);
    math_lo = (uint8_t)prod;                                /* the low byte abs16_math negates */
    uint8_t a = (uint8_t)(prod >> 8);
    if (a >= 0x10) a = 0x10;                                 /* CMP #$10; clamp */
    cpu.A = a;
    cpu.N = hd_sign;                                         /* PLP: the saved sign */
    abs16_math();                                           /* negate (A:math_lo) per N; A -> tail */
    FUN_1c0b();                                             /* heading_step_hi = A; slip flags; sound */
}

#define CAR_DISTANCE_LO   0x08D0u   /* car_distance_lo: distance-round-the-lap, low byte */
#define CAR_DISTANCE_HI   0x08E8u   /* car_distance_hi: ...high byte */

/* ---------------------------------------------------------------------------
   $27A4  car_gap  (twin #125)
   ---------------------------------------------------------------------------
   Byte 0 of the 24-bit separation between cars Y and X.  Only this subtract's BORROW
   survives into the shared three-byte tail (car_gap_tail) — the low difference itself is
   discarded there — so the twin's whole job is to hand that tail the right carry.
   --------------------------------------------------------------------------- */
unsigned car_gap_lo_core(uint8_t a, uint8_t b) { return (unsigned)a - b; }

/* ---------------------------------------------------------------------------
   $27AB  car_gap_tail — SIGNED GAP AROUND THE RING  (twin #137)
   ---------------------------------------------------------------------------
   The shared three-byte tail every car_gap ($27A4) and its two other callers ($10E1, $28FD)
   fall into.  Forms the signed 16-bit separation  D = car_distance[Y] - car_distance[X]  (the
   borrow is chained from the entry carry — car_gap's byte-0 subtract, or a plain SEC), takes
   |D|, then reduces it around a circular track of circumference lap_length:

       * D's high byte is zero      -> the gap IS |D|            (near pair, "direct" branch)
       * otherwise                  -> the shorter way is  lap_length - |D|   ("wrapped" branch)

   Products written to memory:
       math_lo       = the reduced gap's low byte
       math_hi       = |D|'s high byte
       hypot_min_hi  = a per-call sign bit rotated into this shift register: a 1 on the direct
                       branch, a 0 on the wrapped branch (bit 6 of hypot_min_hi later selects
                       negate-vs-accumulate for the caller's sort).

   The original saves D's sign (PHP), and on the wrapped branch FLIPS it (PLA/EOR #$80/PHP) so
   the final abs8 re-signs the complement the opposite way round.  Both are modelled here as
   plain booleans; abs8/abs16_math are inlined as the binary negates they are (D = 0 always —
   race logic, not a BCD site).

   Exit ABI — the three registers/flags callers actually read ($10E1 and $28FD test C then use
   A; check_car_pair tests C, then N, then compares A):
       C = 1 on the three "far" exits (SEC at $27EB), 0 on the two in-range exits (CLC $27E8)
       N = the sign of the returned A          A = the reduced gap byte the caller compares
   V and Z are dead at every caller.  The PHP/PLP stack byte ($01FF) is the oracle's only
   residue the twin does not reproduce; the fixture ignores it.
   --------------------------------------------------------------------------- */

GapTail car_gap_tail_core(uint8_t x, uint8_t y, unsigned carryIn)
{
    /* D = dist[Y] - dist[X], 16-bit, borrow chained from the entry carry. */
    int lo = (int)mem[CAR_DISTANCE_LO + y] - mem[CAR_DISTANCE_LO + x] - (carryIn ? 0 : 1);
    uint8_t maglo = (uint8_t)lo;
    unsigned c1   = (lo >= 0);
    int hi = (int)mem[CAR_DISTANCE_HI + y] - mem[CAR_DISTANCE_HI + x] - (c1 ? 0 : 1);
    uint8_t  dhi  = (uint8_t)hi;                    /* raw high byte of D (its sign, saved by PHP) */
    unsigned n1   = (dhi >> 7) & 1u;               /* D negative? */
    unsigned z1   = (dhi == 0);                    /* D's high byte zero? -> near pair */

    uint8_t maghi = dhi;
    if (n1) {                                      /* |D| via abs16_math: negate (dhi:maglo) */
        uint16_t mag = (uint16_t)(0u - (uint16_t)(((uint16_t)dhi << 8) | maglo));
        maglo = (uint8_t)mag;
        maghi = (uint8_t)(mag >> 8);
    }
    math_lo = maglo;
    math_hi = maghi;

    GapTail r;
    if (z1) {
        /* DIRECT: rotate a 1 into the sign register (C = 1 from the SEC at $27C1). */
        hypot_min_hi = (uint8_t)((hypot_min_hi >> 1) | 0x80u);
        if (maglo >= 0x80u) {                       /* $27DE BCS -> far exit (PLP/SEC) */
            r.a = maglo; r.n = (uint8_t)n1; r.c = 1;/* n1 == 0 here (high byte is zero) */
            return r;
        }
        /* abs8 with N = n1 = 0: no negate; CLC. */
        r.a = maglo; r.n = (uint8_t)((maglo >> 7) & 1u); r.c = 0;
        return r;
    }

    /* WRAPPED: complement = lap_length - |D|, with the saved sign flipped for the re-sign. */
    unsigned flipped = n1 ^ 1u;                     /* P2's N (the PLA/EOR #$80 flip) */
    int clo = (int)lap_length_lo - maglo;
    uint8_t  comp_lo = (uint8_t)clo;
    unsigned c3 = (clo >= 0);
    math_lo = comp_lo;
    int chi = (int)lap_length_hi - maghi - (c3 ? 0 : 1);
    if ((uint8_t)chi != 0) {                        /* $27D5 BNE -> far exit (PLP/SEC) */
        r.a = (uint8_t)chi; r.n = (uint8_t)flipped; r.c = 1;
        return r;
    }
    /* complement fits in a byte: rotate a 0 into the sign register (CLC at $27D7). */
    hypot_min_hi = (uint8_t)(hypot_min_hi >> 1);
    if (comp_lo >= 0x80u) {                         /* $27DE BCS -> far exit (PLP/SEC) */
        r.a = comp_lo; r.n = (uint8_t)flipped; r.c = 1;
        return r;
    }
    /* abs8 with N = flipped: negate the complement iff D was positive; CLC. */
    uint8_t a = flipped ? (uint8_t)(0u - comp_lo) : comp_lo;
    math_lo = a;
    r.a = a; r.n = (uint8_t)((a >> 7) & 1u); r.c = 0;
    return r;
}


/* ---------------------------------------------------------------------------
   $0BCC  section_coord_add_delta  (twin #138)   — was FUN_0bcc
   ---------------------------------------------------------------------------
   Integrates one signed direction step into a section's 3-component world coordinate:

       for each component i = 0..2:
           dest_coord[i] = src_coord[i] + delta[i]        (16-bit, wraps)

   The source section is the byte cursor in Y, the destination the byte cursor in X (both index
   section_coord_lo/hi, 40 sections x 3 bytes, plus the two scratch slots at $FA/$FD).  The step
   delta[i] is the signed 16-bit value build_section_step_delta built for this section: its low bytes are the
   shared math window (math_lo, math_hi, shared_temp_76 at $74/$75/$76) and its high bytes are
   point_delta_hi[0..2] ($83/$84/$85).

   The 6502 does this as three ADC lo / ADC hi byte-pair chains (CLC before each low add); on the
   D=0 road/placement path that is exactly a binary uint16_t add, so it is written as one here.
   No flag escapes: FUN_12f7 does LDX straight after, and place_car_world_coords' AI branch
   likewise — A/N/V/Z/C are all dead at both call sites. */
void section_coord_add_delta_core(uint8_t dst, uint8_t src,
                                         const uint8_t dlo[3], const uint8_t dhi[3])
{
    for (int i = 0; i < 3; i++) {
        uint16_t s = (uint16_t)(mem[SECTION_LO_TBL + src + i]
                                | (mem[SECTION_HI_TBL + src + i] << 8));
        uint16_t d = (uint16_t)(dlo[i] | (dhi[i] << 8));
        uint16_t r = (uint16_t)(s + d);
        mem[SECTION_LO_TBL + dst + i] = (uint8_t)r;
        mem[SECTION_HI_TBL + dst + i] = (uint8_t)(r >> 8);
    }
}


/* ---------------------------------------------------------------------------
   $124D  copy_section_height_to_side1  (twin #139)   — was FUN_124d
   ---------------------------------------------------------------------------
   The road builder keeps two coordinate lists per section: side 0 at the section byte cursor X,
   side 1 (the opposite road edge) at cursor + SECTION_SIDE1.  FUN_12f7 / FUN_122d build side 1's
   ground-plane pair (components 0 and 2) as side 0 plus the across-track normal, but the two
   edges sit at the same HEIGHT, so component 1 is just copied across here (both bytes). */
void copy_section_height_to_side1(void)
{
    uint8_t x = cpu.X;
    mem[SECTION_LO_TBL + SECTION_SIDE1 + x + 1] = mem[SECTION_LO_TBL + x + 1];
    cpu.A = mem[SECTION_HI_TBL + x + 1];                 /* $1253 LDA — dead at both callers */
    mem[SECTION_HI_TBL + SECTION_SIDE1 + x + 1] = cpu.A;
}

/* ---------------------------------------------------------------------------
   $13DA  advance_dir_on_segment_flag  (twin #143)   — was FUN_13da
   ---------------------------------------------------------------------------
   If bit 0 of the current segment's flags is set, advance segment_dir_index;
   otherwise fall through to the FUN_13fa per-circuit hook (RTS on Silverstone).
   Reached via the per-circuit SMC dispatch at $13C9/$1426.  Preserves X; the
   callers discard the exit flags. */
void advance_dir_on_segment_flag(void)
{
    if (cur_segment_flags & 0x01)
        step_segment_dir_index();
    else
        FUN_13fa();
}

/* ---------------------------------------------------------------------------
   $13E0  step_segment_dir_index  (twin #142)   — was FUN_13e0
   ---------------------------------------------------------------------------
   Advances segment_dir_index one track position along the direction tables,
   wrapping at track_dir_count, in whichever sense track_direction's bit 7 gives:
   forward is idx+1 rolling to 0 at the count, backward is idx-1 rolling 0 to
   count-1.  Tail-calls FUN_13fa (a Silverstone RTS no-op the expansion circuits
   patch).  Preserves X; the callers discard the exit flags. */
void step_segment_dir_index(void)
{
    uint8_t count = track_dir_count;
    uint8_t idx   = segment_dir_index;
    if (track_direction & 0x80) {           /* backwards */
        if (idx == 0) idx = count;          /* wrap 0 -> count, then... */
        idx--;                              /* ...count-1 (or plain idx-1) */
    } else {                                /* forwards */
        idx++;
        if (idx == count) idx = 0;          /* wrap count -> 0 */
    }
    segment_dir_index = idx;
    FUN_13fa();                             /* per-circuit hook; RTS on Silverstone */
}

/* ---------------------------------------------------------------------------
   $1442  build_section_step_delta  (twin #141)   — was FUN_1442
   ---------------------------------------------------------------------------
   Builds the signed 16-bit 3-component step delta that section_coord_add_delta then
   integrates into a section's coordinate.  Sign-extends the track's three direction bytes
   track_dir_0/1/2[Y] (ground plane c0/c2 scaled to |.|=$78, c1 the small gradient)
   into 16-bit values, storing the low bytes in math_lo/math_hi/shared_temp_76 and
   the high bytes in point_delta_hi[0..2].  When track_direction is set (running the
   track backwards) each 16-bit component is two's-complement negated.  D=0 on this
   path; the callers ($1335, $2A11) discard the exit registers/flags. */
void build_section_step_delta(void)
{
    uint8_t y = cpu.Y;
    int16_t d[3];
    d[0] = (int16_t)(int8_t)mem[TRACK_DIR_0 + y];
    d[1] = (int16_t)(int8_t)mem[TRACK_DIR_1 + y];
    d[2] = (int16_t)(int8_t)mem[TRACK_DIR_2 + y];
    if (track_direction != 0) {                 /* backwards: negate every component */
        d[0] = (int16_t)-d[0];
        d[1] = (int16_t)-d[1];
        d[2] = (int16_t)-d[2];
    }
    math_lo               = (uint8_t)d[0];  mem[POINT_DELTA_HI + 0] = (uint8_t)(d[0] >> 8);
    math_hi               = (uint8_t)d[1];  mem[POINT_DELTA_HI + 1] = (uint8_t)(d[1] >> 8);
    shared_temp_76        = (uint8_t)d[2];  mem[POINT_DELTA_HI + 2] = (uint8_t)(d[2] >> 8);
}

/* ---------------------------------------------------------------------------
   $125A  derive_car_section_cursor  (twin #140)   — was FUN_125a
   ---------------------------------------------------------------------------
   Maps the walk-origin section cursor to the CAR's section cursor: subtract $60
   and, if that went negative, wrap by the $78-wide section ring (40 sections x
   3 bytes).  D=0 on this path, so the subtract/add is plain binary.  No escaping
   flags — the caller ($13D3) discards N/Z/C. */
uint8_t derive_car_section_cursor_core(uint8_t cursor)
{
    uint8_t t = (uint8_t)(cursor - 0x60);
    if (t & 0x80) t = (uint8_t)(t + 0x78);   /* rolled below 0 -> back into 0..$77 */
    return t;
}

/* ---------------------------------------------------------------------------
   $12F7  build_road_section  (twin #147)   — was FUN_12f7
   ---------------------------------------------------------------------------
   THE ROAD BUILDER.  Builds one live road section per call; build_road_section's callers loop it
   over shared_counter_42 to (re)generate the whole track walk, which is the bulk of the off-track
   freeze.  Each call:

     1. advance the walk cursor by three bytes (section_cursor += 3, wrap at $78), remembering the
        previous cursor in section_cursor_prev;
     2. move the player's car one segment.  Forward (track_direction bit7 clear) also arms the
        finish-line lap credit when the car reaches the lap marker; backward latches the segment
        being left into retreat_segment first.  Either move can report a crossed section boundary
        (carry), which cross_section_boundary commits — and a FORWARD boundary skips the build
        block entirely (it was already built by the crossing);
     3. build this section's flag byte from cur_segment_flags gated by player_seg_offset and the
        segment's field-7 marker byte;
     4. integrate the direction step into the section's own coordinates (section_coord_add_delta),
        share the height across to side 1, then add the scaled across-track normal ($5700/$5800
        indexed by segment_dir_index, x4) to build side 1's components 0 and 2;
     5. run the per-circuit direction-index hook (SMC $13C9), store section_dir_index[cursor], and
        derive the car's cursor + the section curve.

   ⚠ $5700 / $5800 are ModifyGameCode's addresses but, at RACE time, their second tenant is the
   across-track normal pair (docs/rename.md); referenced here by that race-time meaning.
   No flags/registers escape (LIVE_NONE).  The oracle's PHP/PLP byte at $01FF is ignored. */
#define TRACK_NORMAL_X  0x5700u   /* $5700,dir — across-track normal, X component (race-time tenant) */
#define TRACK_NORMAL_Y  0x5800u   /* $5800,dir — across-track normal, Y component (race-time tenant) */

void build_road_section(void)
{
    /* --- 1. advance the walk cursor by one section (three bytes), wrap at $78 --- */
    section_cursor_prev = section_cursor;
    uint8_t nextCursor;
    if (mem[0x12FB] == 0x18 && mem[0x12FC] == 0x69) {       /* unpatched: CLC; ADC #3 */
        nextCursor = (uint8_t)(section_cursor + 3);
    } else if (mem[0x12FB] == 0x20) {                       /* per-circuit hook */
        uint16_t t = (uint16_t)(mem[0x12FC] | (mem[0x12FD] << 8));
        if (t >= 0x5300 && t <= 0x5A25) { revs_track_hook(t); nextCursor = cpu.A; }
        else { platform_smc_unhandled(0x12FB, t); return; }
    } else { platform_smc_unhandled(0x12FB, mem[0x12FB]); return; }
    if (nextCursor >= 0x78) nextCursor = 0;
    section_cursor = nextCursor;

    /* --- 2. step the car one segment; commit a crossed boundary --- */
    int forwardBoundary = 0;
    cpu.X = 0x17;                                           /* the player's car slot */
    if (!(track_direction & 0x80)) {
        /* forward: arm the lap credit if this section is the finish marker */
        uint8_t marker = (uint8_t)(segment_count_x8 >> 1);
        if (mem[0x1310] == 0x29)      marker &= 0xF8;       /* unpatched: AND #$F8 */
        else if (mem[0x1310] == 0xA9) marker = mem[0x1311];/* patched: LDA #imm */
        else { platform_smc_unhandled(0x1310, mem[0x1310]); return; }
        if (marker == player_car_segment) lap_credit_armed = 1;

        cpu.X = 0x17;
        track_pos_advance();
        if (cpu.C) { cross_section_boundary(); forwardBoundary = 1; }
    } else {
        /* backward: remember the segment being left, then retreat */
        retreat_segment = player_car_segment;
        cpu.X = 0x17;
        track_pos_retreat();
        if (cpu.C) cross_section_boundary();
    }

    if (!forwardBoundary) {
        /* --- 3. build this section's flag byte --- */
        cpu.Y = segment_dir_index;
        build_section_step_delta();

        uint8_t x  = section_cursor;
        uint8_t cf = cur_segment_flags;
        int bit0   = cf & 0x01;

        uint8_t base = cf;
        if (!bit0) {                                        /* bit0 clear: maybe clear bits 1,2 */
            uint8_t off = player_seg_offset;
            if (off == 0 || off >= 0x0A) base &= 0xF9;      /* off outside 1..9 */
        }
        shared_temp_77 = base;

        uint8_t fieldLo7 = mem[TRACK_SEGMENT_LO + 7 + player_car_segment];  /* field-7 low byte */
        int clr34 = 0, clr5 = 0;                            /* which extra bits to clear */
        if (bit0) {
            uint8_t y2 = (uint8_t)(fieldLo7 >> 1);
            if (y2 != player_seg_offset) { clr34 = 1; clr5 = 1; }
        } else {
            uint8_t y2 = (uint8_t)(fieldLo7 - player_seg_offset);
            if (y2 == 0x07)                        { /* keep */ }
            else if (y2 == 0x0E || y2 == 0x15)     { clr5 = 1; }
            else                                   { clr34 = 1; clr5 = 1; }
        }
        uint8_t r = base;
        if (clr34) r &= 0xE7;                               /* clear bits 3,4 */
        if (clr5)  r &= 0xDF;                               /* clear bit 5 */
        r &= cf;
        mem[SECTION_FLAGS + x] = r;

        /* --- 4. integrate the step and build side-1's ground-plane pair --- */
        cpu.X = section_cursor;
        cpu.Y = section_cursor_prev;
        section_coord_add_delta();                          /* section N's point from N-1 + step */
        cpu.X = section_cursor;
        copy_section_height_to_side1();                     /* share the height across */

        uint8_t dir = segment_dir_index;
        x = section_cursor;

        /* side-1 comp 0 = side-0 comp 0 + across-track normal X, scaled x4 (sign-extended) */
        uint16_t nx = (uint16_t)((int16_t)(int8_t)mem[TRACK_NORMAL_X + dir] << 2);
        mem[POINT_DELTA_HI + 0] = (uint8_t)(nx >> 8);       /* faithful scratch residue */
        uint16_t c0 = (uint16_t)(mem[SECTION_LO_TBL + x] | (mem[SECTION_HI_TBL + x] << 8));
        uint16_t s0 = (uint16_t)(c0 + nx);
        mem[SECTION_LO_TBL + SECTION_SIDE1 + x] = (uint8_t)s0;
        mem[SECTION_HI_TBL + SECTION_SIDE1 + x] = (uint8_t)(s0 >> 8);

        /* side-1 comp 2 = side-0 comp 2 + across-track normal Y, scaled x4 */
        uint16_t ny = (uint16_t)((int16_t)(int8_t)mem[TRACK_NORMAL_Y + dir] << 2);
        mem[POINT_DELTA_HI + 2] = (uint8_t)(ny >> 8);       /* faithful scratch residue */
        uint16_t c2 = (uint16_t)(mem[SECTION_LO_TBL + 2 + x] | (mem[SECTION_HI_TBL + 2 + x] << 8));
        uint16_t s2 = (uint16_t)(c2 + ny);
        mem[SECTION_LO_TBL + SECTION_SIDE1 + 2 + x] = (uint8_t)s2;
        mem[SECTION_HI_TBL + SECTION_SIDE1 + 2 + x] = (uint8_t)(s2 >> 8);

        /* --- 5a. per-circuit direction-index hook (SMC $13C9) --- */
        if (mem[0x13C9] == 0x20) {
            uint16_t t = (uint16_t)(mem[0x13CA] | (mem[0x13CB] << 8));
            if (t == 0x13DA) { advance_dir_on_segment_flag(); }
            else if (t >= 0x5300 && t <= 0x5A25) { revs_track_hook(t); }
            else { platform_smc_unhandled(0x13C9, t); return; }
        } else { platform_smc_unhandled(0x13C9, mem[0x13C9]); return; }
    }

    /* --- 5b. store the direction index and derive the car cursor + section curve --- */
    mem[SECTION_DIR_IX + section_cursor] = segment_dir_index;
    derive_car_section_cursor();
    step_section_curve();
}

/* ---------------------------------------------------------------------------
   $1267  cross_section_boundary  (twin #146)   — was FUN_1267
   ---------------------------------------------------------------------------
   Commits the road walk crossing into a new section (called by FUN_12f7 after track_pos_advance /
   track_pos_retreat reports a section boundary was crossed).  It marks the near edge points to be
   scrolled next frame, loads the new section's world geometry, and clears the section's flag byte.

     - near_edge_scroll_pending = 6  (and near_edge_last = 6 when a far-edge rebuild is in flight,
       so nothing is treated as reusable);
     - forward (track_direction bit7 clear): load the segment the car is now in
       (Y = player_car_segment); near_edge_shift comes from that segment's field-0 high byte;
     - backward (bit7 set): load the segment being left (Y = retreat_segment), step the direction
       index (SMC $1289), and use a fixed shift of 2;
     - cur_segment_flags <- the new segment's field-0 low byte; section_flags[cursor] <- 0.

   No flags or registers escape (LIVE_NONE; both callers reload Y at L_1333). */
void cross_section_boundary(void)
{
    uint8_t x = section_cursor;             /* the walk's section byte cursor */
    uint8_t shiftSrc;

    near_edge_scroll_pending = 6;
    if (far_edge_rebuild != 0)
        near_edge_last = 6;

    cpu.X = x;                              /* load_section_from_segment reads cpu.X / cpu.Y */
    if (!(track_direction & 0x80)) {
        /* forward: the segment the car has just entered */
        cpu.Y = player_car_segment;
        load_section_from_segment();
        shiftSrc = mem[TRACK_SEGMENT_HI + player_car_segment];   /* field-0 high byte */
    } else {
        /* backward: the segment being left */
        cpu.Y = retreat_segment;
        load_section_from_segment();
        /* SMC $1289: unpatched steps segment_dir_index; a circuit may hook it */
        if (mem[0x1289] == 0x20) {
            uint16_t t = (uint16_t)(mem[0x128A] | (mem[0x128B] << 8));
            if (t == 0x13E0) { step_segment_dir_index(); }
            else if (t >= 0x5300 && t <= 0x5A25) { revs_track_hook(t); }
            else { platform_smc_unhandled(0x1289, t); return; }
        } else {
            platform_smc_unhandled(0x1289, mem[0x1289]); return;
        }
        shiftSrc = 0x02;
    }

    near_edge_shift = (uint8_t)(shiftSrc & 0x07);
    cur_segment_flags = mem[TRACK_SEGMENT_LO + player_car_segment];   /* field-0 low byte */
    mem[SECTION_FLAGS + x] = 0x00;          /* clear this section's feature flags */
}

/* ---------------------------------------------------------------------------
   $122D  load_section_from_segment  (twin #145)   — was FUN_122d
   ---------------------------------------------------------------------------
   Builds one live section's world geometry from its track-file segment record.  A track segment
   is an 8-byte record whose fields 1..6 are three-plus-three 16-bit coordinates (low byte in the
   $5900 bank, high byte in the $5300 bank); this lays them into the two parallel road-edge lists.

     X = the destination section byte cursor (side 0);  Y = the segment byte index.

   load_section_triple copies fields 1,2,3 into side 0's triple (components 0,1,2).  Then fields 4
   and 6 become side 1's components 0 and 2 — the OPPOSITE road edge — and copy_section_height_to_side1
   shares side 0's height (component 1) across.  segment_dir_index is field 5's low byte.

   ⚠ SMC $1248 (per-circuit, ModifyGameCode): unpatched Silverstone is `LDA $5905,Y`; an expansion
   circuit rewrites it to a hook JSR whose result A becomes segment_dir_index.  Reproduced exactly.
   No flags or registers escape (both callers overwrite A next, and X/Y are left as the entry
   values the caller still needs) — bare void shim. */
void load_section_from_segment(void)
{
    uint8_t x = cpu.X;          /* dest section byte cursor (side 0) */
    uint8_t y = cpu.Y;          /* track-file segment byte index */

    load_section_triple_core(x, y);                         /* fields 1..3 -> side-0 triple */

    /* fields 4 and 6 -> side-1 components 0 and 2 (the opposite road edge) */
    mem[SECTION_LO_TBL + SECTION_SIDE1 + x]     = mem[TRACK_SEGMENT_LO + 4 + y];   /* $0978 <- $5904 */
    mem[SECTION_LO_TBL + SECTION_SIDE1 + x + 2] = mem[TRACK_SEGMENT_LO + 6 + y];   /* $097A <- $5906 */
    mem[SECTION_HI_TBL + SECTION_SIDE1 + x]     = mem[TRACK_SEGMENT_HI + 4 + y];   /* $0A78 <- $5304 */
    mem[SECTION_HI_TBL + SECTION_SIDE1 + x + 2] = mem[TRACK_SEGMENT_HI + 6 + y];   /* $0A7A <- $5306 */

    /* segment_dir_index from field-5 low byte (SMC $1248) */
    if (mem[0x1248] == 0xB9) {                              /* unpatched: LDA $5905,Y */
        segment_dir_index = mem[TRACK_SEGMENT_LO + 5 + y];
    } else if (mem[0x1248] == 0x20) {                       /* per-circuit hook JSR */
        uint16_t t = (uint16_t)(mem[0x1249] | (mem[0x124A] << 8));
        if (t >= 0x5300 && t <= 0x5A25) { revs_track_hook(t); segment_dir_index = cpu.A; }
        else { platform_smc_unhandled(0x1248, t); return; }
    } else {
        platform_smc_unhandled(0x1248, mem[0x1248]); return;
    }

    copy_section_height_to_side1();                         /* side-0 height -> side-1 (reads cpu.X) */
}

/* ---------------------------------------------------------------------------
   $150E  step_section_curve  (twin #144)   — was FUN_150e
   ---------------------------------------------------------------------------
   The section-CURVE stepper, a leaf of the FUN_12f7 road-builder cluster.  It writes one byte,
   section_curve[section_cursor], describing the curvature the road should show at the section the
   walk is filling, and it carries a small marker state machine between calls in four zero-page
   cells (near_curve_scale/_marker_countdown/_ramp_width/_curve_signed).

   Indexing is off the player's own position: Y = player_car_segment (the segment number x 8, an
   index into the per-segment track_segment_lo/hi records) and X = Y >> 3 (the plain segment number,
   an index into segment_scale).

   Two modes:

   (a) near_marker_countdown == 0 — SCAN for the next curve marker.  Depending on cur_segment_flags
       bit0 and near_curve_signed bit7, optionally step Y on by one segment record (+8) and X by one,
       then inspect that record:
         * track_segment_lo field-0 bit0 clear, or its countdown (track_segment_hi+5) zero
           -> no marker here: emit segment_scale[X] | $40 and leave the countdown at 0;
         * otherwise LATCH the marker — countdown = track_segment_hi+5, near_curve_signed =
           track_segment_hi+7 (bit7 = direction), near_ramp_width = that & $7F, near_curve_scale =
           segment_scale[X] — and emit near_curve_scale.
       The pre-advance shortcut (player_seg_offset < track_segment_hi+5 with cur_segment_flags bit0
       clear) also emits segment_scale[X] | $40 without touching state.

   (b) near_marker_countdown != 0 — RAMP toward the latched marker.  Decrement the countdown to v
       and emit a curve that ramps with distance:
         * v >= near_ramp_width               -> near_curve_scale        (full curve, near marker)
         * v + (v>>3) >= near_ramp_width       -> 0                        (transition band)
         * otherwise                           -> near_curve_scale ^ $80   (sign-flipped, past band)
       The band boundary is the 6502's SBC-then-ADC-for-carry idiom at $1567: after the borrow, the
       add of (v>>3) sets carry exactly when v + (v>>3) >= near_ramp_width, choosing store-0 vs
       store-flipped.

   D=0 on this path (render/geometry); every arithmetic step is plain binary.  No flags or registers
   escape — the sole caller (the $13CC region inside FUN_12f7) returns immediately after — so the
   shim is a bare void wrapper. */
void step_section_curve(void)
{
    uint8_t y = player_car_segment;          /* segment number x 8: track_segment_* index */
    uint8_t x = (uint8_t)(y >> 3);           /* plain segment number: segment_scale index */
    uint8_t out;

    if (near_marker_countdown != 0) {
        /* --- (b) ramp toward the latched marker --- */
        uint8_t v = (uint8_t)(near_marker_countdown - 1);
        near_marker_countdown = v;
        math_lo = (uint8_t)(v >> 3);             /* $155E scratch — reproduced so 0x74 stays faithful */
        if (v >= near_ramp_width) {
            out = near_curve_scale;                              /* full curve near the marker */
        } else {
            uint8_t diff = (uint8_t)(v - near_ramp_width);      /* the borrowed byte */
            unsigned sum = (unsigned)diff + (v >> 3);           /* ADC (v>>3), carry-in 0 */
            out = (sum & 0x100) ? 0x00                          /* transition band */
                                : (uint8_t)(near_curve_scale ^ 0x80);  /* past the band */
        }
    } else {
        /* --- (a) scan the segment list for the next curve marker --- */
        int advance;
        if (cur_segment_flags & 0x01) {
            advance = !(near_curve_signed & 0x80);              /* bit7 set -> inspect here */
        } else if (player_seg_offset >= mem[TRACK_SEGMENT_HI + 5 + y]) {
            advance = 1;                                        /* offset past this record */
        } else {
            advance = -1;                                      /* shortcut: no marker yet */
        }

        if (advance == -1) {
            out = (uint8_t)(mem[SEGMENT_SCALE + x] | 0x40);
        } else {
            if (advance) { y = (uint8_t)(y + 8); x = (uint8_t)(x + 1); }  /* next segment record */

            if ((mem[TRACK_SEGMENT_LO + y] & 0x01) == 0) {
                out = (uint8_t)(mem[SEGMENT_SCALE + x] | 0x40); /* field-0 bit0 clear: no marker */
            } else {
                uint8_t cd = mem[TRACK_SEGMENT_HI + 5 + y];
                near_marker_countdown = cd;
                if (cd == 0) {
                    out = (uint8_t)(mem[SEGMENT_SCALE + x] | 0x40);   /* zero-length marker */
                } else {
                    uint8_t raw = mem[TRACK_SEGMENT_HI + 7 + y];
                    near_curve_signed = raw;                    /* bit7 = curve direction */
                    near_ramp_width   = (uint8_t)(raw & 0x7F);
                    near_curve_scale  = mem[SEGMENT_SCALE + x];
                    out = near_curve_scale;
                }
            }
        }
    }

    mem[SECTION_CURVE + section_cursor] = out;
}

/* ---------------------------------------------------------------------------
   $2937  place_car_world_coords  (twin #126)
   ---------------------------------------------------------------------------
   Projects one object's within-section offsets — car_state_1 ("along" the section) and
   car_state_2 ("across" it) — through the section's direction basis into the 3-value world
   coordinate at object_coord_lo:object_coord_hi.

   The section byte cursor Y indexes the section ORIGIN (section_coord_lo/hi); the byte it
   points at, section_dir_index, indexes the five direction bytes (track_dir_0/1/2 and the two
   at $5700/$5800).  First loop, three world axes:

       coord[axis] = section_origin[axis] + signextend( (along * dir[axis]) >> 8 )

   as a signed 16-bit add.  Second loop, two axes (0 and 2), folds the "across" offset in at 4x.
   Then coordinate 1 is nudged by $90.  The two products were the 6502's mul8 (an 8x8 shift-add);
   here they are revs_mulu16 (one MULU.W), the sign handled in C.

   ⭐ THE PRODUCT'S SIGN.  mul8 gives an unsigned 16-bit product; the 6502 keeps only its HIGH
   byte and, on a negative direction byte, negates that byte alone (8-bit two's complement) and
   sign-extends into shared_temp_76.  So the value added is signextend8( |dir|*along >> 8 ) with
   the sign of dir — which is exactly `(dir<0 ? -mph : mph)` as a 16-bit signed, mph being that
   high byte (0..254, so no 16-bit overflow either here or after the <<2 in the second loop).

   ⚠ SMC at $298D-$298E: the middle axis's origin HIGH byte is ANDed with a per-circuit mask
   (Silverstone's operand lives at $298E; the carry from the low-byte add is preserved across the
   mask, the 6502's PHP/PLP).  All circuits keep the AND opcode, so a different opcode is the
   unhandled case, faithfully reproduced.

   The tail (from $29F4) is the object queue: FUN_2a5d dispatches on object_dist_hi, and for a
   near car ahead of car_behind the AI branch runs build_section_step_delta / FUN_2b0e / section_coord_add_delta / FUN_2a5f.
   Those are the real generated routines, called with the registers the transliteration set, so
   they cancel in the differential — the twin's job is the two loops and the coordinate adds.
   --------------------------------------------------------------------------- */
#define OBJECT_COORD_LO   0x09FDu   /* 3-byte per-object world coordinate, low bytes  */
#define OBJECT_COORD_HI   0x0AFDu   /*                                    high bytes */
#define SECTION_COORD_LO  0x0900u   /* section origin, low  (section_coord_lo) */
#define SECTION_COORD_HI  0x0A00u   /* section origin, high (section_coord_hi) */
#define TRACK_DIR_3       0x5700u   /* ⚠ shares ModifyGameCode's address; read as DATA here */
#define TRACK_DIR_4       0x5800u
#define SECTION_DIR_INDEX 0x0700u
#define SMC_MASK_OPCODE   0x298Du   /* per-circuit; unpatched = $29 (AND zp) */
#define SMC_MASK_OPERAND  0x298Eu

/* signextend8( |dir| * factor >> 8 ) with the sign of dir — the signed contribution of one axis,
   optionally <<2 (the second loop's ASL/ROL pair).  ⚠ Also reproduces the mul8 residue the
   object-queue tail reads back: math_lo ($74) = the product's low byte (mul8 never shifts it),
   math_hi ($75) = the preserved multiplicand, shared_temp_76 ($76) = the term's high byte (which
   the coord-high ADC also consumes).  Written on every call so the residue is faithful at the
   SMC-trap exit inside loop 1 too. */
static int16_t place_car_axis_term(uint8_t dir, uint8_t factor, int shl2)
{
    uint8_t mag   = (dir & 0x80u) ? (uint8_t)(0u - dir) : dir;   /* EOR #$FF; ADC #1 on the neg arm */
    unsigned prod = revs_mulu16(mag, factor);                    /* mul8 */
    int16_t term  = (dir & 0x80u) ? (int16_t)(-(int)(prod >> 8)) : (int16_t)(prod >> 8);
    if (shl2) term = (int16_t)(term << 2);

    mem[0x0074] = (uint8_t)prod;                    /* math_lo: product low byte */
    mem[0x0075] = factor;                           /* math_hi: preserved multiplicand */
    mem[0x0076] = (uint8_t)((uint16_t)term >> 8);   /* shared_temp_76: the (shifted) sign extension */
    return term;
}

void place_car_world_coords(void)
{
    uint8_t x0  = cpu.X;                              /* object/car slot */
    uint8_t y0  = cpu.Y;                              /* section byte cursor */
    uint8_t soi = mem[SECTION_DIR_INDEX + y0];        /* indexes the direction tables */
    uint8_t along  = mem[CAR_STATE_1 + x0];
    uint8_t across = mem[CAR_STATE_2 + x0];

    /* ⚠ The oracle parks its inputs in the zero-page arithmetic window ($0C soi, $84 along,
       $85 across, $86..$88 dir bytes) and the object-queue tail reads them back through those
       cells.  Reproduce those writes exactly so the shared tail routines see identical memory;
       only the mul8 product residue ($74/$75/$76) and the PHP stack byte then differ. */
    mem[0x000C] = soi;
    mem[0x0084] = along;             /* the oracle's STA $84 from car_state_1[X] */
    mem[0x0085] = across;            /* STA $85 from car_state_2[X] */

    uint8_t dir1[3];
    dir1[0] = mem[TRACK_DIR_0 + soi];
    dir1[1] = mem[TRACK_DIR_1 + soi];
    dir1[2] = mem[TRACK_DIR_2 + soi];
    mem[0x0086] = dir1[0];
    mem[0x0087] = dir1[1];
    mem[0x0088] = dir1[2];

    /* First loop: origin + (along * dir) >> 8, per world axis.  ⚠ The section index is the 6502's
       8-bit Y (LDY y0 then INY per axis), so it WRAPS at 256 — y0+axis must be masked to a byte. */
    for (int axis = 0; axis < 3; axis++) {
        uint8_t sy = (uint8_t)(y0 + axis);
        int16_t sp = place_car_axis_term(dir1[axis], along, 0);

        unsigned lo = (unsigned)(uint8_t)sp + mem[SECTION_COORD_LO + sy];          /* CLC; ADC */
        unsigned carry = lo >> 8;
        mem[OBJECT_COORD_LO + axis] = (uint8_t)lo;

        uint8_t hiOrigin = mem[SECTION_COORD_HI + sy];
        if (axis == 1) {                              /* the SMC site */
            if (mem[SMC_MASK_OPCODE] == 0x29) hiOrigin &= mem[SMC_MASK_OPERAND];
            else { cpu.X = 1; platform_smc_unhandled(SMC_MASK_OPCODE, mem[SMC_MASK_OPCODE]); return; }
        }
        mem[OBJECT_COORD_HI + axis] =
            (uint8_t)(hiOrigin + (uint8_t)((uint16_t)sp >> 8) + carry);           /* ADC shared_temp_76 */
    }

    /* Second loop: fold the "across" offset in at 4x, axes 0 and 2 only. */
    uint8_t dir2[3];
    dir2[0] = mem[TRACK_DIR_3 + soi];                 /* $86 reloaded */
    dir2[2] = mem[TRACK_DIR_4 + soi];                 /* $88 reloaded */
    mem[0x0086] = dir2[0];
    mem[0x0088] = dir2[2];
    for (int axis = 0; axis < 4; axis += 2) {
        int16_t sp = place_car_axis_term(dir2[axis], across, 1);                  /* ASL/ROL x2 */

        unsigned lo = (unsigned)(uint8_t)sp + mem[OBJECT_COORD_LO + axis];        /* CLC; ADC */
        unsigned carry = lo >> 8;
        mem[OBJECT_COORD_LO + axis] = (uint8_t)lo;
        mem[OBJECT_COORD_HI + axis] =
            (uint8_t)(mem[OBJECT_COORD_HI + axis] + (uint8_t)((uint16_t)sp >> 8) + carry);
    }

    /* Nudge coordinate 1 by $90.  The ADC's carry-out is left in C and FUN_2a5d reads it. */
    uint8_t nudge_carry;
    {
        unsigned t = (unsigned)mem[OBJECT_COORD_LO + 1] + 0x90u;                  /* CLC; ADC #$90 */
        mem[OBJECT_COORD_LO + 1] = (uint8_t)t;
        nudge_carry = (t & 0x100u) ? 1 : 0;
        if (nudge_carry) mem[OBJECT_COORD_HI + 1]++;                              /* INC on carry */
    }

    /* ---- The object-queue tail ($29F4).  Real generated routines; registers as set below.
       Y entering the tail is section_dir_index (the loop-2 LDY $0C), which the tail's projections
       read; the oracle parks it in $0C, this twin carries it in `soi`.  Entering FUN_2a5d the 6502
       has X=4 (loop-2 leftover CPX #4), A=4 with N=Z=0 (LDA #4), and C from the $90 nudge. ---- */
    cpu.Y = soi;
    cpu.X = 0x04;
    cpu.C = nudge_carry; cpu.N = 0; cpu.Z = 0;
    cpu.A = 0x04; FUN_2a5d();
    cpu.X = saved_slot_index;
    if (object_dist_hi >= 0x03) {
        if (object_dist_hi >= 0x05 &&                            /* CMP #5; BCC L_2a4d */
            !(mem[CAR_FLAGS_SHAPE + cpu.X] & 0x80))              /* BMI L_2a4d */
            mem[CAR_FLAGS_SHAPE + cpu.X]++;                      /* INC */
        cpu.X = saved_slot_index;                               /* L_2a4d */
        return;
    }
    if (mem[0x001D] != car_behind) { cpu.X = saved_slot_index; return; }   /* $1D: queued in rename.md */
    cpu.A = mem[CAR_FLAGS_SHAPE + cpu.X];                        /* $2A07 LDA $018C,X */
    if (!(cpu.A & 0x80))                                         /* BPL: not yet flagged */
        mem[CAR_FLAGS_SHAPE + cpu.X]--;                          /* DEC */

    /* build_section_step_delta ($1442) reads only Y (the segment index); the C/N/Z the tail
       leaves are set here for the calls that follow it down this branch. */
    cpu.C = 1;
    cpu.Y = soi;                                                 /* $2A0F LDY $0C */
    cpu.N = (soi & 0x80) != 0; cpu.Z = (soi == 0);
    build_section_step_delta();
    FUN_2b0e();
    cpu.Y = 0xFD; cpu.X = 0xFA; section_coord_add_delta();
    FUN_2b0e();
    cpu.X = 0xF4; section_coord_add_delta();
    FUN_2b0e();
    cpu.X = 0xFD; section_coord_add_delta();
    shared_counter_42 = 0x14; cpu.A = 0x02; FUN_2a5d();
    shared_counter_42 = 0x15; cpu.A = 0x01; cpu.X = 0xF4; FUN_2a5f();
    shared_counter_42 = 0x16; cpu.A = 0x00; cpu.X = 0xFA; FUN_2a5f();
    cpu.X = saved_slot_index;                                    /* L_2a4d */
}

/* ---------------------------------------------------------------------------
   $5A25  tally_bcd_column  (twin #127)
   ---------------------------------------------------------------------------
   Front-end grid/standings BCD tally for one column X.  Zeroes the per-column 16-bit BCD
   accumulator standings_bcd_lo:standings_bcd_hi, derives a repeat count, then BCD-accumulates
   standings_increment into the pair that many times before folding the pair into the 24-bit
   BCD car-lap total via FUN_6698.

   The repeat count comes from standings_mode ($5F38):
     * mode 1                     -> count = 1               (accumulate once)
     * the column's car is the player, OR the doubled (mode-1) is zero
                                  -> count = $5F38 * (that A) — the one product, was mul8
     * car past the cutoff $5F39  -> count = $5F38
     * otherwise                  -> count = (mode-1) * 2
   The count is a 16-bit down-counter (low byte, then high byte, exactly as the 6502 walks it).

   ⚠ One of the eight SED sites (docs/static-map.md §Decimal mode): the accumulate stays BCD, so
   the two adds go through adc_value with D set — that is sanctioned here and nowhere on the render
   path.  Only the pre-SED product at $5A52 was a shim (mul8); it is now revs_mulu16.
   --------------------------------------------------------------------------- */
#define STANDINGS_BCD_LO    0x3878u
#define STANDINGS_BCD_HI    0x39F8u
#define STANDINGS_INCREMENT 0x3DF7u
#define STANDINGS_MODE      0x5F38u   /* $5F38, semantics queued in rename.md */
#define STANDINGS_CUTOFF    0x5F39u   /* $5F39, queued in rename.md */

void tally_bcd_column(void)
{
    uint8_t x = cpu.X;
    uint8_t s = mem[STANDINGS_MODE];
    uint8_t y = (x == 0x06) ? mem[CAR_ORDER_TBL] : mem[CAR_ORDER_TBL + x];   /* the column's car */

    mem[STANDINGS_BCD_LO + x] = 0x00;
    mem[STANDINGS_BCD_HI + x] = 0x00;

    uint8_t ctr_lo, ctr_hi = 0x00;

    if (s == 0x01) {
        ctr_lo = s;                                       /* L_5a5a: count = $5F38 (=1) */
    } else {
        uint8_t am1 = (uint8_t)(s - 1);                   /* SEC; SBC #1 */
        int use_product;
        uint8_t aEntry = 0;

        if (y == player_car)            { use_product = 1; aEntry = am1; }   /* BEQ L_5a4d */
        else if (y >= mem[STANDINGS_CUTOFF]) { use_product = 0; ctr_lo = s; }/* BCS L_5a5a */
        else {
            uint8_t sh = (uint8_t)(am1 << 1);             /* ASL A */
            if (sh != 0) { use_product = 0; ctr_lo = sh; }/* BNE L_5a5f */
            else         { use_product = 1; aEntry = 0; } /* -> L_5a4d */
        }

        if (use_product) {                                /* L_5a4d: $5F38 * aEntry, was mul8 */
            unsigned p = revs_mulu16(s, aEntry);
            ctr_lo = (uint8_t)p;
            ctr_hi = (uint8_t)(p >> 8);
        }
    }

    /* SED; the 16-bit BCD accumulate loop. */
    cpu.D = 1;
    do {
        Adc lo = adc_value(mem[STANDINGS_BCD_LO + x], mem[STANDINGS_INCREMENT + x], 0);   /* CLC; ADC */
        mem[STANDINGS_BCD_LO + x] = lo.val;
        Adc hi = adc_value(mem[STANDINGS_BCD_HI + x], 0x00, lo.carry);                    /* ADC #0 */
        mem[STANDINGS_BCD_HI + x] = hi.val;

        if (--ctr_lo != 0) continue;                      /* DEC math_lo; BNE */
        if (!((--ctr_hi) & 0x80)) continue;               /* DEC math_hi; BPL */
        break;
    } while (1);

    cpu.Y = y;                                            /* FUN_6698 indexes the lap total by Y */
    FUN_6698();                                           /* folds the pair into the lap total; CLD */
}

/* ===========================================================================
   $3D5C  paint_fence_backdrop — the crash "show the fence" fill  (twin #128)
   ---------------------------------------------------------------------------
   check_crash's crash arm JSRs this (its ONLY caller) once the car has hit the fence, right
   after INCing horizon_extent.  It paints the whole 3D view SOURCE buffer to a static
   two-band dither so the next frame shows the crash barrier instead of the road.

   The work is one column loop over all 40 view columns.  Column `col` lives in the $80-spaced
   source block at $3000 + col*$80 and fills from row $46 DOWNWARD to its own bottom sentinel
   dash_block_starts[col] (exclusive).  Every filled row also lands in the two per-line
   start-source buffers (view_left_start_src $504, view_right_start_src $4400) at the same row
   index.  The fill byte is a 4-entry dither that repeats every four rows (a per-column counter
   that starts at 3 and cycles 3,2,1,0) and is chosen by the horizon: rows at/above
   horizon_extent take fence_pattern_hi, rows below take fence_pattern_lo — with a zero
   fence_pattern_hi byte falling back to the lo table (dead in the shipping tables, kept
   faithfully).  It also clears wheel_spin_rate: after a crash the car is stopped.

   ⚠ ONE 6502 idiom is load-bearing and kept exactly: the row loop tests Y AFTER decrementing,
   so the bottom sentinel is never itself written, and a sentinel > $46 wraps Y through all 256
   rows.  A `do { } while (y != bottom)` over uint8_t reproduces both.  All addresses are masked
   to 16 bits so the wrap can never index past mem[]; the destinations stay within $3000-$44FF
   and never reach the hardware window, so there are no guarded writes.

   No callees and no register inputs (the body opens by storing constants).  check_crash ignores
   every register on return (its next act is JSR sound_stop_all), so nothing is declared live;
   the shim still reconstructs the true exit ABI for faithfulness.
   =========================================================================== */

#define FENCE_COL_COUNT        0x28u    /* 40 view columns                                    */
#define FENCE_TOP_ROW          0x46u    /* every column fills from row $46 downward            */
#define FENCE_PATTERN_LO       0x3D78u  /* fence_pattern_lo — 4-byte dither, rows below horizon */
#define FENCE_PATTERN_HI       0x3D7Cu  /* fence_pattern_hi — 4-byte dither, rows at/above it   */
#define VIEW_BLOCK_BASE        0x3000u  /* the forty $80-spaced view source blocks             */
#define VIEW_BLOCK_STRIDE      0x80u

uint8_t paint_fence_backdrop_core(uint8_t horizon)
{
    /* After the crash the car has stopped rolling. */
    wheel_spin_rate = 0x00;

    uint8_t  last = 0;                          /* the final byte written (for the exit A) */
    uint16_t block = VIEW_BLOCK_BASE;

    for (uint8_t col = 0; col < FENCE_COL_COUNT; col++) {
        const uint8_t bottom = mem[DASH_BLOCK_STARTS + col];   /* $3900,col — this column's floor */

        uint8_t pat = 3;                        /* dither index resets to 3 at the top of each column */
        uint8_t y   = FENCE_TOP_ROW;
        do {
            uint8_t b = mem[FENCE_PATTERN_LO + pat];
            if (y >= horizon) {                 /* CPY horizon_extent / BCC uses lo */
                uint8_t hi = mem[FENCE_PATTERN_HI + pat];
                if (hi) b = hi;                 /* LDA hi / BNE — zero falls back to lo */
            }
            mem[(uint16_t)(block + y)]                 = b;   /* the column's own source block */
            mem[(uint16_t)(VIEW_LEFT_START_SRC  + y)]  = b;
            mem[(uint16_t)(VIEW_RIGHT_START_SRC + y)]  = b;
            last = b;

            pat = (uint8_t)((pat - 1) & 3);     /* 3,2,1,0,3,... (DEX / BPL / LDX #3) */
            y--;                                /* DEY, then test — sentinel not written */
        } while (y != bottom);

        block += VIEW_BLOCK_STRIDE;             /* next $80-spaced column ($70/$71 EOR $80 / INC) */
    }

    /* Leave the zero-page scratch exactly as the 6502 did — the differential compares all of
       mem[].  math_lo counted the columns up to the $28 terminator; math_hi holds the last
       column's bottom; plot_ptr walked to $3000 + 40*$80 = $4400. */
    math_lo     = FENCE_COL_COUNT;
    math_hi     = mem[DASH_BLOCK_STARTS + (FENCE_COL_COUNT - 1)];
    plot_ptr_lo = (uint8_t)block;
    plot_ptr_hi = (uint8_t)(block >> 8);
    return last;
}


/* ===========================================================================
   THE CAR-ORDER INDEX CLUSTER  (twins #129-#133)
   ===========================================================================
   The mod-20 running-order index helpers, the car_order swap, and the two
   routines built on them.  All of it is index bookkeeping over the 20-entry
   car_order table — no arithmetic the 68000 lacks — so the twin's only job is
   to delete the per-instruction interpreter around a walk that runs every frame.
   car_order holds the field in running order; each entry is a car index 0-19.
   =========================================================================== */

#define RACE_CLOCK_LO   0x06B4u   /* race_clock_lo — per-car 3-byte BCD race clock */
#define RACE_CLOCK_MID  0x06CCu   /* race_clock_mid */
#define RACE_CLOCK_HI   0x06E4u   /* race_clock_hi */
#define FPN_PLAYER_SLOT 0x0003u   /* zp_scratch_index ($0003); in THIS routine = the player's slot in car_order */

/* $507E car_index_dec — step a car_order index back one, wrapping 0 -> 19.
   Faithful to `DEX / BPL / LDX #$13` for ANY input byte: a result with bit 7 set
   (only x==0 among valid indices) wraps to 19. */
static uint8_t car_index_dec_core(uint8_t x)
{
    uint8_t d = (uint8_t)(x - 1);
    return (d & 0x80u) ? 0x13u : d;
}

/* $5084 car_index_inc — step a car_order index forward one, wrapping 19 -> 0.
   Faithful to `INX / CPX #$14 / BCC / LDX #0`: any value that reaches 20 wraps to 0. */
static uint8_t car_index_inc_core(uint8_t x)
{
    uint8_t i = (uint8_t)(x + 1);
    return (i >= 0x14u) ? 0x00u : i;
}

void car_index_dec(void) { cpu.X = car_index_dec_core(cpu.X); }   /* exit ABI: X only */
void car_index_inc(void) { cpu.X = car_index_inc_core(cpu.X); }

/* $267F car_order_swap — exchange car_order[xi] and car_order[yi].  On exit the 6502
   leaves X = the value now at [xi] (old [yi]) and Y = the value now at [yi] (old [xi]),
   and parks old [xi] in the math_lo scratch. */
void car_order_swap_core(uint8_t xi, uint8_t yi, uint8_t* outX, uint8_t* outY)
{
    uint8_t oldX = mem[CAR_ORDER_TBL + xi];
    uint8_t oldY = mem[CAR_ORDER_TBL + yi];
    math_lo = oldX;                              /* $2682 — the scratch the oracle writes */
    mem[CAR_ORDER_TBL + xi] = oldY;
    mem[CAR_ORDER_TBL + yi] = oldX;
    *outX = oldY;
    *outY = oldX;
}


/* $63A2 find_player_neighbours — locate the player's own car in the running order and
   record the cars immediately ahead and behind.  Scans car_order from slot 19 down; a
   miss leaves the slot at $FF (the 6502's DEX-past-0), which the mod-20 helpers then
   wrap exactly as the original did. */
static void find_player_neighbours_core(void)
{
    uint8_t p = player_car;
    int i = 0x13;
    while (i >= 0 && mem[CAR_ORDER_TBL + i] != p) i--;
    uint8_t slot = (uint8_t)i;                   /* i == -1 -> 0xFF, matching DEX past 0 */

    mem[FPN_PLAYER_SLOT] = slot;
    car_ahead  = car_index_inc_core(slot);
    car_behind = car_index_dec_core(slot);
    cpu.X = car_behind;                          /* exit ABI: X = the last index computed */
}

void find_player_neighbours(void) { find_player_neighbours_core(); }

/* $5011 clear_race_clock — zero the 3-byte BCD race clock for car X. */
void clear_race_clock_core(uint8_t x)
{
    mem[RACE_CLOCK_LO  + x] = 0x00u;
    mem[RACE_CLOCK_MID + x] = 0x00u;
    mem[RACE_CLOCK_HI  + x] = 0x00u;
}


/* ===========================================================================
   TWINS #134-#135 — the track-position STEPPERS (user, 2026-08-26, Stage 2).

   Each moves car X one offset-unit along (advance) or back (retreat) the track.
   The track is a ring of segments; a car sits at car_segment[X] (an index in
   units of 8 into segment_len_tbl) plus car_seg_offset[X] within it, and a
   separate 16-bit car_distance[X] counts distance round the lap.  Stepping off
   the current segment moves to the neighbour and wraps the ring at
   segment_count_x8; the distance counter wraps at lap_length and, going
   backward, un-books a lap for the PLAYER only.

   Escaping flag: CARRY = "crossed a segment boundary this step" (the ONLY flag
   any caller consumes — all three branch on !C; see the call sites).  No BCD:
   this is the geometry path, always entered with D=0 (docs/static-map.md
   §Decimal mode), so the distance counter is a plain binary uint16_t.
   =========================================================================== */
#define SEGMENT_LEN_TBL   0x5907u   /* segment_len_tbl: length of each track segment */
#define CAR_LAP_COUNT     0x04B4u   /* car_lap_count: completed laps per car */
/* CAR_DISTANCE_LO / CAR_DISTANCE_HI defined above, before the car_gap twin. */

/* $147C track_pos_advance — step car x one offset-unit forward. */
static uint8_t track_pos_advance_core(uint8_t x)
{
    uint8_t seg  = mem[CAR_SEGMENT_TBL + x];
    uint8_t off  = (uint8_t)(mem[CAR_SEG_OFFSET + x] + 1);
    uint8_t crossed = (off >= mem[SEGMENT_LEN_TBL + seg]);   /* escaping carry */

    if (crossed) {                          /* stepped past this segment's end */
        seg = (uint8_t)(seg + 8);
        if (seg >= segment_count_x8) seg = 0;               /* wrap round the ring */
        mem[CAR_SEGMENT_TBL + x] = seg;
        off = 0;                            /* restart the offset in the new segment */
    }
    mem[CAR_SEG_OFFSET + x] = off;

    /* one unit further round the lap; a completed lap zeroes the counter and books it */
    uint16_t dist = (uint16_t)(mem[CAR_DISTANCE_LO + x] | (mem[CAR_DISTANCE_HI + x] << 8));
    uint16_t lap  = (uint16_t)(lap_length_lo | (lap_length_hi << 8));
    if (++dist == lap) {
        mem[CAR_DISTANCE_LO + x] = 0x00u;
        mem[CAR_DISTANCE_HI + x] = 0x00u;
        lap_complete();                     /* X still == x; lap_complete reads it */
    } else {
        mem[CAR_DISTANCE_LO + x] = (uint8_t)dist;
        mem[CAR_DISTANCE_HI + x] = (uint8_t)(dist >> 8);
    }
    return crossed;
}

void track_pos_advance(void) { cpu.C = track_pos_advance_core(cpu.X); }   /* exit ABI: C only */

/* $14C3 track_pos_retreat — step car x one offset-unit backward (the reverse of advance). */
static uint8_t track_pos_retreat_core(uint8_t x)
{
    uint8_t seg = mem[CAR_SEGMENT_TBL + x];
    uint8_t off = mem[CAR_SEG_OFFSET + x];
    uint8_t crossed = (off == 0);           /* escaping carry: stepping off the segment start */

    if (crossed) {                          /* move back into the previous segment */
        if (seg == 0) seg = segment_count_x8;               /* wrap at the ring start */
        seg = (uint8_t)(seg - 8);
        mem[CAR_SEGMENT_TBL + x] = seg;
        off = mem[SEGMENT_LEN_TBL + seg];   /* resume at that segment's far end */
    }
    mem[CAR_SEG_OFFSET + x] = (uint8_t)(off - 1);

    /* one unit back round the lap; underflowing past 0 wraps to a full lap and, for the
       PLAYER only, un-books a completed lap */
    uint8_t lo = mem[CAR_DISTANCE_LO + x];
    uint8_t hi = mem[CAR_DISTANCE_HI + x];
    for (;;) {
        if (lo != 0) break;                 /* low byte still has room -> just decrement it */
        hi = (uint8_t)(hi - 1);
        if ((hi & 0x80u) == 0) break;       /* no borrow: distance was >= 0x100 */
        lo = lap_length_lo;                 /* distance was 0 -> wrap to a full lap */
        hi = lap_length_hi;
        if (x == player_car && mem[CAR_LAP_COUNT + x] != 0)
            mem[CAR_LAP_COUNT + x]--;
    }
    mem[CAR_DISTANCE_LO + x] = (uint8_t)(lo - 1);
    mem[CAR_DISTANCE_HI + x] = hi;
    return crossed;
}

void track_pos_retreat(void) { cpu.C = track_pos_retreat_core(cpu.X); }   /* exit ABI: C only */

/* ------------------------------------------------------------------------------------------------
 * $109B  full_track_scan_rebuild  —  NATIVE DRIVER (STAGE 5), the root of the crash-freeze subtree.
 * Was FUN_109b.
 *
 * reset_driving_variables calls this on a crash / session reset.  With the off-line-scan flag
 * (track_scan_active bit 7) raised — so lap_complete ignores the artificial track motion — it
 * re-lays the whole field and rebuilds the geometry around it:
 *
 *   1. advance all 20 cars round the ring until car 0's distance counter wraps to 0 (re-anchor the
 *      field to the start line);
 *   2. a triangular retreat grid — for each outer index 0..$13, retreat cars [outer..$13] in
 *      sorted order, each (entry A + 1) times — fanning the field out behind the leader;
 *   3. re-anchor the pace car ($17): advance it until its gap to the player is exactly $20 the
 *      near way round;
 *   4. back the pace car up $31 units, then on to the previous segment boundary, counting the
 *      sections spanned into shared_counter_42;
 *   5. seed every car's car_state_2 with an alternating $AF/$50 pattern in sorted order;
 *   6. rebuild that many track sections from the walk origin (build_road_section).
 *
 * A DRIVER, not a leaf: every loop ends on a game-state boundary (the field's distance wrap; the
 * $20 pace gap; a segment-boundary carry), never on a bounded input, so no randomised validate
 * fixture can drive it to a defined exit.  It is a NATIVE_FUNCS member (transpile.py), gated by
 * `make determinism-crash` — a HOLD_THROTTLE run that actually crashes off-track and runs this
 * routine seven times before the frame-1500 dump.  Its callees are all native twins already, so
 * this reproduces only its OWN mem[] writes and drives the callees in order with the same register
 * inputs.  Exit regs/flags are dead (reset_driving_variables reloads X and A immediately after).
 *
 * The shared-scratch cells the transliteration writes are reproduced faithfully — shared_temp_76/77
 * (the retreat counters), hypot_min_lo (the grid's outer index), shared_counter_42 (the section
 * count) — because the 64K determinism compare sees them.
 * ------------------------------------------------------------------------------------------------ */
void full_track_scan_rebuild(void)
{
    uint8_t entry_a = cpu.A;                  /* the per-cell retreat depth (see step 2) */
    shared_temp_76 = entry_a;

    /* raise the off-line-scan flag.  SEC/ROR at $109E is byte-exact: the rotate shifts the old
       byte down under the new bit 7, and the matching LSR at exit shifts it back — reproduced so
       the flag cell matches, not simplified to a plain bit set/clear. */
    track_scan_active = (uint8_t)(0x80u | (track_scan_active >> 1));

    /* 1. advance the whole field until car 0 sits on the start line (distance == 0) */
    do {
        for (uint8_t x = 0x13u; x != 0xFFu; x--) {
            cpu.X = x;
            track_pos_advance();
        }
    } while ((mem[CAR_DISTANCE_LO + 0] | mem[CAR_DISTANCE_HI + 0]) != 0u);

    /* 2. triangular retreat grid.  hypot_min_lo is the outer index cell ($FF then pre-incremented
       to 0..$13); the inner sweep starts at the outer index and runs to $13. */
    hypot_min_lo = 0xFFu;
    for (;;) {
        hypot_min_lo++;
        if (hypot_min_lo >= 0x14u) break;
        for (uint8_t x = hypot_min_lo; x < 0x14u; x++) {
            shared_temp_77 = shared_temp_76;                 /* reset the per-car retreat counter */
            do {
                cpu.X = mem[CAR_ORDER_TBL + x];              /* the car at this sorted position */
                track_pos_retreat();
            } while ((int8_t)(--shared_temp_77) >= 0);       /* runs entry_a + 1 times */
        }
    }

    /* 3. re-anchor the pace car ($17): advance it until its gap to the player is exactly $20 the
       near way (car_gap_tail: C set = far side, so keep going; A == $20 = the target gap) */
    do {
        cpu.X = 0x17u;
        track_pos_advance();
        cpu.Y = 0x17u;
        cpu.X = player_car;
        cpu.C = 1;                                            /* entry borrow-in for the subtract */
        car_gap_tail();
    } while (cpu.C || cpu.A != 0x20u);

    /* 4. back the pace car up $31 units, then on to the previous segment boundary, counting every
       section it spans (from $31) into shared_counter_42 for step 6 */
    cpu.X = 0x17u;
    shared_temp_76 = 0x31u;
    shared_counter_42 = 0x31u;
    do {
        track_pos_retreat();
    } while (--shared_temp_76 != 0u);
    do {
        shared_counter_42++;
        track_pos_retreat();
    } while (cpu.C == 0);                                     /* until a segment boundary is crossed */

    /* 5. seed car_state_2 for all 20 cars in sorted order with an alternating $AF/$50 pattern
       (A starts $50 and is EOR #$FF'd before each store, so it toggles each car) */
    {
        uint8_t a = 0x50u;
        for (uint8_t y = 0x13u; y != 0xFFu; y--) {
            a ^= 0xFFu;
            mem[CAR_STATE_2 + mem[CAR_ORDER_TBL + y]] = a;
        }
    }

    /* 6. rebuild shared_counter_42 track sections from the walk origin */
    section_cursor = 0u;
    do {
        build_road_section();
    } while (--shared_counter_42 != 0u);

    /* lower the off-line-scan flag (LSR at $111A) */
    track_scan_active = (uint8_t)(track_scan_active >> 1);
}

/* ------------------------------------------------------------------------------------------------
 * $4F77 lap_complete — TWIN #136.  track_pos_advance calls this when a car's distance counter wraps
 * a lap.  It books the completed lap and, when the mode calls for it, records the lap TIME:
 * race_clock - car_lap_start as a 3-byte BCD value (centiseconds / seconds base-60 / minutes), and
 * keeps the per-car best.  The player's finish is credited once per approach (a one-shot debounce)
 * and, on the final lap, arms the end-of-session countdown.
 *
 * BCD is real here — it is one of the eight sanctioned SED sites (docs/static-map.md §Decimal
 * mode) — so the time subtract goes through sbc_value/adc_value inside a cpu.D=1 bracket, exactly
 * mirroring the routine's SED..CLD.  The best-lap check is a plain 24-bit magnitude compare: valid
 * BCD bytes order like their decimal value, so no decimal arithmetic is needed merely to sort them.
 * No register or flag escapes (the sole caller discards A/flags), so the shim marshals nothing back.
 * ------------------------------------------------------------------------------------------------ */
#define CAR_LAP_START_LO  0x0898u   /* car_lap_start_lo: per-car lap-start BCD timestamp, low  */
#define CAR_LAP_START_MID 0x08ACu   /* car_lap_start_mid:                               middle */
#define CAR_LAP_START_HI  0x04DCu   /* car_lap_start_hi:                                 high  */
#define CAR_BEST_LAP_LO   0x06A0u   /* car_best_lap_lo: per-car best lap time, low byte  */
#define CAR_BEST_LAP_MID  0x06B8u   /* car_best_lap_mid */
#define CAR_BEST_LAP_HI   0x06D0u   /* car_best_lap_hi  */

static void lap_complete_core(uint8_t x)
{
    /* an off-line full-track scan fakes position advances — don't count them as finishes */
    if (track_scan_active & 0x80u) return;
    /* only the 20 real cars carry lap stats */
    if (x >= 0x14u) return;
    /* a car the projection rejected (flags bit 6) is skipped */
    if (mem[CAR_FLAGS_SHAPE + x] & 0x40u) return;

    /* the player's lap is credited only once per approach: a self-restoring one-shot debounce that
       fires exactly when lap_credit_armed == 1 (armed), leaving it disarmed */
    if (x == player_car) {
        if (lap_credit_armed != 1u) return;
        lap_credit_armed = 0u;
        lap_completed_flag = 0x80u;         /* ask update_lap_timers to refresh laps-remaining */
    }

    /* book the completed lap (a count already >= $80 is a sentinel and left alone).  laps holds the
       PRE-increment value — that is what the race-mode gate below compares. */
    uint8_t laps = mem[CAR_LAP_COUNT + x];
    if (!(laps & 0x80u)) mem[CAR_LAP_COUNT + x] = (uint8_t)(laps + 1u);

    /* decide whether this lap's TIME gets recorded, by session mode */
    int record;
    if (session_is_race & 0x80u) {                      /* RACE: gate on the session lap total */
        if (laps < race_lap_total) {
            record = 1;                                 /* still within the race distance */
        } else if (laps == race_lap_total) {
            record = 1;                                 /* the final lap is recorded... */
            if (x == player_car)
                session_end_countdown = 0x50;           /* ...and the player's finish ends the race */
        } else {
            record = 0;                                 /* past the finish */
        }
    } else {                                            /* practice / qualifying: gate on slot order */
        record = (x <= player_car);                     /* player, or a car ahead of it in car_order */
    }
    if (!record) return;

    /* lap time = race_clock - car_lap_start, a 3-byte BCD value whose MIDDLE byte is seconds in
       base 60: a borrow there adds $60 and forces a borrow into the minutes byte. */
    cpu.D = 1;                                          /* SED — sanctioned BCD site */
    Adc t_lo = sbc_value(mem[RACE_CLOCK_LO], mem[CAR_LAP_START_LO + x], 1);   /* SEC first */
    math_lo = t_lo.val;

    Adc t_mid = sbc_value(mem[RACE_CLOCK_MID], mem[CAR_LAP_START_MID + x], t_lo.carry);
    unsigned hi_carry_in = t_mid.carry;                 /* 1 = no borrow out of the seconds byte */
    if (!t_mid.carry) {                                 /* seconds underflowed: base-60 fixup */
        t_mid = adc_value(t_mid.val, 0x60, 0);          /* ADC #$60 (C clear) */
        hi_carry_in = 0;                                /* CLC — force the borrow into minutes */
    }
    math_hi = t_mid.val;

    Adc t_hi = sbc_value(mem[RACE_CLOCK_HI], mem[CAR_LAP_START_HI + x], hi_carry_in);
    hypot_min_hi = t_hi.val;

    /* only a non-negative lap time (no borrow out of the whole subtract) can be a best lap */
    if (t_hi.carry) {
        uint32_t lap  = ((uint32_t)hypot_min_hi << 16) | ((uint32_t)math_hi << 8) | math_lo;
        uint32_t best = ((uint32_t)mem[CAR_BEST_LAP_HI + x] << 16)
                      | ((uint32_t)mem[CAR_BEST_LAP_MID + x] << 8) | mem[CAR_BEST_LAP_LO + x];
        if (lap < best) {                               /* a new best for this car */
            mem[CAR_BEST_LAP_LO + x]  = (uint8_t)(math_lo & 0xF0u);   /* drop the centisecond units */
            mem[CAR_BEST_LAP_MID + x] = math_hi;
            mem[CAR_BEST_LAP_HI + x]  = hypot_min_hi;
        }
    }

    /* start the next lap's clock from now */
    mem[CAR_LAP_START_LO + x]  = mem[RACE_CLOCK_LO];
    mem[CAR_LAP_START_MID + x] = mem[RACE_CLOCK_MID];
    mem[CAR_LAP_START_HI + x]  = mem[RACE_CLOCK_HI];
    cpu.D = 0;                                          /* CLD */
}

void lap_complete(void) { lap_complete_core(cpu.X); }   /* X = car index; nothing escapes */
