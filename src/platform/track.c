/* track.c — install one circuit's data into mem[].  See track.h for the whole model. */
#include "track.h"
#include "../cpu/cpu.h"                /* mem[] */
#include "../gen/revs_smc_bytes.h"

unsigned char  g_track = 0;
unsigned char  g_trackInstalled = 0xFF;
unsigned char  g_trackRequested = 0xFF;
unsigned short g_trackUnhonoured = 0;
unsigned short g_trackUnhonouredAddr = 0;

/* Is this byte one the transliteration reads from mem[] at run time?  revs_smc_bytes[] is
   generated sorted, so this bisects — the check runs over ~60 addresses at selection time, so
   speed is irrelevant, but a linear scan over a sorted array is the kind of thing that gets
   copied into a hot path later. */
static int honoured(unsigned short addr)
{
    int lo = 0, hi = REVS_SMC_BYTE_COUNT - 1;
    while (lo <= hi) {
        int mid = lo + ((hi - lo) >> 1);
        unsigned short v = revs_smc_bytes[mid];
        if (v == addr) return 1;
        if (v < addr) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

unsigned short revs_track_check(unsigned char index)
{
    const RevsTrack* t;
    unsigned i;

    g_trackUnhonoured = 0;
    if (index >= REVS_TRACK_COUNT) return 0xFFFF;
    t = &revs_tracks[index];
    for (i = 0; i < t->patchCount; i++) {
        if (!honoured(t->patchAddr[i])) {
            if (g_trackUnhonoured == 0) g_trackUnhonouredAddr = t->patchAddr[i];
            g_trackUnhonoured++;
        }
    }
    return g_trackUnhonoured;
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

/* ⚠⚠ HARNESS ONLY — see the warning in track.h.  Installs the data with the SMC check SKIPPED. */
int revs_track_install_forced(unsigned char index)
{
    if (index >= REVS_TRACK_COUNT) return 0;
    revs_track_check(index);                  /* still recorded, just not obeyed */
    install_data(&revs_tracks[index]);
    return 1;
}
