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

`irq1v_handler` ($4E5C) writes the Video ULA and the User VIA T1 latch about twenty times per
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

## Using it

```
make validate                 # everything
make validate FN=<substring>  # only matching tests — use this; a full run gets slow fast
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
