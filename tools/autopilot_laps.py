#!/usr/bin/env python3
"""make lap — the autopilot drives whole laps of every circuit and fails on any crash.

Runs build/revs (a STRAIGHT_TO_RACE=1 HOLD_THROTTLE=1 host build) once per circuit with
REVS_AUTOPILOT=1 (src/platform/autorun.cpp §THE AUTOPILOT), in parallel, and parses the one
`[autopilot]` summary line each run prints at exit.  A run passes only with ZERO crashes (the game
puts a crashed car back on the grid, so one crash voids the lap), zero stalls, zero airborne frames
(car_height >= 2 — a clean lap has none on any circuit) and at least MIN_LAPS laps.

  python3 tools/autopilot_laps.py [--frames N] [--circuits 0,1,2,3,4,5] [--min-laps N] [--trace]
  --trace   REVS_AP_TRACE=-40: dump the 40 frames before any crash or airborne event
"""
import argparse, os, re, subprocess, sys, concurrent.futures as cf

NAMES = {0: 'Silverstone', 1: 'Brands Hatch', 2: 'Donington', 3: 'Oulton Park',
         4: 'Snetterton', 5: 'Nurburgring'}
LINE = re.compile(r'\[autopilot\] (\d+) frames: (\d+) laps, (\d+) crashes, (\d+) stalls, '
                  r'(\d+) airborne frames \(max height (\d+)\)')

def run(circuit, frames, trace, outdir):
    env = dict(os.environ, REVS_AUTOPILOT='1', REVS_FIXED_RNG='1', REVS_TRACK=str(circuit),
               REVS_SCREEN_DUMP=os.path.join(outdir, f'lap{circuit}'),
               REVS_SCREEN_FRAME=str(frames), REVS_QUIT_AFTER_DUMP='1')
    if trace: env['REVS_AP_TRACE'] = '-40'
    p = subprocess.run(['./build/revs'], env=env, capture_output=True, text=True)
    m = LINE.search(p.stderr)
    dumps = [l for l in p.stderr.split('\n') if l.startswith('[ap]') or l.startswith('  f')]
    refused = 'REFUSED' in p.stderr.upper() and circuit != 0
    return circuit, (tuple(int(g) for g in m.groups()) if m else None), dumps, refused

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=20000)
    ap.add_argument('--circuits', default='0,1,2,3,4,5')
    ap.add_argument('--min-laps', type=int, default=5)
    ap.add_argument('--trace', action='store_true')
    a = ap.parse_args()
    outdir = 'tmp/lap'
    os.makedirs(outdir, exist_ok=True)
    circuits = [int(c) for c in a.circuits.split(',')]
    with cf.ThreadPoolExecutor(len(circuits)) as ex:
        results = list(ex.map(lambda c: run(c, a.frames, a.trace, outdir), circuits))
    bad = 0
    for circuit, r, dumps, refused in results:
        name = NAMES.get(circuit, str(circuit))
        if r is None:
            print(f'  {name:13s} FAIL — no [autopilot] line (the run never reached the race?)')
            bad += 1; continue
        frames, laps, crashes, stalls, air, maxh = r
        ok = crashes == 0 and stalls == 0 and air == 0 and laps >= a.min_laps and not refused
        why = [] if ok else [w for w, c in (('crashed', crashes), ('stalled', stalls),
                                             ('airborne', air), ('too few laps', laps < a.min_laps),
                                             ('circuit refused', refused)) if c]
        print(f'  {name:13s} {"PASS" if ok else "FAIL"}  {laps:2d} laps, {crashes} crashes, '
              f'{stalls} stalls, {air} airborne frames (max height {maxh}) over {frames} frames'
              + ('' if ok else '  <- ' + ', '.join(why)))
        if not ok:
            bad += 1
            for l in dumps[:200]: print('     ' + l)
    print('lap: ' + ('PASS — every circuit lapped with no crash, stall or jump' if not bad
                     else f'FAIL — {bad} circuit(s)'))
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())
