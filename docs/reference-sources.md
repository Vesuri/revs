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

## Still verify against the binary

The reconstruction is of "the version released on the Complete BBC Micro Games Archive", and the
site itself flags that **code variations exist between BBC Micro Revs variants**.  This repo's
disc is a *third* thing again (see below).  So:

> **The binary in this repo remains the authority.**  When the reference and `revs.ssd` disagree,
> the binary wins — and the disagreement is worth a note in `disasm/symbols.csv` or
> `docs/rename.md`.

## What is actually on this repo's disc

`revs.ssd` is **Revs+**, Mark Moxon's own compilation — the front end identifies itself as
"Variant: Revs+ / Contains the Nurburgring track from the Commodore 64, backported by Mark Moxon /
Computer Assisted Steering / Copyright (c) 1985, 1986, 2022".  Six circuits: Silverstone (the
original 1985 track), the four *Revs 4 Tracks* circuits (Brands Hatch, Donington Park, Oulton
Park, Snetterton), and the Nürburgring (from the C64 Revs+, © Firebird 1987, backported to the
BBC by Moxon).  Computer Assisted Steering originates in the **Superior Software** release.

⚠ **So this is a 2022 fan compilation, not a pristine 1985/1986 original.**  Measured: its
`REVS2` engine differs from the 1985 single-track release's `Revs2` in **974 of 24064 bytes**
(first difference `$1737`, last `$6A80`) — same length, so a patched engine rather than a
rebuild.  The Silverstone track data is byte-identical between the two discs.

The 1985 single-track disc is kept locally as `revs-1985-silverstone.ssd` precisely so that
diff stays available; it is the reference point for "what did the original engine do here?"

**This is a fidelity decision, and it is the user's:** the port targets the Revs+ compilation
(user decision — six tracks and one engine beats an authentic two-disc split).  Where a Revs+
change is visible in behaviour, prefer documenting it over silently inheriting it — a faithful
port of a hack should at least know which parts are the hack.

## The extra tracks patch the engine at runtime

The single most consequential structural fact, from the reference's deep dives ("Secrets of the
extra tracks", "How the extra track files modify the main game code using code hooks"):

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
