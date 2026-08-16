#ifndef CPU_H
#define CPU_H
/* ⚠ On the Amiga C++ build the integer types already arrive via the force-included
   framework/SASCCompat.h, and the framework's own compat-include/stdint.h CONTRADICTS it
   (`signed char` vs plain `char` for int8_t) — including both is a hard error.  The C
   build of this same header gets no force-include and does need stdint.h, and so does
   every host build, so the condition is C++-and-Amiga, not Amiga. */
#if !(defined(__cplusplus) && defined(REVS_PLATFORM_AMIGA))
#include <stdint.h>
#endif
#include "mem_decl.h"

/* 6502 register state.  Flags are stored unpacked (0/1 per flag) for
   readable branch conditions in the transliterated C.  PHP/PLP pack/
   unpack via P_pack/P_unpack. */
typedef struct {
    uint8_t A, X, Y;
    uint8_t S;          /* stack pointer */
    /* status flags */
    uint8_t N, V, Z, C;
    uint8_t I, D;       /* interrupt-disable, decimal (D honoured by ADC/SBC for BCD) */
} Cpu6502;

extern Cpu6502 cpu;
/* The 6502 address space.  MEM_QUAL is `volatile` ONLY under BODY_IN_ISR — dropping it in the
   shipping model is worth 10% of the frame, and mem_decl.h carries the argument for why that
   is sound here. */
extern MEM_QUAL uint8_t mem[65536];

/* ---------- the 6502 stack-drop return ----------------------------------------
 * ⭐⭐ ONE routine in Revs returns TWO LEVELS UP, and C cannot express it.
 *
 * $2F7E does `TSX / INX / INX / TXS` and then `RTS`: it throws away its own caller's
 * return address, so the RTS pops the level ABOVE that.  Revs uses it as the exit from
 * the four unrolled road-span chains ($2D17, $2D9A, $2E20, $2E99): each chain repeatedly
 * `JSR road_span_plot[_2]`, and when the plotter finds `Y == $82` (the last column) it
 * branches to $2F7E, drops the plotter's frame, and RTSes straight out of the CHAIN to
 * the chain's caller.
 *
 * The transliteration models `TXS` as `cpu.S = cpu.X`, which is faithful to the register
 * and does nothing whatever to the C call stack — so the plotter returned normally and the
 * chain kept looping.  ⚠ MEASURED CONSEQUENCE: the chain's inner loop is
 * `plot / ADC $83 / BCC`, so once $83 reads 0 (it does, at $82 == Y) the port WEDGES —
 * pc parked in FUN_2e99, g_fpsFrames frozen, ~112 s into a STRAIGHT_TO_RACE run.  That is
 * the "intermittent stall, unexplained" of docs/perf-method.md; it is neither.
 *
 * The model: an unwind flag, set where the drop happens and consumed by the call site of
 * the routine whose frame was dropped, which then returns as the RTS would have.  Exactly
 * one level — two bytes is one return address, so the flag never needs a counter.  The
 * transpiler places both halves; see STACK_DROP_TXS / UNWIND_CALLEES in
 * tools/transpile.py.  ⚠ Not volatile and not in Cpu6502: it is pure control flow within
 * one call chain, never interrupt state, and `make validate` diffs the Cpu6502 struct.
 *
 * ⚠⚠ AND THE TXS AT THAT SITE EMITS NO `cpu.S` WRITE AT ALL (fixed 2026-08-15).  Keeping the
 * register write "because it is faithful" was a 2-byte-per-span LEAK: the bytes `INX/INX`
 * discards are a RETURN ADDRESS, and this model keeps return addresses on the C stack, so
 * nothing ever cancels the +2.  S climbed past $F8, wrapped $FF -> $00, and pushes then landed
 * on mem[$0100] = car_order — which hung a COMPETITION race in check_car_pair's field walk while
 * practice mode (which skips the multi-car path) looked fine.  g_stackHigh is the counter that
 * says whether it is back: it must stay at $F8.  ⭐ The general rule this cost a day of two
 * separate hunts to learn: when a 6502 idiom manipulates S to talk about RETURN ADDRESSES, the
 * faithful transliteration is to model the CONTROL FLOW and leave S alone — modelling the
 * register instead is a silent leak, and modelling neither is a hang.  */
extern uint8_t cpu_unwind;
#define UNWIND_SET()    do { cpu_unwind = 1; } while(0)
#define UNWIND_TAKEN()  (cpu_unwind ? (cpu_unwind = 0, 1) : 0)

/* ---------- flag helpers ------------------------------------------ */
#define UPD_NZ(v)  do { uint8_t _nzv=(uint8_t)(v); cpu.N=_nzv>>7; cpu.Z=(_nzv==0); } while(0)

static inline uint8_t P_pack(void) {
    return (cpu.N<<7)|(cpu.V<<6)|0x30|(cpu.D<<3)|(cpu.I<<2)|(cpu.Z<<1)|cpu.C;
}
static inline void P_unpack(uint8_t p) {
    cpu.N=(p>>7)&1; cpu.V=(p>>6)&1; cpu.D=(p>>3)&1;
    cpu.I=(p>>2)&1; cpu.Z=(p>>1)&1; cpu.C=p&1;
}

/* ---------- stack -------------------------------------------------- */
/* ⚠⚠ PAGE ONE IS NOT ALL STACK.  Revs puts EIGHT 20-entry per-car arrays in the BOTTOM of the
 * stack page — $0100, $0114, $0128, $013C (car_order), $0150, $0164, $0178, $018C — on the
 * assumption that S never descends past $019F.  A real BBC honours that: measured mid-race in a
 * 20-car competition session, S = $F2 (`make refloop-comp`).
 *
 * So S is a CORRECTNESS INVARIANT here, not bookkeeping.  The moment it drops below $A0 a
 * PHA/PHP writes into car_order, and the failure that surfaces is nothing like a stack bug:
 * find_player_neighbours ($63A2) stops finding the player, stores X = $FF into $0003, and
 * check_car_pair's field walk ($2797 `car_index_inc / CPX $03`) never terminates — the port
 * HANGS.  Measured: S = $B8 and falling, with ASCII ('0','1','2') sitting in car_order.
 *
 * g_stackLow is therefore tracked and reported rather than trusted.  A silent leak here reads as
 * "competitor cars don't work", which is where an hour goes.
 */
extern unsigned char g_stackLow;      /* lowest S ever seen (starts $FF) */
extern unsigned char g_stackHigh;     /* HIGHEST S ever seen (starts $00) — see below */
extern unsigned long g_stackTrespass; /* pushes that landed in the per-car arrays */
#define STACK_FLOOR 0xA0              /* $019F is the top of car_flags_1 */
/* ⭐ `make STACK_TRAP=1` (host) + REVS_STACK_TRAP=<hex S> prints ONE backtrace at the first push
   below that S — see cpu_stack_watermark() in cpu.c.  Compiles to nothing otherwise. */
#if defined(REVS_STACK_TRAP)
#ifdef __cplusplus
extern "C" {
#endif
void cpu_stack_watermark(unsigned char s);
void cpu_stack_ceiling(unsigned char s);
#ifdef __cplusplus
}
#endif
#define STACK_WATERMARK_HOOK(s)  cpu_stack_watermark(s)
#define STACK_CEILING_HOOK(s)    cpu_stack_ceiling(s)
#else
#define STACK_WATERMARK_HOOK(s)  ((void)0)
#define STACK_CEILING_HOOK(s)    ((void)0)
#endif
#define PUSH(v)  do { mem[0x100|cpu.S]=(uint8_t)(v); \
                      if (cpu.S < g_stackLow) { g_stackLow = cpu.S; \
                                                STACK_WATERMARK_HOOK(cpu.S); } \
                      if (cpu.S < STACK_FLOOR) g_stackTrespass++; \
                      cpu.S--; } while(0)
/* ⚠ The ceiling matters as much as the floor: S starts at $F8, so a PULL that takes it higher is
   unbalanced, and a few more walk it to $FF and WRAP it to $00 — where pushes hit car_order. */
#define PULL(v)  do { cpu.S++; (v)=mem[0x100|cpu.S]; \
                      if (cpu.S > g_stackHigh) { g_stackHigh = cpu.S; \
                                                 STACK_CEILING_HOOK(cpu.S); } } while(0)
#define PHA()    PUSH(cpu.A)
#define PLA()    do { PULL(cpu.A); UPD_NZ(cpu.A); } while(0)
#define PHP()    PUSH(P_pack())
#define PLP()    do { uint8_t _p; PULL(_p); P_unpack(_p); } while(0)

/* ---------- load / store ------------------------------------------ */
#define LDA(v)  do { cpu.A=(uint8_t)(v); UPD_NZ(cpu.A); } while(0)
#define LDX(v)  do { cpu.X=(uint8_t)(v); UPD_NZ(cpu.X); } while(0)
#define LDY(v)  do { cpu.Y=(uint8_t)(v); UPD_NZ(cpu.Y); } while(0)

/* ---------- transfer ---------------------------------------------- */
#define TAX()   do { cpu.X=cpu.A; UPD_NZ(cpu.X); } while(0)
#define TAY()   do { cpu.Y=cpu.A; UPD_NZ(cpu.Y); } while(0)
#define TXA()   do { cpu.A=cpu.X; UPD_NZ(cpu.A); } while(0)
#define TYA()   do { cpu.A=cpu.Y; UPD_NZ(cpu.A); } while(0)
#define TSX()   do { cpu.X=cpu.S; UPD_NZ(cpu.X); } while(0)
/* ⚠ TXS is the OTHER way S can rise, and on this image it is the two-level-RTS idiom ($2F81,
   `TSX/INX/INX/TXS`) — so it is watched by the same ceiling as PULL.  A TXS that raises S
   without the two C-level returns that are supposed to accompany it leaks 2 bytes a time. */
#define TXS()   do { cpu.S=cpu.X; \
                     if (cpu.S > g_stackHigh) { g_stackHigh = cpu.S; \
                                                STACK_CEILING_HOOK(cpu.S); } } while(0)

/* ---------- arithmetic -------------------------------------------- */
/* ADC: A = A + v + C.  Honours decimal mode (cpu.D) — a 1985 6502 game will use
 * BCD for the lap/score counters, so SED/ADC must produce packed BCD
 * (09+01 -> 10, not 0A) with a decimal carry between digits.
 * NOTE: evaluate the operand EXACTLY ONCE — `v` may have side effects (any
 * bus_read of an I/O register can be read-clocked hardware).  Double-evaluating
 * it made `ADC <hwreg>` read the register twice on the Atari port, desyncing a
 * read-clocked LFSR from the real 6502.  Don't reintroduce it.
 * Decimal flag quirks follow the NMOS 6502: Z from the binary result, V from the
 * binary overflow, N from the high nibble; C is the decimal carry. */
#define ADC(v) do { \
    uint8_t _v = (uint8_t)(v); \
    uint16_t _t = (uint16_t)cpu.A + _v + cpu.C; \
    cpu.V = ((~(cpu.A ^ _v) & (cpu.A ^ (uint8_t)_t)) >> 7) & 1; \
    if (cpu.D) { \
        uint16_t _al = (uint16_t)(cpu.A & 0x0F) + (_v & 0x0F) + cpu.C; \
        uint16_t _ah = (uint16_t)(cpu.A >> 4) + (_v >> 4); \
        if (_al > 9) { _al += 6; _ah += 1; } \
        cpu.Z = ((uint8_t)_t == 0) ? 1 : 0; \
        cpu.N = (_ah & 0x08) ? 1 : 0; \
        if (_ah > 9) _ah += 6; \
        cpu.C = (_ah > 0x0F) ? 1 : 0; \
        cpu.A = (uint8_t)(((_ah << 4) | (_al & 0x0F)) & 0xFF); \
    } else { \
        cpu.C = (_t > 0xFF) ? 1 : 0; \
        cpu.A = (uint8_t)_t; \
        UPD_NZ(cpu.A); \
    } \
} while(0)

/* SBC: A = A - v - (1-C).  In binary mode == ADC(~v).  In decimal mode the NMOS
 * 6502 sets all flags (C/Z/N/V) exactly as the binary subtraction and only the A
 * register gets the decimal correction (low nibble, then high nibble). */
#define SBC(v) do { \
    uint8_t _sv = (uint8_t)(v); \
    uint8_t _nv = (uint8_t)~_sv; \
    uint16_t _t = (uint16_t)cpu.A + _nv + cpu.C; \
    cpu.V = ((~(cpu.A ^ _nv) & (cpu.A ^ (uint8_t)_t)) >> 7) & 1; \
    uint8_t _binres = (uint8_t)_t; \
    if (cpu.D) { \
        int _al = (int)(cpu.A & 0x0F) - (int)(_sv & 0x0F) + (int)cpu.C - 1; \
        if (_al < 0) _al = ((_al - 6) & 0x0F) - 0x10; \
        int _ar = (int)(cpu.A & 0xF0) - (int)(_sv & 0xF0) + _al; \
        if (_ar < 0) _ar -= 0x60; \
        cpu.C = (_t > 0xFF) ? 1 : 0; \
        cpu.A = (uint8_t)(_ar & 0xFF); \
        UPD_NZ(_binres); \
    } else { \
        cpu.C = (_t > 0xFF) ? 1 : 0; \
        cpu.A = _binres; \
        UPD_NZ(cpu.A); \
    } \
} while(0)

/* ---------- compare ----------------------------------------------- */
#define CMP(v) do { uint8_t _v=(uint8_t)(v); uint8_t _d=cpu.A-_v; \
    cpu.N=_d>>7; cpu.Z=(cpu.A==_v); cpu.C=(cpu.A>=_v); } while(0)
#define CPX(v) do { uint8_t _v=(uint8_t)(v); uint8_t _d=cpu.X-_v; \
    cpu.N=_d>>7; cpu.Z=(cpu.X==_v); cpu.C=(cpu.X>=_v); } while(0)
#define CPY(v) do { uint8_t _v=(uint8_t)(v); uint8_t _d=cpu.Y-_v; \
    cpu.N=_d>>7; cpu.Z=(cpu.Y==_v); cpu.C=(cpu.Y>=_v); } while(0)

/* ---------- increment / decrement --------------------------------- */
#define INX()      do { cpu.X++; UPD_NZ(cpu.X); } while(0)
#define INY()      do { cpu.Y++; UPD_NZ(cpu.Y); } while(0)
#define DEX()      do { cpu.X--; UPD_NZ(cpu.X); } while(0)
#define DEY()      do { cpu.Y--; UPD_NZ(cpu.Y); } while(0)
#define INC_M(a)   do { uint8_t _v=bus_read(a)+1; bus_write(a,_v); UPD_NZ(_v); } while(0)
#define DEC_M(a)   do { uint8_t _v=bus_read(a)-1; bus_write(a,_v); UPD_NZ(_v); } while(0)

/* ---------- logical ----------------------------------------------- */
#define AND(v)  do { cpu.A &= (uint8_t)(v); UPD_NZ(cpu.A); } while(0)
#define ORA(v)  do { cpu.A |= (uint8_t)(v); UPD_NZ(cpu.A); } while(0)
#define EOR(v)  do { cpu.A ^= (uint8_t)(v); UPD_NZ(cpu.A); } while(0)
#define BIT(v)  do { uint8_t _v=(uint8_t)(v); \
    cpu.N=_v>>7; cpu.V=(_v>>6)&1; cpu.Z=(cpu.A & _v)==0; } while(0)

/* ---------- shift / rotate ---------------------------------------- */
#define ASL_A()  do { cpu.C=cpu.A>>7; cpu.A<<=1; UPD_NZ(cpu.A); } while(0)
#define LSR_A()  do { cpu.C=cpu.A&1;  cpu.A>>=1; UPD_NZ(cpu.A); } while(0)
#define ROL_A()  do { uint8_t _c=cpu.C; cpu.C=cpu.A>>7; cpu.A=(cpu.A<<1)|_c; UPD_NZ(cpu.A); } while(0)
#define ROR_A()  do { uint8_t _c=cpu.C; cpu.C=cpu.A&1; cpu.A=(cpu.A>>1)|(_c<<7); UPD_NZ(cpu.A); } while(0)

#define ASL_M(a) do { uint8_t _v=bus_read(a); cpu.C=_v>>7; _v<<=1; bus_write(a,_v); UPD_NZ(_v); } while(0)
#define LSR_M(a) do { uint8_t _v=bus_read(a); cpu.C=_v&1;  _v>>=1; bus_write(a,_v); UPD_NZ(_v); } while(0)
#define ROL_M(a) do { uint8_t _v=bus_read(a),_c=cpu.C; cpu.C=_v>>7; _v=(_v<<1)|_c; bus_write(a,_v); UPD_NZ(_v); } while(0)
#define ROR_M(a) do { uint8_t _v=bus_read(a),_c=cpu.C; cpu.C=_v&1; _v=(_v>>1)|(_c<<7); bus_write(a,_v); UPD_NZ(_v); } while(0)

/* ---------- flag ops ---------------------------------------------- */
#define CLC() do { cpu.C=0; } while(0)
#define SEC() do { cpu.C=1; } while(0)
#define CLI() do { cpu.I=0; } while(0)
#define SEI() do { cpu.I=1; } while(0)
#define CLD() do { cpu.D=0; } while(0)
#define SED() do { cpu.D=1; } while(0)
#define CLV() do { cpu.V=0; } while(0)

/* ---------- zero-page indirect indexed (post-index) --------------- */
/* LDA (zp),Y → read 16-bit addr from zp/zp+1, add Y */
#define ZP_IND_Y(zp)   ((uint16_t)(mem[(uint8_t)(zp)] | (mem[(uint8_t)((zp)+1)]<<8)) + cpu.Y)
/* LDA (zp,X)  → read 16-bit addr from zp+X (zero-page wrapped) */
#define ZP_IND_X(zp)   (uint16_t)(mem[(uint8_t)((zp)+cpu.X)] | (mem[(uint8_t)((zp)+cpu.X+1)]<<8))

/* ---------- NOP --------------------------------------------------- */
#define NOP() do {} while(0)

#endif /* CPU_H */
