#include "cpu.h"

Cpu6502 cpu;

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
