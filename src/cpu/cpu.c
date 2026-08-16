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

/* ⭐ The HIGH watermark, and it is not symmetry for its own sake.  S starts at the MOS's $F8 and
   a balanced engine never exceeds it, so anything above $F8 is an unbalanced PULL — which walks
   S to $FF, WRAPS it to $00, and from there pushes land on car_order.  Without this counter that
   arrives looking like a downward leak and gets chased in the wrong direction. */
unsigned char g_stackHigh = 0x00;

/* ⭐ WHERE the watermark fell, not just how far.  A leak of a few bytes per frame is invisible
 * in a total and obvious in a BACKTRACE, and the transliteration keeps the 6502 call graph on
 * the C call stack, so a host backtrace names the 6502 routines directly.
 *
 * Host only, and off unless $REVS_STACK_TRAP is set to a hex threshold (e.g. REVS_STACK_TRAP=f0):
 * the first push that takes S below it prints one backtrace and then disarms, so a leak that
 * fires thousands of times a frame still produces exactly one readable report.
 *
 * ⚠ Deliberately hung off the new-watermark branch in PUSH(), which is already there and is
 * taken O(depth) times, not O(pushes) — the trap costs nothing on the hot path. */
#if defined(REVS_STACK_TRAP)
#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>
static void report(const char* what, unsigned char s, unsigned at)
{
    void* bt[64];
    int n;
    fprintf(stderr, "\n=== STACK TRAP: S %s $%02X (limit $%02X) ===\n", what, s, at);
    n = backtrace(bt, 64);
    backtrace_symbols_fd(bt, n, 2);
    fflush(stderr);
}

static int  s_trapArmed = -1;      /* -1 = not yet read from the environment */
static unsigned s_trapAt = 0;
void cpu_stack_watermark(unsigned char s)
{
    if (s_trapArmed < 0) {
        const char* e = getenv("REVS_STACK_TRAP");
        s_trapAt   = e ? (unsigned)strtoul(e, 0, 16) : 0u;
        s_trapArmed = s_trapAt ? 1 : 0;
    }
    if (s_trapArmed != 1 || s >= s_trapAt) return;
    s_trapArmed = 0;               /* one report, then disarm */
    report("fell to", s, s_trapAt);
}

/* ⭐⭐ THE OTHER DIRECTION, and on this project it is the one that bites.  A 6502 stack can leak
 * UP as well as down: a PULL with no matching push walks S toward $FF and then WRAPS to $00 —
 * at which point pushes land on mem[$0100], i.e. car_order, and the failure looks exactly like a
 * downward leak.  Chasing it as one wastes the run: the low-watermark trap reports "S fell to
 * $00" having never seen $DF, which is the tell that S ARRIVED there rather than descended.
 * S starts at $F8 (the MOS's value), so anything above that is an unbalanced pull. */
static int  s_ceilArmed = -1;
static unsigned s_ceilAt = 0;
void cpu_stack_ceiling(unsigned char s)
{
    if (s_ceilArmed < 0) {
        const char* e = getenv("REVS_STACK_CEIL");
        s_ceilAt   = e ? (unsigned)strtoul(e, 0, 16) : 0u;
        s_ceilArmed = s_ceilAt ? 1 : 0;
    }
    if (s_ceilArmed != 1 || s <= s_ceilAt) return;
    s_ceilArmed = 0;
    report("rose to", s, s_ceilAt);
}
#endif

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
MEM_QUAL uint8_t mem[65536] __attribute__((aligned(4)));
