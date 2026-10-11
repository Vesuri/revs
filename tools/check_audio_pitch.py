#!/usr/bin/env python3
"""Compare the actual Paula period conversions with jsbeeb's generated waveforms.

Run from the repository root. Requires cc and node plus the local jsbeeb checkout.
The shared MOS fixtures check chip registers, not the rate Paula plays them at.
"""
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DIVIDERS = [16, 32, 64, 97, 127, 255, 512, 1008, 1024]


def function(source, name):
    start = source.index(f"static uint16_t {name}(")
    pos = source.index("{", start)
    depth = 1
    end = pos + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    source = (ROOT / "src/platform/amiga/RevsAudio.cpp").read_text()
    assert "noise_period_for(snd_noise_divisor(c))" in source
    harness = "#include <stdint.h>\n#include <stdio.h>\n#define kPaulaMinPer 124u\n"
    harness += function(source, "period_for") + "\n" + function(source, "noise_period_for")
    harness += '\nint main(void) { unsigned d; while(scanf("%u", &d)==1) '
    harness += 'printf("%u %u\\n",period_for(d),noise_period_for(d)); return 0; }\n'
    with tempfile.TemporaryDirectory() as directory:
        c = Path(directory) / "pitch.c"
        exe = Path(directory) / "pitch"
        c.write_text(harness)
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", str(c), "-o", str(exe)], check=True)
        periods = subprocess.check_output([str(exe)], input="\n".join(map(str, DIVIDERS)), text=True)
    # Generate actual reference samples; do not restate its clock formula as the oracle.
    script = '''import { SoundChip } from URL;
const result=[];
for (const d of DIVIDERS) {
  const periods=[];
  for (const noise of [false,true]) {
    const chip=new SoundChip(()=>{});
    chip.registers[2]=d; chip.registers[3]=3;
    chip.volume[2]=1; chip.volume[3]=1; chip.noisePoked();
    const out=new Float32Array(240*d);
    if(noise) chip.noiseChannel(3,out,0,out.length);
    else chip.toneChannel(2,out,0,out.length);
    const edges=[];
    for(let i=1;i<out.length;i++) if(out[i]>0 && out[i-1]===0) edges.push(i);
    if(edges.length<3) throw new Error("No repeated waveform");
    const period=edges[2]-edges[1];
    if(edges[1]-edges[0]!==period) throw new Error("Unstable waveform period");
    periods.push(period);
  }
  result.push(periods);
}
console.log(JSON.stringify(result));'''
    script = script.replace("URL", json.dumps((ROOT / "tools/jsbeeb/src/soundchip.js").as_uri()))
    script = script.replace("DIVIDERS", json.dumps(DIVIDERS))
    reference = json.loads(subprocess.check_output(["node", "--input-type=module", "-e", script], text=True))
    failed = False
    for divider, line, ref in zip(DIVIDERS, periods.splitlines(), reference):
        for label, paula_period, samples, ref_samples in zip(
                ("tone", "periodic"), map(int, line.split()), (2, 15), ref):
            hz = 3546895 / (paula_period * samples)
            ref_hz = 500000 / ref_samples
            error = abs(hz / ref_hz - 1)
            print(f"divider={divider:4} {label:8}: Paula={hz:8.3f} BBC={ref_hz:8.3f} Hz error={error:.3%}")
            failed |= error > 0.003  # integer Paula-period rounding, under 0.3% here
    if failed:
        raise SystemExit("FAIL: Paula pitch differs from the BBC waveform")
    print(f"PASS: {len(DIVIDERS)*2} tone/noise pitch comparisons")


if __name__ == "__main__":
    main()
