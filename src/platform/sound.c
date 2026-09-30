/* sound.c — the MOS sound scheduler and the SN76489 state it drives.
 *
 * Read sound.h first: it carries the model and where every number in here was measured.
 * This file is deliberately free of mem[], of the platform layer and of C++, so
 * `tools/validate_sound.c` can link it on its own and diff it against a real BBC.
 */
#include "sound.h"
#include "diag.h"

/* ⭐⭐ THE STATS ARE A PROBE, NOT THE MODEL — and this file runs 100 times a second forever.
 * Each counter is a `volatile unsigned long` read-modify-write to absolute memory: on a 68000
 * that is a `move.l abs,d0` / `addq.l` / `move.l d0,abs`, ~40 cycles, uncoalescable *because*
 * it is volatile.  snd_tick() bumps about seven of them per tick (one per tick, one per channel
 * visit, one per program() call), which measured at roughly a seventh of the whole routine — and
 * the VERTB ISR is a fixed tax on wall clock, not on the framerate (docs/perf-method.md §the
 * VERTB ISR), so a shipping build must not pay it.  Only .gdb probe scripts ever read these.
 * ⚠ The DEFINITIONS below stay unconditional: amiga/Makefile lists g_sndTicks and friends in
 * PROBE_SYMS for EVERY link, so gating the symbol away fails probe-audit on a plain build. */
/* ⭐ REVS_SND_STAT_OFF is how the instrument's OWN cost gets measured: a PROBES + ISRSPLIT build
   with it defined leaves the timing brackets in place and takes the counters out, so the snd_tick
   row moves by exactly what the counters cost (docs/method-lessons.md: an instrument whose cost
   exceeds what it measures).  Its counter lines read 0 by construction — that is not a failure. */
#if defined(REVS_PROBE) && !defined(REVS_SND_STAT_OFF)
#define SND_STAT(x) do { (x)++; } while (0)
#else
#define SND_STAT(x) ((void)0)
#endif

volatile unsigned long g_sndCommands     = 0;
volatile unsigned long g_sndEnvelopes    = 0;
volatile unsigned long g_sndFlushes      = 0;
volatile unsigned long g_sndTicks        = 0;
volatile unsigned long g_sndChipWrites   = 0;
volatile unsigned long g_sndSyncRequests = 0;
volatile unsigned long g_sndHoldRequests = 0;
volatile unsigned long g_sndQueued       = 0;
volatile unsigned long g_sndQueueDrops   = 0;
volatile unsigned long g_sndBadEnvelope  = 0;

/* ── the MOS's pitch table ─────────────────────────────────────────────────────────────────
 * One octave of tone dividers; pitch p uses kMosPitchDivider[p % 48] >> (p / 48).  MEASURED for
 * all 256 pitches (tools/bbc_probe_sound.mjs --only=pitch), and the shift rule is re-checked by
 * that probe on every run.  Equal temperament with 48 steps to the octave: the entries agree with
 * 1008 / 2^(p/48) to within 2 counts, i.e. under 4 cents, so this is a measurement of a
 * standard musical mapping rather than a lifted artefact. */
static const uint16_t kMosPitchDivider[48] = {
    1008, 994, 980, 966, 951, 938, 925, 912, 898, 886, 874, 862,
     847, 835, 823, 811, 800, 789, 778, 767, 755, 745, 735, 725,
     712, 702, 692, 682, 672, 663, 654, 645, 635, 626, 617, 608,
     599, 591, 583, 575, 565, 557, 549, 541, 534, 527, 520, 513,
};

/* Envelope phases.  Release is entered when the duration expires, never before. */
enum { PH_ATTACK = 0, PH_DECAY, PH_SUSTAIN, PH_RELEASE };

typedef struct {
    uint8_t amp;    /* the block's amplitude LOW byte: 0 = silence, 1..4 = envelope, else static */
    uint8_t pitch;
    uint8_t dur;
} SndCmd;

typedef struct {
    uint8_t  bytes[14];
    uint8_t  defined;
} SndEnvDef;

typedef struct {
    uint8_t  active;
    uint8_t  env;        /* 0 = static amplitude */
    int16_t  level;      /* the MOS's internal amplitude, 0..126 */
    int16_t  pitch;      /* base pitch plus the accumulated pitch envelope */
    uint8_t  phase;
    uint8_t  stepLeft;   /* ticks until the next envelope step */
    uint8_t  pSection;   /* 0..3; 3 = the pitch envelope has run out (and repeats unless held) */
    uint8_t  pStep;
    uint16_t durLeft;    /* ticks; 0 with `infinite` clear means the sound has ended */
    uint8_t  infinite;   /* duration 255 */
    SndCmd   queue[SND_QUEUE];
    uint8_t  qHead, qCount;
    /* ⭐ THE PROGRAM MEMO — see program() below.  The last (active, pitch, level) this channel
       was programmed from, and a valid flag that anything writing the chip behind program()'s
       back must clear. */
    uint8_t  progValid;
    uint8_t  progActive;
    int16_t  progPitch;
    int16_t  progLevel;
} SndChan;

static SndChan   s_chan[SND_CHANNELS];
static SndEnvDef s_env[4];
unsigned long g_sndGen = 0;
#define s_gen g_sndGen

/* ⭐⭐ THE QUIET FLAG: non-zero means the next snd_tick() provably changes nothing, so it returns
 * at once.  A tick is a no-op exactly when every channel is either idle with an empty queue, or
 * sounding with no clock on it — infinite (or already-expired) duration and no defined envelope —
 * and its program memo already matches (program() ran for it on the tick that set the flag).
 * Revs sits in that state almost all the time: the engine note is a static-amplitude infinite
 * sound on one channel, re-issued with a flush only when its pitch moves.
 *
 * WHY: snd_tick() runs twice a display field forever inside the VERTB ISR, a fixed tax on wall
 * clock (docs/perf-method.md §the VERTB ISR), and three idle channels still paid a start_next()
 * call each to find an empty queue.
 *
 * ⚠ EVERY ENTRY POINT THAT WRITES s_chan OR s_env MUST CLEAR IT — snd_sound, snd_flush_channel,
 * snd_envelope and snd_reset do.  snd_tick() itself sets it only from the state it leaves. */
uint8_t g_sndQuiet = 0;
#define s_quiet g_sndQuiet
/* ⚠ CLEARED LAST, behind a compiler barrier: these entry points run in main-loop context and the
   VERTB ISR ticks in between.  Cleared FIRST, a tick preempting the write could see the old state,
   set the flag again, and strand the new command until some later call cleared it. */
#define SND_WAKE() do { __asm__ __volatile__("" ::: "memory"); s_quiet = 0; } while (0)

/* ⭐ NOT static, and that is deliberate: this is the one thing a debugger has to be able to read
   to tell "the scheduler produced nothing" from "the backend dropped it".  Listed in
   amiga/Makefile PROBE_SYMS so --gc-sections cannot turn the name into a .text address and make
   gdb print instruction bytes as a value (docs/method-lessons.md). */
volatile unsigned long g_sndProgramRuns = 0, g_sndProgramSkips = 0;
volatile unsigned long g_sndEnvSteps = 0, g_sndChanVisits = 0;

SndChip g_sndChip;
#define s_chip g_sndChip

/* ── the chip ──────────────────────────────────────────────────────────────────────────── */

uint16_t snd_noise_divisor(const SndChip* c)
{
    switch (c->noise & 3) {
    case 0:  return 0x10;
    case 1:  return 0x20;
    case 2:  return 0x40;
    default: return c->tone[2] ? c->tone[2] : 1024;   /* mode 3: tone 3 = chip tone 2 */
    }
}

/* The pitch-0 divider a channel resets to, and the ordinary conversion.  The +(ch-1) is the MOS's
   own per-channel detune (sound.h); it is applied AFTER the octave shift — measured at pitch 0,
   130 and 200 on all three tone channels. */
/* ⭐⭐ THE WHOLE 256-PITCH MAP, FLATTENED — `kMosPitchDivider[p % 48] >> (p / 48)` for every p.
 * DERIVED from the 48-entry measurement above at reset, so that table stays the single source of
 * truth and `make sound` still proves the mapping against a real MOS.
 *
 * WHY: this is called from program(), which snd_tick() calls per active channel, twice per
 * display field, forever.  `% 48` and `/ 48` on a uint16 are a `divu.w` and a `divs.w` — ~300
 * 68000 cycles between them, plus a variable `asr.l`, to look up a value that only ever takes 256
 * distinct answers.  512 bytes of table removes all of it (docs/m68k-optimisation.md: the 68000
 * has no cheap divide, and a table indexed by a byte is two instructions). */
static uint16_t s_dividerOfPitch[256];
static uint8_t  s_dividerBuilt = 0;

static void build_divider_table(void)
{
    /* Walked rather than divided: `p % 48` / `p / 48` on an `unsigned` are a 32-bit divide, which
       the 68000 does not have and amiga/Makefile's muldiv-audit rejects outright (CLAUDE.md). */
    unsigned p, idx = 0, oct = 0;
    for (p = 0; p < 256; p++) {
        s_dividerOfPitch[p] = (uint16_t)(kMosPitchDivider[idx] >> oct);
        if (++idx == 48u) { idx = 0; oct++; }
    }
    s_dividerBuilt = 1;
}

static uint16_t divider_for(uint8_t bbcChan, int16_t pitch)
{
    if (pitch < 0) pitch = 0;
    if (pitch > 255) pitch = 255;
    if (!s_dividerBuilt) build_divider_table();
    /* The +(ch-1) is the MOS's own per-channel detune, applied AFTER the octave shift. */
    return (uint16_t)(s_dividerOfPitch[(uint8_t)pitch] + (bbcChan - 1));
}

static void chip_set_tone(uint8_t chipCh, uint16_t divider)
{
    if (s_chip.tone[chipCh] != divider) { s_chip.tone[chipCh] = divider; s_gen++; SND_STAT(g_sndChipWrites); }
}

static void chip_set_noise(uint8_t reg)
{
    if (s_chip.noise != reg) { s_chip.noise = reg; s_gen++; SND_STAT(g_sndChipWrites); }
}

static void chip_set_vol(uint8_t chipCh, uint8_t att)
{
    if (s_chip.vol[chipCh] != att) { s_chip.vol[chipCh] = att; s_gen++; SND_STAT(g_sndChipWrites); }
}

/* level 0..126 -> attenuation 15..0.  The one rule behind both static amplitudes and envelopes. */
static uint8_t att_for(int16_t level)
{
    int16_t a;
    if (level < 0) level = 0;
    if (level > 126) level = 126;
    a = 15 - (level >> 3);
    return (uint8_t)(a < 0 ? 0 : a);
}

/* Write a channel's current state to the chip.
 *
 * ⭐⭐ MEMOISED, and it is EXACT rather than an approximation: everything below is a pure
 * function of (bbcChan, c->active, c->pitch, c->level), and the three chip_set_* it calls are
 * already no-ops when the value is unchanged.  So a repeat call with the same three inputs
 * provably cannot move s_chip or s_gen — skipping it is unobservable.
 *
 * WHY IT MATTERS: snd_tick() ends every active channel with program(), and snd_tick() runs
 * TWICE PER DISPLAY FIELD forever (the MOS schedules sound on the System VIA's 100 Hz timer,
 * sound.h).  A held engine note recomputes the identical divider and attenuation 100 times a
 * second.  That is a fixed tax on wall clock, not on the framerate (docs/perf-method.md §the
 * VERTB ISR), which is why it is worth a four-field compare.
 *
 * ⚠ ANY OTHER WRITER OF s_chip MUST CLEAR progValid — snd_flush_channel and snd_reset do.
 * The memo is state ABOUT the chip, so a write that bypasses program() makes it a lie. */
static void program_now(uint8_t bbcChan)
{
    SndChan* c = &s_chan[bbcChan];
    const uint8_t chipCh = (uint8_t)(3 - bbcChan);
    SND_STAT(g_sndProgramRuns);
    c->progValid  = 1;
    c->progActive = c->active;
    c->progPitch  = c->pitch;
    c->progLevel  = c->level;
    if (bbcChan == 0) chip_set_noise((uint8_t)(c->pitch & 7));
    else              chip_set_tone(chipCh, divider_for(bbcChan, c->pitch));
    chip_set_vol(chipCh, c->active ? att_for(c->level) : 15);
}

/* ⭐ THE MEMO GATE, FORCE-INLINED.  The hit rate measured 94%, so the common case must not pay a
   `jsr` + `movem` at all: GCC leaves a four-field compare out of line at -O2 and every skipped
   call then costs more in call overhead than in comparing (docs/perf-method.md §twins #14/#15 —
   the same lesson that put REVS_FLAG_OP's helpers back inline).  The MISS goes out of line to
   program_now(), which is where the divider table and the chip writes live. */
__attribute__((always_inline)) static inline void program(uint8_t bbcChan)
{
    SndChan* c = &s_chan[bbcChan];
    if (c->progValid && c->progActive == c->active &&
        c->progPitch == c->pitch && c->progLevel == c->level) { SND_STAT(g_sndProgramSkips); return; }
    program_now(bbcChan);
}

/* ── the scheduler ─────────────────────────────────────────────────────────────────────── */

void snd_reset(void)
{
    unsigned i, j;
    for (i = 0; i < SND_CHANNELS; i++) {
        SndChan* c = &s_chan[i];
        c->active = 0; c->env = 0; c->level = 0; c->pitch = 0;
        c->phase = PH_ATTACK; c->stepLeft = 1; c->pSection = 0; c->pStep = 0;
        c->durLeft = 0; c->infinite = 0; c->qHead = 0; c->qCount = 0;
        c->progValid = 0;          /* s_chip is reset below, behind program()'s back */
        for (j = 0; j < SND_QUEUE; j++) { c->queue[j].amp = 0; c->queue[j].pitch = 0; c->queue[j].dur = 0; }
    }
    for (i = 0; i < 4; i++) s_env[i].defined = 0;
    for (i = 0; i < 3; i++) s_chip.tone[i] = kMosPitchDivider[0];
    s_chip.noise = 0;
    for (i = 0; i < 4; i++) s_chip.vol[i] = 15;
    s_gen++;
    SND_WAKE();
}

void snd_envelope(const uint8_t blk[14])
{
    uint8_t n = (uint8_t)(blk[0] & 0x0F);
    REVS_DIAG(g_sndEnvelopes++);
    if (n < 1 || n > 4) return;    /* the MOS defines four; a fifth is not a thing to guess at */
    {
        SndEnvDef* e = &s_env[n - 1];
        unsigned i;
        for (i = 0; i < 14; i++) e->bytes[i] = blk[i];
        e->defined = 1;
    }
    SND_WAKE();
}

void snd_sound(const uint8_t blk[8])
{
    const uint8_t chanByte = blk[0];
    const uint8_t chan     = (uint8_t)(chanByte & 3);
    const uint8_t flush    = (uint8_t)((chanByte >> 4) & 0x0F);
    const uint8_t sync     = (uint8_t)(blk[1] & 0x0F);          /* the S nibble of &HSFC */
    const uint8_t hold     = (uint8_t)((blk[1] >> 4) & 0x0F);   /* the H nibble */
    SndChan* c = &s_chan[chan];
    SndCmd cmd;

    REVS_DIAG(g_sndCommands++);
    if (sync) REVS_DIAG(g_sndSyncRequests++);
    if (hold) REVS_DIAG(g_sndHoldRequests++);

    /* Only the LOW byte of each parameter word is significant — see sound.h.  Revs leaves the
       amplitude high byte at $FF, so treating the pair as a signed 16-bit value reads -256. */
    cmd.amp   = blk[2];
    cmd.pitch = blk[4];
    cmd.dur   = blk[6];

    if (flush) {
        /* F = 1: drop whatever is queued and take effect on the next tick.  This is the only
           form Revs ever issues (all five of its blocks are $10..$13). */
        c->qHead = 0; c->qCount = 1; c->queue[0] = cmd;
        c->active = 0;
        SND_WAKE();
        return;
    }
    if (c->qCount >= SND_QUEUE) { REVS_DIAG(g_sndQueueDrops++); return; }
    REVS_DIAG(g_sndQueued++);
    c->queue[(c->qHead + c->qCount) % SND_QUEUE] = cmd;
    c->qCount++;
    SND_WAKE();
}

void snd_flush_channel(uint8_t channel)
{
    SndChan* c;
    channel &= 3;
    c = &s_chan[channel];
    REVS_DIAG(g_sndFlushes++);
    c->active = 0; c->qHead = 0; c->qCount = 0; c->level = 0; c->pitch = 0;
    /* ⚠ DEFENSIVE, NOT LOAD-BEARING — and the argument is worth keeping because a sabotage of
       this line SURVIVES `make sound` (validation-harness.md §FIFTEENTH: "no change at all").
       A flush leaves the chip in EXACTLY the state program() produces from (active 0, pitch 0,
       level 0): noise 0 / divider_for(ch,0) and attenuation 15, which is what the three lines
       below write.  So a memo reading (0,0,0) after a flush is telling the truth.
       ⭐ The SIBLING case is different and its sabotage DOES fail: snd_reset sets all three tone
       dividers to kMosPitchDivider[0] with no per-channel detune, which program() would never
       produce for channels 2 and 3 — so that invalidation is real.  Keep both. */
    c->progValid = 0;
    /* MEASURED: the channel goes silent AND its divider returns to the pitch-0 value; the noise
       register goes to 0.  A flush is how Revs stops the engine noise, so this path is live. */
    if (channel == 0) chip_set_noise(0);
    else              chip_set_tone((uint8_t)(3 - channel), divider_for(channel, 0));
    chip_set_vol((uint8_t)(3 - channel), 15);
    SND_WAKE();
}

/* One envelope step: amplitude first, then pitch, then the phase bookkeeping. */
static void env_step(SndChan* c, const SndEnvDef* e)
{
    const int8_t  aa = (int8_t)e->bytes[8], ad = (int8_t)e->bytes[9];
    const int8_t  as = (int8_t)e->bytes[10], ar = (int8_t)e->bytes[11];
    const uint8_t ala = (uint8_t)(e->bytes[12] & 0x7F), ald = (uint8_t)(e->bytes[13] & 0x7F);
    const int8_t  pi[3] = { (int8_t)e->bytes[2], (int8_t)e->bytes[3], (int8_t)e->bytes[4] };
    const uint8_t pn[3] = { e->bytes[5], e->bytes[6], e->bytes[7] };
    const uint8_t repeat = (uint8_t)((e->bytes[1] & 0x80) == 0);

    switch (c->phase) {
    case PH_ATTACK:
        c->level += aa;
        if ((aa >= 0 && c->level >= (int16_t)ala) || (aa < 0 && c->level <= (int16_t)ala)) {
            c->level = (int16_t)ala;
            c->phase = PH_DECAY;
        }
        break;
    case PH_DECAY:
        /* ⚠ With AD = 0 the level never reaches ALD, so the sound HOLDS at ALA until its duration
           ends.  That is exactly Revs's envelope (AD = AS = AR = 0, ALA = the master volume x 8)
           and it is measured behaviour, not a degenerate case to special-case away. */
        c->level += ad;
        if (ad != 0 && ((ad < 0 && c->level <= (int16_t)ald) || (ad > 0 && c->level >= (int16_t)ald))) {
            c->level = (int16_t)ald;
            c->phase = PH_SUSTAIN;
        }
        break;
    case PH_SUSTAIN:
        c->level += as;
        break;
    case PH_RELEASE:
        c->level += ar;
        if (c->level <= 0) { c->level = 0; c->active = 0; }
        break;
    }
    if (c->level < 0)   c->level = 0;
    if (c->level > 126) c->level = 126;

    /* The pitch envelope.  ⭐ The accumulated pitch is NOT reset when the envelope repeats — it
       keeps walking, which is why Revs's sections are net-zero (+2 x4, -2, -6). */
    if (c->pSection < 3) {
        c->pitch += pi[c->pSection];
        c->pStep++;
        while (c->pSection < 3 && c->pStep >= pn[c->pSection]) { c->pSection++; c->pStep = 0; }
    }
    if (c->pSection >= 3 && repeat) { c->pSection = 0; c->pStep = 0; }
}

static int start_next(uint8_t chan)
{
    SndChan* c = &s_chan[chan];
    SndCmd cmd;
    if (!c->qCount) return 0;
    cmd = c->queue[c->qHead];
    c->qHead = (uint8_t)((c->qHead + 1) % SND_QUEUE);
    c->qCount--;

    c->pitch    = (int16_t)cmd.pitch;
    c->infinite = (uint8_t)(cmd.dur == 255);
    c->durLeft  = (uint16_t)(cmd.dur * 5u);      /* a duration unit is a twentieth of a second */
    c->phase    = PH_ATTACK;
    c->pSection = 0;
    c->pStep    = 0;

    if (cmd.amp == 0) {
        /* Amplitude 0 is silence.  Revs uses it constantly: the muted channel 1 exists only to
           give the noise generator its divider (sound.h), so the PITCH still has to be programmed. */
        c->env = 0; c->level = 0; c->active = 0;
        program(chan);
        return 1;
    }
    if (cmd.amp >= 1 && cmd.amp <= 4) {
        c->env = cmd.amp;
        if (!s_env[cmd.amp - 1].defined) { REVS_DIAG(g_sndBadEnvelope++); c->env = 0; }
        c->level = 0;
    } else {
        /* A static amplitude, held as the two's-complement byte $F1..$FF = -15..-1. */
        c->env   = 0;
        c->level = (int16_t)(-(int16_t)(int8_t)cmd.amp * 8);
    }
    c->active   = 1;
    c->stepLeft = 1;
    return 1;
}

/* ⭐ ALWAYS counted, unlike the SND_STAT counters: the MOS layer reads it to tell whether a
   queued note has been taken off its buffer yet (mos.cpp §THE SOUND BUFFERS) — which decides the
   Y a SOUND call returns.  One byte, one add a tick. */
volatile uint8_t snd_tick_epoch = 0;

void snd_tick(void)
{
    uint8_t ch, busy = 0;
    snd_tick_epoch++;
    SND_STAT(g_sndTicks);
    if (s_quiet) return;

    for (ch = 0; ch < SND_CHANNELS; ch++) {
        SndChan* c = &s_chan[ch];
        int started = 0;
        SND_STAT(g_sndChanVisits);

        if (!c->active) {
            if (!c->qCount || !start_next(ch)) continue;   /* idle, empty queue: stays so */
            started = 1;
            if (!c->active) { busy |= c->qCount; continue; }   /* amplitude 0: pitch programmed, no sound */
        }

        /* The duration first: MEASURED, a duration-4 sound is audible for 19 ticks, so the
           starting tick counts as one of them.  Expiry hands an envelope to its release phase in
           this same tick, and stops a static sound outright. */
        if (!c->infinite && c->durLeft) {
            c->durLeft--;
            if (c->durLeft == 0) {
                if (c->env && s_env[c->env - 1].defined) {
                    c->phase = PH_RELEASE;
                    if ((int8_t)s_env[c->env - 1].bytes[11] == 0) c->active = 0;  /* AR = 0: stop */
                } else {
                    c->active = 0;
                }
            }
        }

        if (c->active && c->env && s_env[c->env - 1].defined) {
            /* ⭐ On the very first tick the envelope has ALREADY taken one step — the first
               attenuation a real MOS writes is AA, never 0.  So step unconditionally on the start
               tick and then every (byte1 & $7F) ticks. */
            const uint8_t stepLen = (uint8_t)(s_env[c->env - 1].bytes[1] & 0x7F);
            if (started || --c->stepLeft == 0) {
                c->stepLeft = (uint8_t)(stepLen ? stepLen : 1);
                env_step(c, &s_env[c->env - 1]);
                SND_STAT(g_sndEnvSteps);
            }
        }

        program(ch);
        /* Can the NEXT tick change this channel?  A queue behind a silent channel, a duration
           still counting, or an envelope still stepping — anything else is fixed until a command.
           ⚠ The two QUEUE arms (here and the amplitude-0 `continue` above) survive sabotage in
           `make sound`: the recorded MOS sweeps never queue a note behind one that ends, and Revs
           cannot — every block it issues is a flush (F = 1), so a queue holds at most the one
           command start_next() consumes.  Unreachable here, a fixture gap for any other client:
           keep both (docs/validation-harness.md §FIFTEENTH). */
        busy |= (uint8_t)(c->active ? ((!c->infinite && c->durLeft) ||
                                       (c->env && s_env[c->env - 1].defined))
                                    : c->qCount != 0);
    }
    s_quiet = (uint8_t)!busy;
}

const SndChip* snd_chip(void) { return &s_chip; }
