/* THE ORACLE-ONLY 6502-ABI SHIMS  (src/gen/revs_native_abi.c)
 *
 * ⭐⭐ WHY THIS FILE EXISTS.  Every routine here is the `void <name>(void)` half of a native
 * twin: it marshals `mem[]` and the `cpu` register file into the twin's typed `_core` and
 * publishes the exit ABI back.  NOTHING IN THE PORT CALLS ANY OF THEM.  Their only callers are
 * the transliteration (`revs_gen.c`, `revs_track_hooks.c` — the validation ORACLE, which
 * `make transtrap` proves no scenario executes) and `make validate`, which enters a twin
 * through its 6502 ABI on purpose so the differential compares like with like.
 *
 * They were moved out of `revs_native.c` so that file can be held cpu-free by a lint
 * (`make cpu-lint`): mixed in among the twins, 200 lines of register marshalling made every
 * `grep cpu\.` ambiguous and kept re-attributing itself to whichever `_core` sat above it.
 * ⚠ Do NOT write a twin's BODY here, and do not add a shim that a native caller uses — the
 * lint's whole value is that this file is the only place in the native surface speaking `cpu`.
 *
 * ⚠⚠ THE COST, MEASURED AND ACCEPTED (user decision).  Twenty of these shims call a core that
 * was `static` in `revs_native.c` and that GCC inlined away completely.  Crossing a TU boundary
 * forces those cores into existence as real functions; see revs_native_seam.h's block for the
 * list and docs/native-sweep.md for the framerate either side.
 */

#include <stdint.h>
#include "../cpu/cpu.h"
#include "../cpu/bus.h"
#include "../cpu/m68k_math.h"
#include "revs_decl.h"
#define REVS_MEM_ALIASES
#include "mem.h"
#include "revs_native_seam.h"
#include "../platform/platform_c.h"

void road_span_plot_2(void)
{
    uint8_t y = cpu.Y; unsigned carry = cpu.C; int ab;
    plot_ptrs_marshal_in();
    span_plot_oracle(&SPAN_PLOT_2, cpu.X, &y, &carry, &ab);
    cpu.Y = y; cpu.C = carry ? 1 : 0;
}

void span_end_marker_p1(void)
{
    uint8_t colMark = cpu.X; unsigned carry = cpu.C;
    plot_ptr_marshal_in();
    span_end_marker(SLOT_MARKER_P1, &plot_ptr_v, cpu.Y, &colMark, &carry);
    cpu.X = colMark; cpu.C = carry ? 1 : 0;
}

void span_end_marker_p2(void)
{
    uint8_t colMark = cpu.X; unsigned carry = cpu.C;
    plot_ptr2_marshal_in();
    span_end_marker(SLOT_MARKER_P2, &plot_ptr2_v, cpu.Y, &colMark, &carry);
    cpu.X = colMark; cpu.C = carry ? 1 : 0;
}

void draw_span_shallow_fwd(void) { span_walk_oracle(&ARM_SHALLOW_FWD, cpu.X, cpu.Y); }

void draw_span_shallow_rev(void) { span_walk_oracle(&ARM_SHALLOW_REV, cpu.X, cpu.Y); }

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
    bit_test(mem[MUL_SIGN]);
    abs16_math();
}

/* The 6502-ABI shims. */
void add_signed_into_element(void)
{
    model_state_marshal_in();
    add_signed_into_element_core(cpu.Y, cpu.N ? 0x80u : 0x00u);
    model_state_marshal_out();
}

void apply_angle_term_at(void)
{
    model_state_marshal_in();
    car_angle_marshal_in(); apply_angle_term_at_core(cpu.A, cpu.X);
    model_state_marshal_out();
}

void rotate_state_pair(void)
{
    model_state_marshal_in();
    car_angle_marshal_in(); rotate_state_pair_core(cpu.A, cpu.Y, cpu.X);
    model_state_marshal_out();
}

/* The 6502-ABI shims — they reconstruct each routine's exit registers/flags from the cpu-free
   core's typed outputs and the mem[] state, and hold the MOS-boundary marshalling.  The stores
   share an exit: A = the stored low byte (LDA math_lo), Y = the element (LDY SLIP_OUT_INDEX),
   N/Z from that low byte, V = bit 6 of SLIP_SIGN (BIT), C is the entry-value clamp compare. */
void slip_magnitude(void)
{
    model_state_marshal_in();
    slip_magnitude_core(cpu.Y);
}

void check_wheel_slip(void)
{
    model_state_marshal_in();
    check_wheel_slip_core(cpu.X);
    model_state_marshal_out();
} /* result-only */

void clamp_slip_to_grip(void)
{
    model_state_marshal_in();
    clamp_slip_to_grip_core(cpu.X);
    model_state_marshal_out();
} /* result-only */

void update_slip_sound(void)
{
    model_state_marshal_in();
    update_slip_sound_core(cpu.X, cpu.Y);
    model_state_marshal_out();
} /* result-only */

/* The 6502-ABI shims. */
void compute_car_angles(void)            { car_angle_marshal_in();
                                           compute_car_angles_core((uint16_t)((cpu.A << 8) | cpu.X));
                                           car_angle_marshal_out(); }

void begin_spin(void)
{
    model_state_marshal_in();                /* it nudges element 2, the heading step */
    begin_spin_from_a_core(road_speed, cpu.X);
    model_state_marshal_out();
    sound_queue_exit_abi(0x04u);
}

void begin_spin_from_a(void)
{
    model_state_marshal_in();                /* it nudges element 2, the heading step */
    begin_spin_from_a_core(cpu.A, cpu.X);
    model_state_marshal_out();
    sound_queue_exit_abi(0x04u);
}

/* The 6502-ABI shims. */
void plot_object(void)          { SlotExit e = plot_object_core(cpu.X, cpu.Y, cpu.V);
                                  cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
                                  cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c; }

void scale_shape_vectors(void)  { SlotExit e = scale_shape_vectors_core(cpu.V);
                                  cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
                                  cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c; }

void apply_steer_demand(void)           { car_angle_marshal_in(); apply_steer_demand_core(cpu.A);
                                          car_angle_marshal_out(); }

void steer_assist_dispatch(void)        { model_state_marshal_in(); car_angle_marshal_in(); steer_assist_dispatch_core(cpu.A);
                                          car_angle_marshal_out(); }

void scale_wing_settings(void)
{
    WingScaleExit e = scale_wing_settings_core();
    cpu.C = e.c;  cpu.V = e.v;
}

void section_angle_curve(void) { cpu.A = section_angle_curve_core(cpu.A); }

void scale_angle_in_section(void) { edge_nearest_marshal_in();
                                    cpu.A = scale_angle_in_section_core(cpu.A, cpu.Y); }

void record_section_jump(void) { cpu.C = record_section_jump_core(cpu.C, cpu.X); }

void place_player_in_section(void)
{
    edge_nearest_marshal_in();   /* its two folds weight the angle by the relocated running
                                    minimum (scale_angle_in_section_core reads edge_nearest_v) */
    EngineRegs ex = place_player_in_section_native(cpu.X, cpu.Y);
    cpu.X = ex.x; cpu.Y = ex.y;   /* the 6502-ABI exit; the native driver takes them by value */
}

void spin_car_out(void)
{
    int c = spin_car_out_core(cpu.X);
    if (c < 0) return;                            /* the bare-RTS arm publishes no flags */
    cpu.A = 0xC0u;                                /* the byte retire_car stored... */
    cpu.N = 1u; cpu.Z = 0u;                       /* ...so N/Z are its */
    cpu.C = (uint8_t)c;                           /* ...and C the lap comparison's */
}

/* 6502-ABI shim: X is the section byte cursor.  The $1253 LDA's exit A is dead at both
   callers (fixture LIVE_NONE), so the core does not produce it. */
void copy_section_height_to_side1(void) { copy_section_height_to_side1_core(cpu.X); }

/* 6502-ABI shim: Y is the segment index.  Only the validation oracle needs this entry —
   every production caller uses the core directly. */
void build_section_step_delta(void) { build_section_step_delta_core(cpu.Y); }

/* 6502-ABI shim: X = dest section byte cursor, Y = segment byte index.  Oracle entry only. */
void load_section_from_segment(void) { load_section_from_segment_core(cpu.X, cpu.Y); }

void place_car_world_coords(void)
{
    view_origin_marshal_in();
    hypot_max_marshal_in();  hypot_min_marshal_in();  bearing_marshal_in();
    cpu.X = place_car_world_coords_core(cpu.X, cpu.Y);
    hypot_max_marshal_out(); hypot_min_marshal_out(); bearing_marshal_out();
}

void car_index_dec(void) { cpu.X = car_index_dec_core(cpu.X); }   /* exit ABI: X only */

void car_index_inc(void) { cpu.X = car_index_inc_core(cpu.X); }

void find_player_neighbours(void) { cpu.X = find_player_neighbours_core(); }

void full_track_scan_rebuild(void) { full_track_scan_rebuild_core(cpu.A); }

void lap_complete(void) { lap_complete_core(cpu.X); }   /* X = car index; nothing escapes */

void shift_key_commands(void) { shift_key_commands_core(cpu.Y); }

/* ⭐ Seven more of the same kind, found by the caller audit `make cpu-lint` forced: each of
   these had no caller anywhere but revs_gen.c and validate_native.c either, so they are oracle
   shims exactly like the ones above and belong on this side of the lint. */

void road_span_plot(void)
{
    uint8_t y = cpu.Y; unsigned carry = cpu.C; int ab;
    plot_ptrs_marshal_in();                         /* read-only: nothing to publish back */
    span_plot_oracle(&SPAN_PLOT_1, cpu.X, &y, &carry, &ab);
    cpu.Y = y; cpu.C = carry ? 1 : 0;               /* A is untouched: the plotter never reads it */
}

void mul8_accum(void)
{
    Mul8AccumExit e = mul8_accum_core();
    cpu.A = e.a; cpu.N = e.n; cpu.Z = e.z; cpu.C = e.c; cpu.V = e.v;
}

void plot_shape_edges(void)     { SlotExit e = plot_shape_edges_core();
                                  cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
                                  cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c; }

void track_pos_advance(void)                     /* exit ABI: C only */
{
    car_distance_marshal_in_one(cpu.X);          /* one slot, not the array — see car_distance_16 */
    cpu.C = track_pos_advance_core(cpu.X);
    car_distance_marshal_out_one(cpu.X);
}

void track_pos_retreat(void)                     /* exit ABI: C only */
{
    car_distance_marshal_in_one(cpu.X);
    cpu.C = track_pos_retreat_core(cpu.X);
    car_distance_marshal_out_one(cpu.X);
}

void neg16_math(void)
{
    math_hi = cpu.A;                    /* $0E42 */
    neg16_math_noinit();
}

void neg16_math_noinit(void)
{
    uint16_t v = (uint16_t)(0u - (uint16_t)(((uint16_t)math_hi << 8) | math_lo));
    math_lo = (uint8_t)v;               /* $0E44-$0E49 — low byte written back */
    cpu.A   = (uint8_t)(v >> 8);        /* $0E4B-$0E4E — high byte escapes in A, math_hi kept */
}

/* ...and five more the lint caught only once its scan stopped skipping ONE-LINE shims.
   Same audit, same answer: revs_gen.c and validate_native.c are their only callers — the
   native path calls `span_walk` with the STEEP descriptors itself, and the three store
   routines are reached through their cores. */

void draw_span_steep_fwd(void) { span_walk_oracle(&ARM_STEEP_FWD, cpu.X, cpu.Y); }

void draw_span_steep_rev(void) { span_walk_oracle(&ARM_STEEP_REV, cpu.X, cpu.Y); }

void store_object_flags(void)   { store_object_flags_core(cpu.Y, cpu.A); }

void steer_demand_store(void)           { car_angle_marshal_in(); steer_demand_store_core(cpu.A);
                                          car_angle_marshal_out(); }

void clamp_and_store_steer_angle(void)  { car_angle_marshal_in(); clamp_and_store_steer_angle_core(cpu.A);
                                          car_angle_marshal_out(); }
