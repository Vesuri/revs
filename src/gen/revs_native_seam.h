/* revs_native_seam.h — cluster-9 seam.  Shared vocabulary between revs_native.c
 * (the cpu-free typed cores) and revs_native_seam.c (the thin 6502-ABI shims).
 * Generated once by tools/split_seam.py; hand-maintained thereafter. */
#ifndef REVS_NATIVE_SEAM_H
#define REVS_NATIVE_SEAM_H
#include <stdint.h>
#ifndef REVS_MEM_ALIASES
#define REVS_MEM_ALIASES
#endif
#include "../cpu/cpu.h"
#include "../cpu/bus.h"
#include "../cpu/m68k_math.h"
#include "../cpu/bcd.h"
#include "revs_decl.h"
#include "mem.h"
#include "../platform/platform_c.h"
#include "../platform/bbc_screen.h"
#include "../platform/probe.h"
#include "../platform/shape.h"
#include "../platform/revs_plot.h"

/* ---- address constants the shims use.  Anything symbols.csv names comes in as a MEM_ name
 *      from mem.h and is NOT redefined here; what is left is a stride, a count, or an alias
 *      that says which TENANT of a shared cell is meant. ---- */
#define EDGE_HALF        40u       /* ⚠ a STRIDE, not an address: the two road sides' point
                                    * halves are 40 apart in every edge_* table */
/* MEM_view_left_start_src — per scan line: the LEFT run's first source byte */
/* MEM_view_right_start_src — ...and the RIGHT run's */
/* MEM_dash_block_starts — per block: the offset its live data begins at (< $4F) */
#define DASH_BLOCK_COUNT  0x29u     /* 41 blocks */
/* MEM_point_delta_hi — point_delta_hi[0..2]   — ...its magnitude's high byte */
#define SLIP_MAG_HI      MEM_plot_ptr3_hi  /* plot_ptr3_hi — ...and its high byte (docs/rename.md) */
#define SLIP_SIGN        MEM_hypot_min_hi  /* hypot_min_hi — here the sign byte abs16_math branches on */
/* MEM_car_section_along — per-driver: distance ALONG the section from its origin */
#define FENCE_COL_COUNT        0x28u    /* 40 view columns                                    */

/* ---- SoA array bases (Step 0 of the wide-value cleanup: one home each, was duplicated
 *      across revs_native.c; the single swap point when a base becomes a value_16[N]) ---- */
/* MEM_model_state_lo — the driving model's 16-bit state vector, low bytes */
/* MEM_model_state_hi — ...and high bytes; element i is +i in each */
#define MODEL_STATE_N    15u       /* ⚠ FIFTEEN elements, 0..14: $62DF is loop_counter_hi and
                                    * $62EF is a separate cell, so the vector stops at 14.
                                    * ⭐ RELOCATED to model_state_16[]: these two bases are now
                                    * the marshals' addresses only. */
#define CAR_ANGLE_LO     MEM_heading_sin_lo   /* car-angle array: heading_sin/heading_cos/steer_angle low; bit0 = SIGN */
#define CAR_ANGLE_HI     MEM_heading_sin_hi   /* ...and their high bytes.  ⭐ RELOCATED to car_angle_16[]:
                                    * these two are now the marshals' addresses only — no twin
                                    * reaches the array through mem[] any more. */
/* Which element is which (see car_angle_16 in revs_native.c for the sign-magnitude packing). */
#define CAR_ANGLE_SIN    0u        /* SIN(car_heading)  — compute_car_angles' output */
#define CAR_ANGLE_COS    1u        /* COS(car_heading)  — ...and its second pass */
#define CAR_ANGLE_STEER  2u        /* the steering angle — the control read owns it */
/* MEM_view_origin_lo — view_origin_lo — 3 components, STRIDE 6, two origins */
/* MEM_view_origin_hi — view_origin_hi */
/* MEM_edge_x_lo — edge_x_lo — the track edges' angle, low byte */
/* MEM_edge_x_hi — edge_x_hi — ...and the high byte */
/* MEM_edge_opp_x_lo — edge_opp_x_lo — the OPPOSITE boundary's angle at that point */
/* MEM_edge_opp_x_hi — edge_opp_x_hi */
/* MEM_edge_y — edge_y      — per edge point: the scan line it projects to */
/* MEM_marker_edge_index — marker_edge_index  — 3 corner markers, per frame */
/* MEM_marker_flags — marker_flags */
/* MEM_marker_offset_lo — marker_offset_lo */
/* MEM_marker_offset_hi — marker_offset_hi */
/* MEM_track_dir_0 — track_dir_0[Y] — direction component 0 (ground plane) */
/* MEM_track_dir_1 — track_dir_1[Y] — component 1 (gradient) */
/* MEM_track_dir_2 — track_dir_2[Y] — component 2 (ground plane) */
/* MEM_surface_colours — surface_colours — four MODE 5 colour bytes */
/* MEM_colour_pattern_and_tbl — colour_pattern_and_tbl */
/* MEM_colour_pattern_keep_tbl — colour_pattern_keep_tbl */
/* MEM_car_flags_shape — per slot: flags, with the object's shape in bits 0-3 */
/* MEM_car_order — the 20-entry sorted car order */
/* MEM_object_width — per slot: object screen width in pixels */
/* MEM_object_bearing_lo — per slot: the 16-bit bearing to the object, low byte */
/* MEM_object_bearing_hi — ...and high byte */
/* MEM_mirror_seg_bearing_tbl — 6 wing-mirror segment heading thresholds */
/* MEM_mirror_seg_state — per wing-mirror segment: last-drawn bottom line, 0 = erased */
/* MEM_dial_needle_dda_tbl — rev-needle DDA len/delta per angle offset (0..0x13) */
/* MEM_dial_needle_origin_lo_tbl — rev-needle origin addr low per quadrant; &F8=ptr, &7=line */
/* MEM_dial_needle_origin_hi_tbl — rev-needle origin addr high per quadrant (all $75) */
/* MEM_steer_needle_dda_tbl — steering-wheel needle minor-axis delta per angle index */
#define MENU_SCREEN_BASE   0x7C00u /* front end: $7C00-$7FFF as the MODE 7 teletext page (== TT_SCREEN_BASE; time-multiplexed with the race view's view_cell_chain_a) */
/* MEM_menu_bar_start_tbl — menu_draw_gfx_bars: per-row start column of the two graphics bars */
/* MEM_menu_bar_end_tbl — ...and end column */
/* MEM_car_track_position — seed_car_track_position: per-car track position (20 entries) */
/* MEM_car_grid_base — per-car grid base row = car index >> 1 */
/* MEM_car_seed_index — car-index cursor for the grid-seeding loop */
/* MEM_car_best_lap_lo — per-car best lap, 3-byte BCD: low byte */
/* MEM_car_best_lap_mid — ...middle */
/* MEM_car_best_lap_hi — ...high ($10 = the 'no time yet' sentinel) */
/* MEM_car_lap_lo — per-car cumulative lap total, 3-byte BCD: low byte */
/* MEM_car_lap_mid — ...middle */
/* MEM_car_lap_hi — ...high */
/* MEM_standings_bcd_lo — per-column 16-bit BCD tally, low byte */
/* MEM_standings_bcd_hi — ...and high */
/* MEM_car_order_grid — the starting-grid order, saved across the results re-sorts */
/* MEM_class_lap_target_mid — the three race classes' BCD best-lap targets, mid byte */
/* MEM_class_lap_target_hi — ...and high byte */
/* MEM_qualify_minutes_tbl — the three qualifying durations: 4, 9, 25 minutes */
/* MEM_race_lap_total_tbl — the three race lengths: 5, 10, 20 laps */
/* ---- the BBC hardware registers the twins touch ----------------------------------------
   6522 VIA register file, offset from the base: +0 ORB, +1 ORA, +2/+3 DDRB/DDRA, +4/+5 T1
   counter lo/hi, +6/+7 T1 LATCH lo/hi, +8/+9 T2 counter lo/hi, +$B ACR, +$D IFR, +$E IER.
   System VIA is at $FE40, User VIA at $FE60.  ⚠ Naming these was not cosmetic: `VIA_T1_LOW`
   used to be defined TWICE, both times as $FE68, which is T2's counter — the engine's
   entropy source, not a T1 register (bbc_hw.cpp models it as a 1 MHz down-counter). */
#define SYSVIA_T1CH        0xFE45u /* System VIA T1 counter high */
#define SYSVIA_T1LL        0xFE46u /* ...T1 latch low */
#define SYSVIA_T1LH        0xFE47u /* ...T1 latch high */
#define SYSVIA_ACR         0xFE4Bu /* ...auxiliary control (T1 continuous-interrupt mode) */
#define SYSVIA_IFR         0xFE4Du /* ...interrupt flags — bit 1 is VSYNC */
#define SYSVIA_IER         0xFE4Eu /* ...interrupt enable */
#define USRVIA_T1CL        0xFE64u /* User VIA T1 counter low */
#define USRVIA_T1CH        0xFE65u /* ...T1 counter high */
#define USRVIA_T1LL        0xFE66u /* ...T1 latch low — the LAST write of every band arm, which
                                      is what closes a band record (see bbc_hw.cpp) */
#define USRVIA_T1LH        0xFE67u /* ...T1 latch high */
#define USRVIA_T2CL        0xFE68u /* ...T2 counter low — the engine's ONLY entropy source */
#define USRVIA_T2CH        0xFE69u /* ...T2 counter high — written once a field to restart T2 */
#define USRVIA_ACR         0xFE6Bu /* ...auxiliary control (T1 free-run) */
#define USRVIA_IFR         0xFE6Du /* ...interrupt flags — bit 6 is the T1 band timeout */
#define USRVIA_IER         0xFE6Eu /* ...interrupt enable — bit 6 is the T1 band timer */
/* The one OS vector Revs claims: hw_init saves the MOS handler out of it into saved_irq1v,
   points it at irq1v_band_schedule ($4E5C), and irq1v_release puts the MOS one back. */
#define IRQ1V_LO           0x0204u /* IRQ1V, low byte */
#define IRQ1V_HI           0x0205u /* ...and high */

/* ---- exit-struct typedefs (moved out of revs_native.c) ---- */
/* The object/slot-writer chain's exit ABI — A/X/Y + N/Z/V/C returned by value so a core stays
   cpu-free; the thin shim (or a caller whose own exit ABI is this) replays it onto cpu. */
typedef struct { uint8_t a, x, y, n, z, v, c; } SlotExit;
/* build_track_geometry's exit ABI: live=AXY, flags a byproduct. */
typedef struct { uint8_t a, x, y; } GeoExit;
typedef struct { uint8_t val, carry; } Adc;
/* The three values the chain and its drivers thread through each other — the 6502's A, X
   and Y under the names of what they actually hold.  Everything else is a plain local. */
typedef struct {
    unsigned byte;   /* A: the pixel byte the chain carries left to right */
    unsigned line;   /* X: the scan line being painted */
    unsigned cell;   /* Y: the cell's byte offset within the line, or a glyph index */
} ViewState;
/* How much of the session reset a restart re-runs.  The 6502 expresses this as three branch
   targets INSIDE race_main_loop's prologue that the tail jumps back to, so the depths are
   nested by construction: each entry point falls through into the next. */
typedef enum {
    RESTART_NONE = 0,   /* $16F9 — back from the pits: keep the session exactly as it was */
    RESTART_LATE,       /* $16F6 — rebuild the player's car and the driver tables only */
    RESTART_MID,        /* $16F3 — and zero $00-$68 plus $6280-$62FF: a fresh lap */
    RESTART_FULL        /* $16EE — and reset the player's race clock: a fresh session */
} RestartDepth;
/* What race_frame_tail decided about this frame. */
typedef enum {
    LOOP_NEXT_FRAME,    /* $17B7 — round again */
    LOOP_RESTART,       /* leave the frame loop and re-run the reset to `g_restartDepth` */
    LOOP_FINISHED       /* $17BA — the session is over; leave the routine */
} LoopVerdict;
typedef struct { uint8_t line; int clip; int behind; } ProjPoint;
typedef struct { uint16_t value; uint8_t c, v; } Wide16Exit;  /* a 16-bit result + the flags
                                                                 its high-byte op leaves live */
/* The exit flags of a 16-bit binary add, returned by value so a core stays cpu-free; a shim
   (or a caller whose own exit ABI is this add's) replays them onto the cpu. */
typedef struct { uint8_t hi, carry, overflow, neg, zero; } AddFlags;
/* update_engine_revs' escaping registers: the tail add's exit A/flags (every arm ends in
   engine_note_only), plus X and Y, which take path-dependent values.  Returned by value so the
   core stays cpu-free; the shim replays them.  EngineRegs is the starter poll's escaping X/Y. */
typedef struct { AddFlags tail; uint8_t x, y; } EngineExit;
typedef struct { uint8_t x, y; } EngineRegs;
/* update_camera_and_drive_state's escaping registers: the final car_speed_scaled add's exit
   A/flags, plus X (= player_car) and Y (= car_section_cursor).  Returned by value; shim replays. */
typedef struct { AddFlags acc; uint8_t x, y; } CameraExit;
typedef struct {
    uint16_t dist;        /* -> point_dist_lo/hi */
    uint16_t min;         /* -> hypot_min_hi, and hypot_min_lo too on the far arm only */
    uint16_t maxEighth;   /* -> math_hi:math_lo (LOW byte in math_hi) — the far arm only */
    int      farArm;
} PointDist;
typedef struct { uint8_t a, y, n, z, v, c; } WidthExit;
typedef struct {
    uint8_t sectionIndex;   /* the walk's starting byte index into section_coord_lo/hi */
    uint8_t wrapLimit;      /* -> section_wrap_limit */
    uint8_t side;           /* -> road_side_index */
} RoadSide;
typedef struct { uint8_t quotient, remainder, overflow, setV; } Div16By8;
typedef struct {
    uint16_t mag;     /* |section coordinate - view origin| for this component */
    uint8_t  rawHi;   /* the subtraction's high byte BEFORE the absolute value — the sign */
} ViewDelta;
typedef struct {
    unsigned stepIn, stepOut;   /* the two Y-step opcode slots */
    unsigned destLo, destHi;    /* the patched operand pair: this pass's surface_edge buffer */
    uint16_t *cellPtr;          /* the screen pointer the colour cell is read and written through */
    uint16_t *linePtr;          /* ...and the one bearing_hi's copy goes through */
} SpanPlotter;
typedef struct {
    unsigned table;        /* the arm's entry-offset table, indexed by the sub-column phase */
    unsigned operand;      /* the branch operand byte the offset is written over */
    unsigned base;         /* the address that offset is relative to (the branch's own next) */
    unsigned addend;       /* what the DDA accumulates */
    unsigned subtrahend;   /* ...and what it takes back off when it carries */
    int      rev;          /* descending */
    int      steep;        /* Y-major */
    uint8_t  bound;        /* the plot_ptr2_hi value at which the walk stops */
} SpanArm;
/* The far/near endpoint indices interp_edge hands back to its caller.  The 6502 left them in
   X and Y ($2D05/$2D08) — not a computed result but the caller's OWN input indices, which the
   convention keeps in place so the caller can step them.  The native caller
   (draw_surface_spans_core) tracks x/y in its own locals and IGNORES this return; only the
   transliterated oracle (draw_surface_spans__t6502) does INX/INY on them, so the interp_edge
   SHIM marshals these two fields back into cpu.X/cpu.Y for that oracle's benefit. */
typedef struct { uint8_t farIdx, nearIdx; } EdgeIndices;
typedef struct { uint8_t a, v, c, n, z; } EdgeOffFlags;
typedef struct { uint16_t product; uint8_t v, setV; } Mul8;
typedef struct { uint8_t a, n, z, c, v; } Mul8AccumExit;
typedef struct { uint8_t hi; int declined; } SlipRef;
typedef struct { uint8_t a, y, n, z, v, c; } SignOriginExit;   /* X passes through the caller's */
typedef struct { uint8_t a, y, n, z, c; } ContactExit;    /* X and V pass through the caller's */
typedef struct { uint8_t a, y; } RejectExit;                   /* N/Z derive from a (bit7 set) */
typedef struct { uint8_t row; uint8_t line; } Mode5Addr;  /* plot_ptr side-effect; row=X, line=A/Y */
typedef struct { uint8_t ch; int usedMos; } VduDef;       /* def took the OSWRCH path? */
typedef struct { uint8_t mag; uint8_t dir; uint8_t reading; } AdcRead;   /* distance from centre, its sign, and the raw MOS reading that leaks out in Y */
typedef struct { uint8_t a, n, c; } GapTail;
/* stage_nearby_car_core's decision: reject == 1 -> the shim calls reject_object_slot; otherwise
   y is the view-section cursor to pass into place_car_world_coords ($2922 TAY). */
typedef struct { int reject; uint8_t y; } StageNearbyCar;
/* draw_corner_marker_core's per-marker result.  math_lo/hi/temp76 are the $74/$75/$76 exit
   values on the SKIP path (on the DRAW path plot_object overwrites them); plot_x/plot_line/
   proj_width are meaningful only when draw != 0. */
typedef struct { int draw; uint8_t mathLo, mathHi, temp76, plotX, plotLine, projWidth; } CornerMarker;
/* MosRegs (an MOS call's A/X/Y + carry) is declared in platform_c.h, the header
   that also declares platform_mos_call_typed the wrappers below funnel through. */
typedef struct { uint8_t y, c, v; } SpinExit;    /* begin_spin's residue: OSWORD Y + block ADC C/V */
/* mirrors_update_setup_core's pre-loop result.  half is math_lo's ($74) 6502 exit value, written
   ONLY when drawable (the routine's skip path leaves math_lo untouched); bottom -> shared_temp_84,
   top -> span_line_cursor are meaningful only when drawable; heading -> shared_temp_76 is set on
   both paths (the raw negative slot flag on the skip path). */
typedef struct { int drawable; uint8_t half, bottom, top, heading; } MirrorSetup;
/* dial_needle_angle_core's result.  offset -> math_lo ($74) 6502 exit value AND the index into
   dial_needle_dda_tbl; octant -> shared_temp_76 (the octant index plot_line_octant dispatches on);
   temp77 -> shared_temp_77; quadrant -> the index into the origin tables. */
typedef struct { uint8_t offset, octant, temp77, quadrant; } NeedleDial;

/* driver_name_address_core's result.  lo -> exit A / plot_ptr2_lo, hi -> exit Y / plot_ptr2_hi;
   scratch -> math_lo ($74) 6502 exit value ((index&3)*4, dead scratch). */
typedef struct { uint16_t addr; uint8_t scratch; } NamePtr;

/* draw_dash_needle_core's result — the steering-wheel needle handed to plot_line_octant.
   angleIndex   -> math_lo ($74) 6502 exit value (folded angle index; plot_line_octant's DDA step);
   stepSize     -> shared_temp_76 ($76), the octant step plot_line_octant dispatches on (2/5, toggled
                   to 3/4 on the small-angle path);
   ddaLen       -> point_delta_hi ($0083), the minor-axis delta (from steer_needle_dda_tbl);
   originMasked -> A into mode5_addr (the &$FC screen origin);
   rowSel       -> Y into mode5_addr (its low 3 bits become plot_line_octant's start scanline);
   subPos       -> shared_temp_77 ($77), the sub-cell X plot_line_octant plots at. */
typedef struct { uint8_t angleIndex, stepSize, ddaLen, originMasked, rowSel, subPos; } DashNeedle;

/* ---- always_inline 6502 flag helpers (moved out of revs_native.c) ---- */
/* The flag-carrying primitives, so that no twin has to be written in 6502.
   ⚠⚠ ALWAYS_INLINE IS LOAD-BEARING, NOT A HINT.  Each wraps one cpu.h macro and writes the
   global `cpu`, so GCC leaves them out of line at -O3 — a `jsr` plus `movem.l` PER SUBTRACT,
   which made an arithmetic twin SLOWER than the transliteration.  Grep the objdump for
   `jsr <sub_from>` before believing one is fast.  (docs/perf-method.md §twins #14/#15) */
#define REVS_FLAG_OP static inline __attribute__((always_inline))
/* A = value, with N and Z from it.  Used where a value reaches A and an SMC trap can then
   exit the routine with both still live. */
REVS_FLAG_OP unsigned load_a(uint8_t value)
{
    LDA(value);
    return cpu.A;
}

/* `BIT` — N and V are bits 7 and 6 of the tested byte and Z is `A & byte`, so all three can
   still be live after it; this is the one form with no plain-C equivalent at all. */
REVS_FLAG_OP void bit_test(uint8_t value)
{
    BIT(value);
}

/* `AND #imm` / `LDX` / `DEX` — the register and the N/Z the 6502 writes with them, for the
   handful of places where those flags are still read after the operation. */
REVS_FLAG_OP unsigned and_a(uint8_t mask)
{
    AND(mask);
    return cpu.A;
}

REVS_FLAG_OP unsigned load_x(uint8_t value)
{
    LDX(value);
    return cpu.X;
}

REVS_FLAG_OP unsigned dec_x(void)
{
    DEX();
    return cpu.X;
}

/* a + addend + carry_in, setting C and V.  C chains (the 16-bit pointer step adds three
   times) and V is the one flag the cell chain can leak to its caller — nothing else in it
   writes V at all. */
REVS_FLAG_OP unsigned adc_step(unsigned a, uint8_t addend, int carry_in)
{
    cpu.A = (uint8_t)a;
    cpu.C = (uint8_t)(carry_in != 0);
    ADC(addend);
    return cpu.A;
}

/* a + addend + carry_in as a VALUE ONLY — the ADC counterpart of sbc_value below, and it
   exists for the same reason: a 16-bit add's low half feeds nothing but the high half's
   carry, so paying cpu.h's five flag stores for it is paying for nothing.  ⚠ Decimal mode is
   honoured, because D changes the RESULT BYTE and not merely the flags. */
REVS_FLAG_OP Adc adc_value(uint8_t a, uint8_t m, unsigned carryIn)
{
    unsigned c = carryIn ? 1u : 0u;
    unsigned t = (unsigned)a + m + c;
    Adc r;

    if (cpu.D) {
        unsigned al = (unsigned)(a & 0x0Fu) + (m & 0x0Fu) + c;
        unsigned ah = (unsigned)(a >> 4) + (m >> 4);
        if (al > 9) { al += 6; ah += 1; }
        if (ah > 9) ah += 6;
        r.carry = (uint8_t)(ah > 0x0Fu);
        r.val   = (uint8_t)(((ah << 4) | (al & 0x0Fu)) & 0xFFu);
    } else {
        r.carry = (uint8_t)(t > 0xFFu);
        r.val   = (uint8_t)t;
    }
    return r;
}

/* a - m - !carry_in as a VALUE + borrow-out — the SBC counterpart of adc_value, for the same
   reason (a multi-byte subtract's low halves feed only the next half's borrow).  ⚠ Decimal mode
   is honoured because D changes the RESULT BYTE.  On the 6502 the CARRY out of an SBC is the
   BINARY borrow even in decimal mode (only the accumulator digits are corrected), so .carry is
   computed from the plain subtraction in both branches. */
REVS_FLAG_OP Adc sbc_value(uint8_t a, uint8_t m, unsigned carryIn)
{
    int borrow = carryIn ? 0 : 1;
    int t = (int)a - (int)m - borrow;               /* binary result -> the carry/borrow */
    Adc r;
    r.carry = (uint8_t)(t >= 0);                     /* 1 = no borrow, exactly like binary SBC */

    if (cpu.D) {
        int lo = (a & 0x0F) - (m & 0x0F) - borrow;
        int hi = (a >> 4)   - (m >> 4);
        if (lo < 0) { lo += 10; hi -= 1; }           /* borrow from the high nibble */
        if (hi < 0) { hi += 10; }                    /* borrow out of the byte */
        r.val = (uint8_t)(((hi << 4) | (lo & 0x0F)) & 0xFF);
    } else {
        r.val = (uint8_t)t;
    }
    return r;
}

/* V for ONE add, replayed from its operands — the ADC counterpart of the SBC overflow replay, and it
   exists for the same reason: mul8's exit V is the V of the LAST add in an eight-step chain,
   so the twin computes that one add's overflow instead of the seven dead ones. */
REVS_FLAG_OP uint8_t adc_overflow(uint8_t a, uint8_t m, unsigned carryIn)
{
    unsigned t = (unsigned)a + m + (carryIn ? 1u : 0u);
    return (uint8_t)(((~(a ^ m) & (a ^ (uint8_t)t)) >> 7) & 1u);
}

/* V for ONE subtract, replayed from its operands — the SBC counterpart of adc_overflow.
   SBC computes A + ~M + C, so its overflow is ((A^M) & (A^result))>>7 (the two operands
   differ in sign and the result took the sign of M).  Used where a converted subtract's V is
   the only flag that escapes the routine. */
REVS_FLAG_OP uint8_t sbc_overflow(uint8_t a, uint8_t m, unsigned carryIn)
{
    uint8_t r = (uint8_t)(a - m - (carryIn ? 0u : 1u));
    return (uint8_t)((((a ^ m) & (a ^ r)) >> 7) & 1u);
}

/* value - subtrahend with the borrow clear (SEC/SBC), setting C and V. */
REVS_FLAG_OP unsigned sub_from(unsigned value, uint8_t subtrahend)
{
    cpu.A = (uint8_t)value;
    cpu.C = 1;
    SBC(subtrahend);
    return cpu.A;
}

/* value - subtrahend - !carry_in, setting C and V — the second half of a 16-bit subtract,
   where the borrow has to come from the low half's own SBC. */
REVS_FLAG_OP unsigned sbc_step(unsigned value, uint8_t subtrahend, int carry_in)
{
    cpu.A = (uint8_t)value;
    cpu.C = (uint8_t)(carry_in != 0);
    SBC(subtrahend);
    return cpu.A;
}

REVS_FLAG_OP unsigned zp_pointer(unsigned zp)
{
    return (unsigned)mem[zp & 0xFFu] | ((unsigned)mem[(uint8_t)(zp + 1)] << 8);
}

REVS_FLAG_OP uint8_t seam_read(unsigned addr, int ram)
{
    return ram ? mem[addr] : (uint8_t)bus_read((uint16_t)addr);
}

/* ── ⭐⭐ THE VIEW SWEEP'S PER-LINE SKIP (docs/direct-bitplane-plan.md §7h) ──────────────────
 * `make VIEWSKIP=1`.  A scan line whose forty sources are all zero paints one flat run of its
 * background byte, so repainting it changes nothing — PROVIDED the background byte has not
 * moved and the previous paint was itself flat.  (That third part is the one a producer flag
 * alone gets wrong: a line that carried road pixels last frame still has to erase them.)
 *
 * The producers mark; the sweep tests and clears.  Marking rides the two choke points every
 * store into $3000..$43CF already passes through, so no producer had to be found by reading —
 * `g_shapeMarkUnmarked` (shape.h) measures that the set is complete, and reads 0.
 *
 * ⚠ `make validate` CANNOT gate this: its fixtures write sources with fill_random, which no
 * hook sees, so the shadow would license a skip the oracle does not take.  The harness resets
 * the state to "everything dirty" before each case, which makes the skip inert there rather
 * than wrong.  The real gates are `make determinism` / -drive / -crash / -race (all compare the
 * frame buffer) and `make viewdiff`. */
#ifdef REVS_VIEWSKIP
#define VIEW_SKIP_LINE_LO 0x03u
#define VIEW_SKIP_LINE_HI 0x4Fu
extern unsigned char g_viewLineDirty[128];   /* a producer wrote one of this line's sources */
/* ⚠⚠ THE SHADOW IS KEYED BY DISPLAY LINE, NOT BY SOURCE LINE.  A source line's destination is
   wherever plot_ptr has walked to, and two source lines — phase 1's and phase 3's — can land on
   the SAME display line, each overwriting what the other painted.  Keyed by source line the
   skip then licenses a repaint that never happened; keyed by the destination it does not. */
extern unsigned char g_viewDstBg[208];       /* the byte this display line was last painted flat with */
extern unsigned char g_viewDstFlat[208];     /* ...and whether that paint was flat at all             */

/* One store that may be a source byte.  The blocks are $80 apart based at $3000 and only
   $03..$4F of each is ever painted, so anything else is not a source. */
REVS_FLAG_OP void view_mark_source(unsigned addr)
{
    unsigned off = (addr & 0xFFFFu) - MEM_view_src_blocks;
    if (off < 40u * 0x80u) {
        unsigned line = off & 0x7Fu;
        if (line - VIEW_SKIP_LINE_LO <= VIEW_SKIP_LINE_HI - VIEW_SKIP_LINE_LO)
            g_viewLineDirty[line] = 1;
    }
}
void view_skip_reset(void);   /* mark everything dirty — the fixture harness's escape hatch */
#define VIEW_MARK_SOURCE(addr) view_mark_source((addr))
#else
#define VIEW_MARK_SOURCE(addr) ((void)0)
#endif

/* ⚠⚠ THE RAM ARM BYPASSES bus_write, SO IT BYPASSES THE INK WATCH TOO — and that made the
   watch answer "nobody writes this cell" about a cell a twin was writing every frame.  Hoisting
   the hardware test out of the loop is the whole point of this seam (CLAUDE.md §bus_read/
   bus_write), so the diagnostic has to be hoisted with it.  Any future choke point that skips
   bus_write must repeat this call, or the instrument silently goes blind on that path. */
REVS_FLAG_OP void seam_write(unsigned addr, int ram, uint8_t value)
{
    if (ram) {
        mem[addr] = value;
        PROBE_SHAPE_MARK(addr);   /* a store here may be a view SOURCE byte (§7h marking) */
        VIEW_MARK_SOURCE(addr);
#ifdef REVS_INK_WATCH
        revs_ink_watch((uint16_t)addr, value);
#endif
    } else {
        bus_write((uint16_t)addr, value);
    }
}

/* ---- the MOS boundary: cpu-free typed wrappers over platform_mos_call_typed ----
 * The register file crosses as a MosRegs value; the wrapper touches no cpu field.
 * ⚠ The differential (validate_native.c) logs each MOS call from the INPUT a/x/y, so
 * every argument must be exactly what the interpreter's LDA/LDX/LDY hands the OS —
 * INCLUDING a register the OS ignores but the harness still compares (OSBYTE 21
 * preserves Y and the log compares it; OSWRCH the same for X/Y).  The one register
 * the harness exempts is OSBYTE &80's entry Y (the ADC reading comes back IN Y).
 * The returned MosRegs is the exit file; a residue-reader shim deposits from it. */
REVS_FLAG_OP MosRegs mos_call(uint16_t entry, uint8_t a, uint8_t x, uint8_t y)
{
    MosRegs in = { a, x, y, 0u };
    return platform_mos_call_typed(entry, in);
}
REVS_FLAG_OP MosRegs mos_osbyte(uint8_t a, uint8_t x, uint8_t y) { return mos_call(0xFFF4u, a, x, y); }
REVS_FLAG_OP MosRegs mos_osword(uint8_t a, uint8_t x, uint8_t y) { return mos_call(0xFFF1u, a, x, y); }
REVS_FLAG_OP void     mos_oswrch(uint8_t a, uint8_t x, uint8_t y) { MosRegs in = { a, x, y, 0u }; platform_mos_call_typed(0xFFEEu, in); }

/* ---- span-plotter descriptors (defined in revs_native.c) ---- */
extern const SpanPlotter SPAN_PLOT_1;
extern const SpanPlotter SPAN_PLOT_2;

/* ---- cpu-free cores the shims call (defined in revs_native.c) ---- */
AdcRead adc_read_core(uint8_t channel);
MosRegs kbd_test_key_regs(uint8_t keyCode);   /* OSBYTE 129 exit file — kbd_test_key's shim reads A/X/Y */
CameraExit apply_driving_model_core(uint16_t heading, int entryC);

/* ⭐⭐ The frame driver's entries into phases 3 and 4, one level below the 6502-ABI INPUT
   marshals: race_main_loop_core runs the two back to back, so each pass's wide inputs are
   already live and mem[] is not the channel between them.  The plain shims are the 6502 ABI
   and keep every marshal; only the driver enters here (src/gen/revs_native.c's phase list). */
void read_driving_controls_frame(void);
void apply_driving_model_frame(void);
void hold_a_for_irq_seam(uint8_t v);
void bearing_to_section_core(uint8_t sectionByte, uint8_t origin);
SignOriginExit build_sign_origin_core(uint8_t offset, uint8_t shift);
GeoExit build_track_geometry_core(uint8_t firstPointSide0, uint8_t firstPointSide1);
/* ⭐⭐ THE _native ENTRIES — a shim minus the marshal-INs that are oracle-only in production.
   Each pair is `void <name>(void)` (the 6502-ABI path `make validate` enters through, which
   still marshals every lane in) and `void <name>_native(void)` (what native callers use, which
   skips the lanes reset_driving_variables' wipes are the only writer of).  Every marshal-OUT and
   every cpu write is on the _native side, so the mem[] mirror `make determinism` compares is
   unchanged.  Reasoning at build_track_geometry in revs_native_seam.c;
   docs/wide-value-cleanup.md §IS THE MARSHALLING ORACLE-ONLY for the measurement. */
GeoExit build_track_geometry_native(void);
void check_crash_native(void);
void draw_dash_needles_native(void);
void mirrors_update_native(void);
void process_car_contact_native(void);
void apply_driving_model_frame_native(void);
void road_edge_walk_resume_native(void);
uint8_t road_edge_walk_resume_from(uint8_t sectionX);   /* $2490 by value */
void    abs8_regs(HookRegs *r);          /* $637C with the file as a value */
void    mul8_noinit_regs(HookRegs *r);   /* $0C02 with the file as a value */
EngineRegs place_player_in_section_native(uint8_t entryX, uint8_t entryY);   /* $4626's entry X/Y — build_track_geometry's exit */
void build_player_car_native(void);

unsigned car_gap_lo_core(uint8_t a, uint8_t b);
GapTail car_gap_tail_core(uint8_t x, uint8_t y, unsigned carryIn);
StageNearbyCar stage_nearby_car_core(uint8_t gapA, unsigned gapFar, uint8_t slot);
void    stage_nearby_car_at_core(uint8_t orderIndex);
void    move_and_draw_cars_core(void);
SlotExit draw_car_field_core(uint8_t entryY, uint8_t entryV, uint8_t entryC);
void check_car_pair_core(void);
void car_order_swap_core(uint8_t xi, uint8_t yi, uint8_t* outX, uint8_t* outY);
void clamp_near_edge_cursor_core(uint8_t candidate);
void clamp_near_edge_window_core(uint8_t nearSlots);
void clear_race_clock_core(uint8_t x);
SlotExit column_gap_walk_core(uint8_t entryX, uint8_t entryY, uint8_t entryV);
void copy_dash_data_core(uint8_t dirFlag);
uint8_t derive_car_section_cursor_core(uint8_t cursor);
SlipRef derive_slip_reference_core(uint8_t axle);
uint8_t draw_gear_indicator_core(void);
SlotExit draw_road_core(uint8_t endCursorFar, uint8_t endCursorNear);
void draw_surface_spans_core(uint8_t pass, uint8_t firstPoint);
SlotExit draw_track_object_core(uint8_t slot, uint8_t entryY, uint8_t entryV, uint8_t entryC);
EdgeOffFlags edge_x_offscreen_core(uint8_t pointX);
uint8_t emit_edge_bearing_at_cursor_core(uint8_t sectionByte);
uint8_t emit_edge_bearing_core(uint8_t slot);
WidthExit emit_edge_width_offset_core(uint8_t sectionByte, uint8_t firstScoringPoint, uint8_t entryV);
SlotExit fill_column_gaps_core(uint8_t pointer, uint8_t branchOffset, uint8_t fallback, uint8_t entryV);
SlotExit fill_dash_edge_columns_core(uint16_t leftStartSrc, uint16_t rightStartSrc);
SlotExit fill_edge_column_run_core(uint8_t firstColumn, uint8_t stopColumn, uint8_t firstLine, uint8_t entryV);
SlotExit fill_line_attr_core(uint8_t bufferLow, uint8_t endCursor, uint8_t firstPoint, int entryC, int entryV);
SlotExit fill_object_gap_core(uint8_t width);
/* ⭐ hypot_max ($7A/$7B) is a mechanism-(B) relocated wide value (see revs_native.c).  A shim
   whose core CONSUMES it marshals the cells in; one whose core PRODUCES it marshals them out —
   the 6502-ABI boundary is the one place a transliterated parent still hands it over in mem[]. */
extern uint16_t edge_nearest_v;  /* edge_nearest ($10/$11) relocated — see revs_native.c */
void edge_nearest_marshal_in(void);
void edge_nearest_marshal_out(void);
extern uint16_t car_heading_v;   /* car_heading ($0A/$0B) relocated — see revs_native.c */
extern uint16_t plot_ptr_v, plot_ptr2_v, plot_ptr3_v;
void plot_ptr_marshal_in(void);   void plot_ptr_marshal_out(void);
void plot_ptr2_marshal_in(void);  void plot_ptr2_marshal_out(void);
void plot_ptr3_marshal_in(void);  void plot_ptr3_marshal_out(void);
void plot_ptrs_marshal_in(void);  void plot_ptrs_marshal_out(void);
void car_heading_marshal_in(void);
void car_heading_marshal_out(void);
/* ⭐ the car-angle array ($62A0-$62A5, plane-split) relocated — see revs_native.c.  A shim whose
   core reads it marshals in; one whose core writes it marshals in AND out, because no writer owns
   all three elements. */
extern uint16_t car_angle_16[3];
void car_angle_marshal_in(void);
void car_angle_marshal_out(void);
/* ⭐ the per-car lap distance ($08D0/$08E8, plane-split, 24 slots) relocated — see revs_native.c.
   ⚠ Its marshal is PER ELEMENT: the boundary shims run once per car, up to twenty times a frame,
   so a whole-array marshal there would cost more traffic than the relocation saves.  The
   whole-array pair is for full_track_scan_rebuild, which walks the field core-to-core. */
/* ⭐ THE DRIVING MODEL'S STATE VECTOR, relocated out of the $62D0/$62E0 plane split.  Fifteen
   16-bit elements: 0/1 the car's VELOCITY IN WORLD AXES and 2 the frame's heading step (0..2
   carry a further 8-bit fraction in mem[MODEL_STATE_FRAC], which stays in mem[]), 3/4/5 their
   rates, 6/7 the same acceleration in the CAR'S OWN axes (lateral, longitudinal), 8 the
   hand-integrated lateral accumulator, 9 the car's signed speed, $0A..$0D two PER-AXLE pairs
   (front = +0, rear = +1) and 14 the per-frame increment.  Marshalled WHOLE at the boundary shims: no
   writer owns a known subset, and a shim entered once a frame can afford 30 bytes.
   ⚠ Two shims import WITHOUT publishing (dial_needle_angle, draw_dash_needles): they read the
   vector to draw the needles and they PLOT, so with a fixture-random plot pointer a line can
   land inside $62D0..$62EE — a whole-array publish would undo a write the routine really made. */
extern uint16_t model_state_16[MODEL_STATE_N];
void model_state_marshal_in(void);
void model_state_marshal_out(void);

/* The named elements, so a site reads as the quantity rather than as an offset.  Every name here
   already exists in mem.h as a lo/hi pair; symbols.csv carries the evidence for each. */
/* ⭐ [MEASURED 2026-09-09] 0/1 and 8/9 are the SAME velocity vector in two frames: |(0,1)| ==
   |(8,9)| frame for frame, and (0,1)'s direction is car_heading exactly while the car runs
   straight and lags it while the car slides.  6/7 is that vector's derivative in the car's axes
   — element 6 is identically zero on a straight — and rotate_state_6_into_3 turns it into 3/4.
   symbols.csv (model_state_lo) carries the runs. */
#define MS_VEL_WORLD_X   0u    /* velocity along view_origin component 0 */
#define MS_VEL_WORLD_Z   1u    /* ...and along component 2 — the pair integrate_car_position moves
                                  the camera by, and the pair rotate_state_0_into_8 resolves */
#define MS_HEADING_STEP  2u    /* heading_step  — the frame's heading increment */
#define MS_RATE_BASE     3u    /* 3/4/5 are the rates of 0/1/2 (integrate_state_rates) */
#define MS_LOAD_LATERAL  6u    /* the acceleration/load pair in the CAR'S axes: across the car... */
#define MS_LOAD_LONG     7u    /* ...and along it; wheel_load is this element's high byte */
#define MS_LATERAL_SPEED 8u    /* car_lateral_speed — the hand-integrated accumulator */
#define MS_SPEED         9u    /* car_speed — the car's SIGNED 16-bit speed */
#define MS_SLIP         10u    /* slip_magnitude, and the base of the per-axle slip cluster:
                                  check_wheel_slip(axle) writes MS_SLIP + axle and MS_SLIP_REF +
                                  axle, axle 0 = FRONT and 1 = REAR (the driven one) */
#define MS_SLIP_REF     12u    /* the second, reference slip term — cleared on the declined arm */
#define MS_INCREMENT    14u    /* the per-frame increment model_integrate_element adds */

/* Where the 6502 genuinely handles ONE LANE of an element — a sign test or a magnitude taken from
   the high byte, or a store that must leave the other lane untouched because an early return can
   happen between the two halves — these name the lane instead of open-coding a shift and a mask.
   Wide arithmetic uses `model_state_16[i]` directly; these are for the byte-shaped cases only. */
static inline uint8_t ms_lo(uint8_t i) { return (uint8_t)model_state_16[i]; }
static inline uint8_t ms_hi(uint8_t i) { return (uint8_t)(model_state_16[i] >> 8); }
static inline void    ms_set_lo(uint8_t i, uint8_t v)
{ model_state_16[i] = (uint16_t)((model_state_16[i] & 0xFF00u) | v); }
static inline void    ms_set_hi(uint8_t i, uint8_t v)
{ model_state_16[i] = (uint16_t)((model_state_16[i] & 0x00FFu) | ((uint16_t)v << 8)); }

/* ⭐ THE PLANE-SPLIT EDGE TABLES, read and written as WHOLE 16-BIT VALUES.  The road pass's three
   edge arrays are all lo/hi plane pairs $50 apart — edge_x ($5E40/$5E90, the point's azimuth),
   edge_opp_x ($5E50/$5EA0, the OPPOSITE boundary's azimuth at the same point) and marker_offset
   ($62B7/$62BA, the corner marker's own offset) — so, as with every plane split, what these
   remove is the lane ARITHMETIC and not the two accesses (the planes are further apart than a
   word, so no word load exists even in principle).
   ⚠ WHERE THE 6502 GENUINELY READS ONE LANE, KEEP READING ONE LANE.  A coarse angle comparison
   ($1B05's surface classifier, the horizon tests, the clip tests) reads only the HIGH byte and
   the low byte is not merely unused, it is not even loaded — writing those as a wide read and a
   shift would ADD an access.  These accessors are for the sites that build or store a whole
   value; `mem[MEM_edge_x_hi + slot]` stays the idiom for a high-byte-only test. */
static inline uint16_t edge_x_word(unsigned slot)
{ return (uint16_t)((unsigned)mem[MEM_edge_x_lo + slot]
                    | ((unsigned)mem[MEM_edge_x_hi + slot] << 8)); }
static inline void edge_x_word_set(unsigned slot, uint16_t v)
{ mem[MEM_edge_x_lo + slot] = (uint8_t)v; mem[MEM_edge_x_hi + slot] = (uint8_t)(v >> 8); }

static inline uint16_t edge_opp_x_word(unsigned slot)
{ return (uint16_t)((unsigned)mem[MEM_edge_opp_x_lo + slot]
                    | ((unsigned)mem[MEM_edge_opp_x_hi + slot] << 8)); }
static inline void edge_opp_x_word_set(unsigned slot, uint16_t v)
{ mem[MEM_edge_opp_x_lo + slot] = (uint8_t)v;
  mem[MEM_edge_opp_x_hi + slot] = (uint8_t)(v >> 8); }

static inline uint16_t marker_offset_word(unsigned slot)
{ return (uint16_t)((unsigned)mem[MEM_marker_offset_lo + slot]
                    | ((unsigned)mem[MEM_marker_offset_hi + slot] << 8)); }
static inline void marker_offset_word_set(unsigned slot, uint16_t v)
{ mem[MEM_marker_offset_lo + slot] = (uint8_t)v; mem[MEM_marker_offset_hi + slot] = (uint8_t)(v >> 8); }

/* object_bearing ($0380/$0398, 24 slots): the 16-bit angle from a viewpoint to the object the
   slot holds — $10000 is a full turn, so it is one value and never two lanes.  Same one-lane
   caveat as the edge tables: the two COARSE heading differences (the collision nudge and the
   wing mirrors' setup) read only the high byte, and they keep doing that. */
static inline uint16_t object_bearing_word(unsigned slot)
{ return (uint16_t)((unsigned)mem[MEM_object_bearing_lo + slot]
                    | ((unsigned)mem[MEM_object_bearing_hi + slot] << 8)); }
static inline void object_bearing_word_set(unsigned slot, uint16_t v)
{ mem[MEM_object_bearing_lo + slot] = (uint8_t)v;
  mem[MEM_object_bearing_hi + slot] = (uint8_t)(v >> 8); }

/* ⭐ THE VIEW ORIGIN, relocated out of the $6280/$6283 plane split.  Every bearing and every
   projection in the frame is measured from it, and there are TWO of them: origin 0 is the camera,
   origin 6 the road sign's own viewpoint (build_sign_origin derives it).  Each holds three
   16-bit components.
   ⭐ INDEXED BY THE 6502'S OWN BYTE OFFSET — `origin + component`, exactly as the addressing
   modes write it — so every converted site keeps its index expression verbatim and no site has to
   remap.  That leaves elements 3..5 UNUSED and unmarshalled, and they must stay that way: their
   "low" bytes ($6283..$6285) are elements 0..2's HIGH bytes, so marshalling them would alias two
   different values onto the same cells.  Three wasted words buy a mechanical substitution.
   ⚠ Components 0 and 2 of origin 0 carry a further 8-bit fraction in mem[VIEW_ORIGIN_FRAC]
   ($62B1), which stays in mem[]: a camera component is the top 16 bits of (element << 8) | frac. */
extern uint16_t view_origin_16[9];
void view_origin_marshal_in(void);
void view_origin_marshal_out(void);

extern uint16_t car_distance_16[24];
void car_distance_marshal_in_one(uint8_t x);
void car_distance_marshal_out_one(uint8_t x);
void car_distance_marshal_in(void);
void car_distance_marshal_out(void);
void hypot_max_marshal_in(void);
void hypot_max_marshal_out(void);
/* ⭐ hypot_min ($78/$79) — the sibling pair, marshalled at the same seams.  Its consumer
   point_distance_hypot produces it back CONDITIONALLY, so it needs IN as well as OUT. */
void hypot_min_marshal_in(void);
void hypot_min_marshal_out(void);

/* ⭐ bearing ($8A/$8B) is a mechanism-(B) relocated wide value too (see revs_native.c), and the
   same IN/OUT rule applies.  Its shipping reader is the transliterated FUN_2a5f. */
void bearing_marshal_in(void);
void bearing_marshal_out(void);
void lateral_speed_entry_marshal_out(void);

/* ⭐⭐ Validation-harness only: scribble every relocated global so neither model in diff_run can
   inherit the other's marshal.  See the banner in revs_native.c. */
void relocated_poison(void);
AddFlags integrate_car_position_core(void);
AddFlags integrate_state_rates_core(void);
EdgeIndices interp_edge_core(uint8_t styleIndex, uint8_t farPoint, uint8_t nearPoint, int publishOnly);
int kbd_test_key_core(uint8_t keyCode);
uint8_t limit_steer_demand_core(uint8_t a, int carryIn);
void load_section_triple_core(uint8_t destSection, uint8_t segmentByte);
SlotExit mark_line_surfaces_core(uint8_t surfaceClass, uint8_t firstPoint, int entryV);
Mode5Addr mode5_addr_core(uint8_t quarterOffset, uint8_t y);
Mode5Addr mode5_addr_for_cell_core(uint8_t column, uint8_t y);
AddFlags model_integrate_element_core(uint8_t slot);
Mul8AccumExit mul8_accum_core(void);
Wide16Exit    mul16_by_1_5_core(uint16_t x);
ContactExit note_object_contact_core(uint8_t threshold, uint8_t entryC);
#define SOUND_SLOT_IMPACT 0x04u  /* the bang: the scrape arm, the crash arm and begin_spin */

/* check_crash_core's three arms — which tail the routine took, and so which exit ABI. */
#define CRASH_ARM_NONE   0u    /* still on the track: it did nothing */
#define CRASH_ARM_SCRAPE 1u    /* the scrape: exit ABI is sound_queue_default's */
#define CRASH_ARM_FULL   2u    /* into the fence: engine off, driving model zeroed */
uint8_t check_crash_core(uint8_t savedX);
void    begin_scrape_core(uint8_t yawKick, uint8_t savedX);
void    sound_stop_all_core(uint8_t ambientY);
void    build_player_car_core(void);
void    step_delta_halve_core(void);
uint8_t place_car_world_coords_core(uint8_t slot, uint8_t sectionCursor);
void    project_object_slot_core(uint8_t coordIndex, uint8_t shape);
uint8_t paint_fence_backdrop_core(uint8_t horizon);
SlotExit plot_view_src_line_core(uint8_t mode, uint8_t colourSelect);
uint8_t point_distance_hypot_apply(void);
void poll_steering_assist_core(void);
ProjPoint project_point_core(uint8_t sectionByte, uint8_t origin);
uint8_t race_main_loop_core(RestartDepth depth);
/* The race, entered natively: the $16DC entry contract plus the core, returning the exit
   carry enter_session hands to abort_to_front_end.  race_main_loop() is the 6502-ABI shim
   around it and exists for the oracle. */
uint8_t race_main_loop_session(void);
void rebase_edge_point_core(uint8_t slot);
RejectExit reject_object_slot_core(void);
RoadSide road_edge_side_apply(uint8_t sideSelect);
void road_edge_start_core(uint8_t nearSlotCount, uint8_t halfStride, uint8_t scratchSection, uint8_t pointLimit, uint8_t staleHorizonCap);
uint8_t road_edge_walk_core(uint8_t firstPoint, uint8_t sectionIndex, uint8_t midSlot, uint8_t pointCap, uint8_t offAxis);
uint8_t road_edge_walk_resume_core(uint8_t section, uint8_t midSlot, uint8_t pointCap, uint8_t offAxis);
uint8_t horizon_half_width_at_core(unsigned horizonPoint, uint8_t sectionX);
uint8_t scale_by_track_gradient_tail_core(uint8_t value, int negative);
void    scale_by_track_gradient_regs(HookRegs *r);   /* $4610 with the file as a value */
void    scale_by_track_gradient_tail_regs(HookRegs *r);  /* $461B, ditto — for the $57BB seam */
int road_span_advance_core(uint8_t y);
AddFlags rotate_velocity_by_steer_core(void);
AddFlags rotate_pair_a_by_steer_core(void);
void section_coord_add_delta_core(uint8_t dst, uint8_t src, const uint8_t dlo[3], const uint8_t dhi[3]);
SlotExit plot_object_core(uint8_t slot, uint8_t entryY, uint8_t entryV);
void build_section_step_delta_core(uint8_t y);
void copy_section_height_to_side1_core(uint8_t x);
void load_section_from_segment_core(uint8_t x, uint8_t y);
void shift_near_edge_points_core(uint8_t topSlot, uint8_t wrapSlot, uint8_t lowTop, uint8_t nearSlots);
MosRegs sound_osword_core(uint8_t oswordNum, uint8_t blockLow);
MosRegs sound_envelope_core(uint8_t envBase, uint8_t savedX);
void    full_track_scan_rebuild_core(uint8_t retreatDepth);
void    reset_driving_variables_core(void);
void    reject_all_object_slots_core(void);
/* $0FFE update_lap_timers — the ambient OSWRCH cursor, the two inputs it does not compute.  (The
   third, the ambient D/I bits, went with the $1017 PHP under THE RESULTS RULE.) */
void    update_lap_timers_core(uint8_t ambX, uint8_t ambY);
void    enter_mos_text_mode_core(void);
void    irq1v_release_core(uint8_t ambientY);
/* $4E5C — the raster-band schedule, cpu-free.  Its 6502-ABI entry (the IFR test, the chain-on,
   the X save and the PLA/TAX/LDA $FC/RTI exit contract) is the ISR seam in revs_native_seam.c. */
void    irq1v_band_schedule_core(void);
uint8_t sound_queue_core(uint8_t slot, uint8_t amplitude, uint8_t savedX);
SlotExit engine_sound_update_core(uint8_t entryX, uint8_t entryY,
                                  unsigned entryV, unsigned entryC, int* pushedPitch);
/* The $0B4D `ADC #$10` block-index carry and overflow — a plain binary add (D = 0), so C is the
   unsigned carry and V the signed overflow.  Shared because three cores in revs_native.c thread
   these out as their exit C/V and `sound_queue_exit_abi` (revs_native_seam.c) replays them into
   cpu.  `static inline` rather than a cross-TU call: the slot is a literal at every call site,
   so both sides fold it to two constants. */
typedef struct { uint8_t c, v; } BlockCV;
static inline BlockCV sound_queue_block_cv(uint8_t slot)
{
    unsigned s   = (uint8_t)(slot << 3);
    unsigned sum = s + 0x10u;
    BlockCV r;
    r.c = (uint8_t)(sum > 0xFFu);
    r.v = (uint8_t)(((~(s ^ 0x10u)) & (s ^ sum) & 0x80u) ? 1u : 0u);
    return r;
}

void sound_queue_exit_abi(uint8_t slot);
uint8_t sound_stop_channel_core(uint8_t chan, uint8_t ambientY);
int state_flags_bit6(void);
void store_slip_clamped_core(uint8_t valueHi);
void store_slip_clamped_off_throttle_core(uint8_t valueHi);
void store_slip_exit_abi(uint8_t sign);
void store_slip_signed_core(uint8_t valueHi);
/* $1EAB's body -- cpu-free; `surface_colour_at` in revs_native_seam.c replays the exit ABI. */
SlotExit surface_colour_at_line_core(uint8_t line, uint8_t entryX, uint8_t entryV);
CameraExit update_camera_and_drive_state_core(void);
EngineExit update_engine_revs_core(uint8_t carryIn, uint8_t entryY);
void update_grip_limits_core(void);
uint8_t print_spaces_core(uint8_t count, uint8_t x, uint8_t y);
int draw_starting_lights_core(void);
void draw_corner_marker_core(uint16_t offset, uint16_t edgeX, uint8_t edgeY, CornerMarker *out);
void mirrors_update_setup_core(uint8_t slotFlag, uint8_t objWidth, uint8_t bearingHi, uint8_t carHeadingHi, MirrorSetup *out);
void dial_needle_angle_core(uint8_t engineRevs, NeedleDial *out);
void driver_name_address_core(uint8_t index, NamePtr *out);
void draw_dash_needle_core(uint16_t steer, DashNeedle *out);
void menu_draw_gfx_bars_core(void);
void plot_line_octant_core(uint8_t entryScanline);
void undraw_plot_lines_core(void);   /* twin #165b — replay the plotter's undo list */
void mirror_draw_car_core(uint8_t lowerBound, uint8_t segment);  /* twin #165c */
void mirror_draw_car_core(uint8_t lowerBound, uint8_t segment);  /* twin #165c */
/* ⭐ returns the script offset Y ended on: print_standings_table hands it straight on as the
   ambient OSWRCH register, so it is a real result, not residue (twin #199). */
uint8_t text_script_interp_core(uint8_t tableIdx);
uint8_t menu_wait_key_core(uint8_t count);
typedef struct { uint8_t a, c, z, n, mathlo, writeMathlo; } ParseNum;
void parse_two_digit_ascii_core(uint8_t char0, uint8_t char1, ParseNum *out);
uint8_t console_read_two_digits_core(void);
void prompt_wing_settings_core(void);
uint8_t seed_car_track_position_core(uint8_t x, uint8_t entropy, uint8_t *mathlo_out);
/* $0B77 scale_wing_settings — the drag coefficient plus the closing `ADC #$3C`'s C and V, which
   are the frame body's ambient carry/overflow at its very first call ($1701). */
typedef struct { uint8_t drag, c, v; } WingScaleExit;
WingScaleExit scale_wing_settings_core(void);
int update_horizon_band_core(uint16_t *r_out, uint8_t *mathhi_out);
/* The number/name printers ($3250/$37D0/$37D6/$7B9C).  TextChar is what the shared $5092
   dispatch returns; TextExit adds the carry the BCD printer's field-width shift produces. */
typedef struct { uint8_t a, n, z; }    TextChar;
typedef struct { uint8_t a, n, z, c; } TextExit;
uint8_t  emit_driver_name_core(uint16_t ptr, uint8_t x);
/* The dashboard readouts that drive those printers ($65C8/$501D/$502D/$502F/$6673/$667B/$1B84). */
typedef struct { uint8_t a, n, z, v, c; }    BcdExit;
typedef struct { uint8_t a, x, n, z, c; }    NameExit;
typedef struct { uint8_t a, x, y, n, z, c; } PosDisplayExit;
BcdExit        position_to_bcd_core(uint8_t index);
TextExit       print_time_row21_core(uint8_t fieldMask, uint8_t y);
TextExit       show_lap_time_lower_core(uint8_t y);
TextExit       show_lap_time_lines_core(uint8_t y);
NameExit       print_driver_name_by_order_core(uint8_t orderPos);
NameExit       print_driver_name_at_row_core(uint8_t row, uint8_t orderPos);
PosDisplayExit update_position_display_core(uint8_t entryX, uint8_t entryY);

/* $17C3 add_frame_time — exit A/Y depend on the overflow arm; the flags are the LOW byte's add,
   which the routine's own PHP/PLP carries past the two higher bytes. */
typedef struct { uint8_t a, y, n, z, v, c; } FrameTimeExit;
FrameTimeExit add_frame_time_core(uint8_t clockIdx);

/* $5052 tick_race_timers — no inputs.  ⭐ It used to take four ambient flag bits and forward
   them two calls down to `seed_car_track_position`'s $6362 `PHP` residue, the only thing in the
   frame that could see them; that byte is stack residue below SP and is gone. */
void tick_race_timers_core(void);

/* $635D seed_car_track_position — cpu-free.  Returns the decremented car-index cursor (the
   exit ABI's X); the $6362 PHP residue it used to compose is gone (the argument is at the body). */
uint8_t seed_car_track_position_next(void);
void shift_key_commands_core(uint8_t entryY);
uint8_t retire_car_core(uint8_t x);
/* $11AB spin_car_out — the slot as an argument; returns retire_car's lap comparison, or -1
   on the scenery-slot exit, where the 6502 leaves the flags alone. */
int spin_car_out_core(uint8_t x);
void finish_race_core(void);
TextExit print_bcd_digits_core(uint8_t bcd, uint8_t x, uint8_t y);
TextExit print_bcd_digits_at_core(uint8_t bcd, uint8_t column, uint8_t row);
TextExit print_lap_time_core(uint8_t fieldMask, uint8_t carIdx, uint8_t y);
/* $43D0 / $43E7 the lap-value table column (twin #196) — exit is always print_spaces'. */
TextExit print_lap_value_field_core(uint8_t x, uint8_t y);
TextExit print_lap_value_from_mid_core(uint8_t bcd, uint8_t x, uint8_t y);
/* $3261 / $34D0 / $34D2 the abort poll and the dismiss-key waiters (twin #198) — result-only;
   every call site reloads its registers immediately, so the exit ABI is dead. */
void abort_if_quit_keys_core(void);
void wait_dismiss_space_core(void);
void compute_segment_scale_core(uint8_t trackClass);
void reset_all_cars_for_session_core(uint8_t startCar);
void sort_cars_by_key_core(uint8_t sel);
void car_reset_best_lap_core(uint8_t car);
void all_cars_reset_best_lap_core(void);
BcdAdd add_tally_to_lap_total_core(uint8_t column, uint8_t car);
void prompt_driver_ready_core(void);
void read_driver_name_core(void);
void wait_dismiss_key_core(uint8_t offerReturn);
/* $3E60 / $3C6F the standings table's leaf callees (twin #197) — result-only. */
void set_row_rule_glyphs_core(uint8_t row);
void print_race_class_name_core(void);
/* twin #199 — the standings/results page.  X = layout variant, A = mode; both result-only. */
void select_text_variant_core(uint8_t variant);
void print_standings_table_core(uint8_t variant, uint8_t mode);
uint8_t tally_bcd_column_core(uint8_t column);
void abort_to_front_end_core(int carry);
void engine_init_core(void);
void hw_init_core(uint8_t osbyteY);
void engine_main_core(void);
void advance_player_section_core(uint8_t entryX, uint8_t entryY);
void clear_surface_buffers_core(void);
void fill_line_surface_core(void);
uint8_t console_io_core(uint16_t field, uint8_t width);
void build_section_ahead_core(void);
void rebuild_walk_reversed_core(uint8_t count);
void rebuild_walk_backward_core(void);
void reverse_walk_direction_core(void);
void enter_session_core(uint8_t kind);
void enter_practice_session_core(void);
void front_end_menus_core(void);
uint8_t print_message_at_row_core(uint8_t row, uint8_t script);
uint8_t print_message_lower_row_core(uint8_t script);
uint8_t print_message_upper_row_core(uint8_t script);
uint8_t print_message_pair_core(uint8_t script);
void select_text_variant(void);
void print_standings_table(void);
uint8_t vdu_char_def_core(uint8_t ch);
uint8_t vdu_char_emit_core(void);
uint8_t vdu_char_wide_core(uint8_t ch);
void view_paint_lines_core(unsigned screenBase, unsigned firstLine, uint8_t entryCell);
SlotExit write_object_slot_core(uint8_t projectedLine, uint8_t entryX, uint8_t entryV, uint8_t entryC);

/* per-circuit hook twins (see revs_native.c's PER-CIRCUIT HOOK TWINS section) */
extern int g_hookOracle;              /* 0 = twins, non-zero = the transliterated bodies */
uint8_t hook_horizon_clamp_core(uint8_t entryY, uint8_t entryX);
uint8_t hook_record_horizon_core(uint8_t line, uint8_t point);
void hook_record_horizon(HookRegs *r);
void hook_edge_walk_limit(HookRegs *r);
void hook_walk_back_gate(HookRegs *r);
typedef struct { uint8_t a; uint8_t v; } HookMergeExit;
HookMergeExit hook_merge_horizon_edges_core(uint8_t point, uint8_t horizonLine,
                                           int clearStyleBelow6);
void hook_merge_horizon_edges(HookRegs *r);
void hook_step_gen_cursor_core(uint16_t block);
void hook_step_gen_cursor_a(HookRegs *r);
void hook_step_gen_cursor_b(HookRegs *r);
void hook_step_dir_gen_cursor_a(HookRegs *r);
void hook_step_dir_gen_cursor_b(HookRegs *r);
void hook_horizon_clamp(HookRegs *r);
void hook_steer_response_brands(HookRegs *r);           /* Brands Hatch $57A1 */
void hook_steer_response_oulton(HookRegs *r);           /* Oulton Park $57A1 */
void hook_steer_response_snetter(HookRegs *r);          /* Snetterton $57A1 */
void hook_steer_response_doning(HookRegs *r);           /* Donington Park $5779 */
void hook_camera_scale_by_gradient(HookRegs *r);        /* $45CB — four circuits */
void hook_span_cap_slot_test(HookRegs *r);
void hook_merge_horizon_edges_nurburg(HookRegs *r);     /* Nurburgring $5772 */
void hook_horizon_store_only(HookRegs *r);              /* Donington Park $5772 */
void hook_horizon_half_width_abs_doning(HookRegs *r);   /* Donington Park $57B6 */
void hook_section_ahead_doning(HookRegs *r);            /* Donington Park $53E9 */
void hook_steer_response_nurburg(HookRegs *r);          /* Nurburgring $59D9 */              /* $2F23 — Donington + Snetterton */
void hook_horizon_clamp_guarded_snetter(HookRegs *r);   /* Snetterton  $56C8 */
void hook_horizon_clamp_guarded_nurburg(HookRegs *r);   /* Nurburgring $56C4 */

/* ---- what the ORACLE-ONLY 6502-ABI SHIMS need from here ------------------------------
 * ⭐ src/gen/revs_native_abi.c holds every `void <name>(void)` shim with no native caller
 * (its header carries the why, and `make cpu-lint` is what keeps them out of revs_native.c).
 * Three kinds of thing had to come out of that file's private scope for the move:
 *
 * 1. The cell aliases below — here, not duplicated, because both files name the same cells.
 * 2. The two shallow span arms, which the shims pass by address.  Both keep their `const`
 *    and their initialisers stay in revs_native.c, so the constant-folding the span
 *    rasteriser depends on is unaffected inside that TU.
 * 3. Twenty cores that were file-static.  ⚠⚠ GCC inlined EVERY one of them away — none has
 *    a symbol in amiga/obj/revs_native.o — so giving them external linkage forces them into
 *    existence as real out-of-line functions.  That cost was measured either side of the
 *    move and is recorded in docs/native-sweep.md.  Nothing but the shims should call them
 *    across a TU boundary: a native caller belongs next to the core. */
#define MUL_SRC_LO     MEM_point_delta_lo   /* point_delta_lo[0] — multiplicand low  (two's complement) */
#define MUL_SRC_HI     (MEM_point_delta_lo + 1u)   /* point_delta_lo[1] — multiplicand high; bit 7 is its sign */
#define MUL_TERM_LO    (MEM_point_delta_lo + 2u)   /* point_delta_lo[2] — multiplier low; the car angle, bit 0 = SIGN */
#define MUL_TERM_HI    MEM_point_delta_hi   /* point_delta_hi[0] — multiplier high (heading_sin/heading_cos) */
#define MUL_SIGN       MEM_hypot_min_hi   /* hypot_min_hi — product-sign accumulator (bit 7) + apply_angle_term's store/accumulate mode (bit 6) */
#define SLIP_OUT_INDEX   MEM_hypot_min_lo  /* hypot_min_lo — here WHICH element the store lands in */
#define SLOT_MARKER_P1     0x2FC0u
#define SLOT_MARKER_P2     0x2FD7u

extern const SpanArm ARM_SHALLOW_FWD;
extern const SpanArm ARM_SHALLOW_REV;
/* ⚠⚠ NOT the span leaves themselves.  `span_plot_core` and `span_walk` are
   `static inline __attribute__((always_inline))` in revs_native.c and MUST STAY that way: the
   SpanPlotter/SpanArm descriptor is a compile-time constant at every native call site, and
   letting it become a memory operand in the inner loop cost 2.6% of the frame when it was
   measured (docs/perf-method.md §twins #25-#39).  These two are out-of-line wrappers that exist
   only so the oracle shims can reach them across the TU boundary; the native path never calls
   them and keeps inlining as before. */
SlotExit plot_shape_edges_core(void);
uint8_t track_pos_advance_core(uint8_t x);
uint8_t track_pos_retreat_core(uint8_t x);
extern const SpanArm ARM_STEEP_FWD;
extern const SpanArm ARM_STEEP_REV;
void store_object_flags_core(uint8_t y, uint8_t a);
void steer_demand_store_core(uint8_t a);
void clamp_and_store_steer_angle_core(uint8_t a);
void span_plot_oracle(const SpanPlotter *p, uint8_t column, uint8_t *y, unsigned *carry,
                      int *abandoned);
void span_walk_oracle(const SpanArm *arm, uint8_t phase, uint8_t startLine);
void span_end_marker(unsigned slot, const uint16_t *ptr, uint8_t y, uint8_t *colMark, unsigned *carry);
/* ⚠ the ORACLE entries, not the cores: div16by8_core and apply_angle_term_core stay `static`
   so their hot native callers keep inlining them (see the note at div16by8_core_oracle). */
Div16By8 div16by8_core_oracle(uint16_t dividend, uint8_t divisor);
void apply_angle_term_core_oracle(uint8_t dest, uint8_t angle, uint8_t source);
void add_signed_into_element_core(uint8_t slot, uint8_t signByte);
void apply_angle_term_at_core(uint8_t mode, uint8_t angle);
void rotate_state_pair_core(uint8_t dest, uint8_t source, uint8_t mode);
void slip_magnitude_core(uint8_t slot);
void check_wheel_slip_core(uint8_t axle);
void clamp_slip_to_grip_core(uint8_t axle);
void update_slip_sound_core(uint8_t axle, uint8_t ambientY);
void compute_car_angles_core(uint16_t heading);
SpinExit begin_spin_from_a_core(uint8_t severity, uint8_t savedX);
uint8_t scale_angle_in_section_core(uint8_t a, uint8_t y);
void apply_steer_demand_core(uint8_t signByte);
uint8_t car_index_dec_core(uint8_t x);
uint8_t car_index_inc_core(uint8_t x);
uint8_t find_player_neighbours_core(void);
void lap_complete_core(uint8_t x);
int record_section_jump_core(int carry_in, uint8_t x);
SlotExit scale_shape_vectors_core(uint8_t entryV);
uint8_t section_angle_curve_core(uint8_t a);
void steer_assist_dispatch_core(uint8_t demand);

#endif /* REVS_NATIVE_SEAM_H */
uint8_t hook_next_section_cursor_core(uint16_t genBlock);
void hook_next_section_cursor_a(HookRegs *r);
void hook_next_section_cursor_b(HookRegs *r);
/* $5472's vector: the two ground-plane components (signed), what the $5493 TAX leaves in X,
   and the C and V standing at the first PHP (V is always 0 there — see the twin). */
typedef struct { uint8_t compA, compB, cosI, c, v; } GenDirVector;
GenDirVector hook_gen_dir_vector_core(uint16_t block);
void hook_gen_dir_vector_brands(HookRegs *r);
void hook_gen_dir_vector_oulton(HookRegs *r);
void hook_gen_dir_vector_snetter(HookRegs *r);
void hook_gen_dir_vector_doning(HookRegs *r);
void hook_gen_dir_vector_nurburg(HookRegs *r);
void hook_gen_step_brands(HookRegs *r);
void hook_gen_step_oulton(HookRegs *r);
void hook_gen_step_snetter(HookRegs *r);
void hook_gen_step_doning(HookRegs *r);
void hook_gen_step_nurburg(HookRegs *r);
void hook_seg_advance_brands(HookRegs *r);
void hook_seg_advance_oulton(HookRegs *r);
void hook_seg_advance_snetter(HookRegs *r);
void hook_seg_advance_doning(HookRegs *r);
void hook_seg_advance_nurburg(HookRegs *r);

/* $5672 — seed the generator at a section boundary (twin #225) */
void hook_gen_seed_brands(HookRegs *r);
void hook_gen_seed_oulton(HookRegs *r);
void hook_gen_seed_snetter(HookRegs *r);
void hook_gen_seed_doning(HookRegs *r);
void hook_gen_seed_nurburg(HookRegs *r);

/* $5A1B — step the generator's cursor, then rebuild its direction vector (twin #226) */
void hook_advance_gen_place_brands(HookRegs *r);
void hook_advance_gen_place_oulton(HookRegs *r);
void hook_advance_gen_place_snetter(HookRegs *r);
void hook_advance_gen_place_doning(HookRegs *r);
void hook_advance_gen_place_nurburg(HookRegs *r);

/* The three cross-circuit one-line hook bodies (twins #227-#229) */
void hook_horizon_half_width_scale(HookRegs *r);
void hook_abs_by_track_direction(HookRegs *r);
void hook_scale_entry_by_gradient(HookRegs *r);
