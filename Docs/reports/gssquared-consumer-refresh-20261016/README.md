# Protected consumer refresh — 2026-10-16

The owner separately approved rebuilding the protected in-tree consumer. Its
September 17 executable has been replaced by a fresh Release build of published
checkpoint `813914febce9d99f9d2300eb423990bb578a0e0b`, after full validation.
Rosetta and other consumers using `build/GSSquared.exe` now see the current
agent interfaces, stage A, CIDAR companions and fleet/media-policy support.
The separate engine-model branch remains unmerged.

## Current executable and rollback

- Current: `D:\projects\gssquared-fork\build\GSSquared.exe`.
- SHA-256: `364c78f7402381b1b323977293a492336b633712e22dd3f51ffd8b815684991e`.
- Preserved whole old build: `D:\gsqs\consumer-20261016\build-rollback`.
- Old executable SHA-256: `af7e0bbd72bee417ffddb5e86a00597df280fba904e8b2ed1ff9ad0738e2956c`.

The preparation checked the exact old hash, published source identity, clean
tracked source and absence of processes executing from the build directory.
Both absolute move paths were checked against their fixed workspace/phase
scope. The whole build was moved for rollback, including its old assets/cache,
before configuring a fresh in-tree UCRT64 GCC/Ninja Release build. Existing
vendored source was preserved: top-level pins match the gitlinks, while existing
macOS framework-link differences and missing nested Git metadata remain. Those
dependency checkouts were neither edited nor repaired by this refresh.

The new executable was held as `GSSquared.candidate.exe` for the full gate run.
After all suites passed, its bytes were rehashed and renamed to the consumer
filename; two subsequent MCP launches tested that final path. Assets, runtime
libraries, native tests and `snapdiff` come from this fresh full build. The old
build remains intact. A rollback would restore that whole directory after
checking process ownership and exact move scopes again.

## Validation and retained evidence

- **14/14 gates**, **14/14 native CTests**, **21 Python client tests**.
- **42 pinned boot checks unchanged**, including IIe/IIgs hashes and cycles.
- **Two independently closed MCP descriptor launches** from the final consumer
  filename: 85 tools, native typed read-only-media policy, fleet schema discovery
  and native QUIT with exit zero.
- Inventory remains 74 native verbs / 85 MCP tools / 161 rails / 32 wire types.
- Stock windowed interactive use was not manually exercised. The automated
  co-pilot gate covers its declared observation/control slice.

Exact identities and limits: [validation.json](validation.json),
[consumer receipt](consumer-rebuild.json). Retained transcripts:
[configure](configure-transcript.log), [build](build-transcript.log),
[gates](gates.log), [gate events](gates.ndjson),
[Python tests](python-tests.log), [MCP launches](mcp-launch-check.log).
The [tested MCP launch descriptor](mcp-launch.json) selects the current consumer
and a dedicated output root. Earlier exported binaries and their historical
receipts remain preserved for their original checkpoints.

## Scratch-space maintenance

The owner's low-space report prompted a separate, scoped cleanup: 31 inactive
temporary directories (21,071,283,894 source bytes, 19.62 GiB) were preserved
under `E:\agent-temp-archive`, with file hashes verified before removing C:
originals and forwarding notes left at the old locations. This includes nine
emulator scratch/rollback directories and 22 older p2nux replay runs. Detailed
private manifests stay in that archive; the local summary is
`D:\gsqs\cspace-summary-20261016.json`. C: had approximately 23.5 GiB free
at closeout. Active C: application/runtime files were left in place.

This build/test phase sets `TEMP`, `TMP` and `TMPDIR` to `D:/gsqs/tmp` for its
own launches. No global application environment setting was changed.

The README, handoff, development setup and roadmap were assessed and updated
for the refreshed consumer. Drift B–E, engine merge and baseline re-blessing
remain separate decisions; this refresh imports none of them. Real kernel/GDB/
debug-info fixtures, complete replay and hardware witnesses remain open.
