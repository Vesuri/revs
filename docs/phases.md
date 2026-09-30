# Project status and subsystem gates

The initial porting phases are complete and version 0.90 is packaged. This is a
status map, not a development log or a second task queue. Current candidates are
in [open work](open-work.md); detailed milestones remain in Git history.

## Phase 0 Scaffolding

Complete. The host backend is headless; the Amiga backend owns its display,
interrupts and input. See [Amiga architecture](amiga-arch.md).

## Phase 1 BBC reference loop

Complete. jsbeeb provides executable ground truth, captures and lockstep runs.
See [the reference loop](bbc-reference-loop.md).

## Phase 2 Static map

Complete enough to build the port; naming remains ongoing. REVS2 relocates itself,
and the dashboard overlay is unpacked separately. Runtime addresses and indirect
entry points are curated in `disasm/symbols.csv` and
`ghidra_scripts/entrypoints.csv`. See [the static map](static-map.md).

## Phase 3 Translation

Complete. Generation handles the engine and per-track self-modifying sites.
Track patch bytes must be evaluated in each circuit's own context; a union of
legal bytes cannot select the correct unpatched arm. See [the transpiler](transpiler.md)
and the `track-smc` and `track-patch` checks.

## Phase 4 Target execution

Complete. The engine runs under the Amiga hardware layer and is profiled on the
68000 target. The frame hook is `$1701`, with the frame-wait hook at `$1760`.
Front-end presentation waits are separate. See [performance method](perf-method.md).

## Phase 5 Display input sound and tracks

Complete. MODE 7 menus, race display, keyboard/mouse input, sound and circuit
selection are implemented. Circuit selection applies the replayed engine patches;
calling the original patch installer again would double-apply them.
`tracks`, `track-run` and `viewdiff` test installation, execution and rendered
results separately. Sound scheduling is compared tick by tick with the BBC.
See [BBC hardware](bbc-hardware.md), [controls](controls.md) and
[the fidelity boundary](faithfulness-seam.md).

## Phase 6 Native engine and performance

Native conversion and the source review are complete. Measured hot paths have
68000 implementations with C differential controls. Performance work continues
against the current renderer; do not repeat completed conversion campaigns.
See [native maintenance](native-maintenance.md) and [rendering design](span-render-plan.md).

## Phase 7 Packaging

Version 0.90 has a WHDLoad slave, installer and release archive. The executable
loads REVS2 from the user's disc rather than embedding the original engine.
The plain executable targets 1 MB; the WHDLoad installation needs 2 MB.
The slave requests 400 KB chip and 512 KB expansion memory in addition to its
Kickstart image. See [release documentation](whdload.md) for exact requirements,
installer reuse behaviour and test procedures.
