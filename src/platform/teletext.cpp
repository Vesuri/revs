/* teletext.cpp — the MOS VDU driver and the SAA5050, for MODE 7.  Read teletext.h first: it
 * carries the measurements this file is built on and the reason screen RAM is the source of
 * truth rather than a shadow grid.
 */
#define TELETEXT_FONT_DATA   /* this translation unit owns the generated glyph table */
#include "teletext.h"
#include "../cpu/cpu.h"

extern "C" {

volatile unsigned long g_ttUnknownVdu   = 0;
volatile unsigned char g_ttLastUnknown  = 0;
volatile unsigned long g_ttVduBytes     = 0;
volatile unsigned long g_ttModeSwitches = 0;
volatile unsigned long g_ttModeDisagree = 0;

/* ⭐ The dirty-row set (teletext.h).  All rows dirty at boot so the first decode draws the whole
   page; thereafter only the writers below add bits, and the backend's decode clears them. */
volatile unsigned long g_ttRowDirty = (1UL << TT_ROWS) - 1UL;
void tt_mark_all_dirty(void) { g_ttRowDirty = (1UL << TT_ROWS) - 1UL; }

/* ═══════════════════════════════════════════════════════════════════════════════════════════
   THE MOS VDU DRIVER
   ═══════════════════════════════════════════════════════════════════════════════════════════ */

static unsigned s_cx = 0, s_cy = 0;
int             g_ttActive = 1;   /* a BBC boots in MODE 7, and so does the port's front end */

/* VDU parameter counts, MOS 1.20.  ⚠ This table is why the driver can be trusted on a stream it
   has never seen: an unhandled command still consumes exactly the right number of following
   bytes, so one unknown code cannot desynchronise everything after it and turn the page into
   noise.  Getting this wrong is indistinguishable from a broken renderer. */
static const unsigned char kVduParams[32] = {
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 2, 5, 0, 0, 1, 9, 8, 5, 0, 0, 4, 4, 0, 2,
};

static unsigned char s_param[9];
static unsigned char s_pendCmd  = 0;
static unsigned char s_pendLeft = 0;
static unsigned char s_pendGot  = 0;

static inline void tt_poke(unsigned x, unsigned y, unsigned char c)
{
    if (x < TT_COLS && y < TT_ROWS) {
        mem[TT_SCREEN_BASE + y * TT_COLS + x] = c;
        g_ttRowDirty |= (1UL << y);
    }
}

static void tt_cls(void)
{
    for (unsigned i = 0; i < TT_SCREEN_SIZE; i++) mem[TT_SCREEN_BASE + i] = 0x20;
    s_cx = s_cy = 0;
    tt_mark_all_dirty();
}

/* Scroll the whole page up one row and blank the last — what the MOS does when text advances
   off the bottom.  40 bytes a row, so a plain byte copy: mem[] must never be aliased as a wide
   pointer (make endian-lint, and the Amiga is big-endian). */
static void tt_scroll(void)
{
    for (unsigned i = 0; i < (TT_ROWS - 1) * TT_COLS; i++)
        mem[TT_SCREEN_BASE + i] = mem[TT_SCREEN_BASE + i + TT_COLS];
    for (unsigned i = 0; i < TT_COLS; i++)
        mem[TT_SCREEN_BASE + (TT_ROWS - 1) * TT_COLS + i] = 0x20;
    tt_mark_all_dirty();   /* every row's content moved up one */
}

static void tt_newline(void)
{
    s_cx = 0;
    if (++s_cy >= TT_ROWS) {
        s_cy = TT_ROWS - 1;
        tt_scroll();
    }
}

static void tt_advance(void)
{
    if (++s_cx >= TT_COLS) tt_newline();
}

static void tt_command(unsigned char cmd, const unsigned char* p)
{
    switch (cmd) {

    /* VDU 22,n — select a screen mode.  ⭐ THE PORT'S MODE SIGNAL.  Only mode 7 is a teletext
       page; every other mode means $7C00-$7FFF is no longer a screen at all (in Revs it becomes
       the dashboard code overlay), so the renderer must stop reading it. */
    case 22:
        g_ttModeSwitches++;
        g_ttActive = (p[0] == 7);
        tt_cls();
        break;

    /* VDU 23,r,... — user-defined characters and direct 6845 access.  The engine uses exactly
       one: 23,0,10,32 = CRTC R10 (cursor start) = 32, which switches the hardware cursor off.
       Nothing here draws a cursor, so honouring it is a no-op — but it is RECOGNISED, so it does
       not inflate the unknown counter and hide a real gap. */
    case 23:
        break;

    case 12:  tt_cls();                       break;   /* CLS */
    case 30:  s_cx = s_cy = 0;                break;   /* HOME */
    case 13:  s_cx = 0;                       break;   /* CR */
    case 10:  tt_newline();                   break;   /* LF */
    case 11:  if (s_cy) s_cy--;               break;   /* cursor up */
    case  9:  tt_advance();                   break;   /* cursor forward */
    case  8:  if (s_cx) s_cx--; else if (s_cy) { s_cy--; s_cx = TT_COLS - 1; } break;

    /* VDU 31,x,y — TAB.  The MOS clamps to the text window; the whole screen is the window
       here because nothing in Revs sets one (no VDU 28 in the measured stream). */
    case 31:
        if (p[0] < TT_COLS) s_cx = p[0];
        if (p[1] < TT_ROWS) s_cy = p[1];
        break;

    /* Recognised and deliberately inert: none of these can affect a teletext page, whose colour
       comes from codes IN the page, not from the VDU's colour state. */
    case  0: case  2: case  3: case  4: case  5: case  6:
    case 14: case 15: case 17: case 18: case 20: case 26: case 27:
        break;

    case  7:  break;   /* BELL — sound, not screen (Phase 5 item 4) */

    default:
        g_ttUnknownVdu++;
        g_ttLastUnknown = cmd;
        break;
    }
}

void tt_vdu(unsigned char c)
{
    g_ttVduBytes++;

    if (s_pendLeft) {                 /* collecting a command's parameters */
        s_param[s_pendGot++] = c;
        if (--s_pendLeft == 0) tt_command(s_pendCmd, s_param);
        return;
    }

    if (c < 0x20) {
        unsigned char n = kVduParams[c];
        if (n) {
            s_pendCmd  = c;
            s_pendLeft = n;
            s_pendGot  = 0;
        } else {
            tt_command(c, s_param);
        }
        return;
    }

    /* ⚠ VDU 127 = DELETE, and it is NOT a character: backspace, blank that cell, and LEAVE the
       cursor on it (derived from the real byte stream — see teletext.h).  The engine's text
       scripts lean on it, so treating 127 as a printable solid block shifts everything after it
       by one cell. */
    if (c == 0x7F) {
        if (s_cx) s_cx--; else if (s_cy) { s_cy--; s_cx = TT_COLS - 1; }
        tt_poke(s_cx, s_cy, 0x20);
        return;
    }

    /* ⚠ THE MOS ROTATES THREE CODES IN MODE 7, measured off a real MOS with
       tools/bbc_probe_m7charmap.mjs (`#` -> `_` -> `` ` `` -> `#`, everything else $20..$FF
       stored verbatim).  The SAA5050's own set has `#` at $60 and a horizontal bar at $5F, so
       ASCII that means `#` or `_` has to be moved to where the chip keeps that shape.  Revs
       leans on it: read_driver_name underlines the ENTER NAME OF DRIVER field with twelve `_`,
       and storing them untranslated put a bar-less $5F on the page (`make mode7` snapshot 26). */
    switch (c) {
        case '#':  c = 0x5F; break;
        case 0x5F: c = 0x60; break;
        case 0x60: c = '#';  break;
        default: break;
    }

    tt_poke(s_cx, s_cy, c);
    tt_advance();
}

void tt_set_active(int on){ g_ttActive = on ? 1 : 0; }
unsigned tt_cursor_x(void){ return s_cx; }
unsigned tt_cursor_y(void){ return s_cy; }

/* ═══════════════════════════════════════════════════════════════════════════════════════════
   THE SAA5050
   ═══════════════════════════════════════════════════════════════════════════════════════════

   The chip sees SEVEN data bits, so a byte's bit 7 is discarded and $80-$9F arrive as control
   codes $00-$1F.  That is why the engine writes $8D for double height and $85 for alpha
   magenta, and it is the first thing to get right — masking is not optional.

   Control codes occupy a cell and display as a space (or, while hold-mosaics is on, as the last
   mosaic seen).  ⚠ SET-AT vs SET-AFTER is a real, visible one-cell distinction and the standard
   assigns it per code: colour, double height, flash and the graphics-set choice take effect from
   the NEXT cell (set-after), while the two background codes, conceal and hold-mosaics take
   effect AT the code's own cell (set-at).  Implemented literally below rather than approximated,
   because "one cell of the wrong colour at the start of every coloured run" is precisely the
   kind of artefact this project has twice spent a day attributing to the wrong layer.
*/

/* ⚠ A GLOBAL, not a static, so gdb can read it on the target (amiga/Makefile PROBE_SYMS).
   "The flash prompt is missing" has two causes — the phase never advancing, or the glyph never
   drawn — and they are indistinguishable from a screen dump alone. */
volatile unsigned char g_ttFlashPhase  = 0;
volatile unsigned long g_ttFlashToggles = 0;
static unsigned s_flashCount = 0;

/* [ASSUMED] ~0.64 s each way at 50 Hz.  The standard specifies a nominal 1 s flash period; the
   exact duty cycle is not something any Revs screen depends on (the front end flashes one
   "PRESS" prompt), and it is measurable off a real BBC if it ever matters. */
#define TT_FLASH_FIELDS 32u

int  tt_flash_phase(void) { return (int)g_ttFlashPhase; }
void tt_tick_flash(void)
{
    if (++s_flashCount >= TT_FLASH_FIELDS) {
        s_flashCount = 0;
        g_ttFlashPhase = (unsigned char)!g_ttFlashPhase;
        g_ttFlashToggles++;
    }
}

/* ⭐ The decoder proper, emitting each cell as a KEY (teletext.h: TT_KEY) — one longword built
   arithmetically, so byte order never shows.  Returns bit 0 = the row carries double height,
   bit 1 = it carries a FLASH code (so the backend knows which rows a flash-phase flip can change
   without scanning them again).  `attr` is every field a displayable cell takes from the row
   state — set, the foreground it SHOWS (conceal and the off phase of flash show the background)
   and the background — re-derived only when a control code changes an input, so a character
   costs one OR and one store (the four-byte TtCell form was ~19 instructions a cell and the
   biggest single part of a page change on the 68000, single-stepped). */
int tt_decode_row_keys(const unsigned char* row, uint32_t out[TT_COLS], int flashOn)
{
    /* Row state, reset at the start of every row — the chip has no memory across rows, which is
       why a teletext page can be decoded a row at a time and why colour never bleeds downward. */
    unsigned char fg = TT_WHITE, bg = TT_BLACK;
    unsigned char set = TT_SET_ALPHA;
    unsigned char sepSet = 0;      /* separated rather than contiguous mosaics */
    unsigned char holdOn = 0;
    unsigned char heldCode = 0x20, heldSet = TT_SET_ALPHA;
    unsigned char flashing = 0, conceal = 0;
    unsigned char useSet = TT_SET_ALPHA;   /* the set a displayable cell draws from */
    uint32_t attr = TT_KEY(0u, TT_SET_ALPHA, TT_WHITE, TT_BLACK);
    int flags = 0;

    for (unsigned x = 0; x < TT_COLS; x++) {
        unsigned char c = (unsigned char)(row[x] & 0x7F);

        if (c >= 0x20) {
            /* A displayable character.  In graphics mode $40-$5F stay alphanumeric, and the
               generated font already holds the alpha glyphs in those slots of both mosaic sets,
               so there is no range test here — that is what the three full 128-entry sets buy. */
            out[x] = attr | c;
            if (set != TT_SET_ALPHA && !(c >= 0x40 && c < 0x60)) { heldCode = c; heldSet = useSet; }
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
        {
            const unsigned char shownNow = (conceal || (flashing && !flashOn)) ? bg : fg;
            out[x] = (holdOn && set != TT_SET_ALPHA) ? TT_KEY(heldCode, heldSet, shownNow, bg)
                                                     : TT_KEY(0x20u, TT_SET_ALPHA, shownNow, bg);
        }

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
        case 0x08: flashing = 1; flags |= 2; break;       /* flash              (set-after) */
        case 0x09: flashing = 0; break;                   /* steady             (set-after) */
        case 0x0C: break;                                 /* normal height      (set-after) */
        case 0x0D: flags |= 1; break;                     /* double height      (set-after) */
        case 0x19: sepSet = 0; break;                     /* contiguous         (set-after) */
        case 0x1A: sepSet = 1; break;                     /* separated          (set-after) */
        case 0x1F: holdOn = 0; break;                     /* release mosaics    (set-after) */
        default: break;                                   /* $0A/$0B box, $0E/$0F, $1B ESC */
        }
        useSet = (set == TT_SET_ALPHA) ? TT_SET_ALPHA : (sepSet ? TT_SET_GFX_SEP : TT_SET_GFX);
        attr   = TT_KEY(0u, useSet, (conceal || (flashing && !flashOn)) ? bg : fg, bg);
    }
    return flags;
}

/* The same decode as TtCells, for the host tools (validate_mode7, validate_trackmenu). */
int tt_decode_row_flags(const unsigned char* row, TtCell out[TT_COLS], int flashOn)
{
    uint32_t keys[TT_COLS];
    const int flags = tt_decode_row_keys(row, keys, flashOn);
    for (unsigned x = 0; x < TT_COLS; x++) {
        out[x].code = (unsigned char)TT_KEY_CODE(keys[x]);
        out[x].set  = (unsigned char)TT_KEY_SET(keys[x]);
        out[x].fg   = (unsigned char)TT_KEY_FG(keys[x]);
        out[x].bg   = (unsigned char)TT_KEY_BG(keys[x]);
    }
    return flags;
}

int tt_decode_row(const unsigned char* row, TtCell out[TT_COLS], int flashOn)
{
    return tt_decode_row_flags(row, out, flashOn) & 1;
}

} /* extern "C" */

/* ⚠⚠ THE VDU DRIVER'S OWN STATE IS PRE-STATE FOR A DIFFERENTIAL, and it is not in mem[].
   `make validate` runs the transliteration and the twin on the same mem[] and cpu; the cursor,
   the pending-command buffer and the flash phase live HERE, so a fixture that reaches OSWRCH
   left them advanced and the twin (running second) wrote one cell further along.  Symptom:
   vdu_char_def's screen bytes differed by exactly one character, in 820 of 2000 cases, with
   the MOS-call trace byte-identical.  Same class as `cpu_unwind`
   (docs/validation-harness.md).  Reset it and the two models see the same driver. */
void tt_reset_state(void)
{
    s_cx = 0; s_cy = 0; g_ttActive = 1;
    s_pendCmd = 0; s_pendLeft = 0; s_pendGot = 0;
    for (unsigned i = 0; i < sizeof s_param; i++) s_param[i] = 0;
    s_flashCount = 0; g_ttFlashPhase = 0;
}
