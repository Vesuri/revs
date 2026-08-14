#!/usr/bin/env python3
"""Check BBC frame-buffer dumps for the horizon-stripe artefact.

⭐ THE CRITERION IS CALIBRATED AGAINST A REAL BBC, not invented.  `make refloop` samples
band 2 — display lines 81..100, the only window where pen 0 is black — on a real machine with
a moving car, and finds at most a 3-cell zero run on any frame, with lines 81..101 usually
completely zero-free.  So a zero run of 8 or more cells REACHING THE RIGHT EDGE is the
artefact: the fill stopped part-way through the line and everything to its right stayed black.

⚠ Band 1 (the sky, lines 18..80) is excluded on purpose: all sixteen of its palette entries
are the same blue, so a zero byte there is invisible, and 5.5 KB of live engine code and
variables sit on display inside it.  Band 3 (101+) is excluded too — there pen-0 black is the
ROAD, which legitimately widens toward the viewer.  Scanning outside band 2 inverts the answer.

    python3 tools/stripe_check.py amiga/.run/fb_*.bin
"""
import sys

CELLS, LINES, BPR = 40, 8, 320
BAND2_LO, BAND2_HI = 81, 101      # inclusive
MIN_RUN = 8


def edge_run(cells):
    """Length of the zero run that reaches the right edge (0 if the last cell is non-zero)."""
    run = 0
    for b in reversed(cells):
        if b:
            break
        run += 1
    return run


def check(path):
    d = open(path, 'rb').read()
    bad = []
    for y in range(BAND2_LO, BAND2_HI + 1):
        row, line = divmod(y, LINES)
        cells = [d[row * BPR + c * LINES + line] for c in range(CELLS)]
        r = edge_run(cells)
        if r >= MIN_RUN:
            bad.append((y, r, CELLS - r))
    return bad


def main(paths):
    if not paths:
        print(__doc__)
        return 2
    frames_with, total_lines = 0, 0
    for p in paths:
        bad = check(p)
        if bad:
            frames_with += 1
            total_lines += len(bad)
            detail = " ".join(f"line{y}:{r}cells@{s}" for y, r, s in bad)
            print(f"  {p}: STRIPES on {len(bad)} line(s) — {detail}")
        else:
            print(f"  {p}: clean")
    print(f"\n{frames_with}/{len(paths)} frames show stripes; {total_lines} striped lines total")
    if frames_with:
        print("  => the fill is still stopping part-way through a line.")
        return 1
    print("  => band 2 is fully filled on every frame, as on a real BBC.")
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
