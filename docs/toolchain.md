# Toolchain and source generation

The development setup is macOS with a native C/C++ compiler, Python 3 and Make.
A fresh checkout also needs the original disc and local analysis tools before it
can generate or compile the engine. Keep disc images and their derived data out
of Git. Read [reference provenance](reference-sources.md) before disassembly.

## Prerequisites

| Tool or input | Purpose |
|---|---|
| `revs.ssd` | Original 1986 five-circuit release; supplied locally |
| Python 3, C/C++ compiler, Make | Generation, host build and differentials |
| Ghidra 12.1 and JDK 21 | Runtime disassembly and listing export |
| `m68k-amiga-elf-gcc`, binutils, vasm, elf2hunk | Amiga build; `. amiga/env.sh` adds local paths |
| FS-UAE with its GDB stub and a Kickstart ROM | Amiga execution and profiling |
| jsbeeb under `tools/jsbeeb/`, Node 24.15 or newer | BBC reference loop; Make recipes use Volta |
| WHDLoad development files and archive tools | Release packaging; see [whdload.md](whdload.md) |

The local convention is a Ghidra installation under `tools/ghidra/` (which may be
a symlink) and a per-repository project under `tools/ghidra-proj/`. The latter is
an annotation database, not disposable build output. `tools/b2/` is an optional
interactive BBC debugger. Do not assume these ignored dependencies exist in a
new checkout.

An optional `revs-hack-nurburgring.ssd` supplies the sixth circuit. Without it,
track generation provides the five original circuits. See
[the circuit provenance](reference-sources.md) before redistributing derived data.

## Reconstructing the engine

From the repository root:

```sh
python3 tools/ssd_map.py revs.ssd
make image TRACK=SILVER
make runtime
```

`make image` creates the loader image and extracted files. `make runtime` replays
REVS2's self-unpack and then reconstructs the dashboard overlay. Disassemble
`disasm/revs_runtime.bin`, not `disasm/revs_mem.bin`. Expansion track files are
programs that patch the engine, not interchangeable passive geometry blobs.

Set `JAVA_HOME` to your JDK 21 installation. For the documented Homebrew layout:

```sh
export JAVA_HOME="/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home"
export PATH="$JAVA_HOME/bin:$PATH"
GH="tools/ghidra/ghidra_12.1_PUBLIC"
REVS_ROOT="$(pwd)"
"$GH/support/analyzeHeadless" tools/ghidra-proj Revs \
  -import disasm/revs_runtime.bin \
  -processor "6502:LE:16:default" -loader BinaryLoader \
  -scriptPath ghidra_scripts \
  -preScript MarkEntries.java \
  -postScript ApplyNames.java "$REVS_ROOT/disasm/symbols.csv" \
  -postScript ExportListing.java "$REVS_ROOT/disasm/listing.txt"
make gen
```

Adjust `GH` to the installed directory containing `support/analyzeHeadless`.
For an existing project, replace `-import disasm/revs_runtime.bin` and the loader
options with `-process revs_runtime.bin`. Run `MarkEntries` before analysis when
adding indirect targets; `-noanalysis` is appropriate only for an export that
requires no new disassembly. Keep the GUI closed while headless owns the project.

`make gen` checks track SMC extents, audits engine SMC sites, translates the engine,
and generates track data, hooks and the title page. Names come from
`disasm/symbols.csv`. Never edit generated output to apply a lasting fix.

## Ghidra scripts

| Script | Purpose |
|---|---|
| `MarkEntries.java` | Seed `entrypoints.csv` and disassemble indirect entries |
| `ApplyNames.java` | Apply the shared symbol map |
| `ExportListing.java` | Export the listing used by the transpiler |
| `DumpHwAccesses.java` | Report BBC hardware and vector accesses |
| `DumpZeroPage.java` | Report zero-page references |
| `DumpCallGraph.java` | Export the call graph |

See [entry point discovery](entrypoint-sweep.md) before changing analysis coverage.

## Build and run

```sh
make
make validate
make endian-lint macro-lint cpu-lint
```

The host executable is `build/revs` and has no renderer. Fixture targets such as
`mode7`, `sound` and `trackmenu` need their separately recorded BBC fixtures;
see [validation](validation-harness.md) and [the reference loop](bbc-reference-loop.md).

For the Amiga:

```sh
cd amiga
. ./env.sh
make
./run.sh
```

`env.sh` uses the local toolchain under `~/.local` and allows `KICKSTART` to
override its default ROM path. Clean before changing defines or build modes.
Use `./debug.sh` for interactive debugging and `./diag_run.sh` for headless runs;
see [the FS-UAE guide](headless-fsuae.md). The normal front end is rendered;
`STRAIGHT_TO_RACE=1` is an optional shortcut for diagnostics.

Root `make dist` builds the release archive. The exact packaging requirements and
installer tests are in [whdload.md](whdload.md).
