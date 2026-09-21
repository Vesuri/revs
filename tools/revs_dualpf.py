#!/usr/bin/env python3
"""⭐⭐ THE DUAL-PLAYFIELD LAYER'S GATE (amiga/dualpf_dump.gdb, `make DUALPF=1`).

The cockpit layer is a pure DECOMPOSITION until the terrain painter stops clipping to the car:
every opaque PF2 pixel carries the colour of the PF1 pixel it hides, and every transparent one
reveals it.  So the check is exact and needs no second run — composite PF2 over PF1 and require
the result, read back as a BBC pen, to equal the plain expansion of the frame buffer.

  PF2 pen 0 -> transparent, take PF1's pen      PF2 pen 2 -> BBC pen 2
  PF2 pen 1 -> BBC pen 1                        PF2 pen 3 -> BBC pen 0   (the permutation)

⚠ Only display lines 117..157 carry the layer; outside them PF2 must be entirely zero, and that
is checked too — a stray non-zero byte there would punch a hole in the terrain.
"""
import os, struct, sys, zlib

RUN = os.path.join(os.path.dirname(__file__), '..', 'amiga', '.run')
W, H, GAP, ROWB = 320, 208, 40, 80
CELLS, LINES, BPR = 40, 8, 320
Y0, Y1 = 117, 157


def rd(n):
    with open(os.path.join(RUN, n), 'rb') as f:
        return f.read()


def expand(nib):
    v = 0
    for p in range(4):
        b = (nib >> (3 - p)) & 1
        if b:
            v |= 3 << (6 - 2 * p)
    return v


def main():
    pf1, pf2, fb = rd('pf1.bin'), rd('pf2.bin'), rd('fb.bin')
    runs = rd('cockrun.bin')
    lo = [expand(b & 15) for b in range(256)]
    hi = [expand(b >> 4) for b in range(256)]

    print('silhouette (display line: terrain runs A / B, the rest is car)')
    for y in range(Y0, Y1 + 1):
        a0, a1, b0, b1 = runs[(y - Y0) * 4:(y - Y0) * 4 + 4]
        art = ''.join('a' if a0 <= c <= a1 else 'b' if b0 <= c <= b1 else 'C'
                      for c in range(CELLS))
        print('%3d  A %2d..%2d  B %2d..%2d  %s' % (y, a0, a1, b0, b1, art))

    bad = badoutside = 0
    firstbad = None
    pens = []
    for y in range(H):
        row, ln = divmod(y, LINES)
        rowpens = []
        for c in range(CELLS):
            b = fb[row * BPR + c * LINES + ln]
            o = y * ROWB + c
            p1lo, p1hi = pf1[o], pf1[o + GAP]
            p2lo, p2hi = pf2[o], pf2[o + GAP]
            if not (Y0 <= y <= Y1) and (p2lo or p2hi):
                badoutside += 1
            for i in range(7, -1, -1):
                q2 = ((p2hi >> i) & 1) * 2 + ((p2lo >> i) & 1)
                q1 = ((p1hi >> i) & 1) * 2 + ((p1lo >> i) & 1)
                got = {0: q1, 1: 1, 2: 2, 3: 0}[q2]
                ref = ((hi[b] >> i) & 1) * 2 + ((lo[b] >> i) & 1)
                rowpens.append(got)
                if Y0 <= y <= Y1 and got != ref:
                    bad += 1
                    if firstbad is None:
                        firstbad = (y, c, i, got, ref)
        pens.append(rowpens)

    print()
    print('composite vs mem[] over %d..%d : %d mismatching pixels' % (Y0, Y1, bad))
    if firstbad:
        print('  first: line %d cell %d bit %d  got pen %d want %d' % firstbad)
    print('PF2 non-zero outside the layer : %d cell-planes' % badoutside)

    pal = {0: (0, 0, 0), 1: (255, 0, 0), 2: (0, 200, 0), 3: (255, 255, 255)}
    rows = [b''.join(bytes(pal[v]) * 2 for v in r) for r in pens]
    raw = b''.join(b'\x00' + r for r in rows)

    def ch(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    out = os.path.join(RUN, 'dualpf.png')
    with open(out, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n'
                + ch(b'IHDR', struct.pack('>IIBBBBB', W * 2, H, 8, 2, 0, 0, 0))
                + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b''))
    print('wrote', out)
    return 1 if (bad or badoutside) else 0


if __name__ == '__main__':
    sys.exit(main())
