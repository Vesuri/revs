/* RevsInput — see RevsInput.h for what the game expects and where it was read out. */
#include <proto/exec.h>
#include <exec/interrupts.h>
#include <exec/nodes.h>
#include <resources/cia.h>
#include <proto/cia.h>
#include <hardware/cia.h>

#include "RevsInput.h"
#include "framework/AmigaHardware.h"

/* ---------------------------------------------------------------------------
   THE KEY MAP.
   Every negative-INKEY code the game tests, from a walk of all `LDX #imm / JSR $0E50`
   sites, paired with the Amiga rawkey that should drive it.  Multiple rows may share a
   BBC code (an alternative Amiga key) or an Amiga key (one key, two meanings).

   BBC internal key numbers decode as (row = n>>4, col = n&15) and a negative-INKEY byte is
   255 - internal.  ⚠⚠ THE FUNCTION KEYS ARE NOT CONTIGUOUS — an earlier version of this map
   assumed "row 7 is f0..f9" and every single function-key row was wrong.  f0, f4 and f7 sit
   OUTSIDE row 7 (they share their columns with other keys), so the real codes are
     f0 $DF  f1 $8E  f2 $8D  f3 $8C  f4 $EB  f5 $8B  f6 $8A  f7 $E9  f8 $89  f9 $88,
   with ESCAPE at $8F [0,7] — which is what the old map had mistaken for f0.  Every row below
   is derived by inverting jsbeeb's own key matrix (tools/jsbeeb/src/utils.js `BBC`), and the
   check on the whole table is that $8E and $8D then land on the two entries the mode table at
   $3DE2/$39D4 pairs with "keyboard" and "joystick", which the disc's instructions call f1/f2.
   --------------------------------------------------------------------------- */
struct KeyMap { uint8_t bbc; uint8_t rawkey; };

/* Amiga rawkeys used below (Commodore's raw table, not ASCII). */
#define RK_1        0x01
#define RK_2        0x02
#define RK_3        0x03
#define RK_4        0x04
#define RK_5        0x05
#define RK_6        0x06
#define RK_Q        0x10
#define RK_E        0x12
#define RK_T        0x14
#define RK_A        0x20
#define RK_S        0x21
#define RK_L        0x28
#define RK_SEMI     0x29
#define RK_SPACE    0x40
#define RK_BACKSPC  0x41
#define RK_TAB      0x42
#define RK_RETURN   0x44
#define RK_ESC      0x45
#define RK_DEL      0x46
#define RK_UP       0x4C
#define RK_DOWN     0x4D
#define RK_RIGHT    0x4E
#define RK_LEFT     0x4F
#define RK_F1       0x50
#define RK_F2       0x51
#define RK_F3       0x52
#define RK_F4       0x53
#define RK_F5       0x54
#define RK_F6       0x55
#define RK_F7       0x56
#define RK_F8       0x57
#define RK_F9       0x58
#define RK_F10      0x59
#define RK_LSHIFT   0x60
#define RK_RSHIFT   0x61
#define RK_HELP     0x5F

/* ⭐ WHICH KEYS THEY ARE IS NOW [DERIVED], not assumed: internal key number is (row<<4)|col and a
   negative-INKEY byte is 255 - internal, so -87 = $56 = row 5 col 6 = **L** and -88 = $57 =
   **;/+** — cross-checked against jsbeeb's own key matrix by inverting it, and they are exactly the
   two keys the manual documents for steering ("L/+ steer").  Both are mapped below, on the SAME
   letters, alongside the arrow keys an Amiga player reaches for first.
   ⭐ WHICH WAY EACH ONE GOES IS NOW [DERIVED] AND CONFIRMED ON THE TARGET.  $15B5 tests -87 and
   sets $76=2, $15C0 tests -88 and makes it 1 — and kbd_test_key returns Z SET when the key is
   held, so in both cases the FALL-THROUGH arm is the held arm.  $15E7 then EORs $76 with $62A2
   and ANDs #1, which is what proves $76 bit 0 IS the direction bit: L = 0, ;/+ = 1.  The disc's
   own instructions settle the rest — REVINST prints "L - Steer left" and "+ - Steer right" — so
   direction bit 0 is LEFT, with no assumption left in the chain.  Confirmed by the player driving
   it.  ⚠ Do not read the arrow rows below as independent evidence: they are pinned to these. */
static const KeyMap kKeys[] = {
    /* --- driving -------------------------------------------------------- */
    { 0xA9, RK_LEFT   },   /* -87  steer left  ($76=2, direction bit 0)       */
    { 0xA8, RK_RIGHT  },   /* -88  steer right ($76=1, direction bit 1)       */
    { 0xA9, RK_L      },   /*      ...and the BBC's OWN steering keys, L and  */
    { 0xA8, RK_SEMI   },   /*      ;/+ — [DERIVED], see the note above.  A    */
                           /*      player following the game's own docs was   */
                           /*      pressing keys this map did not carry.      */
    { 0xAE, RK_S      },   /* -82  throttle (BBC 'S')                         */
    { 0xAE, RK_UP     },   /*      ...and the up arrow, which is what a hand  */
    { 0xBE, RK_A      },   /* -66  brake (BBC 'A')                            */
    { 0xBE, RK_DOWN   },   /*      ...reaches for on an Amiga                 */
    { 0x9F, RK_TAB    },   /* -97  gear down (BBC TAB)                        */
    { 0xEF, RK_Q      },   /* -17  gear up (BBC 'Q')                          */
    { 0x9D, RK_SPACE  },   /* -99  SPACE: fine steering in a race, confirm in a menu */
    { 0xDC, RK_T      },   /* -36  starter motor (BBC 'T')                    */
    /* --- menus and text ------------------------------------------------- */
    { 0xCF, RK_1      },   /* -49  '1'                                        */
    { 0xCE, RK_2      },   /* -50  '2'                                        */
    { 0xEE, RK_3      },   /* -18  '3'                                        */
    /* ⭐ '4'..'6' are NOT keys the GAME asks about — they are the port's own circuit menu
       (src/platform/trackmenu.h), which offers five circuits plus Nurburgring.  Kept in this one
       map anyway, so there is still exactly one place where a BBC key code meets an Amiga rawkey.
       Derived like the rest and cross-checked against jsbeeb's key matrix: internal number is
       (row<<4)|col and the negative-INKEY byte is 255 - internal, so '4' [col 2, row 1] = $12 ->
       $ED.  SPACE's $9D falls out of the same arithmetic, which is what checks it. */
    { 0xED, RK_4      },   /* -19  '4'  menu only                             */
    { 0xEC, RK_5      },   /* -20  '5'  menu only                             */
    { 0xCB, RK_6      },   /* -53  '6'  menu only (Nurburgring)               */
    { 0xB6, RK_RETURN },   /* -74  RETURN                                     */
    { 0xA6, RK_BACKSPC},   /* -90  the BBC's DELETE key -> Amiga Backspace.   */
                           /*      ⭐ Amiga Del is RESERVED for BBC BREAK,    */
                           /*      which is a reset line and not a matrix key */
                           /*      (RK_DEL is defined but deliberately unbound) */
    { 0x8F, RK_ESC    },   /* -113 ESCAPE [0,7] -> Amiga Esc.  No engine site */
                           /*      polls it today; bound so the one key whose  */
                           /*      Amiga equivalent is unambiguous is never    */
                           /*      silently missing (g_keyUnmapped)            */
    { 0xFF, RK_LSHIFT },   /* -1   SHIFT: the prefix for every mode switch    */
    { 0xFF, RK_RSHIFT },
    /* --- SHIFT + <key>: shift_key_commands' table at $3DE2/$39D4 --------
       Scanned from index $0B DOWN to 0, so where the same code appears twice the HIGHER index
       wins (f2 at index 3 is the live one; index 2 is unreachable).  Each row's comment is the
       action byte's effect: low nibble = which byte of the state_flags block ($05F4+n), high
       nibble = the value stored.                                                            */
    { 0xDF, RK_F10    },   /* -33   f0 -> $05F4 = $C0  return to the pits (session continues) */
    { 0x8E, RK_F1     },   /* -114  f1 -> $05F5 = $00  KEYBOARD steering                     */
    { 0x8D, RK_F2     },   /* -115  f2 -> $05F5 = $80  JOYSTICK/analogue (the mouse) steering */
    { 0x8C, RK_F3     },   /* -116  f3 -> $05F8 = $00  computer-assisted steering OFF        */
    { 0xEB, RK_F4     },   /* -21   f4 -> $05F6 = $C0  volume down                           */
    { 0x8B, RK_F5     },   /* -117  f5 -> $05F6 = $40  volume up                             */
    { 0x8A, RK_F6     },   /* -118  f6 -> $05F8 = $80  computer-assisted steering ON         */
    { 0xE9, RK_F7     },   /* -23   f7 -> $05F4 = $20  (effect not yet named)                */
    { 0x96, RK_HELP   },   /* -106  COPY [9,6] -> Amiga Help: $05F7 = $80  PAUSE             */
                           /*       (DELETE, mapped above, is the key that RESUMES)          */
    /* ⭐ THE WAY OUT OF A SESSION: SHIFT + right-arrow writes $80 to state_flags, so
       race_main_loop exits with bit 6 CLEAR and enter_session ($655C) leaves through
       abort_to_front_end ($3273) to the menu.  abort_if_quit_keys ($3261) polls the same pair
       directly from the "press SPACE" prompts, so it works there too.
       ⚠ The right arrow is ALSO this port's steer-right key (above).  On the BBC it was only
       ever the abort key; here the two overlap, and the abort wins — which is the harmless
       order, since you are leaving the race either way.                                     */
    { 0x86, RK_RIGHT  },   /* -122  RIGHT [9,7] -> $05F4 = $80  ABORT to the front end       */
};
#define KEY_COUNT (sizeof(kKeys) / sizeof(kKeys[0]))

/* ⭐ THE REVERSE MAP: BBC negative-INKEY code -> its (at most two) Amiga rawkeys, $FF = none.
   keyDown() is asked seven times a frame and used to scan all of kKeys each time — and without
   stopping at the first match, because a code may carry two rawkeys (A9 = left-arrow and L).
   Built once from kKeys, so kKeys stays the single place a BBC key meets an Amiga rawkey.
   ⚠ Two slots is the table's WIDEST row today; buildReverseMap() counts overflow into
   g_keyMapOverflow, which must read 0 — a third rawkey on one code would otherwise be silently
   dropped and that key would simply stop working. */
enum { RK_NONE = 0xFF };
static uint8_t s_rk[256][2];
static bool    s_rkBuilt = false;
extern "C" { volatile uint8_t g_keyMapOverflow = 0; }

static void buildReverseMap()
{
    for (unsigned c = 0; c < 256; c++) { s_rk[c][0] = RK_NONE; s_rk[c][1] = RK_NONE; }
    for (unsigned i = 0; i < KEY_COUNT; i++) {
        uint8_t* r = s_rk[kKeys[i].bbc];
        if      (r[0] == RK_NONE) r[0] = kKeys[i].rawkey;
        else if (r[1] == RK_NONE) r[1] = kKeys[i].rawkey;
        else g_keyMapOverflow++;
    }
    s_rkBuilt = true;
}

static inline const uint8_t* rawkeysFor(uint8_t bbc)
{
    if (!s_rkBuilt) buildReverseMap();
    return s_rk[bbc];
}

/* ---------------------------------------------------------------------------
   Key state, driven by the CIA-A serial-port interrupt.
   --------------------------------------------------------------------------- */
/* ⭐ Indexed by Amiga rawkey, 1 = held.  extern "C" and in PROBE_SYMS rather than a file
   static, for two reasons: gdb cannot address a file static by name from a script, and
   POKING this array is how the keyboard path gets tested on a headless target — a run with
   no keyboard can still prove keyDown() -> OSBYTE 129 -> the game's own menu logic
   (amiga/keytest.gdb). */
extern "C" volatile uint8_t g_keyDown[128];
volatile uint8_t g_keyDown[128];

/* ⭐⭐ THE TAP LATCH — "a quick tap does not register", and the cause is the port's POLL RATE.
 *
 * OSBYTE 129 asks whether a key is held AT THIS INSTANT, so a level poll can only ever miss a
 * press that began and ended between two polls — and the front end polls rarely.  Measured on the
 * target (amiga/spacerate.gdb): ~27 SPACE polls a SECOND, one per ~37 ms of game time, because
 * every iteration of a front-end loop goes through renderFrame(), which decodes the page and then
 * waits for the next PAL field.  The BBC's own menu loop ($6577) does neither — it polls at 6502
 * speed, hundreds of times in the same 37 ms — so a tap it would have caught is one this port
 * drops on the floor.  That gap is ENTIRELY the port's: the frame wait is a hook this project put
 * there (docs/amiga-arch.md), not something the 6502 did.
 *
 * So the fix belongs at the seam, and it is to stop throwing away what the hardware already told
 * us.  The CIA-A handler sees every real key EDGE, timestamped by field; a key whose down-edge
 * landed within the last few fields is reported held ONCE even if the finger has already come off.
 * That is what a fast poll would have answered, which is why it is a fidelity fix and not a
 * convenience: it makes the port's answer match the machine it is porting.
 *
 * ⚠ THREE BOUNDS, and each one is load-bearing:
 *   • Only while the MODE 7 page is up (g_screenMode7).  In the RACE both machines poll once per
 *     frame — the 6502 read the keyboard from its own 50 Hz body — so there is no gap to close and
 *     a latch there would invent input the BBC never saw.
 *   • Consumed on the answer, so one tap is one answer.  A latch that stayed set would re-answer
 *     the same tap on the next poll, and menu_wait_key polls in a loop: one tap would walk two
 *     rows.
 *   • Expired after KEY_LATCH_FIELDS.  A tap nobody polls for must not surface half a second later
 *     as a phantom press on whatever page happens to be up by then.
 * The live level is still checked FIRST and answered without touching the latch, so a HELD key
 * behaves exactly as before — this only ever adds an answer the slow poll had lost.
 */
extern "C" volatile uint8_t  g_keyLatch[128];
volatile uint8_t  g_keyLatch[128];
extern "C" volatile uint16_t g_keyLatchAt[128];
volatile uint16_t g_keyLatchAt[128];
/* Taps the latch rescued.  ⭐ It is the instrument for the fix, and it doubles as the sabotage
   check: with the latch working this climbs while tapping the menus, and a build where it stays 0
   through a front-end walk has the fix compiled out or gated off. */
extern "C" volatile unsigned long g_keyLatchHits;
volatile unsigned long g_keyLatchHits = 0;
#define KEY_LATCH_FIELDS 4u        /* ~80 ms — one poll interval plus margin, no more */

extern "C" volatile uint16_t g_vbiCount;
extern "C" volatile uint16_t g_screenMode7;

/* ⚠ PROBE_SYMS (amiga/Makefile): a code the game asks about that this map does not carry.
   Counted rather than answered "not held", because a missing mapping and a wrong mapping
   look identical from the game's side — one of them is silent forever. */
extern "C" {
volatile unsigned long g_keyUnmapped = 0;
volatile unsigned char g_keyUnmappedCode = 0;
volatile unsigned long g_keyEvents = 0;     /* keycodes seen; 0 = the handler never ran */

/* ⭐⭐ THE DOUBLE-PRESS INSTRUMENT (docs/controls.md §The double-press).  A menu needing SPACE
   pressed twice has exactly two possible shapes, and these three counters tell them apart in
   one run with a finger on the key:
     g_spaceEdges     — rising edges of the SPACE rawkey, i.e. how many times it was PHYSICALLY
                        pressed.  Counted in the CIA handler, so it is the ground truth.
     g_spaceAnswered  — how many polls of BBC code $9D this backend answered "held".
     g_spacePolls     — how many times the game ASKED about $9D at all.
   Two edges to advance one page with g_spaceAnswered > 0 on the first one means the port SAW the
   press and the engine's own logic discarded it — the masked-by-a-number-key scan or the
   up-then-down debounce, both faithful, both then a TIMING question at the seam.  Two edges with
   nothing answered on the first means the input layer lost it, which is a port bug outright.
   ⚠ The distinction is the whole diagnosis; do not fix either shape before reading these. */
volatile unsigned long g_spaceEdges    = 0;
volatile unsigned long g_spaceAnswered = 0;
volatile unsigned long g_spacePolls    = 0;

/* ⭐ PROBE_SYMS: the three mouse buttons as a bitmask (1 = left, 2 = right, 4 = middle),
   sampled every VBI, plus a sticky OR of everything ever seen.  This exists because the
   POTINP failure mode is SILENT and inverted: get the POTGO setup wrong and right/middle
   read as permanently HELD, which is a stuck throttle and a stuck brake rather than a dead
   control.  A headless run cannot press a button, but it CAN prove the idle state reads 0 —
   which is the half of the contract that breaks silently (amiga/mousebtn.gdb). */
volatile unsigned char g_mouseBtnMask = 0;
volatile unsigned char g_mouseBtnSeen = 0;
}

static struct Library*   s_ciaaBase    = 0;
static struct Interrupt  s_kbInterrupt;
static struct Interrupt* s_savedVector = 0;

/* ===========================================================================
   THE TYPING QUEUE — OSRDCH's characters                            (rdch)
   ---------------------------------------------------------------------------
   keyDown() answers "is this key held NOW", which is all the game's own control
   polling ever asks.  console_io ($6300) needs the other question: what did the
   player TYPE, in order, including the ones pressed and released between two of
   its OSRDCH calls.  Without this, Platform::rdch()'s default CR ended every
   field instantly — the two wing prompts ($3C50) and the twelve-character driver
   name ($66D4) all answered themselves, and the wing page degenerated into a
   screen that only wanted SPACE.

   So the handler pushes a CHARACTER, not a level, on every down edge, and rdch()
   pops one.  A ring rather than a single byte because the queue must survive a
   burst: the front end polls ~27 times a second (docs/controls.md §Fast taps) and
   a fast typist beats that easily.  Sixteen is far more than console_io's widest
   field (12) and costs nothing.

   ⚠ The ISR is the only writer of s_head and rdch() the only writer of s_tail, so
   a byte each and no disabling of interrupts is needed; a full ring drops the
   newest character, which is what the BBC's own buffer does. */
#define TYPE_QUEUE 16u
static volatile uint8_t  s_typed[TYPE_QUEUE];
static volatile uint8_t  s_typeHead = 0;   /* written by the ISR only */
static volatile uint8_t  s_typeTail = 0;   /* written by rdch() only  */

/* Amiga rawkey -> the ASCII the BBC's line editor expects, UNSHIFTED.  Uppercase
   because every field the game reads is displayed uppercase (MODE 7 has no
   lowercase in the game's own font selection) and console_io stores the byte it
   echoes.  0 = "not a typing key", which is every code the table leaves out. */
static const uint8_t kAscii[0x40] = {
/* 00 */ '`', '1','2','3','4','5','6','7','8','9','0', '-','=','\\', 0,  0,
/* 10 */ 'Q','W','E','R','T','Y','U','I','O','P', '[',']', 0,  0,  0,  0,
/* 20 */ 'A','S','D','F','G','H','J','K','L', ';','\'', 0,  0,  0,  0,  0,
/* 30 */  0, 'Z','X','C','V','B','N','M', ',','.','/',  0,  0,  0,  0,  0,
};

static void typeQueuePush(uint8_t raw)
{
    uint8_t ch = 0;
    if (raw < 0x40u)             ch = kAscii[raw];
    else if (raw == RK_SPACE)    ch = 0x20u;
    else if (raw == RK_RETURN)   ch = 0x0Du;
    else if (raw == RK_BACKSPC)  ch = 0x7Fu;   /* the BBC's DELETE — console_io's rub-out */
    else if (raw == RK_DEL)      ch = 0x7Fu;
    if (!ch) return;

    uint8_t next = (uint8_t)((s_typeHead + 1u) % TYPE_QUEUE);
    if (next == s_typeTail) return;            /* full: drop the newest, as the BBC does */
    s_typed[s_typeHead] = ch;
    s_typeHead = next;
}

bool RevsInput::anyKeyDown() const
{
    /* Skip the modifiers: SHIFT/CTRL/ALT/AMIGA are held while chording (CTRL + left button is
       quit) and resting on one must not count as hurrying the title page. */
    for (unsigned raw = 0; raw < 96u; raw++)
        if (g_keyDown[raw]) return true;
    return false;
}

uint8_t RevsInput::typedChar()
{
    if (s_typeTail == s_typeHead) return 0;
    uint8_t ch = s_typed[s_typeTail];
    s_typeTail = (uint8_t)((s_typeTail + 1u) % TYPE_QUEUE);
    return ch;
}

void RevsInput::flushTyped()
{
    s_typeTail = s_typeHead;
}


static uint32_t keyboardHandler()
{
    uint8_t sdr = *ciaasdrPointer;

    /* Acknowledge: pulse SP to output mode (drives KDAT low) then back, so the keyboard
       releases the next code.  HRM Appendix G requires >= 85 us.  ⚠ This spin runs inside
       the ISR, so it is deliberately short: the Atari port measured a 2 ms handshake
       preempting the main loop for whole frames while a key was held.  ~200 iterations is
       ~270 us — a 3x margin, at a seventh of the cost. */
    *ciaacraPointer |= CIACRAF_SPMODE;
    for (volatile uint16_t d = 0; d < 200; d++) { }
    *ciaacraPointer &= (uint8_t)~CIACRAF_SPMODE;

    /* Wire protocol: the code arrives ROL'd by one and KDAT is active low, so SDR holds
       ~(code ROL 1).  Invert, ROR 1; bit 7 of the result is the key-UP flag. */
    uint8_t code = (uint8_t)~sdr;
    code = (uint8_t)((code >> 1) | (code << 7));
    uint8_t raw  = (uint8_t)(code & 0x7Fu);
    bool    down = (code & 0x80u) == 0u;

    if (down && !g_keyDown[raw]) {                  /* a real press — latch the EDGE */
        if (raw == RK_SPACE) g_spaceEdges++;
        g_keyLatch[raw]   = 1u;
        g_keyLatchAt[raw] = g_vbiCount;
        typeQueuePush(raw);                         /* ...and as a TYPED character */
    }
    g_keyDown[raw] = down ? 1u : 0u;
    g_keyEvents++;
    return 0;
}

bool RevsInput::initialize()
{
    m_steer      = 0x80;      /* dead centre */
    m_lastMouseX = (uint8_t)(*joy0datPointer & 0xFFu);
    for (unsigned i = 0; i < 128; i++) { g_keyDown[i] = 0; g_keyLatch[i] = 0; }

    /* ⚠ Make the pot pins INPUTS so POTINP reports the right and middle buttons.  Clearing
       POTGO's four OUT* enables is the whole requirement; the START bit is for the paddle
       counters, which nothing here uses.  Without this both buttons read as held. */
    *potgoPointer = 0x0000u;

    s_ciaaBase = (struct Library*)OpenResource((CONST_STRPTR)CIAANAME);
    if (!s_ciaaBase) return false;

    s_kbInterrupt.is_Node.ln_Type = NT_INTERRUPT;
    s_kbInterrupt.is_Node.ln_Pri  = 0;
    s_kbInterrupt.is_Node.ln_Name = (char*)"Revs KB";
    s_kbInterrupt.is_Data = 0;
    s_kbInterrupt.is_Code = (void(*)())keyboardHandler;

    /* AddICRVector returns NULL on success, or the installed (keyboard.device) vector on
       conflict.  Steal it and remember it, so the OS keyboard works again after we exit. */
    s_savedVector = AddICRVector(s_ciaaBase, CIAICRB_SP, &s_kbInterrupt);
    if (s_savedVector) {
        RemICRVector(s_ciaaBase, CIAICRB_SP, s_savedVector);
        AddICRVector(s_ciaaBase, CIAICRB_SP, &s_kbInterrupt);
    }
    return true;
}

void RevsInput::shutdown()
{
    if (!s_ciaaBase) return;
    RemICRVector(s_ciaaBase, CIAICRB_SP, &s_kbInterrupt);
    if (s_savedVector) {
        AddICRVector(s_ciaaBase, CIAICRB_SP, s_savedVector);
        s_savedVector = 0;
    }
    s_ciaaBase = 0;
}

bool RevsInput::pressBbcKey(uint8_t bbcCode, bool down)
{
    const uint8_t* r = rawkeysFor(bbcCode);
    if (r[0] == RK_NONE) { g_keyUnmappedCode = bbcCode; g_keyUnmapped++; return false; }
    g_keyDown[r[0]] = down ? 1u : 0u;
    if (r[1] != RK_NONE) g_keyDown[r[1]] = down ? 1u : 0u;
    return true;
}

void RevsInput::releaseAllKeys()
{
    /* The whole rawkey array, not just the mapped codes: this is "nothing is held", and
       leaving an unmapped rawkey set would be the same bug with a different key. */
    for (unsigned i = 0; i < 128; i++) { g_keyDown[i] = 0; g_keyLatch[i] = 0; }
}

bool RevsInput::keyDown(uint8_t x) const
{
    const uint8_t* r = rawkeysFor(x);
    if (x == 0x9Du) g_spacePolls++;                     /* SPACE — the instrument above */
    if (r[0] == RK_NONE) { g_keyUnmappedCode = x; g_keyUnmapped++; return false; }

    /* The live level first: a key that is down now needs no latch, and the answer must not
       consume one (see the tap-latch note above — a held key would otherwise eat its own edge). */
    for (unsigned k = 0; k < 2u && r[k] != RK_NONE; k++) {
        if (g_keyDown[r[k]]) {
            g_keyLatch[r[k]] = 0u;                      /* seen while held — the edge is spent */
            if (x == 0x9Du) g_spaceAnswered++;
            return true;
        }
    }

    /* ...then the tap this port's slow front-end poll would otherwise have lost.
       ⚠ `make NOLATCH=1` compiles this arm out — that is the SABOTAGE arm for the proof below,
       and the only thing that separates "the latch works" from "the taps were never short". */
#ifndef REVS_NOLATCH
    if (g_screenMode7) {
        for (unsigned k = 0; k < 2u && r[k] != RK_NONE; k++) {
            const uint8_t rk = r[k];
            if (!g_keyLatch[rk]) continue;
            g_keyLatch[rk] = 0u;                        /* one tap, one answer */
            if ((uint16_t)(g_vbiCount - g_keyLatchAt[rk]) > KEY_LATCH_FIELDS)
                continue;                               /* stale — not this page's press */
            g_keyLatchHits++;
            if (x == 0x9Du) g_spaceAnswered++;
            return true;
        }
    }
#endif
    return false;
}

/* ---------------------------------------------------------------------------
   The mouse as the analogue axis.
   --------------------------------------------------------------------------- */
/* ⭐ WHICH WAY THE COUNTER RUNS IS [MEASURED], AND IT IS NOT WHAT THE ALGEBRA PREDICTED.
   Every step from the mouse to the wheel is derivable except this one, and the derivation
   said "counter up = right", so that is what shipped — and it steered backwards on the
   target.  The whole rest of the chain is now confirmed correct, in both directions:

     - the manual on the disc is explicit ("L - Steer left" / "+ - Steer right"), and
     - $15B3 gives L $76=2 (direction bit 0) and ;/+ $76=1 (direction bit 1) — kbd_test_key
       returns Z SET when held, so the fall-through arm is the HELD arm, and
     - $15E7 EORs $76 with $62A2 and ANDs #1, so $76 bit 0 IS $62A2's direction bit, and
     - adc_read ($5044) does TYA / LDX #1 / ADC #$80 / BPL, so ADC high byte >= $80 leaves
       X = 1 and below it X = 0, and $15A7's TXA / ORA $74 makes that same direction bit
       (mul8 at $0C02 is fully unrolled and never touches X, so it survives the call), and
     - the player confirms L and ;/+ steer the documented way on the target.

   So "ADC high byte below $80" is genuinely LEFT, and the only link left to be wrong was
   this one: the horizontal counter does not move the way assumed here.  Negating the delta
   is therefore the fix, and it is the ONE line that carries the empirical sign — do not
   "correct" it back from a hardware manual without moving a mouse and watching the wheel. */
#define MOUSE_X_SIGN (-1)

void RevsInput::sampleMouse()
{
    /* JOY0DAT's low byte is the mouse's horizontal counter: 8 bits, free-running, wraps.
       The delta must be taken every frame — a skipped frame loses movement, and a delta
       computed across a wrap is indistinguishable from a fast flick the other way. */
    uint8_t now   = (uint8_t)(*joy0datPointer & 0xFFu);
    int8_t  delta = (int8_t)(MOUSE_X_SIGN * (int)(int8_t)(now - m_lastMouseX));
    m_lastMouseX  = now;

    /* ⚠ SENSITIVITY IS A FEEL DECISION AND IT IS PROVISIONAL.  The game's own response
       curve is real logic ($503F biases the reading to $80 = centre and hands the caller a
       magnitude plus a direction; SPACE quarters it), so this only has to deliver a
       faithful axis — one mouse count per ADC step, clamped, self-centring not applied
       because a real analogue wheel does not spring back either. */
    int pos = (int)m_steer + (int)delta;
    if (pos < 0)   pos = 0;
    if (pos > 255) pos = 255;
    m_steer = (uint8_t)pos;

    /* ⭐ ONE POTINP read, not two.  mouseButton(1) and mouseButton(2) both read the same
       register; this runs in the VERTB ISR, which is a fixed 50 Hz tax on wall clock rather than
       a per-frame cost (docs/perf-method.md §the VERTB ISR), so a duplicated chip read here is
       paid forever.  Both pins are active LOW, exactly as mouseButton() reads them. */
    /* ⭐ Branch-free: shift each pin onto its mask bit and invert once — 1 = CIA-A PRA bit 6,
       2 = POTINP bit 10, 4 = POTINP bit 8.  Equal to the per-pin `== 0 ? bit : 0` form over all
       2^24 (PRA, POTINP) inputs, at about half the instructions in a 50 Hz ISR. */
    const unsigned pot = *potinpPointer;
    const unsigned cia = *ciaapraPointer;
    unsigned char b = (unsigned char)(~(((cia >> 6) & 1u) | ((pot >> 9) & 2u) | ((pot >> 6) & 4u)) & 7u);
    g_mouseBtnMask  = b;
    g_mouseBtnSeen |= b;
}

uint16_t RevsInput::axis(uint8_t channel) const
{
    if (channel == 1) {
        /* Channel 1 = steering.  $503F uses ONLY the high byte, biased so $80 is centre
           ($1587 -> $158E).  ⭐ Polarity is [DERIVED]: high byte >= $80 leaves adc_read's
           X = 1, which is ;/+ = RIGHT.  The mouse's own sign lives in MOUSE_X_SIGN. */
        return (uint16_t)((unsigned)m_steer << 8);
    }
    if (channel == 2) {
        /* Channel 2 = throttle/brake ($163F -> $1646).  There is no second mouse axis
           worth spending here (pushing a mouse forward to accelerate is nobody's idea of
           a throttle), so full deflection either side, centre when neither or both are
           held.  This is what makes ANALOGUE mode usable without a joystick.
           ⭐ THE PEDALS ARE THE MOUSE BUTTONS (user decision) — right accelerates, left
           brakes — which is a DELIBERATE DIVERGENCE: the BBC's analogue mode has exactly
           one button and spends it on the gearbox ($168E -> $1696), so there is no
           precedent to be faithful to here.  The S/A keys stay live alongside them, so
           nothing that worked before this stops working. */
        bool up = keyDown(0xAE) || mouseButton(1);
        bool dn = keyDown(0xBE) || mouseButton(0);
        if (up == dn) return 0x8000u;
        return up ? 0xFF00u : 0x0000u;
    }
    return 0x8000u;
}

uint8_t RevsInput::buttons() const
{
    /* X=0 reads the fire-button word; only bit 0 is ever looked at ($1691: TXA / AND #1).
       ⭐ In analogue mode this button IS the whole gearbox, and the player does NOT choose
       the direction: $1696 reads $3E/$3F and picks up or down itself.  That is the one
       faithful button meaning, so it goes on the MIDDLE button — the two outer ones are
       the pedals above.  The gear keys press it too, exactly as before. */
    return (mouseButton(2) || keyDown(0xEF) || keyDown(0x9F)) ? 0x01u : 0x00u;
}

bool RevsInput::mouseButton(uint8_t which) const
{
    /* Left is a CIA-A parallel-port bit; right and middle are the pot pins of port 0 read
       back through POTINP — bit 10 (DATLY) is right, bit 8 (DATLX) is middle.  All three
       are active LOW. */
    if (which == 0) return (*ciaapraPointer & 0x40u) == 0u;
    uint16_t p = *potinpPointer;
    if (which == 1) return (p & 0x0400u) == 0u;
    if (which == 2) return (p & 0x0100u) == 0u;
    return false;
}

#ifdef REVS_TAPTEST
/* ================================================================================================
 * ⭐⭐ THE PROOF THAT THE TAP LATCH WORKS, and it had to be built INSIDE the program.
 *
 * ⚠⚠ The obvious instrument does not exist: gdb cannot write this target's memory through the
 * FS-UAE stub.  `set var g_keyLatchHits = 12345` reads back 0, and so does a write to
 * g_keyDown[] — measured, after a whole harness had been written on the assumption that it could
 * (the harness reported "no key press registered" for a run in which no key was ever pressed,
 * which is the failure shape docs/method-lessons.md keeps warning about).  So a synthetic press
 * must come from the program's own side of the seam.
 *
 * WHAT THIS INJECTS IS A TAP OF NO LENGTH AT ALL: it writes only what the CIA handler's down-edge
 * branch writes — the latch and its field stamp — and NEVER touches g_keyDown.  So the level poll
 * can never see it, and any progress through the front end is the latch's work and nothing else.
 * That is what makes it a proof rather than a demonstration:
 *   make clean && make PROBES=1 NOAUTORUN=1 TAPTEST=1   -> the circuit menu reaches TM_FINISHED
 *                                                          (g_tmPhase 3) and g_keyLatchHits climbs
 *   ...the same plus NOLATCH=1                          -> it must NOT get past TM_SELECT and
 *                                                          g_keyLatchHits must stay 0
 * A run where both arms behave the same is measuring something else — read
 * docs/controls.md §The double-press before believing either.
 * ============================================================================================== */
extern "C" volatile unsigned long g_tapInjected;
volatile unsigned long g_tapInjected = 0;

/* ⭐ It ALTERNATES '1' and SPACE forever rather than replaying a scripted chain, and that is
   deliberate: every page in this front end wants either a row (1 is always a valid row — the first
   circuit, PRACTICE, the shortest race) or a SPACE, so an alternating pair answers all of them
   without the script having to know which page is up.  A fixed chain is what autorun.cpp does, and
   getting it wrong there cost three runs; this proof only has to keep the front end MOVING. */
#define TAP_PERIOD_FIELDS 15u      /* > one front-end iteration, so each tap lands on its own page */

extern "C" void revs_input_tap_test(void)
{
    static uint16_t s_lastTap = 0;
    static unsigned s_next    = 0;
    if (!g_screenMode7) return;                       /* the front end is the whole point */
    if ((uint16_t)(g_vbiCount - s_lastTap) < TAP_PERIOD_FIELDS) return;
    s_lastTap = g_vbiCount;

    /* '1', '1', RETURN, SPACE.  One cycle answers a MENU (a row key, then SPACE) and also
       fills a two-digit wing FIELD (two characters, then RETURN), so one script drives both
       kinds of front-end page and the run reaches the race through the pits. */
    static const uint8_t kSeq[4] = { RK_1, RK_1, RK_RETURN, RK_SPACE };
    const uint8_t raw = kSeq[s_next & 3u];
    s_next++;
    /* EXACTLY the CIA handler's down-edge branch, and deliberately nothing else. */
    g_keyLatch[raw]   = 1u;
    g_keyLatchAt[raw] = g_vbiCount;
    typeQueuePush(raw);              /* ...and its TYPED half, which is what a field reads */
    g_tapInjected++;
}
#endif
