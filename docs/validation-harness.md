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

⚠⚠ **`make clean` BEFORE `make determinism-record`, always.** The record target builds `$(TARGET)`
with whatever objects are already on disk, and `determinism-drive` / `determinism-crash` leave the
tree compiled with `STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1` — so a record run straight after one of
them **bakes the wrong trajectory into the reference**. The poisoned reference then fails every
later honest run, and it fails *consistently*: **1372 bytes, the same count from any build**, which
reads exactly like a real regression that a source-level control reproduces. It cost two rounds of
bisection across two sessions, and both times the "control also fails, so the reference is stale"
diagnosis was right for the wrong reason — the reference was not old, it was *wrong*.

⭐ **The check that distinguishes them: re-record from a clean tree, then `make clean` and run the
check again.** A sound reference passes that; a poisoned one cannot, because the second build is
not the build that made it. Repeatability across a clean rebuild is the property, not freshness.

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

## ⭐⭐ …and a TENTH, found by twins #4 and #5: a RANDOM PRE-STATE CAN BE SYSTEMATICALLY DEGENERATE

`fill_random` over the whole 64 KB is the harness's default and rule 4 of its own header — no
branch stays untested because a gating byte happened to be zero.  For a routine that CALLS other
transliterated code, that argument quietly stops holding, and it failed in three distinct ways
inside one afternoon:

1. **A random SMC byte in a callee is a HANG.**  The span plotters at `$2C00`-`$2FFF` load their
   own backward branch offsets out of the tables at `$3E50`/`$40D0` (`LDA table,X / STA <branch
   operand>`).  A random byte there can be a branch to itself: the ORACLE spins forever and the
   harness never returns.  Found by `sample`ing a hung run — case 5 of the first attempt — not by
   reading.  Fix: plant `$00` across both tables, i.e. "run the chain from the top", legal by
   construction because every target is then forward.
2. **A random SMC byte in a callee is an EXIT, and that starves the routine under test.**
   `road_edge_start` dispatches on `$231A`; a random byte traps and returns, so `horizon_extent`
   came back **0 in every one of 200 cases** and the whole horizon half of `build_track_geometry`
   — the `$4F` clamp, the second `edge_y` store — was never executed.  Two sabotages PASSED
   because of it.  Fix: plant the disassembly's own bytes at the callee subtree's sites.
3. **Uniform bytes are not boundary coverage.**  With `horizon_index` uniform over 0..255, an
   off-by-one in the `>= $28` wrap test is wrong in 1 case of 256 and PASSED 400 cases.  And
   `road_edge_start` clamps the extent to 7 whenever `horizon_index_prev` exceeds 7, which a
   uniform byte does 248 times in 256 — one cell holding the interesting paths hostage.  Fix:
   draw the boundary values explicitly, and hold the gating cell in its interesting range in a
   third of the cases.

⭐ **The tell in all three was the same, and it was not the mismatch count — it was the sabotage
pass.** 7 of 10 detected reads like a good fixture; the 3 that passed were the fixture reporting
that its cases never reached the code.  A green run plus a probe of what the pre-state actually
became (`extent=$00`, 200 times) is what turned it into a fix.

## ⭐⭐ …and an ELEVENTH, found by twins #6/#7/#8: THREE WAYS A SABOTAGE RUN LIES TO YOU

The fixtures for `apply_driving_model`, `draw_track_object` and `fill_dash_edge_columns` were green
on the first run.  Sabotaging them took four attempts to become trustworthy, and none of the four
problems was in the twins.

1. ⚠⚠ **THE BUILD.  `make build/validate_native` matched no rule at all** — the link lived inside
   the `validate` recipe — so it printed "Nothing to be done" and left the previous binary in
   place.  Fixed in the Makefile (the link is its own target now), and **that was still not
   enough**: an automated loop that rewrites one source file and immediately runs `make` got a
   stale object anyway on maybe a third of its iterations.  The fingerprint is unmistakable once
   you look for it — **two different defects reporting byte-identical mismatch counts.**  Cases
   1-3 all said "2 mismatch", 7-12 all said "0 mismatch (the control)", and the run's headline
   claim was that six of seventeen sabotages could not be detected.  A sabotage loop must
   `rm` the object file and the binary before every build.
   ⭐ The same class then produced a *phantom defect*: a "clean" 600-case run reporting 8
   mismatches, which was the previous sabotage's binary.  **Before believing either a green or a
   red, prove the binary is the source you think it is.**
2. **A CALLEE CAN LOOP FOREVER WITHOUT ANY SMC INVOLVED.**  The tenth lesson above says a random
   SMC byte in a callee is a hang; this is the same symptom with a different cause and it needed
   the same `sample` of a 12-minute run to find.  `plot_object`'s outer loop (`$2002`-`$2027`)
   repeats while `$62F3` reads 9 and `mem[$0025]` is positive — and `$62F3` is re-stored from
   `plot_shape` at the top of every pass, so with `plot_shape == 9` the pass is *identical* each
   time round.  The only exit is `FUN_202a` returning carry set, which the real shape tables
   guarantee and random bytes do not.  `$0025` has exactly one writer in the engine and it is not
   in the subtree, so nothing inside can break the loop.  Fix: the fixture excludes shape 9, and
   says why at the point of exclusion.
3. ⭐⭐ **A SELF-HEALING OUTPUT MAKES A REAL DEFECT NEARLY INVISIBLE.**  `fill_dash_edge_columns`
   runs eight column iterations that each write *one byte per scan line into the same boundary
   table*, so only the last write to a line survives — and each walk's start line is the previous
   iteration's leftover `Y`.  Sabotage the SECOND pass's start line by one and the two models
   diverge for a moment and then **reconverge**, because the next iteration's walk wraps past 256
   and overwrites the disagreement.  It survived 60 cases, needed 480 to die once, and only
   became reliable when the pre-state stopped being uniform: `dash_block_starts` holds offsets
   into an `$80`-byte block whose data ends at `$4F`, and a uniform byte there puts the stop
   offset *above* the start line 83% of the time, which makes every walk cover the same complete
   set of lines whatever line it began at.  Steering two thirds of the cases into `0..$4F` — the
   real table's range — plus 600 cases is what made a one-line error detectable.
4. **A SABOTAGE THAT CANNOT FAIL IS SOMETIMES A FACT ABOUT THE CALLEE.**  Reordering the three
   register loads before `fill_edge_column_run` passes, and correctly: the callee stows A, X and Y
   into `$42`/`$85`/`$7F` before touching any of them, and the first flag reader in the whole
   subtree sits after an `LDA` that resets N and Z.  Swapping *which register carries which value*
   fails, as it must.  Recorded in the twin's own comment rather than left as an open gap — the
   distinction from lesson 10's three passing sabotages is that this one was *proved* unobservable
   by reading the callee, not assumed.

## ⭐⭐ …and a TWELFTH, found by twins #9-#12: an EQUALITY boundary a random pre-state cannot reach

Lesson 10 was about a random pre-state being *degenerate* — every case taking the same arm.  This
is the sharper version: the arm exists and is reached, but the branch that decides it turns on an
exact equality between two values, **one of which the routine COMPUTES**.  Random bytes hit it
about once in 256 cases and only when the run gets that far, so the sabotage that tests it passes:

| twin | boundary | sabotage that passed 200 cases |
|---|---|---|
| `road_edge_walk` | `\|edge_x_hi[cursor]\| == $14`, the off-axis threshold | `$14` → `$15` |
| `road_edge_walk` | `point_dist_lo == edge_nearest_lo`, the 16-bit compare's low half | `>=` → `>` |
| `road_edge_start` | `projected_line == horizon_extent`, the horizon tie-break | reversed test |

**The fix is to PROBE THE ORACLE, then plant what came out.**  Each of these quantities is a
function of one pre-state cell that nothing else in the subtree reads, so: copy the pre-state into
`mem[]`, run the `__t6502` oracle once, read the value it produced, adjust that cell, and hand the
corrected pre-state to `diff_run` — which loads it fresh for both models anyway, so the probe costs
nothing but time.  In `tools/validate_native.c`: `steer_off_axis` shifts `car_heading_hi` by the
difference (the angle is `bearing - car_heading` and `car_heading_hi` appears nowhere else in the
subtree, so the shift is exactly linear); `steer_nearest_tie` seeds the running nearest with `$FF`
so every point beats it, then plants the minimum the walk actually found; `steer_horizon_tie` plants
the line `projected_line` came out as.  All three sabotages then fail.

⚠ **Two traps inside the fix, both hit on the way:**

1. **A steer that plants a cell the run then OVERWRITES does nothing.**  `steer_horizon_tie`'s first
   version planted `horizon_extent`/`horizon_index` and left the re-base pass on — and
   `rebase_edge_point` writes both cells itself, so the plant was gone by the time the tie-break
   read it.  Turning the pass off (`near_edge_last` = 6) was what made it bite.  Ask what else
   writes the cell between the plant and the read.
2. **Landing exactly ON the boundary can make the difference unobservable.**  Once the tie is
   forced, the tie-break's store writes the values already in the cells, so `>=` and `>` become
   identical (see `docs/faithfulness-seam.md` §9).  The steer therefore plants the index one either
   side as well, so the comparison's *direction* shows up in `mem[]`.

## ⭐⭐ …and a THIRTEENTH, found by twins #14/#15: the FIRST use of the ignore list, and what earns it

`set_ignore` has sat in `tools/validate_native.c` unused since the harness was built (`(void)`d in
`main`) precisely so that its first use would have to be argued for.  Twin #14 is it, and the
argument is worth keeping because it is the shape any future one has to match.

`bearing_to_section_from`'s sort brackets four stores in `PHP` … `PLP`.  Lesson 9's rule in
`docs/faithfulness-seam.md` — "keep the `PHP(); PLP();` wherever the oracle pushes" — is what
`road_edge_walk` did, and it works when the pair is *flag-neutral scratch*.  Here it is not: the
`PLP` is load-bearing, restoring the deciding compare's `Z` to a `BEQ` and its `C` and `V` to the
45-degree arm's exit.  The twin has to model that as data (a `d2Smaller` / `equal` pair captured
before the stores), and once it does, there is no push left to reproduce — so the oracle leaves a
status byte at `$01FF` that the twin does not.

**Three things make ignoring it legitimate rather than convenient:**

1. **The cell is provably dead.**  `S` is `$FF` on entry and `$FF` on exit, so `$01FF` is *below*
   the stack pointer when the routine returns — nothing reads it before the next push.
2. **`S` stays declared live.**  A twin that actually leaked the stack pointer still fails, so the
   ignore relaxes the residue and not the invariant.
3. **It is scoped.**  `set_ignore(bearIgnore, 1)` before the loop, `set_ignore(0, 0)` after, so it
   cannot loosen another twin's contract — which is the whole reason the mechanism is scoped.

⚠ The order in which those two rules apply matters: **reproduce the push first, and only reach for
the ignore list when the twin has had to model the flags as data.**  Reversed, the ignore list
becomes a way to skip lesson 9 rather than to finish it.

### ⚠ A sabotage that is not a defect, second instance — and this one was arithmetic

`docs/faithfulness-seam.md` §9 already records one (`road_edge_start`'s horizon tie-break).  Twin
#15's was less obvious: `mem[$6180 + divisor]` was "sabotaged" to `mem[$6200 + (divisor & $7F)]`,
and it passed 4000 cases.  It passed because **the divisor always has bit 7 set** — that is what the
normalise loop is for — so `$6180 + divisor` and `$6200 + (divisor & $7F)` are the same address for
every value the routine can produce.  The two forms are the biased and unbiased readings of
`reciprocal_table`, i.e. the sabotage restated the code.  Replaced with an off-by-one-entry index,
which fails 2223 cases.
⭐ The general check is one line of Python over the input domain, and it is cheaper than steering a
fixture at a defect that is not there: **before believing a fixture gap, evaluate the two
expressions over the range the routine can actually reach.**

## ⭐⭐ …and a FOURTEENTH, found by twin #77: the MOS CALL is an output channel too

`mem[]` was half the output (the fifth lesson above), the hardware trace covered the other half
for anything that writes the ULA, and the SMC trap covered a third channel.  **The slip/sound
cluster added a fourth: an OS CALL.**

`sound_stop_channel` (`$0E5A`) silences a MOS sound channel by flushing its buffer — `OSBYTE 21`
on buffer `X|4`.  Delete its already-idle guard so it flushes on *every* call, and it silences a
channel the game meant to leave playing.  That defect:

* writes the same `mem[]` — the guarded store puts `0` over a `0` that was already there;
* leaves the same registers — `PLA` restores A, and `OSBYTE 21` preserves X;
* **passed 1000 cases.**

So `diff_run` now records every `platform_mos_call` as `(entry, A, X, Y) AT THE CALL` and compares
the two runs' logs **as a sequence**, exactly as it already did for hardware writes.  Storage lives
in `src/platform/mos.cpp` under `REVS_HW_TRACE` — ⚠ 16 KB of BSS, which is why it is not in the
Amiga build.  It is the ENTRY AND ARGUMENTS that are compared, not what the MOS did with them:
that is `make sound`'s and `make mode7`'s question.

⚠⚠ **And the instrument alone was not enough.** With the trace in place the same sabotage was
caught in **3 of 1000 cases** — because the guard tests a byte that a random pre-state makes zero
once in 256.  A three-case margin is a coverage hole wearing a pass; the fixture now forces the
already-idle arm in half its cases (and a `[VACUOUS]` line fails if it ever stops doing so), which
took the detection to 473 of 1000.  ⭐ **The pattern: a new output channel needs a new instrument
AND a steered input.  The instrument tells you the defect is visible; only the steering makes it
LIKELY.**

⚠ Every MOS-calling twin written before this was untested in the same way — `kbd_test_key` (#57)
among them.  They pass with the trace on, but that is now a measurement rather than an assumption.

### ⚠ Three sabotages that were not defects, and the arguments that settled them

The same cluster produced three survivors, all of them provably harmless, and each argument is
written at the code rather than here (`src/gen/revs_native.c`, twins #67-#78):

* `check_wheel_slip`'s declined arm uses **absolute** operands for element 12 where every other
  access is `,X`.  That arm is reachable only with X = 0, so indexing them by the axle is the
  same code.  The 6502 was saving three bytes.
* `clamp_slip_to_grip`'s `CPX #0` at `$4B30` is **dead as a decision**: reaching it requires
  `derive_slip_reference` to have accepted with the throttle down, which only happens for X = 1.
* `sound_stop_channel`'s closing `AND #$FB` reads the **MOS's** X rather than the saved channel,
  and OSBYTE 21 preserves X — so the two are equal in the model and on a real BBC alike.

⭐ All three are the good kind of survivor: the differential's silence is the *evidence* for a
structural claim about the 6502 code.  ⚠ Distinguishing them from a coverage hole is an argument
about REACHABILITY, and the way to check the argument is to sabotage the sibling case — which is
how #25 was separated from #26.

## ⭐⭐ …and a FIFTEENTH, found by twins #79-#86: a DEFAULT-ANSWERING TEST BACKEND is a coverage hole

The harness runs against `HeadlessPlatform` (`src/platform/platform_cbridge.cpp`), whose whole
point is that it answers deterministically — `diff_run` runs the two models back to back, so
anything that MOVES between the two runs is a false failure.  Two of its answers were constants:

* `$FE68`, the User VIA T2 counter, read `$00`;
* `keyDown`, behind OSBYTE 129, was always false.

Both are deterministic and both were **wrong as a fixture input**, because the engine's two luck
tests read them: `update_engine_revs`' starter does `LDA $FE68 / AND starter_random_mask / BNE`
and its idle jitter does `AND #7`, and the whole starter arm is behind
`JSR kbd_test_key / BEQ`.  With the constants, `AND` always yields 0 (so the starter always
caught, and the jitter always added nothing) and the key was never held (so **the arm containing
the luck test was unreachable at all**).

⚠⚠ **THE TELL WAS A SURVIVING SABOTAGE, not a suspicious number.**  "the starter's luck mask is
always 7" — replacing `starter_random_mask` with the constant 7 — passed 4000 cases, and then
48000.  Nothing in the output said "this arm never ran"; the fixture reported healthy counts for
every input it was steering, because it was not steering these two.  Adding
`platform_test_via_t2` and `platform_test_key_down` and toggling both per case catches it
immediately.

⭐ **The general rule: for every branch a twin has, ask what the TEST BACKEND answers, not just
what the fixture randomises.**  A pre-state generator can only vary `mem[]` and `cpu`; anything
the routine learns through a `Platform` virtual is fixed by the backend, and a fixed answer to a
question the code asks is a whole arm that never runs.  Both hooks are test-only — the shipping
backends override `hwRead`/`keyDown` with the real models — and both default to the old constants,
so no existing fixture changed.

### ⚠ Three more sabotages that were not defects — and one of them is a property of the CURVE

* `AND #$FE` → `AND #$FF` in **both** arms of `compute_car_angles` (`$0D47`, `$0D6A`).  The masked
  value comes straight out of an `ASL` in one arm and out of `0 - (an ASL result)` in the other, so
  bit 0 is provably 0 and the mask is defensive.  ⭐ The discipline that settled it is the sibling
  check: ONE surviving mask could be a coverage hole; TWO masks that survive for the same
  structural reason are the routine's shape.
* Moving `update_engine_revs`' power-curve breakpoint at `$4A5B` by one.  **The four segments are
  CONTINUOUS at all three breakpoints** — both arms give `$BA` at the first, `$B6` at the second
  and `$A2` at the third — so a one-off breakpoint is arithmetically invisible, in the model and on
  a 6502 alike.  ⚠ This one is worth its own line because the sabotage was BADLY CHOSEN rather
  than the fixture weak: the right probes for a piecewise curve move a segment's OFFSET or SLOPE,
  and four such sabotages all fail as they should.

⭐ **So the taxonomy of a survivor now has three entries, not two**: a coverage hole (fix the
fixture), an unreachable-by-construction defect (argue it at the code), and a **defect that is not
a change at all** because the code is continuous or the bit is provably already clear.  The first
is the only one that costs anything; telling them apart is an argument, and the argument has to be
written down where the next reader will meet it.

## ⭐⭐ …and a SIXTEENTH, found by twins #98-#114: the DEFAULT ANSWER problem has a SHAPE

Twins #79-#86 found one default-answering backend hole (§FIFTEENTH).  The driving-controls group
found three more in one afternoon, and together they make the pattern nameable:

**A test backend that answers ONE value for a whole INPUT CLASS collapses every arm that
distinguishes members of that class.**

| The default | What it collapsed | The sabotage that survived |
|---|---|---|
| `keyDown()` returns the same for every key code | the fixture can produce "none down" or "ALL down", never "one down" | "the key direction is not compared with the current sign" — 5000 cases |
| `adcAxis()` returns dead centre | `adc_read`'s magnitude is pinned to 0, so its dead-zone compare and the joystick's pedal arm never run | "the dead zone is `$0B`" and "the x1.5 drops the ASL carry" |
| `$FE68` returns 0 (§FIFTEENTH) | `VIA & mask` is pinned to 0, so the starter always catches | "the luck mask is always 7" — 4000 cases |

⇒ **The test to apply to a new fixture is not "what does it randomise" but "what can the BACKEND
say".**  Every `Platform` virtual with a constant answer is an arm of the game that never runs, and
it looks exactly like a passing fixture.  `platform_test_key_only` and `platform_test_adc` exist
for that reason; both are in `src/platform/platform_cbridge.cpp` beside `platform_test_via_t2`.

⚠ **AND ITS CHEAP COUSIN: a pre-state byte that GATES a whole body.**  `gear_key_latch` has to be
0 for a gear shift to be accepted at all, and a random byte is 0 once in 256 — so the shift body
ran in 20 of 5000 cases and both of its wrap arms survived.  That is not a backend problem, just
arithmetic: **when a body is behind an equality test on a random byte, force the value.**

## ⚠⚠ A PLATFORM LAYER'S OWN STATE IS PRE-STATE TOO — the MOS VDU cursor (twins #98-#114)

`vdu_char_def`'s OSWRCH arm failed 820 of 2000 cases with the MOS-call trace byte-identical and the
screen bytes off by **exactly one character**.  The cause is the same as `cpu_unwind`'s (below) with
a different owner: **the MOS VDU driver's cursor, pending-command buffer and flash phase live in
`src/platform/teletext.cpp`, not in `mem[]`.**  The oracle ran first, advanced the cursor, and the
twin — running second on the same `mem[]` — wrote one cell along.  Both models were right.

`diff_run` now calls `tt_reset_state()` beside `cpu_unwind = 0`.  ⭐ The general rule, and it is
now two instances: **every piece of state a shared layer owns between the two runs must be reset,
and the tell is a diff that is a consistent OFFSET rather than a wrong value.**

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

## ⚠⚠ `cpu_unwind` IS PART OF THE PRE-STATE — a stale flag made a good twin fail (2026-08-18)

`cpu_unwind` (`src/cpu/cpu.h`) is the flag `$2F7E` sets when a span plotter drops its caller's
frame: the plotter's `TSX/INX/INX/TXS` returns TWO levels up, and since this model keeps return
addresses on the C stack the drop has to be a flag the caller consults (`UNWIND_TAKEN()`).

A fixture that calls a plotter DIRECTLY — `road_span_plot`'s own, 383 abandons in 2000 cases —
leaves that flag SET, because there is no arm above it to consume it.  `diff_run` reset `mem[]`
and `cpu` between models but not this, so the NEXT fixture's oracle read the stale flag, returned
one plot early, and the twin (running second, with the flag now cleared) did not.

**The tell was that the twin passed on its own and failed in the full run** — 1 case in 400, and
only when the plotter fixture had run first.  A `FN=`-filtered run also draws a different random
stream, which is what makes "passes alone, fails together" easy to misread as flakiness.

⇒ `diff_run` now clears `cpu_unwind` alongside `cpu` for both models.  The general rule: **every
global the CPU model owns is pre-state.**  If a new one appears, it belongs in that reset.

## ⚠⚠ A RELOCATED GLOBAL IS PROCESS STATE — the two models covered for each other (2026-09-05)

Every wide-value mechanism-(B) relocation moves a value out of `mem[]` into a native global
(`car_angle_16`, `car_distance_16`, `bearing_v`, `edge_nearest_v`, …), with `mem[]` kept as the
6502-ABI mirror and `marshal_in`/`marshal_out` at the shims.  `diff_run` resets `mem[]`, `cpu` and
`cpu_unwind` between the two models — **but not those globals.**

That is a hole, because many transliterated oracles tail-call the **native** shim:
`stage_nearby_car__t6502` calls `car_gap_tail()`, which marshals.  So whichever model runs second
inherits the value the first one marshalled, and a shim with its `marshal_in` **deleted** reads the
right word by accident.  The sabotage that proves the boundary rule PASSED.

⇒ `relocated_poison()` (`src/gen/revs_native.c`, declared in `revs_native_seam.h`) scribbles every
relocated global with a non-zero, per-element-distinct pattern, and `diff_run` calls it before
**each** model run.  The pattern is deliberately not zero (zero can be the correct answer) and
distinct per element (a marshal covering only some slots must still diverge).

**It immediately found three missing marshals in relocations that were already gated** —
`emit_edge_bearing_at_cursor` (`car_heading`, `bearing`), `road_edge_start` / `road_edge_walk` /
`build_track_geometry` (`car_heading`), `place_player_in_section` (`edge_nearest`).  `determinism`
could not see them either: on Silverstone every writer of those cells is native, so the stale
global happened to hold the right value.  A transliterated or circuit-hook writer would not have.

The general rule, the same one `cpu_unwind` taught: **every global either model can touch is
pre-state.**  A new relocation belongs in `relocated_poison` in the same commit that creates it,
or its boundary sabotage is untestable.

## ⚠⚠ THE GLOBAL PRNG IS ONE STREAM — a NEW fixture's `fill_random` calls shift every LATER fixture's coverage (twin #154)

`rng` (`tools/validate_native.c`) is a single global xorshift seeded ONCE (`0x9D6F1234`) and never
reset per fixture.  So a fixture's random cases are determined by how many `xs()`/`fill_random`
draws every fixture *before* it consumed — the stream position, not the fixture's own code.  Adding
a new fixture that calls `fill_random` (or changing an existing one's case count) **shifts the
stream for every fixture that runs after it**, so they each draw a *different* set of random cases.

This surfaced a latent, coverage-dependent divergence in an UNRELATED, already-green twin:
`driver_name_address` (#154) was added with a boilerplate `fill_random` loop, and `road_span_plot`
— which runs later — then failed exactly one case (`case 1376  A ref=$9E native=$25`), fully
deterministic across runs.  It had been green only because its prior stream never rolled that
input.  ⭐ `make determinism`/`-drive`/`-crash` stayed byte-identical, which is the tell that the
divergent input is a state the *game* never produces (a random-`mem[]` artifact, per §TENTH), not a
real render-path bug — but it is a real coverage hole in `road_span_plot`'s fixture, logged here for
follow-up (guard the impossible input, or reseed `rng` per fixture so coverage stops depending on
order).

⇒ **Discipline for a memory-free twin: do NOT call `fill_random`.**  A pure-arithmetic twin whose
core and oracle read no `mem[]` (e.g. `driver_name_address` reads only `X`) needs no random
pre-state; a deterministic input sweep covers it, and consuming ZERO draws keeps every downstream
fixture on the exact stream it had before — the suite stays green and the addition is truly local.
Randomising 64K you never read is not neutral; it is a stream perturbation with action at a distance.

### ✅ RESOLVED — reseed `rng` per fixture, and the `road_span_plot` hole was a real fixture bug

**The order-dependence is gone.**  `want()` now reseeds `rng` from an FNV-1a hash of the fixture
name every time a fixture is about to run (`seed_rng_for`).  Each fixture draws from a stream keyed
ONLY to its own name, so adding, removing, reordering or rescaling any fixture can no longer shift
another's coverage.  The memory-free-twin discipline above is still good hygiene, but it is no
longer load-bearing for correctness.  ⚠ Reseeding changed *every* fixture's stream at once; the full
suite was re-run green afterwards, which is the check that no fixture had been leaning on its old
stream position to stay green.

**The `road_span_plot` divergence was NOT merely an impossible-`mem[]` artifact — it was a fixture
input the game never makes, and the fixture now excludes it.**  Once the reseed made coverage
order-independent, scaling the fixture (`REVS_VALIDATE_CASES=200`) exposed the divergence at a steady
~1/20000 rate — so it was a genuine hole, not a one-off.  Root cause: `road_span_plot` parks its
incoming accumulator in `$008A` at `$2F45` and RESTORES it from `$008A` at `$2F5E` just before RTS,
so the DDA accumulator the caller threads column-to-column returns unchanged.  `plant_ram_pointers`
only masked the plot-pointer high bytes to `&0x7F` (I/O-window avoidance), which still allowed a high
byte of `$00` — a **zero-page pointer**.  A store through `(cellPtr),Y` / `(linePtr),Y` / the surface
operand could then land on `$008A` itself and clobber the parked A *before* the restore; the oracle
exited with the clobbered value while the twin echoed the input.  The engine's pointers are always
screen buffers (`$30-$5F`), never zero page, so `plant_ram_pointers` now maps every destination high
byte into `$20..$7F` — RAM, out of the I/O window, and clear of the zero-page scratch the routine
relies on (`$82/$85/$8A/$8B`).  400 000 cases per plotter: 0 mismatch, all three arms still reached.
⭐ The lesson that generalises: a fixture that feeds a plotter a *random pointer* must constrain it to
the region the engine actually uses — an out-of-domain pointer can alias the routine's own scratch and
manufacture a divergence that is neither a twin bug nor reachable in the game.

### SIXTEENTH — a relocation's marshals: every PUBLISH must first IMPORT, and a PLOTTING shim must not publish

Two failures from the `MODEL_STATE` → `model_state_16[15]` relocation, both found by the poisoned
differential and neither visible in a hand-read of the shim.

**A whole-array `marshal_out` without a preceding `marshal_in` publishes fourteen stale elements.**
The shim writes back all 15 entries whatever the core touched, so an unimported array publishes
whatever the *previous* shim left there — or, with `relocated_poison()` armed, the poison itself.
`store_slip_signed`, `store_slip_clamped`, `store_slip_clamped_off_throttle` and `check_crash` all
failed this way. The rule: **import is unconditional; publish is conditional on the closure
writing.** Never publish without importing.

**A whole-array publish is wrong in a shim that PLOTS.** `dial_needle_angle` and
`draw_dash_needles` read the vector to draw the needles, and a fixture-random plot pointer can land
a needle line *inside* $62D0..$62EE. The publish then writes the entry values back over a store the
routine genuinely made — 3 mismatches in 3000 cases, at addresses the routine had every right to
touch. Both are import-only. Generalised: **a whole-array publish belongs only to a shim that
actually writes the array**; for anything that writes through a pointer, publish nothing, or
publish only the elements the core owns.

⭐ And the marshal set is a **transitive closure, not a text search.** Four shims that never mention
the vector needed marshals because a `_core` two or three levels down nudges it
(`update_grip_limits`, `update_camera_and_drive_state`, `begin_spin`, `begin_scrape` — all via
`begin_spin_from_a_core`'s $4DD4 `SEC`/`ROR`). Derive the set from the call graph, then let the
poison prove it.

### SEVENTEENTH — a relocation's marshal closure comes from `objdump`, and it stops at the next shim

Two campaign steps in a row, a marshal closure derived by **reading the C** was wrong, and a missing
marshal is invisible to everything but a poisoned differential: MODEL_STATE needed marshals in four
shims that never mention the vector (a `_core` three levels down nudges it), and VIEW_ORIGIN's
regex-derived list matched function names inside **comments**, invented edges, and then omitted three
real shims — `make validate` failed with `road_edge_start 165/200 mismatch`.

`tools/native_closure.py` takes the closure from the compiler's own relocations on the built objects.
⭐ It must **cut at shim boundaries**: a shim reaching the global only through another shim is already
bracketed, because the inner shim marshals for itself. Without the cut it accuses `process_car_contact`
(which tail-calls `begin_scrape()` at $1C18) of a gap that does not exist.

`tools/marshal_audit.py` runs that over every relocated base and diffs it against the marshals actually
written. ⚠ It reads the marshal calls **textually**, because within a translation unit the compiler may
inline one and leave no relocation to find. It found a real gap in committed code — the `race_main_loop`
driver imported one base of five, and its core calls several `_core` functions directly, crossing no
shim. **A native DRIVER is the one shim whose closure is the whole engine; audit it explicitly.**
