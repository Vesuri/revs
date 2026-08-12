# Reference sources — ⚠ this project is NOT binary-only

> **Read this before doing any disassembly work.**  It changes the premise the pipeline was
> designed around, and it changes which phases are expensive.

## A fully documented source reconstruction of Revs already exists

Mark Moxon has published a **complete, buildable, fully annotated source reconstruction** of BBC
Micro Revs:

- <https://revs.bbcelite.com/> — the browsable annotated source, every line documented and
  mostly explained
- <https://github.com/markmoxon/revs-source-code-bbc-micro> — the same content as source files
- <https://github.com/markmoxon/revs-beebasm> — buildable source for the Acornsoft and Superior
  Software variants

It is **not** Crammond's original source (never released).  It is reconstructed from a
disassembly and **builds to 100% identical game files**.  All routine and variable names are
Moxon's invention; the code is the original's.

## What this changes

The two highest-leverage items in `docs/postmortem.md` were both about the cost of *discovery*:

| Postmortem item | Original cost | With this reference |
|---|---|---|
| #1.1 The exhaustive entry-point sweep — handlers reachable only via indirect vectors, discovered piecemeal over the whole RoF project | days, and every premature static conclusion was wrong | largely **already done** — the dispatch structure is documented |
| #1.2 One concentrated behavioural-naming pass — "on a binary-only project the names are your map" | a dedicated phase | a **complete naming scheme already exists** |

`docs/entrypoint-sweep.md` and the naming pass in `docs/phases.md` Phase 2 do not go away — they
become **cross-checks against a reference** instead of open-ended searches.  That is a large
reduction in both cost and risk, and it should visibly change how Phase 2 is run.

## ⚠ Licensing — read before copying anything

**The repositories intentionally carry no LICENSE file.**  Moxon's commentary is intertwined with
the original copyrighted game code, so default copyright law applies to the whole thing:
Revs is © Acornsoft 1985 (Geoffrey J Crammond); the commentary is © Mark Moxon.

**Therefore:**
- ✅ Use it as a **map**: to understand structure, to confirm what a routine does, to cross-check
  the entry-point sweep, to resolve a naming question.
- ✅ Cite it in a comment when it resolved something (`// per revs.bbcelite.com: …`) so the
  provenance of a derived fact is recoverable.
- ❌ Do **not** copy its source, its comments, or its identifier names wholesale into this repo.
  `disasm/symbols.csv` holds *our* names, derived from behaviour.
- ❌ Do not vendor either repository.

The port's own artifacts must remain independently derived.  Using a reference to know *where to
look* is different from copying what it says.

**Scope of the caution:** it covers Moxon's *own authorship* — the annotated source and its
commentary.  It is not a claim about Revs itself, and it does not extend to the game data he
converted: his disc's engine is byte-identical to the 1986 release (measured below), so he modified
no game code, and the Nürburgring he added is Crammond's own C64 track data.  See §A sixth circuit.

## Still verify against the binary

> **The binary in this repo remains the authority.**  When the reference and `revs.ssd` disagree,
> the binary wins — and the disagreement is worth a note in `disasm/symbols.csv` or
> `docs/rename.md`.  See §"Which variant is the reference documenting?" below for why disagreement
> is actively expected.

## What is actually on this repo's disc — and the three-way engine diff

`revs.ssd` is ***Revs Plus Revs 4 Tracks*, © Superior/Acornsoft 1986** — a genuine commercial
release, not a repack.  The front end reads "Revs Plus Revs 4 Tracks / Computer Assisted Steering
/ Copyright (c) Superior/Acornsoft 1986".  Five circuits: Silverstone (the 1985 track) plus Brands
Hatch, Donington Park, Oulton Park and Snetterton.  **Computer Assisted Steering is part of this
release**, not a later addition.

Three discs were in hand while scaffolding, so the engine was diffed all three ways.  The result
settles which differences are authentic and which are a repack's:

| Comparison | `REVS2` diff | Reading |
|---|---|---|
| 1985 single-track Revs → **this disc (1986)** | **974 / 24064 bytes** (same length; first `$1737`, last `$6A80`) | an **authentic Superior/Acornsoft revision** — the 1986 engine is a genuinely different, later engine |
| **this disc (1986)** → Moxon's Revs+ compilation | **0 / 24064 — IDENTICAL** | Moxon did **not** patch the engine at all |

So the "Mark Moxon patch" is entirely in the *track files*, not the engine: he added a `NURBURG`
file (the Nürburgring, from the C64 Revs+, © Firebird 1987, backported by him), padded the four
expansion track files from their original `$738`–`$73C` to a uniform `$7D0`, and normalised
`SILVER`'s exec address from `$0000` to `$70DB`.

⭐ **This is what the port targets, and it is clean:** an authentic 1986 commercial binary, with no
fan modifications in the engine.  The earlier worry about porting a hack does not apply.

### A sixth circuit, and why its provenance is cleaner than it first looks
`revs-hack-nurburgring.ssd` is kept locally (git-ignored) as the only copy on this machine of the
Nürburgring track data.

The licensing caution in this document is about **Moxon's annotated source and commentary** — his
original authorship.  It does **not** extend to the Nürburgring track: measured above, his disc's
engine is byte-identical to the 1986 release, so he modified no game code at all.  What he did was
convert **Crammond's own track data** from the C64 Revs+ (© Firebird 1987) into the BBC's track
format — a data conversion that is reproducible from the C64 original by anyone, including us.

So if a sixth circuit is ever wanted, the honest route is: **take the C64 Revs+ disk image and
extract the Nürburgring data ourselves.**  Then every track in the port descends from a Crammond
original, on the same footing as the other five, with no dependence on a third party's work.
⚠ Not free, though: the BBC and C64 track formats differ (converting between them is precisely
what the backport did), and the expansion tracks are *executable* hook programs rather than plain
data — so a sixth track means reproducing that conversion, not copying a file.  Scope decision,
not a legal one.

### ⚠ Which variant is the reference documenting?
The reconstruction is of "the version released on the Complete BBC Micro Games Archive", and the
site flags that code variations exist between BBC Revs variants.  Given the 974-byte gap measured
above, **assume the reference may be describing the 1985 engine, not this one**, until a specific
routine is confirmed against `revs.ssd`.  That is not a reason to distrust it — it is the reason
the binary stays the authority.

## The extra tracks patch the engine at runtime

The single most consequential structural fact — and this repo's own binaries corroborate it
independently of the reference.  **The DFS exec addresses say it outright:**

```
SILVER   load $70DB  exec $0000   <- passive data; not executable
BRANDS   load $70DB  exec $70DB   <- executable
DONING   load $70DB  exec $70DB   <- executable
OULTON   load $70DB  exec $70DB   <- executable
SNETTER  load $70DB  exec $70DB   <- executable
```

Silverstone, the original release's circuit, is plain data.  Every expansion track is a *program*.
From the reference's deep dives ("Secrets of the extra tracks", "How the extra track files modify
the main game code using code hooks"):

**The extra track files are not passive data.**  Each carries hook code that modifies the game
code as the engine starts — `ModifyGameCode`, `CallTrackHook`, and per-track hooks including
`Hook80Percent`, `HookFieldOfView`, `HookFlattenHills`, `HookJoystick`, `HookSlopeJump`,
`HookUpdateHorizon`.  The extra tracks also **generate geometry at runtime** to fit complex
layouts into ~2 KB files, and their data format differs from Silverstone's.

Three consequences for this port:
1. **A static memory image is the pre-patch state.**  The bytes the engine executes differ per
   track (`tools/ssd_load.py` says so at the top).
2. **This is self-modifying code by construction** — so those routines get hand-written stubs in
   `src/gen/revs_manual.c`, and the hooks must be enumerated in the entry-point sweep.  The
   generic warning in `docs/entrypoint-sweep.md` §"self-modifying-code corollary" now has a
   concrete, named target.
3. **"One engine + swappable data" is only approximately true.**  One binary, but per-track
   behaviour — which is a meaningfully different porting problem, and a better one than six
   engines.

## Other useful references

- <https://www.bbcelite.com/> — Moxon's wider archive (Elite on six platforms, Aviator, The
  Sentinel), useful for BBC-idiom questions in general.
- <https://stardot.org.uk/forums/viewtopic.php?t=24962> — the Revs source announcement thread.

## Sources

- [Fully documented source code for Revs on the BBC Micro](https://revs.bbcelite.com/)
- [markmoxon/revs-source-code-bbc-micro](https://github.com/markmoxon/revs-source-code-bbc-micro)
- [Mark Moxon's Software Archaeology](https://www.bbcelite.com/)
- [Revs source, fully documented and explained — stardot.org.uk](https://stardot.org.uk/forums/viewtopic.php?t=24962)
