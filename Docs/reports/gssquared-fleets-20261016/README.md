# Queue: independent native fleets and observable media policy (2026-10-16)

Status: implemented and validated; complete fork gate stack passes.
API and reproduction: [`Docs/FLEETS.md`](../../FLEETS.md).

## Completed behavior

`fleet.py` drives explicit matrices with up to eight concurrent workers and
64 cells. Each cell owns its native process, CPU/RAM, socket, staged inputs,
artifact root and closed process receipt. SHA256 pins the executable and input
bytes; seeds and native launch pins are explicit. A staged `.gs2` configuration
can replace a built-in platform selector. Native commands and mandatory
assertions retain their replies. The aggregate reports every cell in plan order,
while a completed-cell journal records results as they finish.

Bounded native event capture includes configurable filters, native queue loss,
received/enqueued count agreement, sequence continuity and a separate 4096-record
client cap. Incomplete evidence is inconclusive rather than a passing guest
claim. Context snapshots and artifact digests accompany each cell. There is no
checkpoint cloning or complete-machine replay claim; literal external paths,
configuration references and host endpoints remain caller-controlled resources.

The explicit `--read-only-media` managed launch mode pins RAMDISK and the
spike-end writeback guard, including nested MCP batches. Contradictory launch
settings refuse. A new typed reader exposes the effective presence-based
writeback predicate through all four canonical/legacy rail names. The reader
and actual spike-end flush share one implementation. A value `0` still means
present for these legacy flags; canonical/alias guards both win over requests.
This predicate's scope excludes explicit save/unmount, BRAM and arbitrary host
endpoints. Stock launch defaults are unchanged.

Native capabilities advertise the fleet companion, its structural schema
option and the updated CIDAR contract. Tool/rail/wire counts remain
74 native verbs, 85 MCP tools, 161 rails and 32 wire types.

## Evidence method

The real 2×2 IIe/IIgs × seed matrix executes independently differing byte-store
programs. All four cells pass, with distinct process identities and complete
event closeouts. A planted wrong assertion makes exactly its named cell red.
Invalid plans return CLI exit 2 before output; a failed matrix returns exit 1
and retains every cell. Reused output and changed inputs refuse. An explicit
staged configuration passes. An intentionally tiny native event queue fails
the aggregate. An over-budget synchronous command records unknown effects,
terminates only its owned child and closes its receipt.

The timeout fixture found a Windows AF_UNIX endpoint left after forced
shutdown: it looks like a file, but cannot be read as a payload. The fixed
runner records its IPC disposition without losing the cell's receipt. Device
baseline events also exceeded the initial 256-record default on IIgs; the
default is now 1024, while explicit capacity-one tests still fail on loss.
Native keyboard tokens such as `{RETURN}` remain native syntax rather than
being mistaken for file placeholders. Before/failure evidence is retained.

The existing native contract CTest adds all 16 request/guard combinations.
Live socket/file readers verify canonical/legacy presence, including `0`, on
both IIe and IIgs. A real nested batch passes its guest assertion with the
native RAMDISK pin armed; contradictory media pins refuse before launch.

## Build and validation

The build is `D:\gsqs\fleet-20261016\build\GSSquared.exe`. Its source is a
fresh export of `1878ecc9` plus reviewed SHA256-bound overlay files. The
configure-time revision names that base; the overlay and executable identities,
not that label alone, identify the tested implementation. Vendored sources are
verbatim from the preceding verified publication export, with the same pinned
root/nested revisions. Eight ignored ROM/corpus inputs were copied and checked.
No vendored source or protected in-tree consumer was changed.

| Check | Result |
| --- | --- |
| Fresh Release configure/build | PASS; exact export/overlay/dependency/input identities retained |
| Existing exported Python client | 21/21 PASS |
| New offline fleet shape/refusal cases | 7 PASS |
| Focused native fleets | 20/20 PASS, including CLI exit/evidence controls and native schema discovery |
| Focused media policy | 53 checks PASS, including native readers/aliases and nested batches |
| Full fork gates / native CTest / pinned boots | 14/14 gates, 14/14 native CTests, no skips; 42 pinned boot checks unchanged |
| Repeatable MCP descriptor | Two launches pass: 85 tools, typed policy, fleet schema option, clean native QUIT and distinct closed receipts |

The structural schema document is emitted and JSON-parsed, and the production
runtime validator passes its real-plan/refusal fixtures. The optional
`jsonschema` metavalidator is unavailable in this Python installation; no
formal JSON Schema meta-validation result is claimed. Stock windowed UX was
not manually exercised in this phase; the source defaults and headless gate
matrix are the scope of the regression evidence.

The first full gate invocation omitted Python from the caller PATH. Six
Python-backed suites failed to start; native suites that ran passed. That
invocation is retained separately, and final validation uses the required
UCRT64/Ninja/Python PATH. It is not counted as a successful gate run.

A subsequent full matrix passed, but the stricter MCP launch check found an
older compiled discovery field: the overlay copy had preserved a source mtime
older than its already built object. Changed overlays now use current destination
timestamps; the reviewed code was explicitly rebuilt. Both the launcher check
and umbrella fixture now require `schema_option: "--schema"`. That stale launch
and earlier matrix are retained separately; final evidence selects the rebuilt
executable. `DEVELOPMENT_SETUP.md` records the general build discipline.

README was assessed and updated with the fleet entry point. Native capability
discovery, rail readback documentation, Python launcher guidance and MkDocs
navigation are updated in this phase. Drift B–E, the engine merge, baseline
re-blessing and protected consumer replacement remain separate decisions.

The adjacent reference-consolidation task is also closed by
[`MOCKINGBOARD_IMPLEMENTATIONS.md`](../../MOCKINGBOARD_IMPLEMENTATIONS.md).
It centralizes active build/include edges, compatibility declarations, disabled
tests and historical copies; the old development notebook links there. No
source path, active declaration or reference file is removed. Future deletion
still requires a named compatibility migration rather than assuming external
consumers absent.

Final identities are in `checks.json` and `build-inputs.json`. `fleet-results.json`
indexes the retained matrices; `retained-artifacts.json` binds their local files.
The tested descriptor is `mcp-launch.json`; final launcher proof is
`mcp-launch-checks.log`. No runtime binary, ROM or private disk image is
redistributed in this source/evidence phase.
