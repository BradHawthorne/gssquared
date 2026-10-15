# Agentic layer and upstream sync: the roadmap

**The fork's working task list.** Detailed reports are mirrored in `Docs/reports/`. This
file is the tracked summary. It stays a local commit; pushing is the owner's call.

**The fork's stance:**
- The owner's agentic emulator layer sits on top of upstream gssquared, and upstream stays vanilla by choice.
- "Drift" means individually assessing upstream changes for integration on our terms (`Docs/UPSTREAM_SYNC.md`).
- Every change is gated: ctest plus the 12 gates, with the pinned baselines (unarmedhash, hwtruth and the rest)
  unchanged unless the owner approves a re-bless.

## 1. Upstream sync (drift stages)

Upstream main was still at 643e6c21 on 2026-10-15.

| Stage | Content | Status |
| --- | --- | --- |
| A | Nine take-now commits (the DOC `$E0` IRQ-ack fix and eight small ones) | **Done:** `a2gspu` 053a4e14 |
| B | The realistic 65816 reset (three stack false reads, then the vector pull). Keep the fork's force-I=1; it needs an unarmedhash re-bless | Awaiting the owner |
| C | Serial: baud-metered receive (with `uartbaudtest`), DCD, the Windows serial-startup fix | Awaiting the owner |
| D | The clock-domain refactor (event timers onto clock rails; it overlaps 25 fork-modified files). Accept only if unarmedhash comes out unchanged; it gets costlier the longer it waits | Awaiting the owner |
| E | BRAM and the user systems path in `.gs2` (the fork's BRAM pin keeps precedence for automated runs); Open-Apple and Closed-Apple key mapping | Awaiting the owner |
| Skip | `.gs2pack` and cloud storage, the web build, desktop conveniences | Decided: skip |

## 2. Device models

- **`pico2lite_engine`** (local branch `local/slot-engine-model`, 0dd0216d):
  - a slot card's register set: ID, a latched microsecond counter, a programmable IRQ tick with a watchdog, a TRNG
    port, a block device;
  - a bootable ProDOS block-device ROM page;
  - 92 unit checks; 12/12 gates, baselines unchanged.
- **Merging** it into `a2gspu` is the owner's choice.

## 3. Agentic layer: quick wins (Phase 0)

| Task | Why | Size | Gate |
| --- | --- | --- | --- |
| A headless SCC capture rail: bind SCC channel A/B to a file or pipe in spike and CTRL runs | Gate a guest OS's serial console without a window | S | Known-pass: a guest prints a known string. Known-fail: a wrong channel or a missing binding gives an empty capture |
| Counting coverage: per-PC execution counts in `cov`, an `instructions=` field in acks, per-function roll-up through an `A2MAP1` map | Instruction counts and per-function cycle profiles for guest software | S-M | Known counts on a fixed loop; a planted miscount fails |
| A `WDM` debug console: the guest's `WDM` opcode as a print or trace hook (opt-in rail) | printf-style output from guest code with no device | S | A string round-trips; with the rail off, `WDM` stays a NOP |

## 4. Agentic layer: the phased proposal

*Being written (a full inventory of upstream's and the fork's agentic surfaces, the gaps neither has, and the phases).
It'll be summarised here, with the full text in `Docs/reports/`.*
