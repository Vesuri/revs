/* TRACK MENU validation — the port's circuit menu against a REAL BBC's REVSMEN, byte for byte.
 *
 *   make trackmenu                  diff every recorded page
 *   make trackmenu PPM=tmp/tmenu    ...and write each page as a PPM to look at
 *
 * ── WHY THIS IS NOT VACUOUS ───────────────────────────────────────────────────────────────
 *
 * The menu is PORT-AUTHORED (REVSMEN is BASIC; docs/phases.md §5d), so it is the one screen in
 * this game with no transliteration to be checked against.  Left there it would be the exact shape
 * of thing `docs/validation-harness.md` is about: code whose only oracle is that it looks right.
 *
 * So the oracle is recorded instead.  `tools/bbc_probe_trackmenu.mjs` boots revs.ssd under jsbeeb,
 * CHAINs the real REVSMEN, and dumps MODE 7 screen RAM for the title page, the menu page and the
 * page after each of the five digits.  This harness paints the same six pages through the port's
 * own VDU driver and requires them to be IDENTICAL.  Nothing about the expected page is written
 * down here — only file names.
 *
 * ⚠ SCOPE, STATED RATHER THAN FUDGED.  The shipped menu offers a SIXTH circuit (Nürburgring, a
 * user decision — trackmenu.h §THE ONE DELIBERATE DIVERGENCE) which the real page does not have,
 * so every comparison here is made with TM_OPTIONS_FAITHFUL = 5.  That covers every row, column
 * and attribute the two configurations share; the sixth row comes out of the same loop as the
 * other five and is the only thing outside the fixture.  Its ROUTING is still checked, by the
 * cross-check below.
 *
 * ⚠ The harness FAILS if the fixture is missing or if it made zero comparisons — "0 pages, all
 * passed" is the exact shape of a green test that tests nothing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/cpu/cpu.h"
#include "../src/platform/teletext.h"
#include "../src/platform/trackmenu.h"

#define PAGE ((const unsigned char *)(const void *)(mem + TT_SCREEN_BASE))

static const char *g_dir = "tmp/trackmenu";
static const char *g_ppm = NULL;
static int g_fail = 0, g_compared = 0;

static const unsigned char kRGB[8][3] = {
    {0, 0, 0}, {255, 0, 0}, {0, 255, 0}, {255, 255, 0},
    {0, 0, 255}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255},
};

static unsigned char *load(const char *name, unsigned expect)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", g_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr,
                "trackmenu: cannot open %s\n"
                "   the fixture is recorded off a real BBC and is not checked in:\n"
                "   run `make trackmenu-fixture` first (needs volta/node + revs.ssd)\n",
                path);
        exit(2);
    }
    unsigned char *buf = (unsigned char *)malloc(expect);
    size_t got = fread(buf, 1, expect, f);
    fclose(f);
    if (got != expect) {
        fprintf(stderr, "trackmenu: %s is %zu bytes, expected %u\n", path, got, expect);
        exit(2);
    }
    return buf;
}

/* The same decode the backends use, so an ASCII view here is the SAA5050's view. */
static void print_page(const unsigned char *p, const char *tag)
{
    for (unsigned row = 0; row < TT_ROWS; row++) {
        TtCell cells[TT_COLS];
        tt_decode_row(p + row * TT_COLS, cells, 1);
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

static void write_ppm(const unsigned char *screen, const char *path)
{
    static unsigned char img[TT_HEIGHT][TT_WIDTH][3];
    memset(img, 0, sizeof img);
    for (unsigned row = 0; row < TT_ROWS; row++) {
        TtCell cells[TT_COLS];
        int dbl = tt_decode_row(screen + row * TT_COLS, cells, 1);
        unsigned rows = dbl ? 2u : 1u;
        for (unsigned half = 0; half < rows; half++) {
            unsigned dy = (row + half) * TT_CELL_H;
            if (dy >= TT_HEIGHT) break;
            for (unsigned col = 0; col < TT_COLS; col++) {
                const TtCell *c = &cells[col];
                const unsigned char *g = &g_ttFont[((c->set * 128u) + c->code) << TT_GLYPH_SHIFT];
                for (unsigned y = 0; y < TT_CELL_H; y++) {
                    unsigned sy = dbl ? (half * (TT_CELL_H / 2) + y / 2) : y;
                    unsigned char bits = g[sy];
                    for (unsigned x = 0; x < TT_CELL_W; x++) {
                        unsigned ink = (bits >> (7 - x)) & 1u;
                        const unsigned char *rgb = kRGB[ink ? c->fg : c->bg];
                        unsigned px = col * TT_CELL_W + x, py = dy + y;
                        if (py < TT_HEIGHT && px < TT_WIDTH)
                            memcpy(img[py][px], rgb, 3);
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

static void compare(const char *what, const char *file)
{
    unsigned char *want = load(file, TT_SCREEN_SIZE);
    unsigned diff = 0, first = 0;
    for (unsigned i = 0; i < TT_SCREEN_SIZE; i++)
        if (PAGE[i] != want[i]) { if (!diff) first = i; diff++; }

    g_compared++;
    if (diff) {
        g_fail++;
        printf("FAIL %-22s %u of %u bytes differ; first at row %u col %u "
               "(ours $%02X, real $%02X)\n",
               what, diff, TT_SCREEN_SIZE, first / TT_COLS, first % TT_COLS,
               PAGE[first], want[first]);
        print_page(want,  "real");
        print_page(PAGE,  "ours");
    } else {
        printf("ok   %-22s %u bytes identical\n", what, TT_SCREEN_SIZE);
    }

    if (g_ppm) {
        char path[512];
        snprintf(path, sizeof path, "%s_%s.ppm", g_ppm, what);
        write_ppm(PAGE, path);
    }
    free(want);
}

/* Run the menu from the top and stop at `phase`, pressing `keys` once the menu is up. */
static void run_to_menu(void)
{
    tm_begin(TM_OPTIONS_FAITHFUL);
    /* ⚠ One field at a time, not one jump of TM_TITLE_FIELDS: the dwell is spent inside tm_tick()
       and a single fat call would never exercise the accumulation the backends actually use. */
    for (unsigned f = 0; f < TM_TITLE_FIELDS + 2 && tm_phase() == TM_TITLE; f++) tm_tick(0, 1);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--dir=", 6))      g_dir = argv[i] + 6;
        else if (!strncmp(argv[i], "--ppm=", 6)) g_ppm = argv[i] + 6;
    }

    /* ── the title page ───────────────────────────────────────────────────────────────────
       ⚠ Checked BEFORE any tick: tm_begin() copies it in and the very next thing the menu does
       is clear it, so a harness that ticked first would compare the menu against the title and
       report a 1000-byte difference for a page that was right. */
    tm_begin(TM_OPTIONS_FAITHFUL);
    compare("title", "title.bin");

    /* The cross-check tm_begin() ran: option→revs_tracks[] routing.  ⭐ This is the half of the
       menu the fixture CANNOT see — a page can be byte-perfect and install the wrong circuit. */
    if (g_tmMisrouted) {
        printf("FAIL %-22s %u option(s) do not name the circuit they install\n",
               "option routing", g_tmMisrouted);
        g_fail++;
    } else {
        printf("ok   %-22s all %u options name the circuit they install\n",
               "option routing", TM_OPTIONS_FAITHFUL);
    }
    g_compared++;

    /* ── the menu page ──────────────────────────────────────────────────────────────────── */
    run_to_menu();
    if (tm_phase() != TM_SELECT) {
        printf("FAIL %-22s still phase %u after %u fields\n",
               "title dwell", tm_phase(), TM_TITLE_FIELDS + 2);
        g_fail++;
    }
    compare("menu", "menu.bin");

    /* ── one page per selection ─────────────────────────────────────────────────────────── */
    for (unsigned n = 1; n <= TM_OPTIONS_FAITHFUL; n++) {
        char what[32], file[32];
        snprintf(what, sizeof what, "menu_sel%u", n);
        snprintf(file, sizeof file, "menu_sel%u.bin", n);
        run_to_menu();
        tm_tick(TM_KEY_OPTION(n), 1);
        if (tm_phase() != TM_CONFIRM) {
            printf("FAIL %-22s option %u did not take (phase %u)\n", what, n, tm_phase());
            g_fail++;
        }
        compare(what, file);

        /* ⭐ And the confirm, which no page can show: SPACE must be seen RELEASED before it
           counts (REVSMEN 260's buffer flush).  Held from the start, it must NOT finish. */
        tm_tick(TM_KEY_SPACE, 1);
        if (tm_finished()) {
            printf("FAIL %-22s SPACE held from the digit finished the menu\n", what);
            g_fail++;
        }
        tm_tick(0, 1);
        tm_tick(TM_KEY_SPACE, 1);
        if (!tm_finished()) {
            printf("FAIL %-22s SPACE after a release did not finish the menu\n", what);
            g_fail++;
        } else if (tm_track() != n % TM_OPTIONS_FAITHFUL) {
            /* Options 1..4 are tracks 1..4 and option 5 is track 0 — which is exactly n % 5.
               ⚠ Written as the arithmetic and not as a copy of kOptions[], so this is an
               independent statement of the mapping rather than the same table twice. */
            printf("FAIL %-22s option %u installs track %u, expected %u\n",
                   what, n, tm_track(), n % TM_OPTIONS_FAITHFUL);
            g_fail++;
        }
        g_compared++;
    }

    if (!g_compared) {
        fprintf(stderr, "trackmenu: NOTHING WAS COMPARED — the fixture is empty\n");
        return 2;
    }
    printf("\ntrackmenu: %d checks, %d failed\n", g_compared, g_fail);
    return g_fail ? 1 : 0;
}
