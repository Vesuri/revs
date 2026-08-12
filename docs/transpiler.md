# Transpiler internals — generated-code shape

> **Read this when working on `tools/transpile.py`, or when the shape of generated `revs_gen.c`
> surprises you.**  The everyday parts — the file table, the native-twin seam, and which file a
> twin belongs in — are in `CLAUDE.md` §Transpiler and `docs/faithfulness-seam.md`.
>
> ✅ **The port is done** (Phase 3).  `tools/transpile.py` emits `revs_*`, knows the BBC's
> address ranges, intercepts MOS calls, and emits the self-modifying code as runtime dispatch
> rather than hand-stubs.  The §Porting checklist at the bottom records what changed.

## ⭐ Emit clean C BEFORE mass-generating

**Postmortem finding #2.1**, and it is a sequencing instruction, not a style preference.

On the Atari port the liveness-gated peephole folding landed *late*.  Every regen and every human
read between "first generation" and "peephole added" paid the tax of uglier, slower, harder-to-diff
C — and the fold, when it came, removed ~1490 lines at a stroke.

The transpiler is a force multiplier: **one improvement upgrades the entire corpus at once.**  So
treat "emits clean C" as a **prerequisite milestone**:

- liveness-gated flag elision,
- named `mem.h` accesses driven by `symbols.csv`,
- folded load→store idioms.

Cleaner generated C also directly reduces **how many functions ever need a hand-written twin** —
which is the expensive downstream work.  Getting this right first is the cheapest thing in the
whole pipeline.

## The transliteration model

Every 6502 instruction becomes a C statement over an emulated register struct (`cpu`) and a real
64 KB `mem[]`; `JSR`→call, branches→`goto`, RAM vs I/O split at `bus_read`/`bus_write`
(`src/cpu/bus.h`).  A literal translation is faithful by construction; understanding is added
afterwards by renaming and by replacing hot functions with proven-equivalent twins.

Per-op flag computation and bus dispatch are the overhead that native rewrites remove.

## Named memory accesses (`mem.h`)

`symbols.csv` is the single source of truth not just for function names but for named RAM
addresses.  The transpiler builds a `VAR_NAMES` map from its var rows and emits named accesses
instead of raw `mem[$NNNN]`:

- a direct access becomes a bare lvalue alias (`lap_counter = cpu.X`), via an opt-in
  `REVS_MEM_ALIASES` block in the generated `mem.h`;
- indexed / read-modify-write / `bus_write` forms use `mem[MEM_<name> + i]`.

`revs_native.c` is auto-converted to the same named forms; Amiga-only code uses `mem[MEM_*]`
directly.

## Peephole folding (liveness-gated)

The transpiler runs a backward-CFG register/flag liveness fixpoint (over A/X/Y/N/Z/C/V; function
exits = all-live) and folds the faithful-but-ugly 6502 load→store idioms into direct C
assignments **when the loaded register *and* the N/Z flags it set are provably dead after the
store**, and the sequence is straight-line (no end is a branch target / split / injected hook):

- `LD{R} #imm; ST{R} addr` → `addr = imm;` (or `bus_write(addr, imm)` for I/O), including a run of
  consecutive same-register stores.
- `LDA $x; STA $y` → `$y = $x;` — **single store only**, so a later store in a run cannot change
  what an earlier-read source should hold.

Indexed/indirect modes count their index register as a read, and `JSR` reads and clobbers
everything, so no live value or flag is ever dropped.

## Self-modifying code — emitted, not stubbed

The Atari rule was "a routine that writes its own instruction stream cannot be transliterated
faithfully → hand-stub it in `*_manual.c`".  **Revs does not follow it**, and the reason is worth
stating: that rule costs one hand-written, unvalidated, non-regenerable routine per site, and all
24 of Revs's sites are inside the road rasteriser — the hottest and least-understood code in the
binary.  Freezing one snapshot of it by hand is the expensive way to be wrong.

Every site is one of three **mechanical** classes, and each has an exactly faithful runtime form.
`SMC_SITES` in `tools/transpile.py` carries the table, with the writer instructions as evidence
for every value:

| Class | What is patched | Emitted as |
|---|---|---|
| `operand` | operand BYTES, opcode stands | effective address / immediate read from `mem[]` at run time (byte-wise ⇒ endian-safe), routed through `bus_read`/`bus_write` because a runtime EA cannot be range-tested at gen time |
| `opcode` | a 1-byte opcode SLOT | `switch (mem[site])` with one case per value the writers actually store |
| `branch` | a branch OFFSET | the 6502's own target computation, dispatched over the enclosing routine's instruction starts |

Anything outside the evidence calls `platform_smc_unhandled()`, which **reports** — it never picks
a branch.  A silent no-op there reads exactly like a rasteriser that runs and draws nothing.

Two details that make the classes work:

- The `$2FC0`/`$2FD7` slots switch between `CPX #$80` ($E0) and `RTS` ($60) — **different
  lengths**.  That is representable only because `$60` terminates: with `RTS` in the slot the
  operand byte is never fetched, so nothing downstream shifts.  Both are function starts, so the
  whole routine is switched between "compare and continue" and "return immediately".
- In the static image the four rasteriser `BCC`s read `+0` — a branch to the next instruction, a
  no-op.  That is what an unpatched slot *should* look like, and it is why a naive transliteration
  of `$2C00-$2FFF` would run and do nothing.

`MANUAL_FUNCS` is therefore **empty**.  Add to it only for a routine that cannot be expressed as a
transliteration at all, and say why.

Related trap: **`listing.txt` is the FINAL image.**  Code at an address during one phase may be a
different routine at the same address during another.

## BRK, and calls into memory that holds no code

`BRK` is **not** a no-op on the BBC: it vectors through BRKV into the MOS error handler and does
not return to the following instruction.  It emits `platform_brk(pc)`.

This is load-bearing rather than pedantic.  Four "routines" in the image are a single `$00` byte —
`$7B00`, `$7B4A`, `$7B9C`, `$7BE2` — and the engine JSRs to them from seven sites, one of them four
instructions into the routine the front end calls on release.  Nothing loads that page.
`make gen` lists them (`report_brk_targets`); the open question is in `docs/static-map.md`.

## Spin-waits and hooks

Points where the 6502 busy-waits on a hardware or clock value become hooks that let the platform
drive a real frame (`platform_tick_vbi(); platform_render_frame();`).  ⚠ Only for waits that own
a whole frame boundary — a raster-position wait would be reset by the tick and could never exit.

**`SPINWAIT_HOOKS` is empty on purpose, and `make gen` reports the candidates instead.**  The
Atari port's list grew reactively, one runtime hang at a time, and each entry needed a paragraph
of justification written after the fact.  Revs makes guessing worse: its 50 Hz body is a
raster-timed User VIA T1 interrupt that reloads the palette mid-frame, so several of its waits are
intra-frame by design and a tick in one would prevent the very exit it is meant to enable.  The
report lists every tight backward loop with no `JSR` and at least one memory read (41 of them);
Phase 4 fills the list from what actually stalls on the target, with the loop's exit condition
read out first.  `PRE_INSN_HOOKS` is empty for the same reason.

## What `make gen` reports

Not decoration — each line is one of the ways a generated corpus is silently wrong:

- `[rom]` — Ghidra functions in ROM space, dropped (MOS entries are intercepted, not defined).
- `[orphan]` — code in inter-function gaps, and which function absorbed it. An orphan run that is
  neither fallen into nor branched to is flagged `UNREACHABLE?` rather than quietly kept.
- `SMC:` — the patched instructions, by class, and the routines holding them.
- `BRK-only targets:` — routines that are a single `$00`, with their callers.
- `spin-wait candidates:` — see above.

Generation **fails** (rather than emitting a plausible stub) on: a `JSR`/`JMP` into ROM that is not
a MOS entry; a `VALIDATE_FUNCS` address that is not a function start or mid-function entry; an
`SMC_SITES` address that is not an instruction start in the current listing.

## Porting checklist (transpile.py → Revs) — ✅ done, Phase 3

- [x] Output filenames `rof_*` → `revs_*`; `ROF_*` defines → `REVS_*`.
- [x] Hardware range Atari `$D000-$D7FF` → BBC `$FC00-$FEFF`, matching `bus.h`'s `BBC_IO_LO/HI`
      so the two cannot disagree about what routes.  The `$0200-$02FF` vector page still goes
      through `bus_write` — now for IRQ1V, the only vector Revs claims.
- [x] **MOS-call interception** — `$FF00-$FFFF` emits `platform_mos_call(entry)` with the entry
      named in the comment.  A ROM call that is *not* a MOS entry fails generation; there are none.
- [x] **Emit `src/gen/revs_validate_list.h`** — always, even empty, so an empty list means "no
      twins" and never "stale file".
- [x] `SPINWAIT_HOOKS` re-checked: emptied, with a gen-time candidate report instead (above).

Two boundary bugs the port surfaced, both of which **silently dropped code** — worth knowing about
because both are generic to this pipeline, not to Revs:

- An orphan run was attached to a function only when it FELL THROUGH into it.  Three of Revs's
  runs end in a terminator and were dropped — including `$4E59`, the `JMP ($4F1D)` IRQ1V chain-on.
  The generated handler let a foreign interrupt fall off its end instead of chaining.  Ending in a
  terminator is not evidence of not belonging: a prefix loop body can perfectly well `RTS` out.
  A run is now attached if it falls through **or** the function branches back into it.
- A `JSR` to a nested function start inside the caller's own Ghidra range was treated as a local
  `goto`, so no wrapper was emitted and the call went undeclared.  Branches inside the range are
  gotos; a `JSR` never is, because it has to return.  **The C compiler caught this one** — worth
  remembering that the type system is part of this pipeline's error detection.
