# Checked SCC/WDM byte-capture closure

SCC/WDM byte sinks previously ignored fclose/pipe-close errors and bypassed
the managed output registry. This could report successful closure or exit 0
despite a latched stream error. Byte sinks now consume ownership, release
endpoint reservations, check file/pipe finalization and aggregate failures with
the other managed outputs before machine teardown. Destructor/atexit fallbacks
refuse exit 0 after a finalization error.

Twenty-five focused native contract cases pass: valid/empty captures, separate
bytes, managed closure, injected flush/fclose/pipe-close failures, real latched
stdio errors, aggregation, idempotent cleanup and rebinding after failure.
The same real-stdio fixture against `3cec9767` fails direct/shutdown checks and
exits 0 during late/destructor cleanup; the implementation refuses the errors
and late cleanup exits 2. `close_fixture.cpp` retains that reproducer.

Full exported-candidate validation and publication are pending for this local
checkpoint. The protected consumer is unchanged. README capabilities/build
recipes remain applicable; only the detailed rail close contract needs updating.
No new rail, verb, wire type or MCP tool is introduced.

Close failure consumes the old binding; it is not rollback and cannot be
retried against the same handle. A failed WDM disarm has also removed its owned
handlers. Empty streams remain valid. Close does not promise fsync, power-loss
durability, a complete stream after an earlier write failure or pipe-reader
acknowledgement. Legacy debugger traces, device-private serializers and
asynchronous UI/media flows remain separate audit slices.
