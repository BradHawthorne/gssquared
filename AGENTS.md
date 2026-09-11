# AGENTS.md

Guidance for coding agents working in this repository.

## What this repo is

**gssquared-fork** (`a2gspu` branch) is an instrumented Apple II / IIgs emulator
fork: stock windowed use by default, plus a headless/CTRL **agentic oracle** for
closed-loop development with the Rosetta toolchain and multiple product trees.

## Shared foundation (read first for behavioural changes)

This fork is a **shared foundation** for many projects (a2engine, a2tile, a2os,
a2gpu/a2vga, Rosetta closed-loop, interactive play). Product sessions often
drive fixes here on purpose; the public rail and device contracts must stay
**general**.

→ **[`Docs/SHARED_FOUNDATION.md`](Docs/SHARED_FOUNDATION.md)** — multi-consumer
rules, standing do-not-break list, pairing with Rosetta.

Also required context for instrumentation work:

- [`Docs/AGENTIC_ORACLE.md`](Docs/AGENTIC_ORACLE.md) — no black boxes; named models
- [`Docs/CLOSED_LOOP_INTEGRATION.md`](Docs/CLOSED_LOOP_INTEGRATION.md) — closed-loop mission
- [`Docs/A2GSPU_RAILS.md`](Docs/A2GSPU_RAILS.md) — env rails
- [`README.md`](README.md) — what this fork adds over upstream

## Working rules (short form)

Each rule says **who enforces it**. That column is not decoration: the list
used to read as seven equal absolutes while some were gate-enforced, some were
honoured by review alone, and one had a standing, deliberate exception that
every other document in the tree knew about and this one did not. A rule list
that mixes those without marking them degrades all of them — an agent that
finds one rule quietly excepted has no reason to believe the next.

| # | Rule | Enforced by |
|---|------|-------------|
| 1 | Product-aware discovery, product-agnostic contracts. | review |
| 2 | No hard-coded product memory maps or session paths in the emulator. **One standing exception, below.** | review |
| 3 | Fail loud; never plausible-wrong meters or bare OK on no-ops. | review |
| 4 | **IIe work must not break IIgs** (and the reverse) — see below. | `dualsmoke.ps1` |
| 5 | Dual-mode: instrumentation env-gated off by default; do not break windowed UX. | review. (`railparity.ps1` proves every rail is *declared*, not that it is *off* by default, and nothing runs the no-env windowed comparison automatically — that is still a hand check, and saying otherwise here would be the same kind of false claim this column exists to stop) |
| 6 | New verbs/rails must be discoverable (`help` / `manifest`). | `railparity.ps1`: registry↔`getenv`, registry↔`Docs/A2GSPU_RAILS.md`, and CTRL dispatcher↔`verbs[]`, all both-directions |
| 7 | Prefer foundation-local regression; product suites are clients, not owners. | review |

**Rule 4 in full.** Shared rail/MMU/device changes need evidence on **both**
platforms. IIe-green alone is not done — this regression has already cost real
IIgs logic. See SHARED_FOUNDATION and
[`Docs/DUAL_PLATFORM.md`](Docs/DUAL_PLATFORM.md). After shared rail work run:

```powershell
.\tools\a2rail\dualsmoke.ps1
```

(Portable instrument harness — no a2engine clone required. Full device net:
a2engine `.\tools\gscheck.ps1`.)

**Rule 2 — standing exception: `src/wiz5_config.hpp`.** It compiles in three PC
traps for one 1988 title (`$EC19` SCREENDEV, `$DF09` UNITWRITE, `$DF04`
UNITREAD) together with that title's zero-page layout, and twelve `WIZ5_*` rails
to drive them. This is known, deliberate and indefinite: it is named in
[`Docs/INSTRUMENTATION_SHAPE.md`](Docs/INSTRUMENTATION_SHAPE.md) section 1, and
M10 sets the retirement condition — **retired when its consumers read the
generic `A2GSPU_TRAP` records**, not before, because they are load-bearing for a
live consumer. Do not treat it as a defect to remove, and do not add a second
one: for new work the answer is `A2GSPU_TRAP`, which is what that file was
generalised into.

## Build (summary)

See upstream and `README.md`. Typical Windows flow: configure CMake into
`build/`, build `GSSquared`, drive headless with env rails or the CTRL session
used by product harnesses (e.g. a2engine `tools/session.ps1` + `ctl.ps1`).
