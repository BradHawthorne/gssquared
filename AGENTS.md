# AGENTS.md

Guidance for coding agents working in this repository.

## What this repo is

**gssquared-fork** (`a2gspu` branch) is an instrumented Apple II / IIgs emulator
fork: stock windowed use by default, plus a headless/CTRL **agentic oracle** for
closed-loop development with the Rosetta toolchain and several product trees.

## Shared foundation (read first for behavioural changes)

This fork is a **shared foundation** for many projects (a2engine, a2tile, a2os,
a2gpu/a2vga, Rosetta closed-loop, interactive play). Product sessions often
drive fixes here on purpose; the public rail and device contracts must stay
**general**.

→ **[`Docs/SHARED_FOUNDATION.md`](Docs/SHARED_FOUNDATION.md)** — multi-consumer
rules, standing do-not-break list, pairing with Rosetta.

Also required context for instrumentation work:

- [`Docs/AGENTIC_ORACLE.md`](Docs/AGENTIC_ORACLE.md) — no black boxes; named models
- [`Docs/CLOSED_LOOP_INTEGRATION.md`](Docs/CLOSED_LOOP_INTEGRATION.md) — mission, operating modes, launch recipe
- [`Docs/A2GSPU_RAILS.md`](Docs/A2GSPU_RAILS.md) — env rails, CTRL verbs, and the output lines consumers parse
- [`Docs/INSTRUMENTATION_SHAPE.md`](Docs/INSTRUMENTATION_SHAPE.md) — the mechanisms M1–M12, with a status table
- [`Docs/UPSTREAM_SYNC.md`](Docs/UPSTREAM_SYNC.md) — what has been ported from upstream, and what is owed
- [`README.md`](README.md) — what this fork adds over upstream

## Working rules (short form)

Each rule says **who enforces it**. That column is not decoration: a rule list
that mixes gate-enforced rules with rules honoured by review alone, without
marking which is which, degrades all of them — an agent that finds one rule
quietly unenforced has no reason to believe the next.

| # | Rule | Enforced by |
|---|------|-------------|
| 1 | Product-aware discovery, product-agnostic contracts. | review |
| 2 | No hard-coded product memory maps or session paths in the emulator. **One standing exception, below.** | review |
| 3 | Fail loud; never plausible-wrong meters or bare OK on no-ops. | review, plus the gates that pin specific cases (assertparity, execcheck, railcheck) |
| 4 | **IIe work must not break IIgs** (and the reverse) — see below. | `dualsmoke.ps1` (IIe + IIgs); `unarmedhash.ps1` (pinned IIe and IIgs boots) |
| 5 | Dual-mode: instrumentation env-gated off by default; do not break windowed UX. | **headless half:** `unarmedhash.ps1` — pinned stock boots must reproduce `tools/a2rail/unarmed.baseline.json`. **Windowed half:** by hand — launch with no instrumentation environment and use it; nothing automates that, and saying otherwise would be the false claim this column exists to stop |
| 6 | New verbs/rails must be discoverable (`help` / `manifest`). | `railparity.ps1`: registry ↔ `getenv` both directions; registry → `Docs/A2GSPU_RAILS.md` (a row of its own, under the registry family, with a whole synopsis — one direction); the CTRL dispatcher (the `A2CTRL-DISPATCH` regions) ↔ `verbs[]` both directions; registered readers ↔ reach table both directions |
| 7 | Prefer foundation-local regression; product suites are clients, not owners. | review. The gate stack is owned here; a2engine pulls a copy (`sync-a2rail.ps1` in a2engine) |
| 8 | An observe rail reads guest memory through `a2obs_peek` (`src/a2gspu_observe.hpp`), never through `mmu->read`. | `unarmedhash.ps1`: the same boots with observation rails armed must reproduce the unarmed hash **and** cycle count |
| 9 | A driven session writes only where it was told: run from a scratch directory or set `A2GSPU_OUT_DIR`, and pin `A2GSPU_BRAM` when a number must reproduce. Test inputs live in `corpus/`. | review |

**Before calling shared work done**, run `.\tools\a2rail\gates.ps1` (about
seven minutes; `-Offline` for the seconds-long subset). It runs every gate above
and prints one matrix.

**Rule 4 in full.** Shared rail/MMU/device changes need evidence on **both**
platforms. IIe-green alone is not done — this regression has already cost real
IIgs logic. See SHARED_FOUNDATION and
[`Docs/DUAL_PLATFORM.md`](Docs/DUAL_PLATFORM.md).

**Rule 2 — standing exception: `src/wiz5_config.hpp`.** It compiles in PC traps
for one 1988 title — `$EC19` SCREENDEV (exit `$EC28`), `$DF09`/`$DF04`
UNITWRITE/UNITREAD, and the `$D681` CSP-6 eval-stack trap — together with that
title's zero-page layout, and twelve `WIZ5_*` rails to drive them. This is known,
deliberate and indefinite: it is named in
[`Docs/INSTRUMENTATION_SHAPE.md`](Docs/INSTRUMENTATION_SHAPE.md) section 1, and
M10 sets the retirement condition — **retired when its consumers read the
generic `A2GSPU_TRAP` records**, not before, because they are load-bearing for a
live consumer. Do not treat it as a defect to remove, and do not add a second
one: for new work the answer is `A2GSPU_TRAP`, which is what that file was
generalised into.

## Where things are

| Piece | Source |
|-------|--------|
| The application loop (upstream's) | `src/gs2.cpp` |
| The headless spike, CTRL + co-pilot rails, verb families, snapshots, micro-tests | `src/a2gspu_spike.cpp`, `src/a2gspu_ctrl.cpp`, `src/a2gspu_ctrl_cmds.cpp`, `src/a2gspu_snapshot.cpp`, `src/a2gspu_selftests.cpp` (shared declarations: `src/gs2_app.hpp`) |
| What discovery serves | `src/a2gspu_manifest.hpp` (`verbs[]`, record schemas), `src/a2gspu_rails_registry.hpp` (rails, reach, readers) |
| The gates | `tools/a2rail/` (`gates.ps1` runs them all) |
| Test inputs (not redistributed) | `corpus/` (`corpus/README.md` lists them and their consumers) |

## Build (summary)

Windows: MSYS2 UCRT64 GCC with Ninja. `C:\msys64\ucrt64\bin` must be on `PATH`
to build and to run `build\GSSquared.exe` (without it the process exits
`0xC0000135`); the gate scripts find it themselves (`runtime.ps1`), and ctest
carries it per test. `cmake --build build`, then `.\tools\a2rail\gates.ps1`.

A fresh clone needs the vendored SDL submodules:
`git submodule update --init vendored/SDL vendored/SDL_image vendored/SDL_net` and
`git submodule update --init --recursive vendored/SDL_ttf`. On Windows set
`git config --global core.longpaths true` first (or clone to a short path): SDL_ttf's
nested harfbuzz and plutosvg checkouts exceed MAX_PATH from a deep directory and fail
with "Filename too long". Verified 2026-09-16: a clean clone configures, builds every
target and passes ctest 8/8.

Drive the emulator headless with env rails, or with the CTRL rail through
`tools/a2rail/session.ps1` + `ctl.ps1`, one command at a time.

---

The section below is upstream's own agent file, carried verbatim from
jawaidbazyar2/gssquared so that later by-hand ports of it apply cleanly. Where it and the
fork's rules above disagree, the fork's rules win: here the debug protocol is
one control surface beside the CTRL rail, not the only one.

# Agent instructions

## Vendored dependencies

Never edit vendored SDL libraries (or other vendored third-party code under `vendored/`).

- Do not modify files under `vendored/SDL/`, `vendored/SDL_image/`, `vendored/SDL_ttf/`, `vendored/SDL_net/`, or any other path under `vendored/`.
- If a platform limitation in SDL blocks a feature (for example save-dialog default filenames on macOS), work around it in GSSquared application code, or document the limitation — do not patch vendored sources.
- Upstream fixes belong in the upstream project, not as local edits to vendored trees.

## Build Instructions

### MacOS

#### Normal Development Cycle Build - Single File Executable

```
cmake -DCMAKE_BUILD_TYPE=Release -S . -B build
cmake --build build --parallel
```

or if we need debug symbols

```
cmake -DCMAKE_BUILD_TYPE=Debug -S . -B build
cmake --build build --parallel
```

#### Build MacOS App Bundle

```
cmake -DGS2_PROGRAM_FILES=OFF  -DCMAKE_BUILD_TYPE=Release -S . -B build
cmake --build build --parallel
cmake --install build
```

### Windows (MSYS2 / MinGW64)

Windows development uses **MSYS2** with the MinGW64 toolchain (not a native PowerShell/`cmd` environment).

- Open an MSYS2 bash shell (or invoke `C:\msys64\usr\bin\bash.exe`). Home is `/home/<user>` (`C:\msys64\home\<user>`), not `%USERPROFILE%`.
- **Always `source ~/.profile` first** so compiler, cmake, and other MinGW tools are on `PATH` (`/c/msys64/mingw64/bin`). Without that, `g++`/`clang++`/`cmake` may be missing or the wrong binaries.
- From a non-login shell (including many agent terminals), use:

```
source ~/.profile
```

or start bash as a login shell (`bash -lc '…'`), which reads `~/.profile` automatically.

```
source ~/.profile
cmake -G "MinGW Makefiles" -DGS2_PROGRAM_FILES=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -B build -S .
cmake --build build --parallel
```

## Debug-protocol smoke tests

When launching GSSquared for scripted tests over `--debug SOCKET`:

- Prefer **`c.quit()`** (protocol `QUIT`) to stop the emu. Do **not** `kill`/`SIGTERM` the process unless necessary — SDL turns those into `SDL_EVENT_QUIT`, which opens the QuitModal (“Are you sure?”) and leaves tests hung or racing a broken pipe.
- If a harness must signal-kill, start the emu with **`--no-quit-confirm`** so `SDL_EVENT_QUIT` exits without the modal / dirty-disk prompts.
- Example (IIe Enhanced / IIgs): start `./build/GSSquared --debug /tmp/gs2-….sock -p 3` (or `-p 5`), wait for the socket, then:
  `PYTHONPATH=clients/python/src python3 clients/python/examples/test_breakpoints.py /tmp/gs2-….sock 3`
  The example ends with `c.quit()`; wait for the emu process to exit (expect exit 0) instead of killing it.
- Cookbook: `Docs/gs2debug.md`. Wire protocol: `Docs/DebugProtocol.md`.

## Disk Images

To inspect Apple II disk images and their contents, use CiderPress2:

```
cp2='~/src/cp2_1.0.5_osx-x64_sc/cp2'
```

## Release packages (macOS + Windows + Linux)

Native package builds are orchestrated from this Mac by `scripts/release/gs2-release`. Copy `scripts/release/hosts.toml.example` to `scripts/release/hosts.toml` and fill in SSH hosts. Run `unpushed` before `sync` so no host is sitting on unpushed commits. `sync` creates a detached worktree at `<repo>-release` (e.g. `~/src/gssquared-release`) from `origin/main`; package builds run there, not in the everyday clone. Agent workflow: `.cursor/skills/gs2-release/SKILL.md`.
