#pragma once
/* RevsInput — mouse + keyboard, mapped onto the two input paths the GAME has.
 *
 * ⭐ WHAT THE GAME EXPECTS, read out of the binary rather than invented.  Revs reads all
 * input through the MOS: OSBYTE 129 negative-INKEY for keys, OSBYTE 128 for the analogue
 * axes.  Which of the two it uses is one flag, $05F5:
 *
 *     $1587  BIT $05F5 / BPL $15B3    steering:        bit 7 set -> ADC channel 1, else keys
 *     $163F  BIT $05F5 / BPL $165E    throttle/brake:  bit 7 set -> ADC channel 2, else keys
 *     $1685  BIT $05F5 / BPL $16A3    gear change:     bit 7 set -> fire button,   else keys
 *
 * `engine_init` ($3850) zeroes $05F4-$05FD, so the game BOOTS IN KEYBOARD MODE, and the
 * player switches with SHIFT + a function key — the handler at $0EE5 tests SHIFT (-1),
 * scans the key table at $3DE2 and stores the paired action byte from $39D4 into
 * $05F4+X.  Decoded, that table is the documented mode switches:
 *
 *     SHIFT + f1 (-114)  ->  $05F5 = $00   keyboard
 *     SHIFT + f2 (-115)  ->  $05F5 = $C0   analogue (the mouse, here)
 *
 * So this class answers BOTH paths and lets the game choose: every key the game tests is
 * mapped, and the mouse drives ADC channel 1 with the throttle/brake keys synthesised
 * onto channel 2 and the gear keys onto the fire button.  Nothing here decides the mode.
 *
 * ⚠ The keys the game tests are the ONLY ones that do anything: the twelve negative-INKEY
 * codes below were enumerated by walking every `LDX #imm / JSR kbd_test_key ($0E50)` site
 * in the disassembly, so the table is closed, not a guess.  An unmapped code is COUNTED
 * (g_keyUnmapped), because a key that silently does nothing is indistinguishable from a
 * key that is wired to the wrong thing.
 */
#include "framework/Util.h"

class RevsInput {
public:
    /* Install the CIA-A serial-port keyboard handler (steals keyboard.device's vector and
       gives it back in shutdown).  Safe to call with multitasking forbidden. */
    bool initialize();
    void shutdown();

    /* OSBYTE 129 negative INKEY: is the key with internal number -(256-x) held? */
    bool keyDown(uint8_t x) const;

    /* OSBYTE 128.  channel 1 = steering (the mouse), 2 = throttle/brake (the mouse
       buttons and the keys), X=0 = the fire-button word (middle button, gear keys). */
    uint16_t axis(uint8_t channel) const;
    uint8_t  buttons() const;

    /* ⭐ THE MOUSE BUTTONS, and the one place that knows which register each lives in.
       0 = left, 1 = right, 2 = middle.  Active-low in hardware; true means HELD here.
       ⚠ Right and middle come from POTINP, which only reports them while the pot pins are
       INPUTS — initialize() clears POTGO once for that.  Read them without it and both
       buttons read "held" forever, which looks like a stuck throttle, not a dead read. */
    bool mouseButton(uint8_t which) const;

    /* ⭐ Press/release the Amiga key that this map pairs with a BBC negative-INKEY code,
       as if the CIA-A handler had seen it.  Used by the scripted auto-run so that an
       unattended measurement run drives the REAL input path (map included) instead of
       bypassing it — which makes every FPS run an end-to-end test of the key map, and it
       is the only way this port can verify that path without a keyboard.  Returns false if
       the code is not in the map. */
    bool pressBbcKey(uint8_t bbcCode, bool down);

    /* ⭐⭐ Release EVERY key in the map at once.  The scripted auto-run presses keys through
       pressBbcKey() and can only release the ones the game happens to ASK about while a
       release step is in force — a two-poll release step clears two codes, and any key the
       game did not poll in that window stays down forever once the script stops writing.
       Measured (2026-08-16): 'Q' survived the last release step of the straight-to-race
       script, so the engine saw a gear key held on every frame afterwards.  That kept $58
       negative ($16BD `DEC $58`, cleared each frame at $157F), which routes $49D6 into the
       IDLE arm — and THAT is why the port's engine could never stall while a real BBC
       parked in gear stalls within a second. */
    void releaseAllKeys();

    /* VBI context: accumulate the mouse counter.  Must be sampled every frame — the
       hardware counter is 8 bits and wraps, so a missed frame is a lost delta. */
    void sampleMouse();

private:
    uint8_t m_steer;        /* 0..255, $80 = centre; what channel 1 reports */
    uint8_t m_lastMouseX;
};
