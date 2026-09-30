# Toolchain & how to run the pipeline

All paths relative to the project root (`Revs/`).  Host is macOS (Apple Silicon).

## The pipeline

```
revs.ssd
  └─ tools/ssd_map.py  ──► the DFS catalogue (what files, load/exec/length)
  └─ tools/ssd_load.py ──► disasm/files/*.bin + disasm/revs_mem.bin (post-load image)
                           ⚠ PRE-PATCH + unconfirmed — see docs/bbc-reference-loop.md
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
| **Ghidra 12.1 + JDK 21** | ✅ present (`tools/ghidra`, see below) | disassembly (see below) |
| **jsbeeb / b2** | ✅ present (`tools/jsbeeb`, `tools/b2`) | BBC reference loop — `docs/bbc-reference-loop.md` |

`tools/ghidra` is a **symlink to a shared install at `~/.local/share/ghidra`**, not a per-repo
copy — the Rescue on Fractalus repo's `tools/ghidra` points at the same place, so the ~900 MB
extracted distribution exists once on disk rather than once per binary-only-port repo. (No
trailing slash on the `tools/ghidra` gitignore entry — a trailing-slash pattern doesn't match a
symlink to a directory, only a real one.) `tools/ghidra-proj/` stays per-repo — that project *is*
the annotation database, and it must not be shared. Ghidra needs JDK 21 on `PATH` (installed via
`brew install openjdk@21`):

```sh
export JAVA_HOME="/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home"
export PATH="$JAVA_HOME/bin:$PATH"
```

> `brew install` on this machine triggers a privilege-elevation prompt, so kick installs off
> yourself or approve the prompt when one appears.
>
> ⚠ **Verify a fresh Ghidra extraction has `support/analyzeHeadless` before trusting it.** The
> copy found under RoF's `tools/ghidra` on this machine had been pruned down to ~180 MB at some
> point (missing `support/`, `ghidraRun`, `server/` — just `Ghidra/`, `Extensions/`, `GPL/`,
> `docs/` survived), which silently breaks headless use while looking like a normal install at a
> glance. Re-extracted from the official 12.1 release zip
> (`ghidra_12.1_PUBLIC_20260513.zip`, GitHub releases) to fix it — full size is ~874 MB.

⚠ **Before Ghidra, read `docs/reference-sources.md`.**  An annotated source reconstruction of BBC
Revs already exists, which changes how Phase 2 is run (cross-check, not search) — and it carries
no licence, so it is a map and never something to copy from.

## Disc inspection

```sh
python3 tools/ssd_map.py  revs.ssd                    # DFS catalogue
python3 tools/ssd_load.py revs.ssd disasm             # -> files/*.bin + revs_mem.bin (track SILVER)
python3 tools/ssd_load.py revs.ssd disasm BRANDS      # a different circuit
make image                                            # the default track, from the root Makefile
```

Tracks on the disc: `SILVER` `BRANDS` `DONING` `OULTON` `SNETTER`.  ⚠ The image is the
**pre-patch** state — the four expansion track files are executable and patch the engine at
startup (`docs/reference-sources.md`).

## Disassembly (headless Ghidra)

⚠ **`analyzeHeadless` needs JAVA_HOME set** or it dies with "Unable to locate a Java Runtime"
and, headless, "no TTY detected" rather than prompting:

```sh
export JAVA_HOME="/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home"
export PATH="$JAVA_HOME/bin:$PATH"
```

⚠⚠ **The imported program is `revs_runtime.bin`, NOT `revs_mem.bin`** — the Phase 2 self-unpack
finding (`docs/static-map.md`).  Everything below names it accordingly; a `-process
revs_mem.bin` finds no such program and exports nothing while still exiting 0.

One-shot import + auto-analysis + listing export, mirroring the Atari port's invocation:

```sh
GH="tools/ghidra/ghidra_12.1_PUBLIC"
ABS="$(pwd)"
"$GH/support/analyzeHeadless" tools/ghidra-proj Revs \
  -import disasm/revs_runtime.bin \
  -processor "6502:LE:16:default" \
  -loader BinaryLoader \
  -scriptPath ghidra_scripts \
  -preScript  MarkEntries.java \
  -postScript ExportListing.java "$ABS/disasm/listing.txt"
```

**Re-seed entry points and re-export** — the loop to run after adding a row to
`ghidra_scripts/entrypoints.csv` (Phase 4 used it to disassemble `$1DC5`, which no static walk
reaches).  `MarkEntries` must be a `-preScript` so the new entry is disassembled by the analysis
that follows it; with `-noanalysis` the seed is recorded and nothing decodes:

```sh
"$GH/support/analyzeHeadless" tools/ghidra-proj Revs -process revs_runtime.bin \
  -scriptPath ghidra_scripts \
  -preScript  MarkEntries.java \
  -postScript ExportListing.java "$ABS/disasm/listing.txt"
```

Re-run a script against the already-imported program with **no** re-analysis (export only):

```sh
"$GH/support/analyzeHeadless" tools/ghidra-proj Revs -process revs_runtime.bin \
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
Atari port, and Revs's per-track engine patching makes it more important, not less.

### Then: one concentrated naming pass
Postmortem #1.2.  **On a reverse-engineering project the function names are your map**, and every wrong
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
make image               # rebuild disasm/revs_mem.bin (make image TRACK=BRANDS for another)
```

Why no host renderer: `src/platform/host/PlatformHost.h`.

## The Amiga build

```
cd amiga && . ./env.sh
make                     # out/Revs.exe  (+ Revs.elf for debug, and a muldiv audit on every link)
make clean               # ⚠ mandatory before a PROBES build / after a header edit
./run.sh                 # boot in FS-UAE (CTRL-Q quits); stages ../revs.ssd beside the exe
./debug.sh               # source-level debug via the FS-UAE gdb stub
./diag_run.sh [secs]     # headless probe run (needs PROBES=1)
```

⚠ **Every Amiga build reads the engine off `revs.ssd` at startup** (nothing is `.incbin`'d any
more), so the FS-UAE scripts copy `../revs.ssd` next to the exe; `$REVS_DISC` picks another image.
`make DIST=1` is the release build and `make dist` (repo root) the archive — `docs/whdload.md`.

Details and traps: `docs/headless-fsuae.md`.
