# The BBC reference loop — ground truth (BUILD THIS FIRST)

> ⭐ **Postmortem finding #3.2, and the biggest genuinely-new piece of work in this port.**  On
> the Atari side there was `atari800` in FIFO mode: boot, poke, break, dump RAM.  It became "the
> thing that diagnosed timing and render bugs precisely where static reasoning kept failing" —
> but it was built *reactively*, once already stuck, and the rule "validate against the 6502 +
> the reference emulator, NOT the dev-host backend" was a *learned* lesson rather than a starting
> assumption.
>
> **There is no atari800 here.**  This loop has to be built, and the postmortem is explicit that
> it should be built **before** the port needs it.  Status: **not built yet.**

## Why it is on the critical path here specifically

`tools/ssd_load.py` composes a post-load memory image from the disc — and **it is a
reconstruction, not a fact.**  An Atari `.xex` carries its own segment table, so the Atari port
could reproduce loader semantics exactly.  A DFS disc cannot.  The load *order* is now derived
from the menu's own BASIC (`*LO.<TRACK>` then `*/REVS2`), but two things are still unmodelled:
what the MOS and BASIC left resident when the engine starts, and the **runtime patches the track
file applies to the engine** — so the composed image is the *pre-patch* state.

**So the first job of this loop is: boot the real disc, break at the engine entry, dump RAM, and
diff it against `disasm/revs_mem.bin`.**  Until that diff is clean, every address derived from
that image is provisional and the port is built on sand.

## The decision (2026-07-20, after checking capabilities)

### jsbeeb — the scriptable cycle-diff ORACLE
<https://github.com/mattgodbolt/jsbeeb>

Node/headless by design, with a proven pedigree in automated cycle-accurate BBC regression
testing.  **Build the deterministic differential harness on this** — "validate the physics maths
cycle-for-cycle" is exactly its shape.  This is the analogue of the `make validate` differential,
but against a real BBC rather than against a transliteration.

### b2 — interactive accurate debugger + HTTP automation surface
<https://github.com/tom-seddon/b2> · [Debug-version.md](https://github.com/tom-seddon/b2/blob/master/doc/Debug-version.md)

The **debug build** exposes an HTTP API on `localhost:48075`, curl-driven — the direct analogue of
the atari800 FIFO boot/poke/dump loop:

| Endpoint | Use |
|---|---|
| `reset` | named config / MOS |
| `mount` / `run` | mount a disc image and run it — **matters here: Revs loads from disc** |
| `paste` | feed input |
| **`peek` / `poke`** | read and write memory ranges |

Caveats, both worth knowing before committing:
- b2 is a **GUI app with no true headless mode** — run it under Xvfb, or use the separate
  libretro core (which drops the HTTP API).
- The HTTP surface observed was peek/poke/reset/run/mount/paste.  **Breakpoints, register reads,
  single-step and cycle counts appeared to be GUI-only**, not exposed over HTTP.
  ⚠ **Re-check against a current b2 version:** if the HTTP API has since gained registers and
  breakpoints, b2 could carry BOTH roles the way atari800 did, and this whole split collapses
  into one tool.

### MAME `bbc` driver — fully-headless fallback, held in reserve
`-video none`, full Lua-scriptable breakpoint/register/step debugger.  Use it if scripted
break-and-inspect without a framebuffer is what you need.  **But its BBC accuracy is historically
less trusted than the dedicated emulators**, which matters for a physics-exact port.  Reserve;
don't lead with it.

## What to build, in order

1. **Install and smoke-test both** (`jsbeeb` under node; b2 debug build, confirm `:48075`
   responds).  Record what actually works — the caveats above are from documentation, not from a
   run on this machine.
2. **Boot `revs.ssd` to the engine entry and dump RAM.**  Diff against `disasm/revs_mem.bin` and
   fix `tools/ssd_load.py` until it matches.  ⭐ This is the gate for everything downstream.
   Two extras that come nearly free and are worth far more than the diff itself:
   - **Dump twice — before and after the track hook code runs.**  The difference IS the
     self-modifying-code inventory (`docs/entrypoint-sweep.md` §the track hooks).
   - **Dump for two different tracks and diff those.**  The difference is the per-track behaviour
     surface, i.e. exactly how much of the engine is track-dependent.
3. **Capture reference state at named milestones** (title, on the grid, a fixed lap) as files in
   the repo, so a later port behaviour can be diffed against a real BBC without re-deriving how.
4. **A cycle-diff harness on jsbeeb** for the physics core: same inputs, compare the 6502's
   state evolution against the port's.  This is the thing that makes "1:1 faithful" checkable
   for a simulation rather than for a screen.
5. **A display-composition analyser** — the analogue of the Atari port's display-list analyser
   skill, retooled for the 6845 CRTC + Video ULA (see `docs/bbc-hardware.md`).  Its job: given a
   savestate, say what the screen composition *is*, so the Amiga copper list can be generated
   from a description instead of re-derived by hand from disassembly and dumps.

## The standing rule this loop exists to serve

> **Validate against the 6502 + the reference emulator — never against the dev-host backend.**

The host build here has deliberately been given **no renderer** for this reason; see
`src/platform/host/PlatformHost.h`.
