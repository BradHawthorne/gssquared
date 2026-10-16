# Suite ownership inventory

| Suite | Owner tree | Class | Notes |
|---|---|---|---|
| `gates.ps1` | **gssquared `tools/a2rail`** | instrument | runs every fork-local gate below and prints one matrix |
| gssquared ctest | gssquared `build/` | instrument (offline) | CPU conformance/coverage/cycles, 6522 VIA |
| `railparity.ps1` | **gssquared `tools/a2rail`** | instrument (offline) | registry ↔ code ↔ catalogue ↔ dispatcher |
| `artifacts.py selftest`, `../gdiff/selftest.py` | **gssquared `tools/`** | instrument (offline) | readers and differential tools refuse what they should |
| `buscompare.py selftest`, `bisect.ps1 -SelfTest` | **gssquared `tools/a2rail`** | instrument (offline) | bus witness comparison and scratch revision-search refusal/known-change fixtures |
| `p0check.py` and its interface/socket/MCP/context companions | **gssquared `tools/a2rail`** | instrument (`agentic`) | dual-platform native/client contracts, content, execution, artifacts and context; see `README.md` for the companion list |
| `dualsmoke.ps1` | **gssquared `tools/a2rail`**; a2engine pulls a copy | instrument | IIe+IIgs short bar |
| `railcheck.ps1` | **gssquared `tools/a2rail`**; a2engine pulls a copy | instrument | CTRL verb honesty |
| `gs816/execcheck.ps1` | **gssquared `tools/a2rail/gs816`**; a2engine pulls a copy | instrument | run/step/bp |
| `gs816/altzpcheck.ps1` | **gssquared `tools/a2rail/gs816`**; a2engine pulls a copy | instrument | ALTZP/LC/RAMRD |
| `gs816/assertparity.ps1` | **gssquared `tools/a2rail/gs816`**; a2engine pulls a copy | instrument | env vs CTRL assert verdicts |
| `personalitycheck.ps1` | **gssquared `tools/a2rail`** | instrument | cold start + `cpu` for every built-in configuration; JSON ledger |
| `protocolcheck.ps1` | **gssquared `tools/a2rail`** | instrument | protocol v2, discovery, JSON, ownership, live SHR golden |
| `copilotcheck.ps1` | **gssquared `tools/a2rail`** | instrument | windowed co-pilot rail |
| `unarmedhash.ps1` | **gssquared `tools/a2rail`** | instrument | stock emulation unchanged; armed observers change nothing |
| `../hwtruth/gate.ps1`, `mbaudit.ps1` | **gssquared `tools/`** | instrument | pinned emulator hardware-question verdicts and hardware-derived Mockingboard audit |
| verbcheck, sessioncheck, holdkeycheck, … | a2engine `tools/gs816` | instrument | full `gscheck` |
| audiocheck, drivecheck, netcheck, … | a2engine `tools/gs816` | instrument | device fidelity |
| leakcheck | a2engine `tools/gs816` | instrument | publish-track disclosure; runs first in `gscheck` |
| **validate.ps1** | a2engine | **product** | engine gates |
| **benchcheck.ps1** | a2engine | **product** | engine hot paths |
| **rescheck.ps1** | a2engine | **product** | resource backends |
| **turncheck.ps1** | a2engine | **product** | world time |
| covreport | a2engine | product | engine coverage |

**Rule:** instrument suites must not hard-code a product's memory map or demo content.
Product suites may.
