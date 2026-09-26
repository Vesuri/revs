#ifndef SOUND_H
#define SOUND_H
/* sound.h — THE MOS SOUND SCHEDULER + THE SN76489.  One copy, both backends.
 *
 * ⭐ REVS NEVER ADDRESSES THE SOUND CHIP.  Every note is an `OSWORD 7` (SOUND) control block and
 * one `OSWORD 8` (ENVELOPE) definition — `sound_queue` ($0B4A), `sound_queue_default` ($0B47) and
 * `sound_envelope` ($0B65), with the five 8-byte blocks living at $0B10-$0B37 and the envelope at
 * $0B38.  So the thing the port has to reproduce is the OS's *scheduler*, not a chip's registers:
 * the pitch→divider mapping, the amplitude→attenuation mapping, the noise-mode encoding, the
 * 100 Hz envelope stepper and what a buffer flush does.  None of that is in the game binary.
 *
 * It is therefore all MEASURED, off a real MOS 1.20 under jsbeeb — `tools/bbc_probe_sound.mjs`
 * sweeps every input and records what reaches the chip, and `--sound=` on
 * `tools/bbc_refloop_race.mjs` captures Revs's OWN stream out of a real race with the engine
 * running.  `make sound` replays both through this file and diffs the chip state tick by tick.
 * Nothing here is [ASSUMED] unless it says so, and the four things that are, are all on paths
 * Revs never takes (docs/bbc-hardware.md §Sound).
 *
 * ── WHAT REVS ACTUALLY PLAYS (measured in a real race, 374 SOUND commands) ────────────────
 *
 *   BBC ch0 = NOISE, pitch 3 = periodic noise clocked from channel 1's divider, amplitude -10
 *   BBC ch1 = a tone whose pitch chases the rev counter ($0060), amplitude 0 or -10
 *   BBC ch2 = the same 28 pitch units lower, amplitude 0 or -10
 *   BBC ch3 = envelope 1, the tyre squeal/scrape ($0E7C, gated on a random byte from $FE68)
 *
 * ⭐ The engine sound is ONE MECHANISM WITH TWO HALVES, and it is why channel 1 is often silent:
 * `engine_sound_update` ($0E74) plays the NOISE channel at low revs with channel 1 muted purely to
 * supply its divider (noise pitch 3 = "use tone 1's frequency"), and above rev $5C it stops the
 * noise channel (`sound_stop_channel` $0E5A, an OSBYTE 21 buffer flush) and lets the two tones
 * sound.  A port that ignored the muted channel's pitch would play a fixed-pitch buzz.
 *
 * ── THE PIECES, AND WHERE THE NUMBERS COME FROM ───────────────────────────────────────────
 *
 * pitch → divider  [MEASURED, all 256]  One octave of 48 dividers, shifted right by the octave:
 *      divider = kMosPitchDivider[pitch % 48] >> (pitch / 48), plus (channel - 1).
 *      The channel term is real and deliberate — the same pitch on channels 1/2/3 comes out one
 *      divider count apart, so the MOS detunes its channels — and the port has to reproduce it or
 *      Revs's two engine tones beat differently from the real machine.
 *      The equal-tempered formula 1008 / 2^(pitch/48) agrees within 2 counts everywhere, which is
 *      the check that keeps the table an independently-derivable measurement.
 *
 * amplitude → attenuation  [MEASURED]  The chip's 4-bit attenuation is 15 - (level >> 3), where
 *      `level` is the MOS's internal 0..126 amplitude.  A static SOUND amplitude A (0..-15) sets
 *      level = -A*8, so A=0 is silence and A=-15 is attenuation 0.  ⚠ ONLY THE LOW BYTE of the
 *      block's amplitude word is used: Revs writes only that byte (`sound_queue` stores Y at
 *      block+2) and leaves the template's $FF high byte, so the real machine sees $FF00 and plays
 *      silence.  Reading the word as signed 16 bits gives -256 and a wrong answer.
 *
 * envelopes  [MEASURED]  A 14-byte OSWORD 8 block, stepped every (byte1 & $7F) centiseconds.
 *      Amplitude: attack += AA until ALA, decay += AD until ALD, sustain += AS, and after the
 *      duration expires release += AR until 0.  Pitch: PI1 x PN1 steps, PI2 x PN2, PI3 x PN3,
 *      then REPEATS unless byte 1 bit 7 is set — repeating is the default, and the accumulated
 *      pitch is NOT reset at the repeat, which is why Revs's envelope is net-zero (+2 x4, -2, -6).
 *
 * duration  [MEASURED]  Units of a twentieth of a second = 5 ticks; 255 = play until superseded,
 *      which is what all of Revs's continuous sounds use.
 *
 * OSBYTE 21  [MEASURED]  Flushing sound buffer 4+channel SILENCES the channel then and there and
 *      resets its divider to the pitch-0 value.  This matters: it is the only way Revs ever stops
 *      a sound, since every one of them has duration 255.
 *
 * ── THE TICK ──────────────────────────────────────────────────────────────────────────────
 *
 * The MOS scheduler runs on the System VIA's 100 Hz timer, NOT on vsync, so `snd_tick()` must be
 * called 100 times a second independently of the port's framerate — twice per display field on
 * the Amiga.  A flushed sound starts on the tick AFTER its OSWORD, and on that first tick the
 * envelope has already taken one step (measured: the first attenuation written is AA, not 0).
 */

/* ⚠ NOT <stdint.h> under C++.  The Amiga build force-includes framework/SASCCompat.h, whose
   int8_t is `char` where the toolchain's own compat stdint.h says `signed char`, and the two
   typedefs conflict; C++ translation units get the types from there (host: platform.h's
   <cstdint>).  The C ones — sound.c and tools/validate_sound.c — need the real header. */
#ifndef __cplusplus
#include <stdint.h>
#endif

#define SND_TICK_HZ    100u   /* the System VIA timer rate the MOS schedules sound on */
#define SND_CHANNELS   4u     /* BBC sound channels 0..3; 0 is the noise generator */
#define SND_QUEUE      4u     /* per-channel pending commands (Revs always flushes: never used) */

/* The SN76489 as the MOS leaves it.  Chip channel index, NOT BBC channel: the MOS maps BBC
   channel c onto chip channel 3-c, which is why noise (BBC 0) is chip 3 and why noise mode 3
   ("take the frequency from tone 3") borrows chip tone 2 = BBC channel 1. */
typedef struct {
    uint16_t tone[3];   /* 10-bit dividers; f = 125000 / divider Hz (4 MHz / 32), 0 means 1024 */
    uint8_t  noise;     /* bit 2 = white (else periodic), bits 1..0 = rate: 0/1/2 preset, 3 = tone 2 */
    uint8_t  vol[4];    /* attenuation 0..15 in 2 dB steps; 15 = silent */
} SndChip;

#ifdef __cplusplus
extern "C" {
#endif

/* ── counters (⚠ every one of these must be in amiga/Makefile PROBE_SYMS) ───────────────── */
extern volatile unsigned long g_sndCommands;    /* OSWORD 7 blocks accepted */
extern volatile unsigned long g_sndEnvelopes;   /* OSWORD 8 blocks accepted */
extern volatile unsigned long g_sndFlushes;     /* OSBYTE 21 channel flushes */
extern volatile unsigned long g_sndTicks;       /* 100 Hz scheduler ticks run */
extern volatile unsigned long g_sndChipWrites;  /* chip-state changes the backend must apply */
/* ⭐ WHY THE SCHEDULER COSTS WHAT IT DOES — plain counts, exact, ~1 add each (the ISR runs at
   50 Hz forever, so its cost is a fixed tax on wall clock; docs/perf-method.md §the VERTB ISR). */
extern volatile unsigned long g_sndProgramRuns;   /* program() bodies actually executed  */
extern volatile unsigned long g_sndProgramSkips;  /* ...memo hits that skipped one       */
extern volatile unsigned long g_sndEnvSteps;      /* envelope steps taken                */
extern volatile unsigned long g_sndChanVisits;    /* per-tick channel iterations reached */
/* ⚠ Counted, not shrugged at.  Sync and hold are real MOS features on the channel byte that
   Revs has never once used; a queued (non-flush) command is likewise unmeasured.  If any of
   these ever moves, the model is being asked for something it was not measured against. */
extern volatile unsigned long g_sndSyncRequests;
extern volatile unsigned long g_sndHoldRequests;
extern volatile unsigned long g_sndQueued;
extern volatile unsigned long g_sndQueueDrops;
extern volatile unsigned long g_sndBadEnvelope; /* amplitude 1..4 naming an undefined envelope */

/* ── the OS entry points (mos.cpp calls these) ──────────────────────────────────────────── */
void snd_reset(void);
/* OSWORD 7: an 8-byte block — channel word, amplitude word, pitch word, duration word, all
   little-endian, and only the low byte of each is significant to the MOS. */
void snd_sound(const uint8_t blk[8]);
/* OSWORD 8: a 14-byte envelope definition; byte 0 is the envelope number, 1..4. */
void snd_envelope(const uint8_t blk[14]);
/* OSBYTE 21 with X = 4 + channel. */
void snd_flush_channel(uint8_t channel);
/* One 100 Hz scheduler tick.  Call it SND_TICK_HZ times a second, whatever the framerate. */
void snd_tick(void);

/* ── the chip, for the backend ──────────────────────────────────────────────────────────── */
/* The live chip state.  Exposed as a symbol as well as through snd_chip() so a gdb probe can read
   it directly (amiga/sound.gdb) — the counters say a sound was scheduled, this says what it was. */
extern SndChip g_sndChip;
const SndChip* snd_chip(void);
/* Bumped whenever snd_tick / snd_flush_channel changes the chip state, so a backend can skip
   reprogramming its hardware on an unchanged tick (the common case: nothing is playing).
   ⭐ Both accessors are INLINE: the Amiga backend asks them in the VERTB ISR on every field. */
extern unsigned long g_sndGen;
static inline unsigned long snd_generation(void) { return g_sndGen; }
/* Non-zero while the next snd_tick() provably changes nothing (sound.c, s_quiet) — so a backend
   driving two ticks a field can skip both, and the reprogram behind them, with one test. */
extern uint8_t g_sndQuiet;
static inline int snd_quiet(void) { return g_sndQuiet; }

/* The noise generator's shift-rate divisor: 16, 32, 64, or tone 2's divider (noise mode 3, which
   is the one Revs's engine uses).  Both a tone divider and this divisor count the same clock —
   250 kHz — so one period conversion serves both, and the backend has no second constant to get
   wrong.  ⚠ Deliberately no Hz helper here: 125000/divider is a 32-bit divide, and the 68000 has
   none (CLAUDE.md).  The Amiga backend converts a divider straight to a Paula period in 16 bits. */
uint16_t snd_noise_divisor(const SndChip* c);

#ifdef __cplusplus
}
#endif

#endif /* SOUND_H */
