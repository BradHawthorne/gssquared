# Exposed machine context and Rosetta efficiency experiments — 2026-10-16

The owner asked for the opposite of a closed architecture: expose the emulated
machine so an agent can understand state, transitions, relationships and model
limitations while developing through Rosetta. The subsequent direction adds
adversarial validation and quantifiable cycle/bit efficiency, with completed
projects ultimately validated on physical IIgs hardware.

This continuation implements the connective layer and a concrete experiment
workflow. It does not claim every device variable, causal dependency or host
presentation path is completely instrumented. The API and remaining gaps are
in [`../../MACHINE_CONTEXT.md`](../../MACHINE_CONTEXT.md).

## Suggestions worked through

| Suggestion | Implemented | Practical limit |
| --- | --- | --- |
| One connected machine model | Core graph, every compiled device ID, installed slots, absent models, observer/control/snapshot declarations and source references | Some devices still lack native fields; fallback source references identify the registry |
| Navigable causality | Command begin/completion, interrupt acceptance/vector, timer callback nesting, actual IRQ edges; all share bounded retained IDs | Parent links are dynamic nesting, not universal data-flow or last-writer proof |
| Shared time/identity | Context session/observation IDs, observed clock discontinuity epochs, native CPU/14M/video/frame counters, one-boundary expanded snapshot | Separate calls/pages can observe different moments; no full replay generation |
| Broad awareness with detail | Live socket reads, overview, paged topology/history/deltas, four marks, optional memory windows, full snapshot, sampled context subscriptions | Retention and sample limits are explicit; no complete transient history |
| Explain emulator behavior | Native address, IRQ, stop and reference-pixel queries; sorted timer queues with callback/source labels on 35 scheduling sites | Legacy video modes, 320 fill history, actual GPU/mix and device-local scheduling remain partial |
| Rosetta closed loop | Digest-bound input/build steps, explicit raw image/setup, native completion, poisoned result checks, failing negative control and context bundle | Raw CPU fixture lane; OS loader/OMF/disk/application lanes retain their existing workflows |
| Quantifiable efficiency | Compatible passing-run comparisons report cycles, instructions and image bytes | Observed pairs do not establish architectural floors, ceilings or real-hardware acceptance |

The current inventory is **74 native verbs / 85 MCP tools**, with **161 rails /
32 wire types**. `context` uses the shared native dispatcher, and typed
`gs2_context_read` / `gs2_context_track` adapters use the same implementation.
The selected executable is built outside the protected consumer directory.

## Evidence and validation

- `before.log`: the new native-context fixture fails against the preserved
  preceding executable because the capability is absent.
- `context-stop-before.log`: the added similarly-named-command fixture exposes
  an incorrect stop-receipt match in the first context build; schema-based
  selection fixes it. Pixel queries now read the current machine's scanner
  directly, including before its first frame, rather than a process-global hook.
- `context-final.log`: dual-platform state, absent-model, no-cycle observation,
  I/O-placeholder refusal, typed changes, IRQ/interrupt causality, scheduler,
  snapshot identity/content, pixels, retention loss, real MCP live reads and
  armed/unarmed instruction/clock witnesses.
  An actual CPU RAMWRT/RAMRD sequence independently checks the address query's
  separate FPI read/write destinations.
- `timer-labels.log`: all 35 scheduling call sites migrated to declared
  callback/source labels. Due times and callback/instance arguments are unchanged.
- The native `interface_contract` test covers timer ordering, cancellation,
  callback labels and an actual nested timer-fire → IRQ edge.
- `python-tests.log`: typed parameter validation, live reads, paused observer
  mutations and the existing transport/artifact/MCP tests.
- `experiments-final.log` and `efficiency-study.json`: real Rosetta assembly of
  baseline/candidate fixtures, boundary bytes `00/7F/80/FF`, IIe and IIgs,
  correctness and deliberate wrong-result controls. Removing one redundant
  NOP measures **8 → 6 CPU cycles**, **3 → 2 instructions**, **6 → 5 image
  bytes**. This is a controlled mechanism demonstration, not a production gain.

`experiments.log` is the earlier exploratory run. The final run also checks
actual wrong-result experiments, prelaunch invalid-plan refusals, clean owned
shutdown, native clock continuity and agreement between context intervals and
execution receipts. The two retained `p5-ff-*-run.json`/`*-context.json` pairs
illustrate the bundle; `p5-wrong-result-*` is a measured failure.

Final build, full-gate, source-pairing and executable identity evidence is
recorded in `validation.json` and accompanying logs:

| Check | Final result |
| --- | --- |
| Release build / CTest | 14/14 native tests pass |
| Full umbrella gates | 14/14 pass; no skips. All four pinned boot hashes, run witnesses and cycle counts unchanged |
| Python client tests | 21/21 pass |
| Real MCP launch descriptor | 85 tools, native context result and graceful owned QUIT pass; `mcp-launch.json` / `mcp-launch-proof.json` |
| Rosetta efficiency | Eight baseline/candidate pairs across IIe/IIgs and four byte-boundary cases; two actual wrong-result runs fail; invalid plans refuse before launch |
| CLI evidence identity | Real retained pair passes; a changed image refuses (`comparison-cli.log`) |
| Real Rosetta C statement/value proof | Byte-bound `$0808` → C line 2 and returned value 66, with context active, on 65816 IIe (p4) and IIgs (p5) |
| Rosetta CPU differential | 103 vectors: 100 identical, the same three named COP/BRK stop-policy differences, zero cycle-only differences |
| Protected consumer / vendored code | Consumer SHA256 unchanged; no vendored edits |

Preserved final executable:
`D:\gsqs\coherency-20261015\build\GSSquared-awareness.exe`, SHA256
`f6d61438eee05e999e72a4411117f64ba24429e244b69f484f286d4bae72ae2f`.
The launch descriptor uses this explicit executable and the current Python
source checkout; it does not install application configuration or claim a
frozen source/media package. Initial build/gate evidence is retained under
`*-initial.*`; the final logs/descriptor/validation supersede those checkpoints.

The retained IIgs pair also measures master-14M ticks **45 → 30**. This and
the CPU-cycle reduction are different quantities; both are native measurements
for the declared fixture and initial clock phase. No fixed-MHz conversion or
production-wide improvement is assumed. Runtime heap/workspace size is not
measured by the raw-image-byte metric.

## Continuing priorities

1. Register the remaining device-local state machines/timers and richer
   field dependencies. Retain null/partial coverage until backed by a model.
2. Extend exact causal hooks to selected bus/data-flow paths and bind their
   instruction/source provenance; never infer last writers from sparse traces.
3. Expand pixel/output explanations with generator history and actual GPU/host
   presentation measurements.
4. Add named adversarial workloads for real products: interrupt/DMA contention,
   bank/shadow configurations, storage/input pacing and timing margins. Compare
   each case and distinguish observed envelopes from demonstrated bounds.
5. Carry those witnesses to physical IIgs hardware. Emulator observations and
   hardware measurements remain distinct evidence classes.

Drift B–E, engine-model merge, baseline re-blessing and replacement of the
in-tree consumer binary retain their separate owner decisions. No push or
remote change is part of this continuation.
