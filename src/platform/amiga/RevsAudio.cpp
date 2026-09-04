/* RevsAudio — see RevsAudio.h for the model and for the three Amiga-only decisions. */
#define ECS_SPECIFIC
#include <hardware/custom.h>
#include <proto/exec.h>
#include <exec/memory.h>

#include "RevsAudio.h"
#include "framework/AmigaHardware.h"
#include "../sound.h"
#include "../probe.h"    /* PROBE_ISR_SPLIT(): the ISR audio split — probe.h §ISRSPLIT */

/* ---- Paula registers ---------------------------------------------------------
   AUD0 = $DFF0A0, AUD1 = $DFF0B0, AUD2 = $DFF0C0, AUD3 = $DFF0D0; within each:
   +0 PTR(32), +4 LEN(16), +6 PER(16), +8 VOL(16). */
static const uint32_t kAudioBase[4] = { 0xDFF0A0u, 0xDFF0B0u, 0xDFF0C0u, 0xDFF0D0u };
#define AUD_PTR(ch) (*(volatile uint32_t*)(kAudioBase[ch] + 0))
#define AUD_LEN(ch) (*(volatile uint16_t*)(kAudioBase[ch] + 4))
#define AUD_PER(ch) (*(volatile uint16_t*)(kAudioBase[ch] + 6))
#define AUD_VOL(ch) (*(volatile uint16_t*)(kAudioBase[ch] + 8))

/* Paula's shortest legal period (~28.6 kHz).  Also what a restarting channel is dropped to
   before its DMA-off window, so that "two sample periods" is ~1.1 rasterlines instead of
   milliseconds — the Atari port measured that as the difference between a 48-line and a 7-line
   wait (docs/sfx-events.md there, and its Paula DMA-restart section). */
#define kPaulaMinPer 124u

/* ---- waveforms in chip RAM ---------------------------------------------------
   ⚠ Chip RAM, not a static array: Paula's DMA cannot reach the program's own (possibly fast)
   memory, and a silent failure there is a channel that plays whatever was last at that address. */
/* ⚠ SIZED, not maximal.  The chip's white-noise LFSR has a period of 32767 samples, but 32 KB of
   chip RAM is real on a 512 KB A500 whose PROGRAM already lives in chip RAM.  Revs's only
   white-noise sound is the $0B30 block, duration 4 = 0.2 s, and the fastest noise rate here plays
   this buffer in 0.52 s — so the truncation point is never reached and there is no loop to hear.
   (A word count, because Paula LEN counts words.) */
static const unsigned kWhiteBytes    = 8190u;
static const unsigned kPeriodicBytes = 30u;     /* two 15-sample periods */

static int8_t*  s_square   = 0;   /* 2 samples */
static int8_t*  s_white    = 0;
static int8_t*  s_periodic = 0;
static int      s_ready    = 0;

extern "C" {
/* ⚠ Every one of these must be listed in amiga/Makefile PROBE_SYMS, or --gc-sections drops it and
   gdb prints instruction bytes as a value (docs/method-lessons.md). */
volatile unsigned long g_audioAllocFailed = 0;  /* chip RAM the waveforms could not get */
volatile unsigned long g_audioRestarts    = 0;  /* noise waveform switches (the costly path) */
volatile unsigned long g_audioUpdates     = 0;  /* fields in which Paula was reprogrammed */
volatile unsigned long g_audioTicks       = 0;  /* 100 Hz scheduler ticks driven from the ISR */
volatile unsigned long g_audioWaitLines   = 0;  /* rasterlines spent in restart waits, total */
}

/* attenuation 0..15 -> AUDxVOL 0..64.  The SN76489 attenuates in 2 dB steps, so this is
   64 * 10^(-att/10); attenuation 15 is the chip's "off". */
static const uint8_t kVolume[16] = { 64, 51, 40, 32, 25, 20, 16, 13, 10, 8, 6, 5, 4, 3, 3, 0 };

/* Paula period for a divider of the chip's 250 kHz counter clock: 14.1875 * divider, in 16-bit
   arithmetic only (no 32-bit multiply or divide exists on the 68000). */
static uint16_t period_for(uint16_t divider)
{
    uint32_t per;
    if (divider == 0) divider = 1024;        /* the chip reads 0 as 1024 */
    per = (uint32_t)(divider * 14u) + (uint32_t)((divider * 3u) >> 4);
    if (per < kPaulaMinPer) per = kPaulaMinPer;
    if (per > 0xFFFFu) per = 0xFFFFu;
    return (uint16_t)per;
}

/* ---- the chip's own noise generator, rendered once ---------------------------
   White: the 15-bit register shifts in bit0 ^ bit1 and the output is bit 0 (the published
   SN76489 behaviour, and what jsbeeb's soundchip.js implements).  Periodic: the same register
   with no feedback, so a single 1 walks it and the output is one sample in fifteen. */
static void build_noise(void)
{
    uint16_t lfsr = 1u << 14;
    unsigned i;
    for (i = 0; i < kWhiteBytes; i++) {
        uint16_t bit = (uint16_t)((lfsr & 1u) ^ ((lfsr >> 1) & 1u));
        s_white[i] = (lfsr & 1u) ? (int8_t)127 : (int8_t)-127;
        lfsr = (uint16_t)((lfsr >> 1) | (bit << 14));
    }
    lfsr = 1u << 14;
    for (i = 0; i < kPeriodicBytes; i++) {
        s_periodic[i] = (lfsr & 1u) ? (int8_t)127 : (int8_t)-127;
        lfsr = (uint16_t)(lfsr >> 1);
        if (lfsr == 0) lfsr = 1u << 14;
    }
}

/* ---- BBC/chip channel -> Paula channel --------------------------------------
   Paula pairs are hard-wired 0+3 LEFT, 1+2 RIGHT.  Chip channel index here (3 - BBC channel):
     chip 0 = BBC 3, the envelope/squeal voice  -> Paula 3, LEFT
     chip 1 = BBC 2, the lower engine tone      -> Paula 1, RIGHT
     chip 2 = BBC 1, the main engine tone       -> Paula 0, LEFT
     chip 3 = BBC 0, the noise generator        -> Paula 2, RIGHT (mirrored onto 3 when idle)
   So the two engine tones sit one per side, and see RevsAudio.h for the mirror. */
static const uint8_t kPaulaOf[4] = { 3, 1, 0, 2 };
#define PAULA_SQUEAL 3
#define PAULA_NOISE  2

/* What Paula currently holds, so an unchanged field costs nothing.  ⚠ PER and VOL are also the
   only way to see what the hardware got: AUDxPER/AUDxVOL are WRITE-ONLY, so a probe that read
   $DFF0A6 back would print noise and call it a measurement (docs/amiga-lessons.md).  Hence these
   two are exported and in PROBE_SYMS; amiga/sound.gdb reads them. */
static uint32_t s_curPtr[4] = { 0, 0, 0, 0 };
static uint16_t s_curLen[4] = { 0, 0, 0, 0 };
extern "C" {
volatile uint16_t g_audioPaulaPer[4] = { 0, 0, 0, 0 };
volatile uint16_t g_audioPaulaVol[4] = { 0xFFFFu, 0xFFFFu, 0xFFFFu, 0xFFFFu };
}
#define s_curPer g_audioPaulaPer
#define s_curVol g_audioPaulaVol

static uint16_t beam_line(void)
{
    /* VPOSR/VHPOSR: bit 8 of VPOSR is the high bit of the line counter. */
    uint16_t vpos = *vposrPointer;
    uint16_t vhpos = *vhposrPointer;
    return (uint16_t)(((vpos & 1u) << 8) | (vhpos >> 8));
}

/* Apply one Paula channel.  A waveform (PTR/LEN) change needs the channel held off, because Paula
   latches those only at a DMA loop wrap; a period or volume change does not. */
static void apply(uint8_t pch, uint32_t ptr, uint16_t len, uint16_t per, uint16_t vol)
{
    int restart = (ptr != s_curPtr[pch]) || (len != s_curLen[pch]);
    /* ⭐ A waveform change on a channel that is silent NOW and stays silent is DEFERRED: the
       shadow is left alone, so the restart happens on the field the channel is actually needed.
       Without this the idle noise channel restarted every single field — measured 240 restarts in
       120 updates, i.e. 14 rasterlines of busy-wait per field for something inaudible. */
    if (restart && vol == 0 && s_curVol[pch] == 0) restart = 0;
    if (!restart) {
        if (per != s_curPer[pch]) { AUD_PER(pch) = per; s_curPer[pch] = per; }
        if (vol != s_curVol[pch]) { AUD_VOL(pch) = vol; s_curVol[pch] = vol; }
        return;
    }
    /* ⭐ Drop the period to Paula's minimum FIRST: AUDxPER takes effect immediately, so the
       "two sample periods with the channel off" the hardware manual asks for becomes ~1.1
       rasterlines instead of however long the OUTGOING note was.  Without this the white-noise
       buffer's period would set the wait. */
    {
        const uint16_t mask = (uint16_t)(1u << pch);
        uint16_t start, now;
        AUD_PER(pch) = kPaulaMinPer;
        *dmaconPointer = mask;                       /* AUDxEN off (no SETCLR = clear) */
        AUD_PTR(pch) = ptr;
        AUD_LEN(pch) = len;
        AUD_PER(pch) = per;
        AUD_VOL(pch) = vol;
        start = beam_line();
        for (;;) {
            now = beam_line();
            if ((uint16_t)(now - start) >= 7u || now < start) break;   /* ~7 rasterlines */
        }
        *dmaconPointer = (uint16_t)(0x8000u | mask); /* AUDxEN on */
        g_audioWaitLines += 7;
        g_audioRestarts++;
        s_curPtr[pch] = ptr; s_curLen[pch] = len; s_curPer[pch] = per; s_curVol[pch] = vol;
    }
}

/* ⚠ Decide each Paula channel ONCE and apply it once.  The first version wrote channel 3 twice
   per field — the squeal's square wave, then the noise mirror over the top of it — and each write
   was a waveform change, so it paid two DMA restarts a field for a sound nobody could hear. */
static void program_paula(void)
{
    const SndChip* c = snd_chip();
    uint32_t ptr[4];
    uint16_t len[4], per[4], vol[4];
    uint8_t chip;

    for (chip = 0; chip < 3; chip++) {
        const uint8_t pch = kPaulaOf[chip];
        ptr[pch] = (uint32_t)s_square;
        len[pch] = 1u;
        per[pch] = period_for(c->tone[chip]);
        vol[pch] = kVolume[c->vol[chip] & 15];
    }
    {
        const int white = (c->noise & 4) != 0;
        const uint32_t nptr = (uint32_t)(white ? s_white : s_periodic);
        const uint16_t nlen = (uint16_t)((white ? kWhiteBytes : kPeriodicBytes) >> 1);
        const uint16_t nper = period_for(snd_noise_divisor(c));
        const uint16_t nvol = kVolume[c->vol[3] & 15];
        ptr[PAULA_NOISE] = nptr; len[PAULA_NOISE] = nlen;
        per[PAULA_NOISE] = nper; vol[PAULA_NOISE] = nvol;
        /* ⭐ THE MIRROR (RevsAudio.h).  While the squeal channel is silent, its Paula channel plays
           the noise generator too, so an idling engine — whose ONLY voice is this one, because Revs
           mutes both tones and merely borrows channel 1's divider — is not stuck in one speaker.
           Same buffer and period; the two DMA pointers are not phase-locked, which widens the noise
           rather than doubling it. */
        if (c->vol[0] == 15) {
            ptr[PAULA_SQUEAL] = nptr; len[PAULA_SQUEAL] = nlen;
            per[PAULA_SQUEAL] = nper; vol[PAULA_SQUEAL] = nvol;
        }
    }
    {
        uint8_t pch;
        for (pch = 0; pch < 4; pch++) apply(pch, ptr[pch], len[pch], per[pch], vol[pch]);
    }
}

void revs_audio_init(void)
{
    if (s_ready) return;
    s_square   = (int8_t*)AllocMem(2, MEMF_CHIP | MEMF_CLEAR);
    s_white    = (int8_t*)AllocMem(kWhiteBytes, MEMF_CHIP | MEMF_CLEAR);
    s_periodic = (int8_t*)AllocMem(kPeriodicBytes, MEMF_CHIP | MEMF_CLEAR);
    if (!s_square || !s_white || !s_periodic) { g_audioAllocFailed++; return; }

    s_square[0] = (int8_t)127;
    s_square[1] = (int8_t)-127;
    build_noise();
    snd_reset();

    {
        uint8_t pch;
        for (pch = 0; pch < 4; pch++) {
            AUD_PTR(pch) = (uint32_t)s_square;
            AUD_LEN(pch) = 1u;
            AUD_PER(pch) = 512u;
            AUD_VOL(pch) = 0u;
            s_curPtr[pch] = (uint32_t)s_square;
            s_curLen[pch] = 1u;
            s_curPer[pch] = 512u;
            s_curVol[pch] = 0u;
        }
    }
    /* CIA-A PRA bit 1 HIGH = LED dim = the ~5 kHz audio filter OFF.  Kickstart leaves it on and
       the BBC has no such filter. */
    *ciaapraPointer |= 0x02u;
    /* Audio DMA on for all four channels, and left on: silence is AUDxVOL = 0, which keeps every
       ordinary change a plain register poke (RevsAudio.h). */
    *dmaconPointer = (uint16_t)(0x8000u | 0x000Fu);
    s_ready = 1;
}

void revs_audio_shutdown(void)
{
    uint8_t pch;
    for (pch = 0; pch < 4; pch++) AUD_VOL(pch) = 0u;
    *dmaconPointer = 0x000Fu;      /* AUD0..3 off */
    if (s_square)   { FreeMem(s_square, 2); s_square = 0; }
    if (s_white)    { FreeMem(s_white, kWhiteBytes); s_white = 0; }
    if (s_periodic) { FreeMem(s_periodic, kPeriodicBytes); s_periodic = 0; }
    s_ready = 0;
}

void revs_audio_vbi(void)
{
    unsigned long gen;
    if (!s_ready) return;
    gen = snd_generation();
    PROBE_ISR_SPLIT(PROBE_ISR_SNDTICK);
    /* Two ticks: the MOS schedules sound at 100 Hz and this is a 50 Hz interrupt.  Paula is
       programmed once, from the state after both — see RevsAudio.h for what that quantises. */
    snd_tick();
    snd_tick();
    g_audioTicks += 2;
    if (snd_generation() != gen) {
        PROBE_ISR_SPLIT(PROBE_ISR_PAULA);
        program_paula();
        g_audioUpdates++;
    }
    PROBE_ISR_SPLIT(PROBE_ISR_AUDIO);
}
