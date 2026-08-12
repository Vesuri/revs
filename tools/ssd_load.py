#!/usr/bin/env python3
"""Build the post-load 6502 memory image from revs.ssd — the Revs analogue of RoF's
xex_load.py.

    python3 tools/ssd_load.py revs.ssd disasm            # default track (SILVER)
    python3 tools/ssd_load.py revs.ssd disasm BRANDS      # a specific track

Writes:
    disasm/files/<NAME>.bin   every DFS file, extracted verbatim
    disasm/revs_mem.bin       64 KB post-load memory image
    disasm/revs_blocks.txt    which file covers which address range

## What the real loader does (DERIVED from the BASIC front end on the disc)

`!BOOT` -> `CHAIN "REVINST"` (instructions) -> `CHAIN "REVSMEN"` (the track menu), and the
menu's per-track branch is exactly:

    *LO.<TRACK>      \\ load the track data file at its own load address ($70DB)
    */REVS2          \\ *RUN the engine: loads at $1200 and executes at $1200

So: **track data first, engine second, engine runs.**  The two ranges do not overlap
($70DB-$78AB vs $1200-$7000), so for a static image the order is immaterial — but the
sequence matters for a different reason, below.

`PLUSCRN` ($7C00, 1 KB) is the MODE 7 teletext title screen the menu `*LOAD`s; `REVSMEN` and
`REVINST` are BASIC and load at PAGE ($1900).  None of the three is part of the engine image,
so none is composed in.

## ⚠⚠ THE TRACK FILE PATCHES THE ENGINE AT RUNTIME

The extra track files are not passive data.  Each carries hook code that **modifies the game
code** when the engine starts (`ModifyGameCode`, `CallTrackHook`, and per-track hooks such as
`HookFieldOfView`, `HookFlattenHills`, `HookJoystick`, `HookSlopeJump`, `HookUpdateHorizon`) —
see docs/reference-sources.md.  Consequences for this port:

  * A static memory image is the state *before* those patches are applied.  The bytes the
    engine actually executes differ per track.
  * That is self-modifying code by construction, so those routines cannot be transliterated
    faithfully — they need hand-written stubs, and the hooks must be enumerated during the
    entry-point sweep (docs/entrypoint-sweep.md).
  * It also means "one engine + swappable data" is only approximately true.  The engine is one
    binary; its *behaviour* is per-track.

## ⚠ Still to confirm against a real machine

What is already resident when the engine runs — the MOS workspace, anything REVSMEN left in
RAM, the state of the vector page.  A DFS disc has no segment table (an Atari .xex does), so
this script cannot derive that; only a real BBC can.  Diff this image against a RAM dump taken
at the engine entry point: docs/bbc-reference-loop.md, Phase 1.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ssd_map import read_catalogue  # noqa: E402

# The six circuits on the Revs+ disc.  All load at $70DB; SILVER is $739, the rest $7D0.
TRACKS = ["SILVER", "BRANDS", "DONING", "NURBURG", "OULTON", "SNETTER"]
DEFAULT_TRACK = "SILVER"          # the original 1985 circuit

# Applied in this order, later writes winning.  Mirrors `*LO.<TRACK>` then `*/REVS2`.
# A name not listed is still extracted, just not composed in.
def load_order(track):
    return [track, "REVS2"]


def main(argv):
    src = argv[1] if len(argv) > 1 else "revs.ssd"
    outdir = argv[2] if len(argv) > 2 else "disasm"
    track = (argv[3] if len(argv) > 3 else DEFAULT_TRACK).upper()

    img = open(src, "rb").read()
    cat = read_catalogue(img)
    print(f'{src}: title "{cat["title"]}"  {len(cat["files"])} files')

    os.makedirs(os.path.join(outdir, "files"), exist_ok=True)
    byname = {}
    for f in cat["files"]:
        data = img[f["start"] * 256 : f["start"] * 256 + f["length"]]
        byname[f["name"].upper()] = (f, data)
        with open(os.path.join(outdir, "files", f["name"] + ".bin"), "wb") as fh:
            fh.write(data)
        print(f"  extracted {f['name']:<9} {len(data):>6} bytes  "
              f"load ${f['load']:04X} exec ${f['exec_']:04X}")

    if track not in byname:
        print(f"\nERROR: track '{track}' not on this disc. Available: "
              f"{', '.join(t for t in TRACKS if t in byname)}", file=sys.stderr)
        return 1

    memimg = bytearray(65536)
    blocks = []
    for name in load_order(track):
        f, data = byname[name]
        lo = f["load"] & 0xFFFF
        hi = lo + len(data)
        if hi > 65536:
            print(f"WARNING: {name} runs past $FFFF — truncated", file=sys.stderr)
            data, hi = data[: 65536 - lo], 65536
        memimg[lo:hi] = data
        blocks.append((name, lo, hi, f["exec_"]))

    with open(os.path.join(outdir, "revs_mem.bin"), "wb") as fh:
        fh.write(memimg)

    lines = [
        "# Post-load memory image composition — revs_mem.bin",
        f"# disc: {src}   track: {track}",
        "#",
        "# Mirrors the menu's `*LO.<TRACK>` then `*/REVS2`.",
        "# ⚠ This is the state BEFORE the track file's hook code patches the engine, and it",
        "#   does not model what the MOS/BASIC left resident.  Confirm against a real machine:",
        "#   docs/bbc-reference-loop.md.  See tools/ssd_load.py for the full caveat.",
        "",
        f"{'file':<10} {'from':>6} {'to':>6} {'exec':>6}",
    ]
    for name, lo, hi, ex in blocks:
        lines.append(f"{name:<10} {lo:>6X} {hi:>6X} {ex:>6X}")
    with open(os.path.join(outdir, "revs_blocks.txt"), "w") as fh:
        fh.write("\n".join(lines) + "\n")

    print()
    print("\n".join(lines))
    print()
    print(f"wrote {outdir}/revs_mem.bin (64 KB) and {outdir}/revs_blocks.txt")
    print(f"other tracks: {' '.join(t for t in TRACKS if t in byname and t != track)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
