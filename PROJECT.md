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
| **Target release** | **Revs+** (Moxon's 2022 compilation): six circuits on ONE engine binary. Beats the authentic two-disc split (1985 Revs + 1986 Revs 4 Tracks) on scope-per-effort. ⚠ It is a fan compilation — see `docs/reference-sources.md` |
| **Steering / input** | **Mouse + keyboard.** The BBC's own `SHIFT+f1` keyboard mode is the faithful precedent; the mouse replaces the uPD7002 analogue axis. No joystick requirement |
| **Host renderer** | **None.** Not an SDL port — `make validate` + host algebra proofs only |
| Reference material | Moxon's annotated reconstruction is a **map, not a source**: no licence, so never copied from. `docs/reference-sources.md` |

## Open decisions

1. **Machine target.** RoF ended up needing 1 MB and not fitting a bare 512 KB A500. Revs is a
   much smaller binary (24 KB engine) but the track/render buffers are unknown. Decide the minimum
   spec when the first real measurement exists, not before.
2. **How far to chase Revs+'s own modifications.** Its engine differs from the 1985 original in
   974 bytes, and Computer Assisted Steering comes from the Superior Software variant. A faithful
   port of a compilation should at least *know* which behaviour is the compilation's — decide
   whether any of it should be optional at runtime once the diff is understood.

## The source binary

`revs.ssd` — 200 KB single-sided 80-track Acorn DFS image, title "REVINST", `*OPT 4,3`.

```
$ python3 tools/ssd_map.py revs.ssd
file            load    exec  length sector  offset  ends
$.REVS2*        1200    1200    5E00    105    6900  7000    <- the engine (24 KB), ONE binary for all 6 tracks
$.SNETTER*      70DB    70DB     7D0     97    6100  78AB    <- Snetterton        \
$.SILVER*       70DB    70DB     739     89    5900  7814    <- Silverstone (1985)|
$.OULTON*       70DB    70DB     7D0     81    5100  78AB    <- Oulton Park      | six circuits,
$.NURBURG*      70DB    70DB     7D0     73    4900  78AB    <- Nurburgring (C64)| all at $70DB
$.DONING*       70DB    70DB     7D0     65    4100  78AB    <- Donington Park   |
$.BRANDS*       70DB    70DB     7D0     57    3900  78AB    <- Brands Hatch     /
$.PLUSCRN*      7C00    7C00     400     53    3500  8000    <- MODE 7 title screen
$.REVSMEN*      1900    8023     49B     48    3000  1D9B    <- BASIC track menu
$.REVINST*      1900    8023    2C78      3     300  4578    <- BASIC instructions
$.!BOOT*           0       0      30      2     200  30
```

`!BOOT` is `*BASIC` / `PAGE=&1900` / `*FX21` / `CLOSE#0:CHAIN "REVINST"`, and `REVSMEN`'s per-track
branch is `*LO.<TRACK>` then `*/REVS2` — **track data first, engine second, engine runs.**

This is **Revs+**, Mark Moxon's 2022 compilation: "Variant: Revs+ / Contains the Nurburgring track
from the Commodore 64, backported by Mark Moxon / Computer Assisted Steering / Copyright (c) 1985,
1986, 2022".

⚠⚠ **The track file patches the engine at runtime** (`ModifyGameCode` / `CallTrackHook` /
per-track hooks), and the extra tracks generate geometry at runtime. So the bytes the engine
executes differ per track, and this is self-modifying code by construction.
⚠ **It is a fan compilation, not a pristine original** — measured: its `REVS2` differs from the
1985 single-track engine in **974 of 24064 bytes**, same length. Silverstone's track data is
byte-identical between the two discs. The 1985 disc is kept locally as
`revs-1985-silverstone.ssd` so that diff stays available.
⚠ **`disasm/revs_mem.bin` is still a reconstruction.** The load *order* is now derived from the
menu's own BASIC, but what the MOS/BASIC left resident is not modelled, and the image is the
pre-patch state. Proving it against a real BBC is Phase 1 — `docs/bbc-reference-loop.md`.

Full account: `docs/reference-sources.md`.

Neither `.ssd` is committed (copyrighted binaries, same policy as RoF's `.xex`).

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
- **It is not binary-only.** A fully annotated source reconstruction exists, which turns the two
  highest-leverage postmortem items (entry-point sweep, naming pass) from searches into
  cross-checks. It has no licence, so it is a map and never a source —
  `docs/reference-sources.md`.
- **No `atari800`.** The reference loop has to be built (jsbeeb + b2).
- **Self-modifying code by design.** The track files patch the engine as it starts.
- **The game runs under an OS.** RoF replaced the Atari OS wholesale; Revs reaches the keyboard,
  the ADC and the disc through MOS calls (OSBYTE/OSWORD), which must be enumerated and serviced.
- **Different video hardware.** 6845 CRTC + Video ULA, not ANTIC/GTIA — so the display-composition
  analyser has to be re-tooled.
- **Different hot path.** Vehicle dynamics rather than a terrain rasterizer: different loop shape,
  same lever.

## Repository layout

```
revs.ssd                Revs+ disc image (read-only, git-ignored)
revs-1985-silverstone.ssd  the original 1985 release, kept for the engine diff (git-ignored)
CLAUDE.md               always-loaded working instructions
PROJECT.md              this file
docs/                   the reference docs (see CLAUDE.md's index)
tools/
  ssd_map.py            DFS catalogue dumper
  ssd_load.py           builds disasm/revs_mem.bin + revs_blocks.txt (per track)
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

Two things that make that first task richer than it looks:
- Dump RAM **twice** — before and after the track hook code runs — to see exactly which engine
  bytes a track patches. That is the self-modifying-code inventory, obtained for free.
- Dump for **two different tracks** and diff. The difference is the per-track behaviour surface.
