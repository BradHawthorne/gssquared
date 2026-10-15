# Handoff: gssquared-fork (2026-10-15)

From the Claude session to GPT Astra, for the few days until Claude's weekly quota refreshes. Read this first, then
`Docs/AGENTIC_ROADMAP.md` (the task list) and `Docs/reports/` (the detailed reports).

## 1. What this repo is

The owner's agentic emulator layer on top of upstream gssquared, an Apple II/IIgs emulator.
- **Upstream stays vanilla by choice:** never upstream anything, never open PRs.
- **"Drift"** means individually assessing upstream changes for integration on our terms (`Docs/UPSTREAM_SYNC.md` is
  the ledger).
- **It's the behavioural oracle** for the owner's IIgs projects:
  - Rosetta v3, the owner's 65816 toolchain at `D:\projects\toolchain\rosetta_v3`, whose differential tests drive this
    emulator;
  - GSnux, mainline Linux bare metal on the IIgs's 65816, at `D:\projects\gsnux`;
  - a2nux, p2nux on the A2Fusion card, at `D:\projects\a2nux`;
  - Fast816, an FPGA 65816 replacement, at `D:\projects\fpgaiigs` (its rails here are named `memvu`).
- **The remote is public** (GitHub fork), only because a fork can't be made private.

## 2. State (2026-10-15)

| Item | State |
| --- | --- |
| Branch `a2gspu` | Head `9a62b013`, clean. About 66 commits ahead of `origin/a2gspu`; **nothing pushed** |
| Upstream main | `643e6c21`, unchanged since the assessment |
| Drift stage A | **Done:** nine upstream commits cherry-picked with `-x` (`7481a130`..`053a4e14`) |
| Drift stages B-E | **Awaiting the owner's approval** (`Docs/AGENTIC_ROADMAP.md` §1) |
| Branch `local/slot-engine-model` | Head `0dd0216d`: the `pico2lite_engine` slot-card model (v1: registers, IRQ tick, TRNG, a bootable ProDOS block device). 92 unit checks. **Not merged into `a2gspu`; merging is the owner's choice** |
| In-tree `build\` | **Stale** (2026-09-17). Rosetta and other consumers of that binary don't see stage A or the model until someone runs `cmake --build build`, which needs the owner's nod |
| Reports | `Docs/reports/`: the assessment, the engine model plus stage A, the upstream crosswalk, and the agentic gap proposal (being finished) |

## 3. How to build and verify

- **Build out of tree:** see `Docs/reports/gssquared-assessment-20261015/` for the exact recipe and the 71 s configure
  plus 133 s build.
- **Every change must pass:**
  - ctest (13/13 with the engine model, 12/12 without);
  - all 12 gates, including unarmedhash, hwtruth and mbaudit;
  - the pinned baselines unchanged, e.g. `iigs-rom` 6,236,744 cycles with run hash `800AEA5075763504`. Re-blessing a
    baseline needs the owner's approval.
- **The ROM 3 Tenspeed and ROM 4 images are git-ignored.** They're present in this tree, but a fresh clone lacks them,
  and the `personality` gate fails without them.
- **Sessions:** `tools\a2rail\session.ps1 -ConfigFile <.gs2>`. Engine-card example:
  `apps/pico2liteenginetest/fixtures/IIgs_Pico2LiteEngine.gs2`.

## 4. The rules

1. **Never push, never add or change remotes,** and never open upstream PRs. Pushing is the owner's call. Local commits
   are fine for approved work.
2. **Drift stages B-E need the owner's yes each.** Apply them the way stage A was done: cherry-pick with `-x` in a
   scratch clone first, gate, and revert the whole stage if any baseline hash or cycle count changes.
3. **Mirror rule:** every report or task list about this fork goes into `Docs/reports/`, with `Docs/AGENTIC_ROADMAP.md`
   updated in the same change. Naming the private projects (GSnux, a2nux, Fast816) here is OK'd by the owner.
4. **Test-first:** a known-pass and a known-fail case for every new feature or gate (the owner's evidence method).
5. **Python:** use `python -I` with script files, never `python -` (stdin). With WSL, use `MSYS_NO_PATHCONV=1`.
6. **Never open `E:\20251221_archive`**; it holds credential exports. `E:\` in general is a read-only archive.

## 5. What's ready to do next (each needs the owner's go-ahead)

- **Phase 0 quick wins** (`Docs/AGENTIC_ROADMAP.md` §3), all small and useful for GSnux right away:
  - a headless SCC capture rail;
  - counting coverage with `instructions=` and per-function roll-up;
  - a `WDM` debug console.
- **Drift stage B,** the realistic reset (it needs an unarmedhash re-bless), then C (serial) and D (the clock refactor,
  only if unarmedhash stays unchanged).
- **The phased agentic proposal** (roadmap §4, `Docs/reports/gssquared-agentic-proposal-20261015/` when it lands): gaps
  neither upstream nor the fork covers, like record-replay, checkpoint branching, a 65816 GDB stub, kernel-aware
  debugging, fault injection, event streams, VCD bus export and fleets.

## 6. Open questions for the owner

- Merge `local/slot-engine-model` into `a2gspu`?
- Rebuild the in-tree `build\`?
- Approve drift stages B-E?
- Which proposal phases to start?
