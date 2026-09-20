/* fastmem.c — longword memset/memcpy/memmove for the 68000.  AMIGA ONLY.
 *
 * ⭐⭐⭐ WHY THIS FILE EXISTS.  The toolchain's `-nostdlib` support library
 * (`$(SUPPORT)/gcc8_c_support.c`, shared with the other Amiga projects and so OUTSIDE this repo)
 * implements all three block operations as BYTE LOOPS:
 *
 *     memset:   move.b d0,(a0)+ / cmpa.l d1,a0 / bne.s      8 + 6 + 10 = 24 cyc per BYTE
 *     memcpy:   move.b (a0)+,(a1)+ / cmp.l a0,d1 / bne.s   12 + 6 + 10 = 28 cyc per BYTE
 *     memmove (descending): four instructions a byte,       ~40 cyc per BYTE
 *
 * A longword loop unrolled eight ways is ~4.8 cyc/byte as GCC emits it below — a 5x win — and
 * this is the same defect class that already paid -4.20 ms in the decode and -1.96 ms in the low
 * block: a byte loop over longword-aligned data (docs/perf-method.md).
 *
 * ⚠ WHAT IT IS WORTH, MEASURED, so nobody re-derives it: -0.52 ms in phase 10
 * (`clear_surface_buffers_core`, five fills) and -0.53 ms in phase 24 (`revs_plot_own_reset`'s
 * 208-entry clear plus the view sweep's two `memmove`s).  Those are the only two phase rows in the
 * frame that contain a block operation, and they are the two that moved.  It is SMALLER than the
 * byte counts suggest for a reason worth knowing: `clear_surface_buffers` looks like a 396-byte
 * fill from the source, but `horizon_extent` is ~28 in a real race rather than its $4E ceiling, so
 * it fills ~196.  Size the next such change from the RUNTIME bound, not the declared one.
 *
 * ⚠⚠ WHY LINK-TIME WRAPPING AND NOT A HELPER AT EACH CALL SITE.  Most of the calls are not written
 * anywhere: GCC turns a `for` loop that stores a constant into a `memset` call by itself
 * (`revs_plot_own_reset`'s 208-entry clear, `buildLineModes`', `reset_driving_variables`'), so a
 * named helper would upgrade only the sites that already say `memset` and leave the emitted ones on
 * the byte loop.  `-Wl,--wrap=` redirects EVERY reference, written or emitted, including the
 * `lea <memset>,a2 / jsr (a2)` form GCC uses when a routine calls one twice.  The originals stay
 * reachable as `__real_*` and nothing needs them.
 *
 * ⚠ THE ALIASING RULE IS NOT BROKEN HERE, and the two operations satisfy it for DIFFERENT reasons
 * (CLAUDE.md §mem[] is little-endian):
 *   - memset widens a value whose every byte is the SAME, which is the one case the rule allows.
 *   - memcpy/memmove move bytes to bytes; no value is ever interpreted, so byte order cannot be
 *     observed.  They need only preserve the ORDER of the bytes, which they do.
 *
 * ⚠⚠ AND THE 68000 TAKES AN ADDRESS ERROR EXCEPTION ON AN ODD WORD ACCESS — not a slow path, a
 * crash.  So every wide loop is entered only after the pointer is forced even, and memcpy widens
 * only when source and destination have the SAME parity (a 68000 cannot fix a parity mismatch
 * without a shift network, which costs more than the byte loop saves).
 *
 * GATES.  `make fastmem` is the host differential — all three against libc over every length
 * 0..300 at every alignment pair, plus the odd-address assertion, with seven sabotages that all
 * fail.  `make FASTMEMCHECK=1` is the target-side postcondition check; see its section below for
 * what each one can and cannot see.
 */

typedef unsigned long  fm_size;      /* matches the support library's declared signature */
/* ⭐ `unsigned int`, not `unsigned long`: 32 bits on m68k-amiga-elf AND on the dev host, so
   tools/fastmem_test.c exercises the SAME wide path the target runs.  `unsigned long` is 64-bit
   on the host and the differential would then prove nothing about the shipped code. */
typedef unsigned int   fm_word;

/* ⭐ The threshold is where the wide path's setup (parity fix, longword build, tail) stops being
   longer than the byte loop it replaces.  Eight bytes: below that the unrolled body cannot run
   even once. */
#define FM_MIN 8u

/* ⭐⭐ THE ODD-ADDRESS HOOK, and it exists because the host cannot see the defect it guards.
 * A 68000 word or longword access at an ODD address raises an address error; x86 simply allows it.
 * So deleting the parity tests below is a target CRASH that the host differential passes clean —
 * measured: tools/fastmem_test.c's sabotage 2 was 0 failures of 30100 until this hook existed.
 * FM_WIDE names every pointer the code is about to widen, so the test can ASSERT the parity from
 * the shipped control flow instead of re-deriving the rule from its own copy of it (which is the
 * vacuous-oracle shape: a reference computed from the thing under test proves nothing).  Compiles
 * to nothing without FM_TRACE, so the target build is unaffected. */
#ifdef FM_TRACE
extern void fm_trace_wide(const void *p, unsigned long bytes);
#define FM_WIDE(p, n)  fm_trace_wide((const void *)(p), (unsigned long)(n))
#else
#define FM_WIDE(p, n)  ((void)0)
#endif

static void *fm_set(void *dest, int val, fm_size len);
static void *fm_copy(void *dest, const void *src, fm_size len);
static void *fm_move(void *dest, const void *src, fm_size len);

/* ------------------------------------------------------------------ the implementations ---- */

static void *fm_set(void *dest, int val, fm_size len)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char v = (unsigned char)val;

    if (len < FM_MIN) {
        while (len--) *d++ = v;
        return dest;
    }
    if ((unsigned long)d & 1ul) { *d++ = v; len--; }     /* force even — see the header */

    {
        fm_word w = (fm_word)v;
        w |= w << 8;
        w |= w << 16;
        {
            fm_word *p = (fm_word *)(void *)d;           /* ENDIAN-OK: every byte of w is v */
            fm_size n  = len >> 2;
            FM_WIDE(p, n << 2);
            while (n >= 8u) {                            /* 32 bytes an iteration */
                p[0] = w; p[1] = w; p[2] = w; p[3] = w;
                p[4] = w; p[5] = w; p[6] = w; p[7] = w;
                p += 8; n -= 8u;
            }
            while (n--) *p++ = w;
            d   = (unsigned char *)(void *)p;
            len &= 3u;
        }
    }
    while (len--) *d++ = v;
    return dest;
}

static void *fm_copy(void *dest, const void *src, fm_size len)
{
    unsigned char       *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    /* A parity mismatch cannot be widened on a 68000 — see the header. */
    if (len < FM_MIN || (((unsigned long)d ^ (unsigned long)s) & 1ul)) {
        while (len--) *d++ = *s++;
        return dest;
    }
    if ((unsigned long)d & 1ul) { *d++ = *s++; len--; }

    {
        fm_word       *pd = (fm_word *)(void *)d;        /* ENDIAN-OK: bytes to bytes, in order */
        const fm_word *ps = (const fm_word *)(const void *)s;
        fm_size n = len >> 2;
        FM_WIDE(pd, n << 2);
        FM_WIDE(ps, n << 2);
        while (n >= 8u) {
            pd[0] = ps[0]; pd[1] = ps[1]; pd[2] = ps[2]; pd[3] = ps[3];
            pd[4] = ps[4]; pd[5] = ps[5]; pd[6] = ps[6]; pd[7] = ps[7];
            pd += 8; ps += 8; n -= 8u;
        }
        while (n--) *pd++ = *ps++;
        d   = (unsigned char *)(void *)pd;
        s   = (const unsigned char *)(const void *)ps;
        len &= 3u;
    }
    while (len--) *d++ = *s++;
    return dest;
}

static void *fm_move(void *dest, const void *src, fm_size len)
{
    unsigned char       *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    if (d == s || len == 0u) return dest;
    if (d < s) return fm_copy(dest, src, len);           /* ascending is memcpy's case exactly */

    /* Descending.  Same parity test, same reason; the wide loop walks backwards in longwords. */
    if (len < FM_MIN || (((unsigned long)d ^ (unsigned long)s) & 1ul)) {
        d += len; s += len;
        while (len--) *--d = *--s;
        return dest;
    }
    d += len; s += len;
    if ((unsigned long)d & 1ul) { *--d = *--s; len--; }

    {
        fm_word       *pd = (fm_word *)(void *)d;        /* ENDIAN-OK: bytes to bytes, in order */
        const fm_word *ps = (const fm_word *)(const void *)s;
        fm_size n = len >> 2;
        FM_WIDE(pd - n, n << 2);
        FM_WIDE(ps - n, n << 2);
        while (n >= 8u) {
            pd[-1] = ps[-1]; pd[-2] = ps[-2]; pd[-3] = ps[-3]; pd[-4] = ps[-4];
            pd[-5] = ps[-5]; pd[-6] = ps[-6]; pd[-7] = ps[-7]; pd[-8] = ps[-8];
            pd -= 8; ps -= 8; n -= 8u;
        }
        while (n--) *--pd = *--ps;
        d   = (unsigned char *)(void *)pd;
        s   = (const unsigned char *)(const void *)ps;
        len &= 3u;
    }
    while (len--) *--d = *--s;
    return dest;
}

/* ------------------------------------------- the target-side postcondition check ----------- */
/* ⭐⭐ `make FASTMEMCHECK=1`.
 *
 * tools/fastmem_test.c settles the loop bounds and the parity on the host, exhaustively.  What it
 * cannot settle is which CALLS the link-time wrap actually captured and what arguments the game
 * hands them — in particular a `memcpy` on OVERLAPPING regions, which is undefined in C but which
 * the toolchain's ascending byte loop happened to serve and this file's wide loop would not.
 *
 * ⚠⚠ AND A TARGET PIXEL DIFF CANNOT ANSWER IT.  A render-speed change shifts the simulation's
 * trajectory, so the fast and slow arms are never on the same scene — measured: two
 * `screen_dump.gdb` runs broke four fields apart (vbi 600 vs 604, 63 vs 65 painted) and their
 * bitplanes differ for that reason alone.  That is not a weaker version of this check; it is a
 * confounded one, and it must not be quoted as evidence either way.
 *
 * So check the POSTCONDITION inside each call, on the real data, where no trajectory can intrude:
 * memset leaves `len` copies of `val`; memcpy/memmove leave the source as it was AT ENTRY.  That is
 * what the C library PROMISES, not what this file does — so it cannot be satisfied by a wrong
 * answer both sides compute the same way, which is the vacuous-oracle shape.
 *
 * ⚠ WHAT IT CANNOT SEE: a call longer than the snapshot is counted in `g_fmSkipped` and NOT
 * checked, so require that number to be one you have accounted for — an unchecked call is not a
 * passing one.  `g_fmLongest` says whether the scratch is big enough.  And the check itself is a
 * byte loop over every byte moved, so this arm is SLOWER than either implementation: it is a
 * correctness instrument and its own phase rows are void.
 */
#ifdef REVS_FASTMEM_CHECK
#define FM_SCRATCH 1024u
extern volatile unsigned long g_fmChecks;
extern volatile unsigned long g_fmMismatch;
extern volatile unsigned long g_fmSkipped;
extern volatile unsigned long g_fmLongest;
volatile unsigned long g_fmChecks   = 0;
volatile unsigned long g_fmMismatch = 0;
volatile unsigned long g_fmSkipped  = 0;
volatile unsigned long g_fmLongest  = 0;
static unsigned char s_fmSnap[FM_SCRATCH];

static void fm_note(fm_size len)
{
    if ((unsigned long)len > g_fmLongest) g_fmLongest = (unsigned long)len;
}

/* ⭐ The long-call arm, and it closes the one hole the snapshot left rather than documenting it.
   A copy too big for the scratch can still be checked EXACTLY when the two regions are disjoint:
   the source is then unchanged by the copy, so `dest == src` afterwards is the same assertion.
   The only thing that still escapes is an OVERLAPPING move longer than the scratch, which is
   counted in g_fmSkipped and does not occur — the one >1 KB call in the port is
   `PlatformAmiga::loadImage`'s 64 KB image load (a `for` loop GCC turns into a memcpy) from the
   linked-in ROM image into mem[], two distinct objects. */
static int fm_disjoint(const unsigned char *d, const unsigned char *s, fm_size len)
{
    return (d + len <= s) || (s + len <= d);
}
static void fm_verify_disjoint(const unsigned char *d, const unsigned char *s, fm_size len)
{
    fm_size i;
    g_fmChecks++;
    for (i = 0; i < len; i++) if (d[i] != s[i]) { g_fmMismatch++; return; }
}
#endif

void *__wrap_memset(void *dest, int val, fm_size len);
void *__wrap_memcpy(void *dest, const void *src, fm_size len);
void *__wrap_memmove(void *dest, const void *src, fm_size len);

void *__wrap_memset(void *dest, int val, fm_size len)
{
#ifdef REVS_FASTMEM_CHECK
    void *r = fm_set(dest, val, len);
    {
        const unsigned char *d = (const unsigned char *)dest;
        const unsigned char  v = (unsigned char)val;
        fm_size i;
        fm_note(len);
        g_fmChecks++;
        for (i = 0; i < len; i++) if (d[i] != v) { g_fmMismatch++; break; }
    }
    return r;
#else
    return fm_set(dest, val, len);
#endif
}

void *__wrap_memcpy(void *dest, const void *src, fm_size len)
{
#ifdef REVS_FASTMEM_CHECK
    {
        const unsigned char *s = (const unsigned char *)src;
        fm_size i;
        fm_note(len);
        if (len > (fm_size)FM_SCRATCH) {
            void *rr = fm_copy(dest, src, len);
            if (fm_disjoint((const unsigned char *)dest, s, len))
                fm_verify_disjoint((const unsigned char *)dest, s, len);
            else g_fmSkipped++;
            return rr;
        }
        for (i = 0; i < len; i++) s_fmSnap[i] = s[i];
    }
    {
        void *r = fm_copy(dest, src, len);
        const unsigned char *d = (const unsigned char *)dest;
        fm_size i;
        g_fmChecks++;
        for (i = 0; i < len; i++) if (d[i] != s_fmSnap[i]) { g_fmMismatch++; break; }
        return r;
    }
#else
    return fm_copy(dest, src, len);
#endif
}

void *__wrap_memmove(void *dest, const void *src, fm_size len)
{
#ifdef REVS_FASTMEM_CHECK
    {
        const unsigned char *s = (const unsigned char *)src;
        fm_size i;
        fm_note(len);
        if (len > (fm_size)FM_SCRATCH) {
            void *rr = fm_move(dest, src, len);
            if (fm_disjoint((const unsigned char *)dest, s, len))
                fm_verify_disjoint((const unsigned char *)dest, s, len);
            else g_fmSkipped++;
            return rr;
        }
        for (i = 0; i < len; i++) s_fmSnap[i] = s[i];
    }
    {
        void *r = fm_move(dest, src, len);
        const unsigned char *d = (const unsigned char *)dest;
        fm_size i;
        g_fmChecks++;
        for (i = 0; i < len; i++) if (d[i] != s_fmSnap[i]) { g_fmMismatch++; break; }
        return r;
    }
#else
    return fm_move(dest, src, len);
#endif
}
