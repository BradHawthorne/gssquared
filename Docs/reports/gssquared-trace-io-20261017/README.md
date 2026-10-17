# Checked legacy debugger trace export

The automatic instruction-trace exporter opened raw records in text mode and
ignored write/close failures. On this Windows host, the real-stdio reproducer
turns one 40-byte record containing LF/CR/SUB bytes into 43 bytes. Exporting to
a directory still returns success. The same fixture against the committed
candidate preserves all 40 bytes and refuses the blocked output with exit 2.

Export now uses the existing checked binary artifact writer. Open, short-write,
flush and close failures propagate as `a2artifact::Error`; both automatic-save
call sites run inside the existing application-iteration error boundary, which
reports failure and exits 2. No new catch or success path bypasses that boundary.

Twenty native checks cover three CPU types, empty and wrapped/non-wrapped
records, exact bytes/order, unchanged ring state, failed open, injected short
write/flush/close, finalization of empty files and retry from retained records.
The injection argument is native-test-only; production uses stdio operations.
The ring's existing reserved-slot behavior and raw host struct layout remain.

Fresh exported Release: 14/14 gates, 14/14 native CTests, 26 Python tests,
112 I/O contract checks (20 new), 42 unchanged boots and two MCP launches.
Protected consumer unchanged; tested candidate:
`D:\gsqs\traceio-20261017\build\GSSquared.exe`.
No new rail, verb or MCP tool is introduced. README capabilities, counts and
launch/build recipes were assessed
and remain current; the detailed export contract is updated in
`Docs/UsingTheDebugger.md`.

Failure can leave a partial file. There is no atomic replacement, fsync or
portable/versioned record-format claim. Stock windowed interaction and hardware
validation have not been exercised. Legacy trace input/`gstrace`, monitor
save/load, device-private serializers and asynchronous UI/media flows remain
separate audit slices. In particular, the reader's unchecked text-mode input
and capacity-based decoder loop are not fixed by correcting the exporter.
The 112 named I/O helper checks run inside `interface_contract`; its top-level
assertion counter is separate. The [validation receipt](validation.json),
[exact build inputs](build-inputs.json), [fresh build](build-transcript.log),
[gate log](gates.log), [gate events](gates.ndjson), [native tests](native-tests.log)
and [Python tests](python-tests.log) retain the results. The [reproducer runner](fixture-check.py)
and [results](fixture-checks.json) bind the same fixture to both implementations.
The [MCP descriptor](mcp-launch.json) and [owner receipts](launch-proof.json)
cover EOF and explicit QUIT. No ROM/media payloads or executables are published.

Tested source commit: `db868e65c881a933b4f40033b3240d6987ba0d47`.
Candidate SHA256: `8c711013426771a1ab7b69eaf422071480815cd6c11fdcc635589bba7dfcb12d`.
Protected consumer SHA256: `364c78f7402381b1b323977293a492336b633712e22dd3f51ffd8b815684991e`.
