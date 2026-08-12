#!/usr/bin/env python3
"""Acorn DFS disc-image (.ssd/.dsd) catalogue dumper — the Revs analogue of RoF's xex_map.py.

The BBC has no single load file: Revs is a set of DFS files (loader + code + per-track
data), so the "segment map" of this port is the disc catalogue.  Print it before
reasoning about what loads where.

    python3 tools/ssd_map.py revs.ssd

DFS catalogue format (2 sectors, 256 bytes each, at the start of side 0):
  sector 0  0x000  8 bytes   disc title, chars 1-8
            0x008  31 x 8    per-file: 7 chars name + 1 char directory (bit7 = locked)
  sector 1  0x100  4 bytes   disc title, chars 9-12
            0x104  1 byte    cycle number (BCD)
            0x105  1 byte    number of catalogue entries * 8
            0x106  1 byte    b4-5 boot option (*OPT 4,n), b0-1 total-sectors high
            0x107  1 byte    total sectors low
            0x108  31 x 8    per-file: load lo/hi, exec lo/hi, length lo/hi,
                             packed high bits, start-sector low
"""
import sys

BOOT_OPT = {0: "none", 1: "*LOAD", 2: "*RUN", 3: "*EXEC"}


def read_catalogue(img: bytes, side_offset: int = 0):
    s0 = img[side_offset : side_offset + 256]
    s1 = img[side_offset + 256 : side_offset + 512]
    title = (s0[0:8] + s1[0:4]).decode("latin-1").rstrip(" \0")
    count = s1[5] // 8
    total_sectors = ((s1[6] & 0x03) << 8) | s1[7]
    files = []
    for i in range(count):
        n = s0[8 + i * 8 : 16 + i * 8]
        name = n[0:7].decode("latin-1").rstrip()
        directory = chr(n[7] & 0x7F)
        locked = bool(n[7] & 0x80)
        e = s1[8 + i * 8 : 16 + i * 8]
        packed = e[6]
        load = e[0] | (e[1] << 8) | ((packed & 0x0C) << 14)
        exec_ = e[2] | (e[3] << 8) | ((packed & 0xC0) << 10)
        length = e[4] | (e[5] << 8) | ((packed & 0x30) << 12)
        start = e[7] | ((packed & 0x03) << 8)
        files.append(
            dict(name=name, dir=directory, locked=locked, load=load,
                 exec_=exec_, length=length, start=start)
        )
    return dict(title=title, cycle=s1[4], boot=(s1[6] >> 4) & 3,
                total_sectors=total_sectors, files=files)


def main(argv):
    path = argv[1] if len(argv) > 1 else "revs.ssd"
    img = open(path, "rb").read()
    print(f"{path}: {len(img)} bytes = {len(img)//256} sectors "
          f"({len(img)//2560} tracks x 10 x 256)")
    cat = read_catalogue(img)
    print(f'title "{cat["title"]}"  cycle {cat["cycle"]:02X}  '
          f'boot *OPT 4,{cat["boot"]} ({BOOT_OPT[cat["boot"]]})  '
          f'used {cat["total_sectors"]} sectors')
    print()
    print(f"{'file':<12} {'load':>7} {'exec':>7} {'length':>7} "
          f"{'sector':>6} {'offset':>7}  ends")
    for f in cat["files"]:
        name = f"{f['dir']}.{f['name']}" + ("*" if f["locked"] else "")
        off = f["start"] * 256
        print(f"{name:<12} {f['load']:>7X} {f['exec_']:>7X} {f['length']:>7X} "
              f"{f['start']:>6} {off:>7X}  {f['load'] + f['length']:X}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
