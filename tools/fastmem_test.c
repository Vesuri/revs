/* fastmem_test.c — the differential for src/platform/amiga/fastmem.c.
 *
 * The three wrappers replace the toolchain's block operations on the shipping Amiga binary, so a
 * defect in them is a wrong pixel or a crash in any subsystem, with nothing pointing back here.
 * This runs them against the host libc's over every length 0..300 at every source/destination
 * alignment pair, on a buffer poisoned either side of the region so an overrun is visible.
 *
 * ⚠ It runs on the HOST, which is legitimate for this one file and for a reason worth stating:
 * the code under test contains no `mem[]`, no endianness assumption a byte-for-byte move could
 * expose, and no target hardware — only pointer arithmetic and loop bounds, which are exactly
 * what the host can settle.  What the host canNOT settle is the 68000 address error on an odd
 * word access, so ASSERT PARITY here instead of hoping: every wide store the shipped code makes
 * must be at an even address, and `check_parity` below fails if fastmem would ever widen at an
 * odd one.  (`make validate`'s harness is the wrong home: these are not 6502 twins.)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long fm_size;
void *__wrap_memset(void *dest, int val, fm_size len);
void *__wrap_memcpy(void *dest, const void *src, fm_size len);
void *__wrap_memmove(void *dest, const void *src, fm_size len);

#define PAD  16
#define MAXL 300
#define SKEW 64                      /* the overlap tests' source/destination separation */
/* ⚠ Big enough for the WORST case the loops below construct — PAD + SKEW + alignment + MAXL
   + PAD.  Sized by eye first, and the differential caught it: every case from len=258 up
   "failed" because the harness itself ran past the end of `a`.  A test whose own bounds are
   wrong reports a defect in the code under test. */
#define CAP  (2 * PAD + SKEW + 4 + MAXL + PAD)

static unsigned long g_cases, g_fails;

static void fail(const char *what, unsigned len, unsigned da, unsigned sa)
{
    printf("fastmem: FAIL %s len=%u dstAlign=%u srcAlign=%u\n", what, len, da, sa);
    g_fails++;
}

/* ⭐⭐ THE ODD-ADDRESS ASSERTION — the one property the host's own results cannot show.
   A 68000 word/long access at an odd address is an address error exception; x86 permits it, so a
   fastmem that widened at an odd address would pass every byte comparison above and crash on the
   target.  fastmem.c therefore names each pointer it is about to widen through FM_WIDE, and this
   is the observer.  ⚠⚠ SCOPE, stated here because the last oracle that went unstated was vacuous:
   this checks the addresses THE CODE REACHES on the cases the loops below construct.  It is not a
   proof for inputs never exercised, and it is not a re-derivation of the parity rule — an earlier
   version of this file computed the expected parity from its own copy of the rule and so passed
   the sabotage that deletes the rule (measured: 0 failures of 30100). */
static unsigned long g_wide, g_oddWide;

void fm_trace_wide(const void *p, unsigned long bytes)
{
    if (bytes == 0ul) return;               /* no wide access actually happens */
    g_wide++;
    if ((unsigned long)p & 1ul) {
        if (g_oddWide == 0ul)
            printf("fastmem: FAIL odd wide access at offset %lu\n", (unsigned long)p & 7ul);
        g_oddWide++;
        g_fails++;
    }
}

int main(void)
{
    static unsigned char a[CAP], b[CAP], src[CAP];
    unsigned len, da, sa, i;

    for (i = 0; i < CAP; i++) src[i] = (unsigned char)(i * 37u + 11u);

    for (len = 0; len <= MAXL; len++)
        for (da = 0; da < 4; da++) {
            /* ---- memset ---- */
            memset(a, 0xA5, CAP); memset(b, 0xA5, CAP);
            memset(a + PAD + da, 0x5Cu, len);
            __wrap_memset(b + PAD + da, 0x5Cu, len);
            g_cases++;
            if (memcmp(a, b, CAP) != 0) fail("memset", len, da, 0);

            for (sa = 0; sa < 4; sa++) {
                /* ---- memcpy (disjoint) ---- */
                memset(a, 0xA5, CAP); memset(b, 0xA5, CAP);
                memcpy(a + PAD + da, src + sa, len);
                __wrap_memcpy(b + PAD + da, src + sa, len);
                g_cases++;
                if (memcmp(a, b, CAP) != 0) fail("memcpy", len, da, sa);

                /* ---- memmove, ascending overlap (dst below src) ---- */
                memcpy(a, src, CAP); memcpy(b, src, CAP);
                memmove(a + PAD + da, a + PAD + SKEW + sa, len);
                __wrap_memmove(b + PAD + da, b + PAD + SKEW + sa, len);
                g_cases++;
                if (memcmp(a, b, CAP) != 0) fail("memmove-up", len, da, sa);

                /* ---- memmove, descending overlap (dst above src) — the separate code path */
                memcpy(a, src, CAP); memcpy(b, src, CAP);
                memmove(a + PAD + SKEW + da, a + PAD + sa, len);
                __wrap_memmove(b + PAD + SKEW + da, b + PAD + sa, len);
                g_cases++;
                if (memcmp(a, b, CAP) != 0) fail("memmove-down", len, da, sa);

                /* ---- memmove with heavy overlap (distance 1..3, the aliasing worst case) */
                for (i = 1; i <= 3; i++) {
                    memcpy(a, src, CAP); memcpy(b, src, CAP);
                    memmove(a + PAD + da + i, a + PAD + da, len);
                    __wrap_memmove(b + PAD + da + i, b + PAD + da, len);
                    g_cases++;
                    if (memcmp(a, b, CAP) != 0) fail("memmove-near", len, da, sa);
                }
            }
        }

    printf("fastmem: %lu cases, %lu wide accesses (%lu odd), %lu failures — %s\n",
           g_cases, g_wide, g_oddWide, g_fails, g_fails ? "FAIL" : "PASS");
    return g_fails ? 1 : 0;
}
