#!/usr/bin/env python3
"""Decode a raw BBC framebuffer dump ($5A80-$7AFF) into a PPM.

⚠ A DIAGNOSTIC, NOT A RENDERER.  The host build has no display by design
(src/platform/host/PlatformHost.h); PlatformHost dumps the raw 8320 bytes and every
decision about pixel format, geometry and colour is made HERE, on the host, where a wrong
guess costs a second instead of an FS-UAE round trip.  The Amiga backend re-derives the
same rules in bitplane form (src/platform/bbc_screen.h) — this file is what confirmed them.

THE SCREEN, derived from the CRTC table the engine writes at hw_init ($4DDD, table $4F0F)
and cross-checked against the addresses the game actually draws to:

    R12/R13 = $0B50  -> screen base = $0B50 * 8 = $5A80
    R1  = 40         -> 40 bytes per character row
    R6  = 26         -> 26 character rows displayed
    R9  = 7          -> 8 scan lines per character row     => 208 lines, 8320 bytes
    R4  = 38, R8 = 1 -> 39 rows/field, interlace sync      => 312.5 lines = 20000 us
    R7  = 32         -> vsync at row 32 (line 256)

    $5A80 + 8320 = $7B00, which is exactly where the $7B00-$7FFF overlay begins: the
    framebuffer ends where the code page starts, so the range is not a guess.

BYTE ORDER is the BBC's character-cell layout: 8 consecutive bytes are one 8-pixel-wide
column strip, top line first; cells run left to right; then the next character row.

    offset = row*320 + cell*8 + line

PIXELS.  The Video ULA serialises a byte through a shift register, emitting a 4-bit
palette index taken from bits (7,5,3,1) and shifting left with 1s fed in.  So the pixel's
own bits land in index bits 3 and 1, and index bits 2 and 0 hold the NEXT pixels' bits,
i.e. they are don't-care.  That is why the game's palette tables come in groups of four
(logical 0,1,4,5 share a colour; 2,3,6,7 share the next; …) — see $3458 in the notes
below.  Hence:

    MODE 5 ($FE20 = $C4, 4 px/byte, 4 colours): pixel p colour = bit(7-p)*2 + bit(3-p)
    MODE 4 ($FE20 = $88, 8 px/byte, 2 colours): pixel p colour = bit(7-p)

Both modes read the same 40 bytes per line, so MODE 5 pixels are simply twice as wide.
"""
import sys

ROWS, CELLS, LINES = 26, 40, 8
BPR = CELLS * 8                      # 320 bytes per character row
WIDTH, HEIGHT = 320, ROWS * LINES    # 320 x 208 in MODE 4 pixels

# BBC physical colours 0-7 (bit0 red, bit1 green, bit2 blue).
PHYS = [(0, 0, 0), (255, 0, 0), (0, 255, 0), (255, 255, 0),
        (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255)]

# The palettes the IRQ handler writes, decoded from the tables in the binary.
#   $3468 (band 0, MODE 4): logical 0-7 -> physical 4, 8-15 -> physical 3
#   $3458 (band 2, MODE 5): {0,1,4,5}->0  {2,3,6,7}->4  {8,9,12,13}->7  {10,11,14,15}->2
MODE4_PAL = [4, 3]                   # background blue, foreground yellow
MODE5_PAL = [0, 4, 7, 2]             # black, blue, white, green
MODE5_PAL_BAND3 = [0, 1, 7, 2]       # band 3 ($3478) recolours colour 1 -> red
MODE5_PAL_BAND4 = [0, 4, 7, 6]       # band 4 ($347C) recolours colour 3 -> cyan


def decode(data, mode_of_line, pal_of_line):
    """Return HEIGHT rows of WIDTH physical-colour indices."""
    out = []
    for y in range(HEIGHT):
        row, line = divmod(y, LINES)
        base = row * BPR + line
        pal = pal_of_line(y)
        px = []
        if mode_of_line(y) == 4:
            for cell in range(CELLS):
                b = data[base + cell * 8]
                for p in range(8):
                    px.append(pal[(b >> (7 - p)) & 1])
        else:
            for cell in range(CELLS):
                b = data[base + cell * 8]
                for p in range(4):
                    c = (((b >> (7 - p)) & 1) << 1) | ((b >> (3 - p)) & 1)
                    px.append(pal[c])
                    px.append(pal[c])          # MODE 5 pixels are double width
        out.append(px)
    return out


def write_ppm(path, rows):
    with open(path, 'wb') as f:
        f.write(b'P6\n%d %d\n255\n' % (WIDTH, HEIGHT))
        for r in rows:
            f.write(bytes(v for c in r for v in PHYS[c]))
    print('wrote', path)


def main():
    if len(sys.argv) < 3:
        sys.exit('usage: screen_ppm.py <dump.bin> <out-prefix> [mode4-first-line:count]')
    data = open(sys.argv[1], 'rb').read()
    if len(data) < ROWS * BPR:
        sys.exit('dump is %d bytes, expected %d' % (len(data), ROWS * BPR))
    prefix = sys.argv[2]

    # Experiment 1 and 2: the whole screen decoded as one mode, so the eye can pick out
    # which regions are 8-pixel detail and which are 4-pixel double-width.
    write_ppm(prefix + '-mode5.ppm', decode(data, lambda y: 5, lambda y: MODE5_PAL))
    write_ppm(prefix + '-mode4.ppm', decode(data, lambda y: 4, lambda y: MODE4_PAL))

    # Experiment 3: a hybrid, MODE 4 over the line range given on the command line.
    if len(sys.argv) > 3:
        first, count = (int(x) for x in sys.argv[3].split(':'))
        write_ppm(prefix + '-hybrid.ppm',
                  decode(data,
                         lambda y: 4 if first <= y < first + count else 5,
                         lambda y: MODE4_PAL if first <= y < first + count else MODE5_PAL))


if __name__ == '__main__':
    main()
