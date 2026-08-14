#!/usr/bin/env python3
"""Generate src/platform/teletext_font.h — the MODE 7 character generator, laid out for the
68000.

    python3 tools/gen_teletext_font.py            # -> src/platform/teletext_font.h

── WHAT THIS IS AND WHERE IT COMES FROM ──────────────────────────────────────────────────────

BBC MODE 7 characters are NOT drawn from the MOS ROM font.  They come from the SAA5050, a
Mullard/Philips teletext character generator on the BBC's board with its own internal ROM; the
CPU writes a code to $7C00-$7FFF and the chip does the rest.  (The MOS software font, which
`OSWORD 10` returns, is a different font used only in the bitmap modes — see `vdu_char_def`
$5092, whose two arms are exactly these two mechanisms.)

Three character sets have to exist, and only ONE of them is a typeface:

  * G0 alphanumerics (96 glyphs, 6x10).  Shapes taken from jsbeeb's `src/teletext_data.js`
    (itself "extremely heavily based on b-em"), which is what the user asked for.  These shapes
    are also specified normatively, as printed glyph diagrams, in the published World System
    Teletext standard — the document exists so independent parties can build interoperable
    decoders — so nothing here is reverse-engineered from Acorn's copyrighted OS ROM, which is
    the line this project actually holds (docs/reference-sources.md).
  * G1 contiguous mosaics (64 glyphs) — 2x3 filled rectangles.  GENERATED here from the code
    bits.  There is no design content in a rectangle.
  * G1 separated mosaics (64 glyphs) — the same blocks inset by a pixel.  Also generated.

── THE CELL: 8 WIDE x 10 HIGH, AND WHY ───────────────────────────────────────────────────────

⭐ The VERTICAL axis is what decides this, not the horizontal one.  MODE 7 is 40x25 cells.  The
SAA5050's native cell is 6x10 which it doubles to 12x20 with diagonal smoothing, i.e. 480x500 —
and 500 lines needs interlace, so the doubled form is not reachable on a plain PAL screen.  At
ONE scan line per source row, 25 rows x 10 = 250 lines, which IS a legal PAL display, and the
chip's own 6x10 character ROM then maps 1:1 with no resampling and no lost descenders.  So a
10-row cell is the faithful choice and a taller one is not available.

Horizontally, 40 cells over a 320-pixel lores line gives 8 px per cell for a 6 px glyph.

  [ADAPTED, not faithful]  The real chip fills its 12 px cell with the doubled 6 px glyph, so
  ink is 10/12 = 83% of the cell width.  Here it is 5/8 = 63%: the letters carry two extra
  pixels of tracking and the text reads slightly loose.  Matching 83% exactly would need a
  12-px cell (a 480-px display) or glyph stretching, and neither is worth a second display
  config for a static front end.  MOSAICS are generated at the full 8 px so contiguous
  graphics stay contiguous — that part is faithful, and it is the part the REVS logo and the
  5TRSCRN title art are made of.

── THE LAYOUT: ONE SHIFT AND A MOVE ──────────────────────────────────────────────────────────

Per the user's instruction, and it is the right shape for a 68000: one byte per glyph row,
rows consecutive, glyphs consecutive, padded to a power of two so indexing is a shift.

    glyph = g_ttFont + ((set * 128 + code) << TT_GLYPH_SHIFT)

and the blit is ten `move.b (a0)+,(a1)` / `lea BPR(a1),a1` pairs with no arithmetic in the loop.
16 bytes per glyph (10 used, 6 pad) keeps the shift; the 6 KB total is nothing in chip RAM and
buys a branch-free inner loop.  ⚠ Sets are a full 128 entries each, INCLUDING $00-$1F, so a
control code renders as the blank it is displayed as, with no range test per cell.
"""

import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JSBEEB = os.path.join(REPO, "tools", "jsbeeb", "src", "teletext_data.js")
OUT = os.path.join(REPO, "src", "platform", "teletext_font.h")

CELL_W = 8          # pixels per cell, one byte per row
CELL_H = 10         # scan lines per cell == the SAA5050's native cell height
GLYPH_BYTES = 16    # padded to a power of two: index by shift, not multiply
GLYPH_SHIFT = 4
SETS = 3            # alpha, contiguous mosaics, separated mosaics

SRC_W, SRC_H = 6, 10   # the SAA5050 cell as jsbeeb stores it


def load_g0():
    """The 96 G0 glyphs ($20-$7F) as [glyph][row][col] bits."""
    with open(JSBEEB) as f:
        text = f.read()
    start = text.index("new Uint8Array([") + len("new Uint8Array([")
    body = text[start : text.index("]", start)]
    body = re.sub(r"//[^\n]*", "", body)          # comments carry hex codes; strip them FIRST
    nums = [int(x) for x in re.findall(r"\d+", body)]
    if len(nums) != 96 * SRC_W * SRC_H:
        sys.exit(f"expected {96 * SRC_W * SRC_H} pixels, parsed {len(nums)}")
    if set(nums) - {0, 1}:
        sys.exit("pixel data is not 0/1 — the parse is wrong")
    out = []
    for g in range(96):
        base = g * SRC_W * SRC_H
        out.append([nums[base + r * SRC_W : base + (r + 1) * SRC_W] for r in range(SRC_H)])
    return out


def g0_rows(glyph):
    """A 6x10 source glyph -> ten cell bytes.

    Source column c goes to bit (7-c), so the chip's own always-blank column 0 becomes the
    cell's left-hand spacing and bits 1-0 are the extra tracking documented above.  All ten
    source rows are kept, which is why descenders survive intact.
    """
    rows = []
    for r in range(CELL_H):
        b = 0
        for c in range(SRC_W):
            if glyph[r][c]:
                b |= 0x80 >> c
        rows.append(b)
    return rows


# ── the mosaics ───────────────────────────────────────────────────────────────────────────────
# A G1 code's six blocks come from its bits: b0 top-left, b1 top-right, b2 middle-left,
# b3 middle-right, b4 bottom-left, b6 bottom-right (b5 is what selects the mosaic ranges
# $20-$3F / $60-$7F in the first place).
#
# The three block rows split the 10 scan lines 3/3/4, exactly as the SAA5050 does — the one
# place where a 10-row cell pays off, because the split is the chip's own and needs no rounding.
BLOCK_ROWS = ((0, 3), (3, 6), (6, 10))
BLOCK_COLS = ((0, 4), (4, 8))


def mosaic_rows(code, separated):
    blocks = (code & 0x1F) | ((code & 0x40) >> 1)   # -> six bits, b0..b5 in reading order
    rows = [0] * CELL_H
    for by, (y0, y1) in enumerate(BLOCK_ROWS):
        for bx, (x0, x1) in enumerate(BLOCK_COLS):
            if not (blocks >> (by * 2 + bx)) & 1:
                continue
            ys, xs = range(y0, y1), range(x0, x1)
            if separated:
                # The separated set insets each block: its leftmost column and bottom row go.
                xs = range(x0 + 1, x1)
                ys = range(y0, y1 - 1)
            for y in ys:
                for x in xs:
                    rows[y] |= 0x80 >> x
    return rows


def build():
    g0 = load_g0()
    font = bytearray(SETS * 128 * GLYPH_BYTES)

    def put(sett, code, rows):
        base = ((sett * 128) + code) * GLYPH_BYTES
        font[base : base + len(rows)] = bytes(rows)

    for code in range(0x20, 0x80):
        alpha = g0_rows(g0[code - 0x20])
        put(0, code, alpha)
        # ⭐ In graphics mode $40-$5F are STILL alphanumerics — only $20-$3F and $60-$7F are
        # mosaics.  Filling the alpha glyphs into those slots of both graphics sets is what
        # lets the renderer pick a set from row state and then index with no range test.
        mosaic = 0x20 <= code <= 0x3F or 0x60 <= code <= 0x7F
        put(1, code, mosaic_rows(code, False) if mosaic else alpha)
        put(2, code, mosaic_rows(code, True) if mosaic else alpha)
    return font


def emit(font):
    lines = []
    a = lines.append
    a("#ifndef TELETEXT_FONT_H")
    a("#define TELETEXT_FONT_H")
    a("/* GENERATED by tools/gen_teletext_font.py — do NOT edit by hand.")
    a(" *")
    a(" * The MODE 7 character generator: the SAA5050's three character sets, laid out so that")
    a(" * fetching a glyph is one shift and drawing it is ten byte moves.  The generator's")
    a(" * header comment has the provenance, the cell-size derivation and the one documented")
    a(" * adaptation (horizontal tracking); read it before changing anything here.")
    a(" *")
    a(f" *   glyph = g_ttFont + ((set * 128 + code) << {GLYPH_SHIFT})")
    a(" *")
    a(" * set 0 = G0 alphanumerics, 1 = G1 contiguous mosaics, 2 = G1 separated mosaics.")
    a(" * In sets 1 and 2 the codes $40-$5F hold the ALPHA glyphs, because that is what the")
    a(" * chip displays there in graphics mode — so the renderer never needs a range test.")
    a(" */")
    a("")
    a(f"#define TT_CELL_W      {CELL_W}u   /* pixels per cell — one byte per row */")
    a(f"#define TT_CELL_H      {CELL_H}u   /* scan lines per cell = the SAA5050's native cell */")
    a(f"#define TT_GLYPH_BYTES {GLYPH_BYTES}u")
    a(f"#define TT_GLYPH_SHIFT {GLYPH_SHIFT}u")
    a(f"#define TT_SETS        {SETS}u")
    a("#define TT_SET_ALPHA   0u")
    a("#define TT_SET_GFX     1u   /* contiguous mosaics */")
    a("#define TT_SET_GFX_SEP 2u   /* separated mosaics */")
    a("")
    a("#define TT_COLS        40u")
    a("#define TT_ROWS        25u")
    a("#define TT_WIDTH       (TT_COLS * TT_CELL_W)   /* 320 */")
    a("#define TT_HEIGHT      (TT_ROWS * TT_CELL_H)   /* 250 */")
    a("")
    a("#ifdef __cplusplus")
    a('extern "C" {')
    a("#endif")
    a(f"extern const unsigned char g_ttFont[{SETS} * 128 * {GLYPH_BYTES}];")
    a("#ifdef __cplusplus")
    a("}")
    a("#endif")
    a("")
    a("#ifdef TELETEXT_FONT_DATA")
    a(f"const unsigned char g_ttFont[{SETS} * 128 * {GLYPH_BYTES}] = {{")
    names = ["G0 alphanumerics", "G1 contiguous mosaics", "G1 separated mosaics"]
    for s in range(SETS):
        a(f"    /* ── set {s}: {names[s]} ── */")
        for code in range(128):
            base = (s * 128 + code) * GLYPH_BYTES
            row = font[base : base + GLYPH_BYTES]
            ch = chr(code) if 0x20 < code < 0x7F else " "
            a("    " + "".join(f"0x{b:02X}," for b in row) + f"  /* ${code:02X} {ch} */")
    a("};")
    a("#endif /* TELETEXT_FONT_DATA */")
    a("")
    a("#endif /* TELETEXT_FONT_H */")
    return "\n".join(lines) + "\n"


def selftest(font):
    """A generated table that is silently blank is the classic failure here, so check shapes.

    ⚠ Not "the file is non-empty": the postmortem's rule is that an instrument has to be able
    to FAIL.  These assertions pin the glyphs a human can verify by eye in the dump below.
    """
    def rows_of(sett, code):
        base = (sett * 128 + code) * GLYPH_BYTES
        return list(font[base : base + CELL_H])

    ink = sum(1 for b in font if b)
    # Per SET, so a set that failed to build cannot hide behind the other two.  The bounds are
    # measured, not guessed: alpha 585, contiguous 700, separated 556 inked rows as generated.
    for s, lo in ((0, 450), (1, 550), (2, 430)):
        got = sum(1 for b in font[s * 128 * GLYPH_BYTES : (s + 1) * 128 * GLYPH_BYTES] if b)
        assert got >= lo, f"set {s} is mostly blank ({got} non-zero bytes, expected >= {lo})"
    assert rows_of(0, 0x20) == [0] * CELL_H, "space is not blank"
    assert rows_of(0, 0x00) == [0] * CELL_H, "control-code slot is not blank"
    # 'A' must have a hole in it: a solid or empty block means the parse slipped.
    a_rows = [r for r in rows_of(0, 0x41) if r]
    assert 5 <= len(a_rows) <= 8, f"'A' has {len(a_rows)} inked rows"
    # A descender must reach the last cell row — this is what the 10-row cell is for.
    assert rows_of(0, 0x67)[CELL_H - 1] or rows_of(0, 0x67)[CELL_H - 2], "'g' has no descender"
    # $7F is the solid block in the G0 set, and the front end draws the REVS logo with it.
    assert sum(1 for b in rows_of(0, 0x7F) if b) >= 7, "$7F is not a solid block"
    # Mosaic $FF-equivalent ($7F in the mosaic range) must fill the cell edge to edge, or
    # contiguous graphics will show seams.
    full = rows_of(1, 0x7F)
    assert all(b == 0xFF for b in full), f"contiguous mosaic $7F is not solid: {full}"
    # ...and the separated version must NOT.
    assert not all(b == 0xFF for b in rows_of(2, 0x7F)), "separated mosaic is not separated"
    return ink


def preview(font, codes, sett=0):
    for code in codes:
        base = (sett * 128 + code) * GLYPH_BYTES
        rows = font[base : base + CELL_H]
        print(f"  set {sett} ${code:02X} {chr(code) if 0x20 < code < 0x7f else ''}")
        for b in rows:
            print("    " + "".join("#" if b & (0x80 >> i) else "." for i in range(CELL_W)))


if __name__ == "__main__":
    font = build()
    ink = selftest(font)
    with open(OUT, "w") as f:
        f.write(emit(font))
    print(f"wrote {OUT}  ({len(font)} bytes, {ink} non-zero)")
    if "--preview" in sys.argv:
        preview(font, [0x41, 0x67, 0x52, 0x7F])
        preview(font, [0x23, 0x7F], sett=1)
        preview(font, [0x23, 0x7F], sett=2)
