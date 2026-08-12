# Phase plan

Ordering follows the postmortem's checklist rather than the Atari port's actual history — the
whole point is that the discovery and validation infrastructure comes first, not reactively.

**Status legend:** ✅ done · 🔧 in progress · ⬜ not started

---

## Phase 0 — Scaffolding ✅

Repo structure, gitignores, the reusable machinery carried over from the Atari port (6502 CPU
model, platform abstraction, Amiga framework + build system, Ghidra scripts, the transpiler), the
docs that encode what that project learned, and a **bring-up skeleton that runs on the target**.

Verified: host `make` + `make validate` + `make endian-lint` clean; Amiga `make` links with a
clean muldiv audit; a 25 s headless FS-UAE run reports `vbi=1075 painted=1054` — display
takeover, 50 Hz VERTB handler, copper list, frame pump, and the embedded 6502 image all live and
readable from gdb by name.

---

## Phase 1 — The reference loop ⬜  ⭐ DO THIS FIRST

`docs/bbc-reference-loop.md`.  There is no `atari800` here; ground truth has to be built.

1. Install + smoke-test jsbeeb (headless oracle) and b2 (debug HTTP API on `:48075`).  Record
   what actually works — the documented caveats are not yet confirmed on this machine.
2. **Boot the real disc, break at the engine entry, dump RAM, diff against
   `disasm/revs_mem.bin`.**  Fix `LOAD_ORDER` in `tools/ssd_load.py` until it matches.
3. Capture reference state at named milestones, committed as files.

**Exit criteria:** the composed memory image is confirmed byte-correct against a real BBC, and a
scripted "boot to milestone, dump RAM" command exists.

> ⚠ Until step 2 passes, every address derived from `revs_mem.bin` is provisional.  Nothing
> downstream should be treated as settled.

---

## Phase 2 — Complete static map ⬜

1. **The entry-point sweep** — `docs/entrypoint-sweep.md`.  Every indirect jump, RTS-dispatch
   table, OS vector and hardware vector enumerated and seeded in
   `ghidra_scripts/entrypoints.csv`, before any C is generated.  Self-modifying routines listed.
2. **The hardware-access map** — retool `DumpHwAccesses.java` for `$FC00-$FEFF` + `$0200-$0235`.
   Its output *is* the abstraction boundary, and it replaces the **[ASSUMED]** rows in
   `docs/bbc-hardware.md` with measured ones.
3. **Enumerate every MOS call Revs makes** (`docs/bbc-hardware.md` §MOS calls) — the genuinely
   new piece versus the Atari port.
4. **One concentrated behavioural-naming pass** into `disasm/symbols.csv`.

**Exit criteria:** `listing.txt` has no referenced-but-undisassembled address; the hardware and
MOS-call inventories are complete; names are roughly right everywhere.

---

## Phase 3 — Transpiler quality, then generate ⬜

`docs/transpiler.md`, including its porting checklist.  ⭐ **Clean C is a prerequisite, not a
later optimisation** — liveness-gated flag elision, named `mem.h` accesses, folded load→store
idioms *before* mass-generating, because one transpiler improvement upgrades the whole corpus and
shrinks the twin backlog.

**Exit criteria:** `make gen` produces C that builds clean on both backends, and
`revs_validate_list.h` is emitted.

---

## Phase 4 — End-to-end skeleton ON THE TARGET, then profile ⬜

Postmortem #4.1: get *something* running end to end on the real machine as early as possible, and
**profile it before choosing what to optimise**.  On the Atari port an "algorithmic floor"
conclusion was reached by reasoning and later disproven by hand-asm — a crude full-pipeline
profile would have shown up front exactly which handful of functions ever needed asm.

**Exit criteria:** the genuine entry chain runs under `PlatformAmiga`, a framerate exists from
`FPSCOUNT=1` + `fps_seg.gdb`, and a profile names the hot functions.  **Only then** set a
performance target (`docs/perf-method.md` deliberately does not carry one over).

---

## Phase 5 — Render + input ⬜

- 6845 CRTC + Video ULA composition → copper list + bitplanes (`docs/amiga-lessons.md` for the
  rules; a CRTC/ULA analyser is `docs/bbc-reference-loop.md` step 5).
- The **uPD7002 ADC steering**.  A racing sim's feel lives here — get it right early rather than
  approximating it and tuning later.
- Sound: SN76489 → Paula.

---

## Phase 6 — Native twins, then asm ⬜

`docs/faithfulness-seam.md` for which side of the line each routine lands on;
`docs/validation-harness.md` for the guarantees; `docs/m68k-optimisation.md` for the 68000 rules;
`docs/perf-method.md` for how to price a change honestly.

The physics hot path replaces the terrain rasterizer as the eventual hand-asm target: different
loop shape, same lever (control the registers, force `(a0)+`, verify with an in-process
differential, never cross-run).

---

## Phase 7 — Packaging ⬜

WHDLoad slave, a player-facing README (keys, requirements), and an asset audit: ship only what
the port needs, not the original disc image.
