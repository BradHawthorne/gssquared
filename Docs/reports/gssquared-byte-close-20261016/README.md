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

Fresh Release validation passes **14/14 gates**, **14/14 native CTests**,
**26 Python tests**, **92 I/O contract checks** (25 new), **42 unchanged boot
checks**, eight baseline/candidate reproducer checks and two MCP launches.
The tested source commit is `9393f114b442c5e793f4430e038ac0efc30fe861`; the protected
consumer is unchanged. README capabilities/build
recipes remain applicable; only the detailed rail close contract needs updating.
No new rail, verb, wire type or MCP tool is introduced.

The 92 named I/O subchecks run inside `interface_contract`; its top-level
assertion counter is separate from those helper counts.

Close failure consumes the old binding; it is not rollback and cannot be
retried against the same handle. A failed WDM disarm has also removed its owned
handlers. Empty streams remain valid. Close does not promise fsync, power-loss
durability, a complete stream after an earlier write failure or pipe-reader
acknowledgement. Legacy debugger traces, device-private serializers and
asynchronous UI/media flows remain separate audit slices.


The [validation receipt](validation.json), [committed build inputs](build-inputs.json),
[fresh build](build-transcript.log), [gates](gates.log), [gate events](gates.ndjson),
[native tests](native-tests.log), [focused contracts](focused-contract.log) and
[Python tests](python-tests.log) retain the evidence. The [reproducer results](fixture-checks.json)
and [runner](fixture-check.py) bind old header identities to the same fixture.
The [tested MCP descriptor](mcp-launch.json) and [owner receipts](launch-proof.json)
cover EOF and explicit QUIT. All payloads/builds remain local under
`D:/gsqs/byteclose-20261016`; no private ROM/media bytes are published.

The first fixture-runner assertion incorrectly required a destructor close log
after `std::exit`, which bypasses local C++ destructors. Its [diagnostic](fixture-check-initial.log)
is retained; correcting the assertion required no implementation change.
The four baseline error modes and four candidate refusals then pass the runner.

Native candidate SHA256: `fb720528ffea0e2cf88bb1a74ff72822011d2e32ddaa11ca0b322aaaf2fba852`.
Protected consumer SHA256: `364c78f7402381b1b323977293a492336b633712e22dd3f51ffd8b815684991e`.
