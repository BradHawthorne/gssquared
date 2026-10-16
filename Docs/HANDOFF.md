# Handoff: gssquared-fork (2026-10-16)

> **North star (owner, 2026-10-15):** "my goal is always for our gssquared-fork to be the definitive partner tool for full agentic ai closed loop development along side Rosetta." Every task here is judged by how much it advances that: an agent can build with Rosetta, run and observe in this emulator, and verify, closed-loop and deterministically, without a human in the loop.

Read this first, then `Docs/AGENTIC_ROADMAP.md` (the task list) and `Docs/reports/` (the detailed reports).

**GitHub cadence (owner, 2026-10-16):** keep the fork current at coherent task-phase
completion points. Completed, reviewed and validated work may be committed and
fast-forward pushed to the existing `origin/a2gspu` without asking again at each
ordinary closeout. This supersedes the old blanket no-push rule for that scope.
The first catch-up publication is complete: the integrated agent-interface/context/
design checkpoint `bd276467` passed fresh exported-tree validation, was pushed,
and its remote SHA was independently confirmed. This closeout receipt follows
the same commit/sync cadence. Evidence is in
`Docs/reports/gssquared-publication-20261016/README.md`.

**Design direction, 2026-10-16:** instrumentation must support CIDAR-driven
architectural understanding and measured primitive refinement across Rosetta
and this fork. Archive prevalence is not evidence of optimality. Apply
`Docs/AGENTIC_ORACLE.md` section 10 and the paired shared-foundation contracts.
The design-only checkpoint and pending integration slices are recorded in
`Docs/reports/gssquared-cidar-design-20261016/README.md` and the roadmap.

Updated by the coherency audit, the owner's explicit P0.1-P0.4 approval, and the
subsequent task-queue iterations implementing P2.1 and P2.4 on 2026-10-15.
The 2026-10-16 continuation adds exposed machine context and context-bound
Rosetta efficiency experiments. The latest fresh publication build is
`D:\gsqs\pub-20261016\build\GSSquared.exe`, compiled from `bd276467`;
its identity and results are in the publication report's `validation.json`.
The earlier preserved `GSSquared-awareness.exe` and tested `mcp-launch.json`
remain documented in the machine-context report.
The original Claude-to-Codex handoff and the assessment reports are historical evidence; current status is below.

The CIDAR queue continuation adds versioned spec/candidate/source/build/raw
debug-map joins and input/register preservation, per-PC costs and CPU write
footprints to `experiment.py`. A genuine Rosetta `_BlockMoveBytes` invocation
and owned specialization pass 32 dual-platform comparison pairs; invalid
source/image/map/spec bundles refuse before launch. Rosetta curation retains
the measured tradeoffs without changing library instructions. Schemas and
limits: `Docs/CIDAR_EXPERIMENTS.md`; evidence:
`Docs/reports/gssquared-cidar-pilot-20261016/README.md`.

The whole-fork stale-information audit is recorded in
`Docs/reports/gssquared-stale-audit-20261016/README.md`. Historical proposals and
studies now point to implemented interfaces, and `Docs/DEVELOPMENT_SETUP.md`
describes maintained editor configuration, enforced fork-only release targets
and remaining host-package/harness work. The state table's older feature rows retain their checkpoint
counts; the machine-context row and publication report give the current inventory.

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

## 2. State (latest continuation 2026-10-16)

| Item | State |
| --- | --- |
| Branch `a2gspu` | Integrated checkpoint `bd276467` published and independently verified on GitHub, including the previous 68 local commits. Fresh exported-tree Release: 14/14 gates, 14/14 native CTests, 21/21 Python tests, 3/3 affected Rosetta tests. Documentation receipts follow the same phase-end sync; see `gssquared-publication-20261016/` |
| Upstream main | Network refreshed 2026-10-15: `643e6c21`; separate `1.1/ntscshader` development branch at `0aed0214` assessed in the upstream observation report |
| Drift stage A | **Done:** nine upstream commits cherry-picked with `-x` (`7481a130`..`053a4e14`) |
| Drift stages B-E | **Awaiting the owner's approval** (`Docs/AGENTIC_ROADMAP.md` §1) |
| Branch `local/slot-engine-model` | Head `0dd0216d`: the `pico2lite_engine` slot-card model (v1: registers, IRQ tick, TRNG, a bootable ProDOS block device). 92 unit checks. **Not merged into `a2gspu`; merging is the owner's choice** |
| In-tree `build\` | **Stale** (2026-09-17). Rosetta and other consumers of that binary don't see stage A or the model until someone runs `cmake --build build`, which needs the owner's nod |
| P0.1-P0.4 | Owner approved; included in the integrated publication checkpoint. Original Release evidence: ctest 12/12, 13/13 gates, pinned baselines unchanged. See the continuation and publication reports for evidence and usage |
| P2.1 | Complete: CTRL/co-pilot `diff` and standalone `snapdiff`; ctest 13/13, gates 13/13, baselines unchanged. See `gssquared-queue-p21-20261015/` for current evidence |
| P2.4 | Complete: local revision witness search, nine fixture tests, real archived revision builds with ctest 12/12 each and matching repeated IIgs witnesses. Current ctest 13/13 and gates 14/14, baselines unchanged; see `gssquared-queue-p24-20261015/` |
| Reports | Assessment, engine/stage A, crosswalk, completed proposal, P0 coherency report, and P2.1/P2.4 queue reports |
| Interface completeness | Checkpoint 4 in `gssquared-interface-completeness-20261015/`: native state/coverage, semantic companions, live sinks, faults, output assertions, input macros, socket/IRQ/audio/sampled-device streams and real Rosetta C source proof. Ctest 14/14, gates 14/14, baselines unchanged. Full replay, DWARF, real kernel fixtures and manual GUI proof remain explicitly open |
| Tool integration | Native socket bridge shares CTRL dispatch; 73 verbs, 161 rails, 32 wire types, 75 MCP tools. Explicit agent mode reuses CTRL bootstrap with native proof; isolated batches expose one-shot rails. Release ctest 14/14, gates 14/14, Python 7/7, fresh Rosetta CPU differential checked, pinned baselines unchanged. Tested launch descriptor and preserved executable in `gssquared-tool-integration-20261015/` |
| Upstream observations | Four additional structured MCP/Python read adapters bring MCP to 79 tools. Trace text no longer changes when re-decoded or infers scanlines from fixed CPU speed. Final Release ctest 14/14, gates 14/14, Python 10/10, Rosetta CPU differential classification passes; baselines/consumer binary unchanged. Main and development-renderer comparison plus tested launch descriptor: `gssquared-upstream-observability-20261015/` |
| Autonomous feature shapes | 82 MCP tools after scoped artifact upload/list/read with native PNG delivery. Socket partial-reply recovery, file-client publication guards, quoted load paths, validated execution requests and native completion reasons; all 73 native verbs inventoried by shape. Final 14/14 gates, 14/14 CTests, 18/18 Python tests and Rosetta CPU differential classification pass; baselines/consumer binary unchanged. Scope, tested launch descriptor and evidence: `gssquared-autonomy-shape-20261015/` |
| Exposed machine context | 74 verbs / 85 MCP tools. Native synchronized context, compiled/installed/absent topology, live read queries, address/IRQ/stop/reference-pixel explanations, parented events, scheduler labels, marks/deltas and expanded snapshots. Additive Rosetta experiment companion records correctness and native cycle/timing/image-byte comparisons. Final ctest 14/14, gates 14/14, Python 21/21; eight real Rosetta efficiency pairs, C source/value proof on p4/p5 and unchanged CPU differential classification. Pinned baselines/consumer preserved. Evidence, tested launch descriptor and remaining gaps: `gssquared-machine-context-20261016/README.md`; API: `Docs/MACHINE_CONTEXT.md` |

## 3. How to build and verify

- **Build out of tree:** see `Docs/reports/gssquared-assessment-20261015/` for the exact recipe and the 71 s configure
  plus 133 s build.
- **Every change must pass:**
  - ctest (14 with the native-interface contract test; 15 if the separate engine model is later merged);
  - all 14 gates (the original 12 plus `agentic` and offline `bisect`), including unarmedhash, hwtruth and mbaudit;
  - the pinned baselines unchanged, e.g. `iigs-rom` 6,236,744 cycles with run hash `800AEA5075763504`. Re-blessing a
    baseline needs the owner's approval.
- **The ROM 3 Tenspeed and ROM 4 images are git-ignored.** They're present in this tree, but a fresh clone lacks them,
  and the `personality` gate fails without them.
- **Sessions:** `tools\a2rail\session.ps1 -ConfigFile <.gs2>`. Engine-card example:
  `apps/pico2liteenginetest/fixtures/IIgs_Pico2LiteEngine.gs2`.
  That fixture exists on `local/slot-engine-model`, not on `a2gspu`.
- **Out-of-tree gate selection:** set `A2_GSSQUARED` to the tested executable and
  `GSSQUARED_ROOT` to this source checkout. `dualsmoke` now runs ctest beside the
  selected executable, avoiding accidental validation of the stale in-tree build.
- **File-harness process identity:** receipts now bind the executable path and
  exact native UTC start ticks in `runtime.ps1`. Owned renamed executables are
  supported; wrong/missing identities refuse before command publication. Old
  receipts without the new identity fields require a fresh session. The original
  failure and current known-pass/negative evidence are retained in the harness
  queue report. Use `D:\gsqs\pub-20261016\build\GSSquared.exe` for the current
  native implementation; these queue changes are in the host harness.
- **Windows shell:** invoke MSYS2 bash for commands, with UCRT64 tools on PATH for
  this fork's CMake build. Invoke `pwsh -File` from bash for the fork-mandated
  `.ps1` gates; the verbatim upstream shell guidance below AGENTS.md's fork section
  does not override this fork's UCRT64 build and gate recipe.

## 4. The rules

1. **Commit and sync completed phases to the owner's fork.** The owner's
   2026-10-16 direction authorizes ordinary phase-completion commits and
   fast-forward pushes to the existing `origin/a2gspu`, following section 7.
   It supersedes the earlier blanket no-push rule for this scope. Never push
   upstream, add/change remotes, open upstream PRs or rewrite published history
   under this authorization. Other repositories retain their own policies.
2. **Drift stages B-E need the owner's yes each.** Apply them the way stage A was done: cherry-pick with `-x` in a
   scratch clone first, gate, and revert the whole stage if any baseline hash or cycle count changes.
3. **Mirror rule:** every report or task list about this fork goes into `Docs/reports/`, with `Docs/AGENTIC_ROADMAP.md`
   updated in the same change. Naming the private projects (GSnux, a2nux, Fast816) here is OK'd by the owner.
4. **Test-first:** a known-pass and a known-fail case for every new feature or gate (the owner's evidence method).
5. **Python:** use `python -I` with script files, never `python -` (stdin). With WSL, use `MSYS_NO_PATHCONV=1`.
6. **Never open `E:\20251221_archive`**; it holds credential exports. `E:\` in general is a read-only archive.

## 5. Completed continuation and next decisions

- **GitHub catch-up complete.** The interdependent agent interfaces, clients,
  tests and context/design documentation are published as `bd276467` after
  fresh committed-input validation and remote verification. Continue phase-end
  sync; unfinished CIDAR, replay, drift and model work remain named open tasks.

- **P0.1-P0.4 were approved separately from drift and the engine merge:**
  - SCC A/B completed-TX capture to explicit files or existing pipes;
  - `cov counts`, `instructions=` in execution acks, and validated function profiles;
  - a `WDM` console using existing operand handlers (`$FD` strings, `$FE` bytes; Host FST `$FF` preserved);
  - debug-socket `MANIFEST` plus Python client support, including Windows AF_UNIX fallback.
- **Implementation facts corrected by the audit:** the original socket has **28** wire types, now 32 with
  MANIFEST, SUBSCRIBE, AGENT_TOOLS and AGENT_COMMAND; WDM already had a dispatch table; `A2MAP1` is a binary segment plan, so function names require
  an accompanying symbol file. Archived report claims are superseded by the continuation report.
- **Drift stage B,** the realistic reset (it needs an unarmedhash re-bless), then C (serial) and D (the clock refactor,
  only if unarmedhash stays unchanged).
- **The phased agentic proposal** (roadmap §4, `Docs/reports/gssquared-agentic-proposal-20261015/`) is historical.
  Boundary faults, measured event streams, semantic checkpoint companions and source/layout tools now exist.
  Full replay/branching, a 65816 GDB stub, complete kernel/device semantics, VCD export and fleets remain open;
  use the current roadmap statuses rather than reimplementing completed subsets.
- **P2.1 and P2.4 are implemented independently of drift:** snapshot-file state
  diffs and a local revision witness search. The latter builds in scratch,
  checks repeated witnesses and verifies the earlier prefix before naming a
  first changed commit. Build/run failures are inconclusive. The roadmap names
  integration prerequisites for P1; replay/branching still depend on drift D.

## 6. Open questions for the owner

- Merge `local/slot-engine-model` into `a2gspu`?
- Rebuild the in-tree `build\`?
- Approve drift stages B-E?
- Select the P1 kernel/client/debug-info fixtures for the next integration work.
  P0's approval was limited; subsequent queue requests separately implemented P2.1/P2.4.

## 7. Phase-completion commits and GitHub sync

**Policy owner:** this section, reflecting the owner's 2026-10-16 instruction
to bring the fork current at a good task-phase completion point and keep it
current thereafter. Enforcement is agent/reviewer discipline; there is no
scheduler, Git hook or automatic publisher installed by this policy change.
`AGENTS.md` points here; the roadmap owns task status and the reports retain
publication evidence.

1. **Close the phase explicitly.** State what is complete, the tests/evidence,
   known limits and remaining tasks. A phase can finish with later roadmap
   items open. Before ending a session at such a boundary, include commit/sync
   in closeout instead of silently accumulating local-only completed work.
   Assess `README.md` against the completed feature set, launch/build recipes,
   evidence and remaining limits. Update stale claims and linked entry points
   before publication; record that review with the phase evidence.
2. **Review the complete publication scope.** Fetch `origin`; compare its
   `a2gspu` tip with local history, inventory tracked and untracked changes and
   review both outgoing commits and newly selected files. Include required
   source, clients, tests, fixtures, documentation and reviewable evidence;
   respect existing public-repository/reference/disclosure rules. Keep private
   or non-distributable corpus data and transient build/session outputs local.
   Do not use blanket staging to turn the whole dirty tree into a checkpoint.
3. **Make a coherent local checkpoint.** Prefer focused commits where the
   boundaries are independently valid; keep interdependent interface, client,
   tests and discovery changes together when splitting would break the tree.
   Preserve unrelated/in-progress work and identify it in the handoff. Do not
   invent intermediate passing phases from the accumulated working tree.
4. **Validate the exact publishable candidate.** Verify a build from the
   selected tracked inputs and intended dependency/corpus pins so omitted
   untracked files cannot hide behind a successful dirty-tree build. Run the
   applicable build/CTest/client checks and full fork gate stack, including
   both platforms and unchanged baseline witnesses; retain identities and
   results with the candidate commit. Apply affected Rosetta/consumer checks
   where needed. Documentation-only follow-ups can retain the unchanged code
   checkpoint's runtime evidence with appropriately scoped documentation checks.
5. **Sync the completed phase.** Recheck the remote, ensure the push is a
   fast-forward, and use the explicit destination
   `git push origin HEAD:refs/heads/a2gspu`. Never substitute `--all`, `--mirror`
   or force. If origin advanced, inspect and reconcile it, then revalidate the
   changed candidate. Authentication or publication-review failures are real
   blockers to report, not successful syncs. A required history rewrite remains
   a separate owner decision.
   On this host, MSYS2 Git has no usable push credential, while the installed
   Windows Git uses its existing `manager` credential helper. Invoke
   `C:/Program Files/Git/cmd/git.exe` from MSYS2 bash for the push if needed;
   do not copy credentials or change remotes. That path completed this catch-up.
6. **Verify and report.** Query `origin` after the push and confirm its branch
   SHA equals the published local commit. Report the checkpoint, validation,
   remote link and any work intentionally left local. Update the roadmap and
   handoff at each phase closeout; do not describe a merely local commit as
   backed up on GitHub. If no coherent phase is ready, record the concrete
   remaining closeout work and local/remote gap rather than forcing a WIP push.

The initial catch-up is planned in
`Docs/reports/gssquared-github-cadence-20261016/README.md`. Existing separate
approvals for drift B-E, engine merge, baseline re-blessing and consumer binary
replacement remain unchanged by this publication policy.
