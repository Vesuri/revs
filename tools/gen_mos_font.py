#!/usr/bin/env python3
"""Generate src/platform/mos_font.h — the RACE VIEW's 8x8 character generator.

    python3 tools/gen_mos_font.py            # -> src/platform/mos_font.h

── WHAT THIS IS, AND WHY IT IS DRAWN RATHER THAN EXTRACTED ───────────────────────────────────

`vdu_char_def` ($5092) has two arms, selected by `$64` bit 7, and they use two DIFFERENT fonts
(docs/bbc-hardware.md §MOS calls).  The MODE 7 arm calls OSWRCH and the SAA5050 draws the cell —
that font is `src/platform/teletext_font.h`, and its shapes are normatively published in the
World System Teletext standard.  **This is the other arm.**  $5096 puts the character code in
the OSWORD control block at $62C3, asks `OSWORD 10` for an 8x8 bitmap, and plots the eight
returned bytes into the frame buffer itself.

That bitmap comes from the **MOS ROM software font**, which is Acorn's copyrighted operating
system.  There is no standards document behind it and no licence to copy it, so unlike the
teletext set these 96 glyphs are DRAWN HERE, by hand, from scratch.  They are shaped to the
same METRICS as the ROM font — an 8x8 cell, a 6-wide glyph body in the left six columns, seven
rows of body plus a blank eighth, descenders in the eighth row — because the metrics are what
make the lap-time readout line up in the cells the game plots it into.  The letterforms are
this project's own.

  [ADAPTED, not faithful]  The shapes are not Acorn's and are not claimed to be.  A screenshot
  comparison against a real BBC will show different letters in the right places.  Every other
  property the port can be held to — cell size, advance, baseline, which codes exist — matches,
  because those are the ones `vdu_char_def` and the frame buffer actually depend on.

── WHAT THE ENGINE ASKS FOR ──────────────────────────────────────────────────────────────────

Measured on a real BBC over a 200-frame Silverstone practice race
(`make refloop-charset`, i.e. bbc_refloop_race.mjs --charset):

    88 calls, 15 distinct codes, 76 distinct cells, $64 == $00 every time
    $20 $2e $31 $3a $42 $4c $4f $54 $61 $65 $69 $6d $70 $73 $74
    i.e. ' ' '.' '1' ':' 'B' 'L' 'O' 'T' 'a' 'e' 'i' 'm' 'p' 's' 't'
    printed from $3d57 (print_spaces), $4dc4 (text_script_interp), $37eb, $37fe,
    and $7ba9/$7bb8 — the dashboard overlay

⚠ That is ONE session's vocabulary — a practice lap readout, mixed case.  Competition adds
driver names, digits and qualifying text, so the set is a FLOOR, not the closed surface.  The
whole printable range $20-$7F is therefore drawn rather than just the fifteen measured codes,
and `mos.cpp` counts any request outside it instead of quietly returning a blank.

── ⭐ DOUBLE WIDTH, AND WHY IT DECIDES THE GLYPH GEOMETRY ────────────────────────────────────

`vdu_char_def` has a THIRD entry nobody had noticed, and it is the gear indicator:

    508c  STA $62C3        ; stash the character code...
    508f  JMP $509D        ; ...and jump PAST $509B's `LDA #0 / STA $77`

so a caller that pre-loads `$77` reaches the arm at $50AE that reading the code says is dead.
That arm is a DOUBLE-WIDTH renderer: with `$77` bit 7 clear, `AND #$F0` keeps the glyph's left
four columns; with it set, `ASL A` x4 lifts the right four into their place.  Each half becomes
the four (double-width) pixels of one MODE 5 cell, so one glyph is drawn across two cells.

Its only caller is the gearstick readout at $42D0 — measured, not deduced:

    42d0  LDA #$22 / STA $62CC / STA $77    ; column 34, and $77 = $22 -> LEFT half
    42d7  LDA #$D7 / STA $62CD              ; frame-buffer row 215
    42dc  LDX $40 / LDA $3779,X             ; the current gear -> 'N', '1', '2', ...
    42e1  JSR $508C                         ; plot the left half in column 34
    42e4  LDX #$FF / STX $77                ; $77 = $FF -> RIGHT half
    42e8  JSR $508C                         ; ...and the right half in column 35

Confirmed on a real BBC by `make refloop-charset`: 4 entries at $509D from $508F, `$77` in
{$22, $FF}, character codes {$4E, $31} — 'N' and '1', exactly the gears a practice run engages.

⚠⚠ THAT IS WHY THE 6-COLUMN BODY IS CENTRED IN THE CELL (bits 6..1, not 7..2).  A body hard
against the left edge splits 4 ink columns | 2 under the halving, and a narrow glyph like '1'
puts ALL its ink in the first four — so its second cell comes out blank and the gear indicator
renders as half a character.  Centred, the split is 3 | 3 and every glyph survives.  See
row_byte() below.

── THE LAYOUT ────────────────────────────────────────────────────────────────────────────────

One byte per row, bit 7 = leftmost pixel — which is exactly the byte OSWORD 10 returns and
exactly what $50D1's `STA ($70),Y` stores, so there is no transformation anywhere between this
table and the frame buffer.  8 bytes per glyph, 96 glyphs, 768 bytes total.

    glyph = g_mosFont + ((code - 0x20) << 3)

⚠ Unlike teletext_font.h this is NOT padded to a power of two beyond its natural 8: a glyph is
already 8 bytes, so the index is a shift regardless.
"""

import os
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "src", "platform", "mos_font.h")

FIRST = 0x20
LAST = 0x7F

# ── the glyphs ────────────────────────────────────────────────────────────────────────────
# Six columns of body in an eight-column cell; seven rows of body plus a blank eighth, except
# for the descenders (g j p q y , ;) and '_', which use the eighth.  '#' is ink.
GLYPHS = {
    0x20: ("......", "......", "......", "......", "......", "......", "......", "......"),  # space
    0x21: ("..#...", "..#...", "..#...", "..#...", "..#...", "......", "..#...", "......"),  # !
    0x22: (".#.#..", ".#.#..", "......", "......", "......", "......", "......", "......"),  # "
    0x23: (".#.#..", ".#.#..", "#####.", ".#.#..", "#####.", ".#.#..", ".#.#..", "......"),  # #
    0x24: ("..#...", ".####.", "#.#...", ".###..", "..#.#.", "####..", "..#...", "......"),  # $
    0x25: ("##...#", "##..#.", "...#..", "..#...", ".#..##", "#...##", "......", "......"),  # %
    0x26: (".##...", "#..#..", "#.#...", ".#....", "#.#.#.", "#..#..", ".##.#.", "......"),  # &
    0x27: ("..#...", "..#...", "......", "......", "......", "......", "......", "......"),  # '
    0x28: ("...#..", "..#...", ".#....", ".#....", ".#....", "..#...", "...#..", "......"),  # (
    0x29: (".#....", "..#...", "...#..", "...#..", "...#..", "..#...", ".#....", "......"),  # )
    0x2A: ("......", "..#...", "#.#.#.", ".###..", "#.#.#.", "..#...", "......", "......"),  # *
    0x2B: ("......", "..#...", "..#...", "#####.", "..#...", "..#...", "......", "......"),  # +
    0x2C: ("......", "......", "......", "......", "......", "..##..", "..#...", ".#...."),  # ,
    0x2D: ("......", "......", "......", "#####.", "......", "......", "......", "......"),  # -
    0x2E: ("......", "......", "......", "......", "......", "..##..", "..##..", "......"),  # .
    0x2F: (".....#", "....#.", "...#..", "..#...", ".#....", "#.....", "......", "......"),  # /

    0x30: (".####.", "#....#", "#...##", "#.##.#", "##...#", "#....#", ".####.", "......"),  # 0
    0x31: ("..#...", ".##...", "..#...", "..#...", "..#...", "..#...", ".###..", "......"),  # 1
    0x32: (".####.", "#....#", ".....#", "...##.", "..#...", ".#....", "######", "......"),  # 2
    0x33: ("######", "....#.", "...#..", "..##..", ".....#", "#....#", ".####.", "......"),  # 3
    0x34: ("...##.", "..#.#.", ".#..#.", "#...#.", "######", "....#.", "....#.", "......"),  # 4
    0x35: ("######", "#.....", "#####.", ".....#", ".....#", "#....#", ".####.", "......"),  # 5
    0x36: ("..###.", ".#....", "#.....", "#####.", "#....#", "#....#", ".####.", "......"),  # 6
    0x37: ("######", "#....#", "....#.", "...#..", "..#...", "..#...", "..#...", "......"),  # 7
    0x38: (".####.", "#....#", "#....#", ".####.", "#....#", "#....#", ".####.", "......"),  # 8
    0x39: (".####.", "#....#", "#....#", ".#####", ".....#", "....#.", ".###..", "......"),  # 9
    0x3A: ("......", "..##..", "..##..", "......", "..##..", "..##..", "......", "......"),  # :
    0x3B: ("......", "..##..", "..##..", "......", "..##..", "..#...", ".#....", "......"),  # ;
    0x3C: ("...#..", "..#...", ".#....", "#.....", ".#....", "..#...", "...#..", "......"),  # <
    0x3D: ("......", "......", "#####.", "......", "#####.", "......", "......", "......"),  # =
    0x3E: (".#....", "..#...", "...#..", "....#.", "...#..", "..#...", ".#....", "......"),  # >
    0x3F: (".####.", "#....#", ".....#", "...##.", "..#...", "......", "..#...", "......"),  # ?

    0x40: (".####.", "#....#", "#.####", "#.#.##", "#.###.", "#.....", ".####.", "......"),  # @
    0x41: ("..##..", ".#..#.", "#....#", "#....#", "######", "#....#", "#....#", "......"),  # A
    0x42: ("#####.", "#....#", "#....#", "#####.", "#....#", "#....#", "#####.", "......"),  # B
    0x43: (".####.", "#....#", "#.....", "#.....", "#.....", "#....#", ".####.", "......"),  # C
    0x44: ("####..", "#...#.", "#....#", "#....#", "#....#", "#...#.", "####..", "......"),  # D
    0x45: ("######", "#.....", "#.....", "####..", "#.....", "#.....", "######", "......"),  # E
    0x46: ("######", "#.....", "#.....", "####..", "#.....", "#.....", "#.....", "......"),  # F
    0x47: (".####.", "#....#", "#.....", "#..###", "#....#", "#....#", ".####.", "......"),  # G
    0x48: ("#....#", "#....#", "#....#", "######", "#....#", "#....#", "#....#", "......"),  # H
    0x49: (".###..", "..#...", "..#...", "..#...", "..#...", "..#...", ".###..", "......"),  # I
    0x4A: ("....##", ".....#", ".....#", ".....#", "#....#", "#....#", ".####.", "......"),  # J
    0x4B: ("#....#", "#...#.", "#..#..", "###...", "#..#..", "#...#.", "#....#", "......"),  # K
    0x4C: ("#.....", "#.....", "#.....", "#.....", "#.....", "#.....", "######", "......"),  # L
    0x4D: ("#....#", "##..##", "#.##.#", "#....#", "#....#", "#....#", "#....#", "......"),  # M
    0x4E: ("#....#", "##...#", "#.#..#", "#..#.#", "#...##", "#....#", "#....#", "......"),  # N
    0x4F: (".####.", "#....#", "#....#", "#....#", "#....#", "#....#", ".####.", "......"),  # O

    0x50: ("#####.", "#....#", "#....#", "#####.", "#.....", "#.....", "#.....", "......"),  # P
    0x51: (".####.", "#....#", "#....#", "#....#", "#.##.#", "#...#.", ".###.#", "......"),  # Q
    0x52: ("#####.", "#....#", "#....#", "#####.", "#..#..", "#...#.", "#....#", "......"),  # R
    0x53: (".####.", "#....#", "#.....", ".####.", ".....#", "#....#", ".####.", "......"),  # S
    0x54: ("######", "..#...", "..#...", "..#...", "..#...", "..#...", "..#...", "......"),  # T
    0x55: ("#....#", "#....#", "#....#", "#....#", "#....#", "#....#", ".####.", "......"),  # U
    0x56: ("#....#", "#....#", "#....#", "#....#", "#....#", ".#..#.", "..##..", "......"),  # V
    0x57: ("#....#", "#....#", "#....#", "#.##.#", "##..##", "#....#", "#....#", "......"),  # W
    0x58: ("#....#", "#....#", ".#..#.", "..##..", ".#..#.", "#....#", "#....#", "......"),  # X
    0x59: ("#....#", "#....#", ".#..#.", "..##..", "..#...", "..#...", "..#...", "......"),  # Y
    0x5A: ("######", ".....#", "....#.", "..##..", ".#....", "#.....", "######", "......"),  # Z
    0x5B: (".###..", ".#....", ".#....", ".#....", ".#....", ".#....", ".###..", "......"),  # [
    0x5C: ("#.....", ".#....", "..#...", "...#..", "....#.", ".....#", "......", "......"),  # backslash
    0x5D: (".###..", "...#..", "...#..", "...#..", "...#..", "...#..", ".###..", "......"),  # ]
    0x5E: ("..#...", ".#.#..", "#...#.", "......", "......", "......", "......", "......"),  # ^
    0x5F: ("......", "......", "......", "......", "......", "......", "......", "######"),  # _

    0x60: (".#....", "..#...", "......", "......", "......", "......", "......", "......"),  # `
    0x61: ("......", "......", ".####.", ".....#", ".#####", "#....#", ".#####", "......"),  # a
    0x62: ("#.....", "#.....", "#####.", "#....#", "#....#", "#....#", "#####.", "......"),  # b
    0x63: ("......", "......", ".####.", "#....#", "#.....", "#....#", ".####.", "......"),  # c
    0x64: (".....#", ".....#", ".#####", "#....#", "#....#", "#....#", ".#####", "......"),  # d
    0x65: ("......", "......", ".####.", "#....#", "######", "#.....", ".####.", "......"),  # e
    0x66: ("..###.", ".#....", ".#....", "####..", ".#....", ".#....", ".#....", "......"),  # f
    0x67: ("......", "......", ".####.", "#....#", "#....#", ".#####", ".....#", ".####."),  # g
    0x68: ("#.....", "#.....", "#####.", "#....#", "#....#", "#....#", "#....#", "......"),  # h
    0x69: ("..#...", "......", ".##...", "..#...", "..#...", "..#...", ".###..", "......"),  # i
    0x6A: ("...#..", "......", "..##..", "...#..", "...#..", "...#..", "#..#..", ".##..."),  # j
    0x6B: ("#.....", "#.....", "#...#.", "#..#..", "###...", "#..#..", "#...#.", "......"),  # k
    0x6C: (".##...", "..#...", "..#...", "..#...", "..#...", "..#...", ".###..", "......"),  # l
    0x6D: ("......", "......", "##.##.", "#.#.#.", "#.#.#.", "#.#.#.", "#.#.#.", "......"),  # m
    0x6E: ("......", "......", "#####.", "#....#", "#....#", "#....#", "#....#", "......"),  # n
    0x6F: ("......", "......", ".####.", "#....#", "#....#", "#....#", ".####.", "......"),  # o

    0x70: ("......", "......", "#####.", "#....#", "#....#", "#####.", "#.....", "#....."),  # p
    0x71: ("......", "......", ".#####", "#....#", "#....#", ".#####", ".....#", ".....#"),  # q
    0x72: ("......", "......", "#.###.", "##....", "#.....", "#.....", "#.....", "......"),  # r
    0x73: ("......", "......", ".#####", "#.....", ".####.", ".....#", "#####.", "......"),  # s
    0x74: (".#....", ".#....", "####..", ".#....", ".#....", ".#..#.", "..##..", "......"),  # t
    0x75: ("......", "......", "#....#", "#....#", "#....#", "#....#", ".#####", "......"),  # u
    0x76: ("......", "......", "#....#", "#....#", "#....#", ".#..#.", "..##..", "......"),  # v
    0x77: ("......", "......", "#....#", "#....#", "#.##.#", "##..##", "#....#", "......"),  # w
    0x78: ("......", "......", "#....#", ".#..#.", "..##..", ".#..#.", "#....#", "......"),  # x
    0x79: ("......", "......", "#....#", "#....#", "#....#", ".#####", ".....#", ".####."),  # y
    0x7A: ("......", "......", "######", "....#.", "..##..", ".#....", "######", "......"),  # z
    0x7B: ("...##.", "..#...", "..#...", ".##...", "..#...", "..#...", "...##.", "......"),  # {
    0x7C: ("..#...", "..#...", "..#...", "..#...", "..#...", "..#...", "..#...", "......"),  # |
    0x7D: (".##...", "...#..", "...#..", "...##.", "...#..", "...#..", ".##...", "......"),  # }
    0x7E: ("......", ".##..#", "#..##.", "......", "......", "......", "......", "......"),  # ~
    # $7F is DELETE.  The MOS prints it as nothing; a blank cell is the faithful behaviour and
    # also the safe one — a solid block here would paint over the scene on a stray code.
    0x7F: ("......", "......", "......", "......", "......", "......", "......", "......"),
}


def row_byte(row: str) -> int:
    """'#....#' -> 0b01000010.  The 6-column body is CENTRED in the 8-column cell: column 0 of
    the art lands on bit 6, column 5 on bit 1, and bits 7 and 0 are always clear.

    ⭐ THE CENTRING IS LOAD-BEARING, NOT COSMETIC — it is what makes DOUBLE-WIDTH text work.
    $50AE's arm (below) plots one glyph across TWO cells: `AND #$F0` keeps the glyph's left four
    columns for the first cell and `ASL A` x4 lifts its right four columns into the second, each
    becoming four double-width MODE 5 pixels.  So a body hard against the left edge (bits 7..2)
    splits 4 ink columns | 2, and a narrow glyph like '1' — whose ink is all in the first four —
    renders its second cell entirely BLANK: half a character.  Centred on bits 6..1 the split is
    3 | 3 and every glyph survives the halving.

    This also lands on the MOS ROM font's own metric, which is why the gear indicator lines up in
    the cells $42D0 plots it into.  Convergence on cell geometry, not on letterforms."""
    if len(row) != 6:
        raise ValueError(f"row {row!r} is {len(row)} columns, expected 6")
    b = 0
    for i, c in enumerate(row):
        if c == "#":
            b |= 0x40 >> i
        elif c != ".":
            raise ValueError(f"row {row!r} has {c!r}; only '#' and '.' are pixels")
    return b


def main() -> int:
    missing = [c for c in range(FIRST, LAST + 1) if c not in GLYPHS]
    if missing:
        print("no glyph for: " + " ".join(f"${c:02x}" for c in missing), file=sys.stderr)
        return 1

    out = []
    out.append("/* mos_font.h — GENERATED by tools/gen_mos_font.py.  Do not edit by hand.")
    out.append(" *")
    out.append(" * The race view's 8x8 character generator: what `OSWORD 10` hands back to")
    out.append(" * vdu_char_def's bitmap arm ($5096), which plots the eight bytes straight into the")
    out.append(" * frame buffer.  One byte per row, bit 7 = leftmost pixel — the same orientation the")
    out.append(" * MOS uses, so nothing transforms these bytes between here and the screen.")
    out.append(" *")
    out.append(" * ⚠ THESE GLYPHS ARE DRAWN, NOT EXTRACTED.  The MOS software font is Acorn's")
    out.append(" * copyrighted ROM and there is no standards document behind it (unlike the SAA5050")
    out.append(" * set in teletext_font.h).  The METRICS match — 8x8 cell, 6-wide body, 7 rows plus a")
    out.append(" * blank eighth, descenders in the eighth — because that is what the game's cell")
    out.append(" * layout depends on.  The letterforms are this project's own and do not claim to be")
    out.append(" * Acorn's.  See the generator's header for the full rationale.")
    out.append(" */")
    out.append("#ifndef REVS_MOS_FONT_H")
    out.append("#define REVS_MOS_FONT_H")
    out.append("")
    out.append("/* ⚠ NO #include <stdint.h> here, deliberately: the Amiga build's SASCCompat.h has")
    out.append(" * already typedef'd int8_t and the framework's compat stdint.h then conflicts with")
    out.append(" * it.  teletext_font.h holds the same line — a generated table includes nothing and")
    out.append(" * takes its types from whoever includes it. */")
    out.append("")
    out.append(f"#define MOS_FONT_FIRST 0x{FIRST:02X}   /* space */")
    out.append(f"#define MOS_FONT_LAST  0x{LAST:02X}   /* DELETE */")
    out.append(f"#define MOS_FONT_COUNT {LAST - FIRST + 1}")
    out.append("#define MOS_FONT_HEIGHT 8")
    out.append("")
    out.append("/* glyph = g_mosFont + ((code - MOS_FONT_FIRST) << 3) */")
    out.append("static const uint8_t g_mosFont[MOS_FONT_COUNT][MOS_FONT_HEIGHT] = {")
    for code in range(FIRST, LAST + 1):
        rows = GLYPHS[code]
        if len(rows) != 8:
            raise ValueError(f"${code:02x} has {len(rows)} rows, expected 8")
        body = ", ".join(f"0x{row_byte(r):02X}" for r in rows)
        ch = chr(code) if 0x21 <= code <= 0x7E else " "
        out.append(f"    {{ {body} }},   /* ${code:02X} {ch} */")
    out.append("};")
    out.append("")
    out.append("#endif /* REVS_MOS_FONT_H */")

    text = "\n".join(out) + "\n"
    with open(OUT, "w") as f:
        f.write(text)
    print(f"{OUT}: {LAST - FIRST + 1} glyphs, {(LAST - FIRST + 1) * 8} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
