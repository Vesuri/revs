#!/usr/bin/env python3
"""Steps and slow ticks per emulated second from amiga/sim_clock.gdb's samples.

Only intervals where the race clock moved FORWARD by about the interval's length count: a crash
hold (2 s of real time with no steps) and the reset after it (clock cleared) both break that,
and both are real time that is deliberately not game time.  Usage: sim_clock_report.py <gdb log>.
"""
import re, sys

def clock_cs(m, s, c):
    b = lambda x: (x >> 4) * 10 + (x & 15)
    return (b(m) * 60 + b(s)) * 100 + b(c)

rows = []
for line in open(sys.argv[1]):
    r = re.match(r'sample: vbi=(\d+) steps=(\d+) ticks=(\d+) clock=(\w\w):(\w\w)\.(\w\w)', line)
    if r:
        v, s, t = (int(r.group(i)) for i in (1, 2, 3))
        rows.append((v, s, t, clock_cs(*(int(r.group(i), 16) for i in (4, 5, 6)))))
good = []
for a, b in zip(rows, rows[1:]):
    fields = b[0] - a[0]
    dclock = b[3] - a[3]
    ok = dclock > 0 and b[1] - a[1] > 0
    rate_s = (b[1] - a[1]) * 50 / fields
    rate_t = (b[2] - a[2]) * 50 / fields
    clk = dclock / (fields * 2)          # race clock cs per real cs
    print(f"vbi {a[0]:5d}-{b[0]:5d}  steps/s {rate_s:6.2f}  ticks/s {rate_t:6.2f}  "
          f"clock/real {clk:5.3f}  {'' if ok else '(reset or hold — excluded)'}")
    if ok:
        good.append((fields, b[1] - a[1], b[2] - a[2], dclock))
if good:
    F = sum(g[0] for g in good)
    print(f"reset-free: {len(good)} intervals, {F} fields — steps/s {sum(g[1] for g in good)*50/F:.2f}, "
          f"ticks/s {sum(g[2] for g in good)*50/F:.2f}, race clock / real time {sum(g[3] for g in good)/(F*2):.3f}")
