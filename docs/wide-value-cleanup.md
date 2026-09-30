# Wide values and memory ownership

The wide-value conversion worklist is complete. This document records the rules
needed to maintain the resulting code; per-function conversion logs are in Git
history. The campaign's recorded end-to-end result was +0.65%, within measurement
noise. Fewer instructions did not establish a frame-time improvement.

## Two mechanisms

**A: compute in wide locals.** Assemble the little-endian or split byte lanes into
a `uint16_t` or masked `uint32_t`, compute with ordinary C arithmetic, and publish
required bytes at the boundary. This preserves the original memory representation.

**B: relocate to a native value.** Keep a scalar or array such as `car_angle_16` in
native storage and marshal at boundaries with 6502 callers. This removes byte
traffic only where the production call tree stays in native representation.

Never cast `mem[]` to a wide pointer for a general value: its byte order differs
from the Amiga's. Structure-of-arrays lanes are often non-adjacent as well. Packed
BCD, flags, address windows and mixed-purpose tables are not ordinary wide values.

## Eligibility and the fourth eligibility test

Use `tools/wide_eligibility.py` to find readers, then inspect their call paths.
A label that resembles dead translation can be reached through an expansion hook,
an indirect dispatch or a one-line entry wrapper. A Silverstone execution trace is
not a proof of absence. A body no scenario drives remains unproven.

The scanner distinguishes native, oracle and shipping transliteration references.
An entry-wrapper-only result requires investigation; it is not permission to discard
a reader. Calibrate scanner changes with known relocated cells and deliberately
planted references, including one-line wrappers and both function-brace styles.

Follow a read through copies to its eventual consumer. Use `make rangeaudit
RANGE=0080-0088 DEFUSE=1` and the relevant circuits. Indexed accesses and aliases
matter: shared scratch cells can have different tenants between calls, and an
apparently dead write can be read next frame.

## Marshalling contracts

At a 6502 ABI boundary, memory and native globals must agree for values the caller
or callee uses. A closure that reads a global needs an input marshal; one that
writes it needs an output marshal. A nested shim already establishes its own
boundary. Derive the transitive closure from the built objects with
`tools/native_closure.py` and check it with `tools/marshal_audit.py`.

The live declarations and marshal functions in `src/gen/revs_native_seam.h` are
the source of truth for extents. An old inventory is not: `view_origin`, for
example, spans more entries than its first three-element use suggests.

Important production boundaries remain:

- Dashboard needle plotting can hit `$62A0..$62EE`, so car-angle and model-state
  input marshals can import real game writes.
- `hypot` and `bearing` cells have multiple tenants. A native-only caller still
  needs a round trip if another tenant changes their memory representation.
- Output mirrors can be consumed by translated hooks or compared by determinism.
  Removing them needs a reader audit and an explained baseline change.
- A plot pointer can overlap its own byte lanes near page zero. Preserve the exact
  alias semantics on that path; a statically known screen address has a different
  contract.

## Independent validation state

A relocated global is process state. Reset or marshal it independently before the
twin and oracle runs; otherwise the first run can seed the second and both appear
correct. Compare required output mirrors as well as the native result.

A fixture models the game's legal representation. Only exempt dead scratch with
a documented reader audit and a scoped ignore range. Keep real outputs, hardware
traces and hook hand-offs in the comparison. See
[the validation harness](validation-harness.md).

## Pricing further work

Rank a candidate by useful operations per marshal, not reference count. A heavily
shared scratch address can have many tenants and very little wide arithmetic.
Oracle-only marshalling has no shipping cost. Check the linked target before
pricing host executions of a C body that the Amiga replaces with assembly.

A measured FPS null can be revisited with matched millisecond measurements, but
it is not a reason to repeat the conversion campaign. Remaining candidates belong
in [open work](open-work.md), with their reader contract and a measurement plan.

## Wrap semantics

A translated byte loop can use a sign-bit exit rather than a zero test. Preserve
that condition when widening counters or pointers: overflow and high-bit cases
are part of the algorithm, including `track_pos_retreat`. Exercise the boundary
explicitly rather than relying on random fixtures.

## Table representation

Section coordinate planes remain in their byte representation. Native reads
reconstruct their values explicitly; declaring a table authoritative in native
storage requires a complete reader and marshalling audit. A null measured for
one representation does not justify moving another table without pricing it.
