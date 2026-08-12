# Transpiler internals — generated-code shape

> **Read this when working on `tools/transpile.py`, or when the shape of generated `revs_gen.c`
> surprises you.**  The everyday parts — the file table, the native-twin seam, and which file a
> twin belongs in — are in `CLAUDE.md` §Transpiler and `docs/faithfulness-seam.md`.
>
> ⚑ `tools/transpile.py` is carried over from the Atari port **unmodified so far**, so it still
> emits `rof_*` filenames and knows about Atari address ranges.  Adapting it is Phase 2 work; the
> §Porting checklist at the bottom lists what has to change.

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

## Self-modifying code

A routine that writes its own instruction stream **cannot** be transliterated faithfully.  It
gets a hand-written stub in `src/gen/revs_manual.c`.  Revs is expected to have several — identify
them during the entry-point sweep (`docs/entrypoint-sweep.md`), not when the generated code
misbehaves.

Related trap: **`listing.txt` is the FINAL image.**  Code at an address during one phase may be a
different routine at the same address during another.

## Spin-waits and hooks

Points where the 6502 busy-waits on a hardware or clock value become hooks that let the platform
drive a real frame (`platform_tick_vbi(); platform_render_frame();`).  ⚠ Only for waits that own
a whole frame boundary — a raster-position wait would be reset by the tick and could never exit.

## Porting checklist (transpile.py → Revs)

- [ ] Output filenames `rof_*` → `revs_*` (`revs_gen.c`, `revs_decl.h`, `revs_native.c`,
      `revs_manual.c`, `mem.h`).
- [ ] `ROF_*` defines → `REVS_*` (notably `ROF_MEM_ALIASES` → `REVS_MEM_ALIASES`).
- [ ] Hardware range: Atari `$D000-$D7FF` → BBC `$FC00-$FEFF`; shadow page `$0200-$02FF` stays
      (BBC OS vectors), but the *meaning* of the cells changes.
- [ ] **MOS-call interception**: a `JSR $FFF4`/`$FFF1`/… must emit `platform_mos_call(entry)`
      rather than a call into unmapped ROM.  This is new — the Atari port had no equivalent.
- [ ] **Emit `src/gen/revs_validate_list.h`** — the list of `VALIDATE_FUNCS` names the harness
      uses for fixture-or-fail (`docs/validation-harness.md`).  The harness already reads it.
- [ ] Re-check the `SPINWAIT_HOOKS` list against Revs's actual wait sites.
