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

## Status (2026-08-12) — step 1 and the core of step 2 done

**jsbeeb installed and working.** Vendored at `tools/jsbeeb` (git-ignored, like `tools/ghidra/`) —
`git clone https://github.com/mattgodbolt/jsbeeb.git tools/jsbeeb`, then `npm install` **under
Node ≥24.15** (the repo's own engines field; this machine's default Volta node was 22.14, so
`volta run --node 24.15.0 -- npm install` was used — no jsbeeb source changes needed). The
`tests/test-machine.js` `TestMachine` class (exported from the package, not test-only) is the
scriptable oracle: `loadDiscData`, `type`/keyDown/keyUp, `runFor`/`runUntilAddress`,
`readbyte`/`writebyte`, `debugInstruction.add()` hooks. No jsbeeb caveats hit yet — didn't need b2
for anything done so far.

**b2 not yet attempted.** `cmake` is missing on this machine (needed to build it); not blocking
because jsbeeb's `debugInstruction` hooks already give breakpoints, register/PC access and memory
peek/poke, which covers everything the b2 HTTP API was wanted for so far. Revisit only if jsbeeb
turns out to lack something (e.g. jsbeeb accuracy is ever in doubt, or a true GUI comparison is
needed).

**Driver scripts:** `tools/bbc_refloop_smoke.mjs` and `tools/bbc_refloop_track_diff.mjs` (both
committed; jsbeeb loads its ROMs relative to cwd, so run them as
`cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_refloop_smoke.mjs`). They replay the
disc by hand: `*EXEC !BOOT` → page through REVINST's instructions screens with SPACE → REVSMEN's
track menu (`PRESS 1 BRANDS HATCH 2 DONINGTON PARK 3 OULTON PARK 4 SNETTERTON 5 SILVERSTONE`) →
select a track → handle the "PRESS SPACE BAR TO CONTINUE" / "1 PRACTICE 2 COMPETITION" prompts →
watch for `debugInstruction` hitting `$1200` (REVS2's documented exec address) as the engine-entry
breakpoint.

**Finding: REVS2's own memory image is already byte-correct.** Dumping full 64 KB RAM at the
`$1200` breakpoint for Silverstone and diffing against `disasm/revs_mem.bin` gives 34392/65536
bytes differing overall, but broken down by region:

| Region | Diffs | |
|---|---|---|
| `$1200-$6FFF` (the whole 24 KB REVS2 engine) | **0** | byte-identical |
| `$70DB-$7813` (SILVER track data) | **0** | byte-identical |
| `$0000-$02FF` (zero page/stack/OS vars) | 513/768 | real MOS/BASIC state `ssd_load.py` never modelled — expected, see below |
| `$0300-$11FF` (BASIC/MOS workspace) | 1062/3840 | same reason |
| `$8000-$FFFF` (ROM shadow + OS ROM) | ~31745/32768 | **not a bug** — `revs_mem.bin` was never meant to model ROM contents |

So the part `disasm/revs_mem.bin` exists to serve — the engine code and the track data the
transpiler will actually read — is **already proven correct**. The doc's "provisional until
diffed" warning turns out to have been about zero-page/workspace bytes outside the engine's own
footprint, not about the engine image itself.

**Confirmed for all five tracks (2026-08-12).** Repeated the same `$1200` breakpoint + full-RAM
dump for Brands Hatch, Donington Park, Oulton Park and Snetterton (`bbc_refloop_track_diff.mjs`
already stops at the entry breakpoint before doing anything else, so its "before" dump doubles as
the entry-state capture), and diffed each against `make image TRACK=<name>`'s
`disasm/revs_mem.bin`: **`$1200-$6FFF` is 0 diffs and each track's own data block is 0 diffs, for
all five.** Exit criterion for step 2 is met for the code+data regions, on every track. Not yet
wired into `make` as a repeatable automated check — still a manual jsbeeb script run per track.

**Self-modifying-code inventory: done, and more precise than expected.**
`bbc_refloop_track_diff.mjs <1-5>` dumps RAM at the `$1200` breakpoint ("before") and again 1M
cycles later ("after"). A cycle-count probe (Brands Hatch) showed the `$1200-$6FFF` diff count
against "before" plateaus at exactly 5838 bytes by 500k cycles and is still 5838 at 3M — so 1M is
comfortably past whatever runs at startup and before any of the sustained-input gameplay loop, not
an arbitrary slice of drift.

But comparing each track's "after" dump straight against its own "before" dump turned out to be
the wrong comparison: **Silverstone — whose track data is passive (exec `$0000`, no hook program)
— also shows ~5800 bytes changed** in that same window. So most of that number is the engine's own
ordinary startup self-modification (common in 6502 games — working variables interleaved with
code in one load block), not track-hook patching, and it happens on every track including the one
with nothing to patch.

The "before" dumps are confirmed byte-identical across all five tracks in `$1200-$6FFF` (expected
— it's the same loaded REVS2 image, unexecuted). That makes Silverstone's "after" dump the right
baseline: **diffing each hooked track's "after" dump against Silverstone's "after" dump** isolates
exactly what that track's hook did, net of ordinary engine startup:

| Track | Total patched vs Silverstone | Shared with all 4 | Track-specific |
|---|---|---|---|
| Brands Hatch | 1872 | 1810 | 62 |
| Donington Park | 1874 | 1810 | 64 |
| Oulton Park | 1866 | 1810 | 56 |
| Snetterton | 1866 | 1810 | 56 |

**1810 addresses (`$1248-$5FC8`) are patched identically by all four expansion tracks** —
consistent with one shared `ModifyGameCode`/`CallTrackHook` routine all four call — plus a small
56-64 byte track-specific remainder each, presumably the `Hook*` family
(`HookFieldOfView`/`HookFlattenHills`/`HookJoystick`/...) or track-ID/geometry-seed parameters.
This is a genuinely useful head start for the Phase 2 entry-point sweep
(`docs/entrypoint-sweep.md`) — the shared-patch address range and the per-track deltas are exactly
the self-modifying-code inventory that doc asks for, computed instead of guessed. The dump files
live in `tmp/` (git-ignored; re-derive with the script rather than expecting them to persist).

**Not yet done:** named-milestone captures (step 3); the jsbeeb cycle-diff harness against the
port (step 4); the CRTC/ULA display-composition analyser (step 5); fixing `tools/ssd_load.py`
(nothing to fix — it's already correct for the regions that matter, on all five tracks); b2
(cmake now installed on this machine, not yet built/smoke-tested).

## The standing rule this loop exists to serve

> **Validate against the 6502 + the reference emulator — never against the dev-host backend.**

The host build here has deliberately been given **no renderer** for this reason; see
`src/platform/host/PlatformHost.h`.
