# The faithfulness seam — which side of the line does this code go on?

> ⭐ **Postmortem finding #5.**  The Atari port's split between validated, faithful twins and
> platform-specific lossy code was *good architecture arrived at late*: the rule for which side
> a function lands on got clarified only after churn.  It is a call you make hundreds of times,
> so it is written down here **before the first twin exists**.

## The three tiers

| File | Contract | Validated? | Linked into |
|---|---|---|---|
| `src/gen/revs_gen.c` | The generated 6502 transliteration. Faithful by construction. Never edited by hand. | is the oracle | both backends |
| `src/gen/revs_native.c` | **FAITHFUL native twins.** Byte-identical to the `__t6502` oracle. | ✅ `make validate` | **both** backends |
| `src/platform/amiga/revs_native_amiga.cpp` | Genuinely Amiga-only code. Deliberately lossy — drops hardware-register writes, routes audio to Paula, frame-driven entry points. | ❌ cannot be | Amiga only |

## The rule

> **A faithful, pure-`mem[]` 6502 routine that merely needs a small Amiga variation does NOT
> move to the `.cpp`.  It stays a validated twin in `revs_native.c`, with the variation guarded
> by `#ifdef REVS_PLATFORM_AMIGA`.**

- `#ifdef REVS_PLATFORM_AMIGA` — Amiga-only additions: publishing a dirty region, skipping a
  BBC-hardware tail the copper has already handled.
- `#ifndef REVS_PLATFORM_AMIGA` — BBC-faithful behaviour the validation build must keep so the
  twin still matches its oracle byte for byte.

Amiga-only globals a twin writes belong in **that twin's own translation unit** (the writer's
TU), also under `#ifdef REVS_PLATFORM_AMIGA`.

## Why the rule points this way

The validated tier is the only tier with a *proof*.  Every routine that leaves it loses its
differential against the oracle permanently, and `make validate` becomes blind to it — which is
exactly how a class of bug that passes validation and breaks at runtime gets in.  So the
question is never "does this touch the Amiga?" but **"is this routine's behaviour still defined
by the 6502 original?"**  If yes, it stays validated and the platform difference is an `#ifdef`.

Corollary: prefer making the Amiga variation *smaller* over moving the routine.  A dirty-flag
publish is three lines under an `#ifdef`; re-hosting the routine costs its proof forever.

## The decision, as a checklist

Ask, in order:

1. **Is the observable result defined by the original 6502 code?**
   No → `revs_native_amiga.cpp`.  Yes → continue.
2. **Does it operate on `mem[]` (plus pure locals), rather than on Amiga structures?**
   No (it owns Bitmaps/CopperLists/Sprites) → `revs_native_amiga.cpp`.  Yes → continue.
3. **Can the Amiga difference be expressed as an `#ifdef`-guarded addition or omission,
   leaving the mem[] result identical on the validation build?**
   Yes → **`revs_native.c` + `#ifdef`.**  This is the answer far more often than it looks.
   No → `revs_native_amiga.cpp`, and say in a comment why the difference could not be an
   `#ifdef` — that sentence is what stops the same routine being re-litigated later.

## What "validated" costs and buys

Buys: a byte-exact differential over randomised inputs, re-run in seconds, forever.
Costs: the twin must reproduce every `mem[]` write the oracle makes that anything can still
observe — including scratch cells, unless they are *proven* dead (no reader before the next
write).  Proven-dead cells go in the fixture's ignore list with the proof in a comment, never
just to make a test pass.

See `docs/validation-harness.md` for what the harness guarantees, and `CLAUDE.md` §Transpiler
for the mechanical steps of making a function native.
