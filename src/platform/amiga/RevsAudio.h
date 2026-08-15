#pragma once
/* RevsAudio — the SN76489 the MOS drives (src/platform/sound.h), re-hosted on Paula.
 *
 * ⭐ THE SPLIT.  `src/platform/sound.c` is the FAITHFUL half: the MOS's scheduler and the chip
 * state it produces, validated tick-for-tick against a real BBC (`make sound`).  This file is the
 * Amiga-only half — it never decides what should sound, only how Paula reproduces a chip state —
 * which is exactly the seam docs/faithfulness-seam.md draws.
 *
 * ── HOW A SN76489 CHANNEL BECOMES A PAULA CHANNEL ─────────────────────────────────────────
 *
 * All four voices are 8-bit waveform loops in chip RAM, played at a period that makes Paula's
 * sample rate equal the rate the real chip's output flip-flop (or noise shift register) runs at.
 * Both are counted off the same 250 kHz clock, so ONE conversion serves all of them:
 *
 *     Paula period = 3546895 / 250000 * divider = 14.1875 * divider
 *
 * and it is computed as `divider*14 + divider*3/16` — exact to 0.005% and, deliberately, all in
 * 16 bits, because the 68000 has no 32-bit multiply or divide (CLAUDE.md).
 *
 *   3 tone channels   a 2-sample square (+127/-127), so one wave cycle per two samples: the
 *                     played frequency is 125000/divider Hz, which is the chip's own tone.
 *   white noise       the chip's actual 15-bit LFSR (taps 0 and 1) rendered as one FULL period —
 *                     32766 samples — so there is no loop artefact to hear at all.
 *   periodic noise    the same register walking a single 1 through 15 stages, i.e. a 1-in-15 pulse
 *                     train.  Two periods = 30 samples (Paula LEN counts words, so it must be even).
 *
 * Attenuation is 2 dB a step on the real chip, so AUDxVOL comes from a 16-entry table of
 * 64 * 10^(-att/10), with attenuation 15 mapped to silence as the chip does.
 *
 * ── THE THREE AMIGA-ONLY DECISIONS ────────────────────────────────────────────────────────
 *
 * 1. STEREO PLACEMENT.  Paula's channels are hard-wired 0+3 left, 1+2 right, so a mono source
 *    cannot be centred without spending two channels per voice — and Revs uses four.  The BBC's
 *    two engine tones therefore take one side each (they are a genuine two-voice sound), while
 *    ⭐ the NOISE generator is MIRRORED onto the squeal channel whenever the squeal is silent.
 *    Without that, an idling engine — where the noise generator is the only voice that sounds at
 *    all, because Revs mutes channel 1 and only borrows its divider — would come out of one
 *    speaker.
 *
 * 2. THE 100 Hz TICK COMES OFF THE 50 Hz VERTB, TWO AT A TIME.  The MOS schedules sound on the
 *    System VIA's 100 Hz timer, which is not vsync, and the port has no spare timer interrupt; two
 *    ticks per field is the right average rate.  ⚠ It does quantise an envelope's 1-centisecond
 *    steps into pairs.  Revs's own envelope is a repeating 6-step pitch cycle, so the pairing
 *    changes the shape of that modulation slightly; if it is ever audible the fix is a real CIA
 *    timer at 100 Hz, not more ticks per field.
 *
 * 3. THE LED FILTER GOES OFF.  Kickstart leaves Paula's ~5 kHz low-pass on and the BBC has no
 *    such filter, so switching it off is the faithful choice (as on the Atari port).
 *
 * ⚠ DMA is left ON for all four channels for the whole run and silence is AUDxVOL = 0.  That means
 * a period or volume change is a plain register poke with no restart — the expensive case (Paula
 * only latches AUDxPTR/LEN at a loop wrap, so a waveform change needs the channel held off) is
 * then only reached when the NOISE waveform switches between white and periodic.  It matters:
 * the white buffer is 16383 words, so without a restart a switch away from it would keep hissing
 * for up to a second.
 */

/* Allocates the chip-RAM waveforms and takes Paula over.  Safe to call twice. */
void revs_audio_init(void);
void revs_audio_shutdown(void);
/* Called from the VERTB ISR, after the copper work: runs two 100 Hz scheduler ticks and applies
   whatever the chip state became.  Cheap — four channels of integer arithmetic and, in the common
   case, a handful of register writes. */
void revs_audio_vbi(void);
