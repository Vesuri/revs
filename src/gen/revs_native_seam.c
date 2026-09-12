/* revs_native_seam.c — the thin 6502-ABI marshalling shims for the cpu-free cores
 * in revs_native.c.  Cluster-9 seam relocation (tools/split_seam.py).  Each shim reads
 * cpu/mem[], calls the typed core, and marshals the result + exit ABI back. */
#include "revs_native_seam.h"

/* The 6502-ABI shim.  $6700/$6800 is character row 10 of the frame buffer — display line
   80 — and $4F is the first scan line painted. */
void view_paint_lines(void)
{
    REVS_PLOT_CHECK_BEFORE();
    view_paint_lines_core(0x6700u, 0x4Fu, cpu.Y);
    REVS_PLOT_CHECK_AFTER();
}

/* The 6502-ABI shim.  The only decision the prologue makes is how much to reset: bit 6 of
   state_flags is set when wait_flag_05F4 is re-entering the race after the pit-lane
   wing-settings menu, and then the session state must survive untouched. */
uint8_t race_main_loop_session(void)
{
    /* ⭐ THE DRIVER IMPORTS EVERY RELOCATED VALUE ITS CORE REACHES WITHOUT AN INNER SHIM.
       race_main_loop_core calls a good many `_core` functions DIRECTLY — read_driving_controls_core,
       check_crash's and mirrors_update's bodies, draw_track_object_core — so those calls never cross
       a 6502-ABI shim and never pick up that shim's marshal-in.  Everything the engine set up in
       mem[] before entering the race would otherwise be read from a zero-initialised global on the
       first frame.  Once per race, so the cost is nothing; found by tools/marshal_audit.py.
       No marshal-out: every inner shim that WRITES one of these publishes it back itself, and this
       loop does not return by any path that a publish here would fix. */
    view_origin_marshal_in();
    model_state_marshal_in();
    car_angle_marshal_in();
    car_heading_marshal_in();
    edge_nearest_marshal_in();
    hw_init();

    text_out_via_mos = 0;         /* character output goes to the race view's own plotter */
    copy_dash_data_core(0x00u);   /* 0 = BUILD the $7B00 overlay from the block tails */
    /* The paint's entry cell arrives in Y, and Y here is copy_dash_data's exit Y: the start
       offset of the LAST block its inner loop stopped on.  BUILD mode only READS the
       dash_block_starts table (stow mode is the one that overwrites it), so the offset is the
       same before and after the copy. */
    view_paint_lines_core(0x6700u, 0x4Fu,
                          mem[MEM_dash_block_starts + (DASH_BLOCK_COUNT - 1)]);

    {
        uint8_t exitCarry = race_main_loop_core(state_flags_bit6() ? RESTART_NONE : RESTART_FULL);
        view_origin_marshal_out();
        return exitCarry;
    }
}

/* The 6502-ABI shim proper: the oracle's enter_session JSRs this and then reads the $17BA exit
   register file.  A is `LDA $70 / ADC #$80` (the add's flags are dead — N/Z/C below overwrite
   them, so it is plain arithmetic), X the dash block count, and C the block loop's `CPX #$29`
   closing carry, which the core returns. */
void race_main_loop(void)
{
    uint8_t exitCarry = race_main_loop_session();
    cpu.A = (uint8_t)((uint8_t)(plot_ptr_lo - 0x80) + 0x80u);
    cpu.X = DASH_BLOCK_COUNT;
    cpu.N = 0; cpu.Z = 1; cpu.C = exitCarry;
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
    shift_near_edge_points_core(0x2C, (uint8_t)EDGE_HALF, 0x05, 0x06);
}

void rebase_edge_point(void)
{
    model_state_marshal_in();     /* the 16-bit driving-model state vector */
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
    view_origin_marshal_in();
    /* the fallen-into emit_edge_bearing emits at edge_cursor, so Y exits = edge_cursor; A is the
       point's distance high byte. */
    car_heading_marshal_in();             /* the heading the bearing inside it is measured against */
    hypot_max_marshal_in();  hypot_min_marshal_in();
    bearing_marshal_in();                 /* IN as well as OUT: only the paths that keep a point
                                             produce it — see the note above road_edge_start */
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
    view_origin_marshal_in();   /* read-only: view_delta reads the camera */
    model_state_marshal_in();     /* the 16-bit driving-model state vector */
    car_heading_marshal_in();             /* every bearing it emits is measured against it */
    hypot_max_marshal_in();  hypot_min_marshal_in();  bearing_marshal_in();
    /* the stale-horizon cap is $23B3's SMC operand — see the twin */
    road_edge_start_core(0x06, (uint8_t)EDGE_HALF, (uint8_t)MEM_section_near_point, 0x3C,
                         mem[0x23B3u]);
    hypot_max_marshal_out(); hypot_min_marshal_out(); bearing_marshal_out();
}

void road_edge_walk(void)
{
    view_origin_marshal_in();   /* read-only: view_delta reads the camera */
    car_heading_marshal_in();             /* every bearing it emits is measured against it */
    hypot_max_marshal_in();  hypot_min_marshal_in();  bearing_marshal_in();
    edge_nearest_marshal_in();            /* the running minimum it keeps beating down */
    cpu.X = road_edge_walk_core(cpu.A, cpu.X, (uint8_t)MEM_section_midpoint_triple, 0x12, 0x14);
    hypot_max_marshal_out(); hypot_min_marshal_out(); bearing_marshal_out();
    edge_nearest_marshal_out();
}

/* $2490 — the walk resumed from an expansion circuit's hook.  Same marshalling as the walk
   itself; the section byte arrives in X and the exit X is the walk's own. */
/* The marshal-IN below is oracle-only in production and stays on this 6502-ABI path for the
   harness; native callers enter at the _native split.  Full argument at build_track_geometry. */
void road_edge_walk_resume(void)
{
    cpu.X = road_edge_walk_resume_from(cpu.X);
}

/* The same entry with the section index passed and returned as a value — for a TYPED hook twin
   ($56C5's resume), which has no `cpu` to put it in.  ⚠ The marshal-INs above the core are the
   6502-ABI path's and they are load-bearing here: hook_edge_walk_limit is itself entered with
   mem[] randomised by the harness, and skipping them (the _native split) cost 696/348
   mismatches. */
uint8_t road_edge_walk_resume_from(uint8_t sectionX)
{
    view_origin_marshal_in();
    car_heading_marshal_in();
    edge_nearest_marshal_in();
    hypot_max_marshal_in();  hypot_min_marshal_in();  bearing_marshal_in();
    uint8_t x = road_edge_walk_resume_core(sectionX, (uint8_t)MEM_section_midpoint_triple,
                                           0x12, 0x14);
    hypot_max_marshal_out(); hypot_min_marshal_out(); bearing_marshal_out();
    edge_nearest_marshal_out();
    return x;
}

void road_edge_walk_resume_native(void)
{
    /* ⚠ hypot_max/hypot_min/bearing are the MULTI-TENANT lanes -- the 6502 zero page shares
       $7A/$7B and $8A/$8B between native twins, and they change on ~38% of round trips -- so
       their INs are load-bearing and stay on the native path.  cpu.X is a genuine argument
       here, not marshalling: the walk resumes at the point index the caller stopped on. */
    hypot_max_marshal_in();  hypot_min_marshal_in();  bearing_marshal_in();
    cpu.X = road_edge_walk_resume_core(cpu.X, (uint8_t)MEM_section_midpoint_triple, 0x12, 0x14);
    hypot_max_marshal_out(); hypot_min_marshal_out(); bearing_marshal_out();
    edge_nearest_marshal_out();
}

/* $253B — the horizon point in Y, the half-width out in A. */
void horizon_half_width_at(void)
{
    cpu.A = horizon_half_width_at_core(cpu.Y, cpu.X);
}

/* $461B — the gradient scaler's tail.  ⭐ THE SIGN COMES OFF THE 6502 STACK: the caller's PHP
   is what $4621 pulls, so this is one of the places a flag genuinely escapes and the macro
   stays.  $4622's abs8 then acts on the pulled N, which is the twin above. */
void scale_by_track_gradient_tail(void)
{
    cpu.A = scale_by_track_gradient_tail_core(cpu.A, cpu.N);
    PLP();                                   /* $4621 */
    abs8();                                  /* $4622 — negates A when the pulled N says so */
}

/* ...and the same with the register file as a value, for the typed hook seam at $57BB/$54EB/
   $555C.  Identical work: only the flags' SOURCE changes, from ambient `cpu` to the caller's
   own HookRegs.  The PULL still reads the byte the hook's own push left at $0100+S. */
void scale_by_track_gradient_tail_regs(HookRegs *r)
{
    r->a = scale_by_track_gradient_tail_core(r->a, r->n);
    PLP_REGS(r);                             /* $4621 */
    abs8_regs(r);                            /* $4622 — negates A when the pulled N says so */
}

/* The 6502-ABI shim.  Both walk cursors are constants in the 6502; they are arguments here
   because they are the one thing that decides which half of the edge arrays each side owns. */
void build_track_geometry(void)
{
/* ⭐⭐ THE _native ENTRY, AND WHY IT EXISTS.  Everything above the call is a marshal-IN of a
   wide value whose byte lanes are zeroed ONLY by reset_driving_variables' two wipes, and that
   routine now zeroes the relocated copies too -- so in production the lanes can tell these
   arrays nothing they do not already hold, and the read is pure waste (MEASURED over eleven
   scenarios: docs/wide-value-cleanup.md §IS THE MARSHALLING ORACLE-ONLY).  It cannot simply be
   deleted, because `make validate` randomises mem[] and enters through this shim, and
   build/validate_native links the very same objects as build/revs, so there is no compile-time
   flag to hide it behind.  So it stays HERE, on the 6502-ABI path the harness uses, and native
   callers enter at the _native split below.  ⚠ Every marshal-OUT and every cpu write stays on
   the native path: the publishes are the mem[] mirror `make determinism` byte-compares, and the
   multi-tenant lanes' own marshal-INs (hypot_max/hypot_min/bearing) change on up to 38% of
   round trips and are load-bearing. */
    view_origin_marshal_in();   /* read-only: view_delta reads the camera */
    model_state_marshal_in();     /* the 16-bit driving-model state vector */
    car_heading_marshal_in();             /* every bearing it emits is measured against it */
    edge_nearest_marshal_in();            /* it ARMS the high lane and keeps the low one */
    /* live=AXY on the 6502-ABI path only: A is the horizon half-width, X the walk's last
       section byte, Y the horizon point.  ⭐ The one in-game consumer of that X and Y is the
       NEXT call of the frame ($1710 place_player_in_section, whose $462B hook seam inherits
       them), and the native driver passes them by value instead — so these three writes belong
       to the shim, not to the native path. */
    GeoExit ex = build_track_geometry_native();
    cpu.A = ex.a; cpu.X = ex.x; cpu.Y = ex.y;
}

GeoExit build_track_geometry_native(void)
{
    /* the top of the road pass, and the same IN/OUT pair as the two walks below it: whether any
       point reaches a bearing at all depends on the track, so the cells are carried through. */
    hypot_max_marshal_in();  hypot_min_marshal_in();  bearing_marshal_in();
    GeoExit ex = build_track_geometry_core(0x06, 0x2E);
    hypot_max_marshal_out(); hypot_min_marshal_out(); bearing_marshal_out();
    edge_nearest_marshal_out();
    return ex;
}

/* The 6502-ABI shim.  draw_road takes no arguments — the frame's geometry reaches it entirely
   through the edge lists and the three cursor cells — and leaves A, X and the flags wherever
   its last callee left them, which the core sets by marshalling the near mark's SlotExit into
   cpu at that call site (see above); the shim itself adds nothing. */
void draw_road(void)
{
    /* The road pass owns all three screen pointers from its first seed to its last span. */
    plot_ptrs_marshal_in();
    SlotExit e = draw_road_core(edge_cursor, edge_end_side0);
    /* draw_road leaves A, X, Y and the flags wherever its last callee (the near mark) left
       them — the frame's geometry reaches it entirely through the edge lists and the three
       cursor cells, so this exit is the whole 6502 ABI. */
    cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
    plot_ptrs_marshal_out();
}

/* ⭐⭐ THE FRAME DRIVER'S ENTRY, one level BELOW the 6502-ABI input marshals.  race_main_loop_core
   is the only production caller and it runs phase 3 (read_driving_controls) immediately before
   this, so model_state_16[] and car_angle_16[] are ALREADY the live wide values when it gets
   here — re-reading them out of mem[] would only re-import what the previous phase just wrote
   there, 36 byte reads of it (fifteen model-state elements plus three car angles).  The shim
   below keeps those marshals, because a 6502 caller really does hand its inputs over in mem[].
   ⚠ The OUTPUT marshals stay in this body unconditionally: mem[] is still the faithful mirror
   the whole-corpus differential compares, so every relocated value this pass writes is published
   before the phase returns. */
/* The marshal-IN below is oracle-only in production and stays on this 6502-ABI path for the
   harness; native callers enter at the _native split.  Full argument at build_track_geometry. */
void apply_driving_model_frame(void)
{
    view_origin_marshal_in();
    car_heading_marshal_in();             /* it reads the heading in, as the car's position... */
    apply_driving_model_frame_native();
}

void apply_driving_model_frame_native(void)
{
    CameraExit ce = apply_driving_model_core(car_heading_v, cpu.C);
    car_angle_marshal_out();              /* compute_car_angles_core rebuilt the sin/cos pair */
    car_heading_marshal_out();            /* ...and its tail calls integrate_car_position, which
                                             advances it — core-to-core, so publish it here */
    lateral_speed_entry_marshal_out();       /* $46AE's value back into mem[$38/$39] */
    model_state_marshal_out();    /* ...and publish it back to mem[] */
    view_origin_marshal_out();
    /* A, X, Y and the flags come back from update_camera_and_drive_state untouched. */
    cpu.A = ce.acc.hi; cpu.C = ce.acc.carry; cpu.V = ce.acc.overflow;
    cpu.N = ce.acc.neg; cpu.Z = ce.acc.zero;
    cpu.X = ce.x; cpu.Y = ce.y;
}

/* The 6502-ABI shim.  The player's own position is the routine's one input — it reaches the
   6502 in A and X — and A, X, Y and the flags come back from update_camera_and_drive_state untouched. */
void apply_driving_model(void)
{
    model_state_marshal_in();     /* the 16-bit driving-model state vector */
    car_angle_marshal_in();       /* element 2 (the wheel) comes in; 0/1 go out below */
    apply_driving_model_frame();
}

/* The 6502-ABI shim.  The slot arrives in X; everything else the routine needs is in mem[]. */
void draw_track_object(void)
{
    car_heading_marshal_in();
    SlotExit e = draw_track_object_core(cpu.X, cpu.Y, cpu.V, cpu.C);
    cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
}

/* The 6502-ABI shim.  No inputs at all — every value is an immediate in the original — and
   A, X, Y and the flags come back from the second fill_edge_column_run. */
void fill_dash_edge_columns(void)
{
    SlotExit e = fill_dash_edge_columns_core(MEM_view_left_start_src, MEM_view_right_start_src);
    cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
}

/* The 6502-ABI shim.  A carries the direction in on entry (and is stashed into math_lo ($74),
   the routine's direction flag).  On exit A/X/Y and the flags carry the tail arithmetic:
   `LDA $70 / ADC #$80` leaves A = final src low byte with the add's V; the block loop closes on
   `CPX #$29` (X = $29, N=0 Z=1 C=1); Y is the last block's start offset the inner loop stopped on. */
void copy_dash_data(void)
{
    uint8_t dirFlag = cpu.A;
    math_lo = dirFlag;                                   /* $18EA STA $74 */
    copy_dash_data_core(dirFlag);

    cpu.A = (uint8_t)adc_step((uint8_t)(plot_ptr_lo - 0x80), 0x80u, 0);  /* A + V of `ADC #$80` */
    cpu.X = DASH_BLOCK_COUNT;                            /* $29 */
    cpu.Y = mem[MEM_dash_block_starts + (DASH_BLOCK_COUNT - 1)];
    cpu.N = 0; cpu.Z = 1; cpu.C = 1;                     /* CPX #$29 with X == $29 */
}

void bearing_to_section_from(void)
{
    view_origin_marshal_in();
    bearing_to_section_core(cpu.X, cpu.Y);
    hypot_max_marshal_out();              /* the sorted LARGER magnitude — a by-product */
    hypot_min_marshal_out();              /* ...and the SMALLER: this sort produces both */
    bearing_marshal_out();                /* ...and THE BEARING, this routine's actual output: the
                                             shipping FUN_2a5f reads the cells straight after */
}

/* The 6502-ABI shims.  ⚠ Only the BODIES are twinned: $2145 and $2285 stay transliterated,
   because each is a single `LDY #0` that falls into the body, so a twin of them would run the
   same core the oracle does and the fixture would compare native against native — a fixture
   that passes vacuously (docs/validation-harness.md).  Two generated LDY macros is the right
   price for keeping both oracles real.

   X is the section's byte index into section_coord_lo/hi; Y is the view origin's byte offset,
   0 for the camera and 6 for the road sign's viewpoint. */
void project_point_from(void)
{
    view_origin_marshal_in();
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

/* The 6502-ABI shim.  A is the style record's index, X the endpoint in one edge half, Y the
   endpoint in the other, and the CARRY is "publish this endpoint without drawing a span".  On
   exit the 6502 leaves the far/near indices in X/Y (the oracle's INX/INY read them), so marshal
   the returned pair back there. */
void interp_edge(void)
{
    EdgeIndices r;
    plot_ptrs_marshal_in();               /* it sets the three pages... */
    r = interp_edge_core(cpu.A, cpu.X, cpu.Y, cpu.C);
    plot_ptrs_marshal_out();              /* ...and an oracle caller reads them from mem[] */
    cpu.X = r.farIdx;
    cpu.Y = r.nearIdx;
}

/* The 6502-ABI shim: the edge point index arrives in X (unchanged to exit). */
void edge_x_offscreen(void)
{
    EdgeOffFlags e = edge_x_offscreen_core(cpu.X);
    cpu.A = e.a; cpu.V = e.v; cpu.C = e.c; cpu.N = e.n; cpu.Z = e.z;
}

/* The 6502-ABI shim.  A is the target buffer's low byte, Y the side's end cursor, X the point
   the walk starts from; entry C/V are echoed on the SMC-trap path. */
void fill_line_attr(void)
{
    SlotExit e = fill_line_attr_core(cpu.A, cpu.Y, cpu.X, cpu.C, cpu.V);
    cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
}

/* The 6502-ABI shim.  Y is the pass number, A the first edge index of the pass. */
void draw_surface_spans(void)
{
    draw_surface_spans_core(cpu.Y, cpu.A);
}

/* The 6502-ABI shim.  X is the surface class to OR in, A the first edge index; Y comes back
   as the scan line at which this side's line_attr buffer stops being valid. */
void mark_line_surfaces(void)
{
    SlotExit e = mark_line_surfaces_core(cpu.X, cpu.A, cpu.V);
    cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
}

/* The 6502-ABI shim.  Y is the scan line and EDGE_COLUMN the position; A comes back as the
   colour, X as the surface class on the two arms that compute one. */
void surface_colour_at(void)
{
    surface_colour_apply(cpu.Y);
}

/* The 6502-ABI shim.  X is the first column, A the stop column, Y the first start line; X
   comes back as the column the run stopped at and Y as the last walk's end line. */
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

/* $0DB3  mul16_by_pi — A 16-BIT ANGLE TIMES PI  (twin #47)
   Shifts (A : math_lo) left twice, parks the high byte where mul8_accum wants it, seeds the
   multiplier with $C9 and falls into mul8_accum.  ⭐ $C9/256 = 0.785 = pi/4 to three figures,
   and 4 x pi/4 = pi — so what compute_car_angles gets back is its angle multiplied by pi
   [INFERRED from the constant; the x4 and the multiply are [DERIVED]].  A is high on exit. */
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
    model_state_marshal_in();     /* the 16-bit driving-model state vector */
    AddFlags f = model_integrate_element_core(cpu.X);   /* X = slot; X/Y unchanged at exit */
    cpu.A = f.hi; cpu.C = f.carry; cpu.V = f.overflow; cpu.N = f.neg; cpu.Z = f.zero;
    model_state_marshal_out();    /* ...and publish it back to mem[] */
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

void rotate_velocity_by_steer(void)
{
    model_state_marshal_in();     /* the 16-bit driving-model state vector */
    car_angle_marshal_in();                   /* it multiplies by a car angle */
    AddFlags f = rotate_velocity_by_steer_core();  /* ends in model_integrate_element on element 8 */
    cpu.X = 8u; cpu.Y = 8u;                      /* X live at exit; Y = last apply_angle_term src */
    cpu.A = f.hi; cpu.C = f.carry; cpu.V = f.overflow; cpu.N = f.neg; cpu.Z = f.zero;
    model_state_marshal_out();    /* ...and publish it back to mem[] */
}

void rotate_pair_a_by_steer(void)
{
    model_state_marshal_in();     /* the 16-bit driving-model state vector */
    car_angle_marshal_in();                   /* it multiplies by a car angle */
    AddFlags f = rotate_pair_a_by_steer_core(); /* ends in model_integrate_element on element 10 */
    cpu.X = 10u; cpu.Y = 10u;                    /* X live at exit; Y = last apply_angle_term src */
    cpu.A = f.hi; cpu.C = f.carry; cpu.V = f.overflow; cpu.N = f.neg; cpu.Z = f.zero;
    model_state_marshal_out();    /* ...and publish it back to mem[] */
}

void integrate_car_position(void)
{
    view_origin_marshal_in();
    model_state_marshal_in();     /* the 16-bit driving-model state vector */
    car_heading_marshal_in();
    AddFlags f = integrate_car_position_core();  /* ends in the heading add (car_heading += step) */
    car_heading_marshal_out();           /* ...and this routine IS that add: publish it */
    cpu.Y = 0xFEu; cpu.X = 0xFFu;                /* $4922/$4923 two DEYs -> $FE; $4924 DEX -> $FF */
    cpu.A = f.hi; cpu.C = f.carry; cpu.V = f.overflow; cpu.N = f.neg; cpu.Z = f.zero;
    view_origin_marshal_out();
}

void integrate_state_rates(void)
{
    model_state_marshal_in();     /* the 16-bit driving-model state vector */
    AddFlags f = integrate_state_rates_core();   /* last pass leaves A / C / V of the high add live */
    cpu.A = f.hi; cpu.C = f.carry; cpu.V = f.overflow;
    cpu.Y = 0u;                                  /* $4956's DEY ran until Z — Y leaves at zero */
    cpu.X = 0xFFu; cpu.N = 1u; cpu.Z = 0u;       /* $4974's DEX (0 -> $FF); ITS N/Z are the exit flags */
    model_state_marshal_out();    /* ...and publish it back to mem[] */
}

void store_slip_signed(void)
{
    model_state_marshal_in();     /* the 16-bit driving-model state vector */
    uint8_t sign = mem[SLIP_SIGN];                   /* BIT operand, before the core runs */
    store_slip_signed_core(cpu.A);
    store_slip_exit_abi(sign);                       /* C, X, S untouched by this routine */
    model_state_marshal_out();    /* ...and publish it back to mem[] */
}

void store_slip_clamped(void)
{
    model_state_marshal_in();     /* the 16-bit driving-model state vector */
    uint8_t valueHi = cpu.A, sign = mem[SLIP_SIGN];
    uint8_t clampC = (uint8_t)(valueHi >= mem[SLIP_MAG_HI]);   /* $4B47 CMP SLIP_MAG_HI */
    store_slip_clamped_core(valueHi);
    store_slip_exit_abi(sign);
    cpu.C = clampC;                                  /* the compare's carry survives to the exit */
    model_state_marshal_out();    /* ...and publish it back to mem[] */
}

void store_slip_clamped_off_throttle(void)
{
    model_state_marshal_in();     /* the 16-bit driving-model state vector */
    uint8_t valueHi = cpu.A, sign = mem[SLIP_SIGN], entryC = cpu.C;
    int throttle = (pedal_mode == 1u);              /* $4B42 LDY/DEY/BEQ — throttle skips the CMP */
    uint8_t clampC = (uint8_t)(valueHi >= mem[SLIP_MAG_HI]);
    store_slip_clamped_off_throttle_core(valueHi);
    store_slip_exit_abi(sign);
    cpu.C = throttle ? entryC : clampC;             /* throttle path never runs the CMP */
    model_state_marshal_out();    /* ...and publish it back to mem[] */
}

void derive_slip_reference(void)
{
    model_state_marshal_in();     /* the 16-bit driving-model state vector */
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

/* $0B65 — the OSWORD's A/Y, the parked X back with its own N/Z, and the entry add's C/V. */
void sound_envelope(void)
{
    uint8_t envBase = cpu.A;                         /* the envelope definition block's base */
    MosRegs r = sound_envelope_core(envBase, cpu.X);
    cpu.A = r.a; cpu.Y = r.y;                        /* OSWORD leaves A (the reason code) and Y ($0B) */
    cpu.X = sound_saved_x;                           /* $0B73 LDX sound_saved_x — its N/Z the exit */
    cpu.N = (uint8_t)(sound_saved_x >> 7);
    cpu.Z = (uint8_t)(sound_saved_x == 0u);
    cpu.C = (uint8_t)(((unsigned)envBase + 0x38u) > 0xFFu);   /* $0B69's ADC — nothing below */
    cpu.V = adc_overflow(envBase, 0x38u, 0u);                 /* ...writes either flag again */
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
    model_state_marshal_in();             /* it reads the model's own state to size the limits */
    update_grip_limits_core();
    model_state_marshal_out();
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
    view_origin_marshal_in();
    model_state_marshal_in();             /* its spin arm nudges element 2, the heading step */
    car_heading_marshal_in();
    CameraExit e = update_camera_and_drive_state_core();
    cpu.A = e.acc.hi; cpu.C = e.acc.carry; cpu.V = e.acc.overflow;
    cpu.N = e.acc.neg; cpu.Z = e.acc.zero;
    cpu.X = e.x; cpu.Y = e.y;
    model_state_marshal_out();
    view_origin_marshal_out();
}

void build_sign_origin(void)
{
    view_origin_marshal_in();
    SignOriginExit e = build_sign_origin_core(cpu.A, cpu.Y);
    cpu.A = e.a; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
    view_origin_marshal_out();
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

/* The 6502-ABI shims. */
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

/* A leaf: on the carry path it returns steer_angle_hi with that value's N/Z, and C and V
   unchanged from entry; on the no-carry path A and every flag are the caller's. */
void limit_steer_demand(void)
{
    if (cpu.C) {
        car_angle_marshal_in();                   /* it reads the steering angle back */
        uint8_t r = limit_steer_demand_core(cpu.A, 1);
        cpu.A = r; cpu.N = (r >> 7) & 1u; cpu.Z = (r == 0);
    }
}

/* A leaf: A is preserved, X comes back as the flag (with its N/Z), C as bit 7 of
   track_direction; V is untouched. */
void poll_steering_assist(void)
{
    poll_steering_assist_core();
    uint8_t flag = steering_assist_flag;
    cpu.C = track_direction >> 7;
    cpu.X = flag; cpu.N = (flag >> 7) & 1u; cpu.Z = (flag == 0);
}

/* plot_ptr is the side effect; the scan line within the row comes back in A and Y (N clear,
   the value is < 8) and the row in X.  The fixture drops V and C for this cluster (the second
   add's flags are dead at every caller). */
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

/* OSWORD (inside the core) clobbers X/Y, but the 6502 preserves the caller's X/Y across
   vdu_char_emit and vdu_char_wide ($509D/$50EF PUSH/PULL), so both shims save and restore them.
   Exit A/N/Z come from the block byte the emit leaves live at $62C3.  (The i=5 fixture ignores
   $01FE/$01FF, where the transliterated oracle's PUSH/PULL leaves residue these do not write.) */
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

/* The OSWRCH-path branch is 6502-ABI reconstruction, so it lives here, not in the core. */
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

/* $65C8 position_to_bcd — A in, A out; V is not modelled (no caller reads it). */
void position_to_bcd(void)
{
    BcdExit e = position_to_bcd_core(cpu.A);
    cpu.A = e.a; cpu.N = e.n; cpu.Z = e.z; cpu.C = e.c;
}

/* $502F print_time_row21 / $502D show_lap_time_lower / $501D show_lap_time_lines — the readout
   chain.  A on entry is the field mask only for the innermost one (the other two supply their
   own); X comes back as print_lap_time's car slot, Y is the ambient OSWRCH register. */
void print_time_row21(void)
{
    uint8_t y = cpu.Y;
    TextExit e = print_time_row21_core(cpu.A, y);
    cpu.A = e.a; cpu.X = 0x15u; cpu.Y = y;
    cpu.N = e.n; cpu.Z = e.z; cpu.C = e.c;
}

void show_lap_time_lower(void)
{
    uint8_t y = cpu.Y;
    TextExit e = show_lap_time_lower_core(y);
    cpu.A = e.a; cpu.X = 0x15u; cpu.Y = y;
    cpu.N = e.n; cpu.Z = e.z; cpu.C = e.c;
}

void show_lap_time_lines(void)
{
    uint8_t y = cpu.Y;
    TextExit e = show_lap_time_lines_core(y);
    cpu.A = e.a; cpu.X = 0x15u; cpu.Y = y;
    cpu.N = e.n; cpu.Z = e.z; cpu.C = e.c;
}

/* $667B print_driver_name_by_order / $6673 print_driver_name_at_row — Y is the car_order position
   on entry and emit_driver_name's terminator ($0C) on the way out. */
void print_driver_name_by_order(void)
{
    NameExit e = print_driver_name_by_order_core(cpu.Y);
    cpu.A = e.a; cpu.X = e.x; cpu.Y = 0x0Cu;
    cpu.N = e.n; cpu.Z = e.z; cpu.C = e.c;
}

void print_driver_name_at_row(void)
{
    NameExit e = print_driver_name_at_row_core(cpu.A, cpu.Y);
    cpu.A = e.a; cpu.X = e.x; cpu.Y = 0x0Cu;
    cpu.N = e.n; cpu.Z = e.z; cpu.C = e.c;
}

/* $1B84 update_position_display — A/X/Y all ambient on entry and all path-dependent on exit. */
/* $11BE retire_car — X selects the car.  Exit A is the $C0 it stored, so N/Z are that byte's
   and C is the lap comparison's. */
void retire_car(void)
{
    uint8_t notFinished = retire_car_core(cpu.X);
    cpu.A = 0xC0u;
    cpu.N = 1u; cpu.Z = 0u; cpu.C = notFinished;
}

/* $1163 finish_race — no inputs, and no live outputs: race_main_loop returns after it. */
void finish_race(void)
{
    finish_race_core();
}

/* $5052 tick_race_timers — no live outputs; the four entry flag bits it FORWARDS to the
   seeder's PHP residue are its only inputs (see the twin's header). */
void tick_race_timers(void)
{
    tick_race_timers_core(cpu.C, cpu.V, cpu.D, cpu.I);
}

/* $17C3 add_frame_time — X selects the clock; the flags out are the low byte's decimal add. */
void add_frame_time(void)
{
    FrameTimeExit e = add_frame_time_core(cpu.X);
    cpu.A = e.a; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
    cpu.D = 0;                                       /* $17FA CLD */
}

void update_position_display(void)
{
    PosDisplayExit e = update_position_display_core(cpu.X, cpu.Y);
    cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.C = e.c;
}

/* $3250 emit_driver_name — the pointer arrives in Y:A, X is the ambient OSWRCH register.  Y comes
   back as the loop's terminator ($0C) and the exit flags are that CPY's, so they are constants. */
void emit_driver_name(void)
{
    uint8_t x = cpu.X;
    uint8_t ch = emit_driver_name_core((uint16_t)(cpu.A | (cpu.Y << 8)), x);
    cpu.A = ch; cpu.X = x; cpu.Y = 0x0Cu;
    cpu.N = 0; cpu.Z = 1; cpu.C = 1;             /* $325C CPY #$0C with Y = $0C */
}

/* $37D6 print_bcd_digits — A is the BCD byte, X/Y are ambient.  ⚠ The PHA/PLA residue is NOT
   replayed: see the twin's header (the oracle's own residue address is not the 6502's). */
void print_bcd_digits(void)
{
    uint8_t x = cpu.X, y = cpu.Y;
    TextExit e = print_bcd_digits_core(cpu.A, x, y);
    cpu.A = e.a; cpu.X = x; cpu.Y = y;
    cpu.N = e.n; cpu.Z = e.z; cpu.C = e.c;
}

/* $37D0 print_bcd_digits_at — X/Y place the cursor, then fall through into the printer. */
void print_bcd_digits_at(void)
{
    uint8_t x = cpu.X, y = cpu.Y;
    TextExit e = print_bcd_digits_at_core(cpu.A, x, y);
    cpu.A = e.a; cpu.X = x; cpu.Y = y;
    cpu.N = e.n; cpu.Z = e.z; cpu.C = e.c;
}

/* $7B9C print_lap_time — A is the field mask, X the car index (and the ambient OSWRCH X). */
void print_lap_time(void)
{
    uint8_t x = cpu.X, y = cpu.Y;
    TextExit e = print_lap_time_core(cpu.A, x, y);
    cpu.A = e.a; cpu.X = x; cpu.Y = y;
    cpu.N = e.n; cpu.Z = e.z; cpu.C = e.c;
}

/* $3261 abort_if_quit_keys / $34D0 wait_dismiss_space / $34D2 wait_dismiss_key (twin #198).
   No entry registers except wait_dismiss_key's flag, which arrives in A ($666E PLA), and all
   three exit ABIs are dead: every call site RTSs or reloads X/Y on the next instruction. */
void abort_if_quit_keys(void) { abort_if_quit_keys_core(); }
void wait_dismiss_space(void) { wait_dismiss_space_core(); }
void wait_dismiss_key(void)   { wait_dismiss_key_core(cpu.A); }

/* $6687 prompt_driver_ready / $66D4 read_driver_name (twin #200) — no entry registers (both read
   player_car themselves) and no exit ABI: every caller reloads a register immediately. */
void prompt_driver_ready(void) { prompt_driver_ready_core(); }
void read_driver_name(void)    { read_driver_name_core(); }

/* $3E60 set_row_rule_glyphs — the row arrives in Y.  Result-only: the sole caller reloads X
   into the script index on the next instruction, and A/Y/flags are dead with it. */
void set_row_rule_glyphs(void)
{
    set_row_rule_glyphs_core(cpu.Y);
}

/* $3C6F print_race_class_name — no entry registers, and text_script_interp's exit ABI is dead. */
void print_race_class_name(void)
{
    print_race_class_name_core();
}

/* $43D0 / $43E7 the lap-value table column — X is the car index AND the ambient OSWRCH
   register; $43E7 takes the field's leading byte in A.  Both exit as print_spaces does. */
void print_lap_value_field(void)
{
    uint8_t x = cpu.X, y = cpu.Y;
    TextExit e = print_lap_value_field_core(x, y);
    cpu.A = e.a; cpu.X = x; cpu.Y = y;
    cpu.N = e.n; cpu.Z = e.z; cpu.C = e.c;
}

void print_lap_value_from_mid(void)
{
    uint8_t x = cpu.X, y = cpu.Y;
    TextExit e = print_lap_value_from_mid_core(cpu.A, x, y);
    cpu.A = e.a; cpu.X = x; cpu.Y = y;
    cpu.N = e.n; cpu.Z = e.z; cpu.C = e.c;
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
        mem[STACK_PAGE + cpu.S] = (uint8_t)pattern;
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

/* $51A8's body, WITHOUT the 6502-ABI entry marshal.  draw_dash_needles has already
   published the driving-model state vector when it reaches here, so re-marshalling it would be
   pure duplicated traffic; the shim below does it for the transliterated entry. */
static void dial_needle_angle_plot(void)
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

    uint8_t len = mem[MEM_dial_needle_dda_tbl + d.offset];  /* $51EB — line length / minor delta */
    mem[0x0083] = len;                           /* $83 point_delta_hi */
    math_hi     = len;                           /* $75 — DDA loop count */

    uint8_t org = mem[MEM_dial_needle_origin_lo_tbl + d.quadrant];
    plot_ptr_lo = (uint8_t)(org & 0xF8u);        /* $70 — needle origin address low */
    plot_ptr_hi = mem[MEM_dial_needle_origin_hi_tbl + d.quadrant]; /* $71 */

    /* $5202 sets up X=quadrant / A=plot_ptr_hi as well, but the plotter consumes only the start
       scan line, and both are dead at the caller (result-only fixture). */
    plot_line_octant_core((uint8_t)(org & 0x07u));
}

void dial_needle_angle(void)
{
    model_state_marshal_in();     /* the 16-bit driving-model state vector */
    dial_needle_angle_plot();
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
/* The marshal-IN below is oracle-only in production and stays on this 6502-ABI path for the
   harness; native callers enter at the _native split.  Full argument at build_track_geometry. */
    model_state_marshal_in();     /* READ ONLY here: the needles are drawn FROM the vector */
    draw_dash_needles_native();
}

void draw_dash_needles_native(void)
{
    /* ⚠ model_state's marshal-IN is READ ONLY here and lives in the shim above; this routine
       plots, so it must not publish either (a line can land inside $62D0..$62EE). */
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
    dial_needle_angle_plot();                     /* $513D — rev needle; falls into plot_line_octant.
                                                     Core-to-core: the model state is already live. */

    /* $5145-$5146 / $5186 — the routine's own PHP/PLP is balanced (S restored), but the pushed
       processor status stays on the stack as a residue at $0100+S.  It is the flags AFTER
       LSR steer_angle_lo: N=0, Z from the shifted value, C = bit 0; V/D/I carry through from the
       shared prefix (identical on both differential sides) and B/bit5 are set in a pushed copy.
       The later plot_line_octant pushes only below this cell, so the residue survives. */
    /* ⚠⚠ THIS in-marshal STAYS on the native path, and its POSITION is the reason: it sits after
       the two needle plots because a plotted line can land inside $62A0..$62A5, so what the lanes
       hold here is not what they held at entry.  Hoisting it into the shim would read the cells a
       plot too early. */
    car_angle_marshal_in();                       /* consumer: element 2 of the relocated array */
    uint16_t steerAng = car_angle_16[CAR_ANGLE_STEER];
    mem[STACK_PAGE + cpu.S] = (uint8_t)(0x30u                     /* bit5 = 1, B = 1 */
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

    /* $5192 mode5_addr -> plot_ptr; its exit line number is the plotter's start scan line
       ($5192 leaves it in Y and $51A4 hands it straight on).  X=row and A are dead. */
    Mode5Addr m = mode5_addr_core(n.originMasked, n.rowSel);

    shared_temp_77 = n.subPos;                    /* $5195-$519A (originBase<<1)&7 */
    hypot_min_hi   = 0x04;                        /* $519E */
    math_hi        = 0x06;                        /* $51A0-$51A2 — a 6-pixel line, plot_line_octant's count */
    /* ⚠ The $51A0 `LDA #6` is NOT dead: A stays 6 across the plot, and the port's interrupt seam
       publishes A into mos_irq_a ($FC) on every field, so dropping it moves a real mem[] byte
       (caught by `make determinism`, $00FC 0x06 -> 0x00). */
    hold_a_for_irq_seam(0x06);
    plot_line_octant_core(m.line);                /* $51A4 */
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

/* $41D0 select_text_variant / $65D3 print_standings_table (twin #199).  select_text_variant takes
   the layout variant in X; the table takes that same variant in X and its mode byte in A (which it
   PHAs and hands to wait_dismiss_key at the end).  Both are result-only: select_text_variant's
   callers reload X and Y on the next instruction, and every caller of the table either RTSs or
   reloads — what the page leaves behind is print_field_mask ($78), a mem[] cell. */
void select_text_variant(void)
{
    select_text_variant_core(cpu.X);
}

void print_standings_table(void)
{
    print_standings_table_core(cpu.X, cpu.A);
}

void text_script_interp(void)
{
    /* $4D7E — run the text script whose index is in X (twin #165).  The core does all the mem[]
       work (the plot_ptr2 reload, the character/space/command dispatch, the recursion) and threads
       X/Y to its callees itself; math_lo ($74) keeps its 6502 exit value until the $74/$75
       relocation.
       ⭐ EXIT Y IS LIVE, contrary to this shim's original contract: the 6502 leaves Y holding the
       offset of the $FF that ended the script, and print_standings_table hands exactly that on as
       the ambient OSWRCH register for its time column (twin #199 — the differential caught it as a
       MOS-log Y mismatch, ref $0C against native $06, the stale Y from the driver-name printer).
       X is unchanged (it is the script index throughout) and A is dead at every call site. */
    cpu.Y = text_script_interp_core(cpu.X);
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
    cpu.A   = (uint8_t)p.addr;                   /* $3CFD exit A -> plot_ptr2_lo */
    cpu.Y   = (uint8_t)(p.addr >> 8);            /* $3CF1 exit Y -> plot_ptr2_hi */
}

void menu_draw_gfx_bars(void)
{
    /* $3A50 — two teletext graphics bars into the MODE 7 menu page (twin #156).  The core does
       every mem[] write, including math_lo's dead 6502 exit value (the last row's end column).
       No stack residue (no PHA/PHP), and exit regs/flags are dead — front_end_menus reloads X
       the instant it returns, so nothing to reconstruct at the seam. */
    menu_draw_gfx_bars_core();
}

/* $3C50 prompt_wing_settings — no entry registers and no exit ABI: the pit-stop wait loop
   ($6560) runs the race loop the instant it returns. */
void prompt_wing_settings(void) { prompt_wing_settings_core(); }

/* $3EE0 console_read_two_digits — no entry registers; exit A is the number the caller stores
   into a wing setting, and X/Y/flags are dead there ($3C5D/$3C68 are both STA). */
void console_read_two_digits(void)
{
    cpu.A = console_read_two_digits_core();
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

uint8_t seed_car_track_position_flags(uint8_t c, uint8_t v, uint8_t d, uint8_t i)
{
    /* $635D — seed one car's grid position from a timer-entropy byte (#158).  A math_lo
       reader-nat: $74 is internal scratch, its per-path exit value written below.  Exit ABI:
       X live (the decremented car-index cursor the caller's loop reads); A/flags dead. */
    uint8_t x       = mem[MEM_car_seed_index];               /* $635D LDX car_seed_index */
    uint8_t entropy = (uint8_t)bus_read(USRVIA_T2CL);    /* $635F LDA $FE68 (one read, as the 6502) */

    /* $6362 PHP / $637B PLP is balanced (S restored) but the pushed P byte stays on the stack as a
       residue at $0100+S that the differential compares.  It is the flags AFTER LDA $FE68: N = the
       entropy byte's bit 7, Z set iff it was 0; C/V/D/I carry through from entry; bit5 and B are set
       in the pushed copy.  Nothing after (JSR/RTS are C calls in the oracle) rewrites this cell. */
    mem[STACK_PAGE + cpu.S] = (uint8_t)(0x30u
        | ((entropy & 0x80u) ? 0x80u : 0u)               /* N */
        | (v ? 0x40u : 0u)
        | (d ? 0x08u : 0u)
        | (i ? 0x04u : 0u)
        | ((entropy == 0u) ? 0x02u : 0u)                 /* Z */
        | (c ? 0x01u : 0u));                             /* C */
    /* ⚠ SABOTAGE NOTE on those four forwarded bits.  Falsifying V, C or I fails the fixture
       (4000/4000, 4000/4000 and 2026/4000 mismatch), and so does breaking the forwarding at
       tick_race_timers' shim (175/4000) — the chain is live end to end.  Dropping D alone
       PASSES, and that is explanation two, not a gap: the fixture pins `c.D = 0` because
       docs/static-map.md §Decimal mode inventories all eight `SED` sites and none of them is on
       any path that reaches this routine, so the bit is provably clear at every real entry and
       there is no input on which `| (d ? 0x08u : 0u)` and `| 0u` differ.  It is kept because the
       6502 pushes P, not a subset of it. */

    uint8_t mathlo;
    uint8_t xExit   = seed_car_track_position_core(x, entropy, &mathlo);
    math_lo = mathlo;                                    /* $6384/$6392 — $74 exit value per path */
    mem[MEM_car_seed_index] = xExit;                         /* $639F STA car_seed_index */
    return xExit;                                        /* $639C..$639F — decremented cursor */
}

/* The 6502-ABI shim — the four ambient flag bits out of `cpu` for the transliterated callers
   and the harness; `cpu.S` stays because the residue's ADDRESS is the stack pointer. */
void seed_car_track_position(void)
{
    cpu.X = seed_car_track_position_flags(cpu.C, cpu.V, cpu.D, cpu.I);
}

/* The one flag a caller of the seeder deliberately varies is the carry: reset_all_cars_for_
   session's $4D59 LSR halves the car index and leaves its bit 0 in C, still live when the PHP
   above captures it.  This form takes that bit and returns the decremented cursor, so the twin
   itself needs no cpu at all. */
uint8_t seed_car_track_position_with_carry(uint8_t carry)
{
    return seed_car_track_position_flags(carry ? 1u : 0u, cpu.V, cpu.D, cpu.I);
}

/* The marshal-IN below is oracle-only in production and stays on this 6502-ABI path for the
   harness; native callers enter at the _native split.  Full argument at build_track_geometry. */
void mirrors_update(void)
{
    /* $7B00 — the once-per-frame wing-mirror update (race_main_loop body, $1739).  Result-only:
       exit regs/flags are dead at that caller.  The core carries the pre-loop bracket (a math_lo
       reader-nativization); the segment loop stays here as the shim's own bookkeeping, but
       mirror_draw_car is now twin #165c and is called core-to-core.  All of
       shared_temp_84 / span_line_cursor / shared_temp_76 / math_lo are read LIVE from mem[] in the
       loop so the skip path (which writes none of them) stays byte-exact. */
    car_heading_marshal_in();
    mirrors_update_native();
}

void mirrors_update_native(void)
{
    uint8_t slot = mem[MEM_car_order + car_ahead];          /* the car ahead's object slot */
    MirrorSetup s;
    mirrors_update_setup_core(mem[MEM_car_flags_shape + slot],
                              mem[MEM_object_width + slot],
                              mem[MEM_object_bearing_hi + slot],
                              (uint8_t)(car_heading_v >> 8), &s);
    if (s.drawable) {
        math_lo          = s.half;      /* $74 — 6502 exit value, set only on this path */
        shared_temp_84   = s.bottom;    /* $84 — block bottom line */
        span_line_cursor = s.top;       /* $7F — block top line */
    }
    shared_temp_76 = s.heading;         /* $76 — set on both paths */

    for (int y = 5; y >= 0; --y) {      /* $7B2C-$7B47: segments 5..0 */
        uint8_t v;
        if (shared_temp_76 == mem[MEM_mirror_seg_bearing_tbl + y]) {
            v = shared_temp_84;                 /* the matching segment — draw the car (bottom line) */
        } else if (mem[MEM_mirror_seg_state + y] != 0) {
            v = 0x00;                           /* a still-set stale segment — redraw 0 to erase it */
        } else {
            continue;                           /* $7B46 — empty and stays empty */
        }
        mem[MEM_mirror_seg_state + y] = v;
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
        uint8_t idx = mem[MEM_marker_edge_index + y];             /* $1B18 — the edge point it hangs off */
        marker_count_saved = (uint8_t)y;                    /* $1B1B — Y parked across plot_object */
        uint8_t flags = mem[MEM_marker_flags + y];          /* $1B1D */
        if (flags & 0x20u)
            mem[0x38FEu] = 0x0F;                            /* $1B26 — bit 5 recolours the marker (SMC) */

        uint16_t edgeX = edge_x_word(idx);
        CornerMarker m;
        uint16_t offset = marker_offset_word(y);
        draw_corner_marker_core(offset, edgeX, mem[MEM_edge_y + idx], &m);

        math_lo = m.mathLo;                                 /* $74/$75/$76 — set on both paths */
        math_hi = m.mathHi;
        shared_temp_76 = m.temp76;
        if (m.draw) {
            plot_x     = m.plotX;                           /* $1B52 */
            plot_line  = m.plotLine;                        /* $1B57 */
            proj_width = m.projWidth;                       /* $1B6B */
            plot_shape = 0x06;                              /* $1B6F — always the marker shape */
            /* $1B71 — the slot index and a zero Y are plot_object's whole entry ABI, and its
               exit registers/flags are dead here (the loop reloads everything). */
            plot_object_core(idx, 0x00u, 0u);
        }
        mem[0x38FEu] = 0xF0;                                /* $1B74-$1B76 — restore the marker colour */
    }
    marker_count = 0x00;                                    /* $1B7F-$1B81 — the list is per-frame */
}

/* Exit A/N/Z from the second emit, X forced to $FF ($42E4, then emit-preserved), Y preserved
   from entry (both emits preserve it). */
void draw_gear_indicator(void)
{
    uint8_t y = cpu.Y;
    uint8_t block = draw_gear_indicator_core();
    cpu.X = 0xFFu; cpu.Y = y;
    cpu.A = block; cpu.N = (block >> 7) & 1u; cpu.Z = (block == 0);
}

/* Magnitude in A, sign in X, the dead-zone carry rebuilt from CMP #$0A ($504F).  Y is left as
   the OSBYTE reading the core's MOS call returned; V is dropped. */
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
    /* $27A4-$27AA: state_1[Y] - state_1[X], then FALL THROUGH into the shared tail at $27AB.
       The tail's only inputs are X, Y and the borrow this subtract leaves, so hand them to its
       core directly rather than parking them in cpu for the tail's own shim to read back.
       (cpu.A is not one of them — the tail recomputes the difference from car_distance_16 — so
       the byte this subtract leaves in A is overwritten by the exit ABI below either way.) */
    unsigned d = car_gap_lo_core(mem[MEM_car_section_along + cpu.Y], mem[MEM_car_section_along + cpu.X]);
    car_distance_marshal_in_one(cpu.X);            /* the two slots the gap is measured between */
    car_distance_marshal_in_one(cpu.Y);
    GapTail e = car_gap_tail_core(cpu.X, cpu.Y, (unsigned)!(d & 0x100));
    cpu.A = e.a; cpu.N = e.n; cpu.C = e.c;         /* V, Z dead at every caller */
}

void car_gap_tail(void)
{
    /* consumer of two slots of the relocated distance array — marshal exactly those two */
    car_distance_marshal_in_one(cpu.X);
    car_distance_marshal_in_one(cpu.Y);
    GapTail e = car_gap_tail_core(cpu.X, cpu.Y, cpu.C);   /* entry C is a genuine input */
    cpu.A = e.a; cpu.N = e.n; cpu.C = e.c;                /* V, Z dead at every caller */
}

/* $28F2 — the whole routine as a function of the car_order POSITION it is handed, so a native
   caller (move_and_draw_cars) reaches it without going through the register ABI. */
void stage_nearby_car_at_core(uint8_t orderIndex)
{
    uint8_t slot = mem[MEM_car_order + orderIndex];           /* $28F2 LDA $013C,X */
    saved_slot_index = slot;                              /* $28F5 STA $45 */
    shared_counter_42 = slot;                             /* $28F7 STA $42 */

    /* $28F9 TAX; $28FA LDY #$17; $28FC SEC; $28FD car_gap_tail — X=slot, Y=$17, C=1 in.  The
       car_gap_tail shim leaves cpu.X/cpu.Y untouched, so X is still slot at the tail call. */
    car_distance_marshal_in_one(slot);         /* the two slots this gap is measured between */
    car_distance_marshal_in_one(0x17u);
    GapTail g = car_gap_tail_core(slot, 0x17u, 1u);

    StageNearbyCar s = stage_nearby_car_core(g.a, g.c, slot);

    /* X is held from the $28F9 TAX through to the tail; both tails consume it as a value. */
    if (s.reject) {                                       /* $2911 reject_object_slot; return */
        reject_object_slot_core();                        /* its A/Y/N/Z are dead here */
        return;
    }
    hypot_max_marshal_in();  hypot_min_marshal_in();  bearing_marshal_in();
    place_car_world_coords_core(slot, s.y);               /* $2922 TAY; $2937 — exit X dead */
    hypot_max_marshal_out(); hypot_min_marshal_out(); bearing_marshal_out();
}

/* ⭐ THE IN-MARSHALS SIT ON THE SHIMS OF THIS PASS, NOT INSIDE ITS CORES, and here that is a
   measurement and not just tidiness: stage_nearby_car_at_core ran view_origin_marshal_in TWICE
   per call and move_and_draw_cars_core calls it seven times a frame, so the camera was being
   rebuilt from its byte lanes fourteen times a frame for a value nothing in the pass moves
   (view_origin diverged 6 times in 142258 round trips, car_heading 0 in 77840 --
   docs/wide-value-cleanup.md §IS THE MARSHALLING ORACLE-ONLY).  `make validate` enters at each
   shim and still gets its read; the native caller enters at move_and_draw_cars_core. */
void stage_nearby_car(void) { view_origin_marshal_in(); stage_nearby_car_at_core(cpu.X); }

/* $2637 — result-only: no register argument (car_ahead, zp_scratch_index and track_direction all
   come out of mem[]) and the body's next call reloads every register. */
void move_and_draw_cars(void)
{
    view_origin_marshal_in();    /* for the seven stage_nearby_car_at_core calls */
    car_heading_marshal_in();    /* ...and for draw_car_field_core's bearing measurements */
    move_and_draw_cars_core();
}

/* $66DF draw_car_field — the other-car draw pass.  Y/V/C arrive ambient and thread through the
   whole pass; X does not (draw_track_object reloads it from saved_slot_index).  A/N/Z are the
   last draw's. */
void draw_car_field(void)
{
    car_heading_marshal_in();
    SlotExit e = draw_car_field_core(cpu.Y, cpu.V, cpu.C);
    cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
}

/* $2692 check_car_pair — twin #163.  No meaningful entry registers (it loads its start position
   from zp_scratch_index immediately) and every exit register/flag is dead (both callers reload X),
   so the shim is a bare call — all state lives in mem[]. */
void check_car_pair(void)
{
    car_distance_marshal_in();                 /* it walks every pair in car_order: the whole array */
    check_car_pair_core();
}

void section_coord_add_delta(void)
{
    const uint8_t dlo[3] = { math_lo, math_hi, shared_temp_76 };
    const uint8_t dhi[3] = { mem[MEM_point_delta_hi + 0], mem[MEM_point_delta_hi + 1],
                             mem[MEM_point_delta_hi + 2] };
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
    /* ⚠ The exit flags are NOT dead: $5011's own `LDA #$00` leaves Z = 1 / N = 0, and
       update_lap_timers' $1054 `BEQ $106F` reads that Z as an UNCONDITIONAL jump.  A shim that
       set A alone let the transliterated caller fall through into the readout countdown and
       decrement lap_time_show_timer a second time (caught by twin #177's fixture, 2026-09-05). */
    cpu.A = 0x00u;
    cpu.Z = 1u;
    cpu.N = 0u;
}

/* ---------------------------------------------------------------------------
   The crash / restart subtree's 6502-ABI shims (twins #167-#171).
   --------------------------------------------------------------------------- */

/* $0E74 — the exit ABI is live (A lands in mos_irq_a on the Amiga's interrupt seam) and it is
   path-dependent, so the core hands back the whole register/flag set. */
void engine_sound_update(void)
{
    int pushedPitch;
    SlotExit e = engine_sound_update_core(cpu.X, cpu.Y, cpu.V, cpu.C, &pushedPitch);
    if (pushedPitch >= 0)                    /* $0EAA's PHA/$0EB0's PLA leave it at $0100+S */
        mem[STACK_PAGE + cpu.S] = (uint8_t)pushedPitch;
    cpu.A = e.a; cpu.X = e.x; cpu.Y = e.y;
    cpu.N = e.n; cpu.Z = e.z; cpu.V = e.v; cpu.C = e.c;
}

/* $1805 — result-only: the routine takes no register argument (it reads session_is_race,
   zp_scratch_index and the circuit's own cells out of mem[]) and leaves nothing live. */
void reset_driving_variables(void) { reset_driving_variables_core(); }

/* $261F — result-only: no register argument, and both callers reload X immediately after.
   A = the last slot's new flags byte and X = $FF are pure residue nobody reads. */
void reject_all_object_slots(void)
{
    reject_all_object_slots_core();
    cpu.A = (uint8_t)(mem[0x018Cu] | 0x80u);     /* the $2621-$2626 residue, for the diff */
    cpu.X = 0xFFu;
    cpu.N = 1u; cpu.Z = 0u;                      /* the ORA #$80 that ended the loop */
}

/* $0FFE — result-only.  Both arms end in a callee whose exit ABI nobody reads (race_main_loop's
   body ignores it entirely), and the core does its own PHP/PLP on the real stack pointer, so
   there is nothing left for the shim to marshal in either direction. */
void update_lap_timers(void)
{
    update_lap_timers_core(cpu.X, cpu.Y,
                           (uint8_t)((cpu.D ? 0x08u : 0u) | (cpu.I ? 0x04u : 0u)));
}

/* $4F39 — result-only; the whole product is text_out_via_mos plus whatever the script paints. */
void enter_mos_text_mode(void)
{
    enter_mos_text_mode_core();
}

/* $4F23 — result-only apart from the CLI at $4F35, which PHP still composes from cpu.I. */
void irq1v_release(void)
{
    irq1v_release_core(cpu.Y);      /* Y is the ambient value sound_stop_all's OSBYTEs carry */
    cpu.I = 0;                      /* $4F35 CLI (the $4F23 SEI is over by then) */
}

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
    model_state_marshal_in();                    /* it kicks element 2, the heading step */
    begin_scrape_core(cpu.A, cpu.X);             /* A carries the clamped yaw kick in from $1135 */
    model_state_marshal_out();
    sound_queue_exit_abi(SOUND_SLOT_IMPACT);     /* $1C18 JSR / $1C1B RTS — the tail call's ABI */
}

void check_crash(void)
{
/* The marshal-IN below is oracle-only in production and stays on this 6502-ABI path for the
   harness; native callers enter at the _native split.  Full argument at build_track_geometry. */
    model_state_marshal_in();     /* the 16-bit driving-model state vector */
    edge_nearest_marshal_in();            /* the distance it tests to decide the car is off-track */
    check_crash_native();
}

void check_crash_native(void)
{
    uint8_t entryA = cpu.A, entryX = cpu.X;
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
    model_state_marshal_out();    /* ...and publish it back to mem[] */
}

/* The marshal-IN below is oracle-only in production and stays on this 6502-ABI path for the
   harness; native callers enter at the _native split.  Full argument at build_track_geometry. */
void build_player_car(void)
{
    view_origin_marshal_in();
    build_player_car_native();
}

void build_player_car_native(void)
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
    view_origin_marshal_out();
}

void step_delta_halve(void)
{
    /* A is the high byte of component 0 as it was BEFORE the shift (the last LDA $83,X), and C
       is the bit rotated out of that component's LOW byte — the only two register residues. */
    uint8_t hi0 = mem[MEM_point_delta_hi + 0];
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
    view_origin_marshal_in();
    project_object_slot_core(cpu.X, cpu.A);
    hypot_max_marshal_out(); hypot_min_marshal_out(); bearing_marshal_out();
}

void project_object_coord(void)
{
    view_origin_marshal_in();
    project_object_slot_core(0xFDu, cpu.A);      /* $2A5D LDX #$FD — the object_coord pair */
    hypot_max_marshal_out(); hypot_min_marshal_out(); bearing_marshal_out();
}

void mirror_draw_car(void)
{
    /* $7FB6 — 6502-ABI shim for twin #165c.  A is the car block's bottom line, Y the segment.
       Exit regs/flags are dead: the one caller, mirrors_update, reloads both per segment. */
    mirror_draw_car_core(cpu.A, cpu.Y);
}

void car_reset_best_lap(void)
{
    /* $40EB — X is the car (#203).  Exit ABI: A = $10 from the last LDA, N/Z clear; X, Y unchanged. */
    car_reset_best_lap_core(cpu.X);
    cpu.A = 0x10u; cpu.N = 0; cpu.Z = 0;
}

void all_cars_reset_best_lap(void)
{
    /* $42EC (#203).  Exit ABI: the DEX that ends the loop leaves X = $FF with N set, and A is the
       last car_reset_best_lap's $10. */
    all_cars_reset_best_lap_core();
    cpu.X = 0xFFu; cpu.N = 1; cpu.Z = 0; cpu.A = 0x10u;
}

void add_tally_to_lap_total(void)
{
    /* $6698 — X the standings column, Y the car (#203).  Exit ABI: A and C are the high byte's
       BCD add; N/Z/V are whatever the 6502's ADC left and its one caller (tally_bcd_column,
       whose own exit ABI is X-only) never reads them, so they are not reconstructed here. */
    BcdAdd hi = add_tally_to_lap_total_core(cpu.X, cpu.Y);
    cpu.A = hi.val;
    cpu.C = hi.carry;
}

void compute_segment_scale(void)
{
    /* $44CF — X is the race class / track-scale index. */
    compute_segment_scale_core(cpu.X);
}

void sort_cars_by_key(void)
{
    /* $0F64 — A is the sort-key selector (bit 6 = key B, bit 7 = key C, else key A). */
    sort_cars_by_key_core(cpu.A);
}

void reset_all_cars_for_session(void)
{
    /* $4D4D — X is the seeding cursor / race class / track-scale index (#204).  Exit ABI: the
       walk ends with the cursor at 0, and $4D61's LDA #0 is the last load, so A = 0 with Z set
       and N clear; X = 0 too. */
    reset_all_cars_for_session_core(cpu.X);
    cpu.A = 0x00u; cpu.X = 0x00u; cpu.Z = 1; cpu.N = 0;
}

/* $5A25 tally_bcd_column — X is the statistics column, exit Y the car it belongs to. */
void tally_bcd_column(void) { cpu.Y = tally_bcd_column_core(cpu.X); }

/* $63E0/$655A/$655C the front-end chain (twin #205).  front_end_menus never returns, and
   enter_session's exit registers are dead at all four of its call sites. */
void front_end_menus(void)        { front_end_menus_core(); }
void enter_practice_session(void) { enter_practice_session_core(); }
void enter_session(void)          { enter_session_core(cpu.A); }

/* $17FC/$4D70/$4D74/$4D76 the status-row printers (twin #206).  Entry X = the script index (and
   A = the scan line at $4D76 only); exit Y is text_script_interp's live terminator offset, X is
   the script index it ran, and A is dead at every call site — text_script_interp's own twin does
   not model it either. */
void print_message_at_row(void)    { cpu.Y = print_message_at_row_core(cpu.A, cpu.X); }
void print_message_lower_row(void) { cpu.Y = print_message_lower_row_core(cpu.X); }
void print_message_upper_row(void) { cpu.Y = print_message_upper_row_core(cpu.X); }
void print_message_pair(void)      { cpu.Y = print_message_pair_core(cpu.X); cpu.X = 0x2Du; }

void console_io(void)
{
    cpu.A = console_io_core((uint16_t)(cpu.A | (cpu.Y << 8)), cpu.X);
    cpu.X = 0x00u;                                   /* every OSBYTE it issues returns X = 0 */
    cpu.Y = shared_temp_77;                          /* the field width the CPY exited on */
    cpu.Z = 1; cpu.C = 1; cpu.N = 0;                 /* $6352 CPY, equal */
}

/* The road walk's direction cluster (twins #208-#212).  Every caller is a JSR whose exit
   registers are dead — road_edge_walk's tail returns or falls into its own next test — so the
   shims marshal nothing but the one argument $1420 takes in X. */
void build_section_ahead(void)    { build_section_ahead_core(); }
void rebuild_walk_reversed(void)  { rebuild_walk_reversed_core(cpu.X); }
void rebuild_walk_backward(void)  { rebuild_walk_backward_core(); }
void reverse_walk_direction(void) { reverse_walk_direction_core(); }

void clear_surface_buffers(void) { clear_surface_buffers_core(); }   /* exit ABI dead */
void fill_line_surface(void)     { fill_line_surface_core(); }       /* exit ABI dead */
void advance_player_section(void) { advance_player_section_core(cpu.X, cpu.Y); }   /* exit ABI dead */
void abort_to_front_end(void) { abort_to_front_end_core(cpu.C); }   /* the ROR's input carry */
void engine_init(void) { engine_init_core(); }
void engine_main(void) { engine_main_core(); }
void hw_init(void) { hw_init_core(cpu.Y); }   /* Y is the OSBYTE $9A call's input Y */
