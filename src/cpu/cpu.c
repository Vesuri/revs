#include "cpu.h"

/* ⭐⭐ S IS INHERITED FROM THE MOS, NOT SET UP BY THE GAME — and it must be initialised here.
 *
 * Nothing in REVS2 ever loads the stack pointer: engine_init ($386D) does `TSX / STX $6B`, i.e.
 * it SAVES whatever the MOS handed it (and $3275's `TXS` later restores that same value to
 * unwind back to the front end).  So the engine runs on the OS's stack pointer, and `mem[]` built
 * from the disc image does not model it — the same provisional-zero-page gap
 * docs/bbc-reference-loop.md warns about, except this one is a register.
 *
 * MEASURED on a real BBC at the engine entry (`make refloop-comp`, which prints $6B): **$F8**.
 * Mid-race, 200 frames into a 20-car competition session, S = $F2 — the engine uses six bytes and
 * is perfectly balanced, with no drift at all.
 *
 * ⚠⚠ WHY A ZERO DEFAULT WAS SILENT DATA CORRUPTION, NOT A CRASH.  `cpu` is a global, so S began
 * at 0 and the FIRST push wrote mem[$0100] and wrapped to $FF.  Page 1's bottom is not stack in
 * Revs — it is eight 20-entry per-car arrays ($0100 $0114 $0128 $013C $0150 $0164 $0178 $018C) —
 * so every wrap scribbled pushed bytes across the field.  car_order came out full of ASCII
 * ('0','1','2', the digits being printed at the time).  Practice mode never noticed, because
 * $2637's `LDA $5F3B / BMI` skips the whole multi-car path when the practice flag is set; a
 * COMPETITION race then hung forever, because find_player_neighbours could not find the player,
 * stored X = $FF into $0003, and check_car_pair's field walk compares against it.
 *
 * Five phases of measurement missed this because every one of them was a practice session.
 */
Cpu6502 cpu = { .S = 0xF8 };

/* The 6502 stack watermark.  Page 1's bottom is per-car ARRAY space in Revs, not stack (the
   long note in cpu.h has the mechanism and the measured real-BBC value of $F2), so a low S is
   silent data corruption rather than a near-overflow.  Reported, never assumed. */
unsigned char g_stackLow = 0xFF;
unsigned long g_stackTrespass = 0;

/* The pending one-frame stack drop.  See the long note in cpu.h: $2F7E's RTS returns two
   levels up, which is how the unrolled road-span chains exit, and nothing else in the
   image does it.  Zero at rest — a non-zero value outside a road_span_plot call is a bug
   in the placement, not a state worth carrying. */
uint8_t cpu_unwind;

/* volatile: shared between the main thread (game loop) and the audio
   callback thread (VBI handler).  Without volatile, -O2 proves that
   spin-wait conditions like "while (mem[0x8E] == 0)" never change in
   the loop body and folds them to infinite loops.

   aligned(4): the Amiga asm twins store PAIRS of adjacent mem[] bytes with a single
   MOVE.W at an even absolute offset (mem[] is little-endian, the 68000 big-endian, so
   the value is byte-swapped first — see TerrainSubdivideAssembler.s sd_out).  A 68000
   faults on a word access to an ODD address, so the base must be even.  GCC happened to
   give this array 2-byte alignment anyway; say so, rather than depend on it.           */
volatile uint8_t mem[65536] __attribute__((aligned(4)));
