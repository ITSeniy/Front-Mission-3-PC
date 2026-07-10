#!/usr/bin/env python3
"""Disassemble crash-related PCs from FM3 main EXE."""
from __future__ import annotations

import struct
from pathlib import Path

EXE = Path("fm3/SLUS_010.11")
BASE = 0x80010000
text = EXE.read_bytes()[0x800:]
END = BASE + len(text)

REGS = [
    "zero","at","v0","v1","a0","a1","a2","a3",
    "t0","t1","t2","t3","t4","t5","t6","t7",
    "s0","s1","s2","s3","s4","s5","s6","s7",
    "t8","t9","k0","k1","gp","sp","fp","ra",
]


def se16(x: int) -> int:
    return x - 0x10000 if x & 0x8000 else x


def dis(addr: int, n: int = 24) -> str:
    if not (BASE <= addr < END):
        return f"{hex(addr)}: <outside main EXE  {hex(BASE)}..{hex(END)}>"
    lines = []
    off = addr - BASE
    for i in range(n):
        pc = addr + i * 4
        if pc >= END:
            break
        w = struct.unpack_from("<I", text, off + i * 4)[0]
        op = w >> 26
        rs = (w >> 21) & 31
        rt = (w >> 16) & 31
        rd = (w >> 11) & 31
        sa = (w >> 6) & 31
        fn = w & 63
        imm = w & 0xFFFF
        simm = se16(imm)
        tgt = ((pc & 0xF0000000) | ((w & 0x3FFFFFF) << 2)) if op in (2, 3) else 0
        s = f"{hex(w)}"
        if op == 0:
            if fn == 0 and w == 0:
                s = "nop"
            elif fn == 8:
                s = f"jr ${REGS[rs]}"
            elif fn == 9:
                s = f"jalr ${REGS[rd]}, ${REGS[rs]}"
            elif fn == 0x21:
                s = f"addu ${REGS[rd]}, ${REGS[rs]}, ${REGS[rt]}"
            elif fn == 0x23:
                s = f"subu ${REGS[rd]}, ${REGS[rs]}, ${REGS[rt]}"
            elif fn == 0x24:
                s = f"and ${REGS[rd]}, ${REGS[rs]}, ${REGS[rt]}"
            elif fn == 0x25:
                s = f"or ${REGS[rd]}, ${REGS[rs]}, ${REGS[rt]}"
            elif fn == 0x2A:
                s = f"slt ${REGS[rd]}, ${REGS[rs]}, ${REGS[rt]}"
            elif fn == 0x2B:
                s = f"sltu ${REGS[rd]}, ${REGS[rs]}, ${REGS[rt]}"
            elif fn == 0x00:
                s = f"sll ${REGS[rd]}, ${REGS[rt]}, {sa}"
            elif fn == 0x02:
                s = f"srl ${REGS[rd]}, ${REGS[rt]}, {sa}"
            elif fn == 0x03:
                s = f"sra ${REGS[rd]}, ${REGS[rt]}, {sa}"
            elif fn == 0x08:
                s = f"jr ${REGS[rs]}"
            else:
                s = f"special fn={hex(fn)}"
        elif op == 2:
            s = f"j {hex(tgt)}"
        elif op == 3:
            s = f"jal {hex(tgt)}"
        elif op == 4:
            s = f"beq ${REGS[rs]}, ${REGS[rt]}, {hex(pc+4+simm*4)}"
        elif op == 5:
            s = f"bne ${REGS[rs]}, ${REGS[rt]}, {hex(pc+4+simm*4)}"
        elif op == 6:
            s = f"blez ${REGS[rs]}, {hex(pc+4+simm*4)}"
        elif op == 7:
            s = f"bgtz ${REGS[rs]}, {hex(pc+4+simm*4)}"
        elif op == 8:
            s = f"addi ${REGS[rt]}, ${REGS[rs]}, {simm}"
        elif op == 9:
            s = f"addiu ${REGS[rt]}, ${REGS[rs]}, {simm}"
        elif op == 0x0A:
            s = f"slti ${REGS[rt]}, ${REGS[rs]}, {simm}"
        elif op == 0x0B:
            s = f"sltiu ${REGS[rt]}, ${REGS[rs]}, {simm}"
        elif op == 0x0C:
            s = f"andi ${REGS[rt]}, ${REGS[rs]}, {hex(imm)}"
        elif op == 0x0D:
            s = f"ori ${REGS[rt]}, ${REGS[rs]}, {hex(imm)}"
        elif op == 0x0F:
            s = f"lui ${REGS[rt]}, {hex(imm)}"
        elif op == 0x20:
            s = f"lb ${REGS[rt]}, {simm}(${REGS[rs]})"
        elif op == 0x23:
            s = f"lw ${REGS[rt]}, {simm}(${REGS[rs]})"
        elif op == 0x24:
            s = f"lbu ${REGS[rt]}, {simm}(${REGS[rs]})"
        elif op == 0x25:
            s = f"lhu ${REGS[rt]}, {simm}(${REGS[rs]})"
        elif op == 0x28:
            s = f"sb ${REGS[rt]}, {simm}(${REGS[rs]})"
        elif op == 0x29:
            s = f"sh ${REGS[rt]}, {simm}(${REGS[rs]})"
        elif op == 0x2B:
            s = f"sw ${REGS[rt]}, {simm}(${REGS[rs]})"
        elif op == 0x12:
            s = f"cop2 {hex(w)}"
        lines.append(f"  {hex(pc)}: {s}")
    return "\n".join(lines)


def main() -> None:
    addrs = [
        0x800B8990,  # function entry covering epc
        0x800B89D4,  # epc
        0x800B8A00,
        0x80082700,
        0x8007A8B0,
        0x8007B4B8,
        0x8008B9F0,
        0x80084618,
        0x80084674,
        0x80029934,
        0x800BAEFC,
        0x800B20C0,
        0x8014DADC,  # overlay dirty target
    ]
    for a in addrs:
        print(f"==== {hex(a)}")
        print(dis(a, 20))
        print()


if __name__ == "__main__":
    main()
