# Front Mission 3 Recomp — project rules

This game repo vendors the exact PSXRecomp source snapshot used by the game at
`psxrecomp-master/`. A local `psxrecomp/` junction may exist for compatibility,
but builds and scripts must not depend on it.

## Core rules (inherit framework CLAUDE.md / PRINCIPLES.md)

- Faithful LLE core first. No per-game hacks to fake correctness.
- No stubs. No hand-edited `generated/` files.
- Framework bugs → fix in `psxrecomp-master`. Game config/seeds/enhancements → here.
- Do not commit disc images, BIOS ROMs, generated C, memory cards, Ghidra DBs,
  overlay captures, or build outputs.

## Game facts (USA)

- ID: `SLUS-01011`
- EXE: `SLUS_010.11`
- load `0x80010000`, entry `0x8001004C`, text `0x000D4800`, stack `0x801FFFF0`
- Overlays live under disc `OVL/`; FMV under `MV/`

## Local layout

```
psxrecomp-master/bios/SCPH1001.BIN # required BIOS (local)
fm3/SLUS_010.11               # extracted EXE (local, Phase 1)
"Front Mission 3 (USA).cue"   # disc (local)
generated/                    # recompiler output (local)
```
