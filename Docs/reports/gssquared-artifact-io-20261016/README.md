# Raw artifact I/O failures — 2026-10-16

Raw SPIKE, CTRL VRAM and generic tap CPU-view/full-RAM captures now refuse
failed payload or sidecar I/O. A missing file no longer passes as an empty
capture. Real zero-record files remain valid, and successful raw formats,
record layouts and hash calculations stay compatible.

Final frozen-input validation passes **14/14 gates**, **14/14 native CTests**,
**26 Python tests**, **34 native capture cases**, **19 I/O contract checks**,
**42 unchanged pinned boot checks** and **two stdio MCP launches**. All
**26 capture/guest-diagnostic comparisons** match the previous binary.
The contract and scope are in
[instrumentation shape, M6](../../INSTRUMENTATION_SHAPE.md#m6--artifact-discipline-and-a-shared-reader--p4--s).

The baseline's expanded native fixture found **26 false-success cases** and
**8 positive passes**, plus two clean session exits, across IIe and IIgs. Blocked payload or sidecar paths
still produced zero exits or CTRL OK replies. The initial header fixture also
showed a nonexistent payload receiving a zero-count sidecar and a plausible
hash/count returned after a failed stream open. These failures were retained
before implementing the fixes.

## Behavior and acceptance

`a2gspu_artifact_io.hpp` checks open, complete write, flush and close with
single-owner FILE cleanup. Raw stream dump functions only return their hashes
and assign their output counts after successful payload finalization. Metadata
generation checks source open/read/close, nonzero record size, multiplication
overflow and exact payload length before opening the sidecar. It checks the
sidecar's record write and finalization, removes a sidecar opened by a failed
attempt and diagnoses failed removal. `Record::write_checked` does not count
or notify observers after a failed write/flush; the existing void API remains.

SPIKE and generic tap failures print `status=FAIL artifact-io` and exit 2.
CTRL `vram` returns `status=FAIL vram-artifact-io`; the same session remains
usable and can produce a valid later capture. SPIKE also refuses output-path
truncation. Successful artifact receipts follow payload and sidecar completion.
Observe reads still use `a2obs_peek`.

The extended existing native interface-contract test checks real empty and
missing payloads, invalid lengths/counts, preservation before opening an
invalid sidecar, blocked trace/slot/MMU opens, failed-record notification and
injected short-write/flush/close failures. The injection is explicit C++ test
operations, with no new environment rail or production fault control.
The 34-case native fixture exercises valid and blocked SPIKE captures, valid
and blocked generic tap captures and CTRL failure/recovery on platforms 3/5.
It joins the foundation agentic gate. Native CTest count remains 14;
discovery remains 74 verbs / 85 MCP tools / 161 rails / 32 socket types.

## Limits and next work

These checks establish C stdio completion. They do not provide atomic pair
publication, `fsync`, power-loss durability or authenticated historical bytes.
A failure may leave a partial payload, and files from an earlier run can
survive a refusal before opening. Use fresh output directories and require a
successful run/command plus `artifacts.py raw` before treating captures as
evidence. Observer notifications can precede a later close failure; they are
not final artifact receipts.

General NDJSON sink lifecycle, screenshot, coverage and snapshot output
hardening remain separate paths. The manual stock-windowed interaction check
is not exercised. No drift, engine merge or baseline re-bless is included.
The protected in-tree consumer is preserved; use the tested out-of-tree
candidate for this phase's new native behavior.

## Frozen inputs and retained evidence

The candidate is a fresh Release UCRT64/Ninja build in
`D:\gsqs\artifactio-20261016\build`, from exported `ec08da7c` and 13
SHA-256-bound implementation/test overlays. Eight ignored local input pins
and the pristine 7,459-file dependency snapshot match the prior validated
publication export. Overlay copies use current timestamps in a new build tree.
Later report/documentation edits do not change the tested implementation.

Build identities, transcripts, focused before/after evidence, gate results and
the tested MCP descriptor are retained beside this report at closeout.
README assessment adds the capture refusal/checked-reader requirement; no
tool counts, ordinary launch recipes or successful raw formats changed.

The [staged-source review](publication-review.json) records both frozen-file
and Git-blob hashes. Twelve implementation files match byte for byte; the
slot-bus header differs only by the repository's `core.autocrlf=input`
CRLF-to-LF normalization. No tokens or other whitespace differ. The build
input receipt retains the exact physical bytes compiled and tested.

Exact identities and evidence: [validation](validation.json),
[build inputs](build-inputs.json), [configure](configure.log),
[full fresh build](build-transcript.log), [gate transcript](gates.log),
[gate events](gates.ndjson), [Python tests](python-tests.log),
[initial unit failures](unit-before.log), [final native contract](unit-after.log),
[native baseline](native-before.json), [baseline transcript](native-before.log),
[native results](native-after.json), [native transcript](native-after.log),
[capture comparisons](capture-comparison.json) and
[comparison transcript](capture-comparison.log).

The [tested MCP descriptor](mcp-launch.json) selects the candidate binary and
frozen adapter; [two receipts](launch-proof.json) and the
[launch transcript](launch-proof.log) verify EOF and explicit QUIT close-out.
The candidate executable SHA-256 is
`f1c7f44792302ae7ccd0030a283d77eaf5534566da8f09580a9a22817e5cb547`.
The preserved in-tree consumer remains
`364c78f7402381b1b323977293a492336b633712e22dd3f51ffd8b815684991e`.

The first gate launcher could not find `pwsh` in the MSYS2 PATH; no gate
suite ran in that attempt. Its [failure](gates-launcher-failure.log) is retained.
The full passing run used `C:/Program Files/PowerShell/7/pwsh.exe` explicitly.
All active task scratch and temporary launches use D:, with no protected
consumer rebuild, archive changes or new C: build outputs.
