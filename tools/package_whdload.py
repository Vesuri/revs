#!/usr/bin/env python3
"""Stage and archive the WHDLoad install -> dist/Revs-<version>.lha (LH5, level-1 headers).

    python3 tools/package_whdload.py            (what `make dist` runs, after the DIST build)

Members, and nothing else: the drawer icon plus Install, Install.info, ReadMe, ReadMe.info,
Revs (the DIST exe), Revs.inf and Revs.slave in "Revs Install/".  The version comes from the
exe's own $VER string and must agree with the slave's and the ReadMe's History.

⭐ THE RELEASE CARRIES NO ORIGINAL ENGINE BYTES (docs/phases.md §Phase 7), and this is where
that is enforced rather than hoped: every member is scanned for any 256-byte block of the disc's
REVS2 file (read from revs.ssd), and a single hit refuses the package.  The exe's circuit data,
title page and fonts are in it by the user's decision; REVS2 must never be.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PKG = 'Revs Install'
sys.path.insert(0, str(ROOT / 'tools'))


def version_of(data, name):
    m = re.search(rb'\$VER: ' + re.escape(name.encode()) + rb' (\d+\.\d+) \((\d\d\.\d\d\.\d{4})\)', data)
    if not m:
        raise SystemExit(f'no $VER: {name} string')
    return m.group(1).decode(), m.group(2).decode()


def revs2_bytes():
    d = (ROOT / 'revs.ssd').read_bytes()
    for i in range(d[0x105] // 8):
        e, f = d[8 + 8 * i:16 + 8 * i], d[0x108 + 8 * i:0x110 + 8 * i]
        if bytes(c & 0x7F for c in e[:7]) == b'REVS2  ':
            ln = f[4] | f[5] << 8 | ((f[6] >> 4) & 3) << 16
            sec = f[7] | (f[6] & 3) << 8
            return d[sec * 256:sec * 256 + ln]
    raise SystemExit('revs.ssd has no REVS2 — cannot run the no-original-bytes audit')


def main():
    exe = (ROOT / 'amiga/out/Revs.exe').read_bytes()
    slave = (ROOT / 'whdload/Revs.slave').read_bytes()
    src = ROOT / 'whdload' / PKG
    readme = (src / 'ReadMe').read_bytes()
    ver, date = version_of(exe, 'Revs')
    if version_of(slave, 'Revs.slave') != (ver, date):
        raise SystemExit(f'the slave\'s $VER disagrees with the exe\'s {ver} ({date})')
    if f'version {ver} ({date})'.encode() not in readme:
        raise SystemExit(f'the ReadMe has no History entry "version {ver} ({date})"')
    if b'@' in readme:
        raise SystemExit('the ReadMe still has an unfilled @placeholder@')

    members = {
        PKG + '.info': (ROOT / 'whdload' / (PKG + '.info')).read_bytes(),
        PKG + '/Install': (src / 'Install').read_bytes(),
        PKG + '/Install.info': (src / 'Install.info').read_bytes(),
        PKG + '/ReadMe': readme,
        PKG + '/ReadMe.info': (src / 'ReadMe.info').read_bytes(),
        PKG + '/Revs': exe,
        PKG + '/Revs.inf': (src / 'Revs.inf').read_bytes(),
        PKG + '/Revs.slave': slave,
    }

    engine = revs2_bytes()
    blocks = {engine[a:a + 256] for a in range(0, len(engine) - 255, 256)}
    blocks = {b for b in blocks if len(set(b)) > 8}          # ignore fill/padding runs
    for name, data in members.items():
        hits = sum(1 for b in blocks if b in data)
        if hits:
            raise SystemExit(f'refusing to package: {name} contains {hits} blocks of REVS2')
    print(f'audit: no member contains any of REVS2\'s {len(blocks)} 256-byte blocks')

    lha = shutil.which(os.environ.get('LHA', str(Path.home() / '.local/opt/lha/bin/lha')))
    if not lha:
        raise SystemExit('native LHa compressor not found; set LHA=/path/to/lha (lhasa is extract-only)')
    out = ROOT / 'dist'
    out.mkdir(exist_ok=True)
    archive = out / f'Revs-{ver}.lha'
    with tempfile.TemporaryDirectory(prefix='revs-pack-') as tmp:
        stage = Path(tmp)
        (stage / PKG).mkdir()
        for name, data in members.items():
            (stage / name).write_bytes(data)
        os.chmod(stage / PKG / 'Revs', 0o755)
        temporary = stage / archive.name
        # Level-1 headers and LH5, as the published WHDLoad archives; a fresh archive so a
        # removed member cannot survive.
        subprocess.run([lha, 'a1o5q2', str(temporary), *members], cwd=stage, check=True)
        test = subprocess.run([lha, 't', str(temporary)], capture_output=True, text=True)
        if test.returncode:
            raise SystemExit('archive self-test failed:\n' + test.stdout + test.stderr)
        shutil.move(str(temporary), archive)
    print(f'built {archive.relative_to(ROOT)} ({archive.stat().st_size} bytes, {len(members)} members, version {ver} ({date}))')


if __name__ == '__main__':
    main()
