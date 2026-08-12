# BBC Micro hardware — the abstraction boundary

> The Atari port had `docs/atari-hardware.md`: a primer plus the Atari→host→Amiga mapping, and it
> *was* the definition of what `platform.h` had to expose.  This is that file for the BBC, and it
> is deliberately marked up with what is **derived** vs what is still **assumed** — the postmortem's
> whole theme is not letting an assumption calcify into a documented fact.
>
> ⚠ Everything below marked **[ASSUMED]** is from general BBC knowledge, not from Revs's own
> disassembly or a run.  The hardware-access map (`docs/toolchain.md` §hw map) replaces each of
> them with a measured answer: *which registers does Revs actually touch, and from where.*

## The machine

BBC Micro B / B+ / Master: **6502 @ 2 MHz**, 32 KB RAM, MOS in ROM at `$C000-$FFFF`, a paged
ROM slot at `$8000-$BFFF` (BASIC lives there).  So a game's RAM is roughly `$0000-$7FFF`, and
screen memory is carved out of the top of it.

Revs's own footprint, **[DERIVED from `revs.ssd`]**:

| File | Load | Length | Ends | What |
|---|---|---|---|---|
| `!BOOT` | — | `$2C` | — | `*BASIC` / `PAGE=&1900` / `*FX21` / `CHAIN "CAR"` |
| `Car` | `$1900` | `$23DB` | `$3CDB` | BASIC — front end ("Revs / BBC Version 1 / Copyright (c) Acornsoft Limited 1985"), keys, options |
| `REVS` | `$1900` | `$700` | `$2000` | BASIC + embedded machine code; ends with `*R.Revs2` |
| **`Revs2`** | `$1200` | `$5E00` | `$7000` | **the 24 KB machine-code engine** |
| `Revs1` | `$2000` | `$5AF` | `$25AF` | loaded *inside* Revs2's range — order matters |
| `Silvers` | `$70DB` | `$739` | `$7814` | Silverstone track data |

Disc: 200 KB single-sided 80-track DFS, title "CAR", `*OPT 4,3` (`*EXEC !BOOT`).

⚠ **This image is the single-track (Silverstone) release, not the four-track expansion** the
postmortem named as the target.  Decide explicitly which release the port targets — see
`PROJECT.md` §Open decisions.

## Memory-mapped I/O — the platform boundary

The whole I/O window is the contiguous `$FC00-$FEFF`; `src/cpu/bus.h` routes it to
`Platform::hwRead`/`hwWrite` and everything else to `mem[]`.

| Range | Device | Relevance to Revs |
|---|---|---|
| `$FC00-$FCFF` | FRED (1 MHz bus) | **[ASSUMED]** unused |
| `$FD00-$FDFF` | JIM (1 MHz bus, paged) | **[ASSUMED]** unused |
| `$FE00-$FE07` | **6845 CRTC** | screen address, sizing, sync — the analogue of ANTIC's display list, but a register set rather than a program |
| `$FE08-$FE1F` | 6850 ACIA + serial ULA | **[ASSUMED]** unused |
| `$FE20-$FE2F` | **Video ULA** | `$FE20` control (mode/pixel rate/flash), `$FE21` palette — the analogue of GTIA's colour registers |
| `$FE30-$FE3F` | ROM select latch | paged ROM / shadow banking |
| `$FE40-$FE5F` | **System VIA** | keyboard, SN76489 sound, ADC start, **vsync + timer IRQs** |
| `$FE60-$FE7F` | User VIA | printer / user port |
| `$FE80-$FE9F` | 1770/8271 FDC | disc — Revs loads track data from it |
| `$FEC0-$FEDF` | **uPD7002 ADC** | **the analogue joystick — Revs's steering input** |
| `$FEE0-$FEFF` | Tube | — |

### The three that actually matter

- **Video ULA + 6845** define the screen composition.  This is the piece with **no Atari
  counterpart in the tooling**: the display-list analyser skill has to be re-tooled for
  CRTC+ULA (`docs/bbc-reference-loop.md` step 5).  Expect palette tricks and mid-frame register
  changes to be where the visual character lives, and expect them to map onto copper MOVEs.
- **System VIA** carries the 50 Hz vsync interrupt — the thing the Amiga port replaces with the
  real VERTB handler.
- **uPD7002 ADC** is the steering.  A racing sim reading an analogue axis is a *different* input
  problem from a digital joystick, and it is worth getting exactly right early: the feel of the
  game is in it.

## MOS calls — genuinely new vs the Atari port

⚠ **This is the structural difference from RoF, and it needs a design decision, not just code.**

The Atari port simply *replaced* the Atari OS: the game's own code was self-contained enough that
the OS ROM was mostly a source of shadow registers.  **Revs runs under the MOS.**  It reaches the
keyboard, the ADC, the disc and the screen through OS entry points:

| Entry | Call | Typical use |
|---|---|---|
| `$FFF4` | **OSBYTE** | keyboard scan, ADC read, cursor/flash control, and dozens more (A = reason code) |
| `$FFF1` | **OSWORD** | multi-byte requests — read line, read/write clock, disc access (A = reason code, XY = control block) |
| `$FFEE` | OSWRCH | write character (VDU) |
| `$FFE0` | OSRDCH | read character |
| `$FFDD` | OSFILE / `$FFDA` OSARGS / `$FFD7` OSBGET / `$FFD4` OSBPUT / `$FFD1` OSGBPB / `$FFCE` OSFIND | filing system |

These are **not** bus addresses — they are `JSR` targets into ROM, so `bus.h` never sees them.
They are intercepted as MOS calls in the transpiler / native layer and serviced by
`Platform::mosCall(entry)`.

**The discipline: enumerate every MOS call Revs makes, from the disassembly, BEFORE implementing
any of them.**  Same reasoning as the entry-point sweep — a MOS call you did not know about is a
behaviour you will reason about wrongly.  The list belongs in this file as it is derived.

Also relevant: `*FX21` in `!BOOT` is `OSBYTE 21` (flush a buffer) and `*FX200,3` in `Car`
disables ESCAPE and clears memory on BREAK — so the game is already using OSBYTE before the
engine starts.

## OS vectors — the interrupt seam

`$0200-$0235` holds the MOS vectors.  These stay in `mem[]`, and the platform is *notified* of
writes there (the analogue of the Atari port tracking VVBLKI/VDSLST).  The ones that make code
reachable are enumerated in `docs/entrypoint-sweep.md` §2 — **IRQ1V (`$0204`)** and **EVNTV
(`$0220`)** are the two most likely to carry Revs's own 50 Hz body.

## Screen modes

**[ASSUMED]** Revs is generally described as using a two-part display: a graphics mode for the
3D view plus a text/dashboard area, which on a BBC means **mid-frame register changes** rather
than two independent regions.  Confirm from the CRTC/ULA write trace before designing the copper
list; do not build a copper layout on this paragraph.

The Amiga side of the mapping (region splits, pointers-before-colours, band rules) is in
`docs/amiga-lessons.md`.

## What the port replaces, and with what

| BBC | Amiga |
|---|---|
| 6845 CRTC + Video ULA | copper list + bitplanes |
| Video ULA palette writes (incl. mid-frame) | copper `COLORxx` MOVEs at an end-of-previous-line WAIT |
| System VIA 50 Hz vsync IRQ | the real `INTB_VERTB` handler (vector takeover) |
| SN76489 (3 tone + 1 noise) | Paula (4 channels) |
| uPD7002 ADC steering | analogue joystick / mouse / keyboard — decide, and keep the feel |
| Keyboard via OSBYTE | CIA-A serial-port keyboard handler |
| Disc-loaded track data | embedded in the binary (see `incbin.s`) |
| MOS `$C000-$FFFF` | `Platform::mosCall` for the calls Revs actually makes |
