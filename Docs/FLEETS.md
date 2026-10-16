# Isolated native fleets

`tools/a2rail/fleet.py` runs an explicit matrix of independent owned emulator
sessions. Cells have separate CPU/RAM, socket, staged-input and output roots.
The aggregate reports every cell in plan order, including failures; completed
cells also enter an append-only journal as they finish.

```text
python -I tools/a2rail/fleet.py --plan fleet-plan.json --exe <current-tested-fork> --out <new-directory>
python -I tools/a2rail/fleet.py --schema
python -I tools/a2rail/fleetcheck.py --exe <current-tested-fork> --expect-discovery
```

The native capability catalog advertises the companion and `a2fleet-plan-v1`.
It needs the current fork's typed writeback-policy reader. An older executable
refuses with that requirement; it cannot silently claim the new evidence model.

## Plan

`--schema` emits structural JSON Schema; path/identity/placeholder and UTF8 byte
constraints also have runtime validation. The plan object has exactly
`schema: "a2fleet-plan-v1"`, `workers` (1..8) and
`jobs` (1..64). Each cell declares:

| Field | Meaning |
| --- | --- |
| `id` | Unique case-insensitive safe ID, 1..48 letters/digits/underscore/hyphen; first character alphanumeric |
| `platform` or `config` | Built-in integer selector, or a relative declared input `.gs2` file; choose one |
| `seed` | Explicit integer 1..4294967295 |
| `timeout_seconds` | Native-operation budget 1..600; startup has its separate managed launch deadline |
| `files` | Up to 32 `{source, path, sha256}` rows: absolute source, relative forward-slash staged path, lowercase SHA256 |
| `commands` | 1..64 bounded single-line native commands; all must succeed |
| `assertions` | 1..16 native assertion expressions; the runner supplies `assert ` and requires every result to pass |
| `event_queue_cap` | Optional native producer capacity 1..4096, default 1024 |
| `event_kinds` | Optional unique native filter names, 1..16; discover supported kinds via `MANIFEST` |

`{input:name}` expands to the quoted absolute staged input path.
`{output:name}` expands to a quoted path under the cell's artifacts directory.
Do not add another pair of quotes. Undeclared inputs, escaping outputs, command
control characters, duplicate IDs, missing assertions and invalid bounds refuse
before any cell launches. Each output invocation must name a fresh directory.
The host intentionally supports all native commands; literal external paths
and host endpoints remain explicit caller-controlled shared resources.

For a small real execution cell, `files` can be empty and `commands` can contain
`poke 000800 A9 42 8D 00 40 EA`, `setreg e 1`, `setreg p 34`, `setreg pc 800`,
then `step 2`, with assertion `peek:004000 == $42`. Enumerate the required
platform/seed/config/image cases explicitly. `fleetcheck.py` creates and drives
a real 2×2 platform/seed matrix with independently differing loaded bytes.

## Evidence and failure states

Every cell retains the plan, staged inputs, native command/assertion replies,
before/after context, expanded context snapshot, event JSONL, closed process
receipt, executable identity and artifact digests. Input sources and staged
bytes must still match at closeout. Each cell launches with RAMDISK=1,
NO_WRITEBACK=1, default BRAM and pinned fake time; seed is explicit. Native
readbacks must confirm the RAMDISK/seed and writeback predicate.

The default event subset is `stop`, `irq`, `context`, `scheduler`, `device`.
Cells can explicitly choose other native filters; unsupported filters refuse
at native subscription arm. For long workloads, select relevant filters and
slice execution into commands that fit the declared capture capacities.
Context tracking uses capacity 256. The event file retains at most 4096 records;
the native producer queue and client cap are distinct. Closeout checks native
loss, received/enqueued counts and sequence continuity. Any incomplete event
evidence makes the cell inconclusive, even if its guest assertions passed.
These remain the native subset/sampling models, not exhaustive device transitions.

Assertion failures and native command refusals are reported separately from
inconclusive setup, input identity, transport, event-loss and budget failures.
A timeout is not cancellation. The client waits for the original response;
it never resends a mutation. On budget exhaustion with a pending command it
records unknown effects and terminates only that owned child, then closes the
receipt. Windows socket endpoints left by forced shutdown are classified as
IPC endpoints, not readable payload files. Cleanup cannot replace an unknown
guest outcome with a pass.

The aggregate `fleet.json` is green only when every explicit cell passes.
`completed.jsonl` retains completed-cell evidence if an outer run is interrupted.
These are independent starts, not checkpoint branches or full-machine replay.
Configured external resources can be shared, so this is not a host sandbox.

## Managed media policy

The Python/MCP launcher also accepts `--read-only-media`. It pins RAMDISK and
NO_WRITEBACK at launch and propagates them into its nested batch launches.
Contradictory startup or batch settings refuse. Guest writes can still occur
in emulated backing memory. Defaults without this option are unchanged.

`rail read A2GSPU_NO_WRITEBACK` (or either WRITEBACK spelling) returns
`a2writeback-policy-v1`. One shared predicate now serves that observer and the
spike-end flush itself. Flags test environment presence, even a value `0`;
canonical and legacy suppression are ORed. The payload names all four presence
bits and the effective request/suppression/flush condition. Its scope is the
spike-end flush; explicit save/unmount commands, BRAM and arbitrary device/host
endpoints have their own contracts.
