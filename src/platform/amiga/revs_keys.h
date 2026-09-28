/* revs_keys.h — THE RACE'S KEY TEST, INLINE.  Amiga only; C and C++.
 *
 * kbd_test_key ($0E50) is asked ~8 times per 25 Hz sim step, and the full answer path —
 * platform_key_down -> the virtual Platform::keyDown -> PlatformAmiga::keyDown -> RevsInput::keyDown
 * with its stack frame, its SPACE instrument test and its MODE 7 tap-latch arm — cost ~50
 * instructions a poll to read one byte: 74% of read_driving_controls' 660 instructions a call,
 * single-stepped (amiga/steptrace.gdb).  This answers the common case in line and sends every
 * rare one to that path unchanged:
 *   - the MODE 7 front end, where a key that is not down may still be answered by the tap latch;
 *   - an unmapped code, which the full path counts (g_keyUnmapped);
 *   - an autorun build until its script hands over (g_keyDirect = 0), because the script's answer
 *     is pushed through RevsInput::pressBbcKey on every poll.
 * On the race path the answer is RevsInput::keyDown's exactly — the same level test on the same
 * two rawkey slots, the same latch clear on a held key — with ONE observable difference: the
 * race's own SPACE poll (the steering-amplify key, $9D) no longer ticks g_spacePolls /
 * g_spaceAnswered, which are front-end instruments (the double-press diagnosis, docs/controls.md). */
#ifndef REVS_KEYS_H
#define REVS_KEYS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t           g_keyRawMap[256][2];   /* BBC negative-INKEY code -> rawkeys, $FF = none */
extern volatile uint8_t  g_keyDown[128];        /* rawkey -> held, written by the CIA-A handler */
extern volatile uint8_t  g_keyLatch[128];       /* rawkey -> a tap the front end has not seen */
extern volatile uint16_t g_screenMode7;         /* non-zero while the MODE 7 page is up */
extern volatile uint8_t  g_keyDirect;           /* 1 = this inline test may answer */
int platform_key_down(uint8_t code);

static inline int revs_key_down(uint8_t x)
{
    if (g_keyDirect && !g_screenMode7) {
        const uint8_t k0 = g_keyRawMap[x][0], k1 = g_keyRawMap[x][1];
        if (k0 != 0xFFu) {
            if (g_keyDown[k0])                  { g_keyLatch[k0] = 0u; return 1; }
            if (k1 != 0xFFu && g_keyDown[k1])   { g_keyLatch[k1] = 0u; return 1; }
            return 0;
        }
    }
    return platform_key_down(x);
}

#ifdef __cplusplus
}
#endif

#endif
