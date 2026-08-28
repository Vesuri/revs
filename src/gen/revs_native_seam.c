/* revs_native_seam.c — the thin 6502-ABI marshalling shims for the cpu-free cores
 * in revs_native.c.  Cluster-9 seam relocation (tools/split_seam.py).  Each shim reads
 * cpu/mem[], calls the typed core, and marshals the result + exit ABI back. */
#include "revs_native_seam.h"

void view_paint_lines(void)
{
    REVS_PLOT_CHECK_BEFORE();
    view_paint_lines_core(0x6700u, 0x4Fu, cpu.Y);
    REVS_PLOT_CHECK_AFTER();
}

void race_main_loop(void)
{
    hw_init();

    arg_a(0x00);
    text_out_via_mos = 0;         /* character output goes to the race view's own plotter */
    copy_dash_data();             /* A = 0: BUILD the $7B00 overlay from the block tails */
    view_paint_lines();

    race_main_loop_core(state_flags_bit6() ? RESTART_NONE : RESTART_FULL);
}

void clamp_near_edge_cursor(void)
{
    clamp_near_edge_cursor_core(cpu.X);
}

void clamp_near_edge_window(void)
{
    clamp_near_edge_window_core(0x06);
}

void shift_near_edge_points(void)
{
    /* the clamp inside the core sets X, Y and the flags; A is set last so it survives. */
    cpu.A = shift_near_edge_points_core(0x2C, (uint8_t)EDGE_HALF, 0x05, 0x06);
}

void rebase_edge_point(void)
{
    rebase_edge_point_core(cpu.Y);
}

void load_section_triple(void)
{
    load_section_triple_core(cpu.X, cpu.Y);
}

void point_distance_hypot(void)
{
    cpu.A = point_distance_hypot_apply();
}

void emit_edge_bearing(void)
{
    /* Y is unchanged to exit — entered with the slot in Y, the core never touches it, so the
       walk still reads the same slot back; A comes out as the point's distance high byte. */
    cpu.A = emit_edge_bearing_core(cpu.Y);
}

void emit_edge_bearing_at_cursor(void)
{
    /* the fallen-into emit_edge_bearing emits at edge_cursor, so Y exits = edge_cursor; A is the
       point's distance high byte. */
    cpu.A = emit_edge_bearing_at_cursor_core(cpu.X);
    cpu.Y = edge_cursor;
}

void emit_edge_width_offset(void)
{
    WidthExit e = emit_edge_width_offset_core(cpu.X, 0x03, cpu.V);
    cpu.A = e.a; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;   /* X passes through */
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

void road_edge_start(void)
{
    road_edge_start_core(0x06, (uint8_t)EDGE_HALF, (uint8_t)SECTION_NEAR, 0x3C, 0x07);
}

void road_edge_walk(void)
{
    cpu.X = road_edge_walk_core(cpu.A, cpu.X, (uint8_t)SECTION_MID, 0x12, 0x14);
}

void build_track_geometry(void)
{
    build_track_geometry_core(0x06, 0x2E);
}

void draw_road(void)
{
    draw_road_core(edge_cursor, edge_end_side0);
}

void apply_driving_model(void)
{
    apply_driving_model_core(car_heading_lo, car_heading_hi);
}

void draw_track_object(void)
{
    SlotExit e = draw_track_object_core(cpu.X, cpu.Y, cpu.V, cpu.C);
    cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
}

void fill_dash_edge_columns(void)
{
    SlotExit e = fill_dash_edge_columns_core(VIEW_LEFT_START_SRC, VIEW_RIGHT_START_SRC);
    cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
}

void copy_dash_data(void)
{
    uint8_t dirFlag = cpu.A;
    math_lo = dirFlag;                                   /* $18EA STA $74 */
    copy_dash_data_core(dirFlag);

    cpu.A = (uint8_t)adc_step((uint8_t)(plot_ptr_lo - 0x80), 0x80u, 0);  /* A + V of `ADC #$80` */
    cpu.X = DASH_BLOCK_COUNT;                            /* $29 */
    cpu.Y = mem[DASH_BLOCK_STARTS + (DASH_BLOCK_COUNT - 1)];
    cpu.N = 0; cpu.Z = 1; cpu.C = 1;                     /* CPX #$29 with X == $29 */
}

void bearing_to_section_from(void)
{
    bearing_to_section_core(cpu.X, cpu.Y);
}

void project_point_from(void)
{
    /* The 6502 returned TWO answers in flags, and the transliterated callers read both: carry is
       the clip decision ($23ff BCS), and N is "behind the camera" — bit 7 of the surviving line,
       which the exit SBC left in N ($2401 BPL, reached only when carry is clear).  A caller that
       is itself a native twin takes these from the ProjPoint struct; a caller still transliterated
       reaches for cpu.C/cpu.N, so the shim must restore the OS-exit flag state the clean core no
       longer produces as a side effect.  On the clip path N is dead (the caller's BPL is behind a
       taken BCS), so behind==0 there is harmless. */
    ProjPoint p = project_point_core(cpu.X, cpu.Y);
    cpu.A = p.line;   /* $22FB leaves the line in A; write_object_slot reads it as the slot line */
    cpu.C = p.clip;
    cpu.N = p.behind;
}

void road_span_advance(void)
{
    /* 6502-ABI shim: only the carry escapes; A/X/Y are preserved (the core never touches them)
       and the exit N/Z/V are dead.  math_lo/math_hi are NOT written — they were the 6502's
       register spill, an implementation detail the validate fixture ignores. */
    cpu.C = road_span_advance_core(cpu.Y);
}

void interp_edge(void)
{
    EdgeIndices r = interp_edge_core(cpu.A, cpu.X, cpu.Y, cpu.C);
    cpu.X = r.farIdx;
    cpu.Y = r.nearIdx;
}

void edge_x_offscreen(void)
{
    EdgeOffFlags e = edge_x_offscreen_core(cpu.X);
    cpu.A = e.a; cpu.V = e.v; cpu.C = e.c; cpu.N = e.n; cpu.Z = e.z;
}

void fill_line_attr(void)
{
    SlotExit e = fill_line_attr_core(cpu.A, cpu.Y, cpu.X, cpu.C, cpu.V);
    cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
}

void draw_surface_spans(void)
{
    draw_surface_spans_core(cpu.Y, cpu.A);
}

void mark_line_surfaces(void)
{
    SlotExit e = mark_line_surfaces_core(cpu.X, cpu.A, cpu.V);
    cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
}

void surface_colour_at(void)
{
    surface_colour_apply(cpu.Y);
}

void column_gap_walk(void)
{
    SlotExit e = column_gap_walk_core(cpu.X, cpu.Y, cpu.V);
    cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
}

void fill_column_gaps(void)
{
    SlotExit e = fill_column_gaps_core(cpu.X, cpu.Y, cpu.A, cpu.V);
    cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
}

void fill_edge_column_run(void)
{
    SlotExit e = fill_edge_column_run_core(cpu.X, cpu.A, cpu.Y, cpu.V);
    cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
}

void mul16_by_pi(void)
{
    unsigned scaled = ((((unsigned)cpu.A << 8) | math_lo) << 2) & 0xFFFFu;

    math_lo        = (uint8_t)scaled;           /* $0DB3-$0DB8, two ASL/ROL pairs */
    shared_temp_76 = (uint8_t)(scaled >> 8);    /* $0DB9 */
    math_hi        = 0xC9u;                     /* $0DBB-$0DBD — pi/4 in .8 fixed point */
    { Mul8AccumExit e = mul8_accum_core();      /* falls into mul8_accum; its exit is this routine's */
      cpu.A = e.a; cpu.N = e.n; cpu.Z = e.z; cpu.C = e.c; cpu.V = e.v; }
}

void model_integrate_element(void)
{
    AddFlags f = model_integrate_element_core(cpu.X);   /* X = slot; X/Y unchanged at exit */
    cpu.A = f.hi; cpu.C = f.carry; cpu.V = f.overflow; cpu.N = f.neg; cpu.Z = f.zero;
}

void kbd_test_key(void)
{
    MosRegs r = kbd_test_key_regs(cpu.X);  /* OSBYTE 129 (INKEY) — the MOS answer in A/X/Y */
    cpu.A = r.a; cpu.X = r.x; cpu.Y = r.y;
    int down = (r.x == 0xFFu);
    /* $0E57 CPX #$FF: Z/C set when the key is down (X came back $FF); N = bit 7 of (X-$FF).
       V is untouched by CPX, so it keeps the caller's value. */
    cpu.Z = down;
    cpu.C = down;
    cpu.N = ((uint8_t)(cpu.X - 0xFFu)) >> 7;
}

void rotate_accum_by_steer(void)
{
    AddFlags f = rotate_accum_by_steer_core();  /* ends in model_integrate_element on element 8 */
    cpu.X = 8u; cpu.Y = 8u;                      /* X live at exit; Y = last apply_angle_term src */
    cpu.A = f.hi; cpu.C = f.carry; cpu.V = f.overflow; cpu.N = f.neg; cpu.Z = f.zero;
}

void rotate_pair_a_by_steer(void)
{
    AddFlags f = rotate_pair_a_by_steer_core(); /* ends in model_integrate_element on element 10 */
    cpu.X = 10u; cpu.Y = 10u;                    /* X live at exit; Y = last apply_angle_term src */
    cpu.A = f.hi; cpu.C = f.carry; cpu.V = f.overflow; cpu.N = f.neg; cpu.Z = f.zero;
}

void integrate_car_position(void)
{
    AddFlags f = integrate_car_position_core();  /* ends in the heading add (car_heading += step) */
    cpu.Y = 0xFEu; cpu.X = 0xFFu;                /* $4922/$4923 two DEYs -> $FE; $4924 DEX -> $FF */
    cpu.A = f.hi; cpu.C = f.carry; cpu.V = f.overflow; cpu.N = f.neg; cpu.Z = f.zero;
}

void integrate_state_rates(void)
{
    AddFlags f = integrate_state_rates_core();   /* last pass leaves A / C / V of the high add live */
    cpu.A = f.hi; cpu.C = f.carry; cpu.V = f.overflow;
    cpu.Y = 0u;                                  /* $4956's DEY ran until Z — Y leaves at zero */
    cpu.X = 0xFFu; cpu.N = 1u; cpu.Z = 0u;       /* $4974's DEX (0 -> $FF); ITS N/Z are the exit flags */
}

void store_slip_signed(void)
{
    uint8_t sign = mem[SLIP_SIGN];                   /* BIT operand, before the core runs */
    store_slip_signed_core(cpu.A);
    store_slip_exit_abi(sign);                       /* C, X, S untouched by this routine */
}

void store_slip_clamped(void)
{
    uint8_t valueHi = cpu.A, sign = mem[SLIP_SIGN];
    uint8_t clampC = (uint8_t)(valueHi >= mem[SLIP_MAG_HI]);   /* $4B47 CMP SLIP_MAG_HI */
    store_slip_clamped_core(valueHi);
    store_slip_exit_abi(sign);
    cpu.C = clampC;                                  /* the compare's carry survives to the exit */
}

void store_slip_clamped_off_throttle(void)
{
    uint8_t valueHi = cpu.A, sign = mem[SLIP_SIGN], entryC = cpu.C;
    int throttle = (pedal_mode == 1u);              /* $4B42 LDY/DEY/BEQ — throttle skips the CMP */
    uint8_t clampC = (uint8_t)(valueHi >= mem[SLIP_MAG_HI]);
    store_slip_clamped_off_throttle_core(valueHi);
    store_slip_exit_abi(sign);
    cpu.C = throttle ? entryC : clampC;             /* throttle path never runs the CMP */
}

void derive_slip_reference(void)
{
    SlipRef r = derive_slip_reference_core(cpu.X);
    cpu.A = r.hi;                                    /* product high, or element on the declined arm */
    cpu.C = (uint8_t)r.declined;                     /* SEC on decline / CLC on accept */
}

void sound_osword(void)
{
    MosRegs r = sound_osword_core(cpu.A, cpu.X);     /* number in A, block low in X */
    cpu.A = r.a;                                     /* OSWORD leaves A (the reason code) and Y ($0B) */
    cpu.Y = r.y;
    cpu.X = sound_saved_x;                           /* $0B73 LDX sound_saved_x — its N/Z the exit */
    cpu.N = (uint8_t)(sound_saved_x >> 7);
    cpu.Z = (uint8_t)(sound_saved_x == 0u);
}

void sound_queue(void)
{
    uint8_t slot = cpu.A;
    sound_queue_core(slot, cpu.Y, cpu.X);
    sound_queue_exit_abi(slot);
}

void sound_queue_default(void)
{
    uint8_t slot = cpu.A;
    sound_queue_core(slot, sound_volume, cpu.X);
    sound_queue_exit_abi(slot);
}

void sound_stop_channel(void)
{
    uint8_t entryA = cpu.A;                          /* $0E5A PHA */
    cpu.X = sound_stop_channel_core(cpu.X, cpu.Y);   /* exit X from the core; ambient Y → the OSBYTE 21 */
    cpu.A = entryA;                                  /* $0E72 PLA — its N/Z the exit */
    cpu.N = (uint8_t)(entryA >> 7);
    cpu.Z = (uint8_t)(entryA == 0u);
}

void update_grip_limits(void)
{
    update_grip_limits_core();
    /* $4C46 exit Y = the ANDed surface bytes — the one escaping register (see the core). */
    cpu.Y = (uint8_t)(surface_change_0 & surface_change_1);
}

void update_engine_revs(void)
{
    /* Entry carry feeds the coast arm's ADC #7; entry Y is preserved on the off-power arms. */
    EngineExit e = update_engine_revs_core(cpu.C, cpu.Y);
    cpu.A = e.tail.hi; cpu.C = e.tail.carry; cpu.V = e.tail.overflow;
    cpu.N = e.tail.neg; cpu.Z = e.tail.zero;
    cpu.X = e.x; cpu.Y = e.y;
}

void update_camera_and_drive_state(void)
{
    CameraExit e = update_camera_and_drive_state_core();
    cpu.A = e.acc.hi; cpu.C = e.acc.carry; cpu.V = e.acc.overflow;
    cpu.N = e.acc.neg; cpu.Z = e.acc.zero;
    cpu.X = e.x; cpu.Y = e.y;
}

void build_sign_origin(void)
{
    SignOriginExit e = build_sign_origin_core(cpu.A, cpu.Y);
    cpu.A = e.a; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
}

void write_object_slot(void)
{
    SlotExit e = write_object_slot_core(cpu.A, cpu.X, cpu.V, cpu.C);
    cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
}

void reject_object_slot(void)
{
    RejectExit r = reject_object_slot_core();
    cpu.A = r.a; cpu.Y = r.y; cpu.N = (r.a >> 7) & 1u; cpu.Z = (r.a == 0u);
}

void note_object_contact(void)
{
    ContactExit e = note_object_contact_core(cpu.Y, cpu.C);
    cpu.A = e.a; cpu.Y = e.y; cpu.N = e.n; cpu.Z = e.z; cpu.C = e.c;
}

void plot_view_src_line(void)
{
    SlotExit e = plot_view_src_line_core(cpu.Y, cpu.A);
    cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y; cpu.N = e.n; cpu.Z = e.z;   /* V/C unread */
}

void fill_object_gap(void)
{
    SlotExit e = fill_object_gap_core(cpu.X);
    cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y; cpu.N = e.n; cpu.Z = e.z;
    /* V and C are dropped from this routine's fixture mask (unread by every caller) */
}

void limit_steer_demand(void)
{
    if (cpu.C) {
        uint8_t r = limit_steer_demand_core(cpu.A, 1);
        cpu.A = r; cpu.N = (r >> 7) & 1u; cpu.Z = (r == 0);
    }
}

void poll_steering_assist(void)
{
    poll_steering_assist_core();
    uint8_t flag = steering_assist_flag;
    cpu.C = track_direction >> 7;
    cpu.X = flag; cpu.N = (flag >> 7) & 1u; cpu.Z = (flag == 0);
}

void mode5_addr(void)
{
    Mode5Addr m = mode5_addr_core(cpu.A, cpu.Y);         /* A = quarter-offset, Y = row */
    cpu.X = m.row; cpu.A = m.line; cpu.Y = m.line;
    cpu.N = 0; cpu.Z = (m.line == 0);
}

void mode5_addr_for_cell(void)
{
    Mode5Addr m = mode5_addr_for_cell_core(cpu.A, cpu.Y);/* A = column, Y = row */
    cpu.X = m.row; cpu.A = m.line; cpu.Y = m.line;
    cpu.N = 0; cpu.Z = (m.line == 0);
}

void vdu_char_emit(void)
{
    uint8_t x = cpu.X, y = cpu.Y;
    uint8_t ch = vdu_char_emit_core();
    cpu.X = x; cpu.Y = y;
    cpu.A = ch; cpu.N = (ch >> 7) & 1u; cpu.Z = (ch == 0);
}

void vdu_char_wide(void)
{
    uint8_t x = cpu.X, y = cpu.Y;
    uint8_t ch = vdu_char_wide_core(cpu.A);
    cpu.X = x; cpu.Y = y;
    cpu.A = ch; cpu.N = (ch >> 7) & 1u; cpu.Z = (ch == 0);
}

void vdu_char_def(void)
{
    uint8_t ch = cpu.A;
    if (text_out_via_mos & 0x80u) {                      /* $5092 BIT + $5094 BMI — OSWRCH path */
        /* MOS ABI — documented cpu exception.  BIT set N from bit 7 of the flag byte (set on
           this arm) and Z from (A & flag); OSWRCH ($FFEE) leaves both untouched and preserves
           A (= ch) and X/Y.  V is dropped by the fixture mask. */
        cpu.N = 1;
        cpu.Z = ((ch & text_out_via_mos) == 0);
        mos_oswrch(ch, cpu.X, cpu.Y);                    /* $50F6 — OSWRCH the character (X/Y ambient) */
        return;
    }
    uint8_t x = cpu.X, y = cpu.Y;
    uint8_t rch = vdu_char_def_core(ch);
    cpu.X = x; cpu.Y = y;
    cpu.A = rch; cpu.N = (rch >> 7) & 1u; cpu.Z = (rch == 0);
}

void print_spaces(void)
{
    /* $3D50 — A is the count, X/Y are the ambient OSWRCH registers.  The loop leaves them
       unchanged; exit A = the space char, and N/Z come from the final DEC math_lo -> 0. */
    uint8_t rch = print_spaces_core(cpu.A, cpu.X, cpu.Y);
    cpu.A = rch;
    cpu.N = 0; cpu.Z = 1;
    /* C/V propagate from the last vdu_char_def (untouched here); X/Y ambient. */
}

void draw_starting_lights(void)
{
    /* $7B4A — exit regs/flags are dead at the sole caller (native race_main_loop), so the
       shim only reproduces the one observable side effect the core cannot: the PHA at $7B83
       pushes the pattern and the PLA at $7B90 pops it, leaving that byte as stack residue at
       $0100+S.  (The core returns -1 on the early exits, which push nothing.) */
    int pattern = draw_starting_lights_core();
    if (pattern >= 0)
        mem[0x0100u + cpu.S] = (uint8_t)pattern;
}

void draw_gear_indicator(void)
{
    uint8_t y = cpu.Y;
    uint8_t block = draw_gear_indicator_core();
    cpu.X = 0xFFu; cpu.Y = y;
    cpu.A = block; cpu.N = (block >> 7) & 1u; cpu.Z = (block == 0);
}

void adc_read(void)
{
    AdcRead r = adc_read_core(cpu.X);
    cpu.A = r.mag; cpu.X = r.dir; cpu.Y = r.reading;    /* exit Y = the ADVAL high byte */
    cpu.C = (r.mag >= 0x0Au);
    cpu.Z = (r.mag == 0x0Au);
    cpu.N = ((uint8_t)(r.mag - 0x0Au) >> 7) & 1u;
}

void car_gap(void)
{
    unsigned d = car_gap_lo_core(mem[CAR_STATE_1 + cpu.Y], mem[CAR_STATE_1 + cpu.X]);
    cpu.A = (uint8_t)d;
    cpu.C = !(d & 0x100);                          /* SEC/SBC: C clear = borrow */
    car_gap_tail();
}

void car_gap_tail(void)
{
    GapTail e = car_gap_tail_core(cpu.X, cpu.Y, cpu.C);   /* entry C is a genuine input */
    cpu.A = e.a; cpu.N = e.n; cpu.C = e.c;                /* V, Z dead at every caller */
}

void section_coord_add_delta(void)
{
    const uint8_t dlo[3] = { math_lo, math_hi, shared_temp_76 };
    const uint8_t dhi[3] = { mem[POINT_DELTA_HI + 0], mem[POINT_DELTA_HI + 1],
                             mem[POINT_DELTA_HI + 2] };
    section_coord_add_delta_core(cpu.X, cpu.Y, dlo, dhi);
}

void derive_car_section_cursor(void)
{
    car_section_cursor = derive_car_section_cursor_core(section_cursor);
}

void paint_fence_backdrop(void)
{
    uint8_t last = paint_fence_backdrop_core(horizon_extent);

    /* Exit ABI (not consumed by check_crash, reconstructed for faithfulness): the column loop
       ends on `LDX math_lo / CPX #$28` with X = $28, so N=0 Z=1 C=1; Y is the last column's
       bottom the inner loop stopped on; A is the last byte written. */
    cpu.X = FENCE_COL_COUNT;
    cpu.N = 0; cpu.Z = 1; cpu.C = 1;
    cpu.Y = math_hi;
    cpu.A = last;
}

void car_order_swap(void)
{
    uint8_t x, y;
    car_order_swap_core(cpu.X, cpu.Y, &x, &y);
    cpu.X = x;                                   /* exit ABI: X and Y hold the swapped values */
    cpu.Y = y;
}

void clear_race_clock(void)
{
    clear_race_clock_core(cpu.X);
    cpu.A = 0x00u;                               /* LDA #0 residue (dead, reproduced for the diff) */
}
