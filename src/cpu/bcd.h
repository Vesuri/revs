#ifndef REVS_BCD_H
#define REVS_BCD_H
/* uintN_t comes from the including TU, exactly as in m68k_math.h — do NOT include <stdint.h>. */

/* PACKED BCD, the game's own representation — on the 68000's own BCD instructions.
 *
 * Revs keeps lap times, split times, the race clock and the standings columns as packed
 * BCD — two decimal digits per byte, carried between bytes.  That is the GAME's choice of
 * representation and it is faithful; what is NOT faithful-by-necessity is the 6502 IDIOM
 * for it (set `cpu.D`, run the add through a macro that consults the flag, clear `cpu.D`
 * again).  These helpers are the decimal arithmetic on its own terms, so a BCD twin reads
 * as BCD arithmetic and no `cpu` field is involved.
 *
 * ⭐⭐ THE CONTRACT: BOTH OPERANDS ARE VALID PACKED BCD (every nibble $0..$9).
 *
 * That is a statement about the GAME, and it is what licenses `ABCD`/`SBCD`.  Measured over
 * all 256x256x2 inputs against the 6502's decimal `ADC`:
 *
 *     0 disagreements on every VALID-BCD input; 10188 value and 1296 carry disagreements,
 *     all of them on operands with a nibble in $A..$F (first: $04 + $8F + C=1, where the
 *     6502 gives $9A/C=0 and ABCD gives $FA/C=1).
 *
 * So on the data the engine actually holds, the opcode IS the 6502 — one instruction where
 * the idiom was a macro recomputing five flags.  ⚠ The divergence on `$A..$F` nibbles is
 * therefore NOT a reason to reimplement decimal `ADC` in software: it is a statement about
 * inputs the game does not produce.  `make validate` must model the game, so its BCD
 * fixtures generate valid digits (`validate_native.c`, `rnd_bcd`) rather than forcing the
 * port to reproduce undefined-in-practice NMOS behaviour.  If a real trajectory ever hands
 * one of these an invalid digit, the bug is in whatever WROTE that byte — fix it there.
 *
 * The C body below is the same algorithm for the host build (x86/ARM have no `ABCD`); it is
 * bit-exact with `ABCD` on every valid-BCD input, which is the whole domain.
 */

/* ⚠ The FLAGS are not the opcode's and cannot be: an NMOS decimal `ADC` takes Z and V from
 * the BINARY sum and N from the PRE-correction high nibble, none of which `ABCD` produces
 * (it leaves N and V undefined and sets Z from the decimal result).  So `val`/`carry` come
 * from the hardware and the three flags are replayed from the operands — and at the two
 * sites that read them (`menu_wait_key`'s digit bump, `add_frame_time`'s overflow test) that
 * replay is the only correct source.  Where a caller ignores them, they fold away. */
typedef struct { uint8_t val, carry, n, z, v; } BcdAdd;
typedef struct { uint8_t val, carry; } BcdSub;    /* carry: 1 = no borrow, as on the 6502 */

/* a + b + carryIn in packed BCD. */
static inline BcdAdd bcd_add(uint8_t a, uint8_t b, unsigned carryIn)
{
    unsigned c   = carryIn ? 1u : 0u;
    unsigned bin = (unsigned)a + b + c;
    unsigned loC = ((unsigned)(a & 0x0Fu) + (b & 0x0Fu) + c) > 9u;   /* the ten carried up */
    unsigned hiP = (unsigned)(a >> 4) + (b >> 4) + loC;              /* high nibble, uncorrected */
    BcdAdd   r;

    r.z = (uint8_t)((bin & 0xFFu) == 0u);         /* Z from the BINARY sum */
    r.n = (uint8_t)((hiP & 0x08u) ? 1u : 0u);     /* N from the high nibble BEFORE its correction */
    r.v = (uint8_t)((((unsigned)~(a ^ b) & (a ^ bin)) >> 7) & 1u);

#if defined(__mc68000__)
    {   /* ABCD: one instruction for the digits, X as the decimal carry both ways. */
        uint8_t res = a, cin = (uint8_t)c, cout;
        __asm__ ("moveq  #0,%1    \n\t"    /* cout = 0 (MOVEQ leaves X alone) */
                 "lsr.b  #1,%2    \n\t"    /* cin bit 0 -> X: the decimal carry in.  ⚠ LSR, not
                                              ROR — ROR/ROL do NOT touch X on the 68000, only
                                              the shifts and ROXR/ROXL do.  With ROR here the
                                              carry-in never reached ABCD and the on-target
                                              sweep failed 4500 add / 10000 sub cases. */
                 "abcd   %3,%0    \n\t"    /* res = res + b + X, packed BCD; X = carry out */
                 "addx.b %1,%1    \n\t"    /* cout = 0 + 0 + X */
                 : "+d"(res), "=&d"(cout), "+d"(cin)
                 : "d"(b)
                 : "cc");
        r.val   = res;
        r.carry = cout;
    }
#else
    {   /* The same algorithm in C, for the host build. */
        unsigned lo = (unsigned)(a & 0x0Fu) + (b & 0x0Fu) + c;
        unsigned hi = hiP;
        if (loC) lo += 6u;                        /* digit overflowed: +6 */
        if (hi > 9u) hi += 6u;                    /* ...and so did the high one */
        r.carry = (uint8_t)(hi > 0x0Fu);          /* the DECIMAL carry, i.e. the sum passed 99 */
        r.val   = (uint8_t)(((hi << 4) | (lo & 0x0Fu)) & 0xFFu);
    }
#endif
    return r;
}

/* a - b - !carryIn in packed BCD.  `carry` is 1 for "no borrow", as on the 6502.
 * ⚠ The 6502's carry out of a decimal SBC is the BINARY borrow, while SBCD's X is the
 * DECIMAL borrow — the same bit on valid BCD, because packed digits order exactly like the
 * decimal values they spell, so `a < b + borrow` and `dec(a) < dec(b) + borrow` are one test.
 * No flags beyond C: no BCD twin needs a decimal subtract's N/Z/V. */
static inline BcdSub bcd_sub(uint8_t a, uint8_t b, unsigned carryIn)
{
    BcdSub r;
#if defined(__mc68000__)
    {   /* SBCD, with X carrying the borrow in and out. */
        uint8_t res = a, bin = (uint8_t)(carryIn ? 0u : 1u), bout;
        __asm__ ("moveq  #0,%1    \n\t"    /* bout = 0 */
                 "lsr.b  #1,%2    \n\t"    /* borrow-in bit 0 -> X (LSR, not ROR — see bcd_add) */
                 "sbcd   %3,%0    \n\t"    /* res = res - b - X, packed BCD; X = borrow out */
                 "addx.b %1,%1    \n\t"    /* bout = X */
                 : "+d"(res), "=&d"(bout), "+d"(bin)
                 : "d"(b)
                 : "cc");
        r.val   = res;
        r.carry = (uint8_t)(bout ^ 1u);           /* 6502 sense: 1 = no borrow */
    }
#else
    {   /* The same algorithm in C, for the host build. */
        int borrow = carryIn ? 0 : 1;
        int lo = (a & 0x0F) - (b & 0x0F) - borrow;
        int hi = (a >> 4)   - (b >> 4);
        r.carry = (uint8_t)(((int)a - (int)b - borrow) >= 0);   /* 1 = no borrow */
        if (lo < 0) { lo += 10; hi -= 1; }        /* borrow a ten from the high digit */
        if (hi < 0) { hi += 10; }                 /* ...and out of the byte */
        r.val = (uint8_t)(((hi << 4) | (lo & 0x0F)) & 0xFF);
    }
#endif
    return r;
}

#endif /* REVS_BCD_H */
