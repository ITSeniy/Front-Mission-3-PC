# Working state snapshot — 2026-07-10

Checkpoint after pre-playthrough audio work. Rebuild and launch from this
commit should match the playable boot path exercised locally.

## Stack

| Layer | State |
|---|---|
| Game | SLUS-01011, `game.toml`, 3839 JAL seeds, 4542 recompiled funcs |
| Binary | `Front_Mission_3_Recompiled.exe` via `psx-runtime` |
| Overlays | `build/cache/SLUS-01011` (~105 native DLLs after play+compile) |
| Disc/BIOS | Local only: `fm3/fm3.cue`, `psxrecomp/bios/SCPH1001.BIN` |

## Framework deltas tracked in this repo

Under `psxrecomp-master/runtime/` (hand-edited subset):

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
.\Front_Mission_3_Recompiled.exe --game ..\game.toml --disc ..\fm3\fm3.cue --bios ..\psxrecomp\bios\SCPH1001.BIN
```

## Playthrough A checklist

1. BIOS logo + reverb chord  
2. License screen  
3. FMV intro (picture + XA), Start-skip without rainbow garbage  
4. Title / menu music + input  
5. New Game → first gameplay  

## Known open

See `ISSUES.md` #5 (logo retest vs DuckStation), #6 (mild FMV cost), launcher
MinGW link (#1). MDEC streaming FIFO (D8) still architectural.
