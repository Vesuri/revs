# Revs — BBC Micro → Amiga reimplementation

Reimplement Geoff Crammond's 1985 BBC Micro F1 simulation *Revs* (Acornsoft) on the Amiga by
reverse-engineering the BBC binary, converting the 6502 machine code to portable C, abstracting
all hardware access behind a platform layer, and implementing that layer for the Amiga.

*Revs* — real vehicle dynamics and a true 3D Silverstone — **never received an Amiga port**, so
this is genuine preservation rather than a re-release.

This is the same pipeline used for *Rescue on Fractalus!* (Atari 8-bit → Amiga), and before that
*Attack of the PETSCII Robots* (C64 → Amiga) and the *Stunt Car Racer* 68000 work. What is
different this time is the sequencing: `docs/postmortem.md` is the RoF retrospective, and this
project is deliberately built in the order that document argues for.

## Decisions (locked)

| Question | Decision |
|---|---|
| Disassembly/analysis workflow | **Claude drives Ghidra headless**; user reviews text artifacts in-repo |
| Conversion ordering | **C conversion first**, then understand & rename *in the C* |
| Fidelity | **Faithful 1:1 port** — replicate behaviour exactly (incl. the vehicle dynamics), parity before any improvement |
| Ground truth | **jsbeeb** (scriptable headless oracle) + **b2** (interactive/HTTP debugger). MAME `bbc` held in reserve. Never the dev-host backend |
| Host build | **No renderer.** `make validate` + host algebra proofs only — the RoF SDL backend's approximation cost real time |
| Repo | Commit directly to `main`, one logical change per commit |

## Open decisions — ⚠ ASK BEFORE ASSUMING

1. **Which release is the target?** The postmortem named the **four-track expansion** (one engine
   + swappable track data, so one port rather than four). The image in this repo (`revs.ssd`) is
   the **single-track Silverstone** release — `Silvers` is the only track file on it. Options:
   target this image and add tracks later; obtain the four-track image now and target that; or
   build for swappable data from the start regardless.
2. **Steering input on the Amiga.** The BBC reads an analogue axis via the uPD7002 ADC. Analogue
   joystick, mouse, or keyboard? A racing sim's *feel* lives here, so this is a design decision,
   not an implementation detail. (The documented BBC keys include a "SPACE — amplify steering",
   which suggests the analogue response matters a lot.)
3. **Machine target.** RoF ended up needing 1 MB and not fitting a bare 512 KB A500. Revs is a much
   smaller binary (24 KB engine) but the track/render buffers are unknown. Decide the minimum
   spec when the first real measurement exists, not before.

## The source binary

`revs.ssd` — 200 KB single-sided 80-track Acorn DFS image, title "CAR", `*OPT 4,3` (`*EXEC !BOOT`).

```
$ python3 tools/ssd_map.py revs.ssd
file            load    exec  length sector  offset  ends
$.Revs2*        1200    1200    5E00     60    3C00  7000     <- the machine-code engine (24 KB)
$.Revs1*        2000    2000     5AF     54    3600  25AF     <- loads INSIDE Revs2's range
$.Silvers*      70DB       0     739     46    2E00  7814     <- Silverstone track data
$.REVS*         1900    8023     700     39    2700  2000     <- BASIC (+ machine code); *R.Revs2
$.Car*          1900    8023    23DB      3     300  3CDB     <- BASIC front end
$.!BOOT*           0       0      2C      2     200  2C
```

`!BOOT` is `*BASIC` / `PAGE=&1900` / `*FX21` / `CLOSE#0:CHAIN "CAR"`. `Car` identifies itself as
"Revs / BBC Version 1 / Copyright (c) Acornsoft Limited 1985" and documents the keys.

⚠ **`disasm/revs_mem.bin` is a reconstruction, not a fact.** A DFS disc has no segment table (an
Atari `.xex` does), the files overlap, and a BASIC loader decides the real sequence. Proving that
image correct against a real BBC is Phase 1 — see `docs/bbc-reference-loop.md`.

The `.ssd` is **not committed** (copyrighted binary, same policy as RoF's `.xex`).

## Approach / pipeline

Because the target is a faithful 1:1 port and the code is converted before it is fully understood,
the first C pass is a **mostly-mechanical transliteration** (a static recompilation): every 6502
instruction maps to a C statement over emulated registers and a real 64 KB memory array, with
`JSR`→call, branches→`goto`, and RAM vs I/O split at a memory bus. A literal translation is
faithful by construction; it is then refactored toward idiomatic, named C as understanding grows.
A shared symbol map (`disasm/symbols.csv`) drives both the disassembly annotations and the C
generator, so a name learned once propagates everywhere.

Hot functions are replaced with hand-written native twins proven byte-identical to the
transliteration by `make validate`, and the hottest of those escalate to hand-written m68k asm —
on the RoF port asm beat GCC by ~27% on the hot inner loop where four C restructurings had all
regressed.

**What is genuinely new versus the Atari port:**
- **No `atari800`.** The reference loop has to be built (jsbeeb + b2).
- **The game runs under an OS.** RoF replaced the Atari OS wholesale; Revs reaches the keyboard,
  the ADC and the disc through MOS calls (OSBYTE/OSWORD), which must be enumerated and serviced.
- **Different video hardware.** 6845 CRTC + Video ULA, not ANTIC/GTIA — so the display-composition
  analyser has to be re-tooled.
- **Different hot path.** Vehicle dynamics rather than a terrain rasterizer: different loop shape,
  same lever.

## Repository layout

```
revs.ssd                original disc image (read-only, git-ignored)
CLAUDE.md               always-loaded working instructions
PROJECT.md              this file
docs/                   the reference docs (see CLAUDE.md's index)
tools/
  ssd_map.py            DFS catalogue dumper
  ssd_load.py           builds disasm/revs_mem.bin + revs_blocks.txt
  transpile.py          6502 listing → C generator (needs a Revs pass)
  validate_native.c     the make-validate harness
ghidra_scripts/         headless Ghidra scripts + entrypoints.csv
disasm/                 generated listing + the curated symbols.csv
src/
  cpu/                  6502 register/flag model, memory bus, 68000 math helpers
  gen/                  generated C (git-ignored) + hand-written twins
  platform/             platform.h abstraction + C bridge
    host/               headless dev backend (no renderer, deliberately)
    amiga/              PlatformAmiga + Revs scene + the vendored dA JoRMaS framework
amiga/                  Amiga build infrastructure: Makefile, env.sh, run.sh, debug.sh, *.gdb
```

## Status

- [x] **Phase 0 — Scaffolding.** Host build + `make validate` + `make endian-lint` clean; Amiga
      build links with a clean muldiv audit; a headless 25 s FS-UAE run reports
      `vbi=1075 painted=1054` — display takeover, 50 Hz VERTB handler, copper list, frame pump and
      the embedded 6502 image all verified on the target.
- [ ] **Phase 1 — The BBC reference loop** ← next, and it gates everything
- [ ] Phase 2 — Complete static map (entry-point sweep, hardware map, MOS-call inventory, naming)
- [ ] Phase 3 — Transpiler quality, then generate
- [ ] Phase 4 — End-to-end skeleton on the target, then profile, then set a target
- [ ] Phase 5 — Render + input
- [ ] Phase 6 — Native twins, then asm
- [ ] Phase 7 — Packaging

See `docs/phases.md` for exit criteria and the gating between phases.

## Immediate next step

Phase 1, first task: **install jsbeeb and b2, boot `revs.ssd`, break at the engine entry, dump RAM,
and diff it against `disasm/revs_mem.bin`.** Until that diff is clean, every address derived from
the composed image is provisional — so it is the gate for the whole static-analysis phase.
