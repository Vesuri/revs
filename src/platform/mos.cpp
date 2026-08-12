/* mos.cpp — the MOS (Acorn OS) call layer.
 *
 * ⭐ THE STRUCTURAL DIFFERENCE FROM THE ATARI PORT.  Rescue on Fractalus! simply *replaced*
 * the Atari OS; Revs genuinely runs under the BBC MOS and reaches the keyboard, the
 * analogue steering and the sound scheduler through OS entry points.  Those are `JSR`
 * targets into ROM, not bus addresses, so `bus.h` never sees them — the transpiler emits
 * `platform_mos_call(entry)` and this file is what services it.
 *
 * ⭐ THE SURFACE IS CLOSED, NOT OPEN-ENDED.  Phase 2 enumerated it from the disassembly
 * before a line of this was written (`docs/static-map.md` §MOS calls / `docs/bbc-hardware.md`):
 * **four entries, 17 call sites, 8 distinct OSBYTE reason codes**.  Every one of them is
 * handled below and tagged with its site address.  A call that reaches the `default:` arm
 * is therefore a *discovery*, not a gap to shrug at — it is counted in g_mosUnknown* so a
 * headless run reports it instead of silently returning a plausible zero.  (Same discipline
 * as Platform::smcUnhandled: the failure mode this project keeps paying for is a stub that
 * returns something reasonable and reads exactly like working code.)
 *
 * WHY THIS IS SHARED, NOT PER-BACKEND.  OSBYTE 129's contract (X=Y=$FF when the key is
 * held) is the 6502's ABI, identical on both backends; only *where the input comes from*
 * differs.  So the dispatch lives here once and the four genuinely platform-specific
 * answers are virtual hooks — keyDown / adcAxis / adcButtons / rdch / wrch.  Duplicating
 * the dispatch per backend would let the host and the target disagree about the OS itself,
 * which is the one thing that must not vary between them.
 */
#include "platform.h"
#include "../cpu/cpu.h"

/* ⚠ Counters, not silence.  Listed in amiga/Makefile PROBE_SYMS so --gc-sections cannot
   drop them and leave gdb printing instruction bytes as a value (docs/method-lessons.md). */
extern "C" {
volatile unsigned long g_mosUnknownCount = 0;   /* MOS calls that hit no handler */
volatile uint16_t      g_mosUnknownEntry = 0;   /* ...the last such entry ($FFF4/$FFF1/...) */
volatile uint16_t      g_mosUnknownA     = 0;   /* ...and its reason code (A) */
/* OSWORD 10 reads a MOS ROM character definition.  We have no MOS ROM and will not lift a
   font from a licensed source, so it returns zeros — glyphs come out blank.  This counter
   is how Phase 5 finds out that it matters, rather than debugging an empty screen. */
volatile unsigned long g_mosCharDefCount = 0;
}

/* --------------------------------------------------------------------------
   OSBYTE ($FFF4) — A = reason code, X/Y = parameters, X/Y = results.
   -------------------------------------------------------------------------- */
static void osbyte(void)
{
    switch (cpu.A) {

    /* 0 — read the OS version.  ⭐ NOT IN THE PHASE 2 INVENTORY, and finding it is the
       point of the unknown-call counter: the inventory's reason codes were recovered by a
       nearest-preceding-`LDA #imm` heuristic and marked [DERIVED, heuristic] for exactly
       this reason (docs/bbc-hardware.md).  A site whose A is computed rather than
       immediate is invisible to that scan, and this is one.
       Contract: X=0 asks the MOS to generate an error; X<>0 returns the version in X.
       Revs cannot want the error path from a running engine, so answer as MOS 1.20 — the
       ROM the reference loop boots (docs/bbc-reference-loop.md). */
    case 0x00:
        cpu.X = 0x01;          /* OS 1.20 */
        break;

    /* 2 — select input stream.  Site $630A (X=0, keyboard).  Returns X = previous
       setting.  Nothing in Revs reads it back; keep the register write faithful anyway. */
    case 0x02:
        cpu.X = 0x00;
        break;

    /* 4 — cursor/copy key behaviour.  Site $3856 (X=1: cursor keys act as normal keys,
       which is exactly why the game can INKEY them).  Returns X = previous. */
    case 0x04:
        cpu.X = 0x00;
        break;

    /* 21 — flush buffer X.  Sites $0E6B (sound channel buffers 4-7) and $6311 (keyboard
       buffer 0).  We have no MOS buffers: sound is scheduled by the port, and the
       keyboard is polled not buffered.  A genuine no-op, not a stub. */
    case 0x15:
        break;

    /* 126 — acknowledge ESCAPE.  Site $6349, inside console_io's line editor.  Returns
       X = $FF if an ESCAPE condition was cleared, 0 if there was none.  The port never
       raises ESCAPE, so: none. */
    case 0x7E:
        cpu.X = 0x00;
        break;

    /* 128 (ADVAL) — ⭐ THE STEERING INPUT.  Sites $168E (X=0) and $5041 (X=channel).
         X=0  → X,Y = the "last conversion / fire buttons" word.  $168E does TXA:AND #$01,
                so only fire button 1 is ever looked at.
         X=1..4 → X,Y = the 16-bit conversion for that channel.  $503F does TYA:ADC #$80,
                i.e. it uses ONLY the high byte, biased so $80 is centre. */
    case 0x80: {
        if (cpu.X == 0x00) {
            cpu.X = platform ? platform->adcButtons() : 0x00;
            cpu.Y = 0x00;
        } else {
            uint16_t v = platform ? platform->adcAxis(cpu.X) : 0x8000;
            cpu.X = (uint8_t)(v & 0xFF);
            cpu.Y = (uint8_t)(v >> 8);
        }
        break;
    }

    /* 129 (INKEY) — the input primitive, 17 callers by way of kbd_test_key ($0E50).
         Y=$FF → NEGATIVE INKEY: is the key with internal number -(256-X) held?
                 Returns X=Y=$FF when held, X=Y=$00 when not.  kbd_test_key's caller
                 tests it with CPX #$FF, so Z set means "held".
         Y=$7F → machine-type check.  Revs never issues it; answer as a BBC B (0) rather
                 than fall through to the unknown-call counter if it ever appears. */
    case 0x81:
        if (cpu.Y == 0xFF) {
            bool held = platform ? platform->keyDown(cpu.X) : false;
            cpu.X = held ? 0xFF : 0x00;
            cpu.Y = held ? 0xFF : 0x00;
        } else if (cpu.Y == 0x7F) {
            cpu.X = 0x00;              /* BBC B */
            cpu.Y = 0x00;
        } else {
            /* Timed read of a character.  Not a site Revs has; report rather than guess. */
            g_mosUnknownEntry = 0xFFF4; g_mosUnknownA = 0x81; g_mosUnknownCount++;
            cpu.X = 0x00; cpu.Y = 0xFF;   /* "timed out, no character" */
        }
        break;

    /* 154 — write the Video ULA control register ($FE20) through its OS copy at $0248.
       Site $4DF5 with X=$C4.  Keep BOTH effects: the shadow byte the MOS maintains AND
       the hardware write, because the port's own display code reads the seam at $FE20.
       (bus_write routes $FE20 to Platform::hwWrite, which is where the copper list will
       eventually pick the mode/palette up — Phase 5.) */
    case 0x9A:
        mem[0x0248] = cpu.X;
        if (platform) platform->hwWrite(0xFE20, cpu.X);
        break;

    /* 190 — read/write the uPD7002 conversion type (resolution).  Site $3879 with X=$20.
       Returns X = previous value.  Purely a property of the ADC we are substituting a
       mouse for, so there is nothing behind it to change. */
    case 0xBE:
        cpu.X = 0x00;
        break;

    default:
        /* ⚠ Not in the Phase 2 inventory.  Either the inventory is incomplete or a
           self-modified site produced a new reason code — both are findings. */
        g_mosUnknownEntry = 0xFFF4;
        g_mosUnknownA     = cpu.A;
        g_mosUnknownCount++;
        break;
    }
}

/* --------------------------------------------------------------------------
   OSWORD ($FFF1) — A = reason code, (X,Y) = a control block in mem[].
   -------------------------------------------------------------------------- */
static void osword(void)
{
    uint16_t blk = (uint16_t)cpu.X | ((uint16_t)cpu.Y << 8);

    switch (cpu.A) {

    /* 8 — define a sound ENVELOPE (14-byte block).  Site $0B70.  Revs drives the SN76489
       through the MOS scheduler, so the envelope semantics have to be reproduced rather
       than the chip registers mirrored — that is Phase 5's audio work, and deliberately
       not faked here.  The block stays in mem[] where the audio backend will read it. */
    case 0x08:
        (void)blk;
        break;

    /* 10 — read a character definition.  Site $50A7 (X=$C3, so the block is at $00C3).
       Block: byte 0 = character code (in), bytes 1..8 = the 8x8 bitmap (out).
       ⚠ We have no MOS ROM font and will not lift one from a licensed source, so this
       returns a blank glyph and COUNTS itself.  Blank text is the visible consequence;
       g_mosCharDefCount is how that gets attributed to this instead of to the renderer. */
    case 0x0A:
        for (int i = 1; i <= 8; i++) mem[(uint16_t)(blk + i)] = 0x00;
        g_mosCharDefCount++;
        break;

    default:
        g_mosUnknownEntry = 0xFFF1;
        g_mosUnknownA     = cpu.A;
        g_mosUnknownCount++;
        break;
    }
}

/* --------------------------------------------------------------------------
   The dispatcher.
   -------------------------------------------------------------------------- */
void Platform::mosCall(uint16_t entry)
{
    switch (entry) {

    case 0xFFF4:   /* OSBYTE */
        osbyte();
        break;

    case 0xFFF1:   /* OSWORD */
        osword();
        break;

    /* OSWRCH — VDU output.  Four sites: VDU 127 (delete, $3EF3), VDU 7 (bell, $633F),
       VDU 156 ($6651) and one from a variable ($50F6).  All of them belong to the MODE 7
       front end / line editor, which the port renders itself; the backend gets the byte
       and decides. */
    case 0xFFEE:
        if (platform) platform->wrch(cpu.A);
        break;

    /* OSRDCH — read a character, blocking.  One site: $6316, console_io's line editor
       (entering a driver name).  Returns A = the character, C = 0; C = 1 signals an
       ESCAPE condition.
       ⚠ Returning C=1 here is a HANG: $6319 branches to $6345, which acknowledges the
       ESCAPE and jumps straight back to the read.  The backend's rdch() therefore must
       always return a real character; the default (CR) ends the line immediately. */
    case 0xFFE0:
        cpu.A = platform ? platform->rdch() : 0x0D;
        cpu.C = 0;
        break;

    default:
        g_mosUnknownEntry = entry;
        g_mosUnknownA     = cpu.A;
        g_mosUnknownCount++;
        break;
    }
}

/* --------------------------------------------------------------------------
   Default input answers.  A backend overrides the ones it can really answer.
   -------------------------------------------------------------------------- */
bool    Platform::keyDown(uint8_t /*negInkeyX*/) { return false; }
uint8_t Platform::adcButtons()                   { return 0x00; }
/* $8000 is centre: adc_read ($503F) takes the HIGH byte and adds $80, so $80 → 0 offset. */
uint16_t Platform::adcAxis(uint8_t /*channel*/)  { return 0x8000; }
uint8_t Platform::rdch()                         { return 0x0D; }   /* CR: end of line */
void    Platform::wrch(uint8_t /*c*/)            {}
