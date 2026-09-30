# Documentation

Use this index to find the current instructions and technical references. Open
work belongs in one queue; completed development logs are available in Git history.

## Getting started

- [Project overview](../README.md)
- [Toolchain and source generation](toolchain.md)
- [Controls](controls.md)
- [Release packaging and installer](whdload.md)
- [Working rules](../CLAUDE.md)
- [Open work](open-work.md) and [unresolved names](rename.md)

## Architecture and fidelity

- [Amiga architecture](amiga-arch.md) and [hardware lessons](amiga-lessons.md)
- [BBC hardware](bbc-hardware.md) and [static binary map](static-map.md)
- [Reference sources and provenance](reference-sources.md)
- [Transpiler](transpiler.md) and [generated file ownership](../src/gen/README.md)
- [Native and platform boundary](faithfulness-seam.md)
- [Native engine and ABI maintenance](native-maintenance.md)
- [Wide values and memory ownership](wide-value-cleanup.md)

## Validation and performance

- [Validation harness](validation-harness.md)
- [BBC reference loop](bbc-reference-loop.md)
- [Entry point discovery](entrypoint-sweep.md)
- [Amiga diagnostic tools](../amiga/README.md) and [headless runs](headless-fsuae.md)
- [Performance measurement](perf-method.md)
- [68000 optimisation](m68k-optimisation.md)
- [Investigation methods](method-lessons.md)
- [Rendering architecture and remaining design questions](span-render-plan.md)

Technical references retain evidence needed to understand the implementation.
Measurements describe their recorded configuration, not a promise about the current
build. Re-measure before using them to choose an optimisation.
