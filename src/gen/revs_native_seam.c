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
    hypot_max_marshal_in();               /* the larger magnitude arrives in mem[$7A/$7B] */
    hypot_min_marshal_in();               /* ...and the smaller, which this one SHIFTS IN PLACE */
    cpu.A = point_distance_hypot_apply();
    hypot_min_marshal_out();              /* the shifted minimum is output, not scratch */
}

void emit_edge_bearing(void)
{
    /* Y is unchanged to exit — entered with the slot in Y, the core never touches it, so the
       walk still reads the same slot back; A comes out as the point's distance high byte. */
    car_heading_marshal_in();            /* the heading every bearing is measured against */
    hypot_max_marshal_in();               /* the tail-called hypot's larger magnitude */
    hypot_min_marshal_in();               /* ...and its smaller one */
    bearing_marshal_in();                 /* the absolute bearing its caller left in mem[$8A/$8B] */
    cpu.A = emit_edge_bearing_core(cpu.Y);
    hypot_min_marshal_out();              /* the hypot shifted it — see revs_native.c */
}

void emit_edge_bearing_at_cursor(void)
{
    /* the fallen-into emit_edge_bearing emits at edge_cursor, so Y exits = edge_cursor; A is the
       point's distance high byte. */
    hypot_max_marshal_in();  hypot_min_marshal_in();
    cpu.A = emit_edge_bearing_at_cursor_core(cpu.X);
    cpu.Y = edge_cursor;
    hypot_min_marshal_out();              /* ...and the smaller magnitude, shifted by the hypot */
    hypot_max_marshal_out();              /* the bearing inside it PRODUCES the magnitude */
    bearing_marshal_out();                /* ...and the bearing itself */
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

/* ⚠ hypot_max IN *AND* OUT in both of these: each produces the relocated magnitude only on the
   path that keeps a point (the walk can reject every candidate, the start can find the near
   point already in range), so marshalling in first is what makes marshalling out faithful on
   the paths that never reach a bearing — the cells come back exactly as they went in. */
void road_edge_start(void)
{
    hypot_max_marshal_in();  hypot_min_marshal_in();  bearing_marshal_in();
    road_edge_start_core(0x06, (uint8_t)EDGE_HALF, (uint8_t)SECTION_NEAR, 0x3C, 0x07);
    hypot_max_marshal_out(); hypot_min_marshal_out(); bearing_marshal_out();
}

void road_edge_walk(void)
{
    hypot_max_marshal_in();  hypot_min_marshal_in();  bearing_marshal_in();
    edge_nearest_marshal_in();            /* the running minimum it keeps beating down */
    cpu.X = road_edge_walk_core(cpu.A, cpu.X, (uint8_t)SECTION_MID, 0x12, 0x14);
    hypot_max_marshal_out(); hypot_min_marshal_out(); bearing_marshal_out();
    edge_nearest_marshal_out();
}

void build_track_geometry(void)
{
    /* the top of the road pass, and the same IN/OUT pair as the two walks below it: whether any
       point reaches a bearing at all depends on the track, so the cells are carried through. */
    hypot_max_marshal_in();  hypot_min_marshal_in();  bearing_marshal_in();
    edge_nearest_marshal_in();            /* it ARMS the high lane and keeps the low one */
    build_track_geometry_core(0x06, 0x2E);
    hypot_max_marshal_out(); hypot_min_marshal_out(); bearing_marshal_out();
    edge_nearest_marshal_out();
}

void draw_road(void)
{
    /* The road pass owns all three screen pointers from its first seed to its last span. */
    plot_ptrs_marshal_in();
    draw_road_core(edge_cursor, edge_end_side0);
    plot_ptrs_marshal_out();
}

void apply_driving_model(void)
{
    car_heading_marshal_in();             /* it reads the heading in, as the car's position... */
    car_angle_marshal_in();               /* element 2 (the wheel) comes in; 0/1 go out below */
    apply_driving_model_core((uint8_t)car_heading_v, (uint8_t)(car_heading_v >> 8));
    car_angle_marshal_out();              /* compute_car_angles_core rebuilt the sin/cos pair */
    car_heading_marshal_out();            /* ...and its tail calls integrate_car_position, which
                                             advances it — core-to-core, so publish it here */
    model_accum_entry_marshal_out();       /* $46AE's value back into mem[$38/$39] */
}

void draw_track_object(void)
{
    car_heading_marshal_in();
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
    hypot_max_marshal_out();              /* the sorted LARGER magnitude — a by-product */
    hypot_min_marshal_out();              /* ...and the SMALLER: this sort produces both */
    bearing_marshal_out();                /* ...and THE BEARING, this routine's actual output: the
                                             shipping FUN_2a5f reads the cells straight after */
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
    EdgeIndices r;
    plot_ptrs_marshal_in();               /* it sets the three pages... */
    r = interp_edge_core(cpu.A, cpu.X, cpu.Y, cpu.C);
    plot_ptrs_marshal_out();              /* ...and an oracle caller reads them from mem[] */
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
    car_angle_marshal_in();                   /* it multiplies by a car angle */
    AddFlags f = rotate_accum_by_steer_core();  /* ends in model_integrate_element on element 8 */
    cpu.X = 8u; cpu.Y = 8u;                      /* X live at exit; Y = last apply_angle_term src */
    cpu.A = f.hi; cpu.C = f.carry; cpu.V = f.overflow; cpu.N = f.neg; cpu.Z = f.zero;
}

void rotate_pair_a_by_steer(void)
{
    car_angle_marshal_in();                   /* it multiplies by a car angle */
    AddFlags f = rotate_pair_a_by_steer_core(); /* ends in model_integrate_element on element 10 */
    cpu.X = 10u; cpu.Y = 10u;                    /* X live at exit; Y = last apply_angle_term src */
    cpu.A = f.hi; cpu.C = f.carry; cpu.V = f.overflow; cpu.N = f.neg; cpu.Z = f.zero;
}

void integrate_car_position(void)
{
    car_heading_marshal_in();
    AddFlags f = integrate_car_position_core();  /* ends in the heading add (car_heading += step) */
    car_heading_marshal_out();           /* ...and this routine IS that add: publish it */
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
    car_heading_marshal_in();
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
    hypot_max_marshal_in();               /* the hypot it runs takes the magnitude from mem[] */
    hypot_min_marshal_in();               /* ...both of its magnitudes */
    ContactExit e = note_object_contact_core(cpu.Y, cpu.C);
    hypot_min_marshal_out();              /* the hypot inside it shifts the smaller one */
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
        car_angle_marshal_in();                   /* it reads the steering angle back */
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

void update_horizon_band(void)
{
    /* $4F44 — result-only (exit regs/flags dead at both callers).  The shim reproduces the
       6502's write ORDER and its interrupt fence: math_hi ($4F5C..$4F66) is set BEFORE the
       SEI, then band1_duration is stored inside SEI/CLI ($4F67/$4F75), which fence the
       16-bit store against irq1v_band_schedule reading it from User-VIA interrupt context.
       cpu.I is inert on the host and the SMC-unhandled path returns before the fence — exactly
       as the 6502 does (its SEI is past the SMC checks). */
    uint16_t r; uint8_t mh;
    if (update_horizon_band_core(&r, &mh) != 0)
        return;                              /* per-circuit SMC unrecognised, already reported */
    math_hi = mh;                            /* $4F5C..$4F66: the ROR-pair result, before the fence */
    SEI();                                   /* $4F67 */
    band1_duration_lo = (uint8_t)(r & 0xFFu);/* $4F6B */
    band1_duration_hi = (uint8_t)(r >> 8);   /* $4F72 */
    CLI();                                   /* $4F75 */
}

void dial_needle_angle(void)
{
    /* $51A8 — the rev-counter needle.  The core does the engine_revs -> octant arithmetic; the shim
       does the dial-table lookups, sets up the 6502 entry ABI, and FALLS THROUGH into the shared
       transliterated plot_line_octant (a self-modifying line plotter — both differential sides run
       it, so its frame-buffer writes cancel given identical inputs).  Result-only: exit regs/flags
       are dead at the caller draw_dash_needles ($513D), which reloads A/scratch immediately.
       math_lo ($74) / math_hi ($75) keep their 6502 exit values until the $74/$75 relocation. */
    hypot_min_hi = 0x00;                         /* $51AA */
    NeedleDial d;
    dial_needle_angle_core(engine_revs, &d);

    math_lo        = d.offset;                   /* $74 — folded angle offset; plot_line_octant's DDA */
    shared_temp_76 = d.octant;                   /* $76 — octant index (SMC dispatch) */
    shared_temp_77 = d.temp77;                   /* $77 — step-opcode variant */

    uint8_t len = mem[DIAL_NEEDLE_DDA_TBL + d.offset];  /* $51EB — line length / minor delta */
    mem[0x0083] = len;                           /* $83 point_delta_hi */
    math_hi     = len;                           /* $75 — DDA loop count */

    uint8_t org = mem[DIAL_NEEDLE_ORIGIN_LO_TBL + d.quadrant];
    plot_ptr_lo = (uint8_t)(org & 0xF8u);        /* $70 — needle origin address low */
    plot_ptr_hi = mem[DIAL_NEEDLE_ORIGIN_HI_TBL + d.quadrant]; /* $71 */

    cpu.X = d.quadrant;                          /* plot_line_octant entry ABI: X=quadrant, */
    cpu.Y = (uint8_t)(org & 0x07u);              /* Y=start scanline, */
    cpu.A = plot_ptr_hi;                         /* A=plot_ptr_hi (dead, but faithful to $5202). */
    plot_line_octant();
}

void undraw_plot_lines(void)
{
    /* $511E — 6502-ABI shim for twin #165b.  The exit registers are the loop's residue: A is
       entry 0's restored byte, X the $FF that ended the walk (so N=1, Z=0), Y the 0 the store
       used.  On the empty-list path ($5120) the 6502 returns from LDX with X = 0, Z = 1, N = 0
       and A / Y untouched.  Reproduced rather than declared dead because the one shipping
       caller, draw_dash_needles, falls straight into dial_needle_angle's own 6502 entry. */
    uint8_t count = plot_undo_count;
    undraw_plot_lines_core();

    if (count == 0u) {
        cpu.X = 0u;
        UPD_NZ(0u);
        return;
    }
    cpu.A = mem[0x0780u];                         /* plot_undo_byte[0] — the last one restored */
    cpu.X = 0xFFu;
    cpu.Y = 0u;
    UPD_NZ(0xFFu);                                /* the DEX that ended the loop */
}

void draw_dash_needles(void)
{
    /* $513A — the STEERING-WHEEL needle, the last thing race_main_loop ($17B4) draws.  The prefix
       ($513A/$513D) erases last frame's marks and draws the rev-counter needle: undraw_plot_lines +
       dial_needle_angle (the latter falls through into plot_line_octant).  Both are shared with the
       oracle, so their frame-buffer writes cancel given identical input cells.  The core then does
       the steer_angle -> octant arithmetic and this shim reproduces the 6502 entry ABI and falls
       through into the shared plot_line_octant for a second, 6-pixel line.  Result-only: exit
       regs/flags are dead at the caller.  math_lo ($74) / math_hi ($75) keep their 6502 exit values
       (the folded angle index and a fixed 6) until the $74/$75 relocation. */
    /* ⭐ Core-to-core: undraw_plot_lines' 6502 exit ABI (A/X/Y + N/Z) is dead here —
       dial_needle_angle sets up plot_line_octant's entry registers itself. */
    undraw_plot_lines_core();                     /* $513A */
    dial_needle_angle();                          /* $513D — rev needle; falls into plot_line_octant */

    /* $5145-$5146 / $5186 — the routine's own PHP/PLP is balanced (S restored), but the pushed
       processor status stays on the stack as a residue at $0100+S.  It is the flags AFTER
       LSR steer_angle_lo: N=0, Z from the shifted value, C = bit 0; V/D/I carry through from the
       shared prefix (identical on both differential sides) and B/bit5 are set in a pushed copy.
       The later plot_line_octant pushes only below this cell, so the residue survives. */
    car_angle_marshal_in();                       /* consumer: element 2 of the relocated array */
    uint16_t steerAng = car_angle_16[CAR_ANGLE_STEER];
    mem[0x0100u + cpu.S] = (uint8_t)(0x30u                     /* bit5 = 1, B = 1 */
        | (cpu.V ? 0x40u : 0u)
        | (cpu.D ? 0x08u : 0u)
        | (cpu.I ? 0x04u : 0u)
        | ((((uint8_t)steerAng >> 1) == 0u) ? 0x02u : 0u)     /* Z */
        | (steerAng & 0x01u));                                /* C */

    DashNeedle n;
    draw_dash_needle_core(steerAng, &n);          /* one word — the array is car_angle_16[] now */

    math_lo        = n.angleIndex;                /* $74 — folded angle index; plot_line_octant's DDA */
    shared_temp_76 = n.stepSize;                  /* $76 — octant step (SMC dispatch) */
    mem[0x0083]    = n.ddaLen;                    /* $83 point_delta_hi — line length / minor delta */

    cpu.A = n.originMasked;                        /* $5190 AND #$FC — mode5_addr A */
    cpu.Y = n.rowSel;                             /* $5184 the negated row select — mode5_addr Y */
    mode5_addr();                                 /* $5192 -> plot_ptr; leaves X=row, A/Y=line */

    shared_temp_77 = n.subPos;                    /* $5195-$519A (originBase<<1)&7 */
    hypot_min_hi   = 0x04;                        /* $519E */
    math_hi        = 0x06;                        /* $51A0-$51A2 — a 6-pixel line, plot_line_octant's count */
    cpu.A          = 0x06;                        /* A at plot_line_octant entry ($51A0 LDA #6) */
    plot_line_octant();                          /* $51A4 */
}

void plot_line_octant(void)
{
    /* $5204 — the self-modifying octant line plotter (twin #164).  The 6502 entry ABI carries the
       octant in shared_temp_76 and the start scan line in Y; the core does all the mem[] work (the
       two SMC opcode slots, the plot pointer walk, the undo list, the pixel OR).  math_lo ($74) /
       math_hi ($75) keep their 6502 exit values until the $74/$75 relocation.  Result-only: exit
       regs/flags are dead — dial_needle_angle / draw_dash_needle fall through here and then return. */
    plot_line_octant_core(cpu.Y);
}

void text_script_interp(void)
{
    /* $4D7E — run the text script whose index is in X (twin #165).  The core does all the mem[]
       work (the plot_ptr2 reload, the character/space/command dispatch, the recursion) and threads
       X/Y to its callees itself; math_lo ($74) keeps its 6502 exit value until the $74/$75
       relocation.  Result-only: exit regs/flags are dead. */
    text_script_interp_core(cpu.X);
}

void menu_wait_key(void)
{
    /* $6571 — the front-end menu selector (twin #166).  `count` arrives in X (stored to math_hi,
       $75, and read back across the child calls, so the core touches the cell directly).  The only
       real exit confirms a selection: exit A = $98, X = hypot_min_lo - 1 with N/Z from that X. */
    uint8_t r = menu_wait_key_core(cpu.X);
    cpu.A = 0x98u;                       /* $6595 LDA #$98 */
    cpu.X = r;                           /* $659a LDX hypot_min_lo / $659c DEX */
    cpu.Y = 0x00u;                       /* $658d LDY shared_temp_76 — 0 on the confirm path */
    cpu.N = (r & 0x80u) ? 1 : 0;
    cpu.Z = (r == 0u) ? 1 : 0;
}

void driver_name_address(void)
{
    /* $3CEB — the (lo,hi) address of the Nth driver name in driver_name_table ($4050).  The 6502
       carries the index in X; the core does the layout arithmetic.  Exit A = lo, Y = hi (the two
       bytes emit_driver_name ($3250) loads into plot_ptr2); math_lo keeps its dead 6502 exit
       scratch until the $74/$75 relocation.  Exit flags are dead — every caller JSRs immediately. */
    NamePtr p;
    driver_name_address_core(cpu.X, &p);
    math_lo = p.scratch;                         /* $74 — dead intermediate (index&3)*4 */
    cpu.A   = p.lo;                              /* $3CFD exit A -> plot_ptr2_lo */
    cpu.Y   = p.hi;                              /* $3CF1 exit Y -> plot_ptr2_hi */
}

void menu_draw_gfx_bars(void)
{
    /* $3A50 — two teletext graphics bars into the MODE 7 menu page (twin #156).  The core does
       every mem[] write, including math_lo's dead 6502 exit value (the last row's end column).
       No stack residue (no PHA/PHP), and exit regs/flags are dead — front_end_menus reloads X
       the instant it returns, so nothing to reconstruct at the seam. */
    menu_draw_gfx_bars_core();
}

void parse_two_digit_ascii(void)
{
    /* $32D0 — validate/parse the two ASCII chars in math_lo/math_hi (twin #157).  The core reads
       both cells, so it is called before math_lo is rewritten.  Exit C is the validity/range flag
       the caller ($3EE0) branches on; A carries the value (dead at that caller, reconstructed for
       faithfulness); math_lo keeps its per-path 6502 exit value until the $74 relocation. */
    ParseNum p;
    parse_two_digit_ascii_core(math_lo, math_hi, &p);
    if (p.writeMathlo) math_lo = p.mathlo;       /* $74 — digit0, then digit0*10, per path */
    cpu.A = p.a;
    cpu.C = p.c;
    cpu.Z = p.z;
    cpu.N = p.n;
}

void seed_car_track_position(void)
{
    /* $635D — seed one car's grid position from a timer-entropy byte (#158).  A math_lo
       reader-nat: $74 is internal scratch, its per-path exit value written below.  Exit ABI:
       X live (the decremented car-index cursor the caller's loop reads); A/flags dead. */
    uint8_t x       = mem[CAR_SEED_INDEX];               /* $635D LDX car_seed_index */
    uint8_t entropy = (uint8_t)bus_read(USRVIA_T2CL);    /* $635F LDA $FE68 (one read, as the 6502) */

    /* $6362 PHP / $637B PLP is balanced (S restored) but the pushed P byte stays on the stack as a
       residue at $0100+S that the differential compares.  It is the flags AFTER LDA $FE68: N = the
       entropy byte's bit 7, Z set iff it was 0; C/V/D/I carry through from entry; bit5 and B are set
       in the pushed copy.  Nothing after (JSR/RTS are C calls in the oracle) rewrites this cell. */
    mem[0x0100u + cpu.S] = (uint8_t)(0x30u
        | ((entropy & 0x80u) ? 0x80u : 0u)               /* N */
        | (cpu.V ? 0x40u : 0u)
        | (cpu.D ? 0x08u : 0u)
        | (cpu.I ? 0x04u : 0u)
        | ((entropy == 0u) ? 0x02u : 0u)                 /* Z */
        | (cpu.C ? 0x01u : 0u));                         /* C */

    uint8_t mathlo;
    uint8_t xExit   = seed_car_track_position_core(x, entropy, &mathlo);
    math_lo = mathlo;                                    /* $6384/$6392 — $74 exit value per path */
    cpu.X = xExit;                                       /* $639C..$639F — decremented cursor */
    mem[CAR_SEED_INDEX] = xExit;                         /* $639F STA car_seed_index */
}

void mirrors_update(void)
{
    /* $7B00 — the once-per-frame wing-mirror update (race_main_loop body, $1739).  Result-only:
       exit regs/flags are dead at that caller.  The core carries the pre-loop bracket (a math_lo
       reader-nativization); the segment loop stays here as the shim's own bookkeeping, but
       mirror_draw_car is now twin #165c and is called core-to-core.  All of
       shared_temp_84 / span_line_cursor / shared_temp_76 / math_lo are read LIVE from mem[] in the
       loop so the skip path (which writes none of them) stays byte-exact. */
    uint8_t slot = mem[CAR_ORDER + car_ahead];          /* the car ahead's object slot */
    MirrorSetup s;
    car_heading_marshal_in();
    mirrors_update_setup_core(mem[CAR_FLAGS_SHAPE + slot],
                              mem[OBJECT_WIDTH + slot],
                              mem[OBJECT_BEARING_HI + slot],
                              (uint8_t)(car_heading_v >> 8), &s);
    if (s.drawable) {
        math_lo          = s.half;      /* $74 — 6502 exit value, set only on this path */
        shared_temp_84   = s.bottom;    /* $84 — block bottom line */
        span_line_cursor = s.top;       /* $7F — block top line */
    }
    shared_temp_76 = s.heading;         /* $76 — set on both paths */

    for (int y = 5; y >= 0; --y) {      /* $7B2C-$7B47: segments 5..0 */
        uint8_t v;
        if (shared_temp_76 == mem[MIRROR_SEG_BEARING_TBL + y]) {
            v = shared_temp_84;                 /* the matching segment — draw the car (bottom line) */
        } else if (mem[MIRROR_SEG_STATE + y] != 0) {
            v = 0x00;                           /* a still-set stale segment — redraw 0 to erase it */
        } else {
            continue;                           /* $7B46 — empty and stays empty */
        }
        mem[MIRROR_SEG_STATE + y] = v;
        /* ⭐ Core-to-core, now that mirror_draw_car is a twin (#165c): the 6502 handed it the
           value in A and the segment in Y, and those are its two arguments here. */
        mirror_draw_car_core(v, (uint8_t)y);
    }
}

void draw_corner_markers(void)
{
    /* $1B12 — draws the up-to-three corner markers build_track_geometry queued this frame, then
       zeroes the list.  Result-only: exit regs/flags are dead at the sole native caller
       (race_main_loop, $1730), so the shim reproduces only what the core cannot — the loop
       bookkeeping, the $38FE colour-patch dance, and the plot_object call.  The core carries the
       wide-value projection; math_lo/hi/$76 are written on both paths (the 6502 sets them before
       the draw/skip branch), but on the DRAW path plot_object's first act is math_lo=X, so those
       writes are faithful-but-superseded there and are the routine's own exit values only on the
       SKIP path.  plot_object gets X = marker_edge_index (its object slot); Y=0 is the DEY loop's
       exit and V is dead — neither reaches a plot_object mem[] write. */
    unsigned count = marker_count;
    for (unsigned y = 0; y < count; y++) {
        uint8_t idx = mem[MARKER_EDGE_IDX + y];             /* $1B18 — the edge point it hangs off */
        marker_count_saved = (uint8_t)y;                    /* $1B1B — Y parked across plot_object */
        uint8_t flags = mem[MARKER_FLAGS_TBL + y];          /* $1B1D */
        if (flags & 0x20u)
            mem[0x38FEu] = 0x0F;                            /* $1B26 — bit 5 recolours the marker (SMC) */

        uint16_t edgeX = (uint16_t)(mem[EDGE_X_LO_TBL + idx]
                                    | (mem[EDGE_X_HI_TBL + idx] << 8));
        CornerMarker m;
        draw_corner_marker_core(mem[MARKER_OFF_LO + y], mem[MARKER_OFF_HI + y],
                                edgeX, mem[EDGE_Y_TBL + idx], &m);

        math_lo = m.mathLo;                                 /* $74/$75/$76 — set on both paths */
        math_hi = m.mathHi;
        shared_temp_76 = m.temp76;
        if (m.draw) {
            plot_x     = m.plotX;                           /* $1B52 */
            plot_line  = m.plotLine;                        /* $1B57 */
            proj_width = m.projWidth;                       /* $1B6B */
            plot_shape = 0x06;                              /* $1B6F — always the marker shape */
            cpu.X = idx;                                    /* $1B71 — plot_object reads X (→ math_lo) */
            cpu.Y = 0x00;
            plot_object();
        }
        mem[0x38FEu] = 0xF0;                                /* $1B74-$1B76 — restore the marker colour */
    }
    marker_count = 0x00;                                    /* $1B7F-$1B81 — the list is per-frame */
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

void stage_nearby_car(void)
{
    uint8_t slot = mem[CAR_ORDER + cpu.X];                /* $28F2 LDA $013C,X */
    saved_slot_index = slot;                              /* $28F5 STA $45 */
    shared_counter_42 = slot;                             /* $28F7 STA $42 */

    /* $28F9 TAX; $28FA LDY #$17; $28FC SEC; $28FD car_gap_tail — X=slot, Y=$17, C=1 in.  The
       car_gap_tail shim leaves cpu.X/cpu.Y untouched, so X is still slot at the tail call. */
    GapTail g = car_gap_tail_core(slot, 0x17u, 1u);

    StageNearbyCar s = stage_nearby_car_core(g.a, g.c, slot);

    cpu.X = slot;                                         /* X held from TAX through to the tail */
    if (s.reject) {                                       /* $2911 reject_object_slot; return */
        reject_object_slot();
        return;
    }
    cpu.Y = s.y;                                          /* $2922 TAY */
    place_car_world_coords();                             /* $2937 */
}

/* $2692 check_car_pair — twin #163.  No meaningful entry registers (it loads its start position
   from zp_scratch_index immediately) and every exit register/flag is dead (both callers reload X),
   so the shim is a bare call — all state lives in mem[]. */
void check_car_pair(void)
{
    check_car_pair_core();
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

/* ---------------------------------------------------------------------------
   The crash / restart subtree's 6502-ABI shims (twins #167-#171).
   --------------------------------------------------------------------------- */

void sound_stop_all(void)
{
    uint8_t entryA = cpu.A;                      /* sound_stop_channel preserves A (PHA/PLA) */
    sound_stop_all_core(cpu.Y);
    cpu.A = entryA;
    cpu.X = 0xFFu;                               /* $43FB DEX ran once past channel 0 */
    cpu.N = 1; cpu.Z = 0;                        /* ...and that DEX's flags are the exit's */
}

void begin_scrape(void)
{
    begin_scrape_core(cpu.A, cpu.X);             /* A carries the clamped yaw kick in from $1135 */
    sound_queue_exit_abi(SOUND_SLOT_IMPACT);     /* $1C18 JSR / $1C1B RTS — the tail call's ABI */
}

void check_crash(void)
{
    uint8_t entryA = cpu.A, entryX = cpu.X;
    edge_nearest_marshal_in();            /* the distance it tests to decide the car is off-track */
    switch (check_crash_core(entryX)) {
    case CRASH_ARM_NONE:
        /* $1122 BCC $1162 — the CMP #2's residue.  edge_nearest_hi is 0 or 1, so A-2 is $FE/$FF
           and N is always set; X, Y and V pass through. */
        cpu.A = (uint8_t)(edge_nearest_v >> 8);
        cpu.C = 0; cpu.N = 1; cpu.Z = 0;
        break;
    case CRASH_ARM_SCRAPE:
        sound_queue_exit_abi(SOUND_SLOT_IMPACT); /* the JMP to begin_scrape is a tail call */
        break;
    default:
        /* The sound_queue at $1145 sets C/V and Y; everything after it is loads and stores, so
           only A/X/N/Z are overwritten again by the model-zeroing loop and the six constants. */
        sound_queue_exit_abi(SOUND_SLOT_IMPACT);
        cpu.A = 0x1Fu;                           /* $115E LDA #$1F, still live at the RTS */
        cpu.X = 0xFFu;                           /* $114F DEX ran once past element 0 */
        cpu.N = 0; cpu.Z = 0;
        break;
    }
    (void)entryA;
}

void build_player_car(void)
{
    /* ⭐ NO EXIT ABI TO RECONSTRUCT, and that is an argument, not an omission.  Its ONE caller
       (race_main_loop's RESTART_LATE arm, $16F6) does `LDA #0` next, so A and every flag are
       dead there; X and Y are dead too — the next reader is $0B77, which loads both itself.
       Two of them could not be reconstructed anyway: the second place_car_world_coords leaves
       the exit Y and C, and that twin's contract declares only X.  Fixture: LIVE_NONE.
       ⚠ The marshal-outs come from place_car_world_coords' queue tail (twin #172) — see that
       shim's note; this routine runs the whole thing twice. */
    hypot_max_marshal_in();  hypot_min_marshal_in();  bearing_marshal_in();
    build_player_car_core();
    hypot_max_marshal_out(); hypot_min_marshal_out(); bearing_marshal_out();
    car_heading_marshal_out();           /* it rebuilds the heading from scratch */
}

void step_delta_halve(void)
{
    /* A is the high byte of component 0 as it was BEFORE the shift (the last LDA $83,X), and C
       is the bit rotated out of that component's LOW byte — the only two register residues. */
    uint8_t hi0 = mem[POINT_DELTA_HI + 0];
    uint8_t lo0 = mem[MEM_math_lo + 0];
    step_delta_halve_core();
    cpu.A = hi0;
    cpu.C = (uint8_t)(lo0 & 1u);
    cpu.X = 0xFFu;                               /* $2B1A DEX ran once past component 0 */
    cpu.N = 1; cpu.Z = 0;                        /* ...and its flags are the exit's */
}

/* project_object_slot's 6502-ABI shims (twin #172).  ⭐ The marshal-outs are the whole reason
   these are not one-liners: the core leaves the bearing and the two sorted hypot magnitudes in
   their relocated uint16_t homes, and the transliterated oracle leaves them in mem[$78-$7B] and
   mem[$8A/$8B].  No marshal-IN is needed — the core has no early exit, so bearing_to_section_core
   always overwrites all three before anything reads them.
   The exit ABI is write_object_slot's (a fall-through, so a tail call), which its own twin
   already reconstructs; both callers here ignore every register, so the core discards it and the
   fixture is result-only. */
void project_object_slot(void)
{
    project_object_slot_core(cpu.X, cpu.A);
    hypot_max_marshal_out(); hypot_min_marshal_out(); bearing_marshal_out();
}

void project_object_coord(void)
{
    project_object_slot_core(0xFDu, cpu.A);      /* $2A5D LDX #$FD — the object_coord pair */
    hypot_max_marshal_out(); hypot_min_marshal_out(); bearing_marshal_out();
}

void mirror_draw_car(void)
{
    /* $7FB6 — 6502-ABI shim for twin #165c.  A is the car block's bottom line, Y the segment.
       Exit regs/flags are dead: the one caller, mirrors_update, reloads both per segment. */
    mirror_draw_car_core(cpu.A, cpu.Y);
}

