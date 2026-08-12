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
| **Target release** | ***Revs Plus Revs 4 Tracks*, © Superior/Acornsoft 1986** — a genuine commercial release: five circuits on ONE engine, with Computer Assisted Steering. Verified free of fan modification (see the three-way engine diff in `docs/reference-sources.md`) |
| **Steering / input** | **Mouse + keyboard.** The BBC's own `SHIFT+f1` keyboard mode is the faithful precedent; the mouse replaces the uPD7002 analogue axis. No joystick requirement |
| **Host renderer** | **None.** Not an SDL port — `make validate` + host algebra proofs only |
| Reference material | Moxon's annotated reconstruction is a **map, not a source**: no licence, so never copied from. `docs/reference-sources.md` |
| **A sixth circuit** | **Yes — include the Nürburgring.** Crammond's own track, from the C64 *Revs+* (© Firebird 1987); on the BBC it exists only as Mark Moxon's data conversion, whose engine is byte-identical to the 1986 release (no game code involved). Clean route: **extract the track from a C64 Revs+ image ourselves**, putting every circuit on the same Crammond-original footing rather than copying Moxon's conversion. Cost: BBC and C64 track formats differ and the expansion tracks are executable hook programs, so it means reproducing a conversion, not copying a file. Scheduled in Phase 5 (five BBC tracks first). `docs/reference-sources.md` §A sixth circuit |

## Open decisions

1. **Machine target.** RoF ended up needing 1 MB and not fitting a bare 512 KB A500. Revs is a
   much smaller binary (24 KB engine) but the track/render buffers are unknown. Decide the minimum
   spec when the first real measurement exists, not before.

## The source binary

`revs.ssd` — 200 KB single-sided 80-track Acorn DFS image, title "REVINST", `*OPT 4,3`.
***Revs Plus Revs 4 Tracks*, © Superior/Acornsoft 1986** — "Revs Plus Revs 4 Tracks / Computer
Assisted Steering / Copyright (c) Superior/Acornsoft 1986".

```
$ python3 tools/ssd_map.py revs.ssd
file            load    exec  length sector  offset  ends
$.REVS2*        1200    1200    5E00     97    6100  7000    <- the engine (24 KB), ONE binary, all 5 tracks
$.SILVER*       70DB       0     739     89    5900  7814    <- Silverstone (1985) -- PASSIVE DATA
$.SNETTER*      70DB    70DB     738     81    5100  7813    <- Snetterton     \
$.OULTON*       70DB    70DB     739     73    4900  7814    <- Oulton Park     | the four expansion
$.DONING*       70DB    70DB     73C     65    4100  7817    <- Donington Park  | tracks -- EXECUTABLE
$.BRANDS*       70DB    70DB     73A     57    3900  7815    <- Brands Hatch    /
$.5TRSCRN*      7C00    7C00     400     53    3500  8000    <- MODE 7 title screen
$.REVSMEN*      1900    1900     433     48    3000  1D33    <- the track menu
$.REVINST*      1900    8023    2C40      3     300  4540    <- BASIC instructions
$.!BOOT*           0       0      30      2     200  30
```

`!BOOT` is `*BASIC` / `PAGE=&1900` / `*FX21` / `CLOSE#0:CHAIN "REVINST"`, and `REVSMEN`'s per-track
branch is `*LO.<TRACK>` then `*/REVS2` — **track data first, engine second, engine runs.**

⚠⚠ **The four expansion track files patch the engine at runtime** (`ModifyGameCode` /
`CallTrackHook` / per-track hooks) and generate track geometry at runtime. The exec addresses above
are the binary's own evidence: Silverstone's data is exec `$0000` (not executable) while all four
expansion tracks are exec `$70DB` — they are *programs*. So the bytes the engine executes differ
per track, and this is self-modifying code by construction.

✅ **The engine is authentic.** Three discs were diffed while scaffolding: this 1986 engine is
**byte-identical** to the one in Mark Moxon's Revs+ compilation (0 of 24064 bytes differ — his
patch is entirely in the track files), and differs from the **1985** single-track engine in 974 of
24064 bytes, which is a real Superior/Acornsoft revision. Full table:
`docs/reference-sources.md`.

⚠ **`disasm/revs_mem.bin` is still a reconstruction.** The load *order* is derived from the menu's
own BASIC, but what the MOS/BASIC left resident is not modelled, and the image is the pre-patch
state. Proving it against a real BBC is Phase 1 — `docs/bbc-reference-loop.md`.

No `.ssd` is committed (copyrighted binaries, same policy as RoF's `.xex`).

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
revs.ssd                Revs Plus Revs 4 Tracks, 1986 (read-only, git-ignored)
revs-hack-nurburgring.ssd  Moxon's Revs+ compilation — kept only as the sole copy of the
                        Nurburgring track data; its engine is identical to revs.ssd's (git-ignored)
revsplus.d64            C64 Revs+ (Firebird 1987) — the source for extracting Crammond's own
                        Nurburgring track data ourselves, per the sixth-circuit decision
                        (git-ignored; unused until Phase 5)
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
      build links with clean muldiv and probe-symbol audits; a headless FS-UAE run reports
      `vbi=824 painted=803` on an `FPSCOUNT=1` build (≈48.8 FPS with nothing yet to draw) and
      `painted=0` on a plain one — display takeover, 50 Hz VERTB handler, copper list, frame pump
      and the embedded 6502 image all verified on the target.
- [x] **Phase 1 — The BBC reference loop.** jsbeeb and b2 both installed and working
      (`tools/jsbeeb`, `tools/b2`); all five tracks' REVS2 engine range and track data confirmed
      byte-identical to `disasm/revs_mem.bin`. Exit criteria met — remaining nice-to-haves
      (named-milestone captures, the jsbeeb cycle-diff harness, the CRTC/ULA analyser) are
      deferred until a later phase actually needs them; they no longer gate Phase 2.
      `docs/bbc-reference-loop.md` status section.
- [ ] **Phase 2 — Complete static map** ← next (entry-point sweep, hardware map, MOS-call inventory, naming)
- [ ] Phase 3 — Transpiler quality, then generate
- [ ] Phase 4 — End-to-end skeleton on the target, then profile, then set a target
- [ ] Phase 5 — Render + input
- [ ] Phase 6 — Native twins, then asm
- [ ] Phase 7 — Packaging

See `docs/phases.md` for exit criteria and the gating between phases.

## Immediate next step

Phase 1 is done (exit criteria met — see status table above). **Phase 2 is next**: the entry-point
sweep, the hardware-access map, the MOS-call inventory, and a first behavioural-naming pass into
`disasm/symbols.csv` (`docs/phases.md` Phase 2, `docs/entrypoint-sweep.md`). This needs Ghidra set
up (`tools/ghidra/`, ~2 GB), which hasn't happened yet in this repo.

Deferred rather than blocking: named-milestone captures and the jsbeeb cycle-diff harness — both
exist to check a *port's* behaviour against real hardware, and there's no port yet to check.
Revisit when Phase 4 or 6 need them. Full detail: `docs/bbc-reference-loop.md` status section.
