#!/usr/bin/env python3
"""Locate YMV.BIN (likely movie subsystem) and scan for frame-total tables."""
from __future__ import annotations

import re
import struct
from pathlib import Path

BIN = Path("fm3/fm3.bin")
SECTOR = 2352
USER = 2048

# Verified frame counts (movie id -> frames), MOV10 absent, MOV19 fixed
FRAMES = {
    0: 604, 1: 1564, 2: 864, 3: 454, 4: 229, 5: 1144, 6: 836, 7: 944,
    8: 93, 9: 184, 11: 147, 12: 169, 13: 211, 14: 267, 15: 289, 16: 585,
    17: 364, 18: 844, 19: 454, 20: 276, 21: 634, 22: 424, 23: 169,
    24: 715, 25: 454, 26: 1399,
}


def find_file(iso: bytes, name: bytes) -> tuple[int, int] | None:
    """Return (lba, size) for ISO file NAME;1"""
    pat = re.escape(name) + rb";1"
    for m in re.finditer(pat, iso):
        for back in range(80):
            rs = m.start() - 33 - back
            if rs < 0:
                continue
            nlen = iso[rs + 32]
            if nlen and iso[rs + 33 : rs + 33 + nlen].startswith(name[:nlen]):
                lba = struct.unpack_from("<I", iso, rs + 2)[0]
                size = struct.unpack_from("<I", iso, rs + 10)[0]
                return lba, size
    return None


def main() -> int:
    raw = BIN.read_bytes()
    iso = bytearray()
    for lba in range(25000):
        iso += raw[lba * SECTOR + 24 : lba * SECTOR + 24 + USER]
    iso = bytes(iso)

    for name in (b"YMV.BIN", b"OPENIMG.BIN", b"SDATA.BIN", b"TDATA.BIN"):
        loc = find_file(iso, name)
        print(name.decode(), loc)

    loc = find_file(iso, b"YMV.BIN")
    if not loc:
        print("YMV.BIN not found")
        return 1
    lba, size = loc
    print(f"Reading YMV.BIN lba={lba} size={size}")
    blob = bytearray()
    nsec = (size + USER - 1) // USER
    for i in range(nsec):
        blob += raw[(lba + i) * SECTOR + 24 : (lba + i) * SECTOR + 24 + USER]
    blob = bytes(blob[:size])
    Path("build/YMV.BIN").write_bytes(blob)
    print(f"wrote build/YMV.BIN ({len(blob)} bytes)")

    # strings
    for pat in [rb"MOV", rb"STR", rb"\\MV", rb"MV\\", rb"%02d", rb"MDEC", rb"movie"]:
        ms = list(re.finditer(pat, blob, re.I))
        print(pat, "count", len(ms))
        for m in ms[:8]:
            lo = max(0, m.start() - 16)
            hi = min(len(blob), m.end() + 32)
            ctx = "".join(chr(c) if 32 <= c < 127 else "." for c in blob[lo:hi])
            print(" ", hex(m.start()), ctx)

    # table search
    seq = [FRAMES[i] for i in range(0, 10)]  # 0-9
    needle = struct.pack("<" + "H" * len(seq), *seq)
    print("search frames 0-9", seq, "hits", blob.count(needle))
    j = blob.find(needle)
    if j >= 0:
        vals = list(struct.unpack_from("<" + "H" * 32, blob, j))
        print(" HIT", hex(j), vals)

    # full file order without id10
    file_order = [FRAMES[i] for i in sorted(FRAMES)]
    needle = struct.pack("<" + "H" * 8, *file_order[:8])
    print("search first8 file order", file_order[:8], "count", blob.count(needle))
    j = blob.find(needle)
    if j >= 0:
        print(" HIT", hex(j), list(struct.unpack_from("<" + "H" * 30, blob, j)))

    # MDEC MMIO
    hits = 0
    for i in range(0, len(blob) - 8, 4):
        w = struct.unpack_from("<I", blob, i)[0]
        if (w & 0xFFE00000) == 0x3C000000 and (w & 0xFFFF) == 0x1F80:
            rt = (w >> 16) & 0x1F
            for k in range(1, 6):
                w2 = struct.unpack_from("<I", blob, i + k * 4)[0]
                op = w2 >> 26
                rs = (w2 >> 21) & 0x1F
                if rs != rt:
                    continue
                if op == 0x0D:
                    addr = 0x1F800000 | (w2 & 0xFFFF)
                elif op == 0x09:
                    low = w2 & 0xFFFF
                    if low >= 0x8000:
                        low -= 0x10000
                    addr = 0x1F800000 + low
                else:
                    continue
                if 0x1F801800 <= addr <= 0x1F80182C:
                    print(f" MDEC ref blob+{i:#x} -> {hex(addr)}")
                    hits += 1
    print("mdec hits", hits)

    # any u16 table of 26 values in 50..2000 range with matching set
    want = set(FRAMES.values())
    print("scanning for 26-entry u16 tables covering movie frame set...")
    found = 0
    for i in range(0, len(blob) - 26 * 2, 2):
        vals = list(struct.unpack_from("<" + "H" * 26, blob, i))
        if any(v < 50 or v > 5000 for v in vals):
            continue
        # at least 15 match known frame counts
        match = sum(1 for v in vals if v in want)
        if match >= 12:
            print(f"  cand blob+{i:#x} match={match} vals={vals}")
            found += 1
            if found >= 20:
                break
    print("cands", found)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
