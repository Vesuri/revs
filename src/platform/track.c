/* track.c — install one circuit's data into mem[].  See track.h for the whole model. */
#include "track.h"
#include "diag.h"
#include "../cpu/cpu.h"                /* mem[] */
#include "../gen/revs_smc_bytes.h"
#include "../gen/revs_track_hooks.h"  /* the GENERATED per-circuit hook dispatch */

unsigned char  g_track = 0;
unsigned char  g_trackInstalled = 0xFF;
unsigned char  g_trackRequested = 0xFF;
unsigned short g_trackUnhonoured = 0;
unsigned short g_trackUnhonouredAddr = 0;
unsigned short g_trackHooksUnbuilt = 0;
unsigned short g_trackHooksUnbuiltAddr = 0;
unsigned short g_trackOverinstalls = 0;

/* Does the transliteration read this byte from mem[] at run time?  TWO ways it can:
 *
 *   1. it is not part of an INSTRUCTION at all, so every access to it is an ordinary mem[] read
 *      and a patch lands exactly as it would on the 6502.  ⚠ Two circuits rely on this: $3574
 *      and $35F4 are data, reached through `mem[(0x3500)+cpu.X]`.
 *   2. it is inside an instruction, and the transpiler declared an SMC site over it.
 *
 * Both tables are GENERATED (revs_smc_bytes.h) from the transpiler's own tables, because a
 * hand-kept copy would drift the moment a site or an instruction moved.
 *
 * revs_smc_bytes[] is sorted, so that half bisects — the check runs over ~60 addresses once at
 * selection time, so speed is irrelevant, but a linear scan over a sorted array is the kind of
 * thing that gets copied into a hot path later.
 */
static int is_code(unsigned short addr)
{
    unsigned i;
    for (i = 0; i < REVS_CODE_RANGE_COUNT; i++)
        if (addr >= revs_code_ranges[i][0] && addr <= revs_code_ranges[i][1]) return 1;
    return 0;
}

static int honoured(unsigned short addr)
{
    int lo = 0, hi = REVS_SMC_BYTE_COUNT - 1;
    if (!is_code(addr)) return 1;             /* data: read from mem[] by construction */
    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        unsigned short v = revs_smc_bytes[mid];
        if (v == addr) return 1;
        if (v < addr) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

/* ═════════════════════════════════════════════════════════════════════════════════════════
 * THE ENGINE→TRACK-FILE SEAM
 * ═════════════════════════════════════════════════════════════════════════════════════════
 * Ten of the thirty per-circuit extents turn an engine instruction into `JSR`/`JMP $5xxx` —
 * a call into the circuit's own file.  The generated C hands the target here (SMC kind
 * 'extent', tools/transpile.py) because that address means a DIFFERENT routine per circuit,
 * so the engine side cannot resolve it and this side owns the map.
 *
 * The MAP IS GENERATED — `src/gen/revs_track_hooks.h`, from the same pass that transliterates the
 * bodies (`tools/transpile.py` collect_track_hooks / emit_track_hooks).  It is keyed on BOTH the
 * circuit index and the address, and both halves are load-bearing: $5572 is a hook entry in every
 * circuit and is DIFFERENT CODE in each, so an address-only dispatch would compile, run, and take
 * Brands Hatch's corner through Snetterton's hook.
 *
 * ⚠ `revs_track_hook()` itself lives in the GENERATED src/gen/revs_track_hooks.c, next to the
 * bodies it calls.  Keeping it here would mean every consumer of this file links the whole
 * transliteration — including tools/validate_tracks.c, which runs no engine code and whose whole
 * value is being a small, independent differential.  What stays here is only the TABLE lookup,
 * which is header-only for that reason.
 *
 * `revs_track_check()` therefore refuses any circuit with a hook the build has no body for,
 * alongside the patch-byte check.  Silverstone has zero hooks and installs.
 */
static int hook_implemented(unsigned char index, unsigned short addr)
{
    return revs_track_hook_has(index, addr);
}

unsigned short revs_track_check(unsigned char index)
{
    const RevsTrack* t;
    unsigned i;

    g_trackUnhonoured = 0;
    g_trackHooksUnbuilt = 0;
    if (index >= REVS_TRACK_COUNT) return 0xFFFF;
    t = &revs_tracks[index];
    for (i = 0; i < t->patchCount; i++) {
        if (!honoured(t->patchAddr[i])) {
            REVS_DIAG(if (g_trackUnhonoured == 0) g_trackUnhonouredAddr = t->patchAddr[i]);
            g_trackUnhonoured++;
        }
    }
    /* ⭐ BOTH HALVES, ONE VERDICT — but TWO COUNTERS.  Covering the patch bytes was never
       sufficient: a patched `JSR $5672` reads its target from mem[] correctly and then has
       nothing to call.  The verdict adds them so "installable" keeps meaning "playable"; the
       counters stay separate so the report says WHICH half is missing.  Collapsing them would
       repeat the reporting defect the fallback note below is about. */
    for (i = 0; i < t->hookCount; i++) {
        if (!hook_implemented(index, t->hookAddr[i])) {
            REVS_DIAG(if (g_trackHooksUnbuilt == 0) g_trackHooksUnbuiltAddr = t->hookAddr[i]);
            g_trackHooksUnbuilt++;
        }
    }
    return (unsigned short)(g_trackUnhonoured + g_trackHooksUnbuilt);
}

static void install_data(const RevsTrack* t)
{
    unsigned i;
    for (i = 0; i < REVS_TRACK_BLOCK_LEN; i++)
        mem[REVS_TRACK_BLOCK_LO + i] = t->block[i];
    for (i = 0; i < REVS_TRACK_TAIL_LEN; i++)
        mem[REVS_TRACK_TAIL_LO + i] = t->tail[i];
    /* The engine patches LAST: the block and the tail are the circuit as the loader left it, and
       ModifyGameCode runs after that on hardware too.  Order matters if the two ever overlap —
       they do not today ($5300-$5A25 is excluded from the patch set by construction), and doing
       it in the hardware's order means it stays correct if that ever changes. */
    for (i = 0; i < t->patchCount; i++)
        mem[t->patchAddr[i]] = t->patchVal[i];
}

int revs_track_install(unsigned char index)
{
    /* ⭐ CHECK BEFORE WRITING ANYTHING.  A half-installed circuit — geometry in, patches
       refused — is the worst of the three outcomes: it looks like the selection worked and plays
       another circuit's code over this one's road.  So the whole patch set is validated first and
       mem[] is not touched until it passes.  (Same shape as applyMode()'s "validate, then latch"
       in RevsScreen: the bug that taught it is in docs/amiga-lessons.md.) */
    if (index >= REVS_TRACK_COUNT) return 0;
    /* ⚠⚠ ONE CIRCUIT PER BOOT IMAGE — see track.h.  The previous circuit's patch bytes are still
       in the engine and nothing here can put them back, so this is refused rather than allowed to
       produce a half-and-half engine.  Re-installing the SAME circuit is harmless (identical
       bytes) and stays allowed, because that is what an unattended build does when the menu
       answers itself with the circuit revs_track_boot() already chose. */
    if (g_trackInstalled != 0xFF && index != g_trackInstalled) { REVS_DIAG(g_trackOverinstalls++); return 0; }
    if (revs_track_check(index)) return 0;    /* REFUSED — the caller reports it */
    install_data(&revs_tracks[index]);
    g_track = index;
    g_trackInstalled = index;
    return 1;
}

int revs_track_boot(void)
{
    unsigned char want = (unsigned char)REVS_TRACK_DEFAULT;
    g_trackRequested = want;
    if (want < REVS_TRACK_COUNT && revs_track_install(want)) return 1;

    /* ⚠⚠ FALL BACK, THEN RE-RECORD THE REFUSAL — in that order, and the order is the point.
       The fallback install runs revs_track_check(0), and Silverstone is passive, so it RESETS
       g_trackUnhonoured to 0.  That silently destroyed the one number that distinguishes "the
       build asked for Silverstone" from "the build asked for Nürburgring and could not have it":
       measured on the target as `unhonoured=0 first=$1248`, i.e. a zero count beside a non-zero
       address, which is incoherent and was only noticed because the address survived.
       So the requested circuit's verdict is re-taken afterwards and is what the probes report. */
    revs_track_install(0);
    revs_track_check(want < REVS_TRACK_COUNT ? want : 0);
    return 0;
}

void revs_track_forget(void)
{
    g_trackInstalled = 0xFF;
}

/* ⚠⚠ HARNESS ONLY — see the warning in track.h.  Installs the data with the SMC check SKIPPED. */
int revs_track_install_forced(unsigned char index)
{
    if (index >= REVS_TRACK_COUNT) return 0;
    revs_track_check(index);                  /* still recorded, just not obeyed */
    install_data(&revs_tracks[index]);
    return 1;
}
