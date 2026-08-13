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
"""
import sys
import struct

W, H, BP = 320, 208, 2
ROW = (W // 8) * BP          # 80 bytes per display line, interleaved
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
            if COLOR00 <= reg <= COLOR00 + 6:
                cur[(reg - COLOR00) // 2] = lo & 0x0FFF
    if cur:
        bands.append((line, cur))
    return bands


def palette_for_line(bands, y):
    """The four pens in force on display line y, accumulated in copper order."""
    pens = [0, 0, 0, 0]
    for line, moves in bands:
        at = 0 if line is None else line - DISPLAY_TOP
        if at <= y:
            for pen, rgb in moves.items():
                pens[pen] = rgb
    return pens


def rgb12_to_888(v):
    r, g, b = (v >> 8) & 15, (v >> 4) & 15, v & 15
    return (r * 17, g * 17, b * 17)


def main():
    if len(sys.argv) < 4:
        sys.exit('usage: amiga_ppm.py <planes.bin> <copper.bin> <out-prefix>')
    planes = open(sys.argv[1], 'rb').read()
    bands = parse_copper(open(sys.argv[2], 'rb').read())
    prefix = sys.argv[3]

    print('copper bands (raster line -> pens):')
    for line, moves in bands:
        where = 'top of list' if line is None else 'line %d (display %d)' % (line, line - DISPLAY_TOP)
        print('  %-26s %s' % (where, {k: '%03x' % v for k, v in sorted(moves.items())}))

    if len(planes) < H * ROW:
        sys.exit('planes dump is %d bytes, expected %d' % (len(planes), H * ROW))

    with open(prefix + '-amiga.ppm', 'wb') as f:
        f.write(b'P6\n%d %d\n255\n' % (W, H))
        for y in range(H):
            pens = [rgb12_to_888(v) for v in palette_for_line(bands, y)]
            p1 = planes[y * ROW: y * ROW + 40]
            p2 = planes[y * ROW + 40: y * ROW + 80]
            row = bytearray()
            for byte in range(40):
                b1, b2 = p1[byte], p2[byte]
                for bit in range(7, -1, -1):
                    idx = ((b1 >> bit) & 1) | (((b2 >> bit) & 1) << 1)
                    row += bytes(pens[idx])
            f.write(bytes(row))
    print('wrote', prefix + '-amiga.ppm')


if __name__ == '__main__':
    main()
