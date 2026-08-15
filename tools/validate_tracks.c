/* validate_tracks.c — the circuit installer, diffed byte for byte against the real thing.
 *
 * ⭐ WHAT MAKES THIS EVIDENCE AND NOT A TAUTOLOGY.  The two sides are built down different paths:
 *
 *   under test   src/platform/track.c starts from SILVERSTONE's post-unpack image and writes two
 *                extents ($5300-$5A25, $7800-$78AA) plus the circuit's engine patches.
 *   fixture      tools/gen_tracks.py --fixtures relocates the DISC IMAGE FOR THAT CIRCUIT and
 *                applies its ModifyGameCode replay — a whole 64K image, never a delta.
 *
 * Byte-identical means the installer is right AND that nothing outside those two extents differs
 * between circuits.  A third extent, an off-by-one bound, a missed patch: all three fail here.
 * The replay itself is cross-checked against a real BBC by `make track-patch VERIFY=1`.
 *
 * ⚠ CANNOT PASS VACUOUSLY (docs/validation-harness.md).  It fails if a fixture is missing, if the
 * fixture count disagrees with REVS_TRACK_COUNT, or if zero comparisons ran.  A build without the
 * git-ignored Nürburgring disc legitimately has five circuits, so the count is read from the
 * generated table rather than hardcoded — but it must MATCH the fixtures on disc.
 *
 * ⚠⚠ TWO THINGS HERE EXIST BECAUSE THE FIRST VERSION OF THIS FILE WAS NEARLY VACUOUS, and a
 * sabotage run is what exposed it (2026-08-15).  Truncating the installer's TAIL loop by one byte
 * still PASSED.  Both causes are worth stating, because both are general:
 *
 *   1. THE BASE WAS THE THING UNDER TEST.  mem[] was initialised from Silverstone's own image and
 *      then Silverstone's block was installed over it — writing identical bytes, so the diff could
 *      not see a byte the installer failed to write.  Fixed by POISONING both extents with a
 *      sentinel first: now every byte in them must be written by the installer to match, and the
 *      one-byte truncation fails loudly.  ⭐ An installer test whose "before" state already equals
 *      the expected "after" state measures nothing.
 *   2. IT COULD ONLY EVER INSTALL ONE CIRCUIT OF SIX, because the other five are correctly refused
 *      until their SMC sites exist.  Fixed with revs_track_install_forced(), which skips the check
 *      for the data comparison only; the refusal itself is then tested separately and explicitly
 *      via revs_track_check().  ⭐ Covering 1 of 6 cases is most of the way to proving nothing.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../src/cpu/cpu.h"
#include "../src/platform/track.h"

#define FIXDIR "tmp/tracks"

/* The DFS names, in the same order as revs_tracks[] — the fixture file names come from these.
 * ⚠ Kept in step with tools/gen_tracks.py CIRCUITS by the count check below, not by faith. */
static const char* kDfs[] = { "SILVER", "BRANDS", "DONING", "OULTON", "SNETTER", "NURBURG" };

static int slurp(const char* path, unsigned char* dst, size_t n)
{
    FILE* f = fopen(path, "rb");
    size_t got;
    if (!f) return 0;
    got = fread(dst, 1, n, f);
    fclose(f);
    return got == n;
}

int main(void)
{
    static unsigned char base[65536];
    static unsigned char want[65536];
    char path[256];
    int i, fails = 0, compared = 0;

    snprintf(path, sizeof path, "%s/base.bin", FIXDIR);
    if (!slurp(path, base, sizeof base)) {
        fprintf(stderr, "FAIL: no %s — run `make track-fixtures` first\n", path);
        return 1;
    }

    printf("circuits in this build: %d\n", REVS_TRACK_COUNT);

    for (i = 0; i < REVS_TRACK_COUNT; i++) {
        int bad = 0, first = -1;
        size_t k;
        unsigned short unhon;

        snprintf(path, sizeof path, "%s/expect_%s.bin", FIXDIR, kDfs[i]);
        if (!slurp(path, want, sizeof want)) {
            fprintf(stderr, "FAIL: %s missing — the fixture set does not match "
                            "REVS_TRACK_COUNT=%d\n", path, REVS_TRACK_COUNT);
            fails++;
            continue;
        }

        /* Every circuit starts from the SAME base, which is the port's real situation: one
           embedded image, selection applied on top.  Reset in full so a leak from the previous
           iteration cannot pass as a success. */
        for (k = 0; k < sizeof base; k++) mem[k] = base[k];
        /* …then POISON the two extents.  See note 1 in the header: without this, installing
           Silverstone over Silverstone's own image writes identical bytes and the diff is blind to
           anything the installer fails to write.  $5A is not a byte either extent legitimately
           ends up full of, so a survivor is unmistakable. */
        for (k = 0; k < REVS_TRACK_BLOCK_LEN; k++) mem[REVS_TRACK_BLOCK_LO + k] = 0x5A;
        for (k = 0; k < REVS_TRACK_TAIL_LEN;  k++) mem[REVS_TRACK_TAIL_LO  + k] = 0x5A;

        /* The refusal, tested as its own assertion rather than inferred from a return value. */
        unhon = revs_track_check((unsigned char)i);
        if (revs_track_install((unsigned char)i)) {
            if (unhon) { fails++; printf("  %-8s BUG: installed with %u unhonoured bytes\n",
                                         kDfs[i], unhon); }
        } else if (!unhon) {
            fails++;
            printf("  %-8s BUG: refused with nothing unhonoured\n", kDfs[i]);
            continue;
        } else {
            /* Correct behaviour today for an expansion circuit (track.h says why).  Verify the
               DATA anyway, through the harness-only entry point — otherwise 5 of 6 circuits'
               block, tail and patch tables would go untested until the SMC work lands. */
            if (!revs_track_install_forced((unsigned char)i)) {
                fails++; printf("  %-8s BUG: forced install rejected\n", kDfs[i]); continue;
            }
        }

        for (k = 0; k < sizeof want; k++) {
            if (mem[k] != want[k]) { if (first < 0) first = (int)k; bad++; }
        }
        compared++;
        if (bad) {
            fails++;
            printf("  %-8s MISMATCH: %d bytes, first $%04X (got $%02X want $%02X)\n",
                   kDfs[i], bad, first, mem[first], want[first]);
        } else {
            printf("  %-8s OK: 64K byte-identical to the disc-derived image%s\n", kDfs[i],
                   unhon ? "  (data only — install correctly REFUSED, see below)" : "");
        }
    }

    if (compared == 0) {
        fprintf(stderr, "FAIL: zero circuits were actually installed and compared — a pass here "
                        "would mean nothing\n");
        return 1;
    }
    printf("%d of %d circuits verified byte-exact; %d failures\n",
           compared, REVS_TRACK_COUNT, fails);
    for (i = 0; i < REVS_TRACK_COUNT; i++) {
        unsigned short u = revs_track_check((unsigned char)i);
        if (u) printf("  note: %-8s is NOT yet playable — %u patch bytes await SMC sites "
                      "(first $%04X)\n", kDfs[i], u, g_trackUnhonouredAddr);
    }
    return fails ? 1 : 0;
}
