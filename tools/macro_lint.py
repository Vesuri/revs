#!/usr/bin/env python3
"""macro-lint — a FEATURE macro must never share its name with a CALL macro.

The defect this exists for was silent, shipped, and cost a wrong architectural conclusion.
`revs_plot.h` spelled its invocation macro `REVS_PLOT_RECTS()` and gave the feature-off branch a
no-op of the same name; `RevsPlot.cpp` then asked `#ifdef REVS_PLOT_RECTS`, which C answers TRUE
for a function-like macro as readily as for a `-D`.  So a build with the feature OFF took the
feature's arm: display lines 158..191 were claimed by an owner with no painter behind it, the two
dash needles froze on screen, and an A/B whose arms BOTH owned those rows was published as if one
of them did not.

The rule is mechanical, so the check is: any name that is somewhere `#define`d WITH PARAMETERS and
somewhere else tested by `#ifdef` / `#ifndef` / `defined()` is a collision.  Nothing else about
macros is linted here — this is one defect class, stated once.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIRS = ("src",)   # the port's own tree; vendored code is not ours to rename
EXTS = (".c", ".h", ".cpp", ".hpp")
SKIP = ("/amiga/framework/", "/compat-include/")

def_re  = re.compile(r'^\s*#\s*define\s+([A-Za-z_]\w*)\(')
# ⚠ `#ifdef` and `defined()` only, never `#ifndef`: `#ifndef MIN / #define MIN(a,b)` is the
# define-if-absent idiom, which is correct and would be nothing but noise here.
test_re = re.compile(r'#\s*ifdef\s+([A-Za-z_]\w*)|defined\s*\(\s*([A-Za-z_]\w*)\s*\)')

defined, tested = {}, {}
for d in DIRS:
    for base, _, files in os.walk(os.path.join(ROOT, d)):
        for f in files:
            if not f.endswith(EXTS):
                continue
            path = os.path.join(base, f)
            rel = os.path.relpath(path, ROOT)
            if any(s in "/" + rel for s in SKIP):
                continue
            with open(path, encoding="utf-8", errors="replace") as fh:
                for n, line in enumerate(fh, 1):
                    m = def_re.match(line)
                    if m:
                        defined.setdefault(m.group(1), []).append((rel, n))
                    for m in test_re.finditer(line):
                        name = m.group(1) or m.group(2)
                        tested.setdefault(name, []).append((rel, n))

bad = sorted(set(defined) & set(tested))
if bad:
    for name in bad:
        print("macro-lint: %s is BOTH a call macro and a feature test" % name)
        for rel, n in defined[name]:
            print("    #define %s(...)  %s:%d" % (name, rel, n))
        for rel, n in tested[name][:6]:
            print("    tested          %s:%d" % (rel, n))
    print("macro-lint: %d collision(s) — rename the CALL macro (a _RUN/_DO suffix)" % len(bad))
    sys.exit(1)
print("macro-lint: clean (%d call macros, %d feature tests)" % (len(defined), len(tested)))
