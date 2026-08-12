/* AutoRun — see autorun.h for why a scripted keyboard is a measurement prerequisite. */
#include "autorun.h"

/* Negative-INKEY codes, in the raw 256-n form OSBYTE 129 wants in X.  Read out of the
   binary, not from a key-code table: menu_key_tbl ($39E0) holds exactly SPACE/1/2/3, and
   the driving keys are the LDX immediates feeding kbd_test_key ($0E50) at $1660/$166D/
   $16A5/$16AC.  (docs/static-map.md; disasm/symbols.csv menu_key_tbl.) */
enum : uint8_t {
    KEY_NONE  = 0x00,
    KEY_SPACE = 0x9D,   /* -99  menu confirm; in the driving loop, amplify steering */
    KEY_1     = 0xCF,   /* -49  menu option 1 */
    KEY_S     = 0xAE,   /* -82  throttle   ($1660) */
    KEY_A     = 0xBE,   /* -66  brake      ($166D) */
    KEY_TAB   = 0x9F,   /* -97  gear down  ($16A5) */
    KEY_Q     = 0xEF,   /* -17  gear up    ($16AC) */
};

struct AutoStep {
    uint8_t  key;     /* the one key held for this step (KEY_NONE = nothing held) */
    uint16_t polls;   /* how many answered polls the step lasts */
};

/* The front end asks a short series of menu questions — menu_wait_key is called from five
   sites in the $63E0 chain ($63F7 X=2, $6416 X=3, $6426 X=3, $646D X=2, $64E7).  Each one
   wants a number key to SELECT (which sets $0077/$0078) and then SPACE to CONFIRM; SPACE
   alone is ignored until something has been selected ($6591: LDA $77 / BEQ back).
   So the pattern is [1, release, SPACE, release], repeated.
   ⚠ Repeated MORE times than there are menus on purpose.  Which menus a given track and
   mode actually presents is not statically obvious, and a script that runs out early
   parks the run back in a menu spin — the exact failure this class exists to prevent.
   Surplus presses land in the driving loop, where '1' is not a bound key and SPACE only
   amplifies steering. */
#define MENU_ANSWER  {KEY_1, 300}, {KEY_NONE, 150}, {KEY_SPACE, 300}, {KEY_NONE, 150}

static const AutoStep s_script[] = {
    {KEY_NONE, 500},        /* let the title/attract settle before touching anything */
    MENU_ANSWER,
    MENU_ANSWER,
    MENU_ANSWER,
    MENU_ANSWER,
    MENU_ANSWER,
    MENU_ANSWER,
    MENU_ANSWER,
    MENU_ANSWER,
};
static const unsigned S_SCRIPT_LEN = sizeof(s_script) / sizeof(s_script[0]);

bool AutoRun::keyDown(uint8_t x)
{
    m_polls++;

    /* Steady state, past the end of the script: hold the throttle and nothing else.  This
       is the state a measurement window must be in — the car under power on the circuit,
       with the same key set every build. */
    if (m_step >= S_SCRIPT_LEN)
        return x == KEY_S;

    const AutoStep& s = s_script[m_step];
    if (m_polls - m_stepAt >= s.polls) {
        m_step++;
        m_stepAt = m_polls;
        return false;               /* one guaranteed released poll at every boundary */
    }
    return s.key != KEY_NONE && x == s.key;
}
