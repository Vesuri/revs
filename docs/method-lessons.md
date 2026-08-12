# Method lessons — how to work on a binary-only port

> ⚑ **Carried over from the *Rescue on Fractalus!* port.**  These are workflow lessons, not
> facts about hardware, and every one of them replaced a habit that had already cost time.

## Measure, don't theorise

The headless emulator + gdb loop repeatedly diagnosed timing and render bugs **precisely where
static reasoning kept failing**.  Both loops (`docs/headless-fsuae.md` for the Amiga side,
`docs/bbc-reference-loop.md` for the BBC reference) exist so that "measure" is cheaper than
"argue".  Use them.

Corollaries:

- **Measure the baseline before reasoning from it.**  A stale "~6 FPS" figure — really 14.4 —
  was the premise behind "only architectural changes matter" *for months*.  Re-measure; never
  quote an old number.
- **Bisect works great here — USE IT.**  Never assert a bug is "pre-existing" without bisecting.
  That claim was wrong three times, and right once (proven by A/B-ing a baseline build) —
  it is cheap either way, so just do it.
- **A probe that reads the same source as the code under test is vacuous.**  If a probe has
  never fired, suspect the probe first.
- ⭐ **A DROPPED probe counter does not read zero — it reads garbage that looks like data.**
  Measured in this repo while scaffolding: `-fdata-sections` + `--gc-sections` dropped
  `g_fpsFrames` in a build where nothing referenced it, gdb resolved the name into `.text`, and
  the harness reported `painted=1223110688` — m68k instruction bytes inside
  `processBlitterQueue`.  A zero would have read as "not counting"; garbage reads as a
  measurement, which is strictly worse.
  `__attribute__((used, retain))` does **not** fix it (`retain` is ignored on this target;
  `used` only binds the compiler).  The fix is a linker gc root — `PROBE_SYMS` in
  `amiga/Makefile` becomes `-Wl,--undefined=<sym>` — plus `make probe-audit`, which fails the
  link if any listed symbol is missing from the ELF.  **Add every new counter to `PROBE_SYMS`.**
  Generalisation: when a number looks wrong by orders of magnitude, check that the symbol you
  read is the symbol you meant, before theorising about the value.
- **A gdb script ABORTS THE WHOLE FILE at the first unknown symbol**, from that line onward.
  When you delete or rename a probe global, grep every `.gdb` for it — and read "the trace
  stopped after the header" as a stale script, not a dead probe.
- **Quote the DURATION, not the hit count.**  A beam-overlap counter read 29 and 46 on two runs
  of the *same binary*.

## Prove a pure REORDERING on the HOST, not on target

Compile the old and new bodies side by side over randomised inputs (160k cases, seconds).  It is
the only check that reaches **Amiga-only framework code, which `make validate` cannot see at
all**, and it rules a routine out as the cause of a visual change far faster than an emulator
round trip.

⚠ **And re-measure after:** reordering a loop for correctness cost 2× until the hot shape was
unrolled.  Byte-identical does not mean cost-identical.

## How a "structural, faithfulness-bound" ceiling actually fell

The −36% win on RoF's hot rasterizer, as a repeatable recipe:

1. **Shape-probe the algorithm's own input distribution** with dedicated counters — not a PC
   profile.  (It found that two cases covered 47.7% of all calls.)
2. **Prove the algebra on the host** over millions of randomised cases.
3. **Then** write the asm.
4. **Then** run the on-target in-process differential, A/B'd against the same C oracle.

Two generalisable questions from it:
- Does a "serial" accumulator really *have* to be serial?
- After special-casing a recursion's leaves, **re-price their parents**.

## Clean-C twin rewrite loop

The proven byte-identical loop for de-transliterating a routine: disasm-verify → splice by line
→ `make validate FN=<name>` → one commit each.  Small steps, each independently green.

Gotchas that pass `make validate` yet break at runtime — all three are now designed out of the
harness (`docs/validation-harness.md`), but know the shapes:
- a twin that takes an argument in a CPU register the fixture never varied;
- the wrong gating byte, so a whole branch was never exercised;
- a live exit register filed as an "incidental" difference.

## Register-ABI handoff between asm twins

Two adjacent asm twins can pass values in registers instead of round-tripping through `mem[]` —
but keep the **shim seam** so the differential stays valid (the C oracle must still be reachable
through the `mem[]` path), and write down the **zero-extension precondition** the callee
inherits.

## "Is this drift a bug or faithful?" — look for the ORIGINAL's own compensation table

If the original binary carries a table that *equals* the formula the hardware model implies, that
confirms the model **and** the faithfulness, from the binary alone — no emulator round trip.
Worked on RoF: GTIA anchors a wide player at its left edge, so keeping its centre fixed needs a
shift of exactly `4(s−1)`, and the game's own two tables ARE that.  So the placement was provably
exact and the residual wobble was the original's own shape data.

## Audit the ADDRESS width, then its value range

In any hand-written asm twin.  A wild write hunt on RoF ended at a `.w` address hazard.  The
method that found it: hardware watchpoints, a checksum canary, and runtime-address→symbol
lookup.

## Grep every reader before narrowing a render signal

Narrowing "what counts as dirty" is a normal optimisation, and it silently breaks the *other*
consumer you did not know about.  Survey the readers first.

## A gate that works may be the bug

If a guard "works" but the behaviour is still wrong, consider that the guard itself is
suppressing the corrective action.  (Same family as "don't cache a write-only register".)

## Read the BOUND from the instruction, never from the first example ⚑ Revs

Learned twice in one Phase 2 session, both times *after* the wrong answer looked convincing:

- The unpack's block-move tables must be read from the stub's self-copy at `$79xx`, not from
  `$12xx`, because one of the moves overwrites the tables mid-sequence.  Reading them in place gave
  a correct first move and then garbage, and reported a confident engine entry of `$919D`.
- `ModifyGameCode`'s patch count is `LDX #n` at `$5701`, and **n differs per track** — 19 for Brands
  and Oulton, 20 for Donington and Snetterton.  Hardcoding 19 (from the first track examined) made
  the other two report two bytes as unexplained, which read like a gap in the *measurement* rather
  than an off-by-one in the checker.

The general form: when a loop's trip count, table length or pointer source is *in the code*, take
it from there and `assert` the instruction shape you are relying on.  A constant copied from the
first instance is a guess that will hold long enough to be trusted.  `docs/static-map.md` has both
cases in full.

## A number that improves because the tool got looser is worse than no number ⚑ Revs

The static-coverage report had 5327 unexplained bytes.  Widening one heuristic — the look-back
window for finding what writes a zero-page pointer, from one instruction to four — took it to
**zero**.  It was wrong: the wider window matched unrelated instructions and the resolver then
fabricated a pointer target at nearly every page boundary, so every unexplained run came back
"explained".

Reverted, with the reason in the code, and the honest figure (685) is what ships.  This is the same
family as the validation harness's fixture-or-fail rule (`docs/validation-harness.md`): **when a
metric moves in the direction you want, check whether the thing being measured changed or only the
measuring.**  Loosening a classifier is indistinguishable from progress if you only read the total.

## Instrument the state, not the event, when the event has already happened

Counting executions of five menu call sites reported "none reached" while the engine was demonstrably
sitting inside one of them — the counter was installed after the call was already outstanding, and a
call-site counter cannot see a call in progress.  Reading the **6502 stack** and decoding it as
return addresses answered it immediately.

Generalise: for "where is it stuck", prefer state that persists (stack, flags, a wait variable) over
events you have to be present for.

## Record findings the moment you find them

Two conventions that exist because deferring cost real time:
- **A function whose name contradicts its behaviour** → append to `docs/rename.md` immediately.
  Do not rename piecemeal in generated files; `disasm/symbols.csv` is the source of truth and a
  batch rename via the transpiler is cheap.  **On a binary-only project the function names are
  your map**, and every wrong name taxes every later reasoning step.
- **A newly-found interrupt handler / dispatch target** → add it to
  `ghidra_scripts/entrypoints.csv` the moment you find it.  Handlers reachable only via indirect
  vectors are invisible to Ghidra's own analysis.  (See `docs/entrypoint-sweep.md`, which is the
  attempt to make this convention unnecessary by front-loading the whole sweep.)

## Housekeeping

- **No redundant waiter shells** — backgrounded tasks self-notify.
- **Run `validate` targeted** (`FN=<substr>`); a full suite run gets slow fast.
- **Judge rendering only from a real run with a wiped emulator state** — the remote debugger
  greys the display, so a headless run can prove cost and state but never appearance.
- **Screenshot pixel forensics beats eyeballing**: decode the shot into lines/pens and match it
  against a gdb memory dump.
