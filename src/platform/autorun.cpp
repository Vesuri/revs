/* AutoRun — see autorun.h for why a scripted keyboard is a measurement prerequisite. */
#include "autorun.h"
#if !defined(REVS_PLATFORM_AMIGA)
#include <cstdlib>
#include <cstdio>
#include <cmath>
#endif

/* The game's own state, for AutoStep::until — a scripted key that waits on a PROBABILISTIC
   effect (the starter) can only be released by looking at what the game did with it. */
#include "../cpu/mem_decl.h"
#include "../gen/mem.h"   /* MEM_<name> offsets, generated from disasm/symbols.csv */
extern "C" MEM_QUAL uint8_t mem[65536];

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
    KEY_L     = 0xA9,   /* -87  steer LEFT  ($15B5) — BBC 'L' */
    KEY_SEMI  = 0xA8,   /* -88  steer RIGHT ($15C0) — BBC ';/+' */
};

/* A step holds ONE key and ends when EITHER limit is reached; 0 disables that limit.
     polls  — after that many answered polls, whatever was asked.  Wall-clock-free but
              blind: the step can expire before the game ever asks for its key.
     hits   — as soon as the key has been answered HELD that many times.  Reacts to the
              game instead of guessing at it, which is what makes the straight-to-race
              script take three frames instead of a thousand.
   Set both and `polls` is the escape hatch: a hit-counted step whose key the game stops
   asking for would otherwise wedge the script forever, which is a silent hang rather
   than a visible one.  A release step must use `polls` — there is no key to count.
     until  — ⭐ hold the key until the GAME'S OWN STATE says the job is done (mem[until]
              non-zero), whatever the hit count.  This is the only limit that survives a
              key whose effect is PROBABILISTIC, and the starter is exactly that: $498C
              catches on `LDA $FE68 / AND $09`, so one hit starts the engine only about
              one time in eight.  Counting hits worked solely because the port used to
              answer a constant $0 at $FE68 — the moment that became a real 1 MHz counter
              (bbc_hw.cpp), a one-hit starter step left the engine OFF for the whole run.
              ⚠ `polls` still caps it, so a state that never arrives is a bounded failure. */
struct AutoStep {
    uint8_t  key;
    uint16_t polls;
    uint16_t hits;
    uint16_t until;      /* 0 = no state condition */
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

#if defined(REVS_RACE_PROPER)

/* ⭐⭐ STRAIGHT TO THE RACE PROPER — the only script that reaches session_is_race = $80.
 *
 * Why it exists: REVS_COMPETITION below stops in QUALIFYING.  front_end_menus' championship
 * cycle runs one qualifying session per car and only then lays the grid and races, so a
 * competition build sits in a timed practice session with session_is_race = $28 and every
 * `session_is_race & $80` arm in the engine stays unexecuted.  reset_driving_variables' race
 * arm ($18A5-$18BB), update_lap_timers' race arm, draw_starting_lights, spin_car_out's
 * race-only gate and race_position_offset are all in that set — and `determinism`,
 * `determinism-drive` and `determinism-crash` are every one of them a PRACTICE trajectory,
 * which is why the race arms were gated by nothing at all.
 * ⭐ SABOTAGED, so this is a measured claim and not an intention (2026-09-09): with the
 * reference recorded, `make determinism-race` sees four of those five — reset_driving_variables'
 * race arm, draw_starting_lights, race_position_offset and spin_car_out's gate, the last
 * because the field drives into the PARKED player, so contact happens with nobody steering.
 * ⚠ The fifth, update_lap_timers' chequered-flag arm, is still gated by NOTHING: it needs
 * laps-left to go negative, and the player's lap count is 0 even at frame 40000 under a held
 * throttle.  docs/validation-harness.md carries the table and the driving-race negative.
 *
 * ⭐ THE WALK NEEDS EXACTLY ONE QUALIFYING RUN, not twenty.  $6462's second question is
 * `1 ENTER ANOTHER DRIVER / 2 START RACE`; answering 2 makes the car just qualified the
 * human/computer boundary (human_car_first = $13) and drops straight through to the grid.
 * So the cost of reaching the race is ONE qualifying session, and that session ends on the
 * clock rather than on driving: tick_race_timers advances the player's clock on every frame
 * the lights are out, whatever the car is doing, so a parked qualifying run still hits the
 * 4-minute deadline (the shortest qualify_minutes_tbl entry) and ends itself.  MEASURED on
 * the host: the deadline lands around frame 12000 and the menu is up by 14000.
 *
 * ⚠ Holding KEY_2 across the whole qualifying session is deliberate and it is safe: the
 * driving loop polls S/A/TAB/Q/T/L/; and never -50, so the key is invisible until
 * menu_wait_key finally asks for it.  That is what makes this step self-timing — it expires
 * on being ANSWERED, not on a frame count nobody can predict.
 * ⚠ It is the ONE step in any script with `polls` disabled, and it has to be: `polls` is a
 * uint16_t, and 65535 answered polls is fewer frames than the 4-minute deadline takes, so an
 * escape hatch that fits in the field would fire BEFORE the menu it is waiting for.  What
 * bounds it instead is the GAME: qualifying ends on its own clock, which tick_race_timers
 * advances whatever the car does, so the menu this step is waiting for always arrives.  That
 * is the whole argument — there is no timeout underneath it.  ⚠⚠ Do not copy the disabled
 * hatch to the steps after it: a front-end key wait spins WITHOUT advancing the frame
 * counter, so an unanswered hit-counted step there hangs the process outright, and neither
 * REVS_SCREEN_FRAME nor REVS_QUIT_AFTER_DUMP can end it (measured — a 3-minute 99% spin).
 */
static const AutoStep s_script[] = {
    {KEY_2,     600, 2}, /* 1 PRACTICE / 2 COMPETITION -> COMPETITION                    */
    {KEY_NONE,  4,   0},
    {KEY_SPACE, 600, 2},
    {KEY_NONE,  4,   0},
    {KEY_1,     600, 2}, /* SELECT THE CLASS OF RACE -> Novice                           */
    {KEY_NONE,  4,   0},
    {KEY_SPACE, 600, 2},
    {KEY_NONE,  4,   0},
    {KEY_1,     600, 2}, /* DURATION OF QUALIFYING LAPS -> 4 mins, the shortest           */
    {KEY_NONE,  4,   0},
    {KEY_SPACE, 600, 2},
    {KEY_NONE,  4,   0},
    /* ...the driver name and the two wing settings answer themselves through rdch().  Then
       the pits page, and the QUALIFYING session runs until its own clock ends it. */
    {KEY_SPACE, 900, 2},
    {KEY_NONE,  4,   0},
    /* Held right through qualifying; answered the moment $6466's menu comes up. */
    {KEY_2,     0,      2}, /* 1 ENTER ANOTHER DRIVER / 2 START RACE -> START RACE        */
    {KEY_NONE,  4,   0},

    /* ⭐⭐ EVERY DISMISS PAGE FROM HERE COSTS *TWO* SPACE STEPS, because a SCRIPT holds keys
       in a way a player does not.  wait_dismiss spins while SPACE is DOWN ($34D9) and only
       then waits for it to come down again ($34E0), so a step that is still holding SPACE
       when the page opens satisfies only the up-wait; the second step is the press that
       actually dismisses.  One step per page leaves the page waiting for a key the script
       has already moved past — which is how this chain ran out three separate times.
       ⚠⚠ This is a fact about the SCRIPT, not about playing the game.  A human needs one
       press per page, and the port needing two is a REPORTED DEFECT, not this contract —
       docs/controls.md §The double-press.  Do not cite these paired steps as evidence that
       the double-press is faithful: they exist because the script never lets go on its own.
       ⚠⚠ And a wrong chain here cannot be diagnosed from a memory dump: wait_dismiss paints
       its prompt and then spins on the keyboard WITHOUT rendering a frame (faithfully — the
       6502 does the same), so the frame counter stops dead and REVS_SCREEN_FRAME never
       arrives.  A run that looks slow is a run that is stopped.  REVS_AUTORUN_TRACE is the
       instrument: it names the step and the key codes the page really asked for. */
    {KEY_SPACE, 65535, 2},  /* $6466's confirm — menu_wait_key takes SPACE straight away    */
    {KEY_NONE,  4,   0},
    {KEY_SPACE, 65535, 2},  /* the class announcement ($64A8): seen, then released          */
    {KEY_NONE,  4,   0},
    {KEY_SPACE, 65535, 2},  /* ...and dismissed                                             */
    {KEY_NONE,  4,   0},
    {KEY_1,     65535, 2},  /* NUMBER OF LAPS ($64D7) -> 5, the shortest                     */
    {KEY_NONE,  4,   0},
    {KEY_SPACE, 65535, 2},  /* the laps confirm — a row, then a SPACE                        */
    {KEY_NONE,  4,   0},
    {KEY_SPACE, 65535, 2},  /* prompt_driver_ready ($64F8): seen, then released              */
    {KEY_NONE,  4,   0},
    {KEY_SPACE, 65535, 2},  /* ...and dismissed — the RACE PROPER begins                     */
    {KEY_NONE,  4,   0},

    {KEY_T,     400, 0, MEM_engine_running}, /* held until the engine CAUGHT                 */
    {KEY_NONE,  2,   0},
    {KEY_Q,     400, 1},    /* first gear                                                    */
    {KEY_NONE,  2,   0},
};

#elif defined(REVS_COMPETITION)

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
 * ⚠ THE TWO LINE-EDITOR PROMPTS NEED NO KEYS HERE, and that is a property of this BUILD: both
 * read through OSRDCH, and under REVS_AUTORUN_BUILD rdch() answers CR — an immediate end-of-line.
 * The name comes out empty and the wings take the validator's default.  On the real BBC those
 * same prompts had to be typed, which is why the reference loop grew answerName() and
 * answerNumber() and this script did not.
 * ⚠⚠ The Amiga backend's rdch() DOES return real characters for an ordinary build
 * (docs/controls.md §Typing), and it BLOCKS.  The instant answer is kept for autorun builds
 * precisely so this script cannot wedge at the name prompt; if it ever does wedge there, the
 * REVS_AUTORUN_BUILD guard in PlatformAmiga::rdch() is the first place to look.
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
    {KEY_T,     400, 0, MEM_engine_running}, /* held until the engine CAUGHT (below) */
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
    /* ⭐⭐ Starter motor, held until the ENGINE CATCHES ($61 = $FF), not for a fixed number
       of hits.  A hit is not a catch: $498C is `LDA $FE68 / AND $09`, so with $09 = 7 the
       crank succeeds about one poll in eight and the rest of the time the engine just makes
       cranking noise.  A one-hit step was right only while the port answered a CONSTANT $0
       at $FE68 — under the real 1 MHz T2 counter (bbc_hw.cpp) it left the engine off for the
       entire run, and a straight-to-race build then handed the player a dead car.
       ⚠ Once $61 is $FF, $49CE's `LDA $61 / BEQ $4978` stops polling -36 altogether, so the
       state IS the completion signal and the 400-poll cap is only the failure bound. */
    {KEY_T,     400, 0, 0x61},
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

#if !defined(REVS_PLATFORM_AMIGA)
/* ⭐⭐ THE AUTOPILOT (host, `REVS_AUTOPILOT=1`) — a test driver that races every circuit, so a run
   can cover whole laps at speed instead of "drive straight and crash" (`make lap`).
   It reads only the game's own state and answers the same keys a player presses: no engine code
   changes and nothing is written to mem[], so under FIXED_RNG it is deterministic, and the same
   run replays on a real BBC (`make lockstep`).  Everything it steers and brakes by is the TRACK:
   the live section ring (section_coord_lo/hi — 40 sections, their two road edges, ~32 of them
   ahead of the car) relative to the camera (view_origin_16), from which it takes the road
   centre as a path starting at the section behind the car.
     STEERING  pure pursuit.  Measured, the car is kinematic up to its grip: its yaw a frame is
               wheel x ground speed / 100 (heading units, road_speed units), so the wheel that
               puts it on the circle through a point L world units ahead at angle a is a*400/L.
               L grows with speed (REVS_AP_AIM + REVS_AP_AIMK/10 per unit), plus a damping term on
               the aim's rate.  A held key winds the wheel ~64 units a frame, so it is steered to
               that target with SPACE (the game's steering amplifier) while it is far behind.
     SPEED     the lowest of sqrt(lat*R + 2*decel*s) over every bend ahead (R from the net turn
               over one to three sections, s the distance to it less REVS_AP_LEAD, where the bend
               is already under way): the corner speed the grip allows, raised by the braking
               the distance leaves room for.  Compared with the GROUND speed (the camera's own
               step a frame) — road_speed falls away from it whenever the car slides or the
               wheels lock, so braking waits while they differ by REVS_AP_SLIP (the brake is a
               key, so this is ABS).
     TRACTION  a yaw more than REVS_AP_YAWTOL away from the kinematic one is the car sliding: off
               both pedals until it grips.  Without this, throttle or brake with the wheel turned
               spun the car at 25 on the Nurburgring, and a slower speed target did not help.
     ENGINE    stalled -> down to neutral, starter; neutral -> up to first.  A gear key counts
               only on a PRESS, so a shift is a tap on alternate frames.
     GEARS     up on revs under throttle, down on low revs (REVS_AP_TOPGEAR caps it).
   Defaults are tuned: a sweep over all six circuits, 20000 frames each, 0 crashes, 0 stalls and 0
   airborne frames, and each knob moved alone by ~10% either side stays clean; every one can be
   overridden by REVS_AP_<NAME>.  (The picture's edge points were the first steering input and
   are not a usable one at speed: the look-ahead point is a SLOT, whose distance jumps as the
   walk's point count changes, and on the Nurburgring it swung 40 degrees in four frames.)
   THE CHECKS, printed at exit as one `[autopilot]` line that `make lap` parses:
     laps      the player's distance counter wrapping after real progress
     crashes   the car put back on the grid (a distance jump that is not a lap wrap) — the
               game RESETS the car on a crash, so a single one invalidates the run
     stalls    the engine dying where it stands
     airborne  frames with car_height >= 2 — a clean lap has none on any circuit
   and the top ground speed and gear index reached, which say what the run exercised. */
extern "C" uint16_t car_distance_16[];
extern "C" uint16_t car_angle_16[];
extern "C" uint16_t view_origin_16[9];

/* Speeds are in road_speed units: the ground moves 2 world units a frame per unit (measured).
   REVS_AP_LAT and REVS_AP_DECEL are hundredths of a world unit per frame squared. */
static int s_apOn = -1, s_apTrace, s_apAim = 400, s_apAimK = 100, s_apPursuit = 400, s_apKd = 50,
           s_apDead = 32, s_apAmpAt = 600, s_apVmax = 120, s_apVmin = 10, s_apLat = 200,
           s_apDecel = 300, s_apVend = 60, s_apLead = 150, s_apSlip = 4, s_apYawTol = 50,
           s_apTopGear = 6, s_apUpRevs = 120, s_apDownRevs = 70;
static uint8_t s_apSteer, s_apPedal = KEY_S, s_apAmp;   /* the frame's decision */
static int s_apPrevAim, s_apPrevHeading, s_apTspeed, s_apGround, s_apTopSpeed, s_apTopGearSeen;
static int16_t s_apPrevOx, s_apPrevOz;
static unsigned long s_apFrames, s_apLaps, s_apCrashes, s_apStalls, s_apAir, s_apMaxH, s_apRun;
static unsigned s_apPrevDist = 0xFFFFu;
static int s_apPrevRun;
static char s_apRing[64][160];                 /* the last frames, dumped on a crash or a jump */
static unsigned s_apRingAt;
static unsigned long s_apAirGap = 1000;

/* The wheel: car_angle_16[2], sign in bit 0 and magnitude above it (steer_angle_lo's note). */
static int apWheel(void)
{
    const unsigned w = car_angle_16[2];
    return (w & 1u) ? -(int)(w >> 1) : (int)(w >> 1);
}
static int apHeading(void)
{
    return mem[MEM_car_heading_lo] | (mem[MEM_car_heading_hi] << 8);
}
/* The road centre at ring section m (0..39) relative to the camera: the mean of the section's two
   edges, section_coord byte index 3m and 3m + $78 (section_coord_lo's note). */
struct ApPt { float x, z; };
static ApPt s_apPath[40];                       /* the road centre ahead, from the section behind */
static unsigned s_apPathLen;
static ApPt apSection(unsigned m)
{
    auto rel = [](unsigned b, unsigned o) {
        const int v = mem[MEM_section_coord_lo + b + o] | (mem[MEM_section_coord_hi + b + o] << 8);
        return (float)(int16_t)(v - view_origin_16[o]);
    };
    const unsigned i = 3u * m, j = i + 0x78u;
    return { (rel(i, 0) + rel(j, 0)) * 0.5f, (rel(i, 2) + rel(j, 2)) * 0.5f };
}
/* The path: the last section behind the car, then on round the ring the way the car is going
   (or facing, when it is still) until the seam, where a step far longer than a section is the
   ring's far end meeting its near one.  q[0] is BEHIND so that the first bend ahead is measured
   between two sections — the car's own offset across the road is no bend. */
static void apBuildPath(float vx, float vz)
{
    ApPt p[40];
    unsigned m0 = 0;
    for (unsigned m = 0; m < 40u; m++) {
        p[m] = apSection(m);
        if (std::hypot(p[m].x, p[m].z) < std::hypot(p[m0].x, p[m0].z)) m0 = m;
    }
    if (vx * vx + vz * vz < 16.0f) {
        const float th = (float)apHeading() * (float)(2.0 * M_PI / 65536.0);
        vx = std::sin(th); vz = std::cos(th);
    }
    const unsigned up = (m0 + 1u) % 40u, down = (m0 + 39u) % 40u;
    const unsigned step = (p[up].x - p[m0].x) * vx + (p[up].z - p[m0].z) * vz
                        >= (p[down].x - p[m0].x) * vx + (p[down].z - p[m0].z) * vz ? 1u : 39u;
    ApPt* const q = s_apPath;
    unsigned n = 0, m = p[m0].x * vx + p[m0].z * vz > 0 ? (m0 + 40u - step) % 40u : m0;
    for (unsigned k = 0; k < 40u; k++, m = (m + step) % 40u) {
        if (n > 1 && std::hypot(p[m].x - q[n - 1].x, p[m].z - q[n - 1].z) > 400.0f) break;
        q[n++] = p[m];
    }
    s_apPathLen = n;
}
/* The angle from where the car points to the road centre `ahead` world units along the path, in
   the game's own angle units ($10000 a turn, the sense of edge_x: bearing minus car_heading). */
static int apAim(float ahead)
{
    const ApPt* q = s_apPath;
    ApPt at = q[s_apPathLen - 1];
    float s = 0;
    for (unsigned i = 1; i < s_apPathLen; i++) {
        const float seg = std::hypot(q[i].x - q[i - 1].x, q[i].z - q[i - 1].z);
        const float from = i == 1u ? std::hypot(q[1].x, q[1].z) : seg;   /* the car to q[1] */
        if (s + from >= ahead) {
            const float t = seg > 0 ? 1.0f - (s + from - ahead) / seg : 1.0f;
            at = { q[i - 1].x + (q[i].x - q[i - 1].x) * t, q[i - 1].z + (q[i].z - q[i - 1].z) * t };
            break;
        }
        s += from;
    }
    const int bearing = (int)std::lround(std::atan2(at.x, at.z) * (65536.0 / (2.0 * M_PI)));
    return (int16_t)(bearing - apHeading());
}
/* The target ground speed from the bends ahead (THE AUTOPILOT's SPEED), in road_speed units. */
static int apPlan(void)
{
    const ApPt* q = s_apPath;
    const unsigned n = s_apPathLen;
    const float lat = s_apLat / 100.0f, twoB = 2.0f * s_apDecel / 100.0f;
    float s = std::hypot(q[1].x, q[1].z), best = 4.0f * s_apVmax * s_apVmax;   /* car to q[1] */
    for (unsigned i = 1; i + 1 < n; i++) {
        if (i > 1u) s += std::hypot(q[i].x - q[i - 1].x, q[i].z - q[i - 1].z);
        const float room = s > s_apLead ? s - s_apLead : 0.0f;
        const float h0 = std::atan2(q[i].x - q[i - 1].x, q[i].z - q[i - 1].z);
        float len = 0;
        for (unsigned w = 1; w <= 3u && i + w < n; w++) {
            len += std::hypot(q[i + w].x - q[i + w - 1].x, q[i + w].z - q[i + w - 1].z);
            float turn = std::atan2(q[i + w].x - q[i + w - 1].x, q[i + w].z - q[i + w - 1].z) - h0;
            turn = std::fabs(std::remainder(turn, (float)(2.0 * M_PI)));
            if (turn < 1e-3f) continue;
            const float v2 = lat * len / turn + twoB * room;
            if (v2 < best) best = v2;
        }
    }
    const float end = 4.0f * s_apVend * s_apVend + twoB * s;   /* beyond the ring: unknown */
    if (end < best) best = end;
    const int t = (int)(std::sqrt(best) * 0.5f);
    return t < s_apVmin ? s_apVmin : t;
}
static void apDump(const char* what)
{
    if (s_apTrace >= 0) return;
    std::fprintf(stderr, "[ap] %s at f%lu (height %u) — the frames before it:\n", what, s_apFrames,
                 mem[MEM_car_height]);
    for (unsigned i = 0; i < (unsigned)-s_apTrace && i < 64u; i++)
        std::fprintf(stderr, "%s\n", s_apRing[(s_apRingAt - (unsigned)-s_apTrace + i) & 63u]);
}
static void apChecks(int wheel, int target, int aim)
{
    const unsigned dist   = car_distance_16[mem[MEM_player_car]];
    const unsigned lapLen = mem[MEM_lap_length_lo] | (mem[MEM_lap_length_lo + 1] << 8);
    const int run = mem[MEM_engine_running] != 0;
    const int have = s_apPrevDist != 0xFFFFu;
    const int wrapped = have && s_apPrevDist > dist + lapLen / 2u;
    const int jumped  = have && !wrapped
                     && (dist > s_apPrevDist ? dist - s_apPrevDist : s_apPrevDist - dist) > 32u;

    s_apFrames++;
    std::snprintf(s_apRing[s_apRingAt++ & 63u], 160,
                  "  f%lu dist %u speed %u ground %d target %d gear %u revs %u wheel %d tgt %d "
                  "aim %d h %u dir %02X pedal %02X",
                  s_apFrames, dist, mem[MEM_road_speed], s_apGround, s_apTspeed,
                  mem[MEM_gear_index], mem[MEM_engine_revs], wheel, target, aim,
                  mem[MEM_car_height], mem[MEM_track_direction], s_apPedal);
    if (have && !wrapped && dist > s_apPrevDist && !jumped) s_apRun += dist - s_apPrevDist;
    if (wrapped) {
        s_apRun += dist + lapLen - s_apPrevDist;
        if (s_apRun >= lapLen / 2u) s_apLaps++;          /* a wrap after real progress */
    }
    if (jumped) { s_apCrashes++; s_apRun = 0; apDump("CRASH"); }
    else if (s_apPrevRun && !run) { s_apStalls++; apDump("STALL"); }
    if (mem[MEM_car_height] >= 2) {
        if (s_apAirGap > 50) apDump("AIRBORNE");
        s_apAir++; s_apAirGap = 0;
        if (mem[MEM_car_height] > s_apMaxH) s_apMaxH = mem[MEM_car_height];
    } else s_apAirGap++;
    s_apPrevDist = dist; s_apPrevRun = run;
}
static void apDecide(void)
{
    /* the ground speed: the camera's step since the last decision (one a frame); a step no car
       can make is the first frame or a reset, and the wheels stand in for it */
    const int16_t ox = (int16_t)view_origin_16[0], oz = (int16_t)view_origin_16[2];
    const float vx = (int16_t)(ox - s_apPrevOx), vz = (int16_t)(oz - s_apPrevOz);
    s_apPrevOx = ox; s_apPrevOz = oz;
    const int speed  = mem[MEM_road_speed];
    const int step   = (int)(std::hypot(vx, vz) * 0.5f + 0.5f);
    const int ground = step > 250 ? speed : step;
    if (ground > s_apTopSpeed) s_apTopSpeed = ground;
    if (mem[MEM_gear_index] > s_apTopGearSeen) s_apTopGearSeen = mem[MEM_gear_index];

    apBuildPath(vx, vz);
    const int wheel = apWheel();
    int aim = 0, target = wheel, tspeed = s_apVmin;
    if (s_apPathLen >= 3u) {
        const int ahead = s_apAim + s_apAimK * ground / 10;
        aim    = apAim((float)ahead);
        target = aim * s_apPursuit / ahead + (aim - s_apPrevAim) * s_apKd / 100;
        tspeed = apPlan();
    }
    s_apPrevAim = aim;
    s_apSteer = wheel < target - s_apDead ? KEY_SEMI : wheel > target + s_apDead ? KEY_L : KEY_NONE;
    s_apAmp   = (target - wheel > s_apAmpAt || wheel - target > s_apAmpAt) ? 1 : 0;

    const int heading = apHeading();
    const int yaw     = s_apFrames ? (int16_t)(heading - s_apPrevHeading) : 0;
    const int yawErr  = yaw - wheel * ground / 100;
    s_apPrevHeading = heading;
    const bool slide = (yawErr < 0 ? -yawErr : yawErr) > s_apYawTol;
    s_apPedal  = slide ? KEY_NONE
               : ground < tspeed ? KEY_S
               : ground > tspeed + 2 && speed + s_apSlip >= ground ? KEY_A : KEY_NONE;
    s_apTspeed = tspeed; s_apGround = ground;

    apChecks(wheel, target, aim);
    if (s_apTrace > 0 && (s_apFrames % (unsigned)s_apTrace) == 0)
        std::fprintf(stderr, "[ap]%s\n", s_apRing[(s_apRingAt - 1u) & 63u]);
}
#ifdef REVS_TERRAIN_LOW
extern "C" volatile unsigned long g_terrainClipBad;
#endif
static void apReport(void)
{
    std::fprintf(stderr, "[autopilot] %lu frames: %lu laps, %lu crashes, %lu stalls, "
                 "%lu airborne frames (max height %lu); top speed %d in gear index %d\n",
                 s_apFrames, s_apLaps, s_apCrashes, s_apStalls, s_apAir, s_apMaxH,
                 s_apTopSpeed, s_apTopGearSeen);
#ifdef REVS_TERRAIN_LOW
    /* ⭐ view_low_build's rejected lines, summed over its attempts — tools/autopilot_laps.py fails
       on any.  A rejection is not a wrong picture (the chain paints instead), it is the SLOW path,
       so nothing else could see it: it read 12 a sweep on Brands' grid for as long as the car was
       parked, and the game ran at 5.07 fps there where it runs 12.50 (revs_native.c §s_lowClipped). */
    std::fprintf(stderr, "[autopilot] low-block build rejects: %lu\n", g_terrainClipBad);
#endif
}
static void apInit(void)
{
    const char* e = std::getenv("REVS_AUTOPILOT");
    s_apOn = e && e[0] == '1';
    if (!s_apOn) return;
    static const struct { const char* name; int* v; } knobs[] = {
        {"REVS_AP_TRACE", &s_apTrace},   {"REVS_AP_AIM", &s_apAim},     {"REVS_AP_AIMK", &s_apAimK},
        {"REVS_AP_PURSUIT", &s_apPursuit}, {"REVS_AP_KD", &s_apKd},     {"REVS_AP_DEAD", &s_apDead},
        {"REVS_AP_AMP", &s_apAmpAt},     {"REVS_AP_VMAX", &s_apVmax},   {"REVS_AP_VMIN", &s_apVmin},
        {"REVS_AP_LAT", &s_apLat},       {"REVS_AP_DECEL", &s_apDecel}, {"REVS_AP_VEND", &s_apVend},
        {"REVS_AP_LEAD", &s_apLead},     {"REVS_AP_SLIP", &s_apSlip},   {"REVS_AP_YAWTOL", &s_apYawTol},
        {"REVS_AP_TOPGEAR", &s_apTopGear}, {"REVS_AP_UPREVS", &s_apUpRevs},
        {"REVS_AP_DOWNREVS", &s_apDownRevs},
    };
    for (const auto& k : knobs)
        if ((e = std::getenv(k.name))) *k.v = std::atoi(e);
    std::atexit(apReport);
}
static bool apKey(uint8_t x)
{
    if (x == KEY_L) apDecide();                   /* the first driving key a frame polls */
    const bool tap = (s_apFrames & 1u) == 0;
    if (mem[MEM_engine_running] == 0)
        return mem[MEM_gear_index] > 1 ? (tap && x == KEY_TAB) : x == KEY_T;
    if (mem[MEM_gear_index] < 2)      return tap && x == KEY_Q;
    /* racing: up on high revs under throttle, down on low (gear_index 2 = first, 6 = top) */
    {
        const unsigned g = mem[MEM_gear_index], revs = mem[MEM_engine_revs];
        if (g < (unsigned)s_apTopGear && revs > (unsigned)s_apUpRevs && s_apPedal == KEY_S)
            return tap && x == KEY_Q;
        if (g > 2u && revs < (unsigned)s_apDownRevs)
            return tap && x == KEY_TAB;
    }
    if (x == KEY_L || x == KEY_SEMI)  return x == s_apSteer;
    if (x == KEY_SPACE)               return s_apAmp != 0;
    return x == s_apPedal;
}
#endif

bool AutoRun::done() const
{
    return !AUTORUN_HOLD_THROTTLE && m_step >= S_SCRIPT_LEN;
}

bool AutoRun::keyDown(uint8_t x)
{
    m_polls++;

    if (!m_steerRead) {
        m_steerRead = true;
#if defined(REVS_HOLD_STEER_LEFT)
        m_holdSteer = KEY_L;                    /* the target has no environment to read */
#elif defined(REVS_HOLD_STEER_RIGHT)
        m_holdSteer = KEY_SEMI;
#elif !defined(REVS_PLATFORM_AMIGA)
        const char* e = std::getenv("REVS_HOLD_STEER");
        if (e && (e[0] == 'l' || e[0] == 'L')) m_holdSteer = KEY_L;
        else if (e && (e[0] == 'r' || e[0] == 'R')) m_holdSteer = KEY_SEMI;
#endif
    }

    if (m_step >= S_SCRIPT_LEN) {
        /* Steady state, past the end of the script.  A measurement build holds the
           throttle and nothing else — the car under power on the circuit, with the same
           key set every build.  A straight-to-race build answers nothing, and its caller
           stops routing through here at all (done()), so the player owns the keyboard. */
        if (!AUTORUN_HOLD_THROTTLE) return false;
#if !defined(REVS_PLATFORM_AMIGA)
        if (s_apOn < 0) apInit();
        if (s_apOn) return apKey(x);
#endif
        /* ⭐⭐ AND IT RESTARTS AFTER A CRASH, because otherwise the measurement window ends
           on a PARKED CAR.  A straight-line autorun leaves the circuit after ~225 game
           frames; the game resets it to the pits with the engine OFF, and a held throttle
           cannot restart a stopped engine — so the tail of every run was a static scene
           (measured: at the interrupt of a 30 s run, $61 = 00, revs 00, speed 00, i.e. ~36%
           of the sample).  A share table averaged over that is diluted with a workload
           nobody asked for, and a redundancy census over it reads 99% (docs/direct-bitplane-
           plan.md §7g).  So the steady state is a three-state machine on the game's OWN
           cells, in the same order and with the same keys the script used:
             engine off      -> the starter, exactly as the KEY_T step does
             neutral         -> one upshift, and gear_index reaching 2 is what stops it
             otherwise       -> the throttle
           ⚠ It presses nothing the script did not, so the key set is unchanged and the
           workload stays comparable to a run that never crashed. */
        if (mem[MEM_engine_running] == 0) return x == KEY_T;
        if (mem[MEM_gear_index] < 2)      return x == KEY_Q;
        /* ⭐ REVS_HOLD_STEER=l|r additionally holds a STEERING key, which is the only way to
           exercise the steering chain on a host build (the host has no keyboard, and the mouse
           axis belongs to the Amiga).  Without it a host run can only ever show a car going
           straight, so "does the wheel move at all" is unanswerable off-target. */
        if (m_holdSteer && x == m_holdSteer) return true;
        return x == KEY_S;
    }

    const AutoStep& s = s_script[m_step];
    m_seen[(uint8_t)x] = 1;
    m_seen[(uint8_t)x] = 1;

    const bool expired = (s.until && mem[s.until])
                      || (s.hits  && m_hits >= s.hits)
                      || (s.polls && m_polls - m_stepAt >= s.polls);
    if (expired) {
        /* ⭐ REVS_AUTORUN_TRACE prints every step boundary: which step ended, on what, and at
           which poll.  A menu chain is written blind — the pages between two known ones are a
           guess — and the failure it produces (a step expiring on its escape hatch before its
           page was ever drawn, so the rest of the script runs off the end unanswered) looks
           from the outside exactly like a page the game never reached.  This tells the two
           apart in one run: an `on=hits` line is an answered key, `on=polls` is a guess that
           was wrong. */
#if !defined(REVS_PLATFORM_AMIGA)
        static const int trace = std::getenv("REVS_AUTORUN_TRACE") != 0;
        if (trace && !(s.hits && m_hits >= s.hits)) {
            /* A step that ran out of polls is the interesting one: print the key codes the
               game DID ask for while it was current, because the answer is almost always
               that the page on screen wants a different key from the one being held. */
            std::fprintf(stderr, "[autorun] step %u polled:", m_step);
            for (unsigned i = 0; i < 256; i++)
                if (m_seen[i]) std::fprintf(stderr, " %d", (int)(int8_t)i);
            std::fprintf(stderr, "\n");
        }
        for (unsigned i = 0; i < 256; i++) m_seen[i] = 0;
        if (trace && !(s.hits && m_hits >= s.hits)) {
            /* A step that ran out of polls is the interesting one: print the key codes the
               game DID ask for while it was current, because the answer is almost always
               that the page on screen wants a different key from the one being held. */
            std::fprintf(stderr, "[autorun] step %u polled:", m_step);
            for (unsigned i = 0; i < 256; i++)
                if (m_seen[i]) std::fprintf(stderr, " %d", (int)(int8_t)i);
            std::fprintf(stderr, "\n");
        }
        for (unsigned i = 0; i < 256; i++) m_seen[i] = 0;
        if (trace)
            std::fprintf(stderr, "[autorun] step %u key=%d ended on=%s poll=%lu\n",
                         m_step, (int)s.key,
                         (s.until && mem[s.until]) ? "until"
                             : (s.hits && m_hits >= s.hits) ? "hits" : "polls",
                         (unsigned long)m_polls);
#endif
        m_step++;
        m_stepAt = m_polls;
        m_hits   = 0;
        return false;               /* one guaranteed released poll at every boundary */
    }

    const bool held = (s.key != KEY_NONE && x == s.key);
    if (held) m_hits++;
    return held;
}
