# Agentic layer and upstream sync: the roadmap

> **North star (owner, 2026-10-15):** "my goal is always for our gssquared-fork to be the definitive partner tool for full agentic ai closed loop development along side Rosetta." Every task here is judged by how much it advances that: an agent can build with Rosetta, run and observe in this emulator, and verify, closed-loop and deterministically, without a human in the loop.

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

The full text is in `Docs/reports/gssquared-agentic-proposal-20261015/README.md`: per-task files, sizes, dependencies,
known-pass and known-fail gates, and the home (fork, Rosetta or GSnux).

**The two surfaces, inventoried from code:**
- **Upstream** (643e6c21): `DebugProtocolServer`, a binary protocol over a Unix socket.
  - 27 message types: session, run control, registers, a 16K trace ring, memory read/write/find across six domains,
    device state, typed breakpoints, keys and paste, video text, mount.
  - It pushes "stopped" and "run-state" events. Around it: a Python client, `gstrace` and the in-app debugger.
- **The fork** (053a4e14) is a strict superset:
  - CTRL with 65 verbs, and configurable traps;
  - coverage, `itrace`/`tbuf`, `iolog` and bus watch;
  - Toolbox and GS/OS tracing;
  - transactional snapshots;
  - determinism (seed, fake time, BRAM pin, run hash);
  - about 144 rails, 12 gates, and headless runs.

**The 20 gaps neither has** (G1-G20): SCC capture, counting coverage and profiles, a `WDM` console, Linux kernel
awareness, a 65816 GDB stub, DWARF and `cciigs -g` source maps, record/replay with reverse step, checkpoint branching
and state diffs, fault injection (with NMI and ABORT sources), event streams, fleets, VCD slot-bus export,
hardware-in-the-loop compare, SHR text reading and tolerant image compare, audio assertions, input macros, a
cross-emulator differential, bisect, and a socket manifest.

| Phase | Content (sizes) | Each task needs the owner's go-ahead |
| --- | --- | --- |
| **P0** | SCC capture (S), counting coverage (S-M), `WDM` console (S), socket manifest (S), plus **drift B**, the reset (S-M) | GSnux-critical |
| **P1** | Linux kernel awareness (M), the GDB stub (M), source maps (M), plus **drift C**, serial (M) | GSnux-critical |
| **P2** | **Drift D**, the clock refactor, first (M-L); then state diffs (S-M), branching (M), record/replay with reverse step (L), bisect (S-M) | P2.1 and P2.3 are GSnux-critical |
| **P3** | NMI/ABORT sources (S-M), an interrupt-fault suite (M), card faults (M) | P3.1 and P3.2 are GSnux-critical |
| **P4** | Event streams (M), fleets (M) | P4.2 is GSnux-critical |
| **P5** | VCD export (M), hardware-in-the-loop trace compare (L) | P5.1 is GSnux-critical |
| **P6** | SHR text reading (M), tolerant image compare (S-M), audio assertions (M), input macros (S-M) | P6.1 is GSnux-critical |
| **P7** | Cross-emulator differential (L), plus **drift E** (S). The Fast816 tier belongs to the Fast816 project | |
