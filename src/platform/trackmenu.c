/* trackmenu.c — the circuit menu.  See trackmenu.h for the model and the measurements.
 *
 * Every literal in this file is annotated with the REVSMEN line that produced it, and every one
 * of them is checked against a page recorded off a real BBC by `make trackmenu`.
 */
#include "trackmenu.h"
#include "titlescreen.h"
#include "track.h"
#include "../cpu/cpu.h"          /* mem[] — the teletext page lives in it */

/* ── the options ──────────────────────────────────────────────────────────────────────────
 * REVSMEN lines 160-200 print these five names in this order; line 280's `ON A% GOTO` pairs each
 * with a `*LO.` of the matching file, which is where the track index comes from.
 * ⚠ The order is NOT revs_tracks[]'s — see the header note.  Nürburgring is the port's own
 * addition and has no REVSMEN line; it is last so that a build without that disc simply offers
 * one fewer option.
 */
typedef struct {
    const char*   label;   /* exactly as the real page prints it (upper case) */
    unsigned char track;   /* index into revs_tracks[] */
} TmOption;

static const TmOption kOptions[TM_OPTIONS_MAX] = {
    { "BRANDS HATCH",   1 },   /* REVSMEN 160 / 290 *LO.BRANDS  */
    { "DONINGTON PARK", 2 },   /* REVSMEN 170 / 320 *LO.DONING  */
    { "OULTON PARK",    3 },   /* REVSMEN 180 / 350 *LO.OULTON  */
    { "SNETTERTON",     4 },   /* REVSMEN 190 / 380 *LO.SNETTER */
    { "SILVERSTONE",    0 },   /* REVSMEN 200 / 410 *LO.SILVER  */
    { "NURBURGRING",    5 },   /* the port's sixth option (user decision)  */
};

/* The negative-INKEY byte per menu key.  Derived the same way as RevsInput's map and cross-checked
   against jsbeeb's own key matrix: a BBC internal key number is (row<<4)|col and the negative-INKEY
   byte is 255 - internal, so SPACE [col 2, row 6] = $62 -> $9D, which is the value the existing map
   already carries — that agreement is the check on the six digits below. */
const unsigned char tm_key_codes[TM_OPTIONS_MAX + 1] = {
    0x9Du,   /* SPACE  -99  confirm            */
    0xCFu,   /* '1'    -49                     */
    0xCEu,   /* '2'    -50                     */
    0xEEu,   /* '3'    -18                     */
    0xEDu,   /* '4'    -19                     */
    0xECu,   /* '5'    -20                     */
    0xCBu,   /* '6'    -53  (Nürburgring)      */
};

volatile unsigned char  g_tmPhase     = TM_TITLE;
volatile unsigned char  g_tmOption    = 0;
volatile unsigned char  g_tmTrack     = 0;
volatile unsigned short g_tmMisrouted = 0;
volatile unsigned short g_tmRefusals  = 0;
volatile unsigned long  g_tmFields    = 0;

static unsigned s_options   = TM_OPTIONS_FAITHFUL;
/* Rows to lift the menu by, from the PRESS line down (the rules and the REVS logo above it stay
   put).  0 for the faithful five-option page — which is what `make trackmenu` diffs byte-for-byte
   against the real BBC, so it MUST stay put.  The port's sixth option (Nürburgring) would
   otherwise land on row 22, jammed against the row-23 SPACE prompt; dropping the two line feeds
   before PRESS lifts it and every circuit two rows, restoring the two blank rows the five-option
   page has between its last circuit and the prompt.  ⚠ The prompt itself is NOT lifted — it stays
   at row 23, and those two blank rows are the gap this creates. */
static unsigned s_rowShift  = 0;
static unsigned s_dwell     = 0;   /* fields the title page has been up for */
static unsigned s_spaceSeenUp = 0; /* SPACE has been observed released since the digit landed */

/* ── painting ─────────────────────────────────────────────────────────────────────────────
 * Through tt_vdu(), never straight into mem[]: the driver is the validated one and it is what
 * keeps the cursor, the wrap and the attribute cells consistent with everything else on screen.
 */
static void vdu(unsigned char b) { tt_vdu(b); }

static void tab(unsigned x, unsigned y)   /* VDU 31,x,y — PRINT TAB(x,y) */
{
    vdu(31); vdu((unsigned char)x); vdu((unsigned char)y);
}

static void text(const char* s) { while (*s) vdu((unsigned char)*s++); }

static void spaces(unsigned n) { while (n--) vdu(' '); }

/* REVSMEN 80 / 130: `VDU151:FORI%=1TO39:VDU185:NEXT` — graphics white, then 39 mosaics.
   ⚠ 39, not 40: the control code occupies column 0, which is what makes the rule start one cell
   in.  A 40-mosaic row would wrap and scroll the page. */
static void rule(unsigned row)
{
    unsigned i;
    tab(0, row);
    vdu(151);
    for (i = 0; i < 39; i++) vdu(185);
}

/* REVSMEN 100: one double-height "REVS REVS REVS", printed on two consecutive rows so the
   SAA5050 draws the top halves on the first and the bottoms on the second. */
static void logo(unsigned row)
{
    unsigned rep;
    tab(0, row);
    vdu(141);              /* double height     */
    vdu(133);              /* alpha magenta     */
    spaces(4);
    for (rep = 0; rep < 3; rep++) {
        if (rep) { spaces(3); vdu(133); }
        text("R"); vdu(131);   /* alpha yellow */
        text("E"); vdu(134);   /* alpha cyan   */
        text("V"); vdu(130);   /* alpha green  */
        text("S");
    }
}

/* REVSMEN 160-200, one line each: five spaces, then the digit on a blue field, then the name in
   yellow on black.  `hot` recolours the field red — REVSMEN 240, `VDU129,157,131`. */
static void option_row(unsigned n, int hot)
{
    tab(0, (10u + 2u * n) - s_rowShift);
    spaces(5);
    vdu(hot ? 129u : 132u);   /* alpha red when chosen, else alpha blue */
    vdu(157);                 /* new background = that colour           */
    vdu(hot ? 131u : 134u);   /* alpha yellow when chosen, else cyan    */
    vdu((unsigned char)('0' + n));
    spaces(2);
    vdu(156);                 /* black background */
    vdu(131);                 /* alpha yellow     */
    spaces(7);
    text(kOptions[n - 1].label);
}

static void paint_menu(void)
{
    unsigned n;
    vdu(12);                                  /* REVSMEN 60: MODE7 clears the page      */
    rule(2);                                  /* REVSMEN 80                              */
    logo(3);                                  /* REVSMEN 90-120, twice                   */
    logo(4);
    rule(5);                                  /* REVSMEN 130                             */
    tab(0, 10u - s_rowShift);                 /* REVSMEN 150                             */
    vdu(134); vdu(136);                       /*   alpha cyan, FLASH                     */
    text("    PRESS");
    for (n = 1; n <= s_options; n++) option_row(n, 0);
}

/* REVSMEN 250, printed only once a digit has been accepted. */
static void paint_space_prompt(void)
{
    tab(0, 23);
    vdu(134);
    text("     PRESS SPACE BAR TO CONTINUE");
}

/* ⚠ PORT-AUTHORED, and there is no original to be faithful to — see tm_reject(). */
static void paint_refusal(void)
{
    tab(0, 23);
    vdu(129); vdu(136);                       /* alpha red, flashing */
    text("   THAT CIRCUIT IS NOT IN THIS BUILD   ");
}

/* ── the option→index cross-check ─────────────────────────────────────────────────────────
 * Both halves of an option must agree: the label the page prints and the circuit the index
 * installs.  Compared case-insensitively because the page is upper case and revs_tracks[].name is
 * the front end's mixed case ("Brands Hatch"), which is the only difference between them.
 */
static int same_name(const char* upper, const char* mixed)
{
    for (;; upper++, mixed++) {
        unsigned char a = (unsigned char)*upper, b = (unsigned char)*mixed;
        if (b >= 'a' && b <= 'z') b = (unsigned char)(b - 'a' + 'A');
        if (a != b) return 0;
        if (!a) return 1;
    }
}

static void cross_check(void)
{
    unsigned n;
    g_tmMisrouted = 0;
    for (n = 1; n <= s_options; n++) {
        unsigned char t = kOptions[n - 1].track;
        if (t >= REVS_TRACK_COUNT || !same_name(kOptions[n - 1].label, revs_tracks[t].name))
            g_tmMisrouted++;
    }
}

/* ── the sequence ─────────────────────────────────────────────────────────────────────────── */

void tm_begin(unsigned options)
{
    unsigned i;

    if (options > TM_OPTIONS_MAX)  options = TM_OPTIONS_MAX;
    /* One option per circuit this build actually has.  ⚠ Not `options` as asked for: a checkout
       without the Nürburgring disc has five circuits and offering a sixth would let the player
       pick a row that cannot install (track.h §REVS_TRACK_AVAILABLE). */
    if (options > REVS_TRACK_AVAILABLE) options = REVS_TRACK_AVAILABLE;
    if (options < 1) options = 1;
    s_options = options;
    /* Lift the block only once it is taller than the faithful page (the sixth option).  The
       five-option page stays exactly where `make trackmenu` recorded it. */
    s_rowShift = (s_options > TM_OPTIONS_FAITHFUL) ? 2u : 0u;
    cross_check();

    g_tmPhase  = TM_TITLE;
    g_tmOption = 0;
    g_tmTrack  = 0;
    g_tmFields = 0;
    s_dwell    = 0;
    s_spaceSeenUp = 0;

    /* REVSMEN 40: `*LOAD 5TRSCRN` — a straight 1 KB copy into MODE 7 screen RAM, which is
       exactly what the real machine displays (titlescreen.h has the provenance).  ⚠ Not through
       tt_vdu(): `*LOAD` is not a VDU stream, and routing it through the driver would interpret
       teletext codes as commands.  The cursor is left wherever it was; nothing reads it until
       paint_menu()'s VDU 12. */
    for (i = 0; i < REVS_TITLESCREEN_LEN; i++)
        mem[REVS_TITLESCREEN_LO + i] = revs_titlescreen[i];
}

void tm_tick(unsigned keys, unsigned fields)
{
    g_tmFields += fields;

    switch (g_tmPhase) {
    case TM_TITLE:
        /* REVSMEN 50: a delay loop, measured on real hardware rather than counted in BASIC. */
        s_dwell += fields;
        if (s_dwell >= TM_TITLE_FIELDS) {
            paint_menu();
            g_tmPhase = TM_SELECT;
        }
        break;

    case TM_SELECT: {
        /* REVSMEN 220: the first digit in range wins and the REPEAT never runs again.  Scanned
           low-to-high so a stuck high key cannot mask option 1 — the real program takes whatever
           the keyboard buffer hands it, which has no ordering at all. */
        unsigned n;
        for (n = 1; n <= s_options; n++) {
            if (!(keys & TM_KEY_OPTION(n))) continue;
            g_tmOption = (unsigned char)n;
            g_tmTrack  = kOptions[n - 1].track;
            option_row(n, 1);            /* REVSMEN 240: recolour the field red */
            paint_space_prompt();        /* REVSMEN 250                          */
            g_tmPhase = TM_CONFIRM;
            /* ⚠ REVSMEN 260 is `*FX15,0` — flush the keyboard buffer — and then 270 waits for a
               SPACE to ARRIVE.  So a SPACE already down when the digit landed must not count:
               require one field with it released first.  Without this, a player resting on SPACE
               would skip the confirm entirely, which looks like the digit doing two things. */
            s_spaceSeenUp = 0;
            break;
        }
        break;
    }

    case TM_CONFIRM:
        if (!(keys & TM_KEY_SPACE)) s_spaceSeenUp = 1;
        else if (s_spaceSeenUp)     g_tmPhase = TM_FINISHED;
        break;

    default:
        break;
    }
}

unsigned tm_option_for_track(unsigned track)
{
    unsigned n;
    for (n = 1; n <= s_options; n++)
        if (kOptions[n - 1].track == track) return n;
    return 0;
}

int      tm_finished(void) { return g_tmPhase == TM_FINISHED; }
unsigned tm_phase(void)   { return g_tmPhase; }
unsigned tm_option(void)  { return g_tmOption; }
unsigned tm_track(void)   { return g_tmTrack; }

void tm_reject(void)
{
    g_tmRefusals++;
    /* Un-highlight the row, say why, and take the choice back.  The page is otherwise left
       standing: repainting it whole would clear the message on the same field it was written. */
    if (g_tmOption) option_row(g_tmOption, 0);
    paint_refusal();
    g_tmOption    = 0;
    g_tmTrack     = 0;
    g_tmPhase     = TM_SELECT;
    s_spaceSeenUp = 0;
}
