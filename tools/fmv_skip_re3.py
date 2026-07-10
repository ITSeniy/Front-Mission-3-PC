#!/usr/bin/env python3
"""Hunt FM3 movie-end mechanism: frame table may be absent; try size-derived
totals, sparse tables, and code xrefs to \\MV\\ strings on disc overlays."""
from __future__ import annotations

import struct
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "fm3" / "fm3.bin"
EXE = ROOT / "fm3" / "SLUS_010.11"
SECTOR = 2352
USER = 2048

# From re2
FRAMES = {
    0: 604, 1: 1564, 2: 864, 3: 454, 4: 229, 5: 1144, 6: 836, 7: 944,
    8: 93, 9: 184, 11: 147, 12: 169, 13: 211, 14: 267, 15: 289, 16: 585,
    17: 364, 18: 844, 19: 0, 20: 276, 21: 634, 22: 424, 23: 169, 24: 715,
    25: 454, 26: 1399,
}


def main() -> int:
    raw = BIN.read_bytes()
    # Fix MOV19: dump submode histogram
    lba19 = 170128
    c = Counter()
    for i in range(min(100, 4540)):
        sec = raw[(lba19 + i) * SECTOR : (lba19 + i) * SECTOR + SECTOR]
        c[sec[18]] += 1
        if i < 4:
            user = sec[24:24+32]
            print(f"MOV19 sec+{i} sub={sec[18]:02x} head={user.hex()}")
    print("MOV19 submodes", dict(c))

    # Search entire disc for u16 604 followed within 64 bytes by 1564
    print("\nSearching disc for 604 then 1564 (u16 LE)...")
    needle_a = struct.pack("<H", 604)
    needle_b = struct.pack("<H", 1564)
    hits = 0
    # sample every sector - 300k sectors
    nsec = len(raw) // SECTOR
    for lba in range(nsec):
        user = raw[lba * SECTOR + 24 : lba * SECTOR + 24 + USER]
        ja = 0
        while True:
            j = user.find(needle_a, ja)
            if j < 0:
                break
            window = user[j : j + 80]
            if needle_b in window:
                # dump as u16s
                vals = list(struct.unpack_from("<" + "H" * (len(window) // 2), window))
                print(f"  LBA {lba}+{j:#x}: {vals[:24]}")
                hits += 1
                if hits >= 30:
                    print("  (cap)")
                    break
            ja = j + 2
        if hits >= 30:
            break
    print(f"hits={hits}")

    # Search EXE for 604 and 1564 as separate
    exe = EXE.read_bytes()[0x800:]
    print("\nEXE occurrences of key frame counts:")
    for v in (604, 1564, 864, 454, 229, 1144, 93, 1399):
        n = exe.count(struct.pack("<H", v))
        print(f"  {v}: {n} times")

    # Search for path string \MV\ as ASCII in whole disc (already know list at ~0x3073cf4 iso offset)
    # Convert iso offset to LBA: iso_user_offset / 2048
    iso_off = 0x3073CF4
    lba = iso_off // USER
    print(f"\nPath list approx LBA {lba} (iso_off {iso_off:#x})")
    # dump neighboring sectors for tables
    for d in range(-2, 6):
        user = raw[(lba + d) * SECTOR + 24 : (lba + d) * SECTOR + 24 + USER]
        # show printable strings
        s = "".join(chr(c) if 32 <= c < 127 else "." for c in user)
        if "MOV" in s or "MV" in s:
            print(f"  LBA {lba+d}: ...{s[s.find('MOV')-10:s.find('MOV')+80] if 'MOV' in s else s[:100]}")

    # Maybe totals are u32
    print("\nDisc search u32 604 then 1564...")
    na, nb = struct.pack("<I", 604), struct.pack("<I", 1564)
    hits = 0
    for lba in range(0, min(nsec, 200000)):
        user = raw[lba * SECTOR + 24 : lba * SECTOR + 24 + USER]
        j = user.find(na)
        if j >= 0 and nb in user[j : j + 64]:
            vals = list(struct.unpack_from("<" + "I" * 12, user, j))
            print(f"  LBA {lba}+{j:#x}: {vals}")
            hits += 1
            if hits >= 15:
                break
    print(f"u32 hits={hits}")

    # Sector-count derived totals: video_secs/spf ~ frames
    print("\nDerived frames from size (if player uses size not table):")
    # from earlier nsec and spf
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
