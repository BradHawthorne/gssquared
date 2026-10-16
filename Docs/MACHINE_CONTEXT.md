# Machine context for an exposed architecture

The target is a continuously inspectable and explainable emulated machine.
An agent should discover what exists, observe its state and transitions, follow
available causal evidence, and find the implementation and the remaining gaps.
This interface connects the existing debugger, native inspection, Observatory
signals and Rosetta source maps.

## Native interface

`context` is available through the shared CTRL/socket dispatcher and windowed
co-pilot. `json context ...` wraps its payload for file-rail clients. Socket
clients receive the native payload in `agent_command(...)["data"]`.

| Query | Evidence |
| --- | --- |
| `context overview` | CPU/clock state, installed/observer/signal counts, history status, query discovery and model limits |
| `context topology [offset]` | Core graph and every compiled device ID, installed slots, absent models, observer names, snapshot candidates and implementation sources; eight nodes per page |
| `context device <id>` | Native owner fields and declared controls; unavailable/incomplete state is explicit |
| `context address <hex>` | Side-effect-free byte or null, live page-table metadata, IIgs mapping registers and resolved FPI read/write/shadow destinations where implemented |
| `context irq` | Every IRQ source bit, aggregate CPU line, I flag and sampled pipeline; no arbitrary selection of one pending source |
| `context stop` | Current CPU stop state and most recent retained execution receipt; these identify different boundaries |
| `context pixel <x> <y>` | IIe HGR monochrome source-dot or IIgs SHR source-pixel decode, physical source addresses and implementation references; named reference models, not actual GPU output |
| `context schedule [offset]` | Sorted copies of three EventTimer queues, instance IDs, callback/source labels, due/next times in their own domains; eight entries per queue per page |
| `context on [capacity]` / `off` | Observer activation/deactivation; default off, capacity 1..4096; restarting clears retained events without reusing event IDs |
| `context history [after-id] [count]` | Ordered records, IDs, parent links, payloads, clock stamps, cursor continuation and loss; count 1..8, further bounded by reply bytes |
| `context mark [hex-address decimal-length]` | Mark native CPU/device fields and optionally 1..256 CPU-memory bytes; retain four marks |
| `context changes <mark-id> [offset]` | Typed before/after fields, eight per page; unknown/evicted IDs refuse |
| `context snapshot <file>` | Expanded core/topology/IRQ/scheduler/native-device/signal snapshot at one machine-thread boundary; reference and file share a stamp |

Read queries work over the socket while the machine runs. They execute on the
machine thread without implicit pause or guest execution. Separate calls may
observe different moments. Use one expanded snapshot when sections must share
a boundary. Socket `on`, `off` and `mark` require explicit pause; they change
observer state. `snapshot` writes a host file. Co-pilot retains its existing
frame-boundary model and prohibition of guest-memory/register splices.

## Identity, clocks and evidence

`a2context-v1` has `kind`, `stamp`, `capture_host_ns`, `capture_cost_model` and
`data`. Stamps carry a host-generated context-session token, monotonic
observation ID, clock epoch, native CPU cycles, master 14M ticks, video cycles,
video frames and execution mode. IDs/counters are decimal strings. The epoch
advances when an observed CPU clock moves backward. This detects observed
discontinuities; it is not a replay generation or proof that an unobserved
reset/restore never happened. Managed receipts bind the executable separately.

`capture_host_ns` measures host wall duration through capture/render, excluding
the final envelope, file write and transport. It is observation overhead, not
guest work. No scanline or CPU-cycle conversion assumes a fixed MHz rate.
Continuous sampler/producer overhead is outside this query timer.

History includes central records, command begin/completion, IRQ edges,
interrupt acceptance/resolved vector, timer scheduling/firing/cancellation,
and sampled fields. A completion refers to its actual command begin. A vector
result refers to its actual accepted entry. A timer callback's nested emitted
records refer to that firing. These are dynamic nesting relationships;
temporal adjacency does not establish data-flow causality. Parents may have
been evicted.

File/socket `subscribe` filters also include `context`, `scheduler` and
`interrupt`. Context deltas sample fields at existing command/frame boundaries,
include up to eight changes and report omitted detail. Marks/snapshots expand
the relevant state. Streams can miss transitions between samples; exact
timer/IRQ/interrupt hooks retain their actual boundaries. Context polling is
excluded from command-history feedback.

History is a retained subset. Capacity loss and oversized omissions are
explicit. Payloads above 4096 bytes become omission records, never cut JSON.
Full producer artifacts use existing capture interfaces. An oversized inline
device result refuses with the expanded-snapshot route. A future cursor or
unknown mark fails loudly. Query pages have separate observation stamps.
Expanded snapshot references include a closed-producer receipt, byte length
and explicitly named noncanonical house-FNV content witness. This does not
prevent a later explicit overwrite; consumers can check the witness on read.

## Python and MCP

`gs2debug.context.call(client, name, parameters)` is shared with the stdio server:

```json
{"name":"gs2_context_read","arguments":{"kind":"address","address":49152}}
{"name":"gs2_context_track","arguments":{"action":"mark","address":81920,"length":4}}
{"name":"gs2_context_read","arguments":{"kind":"changes","mark_id":"1","offset":0}}
```

The generated `gs2_context` tool exposes the full native grammar. The current
inventory is 74 native verbs and 85 MCP tools; rail/wire counts stay 161/32.
Typed adapters are advertised only if live discovery includes `context`.

## Rosetta experiments and measurable improvement

`tools/a2rail/experiment.py` is an additive companion. An
`a2experiment-plan-v1` declares digest-bound inputs, optional explicit build
executable/argv lists, platform, **raw** image/load address, entry/target,
instruction-boundary-attempt budget, CPU setup and expected memory bytes.
There is no shell or new Rosetta v3 command. OMF load plans and GS/OS boot
remain their existing workflows; raw splicing does not test those paths.

Each invocation owns a fresh directory and machine. Build receipts bind tools
and inputs. Output memory is poisoned before execution. Native execution must
reach its target after actual instructions, expected bytes must match, and a
deliberately wrong assertion must fail. Bundles retain before/after context,
a digest-bound expanded snapshot, native completion receipts, output bytes and
session/executable identity. A timed-out command may wait for its original
response; mutations are never resent.

```text
python -I tools/a2rail/experiment.py run --plan fixture.plan.json --exe <tested-emulator> --out <new-directory>
python -I tools/a2rail/experiment.py compare baseline/run.json candidate/run.json --out comparison.json
```

Optional declared data loads, preserved inputs/registers, per-PC profiles,
CPU write footprints and versioned CIDAR joins extend this workflow. Their
schemas, refusal conditions, limits and real `_BlockMoveBytes` pilot are in
[`CIDAR_EXPERIMENTS.md`](CIDAR_EXPERIMENTS.md).

Comparison requires passing results for the same workload/expectations,
platform, emulator, pins and clock setup. The CLI rechecks retained image and
snapshot and declared input/observation digests. It reports CPU-cycle/instruction/code-size deltas and native
master-14M/video-counter intervals. The context CPU interval must agree with
the execution receipt, without a session/observed-clock discontinuity. Wrong answers
and incomparable runs cannot win. Results are observed pairs; proven floors,
ceilings and physical-IIgs acceptance are not inferred. Adversarial cases
should name their workload and compare each case before deriving an observed
envelope.

`experimentcheck.py --exe ... [--asmiigs <path>] [--out <directory>]` provides
boundary-input positive/negative controls. With an assembler it executes real
Rosetta builds; the foundation gate uses owned opcodes and needs no consumer
checkout. Its redundant-NOP example proves the measurement mechanism, not a
production-project optimization.

## Remaining exposure gaps

The graph identifies partial/absent observers; every device variable is not
yet typed. Some source references identify the registry rather than a specific
implementation. Address explanations resolve selected FPI mappings and name
delegated Mega-II paths; universal last-writer attribution is open. Scheduling
covers EventTimer queues with callback/source labels on all 35 current device
and display scheduling call sites; unlabeled future clients are explicit nulls.
Device-local timers and host queues need further registration. Pixel explanations cover named HGR/SHR paths;
320 fill history, other legacy modes, actual GPU output and host audio mix
remain separate work. Existing trace/source tools provide instruction and
statement evidence; variables/unwind/DWARF and complete causal data flow are
open. Snapshots/replay retain their exclusions and drift decisions. Physical
hardware remains a separate validation authority.
