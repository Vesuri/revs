# Revs for Amiga

An Amiga port of Geoff Crammond’s BBC Micro racing simulation, based on
*Revs Plus Revs 4 Tracks* (Superior/Acornsoft, 1986). The engine is reconstructed
from the BBC binary, translated to C, and implemented with native C and 68000
routines behind an Amiga hardware layer.

The port includes the race view, menus, sound, mouse and keyboard controls, five
original circuits and optional Nürburgring data. Version 0.90 includes a WHDLoad
installer. Development continues on performance and fidelity checks.

## Playing

The plain executable targets a 68000 Amiga with 1 MB RAM (512 KB chip plus 512 KB
slow RAM). The WHDLoad package requires 2 MB and a Kickstart 1.3 image for its
kickemu slave. See [release and installation details](docs/whdload.md).

You must supply the original `revs.ssd` disc image. The executable loads its engine
at startup; the repository does not distribute the disc. See the
[controls](docs/controls.md) and packaged [ReadMe](whdload/Revs%20Install/ReadMe).

## Building

This is not a standalone source checkout: the disc, generated sources, Ghidra
listing and external toolchains are local prerequisites. Follow
[the toolchain guide](docs/toolchain.md) to reconstruct them.

With the generated inputs available:

```sh
make                         # headless development executable
make validate                # native routines against the 6502 translation
make endian-lint macro-lint cpu-lint
```

For an Amiga build:

```sh
cd amiga
. ./env.sh
make
./run.sh                     # FS-UAE; requires a local Kickstart ROM
```

Run `make dist` from the repository root to create the WHDLoad archive. Clean
before switching build flags. The host executable has no renderer: jsbeeb is the
BBC visual reference and FS-UAE runs the Amiga output.

## Repository guide

| Directory | Contents |
|---|---|
| `src/gen/` | Handwritten native engine and ABI seams; generated oracle files stay local |
| `src/cpu/` | 6502 state model and 68000 arithmetic helpers |
| `src/platform/` | Shared hardware models and host/Amiga backends |
| `amiga/` | Cross-build, emulator runners and reusable debugger probes |
| `tools/` | Generation, validation, profiling and release tools |
| `disasm/` | Curated symbols and binary analysis; derived images stay local |
| `ghidra_scripts/` | Analysis scripts and indirect entry points |
| `whdload/` | Slave and installer sources |
| `docs/` | Architecture, validation, maintenance and reference documentation |

Start with the [documentation index](docs/README.md).
[Open work](docs/open-work.md) and `make todo` track remaining tasks.
[CLAUDE.md](CLAUDE.md) contains contributor and agent working rules.

## Original material

Game images and generated game data are excluded from Git. The annotated BBC
source reconstruction is used as a reference, not copied into this project.
See [reference sources and provenance](docs/reference-sources.md) and the
[vendored framework provenance](src/platform/amiga/framework/UPSTREAM.md).
