#!/usr/bin/env python3
"""THE PHYSICAL-EQUIVALENCE CHECK for the frame-rate-independent simulation.

Four decoupled step sizes can never be byte-exact against the 6502 (docs/faithfulness-seam.md
§THE FRAME-RATE-INDEPENDENT SIMULATION), so they are gated here instead: the same scripted drive
in legacy mode (one engine step per painted frame, 93.6 ms of game time each) and in decoupled
modes on the host, compared as functions of GAME TIME.

    python3 tools/sim_equiv.py [--steer[=l|r]] [--seconds=N] [--modes=legacy,h400,h200,h200x5]

A mode is `h<step tenths>[x<fields per painted frame>]`; the default field count gives one
step per painted frame.  `x5` on a 20 ms step puts FIVE steps in each painted frame, which is
what exercises the values held between steps (the surface probe, placement) — the coupling a
one-step mode cannot see.

The drive is STRAIGHT_TO_RACE + HOLD_THROTTLE (a practice session, throttle held from the
start), with REVS_HOLD_STEER=l for --steer.  Each series starts on the first frame the car
moves and is cut at the first crash reset (the race clock going backwards).  REVS_T2_ZERO pins the
VIA timer (the only entropy) so every mode draws the same "random" numbers.  ⚠ Built into
build/revs — `make clean` and rebuild plain before any determinism gate.
"""
import os, subprocess, sys, glob, shutil

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, 'tmp', 'sim_equiv')

def bcd(b): return (b >> 4) * 10 + (b & 15)

def s16(v): return v - 0x10000 if v & 0x8000 else v

def sample(m):
    speed = s16(m[0x62E9] << 8 | m[0x62D9]) / 256.0
    lat = s16(m[0x62E8] << 8 | m[0x62D8]) / 256.0
    x = (m[0x6283] << 16 | m[0x6280] << 8 | m[0x62B1]) / 256.0
    z = (m[0x6285] << 16 | m[0x6282] << 8 | m[0x62B3]) / 256.0
    heading = (m[0x0B] << 8 | m[0x0A]) * 360.0 / 65536
    clock = (bcd(m[0x6E4]) * 60 + bcd(m[0x6CC])) * 100 + bcd(m[0x6B4])
    return dict(speed=speed, lat=lat, x=x, z=z, heading=heading, revs=m[0x3C], note=m[0x0060],
                gear=m[0x40], height=m[0x2D], clock=clock)

def run(mode, steer, seconds):
    env = dict(os.environ, REVS_FIXED_RNG='1', REVS_T2_ZERO='1', REVS_MEM_DUMP='1', REVS_QUIT_AFTER_DUMP='1')
    if steer: env['REVS_HOLD_STEER'] = steer
    if mode == 'legacy':
        dt = 93.6
    else:
        spec = mode[1:]
        step, _, fields = spec.partition('x')
        step = int(step)
        fields = int(fields) if fields else max(1, round(step / 200))
        env['REVS_SIM_STEP'] = str(step)
        env['REVS_SIM_FIELDS'] = str(fields)
        dt = fields * 20.0                       # game ms per painted frame (= real time)
    first = 1                                    # before the race loop: the series starts on the first MOVING frame
    count = int(seconds * 1000 / dt) + 400
    d = os.path.join(OUT, mode + ('_steer' if steer else ''))
    shutil.rmtree(d, ignore_errors=True); os.makedirs(d)
    env['REVS_SCREEN_DUMP'] = os.path.join(d, 'f')
    env['REVS_SCREEN_FRAME'] = str(first)
    env['REVS_SCREEN_COUNT'] = str(count)
    subprocess.run([os.path.join(REPO, 'build', 'revs')], env=env, cwd=REPO,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=1200)
    frames = sorted(int(f.rsplit('.', 1)[1]) for f in glob.glob(os.path.join(d, 'f.mem.*')))
    rows = []
    started = None
    for f in frames:
        s = sample(open(os.path.join(d, f'f.mem.{f}'), 'rb').read())
        if started is None:
            if s['speed'] == 0: continue
            started = f
        if rows and s['clock'] < rows[-1][1]['clock']:
            break                                # a crash reset — the comparison ends here
        # t is game time at the END of the frame: the first moving frame has already run its
        # whole frame of steps, which is 93.6 ms in legacy and one short step in a decoupled mode
        rows.append(((f - started + 1) * dt / 1000.0, s))
        if rows[-1][0] > seconds: break
    shutil.rmtree(d, ignore_errors=True)
    return rows

def at(rows, t, key):
    for (t0, a), (t1, b) in zip(rows, rows[1:]):
        if t0 <= t <= t1:
            w = 0 if t1 == t0 else (t - t0) / (t1 - t0)
            return a[key] + (b[key] - a[key]) * w
    return None

def distance(rows, t):
    d = 0.0
    for (t0, a), (t1, b) in zip(rows, rows[1:]):
        if t1 > t: break
        d += ((b['x'] - a['x']) ** 2 + (b['z'] - a['z']) ** 2) ** 0.5
    return d

def main():
    args = sys.argv[1:]
    steer = ''
    for a in args:
        if a == '--steer': steer = 'l'
        if a.startswith('--steer='): steer = a.split('=')[1]
    seconds = 12.0
    modes = ['legacy', 'h400', 'h200', 'h200x5']
    for a in args:
        if a.startswith('--seconds='): seconds = float(a.split('=')[1])
        if a.startswith('--modes='): modes = a.split('=')[1].split(',')
    series = {m: run(m, steer, seconds) for m in modes}
    ref = series.get('legacy')
    print(f"drive: throttle held{(' + wheel held ' + ('left' if steer == 'l' else 'right')) if steer else ''}; "
          f"t = game seconds from the first moving frame; each series cut at its first crash reset")
    for m, rows in series.items():
        print(f"  {m:8s} {len(rows):5d} frames, {rows[-1][0] if rows else 0:6.2f} s before a reset")
    keys = [('speed', '%7.2f'), ('revs', '%5.0f'), ('note', '%5.0f'), ('gear', '%3.0f')] + \
           ([('heading', '%7.1f'), ('lat', '%6.2f'), ('height', '%4.0f')] if steer else [])
    print()
    hdr = '   t  ' + ''.join(f"| {m:^{8 * len(keys) + 9}s}" for m in modes)
    print(hdr)
    print('      ' + ''.join('| ' + ' '.join(f'{k:>7s}' for k, _ in keys) + '    dist ' for _ in modes))
    t = 1.0
    worst = {}
    while t <= seconds + 1e-9:
        line = f"{t:5.1f} "
        for m in modes:
            rows = series[m]
            vals = [at(rows, t, k) for k, _ in keys]
            if None in vals:
                line += '| ' + ' ' * (8 * len(keys) + 8)
                continue
            dist = distance(rows, t)
            line += '| ' + ' '.join((f % v).rjust(7) for (k, f), v in zip(keys, vals)) + f' {dist:8.1f}'
            if ref and m != 'legacy':
                r = at(ref, t, 'speed')
                if r:
                    worst[m] = max(worst.get(m, 0), abs(vals[0] - r) / max(abs(r), 1.0))
        print(line)
        t += 1.0
    if worst:
        print()
        for m, w in worst.items():
            print(f"worst speed deviation vs legacy, {m}: {w * 100:.1f}%")

if __name__ == '__main__':
    main()
