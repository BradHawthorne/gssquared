# a2rail — the fork's instrument gates

This directory is the home of the gates that challenge the CTRL rail, the env rails and
stock emulation on **IIe and IIgs**, runnable from this repository alone.

## Run

From the gssquared repo root (or any cwd):

```powershell
.\tools\a2rail\gates.ps1                 # every gate below, one matrix (about 7 minutes)
.\tools\a2rail\gates.ps1 -Offline        # railparity + the two selftests (seconds)
.\tools\a2rail\dualsmoke.ps1             # the short dual-platform bar after shared rail/MMU work
```

Requires a built `build\GSSquared.exe` (or `GSSQUARED_ROOT` / `A2_GSSQUARED`). On Windows,
`runtime.ps1` finds the UCRT DLL directory from `A2GSPU_UCRT_BIN`, `MSYS2_ROOT` or
`C:\msys64\ucrt64\bin` and fails with an actionable message instead of the opaque
`0xC0000135` exit. `unarmedhash.ps1` needs a GS/OS 6.0.1 image in `corpus\gsos601.po`
(see `corpus/README.md`), or `-SkipCorpus`, which it reports as PARTIAL.

Pass `-Report trace.ndjson` to `ctl.ps1`, or set `A2RAIL_REPORT`, to keep a
machine-readable event for every command, acknowledgement, refusal and stall.

## What runs

| Gate | Run by | Platforms | What it proves |
|---|---|---|---|
| `railparity.ps1` | gates, dualsmoke | offline | every rail the code reads is declared, has a catalogue row under its family, and a whole synopsis; every CTRL verb is dispatched and listed; readers and sinks are registered |
| `artifacts.py selftest` | gates | offline | the artifact reader refuses every malformed file it should |
| `../gdiff/selftest.py` | gates | offline | the differential tools against real captures |
| ctest | dualsmoke | host | CPU conformance/coverage/cycles, 6522 VIA |
| `railcheck.ps1` | dualsmoke | 3 and 5 | CTRL verb honesty |
| `gs816/execcheck.ps1` | dualsmoke | 3 and 5 | run / step / bp / banked PCs |
| `gs816/altzpcheck.ps1` | dualsmoke | 3 and 5 | ALTZP / language card / RAMRD |
| `personalitycheck.ps1` | gates | all 16 built-in configurations, platforms 0–5 | every machine cold-starts and answers `cpu` |
| `protocolcheck.ps1` | gates | 5 | protocol v2, `systems` discovery, JSON replies, ownership refusal, live SHR golden |
| `copilotcheck.ps1` | gates | 3 | the windowed co-pilot rail observes, controls, and refuses `poke` (its window cannot take focus) |
| `gs816/assertparity.ps1` | gates | 5 | the env and CTRL assert surfaces give the same verdicts |
| `unarmedhash.ps1` | gates | 3 and 5 | pinned stock boots reproduce `unarmed.baseline.json`; observation rails armed change nothing |

`personalitycheck.ps1` writes a JSON ledger to `$TEMP\a2rail-personalities\personality-results.json`
and derives the expected configuration count from `src/systemconfig.cpp`, so adding a
configuration turns it red until the list is extended.

## Ownership

**The stack is owned here.** a2engine keeps a pulled copy of `dualsmoke`, `ctl`, `session`,
`railcheck`, `runtime`, `toolpin`, `execcheck`, `altzpcheck` and `assertparity` for its
own `gscheck.ps1` orchestration; after editing any of them here, run `.\tools\sync-a2rail.ps1`
**in a2engine** to pull them (it backs up what it replaces). Until 2026-09-16 the direction
was the reverse, and the fork's copies had become the fixed ones while a2engine's were a
month older — the old sync would have undone those fixes.

The full device net (`audiocheck`, `drivecheck`, `netcheck`, …) stays in a2engine's
`tools\gs816` and `gscheck.ps1`; see `SUITES.md`. Product gates (`validate`, `benchcheck`,
`rescheck`) never belong here.

## Docs

- `Docs/DUAL_PLATFORM.md` — why both platforms
- `Docs/SHARED_FOUNDATION.md` — multi-consumer rules
- `SUITES.md` — instrument vs product inventory
