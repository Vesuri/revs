#!/usr/bin/env python3
"""Report for amiga/steptrace.gdb: map single-stepped PCs to function and source line.

  python3 tools/steptrace_report.py [log] [--top=N] [--elf=PATH]

Counts INSTRUCTIONS (not cycles) per function and per source line over every traced call, and
counts memory-operand instructions (anything addressing mem[], the stack or an absolute) so a
line that is all byte traffic stands out.  Relocation as tools/pcsample_report.py (the OFF line).
"""
import re, subprocess, sys, collections, os, shutil, bisect
BIN = os.path.expanduser('~/.local/opt/bin')
tool = lambda n: shutil.which(n) or os.path.join(BIN, n)
args = [a for a in sys.argv[1:] if not a.startswith('--')]
opt = dict(a[2:].split('=', 1) if '=' in a else (a[2:], '1') for a in sys.argv[1:] if a.startswith('--'))
log = args[0] if args else 'amiga/.run/gdb-out.log'
elf = opt.get('elf', 'amiga/out/Revs.elf')   # --elf= when the trace's binary has been rebuilt since
lines = open(log).read().split('\n')
off_line = next(l.split() for l in lines if l.startswith('OFF '))
off_rt = int(off_line[1], 16)
off_sym = off_line[2] if len(off_line) > 2 else 'interp_edge_core'   # older logs name no symbol
tab = subprocess.run([tool('m68k-amiga-elf-objdump'), '-t', elf], capture_output=True, text=True).stdout
syms = sorted((int(m.group(1), 16), m.group(2)) for m in
              (re.match(r'^([0-9a-f]{8})\s.*\sF\s\.text\s+[0-9a-f]+\s(.+)$', l) for l in tab.split('\n')) if m)
addrs = [a for a, _ in syms]
delta = off_rt - next(a for a, n in syms if n == off_sym)
pcs = [int(l.split()[1], 16) - delta for l in lines if l.startswith('T ')]
calls = [int(l.split()[2]) for l in lines if l.startswith('C ')]
def fn(pc):
    i = bisect.bisect_right(addrs, pc) - 1
    return syms[i][1] if i >= 0 and pc - addrs[i] < 0x20000 else '(ROM/OS/ISR)'
dis = subprocess.run([tool('m68k-amiga-elf-objdump'), '-d', elf], capture_output=True, text=True).stdout
insn = {}
for l in dis.split('\n'):
    m = re.match(r'^\s+([0-9a-f]+):\s+(?:[0-9a-f]{4} )+\s*(.*)$', l)
    if m: insn[int(m.group(1), 16)] = m.group(2).strip()
uniq = sorted(p for p in set(pcs) if p >= 0)   # ROM/ISR PCs below the program would break addr2line
a2l = subprocess.run([tool('m68k-amiga-elf-addr2line'), '-e', elf] + [hex(p) for p in uniq],
                     capture_output=True, text=True).stdout.split('\n')
line_of = collections.defaultdict(lambda: '(ROM/OS)')
line_of.update({p: os.path.basename(a2l[i]) if i < len(a2l) else '?' for i, p in enumerate(uniq)})
n = len(pcs)
print('%d instructions over %d calls (%.0f per call; per call: %s)' % (n, len(calls), n / max(1, len(calls)), calls))
memop = lambda s: bool(re.search(r'\(|<[a-z_]', s.split(' ', 1)[-1])) if s else False
byfn = collections.Counter(fn(p) for p in pcs)
print('\nby function:')
for f, c in byfn.most_common(15): print('  %6.1f%% %6d  %s' % (100 * c / n, c, f))
byline = collections.Counter(line_of[p] for p in pcs)
mem_line = collections.Counter(line_of[p] for p in pcs if memop(insn.get(p, '')))
top = int(opt.get('top', 40))
print('\nby source line (instructions, of which memory-operand):')
for ln, c in byline.most_common(top): print('  %6.1f%% %6d  mem %5d  %s' % (100 * c / n, c, mem_line[ln], ln))
print('\nmemory-operand instructions: %d of %d (%.0f%%)' % (sum(mem_line.values()), n, 100 * sum(mem_line.values()) / n))
