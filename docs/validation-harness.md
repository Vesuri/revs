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

### ⭐⭐ …and FIVE in all, because a SESSION KIND is a trajectory too — and so is the WHEEL

`determinism` and `determinism-drive` are both a **practice** session, and practice is only one of
the engine's three session kinds.  `session_is_race` is `$28` in practice/qualifying and `$80` in
the race proper, and a good deal of the engine reads it: `reset_driving_variables`' opening-message
arm (`$18A5-$18BB`), `update_lap_timers`' race arm, `draw_starting_lights`, `spin_car_out`'s
race-only gate and `race_position_offset`.  Two further targets cover what the practice pair
provably cannot:

```
make determinism-crash-record / make determinism-crash   # DET_CRASH_FRAME=1500
make determinism-race-record  / make determinism-race    # DET_RACE_FRAME=13000
```

- **`determinism-crash`** drives the car off the track and lets the reset ladder run:
  `full_track_scan_rebuild` executes seven times, which is the only thing that gates that driver
  and `reset_driving_variables`' PRACTICE arm.
- **`determinism-race`** is the ONLY target that reaches `session_is_race = $80`.  It builds
  `RACEPROPER=1`, whose autorun script answers the championship menus, sits out one qualifying
  session and answers START RACE.  ⚠ Its frame depth is 13000 because ~12000 of those frames *are*
  that qualifying session, which ends on its own 4-minute clock and cannot be shortened — which is
  also why it is the one determinism target built `RELEASE=1` (at `-O0` the run does not finish in
  a tolerable time; record and check use the same flags, so the comparison is still like for like).

⚠ Neither reference is in git (`tmp/` is ignored), so **a fresh clone has to record both before
either can fail** — and a missing reference is a hard error, not a skip.

**Sabotage record (2026-09-09), `determinism-race` against `reset_driving_variables`' race arm:**
four of five defects detected with distinct counts — `lap_completed_flag` `$01`→`$00` (1 byte),
message token `$2B`→`$2A` (135), `$2C`→`$2D` (91), `pass_count_bcd` off by one (13).  The fifth,
`position_swap_flag` `$01`→`$00`, is a **no change** rather than a gap: only bit 7 of that cell is
functional and it is clear either way, and bit 0 — the one bit that differs — is consumed by the
closing `LSR` into an exit carry its single call site discards.  The decisive check was the
SIBLING (§FIFTEENTH): `lap_completed_flag`, written from the same `A` one instruction earlier, IS
seen, so the gate demonstrably reaches the arm.

**…and the other four race arms the same commit named** (`autorun.cpp` lists five routines that
were gated by nothing).  One defect each, against `determinism-race`:

| Arm | Defect | Result |
|---|---|---|
| `draw_starting_lights` | the 64-frame dwell mask `$3F` → `$1F` | 737 bytes — **gated** |
| `race_position_offset` (`$284B`) | the race gap offset off by one | 928 bytes — **gated** |
| `spin_car_out`'s race gate (`$1BD4`) | `impact2 >= $28` → `>= $40` | 352 bytes — **gated** |
| `update_lap_timers`' chequered flag (`$1027`) | token `$35` → `$36` | PASS — **NOT reached** |

⭐ The spin arm is reached because the run is **parked on the grid** and the field drives into the
player — so the contact path runs without anyone steering.
⚠⚠ **MEASURED, so nobody builds it twice:** a *driving* race proper (`RACEPROPER=1
HOLD_THROTTLE=1`, which needs no new script — the two flags are orthogonal) covers strictly
**less**.  It reaches `session_is_race = $80` and the AI field laps normally, but the held-throttle
car leaves the pack within seconds, so the same spin defect PASSES there at frame 14500, and the
player's own lap count is still **0 at frame 40000** — which is also why the chequered-flag arm is
out of reach: it needs laps-left to go NEGATIVE, i.e. a player-completed five-lap race, and no
scripted key set drives one.  That arm is a **declared hole**, not a covered one.

### ⭐⭐ …and the FIFTH: `determinism-steer`, because all four above drive in a STRAIGHT LINE

`determinism` and `determinism-crash` never work the steering path, and `determinism-drive` holds
only the *throttle*.  So at every one of their dump frames `steer_angle` (`$62A2/$62A5`) and the
car's lateral velocity (`$38/$39`) read `00/00` — and a value that is zero on both sides of a
change cannot fail a byte differential.  That left the whole steering chain, and more sharply the
`race_main_loop` phase call sites whose SHIM publishes a relocated wide value back into those
cells, gated by nothing at all.

```
make determinism-steer-record / make determinism-steer   # DET_STEER_FRAME=300
```

It is the `determinism-drive` build (`STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1`) with the run-time knob
`REVS_HOLD_STEER=l` — the autorun holds `KEY_L` beside the throttle — so it adds a trajectory
without adding a build configuration.  MEASURED at frame 300: `steer_angle` = `$0F81` and lateral
= `$0015`, where the identical build without the steering key reads `$0000` and `$0000`.

**Sabotage record (2026-09-10) — and the point of the table is the first three COLUMNS agreeing:**

| Defect | `determinism` | `-drive` | `-steer` |
|---|---|---|---|
| phase 3 calls `read_driving_controls_core` instead of the shim | PASS | PASS | **FAIL** |
| phase 4's `lateral_speed_entry_marshal_out()` deleted | PASS | PASS | **FAIL** |
| `read_driving_controls`' `car_angle_marshal_out()` deleted | PASS | PASS | **FAIL** |
| `steer_keys` forced to `$01` at `$1579` | FAIL | FAIL | **FAIL** |

The first three are the blind spot stated as a measurement rather than an argument: each one
discards a frame's steering and **is invisible to every gate that existed before this one**.  The
fourth is the control — a defect on the shared input path, which every trajectory sees — and it is
what shows the new target is not simply failing everything handed to it.

⚠ Like the other four, its reference lives in `tmp/` and a fresh clone must record it first.

### ⚠⚠ A defect must be reachable in VALUE SPACE, not just in control flow

The spin-arm defect above was first written `impact2 >= $28` → `>= $27` and it PASSED — and that
was a **bad defect**, not a coverage gap: `impact2` is `impact << 1`, so it is always EVEN and can
never equal `$27`.  The comparison was moved to a value the program cannot produce, in *any*
scenario, so the sabotage was a no-op by construction and said nothing about the gate.  Adding a
fourth question to §FIFTEENTH's three: before concluding anything from a surviving sabotage, check
that the value you moved a threshold to is **producible** — parity, a floor, a mask or a BCD
constraint can make a one-step change unreachable while looking like the smallest possible edit.

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

## ⭐⭐⭐ THE DOMAIN RULE (user-stated, and it outranks the `fill_random` default)

> **We are not here to prove that a function behaves correctly on all possible random inputs.
> It has to behave correctly on the inputs used in the actual game.**

`fill_random` is the right default for a byte the engine treats as arbitrary — that is what the
FOURTH..TENTH lessons below are about, and none of them are softened. It is the WRONG default
for a byte whose **representation is constrained**, because then the harness is asserting
behaviour on states the game cannot reach, and a twin can only satisfy that by reproducing
machinery the port exists to delete.

⚠⚠ **The tell: the harness is the only reason a twin is not written the obvious way.** That is
not a fidelity win, it is a fixture bug. It cost a whole software reimplementation of the NMOS
decimal `ADC` before it was named: the 68000's `ABCD` agrees with the 6502 on **every valid-BCD
input** and diverges only where a nibble is `$A..$F`, which Revs never stores — so randomising
those bytes was forcing a helper to emulate behaviour the game does not have, in place of one
instruction. The fix is to narrow the fixture (`rnd_bcd` / `RND_BCD_ALL`), not to widen the twin.

**How to narrow one honestly:**

1. Narrow to a **representation invariant you can state about the game** ("these cells are packed
   BCD", "this index is < 21", "this pointer is in `$3000..$4380`") — never to "the inputs that
   happen to pass".
2. Write the invariant, and where it comes from, at the fixture.
3. Say what happens if it is ever violated at runtime: **the defect is in whatever wrote the
   byte**, and that is where it gets fixed. If nothing in the engine can be pointed at as the
   writer, the invariant is not real — go back to `fill_random`.
4. ⚠ Narrowing is not licence to stop randomising the CONTROL FLOW. Every branch must still go
   both ways, and the VACUOUS checks still apply.

⭐ And re-read `docs/faithfulness-seam.md` before invoking this on a *value* range rather than a
representation: "the game only ever passes 0..7 here" is a claim about a trajectory, which is
much weaker than a claim about a format, and `determinism`/`viewdiff` are the only things that
could back it.

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
   ⚠⚠ **BUT THE TELL HAS A LOOK-ALIKE, AND CALLING IT STALENESS WASTES THE RUN: SATURATION.**  A
   count equal to the fixture's TOTAL case count is not two defects agreeing, it is every case
   failing — which is what a defect in the twin's *entry* does, and it is the honest result.  Two
   sabotages both reading `1200 of 1200` say nothing about the build.  ⭐ Discriminate on the
   **first differing byte**, not the count: staleness reproduces the whole report, saturation
   reproduces only the total.  (The counts to distrust are the small ones — `2`, `8`, `17` — where
   an identical number across unrelated defects really is one binary.)
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
(`update_grip_limits`, `update_camera_and_height`, `begin_jump`, `begin_scrape` — all via
`begin_jump_from_a_core`'s $4DD4 `SEC`/`ROR`). Derive the set from the call graph, then let the
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

### EIGHTEENTH — a fixture for a routine that DRIVES has to pin every ring the callees walk

`retire_car` ($11BE) fixtured in one pass.  Its caller `finish_race` ($1163) did not: the first run
never returned.  `sample` on the stuck process put it inside `check_car_pair`, whose walk closes only
when `car_index_inc(pos)` comes back around to `zp_scratch_index` ($03) — with `fill_random` in the
memory that index is usually **above 20**, so the ring never meets it and the routine spins forever.
`check_car_pair`'s own fixture pins `zp_scratch_index` and the twenty `car_order` entries to real
slots; a fixture for anything that *calls* it must do the same.  Generalised: **randomised memory is
only safe for a leaf.  The moment a fixture drives a subsystem, every loop bound its callees read is
an input, and an unpinned one is a hang, not a mismatch.**

⭐⭐ **And a LOOPING driver needs the bound to survive its own pass, which means pinning the
INVARIANT and not just the value.**  Copying `check_car_pair`'s pins verbatim — `car_order[i] =
xs() % 20` — fixed `finish_race`'s first pass and hung its second, and the surprise was that the
native twin's trace printed nothing for the stuck case: the ORACLE runs first in `diff_run`, so the
hang was in the transliteration, on memory the fixture itself had built.  The cause is that the pass
*ends* with `find_player_neighbours`, which searches `car_order[$13..0]` for `player_car` and parks
the position in `zp_scratch_index` — leaving **`$FF` there when the player is absent**.  Twenty
random slots usually omit the player, so the next pass's ring had no closing index.  The real
precondition is not "each entry is a valid slot" but "the array is a PERMUTATION of the twenty
slots", which is what it is in the game by construction; the fixture now shuffles `0..19`.  Once it
did, the whole fixture went from minutes-per-case to **0.2 ms** — the "pathologically slow fixture"
was never slow, it was a thousand runaway ring walks.  Two rules fall out: **when a driver's fixture
hangs, look at what the LAST call in the pass writes, not just what the first one reads**, and
**instrument the model that runs FIRST** (`t6502`), because that is the one a shared-memory hang
stops.

⚠ The related trap is a **coverage limit that reads as a passing sabotage.**  `finish_race`'s race arm
loops while any driver is still running, so a fixture can only present cases that end on their first
pass — and a defect that makes the field walk stop early ends there too, with identical `mem[]`.  Such
a defect is not "no change at all" (the THIRD explanation in §FIFTEENTH); it is a real hole, and the
honest move is to write the hole into the fixture's header and gate the loop machinery some other way
(there, a practice case two frames short of the bound, which loops exactly once — D38, 200
mismatches, against D39's 0 for the walk itself).  ⚠ Trying to close the hole by covering the walk's
two tests *separately* — one driver home by the flag but short on distance, another beyond the
distance with the flag clear — reopens the hang, because the drive pass rewrites `car_flags_shape`
itself: the flag is not stable across a pass, so such a case loops until the field happens to
retire, which on randomised memory may be never.  A coverage limit that a second construction also
cannot reach is worth *recording* rather than fighting.

### ⚠⚠ A SABOTAGE SCRIPT'S RESTORE CLOBBERS ANY EDIT MADE WHILE IT RUNS

§ELEVENTH's rule is to drive a sabotage loop from a script that copies a pristine backup over the
file for each defect and restores it at the end.  The trap on the other side of that: **editing the
same file while the script is in flight silently loses the edit** — the restore at exit puts the
pre-sabotage backup back.  Worse, an edit made *after* the script's first copy and then saved as
"the good version" preserves the SABOTAGE, not the twin.  That is how twin #198's second re-arm
went missing: the fixture's ledger was written while the instrument sabotage (D55, the re-arm
deleted) was applied, the annotated file was snapshotted as the keeper, and the next clean run hung
in the very loop D55 was built to break.

- **While a sabotage script is running, treat every file it touches as read-only.** Queue the
  write-ups and apply them after it restores.
- **A hang right after restoring "the good version" is the tell.** Verify the load-bearing lines are
  present (`grep -c` on the call the instrument needs) rather than re-reasoning about the twin.
- Sabotage scripts should also **kill their own grandchildren**: `subprocess.run(timeout=)` kills
  `make`, not the `validate_native` it spawned, so a hung defect leaves an orphan spinning at 100%
  behind every later measurement.

### NINETEENTH — a SHARED fixture leaf hides a wrong INDEX, and identical counts have a fourth explanation

`print_race_class_name` ($3C6F, twin #197) is one call: `text_script_interp(race_class + 7)`.  The
obvious fixture seeds the script machinery the way `test_text_script_interp` does — every legal
pointer-table index aimed at **one** terminating leaf — and it passed clean.  It also scored **D44
(`race_class + 8`) and D46 (`race_class` with the offset dropped) at exactly 992/2000 each**, which
§ELEVENTH names as the tell for a stale object file reused across a sabotage loop.  It was not.  With
one shared leaf, a wrong index prints **byte-identical text**, and the only thing still different is
the index itself, which `text_script_interp_core` passes on as the 6502's X: `mos_oswrch(a, tableIdx,
y)` records it, `vdu_char_def_core(a)` never sees it.  So detection was pinned to the fixture's own
`text_out_via_mos` coin flip — half the cases, and 992 was that half twice over.  Giving each index
its own leaf, signed by its first glyph, takes every wrong-index defect to **2000/2000**.

Two rules:

- **When a fixture points many table entries at the same target, it has stopped testing the
  selector.** Sign each target so the *choice* is observable in `mem[]`, not just the action.
- **Identical mismatch counts from two different defects have a FOURTH explanation** beside a stale
  build: both defects are being caught by the same *narrow* channel, and the count is that channel's
  size rather than the defects'.  Distinguish them the way this one was — a third defect whose count
  differs (D45, the class order reversed, 1305) proves the rebuilds happened, and then the shared
  number is a coverage question.  ⚠ Once the fixture is fixed the counts **saturate** instead
  (D44/D46/D47 all 2000); saturation at the case total is not staleness either.

⚠ And one sabotage shape to avoid here: making the index a **random byte** does not fail, it **hangs**
— index `> $35` walks `text_script_ptr_lo` ($3AD0) into `char_row_addr_hi` ($3B06) and aims the
emitter at zero page.  Keep every defect inside the table (0..$34); a hang is not a detection.

### ⚠⚠ `want()` RESEEDS — CALLING IT IN A LOOP CONDITION MAKES EVERY CASE THE SAME CASE

`want()` is the per-fixture entry gate *and* the per-fixture reseed (the FNV hash of the name),
so it must be called **once**, before the case loop. Put it in the loop's own condition —
`for (t = 0; want("x") && t < cases; t++)` — and the generator restarts on every iteration:
4000 cases that are one case, printed as "4000 cases, 0 mismatch".

Measured on the first hook twin, 2026-09-07: the fixture's own VACUOUS check caught it (two of
five arms read 0), which is exactly what that check is for — but only *after* six sabotages had
been "detected" by a fixture running a single input. **A sabotage result is only as strong as the
coverage line printed next to it**; re-run the sabotages after any fixture fix that changes
coverage.

⚠ And when scripting a sabotage loop, grep for **`REG DIFF` as well as `MEM DIFF`**. A twin whose
only defect is an exit FLAG prints nothing matching `MEM DIFF`, and the loop reports it as
surviving — a fake gap that costs an investigation.


### TWENTIETH — a surviving sabotage filed as "the value is dead" was a FIXTURE GAP all along

`view_paint_lines_core`'s third parameter is the 6502's entry Y, the cell index the rasteriser
starts from. A fixture sabotage forcing it to 0 changed **nothing** over 700 cases, and that was
written at the code as the argument that the seed is dead — the first chain overwrites it before
any read.

It is not dead. Dropping the parameter and seeding 0 **FAILS `make determinism`** on the parked
trajectory; it PASSES `make determinism-drive`. The 700 fixture cases genuinely do overwrite the
seed before reading it, so the fixture cannot see the value at all, and the one trajectory that
can is the one where the car is not moving.

Two rules come out of it, and they sharpen §FIFTEENTH's three explanations for a surviving
sabotage:

- ⚠⚠ **"Unreachable by construction" has to be argued from the CODE, never from the fixture's
  silence.** A fixture sabotage that changes nothing is evidence about the *fixture*. The three
  explanations are a menu you choose from with an argument — reaching for the second one because
  the run came back green is just the first one (a fixture gap) wearing its clothes.
- ⚠ **A parameter is not proven dead until a determinism run says so.** The fixture compares one
  routine on synthetic memory; `determinism` compares the whole corpus on a trajectory the game
  actually takes. For an ambient register threaded in from a driver, only the second has the real
  entry value. And it takes BOTH parked and moving: this one is invisible to `-drive`.

## ⚠⚠ A CONTROL THE INSTRUMENT ERASES IS NOT A CONTROL (the `view_paint_lines` live-mask probe)

Narrowing a fixture's live mask is a claim about the *callers*, so it is settled empirically as
well as by hand: poison the registers at the shim's exit and see whether anything downstream
notices. The probe needs a positive control — a deliberate defect the gate **must** catch — and
the first one chosen, `mem[0x6700] ^= 0xFF` right after the paint, was wrong in a way that only
one of the two gates exposed:

* `make determinism` **FAILED** it. 64 KB compared at frame 300, so the flip's downstream effects
  are visible.
* `make viewdiff` **PASSED** it. The gate compares a *screen dump* at frame 60, and the frame
  repaints that cell before the dump — the sabotage was undone by the very pass under test.

A control that fails one gate and passes another is not "mostly fine": for the gate it passes it
proves nothing, and the poisoned run through that gate is vacuous. The fix is a control the pass
**cannot** repair, which means changing an **input**, not a result — here `view_paint_lines_core`
called with a wrong first scan line (`$50` for `$4F`), which fails both gates. ⚠ Verify a control
per gate, not once: `cmp` the two dumps directly before trusting a `viewdiff` PASS.

### ...and the eraser can be the TIME AXIS, not just a repaint (the `arg_a` audit)

The same trap has a second shape, and it caught two controls in a row on the `arg_a` class. Both
determinism targets dump **at one frame** — 300, or 1500 for the crash ladder — so any defect whose
observable is *transient* survives its own control:

* a wrong entry cell handed to the **one-time prologue paint** passed `determinism`: 300 frames of
  repaint erase it.
* deleting the crash hold's `LDA #$9C` passed `determinism-crash`: `mem[$FC]` differs only during
  the ~100-field hold, and the 1000+ frames after it rewrite that byte before the dump. Its
  sibling in `plot_line_at_row` (`LDA #6`) *does* fail, because that one is on the **per-frame**
  path, so `$FC` carries it at every frame boundary.

And a third eraser, independent of this control: the **WINDOW IN SPACE**. `viewdiff` compares only
display lines 82..166 while *printing* diffs from the whole frame, and the port carries ~119
permanent ungated residual bytes in the text/sky and dash bands. So a `viewdiff` PASS is evidence
about lines 82..166 and nothing else, and **a gate that prints a difference and still passes is
still a pass — read the gated count, never the listing.**

### ⚠⚠ ...and the truncated-listing trap: `tail` on one run is not a baseline for another

Reading this very control, I concluded it "moved real pixels at text/sky lines 10..17" and wrote
that into two docs. It did not. I had captured the baseline run with `tail -N` and the control run
in full, so the baseline's own lines 10..17 were simply off the top of my window, and I read
pre-existing residuals as a new defect. Re-run properly, control and baseline are byte-identical:
the same 119 count and the same 15 detail lines.

⭐ **Compare the SUMMARY COUNT first — it covers the whole frame, gated or not — and only then the
per-line listing; and never diff two runs captured with different amounts of output.** The count
is what would have caught this instantly: 119 both times means nothing anywhere moved.

⭐ **So ask where in TIME and in SPACE the defect is observable, and compare that to where the gate
samples.**
A one-shot prologue effect and a mid-run transient are both invisible to an end-of-run dump, and
the surviving control says nothing about the change — it says the gate is the wrong instrument.
When no gate can see a byte the 6502 provably wrote, the faithful move is to KEEP reproducing it
and to write the argument at the code, because a run will never report its loss.

### ⚠⚠ ...and a PASS OVER A WINDOW TOO SHORT TO MEET THE CASE, and SIBLING ORACLES THAT SCOPE DIFFERENTLY (`LOWFULLCHECK`, 2026-09-26)
`LOWFULLCHECK` was recorded clean at the scan-in-asm commit over 2500 fields and was found failing
three days later (38 of ~214 000, always display line 133 cell 0). A `git bisect` named the next
commit — and was wrong: that build merely moved the trajectory (no `FIXED_RNG`), and **the recorded
PASS itself failed over 5000 fields**. The cause was the oracle's SCOPE: its two siblings over the
same rows, `TERRAINCHECK` and `DUALPFCHECK`, both exclude the tyre-tread footprint because
`revs_tyres_outline` masks PF1 there after every paint, and this one did not, so it compared last
paint's masked byte against this paint's unmasked one whenever a line's entry colour changed.
⇒ **Two rules.** (1) A pass is a pass *over its window*: run a new oracle for at least twice the
window that first reached every arm it guards, and bisect only with the trajectory pinned (or a
window long enough that both ends meet the case). (2) **When oracles share rows, their scope
exclusions must be shared too** — an exclusion added to one for a reason that is about the ROWS
(a post-pass that rewrites them) belongs in every oracle over those rows, with the reason at each.
The first-mismatch probe that settled it: break on the mismatch counter's increment and dump the
oracle's three buffers plus its run tables raw, then analyse off-target (⚠ in gdb on m68k `$a0` and
`$a1` ARE the address registers — a convenience variable of that name silently reads the register).

## ⚠⚠ A DIFFERENTIAL IS BLIND TO WHAT ITS TWO SIDES SHARE (the decode rewrite, 2026-09-13)

`make DIRTYCHECK=1` is a strong-looking oracle: after the optimised decode has run, re-decode the
same frame **unconditionally** and require the two buffers to agree byte for byte. It reported
`checks=28 mismatch=0 firstOff=65535` on a rewritten `RevsScreen::convertRace`, with the engine
provably under power (`$61=ff`).

⚠ **And it could not have caught a bug in the expansion, because both of its sides call
`revs_expand_cell`.** It compares *which cells were selected*, not *what was written into them*. A
defect in the shared leaf — a swapped plane, a wrong table, an off-by-one row stride — produces the
same wrong bytes on both sides and passes silently. This is the same shape as §a differential
harness carrying state between its two models, one level down: **the two models were never
independent to begin with.**

⭐ **The habit: before believing a differential, name what its two sides SHARE, and find a check
that does not share it.** Here that is `FILLWATCH=1` "check 2", which re-derives the expected
bitplane bytes straight from `mem[]` through `s_expandLo`/`s_expandHi` and compares them against
`dst` over display lines 74..167 — it never calls the decode's code at all. It read
`decode mismatch=0 firstLine=65535` over 197 painted frames with `horizon change max=2091 cells`,
which is the result that actually licensed the commit.
⚠ Note also what was REJECTED: diffing a `planes.bin` dump between the two BUILDS is not an
equivalence check, because they run at different speeds and at a fixed `g_vbiCount` the simulation
has advanced to a different place. A render-speed change moves the trajectory (`docs/perf-method.md`).

### ⭐⭐⭐ …and the SHARED THING CAN BE AN INPUT, WHICH BOUNDS WHAT THE ORACLE COVERS FOR GOOD (2026-09-17)

The same oracle, measured both ways on the same commit, and this is the sharp statement of its
scope. `convertRace` classifies each character row from its eight `m_lineMode` bytes — is any line
non-blank (`any`), are all eight the same mode (`uniform`) — and then either skips the row or picks
an expand path. Two sabotages:

| sabotage | what it breaks | oracle |
|---|---|---:|
| the shadow-mode compare always says "equal" | a moved band boundary is never re-expanded — the dirty pass **SKIPS** work | **mismatch=4050**, firstOff=805 |
| the `m0 == m1` guard dropped from `uniform` | a straddling row is **CLASSIFIED** as uniform and expanded down one path | **mismatch=0**, 31/31 |

⇒ The reference pass is `convertRace(scratch, 0, 0)` — **the same code over the same
`m_lineMode`** — so anything derived from that input is derived identically wrongly on both sides.
The differential sees *which cells were selected*, and a classification error changes the selection
on **neither** side.

⭐⭐ **So this is a fixture gap that cannot be closed by widening the fixture** — it is what an
in-process differential IS. The three §FIFTEENTH explanations for a surviving sabotage want a
fourth reading here: *the oracle shares the input the defect lives in.* Two consequences:

1. **State the scope AT the oracle**, in the terms of the code it guards: `DIRTYCHECK` gates what
   the dirty pass SKIPS, and nothing about how it classifies. Written at
   `RevsScreen::decode()`'s `#ifdef REVS_DIRTYCHECK` block and at the `uniform` switch.
2. **Gate a classification another way** — by argument where the predicate is exactly equivalent
   (`m0 == m1 && m0 == v*0x01010101` ⟺ all eight bytes are `v`, which is what "uniform" means),
   or by the PICTURE (`amiga/screen_dump.gdb`), or with `make DIRTY=0`, which takes the mixed path
   for every row and so does not consult the classification at all.

## ⭐⭐⭐ THE RESULTS RULE (user-stated, and it outranks the full-`mem[]` diff)

⭐⭐⭐ **A WORKED EXAMPLE OF THE AUDIT THIS RULE DEMANDS is `docs/span-render-plan.md` §12b** — the
forty view source blocks, `make srcaudit`. Read it before writing the next one; three things in it
generalise. **(1) Get the address set right first**: the obvious range was 3200 bytes and the real
one is 2188, because the game packs twelve tables into the offsets below each block's start —
auditing the whole range would have put twelve innocent tables in the answer and made the item look
blocked. **(2) Attribute on the AUTHENTIC ENGINE, not the port**: `make srcaudit` flags the bytes
under jsbeeb and names the routine behind every read, which is the only way to cover the arms a
Silverstone run cannot reach (it was run on all five circuits and they agree). **(3) The interesting
answer is usually a class you did not predict**: the three readers named in advance included one
that does not read those bytes at all, missed the producers' own read-modify-write (13% of reads,
and the real design constraint), and missed two producers that only run outside a practice session.


> "Our intent is to have faithful behavior from the user's point of view, not exact replication of
> the 6502 architecture. We want validation of results, not implementation details. This should
> govern all the work; otherwise there is no chance to improve the performance of the port to the
> intended level."

This is the same correction as §THE DOMAIN RULE, applied to the **output** side instead of the
input side.  There, the lesson was that a fixture models the GAME and not the input space; here it
is that the differential must pin the **results** the rest of the engine and the player can
observe, and **not** the transliteration's own working notes.

**What that makes an implementation detail — and therefore ignorable:**

- A **scratch `mem[]` cell** the oracle stores into and nothing outside the twin ever reads.  The
  6502 had three registers, so it spilled constantly; the 68000 keeps the value in `d0` and the
  store is pure cost.  §THIRTEENTH's `$01FF` residue is the first instance of exactly this shape.
- A **`cpu` register or flag dead at the exit** (the whole `cpu`-struct campaign, now held by
  `make cpu-lint`).
- The **order or count** of writes to a cell whose final value matches.

**What is a RESULT and must still be byte-exact:** every cell any other code reads — and that
question is answered by a written reader audit, not by intuition.  ⚠⚠ The audit is the hard half,
and two traps have already been paid for:

1. **The readers include the transliteration.** `region_23d8` is re-entered at `$2490` by every
   expansion circuit's track hook, so a cell "only this twin uses" on Silverstone is live on four
   other circuits.  A native-surface scan is not a reader audit (`docs/faithfulness-seam.md`).
2. **The readers include the next pass.** The forty `$80`-spaced blocks at `$3000..$4380` are
   `build_track_geometry` → `draw_road` → `view_paint_lines`'s interchange format; a producer's
   "scratch" is the consumer's input.

**How to apply it** — the same three conditions §THIRTEENTH established, which are the audit
written down:

1. The cell is provably unread outside the twin (transitively, and on every circuit).
2. The surrounding invariant stays declared live, so a real leak still fails.
3. The relaxation is **scoped** (`set_ignore` before, `set_ignore(0, 0)` after), so it cannot
   loosen another twin's contract.

⚠ And the ordering rule from §THIRTEENTH still holds, restated: **do not reach for the ignore list
to avoid understanding a cell.** The ignore list records a *finished* argument that the cell is
not a result; used before the argument exists it is just a green light on an unproven twin.

⚠ `make determinism*` diffs all 64 KB and has no ignore mechanism, so a dropped scratch write shows
up there as a divergence.  A cell this rule exempts must therefore be re-recorded
(`make clean && make determinism-record`) **with the reader audit quoted in the commit message** —
otherwise the re-record silently blesses whatever else moved.

### ...and its first application to CODE bytes: the span walk's step and marker slots

The four cells above are all *data*.  The span rasteriser gave the rule its first **executable**
subject, and the shape is worth keeping because the audit questions are the same but the answers
come from different places.

The 6502 remembered two things about a span by writing **opcode bytes into the two plotters' own
instruction streams**: which way Y steps (`$2F47`/`$2F60` for `road_span_plot`, `$2F89`/`$2FA2` for
`road_span_plot_2` — entry slot and exit slot each), and whether the end markers are switched on
(`$2FC0`/`$2FD7`, `CPX #imm` vs `RTS`).  The descending arms' shared exit then **copied** the
entry opcode into a third slot of its own (`$2F12` → `$2F18`) and executed that.  `interp_edge` is
the only writer, and it writes p1 and p2 identically at every site — so **seven bytes of traffic
per span carried three values**, one of them a boolean.

The audit, the seven addresses taken together:

1. **The twins themselves** — yes, and that is the point: they are the only consumers.
2. **`revs_gen.c`** — only inside `__t6502` oracles, plus `FUN_2f12`/`FUN_2f19`, whose only callers
   are two of those oracles.  Oracle-only *by transitive closure*, which is the form trap 1 above
   demands.
3. **A track hook re-entering the transliteration** — none.  **No hook targets the `$2F` page at
   all**, on any of the five circuits.
4. **A per-circuit patch** — none.  The page's only patched bytes across all five circuits are
   `$2F23-$2F25`, a *different* cell (`MEM_smc_span_cap_load`, modelled by
   `span_cap_line_slot_z`) which therefore **stays in `mem[]`**.
5. **`revs_smc_bytes.h` → `honoured()`** — a selection-time check, asked only about bytes a
   circuit patches; unaffected, and the header does not change.
6. **`src/platform/*`** — absent.
7. **The next pass** (trap 2): `view_paint_lines` reads `view_line_surface` and the `$3000` blocks.
   It never reads the `$2F` code page.

So the direction became `SpanStep g_spanStepIn/g_spanStepOut` and the switch `int g_spanMarkOn`,
with `mem[]` kept only as the **6502-ABI channel** that `span_plot_oracle` / `span_walk_oracle`
decode at the two boundaries — which is what lets the standalone plotter, marker and walk fixtures
go on planting opcode bytes.

**Three things this cost, none of them predictable from the audit:**

⭐⭐ **The differential's own boundary can scribble its own slot.** `span_plot_oracle` decoded the
slots once on entry and set "the slots were just read"; but the caller hands over the screen
pointers, and a fixture may leave them on page `$2F` — so the plotter's three stores land on the
step bytes *between* that decode and the exit step that reads it.  Measured as `mem[$2FA2]` turning
into a trap byte mid-plot.  A 6502-ABI boundary must **re-read at each use**, not cache.

⭐⭐ **The `relocated_poison` rule extends to it, and the proof is a surviving sabotage.** Deleting
`interp_edge`'s `g_spanStepIn` reset PASSED 600/600: the oracle runs first, its own boundary decode
leaves the correct direction in the global, and the twin inherits it — the exact class
`relocated_poison` exists for, on globals that were not relocations but *former `mem[]` code*.
Poisoned with the decoders' own `SPAN_STEP_TRAP`/`SPAN_MARK_TRAP` (a value that can never be a
correct answer), the sabotage is detected.  Five sibling sabotages fail loudly at 237/105/31/894/214
mismatches — all distinct counts, which is also the staleness check.

⭐ **An in-game-dead arm, kept behind an `#ifdef` for validation only.** On an ascending arm entered
*above* its bound the walk runs the long way round, climbing the three screen pointers through
every page including `$2F`, so its own stores land on the slots and the 6502 executes what it
overwrote — a cached direction cannot see that (3 of 400 cases on `draw_span_shallow_fwd`, 11 of 400
on `draw_span_steep_fwd`, **0 on both `rev` arms**, which stop *at* their bound and so never store
there).  It cannot happen in the game, and the proof is **arithmetic, not empirical**: `interp_edge`
derives the start page from a block it has already forced under `$28` (`block = (x - $30) >> 2`;
`if (block >= $28) return`; `page = (block >> 1) + $30`), so plot_ptr/plot_ptr2 start in `$30..$43`
and plot_ptr3 in `$31..$44` — strictly below the ascending bound `$44` and strictly above the
descending bound `$2F`.  Either walk terminates within 20 scan lines without wrapping.  (The
descending cap writes page `$2F` only as a *value* — `span_cap_line` stores to
`view_line_surface + y`.)  The fixture plants the above-bound entry deliberately, because it is the
only way the DDA's carry-in is ever 1.  So the re-read lives under
`#if !defined(REVS_PLATFORM_AMIGA)` (`REVS_SPAN_SLOT_FALLBACK`) — the cache stays live and validated
for every reachable input, and only the unreachable re-read is host-only.

**What the whole-corpus differential then says**, and it is the audit's independent confirmation:
across four 300-frame trajectories the change's entire 64 KB footprint is **five bytes** — the four
step slots on `determinism`, `-steer` and `-crash`, plus `$2F18` on `-drive` (a descending arm ran).
Nothing else moved, and `make viewdiff` is 0 differing bytes on the gated road view of all five
circuits.  The markers never differ at all: their last-written value already equals the runtime
image's own `CPX #imm`.

### ⭐⭐ The def-use audit: "who reads the value THIS store left" (`make rangeaudit DEFUSE=1`)

A plain range audit answers *who touches these cells*, and on a crowded scratch cell that is a
dozen tenants — every one a "reader" of the cell and almost none a reader of the value under audit.
`DEFUSE=1` pairs each READ with the PC that last WROTE the byte, grouped by writer, so the stores
being freed read as a block and every line under them is a consumer of what they left. Several
ranges go in one run (`RANGE=0074-0085,002A-002B`). Two lessons from its first use (the true ratio,
below):

1. **A read of the value is not yet a USE of it.** `plot_view_src_line` read the bearing's `$85` on
   four circuits — it copies `$85` to `$7C` unconditionally at `$1C2A` — and the audit alone makes
   that look like a live consumer of a lane the new maths stopped writing. One further hop settled
   it: tagging each `$7C` read with the ORIGIN of the `$85` value it was copied from showed that on
   all five circuits every `$7C` actually consumed came from the routine's own `$1C55`; the copies
   of the bearing's byte are always overwritten first (mode 1 returns at `$1CA6` without reading
   `$7C`). **When a def-use pair lands on a COPY, follow the copy one hop before believing it.**
3. ⭐ **It now also lists DEAD STORES** — per writer PC, the stores overwritten before any read
   ("DEAD STORES" for a writer none of whose resolved stores was read, "PARTLY dead" beside it). A
   store still pending when the window closes counts neither way, so a writer that runs once at
   the end of the window can hide; a deletion still wants all five circuits. Checked against a
   known answer: the geometry walk's cells, whose eight stores were deleted on the def-use audit,
   show as live-within-the-routine (their consumers were inside the walk, which is why the asm
   could hold them in registers), and none of the five cells read a frame later shows as dead.
2. **Run it on all five circuits, and expect them to disagree.** Silverstone showed the unshifted
   store reaching that copy; Brands Hatch, Oulton Park and Snetterton showed the SHIFTED one.

## ⭐⭐ THE TOLERANCE MODE — a twin that is deliberately better than its oracle (2026-09-23)

The RESULTS rule says which cells are results. This is for the other case: **a result the twin
computes differently on purpose** — the user's "true 68000 ratio" decision, where `bearing_to_section`
and `project_point` take `(S << 8) / L` in one `DIVU` instead of the 6502's quotient over a divisor
truncated to its top byte, accepted at ±1 LSB against a real BBC. Equality cannot gate that and
`set_ignore` would gate nothing, so `set_tolerance(fn)` installs a per-fixture hook that runs after
the ignore list and before the exact compare: for each difference it ACCEPTS it copies the native
value into the oracle's copy; one it rejects it prints (`[TOL DIFF]`) and fails. Everything it does
not touch stays byte-exact. Rules, all four learned on the first four users:

1. **Check in the space where the error model is exact, not as a ±N band on the output.** The
   bearing fixture's arctan table is random bytes, so a band on the angle means nothing (its largest
   accepted difference reads 8192). Instead: the TWIN must be exact to the true quotient `qt` (its
   table byte, its angle), and the ORACLE must be the same computation for `qt+0..+2` or the 45°
   door a quotient of 256 takes. Where no table is involved, a relative bound (road width
   `(T >> 6) + 2`, object width ±2) is honest.
2. **Derive the bound, and tighten it where arithmetic allows.** The projection's slack is +1, not
   +2, and that is not a fixture gap: the truncated divisor's error is under 1/128 and a surviving
   quotient is under `$80`, so the two differ by less than one before the floors. The bearing's
   quotient runs to 255, which is why it really does reach +2.
3. **A tolerance must FIRE and must be SABOTAGED.** Each user prints its census (which slack the
   oracle used, how many cases differed, how many took an exact-path arm) and requires it non-zero,
   or the relaxation proves nothing. Eight sabotages at bound + 1 and at each exact-arm boundary all
   failed with distinct counts.
4. **A fixture whose oracle and twin read DIFFERENT representations must stage both from ONE
   value.** The width routines' oracle reads project_point's mantissa/exponent float; the twin reads
   the distance. So the fixture draws a distance, writes it to `point_dist`, and writes the 6502's
   float of it — against the REAL reciprocal table (§THE DOMAIN RULE: a random mantissa is not a
   float of any distance, and the two models would not be computing the same number).

⚠ **Only the direct fixtures need it.** Every composite fixture that reaches these routines
(`road_edge_walk`, `build_road_sign`, the object projectors, …) still passes byte-exact, because its
oracle's `JSR`s land on the native shims — both sides compute the new maths.

⚠ **`viewdiff` is the gate that says the tolerance is the one the user accepted**, and it has no
tolerance of its own: record what HEAD shows against the SAME BBC captures (a worktree build per
circuit), then classify every new gated byte by decoding the pixels. For the true ratio: HEAD 0 on
all five circuits, the change 1/5/17/0/3 bytes, every one an edge transition displaced along its own
line — 20 of 26 lines by one pixel, one shallow kerb by one scan line (2..6 px sideways on a ~3:1
slope). Quote that classification in the commit that re-records `make determinism*`.

## ⭐⭐⭐ WHOLE LAPS — `make lap`, the autopilot that races every circuit (2026-09-26, racing speed 2026-09-28)

**The gap it closes.** Every determinism trajectory is *drive straight until the car leaves the
road* (or parked, or one held steering key): ~225 frames of one straight, then a crash reset to the
grid. So no gate had ever seen a corner taken, a hill crested at speed, or a lap completed — and a
user found a physics jump-and-crash on the Nurburgring that none of them could reach.

**The driver** (`src/platform/autorun.cpp` §THE AUTOPILOT, host, `REVS_AUTOPILOT=1`) reads only the
game's own state and answers the player's own keys, so it changes no engine code and is
deterministic under `FIXED_RNG`. ⭐ It drives by the TRACK, not the picture: the live section ring
(`section_coord_lo/hi`, 40 sections × two road edges, ~32 ahead of the car) relative to the camera
(`view_origin_16`) is the road centre as a path.
- *steering* — **pure pursuit**. Measured, the car is kinematic up to its grip: yaw per frame =
  wheel × ground speed / 100 (heading units, `road_speed` units), so the wheel for the circle through
  a point `L` ahead at angle `a` is `a·400/L` (the old hand-tuned Kp 0.45 at ~800 ahead was exactly
  that). `L` = 400 + 10 per speed unit, a damping term on the aim's rate, SPACE (the steering
  amplifier) while the wheel lags. ⚠ Bang-bang on the aim oscillates to a crash: a held key winds the
  wheel ~64 units a frame — the lag is the wheel's slew, not yaw inertia;
- *speed* — the lowest `sqrt(lat·R + 2·decel·s)` over every bend on the path (R from the net turn over
  one to three sections, s its distance less a 150-unit lead), against the GROUND speed (the camera's
  step a frame: 2 world units per `road_speed` unit). `road_speed` falls away from it when the car
  slides or the wheels lock (measured: 0 against 72 units a frame braking from 63), so braking waits
  while they differ — the brake is a key, so this is ABS;
- *traction* — a yaw more than 50 away from the kinematic one is a slide: off both pedals until it grips;
- *engine and gears* — a stalled engine will not catch in gear, so drop to neutral first; a gear key
  counts only on a PRESS, so shifts are taps on alternate frames; up at revs 120 under throttle, down
  at 70.
- ⚠ The game's own Computer Assisted Steering was tried as the steering and does NOT do this: it
  shapes a demand only while a key is held and only for `track_direction` positive; with direction
  chosen by the aim it held Silverstone and failed on every expansion circuit.

**What racing speed took, in the order the traces showed it** (every knob is a `REVS_AP_<NAME>`
variable and a host run is ~0.1 s per 1000 frames, so each question was a six-circuit sweep):
1. ⛔ **The picture is not a usable steering input at speed.** The first driver aimed at an EDGE-POINT
   SLOT (`edge_x`), whose distance ahead jumps as the walk's point count changes: on the Nurburgring
   the aim swung 40° in four frames at speed 26 and the car was steered into a spin. The same point
   taken from the section ring moves smoothly.
2. **Brake by distance, not by bend angle.** A target from "how far the road bends at a far window"
   has no braking distance in it, so above 40 it saw a bend ~4 frames out.
3. ⭐ **The spins were TRACTION, not corner speed**: throttle in first gear with the wheel turned, or
   braking into a bend, made the car rotate faster than its wheel could explain — and every SLOWER
   corner target made it worse, because it put more lift-off and braking inside the bends. A slide
   test on yaw against the kinematic prediction fixed the Nurburgring where no speed setting did.
4. Two first-frame bugs, both reading a previous-frame value that was zero: ground speed (a 10960
   step, so it braked at the start) and yaw (a spin on frame 1, so it lifted off and stalled).
Every knob moved alone ~10% either side stays at zero crashes on all six circuits.

**The checks** (`tools/autopilot_laps.py`, one `[autopilot]` summary line per run): per circuit,
20000 frames, **zero crashes** (a crash resets the car to the grid — detected as a distance jump
that is not a lap wrap — so one crash voids the run), **zero stalls**, **zero airborne frames**
(`car_height >= 2`; a clean lap has none on any circuit, hilly ones included) and at least 5 laps.
Each line also reports the top ground speed and gear reached. Baseline: **12-18 laps on every
circuit** (it was 8-12 at first-gear pace), top speed 63-73 in third or fourth gear, all zeros.
`LAPARGS=--trace` prints the 40 frames before any crash, stall or jump (distance, speed, ground
speed, target, gear, revs, wheel, aim, height, direction, pedal).

**Sabotaged, and the result is the point** (re-run at racing speed): doubling the gradient term in
the height model (`scale_by_track_gradient_core` at `$457F`) FAILS Brands (31 airborne frames,
height 51), Oulton Park (86, height 29) and the Nurburgring (14, height 22) while flat Silverstone,
Donington and Snetterton pass — the user's reported symptom class, which no other gate can see.
Lowering the touchdown-rebound threshold (5 → 2) fails all six.
⚠ Scope: PRACTICE, alone on track, up to ~73 and fourth gear. The corner speeds are the driver's
(a lateral limit of 2 world units a frame², cautious by design), not the car's.

## ⭐⭐⭐ THE LOCKSTEP — the autopilot's run replayed on a REAL BBC, frame for frame (`make lockstep`, 2026-09-27)

Layer 2 of `make lap`. Invariants (no crash, no jump) say a lap LOOKED right; they cannot say the
port computed what a BBC computes, so a defect with plausible symptoms — or a faithful quirk that
looks like one — goes undecided. The lockstep decides it.

**The mechanism.** The host (`src/platform/host/PlatformHost.cpp` §THE LOCKSTEP RECORDER,
`REVS_LOCKSTEP=<file>`) logs, from the first steering-key poll (-87, which only the race body makes):
every key poll — the code the engine asked and the answer given; every value read from `$FE68`; and
at each SHIFT poll (-1, once a frame in `race_main_loop`'s tail) a snapshot of the car-state regions
and the view's frame buffer. jsbeeb (`bbc_refloop_race.mjs --lockstep=<file> --lockstep-out=<file>`)
hooks `kbd_test_key` ($0E50): each call must ask the SAME code the host was asked — otherwise the
CONTROL FLOW diverged, and the replay stops and says where — and that key's matrix state is set to
the recorded answer before the MOS reads it; the User VIA's T2 read returns the host's value (the
real read still happens, keeping its flag side effect). So up to the first difference both machines
saw identical input, and `tools/lockstep_diff.py` names the first frame their state differs.
Replaying recorded answers needs no second copy of the controller. ~30 BBC frames a second.

**What it took to make two machines agree, each one a measured divergence first:**
1. ⚠ **The starter's catch is a VIA-timer lottery**, so both sides force `starter_random_mask`
   ($0009) to 0 — the engine catches on the first `T`.
2. ⚠⚠ **`$FE68` is the engine's only entropy source** (six sites: the starter, the idle-rev jitter,
   the gravel trigger, ...) and reads a cycle-timed clock no two machines share: without replaying it
   `engine_revs` differed by one from frame 0.
3. ⚠⚠ **The wings.** The host's script takes the validator's default (0); the refloop typed 20 by
   default, and wing angle feeds grip — the physics parted at frame 6 through `grip_limit`. Both 0 now.
4. ⚠⚠⚠ **The accepted ±1 LSB of the true 68000 ratio cannot be accepted here, because the PHYSICS
   READS THE PICTURE**: the grip model's two surface bytes are frame-buffer cells, so a one-pixel
   edge shift is a different grip and a different trajectory within a few frames. ⇒ `make
   EXACTRATIO=1` (host only) computes the 6502's own ratio in `bearing_to_section`, `project_point`
   and the two width routines — and `make validate EXACTRATIO=1` switches the tolerance OFF, so those
   twins must match their oracles EXACTLY (they do; `bearing_to_section`'s shifted `point_delta`
   scratch lanes are out of its compare in that build, by the same reader audit).
5. The port keeps several values outside `mem[]`, so every host snapshot and dump comes from
   `ls_true_mem` (every marshal-out, on a scratch copy) — true values without perturbing the run.

**What is not compared, and why** (`tools/lockstep_diff.py`, each set named at the code): the 6502's
scratch cells the port does not reproduce (the RESULTS rule), the MOS/BASIC workspace, the 50 Hz
counters (`field_countdown`, `wheel_spin_accum` — how often the interrupt runs per frame differs
between the machines), the span/object plotters' per-draw state, the other car slots' seeds in
practice, and edge-array slots past the live end cursors (stale).

**What is not compared in the PICTURE** (`--view`): `tick_wheel_spin`'s six EOR runs (the tyres
turn in the 50 Hz interrupt — ~5 times a BBC frame, once a host frame) and the gear digit, whose
glyph is this project's own font (`src/platform/mos_font.h`), as are `vdu_char_block`'s rows
(`FONT`) in the physics set.

**Results (practice, the autopilot's line, 2995 frames each — ≈1.2 laps at first-gear pace, and
re-run at racing speed, 1-2 laps up to ~73 in fourth gear):** ALL SIX
circuits are **identical to a real BBC in the physics AND the picture**. It took four port bugs,
each invisible to every other gate:
1. **`EXACTRATIO`'s width routines divided by a distance the projection had already shifted in
   place** — the test build's own bug; every road edge and sign at the wrong width.
2. ⭐⭐⭐ **THE USER'S JUMP-AND-CRASH.** The low block's terrain painter let a source event on a
   run's FIRST cell replace the run's composed entry, where the chain it replaced enters at a
   unit's +$05 and consumes that source unread. Display line 149 cell 32 is run B's first cell AND
   the grip model's right-wheel probe: grass under a wheel on the road, the grass arm, `begin_jump`.
3. The low block's first sweep scanned before its clip table existed — frame 0 wrong everywhere.
4. **An expansion circuit's walk hook resumed the walk with last frame's `edge_nearest`** (the
   6502-ABI resume marshals it IN; production published it only at the walk's end) — the queued
   SUSPECTED item, seen at Nurburgring frame 1309 with every input byte identical.

⭐⭐⭐ **THE TRANSFERABLE LESSONS.**
- **An equivalence oracle is only as good as the TRAJECTORY it runs on.** `TERRAINLOWCHECK` (the
  replaced chain vs its replacement, cell by cell, in process) was 0-mismatch over every
  determinism trajectory and wrong for the whole life of the path: a road boundary exactly on a
  run's first cell is rare on a straight run and routine under a STEERING driver. Run the in-process
  oracles under `make lap`'s autopilot, not only under `STRAIGHT_TO_RACE`.
- ⭐⭐ **Bisect a lockstep divergence with FULL-MEMORY dumps at matched points, and the answer
  arrives in three runs**: `REVS_LOCKSTEP_DUMP=N:<f>` / `--lockstep-dump=N:<f>` (64 KB at frame N's
  snapshot) and `REVS_LOCKSTEP_AT=PC:N:<f>` / `--lockstep-at=PC:N:<f>[,...]` (the first time the
  6502 reaches PC in lockstep frame N; the host fires at its `platform_mem_snapshot_at` sites, which
  carry the 6502 JSR addresses). "Everything matches at the end of frame N-1, and at routine R's
  entry, and not at its exit" names R — and then the in-process oracle of R, run on that frame,
  names the cell.
- An oracle's report that nobody REGISTERS is no oracle: `revs_report_low` had never printed.
- ⚠⚠ **THE LOCKSTEP'S SCOPE IS THE HOST'S `mem[]`, AND THE AMIGA'S OWNED ROWS NEVER REACH IT.** On
  the target the terrain rows are painted from the scan's EVENT LISTS straight into the bitplanes,
  so an Amiga-only defect there passes every host gate and the lockstep by construction. Found
  that way (2026-09-28): Brands Hatch started with no road above the cockpit for ~22 sweeps on an
  A500 (~120 on an A1200) — `view_low_build` failed and retried there, and the scan's per-cell floor
  `s_lowConsume` was `$FF` until it succeeded, a value from when the scan covered the low block
  alone that also skipped phase 1 once it covered 0..79. `SCANCHECK` could not see it (asm and C
  read the same floor — a SHARED INPUT, CLAUDE.md §sabotage). ⭐ **The target-side gate is cheap:**
  break at `revs_plot_terrain`, dump `g_viewEv` / `g_viewRowBg` / `g_viewRowAddr` and the displayed
  buffer, rebuild each row from its events and diff it against the HOST's frame buffer at the same
  frame (`REVS_SCREEN_COUNT` + `REVS_MEM_DUMP`, which the lockstep has proven equal to a real BBC's).
  That separates "the events are wrong" (scan/sources) from "the pixels are wrong" (painter,
  signatures, flip) in one run.
- ⭐⭐ **AN ASSERTION NOTHING GATES ON IS A MEASUREMENT NOBODY READS — AND A FALLBACK THAT PAINTS
  CORRECTLY HIDES A SLOW PATH FROM EVERY PICTURE GATE.** `g_terrainClipBad` was documented "MUST BE
  0" and read non-zero on every circuit: `view_low_build` classified a phase-2 line by the byte at
  `view_run_right_end + line`, which the driver never reads there because it is column 1's LIVE
  SOURCE (`revs_native.c` §s_lowClipped). Zero at Silverstone's start (2 failed sweeps), non-zero on
  Brands' grid for as long as the car stood still — **5.07 fps parked where the fix runs 12.50** —
  and 602 rejected lines on Oulton under the autopilot. The chain painted every rejected sweep
  byte-correctly, so determinism, the lockstep and the picture were all green; the user's "choppy
  until the car moves" was the only report. ⇒ `make lap` now FAILS on any rejected line (the
  `[autopilot] low-block build rejects:` line). ⭐ **Derive a table's domain from its READER's index
  range, never from the value found in it** — a byte outside the range the driver indexes belongs
  to whatever else shares the page.
