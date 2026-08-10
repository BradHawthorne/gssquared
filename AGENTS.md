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

1. Product-aware discovery, product-agnostic contracts.
2. No hard-coded product memory maps or session paths in the emulator.
3. Fail loud; never plausible-wrong meters or bare OK on no-ops.
4. **IIe work must not break IIgs** (and the reverse). Shared rail/MMU/device
   changes need evidence on **both** platforms. IIe-green alone is not done —
   this regression has already cost real IIgs logic. See SHARED_FOUNDATION and
   [`Docs/DUAL_PLATFORM.md`](Docs/DUAL_PLATFORM.md). From a2engine run
   `.\tools\dualsmoke.ps1` after shared rail work.
5. Dual-mode: instrumentation env-gated off by default; do not break windowed UX.
6. New verbs/rails must be discoverable (`help` / `manifest`).
7. Prefer foundation-local regression; product suites are clients, not owners.

## Build (summary)

See upstream and `README.md`. Typical Windows flow: configure CMake into
`build/`, build `GSSquared`, drive headless with env rails or the CTRL session
used by product harnesses (e.g. a2engine `tools/session.ps1` + `ctl.ps1`).
