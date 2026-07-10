#!/usr/bin/env python3
import json
from pathlib import Path

TARGET = 0x8014DADC
p = Path("build/overlay_captures.json")
d = json.loads(p.read_text(encoding="utf-8"))
print("captures type", type(d).__name__, "len", len(d) if hasattr(d, "__len__") else "?")

def parse_int(x):
    if x is None:
        return 0
    if isinstance(x, int):
        return x
    return int(x, 0)

if isinstance(d, list):
    for i, e in enumerate(d):
        if not isinstance(e, dict):
            continue
        la = parse_int(e.get("load_addr"))
        sz = parse_int(e.get("size"))
        print(f"cap[{i}] load={hex(la)} size={sz} seeds={len(e.get('seeds') or [])} exec={len(e.get('executed_pcs') or [])}")
        if la and sz and la <= TARGET < la + sz:
            print("  ** COVERS TARGET **")
            seeds = [parse_int(s) for s in (e.get("seeds") or [])]
            execs = [parse_int(s) for s in (e.get("executed_pcs") or [])]
            print("  nearest seed", hex(min(seeds, key=lambda s: abs(s - TARGET))) if seeds else None)
            print("  target in seeds", TARGET in seeds)
            print("  target in exec", TARGET in execs)
            # nearby executed
            near = sorted([s for s in execs if abs(s - TARGET) < 0x100])
            print("  exec near", [hex(x) for x in near[:20]])

# cache
cache = Path("build/cache/SLUS-01011")
if cache.exists():
    dlls = list(cache.rglob("*.dll"))
    print("dll count", len(dlls))
    for rf in sorted(cache.rglob("*.ranges"))[:30]:
        txt = rf.read_text(encoding="utf-8", errors="replace")
        if "8014DA" in txt.upper() or "14DADC" in txt.upper():
            print("ranges hit", rf)
            for line in txt.splitlines():
                if "14DA" in line.upper() or "14DB" in line.upper():
                    print(" ", line)

# jal targets from main that hit 0x8014xxxx
exe = Path("fm3/SLUS_010.11").read_bytes()[0x800:]
base = 0x80010000
import struct
jals = []
for i in range(0, len(exe) - 4, 4):
    w = struct.unpack_from("<I", exe, i)[0]
    if (w >> 26) != 3:
        continue
    pc = base + i
    tgt = (pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
    if 0x80140000 <= tgt <= 0x80160000:
        jals.append((pc, tgt))
print("main-EXE jals into 0x8014xxxx..0x8016:", len(jals))
for pc, tgt in jals[:40]:
    print(f"  {hex(pc)} -> {hex(tgt)}")
