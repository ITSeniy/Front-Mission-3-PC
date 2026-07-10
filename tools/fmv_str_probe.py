#!/usr/bin/env python3
from pathlib import Path
import struct
import re

BIN = Path("fm3/fm3.bin")
SECTOR = 2352
USER = 2048
raw = BIN.read_bytes()


def su(lba: int):
    off = lba * SECTOR
    sec = raw[off : off + SECTOR]
    return sec, sec[24 : 24 + USER]


# MOV00 lba from fmv_skip_re
lba = 118885
print("=== first sectors of MOV00.STR ===")
for i in range(0, 16):
    sec, user = su(lba + i)
    print(
        f"sec+{i} mode={sec[15]} sub={sec[16]:02x}{sec[17]:02x}{sec[18]:02x}{sec[19]:02x} "
        f"head={user[:40].hex()}"
    )
    print("  u16:", struct.unpack_from("<10H", user, 0))
    print("  u32:", struct.unpack_from("<6I", user, 0))

print("\n=== string scan first 30k sectors ===")
iso = bytearray()
for i in range(0, 30000):
    iso += raw[i * SECTOR + 24 : i * SECTOR + 24 + USER]
iso = bytes(iso)
for pat in [
    rb"MOV%02d",
    rb"MOV%02d.STR",
    rb"MOV00.STR",
    rb"MOV%02d.STR;1",
    rb"\\MOV",
    rb"MOVIE",
    rb"movie",
    rb"StSetStream",
    rb"DecDCT",
    rb"CdRead",
]:
    ms = list(re.finditer(pat, iso, re.I))
    print(pat, "count", len(ms))
    for m in ms[:6]:
        lo = max(0, m.start() - 24)
        hi = min(len(iso), m.end() + 24)
        ctx = "".join(chr(c) if 32 <= c < 127 else "." for c in iso[lo:hi])
        print(" ", hex(m.start()), ctx)

# Estimate frame count: count video sectors / typical sectors-per-frame
# PSX 15fps STR often ~8-10 sectors/frame video+audio interleaved
print("\n=== sector-type histogram MOV00 (first 2000 sectors) ===")
from collections import Counter

c = Counter()
for i in range(2000):
    sec, user = su(lba + i)
    c[sec[18]] += 1
print(dict(sorted(c.items())))

# Try standard libpress header: after 32-byte? 
# Many STR: bytes 0-3 = frame number (BE or LE), 4-5 = chunk#, etc.
print("\n=== scan for increasing frame fields (sample offsets) ===")
for off in range(0, 0x30, 2):
    prev = None
    inc = 0
    tot = 0
    mx = 0
    for i in range(0, 800):
        sec, user = su(lba + i)
        if sec[18] & 0x04:  # audio
            continue
        v = struct.unpack_from("<H", user, off)[0]
        if prev is not None:
            tot += 1
            if v == prev or v == prev + 1:
                inc += 1
        prev = v
        if v < 20000:
            mx = max(mx, v)
    score = inc / max(1, tot)
    if score > 0.5 and mx > 20:
        print(f"  off={off:#x} score={score:.2f} max={mx}")
