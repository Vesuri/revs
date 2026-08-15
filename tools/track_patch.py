#!/usr/bin/env python3
"""Replay a track file's ModifyGameCode patcher → the exact bytes it writes into the engine.

    python3 tools/track_patch.py                     # every track, a report
    python3 tools/track_patch.py --track BRANDS      # one
    python3 tools/track_patch.py --verify            # cross-check against the measured
                                                     #   differential (needs tmp/dump_*)

## Why this exists

The four expansion tracks are PROGRAMS: the engine calls `CallTrackHook` ($5A22) during
`engine_init`, that is `JMP $5700` in every expansion track, and the code there patches the
engine before a frame is drawn (docs/static-map.md §The per-track engine patches).

The port is a TRANSLITERATION, not an emulator — engine "code" is C, not bytes in `mem[]` — so
the patcher cannot simply be run.  Two things are needed instead, and this tool provides the
first: the exact address→byte set the patcher produces, so the port can apply it as data at
track-selection time and the generated C can read the patched sites back out of `mem[]` through
the transpiler's SMC machinery.

## Why a REPLAY rather than a pattern match

`tools/track_hooks.py` decodes the patcher's 19-or-20-entry table statically and cross-checks it
against a RAM differential.  That is the right instrument for *inventory* — it answers "which
bytes does this track touch".  It is the wrong one for *values*, because the straight-line poke
runs at $5800 and $5600 carry their values in `LDA #imm` a few instructions earlier, and
recovering those by pattern is exactly the kind of nearly-right scan that cost this project the
OSBYTE inventory (docs/bbc-hardware.md).  So: execute it.  The patcher uses TWELVE instruction
forms —

    BPL rel   DEX   INY   JMP abs   LDA abx   LDA imm   LDX imm   LDY imm   RTS
    STA abs   STA (zp),Y   STA zp

— which is a 60-line interpreter that is exact rather than heuristic.  Same argument as
`tools/relocate.py`, which replays the engine's own unpack instead of describing it.

⚠ The bound comes from the CODE, never from the first example: the patch loop is `LDX #n` at
$5700 and n is $12 (19 entries) for Brands and Oulton but $13 (20) for Donington and Snetterton.
The interpreter reads it because it executes it; nothing here hardcodes a count.

## Confidence

`--verify` diffs this replay's output against the patch surface `track_hooks.py` measures on a
real BBC (relocate(before_T) vs after_T, minus the Silverstone control).  Agreement means a
static read and a hardware measurement independently produced the same set — the two failure
modes are opposite (a static read cannot tell a live table from a dead one; a differential cannot
tell a patch from ordinary state), so agreeing rules out both.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from relocate import relocate  # noqa: E402

TRACKS = ["SILVER", "BRANDS", "DONING", "OULTON", "SNETTER"]
CONTROL = "SILVER"

HOOK = 0x5A22           # CallTrackHook — the engine's fixed entry into the track file
TRACK_LO, TRACK_HI = 0x5300, 0x5A26   # where the unpack swap deposits the track file
ENGINE_LO, ENGINE_HI = 0x0B00, 0x7900


class Patcher:
    """Just enough 6502 to run ModifyGameCode.  Anything else is an error, not a guess."""

    def __init__(self, mem):
        self.m = mem
        self.a = self.x = self.y = 0
        self.n = False
        self.writes = {}        # address -> value, in execution order (last write wins)
        self.steps = 0

    def _nz(self, v):
        self.n = bool(v & 0x80)
        return v

    def _store(self, addr, val):
        self.m[addr] = val
        self.writes[addr] = val

    def run(self, pc, limit=100000):
        m = self.m
        while True:
            self.steps += 1
            if self.steps > limit:
                raise RuntimeError(f"patcher did not terminate within {limit} steps")
            op = m[pc]
            if op == 0xA9:      # LDA #imm
                self.a = self._nz(m[pc + 1]); pc += 2
            elif op == 0xA2:    # LDX #imm
                self.x = self._nz(m[pc + 1]); pc += 2
            elif op == 0xA0:    # LDY #imm
                self.y = self._nz(m[pc + 1]); pc += 2
            elif op == 0xBD:    # LDA abs,X
                base = m[pc + 1] | m[pc + 2] << 8
                self.a = self._nz(m[(base + self.x) & 0xFFFF]); pc += 3
            elif op == 0x8D:    # STA abs
                self._store(m[pc + 1] | m[pc + 2] << 8, self.a); pc += 3
            elif op == 0x85:    # STA zp
                self._store(m[pc + 1], self.a); pc += 2
            elif op == 0x91:    # STA (zp),Y
                zp = m[pc + 1]
                self._store(((m[zp] | m[(zp + 1) & 0xFF] << 8) + self.y) & 0xFFFF, self.a)
                pc += 2
            elif op == 0xCA:    # DEX
                self.x = self._nz((self.x - 1) & 0xFF); pc += 1
            elif op == 0xC8:    # INY
                self.y = self._nz((self.y + 1) & 0xFF); pc += 1
            elif op == 0x10:    # BPL rel
                off = m[pc + 1]
                pc += 2
                if not self.n:
                    pc = (pc + (off - 256 if off & 0x80 else off)) & 0xFFFF
            elif op == 0x4C:    # JMP abs
                pc = m[pc + 1] | m[pc + 2] << 8
            elif op == 0x60:    # RTS
                return
            else:
                raise RuntimeError(
                    f"${pc:04X}: opcode ${op:02X} is not in the patcher's vocabulary. "
                    "The patcher's shape changed, or this is not the patcher — do not extend "
                    "this interpreter without checking which.")


def runtime_image(track, ssd="revs.ssd", tmpdir="tmp/tracks"):
    """The post-unpack image for one track, built from the disc — no emulator involved."""
    import subprocess
    import shutil
    os.makedirs(tmpdir, exist_ok=True)
    mem_path = os.path.join(tmpdir, f"mem_{track}.bin")
    if not os.path.exists(mem_path):
        subprocess.run([sys.executable, "tools/ssd_load.py", ssd, tmpdir, track],
                       check=True, capture_output=True)
        shutil.move(os.path.join(tmpdir, "revs_mem.bin"), mem_path)
    img = bytearray(open(mem_path, "rb").read())
    relocate(img, log=lambda *_: None)
    return img


def patch_set(track):
    """(pre-patch image, {address: byte} the patcher writes into the ENGINE).

    Writes inside the track file's own window are excluded: $5300-$5A25 arrives with the track
    and is copied wholesale, so a patcher write there is already in the block.  Zero-page writes
    are excluded too — $74/$75 are the patcher's own pointer scratch, not a patch."""
    img = runtime_image(track)
    pre = bytes(img)
    if img[HOOK] == 0x60:                      # RTS — a passive circuit (Silverstone)
        return pre, {}
    if img[HOOK] != 0x4C:
        raise RuntimeError(f"{track}: $5A22 is ${img[HOOK]:02X}, neither RTS nor JMP")
    p = Patcher(img)
    p.run(img[HOOK + 1] | img[HOOK + 2] << 8)
    return pre, {a: v for a, v in sorted(p.writes.items())
                 if ENGINE_LO <= a < ENGINE_HI and not (TRACK_LO <= a < TRACK_HI)
                 and pre[a] != v}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--track", action="append", help="only this track (repeatable)")
    ap.add_argument("--verify", action="store_true",
                    help="cross-check against the measured differential (needs tmp/dump_*)")
    args = ap.parse_args()
    tracks = args.track or TRACKS

    sets = {}
    for t in tracks:
        pre, ps = patch_set(t)
        sets[t] = ps
        if not ps:
            print(f"# {t}: passive — $5A22 is RTS, the engine is not patched at all")
            continue
        print(f"# {t}: {len(ps)} engine bytes patched")
        for a, v in ps.items():
            print(f"    ${a:04X}  ${pre[a]:02X} -> ${v:02X}")
        print()

    if len(sets) > 1:
        active = {t: set(p) for t, p in sets.items() if p}
        if active:
            common = set.intersection(*active.values())
            print(f"# patched by EVERY expansion track: {len(common)} addresses")
            for t, p in active.items():
                print(f"#   {t}-only: {sorted('$%04X' % a for a in p - common)}")
            print()
            union = sorted(set.union(*active.values()))
            print(f"# ⭐ THE SMC SURFACE the port must honour: {len(union)} addresses")
            print("#   (each needs a site in tools/transpile.py SMC_SITES, so the generated C")
            print("#    reads it from mem[] at run time instead of baking the Silverstone byte)")
            for a in union:
                vals = sorted({sets[t][a] for t in active if a in sets[t]})
                base = patch_set(CONTROL)[0][a]
                print(f"    ${a:04X}  unpatched ${base:02X}  patched to "
                      + " ".join(f"${v:02X}" for v in vals))

    if args.verify:
        print()
        try:
            from track_hooks import residue
        except ImportError:
            print("verify: tools/track_hooks.py not importable"); return 1
        base = residue(CONTROL)
        if base is None:
            print("verify: no tmp/dump_SILVER_{before,after}.bin — run the jsbeeb track dumps "
                  "first (docs/bbc-reference-loop.md).  NOT a pass.")
            return 1
        ok = True
        for t in tracks:
            if t == CONTROL or not sets[t]:
                continue
            r = residue(t)
            if r is None:
                print(f"verify {t}: no dumps — SKIPPED, which is not agreement")
                ok = False
                continue
            measured = r[0] - base[0]
            replayed = set(sets[t])
            miss, extra = sorted(measured - replayed), sorted(replayed - measured)
            # ⚠ ASYMMETRIC ON PURPOSE, and the asymmetry is the whole verdict.
            #
            # only-in-replay MUST be empty: a byte this tool claims the patcher writes that the
            # machine never changed is a fabricated patch, and the port would apply it.
            #
            # only-in-differential is NOT symmetric, because the "after" dumps are taken at a
            # fixed cycle offset and therefore contain ordinary gameplay state (track_hooks.py's
            # own caveat).  $5FC9-$5FCD is that state — docs/static-map.md names it, Snetterton
            # shows 0 here, and the run length varies between tracks, which is what state looks
            # like and not what a patch looks like.  Anything OUTSIDE that window is a real gap.
            known_state = set(range(0x5FC9, 0x5FCE))
            unexplained = [a for a in miss if a not in known_state]
            print(f"verify {t}: replay {len(replayed)}, differential {len(measured)}; "
                  f"only-in-replay {len(extra)}, "
                  f"only-in-differential {len(miss)} "
                  f"({len(miss) - len(unexplained)} known runtime state, "
                  f"{len(unexplained)} unexplained)")
            for a in extra[:10]:
                print(f"    ⚠ ${a:04X} the replay writes it but the machine did not change it — "
                      "a FABRICATED patch")
            for a in unexplained[:10]:
                print(f"    ⚠ ${a:04X} changed on the machine, outside the known runtime-state "
                      "window, and the replay does not write it")
            ok &= not (unexplained or extra)
        print("verify: ✅ replay and hardware agree — every byte the replay writes was seen "
              "changing on a real BBC, and everything the machine changed beyond it is the "
              "documented $5FC9-$5FCD runtime state"
              if ok else
              "verify: ⚠ MISMATCH — read the addresses above before trusting either")
        return 0 if ok else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
