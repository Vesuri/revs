#!/usr/bin/env python3
"""Isolate what each track file PATCHES INTO THE ENGINE, separated from the engine's own unpack.

    python3 tools/track_hooks.py                    # all tracks with dumps in tmp/

## Why the obvious diff is wrong

`tools/bbc_refloop_track_diff.mjs` diffs a RAM dump taken at the REVS2 entry against one taken
1M cycles later and reads the result as "the per-track hook patch surface".  It is not.  The
engine unpacks itself on startup (docs/static-map.md), and that unpack happens for **every**
track — including Silverstone, whose track file is passive data and which the script predicted
would show zero diffs.  It shows 5805.

So the raw diff is unpack + patch + running state, and reading it as a patch inventory attributes
the engine's own relocation to the track file.  Subtract the unpack first:

    patch surface(T) = { a : relocate(before_T)[a] != after_T[a] }  -  runtime-state(SILVER)

Silverstone is the control: its track data is exec $0000, so whatever its residue is, it is the
engine's own runtime state and not a hook.  Anything an expansion track changes beyond that is
the patch.

## Caveat this cannot remove

The "after" dumps are taken at a fixed cycle offset, so the residue also contains ordinary
gameplay state that happens to differ between runs.  Addresses that are pure state show up as
noise; the signal is the addresses that appear for *every* expansion track and not for
Silverstone.  Both are reported separately rather than merged.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from relocate import relocate, runs  # noqa: E402

TRACKS = ["SILVER", "BRANDS", "DONING", "OULTON", "SNETTER"]
CONTROL = "SILVER"
ENGINE_LO, ENGINE_HI = 0x0B00, 0x7900
# The swap deposits the track's own data here, so it differs per track BY CONSTRUCTION.
# Counting it as a patch would drown the real signal in 1830 bytes of track geometry.
TRACK_DATA = (0x5300, 0x5A25)


def residue(track, tmpdir="tmp"):
    """Engine-range addresses the unpack replay does not explain, for one track."""
    bp = os.path.join(tmpdir, f"dump_{track}_before.bin")
    ap = os.path.join(tmpdir, f"dump_{track}_after.bin")
    if not (os.path.exists(bp) and os.path.exists(ap)):
        return None
    sim = bytearray(open(bp, "rb").read())
    after = open(ap, "rb").read()
    relocate(sim, log=lambda *_: None)
    return {a for a in range(ENGINE_LO, ENGINE_HI)
            if sim[a] != after[a] and not (TRACK_DATA[0] <= a < TRACK_DATA[1])}, sim, after


def main():
    base = residue(CONTROL)
    if base is None:
        sys.exit(f"need tmp/dump_{CONTROL}_before.bin and _after.bin "
                 "(tools/bbc_refloop_track_diff.mjs 5)")
    control, _, _ = base
    print(f"# control: {CONTROL} (passive track data, exec $0000)")
    print(f"  {len(control)} engine-range bytes differ after the unpack is subtracted.")
    print("  These are the engine's own runtime state, NOT a hook patch:")
    for s, e in runs(sorted(control)):
        print(f"    ${s:04X}-${e:04X} ({e - s + 1})")
    print()

    per_track = {}
    for t in TRACKS:
        if t == CONTROL:
            continue
        r = residue(t)
        if r is None:
            print(f"# {t}: no dumps in tmp/ — skipped")
            continue
        res, sim, after = r
        patch = res - control
        per_track[t] = patch
        print(f"# {t}: {len(res)} residue bytes, {len(patch)} beyond the control")
        for s, e in runs(sorted(patch)) if patch else []:
            n = e - s + 1
            detail = ""
            if n <= 8:
                detail = ("   " + " ".join(f"{sim[a]:02X}->{after[a]:02X}"
                                           for a in range(s, e + 1)))
            print(f"    ${s:04X}-${e:04X} ({n}){detail}")
        print()

    # Resolve what each patch REDIRECTS TO.  The shape is consistent across tracks: a 3-byte
    # patch rewrites a whole instruction into `JSR/JMP $53xx-$59xx`, and a 2-byte patch rewrites
    # just an absolute operand to point there.  $5300-$5A25 is where the swap deposits the track
    # file — so every target is a routine inside the track program, and every target is an ENTRY
    # POINT that exists in no static image of the engine.
    # ⚠ Two different things wear the same shape here, and conflating them would put data
    # addresses into entrypoints.csv:
    #   * a 3-byte patch to `JSR/JMP $5xxx`  = a CODE HOOK.  A new entry point.
    #   * a 2-byte patch to an absolute operand = DATA REBINDING.  The instruction stays put and
    #     is redirected at a table inside the track file.  NOT an entry point.
    # The give-away for the second kind is the addresses: $5462/$5562/$5662/$5762 and
    # $5572/$5672/$5772 sit on exact $100 boundaries apart, which is a row-indexed table, not
    # hand-written routines that happen to land there.
    print("# CODE HOOKS — new entry points inside the track file ($5300-$5A25 after the swap)")
    all_hooks = {}
    for t, patch in per_track.items():
        _, sim, after = residue(t)
        hooks, rebind, other = {}, {}, []
        for s, e in runs(sorted(patch)) if patch else []:
            n = e - s + 1
            if n == 3 and after[s] in (0x20, 0x4C):
                tgt = after[s + 1] | after[s + 2] << 8
                hooks.setdefault(tgt, []).append(
                    f"{'JSR' if after[s] == 0x20 else 'JMP'} patched in at ${s:04X}")
            elif n == 2:
                tgt = after[s] | after[s + 1] << 8
                # A 2-byte run only reads as a rebound pointer if it actually points into the
                # track file.  Anything else is two adjacent changed bytes that the run-merging
                # happened to join — report it, do not dress it up as a pointer.
                if TRACK_DATA[0] <= tgt < TRACK_DATA[1]:
                    rebind.setdefault(tgt, []).append(f"${s:04X}")
                else:
                    other.append((s, tgt))
        all_hooks[t] = hooks
        print(f"  {t}: {len(hooks)} code hooks, {len(rebind)} rebound data operands")
        for tgt in sorted(hooks):
            warn = "" if TRACK_DATA[0] <= tgt < TRACK_DATA[1] else "   ⚠ NOT in the track file"
            print(f"    ${tgt:04X}  <- {', '.join(hooks[tgt])}{warn}")
        print(f"    data operands rebound to: "
              + " ".join(f"${a:04X}" for a in sorted(rebind)))
        if other:
            print("    2-byte changes that are NOT a track-file pointer (unclassified): "
                  + " ".join(f"${s:04X}" for s, _ in other))
    common_hooks = set.intersection(*(set(h) for h in all_hooks.values())) if all_hooks else set()
    print(f"\n  code-hook targets common to every expansion track: "
          + (" ".join(f"${a:04X}" for a in sorted(common_hooks)) or "(none)"))
    print()

    if len(per_track) > 1:
        common = set.intersection(*per_track.values())
        print(f"# patched by EVERY expansion track ({len(common)} bytes) — the shared hook "
              "mechanism (ModifyGameCode / CallTrackHook):")
        for s, e in runs(sorted(common)) if common else []:
            print(f"    ${s:04X}-${e:04X} ({e - s + 1})")
        print()
        for t, p in per_track.items():
            only = p - common
            print(f"# {t}-only: {len(only)} bytes")
            for s, e in runs(sorted(only)) if only else []:
                print(f"    ${s:04X}-${e:04X} ({e - s + 1})")


if __name__ == "__main__":
    main()
