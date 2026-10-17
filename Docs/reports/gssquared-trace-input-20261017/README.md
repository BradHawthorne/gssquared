# Checked legacy trace input and decoding

The reader opened raw records in text mode, ignored input errors and left the
ring's loaded count unchanged. `gstrace` then decoded every physical slot.
The old executable emits at least 100,000 rows and exits 0 for valid two-record,
empty, missing, partial and oversized inputs, a missing requested label file,
and a command with no CPU selection. The fixture also reproduces binary input
corruption. Only row counts are retained from baseline decoding, because its
unused slots contain uninitialized host memory.

Loads now use bounded binary input, reject partial/oversized records, check
read/EOF/close, and publish ring entries and indices only after complete success.
The maximum is the live ring's usable capacity (`size-1`); the CLI accepts at
most 99,999 records. Empty files replace old state with a valid empty trace.
Append, wrapped export/reload and retry after refusal preserve ring semantics.

The CLI decodes only loaded entries. It requires exactly one CPU selection,
rejects missing/duplicate option operands and missing requested labels, and
checks stdout writes and finalization. Syntax errors exit 1; input/output errors
exit 2. The existing label-count summary and permissive VICE label parser remain.

Twenty-nine native checks cover three CPU types, empty/binary/wrapped loads,
bounds, truncated records, failed reads/probes/closes and state preservation.
Twenty-one CLI cases cover actual decoding/order, capacity limits, syntax,
labels, refused inputs and real read-only stdout. The CLI fixture generator
uses the native host struct layout via `interfacecontracttest`; these checks
run in the regular `agentic` gate. Read-operation injection is native-test-only.

Fresh exported Release: 14/14 gates, 14/14 native CTests, 26 Python tests,
141 I/O contract checks (29 new), 21 integrated CLI checks, 42 unchanged boots
and two MCP launches. Protected consumer unchanged; tested candidate:
`D:\gsqs\tracein-20261017\build\GSSquared.exe`.
Protected consumer unchanged. README capabilities, counts and launch/build
recipes were assessed and remain current; the offline-decoding contract is
documented in `Docs/UsingTheDebugger.md`. No new emulator rail, verb or MCP tool.

Raw records retain the host struct layout and require the caller's CPU choice.
Headerless files cannot reveal missing whole records, a wrong same-sized
layout, or a concurrent writer's internally mixed capture. Successful loading
does not establish complete trace provenance. CLI output can be partial on
failure. No windowed interaction or physical-hardware validation is claimed.
Monitor save/load, symbol-file read/close checks, device-private serializers and
asynchronous UI/media flows remain separate audit slices. The label parser's
permissive syntax and unchecked text read/close are not hardened by the raw
record reader; this phase only refuses labels that fail to open.


The 141 named I/O checks run inside `interface_contract`; its top-level counter
is separate. The [validation receipt](validation.json), [exact build inputs](build-inputs.json),
[fresh build](build-transcript.log), [gate log](gates.log), [gate events](gates.ndjson),
[native tests](native-tests.log) and [Python tests](python-tests.log) retain the results.
[Baseline results](baseline-checks.json), [baseline runner](baseline-check.py),
[old reader result](baseline-reader.log) and [candidate result](candidate-reader.log)
bind the same [native fixture](input_fixture.cpp) to both reader implementations.
The [MCP descriptor](mcp-launch.json) and [owner receipts](launch-proof.json)
cover EOF and explicit QUIT. No ROM/media payloads or executables are published.

The [first gate attempt](initial-failed-gates.log) passed 13 gates and refused
dual-smoke before its exec-control session because a reused global scratch
directory recorded a live PID. The recorded PID had gone by [read-only inspection](refusal-inspection.json);
PID reuse is plausible but not proven. No process was altered or killed. The
full unchanged candidate was rerun with phase-private `TEMP`/`TMP`/`TMPDIR`
and `A2RAIL_DIR` and passes all 14 gates. The [initial matrix](initial-failed-gates.ndjson)
and [initial native tests](initial-native-tests.log) are retained. Earlier progress
updates incorrectly counted completed gates as passing; the final matrix caught
that reporting mistake, and this receipt uses explicit outcomes.

Tested source commit: `df5be98fe57a488087d059d5efa97bc2a9f94a06`.
Candidate SHA256: `b18e3db38d1e18f4fd4e3d54216c08528a2de93c0d5d09c5c5887f78ad383923`.
Protected consumer SHA256: `364c78f7402381b1b323977293a492336b633712e22dd3f51ffd8b815684991e`.
