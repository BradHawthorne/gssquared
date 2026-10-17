# Checked legacy debugger trace export

The automatic instruction-trace exporter opened raw records in text mode and
ignored write/close failures. On this Windows host, the real-stdio reproducer
turns one 40-byte record containing LF/CR/SUB bytes into 43 bytes. Exporting to
a directory still returns success. The same fixture will run against the
committed candidate to pin both corrections.

Export now uses the existing checked binary artifact writer. Open, short-write,
flush and close failures propagate as `a2artifact::Error`; both automatic-save
call sites run inside the existing application-iteration error boundary, which
reports failure and exits 2. No new catch or success path bypasses that boundary.

Twenty native checks cover three CPU types, empty and wrapped/non-wrapped
records, exact bytes/order, unchanged ring state, failed open, injected short
write/flush/close, finalization of empty files and retry from retained records.
The injection argument is native-test-only; production uses stdio operations.
The ring's existing reserved-slot behavior and raw host struct layout remain.

Full trace candidate validation and publication are pending for this local checkpoint.
The protected consumer binary is unchanged. No new rail, verb or MCP tool is
introduced. README capabilities, counts and launch/build recipes were assessed
and remain current; the detailed export contract is updated in
`Docs/UsingTheDebugger.md`.

Failure can leave a partial file. There is no atomic replacement, fsync or
portable/versioned record-format claim. Stock windowed interaction and hardware
validation have not been exercised. Legacy trace input/`gstrace`, monitor
save/load, device-private serializers and asynchronous UI/media flows remain
separate audit slices. In particular, the reader's unchecked text-mode input
and capacity-based decoder loop are not fixed by correcting the exporter.
