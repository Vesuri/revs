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

   BBC internal key numbers decode as (row = n>>4, col = n&15), which is how the function
   keys were identified: row 7 is f0..f9, so -114 = f1 and -115 = f2 — and those are
   exactly the two the mode table at $3DE2/$39D4 pairs with "keyboard" and "analogue".
   That agreement is the check on the whole table.
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
   ⚠ WHICH WAY EACH ONE GOES IS STILL [ASSUMED].  $15B5 tests -87 first (setting $76=2) and $15C0
   tests -88 (making it 1 or 3); nothing in that code says which value is left.  L sits to the LEFT
   of ;/+ on the keyboard, so L is assumed to steer left.  One line to flip, and the honest way to
   settle it is to hold each on a real BBC and watch the car's track position — not by feel. */
static const KeyMap kKeys[] = {
    /* --- driving -------------------------------------------------------- */
    { 0xA9, RK_LEFT   },   /* -87  steer left  [ASSUMED direction]            */
    { 0xA8, RK_RIGHT  },   /* -88  steer right [ASSUMED direction]            */
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
    { 0xDD, RK_E      },   /* -35  the sixth menu_key_tbl entry               */
    { 0xB6, RK_RETURN },   /* -74  RETURN                                     */
    { 0xA6, RK_BACKSPC},   /* -90  DELETE                                     */
    { 0xFF, RK_LSHIFT },   /* -1   SHIFT: the prefix for every mode switch    */
    { 0xFF, RK_RSHIFT },
    /* --- SHIFT + function key: the mode table at $3DE2 ------------------ */
    /* BBC f0..f9 are -113..-122; the Amiga's F1..F10 sit one place along, so F1 IS f0. */
    { 0x8F, RK_F1     },   /* -113 f0  ($05F4 = $80 elsewhere; pits)          */
    { 0x8E, RK_F2     },   /* -114 f1  -> $05F5 = $00   KEYBOARD mode         */
    { 0x8D, RK_F3     },   /* -115 f2  -> $05F5 = $C0   ANALOGUE (mouse) mode */
    { 0x8C, RK_F4     },   /* -116 f3  -> $05F8 = $00                         */
    { 0x8B, RK_F5     },   /* -117 f4  -> $05F6 = $40                         */
    { 0x8A, RK_F6     },   /* -118 f5  -> $05F8 = $80                         */
    { 0x86, RK_F10    },   /* -122 f9  -> $05F4 = $80                         */
    { 0x96, RK_HELP   },   /* -106     -> $05F7 = $80  (function not yet named) */
    { 0xEB, RK_F9     },   /* -21      -> $05F6 = $C0  (function not yet named) */
    { 0xE9, RK_ESC    },   /* -23      -> $05F4 = $20  (function not yet named) */
    { 0xDF, RK_F6     },   /* -33      -> $05F4 = $C0  (function not yet named) */
};
#define KEY_COUNT (sizeof(kKeys) / sizeof(kKeys[0]))

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

/* ⚠ PROBE_SYMS (amiga/Makefile): a code the game asks about that this map does not carry.
   Counted rather than answered "not held", because a missing mapping and a wrong mapping
   look identical from the game's side — one of them is silent forever. */
extern "C" {
volatile unsigned long g_keyUnmapped = 0;
volatile unsigned char g_keyUnmappedCode = 0;
volatile unsigned long g_keyEvents = 0;     /* keycodes seen; 0 = the handler never ran */
}

static struct Library*   s_ciaaBase    = 0;
static struct Interrupt  s_kbInterrupt;
static struct Interrupt* s_savedVector = 0;

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

    g_keyDown[raw] = down ? 1u : 0u;
    g_keyEvents++;
    return 0;
}

bool RevsInput::initialize()
{
    m_steer      = 0x80;      /* dead centre */
    m_lastMouseX = (uint8_t)(*joy0datPointer & 0xFFu);
    for (unsigned i = 0; i < 128; i++) g_keyDown[i] = 0;

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
    bool mapped = false;
    for (unsigned i = 0; i < KEY_COUNT; i++)
        if (kKeys[i].bbc == bbcCode) { g_keyDown[kKeys[i].rawkey] = down ? 1u : 0u; mapped = true; }
    if (!mapped) { g_keyUnmappedCode = bbcCode; g_keyUnmapped++; }
    return mapped;
}

bool RevsInput::keyDown(uint8_t x) const
{
    bool mapped = false;
    for (unsigned i = 0; i < KEY_COUNT; i++) {
        if (kKeys[i].bbc != x) continue;
        mapped = true;
        if (g_keyDown[kKeys[i].rawkey]) return true;
    }
    if (!mapped) { g_keyUnmappedCode = x; g_keyUnmapped++; }
    return false;
}

/* ---------------------------------------------------------------------------
   The mouse as the analogue axis.
   --------------------------------------------------------------------------- */
void RevsInput::sampleMouse()
{
    /* JOY0DAT's low byte is the mouse's horizontal counter: 8 bits, free-running, wraps.
       The delta must be taken every frame — a skipped frame loses movement, and a delta
       computed across a wrap is indistinguishable from a fast flick the other way. */
    uint8_t now   = (uint8_t)(*joy0datPointer & 0xFFu);
    int8_t  delta = (int8_t)(now - m_lastMouseX);
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
}

uint16_t RevsInput::axis(uint8_t channel) const
{
    if (channel == 1) {
        /* Channel 1 = steering.  $503F uses ONLY the high byte, biased so $80 is centre
           ($1587 -> $158E).  ⚠ Polarity is [ASSUMED]: increasing = right. */
        return (uint16_t)((unsigned)m_steer << 8);
    }
    if (channel == 2) {
        /* Channel 2 = throttle/brake ($163F -> $1646).  There is no second mouse axis
           worth spending here (pushing a mouse forward to accelerate is nobody's idea of
           a throttle), so the digital keys are projected onto the axis: full deflection
           either side, centre when neither or both are held.  This is what makes ANALOGUE
           mode usable without a joystick. */
        bool up = keyDown(0xAE), dn = keyDown(0xBE);
        if (up == dn) return 0x8000u;
        return up ? 0xFF00u : 0x0000u;
    }
    return 0x8000u;
}

uint8_t RevsInput::buttons() const
{
    /* X=0 reads the fire-button word; only bit 0 is ever looked at ($1691: TXA / AND #1).
       In analogue mode the button IS the gear change, with the throttle axis choosing up
       or down ($1696), so either gear key presses it. */
    return (keyDown(0xEF) || keyDown(0x9F)) ? 0x01u : 0x00u;
}
