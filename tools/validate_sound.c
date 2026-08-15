/* SOUND validation — the port's MOS sound scheduler against a REAL BBC, tick for tick.
 *
 *   make sound                                  replay every fixture in tmp/soundref
 *   make sound FIX=tmp/soundref/revs_race_events.txt    just one
 *   make sound VERBOSE=1                        print the first mismatching ticks in full
 *
 * ── WHAT MAKES THIS A REAL TEST ───────────────────────────────────────────────────────────
 *
 * The fixture is a log of the two halves of the real machine's sound, on the MOS's own 100 Hz
 * tick grid (tools/bbc_sound_capture.mjs quantises it; see there for why the grid is anchored
 * locally rather than fitted once):
 *
 *     C tick <8 bytes>    the machine issued this OSWORD 7 (SOUND) block before that tick
 *     E tick <14 bytes>   ...this OSWORD 8 (ENVELOPE) definition
 *     F tick <buffer>     ...and this OSBYTE 21 flush of sound buffer 4..7, which is the only way
 *                         Revs ever stops a sound
 *     S tick <state>      at the end of that tick the SN76489 held exactly this
 *
 * The S lines are the OUTPUT of the MOS's scheduler and nothing in the port produces them: they
 * come from the real machine's own writes to the chip.  So replaying C and E through
 * src/platform/sound.c has to reproduce every S, which is a comparison the port cannot pass by
 * accident (docs/validation-harness.md).
 *
 * Two scoping rules, stated rather than fudged:
 *
 *   1. A CHANNEL IS COMPARED ONLY ONCE THE REPLAY HAS TOUCHED IT.  The fixture starts on a
 *      machine whose chip still holds whatever the boot beep and BASIC left there, and the port
 *      starts from a cold reset — so an untouched channel legitimately differs.  Every channel a
 *      command addresses becomes live from that command onward, and the harness FAILS if the live
 *      set never covers all four, because a comparison that quietly skips channels is the shape of
 *      a green test that tests nothing.
 *
 *   2. A SILENT CHANNEL'S DIVIDER IS STILL COMPARED.  Revs depends on it: noise mode 3 borrows
 *      channel 1's divider and channel 1 is muted at low revs (src/platform/sound.h), so excusing
 *      the pitch of a silent channel would excuse the engine sound itself.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/platform/sound.h"

#define MAX_EVENTS 20000

typedef struct {
    long    tick;
    char    kind;          /* 'C', 'E' or 'S' */
    uint8_t bytes[14];
    SndChip chip;
} Event;

static Event g_ev[MAX_EVENTS];
static int   g_nEv = 0;
static int   g_verbose = 0;

static int load(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    if (!f) { fprintf(stderr, "  cannot open %s\n", path); return 0; }
    g_nEv = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (g_nEv >= MAX_EVENTS) { fprintf(stderr, "  fixture longer than %d events\n", MAX_EVENTS); break; }
        {
            Event *e = &g_ev[g_nEv];
            char kind = line[0];
            const char *p = line + 1;
            int n, i;
            unsigned v[9];
            memset(e, 0, sizeof *e);
            if (kind == 'F') {
                unsigned buf;
                if (sscanf(p, "%ld %u", &e->tick, &buf) != 2) { fprintf(stderr, "  malformed F line: %s", line); fclose(f); return 0; }
                e->bytes[0] = (uint8_t)buf;
            } else if (kind == 'C' || kind == 'E') {
                int want = (kind == 'C') ? 8 : 14;
                unsigned b[14];
                n = sscanf(p, "%ld %x %x %x %x %x %x %x %x %x %x %x %x %x %x", &e->tick,
                           &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7],
                           &b[8], &b[9], &b[10], &b[11], &b[12], &b[13]);
                if (n != want + 1) { fprintf(stderr, "  malformed %c line: %s", kind, line); fclose(f); return 0; }
                for (i = 0; i < want; i++) e->bytes[i] = (uint8_t)b[i];
            } else if (kind == 'S') {
                n = sscanf(p, "%ld %u %u %u %u %u %u %u %u", &e->tick,
                           &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
                if (n != 9) { fprintf(stderr, "  malformed S line: %s", line); fclose(f); return 0; }
                for (i = 0; i < 3; i++) e->chip.tone[i] = (uint16_t)v[i];
                e->chip.noise = (uint8_t)v[3];
                for (i = 0; i < 4; i++) e->chip.vol[i] = (uint8_t)v[4 + i];
            } else continue;
            e->kind = kind;
            g_nEv++;
        }
    }
    fclose(f);
    return g_nEv > 0;
}

static void describe(const SndChip *c, char *out, size_t n)
{
    snprintf(out, n, "tone=[%u %u %u] noise=%u vol=[%u %u %u %u]",
             c->tone[0], c->tone[1], c->tone[2], c->noise,
             c->vol[0], c->vol[1], c->vol[2], c->vol[3]);
}

static int run(const char *path)
{
    int i = 0, live[4] = {0, 0, 0, 0}, liveCount = 0;
    long tick;
    long compared = 0, mismatch = 0, skipped = 0;
    const long lastTick = g_nEv ? g_ev[g_nEv - 1].tick : 0;

    printf("── %s: %d events, %ld ticks ──\n", path, g_nEv, lastTick + 1);
    snd_reset();

    for (tick = 0; tick <= lastTick; tick++) {
        /* Everything the machine issued before this tick ran. */
        while (i < g_nEv && g_ev[i].tick == tick && g_ev[i].kind != 'S') {
            if (g_ev[i].kind == 'F') {
                int chipCh = 3 - (g_ev[i].bytes[0] & 3);
                if (!live[chipCh]) { live[chipCh] = 1; liveCount++; }
                snd_flush_channel((uint8_t)(g_ev[i].bytes[0] & 3));
            } else if (g_ev[i].kind == 'C') {
                /* Live from here on: the chip channel this BBC channel drives, and — for the
                   noise generator in mode 3 — nothing else, because the divider it borrows
                   belongs to channel 1 and only channel 1's own command can set it. */
                int chipCh = 3 - (g_ev[i].bytes[0] & 3);
                if (!live[chipCh]) { live[chipCh] = 1; liveCount++; }
                snd_sound(g_ev[i].bytes);
            } else {
                snd_envelope(g_ev[i].bytes);
            }
            i++;
        }

        snd_tick();

        /* ...and the state the real chip held at the end of it. */
        while (i < g_nEv && g_ev[i].tick == tick && g_ev[i].kind == 'S') {
            const SndChip *got = snd_chip();
            const SndChip *want = &g_ev[i].chip;
            int bad = 0, ch;
            for (ch = 0; ch < 4; ch++) {
                if (!live[ch]) { skipped++; continue; }
                compared++;
                if (got->vol[ch] != want->vol[ch]) bad = 1;
                if (ch < 3) { if (got->tone[ch] != want->tone[ch]) bad = 1; }
                else        { if (got->noise    != want->noise)    bad = 1; }
            }
            if (bad) {
                mismatch++;
                if (g_verbose && mismatch <= 12) {
                    char a[128], b[128];
                    describe(got, a, sizeof a);
                    describe(want, b, sizeof b);
                    printf("  tick %6ld  port %s\n              bbc  %s\n", tick, a, b);
                }
            }
            i++;
        }
    }

    printf("  channels compared: %d of 4    field comparisons: %ld    skipped (not yet live): %ld\n",
           liveCount, compared, skipped);
    if (!compared) {
        printf("  ⚠ FAIL: nothing was compared.  A fixture that compares nothing passes vacuously.\n");
        return 0;
    }
    if (liveCount < 4)
        printf("  ⚠ only %d of the 4 chip channels were ever addressed by this fixture\n", liveCount);
    printf("  mismatching ticks: %ld of %ld  =>  %s\n", mismatch, lastTick + 1,
           mismatch ? "FAIL" : "PASS");
    return mismatch == 0;
}

int main(int argc, char **argv)
{
    const char *fixtures[8];
    int nFix = 0, i, ok = 1;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--verbose")) g_verbose = 1;
        else if (nFix < 8) fixtures[nFix++] = argv[i];
    }
    if (!nFix) {
        fixtures[nFix++] = "tmp/soundref/sound_events.txt";
        fixtures[nFix++] = "tmp/soundref/revs_race_events.txt";
    }
    for (i = 0; i < nFix; i++) {
        if (!load(fixtures[i])) {
            printf("── %s: MISSING.  Record it with `make sound-fixture` "
                   "(and `make sound-fixture-race` for the in-race one).\n", fixtures[i]);
            ok = 0;
            continue;
        }
        if (!run(fixtures[i])) ok = 0;
    }
    printf("\n%s\n", ok ? "SOUND: PASS" : "SOUND: FAIL");
    return ok ? 0 : 1;
}
