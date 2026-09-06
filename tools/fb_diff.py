#!/usr/bin/env python3
"""Diff the port's race view against a real-BBC capture of the same parked scene, per line.

  python3 tools/fb_diff.py <bbc_fb_NNNNNN.bin> <port dump> [--from=82] [--to=166]

The BBC capture (`tools/bbc_refloop_race.mjs --park --dump=...`) is $5A80+$2580 = 9600 bytes;
the port's (REVS_SCREEN_DUMP) is BBC_SCREEN_BYTES = 8320 = $5A80..$7AFF, so only the common
prefix is comparable — the $7B00 overlay is live code, not picture.

⭐⭐ WHAT IS GATED, AND WHY IT IS NOT THE WHOLE PICTURE.  Only display lines 82..166 — the
HORIZON and TRACK bands — decide the exit status.  They are the part a circuit's geometry and
its hooks actually draw, which is the question this differential exists to answer.  The rest is
printed but not gated, for two measured reasons:

  lines 0..81   the top text rows and the SKY band.  The sky is invisible (flat-blue palette)
                and holds LIVE CODE, and the text rows hold the lap/time readouts.
  lines 167+    the DASHBOARD, which is painted ON TRANSITIONS and therefore records each run's
                HISTORY rather than its current state.  Measured 2026-09-06 on a parked
                Silverstone: the two machines agree on every byte of the road view and disagree
                on 14 — the gear digit at cells 34-35, where the real BBC still shows the 'N' it
                painted before the gear change and the port shows the '1' that matches $40 = 2
                on BOTH machines (gear_char_tbl $3779: 0='R', 1='N', 2='1').  Same state, two
                different repaint histories.  ⚠ So a real dashboard defect will show up here as
                an UNGATED line — read the report, do not just trust the exit status.

⚠ Both sides must be in the same driving state or every byte differs for reasons that are not
bugs.  --park is the matched state: engine started, first gear, nothing held — which then
STALLS on both machines within a second ($4A43 INCs engine_running back to 0 below 3 revs).
"""
import sys

CELLS, LINES, BPR = 40, 8, 320
COMMON = 8320


def main(argv):
    bbc_path, port_path = argv[1], argv[2]
    first, last = 82, 166
    for a in argv[3:]:
        if a.startswith('--from='):
            first = int(a.split('=', 1)[1])
        elif a.startswith('--to='):
            last = int(a.split('=', 1)[1])

    with open(bbc_path, 'rb') as f:
        bbc = f.read()
    with open(port_path, 'rb') as f:
        port = f.read()
    n = min(len(bbc), len(port), COMMON)
    if n < COMMON:
        print("⚠ only %d bytes comparable (bbc %d, port %d)" % (n, len(bbc), len(port)))

    per_line = {}
    for off in range(n):
        if bbc[off] != port[off]:
            row, rem = divmod(off, BPR)
            cell, line = divmod(rem, LINES)
            per_line.setdefault(row * LINES + line, []).append(cell)

    total = sum(len(v) for v in per_line.values())
    gated = sum(len(v) for dl, v in per_line.items() if first <= dl <= last)
    print("frame buffer diff: %s vs %s" % (bbc_path, port_path))
    print("  %d of %d bytes differ; %d on the GATED road view (display lines %d..%d)"
          % (total, n, gated, first, last))
    for dl in sorted(per_line):
        cells = sorted(set(per_line[dl]))
        band = ('text/sky' if dl < first else 'ROAD' if dl <= last else 'dash')
        print("  %-8s line %3d  %3d cells  %d..%d" % (band, dl, len(cells), cells[0], cells[-1]))
    return 1 if gated else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
