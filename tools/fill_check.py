#!/usr/bin/env python3
"""Map the horizon fill, cell by cell, across a series of BBC frame-buffer dumps.

stripe_check.py answers one yes/no question: is there a BLACK run reaching the right edge in
band 2?  The residual artefact is not only black — a fill run can also be GREEN — and a run
that stops mid-line is only wrong relative to the *other* frames of the same scene.  So this
prints the actual cell bytes as a picture and flags temporal outliers.

    offset = charRow*320 + cell*8 + lineInRow          (bbc_screen.h)

A MODE 5 byte holds four pixels, pen = bit(7-p)*2 + bit(3-p), so a uniform cell is one of
    0x00 pen0 (black in band 2)   '.'
    0x0F pen1 (blue)              'b'
    0xF0 pen2 (white)             'w'
    0xFF pen3 (green)             '#'
anything else is mixed detail and prints as '+'.

    python3 tools/fill_check.py amiga/.run/fl_*.bin
"""
import sys
from collections import Counter

CELLS, LINES, BPR = 40, 8, 320
LO, HI = 74, 112                 # the horizon neighbourhood: band 2 (81..100) plus margin

SYM = {0x00: '.', 0x0F: 'b', 0xF0: 'w', 0xFF: '#'}


def cells(d, y):
    row, line = divmod(y, LINES)
    return [d[row * BPR + c * LINES + line] for c in range(CELLS)]


def edge_run(cs):
    """(value, length) of the constant run reaching the right edge."""
    v, n = cs[-1], 1
    for b in reversed(cs[:-1]):
        if b != v:
            break
        n += 1
    return v, n


def main(paths):
    if not paths:
        print(__doc__)
        return 2
    frames = [(p, open(p, 'rb').read()) for p in paths]

    # Per line, the modal right-edge run across the whole series is "what this scene does".
    modal = {}
    for y in range(LO, HI + 1):
        modal[y] = Counter(edge_run(cells(d, y)) for _, d in frames).most_common(1)[0][0]

    bad_frames = 0
    for p, d in frames:
        rows, flags = [], []
        for y in range(LO, HI + 1):
            cs = cells(d, y)
            v, n = edge_run(cs)
            mv, mn = modal[y]
            # An outlier: the run reaching the right edge is much longer than this line's
            # normal, i.e. the fill stopped early and left one value smeared to the edge.
            if n - mn >= 6:
                flags.append(f"line{y}: {n} cells of {v:02X} (usual {mn} of {mv:02X})")
            rows.append((y, "".join(SYM.get(b, '+') for b in cs)))
        if flags:
            bad_frames += 1
            print(f"\n{p}: FILL OUTLIER on {len(flags)} line(s)")
            for f in flags:
                print(f"    {f}")
            for y, r in rows:
                print(f"    {y:3d} {r}")
        else:
            print(f"{p}: ok")

    print(f"\n{bad_frames}/{len(frames)} frames show a fill outlier")
    return 1 if bad_frames else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
