#!/usr/bin/env python3
"""⭐⭐ Report for amiga/pcsample.gdb: map sampled target PCs to functions and source lines.

The gdb log carries `OFF <runtime address of interp_edge_core>` once and `S <field> <pc>` per
sample; the ELF's own address for interp_edge_core gives the relocation, and every sample is
mapped through `nm` (function) and `addr2line` (source line).  Samples before --from-field are
dropped (boot and front end).

  python3 tools/pcsample_report.py [log] [--from-field=N] [--fn=name --lines]
"""
import re, subprocess, sys, collections, bisect, os, shutil
BIN = os.path.expanduser('~/.local/opt/bin')
tool = lambda n: shutil.which(n) or os.path.join(BIN, n)
args = [a for a in sys.argv[1:] if not a.startswith('--')]
opt = dict(a[2:].split('=', 1) if '=' in a else (a[2:], '1') for a in sys.argv[1:] if a.startswith('--'))
log = args[0] if args else 'amiga/.run/gdb-out.log'
elf = 'amiga/out/Revs.elf'
lines = open(log).read().split('\n')
off_rt = next(int(l.split()[1], 16) for l in lines if l.startswith('OFF '))
# the toolchain ships no nm, so the symbol table comes from objdump -t (function symbols only)
tab = subprocess.run([tool('m68k-amiga-elf-objdump'), '-t', elf], capture_output=True, text=True).stdout
syms = []
for l in tab.split('\n'):
    m = re.match(r'^([0-9a-f]{8})\s.*\sF\s\.text\s+[0-9a-f]+\s(.+)$', l)
    if m: syms.append((int(m.group(1), 16), m.group(2)))
syms.sort()
addrs = [a for a, _ in syms]
link = next(a for a, n in syms if n == 'interp_edge_core')
delta = off_rt - link
frm = int(opt.get('from-field', 0))
samples = []
for l in lines:
    m = re.match(r'S (\d+) ([0-9a-f]+)', l)
    if m and int(m.group(1)) >= frm:
        samples.append(int(m.group(2), 16) - delta)
def fn(pc):
    i = bisect.bisect_right(addrs, pc) - 1
    return syms[i][1] if i >= 0 and pc - addrs[i] < 0x20000 else '(outside program: ROM/OS)'
c = collections.Counter(fn(pc) for pc in samples)
n = len(samples)
print(f'{n} samples (from field {frm})')
for name, k in c.most_common(int(opt.get('top', 40))):
    print(f'  {100*k/n:5.1f}%  {k:5d}  {name}')
if 'fn' in opt:
    want = opt['fn']
    pcs = [pc for pc in samples if fn(pc) == want]
    out = subprocess.run([tool('m68k-amiga-elf-addr2line'), '-e', elf] + [hex(p) for p in pcs],
                         capture_output=True, text=True).stdout.split('\n')
    lc = collections.Counter(re.sub(r'.*/', '', o) for o in out if o)
    print(f'\n{want}: {len(pcs)} samples by source line')
    for ln, k in lc.most_common(int(opt.get('ltop', 40))):
        print(f'  {100*k/max(1,len(pcs)):5.1f}%  {k:4d}  {ln}')
