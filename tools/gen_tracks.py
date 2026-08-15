#!/usr/bin/env python3
"""gen_tracks.py — the per-track data table the port selects between.

    python3 tools/gen_tracks.py            # -> src/gen/revs_tracks.c
    python3 tools/gen_tracks.py --check    # print the table, write nothing

⭐ WHY THIS EXISTS AS DATA AND NOT AS CODE.  On a real BBC each circuit is a separate FILE, and
four of the five are *programs*: the loader drops the track file at $70DB, the engine's unpack
swaps $70DB-$7800 into $5300-$5A25, and then `CallTrackHook` ($5A22) runs the track's own
`ModifyGameCode`, which patches the engine.  The port is a transliteration — there is no 6502 to
run a patcher with — so the patcher's OUTPUT is applied as data instead.  `tools/track_patch.py`
replays it (twelve instruction forms, exactly; `make track-patch VERIFY=1` cross-checks the result
against the patch surface measured on a real BBC, only-in-replay = 0 for every track).

⭐ WHAT MAKES ONE BINARY TRACTABLE — two measurements, both re-derived here rather than trusted:

  1. Outside two extents the post-unpack engine image is BYTE-IDENTICAL across every circuit.
     Those extents are:
       BLOCK  $5300-$5A25  (1830 B) — where the unpack swap deposits the track file
       TAIL   $7800-$78AA  ( 171 B) — the part of the track file the swap does NOT move
     ⚠ The TAIL bound is derived from the longest track FILE, not from the diff of the commercial
     five.  Those are $738-$73C long, so they cannot write past $7816 — and the diff duly stops
     there.  The Nürburgring file is padded to $7D0, reaching $78AA, and it *uses* the space: a
     table of 5-byte records at $786B-$78AA that every commercial circuit leaves zero.  Sizing
     this extent off the five would have silently truncated the sixth.
  2. The patch surface is small and fixed per circuit (54-60 engine bytes).

So one embedded engine + N x (block, tail, patch list) covers every circuit.

⚠ SIXTH-CIRCUIT PROVENANCE.  The Nürburgring block is read from `revs-hack-nurburgring.ssd`, which
is **git-ignored** — exactly as `revs.ssd` is.  The repository therefore contains no third party's
file and no committed dependence on one, while a machine that has the disc gets the circuit.  The
measurements behind that decision (its hook code is 69.9% byte-identical to Brands's, with the
patcher byte-identical; and `revsplus.d64` has no Nürburgring at all) are in
docs/reference-sources.md §The Nürburgring file, MEASURED.  If the disc is absent this generator
emits the five commercial circuits and says so — it is not an error.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from relocate import relocate            # noqa: E402
from ssd_load import read_catalogue      # noqa: E402
from track_patch import Patcher, HOOK, TRACK_LO, TRACK_HI, ENGINE_LO, ENGINE_HI  # noqa: E402
from track_smc import hook_targets, extent_rows                                  # noqa: E402

BLOCK_LO, BLOCK_HI = 0x5300, 0x5A26      # [lo, hi)
TAIL_LO,  TAIL_HI  = 0x7800, 0x78AB      # [lo, hi)  — see the header note on the bound

# (ssd, dfs name, display name).  ⚠ Order is the SELECTOR ORDER and it is part of the port's
# behaviour: Silverstone first because it is the 1985 circuit and the engine's default.
CIRCUITS = [
    ("revs.ssd",                  "SILVER",  "Silverstone"),
    ("revs.ssd",                  "BRANDS",  "Brands Hatch"),
    ("revs.ssd",                  "DONING",  "Donington Park"),
    ("revs.ssd",                  "OULTON",  "Oulton Park"),
    ("revs.ssd",                  "SNETTER", "Snetterton"),
    ("revs-hack-nurburgring.ssd", "NURBURG", "Nurburgring"),
]


def runtime_image(ssd, track):
    """The post-unpack image for one circuit, straight from a disc.  No emulator involved."""
    img = open(ssd, "rb").read()
    cat = read_catalogue(img)
    byname = {f["name"].upper(): f for f in cat["files"]}
    for need in (track, "REVS2"):
        if need not in byname:
            raise RuntimeError(f"{ssd}: no file '{need}'")
    mem = bytearray(65536)
    for name in (track, "REVS2"):        # track first, then the engine — the *LO. / */REVS2 order
        f = byname[name]
        data = img[f["start"] * 256: f["start"] * 256 + f["length"]]
        mem[f["load"]:f["load"] + len(data)] = data
    relocate(mem, log=lambda *_: None)
    return mem


def patch_list(mem):
    """(address, value) pairs ModifyGameCode writes into the ENGINE, in address order.

    Writes inside the track window are excluded — $5300-$5A25 arrives WITH the track and is
    installed wholesale, so a patcher write there is already in the block.  Zero page is excluded
    because $74/$75 are the patcher's own pointer scratch, not a patch.  Same predicate as
    tools/track_patch.py's patch_set(); the two must not drift, so this imports its Patcher."""
    if mem[HOOK] == 0x60:                # RTS — a passive circuit (Silverstone)
        return []
    if mem[HOOK] != 0x4C:
        raise RuntimeError(f"$5A22 is ${mem[HOOK]:02X}, neither RTS nor JMP")
    pre = bytes(mem)
    p = Patcher(mem)                     # ⚠ mutates `mem`; callers pass a throwaway image
    p.run(mem[HOOK + 1] | mem[HOOK + 2] << 8)
    return [(a, v) for a, v in sorted(p.writes.items())
            if ENGINE_LO <= a < ENGINE_HI and not (TRACK_LO <= a < TRACK_HI) and pre[a] != v]


def collect():
    out = []
    rows = extent_rows()
    for ssd, dfs, disp in CIRCUITS:
        if not os.path.exists(ssd):
            print(f"# note: {ssd} not present — skipping {disp} "
                  f"(see the provenance note in this file's header)", file=sys.stderr)
            continue
        img = runtime_image(ssd, dfs)
        block = bytes(img[BLOCK_LO:BLOCK_HI])
        tail = bytes(img[TAIL_LO:TAIL_HI])
        patches = patch_list(runtime_image(ssd, dfs))    # fresh image: patch_list mutates
        # ⭐ The circuit's HOOK ENTRIES — the track-window addresses its patched JSR/JMPs call.
        # Installing the patch bytes is half the job; something has to be at the other end, and
        # a circuit whose hooks have no C bodies must be REFUSED rather than run into a trap
        # mid-race.  Derived by decoding the patched extents (tools/track_smc.py hook_targets).
        hooks = hook_targets(bytes(img), dict(patches), rows)
        out.append(dict(dfs=dfs, disp=disp, block=block, tail=tail, patches=patches,
                        hooks=hooks))
    return out


def carr(name, data, per=16):
    lines = [f"static const unsigned char {name}[{len(data)}] = {{"]
    for i in range(0, len(data), per):
        lines.append("    " + " ".join(f"0x{b:02X}," for b in data[i:i + per]))
    lines.append("};")
    return "\n".join(lines)


HEADER = """/* GENERATED by tools/gen_tracks.py — DO NOT EDIT.
 *
 * The per-circuit data the port selects between: for each circuit the two extents that differ in
 * the post-unpack engine image, plus the engine patches its ModifyGameCode would have written.
 * src/platform/track.h is the model and has the reasoning; the generator's header has the
 * measurements the extents are derived from.
 */
#include "revs_tracks.h"
"""


def emit(tracks):
    parts = [HEADER]
    for i, t in enumerate(tracks):
        parts.append(f"\n/* ---- {t['disp']} ({t['dfs']}) ---- */")
        parts.append(carr(f"blk{i}", t["block"]))
        parts.append(carr(f"tail{i}", t["tail"]))
        if t["patches"]:
            addrs = "".join(f"0x{a:04X}," for a, _ in t["patches"])
            vals = "".join(f"0x{v:02X}," for _, v in t["patches"])
            parts.append(f"static const unsigned short pa{i}[{len(t['patches'])}] = {{{addrs}}};")
            parts.append(f"static const unsigned char  pv{i}[{len(t['patches'])}] = {{{vals}}};")
        else:
            parts.append(f"/* {t['disp']} is PASSIVE — $5A22 is RTS, the engine is not patched. */")
        if t["hooks"]:
            hk = "".join(f"0x{a:04X}," for a in t["hooks"])
            parts.append(f"static const unsigned short hk{i}[{len(t['hooks'])}] = {{{hk}}};")
    parts.append(f"\nconst RevsTrack revs_tracks[REVS_TRACK_COUNT] = {{")
    for i, t in enumerate(tracks):
        pa = f"pa{i}, pv{i}, {len(t['patches'])}" if t["patches"] else "0, 0, 0"
        hk = f"hk{i}, {len(t['hooks'])}" if t["hooks"] else "0, 0"
        parts.append(f'    {{ "{t["disp"]}", blk{i}, tail{i}, {pa}, {hk} }},')
    parts.append("};\n")
    return "\n".join(parts)


HDR = """/* GENERATED by tools/gen_tracks.py — DO NOT EDIT.  See src/platform/track.h for the model. */
#ifndef REVS_TRACKS_H
#define REVS_TRACKS_H

#define REVS_TRACK_COUNT       %(n)d
#define REVS_TRACK_BLOCK_LO    0x%(blo)04X
#define REVS_TRACK_BLOCK_LEN   %(blen)d
#define REVS_TRACK_TAIL_LO     0x%(tlo)04X
#define REVS_TRACK_TAIL_LEN    %(tlen)d

/* ⚠ `patches` is (address, value) in ADDRESS order, not execution order: the replay's last write
   wins, so what is stored is the settled value per address.  A circuit with none is passive. */
typedef struct {
    const char*           name;      /* as the front end prints it */
    const unsigned char*  block;     /* REVS_TRACK_BLOCK_LEN bytes -> REVS_TRACK_BLOCK_LO */
    const unsigned char*  tail;      /* REVS_TRACK_TAIL_LEN  bytes -> REVS_TRACK_TAIL_LO  */
    const unsigned short* patchAddr;
    const unsigned char*  patchVal;
    unsigned short        patchCount;
    /* ⭐ The track-window ($5300-$5A25) addresses this circuit's patched JSR/JMPs call — its
       HOOK ENTRIES, ascending.  revs_track_hook() must have a body for every one of them or the
       circuit is refused: patch bytes in mem[] with nothing at the other end is not a playable
       circuit, it is a trap waiting for the first corner.  Derived by decoding the patched
       extents (tools/track_smc.py hook_targets), never by matching the patch values. */
    const unsigned short* hookAddr;
    unsigned short        hookCount;
} RevsTrack;

#ifdef __cplusplus
extern "C" {
#endif
extern const RevsTrack revs_tracks[REVS_TRACK_COUNT];
#ifdef __cplusplus
}
#endif

#endif
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="print the table, write nothing")
    ap.add_argument("--outdir", default="src/gen")
    ap.add_argument("--fixtures", metavar="DIR",
                    help="also write the expected POST-INSTALL 64K image per circuit, for "
                         "tools/validate_tracks.c to diff the real installer against")
    a = ap.parse_args()

    tracks = collect()
    if not tracks:
        print("ERROR: no circuits could be built", file=sys.stderr)
        return 1

    print(f"# {len(tracks)} circuits, block {BLOCK_HI - BLOCK_LO} B + tail {TAIL_HI - TAIL_LO} B each")
    for t in tracks:
        nz = sum(1 for b in t["tail"] if b)
        print(f"    {t['disp']:<15} {len(t['patches']):>2} engine patches, "
              f"tail {nz:>3}/{len(t['tail'])} non-zero")
    total = len(tracks) * (BLOCK_HI - BLOCK_LO + TAIL_HI - TAIL_LO) + \
        sum(3 * len(t["patches"]) for t in tracks)
    print(f"# embedded size ~{total} bytes")

    if a.fixtures:
        # ⭐ The fixture is the WHOLE 64K post-install image, built down an INDEPENDENT path:
        # relocate() the disc image for THIS circuit, then apply its patches.  The installer under
        # test instead starts from SILVERSTONE's image and writes two extents plus the patches.
        # Requiring the two to be byte-identical tests the installer AND re-proves the claim that
        # nothing outside those extents differs — if a third extent existed, this fails.
        os.makedirs(a.fixtures, exist_ok=True)
        names = []
        for ssd, dfs, disp in CIRCUITS:
            if not os.path.exists(ssd):
                continue
            img = runtime_image(ssd, dfs)
            for addr, val in patch_list(runtime_image(ssd, dfs)):
                img[addr] = val
            p = os.path.join(a.fixtures, f"expect_{dfs}.bin")
            open(p, "wb").write(bytes(img))
            names.append(dfs)
        # The base image the installer starts from: circuit 0, unpatched.
        ssd0, dfs0, _ = CIRCUITS[0]
        open(os.path.join(a.fixtures, "base.bin"), "wb").write(bytes(runtime_image(ssd0, dfs0)))
        print(f"# wrote {len(names)} fixtures + base.bin to {a.fixtures}/")

    if a.check:
        return 0
    os.makedirs(a.outdir, exist_ok=True)
    with open(os.path.join(a.outdir, "revs_tracks.h"), "w") as fh:
        fh.write(HDR % dict(n=len(tracks), blo=BLOCK_LO, blen=BLOCK_HI - BLOCK_LO,
                            tlo=TAIL_LO, tlen=TAIL_HI - TAIL_LO))
    with open(os.path.join(a.outdir, "revs_tracks.c"), "w") as fh:
        fh.write(emit(tracks))
    print(f"# wrote {a.outdir}/revs_tracks.[ch]")
    return 0


if __name__ == "__main__":
    sys.exit(main())
