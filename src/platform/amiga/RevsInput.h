#pragma once
/* Keyboard, proportional joystick and mouse input through the original MOS ABI.
 * The engine owns keyboard/analogue selection ($05F5 bit 7). Amiga Shift+F2 and
 * Shift+F3 both answer BBC f2; the input layer selects joystick or mouse when
 * that key is polled. Shift+F8/F9 answer BBC f3/f6 (steering assistance off/on).
 * See docs/controls.md for wiring, centring and the original command table.
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

    /* OSBYTE 128: channel 1 = steering, 2 = pedals; buttons = gear trigger. */
    uint16_t axis(uint8_t channel) const;
    uint8_t  buttons() const;

    /* Cached before POTGO START so its discharge cannot look like a mouse press.
       0 = left, 1 = right, 2 = middle. */
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

    /* ⭐ OSRDCH's side of the keyboard: the next character the player TYPED, or 0 if
       nothing is queued.  Separate from keyDown() because console_io ($6300) asks
       what was typed between two polls, not what is held now — see the typing queue
       in RevsInput.cpp.  flushTyped() empties it, which is OSBYTE $15. */
    /* ⭐ Is ANY key at all held — including one this map does not carry?  The only caller is the
       circuit menu's title page, which lets any key cut the dwell short; a player reaching to
       hurry a title screen does not consult the key map first.  ⚠ Reads the RAW state, so it
       ignores the tap latch: a skip should follow the finger, not a 4-field echo of it. */
    bool anyKeyDown() const;

    uint8_t typedChar();
    void    flushTyped();

    /* VBI context: sample both controllers, then start the next pot conversion.
       Mouse deltas must be sampled every field because the counter wraps. */
    void sampleMouse();

private:
    void selectAnalogueSource(uint8_t raw) const;
    mutable bool m_mouseMode;
    mutable volatile bool m_joystickSelectHeld;
    mutable uint16_t m_joystickCentre;
    volatile uint16_t m_joystickSample;
    volatile bool m_joystickReady;
    volatile uint8_t m_mouseButtons, m_joystickButtons;
    bool m_potStarted;
    uint16_t m_joystickHistory[4];
    uint8_t m_joystickHistoryIndex;
    volatile uint16_t m_steerHalfSteps; /* 0..510 half-ADC steps, 256 = centre */
    uint8_t m_lastMouseX;
};
