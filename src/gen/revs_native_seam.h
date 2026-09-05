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
#include "revs_decl.h"
#include "mem.h"
#include "../platform/platform_c.h"
#include "../platform/bbc_screen.h"
#include "../platform/probe.h"
#include "../platform/shape.h"
#include "../platform/revs_plot.h"

/* ---- address constants the shims use (copied from revs_native.c; identical) ---- */
#define EDGE_HALF        0x0028u   /* 40 — the stride between the two road sides' halves */
#define SECTION_MID      0x00FAu   /*   ...the triple road_edge_walk interpolates midpoints into */
#define SECTION_NEAR     0x00FDu   /*   ...and the one road_edge_start stages the near point in */
#define VIEW_LEFT_START_SRC   0x0504u   /* per scan line: the LEFT run's first source byte */
#define VIEW_RIGHT_START_SRC  0x4400u   /* ...and the RIGHT run's */
#define DASH_BLOCK_STARTS 0x3900u   /* per block: the offset its live data begins at (< $4F) */
#define DASH_BLOCK_COUNT  0x29u     /* 41 blocks */
#define POINT_DELTA_HI    0x0083u  /* point_delta_hi[0..2]   — ...its magnitude's high byte */
#define SLIP_MAG_HI      0x008Fu  /* plot_ptr3_hi — ...and its high byte (docs/rename.md) */
#define SLIP_SIGN        0x0079u  /* hypot_min_hi — here the sign byte abs16_math branches on */
#define CAR_STATE_1    0x0164u   /* per-driver; the camera adds a gradient-scaled copy */
#define FENCE_COL_COUNT        0x28u    /* 40 view columns                                    */

/* ---- SoA array bases (Step 0 of the wide-value cleanup: one home each, was duplicated
 *      across revs_native.c; the single swap point when a base becomes a value_16[N]) ---- */
#define MODEL_STATE_LO   0x62D0u   /* the driving model's 16-bit state vector, low bytes */
#define MODEL_STATE_HI   0x62E0u   /* ...and high bytes; element i is +i in each */
#define MODEL_STATE_N    15u       /* ⚠ FIFTEEN elements, 0..14: $62DF is loop_counter_hi and
                                    * $62EF is a separate cell, so the vector stops at 14.
                                    * ⭐ RELOCATED to model_state_16[]: these two bases are now
                                    * the marshals' addresses only. */
#define CAR_ANGLE_LO     0x62A0u   /* car-angle array: heading_sin/heading_cos/steer_angle low; bit0 = SIGN */
#define CAR_ANGLE_HI     0x62A3u   /* ...and their high bytes.  ⭐ RELOCATED to car_angle_16[]:
                                    * these two are now the marshals' addresses only — no twin
                                    * reaches the array through mem[] any more. */
/* Which element is which (see car_angle_16 in revs_native.c for the sign-magnitude packing). */
#define CAR_ANGLE_SIN    0u        /* SIN(car_heading)  — compute_car_angles' output */
#define CAR_ANGLE_COS    1u        /* COS(car_heading)  — ...and its second pass */
#define CAR_ANGLE_STEER  2u        /* the steering angle — the control read owns it */
#define VIEW_ORIGIN_LO   0x6280u   /* view_origin_lo — 3 components, STRIDE 6, two origins */
#define VIEW_ORIGIN_HI   0x6283u   /* view_origin_hi */
#define EDGE_X_LO_TBL    0x5E40u   /* edge_x_lo — the track edges' angle, low byte */
#define EDGE_X_HI_TBL    0x5E90u   /* edge_x_hi — ...and the high byte */
#define EDGE_Y_TBL       0x5F20u   /* edge_y      — per edge point: the scan line it projects to */
#define MARKER_EDGE_IDX  0x62B4u   /* marker_edge_index  — 3 corner markers, per frame */
#define MARKER_FLAGS_TBL 0x6299u   /* marker_flags */
#define MARKER_OFF_LO    0x62B7u   /* marker_offset_lo */
#define MARKER_OFF_HI    0x62BAu   /* marker_offset_hi */
#define TRACK_DIR_0      0x5400u   /* track_dir_0[Y] — direction component 0 (ground plane) */
#define TRACK_DIR_1      0x5500u   /* track_dir_1[Y] — component 1 (gradient) */
#define TRACK_DIR_2      0x5600u   /* track_dir_2[Y] — component 2 (ground plane) */
#define SURFACE_COLOURS_TBL 0x38FCu /* surface_colours — four MODE 5 colour bytes */
#define COLOUR_PATTERN_AND  0x337Cu /* colour_pattern_and_tbl */
#define COLOUR_PATTERN_KEEP 0x33FCu /* colour_pattern_keep_tbl */
#define CAR_FLAGS_SHAPE  0x018Cu   /* per slot: flags, with the object's shape in bits 0-3 */
#define CAR_ORDER        0x013Cu   /* the 20-entry sorted car order */
#define OBJECT_WIDTH     0x03C8u   /* per slot: object screen width in pixels */
#define OBJECT_BEARING_HI 0x0398u  /* per slot: object bearing high byte */
#define MIRROR_SEG_BEARING_TBL 0x3BA4u /* 6 wing-mirror segment heading thresholds */
#define MIRROR_SEG_STATE 0x6293u   /* per wing-mirror segment: last-drawn bottom line, 0 = erased */
#define DIAL_NEEDLE_DDA_TBL       0x3100u /* rev-needle DDA len/delta per angle offset (0..0x13) */
#define DIAL_NEEDLE_ORIGIN_LO_TBL 0x32FCu /* rev-needle origin addr low per quadrant; &F8=ptr, &7=line */
#define DIAL_NEEDLE_ORIGIN_HI_TBL 0x397Cu /* rev-needle origin addr high per quadrant (all $75) */
#define STEER_NEEDLE_DDA_TBL      0x3980u /* steering-wheel needle minor-axis delta per angle index */
#define MENU_SCREEN_BASE   0x7C00u /* front end: $7C00-$7FFF as the MODE 7 teletext page (== TT_SCREEN_BASE; time-multiplexed with the race view's view_cell_chain_a) */
#define MENU_BAR_START_TBL 0x3A6Fu /* menu_draw_gfx_bars: per-row start column of the two graphics bars */
#define MENU_BAR_END_TBL   0x3A71u /* ...and end column */
#define CAR_TRACK_POSITION 0x0128u /* seed_car_track_position: per-car track position (20 entries) */
#define CAR_GRID_BASE      0x04A0u /* per-car grid base row = car index >> 1 */
#define CAR_SEED_INDEX     0x004Au /* car-index cursor for the grid-seeding loop */
#define USRVIA_T2CL        0xFE68u /* User VIA Timer 2 counter low — free-running entropy source */

/* ---- exit-struct typedefs (moved out of revs_native.c) ---- */
typedef struct { uint8_t a, x, y, n, z, v, c; } SlotExit;
typedef struct { uint8_t val, carry; } Adc;
typedef struct {
    unsigned byte;   /* A: the pixel byte the chain carries left to right */
    unsigned line;   /* X: the scan line being painted */
    unsigned cell;   /* Y: the cell's byte offset within the line, or a glyph index */
} ViewState;
typedef enum {
    RESTART_NONE = 0,   /* $16F9 — back from the pits: keep the session exactly as it was */
    RESTART_LATE,       /* $16F6 — rebuild the player's car and the driver tables only */
    RESTART_MID,        /* $16F3 — and zero $00-$68 plus $6280-$62FF: a fresh lap */
    RESTART_FULL        /* $16EE — and reset the player's race clock: a fresh session */
} RestartDepth;
typedef enum {
    LOOP_NEXT_FRAME,    /* $17B7 — round again */
    LOOP_RESTART,       /* leave the frame loop and re-run the reset to `g_restartDepth` */
    LOOP_FINISHED       /* $17BA — the session is over; leave the routine */
} LoopVerdict;
typedef struct { uint8_t line; int clip; int behind; } ProjPoint;
typedef struct { uint8_t hi, carry, overflow, neg, zero; } AddFlags;
typedef struct { AddFlags tail; uint8_t x, y; } EngineExit;
typedef struct { uint8_t x, y; } EngineRegs;
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
typedef struct { uint8_t lo, hi, scratch; } NamePtr;

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
#define REVS_FLAG_OP static inline __attribute__((always_inline))
REVS_FLAG_OP unsigned load_a(uint8_t value)
{
    LDA(value);
    return cpu.A;
}

REVS_FLAG_OP unsigned adc_step(unsigned a, uint8_t addend, int carry_in)
{
    cpu.A = (uint8_t)a;
    cpu.C = (uint8_t)(carry_in != 0);
    ADC(addend);
    return cpu.A;
}

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

REVS_FLAG_OP uint8_t adc_overflow(uint8_t a, uint8_t m, unsigned carryIn)
{
    unsigned t = (unsigned)a + m + (carryIn ? 1u : 0u);
    return (uint8_t)(((~(a ^ m) & (a ^ (uint8_t)t)) >> 7) & 1u);
}

REVS_FLAG_OP uint8_t sbc_overflow(uint8_t a, uint8_t m, unsigned carryIn)
{
    uint8_t r = (uint8_t)(a - m - (carryIn ? 0u : 1u));
    return (uint8_t)((((a ^ m) & (a ^ r)) >> 7) & 1u);
}

REVS_FLAG_OP unsigned sub_from(unsigned value, uint8_t subtrahend)
{
    cpu.A = (uint8_t)value;
    cpu.C = 1;
    SBC(subtrahend);
    return cpu.A;
}

REVS_FLAG_OP unsigned sbc_step(unsigned value, uint8_t subtrahend, int carry_in)
{
    cpu.A = (uint8_t)value;
    cpu.C = (uint8_t)(carry_in != 0);
    SBC(subtrahend);
    return cpu.A;
}

REVS_FLAG_OP int cmp_ge(unsigned value, uint8_t limit)
{
    cpu.A = (uint8_t)value;
    CMP(limit);
    return cpu.C;
}

REVS_FLAG_OP int cpx_ge(unsigned value, uint8_t limit)
{
    cpu.X = (uint8_t)value;
    CPX(limit);
    return cpu.C;
}

REVS_FLAG_OP unsigned zp_pointer(unsigned zp)
{
    return (unsigned)mem[zp & 0xFFu] | ((unsigned)mem[(uint8_t)(zp + 1)] << 8);
}

REVS_FLAG_OP uint8_t seam_read(unsigned addr, int ram)
{
    return ram ? mem[addr] : (uint8_t)bus_read((uint16_t)addr);
}

/* ⚠⚠ THE RAM ARM BYPASSES bus_write, SO IT BYPASSES THE INK WATCH TOO — and that made the
   watch answer "nobody writes this cell" about a cell a twin was writing every frame.  Hoisting
   the hardware test out of the loop is the whole point of this seam (CLAUDE.md §bus_read/
   bus_write), so the diagnostic has to be hoisted with it.  Any future choke point that skips
   bus_write must repeat this call, or the instrument silently goes blind on that path. */
REVS_FLAG_OP void seam_write(unsigned addr, int ram, uint8_t value)
{
    if (ram) {
        mem[addr] = value;
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
void apply_driving_model_core(uint8_t posLo, uint8_t posHi);
void arg_a(uint8_t v);
void bearing_to_section_core(uint8_t sectionByte, uint8_t origin);
SignOriginExit build_sign_origin_core(uint8_t offset, uint8_t shift);
void build_track_geometry_core(uint8_t firstPointSide0, uint8_t firstPointSide1);
unsigned car_gap_lo_core(uint8_t a, uint8_t b);
GapTail car_gap_tail_core(uint8_t x, uint8_t y, unsigned carryIn);
StageNearbyCar stage_nearby_car_core(uint8_t gapA, unsigned gapFar, uint8_t slot);
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
void draw_road_core(uint8_t endCursorFar, uint8_t endCursorNear);
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
   16-bit elements: 0/1/2 the body angles and the frame's heading step (0..2 carry a further
   8-bit fraction in mem[MODEL_STATE_FRAC], which stays in mem[]), 3/4/5 their rates, 6/7 the
   axle loads, 8 the hand-integrated accumulator, 9 the car's signed speed, $0A..$0D the per-axle
   slip cluster and 14 the per-frame increment.  Marshalled WHOLE at the boundary shims: no
   writer owns a known subset, and a shim entered once a frame can afford 30 bytes.
   ⚠ Two shims import WITHOUT publishing (dial_needle_angle, draw_dash_needles): they read the
   vector to draw the needles and they PLOT, so with a fixture-random plot pointer a line can
   land inside $62D0..$62EE — a whole-array publish would undo a write the routine really made. */
extern uint16_t model_state_16[MODEL_STATE_N];
void model_state_marshal_in(void);
void model_state_marshal_out(void);

/* The named elements, so a site reads as the quantity rather than as an offset.  Every name here
   already exists in mem.h as a lo/hi pair; symbols.csv carries the evidence for each. */
#define MS_HEADING_STEP  2u    /* heading_step  — the frame's heading increment */
#define MS_ACCUM         8u    /* model_accum   — the hand-integrated accumulator */
#define MS_SPEED         9u    /* car_speed     — the car's SIGNED 16-bit speed */
#define MS_SLIP         10u    /* slip_magnitude, and the base of the per-axle slip cluster */
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
void model_accum_entry_marshal_out(void);

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
void race_main_loop_core(RestartDepth depth);
void rebase_edge_point_core(uint8_t slot);
RejectExit reject_object_slot_core(void);
RoadSide road_edge_side_apply(uint8_t sideSelect);
void road_edge_start_core(uint8_t nearSlotCount, uint8_t halfStride, uint8_t scratchSection, uint8_t pointLimit, uint8_t staleHorizonCap);
uint8_t road_edge_walk_core(uint8_t firstPoint, uint8_t sectionIndex, uint8_t midSlot, uint8_t pointCap, uint8_t offAxis);
int road_span_advance_core(uint8_t y);
AddFlags rotate_accum_by_steer_core(void);
AddFlags rotate_pair_a_by_steer_core(void);
void section_coord_add_delta_core(uint8_t dst, uint8_t src, const uint8_t dlo[3], const uint8_t dhi[3]);
uint8_t shift_near_edge_points_core(uint8_t topSlot, uint8_t wrapSlot, uint8_t lowTop, uint8_t nearSlots);
MosRegs sound_osword_core(uint8_t oswordNum, uint8_t blockLow);
uint8_t sound_queue_core(uint8_t slot, uint8_t amplitude, uint8_t savedX);
void sound_queue_exit_abi(uint8_t slot);
uint8_t sound_stop_channel_core(uint8_t chan, uint8_t ambientY);
int state_flags_bit6(void);
void store_slip_clamped_core(uint8_t valueHi);
void store_slip_clamped_off_throttle_core(uint8_t valueHi);
void store_slip_exit_abi(uint8_t sign);
void store_slip_signed_core(uint8_t valueHi);
uint8_t surface_colour_apply(uint8_t line);
CameraExit update_camera_and_drive_state_core(void);
EngineExit update_engine_revs_core(uint8_t carryIn, uint8_t entryY);
void update_grip_limits_core(void);
uint8_t print_spaces_core(uint8_t count, uint8_t x, uint8_t y);
int draw_starting_lights_core(void);
void draw_corner_marker_core(uint8_t offLo, uint8_t offHi, uint16_t edgeX, uint8_t edgeY, CornerMarker *out);
void mirrors_update_setup_core(uint8_t slotFlag, uint8_t objWidth, uint8_t bearingHi, uint8_t carHeadingHi, MirrorSetup *out);
void dial_needle_angle_core(uint8_t engineRevs, NeedleDial *out);
void driver_name_address_core(uint8_t index, NamePtr *out);
void draw_dash_needle_core(uint16_t steer, DashNeedle *out);
void menu_draw_gfx_bars_core(void);
void plot_line_octant_core(uint8_t entryScanline);
void undraw_plot_lines_core(void);   /* twin #165b — replay the plotter's undo list */
void mirror_draw_car_core(uint8_t lowerBound, uint8_t segment);  /* twin #165c */
void mirror_draw_car_core(uint8_t lowerBound, uint8_t segment);  /* twin #165c */
void text_script_interp_core(uint8_t tableIdx);
uint8_t menu_wait_key_core(uint8_t count);
typedef struct { uint8_t a, c, z, n, mathlo, writeMathlo; } ParseNum;
void parse_two_digit_ascii_core(uint8_t char0, uint8_t char1, ParseNum *out);
uint8_t seed_car_track_position_core(uint8_t x, uint8_t entropy, uint8_t *mathlo_out);
int update_horizon_band_core(uint16_t *r_out, uint8_t *mathhi_out);
uint8_t vdu_char_def_core(uint8_t ch);
uint8_t vdu_char_emit_core(void);
uint8_t vdu_char_wide_core(uint8_t ch);
void view_paint_lines_core(unsigned screenBase, unsigned firstLine, uint8_t entryCell);
SlotExit write_object_slot_core(uint8_t projectedLine, uint8_t entryX, uint8_t entryV, uint8_t entryC);

#endif /* REVS_NATIVE_SEAM_H */
