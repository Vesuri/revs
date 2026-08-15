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
    KEY_2     = 0xCE,   /* -50  menu option 2 — menu_key_tbl[2].  Derived the same way as
                           KEY_1 and cross-checked on a real BBC: the refloop reports the
                           engine polling [col 1, row 3] for option 2, internal key number
                           (3<<4)|1 = 49, and a negative INKEY byte is 255 - internal. */
    KEY_S     = 0xAE,   /* -82  throttle   ($1660) */
    KEY_A     = 0xBE,   /* -66  brake      ($166D) */
    KEY_TAB   = 0x9F,   /* -97  gear down  ($16A5) */
    KEY_Q     = 0xEF,   /* -17  gear up    ($16AC) */
    KEY_T     = 0xDC,   /* -36  starter motor ($497A) — BBC 'T' */
};

/* A step holds ONE key and ends when EITHER limit is reached; 0 disables that limit.
     polls  — after that many answered polls, whatever was asked.  Wall-clock-free but
              blind: the step can expire before the game ever asks for its key.
     hits   — as soon as the key has been answered HELD that many times.  Reacts to the
              game instead of guessing at it, which is what makes the straight-to-race
              script take three frames instead of a thousand.
   Set both and `polls` is the escape hatch: a hit-counted step whose key the game stops
   asking for would otherwise wedge the script forever, which is a silent hang rather
   than a visible one.  A release step must use `polls` — there is no key to count. */
struct AutoStep {
    uint8_t  key;
    uint16_t polls;
    uint16_t hits;
};

/* Does the script hold the throttle forever once it runs out, or hand the keyboard back?
   A measurement window must be a fixed key set every build (perf-method.md §Rule 2), so
   the measurement builds hold; a straight-to-race build exists so a HUMAN can drive. */
/* REVS_HOLD_THROTTLE forces it on for a build that is neither: the host build needs it to
   reproduce a MOVING car, and without it a host/target comparison silently compares a moving
   Amiga against a parked host — two different scenes, which is how "the host frame buffer is
   clean" nearly became "the bug is Amiga-only". */
#if defined(REVS_FPSCOUNT) || defined(REVS_PROBE) || defined(REVS_HOLD_THROTTLE)
  #define AUTORUN_HOLD_THROTTLE 1
#else
  #define AUTORUN_HOLD_THROTTLE 0
#endif

#if defined(REVS_COMPETITION)

/* ⭐ STRAIGHT TO A COMPETITION RACE — the session that has a FIELD OF CARS in it.
 *
 * Why this exists as its own script rather than a flag on the one below: PRACTICE runs the
 * player alone on the circuit, so it is structurally incapable of showing whether competitor-car
 * rendering works.  A clean practice frame is not evidence about the other nineteen cars, and
 * every measurement this port has taken so far was a practice frame.
 *
 * The COMPETITION branch is the long arm of front_end_menus ($640A onwards).  Driven on a real
 * BBC by `make refloop-comp`, it asks, in order:
 *
 *     $63F7  X=2   1 PRACTICE / 2 COMPETITION            -> 2
 *     $6416  X=3   SELECT THE CLASS OF RACE              -> 1  (Novice)
 *     $6426  X=3   DURATION OF QUALIFYING LAPS           -> 1  (5 mins)
 *            —     ENTER NAME OF DRIVER (console_io)     -> no key: see below
 *            —     WING SETTINGS, rear then front        -> no key: see below
 *
 * ⚠ THE TWO LINE-EDITOR PROMPTS NEED NO KEYS HERE, and that is a property of the PORT, not of
 * the game: both read through OSRDCH, and Platform::rdch() returns CR — an immediate
 * end-of-line.  The name comes out empty and the wings take the validator's default.  On the
 * real BBC those same prompts had to be typed, which is why the reference loop grew answerName()
 * and answerNumber() and this script did not.  If rdch() ever starts returning real characters,
 * this script wedges at the name prompt and that is the first place to look.
 *
 * The result is $5F3B = $4 rather than practice's $FF — measured on a real BBC, and the cheapest
 * single check that this script took the branch it thinks it did.
 */
static const AutoStep s_script[] = {
    {KEY_2,     600, 2}, /* 1 PRACTICE / 2 COMPETITION -> COMPETITION                    */
    {KEY_NONE,  4,   0}, /* release: menu_wait_key scans high-to-low and stops at the
                            first key HELD, so a held '2' masks everything below it      */
    {KEY_SPACE, 600, 2},
    {KEY_NONE,  4,   0},
    {KEY_1,     600, 2}, /* SELECT THE CLASS OF RACE -> Novice                           */
    {KEY_NONE,  4,   0},
    {KEY_SPACE, 600, 2},
    {KEY_NONE,  4,   0},
    {KEY_1,     600, 2}, /* DURATION OF QUALIFYING LAPS -> 5 mins                        */
    {KEY_NONE,  4,   0},
    {KEY_SPACE, 600, 2},
    {KEY_NONE,  4,   0},
    /* ...name and wings answer themselves through rdch().  Then the pits page. */
    {KEY_SPACE, 900, 2},
    {KEY_NONE,  4,   0},
    {KEY_T,     400, 1}, /* starter — one hit is the whole job ($4978 stops polling)     */
    {KEY_NONE,  2,   0},
    {KEY_Q,     400, 1}, /* first gear, or the "race" is a parked car                    */
    {KEY_NONE,  2,   0},
};

#elif defined(REVS_STRAIGHT_TO_RACE)

/* ⭐ STRAIGHT TO RACE — and it skips NO game code at all.
 *
 * front_end_menus ($63E0) asks its first question with X=2 at $63F7: `1 PRACTICE
 * 2 COMPETITION`, drawn by text script $27 ($3880).  menu_wait_key returns X = $0078-1,
 * so option 1 comes back as X=0, and $63FA `CPX #1 / BCS $640A` falls through to:
 *
 *     63FE  STX $6F        player count 0
 *     6400  DEX
 *     6401  STX $5F3B      = $FF  — the practice flag ($1768 `LDA $5F3B / BMI` loops the
 *                                   body back instead of running a session state machine)
 *     6404  JSR $42EC
 *     6407  JSR $655A      the practice session: JSR $3C50 / JSR $16DC until $05F4 says stop
 *
 * So PRACTICE needs exactly ONE menu answer and nothing else — no class, no qualifying
 * duration, no name entry, no ANOTHER/START loop.  Those live on the COMPETITION side
 * ($640A onwards) and on the pass front_end_menus makes AFTER a practice session ends.
 * ⚠ Which is why this is a scripted answer and not a seeded jump: the game itself still
 * runs $4D4D/$41D0/$3A50/$42EC and every derived value, so there is no state for this
 * build to get wrong.
 *
 * Then the starter.  $49CE (from $46A1, the player-car update, every frame) does
 * `LDA $61 / BEQ $4978`, and $4978 polls -36 = 'T'; a hit sets $09=7 and $61=$FF, which
 * is the engine running.  Held by HITS, not by poll count, because the poll only happens
 * once per body frame and a body frame is over a second on an A500 right now.
 */
static const AutoStep s_script[] = {
    {KEY_1,     600, 2}, /* select PRACTICE the first time the menu asks for '1'        */
    {KEY_NONE,  4,   0}, /* release — menu_wait_key scans '2','1',SPACE and stops at the
                            first key HELD, so it never reaches SPACE while '1' is down  */
    {KEY_SPACE, 600, 2}, /* confirm ($6591: SPACE is ignored until $77 is set)          */
    {KEY_NONE,  4, 0},
    /* $655A's loop opens with JSR $3C50 — the WING SETTINGS screen ($3C02, text $18/$19,
       two JSR $3EE0 line reads) and then $34D0, `SPACE BAR TO CONTINUE`.  The line reads
       come back through OSRDCH, not the keyboard, so they need no answer here; the SPACE
       does.  ⚠ $34D0 waits for SPACE to be RELEASED first ($34D9) and only then for a
       press ($34E0), which is exactly why every step here has a release after it. */
    {KEY_SPACE, 600, 2},
    {KEY_NONE,  4,   0},
    /* Starter motor.  ONE hit is the whole job: $4978's hit sets $09=7 and $61=$FF, and
       $49CE's `LDA $61 / BEQ $4978` then never polls -36 again — so a hit count above 1
       can never be reached and would wedge the script (measured: it did). */
    {KEY_T,     400, 1},
    {KEY_NONE,  2,   0},
    /* ...and into first.  The engine idles at $3C = $28 ($49B9) but the car cannot move in
       neutral, so without this the "race" is still a parked car — the same trap the old
       script fell into one step earlier.  $16AC polls -17 ('Q') for shift-up; one hit is
       one gear.  ⚠ Unlike the starter, -17 keeps being polled forever, so the poll cap
       here is not a safety net but the thing that stops it from selecting every gear. */
    {KEY_Q,     400, 1},
    {KEY_NONE,  2,   0},
};

#else

/* The front end asks a short series of menu questions — menu_wait_key is called from five
   sites in the $63E0 chain ($63F7 X=2, $6416 X=3, $6426 X=3, $646D X=2, $64E7).  Each one
   wants a number key to SELECT (which sets $0077/$0078) and then SPACE to CONFIRM; SPACE
   alone is ignored until something has been selected ($6591: LDA $77 / BEQ back).
   So the pattern is [1, release, SPACE, release], repeated.
   ⚠ Repeated MORE times than there are menus on purpose.  Which menus a given track and
   mode actually presents is not statically obvious, and a script that runs out early
   parks the run back in a menu spin — the exact failure this class exists to prevent.
   Surplus presses land in the driving loop, where '1' is not a bound key and SPACE only
   amplifies steering.
   ⚠ Kept as-is because every published measurement was taken with it — see
   REVS_STRAIGHT_TO_RACE above for what only ONE of these answers actually does. */
#define MENU_ANSWER  {KEY_1, 300, 0}, {KEY_NONE, 150, 0}, {KEY_SPACE, 300, 0}, {KEY_NONE, 150, 0}

static const AutoStep s_script[] = {
    {KEY_NONE, 500, 0},     /* let the title/attract settle before touching anything */
    MENU_ANSWER,
    MENU_ANSWER,
    MENU_ANSWER,
    MENU_ANSWER,
    MENU_ANSWER,
    MENU_ANSWER,
    MENU_ANSWER,
    MENU_ANSWER,
    /* ⚠ INTENDED to start the engine, and measured NOT to.  ⭐ SOLVED — and the key code was
       never the problem: -36 IS 'T', $4978 does poll it every frame while $61 == 0, and the
       STRAIGHT_TO_RACE script above starts the engine with the SAME code on the first hit.
       What breaks here is the 200-poll window: it is spent while the surplus MENU_ANSWER
       presses above are still being consumed, so it expires before the driving loop ever
       asks for -36.  A poll count cannot fix that — the fix is to hold the key until it is
       ANSWERED, which is what `hits` is for.
       ⭐ And even with the engine running this script would still measure a stationary car,
       because it never selects a gear ($0063 is 0 in neutral whatever the throttle does).
       Consequence for every number measured with this script, old and new: the measurement
       window is a PARKED car.  docs/perf-method.md. */
    {KEY_T,    200, 0}, {KEY_NONE, 100, 0},
};

#endif

static const unsigned S_SCRIPT_LEN = sizeof(s_script) / sizeof(s_script[0]);

bool AutoRun::done() const
{
    return !AUTORUN_HOLD_THROTTLE && m_step >= S_SCRIPT_LEN;
}

bool AutoRun::keyDown(uint8_t x)
{
    m_polls++;

    if (m_step >= S_SCRIPT_LEN) {
        /* Steady state, past the end of the script.  A measurement build holds the
           throttle and nothing else — the car under power on the circuit, with the same
           key set every build.  A straight-to-race build answers nothing, and its caller
           stops routing through here at all (done()), so the player owns the keyboard. */
        return AUTORUN_HOLD_THROTTLE && x == KEY_S;
    }

    const AutoStep& s = s_script[m_step];

    const bool expired = (s.hits  && m_hits >= s.hits)
                      || (s.polls && m_polls - m_stepAt >= s.polls);
    if (expired) {
        m_step++;
        m_stepAt = m_polls;
        m_hits   = 0;
        return false;               /* one guaranteed released poll at every boundary */
    }

    const bool held = (s.key != KEY_NONE && x == s.key);
    if (held) m_hits++;
    return held;
}
