#ifndef TELETEXT_H
#define TELETEXT_H
/* teletext.h — THE MODE 7 MODEL.  One copy, both backends.
 *
 * Out of a race Revs runs in MODE 7, and that is TWO mechanisms, not one.  Both are modelled
 * here because both are BBC hardware/OS behaviour rather than game code, which is the same
 * seam `bbc_hw.cpp` sits on (docs/faithfulness-seam.md):
 *
 *   1. THE MOS VDU DRIVER.  `vdu_char_def` ($5092) branches on `$64` bit 7; the MODE 7 arm is
 *      `JSR $FFEE` — OSWRCH — so in the front end every character leaves the engine as a VDU
 *      byte and the OS maintains the screen.  Measured on a real BBC
 *      (`tools/bbc_probe_mode7.mjs`, 310 engine VDU bytes): the MOS driver performs 29878 of
 *      the writes to $7C00-$7FFF and the game only 1105, so this is the majority of the work
 *      and the port has to do it.
 *
 *   2. THE SAA5050.  MODE 7 glyphs are NOT the MOS ROM font that `OSWORD 10` returns — they
 *      come from a Mullard/Philips teletext character generator with its own ROM, driven by the
 *      codes in screen RAM.  `teletext_font.h` is that character generator; the decode of
 *      colours, mosaics, double height, flash and hold is here.
 *
 * ⭐ SCREEN RAM IS THE SINGLE SOURCE OF TRUTH, and that is a measurement, not a preference:
 * the game ALSO pokes teletext codes straight into $7C00-$7FFF (70 writes from $3A65, 8 from
 * $65BA, 1 from $659A on a real front-end run).  A driver that kept its own shadow grid would
 * silently lose those.  So the VDU driver writes `mem[]` exactly where the MOS would, and the
 * renderer reads `mem[]`.
 *
 * ⚠⚠ $7C00-$7FFF IS TIME-MULTIPLEXED with the dashboard code overlay `copy_dash_data` ($18EA)
 * assembles for the race view — the same 1 KB (docs/static-map.md §Open items 6 and 10).  So
 * rendering it as teletext is only correct while the machine is in MODE 7, and `tt_active()` is
 * the guard.  Render the page during a race and you draw executable code as mosaics.
 *
 * ── THE ENGINE'S ENTIRE VDU VOCABULARY ────────────────────────────────────────────────────
 *
 * Measured, not assumed — `tools/bbc_probe_mode7.mjs` decodes the stream with the correct
 * per-command parameter counts (a raw byte histogram is misleading here, because VDU 31's x/y
 * look like VDU 2 and VDU 4 commands).  REVS2 issues exactly FIVE:
 *
 *     VDU 22,7                    select MODE 7                          x1
 *     VDU 23,0,10,32,0,0,0,0,0,0  6845 R10 = 32, i.e. cursor off         x1
 *     VDU 12                      CLS                                    x2
 *     VDU 31,x,y                  TAB(x,y)                               x10
 *     VDU 127                     backspace, blank, stay                 (the line editor)
 *
 * everything else being a character $20-$FF that goes to screen RAM as-is, teletext attribute
 * codes included ($8D double height, $9C/$9D background, $82-$86 alpha colours, $7F block).
 * The driver implements more than five anyway — CR, LF, HOME, cursor moves and scrolling are a
 * line each and the front end has paths this measurement did not reach — and COUNTS anything it
 * does not know (`g_ttUnknownVdu`), because the MOS inventory's history in this project is that
 * it is a floor, never a closed surface (`mos.cpp`).
 *
 * ⚠ VDU 127 was DERIVED FROM THE BYTES, not from a manual: the engine emits `$9D $7F` and the
 * real screen ends up with neither — $9D lands, then 127 backspaces over it, blanks it and
 * leaves the cursor there, so the following $85 overwrites the same cell.  Getting this wrong
 * shifts the whole REVS logo one cell right, which is exactly the kind of off-by-one that looks
 * like a font bug.
 */
/* ⚠ NOT <stdint.h> in the Amiga C++ build — the clash cpu.h documents: the force-included
   framework/SASCCompat.h already brings the types and contradicts the compat header's int8_t.
   Every other build (host C++, the C tools) needs the real header. */
#if !(defined(__cplusplus) && defined(REVS_PLATFORM_AMIGA))
#include <stdint.h>
#endif
#include "teletext_font.h"

#define TT_SCREEN_BASE  0x7C00u   /* the MODE 7 screen: 25 rows of 40, 1 KB */
#define TT_SCREEN_SIZE  0x0400u

/* Teletext colours, in the order the control codes select them.  These are the SAA5050's own
   fully-saturated RGB triples — bit 0 red, bit 1 green, bit 2 blue — so the Amiga palette is a
   direct expansion and there is nothing to choose. */
#define TT_BLACK   0u
#define TT_RED     1u
#define TT_GREEN   2u
#define TT_YELLOW  3u
#define TT_BLUE    4u
#define TT_MAGENTA 5u
#define TT_CYAN    6u
#define TT_WHITE   7u

/* One decoded cell: what the chip would actually put on screen at that position. */
typedef struct {
    unsigned char code;  /* 0..127 — the glyph to draw, hold-mosaic substitution applied */
    unsigned char set;   /* TT_SET_ALPHA / TT_SET_GFX / TT_SET_GFX_SEP */
    unsigned char fg;    /* 0..7 */
    unsigned char bg;    /* 0..7 */
} TtCell;

#ifdef __cplusplus
extern "C" {
#endif

/* ── counters (⚠ every one of these is in amiga/Makefile PROBE_SYMS) ────────────────────── */
extern volatile unsigned long g_ttUnknownVdu;   /* VDU codes the driver has no arm for */
extern volatile unsigned char g_ttLastUnknown;  /* ...the most recent one */
extern volatile unsigned long g_ttVduBytes;     /* bytes through tt_vdu() */
extern volatile unsigned long g_ttModeSwitches; /* VDU 22 calls */
/* ⭐ The port's mode belief vs the GAME'S OWN flag ($64, written only by $16E1 and $4F3B).
   They are derived independently — ours from VDU 22 and the CRTC, the game's from its own
   state — so a disagreement means one of the two is wrong and is worth a loud counter rather
   than a silent wrong screen. */
extern volatile unsigned long g_ttModeDisagree;
extern volatile unsigned char g_ttFlashPhase;   /* the SAA5050 flash phase, advanced per FIELD */
extern volatile unsigned long g_ttFlashToggles; /* ...and how many times it has flipped */
/* ⭐ THE DIRTY-ROW SET: one bit per screen row (bit y = row y), set by every writer of the page
   (tt_poke/tt_cls/tt_scroll here, and the *LOAD bulk copy in trackmenu.c via tt_mark_all_dirty).
   ⚠ It is a HINT, not the record: the game also pokes screen RAM directly ($3A65, $65BA,
   $659A — the menu highlight among them) without marking anything, so the Amiga decode compares
   the page against the bytes it last drew (RevsScreen::decodeTeletext) and ORs this in.  ⚠ Written ONLY from main-loop
   context (the VDU stream and the menu paint both run there, never in the VBI), which is what
   makes the plain read-then-clear in decode race-free.  In PROBE_SYMS. */
extern volatile unsigned long g_ttRowDirty;
/* Mark every row dirty — for a whole-page write that bypasses tt_vdu (the *LOAD of 5TRSCRN) and
   for the mode switch, where the target bitmap may hold a stale front end. */
void tt_mark_all_dirty(void);

/* ── the MOS VDU driver ─────────────────────────────────────────────────────────────────── */
/* Feed it one OSWRCH byte.  Writes screen RAM through mem[] exactly where the MOS would. */
void tt_vdu(unsigned char c);
/* Non-zero while the machine is in MODE 7, i.e. while $7C00-$7FFF is a teletext page and not
   the dashboard code overlay.  Set by VDU 22,7; cleared when hw_init programs the 6845.
   ⭐ INLINE, because the VERTB ISR asks it on every field (applyMode, vbiUpdate) and a cross-TU
   call there is a permanent wall-clock tax (docs/perf-method.md §The VERTB ISR). */
extern int g_ttActive;
static inline int tt_active(void) { return g_ttActive; }
void tt_set_active(int on);
/* Reset the VDU driver's own state (cursor, pending command, flash phase).  Needed by
 * `make validate`: it is pre-state for a differential and it does not live in mem[]. */
void tt_reset_state(void);
/* Cursor, for a backend that wants to draw one (the engine turns it off, so nothing does). */
unsigned tt_cursor_x(void);
unsigned tt_cursor_y(void);

/* ── the SAA5050 ────────────────────────────────────────────────────────────────────────── */
/* Decode one screen row into 40 cells.  `flashOn` picks the phase for flashing text.
   Returns non-zero if the row carried a double-height code, in which case the caller draws the
   TOP halves on this row and the BOTTOM halves on the next, and skips that next row — which is
   what the chip does. */
int  tt_decode_row(const unsigned char* row, TtCell out[TT_COLS], int flashOn);
/* ...and the same decode reporting bit 0 = double height, bit 1 = the row carries a FLASH code
   (the backend's flash-flip set, without a second pass over the row). */
int  tt_decode_row_flags(const unsigned char* row, TtCell out[TT_COLS], int flashOn);
/* ⭐ ...and the form the Amiga painter uses: each cell as one KEY, built arithmetically so byte
   order never shows.  Bits 0-6 code, 7-8 set, 12-13 FREE (the backend puts a double-height half
   there), 16-18 fg, 19-21 bg — so `key & 0x1FF` is the glyph index (set << 7 | code) and
   `key >> 16` the colour index (fg | bg << 3). */
#define TT_KEY(code, set, fg, bg)  ((uint32_t)(code) | (uint32_t)(set) << 7 | \
                                    (uint32_t)(fg) << 16 | (uint32_t)(bg) << 19)
#define TT_KEY_CODE(k)  ((k) & 0x7Fu)
#define TT_KEY_SET(k)   (((k) >> 7) & 3u)
#define TT_KEY_FG(k)    (((k) >> 16) & 7u)
#define TT_KEY_BG(k)    (((k) >> 19) & 7u)
#define TT_KEY_GLYPH(k) ((k) & 0x1FFu)
#define TT_KEY_COLOUR(k) ((k) >> 16)
int  tt_decode_row_keys(const unsigned char* row, uint32_t out[TT_COLS], int flashOn);
/* The flash phase, advanced one display field at a time by the backend's VBI. */
int  tt_flash_phase(void);
void tt_tick_flash(void);

#ifdef __cplusplus
}
#endif

#endif /* TELETEXT_H */
