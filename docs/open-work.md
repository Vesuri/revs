# Open work

This is the maintenance queue. Remove an item when it closes; record reusable
contracts in the relevant reference document. `make todo` also scans tracked
sources for explicit unfinished-work markers. Naming questions live in
[rename.md](rename.md).

## Performance context

The latest recorded practice window is 74.28 ms of bracketed work. The race
window after the object assembly changes is 95.72 ms (`QUICKQUAL=1`); practice
omits the other cars and must not stand in for a race. These are different
workloads, not a paired comparison. See [measurement method](perf-method.md).

The default renderer uses dual playfields, tyre sprites, needle sprites and low
row ownership. The old per-frame BBC framebuffer conversion is now a cold-start
path. Plans to remove that conversion or add the existing sprites are complete.
The simulation is decoupled from painting by default.

## Analytic line renderer

The remaining architectural candidate is design D in
[the rendering design](span-render-plan.md), section 13. Its estimated ceiling is
about 5 ms; this is an estimate, not an implemented saving. The previous 34 ms
source-buffer estimate and old 48 ms staged roadmap are superseded.

Before implementation (the colour model itself is exact on all six circuits:
[§13e](span-render-plan.md), including the after-edge probe and the edge-2 rule):

- Identify the mixed-byte producer attributed to phase 23. The model cannot yet
  classify it as road or object output.
- Specify how signs, cars, markers and starting lights enter the object layer
  without paying for another full source scan.
- Preserve same-cell composition, composite cockpit boundaries, warm-up fallback,
  the surface probe and expansion-circuit behaviour.

Gate the painted rows against the current pipeline in-process on the Amiga,
exercise `LOWFULLCHECK`, and compare all affected circuits with `viewdiff`.

## Smaller performance candidates

- **Renderer driver fold:** design A needs a deletion experiment demonstrating more
  than roughly 1 ms before implementation. Prior C driver tidying was a null.
  Preserve hook-written stop slots and row backgrounds.
- **Model-state output marshal:** recorded at about 0.16 ms. The publish runs once per
  simulation step, so at the 68000's 12.5 Hz step (about 1.1 steps a painted frame) it is
  likely smaller; re-measure before pricing it. Its last native reader is
  `advance_player_section_core` at `$62E2`. Moving it is a representation change that
  re-records the determinism baselines, so batch it with another such change. The
  needle-related input marshal remains load-bearing.
- **Other cars:** the line plotter, gap walk and `scale_shape_vectors` already use
  assembly (the last: race ph43 9.98 → 9.28, practice ph15 2.06 → 1.92). Further work
  ranks below the renderer candidates unless it reduces calls per object. Ordinary
  per-car AI, staging and collision work are not unused machinery.
- **Old FPS-only measurements:** re-price the run-entry specialisation, wide-value
  campaign, span call/search flattening and direct plotter only if a matched
  millisecond experiment is useful. This is measurement work, not a rewrite queue.

`make fatscan` is a starting point for a new audit, not proof of dead state. Host
execution counts can overprice C routines replaced by target assembly. Main-loop
shim hand-offs, hook flags and shared scratch marshals need reader evidence.

The remaining ISR work is largely real work or work per painted frame. Keep the
two sound ticks distinct; the intermediate chip state is part of the contract.
Do not claim faster AUTORUN keyboard scaffolding as a shipping improvement.

## Naming and source presentation

The static map still identifies naming work in the 3D pipeline, prompt chain and
other-car AI. Verify each candidate against today's `disasm/symbols.csv` before
adding it to `rename.md`; old inventories are not authoritative.

A wholesale condensation of `src/gen/revs_native.c` comments remains deferred.
Keep local comments accurate when touching a routine, and preserve reader audits
and hardware contracts.

## Measured dead ends

Do not repeat these without a changed premise. Detailed measurements remain in
[performance method](perf-method.md) and [rendering design](span-render-plan.md).

| Approach | Recorded result or constraint |
|---|---|
| Writer-maintained framebuffer dirty map | About 8.5% slower |
| Writer-maintained consumer predicate | Producer cost exceeded the skipped work |
| Source-event consumer | +25.46 ms; ordering and address computation erased the scan saving |
| Producer events after the assembly conversion | Re-priced near the full scan cost; no gain established |
| Plotting alongside the existing sweep | Repeated losses; replacement must remove the old work |
| Road-record span painter | +7.16 ms; coarse qualification repeated the fine scan |
| Terrain blitter area fill | +5.25 ms; preparing toggles outweighed the blits |
| Dashboard rectangles | +8.12 ms; wholesale decode rates do not price scattered rectangles |
| Per-byte dashboard delta painter | Delivery overhead cancelled the saved decode |
| Dropping the edge source-gap fill | Incorrect road pixels on all five original circuits |
| Skipping empty pass-B walks | No empty walks observed; added overhead |
| Packed `SlotExit` or packed register ABI | Extra packing cost; struct returns were already optimised |
| Cold hardware fallback moved to `noinline` | +0.73 ms; call barrier increased hot-loop memory traffic |
| `always_inline` on `view_plant` | +1.04 ms |
| Branch prediction hint in the gap walk | +1.38 ms; worse loop shape |
| Sweep driver C tidy-up | About -0.03 ms; instruction counts overstated its price |
| MODE 7 row staging for blitter painting | Staging dominated; blank-run clears are a different mechanism |
| MODE 7 blank-glyph lookup | Lookup cost outweighed skipped paints |

The title page's recorded 78 ms first paint is accepted. The earlier row-ownership
closures depended on pre-sprite layouts; use the current implementation rather
than treating their old cost tables as a new worklist. Reducing geometry detail
would change visual fidelity and is not an authorised optimisation.
