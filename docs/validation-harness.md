# The validation harness — what it guarantees, and why

> ⭐ **Postmortem finding #3 — "where I'd change the most".**  The nastiest bug class on the
> Atari port was **passes `make validate` green, breaks at runtime**, and it bit three separate
> times.  The conclusion was not "be more careful" but *make each failure mode structurally
> impossible from twin #1*.  That is what `tools/validate_native.c` already does here, before a
> single twin exists.

## The three failures being designed out

| Failure | What happened on the Atari port | The guard here |
|---|---|---|
| **Vacuous green** | A function registered in `VALIDATE_FUNCS` but with no fixture printed `PASS` having run ZERO comparisons. | `check_coverage()` — the transpiler emits every validated name into `src/gen/revs_validate_list.h`, and a name with no registered fixture **fails the run**. |
| **Endianness** | `mem[]` aliased as `uint16_t*`/`uint32_t*` reads correct on the little-endian host and byte-swapped on the big-endian Amiga. Green host run, garbage on target. | `make endian-lint` greps for such aliases; the one legitimate case (uniform-byte broadcast) must be marked `ENDIAN-OK:`. |
| **Live exit registers** | A twin's exit register consumed by a transpiled caller is a real contract, but the harness filed register diffs as "incidental" and moved on. An exit `A=0` shipped a live bug. | No "incidental" bucket. Every fixture **declares** its live exit registers (`LIVE_NONE`/`LIVE_A`/…); declared ones are hard failures, undeclared ones are asserted dead. |

And the fourth, from the same finding: **auto-randomised inputs including the gating bytes.**
Fixtures randomise the whole 64 KB by default, so no branch stays untested because its gating
byte happened to be zero in every hand-authored case.

## ⭐⭐ …and a FIFTH, found by twin #1: `mem[]` is only half the output

**`diff_run()` also compares the sequence of BBC hardware writes.**  This was not designed in —
it was found by sabotaging the first twin (2026-08-16), and it is the one failure mode on this
page that the Atari port could not have taught, because that machine's equivalent registers were
mostly in `mem[]`.

`irq1v_band_schedule` ($4E5C) writes the Video ULA and the User VIA T1 latch about twenty times per
call and leaves **four bytes** in `mem[]`.  Nine deliberate defects were injected into the twin;
with a `mem[]`-only diff, **four of them passed 25 628 cases** — including the horizon-split
comparison off by one and a wrong band's T1 latch, i.e. defects that would change what the screen
shows on every field.  The ULA is not in `mem[]`, so nothing was looking.

How it works: `HeadlessPlatform::hwWrite` appends `(addr, val)` to `g_hwLog*`, `diff_run()` runs
the oracle, snapshots the log, runs the twin, and compares the two **as a sequence** — order is
semantic (the last write to a palette slot wins; `$FE66` closes a band record).

⚠ **The one trap, and it is the price of a fast path.**  A twin may reach the hardware model
without going through `hwWrite` — twin #1 calls `bbc_ula_palette_write()` in `bbc_screen.h`
directly, which is most of why it is 62% faster.  Such a path **must trace itself**, or the twin
is validated on the writes it did not optimise.  So the trace hook lives in that shared inline
(`BBC_HW_TRACE`, host-only via `-DREVS_HW_TRACE`), and `hwWrite` deliberately does *not* log
`$FE20`/`$FE21` because it reaches them through the same inline.  Keep those two halves in step.

⚠ It follows that **a twin must be sabotaged, not just run.**  A first-run PASS on a routine
whose output the harness cannot see is indistinguishable from a first-run PASS on a correct twin.

## ⭐ …and a SEVENTH, found by twin #2: the SMC TRAP is an output channel too

`diff_run()` also compares **how many times the routine trapped through
`platform_smc_unhandled`, and on what site and value.**  Same argument as the hardware trace: an
unhandled self-modified operand is a *reachable path* in a twin, not an error case — the twin has
to trap where the transliteration traps and unwind the same way — and it produces no `mem[]` at
all.  `view_paint_lines` ($7BE2) has nine such sites, and one of its sabotages (a planted RTS
ignored) moved the trap count from 196 to 314 while leaving plenty of `mem[]` agreement.

⭐ **Which forced a second change: `Platform::smcUnhandled` can now COUNT instead of abort**, under
`REVS_SMC_CONTINUE=1`.  Aborting on the first trap made the path untestable in-process. ⚠ The
escape hatch is only sound because the harness then **asserts the count in both directions** — zero
over the cases that are supposed to be legal (a pre-state that traps immediately compares almost
nothing, which is the vacuous-green failure mode again) and non-zero over the cases that are
supposed to be illegal. Never set it outside the harness.

⚠⚠ **And a fixture for a self-modifying routine cannot be `fill_random` alone.**  Random bytes in
an opcode slot or a patched operand are neither legal value, so both models trap on the first
instruction and agree about nothing.  `test_view_paint_lines` builds the pre-state instead: random
everywhere, legal at each site *for the reason the operand encoding gives* — which is written out
case by case above the fixture, because "I made these bytes legal" is worthless without why.

## ⭐⭐ …and an EIGHTH: a state the SHIM RESEEDS is a state no fixture can vary

`view_paint_lines`'s shim seeds `plot_ptr` and `plot_ptr2` exactly one page apart on every call
(that is what `$6700`/`$6800` mean), so **`plot_ptr2 == plot_ptr + 256` is an invariant of every one
of the 700 cases** and any code path conditioned on the two pointers differing is unreachable from
the harness.  Found the honest way, by sabotage: forcing the "one contiguous segment" fast path on
unconditionally — which is *wrong* whenever the pointers drift — passes 700/700 (2026-08-17,
`docs/perf-method.md` §one segment per line).

⚠ **Do not "fix" it by seeding the pointers in the fixture.**  The pre-state a fixture randomises is
the state the 6502 routine is ENTERED with, and this routine overwrites both pointers in its
prologue; a fixture that set them would be testing a call that cannot happen.  The gap is real and
the consequence is a rule: **when a twin adds a fast path guarded by a condition the shim fixes,
the equivalence has to be argued by construction and written next to the code**, because the green
is not evidence.  `make determinism` is the check that has teeth there — it runs the real race.

## ⭐⭐ …and a SIXTH: `make validate` cannot see a change that hits the ORACLE too

A twin is compared against a transliteration **the same transpiler generated**.  So any change to
the transpiler, to `cpu.h`, or to the memory model changes both sides identically and the
differential stays green while the whole corpus rots.  That is not a hypothetical: the
dead-flag-elimination experiment (`docs/perf-method.md`) rewrote 2783 instructions across every
routine in the image, and `make validate` had exactly one function's worth of opinion about it.

**`make determinism` is the answer, and it covers the whole corpus.**  Drive the real engine into
a race for N frames with the clock pinned (`REVS_FIXED_RNG=1`, which substitutes the deterministic
`hwMicros` fallback), dump all 64 KB, and require byte-equality against a recorded reference.

```
make determinism-record   # after a change you have already proven correct
make determinism          # the check (DET_FRAME=300 by default)
```

Sabotage-tested three ways: an unpinned clock diverges; a deliberately wrong flag-liveness rule
(dropping a LIVE `N`) does not merely diverge — it **hangs**, so the run never reaches the dump at
all; and two different configurations / two different frame depths never compare equal to each
other.  Its positive result on the flag experiment was byte-identity at frames 300 and 1500 in
both PRACTICE and COMPETITION.

### ⭐⭐ TWO trajectories, because one is not a workload

`make determinism` alone drives the engine's **idle** path: `road_speed` reads 0 at frame 300 *and*
at frame 1500, because the autorun script hands the keyboard back and a headless run has no hand on
the throttle.  `make determinism-drive` boots straight into the race and holds it — a moving car,
and a genuinely different trajectory through the same code.

```
make determinism-drive-record   # after a change you have already proven correct
make determinism-drive          # the check (DET_DRIVE_FRAME=300 by default)
```

**MEASURED, not assumed (2026-08-17):** dropping the fourth `engine_sound_update` call from
`race_main_loop`'s tail is byte-identical in the parked run at *both* depths and **FAILS** the
driving run at frame 300.  The parked car's engine note is saturated (`engine_note == its target` in
every dump), so one missing ±1 step changes nothing there.

⚠ The drive target `clean`s, builds, runs, and then rebuilds the default configuration — two full
builds per invocation.  That is deliberate: this Makefile tracks no build flag, so anything cheaper
would eventually compare a DRIVE reference against a stale default binary, which is the failure
this project has already hit twice.

## ⭐⭐ …and a NINTH: a DRIVER with no fixture, and what actually gates it

`transpile.py` has a second split set beside `VALIDATE_FUNCS`: **`NATIVE_FUNCS`**, for a native twin
whose oracle *cannot be run by this harness at all*.  Its one member is `race_main_loop` (`$16DC`),
and the reason is structural rather than effort:

- the frame body at `$1701` **always** runs at least once before any exit test, and that body is the
  whole engine — 24 subsystem calls, the MOS, the self-modifying view rasteriser.  On `fill_random`
  memory every one of them traps or diverges, so the two models would be compared on nothing;
- the only meaningful pre-state is a live race, i.e. 64 KB derived from `revs.ssd`, which cannot be
  committed;
- and even from a live snapshot the two runs are not comparable: three body calls read `$FE68`,
  whose clock advances monotonically **across** `diff_run`'s two runs.

So the names are emitted into `revs_validate_list.h` as `NATIVE_UNVALIDATED_NAMES[]` and
`tools/validate_native.c` **prints them on every run** with what does gate them.  Silence would be
the vacuous-green failure mode wearing a different hat; a declared hole is not the same thing as a
forgotten one.

### ⚠⚠ What determinism gates for that twin, and what it provably does not

Sabotage record, six deliberate defects in `race_main_loop`, against both trajectories:

| defect | parked @300 / @1500 | driving @300 |
|---|---|---|
| the raster-band re-arm (`irq_band_state++`) dropped | **FAIL** | — |
| `LDX #$17` before slot 15 changed to `$16` | **FAIL** | — |
| the tail's `engine_sound_update` dropped | PASS | **FAIL** |
| restart depth MID instead of FULL on entry | PASS | PASS |
| `text_out_via_mos` not cleared | PASS | PASS |
| `session_end_countdown` not written back | PASS | PASS |
| the resume test on `session_is_race` inverted | — | PASS |
| the crash pause (`field_countdown = $9C`) dropped | — | PASS (also at frame **6000**) |

**The last five survive because the trajectory never enters those paths.**  The pinned run does not
crash, does not end its session, does not return to the pits and prints no text — checked to frame
6000, where the crash arm is *still* unreached.  No frame depth fixes that; only a different
scenario would.  So, stated rather than implied:

> `make determinism` + `make determinism-drive` gate the main loop's **per-frame path**.  The tail's
> crash / session-end / restart / pit-return arms have **no automated gate at all** and are faithful
> by construction only.  A change to one of them must be argued against `disasm/listing.txt` and
> then seen to work — `make refloop` for the crash, a real session for the rest.

⚠ The reference is git-ignored and machine-local **on purpose**.  It is a witness that this tree
still computes what it computed an hour ago — it has no independent authority, and it must never
be re-recorded to make a failing check pass.  Ground truth for behaviour is still the BBC
(`make refloop`).

## Using it

```
make validate                 # everything
make validate FN=<substring>  # only matching tests — use this; a full run gets slow fast
make determinism              # the WHOLE-CORPUS differential (see above)
make determinism-drive        # ...and the same, with the car MOVING — run BOTH
make endian-lint              # the wide-pointer-alias grep
```

A fixture is one call:

```c
fail += test_contract("divide_16x16", divide_16x16, divide_16x16__t6502,
                      LIVE_NONE, 200000);
```

Anything with a restricted input domain, or a stack-aware leaf whose contract includes `S`,
gets its own `test_<name>()` — but it still calls `diff_run()` and still declares `liveMask`.

## The rules that are not automatable

- **0 `mem[]` mismatch is mandatory.**  Not "small", not "only scratch cells" — zero, or the
  cell is in the ignore list *with its proof written next to it*.
- **The ignore list is for PROVEN-dead cells only.**  "Proven" means you surveyed the readers
  and none runs before the next write.  Adding an address to make a test pass is how a
  faithfulness bug gets laundered into a green run.
- **Registering in `VALIDATE_FUNCS` is half the job.**  The other half is the fixture.  The
  harness now enforces this, which is why it is stated here as well.
- **`make validate` cannot see the Amiga.**  It runs on the host, so it can never test
  Amiga-only framework code, endianness, or beam timing.  For a pure *reordering* of
  Amiga-only code, prove it byte-identical on the host by compiling the old and new bodies
  side by side over randomised inputs — that is the only check that reaches code the harness
  cannot (see `docs/method-lessons.md`).
- **On-target correctness needs an on-target differential.**  For an asm twin, `make VERIFY=1
  PROBES=1` runs the asm and the C oracle back-to-back on identical state every call and
  compares.  That is also the only valid way to *price* the twin — see `docs/perf-method.md`.

## Determinism

Fixed-seed xorshift PRNG, no wall clock, no `rand()`.  A green run is reproducible and a
regression is bisectable.  Anything the harness's headless platform emulates (a MOS call a twin
makes, a timer a twin polls) must be modelled identically for the twin *and* its oracle, or the
differential is comparing two different machines.
