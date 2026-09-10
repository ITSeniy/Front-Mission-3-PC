# Working state snapshot — 2026-07-10

Checkpoint after pre-playthrough audio work. Rebuild and launch from this
commit should match the playable boot path exercised locally.

## Stack

| Layer | State |
|---|---|
| Game | SLUS-01011, `game.toml`, 3839 JAL seeds, 4542 recompiled funcs |
| Binary | `Front_Mission_3_Recompiled.exe` via `psx-runtime` |
| Overlays | `build/cache/SLUS-01011` (~105 native DLLs after play+compile) |
| Disc/BIOS | Local only: `fm3/fm3.cue`, `psxrecomp-master/bios/SCPH1001.BIN` |

## Framework deltas tracked in this repo

Under the tracked `psxrecomp-master/runtime/` snapshot:

- **GPU/FMV:** depth24 present path, skip flash blank, bulk scanout
- **MDEC (Block C):** Beetle-faithful dequant/IDCT/YUV; 4/8bpp mono pack;
  status current-block + cmd mirror; zig-zag EOB. Still one-shot (no FIFO).
- **SPU guest clock:** 1 sample / 768 cycles, lag-capped out-ring
- **SPU audio model:** reverb, volume sweeps, noise (NON), PMON
- **SPU IRQ9 + capture:** address-match → `IRQ_SPU`; CD L/R + voice1/3 banks

## Launch

```powershell
$env:Path = "C:\msys64\mingw64\bin;" + $env:Path
cd build
.\Front_Mission_3_Recompiled.exe --game ..\game.toml --disc ..\fm3\fm3.cue --bios ..\psxrecomp-master\bios\SCPH1001.BIN
```

## Playthrough A checklist

1. BIOS logo + reverb chord  
2. License screen  
3. FMV intro (picture + XA), Start-skip without rainbow garbage  
4. Title / menu music + input  
5. New Game → first gameplay  

## Block C (enhancements after playable)

See `docs/BLOCK_C_ENHANCEMENTS.md`: launcher MinGW (C1), widescreen Phase 0
(C2 — `docs/WIDESCREEN_FM3.md`), SSAA (C3), turbo (C4), FMV skip RE (C5 —
`docs/FMV_SKIP_RE.md`).

## Known open

See `ISSUES.md` #5 / #6. MDEC streaming FIFO (D8) still architectural.
Widescreen + FMV skip need per-game RE.
