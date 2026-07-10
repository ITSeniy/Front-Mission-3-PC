#!/usr/bin/env python3
"""Second-pass FMV RE for Front Mission 3.

STR sector layout (verified from MOV00):
  +0x00 u16: 0x0160 (fixed)
  +0x02 u16: 0x8001
  +0x04 u16: sector-in-frame index (0..N-1)
  +0x06 u16: sectors-per-frame (e.g. 9)
  +0x08 u16: frame number (1-based)
  +0x0A u16: 0
  +0x0C u16: demux chunk size
  +0x10 u16: width (320)
  +0x12 u16: height (240)
"""
from __future__ import annotations

import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "fm3" / "fm3.bin"
EXE = ROOT / "fm3" / "SLUS_010.11"
SECTOR = 2352
USER = 2048
EXE_BASE = 0x80010000


def su(raw: bytes, lba: int) -> tuple[bytes, bytes]:
    off = lba * SECTOR
    sec = raw[off : off + SECTOR]
    return sec, sec[24 : 24 + USER]


def rebuild_iso(raw: bytes, nsec: int) -> bytes:
    out = bytearray()
    for lba in range(nsec):
        out += raw[lba * SECTOR + 24 : lba * SECTOR + 24 + USER]
    return bytes(out)


def find_str_entries(iso: bytes) -> list[tuple[str, int, int]]:
    entries = []
    for m in re.finditer(rb"MOV\d{2}\.STR;1", iso):
        name = m.group().decode().split(";")[0]
        for back in range(80):
            rs = m.start() - 33 - back
            if rs < 0:
                continue
            if iso[rs + 32] and iso[rs + 33 : rs + 33 + iso[rs + 32]].startswith(
                m.group()[: iso[rs + 32]]
            ):
                lba = struct.unpack_from("<I", iso, rs + 2)[0]
                size = struct.unpack_from("<I", iso, rs + 10)[0]
                entries.append((name, lba, size))
                break
    seen = {}
    for e in entries:
        seen[e[0]] = e
    return [seen[k] for k in sorted(seen)]


def str_frame_total(raw: bytes, lba: int, size: int) -> tuple[int, int, int]:
    nsec = size // USER
    max_frame = 0
    n_vid = 0
    spf = 0
    for i in range(nsec):
        sec, user = su(raw, lba + i)
        if len(sec) < SECTOR:
            break
        if sec[18] != 0x48:  # DATA|REALTIME video
            continue
        n_vid += 1
        frame = struct.unpack_from("<H", user, 0x08)[0]
        spf = struct.unpack_from("<H", user, 0x06)[0]
        if frame > max_frame:
            max_frame = frame
    return max_frame, n_vid, spf


def find_u16_seq(hay: bytes, seq: list[int], base: int = 0) -> list[tuple[int, list[int]]]:
    if not seq or any(v < 0 or v > 0xFFFF for v in seq):
        return []
    needle = struct.pack("<" + "H" * len(seq), *seq)
    hits = []
    start = 0
    while True:
        j = hay.find(needle, start)
        if j < 0:
            break
        # expand
        n = min(40, (len(hay) - j) // 2)
        vals = list(struct.unpack_from("<" + "H" * n, hay, j))
        hits.append((base + j, vals))
        start = j + 2
    return hits


def main() -> int:
    print("Loading disc...")
    raw = BIN.read_bytes()
    iso = rebuild_iso(raw, 8000)
    entries = find_str_entries(iso)
    print(f"{len(entries)} STR files\n")

    totals = []
    for name, lba, size in entries:
        mx, nvid, spf = str_frame_total(raw, lba, size)
        totals.append(mx)
        print(f"{name}: frames={mx} video_secs={nvid} spf={spf} size={size} lba={lba}")

    print("\nTotals vector:", totals)

    exe = EXE.read_bytes()[0x800:]
    print("\n--- EXE table search ---")
    for label, seq in (
        ("exact frames", totals),
        ("frames+0 pad", totals + [0]),
        ("first 8", totals[:8]),
        ("first 12", totals[:12]),
    ):
        hits = find_u16_seq(exe, seq, EXE_BASE)
        print(f"{label}: {len(hits)} hits")
        for addr, vals in hits[:8]:
            print(f"  {hex(addr)} {vals[:len(totals)+2]}")

    # Search WHOLE disc ISO image (first 80k sectors ~160MB user) for table
    print("\n--- Disc-wide table search (overlays) ---")
    # only first 4 distinct totals as needle to limit cost
    if len(totals) >= 6:
        needle_seq = totals[:6]
        needle = struct.pack("<" + "H" * 6, *needle_seq)
        # scan sector-by-sector user data
        found = 0
        for lba in range(0, 120000):
            user = raw[lba * SECTOR + 24 : lba * SECTOR + 24 + USER]
            j = user.find(needle)
            if j < 0:
                continue
            vals = list(struct.unpack_from("<" + "H" * 30, user, j))
            # absolute disc offset as pseudo-addr for human
            print(f"  disc LBA {lba} +{j:#x}: {vals[:28]}")
            found += 1
            if found >= 20:
                break
        print(f"  total hits reported: {found}")

    # Path string refs on disc for \\MV\\MOV
    print("\n--- \\MV\\ path clusters (possible file lists) ---")
    # rebuild larger iso prefix for string search
    big = rebuild_iso(raw, 40000)
    for m in re.finditer(rb"\\MV\\MOV\d{2}\.STR", big):
        # only print cluster starts
        pass
    # find consecutive MOV list
    for m in re.finditer(rb"(?:\\MV\\MOV\d{2}\.STR;1\.){3,}", big):
        s = m.group()[:120]
        print(f"  iso+{m.start():#x}: {s!r}")

    # Search for code-like: "MOV" format near li of movie counts
    print("\n--- EXE: look for small switch tables with 26-28 entries of pointers ---")
    # pointer tables into KSEG0
    ptr_hits = 0
    for i in range(0, len(exe) - 26 * 4, 4):
        ok = 0
        for k in range(26):
            p = struct.unpack_from("<I", exe, i + k * 4)[0]
            if 0x80010000 <= p <= 0x80100000:
                ok += 1
        if ok >= 24:
            ptrs = [hex(struct.unpack_from("<I", exe, i + k * 4)[0]) for k in range(8)]
            print(f"  ptr table @{hex(EXE_BASE+i)} first8={ptrs}")
            ptr_hits += 1
            if ptr_hits >= 15:
                break

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
