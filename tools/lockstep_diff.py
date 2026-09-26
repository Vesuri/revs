#!/usr/bin/env python3
"""The first frame a real BBC and the port disagree — layer 2 of `make lap`.

  python3 tools/lockstep_diff.py host.rls bbc.rls [--view] [--raw] [--ignore=ADDR[-ADDR],...] [--all]

  default   the PHYSICS: the car-model block $6280-$62FF and zero page below $90, less the cells
            below that are not state the port keeps (each with its reason)
  --view    the PICTURE: the race view's frame buffer ($6700-$7AFF), per frame, less the tyre
            flicker and the gear digit (WHEEL_SPIN / GLYPH below, each with its reason)
  --raw     every snapshotted byte, nothing excluded

Both logs are RLS1 (src/platform/host/PlatformHost.cpp §THE LOCKSTEP RECORDER): the host's from
an autopilot run, the BBC's from `tools/bbc_refloop_race.mjs --lockstep=host.rls
--lockstep-out=bbc.rls`, which replays the host's key answers poll for poll.  So up to the first
difference both machines saw identical input, and the first snapshot that differs is where the
port's computation left the 6502's.  Cells are named from disasm/symbols.csv.
"""
import argparse, csv, struct, sys

def load(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'RLS1', path + ': not an RLS1 log'
    n = struct.unpack_from('<H', b, 4)[0]
    regions = [struct.unpack_from('<HH', b, 6 + 4 * i) for i in range(n)]
    snap = sum(l for _, l in regions)
    frames, polls, o = [], [], 6 + 4 * n
    while o < len(b):
        t = b[o:o + 1]
        if t == b'P': polls.append((len(frames), b[o + 1], b[o + 2])); o += 3
        elif t == b'R': o += 2
        elif t == b'S':
            frames.append(b[o + 5:o + 5 + snap]); o += 5 + snap
        else: raise SystemExit(f'{path}: bad tag at {o}')
    return regions, frames, polls

# Not physics state, each for a stated reason.  Measured on Silverstone and the Nurburgring: with
# these out, both circuits compare identical over all 2995 frames of `make lockstep`.
SCRATCH = {  # 6502 working cells the port deliberately does not reproduce (the RESULTS rule):
    0x1B, 0x2A, 0x33, 0x35, 0x36, 0x37, 0x47, 0x48, 0x75, 0x77, 0x78, 0x79, 0x7A, 0x7B,
    0x7C, 0x7D, 0x7E, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x8A, 0x8B,
    0x8D, 0x8E, 0x8F,
}
TICK = {0x62CA, 0x62F7, 0x62FA}   # driven by the 50 Hz interrupt, whose count per frame differs
FONT = set(range(0x62C4, 0x62CA))  # vdu_char_block's glyph rows: OSWORD 10's answer is this project's
                                   # own font (src/platform/mos_font.h), never Acorn's ROM bytes
RENDER = set(range(0x628F, 0x62A0)) | {0x62F3, 0x62F9, 0x62FD}   # the object plotter's scale state, and the span plotters' per-span colour patterns — drawing state
                                      # (a difference here is a hint for --view, not physics)
PHYSICS = [a for a in list(range(0x00, 0x90)) + list(range(0x6280, 0x6300))
           if a not in SCRATCH and a not in TICK and a not in FONT and a not in RENDER]
# The picture, less two things that are not the port's to match (each measured on Silverstone and
# the Nurburgring, 2995 frames: with these out, the only differences left are real ones).
WHEEL_SPIN = (set(range(0x6FC0, 0x6FC5)) | set(range(0x70F8, 0x70FD)) | set(range(0x6E85, 0x6E88))
              | set(range(0x6FBD, 0x6FC0)) | set(range(0x6E8A, 0x6E8F)) | set(range(0x6FB2, 0x6FB7)))
              # tick_wheel_spin's six EOR runs, the front tyres turning: it runs in the 50 Hz
              # interrupt, ~5 times a BBC frame and once a host frame (TICK above), so the two
              # machines' tread phases part by construction
GLYPH = set(range(0x7990, 0x79A0))
              # draw_gear_indicator's digit (cells 34-35): the race view's font (src/platform/
              # mos_font.h) is this project's own drawing, not Acorn's ROM, so a glyph may differ
VIEW = [a for a in range(0x6700, 0x7B00) if a not in WHEEL_SPIN and a not in GLYPH]

def names():
    out = {}
    try:
        for row in csv.reader(open('disasm/symbols.csv')):
            if row and row[0].startswith('0x') and len(row) > 1:
                out.setdefault(int(row[0], 16), row[1])
    except FileNotFoundError: pass
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('host'); ap.add_argument('bbc')
    ap.add_argument('--ignore', default='')
    ap.add_argument('--view', action='store_true')
    ap.add_argument('--raw', action='store_true')
    ap.add_argument('--all', action='store_true', help='list every differing frame, not just the first')
    a = ap.parse_args()
    rh, fh, ph = load(a.host)
    rb, fb, pb = load(a.bbc)
    assert rh == rb, 'the two logs snapshot different regions'
    addrs = [s + i for s, l in rh for i in range(l)]
    at = {ad: i for i, ad in enumerate(addrs)}
    ign = set()
    for part in filter(None, a.ignore.split(',')):
        lo, _, hi = part.partition('-')
        ign.update(range(int(lo, 16), int(hi or lo, 16) + 1))
    cells = addrs if a.raw else VIEW if a.view else PHYSICS
    cells = [c for c in cells if c in at and c not in ign]
    nm = names()
    def label(ad):
        if a.view:
            o = ad - 0x5A80                           # BBC_SCREEN_BASE (src/platform/bbc_screen.h)
            return f'display line {(o // 320) * 8 + (ad & 7)}, cell {(o % 320) // 8}'
        base = max((k for k in nm if k <= ad and ad - k < 8), default=None)
        return f'{nm[base]}+{ad - base}' if base is not None and base != ad else nm.get(ad, '')
    n = min(len(fh), len(fb))
    print(f'host {len(fh)} frames, bbc {len(fb)} frames; comparing {n} frames of '
          f'{"the VIEW" if a.view else "every snapshotted byte" if a.raw else "the PHYSICS"} '
          f'({len(cells)} cells)')
    first, bad = None, 0
    for f in range(n):
        diffs = [(ad, fh[f][at[ad]], fb[f][at[ad]]) for ad in cells if fh[f][at[ad]] != fb[f][at[ad]]]
        if not diffs: continue
        bad += 1
        if first is None:
            first = f
            print(f'\nFIRST DIFFERENCE at frame {f}: {len(diffs)} cells')
            for ad, h, bb in diffs[:40]:
                print(f'  ${ad:04X} {label(ad):32s} host {h:02X}  bbc {bb:02X}')
            if not a.all: break
        else:
            print(f'  frame {f}: {len(diffs)} cells')
    if first is None:
        print(f'no difference over {n} frames' + ('' if len(fh) == len(fb) else
              ' — but the BBC stopped early: read the replay log for why'))
    elif a.all:
        print(f'{bad} of {n} frames differ')
    return 1 if first is not None else 0

if __name__ == '__main__':
    sys.exit(main())
