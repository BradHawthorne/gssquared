# Whole-fork stale-information audit — 2026-10-16

Starting checkpoint: `21b88bb1`. The owner requested a walk of the whole fork
for stale information, then explicitly reiterated that no work should target
upstream. This phase corrects current guidance and development tooling while
preserving dated evidence and the README's one-way integration policy.

## Coverage and method

The initial tracked inventory contains **1,308 paths**, with **1,179 UTF-8 text
files** scanned and **168 Markdown files** checked. Binary inputs and vendored
gitlinks were inventoried rather than edited. The scan covered root guidance,
Docs, reports, native source, applications, Python clients, tools, scripts,
editor configuration, discovery registries, CMake and site navigation.

Automated checks looked for broken local Markdown destinations, obsolete
counts/status claims, host-specific paths and TODO/FIXME/HACK/XXX markers.
Focused source review checked the findings against implementation and the
latest validated executable. Initial results: five broken active Markdown
destinations, no broken report destinations, 52 claim candidates, 53 host-path
candidates and 659 debt-marker matches. A marker match is not a proven defect;
old TODOs and historical reports were not rewritten as current findings.
`inventory.json` records scan scope, classifications and remaining work.

This is a repository-wide stale-information and tooling audit, not a proof
that every emulation path is correct. The test stack provides scoped behavioral
evidence; physical hardware, complete replay and exhaustive device semantics
remain separate evidence/work.

## Corrections

| Area | Stale finding and resulting change |
| --- | --- |
| Agent instructions | Historical 8/8 CTest evidence is dated; current 14/14 evidence is linked. Build guidance now protects the in-tree consumer and uses a separate build; offline fixtures are described accurately |
| Public contribution policy | Fork rules and upstream hardware-evidence guidance are distinguished; the PR-template link resolves from `.github/` |
| Changelog | Added a dated fork checkpoint pointing to publication/audit evidence; inherited upstream entries are retained without presenting them as the complete fork history |
| Protocol/client contracts | Removed blanket no-JSON claims contradicted by MANIFEST/shared-agent/event envelopes; preserved binary frame/packed-field rules and host-side MCP distinction. Removed future-tense socket implementation instructions |
| MCP design page | Old Go/separate-repo proposal explicitly marked historical, with implemented Python stdio/MCP/context entry points |
| Snapshot guidance | Original JSON-design sketch quarantined; closure/status text acknowledges implemented scanner/media/RTC/serial blocks and remaining session-clock/replay limits |
| `.gs2` configuration | Corrected “not implemented” status and example seeding against `src/util/SystemConfig.cpp`; documented implemented editor Save, positional config CLI versus `-c N`, and automated BRAM/preference exceptions; retained proposal details with explicit loader/user-guide authority |
| User/developer navigation | Site nav includes machine context, Python/MCP, wire protocol, cookbook, current roadmap/handoff, config guides and developer setup. Replaced the stale “five omissions” comment with honest report/reference scope |
| Historical status pages | Upstream release roadmap, issue inventory, clock journal, original DAP comparison and AY diagnosis are labeled as dated material and linked to current contracts |
| Fidelity notes | Added a current overlay for boundary faults, shared dispatcher, capture/profiles/MCP/context; original revision-relative studies remain intact |
| DMABank | Removed the false current-absence claim: `$C037` handlers are installed. Register readback does not imply complete external-DMA modeling |
| VOC provenance | KEGS source paths are explicitly external references, not broken links into this fork; the unpinned reference revision is stated |
| Gate inventory | Corrected 13 to 14 umbrella gates; included context/experiment, buscompare, bisect, hardware-question and MB-Audit ownership |
| Editor tasks | UCRT64 GCC/CMake/Ninja on Windows, separate `build-local`, explicit configure/build dependencies and one default build task; unused `DEV_MODE` removed |
| Editor launches | Removed unbuilt dpp/vpp/speaker variants and author-specific media/fixture paths; retained the active vpp2 target, supplied CPU fixture directory input, current target references and UCRT runtime PATH |
| Editor metadata | CMake Tools uses `build-local`; Windows IntelliSense follows UCRT64 GCC. Removed the orphan root `hello_world` launch template and ignored only the documented developer build directory |
| Package publication | Changed release defaults from upstream/main to the owner's fork and `origin/a2gspu`; removed fallback/override redirection. Host scripts refuse non-fork origins before commands, sync validates origin before fetch, and uploads refuse non-fork CLI/environment destinations before artifacts/`gh` |
| Release Windows recipe | UCRT64 GCC/Ninja replaces the inherited MinGW/Clang recipe; local release skill and example configuration match the fork-only policy |
| Local Windows shell | A known-pass release fixture exposed bare `bash` launching WSL despite PATH discovery resolving MSYS2. Local host calls now use the configured MSYS2 executable explicitly; the failing first run is retained alongside the passing policy log |
| Campaign helper | Scout requirements, optional external pin script and shared temporary-directory limitation are stated; campaign-specific behavior is not advertised as a generic parallel-safe runner |

## Remaining work, explicitly retained

- `build/GSSquared.exe` remains the owner's protected older consumer. Current
  tested execution uses the preserved publication build. Replacing the consumer,
  merging the engine model, drift B–E and baseline changes retain their separate
  owner decisions.
- The file harness still assumes process name `GSSquared` and a coarse start-time
  window. Bind exact executable/start identity before claiming renamed-process
  support; this is already on the roadmap.
- Scout still uses shared scratch and campaign-specific milestones/pin helpers.
  Generalization needs behavior tests and isolated-run ownership.
- Alternate Mockingboard implementations and backup headers are retained pending
  a dependency/consumer review. The active library uses `mb2.cpp`,
  `AY8910-2.hpp` and `W6522.hpp`; `devices.cpp` still includes `mb.hpp`, and the
  test application includes `N6522.hpp`. Names suggesting a variant or backup
  do not establish that it is unused; they are not grounds for blind removal.
- Actual multi-host package creation, interactive editor/Instruments sessions
  and a rendered MkDocs site were not exercised. MkDocs is not installed in the
  audit Python environment; navigation destinations are checked statically.
- Legacy imported journals/proposals retain their original checkboxes and
  upstream citations. They are evidence/provenance, not the active fork queue.

## Validation and publication

Detailed results and identities are in `checks.json`, `gates.ndjson` and logs.
The full fork gates use the current publication executable compiled from
`bd276467`; native emulator sources are unchanged by this audit. Release-policy
fixtures exercise allowed fork identities and wrong-target refusals without
SSH, fetch, release creation or upload. Editor validation resolves task graphs
and actual Ninja targets without invoking a build or replacing the consumer.

This phase follows handoff section 7: scoped review, checks, coherent commit,
explicit fast-forward push to `origin/a2gspu`, then independent remote SHA
verification. No upstream write, PR, package release or tag creation is part
of the audit.

Current results: **14/14 fork gates**, including **14/14 native CTests** and
unchanged pinned boot witnesses; **11/11 release-policy tests** and **21/21
Python client tests**. Static checks resolve 418 local links, 17 anchors in
changed documents and 83 navigation destinations; 13 editor tasks and 11 launch
targets match their graph/build inventory. All 92 Python and 21 PowerShell
sources parse. Exact identities and limits are recorded in `checks.json`.
