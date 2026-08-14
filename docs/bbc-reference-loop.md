# The BBC reference loop — ground truth (BUILD THIS FIRST)

> ⭐ **Postmortem finding #3.2, and the biggest genuinely-new piece of work in this port.**  On
> the Atari side there was `atari800` in FIFO mode: boot, poke, break, dump RAM.  It became "the
> thing that diagnosed timing and render bugs precisely where static reasoning kept failing" —
> but it was built *reactively*, once already stuck, and the rule "validate against the 6502 +
> the reference emulator, NOT the dev-host backend" was a *learned* lesson rather than a starting
> assumption.
>
> **There is no atari800 here.**  This loop has to be built, and the postmortem is explicit that
> it should be built **before** the port needs it.
>
> ⭐ **Status (2026-08-14): IT DRIVES.**  A real BBC now boots the disc, answers the front end,
> and races with a moving car, dumping both the frame buffer and the real ULA output.  See
> §Status (2026-08-14) below for the tool, the three false beliefs that had blocked it, and what
> it already settled about the 3D view's black stripes.

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

**b2 built and its HTTP API confirmed working.** Vendored at `tools/b2` (git-ignored), cloned with
submodules (`git clone --recurse-submodules`), built via `make init && cd build/d.osx && ninja`
(needed `brew install cmake ninja`, plus Xcode command line tools which were already present).
`build/d.osx` (Debug/unoptimized) *is* the debug build the HTTP API doc describes — no separate
flag needed. Ran the built `b2.app`, confirmed `GET /peek/b2/0000/+10` returns `200` with real
memory bytes and `POST .../run/b2?name=revs.ssd` (disc image as the body) returns `200`. No
headless caveat hit — this was run with a real display available, not under Xvfb, so that
limitation from the doc's original research is still unconfirmed either way. Not wired into any
script; jsbeeb remains the primary scripted oracle for everything done so far, b2 is available as
a secondary/interactive cross-check if jsbeeb's accuracy is ever in doubt.

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

**Not yet done:** named-milestone captures (step 3, deferred — there's no port yet to diff a
milestone against, so this is better done once Phase 4/6 actually need it); the jsbeeb cycle-diff
harness against the port (step 4, same reasoning); the CRTC/ULA display-composition analyser
(step 5). `tools/ssd_load.py` needs no fix — it's already correct for the regions that matter, on
all five tracks.

## ⭐⭐ Status (2026-08-14) — THE LOOP DRIVES.  A real BBC now races, and it renders.

`tools/bbc_refloop_race.mjs` boots `revs.ssd`, answers the front end, enters a Silverstone
practice session, starts the engine, engages first gear and drives with the throttle held, for as
many frames as asked.  Run it from inside `tools/jsbeeb` (jsbeeb resolves its ROMs against cwd):

```
cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_refloop_race.mjs \
    --frames=200 --drive --dump=tmp/bbcref [--track=1..5] [--wing=0..40]
```

It writes two kinds of ground truth per run: the BBC frame buffer (`$5A80+$2580`, exactly the
bytes `src/platform/bbc_screen.h` models) and **what the real 6845 + Video ULA actually put on
the screen**, as a 1024x625 PPM.  The second is the half no memory dump can give.

### What was actually wrong — three beliefs, all false

1. **"No RETURN reaches the MOS; fix key injection."**  This was `bbc_drive.mjs`'s own bisect
   verdict and it stood for two days.  `tools/bbc_probe_return.mjs` types `PRINT 1+1` at the
   BASIC prompt — where success is visible in `drainText()` — and RETURN arrives on **all three**
   injection paths (`keyDownRaw([9,4])`, `keyDown(13)`, `keyDown(keyCodes.ENTER)`).  The verdict
   had been measured through a game whose state could not be seen, where "the key did not arrive"
   and "we are not where we think we are" look identical.

2. **⭐⭐ `TestMachine` has NO VERTICAL SYNC.**  It defaults to jsbeeb's `FakeVideo`, whose
   `polltime()` is empty and which never calls `sysvia.setVBlankInt()`.  The MOS does not care —
   it runs on the System VIA's 100 Hz timer — which is exactly why booting, REVINST, the track
   menu and the whole of Revs' front end always worked, and why this hid for so long.  But the
   engine's display setup spins on `LDA #2 / BIT $FE4D / BEQ` at **`$4E11`** (System VIA IFR bit
   1 = CA1 = vsync), and that bit never arrived.  **Every "cannot reach a race" run ended in that
   two-instruction loop.**  Fitting a real `Video` (as `src/machine-session.js` already does)
   fixes it and pays twice, because a real Video also renders real pixels.

3. **The `$63F7` poke-and-jump did not skip the front end — it skipped the ANSWER.**
   `$6407` is `JSR $655A`, and when `$655A` returns execution falls into `$640A`, which is the
   **COMPETITION** chain: class, qualifying duration, then the driver-name line editor.  That is
   where all those unexplained OSRDCH hits came from.  Answering the menu properly is both more
   faithful and shorter.

### The prompt nobody had ever seen

The front end draws in MODE 4/5, so `drainText()` shows nothing and every earlier probe drove
blind.  But it prints through **one** routine — `print_message` (`$4D7E`), indexed by X, strings
via the pointer pair `$3AD0`/`$3B50`, with `$FF` end, `>= $C8` nested message, `$A0+n` = n
spaces.  Hooking that address and decoding the string yields a readable transcript, and the
transcript immediately named the blocker:

> `SELECT WING SETTINGS ] range 0 to 40` — **rear**, then **front**

`$3C50`, the session driver's own preamble, asks for both through the two-character line editor
`console_io` (`$6300`, buffer at `$0074`), validated by `$32D0` (carry clear = 0..40 in range).
Nothing reaches a race until both are answered.  It is a genuine game prompt, not a bug.

### How it drives, and why every stimulus is acknowledged

`menu_wait_key` (`$6571`) is the only menu primitive: with X = option count it polls
`menu_key_tbl` (`$39E0`) from X down to 0, where entry 0 is the **confirm** key and 1..X are the
options.  So the driver reads the key the engine is *actually* polling out of live memory and
presses that — no guessed keys, no timed pulses.  ⭐ The mapping is exact and worth reusing:

> a BBC negative-INKEY byte `b` is internal key number `255 - b`, and the internal key number is
> `(row << 4) | col` on the very matrix `jsbeeb`'s `keyDownRaw()` takes.
> `$9D -> 98 -> [col 2, row 6] = SPACE`, `$CF -> 48 -> [col 0, row 3] = '1'`.

It is asserted against jsbeeb's own `utils.BBC` table at startup, so a bad derivation fails loudly
instead of becoming a silent no-op.  The same relation gives the **complete input inventory** —
every `LDX #imm / JSR $0E50` site in the listing:

| INKEY | key | polled at | what |
|---|---|---|---|
| `$9D` | SPACE | `$1583` | amplify steering |
| `$9F` | TAB | `$16A5` | gear down |
| `$A8` | `;+` | `$15C0` | steer right |
| `$A9` | `L` | `$15B5` | steer left |
| `$AE` | `S` | `$1660` | throttle |
| `$BE` | `A` | `$166D` | brake |
| `$DC` | `T` | `$497A` | starter |
| `$EF` | `Q` | `$16AC` | gear up |
| `$FF` / `$86` | SHIFT / RIGHT | `$3263` | the abort combo (restores S from `$6B`, `JMP $63E0`) |

Every keystroke waits on the engine's own state, never a timer, because three separate silent
failures were caught exactly there:

- a digit was confirmed by "the buffer byte now holds what I sent" — but the buffer is **reused**
  between the rear and front wing prompts, so the byte was often already there and the key was
  never pressed.  `"20"` silently became `2`.  The fix is to acknowledge on `console_io`'s own
  `STA ($70),Y` at `$633C`.  The run now also checks the values the engine *stored*.
- a 0.15 s tap on `T` looked fine — the engine polled `$DC` once per frame all race — and left
  `$61` (engine-running) at 0 for 200 frames.  Hold until `$61 == $FF` instead.
- with the car parked the scene never changes, so 50 sampled frames returned **byte-identical**
  counts and a perfectly confident, useless answer.  The scan now checksums the picture and says
  `⚠ THE SCENE NEVER CHANGED` rather than reporting a number.

⚠ And one wrong sampling point: `$655A` is entered **once**, not once per frame — `$16DC` does not
return until the session ends, and the per-frame back-edge is inside it (`$1768 BMI $16EE`).
Sampling at `$6560` produced a confident "no samples", which reads as "no stripes".

### What it says about the stripes

Two results, both from a **moving** car on Silverstone practice (`--wing=20`):

**1. The band model is confirmed against real hardware.**  Hooking writes to `$FE20`/`$FE21` and
recording the raster line each landed on gives the real band schedule.  ⚠ `video.bitmapY` is a
row in a 625-row **doubled**-scanline buffer whose first displayed row is 112, so
`display line = (bitmapY - 112) / 2`.  Converted:

| measured | derived in `bbc_screen.h` | band |
|---|---|---|
| -26 (MODE 4) | -26.4 | 0, top text rows |
| 18 (MODE 5) | 18.0 | 1, sky |
| 81 | 81.1 | 2, horizon |
| 101 | 100.5 | 3, track |
| 166 | 166.1 | 4, dashboard |

All five match.  **So the stripes are not a band-phase error**, and `bbc_screen.h`'s table moves
from [DERIVED] to measured.

**2. The horizon band is essentially never left unfilled.**  ⚠⚠ The window must be band 2
**alone** — lines 81..100 — because band 1 (the sky) maps all sixteen palette entries to the same
blue, so a zero byte there is invisible, and band 3's pen-0 black is the **road**.  The old 80..99
window straddles them: line 80 by itself contributes a 16-cell run of zeros and inverts the
answer.  Over 74 sampled frames, band 2 holds **min 0, max 18, mean 4.3 zero bytes of 800, with
the longest run only 3 cells** — and a per-line map shows lines 81..101 completely zero-free, the
remaining zeros being the road's vanishing-point triangle from line 102 down.

The port shows **long** black horizontal runs there.  The real game does not.  So the port's
black horizon lines are a **port bug in the fill, not a palette or band problem** — and the
window to compare against is band 2, lines 81..100, moving-car frames only.

## ⭐⭐ What it found: the horizon stripes were an INTERRUPT-CONTRACT bug (2026-08-14)

The loop's first real job, and it paid for itself. The port's black horizontal runs above the
horizon were **`irq1v_handler` returning with A = 0**.

**The fill loop.** `--fill=81-101` attributes every frame-buffer write in the horizon band to the
routine that made it, and the answer is not in the engine at all — it is the unrolled chain in the
**runtime-assembled `$7B00-$7FFF` overlay** (`make dashcode`), PCs marching at a 17-byte stride
from `$7C11`. One element per screen CELL:

```
LDY $3000+k*$80,X   ; this cell's column source
BEQ skip            ; ⭐ zero means "same as the previous cell" — A CARRIES ACROSS
LDA #0
STA $3000+k*$80,X
LDA $6000,Y         ; translate
skip:
LDY #k*8
STA ($70),Y         ; ⭐ opcode slot: patched to RTS ($60) to END the span
```

So **A is the live pixel value threaded through the whole line.** Corrupt A mid-chain and every
later element whose source is zero stores the corrupt value — a run to the RIGHT EDGE of that
display line, which is exactly the artefact's shape.

**The corruption.** `irq1v_handler` ends `PLA / TAX / LDA $FC / RTI` (`$4F0A`): it saves only X
for itself and recovers the interrupted **A from `$FC`**, because on a real BBC the MOS's
interrupt entry does `STA $FC` before `JMP (IRQ1V)`. The port's shim called the handler directly
and **never wrote `$FC`**, which therefore held 0 forever — so every ISR return set A = 0.

**Why it looked Amiga-only.** On the host `tickVBI()` fires at a controlled point (the top of
`renderFrame`), where no foreground routine is mid-computation. On the Amiga the game body is a
*real* VERTB that preempts the main loop anywhere — including inside the fill chain. Identical
generated C, identical `mem[]`, opposite outcome. ⚠ And the first host/target comparison nearly
mis-attributed it, because the host build sat **parked** while the Amiga was moving: the autorun
only holds the throttle under `FPSCOUNT`/`PROBES`. `make STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1` now
exists so the host can run a moving car and the two are the same scene.

**The contract, measured not reasoned.** `--irq-abi` catches the CPU at the OS interrupt entry
(where A/X/Y are still the interrupted program's), reads the return address off the 6502 stack,
and compares again on arrival back there. ⚠ It **must** be filtered to interrupts whose return
address is in the engine (`$1200-$7FFF`): unfiltered it reports "A, X and Y are all clobbered",
which is true of the machine as a whole (interrupts during MOS code) and useless as a contract.
Filtered, over 2858 engine-context interrupts: **A, X and Y are preserved every single time.**
That contract is now asserted at the seam in `Platform::fireIrq1v` on **both** backends
(`g_irqClobberCount` / `g_irqClobberWhich`, in `PROBE_SYMS`), because the next violation will
look like something else entirely too.

**Verified.** Band 2 holds zero zero-bytes on 10/10 consecutive Amiga frames from the race start
(`amiga/stripes_series.gdb` + `tools/stripe_check.py`, whose ≥8-cell criterion is calibrated
against the real machine's ≤3), `g_irqClobberCount` is 0, and the host detector
(`REVS_STRIPE_WATCH=1`) reports nothing over a long moving-car run.

⬜ **Residual, and the honest remaining risk.** A single frame with a *green* (not black) fill run
has been seen since. Same carry mechanism with a non-zero A, and the register contract is held, so
the most likely cause is a fidelity gap the counter cannot see: **a 6502 IRQ is taken only between
instructions, but the Amiga's VERTB preempts transliterated C mid-statement** — e.g. between
`mem[]` being read and `cpu.Z` being assigned inside one `LDY(...)`. A real BBC cannot split that;
the port can. The two ways out are to run the body at a 6502-instruction boundary (defer it to the
main loop / spin-wait hooks, at the cost of the 50 Hz tick's regularity) or to mask the VERTB
around transliterated code. Not attempted yet — recorded so it is not re-derived.

### Still not done

Named-milestone captures (step 3) and the jsbeeb cycle-diff harness against the port's physics
(step 4).  Step 5's display-composition analyser now exists in the specific form the stripes
needed (the measured band schedule above) but is not yet a general savestate-to-composition
description.  Not yet wired into `make` as a repeatable check.

## The standing rule this loop exists to serve

> **Validate against the 6502 + the reference emulator — never against the dev-host backend.**

The host build here has deliberately been given **no renderer** for this reason; see
`src/platform/host/PlatformHost.h`.
