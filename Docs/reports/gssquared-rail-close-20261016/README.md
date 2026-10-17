# Per-rail finalization, checked exports and ACK publication

The file rail could silently lose an acknowledgement, replace an existing
receipt, or report success after selected output errors. Per-rail shutdown
also ignored latched stream failures. This phase closes those paths without
changing guest execution, record layouts or the discovery inventory.

Final frozen-input validation passes **14/14 gates**, **14/14 native CTests**,
**26 Python tests**, **18 ACK checks**, **42 export checks**, **67 I/O contract
checks** (24 new), **42 unchanged boot checks** and **two MCP launches**.

The owned-output registry closes registered per-rail streams and container
finalizers while the machine clock remains valid. It attempts all registered
closures, consumes failed handles, aggregates errors and exits 2 on failed
shutdown. Live disarm/replacement errors return a failure through the shared
dispatcher; already applied effects are not rolled back. Trap clearing removes
the selected traps even when their old output fails to close. Legacy title
trap ownership is preserved under the standing exception.

ACK publication checks the entire temporary-file write, flush, close and
rename. An existing final receipt is refused and preserved. Publication
failure reports `ack-publication`, sequence and `effects=may-have-occurred`,
then exits 2. It cannot acknowledge its own failure through the failed rail.
Use a fresh session directory and strictly increasing sequence numbers;
inspect the process/log and retained artifacts before deciding whether to retry.
A completed temporary receipt remains after a rename failure. This is checked
publication in an owned session directory, not a cross-process no-clobber
guarantee, fsync or power-loss durability.

Scoped checked writers cover discovery/help/limitations, memory/disassembly,
device-state and trace exports, MEMVU reports, golden writers, DHGR reference
exports, the screen text sidecar and SPIKE text/key-RAM dumps. Existing open
refusal messages remain where present; blocked screen sidecars and SPIKE text
dumps now refuse. A failed export can leave partial files. No automatic retry,
guest rollback or atomic payload/sidecar pair is promised.

Before/after evidence uses the preserved previous candidate and the same
dual-platform fixtures. The ACK baseline has 8 failed checks out of 18;
the export baseline has 4 failed checks out of 42. A real-stdio fixture latches
errors in read-only streams and demonstrates three previously ignored errors
in production liveness/trap/legacy closers. Native contract checks additionally
inject write/flush/close/rename failures without a production fault rail.

The protected consumer executable is unchanged. Stock windowed interaction
has not been exercised manually. Remaining audit work includes legacy debug
traces, SCC/WDM `a2gspu_output.hpp` binding close handling, device-private
serializers and asynchronous UI/media output flows;
this phase does not establish universal output coverage.

The upstream refresh was read-only. Main advanced from `643e6c21` to
`b2b7b6db` in three commits; their inspected changes and pending disposition
are recorded in `../../UPSTREAM_SYNC.md` section 25. No upstream change was
ported in this phase, and drift B–E remain separate owner decisions.

During validation the owner added a possible long-horizon IIgs+ clone-mainboard
target. Its design implications are recorded in `AGENTIC_ORACLE.md` section 11,
the shared-foundation contract, roadmap and handoff: preserve reference
machines, identify hypothetical changes, quantify pressure points and
compatibility, and retain model limits before a separate hardware project.
No machine profile, board design or fabrication work is claimed here.


Exact [validation](validation.json), [frozen inputs](build-inputs.json),
[configure](configure.log), [fresh build](build-transcript.log),
[gates](gates.log), [gate events](gates.ndjson), [Python tests](python-tests.log)
and [I/O contract checks](unit-after.log) are retained here.
[ACK baseline](ack-before.json), [ACK results](ack-after.json),
[export baseline](export-before.json) and [export results](export-after.json)
retain every check. The [real-stdio fixture](close-failure-fixture.cpp)
and its [old](close-before.log) / [new](close-after.log) transcripts prove
latched-error refusal in three actual production closers. Per-case ACK
emulator transcripts are retained beside the aggregate logs.
The [tested MCP descriptor](mcp-launch.json), [receipts](launch-proof.json)
and [transcript](launch-proof.log) verify 85-tool discovery and clean owned
shutdown for both EOF and explicit QUIT. The [publication review](publication-review.json)
binds the staged implementation to the frozen export.

Reproduce with `python -I tools/a2rail/ackcheck.py --exe <candidate> --out
<fresh-directory>` and the same invocation of `exportiocheck.py`;
`ctest --test-dir <build> -R interface_contract -V` exercises injected faults.
Both native scripts also run in the `agentic` umbrella gate.
The native candidate SHA-256 is `ccfd86f6460580ee82b9aedffc4d11eb989b86a273780bcbc4dadaa3a39dcf24`.
The protected consumer SHA-256 remains `364c78f7402381b1b323977293a492336b633712e22dd3f51ffd8b815684991e`.
