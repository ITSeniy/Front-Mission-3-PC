# PSXRecomp framework provenance

The runtime source used by Front Mission 3 is tracked as a vendored snapshot in
`psxrecomp-master/`. This is intentional: the previous workspace contained a
plain source dump plus a local `psxrecomp/` junction, while Git tracked only 32
selected framework files. A fresh clone therefore could not reproduce the
binary that produced a crash report.

Upstream reference audited on 2026-07-11:

- repository: `https://github.com/mstan/psxrecomp.git`
- upstream HEAD at audit time: `a6e166e82e5250137a126d628a816f00ae62bfc9`
- RmlUi commit used locally: `2cd28864ae25ed345b70598751703a5433b12356`
- FreeType commit used locally: `5336c0d4da22a13dab3389eb153b12672fdf841c`

The local framework differs materially from that upstream HEAD in scheduler,
interrupt, diagnostics, GPU, MDEC, and SPU code. The upstream hash is recorded
as an audit reference, not as a claim that the local snapshot is a clean patch
on top of that exact commit.

Clone with `--recurse-submodules` (or run `git submodule update --init
--recursive`) to populate RmlUi and FreeType. BIOS ROMs, generated BIOS/game C,
disc images, memory cards, overlay captures, and build outputs remain ignored.
