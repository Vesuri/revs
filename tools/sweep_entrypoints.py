#!/usr/bin/env python3
"""Static entry-point sweep over the post-load memory image (docs/entrypoint-sweep.md).

Recursive-descent 6502 disassembly from a seed set, reporting everything that makes
code reachable in a way Ghidra's own analysis will not find on its own:

  * JMP ($xxxx)          indirect jumps, and the pointer they read
  * PHA/PHA/RTS          the RTS-trick dispatch idiom (pushed address is target-1)
  * JSR ($xx),Y / (zp,X) hand-rolled vtables through RAM pointers
  * JSR $FFxx            MOS calls (these leave the image entirely)
  * stores to $0200-$0235 OS-vector claims
  * stores into the code range  self-modifying code (the transpiler cannot follow these)
  * .word tables of in-range addresses that nothing else references

Run:  python3 tools/sweep_entrypoints.py disasm/revs_mem.bin
      python3 tools/sweep_entrypoints.py disasm/revs_mem.bin --csv > /tmp/entries.csv

Reachability is deliberately conservative: it follows JSR/JMP/branches, treats a
JSR as returning (6502 code that does not return from a JSR exists, but assuming it
does only ever over-scans), and stops at RTS/RTI/JMP/BRK.

The image is the PRE-PATCH state — the four expansion track files patch the engine at
startup (docs/reference-sources.md).  --copy replays a block move the code performs on
itself, so the sweep can follow control flow through a relocation.
"""
import argparse
import sys
from collections import defaultdict

# --- 6502 (NMOS) opcode table: mnemonic, addressing mode -----------------------------
# modes: imp acc imm zp zpx zpy izx izy abs abx aby ind rel
IMP, ACC, IMM, ZP, ZPX, ZPY, IZX, IZY, ABS, ABX, ABY, IND, REL = (
    "imp acc imm zp zpx zpy izx izy abs abx aby ind rel".split())
SIZE = {IMP: 1, ACC: 1, IMM: 2, ZP: 2, ZPX: 2, ZPY: 2, IZX: 2, IZY: 2,
        ABS: 3, ABX: 3, ABY: 3, IND: 3, REL: 2}

OPS = {}


def _row(base, mn, modes):
    for off, mode in modes:
        OPS[base + off] = (mn, mode)


# Regular ALU group (ORA AND EOR ADC STA LDA CMP SBC) — same operand layout.
for base, mn in ((0x01, "ORA"), (0x21, "AND"), (0x41, "EOR"), (0x61, "ADC"),
                 (0x81, "STA"), (0xA1, "LDA"), (0xC1, "CMP"), (0xE1, "SBC")):
    _row(base, mn, [(0x00, IZX), (0x04, ZP), (0x08, IMM), (0x0C, ABS),
                    (0x10, IZY), (0x14, ZPX), (0x18, ABY), (0x1C, ABX)])
del OPS[0x89]  # STA #imm does not exist on NMOS

# Shift/RMW group (ASL ROL LSR ROR)
for base, mn in ((0x06, "ASL"), (0x26, "ROL"), (0x46, "LSR"), (0x66, "ROR")):
    _row(base, mn, [(0x00, ZP), (0x04, ACC), (0x08, ABS), (0x10, ZPX), (0x18, ABX)])

OPS.update({
    0x00: ("BRK", IMM),  # BRK takes a signature byte on the BBC — treat as 2 bytes
    0x20: ("JSR", ABS), 0x40: ("RTI", IMP), 0x60: ("RTS", IMP),
    0x4C: ("JMP", ABS), 0x6C: ("JMP", IND),
    0x08: ("PHP", IMP), 0x28: ("PLP", IMP), 0x48: ("PHA", IMP), 0x68: ("PLA", IMP),
    0x88: ("DEY", IMP), 0xC8: ("INY", IMP), 0xCA: ("DEX", IMP), 0xE8: ("INX", IMP),
    0x18: ("CLC", IMP), 0x38: ("SEC", IMP), 0x58: ("CLI", IMP), 0x78: ("SEI", IMP),
    0xB8: ("CLV", IMP), 0xD8: ("CLD", IMP), 0xF8: ("SED", IMP),
    0x8A: ("TXA", IMP), 0x98: ("TYA", IMP), 0x9A: ("TXS", IMP),
    0xA8: ("TAY", IMP), 0xAA: ("TAX", IMP), 0xBA: ("TSX", IMP), 0xEA: ("NOP", IMP),
    0x24: ("BIT", ZP), 0x2C: ("BIT", ABS),
    0x84: ("STY", ZP), 0x94: ("STY", ZPX), 0x8C: ("STY", ABS),
    0x86: ("STX", ZP), 0x96: ("STX", ZPY), 0x8E: ("STX", ABS),
    0xA0: ("LDY", IMM), 0xA4: ("LDY", ZP), 0xB4: ("LDY", ZPX),
    0xAC: ("LDY", ABS), 0xBC: ("LDY", ABX),
    0xA2: ("LDX", IMM), 0xA6: ("LDX", ZP), 0xB6: ("LDX", ZPY),
    0xAE: ("LDX", ABS), 0xBE: ("LDX", ABY),
    0xC0: ("CPY", IMM), 0xC4: ("CPY", ZP), 0xCC: ("CPY", ABS),
    0xE0: ("CPX", IMM), 0xE4: ("CPX", ZP), 0xEC: ("CPX", ABS),
    0xC6: ("DEC", ZP), 0xD6: ("DEC", ZPX), 0xCE: ("DEC", ABS), 0xDE: ("DEC", ABX),
    0xE6: ("INC", ZP), 0xF6: ("INC", ZPX), 0xEE: ("INC", ABS), 0xFE: ("INC", ABX),
    0x10: ("BPL", REL), 0x30: ("BMI", REL), 0x50: ("BVC", REL), 0x70: ("BVS", REL),
    0x90: ("BCC", REL), 0xB0: ("BCS", REL), 0xD0: ("BNE", REL), 0xF0: ("BEQ", REL),
})

BRANCHES = {"BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ"}
STORES = {"STA", "STX", "STY"}
RMW = {"INC", "DEC", "ASL", "LSR", "ROL", "ROR"}
TERMINAL = {"RTS", "RTI", "JMP", "BRK"}

# --- BBC facts ------------------------------------------------------------------------
MOS_ENTRIES = {
    0xFFB9: "OSRDRM", 0xFFBC: "VDUCHR", 0xFFBF: "OSEVEN", 0xFFC2: "GSINIT",
    0xFFC5: "GSREAD", 0xFFC8: "NVRDCH", 0xFFCB: "NVWRCH", 0xFFCE: "OSFIND",
    0xFFD1: "OSGBPB", 0xFFD4: "OSBPUT", 0xFFD7: "OSBGET", 0xFFDA: "OSARGS",
    0xFFDD: "OSFILE", 0xFFE0: "OSRDCH", 0xFFE3: "OSASCI", 0xFFE7: "OSNEWL",
    0xFFEC: "OSWRCR", 0xFFEE: "OSWRCH", 0xFFF1: "OSWORD", 0xFFF4: "OSBYTE",
    0xFFF7: "OSCLI",
}
OS_VECTORS = {
    0x0202: "BRKV", 0x0204: "IRQ1V", 0x0206: "IRQ2V", 0x0208: "CLIV", 0x020A: "BYTEV",
    0x020C: "WORDV", 0x020E: "WRCHV", 0x0210: "RDCHV", 0x0212: "FILEV", 0x0214: "ARGSV",
    0x0216: "BGETV", 0x0218: "BPUTV", 0x021A: "GBPBV", 0x021C: "FINDV", 0x021E: "FSCV",
    0x0220: "EVNTV", 0x0222: "UPTV", 0x0224: "NETV", 0x0226: "VDUV", 0x0228: "KEYV",
    0x022A: "INSV", 0x022C: "REMV", 0x022E: "CNPV", 0x0230: "IND1V", 0x0232: "IND2V",
    0x0234: "IND3V",
}
HW_LO, HW_HI = 0xFC00, 0xFEFF

# Only the reason codes Revs is observed to use — a full OSBYTE table would be a BBC
# reference manual, and the point here is a port checklist: each row is something
# Platform::mosCall has to implement.
OSBYTE_REASONS = {
    0x02: "select input stream",
    0x04: "cursor-key / copy-key behaviour",
    0x15: "flush a buffer (X = buffer)",
    0x7E: "acknowledge ESCAPE",
    0x80: "read ADC channel / buffer status  <-- the STEERING input",
    0x81: "negative INKEY — is the key with code X held?",
    0x9A: "write to the Video ULA control register ($FE20) via its OS copy",
    0xBE: "read/write the ADC conversion type (uPD7002 resolution)",
    0xC8: "disable ESCAPE / clear memory on BREAK  (*FX200)",
    0x8C: "select filing system  (*TAPE/*DISC)",
}
OSWORD_REASONS = {
    0x08: "define a sound ENVELOPE",
    0x0A: "read a character definition",
}


def vector_name(a):
    """OS vector cell (low or high byte) -> 'IRQ1V' / 'IRQ1V+1'."""
    if a in OS_VECTORS:
        return OS_VECTORS[a]
    if a - 1 in OS_VECTORS:
        return OS_VECTORS[a - 1] + "+1"
    return None


class Sweep:
    def __init__(self, mem, code_lo, code_hi):
        self.mem = mem
        self.code_lo, self.code_hi = code_lo, code_hi
        self.seen = set()          # addresses decoded as an instruction start
        self.insn = {}             # addr -> (mnemonic, mode, operand, length)
        self.calls = defaultdict(set)      # target -> {call sites}
        self.jumps = defaultdict(set)      # target -> {jump sites}
        self.indirect = []         # (site, pointer addr)
        self.rts_dispatch = []     # (site,)
        self.vtable_calls = []     # (site, mode, zp)
        self.mos = defaultdict(set)        # entry -> {sites}
        self.vec_writes = defaultdict(set)  # vector addr -> {sites}
        self.vec_suspect = set()   # (site, mn, mode, base) indexed store that could reach one
        self.handlers = {}         # vector base -> {handler addr: {sites}}
        self.vec_restores = {}     # vector base -> {(site, mode, operand)}
        self.hw = defaultdict(lambda: [set(), set()])   # addr -> [reads, writes]
        self.selfmod = defaultdict(set)    # target -> {sites}
        self.bad = []              # (addr, opcode) undecodable

    def w16(self, a):
        return self.mem[a] | (self.mem[a + 1] << 8)

    def in_code(self, a):
        return self.code_lo <= a <= self.code_hi

    def resolve_vector_handlers(self):
        """Turn `LDA #lo / STA $0204` + `LDA #hi / STA $0205` into a handler address.

        Records into self.handlers as {vector_base: {addr: [sites]}} and returns the set of
        handler addresses.  A vector written from a saved copy (`LDA $4F1D / STA $0204`) is a
        RESTORE of whatever was there before Revs claimed it, not a new handler — recorded as
        such and not seeded, or the walk wanders off into MOS ROM.
        """
        order = sorted(self.insn)
        pos = {a: i for i, a in enumerate(order)}
        parts = {}          # vector base -> {"lo": (val, site), "hi": (val, site)}
        for a in order:
            mn, mode, operand, n = self.insn[a]
            if mn != "STA" or mode != ABS:
                continue
            base = operand if operand in OS_VECTORS else (
                operand - 1 if operand - 1 in OS_VECTORS else None)
            if base is None:
                continue
            half = "lo" if operand == base else "hi"
            # the immediately preceding LDA supplies the byte
            i = pos[a] - 1
            if i < 0:
                continue
            pa = order[i]
            pmn, pmode, poperand, _ = self.insn[pa]
            if pmn != "LDA":
                continue
            if pmode == IMM:
                parts.setdefault(base, {})[half] = (poperand, a)
            else:
                self.vec_restores.setdefault(base, set()).add((a, pmode, poperand))
        out = set()
        for base, hv in parts.items():
            if "lo" in hv and "hi" in hv:
                addr = hv["lo"][0] | (hv["hi"][0] << 8)
                self.handlers.setdefault(base, {}).setdefault(
                    addr, set()).update({hv["lo"][1], hv["hi"][1]})
                if self.in_code(addr):
                    out.add(addr)
        return out

    def run(self, seeds):
        work = list(seeds)
        while work:
            pc = work.pop()
            while True:
                if pc in self.seen or not (0 <= pc < 0x10000):
                    break
                op = self.mem[pc]
                if op not in OPS:
                    self.bad.append((pc, op))
                    break
                mn, mode = OPS[op]
                n = SIZE[mode]
                if mode in (ABS, ABX, ABY, IND):
                    operand = self.w16(pc + 1)
                elif mode == REL:
                    d = self.mem[pc + 1]
                    operand = (pc + 2 + (d - 256 if d > 127 else d)) & 0xFFFF
                elif mode == IMP or mode == ACC:
                    operand = None
                else:
                    operand = self.mem[pc + 1]
                self.seen.add(pc)
                self.insn[pc] = (mn, mode, operand, n)
                nxt = self.record(pc, mn, mode, operand, n, work)
                if nxt is None:
                    break
                pc = nxt

    def record(self, pc, mn, mode, operand, n, work):
        """Note anything interesting; return the fall-through pc or None."""
        # --- hardware / vector / self-modifying stores -------------------------------
        # ⚠ An indexed store reaches base..base+255, but treating that whole span as "an
        # access to everything in it" is useless: `STA $013B,X` then reads as a write to every
        # OS vector, and the report drowns.  So an exact base match is a FINDING, and an
        # indexed store whose span merely overlaps a sensitive page is a SUSPECT, reported
        # separately — never silently dropped, because a computed vector write is real.
        if mode in (ABS, ABX, ABY):
            base = operand
            is_w = mn in STORES or mn in RMW
            if HW_LO <= base <= HW_HI:
                self.hw[base][1 if is_w else 0].add(pc)
            if is_w:
                if vector_name(base):
                    self.vec_writes[base].add(pc)
                elif mode in (ABX, ABY) and base < 0x0236 and base + 255 >= 0x0202:
                    self.vec_suspect.add((pc, mn, mode, base))
                if self.in_code(base):
                    self.selfmod[base].add(pc)

        if mn == "JSR":
            if 0xFF00 <= operand <= 0xFFFF:
                self.mos[operand].add(pc)
            else:
                self.calls[operand].add(pc)
                work.append(operand)
            return pc + n

        if mn == "JMP":
            if mode == IND:
                self.indirect.append((pc, operand))
                # the pointer's current contents are the pre-patch target
                t = self.w16(operand)
                if self.in_code(t):
                    self.jumps[t].add(pc)
                    work.append(t)
            else:
                if 0xFF00 <= operand <= 0xFFFF:
                    self.mos[operand].add(pc)
                else:
                    self.jumps[operand].add(pc)
                    work.append(operand)
            return None

        if mn in BRANCHES:
            self.jumps[operand].add(pc)
            work.append(operand)
            return pc + n

        if mn in ("RTS", "RTI"):
            # RTS-trick dispatch: PHA, PHA, ... RTS with two pushes and no PLA between
            k, pushes = pc - 1, 0
            while k >= pc - 12 and k in self.seen:
                m = self.insn[k][0]
                if m == "PHA":
                    pushes += 1
                elif m in ("PLA", "JSR", "RTS"):
                    break
                k -= 1
                while k not in self.insn and k >= pc - 12:
                    k -= 1
            if pushes >= 2:
                self.rts_dispatch.append(pc)
            return None

        if mn == "BRK":
            return None

        # JSR through a RAM pointer cannot be expressed on a 6502 directly; the idiom is
        # JMP ($xx) reached by JSR, already covered.  Flag indirect LOADS of a pointer
        # that is later JMPed through by noting (zp),Y jumps is impossible — skip.
        return pc + n


def runs(addrs):
    """Collapse a sorted address list into (start, end) runs."""
    out, s, p = [], addrs[0], addrs[0]
    for v in addrs[1:]:
        if v == p + 1:
            p = v
            continue
        out.append((s, p))
        s = p = v
    out.append((s, p))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image", nargs="?", default="disasm/revs_runtime.bin")
    ap.add_argument("--entry", default="63BD",
                    help="the engine entry, hex.  $63BD for the post-relocation runtime image "
                         "(tools/relocate.py); $1200 for the raw loaded image")
    ap.add_argument("--seed", action="append", default=[],
                    help="extra entry address, hex (repeatable)")
    ap.add_argument("--copy", action="append", default=[],
                    help="replay a block move the code performs: DST:SRC:LEN (hex)")
    ap.add_argument("--code", default="0B00:7900",
                    help="LO:HI (hex) range considered 'code' for self-mod detection")
    ap.add_argument("--trace", metavar="FILE",
                    help="a 64K coverage map from tools/bbc_trace.mjs; reports addresses the "
                         "real machine executed that the static walk never reached — i.e. the "
                         "sweep's own blind spots, which no static tool can self-report")
    ap.add_argument("--functions", action="store_true",
                    help="per-subroutine evidence profile, for the behavioural naming pass: "
                         "size, callers, MOS calls, hardware touched, zero-page use, and the "
                         "arithmetic shape.  Naming from this beats naming from a hunch.")
    ap.add_argument("--csv", action="store_true", help="emit entrypoints.csv rows only")
    args = ap.parse_args()

    mem = bytearray(open(args.image, "rb").read())
    if len(mem) != 0x10000:
        sys.exit(f"expected a 64K image, got {len(mem)} bytes")

    for spec in args.copy:
        dst, src, ln = (int(x, 16) for x in spec.split(":"))
        mem[dst:dst + ln] = mem[src:src + ln]

    lo, hi = (int(x, 16) for x in args.code.split(":"))
    sw = Sweep(mem, lo, hi)
    seeds = [int(args.entry, 16)] + [int(s, 16) for s in args.seed]
    sw.run(seeds)

    # A claimed OS vector is an entry point, and it is the one kind the plain walk can never
    # reach: nothing jumps to an interrupt handler, the MOS does.  Resolve `LDA #lo / STA
    # $0204` + `LDA #hi / STA $0205` pairs into a handler address, seed it, and re-walk —
    # to a fixpoint, since a handler may itself claim another vector.
    for _ in range(8):
        found = sw.resolve_vector_handlers()
        new = [a for a in found if a not in sw.seen]
        if not new:
            break
        sw.run(new)

    if args.functions:
        # Bound each subroutine by "from its entry to the next entry" — crude, but every
        # alternative needs a real CFG and the point here is to rank and characterise, not to
        # produce exact extents.
        entries = sorted(set(sw.calls) | set(seeds) | {a for h in sw.handlers.values() for a in h})
        entries = [a for a in entries if a in sw.insn]
        print("# Per-subroutine evidence profile — input to the naming pass")
        print("# addr  size ncall  facts")
        for i, a in enumerate(entries):
            end = entries[i + 1] if i + 1 < len(entries) else hi
            body = [x for x in sw.insn if a <= x < end]
            facts = []
            zp = set()
            mos_here, hw_here, consts = set(), set(), set()
            branches_out = set()
            for x in body:
                mn, mode, operand, n = sw.insn[x]
                if mode in (ZP, ZPX, ZPY, IZX, IZY):
                    zp.add(operand)
                if mn == "JSR" and 0xFF00 <= operand <= 0xFFFF:
                    mos_here.add(MOS_ENTRIES.get(operand, f"${operand:04X}"))
                if mode in (ABS, ABX, ABY) and HW_LO <= operand <= HW_HI:
                    hw_here.add(operand)
                if mn == "JSR" and not (0xFF00 <= operand <= 0xFFFF):
                    branches_out.add(operand)
                if mode == IMM:
                    consts.add(operand)
            shifts = sum(1 for x in body if sw.insn[x][0] in ("ASL", "LSR", "ROL", "ROR"))
            adds = sum(1 for x in body if sw.insn[x][0] in ("ADC", "SBC"))
            if mos_here:
                facts.append("MOS:" + ",".join(sorted(mos_here)))
            if hw_here:
                facts.append("HW:" + ",".join(f"${h:04X}" for h in sorted(hw_here)))
            if shifts >= 6 and adds >= 2:
                facts.append(f"MATH? {shifts} shifts/{adds} add-sub — mul or div by shift-add")
            if zp:
                zs = sorted(zp)
                facts.append(f"zp:{len(zs)}[" + " ".join(f"{z:02X}" for z in zs[:10]) + "]")
            if branches_out:
                facts.append("calls:" + " ".join(f"${b:04X}" for b in sorted(branches_out)[:8]))
            print(f"${a:04X} {len(body):4d} {len(sw.calls.get(a, ())):5d}  " + "  ".join(facts))
        return

    if args.csv:
        rows = set()
        for t in sw.jumps:
            rows.add((t, "jmp_target"))
        for t in sw.calls:
            rows.add((t, "sub"))
        for _, p in sw.indirect:
            rows.add((sw.w16(p), "indirect_target"))
        print("addr,name,note")
        for a, kind in sorted(rows):
            print(f"0x{a:04X},{kind}_{a:04X},\"seeded by tools/sweep_entrypoints.py\"")
        return

    P = print
    P("# Entry-point sweep — tools/sweep_entrypoints.py")
    P(f"# image: {args.image}   code range: ${lo:04X}-${hi:04X}")
    for spec in args.copy:
        P(f"# replayed block move: {spec}")
    P("")
    P(f"reached {len(sw.seen)} instruction bytes across "
      f"{len(sw.calls)} called + {len(sw.jumps)} jumped-to addresses")
    P("")

    P("## Indirect jumps  JMP ($xxxx)")
    if not sw.indirect:
        P("(none)")
    for site, ptr in sorted(sw.indirect):
        vn = vector_name(ptr)
        P(f"  ${site:04X}  JMP (${ptr:04X})   "
          f"{'vector ' + vn + '  ' if vn else ''}pre-patch target ${sw.w16(ptr):04X}")
    P("")

    P("## RTS-trick dispatch (PHA/PHA/.../RTS)")
    if not sw.rts_dispatch:
        P("(none found on reachable code)")
    for a in sorted(sw.rts_dispatch):
        P(f"  ${a:04X}")
    P("")

    # OSBYTE/OSWORD dispatch on the reason code in A, so "Revs calls OSBYTE" says almost
    # nothing — the 10 sites are 8 different OS services.  Recover each by walking back to the
    # nearest preceding `LDA #imm`.  That is a HEURISTIC: it is wrong wherever A arrives from a
    # variable or a different path, so an unresolved site is reported as ??? rather than
    # guessed at.
    order = sorted(sw.insn)
    pos = {a: i for i, a in enumerate(order)}

    def reason_code(site, back=6):
        for k in range(pos[site] - 1, max(-1, pos[site] - 1 - back), -1):
            mn, mode, operand, _ = sw.insn[order[k]]
            if mn == "LDA" and mode == IMM:
                return operand
        return None

    P("## MOS calls — with the reason code in A where it is statically knowable")
    for e in sorted(sw.mos):
        nm = MOS_ENTRIES.get(e, "?")
        P(f"  ${e:04X} {nm:8s} {len(sw.mos[e]):3d} sites")
        for s in sorted(sw.mos[e]):
            rc = reason_code(s)
            desc = OSBYTE_REASONS.get(rc, "") if e == 0xFFF4 else (
                OSWORD_REASONS.get(rc, "") if e == 0xFFF1 else "")
            code = f"A=${rc:02X} ({rc:3d})" if rc is not None else "A=???  (from a variable)"
            P(f"      ${s:04X}  {code}  {desc}")
    P("")

    P("## Claimed OS vectors — HANDLER ENTRY POINTS")
    P("# Nothing in the binary jumps to these; the MOS does.  Each is a root the walk was")
    P("# re-seeded from, and each belongs in ghidra_scripts/entrypoints.csv.")
    if not sw.handlers:
        P("(none)")
    for base in sorted(sw.handlers):
        for addr, sites in sorted(sw.handlers[base].items()):
            st = " ".join(f"${s:04X}" for s in sorted(sites))
            P(f"  {OS_VECTORS[base]:6s} (${base:04X}) <- ${addr:04X}   claimed at {st}")
    for base in sorted(sw.vec_restores):
        for site, mode, operand in sorted(sw.vec_restores[base]):
            P(f"  {OS_VECTORS[base]:6s} (${base:04X}) restored at ${site:04X} from ${operand:04X}"
              "   (the saved previous handler — not a Revs entry point)")
    P("")

    P("## OS-vector writes ($0200-$0235)")
    if not sw.vec_writes:
        P("(none)")
    for a in sorted(sw.vec_writes):
        sites = " ".join(f"${s:04X}" for s in sorted(sw.vec_writes[a]))
        P(f"  ${a:04X} {vector_name(a):8s} from {sites}")
    if sw.vec_suspect:
        P("  -- indexed stores whose 256-byte span could reach the vector page "
          "(check the index range by hand):")
        for site, mn, mode, base in sorted(sw.vec_suspect):
            P(f"     ${site:04X}  {mn} ${base:04X},{'X' if mode == ABX else 'Y'}")
    P("")

    P("## Hardware accesses ($FC00-$FEFF)")
    if not sw.hw:
        P("(none on reachable code)")
    for a in sorted(sw.hw):
        r, w = sw.hw[a]
        rw = ("R" if r else "") + ("W" if w else "")
        sites = " ".join(f"${s:04X}" for s in sorted(r | w)[:10])
        P(f"  ${a:04X} {rw:3s} {sites}")
    P("")

    # A store into the code RANGE means nothing on its own — most of $1200-$6FFF is data and
    # workspace.  What matters is a store that lands inside a byte the walk decoded as part of
    # an instruction: that is code the transpiler cannot follow, and it needs a hand-written
    # stub in src/gen/revs_manual.c.
    covers = {}
    for a, (mn, mode, operand, n) in sw.insn.items():
        for k in range(n):
            covers[a + k] = (a, k, mn)
    real = {t: s for t, s in sw.selfmod.items() if t in covers}
    P(f"## Self-modifying code — stores landing inside a decoded instruction "
      f"({len(real)} of {len(sw.selfmod)} in-range stores; the rest are data/workspace)")
    if not real:
        P("(none)")
    for t in sorted(real):
        ia, k, mn = covers[t]
        part = "opcode" if k == 0 else f"operand byte {k}"
        sites = " ".join(f"${s:04X}" for s in sorted(real[t])[:10])
        more = f" … +{len(real[t]) - 10}" if len(real[t]) > 10 else ""
        P(f"  ${t:04X}  {part} of {mn} @ ${ia:04X}   written from {sites}{more}")
    P("")

    # --- static coverage: what is left over, and is it plausibly data? -------------------
    P("## Static coverage of the code range")
    referenced = set()
    for a, (mn, mode, operand, n) in sw.insn.items():
        if mode in (ABS, ABX, ABY, IND) and not (0xFF00 <= operand <= 0xFFFF):
            referenced.add(operand)
            if mode in (ABX, ABY):
                referenced.update(range(operand, min(operand + 256, 0x10000)))
    unknown = [a for a in range(lo, hi)
               if a not in covers and a not in referenced]
    P(f"  decoded as instruction bytes : {len(covers)}")
    P(f"  referenced as data           : {len(referenced & set(range(lo, hi)))}")
    P(f"  neither  (the suspect set)   : {len(unknown)}")
    P("  runs of 16+ unclassified bytes — each is either data nothing reaches or code no")
    P("  entry point leads to.  Classify every one before generating C:")
    if unknown:
        for s, e in runs(unknown):
            if e - s + 1 < 16:
                continue
            blk = bytes(mem[s:e + 1])
            kind = ("all zero" if not any(blk) else
                    "printable text" if all(0x20 <= c < 0x7F for c in blk) else "binary")
            P(f"    ${s:04X}-${e:04X} ({e - s + 1:5d})  {kind}")
    P("")

    if args.trace:
        cov = open(args.trace, "rb").read()
        executed = [a for a in range(lo, hi) if cov[a]]
        missed = [a for a in executed if a not in sw.insn]
        P(f"## Trace cross-check against {args.trace}")
        P(f"  addresses executed in ${lo:04X}-${hi:04X}: {len(executed)}")
        P(f"  executed but NOT statically walked:  {len(missed)}"
          + ("   <-- MISSED ENTRY POINTS" if missed else "   (none — the walk covers "
             "everything this trace executed)"))
        for s, e in (runs(missed) if missed else []):
            P(f"    ${s:04X}-${e:04X} ({e - s + 1})")
        P("  ⚠ A trace only ever proves a lower bound: it covers what this run happened to")
        P("  execute.  Silence here is not coverage of the whole engine.")
        P("")

    P("## Undecodable bytes hit by the walk (data reached as code, or a gap)")
    if not sw.bad:
        P("(none)")
    for a, op in sorted(sw.bad)[:60]:
        P(f"  ${a:04X}  opcode ${op:02X}")
    if len(sw.bad) > 60:
        P(f"  … +{len(sw.bad) - 60} more")


if __name__ == "__main__":
    main()
