# Suite ownership inventory

| Suite | Owner tree | Class | Notes |
|---|---|---|---|
| gssquared ctest | gssquared `build/` | instrument (offline) | CPU/VIA conformance |
| dualsmoke | **gssquared `tools/a2rail`** + a2engine mirror | instrument | IIe+IIgs short bar |
| railcheck | a2rail / a2engine tools | instrument | CTRL verb honesty |
| execcheck | a2rail / a2engine tools | instrument | run/step/bp |
| altzpcheck | a2rail / a2engine tools | instrument | ALTZP/LC/RAMRD |
| verbcheck, sessioncheck, holdkeycheck, … | a2engine `tools/gs816` | instrument | full `gscheck` |
| audiocheck, drivecheck, netcheck, … | a2engine `tools/gs816` | instrument | device fidelity |
| **validate.ps1** | a2engine | **product** | engine gates |
| **benchcheck.ps1** | a2engine | **product** | engine hot paths |
| **rescheck.ps1** | a2engine | **product** | resource backends |
| **turncheck.ps1** | a2engine | **product** | world time |
| covreport | a2engine | product | engine coverage |

**Rule:** instrument suites must not hard-code a2engine memmap or demo content.
Product suites may.
