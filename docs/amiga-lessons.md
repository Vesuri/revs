# Amiga OCS lessons

The OCS and framework rules this port inherited and added to (copper, the pointer-swap rule,
sprites, dual playfield, write-only registers, interrupts, keyboard, vasm, include order) are
maintained in AmigaXDev `docs/reference/amiga-hardware.md`; read it before writing or changing a
copper list, a sprite or anything in the VBI. This page records how Revs applies them.

## Revs' design records

- **Copper work first in the VERTB handler.** The handler runs the game's 50 Hz body (the IRQ1V
  band cycle, ending in `tick_wheel_spin`); before the swap moved to the top, 49 of 49 presents
  landed at raster lines 46–149, inside the display window, and a rewritten `WAIT` already behind
  the beam blocked the copper until the next field (a black or green run across the horizon).
  The standing check is the beam position at the write: `amiga/beam_watch.gdb` and
  `g_beamPresentsLate`, which must read 0. A frame-boundary dump cannot see this.
- **Dual playfield.** The terrain stays double-buffered on planes 1/3 (PF1) and the cockpit is
  single-buffered on 2/4 (PF2), each an interleaved 2-plane bitmap with its own modulo; only PF1's
  two pointers are rewritten at the swap. Going from 2 to 4 lores planes cost 16.6 KB and no
  cycles. BBC pen 0 is stored as PF2 pen 3 and BBC pen 3 falls through to PF1 (the colour census of
  the region licensed this). The copper read-back requires `COLOR09 == COLOR01`,
  `COLOR10 == COLOR02`, `COLOR11 == COLOR00`, which a pen-level oracle cannot see.
- **Sprites.** Pair 0/1 pens are COLOR17–19 (`$1A2`…); HSTART is `$80 + x` for this display
  window's first pixel (measured here).
