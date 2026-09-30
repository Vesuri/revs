/* engine_image_test.c — `make engine-image`: the release's startup loader against the dev image.
 *
 *   build/engine_image_test <revs_runtime.bin> <disc.ssd> [<disc.ssd> ...]
 *
 * For each disc: read the catalogue and REVS2 exactly as the Amiga backend does (two sectors,
 * then one read at the catalogue's offset), build the image with engine_build_image(), and
 * require every one of the 65536 bytes to equal revs_runtime.bin — the image every gate in this
 * repo was run against.  Then the refusals, which must FAIL: one flipped REVS2 byte (the CRC),
 * and a catalogue with REVS2 renamed away.  A disc passed with a leading '!' must be REFUSED
 * (the 1985 discs), and the status it gets is printed.
 */
#include <stdio.h>
#include <string.h>
#include "../src/platform/engine_image.h"

static unsigned char ref[65536], mem[65536], disc[1 << 20];

static long slurp(const char* path, unsigned char* buf, long max)
{
    FILE* f = fopen(path, "rb");
    long n;
    if (!f) return -1;
    n = (long)fread(buf, 1, (size_t)max, f);
    fclose(f);
    return n;
}

/* The Amiga backend's sequence, over an in-memory disc. */
static int load(const unsigned char* d, long size, unsigned char* m)
{
    unsigned long off;
    int st;
    if (size < (long)ENGINE_CATALOGUE_BYTES) return ENGINE_READ_FAILED;
    st = engine_find_revs2(d, &off);
    if (st != ENGINE_OK) return st;
    if ((long)(off + ENGINE_REVS2_LENGTH) > size) return ENGINE_READ_FAILED;
    memset(m, 0xA5, 65536);                  /* a stale image must not survive the build */
    memcpy(m + ENGINE_REVS2_LOAD, d + off, ENGINE_REVS2_LENGTH);
    return engine_build_image(m);
}

int main(int argc, char** argv)
{
    int i, fails = 0;
    if (argc < 3) { fprintf(stderr, "usage: %s runtime.bin disc.ssd...\n", argv[0]); return 2; }
    if (slurp(argv[1], ref, 65536) != 65536) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }

    for (i = 2; i < argc; i++) {
        const int refuse = argv[i][0] == '!';
        const char* path = argv[i] + refuse;
        long size = slurp(path, disc, sizeof disc);
        int st, diff = 0, a;
        if (size < 0) { fprintf(stderr, "cannot read %s\n", path); return 2; }

        st = load(disc, size, mem);
        if (refuse) {
            printf("%-50s refused as required: %s\n", path, engine_status_text(st));
            if (st == ENGINE_OK) { printf("  *** FAIL: accepted\n"); fails++; }
            continue;
        }
        if (st != ENGINE_OK) { printf("%-50s *** FAIL: %s\n", path, engine_status_text(st)); fails++; continue; }
        for (a = 0; a < 65536; a++) if (mem[a] != ref[a]) diff++;
        printf("%-50s %s (%d of 65536 bytes differ)\n", path, diff ? "*** FAIL" : "identical", diff);
        if (diff) fails++;

        /* The refusals.  Each must fail, or the check is decoration. */
        {
            unsigned long off;
            engine_find_revs2(disc, &off);
            disc[off + 0x1234] ^= 0x01;
            st = load(disc, size, mem);
            printf("  one REVS2 byte flipped:  %s\n", st == ENGINE_BAD_CRC ? "refused (CRC)" : "*** FAIL: accepted");
            if (st != ENGINE_BAD_CRC) fails++;
            disc[off + 0x1234] ^= 0x01;
        }
        {
            unsigned n = disc[256 + 5] >> 3, e;
            for (e = 0; e < n; e++)
                if (!memcmp(disc + 8 + 8 * e, "REVS2  ", 7)) disc[8 + 8 * e + 4] = '3';
            st = load(disc, size, mem);
            printf("  REVS2 renamed away:      %s\n", st == ENGINE_NO_REVS2 ? "refused (no REVS2)" : "*** FAIL: accepted");
            if (st != ENGINE_NO_REVS2) fails++;
        }
    }
    printf("%s\n", fails ? "engine-image: FAILED" : "engine-image: clean");
    return fails ? 1 : 0;
}
