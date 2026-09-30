#!/usr/bin/env python3
"""Run the real Amiga Installer on "Revs Install/Install" in FS-UAE, with scripted answers.

    . amiga/env.sh && python3 tools/test_installer.py            (every scenario)
    python3 tools/test_installer.py --only=keep-delete            (one of them)

Only the requesters are replaced (askdir / askbool / askfile answered from the scenario, every
`message` logged instead of shown, the ReadMe viewer skipped); the copying, the drawer deletion
and the disc-image handling are the release script's own code, run by Commodore's Installer
43.3 on an A1200 (Kickstart 3.1) + the Workbench 2.04 floppy.  Modelled on the Vette port's
tools/install-data/test_installer_script.py.  The Installer binary, ROM and Workbench disk are
local inputs, never shipped.

The scenarios are the disc-image branches:
  fresh          no drawer: the .ssd is asked for and installed
  keep-delete    drawer exists, "Use existing" + "Delete": the old image survives the deletion
  keep-skip      drawer exists, "Use existing" + "Skip": the old image and the drawer stay
  again-delete   "Select again" + "Delete": the .ssd is asked for and replaces the old image
  stash          a cancelled reinstall left the image parked beside the drawer: it is offered and restored
  bad-size       an installed image of the wrong size is not offered; the .ssd is asked for
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
PKG = ROOT / 'whdload' / 'Revs Install'
BIOS = Path.home() / 'Documents/RetroPie/BIOS'

# name: (seed, ssd-keep answer, drawer-delete answer, expect ssd asked, expect file asked,
#        expect installed image ('old' | 'new'), expect the old drawer's marker file kept)
SCENARIOS = {
    'fresh':        (None,    None, None, False, True,  'new', False),
    'keep-delete':  ('drawer', 1,   1,    True,  False, 'old', False),
    'keep-skip':    ('drawer', 1,   0,    True,  False, 'old', True),
    'again-delete': ('drawer', 0,   1,    True,  True,  'new', False),
    'stash':        ('stash',  1,   1,    True,  False, 'old', False),
    'bad-size':     ('bad',    None, 1,   False, True,  'new', False),
}


def replace_form(text, start, replacement):
    """Replace the whole parenthesised form that begins at `start` (string-aware)."""
    a = text.index(start)
    depth, quoted, i = 0, False, a
    while i < len(text):
        c = text[i]
        if quoted and c == '\\':
            i += 2
            continue
        if c == '"':
            quoted = not quoted
        elif not quoted:
            if c == '(':
                depth += 1
            elif c == ')':
                depth -= 1
                if depth == 0:
                    return text[:a] + replacement + text[i + 1:]
        i += 1
    raise ValueError('unbalanced Installer form at ' + start)


def mark(name):
    return f'(textfile (dest "DH2:{name}") (append "asked"))'


def scripted(keep, delete, trace=False):
    s = (PKG / 'Install').read_text(encoding='latin1')
    # Installer shows a startup requester unless the script mentions (welcome) somewhere.
    s = '(if 0 (welcome))\n' + s
    # ⚠ Installer's `textfile` fails (a requester, i.e. a hang here) when its file already exists,
    # so every logged form writes a file of its own.
    n = 0
    while '(message' in s:
        s = replace_form(s, '(message', f'(textfile (dest "DH2:message-{n}") (append "shown"))')
        n += 1
    s = replace_form(s, '(if (exists #readme-file)\n  (if (= 0', '("")')
    s = replace_form(s, '(if (exists #readme2-file)\n  (if (= 0', '("")')
    s = replace_form(s, '(set #dest\n  (askdir', '(set #dest "DH2:out")')
    s = replace_form(s, '(set #ssd-keep\n    (askbool', f'({mark("asked-ssd")} (set #ssd-keep {keep or 0}))')
    s = replace_form(s, '(set #choice\n    (askbool', f'({mark("asked-drawer")} (set #choice {delete or 0}))')
    s = replace_form(s, '(set #SF_filename\n    (askfile', f'({mark("asked-file")} (set #SF_filename "DH1:new.ssd"))')
    s = replace_form(s, '(exit)', '(exit (quiet))')
    if trace:
        for i, at in enumerate(('(set #program "WHDLoad")', '(if (getenv "WHDLInstPath")', '(P_SelectVersion)\n', '(run ("setenv WHDLInstPath',
                                '(makedir #dest\n  (help @makedir-help)\n  (infos)', '(P_MakeIcons)\n(set #copy-file',
                                '(P_MakeImages)\n(PDI_Eject)', '(exit (quiet))')):
            assert s.count(at) == 1, at
            s = s.replace(at, f'(textfile (dest "DH2:trace-{i}") (append "reached"))\n' + at)
    return s


def run(name, args):
    seed, keep, delete, want_ssd, want_file, want_image, want_marker = SCENARIOS[name]
    base = Path(tempfile.mkdtemp(prefix=f'installer-{name}-', dir=ROOT / 'tmp'))
    boot, src, work = base / 'boot', base / 'src', base / 'work'
    for d in (boot / 's', boot / 'devs/Kickstarts', src, work / 'out', base / 'state'):
        d.mkdir(parents=True, exist_ok=True)
    for f in ('Install.info', 'ReadMe', 'ReadMe.info', 'Revs.inf'):
        shutil.copyfile(PKG / f, boot / f)
    shutil.copyfile(args.exe, boot / 'Revs')
    shutil.copyfile(args.slave, boot / 'Revs.slave')
    shutil.copyfile(args.installer, boot / 'Installer')
    shutil.copyfile(args.whdload, boot / 'WHDLoad')
    (boot / 'Install').write_text(scripted(keep, delete, args.trace), encoding='latin1')

    new = (ROOT / 'revs.ssd').read_bytes()
    old = bytes((i * 7 + 3) & 0xFF for i in range(204800))      # a 204800-byte image the test can tell apart
    shutil.copyfile(ROOT / 'revs.ssd', src / 'new.ssd')
    dest = work / 'out' / 'Revs'
    if seed in ('drawer', 'stash', 'bad'):
        (dest / 'data').mkdir(parents=True)
        (dest / 'marker').write_text('old installation')
        (dest / 'data' / 'Revs').write_bytes(b'old release')
        if seed == 'drawer':
            (dest / 'data' / 'revs.ssd').write_bytes(old)
        elif seed == 'bad':
            (dest / 'data' / 'revs.ssd').write_bytes(b'x' * 1000)
        else:
            (work / 'out' / 'Revs.revs-ssd').write_bytes(old)

    (boot / 's/startup-sequence').write_text(
        # ENV:/ENVARC: must exist: the script's getenv/setenv otherwise raise "Please insert
        # volume ENV:" and wait for ever (a normal startup-sequence makes them).
        'DF0:C/Assign C: DF0:C\nDF0:C/Assign LIBS: DF0:Libs\nDF0:C/Assign DEVS: DH0:devs\n'
        'MakeDir RAM:ENV RAM:ENVARC\nAssign ENV: RAM:ENV\nAssign ENVARC: RAM:ENVARC\n'
        'CD DH0:\nStack 16384\nPath DH0: ADD\nC:LoadWB\n'
        'Installer SCRIPT DH0:Install APPNAME Revs MINUSER NOVICE DEFUSER NOVICE '
        'LOGFILE DH2:installer.log NOPRETEND >DH2:installer-console.log\n'
        'Echo done >DH2:finished\n')

    with (base / 'emulator.log').open('w') as log:
        # An A1200, as Vette's and Slicks' Installer tests: the script is not what a 68000 is
        # needed to test, and Installer's run/copy work on a 7 MHz machine outlasts the ceiling.
        emu = subprocess.Popen(['fs-uae', '--amiga_model=A1200', '--chip_memory=2048',
            '--fast_memory=0', '--kickstart_file=' + str(args.host_rom),
            '--hard_drive_0=' + str(boot), '--hard_drive_0_priority=10',
            '--hard_drive_1=' + str(src), '--hard_drive_2=' + str(work),
            '--floppy_drive_0=' + str(args.workbench), '--audio_driver=dummy', '--warp_mode=1',
            '--fullscreen=0', '--window_width=720', '--window_height=568',
            '--state_dir=' + str(base / 'state')], stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline and not (work / 'finished').exists():
                if emu.poll() is not None:
                    raise SystemExit(f'{name}: FS-UAE exited unexpectedly ({base})')
                time.sleep(.25)
            time.sleep(1)
            if args.screenshot and not (work / 'finished').exists():
                subprocess.run(['screencapture', '-x', str(base / 'screen.png')])
        finally:
            emu.terminate()
            try:
                emu.wait(timeout=5)
            except subprocess.TimeoutExpired:
                emu.kill()
                emu.wait()

    report = ''
    for f in ('installer.log', 'installer-console.log'):
        if (work / f).exists():
            report += f'--- {f}\n' + (work / f).read_text(errors='replace')
    report += '--- logged: ' + ' '.join(sorted(f.name for f in work.glob('*') if f.name.startswith(('message-', 'trace-', 'asked-')) and not f.name.endswith('.uaem'))) + '\n'
    problems = []
    if not (work / 'finished').exists():
        problems.append('Installer did not finish')
    ssd = dest / 'data' / 'revs.ssd'
    checks = [
        ((work / 'asked-ssd').exists() == want_ssd, f'"use the existing image?" asked={not want_ssd}, expected {want_ssd}'),
        ((work / 'asked-drawer').exists() == (seed is not None), 'wrong "drawer exists" prompt path'),
        ((work / 'asked-file').exists() == want_file, f'disc image file requester asked={not want_file}, expected {want_file}'),
        (ssd.exists() and ssd.read_bytes() == (old if want_image == 'old' else new), f'installed revs.ssd is not the {want_image} image'),
        ((dest / 'marker').exists() == want_marker, f'old drawer contents kept={not want_marker}, expected {want_marker}'),
        (not (work / 'out' / 'Revs.revs-ssd').exists(), 'the parked image was left beside the drawer'),
        ((dest / 'data' / 'Revs').exists() and (dest / 'data' / 'Revs').read_bytes() == (boot / 'Revs').read_bytes(), 'data/Revs is not the new exe'),
        ((dest / 'Revs.slave').exists() and (dest / 'Revs.slave').read_bytes() == (boot / 'Revs.slave').read_bytes(), 'Revs.slave missing or stale'),
        ((dest / 'Revs.info').exists(), 'no Revs.info'),
    ]
    problems += [msg for ok, msg in checks if not ok]
    if problems:
        print(f'FAIL {name}: ' + '; '.join(problems) + f'  ({base})\n{report[-3000:]}')
        return False
    print(f'PASS {name}')
    shutil.rmtree(base, ignore_errors=True)
    return True


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--only', choices=sorted(SCENARIOS))
    p.add_argument('--exe', type=Path, default=ROOT / 'amiga/out/Revs.exe')
    p.add_argument('--slave', type=Path, default=ROOT / 'whdload/Revs.slave')
    p.add_argument('--installer', type=Path, default=Path.home() / 'Documents/Stunt Car Racer/data/Installer43_3/Installer')
    p.add_argument('--whdload', type=Path, default=Path.home() / '.local/share/amiga/WHDLoad/C/WHDLoad')
    p.add_argument('--host-rom', type=Path, default=BIOS / 'kick40068.A1200')
    p.add_argument('--workbench', type=Path, default=Path.home() / 'Documents/Vette/tmp/Workbenchv2.04rev37.67Workbench.adf')
    p.add_argument('--screenshot', action='store_true', help='on a hang, capture the host screen to <fixture>/screen.png')
    p.add_argument('--trace', action='store_true', help='log checkpoints through the script to DH2:trace')
    p.add_argument('--seconds', type=int, default=240, help='host ceiling per scenario')
    args = p.parse_args()
    (ROOT / 'tmp').mkdir(exist_ok=True)
    names = [args.only] if args.only else list(SCENARIOS)
    failed = [n for n in names if not run(n, args)]
    if failed:
        raise SystemExit('FAIL: ' + ', '.join(failed))
    print(f'PASS: all {len(names)} Installer scenarios')


if __name__ == '__main__':
    main()
