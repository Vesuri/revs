# Phase plan

Ordering follows the postmortem's checklist rather than the Atari port's actual history — the
whole point is that the discovery and validation infrastructure comes first, not reactively.

**Status legend:** ✅ done · 🔧 in progress · ⬜ not started

---

## Phase 0 — Scaffolding ✅

Repo structure, gitignores, the reusable machinery carried over from the Atari port (6502 CPU
model, platform abstraction, Amiga framework + build system, Ghidra scripts, the transpiler), the
docs that encode what that project learned, and a **bring-up skeleton that runs on the target**.

Verified: host `make` + `make validate` + `make endian-lint` clean; Amiga `make` links with clean
muldiv and probe-symbol audits; a headless FS-UAE run reports `vbi=824 painted=803` on an
`FPSCOUNT=1` build (≈48.8 FPS with nothing yet to draw) and `painted=0` on a plain one — display
takeover, 50 Hz VERTB handler, copper list, frame pump, and the embedded 6502 image all live and
readable from gdb by name.

One real bug was found and fixed by running it rather than reading it: `--gc-sections` was dropping
the framerate counter, and gdb reported instruction bytes as a frame count
(`painted=1223110688`).  Guarded now by `PROBE_SYMS` + `make probe-audit`
(`docs/method-lessons.md`).

---

## Phase 1 — The reference loop 🔧  ⭐ DO THIS FIRST

`docs/bbc-reference-loop.md`.  There is no `atari800` here; ground truth has to be built.

1. ✅ Install + smoke-test jsbeeb (headless oracle).  Working, vendored at `tools/jsbeeb`
   (git-ignored). b2 not attempted yet (`cmake` missing on this machine) — not blocking, since
   jsbeeb's `debugInstruction` hooks already cover breakpoints/registers/peek/poke.
2. 🔧 **Boot the real disc, break at the engine entry, dump RAM, diff against
   `disasm/revs_mem.bin`.** Done for Silverstone: `$1200-$6FFF` (the whole REVS2 engine) and the
   SILVER track data are **byte-identical** to `disasm/revs_mem.bin` — no `ssd_load.py` fix was
   needed for the regions that matter. Remaining diffs are zero-page/workspace/ROM, which
   `revs_mem.bin` never modelled and don't affect the transpiler's inputs. Not yet repeated for
   the four expansion tracks, and not yet wired into `make` as a repeatable check. Full detail and
   the scripted commands: `docs/bbc-reference-loop.md` status section.
3. ⬜ Capture reference state at named milestones, committed as files.

**Exit criteria:** the composed memory image is confirmed byte-correct against a real BBC, and a
scripted "boot to milestone, dump RAM" command exists. **The scripted command exists
(`tools/bbc_refloop_smoke.mjs`, `tools/bbc_refloop_track_diff.mjs`) and Silverstone is confirmed
byte-correct where it matters; the four expansion tracks are not yet checked**, so this phase
isn't closed out yet.

> ⚠ Zero-page/workspace addresses derived from `revs_mem.bin` are still provisional (real MOS/
> BASIC leaves them populated; `ssd_load.py` doesn't model that). Addresses inside `$1200-$6FFF`
> (REVS2) and the per-track data blocks are now measured, not provisional, for Silverstone.

---

## Phase 2 — Complete static map ⬜

> ⚑ **Cheaper than the postmortem assumed:** a fully annotated source reconstruction of BBC Revs
> exists (`docs/reference-sources.md`), so items 1 and 4 are **cross-checks against a reference**
> rather than open-ended searches.  The reference has no licence — it is a map, never a source.

1. **The entry-point sweep** — `docs/entrypoint-sweep.md`.  Every indirect jump, RTS-dispatch
   table, OS vector and hardware vector enumerated and seeded in
   `ghidra_scripts/entrypoints.csv`, before any C is generated.  Self-modifying routines listed —
   including the **known** case: the per-track engine patches (`ModifyGameCode`, `CallTrackHook`,
   `Hook*`), whose inventory falls out of the Phase 1 before/after RAM dumps.
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
  rules; a CRTC/ULA analyser is `docs/bbc-reference-loop.md` step 5).  Note the MODE 7 teletext
  title screen (`5TRSCRN`) is a separate rendering problem from the 3D view.
- **Input: mouse + keyboard** (decided).  A racing sim's feel lives here — and the steering
  response curve is real logic in the binary (there is a per-track `HookJoystick` and a
  "SPACE — amplify steering" key), so read it out rather than tuning by feel.
- Sound: SN76489 (3 tone + 1 noise) → Paula.
- Five tracks: the engine is one binary, but behaviour is per-track (the expansion tracks are
  executable and patch it).  Decide how track selection works on the Amiga — the BBC's `REVSMEN`
  menu is BASIC and is not being ported.

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
