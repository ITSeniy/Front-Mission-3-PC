# C5 — Front Mission 3 FMV skip RE notes

Status: **partial.** Table-based Tomba-style skip is **not** available yet.
Generic START injection works without addresses. Tools under `tools/fmv_*`.

## Disc layout

| Item | Value |
|---|---|
| Folder | `\MV\` |
| Files | `MOV00.STR` … `MOV09.STR`, `MOV11.STR` … `MOV26.STR` (**no MOV10**) |
| Count | **26** movies |
| Overlay | `\YMV.BIN` (~81 KB) — path table + PsyQ MDEC strings (`MDEC_in_sync`, …) |
| Main EXE | No `\MV\` / `MOV` strings; loads `YMV.BIN` by name from file list at `0x800d56ed` |

## STR sector format (verified)

Mode2 Form1 user data, video submode `0x48` (some clips e.g. MOV19 use `0x08`):

| Off | Type | Meaning |
|---|---|---|
| +0x00 | u16 | `0x0160` fixed |
| +0x02 | u16 | `0x8001` |
| +0x04 | u16 | sector-in-frame index |
| +0x06 | u16 | sectors per frame (8 or 9) |
| +0x08 | u16 | **frame number** (1-based) |
| +0x10 | u16 | width (320) |
| +0x12 | u16 | height (240) |

Audio interleaved with submode `0x64`.

## Per-movie frame totals (from STR headers)

| ID | File | Frames |
|---:|---|---:|
| 0 | MOV00.STR | 604 |
| 1 | MOV01.STR | 1564 |
| 2 | MOV02.STR | 864 |
| 3 | MOV03.STR | 454 |
| 4 | MOV04.STR | 229 |
| 5 | MOV05.STR | 1144 |
| 6 | MOV06.STR | 836 |
| 7 | MOV07.STR | 944 |
| 8 | MOV08.STR | 93 |
| 9 | MOV09.STR | 184 |
| 11 | MOV11.STR | 147 |
| 12 | MOV12.STR | 169 |
| 13 | MOV13.STR | 211 |
| 14 | MOV14.STR | 267 |
| 15 | MOV15.STR | 289 |
| 16 | MOV16.STR | 585 |
| 17 | MOV17.STR | 364 |
| 18 | MOV18.STR | 844 |
| 19 | MOV19.STR | 454 |
| 20 | MOV20.STR | 276 |
| 21 | MOV21.STR | 634 |
| 22 | MOV22.STR | 424 |
| 23 | MOV23.STR | 169 |
| 24 | MOV24.STR | 715 |
| 25 | MOV25.STR | 454 |
| 26 | MOV26.STR | 1399 |

## What we looked for (and did **not** find)

Tomba ends movies when:

```text
streamed_frame >= table[movie_id] - 3
```

with `table` in **main EXE BSS/data** and `movie_id` on scratchpad.

For FM3:

1. **No u16 sequence** of the frame totals above in main EXE.
2. **No such sequence** in `YMV.BIN` or a full-disc scan of consecutive totals.
3. Path list in `YMV.BIN` @ file `+0x24F0` is **only** `\MV\MOVxx.STR;1` strings (16-byte records), not lengths.
4. MDEC I/O in main EXE not materialized as simple `lui 0x1F80` sites (player lives in `YMV.BIN` / BIOS libs).

Conclusion: FM3’s player almost certainly ends on **stream EOF / PsyQ St\* status**, not a static per-movie frame table. Tomba’s `fmv_skip_total_table` + `fmv_skip_movie_id` knobs **do not apply** until live RAM proves otherwise.

## What works today (no RE addresses)

Runtime (`main.cpp`) when `auto_skip_fmv = true` and **no** table:

- Detect MDEC decode + XA stream
- Hold **START** (active-low pad bit 3) so the game’s own FMV handler aborts
- Mute audio + blank present during skip transition

```toml
[video]
auto_skip_fmv = false   # set true to try START-injection skip
# leave fmv_skip_total_table / fmv_skip_movie_id unset
```

Limitations (same as any START fallback):

- Movies that **ignore** START stay unskippable
- Not as instant as table poke (depends on game poll rate)

Manual Start during FMV still works for playable skips (present blank path already polished).

## Next RE steps (live / debugger)

When a debug build is handy during intro FMV:

1. Confirm `YMV.BIN` load address (watch CdRead / file loader for `YMV.BIN`).
2. In `YMV` BSS after movie start, search for **u16 current frame** matching STR `+0x08`.
3. Find comparison against end (EOF flag, remaining sectors, or a **runtime-filled** total).
4. If a runtime total exists → add `fmv_skip_*` once addresses are stable.
5. If only EOF → consider stream-abort poke (Cd/St state) as a new lever (framework change).

Useful tools:

```text
python tools/fmv_skip_re2.py   # frame totals from disc
python tools/fmv_str_probe.py  # sector header dump
python tools/fmv_ymv_probe.py  # extract build/YMV.BIN + scans
```

## Config recommendation (FM3)

| Knob | Value | Why |
|---|---|---|
| `auto_skip_fmv` | `false` (default) | Don’t surprise playthrough; manual Start OK |
| `fmv_skip_total_table` | omit / 0 | No static table |
| `fmv_skip_movie_id` | omit / 0 | N/A |
| `fmv_skip_no_xa` | `false` | FM3 STRs carry XA (`0x64` sectors) |

Optional tester profile: `auto_skip_fmv = true` to exercise START injection on boot movies.
