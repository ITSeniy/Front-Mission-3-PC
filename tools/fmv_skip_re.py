#!/usr/bin/env python3
"""RE helper: locate Front Mission 3 FMV (MOV*.STR) frame totals and
possible fmv_skip_total_table candidates in the main EXE.

Usage:
  python tools/fmv_skip_re.py
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


def sector_user(raw: bytes, lba: int) -> bytes:
    off = lba * SECTOR
    return raw[off + 24 : off + 24 + USER]


def rebuild_iso_prefix(raw: bytes, nsec: int = 5000) -> bytes:
    out = bytearray()
    for lba in range(nsec):
        out += sector_user(raw, lba)
    return bytes(out)


def find_str_entries(iso: bytes) -> list[tuple[str, int, int]]:
    entries: list[tuple[str, int, int]] = []
    for m in re.finditer(rb"MOV\d{2}\.STR;1", iso):
        name = m.group().decode().split(";")[0]
        for back in range(0, 80):
            rs = m.start() - 33 - back
            if rs < 0:
                continue
            reclen = iso[rs]
            if reclen == 0:
                continue
            nlen = iso[rs + 32]
            if nlen and iso[rs + 33 : rs + 33 + nlen] == m.group()[:nlen]:
                lba = struct.unpack_from("<I", iso, rs + 2)[0]
                size = struct.unpack_from("<I", iso, rs + 10)[0]
                entries.append((name, lba, size))
                break
    # unique by name
    seen: dict[str, tuple[str, int, int]] = {}
    for e in entries:
        seen[e[0]] = e
    return [seen[k] for k in sorted(seen)]


def parse_str_max_frame(raw: bytes, lba: int, size: int) -> tuple[int, int, dict]:
    """Return (max_frame, n_video_sectors, debug).

    Common libpress / custom STR: first 32 bytes of each VIDEO sector carry
    demux header; frame number is often a little-endian u16/u32 near the start.
    We try several offsets and pick the one that increases nearly monotonically.
    """
    nsec = size // USER
    candidates = {0: [], 4: [], 8: [], 0x10: [], 0x14: [], 0x1C: [], 0x20: []}
    n_vid = 0
    for i in range(nsec):
        off = (lba + i) * SECTOR
        sec = raw[off : off + SECTOR]
        if len(sec) < SECTOR:
            break
        submode = sec[18]
        # 0x48 = DATA|REALTIME typical video; skip AUDIO (0x64 etc.)
        if submode & 0x20:  # FORM2 often audio
            if submode & 0x04:  # AUDIO bit
                continue
        user = sec[24 : 24 + USER]
        # Heuristic: video MDEC bitstream sectors usually have nonzero early bytes
        n_vid += 1
        for o in candidates:
            if o + 4 > len(user):
                continue
            v32 = struct.unpack_from("<I", user, o)[0]
            v16 = struct.unpack_from("<H", user, o)[0]
            candidates[o].append((v16, v32))

    best_off = None
    best_max = 0
    best_score = -1
    debug = {}
    for o, seq in candidates.items():
        if len(seq) < 8:
            continue
        # prefer u16 that goes 0,1,2... or 1,2,3...
        u16s = [a for a, _ in seq]
        # sample every sector may include non-frame sectors — take unique ordered
        # Score: fraction of consecutive increases by 0 or 1 among first 200
        sample = u16s[: min(400, len(u16s))]
        inc = 0
        tot = 0
        for a, b in zip(sample, sample[1:]):
            tot += 1
            if 0 <= b - a <= 2 and b < 10000:
                inc += 1
        score = inc / max(1, tot)
        mx = max((x for x in u16s if x < 10000), default=0)
        debug[o] = {"score": round(score, 3), "max_u16": mx, "first8": u16s[:8]}
        if score > best_score and mx > 10:
            best_score = score
            best_off = o
            best_max = mx

    if best_off is None:
        return 0, n_vid, debug
    return best_max, n_vid, {"best_off": best_off, **debug.get(best_off, {}), "all": debug}


def find_table_in_exe(exe_text: bytes, totals: list[int]) -> list[tuple[int, list[int]]]:
    """Search for a u16 LE sequence matching the movie totals (exact or prefix)."""
    if not totals:
        return []
    # pack
    needle = struct.pack("<" + "H" * len(totals), *totals)
    hits = []
    start = 0
    while True:
        j = exe_text.find(needle, start)
        if j < 0:
            break
        hits.append((EXE_BASE + j, totals))
        start = j + 2
    if hits:
        return hits

    # try without zeros / with small padding variants: search first 8 non-zero
    nz = [t for t in totals if t > 0][:10]
    if len(nz) < 4:
        return []
    needle = struct.pack("<" + "H" * len(nz), *nz)
    start = 0
    while True:
        j = exe_text.find(needle, start)
        if j < 0:
            break
        # read surrounding as table
        vals = list(struct.unpack_from("<" + "H" * min(32, (len(exe_text) - j) // 2), exe_text, j))
        hits.append((EXE_BASE + j, vals[: len(totals) + 4]))
        start = j + 2
    return hits


def find_mdec_refs(exe_text: bytes) -> list[tuple[int, int]]:
    """Find lui/ori materializations of 0x1F801820 range."""
    hits = []
    for i in range(0, len(exe_text) - 8, 4):
        w = struct.unpack_from("<I", exe_text, i)[0]
        if (w & 0xFFE00000) != 0x3C000000:
            continue
        imm = w & 0xFFFF
        if imm != 0x1F80:
            continue
        rt = (w >> 16) & 0x1F
        for j in range(1, 8):
            if i + j * 4 + 4 > len(exe_text):
                break
            w2 = struct.unpack_from("<I", exe_text, i + j * 4)[0]
            op = w2 >> 26
            rs = (w2 >> 21) & 0x1F
            if rs != rt:
                continue
            if op == 0x0D:  # ori
                addr = (imm << 16) | (w2 & 0xFFFF)
            elif op == 0x09:  # addiu
                low = w2 & 0xFFFF
                if low >= 0x8000:
                    low -= 0x10000
                addr = (imm << 16) + low
            else:
                continue
            if 0x1F801800 <= addr <= 0x1F80182C:
                hits.append((EXE_BASE + i, addr))
    return hits


def scan_movie_name_refs(exe_text: bytes) -> list[tuple[int, str]]:
    hits = []
    for m in re.finditer(rb"MOV\d{2}\.STR", exe_text, re.I):
        hits.append((EXE_BASE + m.start(), m.group().decode("ascii", "replace")))
    # also format strings
    for pat in (rb"MOV%02d", rb"MOV%02d.STR", rb"mov%02d", rb"%s.STR", rb"\\MOV"):
        for m in re.finditer(pat, exe_text, re.I):
            hits.append((EXE_BASE + m.start(), m.group().decode("ascii", "replace")))
    return hits


def main() -> int:
    if not BIN.exists() or not EXE.exists():
        print("missing fm3.bin or SLUS_010.11")
        return 1

    print("Reading disc (this may take a moment)...")
    raw = BIN.read_bytes()
    print(f"  bin={len(raw)} bytes")
    iso = rebuild_iso_prefix(raw, 8000)
    entries = find_str_entries(iso)
    print(f"STR files: {len(entries)}")

    totals: list[int] = []
    for name, lba, size in entries:
        mx, nvid, dbg = parse_str_max_frame(raw, lba, size)
        # frame total is often max_frame+1
        total = mx + 1 if mx > 0 else 0
        totals.append(total)
        print(f"  {name}: lba={lba} size={size} max_frame={mx} total~={total} nsec~={size//USER} best={dbg.get('best_off')} score={dbg.get('score')} first={dbg.get('first8')}")

    exe = EXE.read_bytes()
    text = exe[0x800:]
    print("\nMovie path strings in EXE:")
    for pc, s in scan_movie_name_refs(text)[:40]:
        print(f"  {hex(pc)} {s}")

    print("\nMDEC MMIO materialize sites:")
    mdec = find_mdec_refs(text)
    print(f"  count={len(mdec)}")
    for pc, addr in mdec[:30]:
        print(f"  {hex(pc)} -> {hex(addr)}")

    print("\nSearching EXE for frame-total table matching STR lengths...")
    # try totals and totals-1 variants
    for variant_name, seq in (
        ("max+1", totals),
        ("max", [max(0, t - 1) for t in totals]),
        ("nsec_est", []),  # filled below if needed
    ):
        if not seq or all(v == 0 for v in seq):
            continue
        hits = find_table_in_exe(text, seq)
        print(f"  variant {variant_name}: {len(hits)} hits; seq[:8]={seq[:8]}")
        for addr, vals in hits[:10]:
            print(f"    table@{hex(addr)} -> {vals[:20]}")

    # Also dump nearby u16 runs at any partial match of first 4 totals
    if any(totals):
        first4 = [t for t in totals if t > 0][:4]
        if len(first4) == 4:
            needle = struct.pack("<HHHH", *first4)
            idx = 0
            print(f"\nPartial first-4 search {first4}:")
            while True:
                j = text.find(needle, idx)
                if j < 0:
                    break
                vals = list(struct.unpack_from("<" + "H" * 28, text, j))
                print(f"  {hex(EXE_BASE+j)} {vals}")
                idx = j + 2

    print("\nDone. Next: if a table hit looks right, RE who indexes it (movie_id).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
