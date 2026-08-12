#!/usr/bin/env python3
"""Build the post-load 6502 memory image from revs.ssd — the Revs analogue of RoF's
xex_load.py.

    python3 tools/ssd_load.py revs.ssd disasm

Writes:
    disasm/files/<NAME>.bin   every DFS file, extracted verbatim
    disasm/revs_mem.bin       64 KB post-load memory image
    disasm/revs_blocks.txt    which file covers which address range

⚠⚠ THE LOAD ORDER BELOW IS A HYPOTHESIS, NOT A FACT.

An Atari .xex carries its own segment table, so `xex_load.py` could reproduce real
loader semantics exactly.  A DFS disc does not: what gets loaded, in what order, and
what is already resident when the machine-code entry point runs is decided by the BASIC
loader (`$.Car` -> `$.REVS` -> `*RUN Revs2`), and the files here OVERLAP:

    Revs2   $1200-$7000   (24 KB — the machine-code engine)
    Revs1   $2000-$25AF   (inside Revs2's range!)
    Silvers $70DB-$7814   (Silverstone track data)

So an image composed by this script is a plausible reconstruction, and the port must NOT
be built on it until it has been CONFIRMED against a real machine: run the disc in jsbeeb
or b2, break at the engine's entry point, dump RAM, and diff it against this file.  That
is the first job of the reference loop (docs/bbc-reference-loop.md), and postmortem §3.2
is explicit that building the reference loop first is what this port should do
differently.  Until that diff is clean, treat every address derived from this image as
provisional.

Adjust LOAD_ORDER once the real sequence is known.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ssd_map import read_catalogue  # noqa: E402

# Files applied in this order, later writes winning — the part that needs confirming.
# A name not listed is extracted but NOT composed into the image.
LOAD_ORDER = ["Revs2", "Revs1", "Silvers"]


def main(argv):
    src = argv[1] if len(argv) > 1 else "revs.ssd"
    outdir = argv[2] if len(argv) > 2 else "disasm"
    img = open(src, "rb").read()
    cat = read_catalogue(img)

    os.makedirs(os.path.join(outdir, "files"), exist_ok=True)
    byname = {}
    for f in cat["files"]:
        data = img[f["start"] * 256 : f["start"] * 256 + f["length"]]
        byname[f["name"]] = (f, data)
        with open(os.path.join(outdir, "files", f["name"] + ".bin"), "wb") as fh:
            fh.write(data)
        print(f"extracted {f['name']:<8} {len(data):>6} bytes  "
              f"load ${f['load']:04X} exec ${f['exec_']:04X}")

    memimg = bytearray(65536)
    blocks = []
    for name in LOAD_ORDER:
        if name not in byname:
            print(f"WARNING: {name} not on this disc — skipped", file=sys.stderr)
            continue
        f, data = byname[name]
        lo = f["load"] & 0xFFFF
        hi = lo + len(data)
        if hi > 65536:
            print(f"WARNING: {name} runs past $FFFF — truncated", file=sys.stderr)
            data = data[: 65536 - lo]
            hi = 65536
        memimg[lo:hi] = data
        blocks.append((name, lo, hi, f["exec_"]))

    with open(os.path.join(outdir, "revs_mem.bin"), "wb") as fh:
        fh.write(memimg)

    lines = [
        "# Post-load memory image composition — revs_mem.bin",
        "# ⚠ PROVISIONAL: this load order is a hypothesis until diffed against a real",
        "#   machine (docs/bbc-reference-loop.md).  See tools/ssd_load.py.",
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
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
