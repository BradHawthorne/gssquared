# Output sink audit and finalization — 2026-10-16

This continuation fixes false success in synchronous screenshots, text exports,
coverage and snapshot output, tap sessions and central record writes. It also
checks global-record and subscription finalization. Final frozen-input validation passes **14/14 gates**, **14/14 native CTests**,
**26 Python tests**, **25 native output cases**, **43 I/O contract checks**
(24 new), **42 unchanged boot checks** and **two MCP launches**.

The native baseline has 15 false-success cases and 10 positive/refusal passes
across IIe and IIgs. CTRL shot/text, SPIKE BMP/coverage/snapshot outputs and
partially opened tap sessions could report success. A separate real-stdio
fixture confirms both an ignored record write and a zero process exit after a
latched global-sink error. The updated fixture refuses both.

## What changed

- The shared owned-FILE finalizer checks flush, existing error indicators and
  close, consumes ownership even on failure and closes exactly once.
- `Record::write` propagates failure rather than discarding `write_checked`'s
  result. Failed writes/flushes skip counts and observer callbacks. Global
  readback flushes before answering, and global close is checked. Its `atexit`
  fallback reports a late failure and exits 2 without recursively calling
  `exit`. That emergency `_Exit` can bypass later cleanup; it is a failure
  exit, never a complete-capture receipt.
- CTRL/socket dispatch and co-pilot command boundaries return output failures
  as FAIL. The SDL iteration boundary catches otherwise uncaught instrumentation
  I/O errors. A failure can follow partial execution or host-file mutation;
  it does not roll back a command and must not trigger blind retries. A failed
  global sink can keep subsequent agent commands failing; start a fresh session.
  Native QUIT remains available for owned shutdown.
- Tap sessions require all four NDJSON files to open before installing any
  pointers/hooks. Refusal closes opened streams and leaves the session inactive.
  Closing checks all session files even if one fails. Periodic metadata is
  checked too. SPIKE closes taps before its guest verdict; ordinary SDL teardown
  also closes tap/global sinks explicitly.
- A2COV1/A2COV2 and profile output checks completion; successful hash/format
  receipts follow finalization. SPIKE propagates failed requested coverage.
  PNG writers check short write, flush and close.
- CTRL save, SPIKE SNAP_SAVE and SAVE_AT use checked finalization. Shared
  device-block position/length backpatch seeks are checked. Legacy SPIKE
  SNAP_SAVE/SAVE_AT require IIgs; CTRL save supports both platforms.
- The named synchronous screenshot returns SDL's readback/save verdict. CTRL
  shot returns FAIL and SPIKE exits 2 on failure. CTRL text checks the full 2KB
  export. The ordinary asynchronous UI screenshot path is unchanged.
- File subscriptions retain close errors in their error/status fields, making
  `subscribe off` incomplete on finalization failure. A failed initial control
  record also refuses the subscription arm.

Formats, guest execution models and 74 verbs / 85 MCP tools / 161 rails /
32 socket types stay compatible. Explicit test-only WriteOps arguments provide
faults without production rails or new native tools.

## Whole-output audit disposition

| Path | Disposition |
| --- | --- |
| SPIKE/VRAM/raw tap payloads and sidecars | Checked in `89019ae5`; retained regression remains gated |
| Central records and global readback/close | Checked here; buffered acceptance is not disk durability |
| Generic tap session/call sink and periodic metadata | Checked opens, writes and close |
| Coverage bitmap, bound coverage and profiles | Checked writes and finalization |
| Snapshot save entry points and shared backpatching | Checked here; restore semantics unchanged |
| Synchronous BMP and reference PNG | Propagate SDL verdict / checked stdio respectively |
| File subscription close receipt | Close errors make status incomplete; stopped record precedes close |
| Trap/liveness/watch/callstream/loadmap/card/U2/region-log and legacy title-specific sink replacement/shutdown | Central record errors propagate; several per-rail closers still ignore finalization; next queue slice |
| Other CTRL exports (catalog/disassembly/read/dumps/goldens), ACK publication, legacy diagnostic traces and device serializers' private seeking | Independent I/O audit/hardening remains; no blanket completion claim |
| Asynchronous UI screenshot and media/writeback | Separate application/device lifecycles; preserved here |

No atomic file-set publication, fsync, power-loss durability, mutation rollback,
physical-hardware result or complete device replay is claimed. Partial files
can remain after refusal. Use fresh directories, process/command verdicts,
explicit stream close/status and format-specific checked readers. The manual
stock-windowed interaction check remains unexercised.

## Frozen candidate and evidence

Fresh UCRT64/Ninja Release: `D:\gsqs\sinkio-20261016\build\GSSquared.exe`,
from exported `89019ae5` plus 17 digest-bound implementation/test overlays,
the pristine publication dependency snapshot and eight pinned local inputs.
The in-tree consumer and earlier candidates remain preserved. All scratch
launches pin TEMP/TMP/TMPDIR to D:. No archive, upstream, drift, model-merge or
baseline re-bless change is included.

The [staged-source review](publication-review.json) records frozen-file and
Git-blob hashes: 16 implementation files match byte for byte; the rail-setter
file differs only by `core.autocrlf=input` CRLF-to-LF normalization. The input
receipt retains the exact physical bytes compiled and tested. The updated
[client output-failure contract](../../DebugClient.md#output-failures) and
[final catalogue parity check](final-doc-parity.log) cover the documentation
closeout edits after the frozen implementation build.

README assessment adds the finalization contract and names remaining lifecycles.
Validation receipts and identities are added at phase closeout.

Exact [validation](validation.json), [frozen inputs](build-inputs.json),
[configure](configure.log), [fresh build](build-transcript.log),
[gates](gates.log), [gate events](gates.ndjson), [Python tests](python-tests.log),
[native baseline](native-before.json), [baseline transcript](native-before.log),
[native results](native-after.json), [native transcript](native-after.log)
and [I/O contract checks](unit-after.log) are retained here.
The [real-stdio fixture](record-failure-fixture.cpp),
[before/after cases](record-proof.json) and [proof transcript](record-proof.log)
retain a genuine failed write and late-close error, without exhausting a disk
or adding a production fault rail. The original close exits zero; the new
fallback exits 2. The [tested MCP descriptor](mcp-launch.json),
[launch receipts](launch-proof.json) and [transcript](launch-proof.log) verify
EOF and explicit QUIT close-out against this candidate.

Reproduce focused checks with `python -I tools/a2rail/sinkiocheck.py --exe
<candidate> --out <fresh-directory>` and `ctest --test-dir <build> -R
interface_contract -V`; run the full `tools/a2rail/gates.ps1` stack at closeout.
The native candidate SHA-256 is `6788c43b848ef59aa9bf58b3808e3a8486bfc543108d8a4371724f22688412ec`.
The protected consumer SHA-256 remains `364c78f7402381b1b323977293a492336b633712e22dd3f51ffd8b815684991e`.
