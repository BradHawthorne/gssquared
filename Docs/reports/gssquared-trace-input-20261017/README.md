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

Full trace-input candidate validation and publication are pending for this local checkpoint.
Protected consumer unchanged. README capabilities, counts and launch/build
recipes were assessed and remain current; the offline-decoding contract is
documented in `Docs/UsingTheDebugger.md`. No new emulator rail, verb or MCP tool.

Raw records retain the host struct layout and require the caller's CPU choice.
Headerless files cannot reveal missing whole records, a wrong same-sized
layout, or a concurrent writer's internally mixed capture. Successful loading
does not establish complete trace provenance. CLI output can be partial on
failure. No windowed interaction or physical-hardware validation is claimed.
Monitor save/load, device-private serializers and asynchronous UI/media flows
remain separate audit slices.
