#!/usr/bin/env python3
"""Decode an Amiga bitplane + copper-list dump from a running FS-UAE into a PPM.

⭐ THIS IS THE PORT'S VISUAL GROUND TRUTH ON THE TARGET.  amiga/screen_dump.gdb dumps two
blocks out of the emulated machine — the displayed interleaved bitplane block and the copper
list — and this decodes them exactly as the hardware would: two bitplanes, 320x208, with the
palette taken from the copper list's own COLORxx MOVEs at the raster lines its WAITs specify.
Nothing here is told what the picture should look like, which is the point: a band at the
wrong line or a nibble in the wrong plane shows up as a wrong picture rather than as an
argument.

Layout assumptions come from src/platform/amiga/RevsScreen.cpp (interleaved, plane 1 then
plane 2 per row) and src/platform/bbc_screen.h (320x208, kDisplayTop = 0x2C).

⭐ TWO CONFIGURATIONS.  The port now has a second one — MODE 7's front end is 320x250 with THREE
bitplanes and eight colours (RevsScreen.cpp) — so the shape is no longer a constant here.  Pass
`--planes=N --height=N`, which amiga/mode7_dump.gdb prints straight out of the program
(g_screenPlanes / g_screenHeight), rather than editing the constants: a decoder that assumes the
race view silently renders the front end as garbage, and garbage reads as a renderer bug.

With three planes the palette also comes from the copper's COLOR00..07 rather than 00..03, and
MODE 7 has no raster bands at all — one palette for the whole page.
"""
import sys
import struct

W, H, BP = 320, 208, 2       # defaults: the race view.  --planes/--height override.
DISPLAY_TOP = 0x2C
COLOR00 = 0x180


def parse_copper(data):
    """-> list of (rasterLine, {penIndex: rgb12}) in list order; line None = no WAIT yet."""
    bands, cur, line = [], {}, None
    for i in range(0, len(data) - 3, 4):
        hi, lo = struct.unpack('>HH', data[i:i + 4])
        if hi & 1:                                  # WAIT / SKIP
            vp = (hi >> 8) & 0xFF
            if vp == 255:                           # the list's park word
                break
            if cur:
                bands.append((line, cur))
                cur = {}
            line = vp + 1                           # WAIT is on the line BEFORE the band
        else:
            reg = hi & 0x1FE
            if COLOR00 <= reg <= COLOR00 + 14:      # up to COLOR07 (three planes)
                cur[(reg - COLOR00) // 2] = lo & 0x0FFF
    if cur:
        bands.append((line, cur))
    return bands


def palette_for_line(bands, y, npens=4):
    """The pens in force on display line y, accumulated in copper order."""
    pens = [0] * npens
    for line, moves in bands:
        at = 0 if line is None else line - DISPLAY_TOP
        if at <= y:
            for pen, rgb in moves.items():
                if pen < len(pens):
                    pens[pen] = rgb
    return pens


def rgb12_to_888(v):
    r, g, b = (v >> 8) & 15, (v >> 4) & 15, v & 15
    return (r * 17, g * 17, b * 17)


def main():
    global W, H, BP
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    for a in sys.argv[1:]:
        if a.startswith('--planes='):
            BP = int(a.split('=')[1])
        elif a.startswith('--height='):
            H = int(a.split('=')[1])
        elif a.startswith('--width='):
            W = int(a.split('=')[1])
    if len(args) < 3:
        sys.exit('usage: amiga_ppm.py [--planes=N --height=N] <planes.bin> <copper.bin> <prefix>')
    planes = open(args[0], 'rb').read()
    bands = parse_copper(open(args[1], 'rb').read())
    prefix = args[2]

    print('copper bands (raster line -> pens):')
    for line, moves in bands:
        where = 'top of list' if line is None else 'line %d (display %d)' % (line, line - DISPLAY_TOP)
        print('  %-26s %s' % (where, {k: '%03x' % v for k, v in sorted(moves.items())}))

    gap = W // 8                 # one plane's bytes per line
    row_bytes = gap * BP         # interleaved: all planes of a line, then the next line
    npens = 1 << BP
    if len(planes) < H * row_bytes:
        sys.exit('planes dump is %d bytes, expected %d (%d planes x %d lines)'
                 % (len(planes), H * row_bytes, BP, H))
    print('decoding %dx%d, %d bitplanes, %d bytes/line' % (W, H, BP, row_bytes))

    with open(prefix + '-amiga.ppm', 'wb') as f:
        f.write(b'P6\n%d %d\n255\n' % (W, H))
        for y in range(H):
            pens = [rgb12_to_888(v) for v in palette_for_line(bands, y, npens)]
            base = y * row_bytes
            pl = [planes[base + p * gap: base + (p + 1) * gap] for p in range(BP)]
            row = bytearray()
            for byte in range(gap):
                bs = [pl[p][byte] for p in range(BP)]
                for bit in range(7, -1, -1):
                    idx = 0
                    for p in range(BP):
                        idx |= ((bs[p] >> bit) & 1) << p
                    row += bytes(pens[idx])
            f.write(bytes(row))
    print('wrote', prefix + '-amiga.ppm')


if __name__ == '__main__':
    main()
