# Toolchain & how to run the pipeline

All paths relative to the project root (`Revs/`).  Host is macOS (Apple Silicon).

## The pipeline

```
revs.ssd
  └─ tools/ssd_map.py  ──► the DFS catalogue (what files, load/exec/length)
  └─ tools/ssd_load.py ──► disasm/files/*.bin + disasm/revs_mem.bin (post-load image)
                           ⚠ load order is a HYPOTHESIS — see docs/bbc-reference-loop.md
        └─ Ghidra headless (ghidra_scripts/) ──► disasm/listing.txt, xrefs, hw-access map
              └─ disasm/symbols.csv  ◄── shared, grows over time (addr → name/type/is_hw)
                    └─ tools/transpile.py ──► src/gen/*.c   (transliterated 6502)
                          ├─ src/cpu/       6502 register/flag model + memory bus
                          ├─ src/platform/  platform.h abstraction (the hardware boundary)
                          └─ src/platform/{host,amiga}/  concrete backends
```

## Installed / needed

| Tool | Where | Purpose |
|---|---|---|
| Python 3 | `python3` | disc tools, transpiler |
| clang / make | system | the host build + `make validate` |
| m68k-amiga-elf-gcc, vasm, elf2hunk | `~/.local` (`. amiga/env.sh`) | Amiga cross-build ✅ present |
| FS-UAE + `m68k-amiga-elf-gdb` | `~/.local/fs-uae` (same `env.sh`) | Amiga measurement loop ✅ present |
| Kickstart 3.1 | `$KICKSTART` | FS-UAE boot ✅ present |
| **Ghidra + JDK 21** | not installed yet | disassembly (see below) |
| **jsbeeb / b2** | not installed yet | BBC reference loop — `docs/bbc-reference-loop.md` |

Ghidra on the Atari port lived under `tools/ghidra/` (~2 GB, git-ignored) with a persistent
project at `tools/ghidra-proj/` — that project *is* the annotation database.  Reproduce that
layout here.  Ghidra needs JDK 21 on `PATH`:

```sh
export JAVA_HOME="/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home"
export PATH="$JAVA_HOME/bin:$PATH"
```

> `brew install` on this machine triggers a privilege-elevation prompt, so kick installs off
> yourself or approve the prompt when one appears.

## Disc inspection

```sh
python3 tools/ssd_map.py  revs.ssd            # DFS catalogue
python3 tools/ssd_load.py revs.ssd disasm     # -> disasm/files/*.bin + revs_mem.bin + revs_blocks.txt
make image                                    # same thing, from the root Makefile
```

## Disassembly (headless Ghidra)

One-shot import + auto-analysis + listing export, mirroring the Atari port's invocation:

```sh
GH="tools/ghidra/ghidra_12.1_PUBLIC"
ABS="$(pwd)"
"$GH/support/analyzeHeadless" tools/ghidra-proj Revs \
  -import disasm/revs_mem.bin \
  -processor "6502:LE:16:default" \
  -loader BinaryLoader \
  -scriptPath ghidra_scripts \
  -preScript  MarkEntries.java \
  -postScript ExportListing.java "$ABS/disasm/listing.txt"
```

Re-run a script against the **already-imported** program (no re-analysis):

```sh
"$GH/support/analyzeHeadless" tools/ghidra-proj Revs -process revs_mem.bin \
  -scriptPath ghidra_scripts -noanalysis \
  -postScript ExportListing.java "$ABS/disasm/listing.txt"
```

⚠ **Do not open the project in the Ghidra GUI and headless at the same time** — Ghidra locks
projects.

### The iteration loop

1. Export current state → `disasm/listing.txt` (+ xref / hw dumps).
2. Read the text; work out what routines and variables do.
3. Append findings to `disasm/symbols.csv` (`addr,name,type,is_hw,note`).
4. Run `ApplyNames` → names/comments persist into `tools/ghidra-proj`.
5. Re-export and continue, subsystem by subsystem.

`tools/ghidra-proj/` is the durable annotation database; the repo holds the text exports,
`symbols.csv`, and the generated C.

### ⭐ Before the FIRST export is trusted
Do the **entry-point sweep** — `docs/entrypoint-sweep.md`.  Seeding every indirect-jump target
and OS vector *before* generating C is the single highest-leverage item carried over from the
Atari port, and Revs's self-modifying code makes it more important, not less.

### Then: one concentrated naming pass
Postmortem #1.2.  **On a binary-only project the function names are your map**, and every wrong
name taxes every later reasoning step.  Give rough-but-directionally-correct names
(`physics_*`, `track_*`, `vdu_*`) in one focused pass before deep work — not as a trickle.
`symbols.csv` → transpiler makes a later batch rename cheap, so the cost of being roughly right
early is near zero.

## Ghidra scripts (`ghidra_scripts/`)

Copied from the Atari port; each needs a Revs pass (they carry Atari address ranges).

| Script | Status | Purpose |
|---|---|---|
| `MarkEntries.java` | needs Revs entry addresses + `entrypoints.csv` | mark entry points + disassemble |
| `ExportListing.java` | reusable as-is | dump `listing.txt` |
| `DumpHwAccesses.java` | **needs the BBC ranges** (`$FC00-$FEFF` + `$0200-$0235`, not `$D000-$D7FF`) | the hardware-access map |
| `DumpZeroPage.java` | reusable | zero-page variable map |
| `DumpCallGraph.java` | reusable | call graph |
| `ApplyNames.java` | reusable | apply `symbols.csv` to the project |

### The hardware-access map is the abstraction boundary
Its output tells you exactly what `platform.h` must expose, and — just as importantly — which of
the **[ASSUMED]** rows in `docs/bbc-hardware.md` are real.  Generate it early.

## The host build

```
make                     # build/revs (headless — no renderer, deliberately)
make validate            # the native-twin differential
make validate FN=<sub>   # only matching tests — use this
make endian-lint         # the wide-pointer-alias guard
make gen                 # regenerate src/gen from disasm/listing.txt
make image               # rebuild disasm/revs_mem.bin from revs.ssd
```

Why no host renderer: `src/platform/host/PlatformHost.h`.

## The Amiga build

```
cd amiga && . ./env.sh
make                     # out/Revs.exe  (+ Revs.elf for debug, and a muldiv audit on every link)
make clean               # ⚠ mandatory before a PROBES build / after a header edit
./run.sh                 # boot in FS-UAE (left mouse quits)
./debug.sh               # source-level debug via the FS-UAE gdb stub
./diag_run.sh [secs]     # headless probe run (needs PROBES=1)
```

Details and traps: `docs/headless-fsuae.md`.
