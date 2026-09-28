/* MODE 7 validation — the port's VDU driver + SAA5050 against a REAL BBC, byte for byte.
 *
 *   make mode7                      replay the fixture and diff every in-scope snapshot
 *   make mode7 PPM=tmp/m7           ...and write each decoded page as a PPM to look at
 *
 * ── WHAT MAKES THIS A REAL TEST ───────────────────────────────────────────────────────────
 *
 * The fixture (tmp/mode7/mode7_events.txt, produced by tools/bbc_probe_mode7.mjs) is an ORDERED
 * log of everything that touched the real machine's teletext page except the MOS itself:
 *
 *     V b          the engine sent VDU byte b through OSWRCH ($50F6, the only site)
 *     P off val    the GAME poked screen RAM directly (measured: 78 such writes in the front
 *                  end, from $3A65/$65BA/$659A — a driver-only model would silently lose them)
 *     S n file m7  at this point the real screen RAM was exactly `file`, with the Video ULA
 *                  in teletext mode (m7=1) or not (m7=0)
 *
 * MOS writes are deliberately absent from the log: reproducing them IS the thing under test, so
 * logging them would make the comparison vacuous — the failure mode `docs/validation-harness.md`
 * exists to prevent.  Replaying V and P must therefore reproduce every S exactly.
 *
 * ⚠ SCOPE, STATED RATHER THAN FUDGED.  Snapshots taken before the engine's own `VDU 22,7` are
 * BASIC's work (REVINST's instruction pages, REVSMEN's menu) and the port does not run BASIC, so
 * they are NOT reproducible and are reported as SKIPPED with the reason.  In-scope snapshots are
 * those from the first V event onward AND taken with the ULA in teletext: $7C00-$7FFF is
 * time-multiplexed with the dashboard code overlay, so a page captured during a SESSION holds
 * that overlay's machine code and not anything the VDU driver produced.
 * ⭐ That skip is a SCOPE statement, not a hiding place, and the difference was measured: mark
 * such a snapshot as teletext by hand and it PASSES, because copy_dash_data's own writes to
 * $7C00 are logged as P events and replayed verbatim.  It is skipped because this harness is the
 * gate on the VDU driver + SAA5050, and the overlay has its own (`make validate FN=copy_dash_data`).  The harness FAILS if the in-scope set is empty, because
 * "0 comparisons, all passed" is the exact shape of a green test that tests nothing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/cpu/cpu.h"
#include "../src/platform/teletext.h"

/* `mem[]` is the shared 6502 address space, defined in src/cpu/cpu.c and declared volatile
   there; this harness links that object rather than shadowing it.  The casts below are only to
   drop `volatile` for bulk reads — never to widen a byte pointer, which is the thing
   `make endian-lint` forbids (mem[] is little-endian and the Amiga is not). */
#define TT_PAGE ((const unsigned char *)(const void *)(mem + TT_SCREEN_BASE))

#define FIXTURE_DIR_DEFAULT "tmp/mode7"

static const char *g_dir = FIXTURE_DIR_DEFAULT;
static const char *g_ppm = NULL;

/* ── the teletext palette: the SAA5050's own saturated RGB ─────────────────────────────── */
static const unsigned char kRGB[8][3] = {
    {0, 0, 0}, {255, 0, 0}, {0, 255, 0}, {255, 255, 0},
    {0, 0, 255}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255},
};

/* Render a page exactly as the backends must: decode a row, then blit glyphs from the generated
   font.  ⭐ This is the SAME decode the Amiga uses, so a bug here is a bug there — which is the
   point of putting the decode in shared code rather than in the backend. */
static void render_ppm(const unsigned char *screen, const char *path)
{
    static unsigned char img[TT_HEIGHT][TT_WIDTH][3];
    memset(img, 0, sizeof img);

    for (unsigned row = 0; row < TT_ROWS; row++) {
        TtCell cells[TT_COLS];
        /* Flash phase ON for a still image: a PPM cannot blink, and rendering the off phase
           would drop the front end's flashing "PRESS" prompt from the picture entirely — which
           looks exactly like a missing-glyph bug.  The phase itself is exercised on the target,
           where the VBI advances it. */
        int dbl = tt_decode_row(screen + row * TT_COLS, cells, 1);
        /* A double-height row draws top halves here and bottom halves on the next row, and the
           next row's own content is not displayed — the chip's rule, not an approximation. */
        unsigned rows = dbl ? 2u : 1u;
        for (unsigned half = 0; half < rows; half++) {
            unsigned dy = (row + half) * TT_CELL_H;
            if (dy >= TT_HEIGHT) break;
            for (unsigned col = 0; col < TT_COLS; col++) {
                const TtCell *c = &cells[col];
                const unsigned char *g =
                    &g_ttFont[((c->set * 128u) + c->code) << TT_GLYPH_SHIFT];
                for (unsigned y = 0; y < TT_CELL_H; y++) {
                    /* Double height: each source row covers two display rows, the top half of
                       the glyph on the first row and the bottom half on the second. */
                    unsigned sy = dbl ? (half * (TT_CELL_H / 2) + y / 2) : y;
                    unsigned char bits = g[sy];
                    for (unsigned x = 0; x < TT_CELL_W; x++) {
                        unsigned ink = (bits >> (7 - x)) & 1u;
                        const unsigned char *rgb = kRGB[ink ? c->fg : c->bg];
                        unsigned px = col * TT_CELL_W + x, py = dy + y;
                        if (py < TT_HEIGHT && px < TT_WIDTH) {
                            img[py][px][0] = rgb[0];
                            img[py][px][1] = rgb[1];
                            img[py][px][2] = rgb[2];
                        }
                    }
                }
            }
        }
        if (dbl) row++;
    }

    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
    fprintf(f, "P6\n%u %u\n255\n", TT_WIDTH, TT_HEIGHT);
    fwrite(img, 1, sizeof img, f);
    fclose(f);
}

/* An ASCII view, for a terminal diff.  Alpha vs mosaic is per-row state, so a raw dump lies. */
static void print_page(const unsigned char *screen, const char *tag)
{
    for (unsigned row = 0; row < TT_ROWS; row++) {
        TtCell cells[TT_COLS];
        tt_decode_row(screen + row * TT_COLS, cells, 1);
        char line[TT_COLS + 1];
        int any = 0;
        for (unsigned col = 0; col < TT_COLS; col++) {
            unsigned char c = cells[col].code;
            if (cells[col].set != TT_SET_ALPHA && !(c >= 0x40 && c < 0x60))
                line[col] = (c == 0x20) ? ' ' : '#';
            else
                line[col] = (c >= 0x20 && c < 0x7F) ? (char)c : ' ';
            if (line[col] != ' ') any = 1;
        }
        line[TT_COLS] = 0;
        if (any) printf("   %s %2u|%s|\n", tag, row, line);
    }
}

static int load_dump(const char *file, unsigned char *out)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", g_dir, file);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "MISSING fixture dump %s\n", path); return 0; }
    size_t n = fread(out, 1, TT_SCREEN_SIZE, f);
    fclose(f);
    if (n != TT_SCREEN_SIZE) { fprintf(stderr, "%s is %zu bytes, want 1024\n", path, n); return 0; }
    return 1;
}


/* ⭐⭐ THE DECODER'S OWN DIFFERENTIAL.  Nothing above can see the SAA5050 decode — the snapshots
   compare screen RAM, and `render_ppm` is for a human to look at — so a change to
   `tt_decode_row_flags` is gated here against the decoder it replaced, kept verbatim below
   (`ref_decode_row`, the row loop before the flash flag and the carried foreground).  Every
   snapshot page in both flash phases, then random rows weighted to CONTROL codes (colours, flash,
   conceal, hold, double height, backgrounds), which is where every rule lives: cells, the double
   height bit, and the flash bit against "a byte whose low seven bits are $08". */
static int ref_decode_row(const unsigned char* row, TtCell out[TT_COLS], int flashOn)
{
    /* Row state, reset at the start of every row — the chip has no memory across rows, which is
       why a teletext page can be decoded a row at a time and why colour never bleeds downward. */
    unsigned char fg = TT_WHITE, bg = TT_BLACK;
    unsigned char set = TT_SET_ALPHA;
    unsigned char sepSet = 0;      /* separated rather than contiguous mosaics */
    unsigned char holdOn = 0;
    unsigned char heldCode = 0x20, heldSet = TT_SET_ALPHA;
    unsigned char flashing = 0, conceal = 0;
    int doubleHeight = 0;

    for (unsigned x = 0; x < TT_COLS; x++) {
        unsigned char c = (unsigned char)(row[x] & 0x7F);

        if (c >= 0x20) {
            /* A displayable character.  In graphics mode $40-$5F stay alphanumeric, and the
               generated font already holds the alpha glyphs in those slots of both mosaic sets,
               so there is no range test here — that is what the three full 128-entry sets buy. */
            unsigned char useSet = set;
            if (set != TT_SET_ALPHA) {
                useSet = sepSet ? TT_SET_GFX_SEP : TT_SET_GFX;
                if (!(c >= 0x40 && c < 0x60)) { heldCode = c; heldSet = useSet; }
            }
            out[x].code = c;
            out[x].set  = useSet;
            out[x].fg   = (conceal || (flashing && !flashOn)) ? bg : fg;
            out[x].bg   = bg;
            continue;
        }

        /* A control code.  Apply the set-at ones first, then decide what the cell displays, then
           apply the set-after ones — that ordering IS the set-at/set-after rule. */
        switch (c) {
        case 0x1C: bg = TT_BLACK; break;                  /* black background   (set-at) */
        case 0x1D: bg = fg;       break;                  /* new background     (set-at) */
        case 0x18: conceal = 1;   break;                  /* conceal            (set-at) */
        case 0x1E: holdOn = 1;    break;                  /* hold mosaics       (set-at) */
        default: break;
        }

        /* While hold-mosaics is on, a control cell shows the last mosaic instead of a space. */
        if (holdOn && set != TT_SET_ALPHA) {
            out[x].code = heldCode;
            out[x].set  = heldSet;
        } else {
            out[x].code = 0x20;
            out[x].set  = TT_SET_ALPHA;
        }
        out[x].fg = (conceal || (flashing && !flashOn)) ? bg : fg;
        out[x].bg = bg;

        switch (c) {
        case 0x00: case 0x01: case 0x02: case 0x03:       /* alpha colour       (set-after) */
        case 0x04: case 0x05: case 0x06: case 0x07:
            fg = c; set = TT_SET_ALPHA; conceal = 0;
            holdOn = 0; heldCode = 0x20; heldSet = TT_SET_ALPHA;
            break;
        case 0x10: case 0x11: case 0x12: case 0x13:       /* graphics colour    (set-after) */
        case 0x14: case 0x15: case 0x16: case 0x17:
            fg = (unsigned char)(c & 0x07); set = TT_SET_GFX; conceal = 0;
            break;
        case 0x08: flashing = 1; break;                   /* flash              (set-after) */
        case 0x09: flashing = 0; break;                   /* steady             (set-after) */
        case 0x0C: doubleHeight |= 0; break;              /* normal height      (set-after) */
        case 0x0D: doubleHeight = 1; break;               /* double height      (set-after) */
        case 0x19: sepSet = 0; break;                     /* contiguous         (set-after) */
        case 0x1A: sepSet = 1; break;                     /* separated          (set-after) */
        case 0x1F: holdOn = 0; break;                     /* release mosaics    (set-after) */
        default: break;                                   /* $0A/$0B box, $0E/$0F, $1B ESC */
        }
    }
    return doubleHeight;
}

static int decode_check_row(const unsigned char* row, int flashOn)
{
    TtCell a[TT_COLS], b[TT_COLS];
    int wantFlash = 0;
    for (unsigned x = 0; x < TT_COLS; x++) if ((row[x] & 0x7F) == 0x08) wantFlash = 1;
    const int want = ref_decode_row(row, a, flashOn) | (wantFlash << 1);
    const int got  = tt_decode_row_flags(row, b, flashOn);
    if (got != want) return 1;
    for (unsigned x = 0; x < TT_COLS; x++)
        if (a[x].code != b[x].code || a[x].set != b[x].set || a[x].fg != b[x].fg || a[x].bg != b[x].bg)
            return 1;
    return 0;
}
static int decode_differential(const unsigned char* pages, unsigned npages)
{
    unsigned rows = 0, bad = 0;
    for (unsigned p = 0; p < npages; p++)
        for (unsigned r = 0; r < TT_ROWS; r++)
            for (int ph = 0; ph < 2; ph++) { bad += decode_check_row(pages + p * TT_SCREEN_SIZE + r * TT_COLS, ph); rows++; }
    unsigned long seed = 0x2545F491u;
    for (unsigned i = 0; i < 200000u; i++) {
        unsigned char row[TT_COLS];
        for (unsigned x = 0; x < TT_COLS; x++) {
            seed = seed * 1103515245u + 12345u;
            const unsigned v = (unsigned)(seed >> 16);
            row[x] = (v & 3u) == 0u ? (unsigned char)(0x80u | ((v >> 2) & 0x1Fu))   /* control */
                   : (unsigned char)(v >> 4);                                        /* anything */
        }
        bad += decode_check_row(row, (int)(i & 1u)); rows++;
    }
    printf("decoder differential: %u rows, %u differ from the reference decoder\n", rows, bad);
    return bad != 0;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--dir=", 6)) g_dir = argv[i] + 6;
        else if (!strncmp(argv[i], "--ppm=", 6)) g_ppm = argv[i] + 6;
    }

    char path[512];
    snprintf(path, sizeof path, "%s/mode7_events.txt", g_dir);
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr,
                "no fixture at %s\n"
                "  produce it first:  cd tools/jsbeeb && volta run --node 24.15.0 -- \\\n"
                "      node ../bbc_probe_mode7.mjs --dump=../../tmp/mode7\n", path);
        return 2;
    }

    /* Start from a blank page.  ⚠ NOT from revs_mem.bin: $7C00-$7FFF in any static image holds
       the dashboard code overlay's source, not a teletext page (docs/static-map.md), so seeding
       from it would start the comparison with 1 KB of wrong bytes. */
    memset((void *)(mem + TT_SCREEN_BASE), 0x20, TT_SCREEN_SIZE);

    unsigned long vdu = 0, pokes = 0;
    int inScope = 0;                 /* the engine's VDU 22,7 has been seen */
    int checked = 0, failed = 0, skipped = 0, notTeletext = 0;
    static unsigned char pages[64 * TT_SCREEN_SIZE];
    unsigned nPages = 0;
    char line[256];

    while (fgets(line, sizeof line, f)) {
        if (line[0] == 'V') {
            unsigned b = (unsigned)strtoul(line + 2, NULL, 10);
            tt_vdu((unsigned char)b);
            vdu++;
            inScope = 1;
            continue;
        }
        if (line[0] == 'P') {
            char *end;
            unsigned off = (unsigned)strtoul(line + 2, &end, 10);
            unsigned val = (unsigned)strtoul(end, NULL, 10);
            if (off < TT_SCREEN_SIZE) mem[TT_SCREEN_BASE + off] = (unsigned char)val;
            pokes++;
            continue;
        }
        if (line[0] != 'S') continue;

        int idx, m7 = 1;
        char file[128];
        if (sscanf(line + 2, "%d %127s %d", &idx, file, &m7) < 2) continue;

        if (!inScope) {
            skipped++;
            continue;      /* BASIC's own screens — stated in the summary, not silently dropped */
        }
        if (!m7) {
            notTeletext++; /* a session was up: those 1024 bytes are the dashboard code overlay,
                              which is copy_dash_data's business, not the VDU driver's */
            continue;
        }

        static unsigned char want[TT_SCREEN_SIZE];
        if (!load_dump(file, want)) { failed++; continue; }

        unsigned bad = 0, firstBad = 0;
        for (unsigned i = 0; i < TT_SCREEN_SIZE; i++) {
            if (mem[TT_SCREEN_BASE + i] != want[i]) {
                if (!bad) firstBad = i;
                bad++;
            }
        }
        checked++;
        if (nPages < 64u) memcpy(pages + (nPages++) * TT_SCREEN_SIZE, mem + TT_SCREEN_BASE, TT_SCREEN_SIZE);
        if (bad == 0) {
            printf("PASS  snapshot %-2d %s   1024/1024 bytes identical\n", idx, file);
        } else {
            failed++;
            printf("FAIL  snapshot %-2d %s   %u of 1024 bytes differ, first at "
                   "offset %u (row %u col %u): got $%02X want $%02X\n",
                   idx, file, bad, firstBad, firstBad / TT_COLS, firstBad % TT_COLS,
                   mem[TT_SCREEN_BASE + firstBad], want[firstBad]);
            print_page(TT_PAGE, "port");
            print_page(want, "bbc ");
        }
        if (g_ppm) {
            char out[512];
            snprintf(out, sizeof out, "%s_%02d.ppm", g_ppm, idx);
            render_ppm(TT_PAGE, out);
            snprintf(out, sizeof out, "%s_%02d_bbc.ppm", g_ppm, idx);
            render_ppm(want, out);
        }
    }
    fclose(f);

    printf("\n%lu VDU bytes, %lu direct pokes replayed\n", vdu, pokes);
    printf("unknown VDU codes: %lu (last $%02X)\n", g_ttUnknownVdu, g_ttLastUnknown);
    printf("snapshots: %d checked, %d failed, %d skipped (before the engine's VDU 22,7 — "
           "BASIC's screens, which this port does not run)", checked, failed, skipped);
    if (notTeletext)
        printf(", %d not a teletext page (a session was running, so $7C00 held the dashboard "
               "code overlay)", notTeletext);
    printf("\n");

    /* ⭐ Fixture-or-fail, the same rule as make validate: a run that compared nothing is a
       FAILURE, not a pass.  Every green in this project has to have been able to go red. */
    if (checked == 0) {
        printf("\nFAIL — zero in-scope snapshots compared.  That is a vacuous pass, not a pass:\n"
               "  either the fixture has no V events (the probe never reached REVS2's front end)\n"
               "  or every snapshot predates it.  Re-run the probe.\n");
        return 1;
    }
    if (g_ttUnknownVdu) {
        printf("\nFAIL — the driver met %lu VDU code(s) it has no arm for; the last was $%02X.\n"
               "  An unhandled code still consumes the right parameter count, so the page may\n"
               "  look fine and be wrong later.  Add the arm.\n", g_ttUnknownVdu, g_ttLastUnknown);
        return 1;
    }
    if (decode_differential(pages, nPages)) failed++;
    printf(failed ? "\nFAIL\n" : "\nPASS — the port's MODE 7 page is byte-identical to the BBC's, "
                                  "and the decoder matches its reference\n");
    return failed ? 1 : 0;
}
