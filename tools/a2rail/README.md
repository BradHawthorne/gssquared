# a2rail — portable instrument harness (DualSmoke)

This directory is the **gssquared-local home** for the short dual-platform rail
gate (FOUNDATION_MILESTONES M9). It is enough to challenge the CTRL rail on
**IIe + IIgs** without cloning a2engine.

## Run

From the gssquared repo root (or any cwd):

```powershell
.\tools\a2rail\dualsmoke.ps1
.\tools\a2rail\dualsmoke.ps1 -SkipOffline
.\tools\a2rail\personalitycheck.ps1
.\tools\a2rail\protocolcheck.ps1
.\tools\a2rail\copilotcheck.ps1
```

Pass `-Report trace.ndjson` to `ctl.ps1`, or set `A2RAIL_REPORT`, to retain a
machine-readable event for every command, acknowledgement, refusal, and stall.
The a2engine `gscheck -Report ...` orchestrator enables this automatically in a
sibling `.rail.ndjson` file, exposing progress inside long opcode sweeps.

Requires a built `build\GSSquared.exe` (or `GSSQUARED_ROOT` / `A2_GSSQUARED`).
On Windows, `runtime.ps1` discovers the UCRT DLL directory from
`A2GSPU_UCRT_BIN`, `MSYS2_ROOT`, or `C:\msys64\ucrt64\bin` and fails with an
actionable message instead of the opaque `0xC0000135` process exit.

## What runs

| Suite | Platforms |
|---|---|
| gssquared ctest (offline) | host |
| railcheck | 3 and 5 |
| execcheck | 3 and 5 |
| altzpcheck | 3 and 5 |
| personalitycheck | all 15 built-in configurations across platforms 0–5 |
| protocolcheck | protocol v2, systems discovery, JSON replies, ownership, live SHR golden |
| copilotcheck | normal windowed loop + read-only telemetry + mutation refusal |

`personalitycheck.ps1` cold-starts every built-in machine, requires a valid
`cpu` acknowledgement, shuts it down, and writes a durable JSON ledger to
`$TEMP\a2rail-personalities\personality-results.json`. This covers the base
Apple II/Plus/IIe families plus PAL, mouse, dual Mockingboard, IIe/65816,
IIgs Disk II, A2GSPU, Uthernet II, ROM 3, ROM 4, and Thunderclock variants.

Full device net remains in **a2engine** `tools\gscheck.ps1` (see `SUITES.md`).

## Source of truth / sync

Lab development still authors suites next to a2engine product gates. After
editing dualsmoke / railcheck / exec / altzp / session / ctl / toolpin in
**a2engine**, refresh this copy:

```powershell
# from a2engine
.\tools\sync-a2rail.ps1
```

Do not invent product-only gates here (validate, benchcheck, rescheck stay in
a2engine).

## Docs

- `Docs/DUAL_PLATFORM.md` — why both platforms
- `Docs/SHARED_FOUNDATION.md` — multi-consumer rules
- `SUITES.md` — instrument vs product inventory
