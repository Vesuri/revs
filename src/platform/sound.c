/* sound.c — the MOS sound scheduler and the SN76489 state it drives.
 *
 * Read sound.h first: it carries the model and where every number in here was measured.
 * This file is deliberately free of mem[], of the platform layer and of C++, so
 * `tools/validate_sound.c` can link it on its own and diff it against a real BBC.
 */
#include "sound.h"

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
} SndChan;

static SndChan   s_chan[SND_CHANNELS];
static SndEnvDef s_env[4];
static unsigned long s_gen = 0;

/* ⭐ NOT static, and that is deliberate: this is the one thing a debugger has to be able to read
   to tell "the scheduler produced nothing" from "the backend dropped it".  Listed in
   amiga/Makefile PROBE_SYMS so --gc-sections cannot turn the name into a .text address and make
   gdb print instruction bytes as a value (docs/method-lessons.md). */
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
static uint16_t divider_for(uint8_t bbcChan, int16_t pitch)
{
    uint16_t p, d;
    if (pitch < 0) pitch = 0;
    if (pitch > 255) pitch = 255;
    p = (uint16_t)pitch;   /* unsigned, so the 68000 gets a divu.w and not a soft divide */
    d = (uint16_t)(kMosPitchDivider[p % 48u] >> (p / 48u));
    return (uint16_t)(d + (bbcChan - 1));
}

static void chip_set_tone(uint8_t chipCh, uint16_t divider)
{
    if (s_chip.tone[chipCh] != divider) { s_chip.tone[chipCh] = divider; s_gen++; g_sndChipWrites++; }
}

static void chip_set_noise(uint8_t reg)
{
    if (s_chip.noise != reg) { s_chip.noise = reg; s_gen++; g_sndChipWrites++; }
}

static void chip_set_vol(uint8_t chipCh, uint8_t att)
{
    if (s_chip.vol[chipCh] != att) { s_chip.vol[chipCh] = att; s_gen++; g_sndChipWrites++; }
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

/* Write a channel's current state to the chip. */
static void program(uint8_t bbcChan)
{
    SndChan* c = &s_chan[bbcChan];
    const uint8_t chipCh = (uint8_t)(3 - bbcChan);
    if (bbcChan == 0) chip_set_noise((uint8_t)(c->pitch & 7));
    else              chip_set_tone(chipCh, divider_for(bbcChan, c->pitch));
    chip_set_vol(chipCh, c->active ? att_for(c->level) : 15);
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
        for (j = 0; j < SND_QUEUE; j++) { c->queue[j].amp = 0; c->queue[j].pitch = 0; c->queue[j].dur = 0; }
    }
    for (i = 0; i < 4; i++) s_env[i].defined = 0;
    for (i = 0; i < 3; i++) s_chip.tone[i] = kMosPitchDivider[0];
    s_chip.noise = 0;
    for (i = 0; i < 4; i++) s_chip.vol[i] = 15;
    s_gen++;
}

void snd_envelope(const uint8_t blk[14])
{
    uint8_t n = (uint8_t)(blk[0] & 0x0F);
    g_sndEnvelopes++;
    if (n < 1 || n > 4) return;    /* the MOS defines four; a fifth is not a thing to guess at */
    {
        SndEnvDef* e = &s_env[n - 1];
        unsigned i;
        for (i = 0; i < 14; i++) e->bytes[i] = blk[i];
        e->defined = 1;
    }
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

    g_sndCommands++;
    if (sync) g_sndSyncRequests++;
    if (hold) g_sndHoldRequests++;

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
        return;
    }
    if (c->qCount >= SND_QUEUE) { g_sndQueueDrops++; return; }
    g_sndQueued++;
    c->queue[(c->qHead + c->qCount) % SND_QUEUE] = cmd;
    c->qCount++;
}

void snd_flush_channel(uint8_t channel)
{
    SndChan* c;
    channel &= 3;
    c = &s_chan[channel];
    g_sndFlushes++;
    c->active = 0; c->qHead = 0; c->qCount = 0; c->level = 0; c->pitch = 0;
    /* MEASURED: the channel goes silent AND its divider returns to the pitch-0 value; the noise
       register goes to 0.  A flush is how Revs stops the engine noise, so this path is live. */
    if (channel == 0) chip_set_noise(0);
    else              chip_set_tone((uint8_t)(3 - channel), divider_for(channel, 0));
    chip_set_vol((uint8_t)(3 - channel), 15);
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
        if (!s_env[cmd.amp - 1].defined) { g_sndBadEnvelope++; c->env = 0; }
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

void snd_tick(void)
{
    uint8_t ch;
    g_sndTicks++;

    for (ch = 0; ch < SND_CHANNELS; ch++) {
        SndChan* c = &s_chan[ch];
        int started = 0;

        if (!c->active) {
            if (!start_next(ch)) continue;
            started = 1;
            if (!c->active) continue;    /* an amplitude-0 command: pitch programmed, no sound */
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
            }
        }

        program(ch);
    }
}

const SndChip* snd_chip(void) { return &s_chip; }
unsigned long  snd_generation(void) { return s_gen; }
