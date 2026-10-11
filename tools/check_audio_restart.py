#!/usr/bin/env python3
"""Check actual FS-UAE audio DMA reads across the crash/starter fixture.

Usage: check_audio_restart.py PROFILE GDB_LOG
The profile is the barto debugger's six-frame capture (58-byte DMA records).
This observes DMA addresses, not the game's write-only-register shadows.
"""
import argparse
from pathlib import Path
import re
import struct


def frames(data):
    pos = 0

    def u32():
        nonlocal pos
        value = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        return value

    def skip_blob():
        nonlocal pos
        length = u32()
        pos += length

    count, sections = u32(), u32()
    if count != 6 or sections > 64:
        raise ValueError("expected six-frame barto profile")
    pos += sections * 4 + 16  # loaded sections and four stack bounds
    for _ in range(3):
        skip_blob()  # ROM, Chip RAM, Slow RAM
    pos += 8  # CPU clock and cycle unit
    for _ in range(count):
        skip_blob()  # custom registers
        skip_blob()  # AGA palette
        size, records = u32(), u32()
        if size != 58 or records != 227 * 313:
            raise ValueError("unsupported DMA record layout")
        reads = {2: [], 3: []}
        for index in range(records):
            offset = pos + index * size
            register = struct.unpack_from("<H", data, offset)[0]
            kind = struct.unpack_from("<h", data, offset + 29)[0]
            if kind == 4 and register in (0xca, 0xda):
                address = struct.unpack_from("<I", data, offset + 12)[0]
                reads[(register - 0xaa) // 16].append(address)
        pos += size * records
        size, resources = u32(), u32()
        pos += size * resources + 8  # resources, total and idle CPU cycles
        samples = u32()
        pos += samples * 4
        length = u32()
        pos += 4 + length  # screenshot type and bytes
        if pos > len(data):
            raise ValueError("truncated profile")
        yield reads
    if pos != len(data):
        raise ValueError("unparsed profile bytes")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    log = args.log.read_text()
    match = re.search(r"WAVES white=(0x[0-9a-f]+) periodic=(0x[0-9a-f]+)", log)
    if not match or "CHIP noise=3" not in log:
        raise SystemExit("FAIL: missing waveform addresses or final periodic chip state")
    white, periodic = (int(value, 16) for value in match.groups())
    capture = list(frames(args.profile.read_bytes()))
    failed = False
    for channel in (2, 3):
        before = [p for frame in capture[:2] for p in frame[channel]]
        after = [p for frame in capture[-3:] for p in frame[channel]]
        stale = sum(not periodic <= p < periodic + 30 for p in after)
        exercised = any(white <= p < white + 8190 for p in before)
        print(f"Paula {channel}: crash observed={exercised}, "
              f"post-restart reads={len(after)}, wrong waveform={stale}")
        failed |= not exercised or not after or bool(stale)
    if failed:
        raise SystemExit("FAIL: DMA did not switch to the engine waveform")
    print("PASS: both audible engine channels fetch the periodic waveform after restart")


if __name__ == "__main__":
    main()
