#!/usr/bin/env python3
"""Disassemble overlay function at 0x8014DADC from capture bytes."""
from __future__ import annotations

import base64
import json
import struct
from pathlib import Path

TARGET = 0x8014DADC
d = json.loads(Path("build/overlay_captures.json").read_text(encoding="utf-8"))
e = d[0]
la = int(e["load_addr"], 0) if isinstance(e["load_addr"], str) else e["load_addr"]
blob = base64.b64decode(e["bytes_b64"])
print("load", hex(la), "blob", len(blob), "end", hex(la + len(blob)))

REGS = [
    "zero","at","v0","v1","a0","a1","a2","a3",
    "t0","t1","t2","t3","t4","t5","t6","t7",
    "s0","s1","s2","s3","s4","s5","s6","s7",
    "t8","t9","k0","k1","gp","sp","fp","ra",
]


def se16(x):
    return x - 0x10000 if x & 0x8000 else x


def dis_range(start: int, n: int = 40):
    off = start - la
    for i in range(n):
        pc = start + i * 4
        o = off + i * 4
        if o < 0 or o + 4 > len(blob):
            print(hex(pc), "OOB")
            break
        w = struct.unpack_from("<I", blob, o)[0]
        op = w >> 26
        rs = (w >> 21) & 31
        rt = (w >> 16) & 31
        rd = (w >> 11) & 31
        fn = w & 63
        imm = w & 0xFFFF
        simm = se16(imm)
        s = hex(w)
        if op == 0 and fn == 8:
            s = f"jr ${REGS[rs]}"
        elif op == 0 and fn == 9:
            s = f"jalr ${REGS[rd]}, ${REGS[rs]}"
        elif op == 2:
            t = (pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
            s = f"j {hex(t)}"
        elif op == 3:
            t = (pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
            s = f"jal {hex(t)}"
        elif op == 4:
            s = f"beq ${REGS[rs]}, ${REGS[rt]}, {hex(pc+4+simm*4)}"
        elif op == 5:
            s = f"bne ${REGS[rs]}, ${REGS[rt]}, {hex(pc+4+simm*4)}"
        elif op == 9:
            s = f"addiu ${REGS[rt]}, ${REGS[rs]}, {simm}"
        elif op == 0x0F:
            s = f"lui ${REGS[rt]}, {hex(imm)}"
        elif op == 0x23:
            s = f"lw ${REGS[rt]}, {simm}(${REGS[rs]})"
        elif op == 0x2B:
            s = f"sw ${REGS[rt]}, {simm}(${REGS[rs]})"
        elif op == 0x0D:
            s = f"ori ${REGS[rt]}, ${REGS[rs]}, {hex(imm)}"
        elif w == 0:
            s = "nop"
        print(f"  {hex(pc)}: {s}")


print("=== 0x8014DADC entry ===")
dis_range(TARGET, 50)

# also nearby seeds
seeds = [int(s, 0) if isinstance(s, str) else int(s) for s in e.get("seeds") or []]
seeds = sorted(set(seeds))
print("\nseeds around target:")
for s in seeds:
    if abs(s - TARGET) < 0x200:
        print(hex(s))

# Check if native DLL exists for this overlay
cache = Path("build/cache/SLUS-01011")
for rf in cache.rglob("*.ranges"):
    txt = rf.read_text(encoding="utf-8", errors="replace")
    if "8014DADC" in txt.upper() or "8014dadc" in txt:
        print("FOUND ranges", rf)
        print(txt[:500])
