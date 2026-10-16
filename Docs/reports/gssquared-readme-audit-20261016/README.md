# Public README coherency audit — 2026-10-16

The owner requested a stale-information assessment after updating GitHub. The
starting public/local tip was `1df5dbd0`; implementation and runtime evidence
remain the publication checkpoint `bd276467`. This change is documentation only.

## Findings and corrections

| Previous README claim or omission | Correction / evidence |
| --- | --- |
| Headless machine driving described as absent upstream | Credit upstream's existing debug protocol; explain the fork's extensions. Protocol bridge: `src/debug/`, `clients/python/src/gs2debug/` |
| Everything lives on one branch and all changes are env-gated/stock | Identify the published default branch without erasing the separate unmerged model; distinguish default-off instrumentation from fidelity fixes and selected ports. `HANDOFF.md`, `UPSTREAM_SYNC.md` |
| Diagnostics are stdout-only | Describe file/socket/MCP interfaces, artifacts, streams and SCC/WDM capture. `A2GSPU_RAILS.md`, `DebugClient.md` |
| Goldens catch any boot/render drift; toolchain bugs become immediate assertion failures | Scope checks to declared behavior/models and require known-bad controls; ordinary run completion is not guest correctness. `CLOSED_LOOP_INTEGRATION.md` |
| Snapshot description implies complete machine capture | State compatibility/coverage limits and separate snapshots from deterministic replay. `MACHINE_CONTEXT.md`, roadmap |
| Toolchain loop only shows building a GS/OS boot disk | Describe direct raw execution separately from disk boots and the additive experiment companion, with its OMF/GS/OS limits |
| Agent integration described chiefly as Claude Code plus files/text/exit codes | Present vendor-neutral native tools, Python and stdlib MCP, explicit pause/ownership, pending-reply recovery and artifact transport |
| Current capabilities lack practical entry points | Add MCP launch, build/submodule/CTest recipes, gate selection and local ROM/corpus requirements; link canonical detailed contracts |
| CIDAR refinement intent and evidence limits absent | State measured primitive refinement direction, prevalence caveat, future archive/promotion scope and physical-hardware distinction |
| Adjacent client contract still says 73 native / 82 MCP tools | Correct to dated 74 / 85 and enumerate adapters; live discovery stays authoritative |
| README assessment missing from publication cadence | Add a standing README review to handoff section 7 and record completion in the roadmap |

Original upstream credits and license statement are retained. No native code,
dependencies, baselines, corpus inputs or consumer executables were changed.

## Verification

Read the complete README and checked its claims against the handoff, roadmap,
rail reference, context/client/integration contracts, CMake configuration,
session/MCP/experiment implementations and recorded publication evidence.
Local Markdown destinations, referenced anchors, CLI help and source discovery
inventory are checked in `checks.json`. Whitespace is checked with
`git diff --check`. Results: 30 local destinations and three anchors resolve;
source discovery generates 74 native / 85 MCP tools; both companion CLI help
commands pass; recorded validation counts and the unchanged protected consumer
SHA256 match. No emulator build or runtime suite is rerun for this
documentation-only follow-up, as permitted by handoff section 7.

Runtime evidence is retained in
[`../gssquared-publication-20261016/README.md`](../gssquared-publication-20261016/README.md):
14/14 gates, 14/14 native CTests, 21/21 Python tests and 3/3 affected Rosetta tests.
The CPU differential is 100 identical plus three known stop-policy differences
over 103 vectors; it is not 103 identical. Manual stock-windowed and physical
IIgs validation are not claimed. Commit/sync follows the existing authorized
fast-forward phase-closeout policy, with the live remote SHA verified after push.
