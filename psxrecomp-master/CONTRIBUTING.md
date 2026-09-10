# Contributing to PSXRecomp

Thanks for your interest! PSXRecomp is a static recompiler, not an emulator, and
it holds itself to a high correctness bar. This guide covers how the project is
organized, the rules that keep it faithful, and how to get a change merged.

New to the codebase? Read [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) and
[`docs/EXECUTION_MODEL.md`](docs/EXECUTION_MODEL.md) first, then
[`docs/BUILDING.md`](docs/BUILDING.md) to get a build going.

## Repository layout

- **`mstan/psxrecomp`** (this repo) — the framework: the recompiler tool
  (`recompiler/`), the runtime engine (`runtime/`), shared tools, and docs.
  Framework changes go here.
- **Per-game repos** (`mstan/TombaRecomp`, `mstan/MegaManX6Recomp`, …) — one per
  title. Each contains only that game's config, seeds, and build glue and links
  this framework in as a submodule at `psxrecomp/`. Game-specific work goes in
  the game repo; nothing game-specific belongs in the framework.

See [Linking the framework](docs/BUILDING.md#linking-the-framework) for the
submodule + local-junction dev setup.

## The core rules

These are non-negotiable because violating them is how past efforts turned into
un-debuggable emulators. The full engineering constitution is
[`CLAUDE.md`](CLAUDE.md); the debugging philosophy is
[`PRINCIPLES.md`](PRINCIPLES.md). The short version:

1. **Build the faithful core; no per-game hacks in the foundation.** The correct
   fix is the general, hardware-accurate one, not a surgical workaround for one
   title. (Per-game *enhancements* — widescreen, faster loads — are legitimate
   once the faithful core is proven; see [`ENHANCEMENTS.md`](ENHANCEMENTS.md).)
2. **No stubs.** A function is fully implemented or it fails loudly. No
   `return 0;` placeholders, no `// TODO` behavior, no hand-delivered fake
   events. If execution reaches code we can't handle, we stop and fix
   discovery/codegen — we don't fake the result.
3. **Never hand-edit generated code.** Files under `generated/` and the
   recompiler's output are build artifacts. If the emitted C is wrong, fix the
   recompiler (`recompiler/src/`), then regenerate.
4. **LLE is the baseline; HLE is a validated subsystem replacement, never a
   starting point.** The recompiled BIOS is the reference and oracle. An HLE tier
   is allowed only where the LLE path has a genuine landmine, must be general
   (every game), must operate on the real guest structures, and must be
   continuously checked against the Beetle oracle — never a "produce the answer
   the BIOS would have" shim.
5. **No `printf`/log-file debugging.** Runtime inspection goes through the TCP
   debug server ([`TCP_COMMANDS.md`](TCP_COMMANDS.md)) and always-on ring
   buffers, not `fprintf`.
6. **Fix broken tooling immediately** — don't route around it with indirect
   evidence, and don't infer correctness from two implementations sharing a bug.

## Verifying a change

Correctness is demonstrated, not asserted:

- **Oracle-check against Beetle.** Run `psx-beetle` alongside `psx-runtime` and
  compare via the shared TCP protocol; for timing/codegen, use the co-sim build.
  See [`docs/internal/COSIM_ORACLE.md`](docs/internal/COSIM_ORACLE.md).
- **Verify visually, end to end.** A milestone is "the pixels appear on screen"
  — take a screenshot, don't trust a counter or a ring-buffer flag alone.
- **A decoder / codegen change requires a playthrough check**, not just a clean
  build: cosim/lockstep first, then run the affected area.

## Adding a new game

At a high level: create a game repo from the template, add this framework as the
`psxrecomp/` submodule, seed function starts (typically from a Ghidra export in
`seeds/`), write `game.toml`, generate the game C with
`psxrecomp-game --config game.toml`, then build and bring the title up boot →
menu → gameplay, oracle-checking as you go. The existing game repos
(TombaRecomp, MegaManX6Recomp) are the working references.

## Pull requests

- Branch off `master`; keep framework changes and game changes in their
  respective repos.
- **Do not commit** local artifacts: BIOS images, disc images, generated C
  (`generated/`), memory cards, Ghidra databases, build outputs, or overlay
  capture files. These are gitignored for good reasons (copyright + size).
- Explain *what hardware behavior* the change matches and *how you verified it*
  (oracle diff, screenshot, playthrough). Changes that can't state their
  verification are hard to accept.
- Keep the fix minimal and at the right layer (recompiler vs runtime vs game
  config). If you find yourself editing generated code or adding a stub, step
  back — the real fix is upstream of that.

## Questions / community

Development happens with the **R.A.I.D. (Retro AI Development)** community —
Discord invite in the [README](README.md). Open a GitHub issue for bugs and
build problems (include `gcc -v` / OS / generator for build failures).
