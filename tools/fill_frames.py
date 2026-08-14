#!/usr/bin/env python3
"""Render the frames amiga/fill_frames*.gdb dumped, and stack their horizons for comparison.

amiga/screen_dump.gdb + tools/amiga_ppm.py answer "what is the target showing?" for ONE frame.
The horizon artefacts (a black or green run where the green band belongs) are per-frame, so the
useful picture is a STACK: the same horizon strip from a dozen consecutive frames, one above the
other, where a bad frame stands out against its neighbours instead of having to be judged alone.

    python3 tools/fill_frames.py [prefix]        # prefix default amiga/.run/ff

Writes tmp/fill/<n>-amiga.ppm per frame plus tmp/fill/horizon_stack.png.

⚠ WHAT THIS CANNOT SEE, and it is the reason the raster race took a second instrument: a dump
taken at a frame boundary shows a CONSISTENT copper list whatever the beam did while the list
was being rewritten mid-field.  Twelve clean frames here were followed by an obviously broken
FS-UAE window.  For that question, measure the beam (amiga/beam_watch.gdb).
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(HERE, 'tmp', 'fill')
BAND = (72, 112)          # the horizon neighbourhood, display lines


def main(prefix):
    os.makedirs(OUT, exist_ok=True)
    frames = []
    for i in range(64):
        pl = '%s_pl_%02d.bin' % (prefix, i)
        cop = '%s_cop_%02d.bin' % (prefix, i)
        if not (os.path.exists(pl) and os.path.exists(cop)):
            break
        out = os.path.join(OUT, '%02d' % i)
        subprocess.run([sys.executable, os.path.join(HERE, 'tools', 'amiga_ppm.py'),
                        pl, cop, out], check=True, capture_output=True)
        frames.append(out + '-amiga.ppm')
    if not frames:
        sys.exit('no dumps at %s_pl_NN.bin — run amiga/fill_frames.gdb first' % prefix)

    try:
        from PIL import Image
    except ImportError:
        print('rendered %d frames into %s (install Pillow for the stack)' % (len(frames), OUT))
        return
    h = BAND[1] - BAND[0]
    stack = Image.new('RGB', (640, 2 * h * len(frames)))
    for i, p in enumerate(frames):
        strip = Image.open(p).crop((0, BAND[0], 320, BAND[1])).resize((640, 2 * h), Image.NEAREST)
        stack.paste(strip, (0, i * 2 * h))
    path = os.path.join(OUT, 'horizon_stack.png')
    stack.save(path)
    print('%d frames rendered; horizon stack -> %s' % (len(frames), path))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'amiga', '.run', 'ff'))
