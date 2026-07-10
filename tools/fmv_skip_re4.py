#!/usr/bin/env python3
import re
import struct
from pathlib import Path

exe = Path("fm3/SLUS_010.11").read_bytes()[0x800:]
base = 0x80010000
for v in (1144, 1399, 1564, 864, 604):
    b = struct.pack("<H", v)
    i = 0
    n = 0
    while True:
        j = exe.find(b, i)
        if j < 0:
            break
        n += 1
        lo = max(0, j - 16)
        lo -= lo % 2
        hi = min(len(exe), j + 32)
        vals = list(struct.unpack_from("<" + "H" * ((hi - lo) // 2), exe, lo))
        if n <= 8:
            print(f"{v} @ {hex(base+j)} ctx={vals}")
        i = j + 2
    print(f"  total {v}: {n}")

raw = Path("fm3/fm3.bin").read_bytes()
SECTOR = 2352
USER = 2048
iso = bytearray()
for lba in range(0, 20000):
    iso += raw[lba * SECTOR + 24 : lba * SECTOR + 24 + USER]
iso = bytes(iso)
names = set()
for m in re.finditer(rb"([A-Z0-9_]{1,16}\.[A-Z0-9]{3});1", iso):
    names.add(m.group(1).decode())
print("file count", len(names))
for n in sorted(names):
    if any(x in n for x in ("MOV", "MV", "STR", "OVL", "EXE", "BIN")):
        print(" ", n)

# Search disc for consecutive frame totals as u16 with movie-id order including gap at 10
# Index order: 0..9,11..26  OR 0..26 with 0 at 10
order_gap = [FRAMES for FRAMES in []]  # placeholder

totals_by_id = [
    604, 1564, 864, 454, 229, 1144, 836, 944, 93, 184,  # 0-9
    0,  # 10 missing
    147, 169, 211, 267, 289, 585, 364, 844,  # 11-18
    454,  # try 19 as 454 if mis-read - still 0 from file
    276, 634, 424, 169, 715, 454, 1399,
]
# Without MOV10 hole - list as files sorted
file_order = [604, 1564, 864, 454, 229, 1144, 836, 944, 93, 184, 147, 169, 211, 267, 289, 585, 364, 844, 0, 276, 634, 424, 169, 715, 454, 1399]

# Search disc for first 5 non-zero of file_order
seq = [604, 1564, 864, 454, 229]
needle = struct.pack("<" + "H" * 5, *seq)
print("disc search", seq)
hits = 0
nsec = len(raw) // SECTOR
for lba in range(nsec):
    user = raw[lba * SECTOR + 24 : lba * SECTOR + 24 + USER]
    j = user.find(needle)
    if j >= 0:
        vals = list(struct.unpack_from("<" + "H" * 28, user, j))
        print(f" HIT LBA {lba}+{j:#x} {vals}")
        hits += 1
        if hits >= 10:
            break
print("hits", hits)

# Overlay captures: any guest PC ranges that run during FMV?
cap = Path("build/overlay_captures.json")
if cap.exists():
    text = cap.read_text(encoding="utf-8", errors="replace")
    print("overlay_captures bytes", len(text))
    # show keys if json
    try:
        import json

        data = json.loads(text)
        if isinstance(data, dict):
            print("top keys", list(data.keys())[:20])
            print("n keys", len(data))
        elif isinstance(data, list):
            print("list len", len(data))
            if data:
                print("sample0 keys", data[0].keys() if isinstance(data[0], dict) else type(data[0]))
    except Exception as e:
        print("json err", e)
