#!/usr/bin/env python3
"""Run the installed game under WHDLoad in FS-UAE and judge WHDLoad's own core dump.

    python3 tools/test_whdload.py --mode=run  --exe amiga/out/Revs.exe     # the release (DIST=1)
    python3 tools/test_whdload.py --mode=quit --exe <a QUITTEST=600 build>
    python3 tools/test_whdload.py --mode=nodisc                            # must refuse to start

The machine is a 2 MB 68000 A500 (1 MB chip + 1 MB fast, Kickstart 3.1 under WHDLoad from a
Workbench 2.04 floppy), and the SLAVE boots Kickstart 1.3 (kick34005.A500 + .RTB) inside WHDLoad's
memory, so this is the Kickstart 1.3 run the plain FS-UAE harness cannot do (FS-UAE does not boot
1.3 from a directory drive).  ROMs, the Workbench disk and the Revs disc image are local inputs,
never shipped.  Modelled on the Vette port's harness.

  run     WHDLoad TIMEOUT ends the run and dumps memory — on a 68010, because WHDLoad sees its
          timer (like the QuitKey) only through a moved VBR; on a 68000 the run never ends.  PASS = the dump holds the engine image
          (most 256-byte blocks of disasm/revs_runtime.bin's code, $1200-$52FF, verbatim), i.e.
          the exe read REVS2 off data/revs.ssd, rebuilt the image and the game is running on 1.3.
  quit    the exe quits by itself (QUITTEST): PASS = the slave returned normally ("Return OK").
  nodisc  no revs.ssd: PASS = WHDLoad reports the slave's startup failure, not a crash.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
BIOS = Path.home() / 'Documents/RetroPie/BIOS'


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--mode', choices=('run', 'quit', 'nodisc'), default='run')
    p.add_argument('--exe', type=Path, default=ROOT / 'amiga/out/Revs.exe')
    p.add_argument('--slave', type=Path, default=ROOT / 'whdload/Revs.slave')
    p.add_argument('--disc', type=Path, default=ROOT / 'revs.ssd')
    p.add_argument('--whdload', type=Path, default=Path.home() / '.local/share/amiga/WHDLoad/C/WHDLoad')
    p.add_argument('--rom13', type=Path, default=BIOS / 'kick34005.A500')
    p.add_argument('--rtb', type=Path, default=Path.home() / 'Documents/amiberry/whdboot/save-data/Kickstarts/kick34005.A500.RTB')
    p.add_argument('--host-rom', type=Path, default=BIOS / 'kick31.rom')
    p.add_argument('--workbench', type=Path, default=Path.home() / 'Documents/Vette/tmp/Workbenchv2.04rev37.67Workbench.adf')
    p.add_argument('--cpu', default=None, help='default: 68010 for run (WHDLoad\'s TIMEOUT needs a movable VBR), else 68000')
    p.add_argument('--ticks', type=int, default=1500, help='WHDLoad timeout in PAL fields (run mode)')
    p.add_argument('--seconds', type=int, default=400, help='host safety ceiling')
    args = p.parse_args()

    (ROOT / 'tmp').mkdir(exist_ok=True)
    base = Path(tempfile.mkdtemp(prefix='whdload-test-', dir=ROOT / 'tmp'))
    print('Fixture:', base, flush=True)
    boot, game = base / 'boot', base / 'game'
    for d in (boot / 's', boot / 'devs/Kickstarts', game / 'data', base / 'state'):
        d.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.whdload, game / 'WHDLoad')
    shutil.copyfile(args.slave, game / 'Revs.slave')
    shutil.copyfile(args.rom13, boot / 'devs/Kickstarts/kick34005.A500')
    shutil.copyfile(args.rtb, boot / 'devs/Kickstarts/kick34005.A500.RTB')
    shutil.copyfile(args.exe, game / 'data/Revs')
    if args.mode != 'nodisc':
        shutil.copyfile(args.disc, game / 'data/revs.ssd')
    timeout = f'TIMEOUT={args.ticks} ' if args.mode == 'run' else ''
    (boot / 's/WHDLoad.prefs').write_text('Expert\nReadDelay=0\n')
    (boot / 's/startup-sequence').write_text(
        'DF0:C/Assign C: DF0:C\nDF0:C/Assign LIBS: DF0:Libs\n'
        'DF0:C/Assign DEVS: DH0:devs\nStack 16384\nFailAt 999\n'
        f'CD DH1:\nWHDLoad Revs.slave PRELOAD SPLASHDELAY=0 NOREQ COREDUMP {timeout}>DH0:result\n'
        'If WARN\nEcho failed >DH0:failed\nElse\nEcho passed >DH0:passed\nEndIf\n')

    with (base / 'emulator.log').open('w') as log:
        cpu = args.cpu or ('68010' if args.mode == 'run' else '68000')
        print('CPU:', cpu, flush=True)
        emu = subprocess.Popen(['fs-uae', '--amiga_model=A500', '--cpu=' + cpu,
            '--chip_memory=1024', '--fast_memory=1024',
            '--kickstart_file=' + str(args.host_rom),
            '--hard_drive_0=' + str(boot), '--hard_drive_0_priority=10', '--hard_drive_1=' + str(game),
            '--floppy_drive_0=' + str(args.workbench), '--audio_driver=dummy',
            '--joystick_port_0=mouse', '--joystick_port_1=nothing', '--warp_mode=1', '--fullscreen=0',
            '--window_width=720', '--window_height=568', '--state_dir=' + str(base / 'state')],
            stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline and not any((boot / n).exists() for n in ('passed', 'failed')):
                if emu.poll() is not None:
                    raise SystemExit('FS-UAE exited unexpectedly: ' + str(base / 'emulator.log'))
                time.sleep(.25)
            time.sleep(1)
        finally:
            emu.terminate()
            try: emu.wait(timeout=5)
            except subprocess.TimeoutExpired: emu.kill(); emu.wait()

    output = (boot / 'result').read_text(errors='replace') if (boot / 'result').exists() else ''
    report = (game / '.whdl_register').read_text(encoding='latin1') if (game / '.whdl_register').exists() else ''
    if not report:
        raise SystemExit(f'FAIL: no WHDLoad core dump ({base})\n{output}')
    first = next((l for l in report.splitlines() if l.strip()), '')
    print('WHDLoad:', first.strip())

    if args.mode == 'run':
        dumps = b''.join((game / n).read_bytes() for n in ('.whdl_dump', '.whdl_expmem') if (game / n).exists())
        image = (ROOT / 'disasm/revs_runtime.bin').read_bytes()
        blocks = [image[a:a + 256] for a in range(0x1200, 0x5300, 256)]
        found = sum(1 for b in blocks if b in dumps)
        print(f'engine image in the dump: {found} of {len(blocks)} code blocks verbatim')
        ok = 'DEBUG caused' in report or 'timeout' in report.lower()
        if not ok or found < len(blocks) * 3 // 4:
            raise SystemExit('FAIL: the game did not reach a running engine\n' + report[:2000])
        print('PASS: Revs runs under WHDLoad on the Kickstart 1.3 kickemu, engine read off the disc')
    elif args.mode == 'quit':
        if 'Return OK' not in report:
            raise SystemExit('FAIL: the slave did not return normally\n' + report[:2000])
        print('PASS: CTRL-Q returned through the slave (Return OK)')
    else:
        if 'could not start' not in report and 'could not start' not in output:
            raise SystemExit('FAIL: no startup-failure message\n' + report[:2000] + output)
        print('PASS: a missing disc image is reported, not a crash')


if __name__ == '__main__':
    main()
