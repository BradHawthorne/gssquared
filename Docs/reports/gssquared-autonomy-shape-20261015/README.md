# Autonomous closed-loop feature-shape review — 2026-10-15

This pass asks whether an agent can discover a feature, supply its input,
control its effects, measure the result, retrieve evidence, and recover from a
failure without guessing or repeating a mutation. It inventories every native
verb, then deepens the implementation where the cross-feature workflow broke.

The resulting MCP workflow can upload a digest-checked program, load it from a
filename containing spaces, execute, pass a correct assertion, reject a wrong
assertion, retrieve typed state and a viewable PNG, run a separate cold batch,
consume its evidence, and retain access to artifacts after guest shutdown.
Both IIe and IIgs are fixtures. This is a complete tested workflow, not a claim
that every feature or device is now ideal or exhaustively modeled.

## Inventory and shape criteria

[feature-review.md](feature-review.md) and [feature-review.json](feature-review.json)
assign all **73 native verbs** exactly once to 11 feature families. The
inventory was checked against the current native registry, without omissions
or duplicate assignments. Its scope is interfaces and declared models;
the new deep code/failure fixtures are named below. Existing gate coverage
continues to apply to the other implementations.

Every feature is evaluated against these questions:

| Boundary | An agent needs |
| --- | --- |
| Discovery | Actual operation, model, installed-device availability and limits; a family label alone cannot prove a mixed verb is read-only |
| Input | Unambiguous types, units, bounds, paths and content identity; reject bad requests before effects |
| Action | Deliberate guest execution/mutation, with ownership and preconditions stated |
| Completion | Work performed, stop reason and whether the requested goal was reached; process success is insufficient |
| Evidence | Retrievable bytes/content, digest, capture model and honest completeness/finalization limits |
| Failure | Distinguish refusal, measured mismatch, incomplete work and unknown transport outcome |
| Recovery | Resume the original pending result or inspect evidence; never silently reissue a mutation |
| Reproduction | Identify the executable and pins while naming the state/assets that are not captured |

## Changes and failure evidence

| Gap found | Final behavior | Evidence |
| --- | --- | --- |
| MCP-only agents could issue file-producing commands but lacked input upload and artifact consumption | Three artifact tools provide create-once input upload, scoped bounded listing and content-bound reads. Native PNGs become MCP image content | New artifact tests initially fail because the implementation is absent; complete real MCP loop then passes with wrong hash, duplicate upload and outside-root negatives |
| A native load split quoted filenames at spaces | Shared native `load` uses the existing checked argument tokenizer; missing, malformed and extra arguments fail before writes | `quoted-load-before.log` reproduces open failure at the first space. Both real machines now load the uploaded `program input.bin`; malformed/extra forms refuse |
| File client accepted control characters and published a command before failing | Printable valid UTF-8 is checked before publication; invalid commands create no `cmd.N` | `recovery-before.log`: control-character fixture fails on published command. Final fixture checks all controls before any file publication |
| Socket timeouts discarded partial header/payload bytes and allowed a fresh request over uncertain framing | Partial frames persist; a sent request remains pending; new requests are blocked. Raw/agent wait methods recover its original reply without resending | Before log reproduces reply/event recovery failures. Controlled split-header and split-payload tests, plus real timed-out runs on both machines, recover original evidence and publish no second mutation |
| Unrelated events could restart a stop wait's budget | Stop and agent-record waits share one elapsed deadline | Bounded unrelated-event fixture; partial event framing retained across a timeout |
| `step 0` silently executed one instruction; malformed numeric prefixes were accepted | Run/step/run-until validate complete positive decimal counts, bounds and argument count before arming/clearing CPU state | `execution-before.log` fails at `step 0`; both machines now preserve full observed CPU state and report zero instructions/cycles for all invalid cases |
| `run-until` executed before checking an already-satisfied PC target | It checks the current PC first; already-reached goals do zero work and do not arm/clear CPU state | A known INC instruction at the target must leave its memory effect unexecuted; a subsequent one-attempt run reaches the next target and increments exactly once |
| Execution replies lacked direct completion reasons | Native execution JSON names completed/requested work, target satisfaction and stop reason. Step reports HALT or BUDGET for incomplete work; actual instruction counts exclude idle/interrupt-only attempts | Target reached, exact final-instruction arrival, exhausted budget and valid step fixtures; real recovered frame run remains usable |
| An executable path alone could refer to a later/different binary | Managed session receipts include the selected executable's launch-time SHA256 and native bootstrap proof | MCP retrieves the receipt, and the fixture compares its digest against the selected executable independently |

No mutation retry, implicit pause, embedded guest workflow language or
automatic AI-client configuration was added.

## Current interfaces

MCP now exposes **82 tools**: 73 native verbs, native catalog and cold-batch
adapters, four structured debugger observations, and three artifact adapters.
Native counts remain **73 verbs, 161 rails and 32 wire types**. The live native
capability document also describes the companion's artifact surface.

Artifact contracts are detailed in [DebugClient.md](../../DebugClient.md):

- `gs2_artifact_put`: exactly one UTF-8/base64 input, required SHA256,
  maximum 512 KiB decoded, existing parent directory, atomic create without
  overwrite. Uploading is a host-file effect; loading into the guest is separate.
- `gs2_artifact_list`: one directory-name snapshot, no recursive walk or link
  following; bounded offset pagination. File metadata can subsequently change.
- `gs2_artifact_read`: at most 1 MiB, optional explicit range and expected
  digest. The digest covers returned bytes. Native PNG preview checks CRC,
  chunks, bounded deflate framing and filter tags; text is strict UTF-8 and
  binary can be base64. No claim that a reference raster equals actual GPU
  output. Other image formats remain binary transport.
- Managed output is the default root; attached servers require an explicit
  `--artifact-root`. Artifact-root scope does not constrain native commands'
  separately explicit paths. A read's `complete=true` means the whole file at
  that read, not producer finalization; close sinks before final assertions.
- Artifact access remains available after native QUIT while the adapter is
  alive. Exiting stdio closes only a machine that adapter owns.

Socket recovery is explicit: `RequestPending`, `pending_request`,
`wait_response()` and `wait_agent_command()`. Complete publication with a
partial reply is recoverable. Failed sends/terminal reads close the socket;
disconnect still does not establish cancellation. The client remains
synchronous, with one request/reader owner. The file client retains its
existing lease and pending-sequence `wait()` behavior.

Execution keeps the existing positional commands and native models. Run frames
are 1..1,000,000; step/run-until counts are 1..2,147,483,647. Run-until defaults
to 2,000,000 boundary attempts; step defaults to one. Idle and interrupt-only
attempts consume the bound, and are not counted as executed instructions.
Native `completed`, `stop_reason`, and target/attempt/frame fields are additive
to `a2execution-v1`. The earlier acceptance of zero/negative/malformed counts
and extra arguments was a defect; those calls now fail without executing.

## Remaining shape priorities

1. **Typed operation contracts for mixed native verbs.** Their generic
   `arguments` strings remain discoverable but require grammar knowledge.
   Incrementally attach types, preconditions, effect classes and readback
   schemas to the shared native dispatcher; do not build another implementation
   beside it. Do not mark an entire `observe` family read-only when a subcommand
   arms/resets instrumentation or writes files. Path quoting and SDK range
   handling still need operation-by-operation consistency; only the load and
   execution paths described here were newly hardened.
2. **Producer-bound artifact receipts.** Content hashes and stable reads now
   establish consumed bytes, but finalization must still be proved by capture
   close receipts. Carry producer identity/completion into artifact references
   before claiming a generic read proves finished evidence.
3. **Structured error/recovery classification across every operation.** Native
   verdicts and execution completion are explicit; many operation-specific
   syntax/unavailability details still live in text. Keep measured mismatch,
   unsupported state and unknown transport outcome distinct.
4. **Host presentation and media extensions.** Reference PNGs are agent-visible;
   SDL BMP/physical GUI, host audio mix and actual GPU pipeline verification
   remain separate from these observation models. Upstream's Metal readback
   harness remains relevant to a future renderer port.
5. **Checkpoint/replay, device and concurrency completeness.** Current snapshot
   exclusions, explicit kernel layouts, sampled device events and exclusive
   session ownership still define the limits. Do not infer exact branching,
   complete kernel debugging or multi-writer transactions from these tools.

These priorities are recorded in the roadmap; drift B–E, the engine merge and
consumer-binary replacement remain separate owner decisions. No baselines are
re-blessed, no vendor trees edited, and nothing is pushed.

## Validation

`autonomycheck.py` and `executionshapecheck.py` are part of the foundation
`agentic` gate through `mcpcheck.py`. The gate also executes the new artifact
and transport-recovery unit fixtures without pytest. Before failures, final
logs and the launch/binary receipt are mirrored here.

Final checks:

- **14/14 gates, 14/14 CTests, 18/18 Python tests pass.** The complete workflow,
  exact interrupt/vector/stack/timing cases and publication/recovery negatives
  run inside the foundation gate, without pytest as a gate dependency.
- The first full run passed 13 gates and failed an old fault fixture that
  required `ok=true` for a zero-opcode interrupt entry. That now correctly
  reports BUDGET/incomplete. Updated the fixture to require that result while
  retaining exact vector, stack, cycle and priority assertions; the standalone
  fault and MCP suites then passed, followed by a fresh all-green full matrix.
  Both `first-gates.*` and final `gates.*` are retained.
- All four pinned boot hashes and cycle counts, all 24 hardware verdicts and
  the 1,922-test Mockingboard audit pass without baseline changes.
- Rosetta's differential test passes its existing classification contract:
  103 vectors, 100 identical, three known COP/BRK stop-policy state differences,
  zero cycle-only differences. This is not 103 identical CPU outcomes.
- Preserved executable:
  `D:\gsqs\coherency-20261015\build\GSSquared-autonomy.exe`, SHA256
  `3e56516aedd10e41502cf8b456c073bab50eb7fa061cf8c465675035031a693b`.
  The [launch descriptor](mcp-launch.json) was tested by an actual IIgs stdio
  client: initialize, list 82 tools, native catalog, digest-bound upload/read,
  and owned EOF shutdown with exit zero. `mcp-launch-proof.json` records that
  check; no AI application configuration was edited.
- `validation.json` includes source hashes for the Python client modules.
  The descriptor uses the working source checkout, not a frozen package or
  complete asset/media capsule. Native executable identity is recorded at
  session launch.
- Consumer `build\GSSquared.exe` remains SHA256
  `af7e0bbd72bee417ffddb5e86a00597df280fba904e8b2ed1ff9ad0738e2956c`.
  Earlier preserved executables remain available. All changes are local;
  nothing was committed or pushed in this continuation.

The roadmap/handoff also distinguish historical feature counts from the
current surface, correct the old trace-ring/window conflation, and stop listing
implemented fault/event/source subsets as wholly absent.
