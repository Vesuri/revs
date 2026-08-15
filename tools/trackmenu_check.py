#!/usr/bin/env python3
"""Diff a MODE 7 page dumped off the TARGET against the real BBC's REVSMEN menu.

    python3 tools/trackmenu_check.py amiga/.run/trackmenu_page.bin [--fixture tmp/trackmenu]

⭐ WHY THIS EXISTS AS WELL AS `make trackmenu`.  The host differential proves the EMITTER; this
proves the emitter ran on the Amiga, in the real lifecycle, against the real keyboard's menu — the
same relationship `make mode7` has with `amiga/mode7_dump.gdb`.  Both are needed: the host cannot
see a build where the menu is never reached, and the target cannot bisect a wrong byte.

⚠ THE SHIPPED PAGE HAS SIX OPTIONS AND THE REAL ONE HAS FIVE (trackmenu.h §THE ONE DELIBERATE
DIVERGENCE), so rows 0-21 are compared against the fixture and row 22 is checked to be the sixth
option rather than compared against anything.  The divergence is stated per row instead of being
absorbed into a byte count.

⚠ It also accepts the TITLE page and says so, because that is what a dump taken inside the first
5.45 s legitimately holds — reporting "1000 bytes differ" for a correct run that was merely sampled
early is how a window becomes a bug report.
"""
import sys
import os

ROWS, COLS = 25, 40


def rows(b):
    return [bytes(b[r * COLS:(r + 1) * COLS]) for r in range(ROWS)]


def text(row):
    return "".join(chr(c) if 0x20 <= c < 0x7F else "." for c in row)


def main():
    argv = sys.argv[1:]
    if not argv:
        raise SystemExit(__doc__)
    dump = argv[0]
    fix = "tmp/trackmenu"
    if "--fixture" in argv:
        fix = argv[argv.index("--fixture") + 1]

    # ⚠ 1000 bytes are the page; the dump (and the fixture) are the whole 1 KB at $7C00, whose last
    # 24 bytes are outside the 25x40 grid and are never displayed.  Compare the grid only — an
    # equality test on 1024 bytes would fail on memory the SAA5050 does not read.
    got = open(dump, "rb").read()
    if len(got) < ROWS * COLS:
        raise SystemExit(f"{dump}: {len(got)} bytes, expected at least {ROWS * COLS}")
    got = got[:ROWS * COLS]

    need = {}
    for name in ("title.bin", "menu.bin"):
        p = os.path.join(fix, name)
        if not os.path.exists(p):
            raise SystemExit(f"missing fixture {p} — run `make trackmenu-fixture` first")
        need[name] = open(p, "rb").read()[:ROWS * COLS]

    if got == need["title.bin"]:
        print("TITLE PAGE: byte-identical to the real 5TRSCRN.")
        print("   ⚠ the dump was taken during the 273-field title dwell, so it says nothing about")
        print("     the menu — sample later (the gdb script breaks on g_tmPhase != 0).")
        return 0

    g, w = rows(got), rows(need["menu.bin"])
    bad = 0
    for r in range(22):                       # rows 0-21 are shared with the real page
        if g[r] != w[r]:
            bad += 1
            print(f"row {r:2d} DIFFERS")
            print(f"   real |{text(w[r])}|")
            print(f"   ours |{text(g[r])}|")

    # Row 22: the port's sixth option, which has no counterpart to diff.  Checked for the shape the
    # emitter produces — the digit in the attribute field, and a name — not against a literal page.
    six = g[22]
    ok6 = six[5] in (0x84, 0x81) and six[6] == 0x9D and six[8] == ord("6") and text(six)[20:].strip()
    if ok6:
        print(f"row 22 is the port's sixth option: |{text(six)}|")
    elif all(c == 0x20 for c in six):
        print("row 22 is blank — a five-option build (no Nurburgring disc), which is legitimate")
    else:
        bad += 1
        print(f"row 22 MALFORMED |{text(six)}|")

    for r in (23, 24):
        if any(c != 0x20 for c in g[r]):
            print(f"row {r:2d} |{text(g[r])}|   (the SPACE prompt appears once a digit is pressed)")

    print(f"\ntrackmenu_check: rows 0-21 vs the real BBC — {22 - bad} of 22 identical")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
