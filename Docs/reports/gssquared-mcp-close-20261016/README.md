# MCP close-out and unresolved outcomes — 2026-10-16

Managed close-out now bounds its QUIT reply and exit waits, preserves unresolved
request identity and records forced termination. Attach still only disconnects.
The native binary and protected consumer build are unchanged.

Six focused native lifecycle cases on IIe/IIgs and 26 Python tests pass.
Full validation from frozen candidate inputs is complete: **14/14 gates**,
**14/14 native CTests**, **26 Python tests**, **42 unchanged pinned boot checks**
and **two tested stdio launches**. The contract is
[managed close-out](../../DebugClient.md#managed-close-out).

The initial suspicion of a skipped reap was falsified: existing `RequestPending`
is a `TimeoutError`, and therefore an `OSError`, already caught by the old close
path. Both old pending cases reaped their child. Their real gaps were missing
unknown-outcome metadata and an unbounded closing QUIT reply. Known-pass and
known-fail observations are retained before fixture emergency cleanup.

The SDK keeps `quit()` compatible and adds optional `quit(timeout=seconds)`.
MCP close uses a two-second QUIT reply budget, a ten-second acknowledged exit
wait, or a two-second exit wait when a request is unresolved. If needed, it
kills only its own `Popen` child and waits up to five seconds. Individual wait
budgets do not impose a hard deadline on host scheduling or receipt filesystem
I/O. It never retries a pending mutation or sends replacement QUIT over it.
Cleanup begins after the synchronous handler returns to EOF/error handling;
these close budgets do not interrupt a blocked tool or impose a deadline on
every tool call.

`mcp-session.json` adds a nullable `shutdown` object with schema
`a2mcp-shutdown-v1`: pending sequence/type, unknown effects, QUIT attempt/reply,
transport close, owned termination, outcome and caught errors. `closed=true`
still only means the owned process exited. It establishes neither guest
correctness nor complete artifacts. Repeated close preserves the final receipt.

The native fixture covers graceful close, pending close and attach/reconnect
on both platforms. Controlled Python peers cover missing QUIT replies and
no resend; child doubles cover forced reap, already-exited pending work and
unanswered closing QUIT. No physical-hardware result is claimed.

The first expanded attach fixture accidentally supplied a launch seed to
attach; validation correctly rejected it. The fixture now clears all launch
options, and its owned emergency cleanup retained the failure before rerun.

Remaining work includes raw artifact I/O failure handling, phase-aware waveform
capture, replay, real kernel/debug-info fixtures and separately approved drift.

## Frozen inputs and retained evidence

The host changes were tested from `D:\gsqs\mcpclose-20261016\src`, exported
from `a9766740` plus reviewed SHA-256-bound overlays and eight pinned local
inputs. All changed implementation bytes still match that export. This is a
host-only change: native source matches the existing compiled checkpoint
`813914fe`, and the selected consumer executable remains
`364c78f7402381b1b323977293a492336b633712e22dd3f51ffd8b815684991e`.
Native CTests were selected beside that executable. No native rebuild or
protected consumer replacement occurred in this phase. Later documentation
closeout edits do not alter the frozen implementation.

Exact inputs and results: [build inputs](build-inputs.json),
[validation](validation.json), [full gate transcript](gates.log),
[gate events](gates.ndjson), [frozen Python tests](python-tests.log).
The [tested MCP descriptor](mcp-launch.json) selects this frozen host adapter
and the unchanged consumer. [Two launch receipts](launch-proof.json) and the
[launch transcript](launch-proof.log) distinguish EOF close from an explicit
native QUIT. Both exit zero, acknowledge QUIT, avoid forced termination and
report no unresolved close-stage effects. Restart an existing adapter process
to load the updated Python implementation.

Known-pass/fail and corrected-fixture evidence:

- [Original native lifecycle transcript](lifecycle-before.log) and
  [pre-cleanup observations](lifecycle-before.json): two graceful passes;
  two failures for absent shutdown metadata. Both pending children were
  already reaped by the original implementation.
- [Original Python failures](unit-before.log): four new refusal/evidence cases
  fail against the previous SDK, while its 21 existing tests pass.
- [Corrected native lifecycle transcript](lifecycle-after.log) and
  [pre-cleanup observations](lifecycle-after.json): all six cases pass,
  including attached machines remaining alive and controllable.
- [Rejected attach-fixture attempt](fixture-invalid-attach.log) and
  [cleanup observations](fixture-invalid-attach.json): a launch seed
  was incorrectly supplied to attach; the native case did not run, and the
  fixture's owned emergency cleanup ran before correcting the test inputs.

The new lifecycle fixture runs in the foundation's agentic gate, and its five
transport/child-doubled cases join the existing MCP fixture runner. Inventory
remains 74 native verbs / 85 MCP tools / 161 rails / 32 wire types. The README
was assessed and updated for bounded close and retained unknown outcomes.
The manual stock-windowed check remains unexercised; this phase changes no
native source, binary or default interactive behavior. Drift/engine/re-bless
decisions and future raw-artifact I/O checks remain separate work.
