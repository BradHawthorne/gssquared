# Agentic layer and upstream sync: the roadmap

> **North star (owner, 2026-10-15):** "my goal is always for our gssquared-fork to be the definitive partner tool for full agentic ai closed loop development along side Rosetta." Every task here is judged by how much it advances that: an agent can build with Rosetta, run and observe in this emulator, and verify, closed-loop and deterministically, without a human in the loop.

**Consumer refresh, 2026-10-16:** separately approved and complete. The in-tree
consumer now builds published `813914fe`: 14/14 gates, 14/14 native tests,
21 client tests, two final-path MCP launches and 42 unchanged boot checks.
The whole September 17 build is preserved for rollback. Source/binary identity,
tested descriptor and scoped C: scratch cleanup:
`reports/gssquared-consumer-refresh-20261016/README.md`.

**Machine-context iteration, 2026-10-16:** the owner's exposed-architecture
direction now has a native `context` interface: synchronized observations,
compiled/installed/absent topology, device/mapping/IRQ/stop/reference-pixel
explanations, EventTimer inspection and callback/source labels, bounded parented
history, sampled live deltas, marks and expanded snapshots. Read queries work
while the socket machine runs. Native dispatch plus two typed adapters bring
the inventory to 74 verbs / 85 MCP tools (161 rails / 32 wire types).
The additive `experiment.py` companion binds builds, poisoned result checks,
negative controls, native execution and context evidence; comparisons quantify
cycles/instructions/bytes without calling observed results proven bounds.
See `MACHINE_CONTEXT.md` and
`reports/gssquared-machine-context-20261016/README.md` for current validation.
Final Release: ctest 14/14, gates 14/14 and Python 21/21. Eight real Rosetta
efficiency pairs, actual wrong-result controls, C statement/value proof on
65816 IIe/IIgs, and the 103-vector CPU classification pass their contracts.
At that checkpoint, pinned baselines and the protected consumer were unchanged. The preserved
`GSSquared-awareness.exe` and tested MCP descriptor are named in that report.
Remaining work is explicit: all device-local fields/timers, universal causal
data flow/last writers, full presentation models, source variables/unwind,
real-hardware witnesses and the existing replay/drift prerequisites.

**The fork's working task list.** Detailed reports are mirrored in `Docs/reports/`. This
file is the tracked summary. The owner's 2026-10-16 direction makes normal
commit/sync part of validated phase closeout; the policy is `HANDOFF.md` section 7.

**Output sink continuation, 2026-10-16:** central record errors propagate;
global/tap/subscription finalization and screenshot/text/coverage/PNG/snapshot
save outputs are checked. Fresh exported Release passes 14/14 gates, 14/14
native CTests, 26 Python tests, 25 output cases, 43 I/O contract checks (24 new),
42 unchanged boots and two MCP launches. Real-stdio fixtures retain two old
false-success cases and their refusals. Consumer unchanged; tested candidate:
`D:\gsqs\sinkio-20261016\build\GSSquared.exe`. The audit explicitly leaves
per-rail closers, miscellaneous exports and ACK publication for the next slice.
Evidence: `reports/gssquared-sink-io-20261016/README.md`.

**Raw artifact I/O continuation, 2026-10-16:** SPIKE, CTRL VRAM and generic
tap raw writers now refuse payload/sidecar I/O failures while retaining valid
empty streams. Fresh exported Release: 14/14 gates, 14/14 native CTests,
26 Python tests, 34 capture cases, 19 I/O contract checks, 42 unchanged boot
checks and two MCP launches. Captures/guest diagnostics match the previous
binary in 26 comparisons. The protected consumer remains unchanged; the
tested candidate is `D:\gsqs\artifactio-20261016\build\GSSquared.exe`.
Scope and evidence: `reports/gssquared-artifact-io-20261016/README.md`.

**MCP close-out continuation, 2026-10-16:** bounded managed QUIT/exit waits and
typed shutdown evidence preserve unresolved requests and owned termination;
attached machines remain controllable. Six dual-platform lifecycle cases and
26 Python tests pass. Final frozen-input validation: 14/14 gates, 14/14 native
CTests, 42 unchanged boot checks and two stdio descriptor launches.
Contract: `DebugClient.md#managed-close-out`; evidence:
`reports/gssquared-mcp-close-20261016/README.md`.

**Fleet continuation, 2026-10-16:** explicit independent matrices, staged inputs,
bounded event closeouts and per-cell assertions now run through `fleet.py`.
Native discovery exposes its structural schema option; a shared typed writeback
predicate supports the managed read-only-media launch mode and nested batches.
Final: 14/14 gates, 14/14 native CTests, 21 client tests, 7 new plan cases,
20 fleet and 53 media checks, two tested MCP descriptor launches and 42 unchanged
boot checks. The source-variant reference map also closes its consolidation
slice without removing compatibility paths. API: `FLEETS.md`; exact build,
retained failures/receipts and limits:
`reports/gssquared-fleets-20261016/README.md`.

**GitHub catch-up complete (2026-10-16).** The integrated checkpoint
`bd276467` is published on `origin/a2gspu`, independently verified by a live
remote query. It includes the previous 68 local commits plus the completed
agent-interface/context/design work. The starting point was GitHub
`ed2c00fc` (2026-09-16), local `834028cd`, and the accumulated dirty tree.
Detailed evidence: `reports/gssquared-github-cadence-20261016/README.md`.
The execution record is `reports/gssquared-publication-20261016/README.md`.

| Closeout task, before another major feature phase | State / completion evidence |
| --- | --- |
| Review outgoing history and complete file inventory for publication | Reviewed: 68 outgoing commits, 960 outgoing blobs and 284 selected changed/new files at the initial inventory; high-confidence credential scan finds no matches; no newly selected binary artifacts |
| Commit the completed agent-interface/context/design checkpoint | Complete: `bd276467`, one integrated 287-file checkpoint preserving native/client/test/discovery dependencies |
| Verify selected tracked candidate and record validation | Complete: fresh exported committed inputs, pristine pinned dependency objects, 14/14 native CTests, 21/21 Python tests, all 14 gates and 3/3 affected Rosetta checks; pinned boot witnesses unchanged |
| Fast-forward sync and verify `origin/a2gspu` | Complete: `ed2c00fc` -> `bd276467`, explicit ordinary push and independent matching remote SHA; documentation receipt follows the same cadence |
| Repeat commit/sync at subsequent coherent phase boundaries | Standing owner-authorized practice; report unsynced completed work or concrete blockers at handoff |
| Assess README freshness after the catch-up and at future phase closeouts | Complete 2026-10-16: current interfaces, Rosetta/CIDAR scope, setup, evidence and limits replace stale claims; adjacent MCP inventory corrected. Documentation-only checks and audit: `reports/gssquared-readme-audit-20261016/README.md`. README assessment is now required by handoff section 7 |
| Audit the whole fork for stale guidance and development assumptions | Complete 2026-10-16: tracked-tree inventory, active/historical doc triage, source/discovery/tooling checks, corrected protocol/snapshot/config claims, editor setup and site navigation. Scope, validation and concrete remaining work: `reports/gssquared-stale-audit-20261016/README.md` |

Publication closes this completed phase; it does not require finishing all
future roadmap items. Next integration work remains in the CIDAR queue below;
the renamed-process harness issue is now closed by the harness queue report.

**CIDAR design direction, 2026-10-16 — first bound pilot implemented.** The
paired shared-foundation contracts now require architectural understanding,
controlled experiments and evidence-driven primitive refinement. The emulator
feature review is `AGENTIC_ORACLE.md` section 10; Rosetta's promotion criteria
live in its `stdlib/primitives/CURATION.md`. Prevalence is not optimality;
historical recovery, candidate synthesis and measured promotion are distinct.
Evidence and acceptance details:
`reports/gssquared-cidar-design-20261016/README.md`.

| Next integration slice | Owner / acceptance evidence | State |
| --- | --- | --- |
| Join CIDAR contracts to experiment evidence | Rosetta/companion: versioned primitive/spec/candidate identities, source/binary lineage and build/load mapping joined to retained execution/context artifacts; wrong image/map/spec binding must refuse | Implemented for owned raw images/absolute Rosetta debug labels: versioned spec/candidate, pinned sources/build/map and retained execution/context/profile/write evidence. Eight substituted source/image/map/spec bundles refuse before launch; historical-binary authentication and OMF-loader mapping remain separate work |
| Pilot a real distilled primitive | Rosetta + emulator client: select a source-grounded primitive, establish a genuine reference invocation and independent expected results, test boundaries/adversarial controls, and compare candidate cycles/bytes plus relevant side effects | Complete scoped `_BlockMoveBytes` pilot: 32 IIe/IIgs pairs, independent data/guard/pointer/register checks and planted omitted-store/clobber/write/loss failures. See `reports/gssquared-cidar-pilot-20261016/README.md` |
| Expand observations from pilot uncertainties | Emulator: add the general missing transition/data-flow/resource channel exposed by the pilot; distinguish observed and inferred causality, check loss and armed/unarmed dual-platform behavior | Implemented the pilot-needed general input/register preservation, per-PC cost and PC-attributed CPU write-footprint companion channels, with explicit windows/loss and armed/unarmed dual-platform proof. Universal data flow, DMA/device transitions and other gaps remain open |
| Close measured findings into curation | Rosetta: preserve counterexamples and workload constraints, refine/fuse/specialize candidate forms, retain useful nondominated variants and revalidate affected callers; hardware claims require hardware witnesses | First feedback complete: Rosetta evidence/INDEX/curation log/acceptance queue record scope and speed/size tradeoffs; false optimality/bank-length claims corrected without changing library instructions. Broader caller/hardware promotion and automatic selection remain open |
| Remove file-harness executable-name assumptions | Fork harness: exact native start ticks and executable path, explicit mismatch/metadata refusals without command publication, renamed executables on both platforms | Implemented; 40 focused checks plus full-stack evidence in `reports/gssquared-queue-harness-20261016/README.md`. Existing receipts without identity fields require a fresh session |
| Bound managed close-out and preserve unknown outcomes | Fork MCP/SDK: bounded QUIT, pending identity before disconnect, only owned children reaped, attached machines left controllable, idempotent receipts | Implemented with six native lifecycle cases and 26 Python tests; full frozen-input validation passes all 14 gates, 14 native tests and 42 pinned boot checks, plus two stdio launches. `reports/gssquared-mcp-close-20261016/README.md` |
| Verify raw artifact I/O failures | Fork artifact producers: missing/failed payload or sidecar writes must fail visibly; a real zero-record artifact remains valid | Implemented for SPIKE, CTRL VRAM and generic tap CPU-view/full-RAM payloads/sidecars: 34 native cases and 19 contract checks, full 14-gate validation, unchanged successful captures and pinned boots. Limits and evidence: `reports/gssquared-artifact-io-20261016/README.md` |
| Audit remaining output sink lifecycles | Fork: inspect structured NDJSON sinks, screenshot/coverage/snapshot finalization and ensure failure receipts cannot imply complete evidence | Audited; central/global/tap/subscription and principal image/coverage/snapshot save paths are hardened with 25 native cases and 24 new contract checks. Per-rail closers, miscellaneous exports and ACK publication remain next. Evidence: `reports/gssquared-sink-io-20261016/README.md` |
| Adapt inherited package-release tooling for this fork | Fork packaging: explicit fork branch/repository, non-fork origins and upload overrides refused, UCRT64 Windows recipe, no network in policy fixtures | Implemented in the stale audit under the owner's explicit no-upstream direction; see report for fixture results. Actual multi-host packages remain unbuilt and unuploaded; ordinary phase sync remains handoff section 7 |
| Generalize retained campaign helpers | Fork tooling: isolated invocation/child directories, staged media/metadata, explicit pins/provider identities, restored caller environment and retained receipts | Implemented; 20 focused scout checks and current gate integration in the harness queue report |
| Consolidate alternate implementation references | Fork source: active Mockingboard library/headers, compatibility declarations and disabled test/reference variants have explicit dependency dispositions before retirement | Complete reference consolidation in `MOCKINGBOARD_IMPLEMENTATIONS.md`; historical notebook points there. Active paths and disabled fixtures are preserved. Any later file removal remains a compatibility migration requiring named consumer evidence |

The implemented schemas, observations and reproduction recipe are in
`CIDAR_EXPERIMENTS.md`; phase evidence is in
`reports/gssquared-cidar-pilot-20261016/README.md`.

Archive-wide ingest remains a long-horizon goal. No bulk download, full GS/OS
build, source fork, compiler default change or physical-hardware claim is part
of this design checkpoint. Existing individually gated decisions still apply.

**Initial tool-access checkpoint:** native AGENT_TOOLS/AGENT_COMMAND share the
CTRL dispatcher; 73 verbs are discoverable and usable through the socket, with
QUIT routed natively. `ui` adds checked presentation/clock settings, and a
stdlib MCP adapter initially supplied 75 tools from live discovery and isolated
batches. Later checkpoints bring the total to 82, then the current 85 above.
`A2GSPU_AGENT` brings the rail inventory to 161 and gives owned socket sessions
the shared CTRL bootstrap, proven native startup receipt and paused ownership.
Managed launch applies interactive rails; one-shot-only rails use batch jobs.
EOF ownership, restartable output roots and long-command result
handling have explicit fixtures. Final Release ctest 14/14 and all 14 gates
passed with pinned baselines unchanged; Python fixtures 7/7 and the Rosetta CPU
differential also pass their stated contracts. The preserved executable and
tested restartable launch descriptor are recorded in the report.
See `Docs/reports/gssquared-tool-integration-20261015/README.md`.

**Upstream observation audit:** network refreshed on 2026-10-15; main remains
`643e6c21`. Reused its structured trace/memory/text protocol through four MCP
adapters, with readable-byte-aware masked search. Corrected stateful extended
trace deltas and removed fixed-speed scanline guesses. The separate
`1.1/ntscshader` branch has newer display rendering, with Metal-only GPU
decoders and no additional telemetry protocol; assessed separately from main.
Validation and remaining drift decisions are recorded in
`Docs/reports/gssquared-upstream-observability-20261015/README.md`.
Final Release checks: ctest 14/14, gates 14/14, Python 10/10 and Rosetta CPU
differential classification pass. Pinned baselines and the consumer executable
are unchanged; a separately preserved executable and tested 79-tool launch
descriptor accompany the report.

**Autonomous feature-shape continuation:** a full native-verb inventory now
tracks interface shape by feature family. This pass closes the MCP-only input
and evidence loop with create-once SHA256 uploads, scoped reads/listing and
native PNG image content. Managed receipts identify the executable bytes.
Socket partial-frame timeouts recover the original result without resending;
file commands reject control characters before publication. Native `load`
accepts quoted filenames, execution validates counts before effects, and an
already-satisfied PC target runs zero instructions. Completion/stop metadata
distinguishes goals from budgets. Evidence and remaining shape priorities:
`Docs/reports/gssquared-autonomy-shape-20261015/README.md`.
Final validation: 14/14 gates, 14/14 CTests, 18/18 Python tests and Rosetta's CPU
differential classification pass. The 82-tool launch descriptor is tested;
consumer executable and pinned baselines remain unchanged. The first gate
run's outdated interrupt-step completion assertion and its corrected evidence
are retained in the report.

**The fork's stance:**
- The owner's agentic emulator layer sits on top of upstream gssquared, and upstream stays vanilla by choice.
- "Drift" means individually assessing upstream changes for integration on our terms (`Docs/UPSTREAM_SYNC.md`).
- Every change is gated: ctest plus the 14 gates (including `agentic` and `bisect`), with the pinned baselines (unarmedhash, hwtruth and the rest)
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

**Owner approved P0.1-P0.4 on 2026-10-15.** Implemented as working-tree changes
after `834028cd`; see `Docs/reports/gssquared-coherency-p0-20261015/README.md`
for validation: Release build, ctest 12/12, all 13 gates green, baselines unchanged.
This approval excludes drift B-E, the engine merge,
and rebuilding the consumer-facing in-tree binary. The latter was separately
approved and completed on 2026-10-16; see the refresh report above.

| Task | Why | Size | Gate |
| --- | --- | --- | --- |
| A headless SCC capture rail: bind SCC channel A/B to a file or pipe in spike and CTRL runs | Gate a guest OS's serial console without a window | S | Known-pass: a guest prints a known string. Known-fail: a wrong channel or a missing binding gives an empty capture |
| Counting coverage: per-PC execution counts in `cov`, an `instructions=` field in acks, per-function roll-up through an `A2MAP1` map | Instruction counts and per-function cycle profiles for guest software | S-M | Known counts on a fixed loop; a planted miscount fails |
| A `WDM` debug console: the guest's `WDM` opcode as a print or trace hook (opt-in rail) | printf-style output from guest code with no device | S | A string round-trips; with the rail off, `WDM` stays a NOP |
| Debug-socket manifest (P0.4): additive MANIFEST request and Python client support | Discover types and framing from the running server | S | Existing 28 types plus MANIFEST are present; a planted omitted type or malformed request fails |

The implementation uses completed SCC TX capture, separate `a2profile-v1`
count artifacts, and existing WDM handlers (`$FD`/`$FE`, leaving Host FST `$FF`).
`A2MAP1` supplies binary segment identity; per-function roll-up takes a symbol
file as well. Full contracts and recipes are in `Docs/A2GSPU_RAILS.md`.

## 4. Agentic layer: the phased proposal

**Interface completeness, 2026-10-15:** the owner requested implementation of
all suggestions from the live black-box audit. The acceptance queue and evidence
are in `Docs/reports/gssquared-interface-completeness-20261015/README.md`.
Work starts with accurate discovery, native typed observations and a per-device
capability matrix, then control, semantic diffs, events, guest meaning, output
assertions and replay. Existing drift/merge/re-bless decisions
remain individually gated; pending work is not represented as complete.

Checkpoint 2 passed ctest 14/14 and gates 14/14 with native memory, the public
file client, byte-bound configured guest views and a real Rosetta assembly
source proof. The next slice adds live capture bindings, installed-device
control/snapshot declarations, semantic checkpoint bundles, generated DOC/SFX
sample capture, pixel/audio/font assertions, boundary NMI/ABORT/IRQ injection,
and cycle/frame input macros. Its current tests and remaining gaps are tracked
in that same report. Full replay is still conditional on separately approved
drift D; C and D evaluation approvals have been requested, not inferred.
Checkpoint 3 passed ctest 14/14 and all 14 umbrella gates, with the pinned
baseline witnesses unchanged. The preserved executable and raw gate evidence
are recorded in the interface-completeness report.

The next slice implements bounded native socket subscriptions, IRQ/audio
fan-out, sampled device changes and active-output shutdown checks. A related
Rosetta fix produces opt-in C statement metadata; a real compiled/linked
function resolves to line 2 and returns 66 on a 65816 IIe and IIgs. Checkpoint 4
passed ctest 14/14 and all 14 gates with unchanged baselines. Rosetta's final
serial non-GUI run passed (375 passes, one declared skip); its unchanged GUI
suite passed in the preceding full run. DWARF, a selected real kernel profile, complete device
transitions and full replay remain explicit open items; C/D approvals remain
pending. See the interface-completeness report for corpus classification and
current evidence.

**Queue iteration, 2026-10-15:** P2.1 (snapshot state diffs) is implemented after
the owner's “iterate task queue” request. It is the next standalone item with
no drift or compiler/kernel fixture dependency. See
`Docs/reports/gssquared-queue-p21-20261015/README.md` for current evidence.
The new CTRL/co-pilot `diff` and standalone `snapdiff` share a checked GSPS v1
reader; snapshot/restore formats remain unchanged. At that checkpoint the test
count was 13; the latest count is 14 without the engine model (15 if merged).

**Next-steps iteration, 2026-10-15:** P2.4 now supplies `revisionbisect.py` /
`bisect.ps1` and a JSON scratch-build scenario. It preserves the source checkout,
records input/dependency bytes, repeats strict witnesses, and verifies the
earlier first-parent prefix before naming the first change. See the P2.4 report
for real-history and current gate evidence. The umbrella now has 14 gates;
the new offline `bisect` gate builds tiny fixtures and needs a compiler/Ninja.

| Queue item | State / next action |
| --- | --- |
| P0.1-P0.4 | Complete; historical validation in the P0 report |
| P2.1 state diffs | Complete: ctest 13/13, gates 13/13; synthetic refusals and live IIe/IIgs comparisons, pinned baselines unchanged |
| P1.1 kernel awareness | Configured tasks/log/frame-chain/panic readers implemented with checked synthetic and live-memory fixtures; select a real kernel/layout/symbol fixture before claiming Linux integration |
| P1.2 GDB stub | Define the 65816 register/target description and choose a working client fixture before implementation |
| P1.3 source maps | Bound Rosetta assembler and C statement maps implemented; real compiled/linked C proof passes on 65816 IIe/IIgs. DWARF and source-text/variable/unwind verification remain open |
| P3 boundary faults | Scheduled NMI/ABORT/owned IRQ sources and instruction-boundary vector/stack/timing fixtures pass. Mid-bus ABORT rollback is outside this model |
| P4.1 event streams | File/co-pilot and bounded socket native streams pass; IRQ/audio and command/frame-sampled device changes included; exhaustive transient device events remain open |
| P4.2 independent fleets | Implemented explicit platform/seed/config/image command matrices, isolated owned sessions and staged inputs, assertions, bounded event closeout and per-cell/aggregate verdicts. Current phase validation/evidence: `reports/gssquared-fleets-20261016/README.md`. These are independent starts, not checkpoint branches |
| P6 output/input tools | Configured SHR glyphs, tolerant PNG comparisons, generated DOC/SFX samples and cycle/frame agent-input macros implemented and gated; physical host mix/input and full-machine replay are outside these models |
| P2.4 bisect | Complete: scratch-only revision builds, strict repeated witnesses, verified earlier prefix, inconclusive failures. Nine fixture tests, real-history no-change case, current ctest 13/13 and gates 14/14; see `gssquared-queue-p24-20261015/` |
| Protected consumer build | Separately approved and refreshed on 2026-10-16; full validation and whole-build rollback in `reports/gssquared-consumer-refresh-20261016/README.md` |
| Drift B-E / engine merge | Still separate owner decisions; no baseline re-bless or model merge |

The full text is in `Docs/reports/gssquared-agentic-proposal-20261015/README.md`: per-task files, sizes, dependencies,
known-pass and known-fail gates, and the home (fork, Rosetta or GSnux).

**Historical surface inventory:** checkpoint 4 added native typed interfaces
and subscriptions at 72 verbs, 160 rails and 30 socket types. The later
continuations above bring current discovery to 74 native verbs, 161 rails,
32 socket types and 85 MCP tools, with 14 umbrella gates.
- **Upstream** (643e6c21): `DebugProtocolServer`, a binary protocol over a Unix socket.
  - 28 message types (the proposal's 27 was a counting error): session, run control, registers, a 100,000-record trace ring (16,384-record maximum request window), memory read/write/find across six domains,
    device state, typed breakpoints, keys and paste, video text, mount.
  - It pushes "stopped" and "run-state" events. Around it: a Python client, `gstrace` and the in-app debugger.
- **The fork's agent-control additions** at the audited `053a4e14` checkpoint:
  - CTRL with 66 verbs, and configurable traps (the proposal's 65 was stale);
  - coverage, `itrace`/`tbuf`, `iolog` and bus watch;
  - Toolbox and GS/OS tracing;
  - transactional snapshots;
  - determinism (seed, fake time, BRAM pin, run hash);
  - 157 rails at the audited base (160 after P0), 12 gates at that base (13 after P0), and headless runs.

**The proposal's original 20 gaps** (G1-G20; P0, P2.1 and P2.4 are now implemented): SCC capture, counting coverage and profiles, a `WDM` console, Linux kernel
awareness, a 65816 GDB stub, DWARF and `cciigs -g` source maps, record/replay with reverse step, checkpoint branching
and state diffs, fault injection (with NMI and ABORT sources), event streams, fleets, VCD slot-bus export,
hardware-in-the-loop compare, SHR text reading and tolerant image compare, audio assertions, input macros, a
cross-emulator differential, bisect, and a socket manifest.

| Phase | Content (sizes) | Approval / priority |
| --- | --- | --- |
| **P0** | SCC capture (S), counting coverage (S-M), `WDM` console (S), socket manifest (S); **drift B** remains a separate decision | P0.1-P0.4 approved and implemented; drift B unapproved |
| **P1** | Configured kernel readers and Rosetta assembly/C maps implemented; real kernel integration, DWARF and GDB stub remain; **drift C**, serial (M) | GSnux-critical; C unapproved |
| **P2** | State diffs (P2.1) and local bisect (P2.4) implemented independently; **drift D** remains required before timing-heavy replay/branching; branching (M), record/replay (L) remain | P2.1/P2.4 implemented; P2.3 GSnux-critical |
| **P3** | Boundary NMI/ABORT/IRQ sources and fixtures implemented; existing card fault rails retained; mid-bus rollback excluded | P3.1/P3.2 boundary model gated |
| **P4** | Native event subset and independent fleets implemented; complete device transition coverage remains | P4.2 provides explicit matrix/aggregate contracts; full replay/branching remains gated |
| **P5** | VCD export (M), hardware-in-the-loop trace compare (L) | P5.1 is GSnux-critical |
| **P6** | Configured SHR text, tolerant image/audio assertions and agent-input macros implemented; physical host state and complete replay excluded | Model-specific positive/negative fixtures gated |
| **P7** | Cross-emulator differential (L), plus **drift E** (S). The Fast816 tier belongs to the Fast816 project | |
