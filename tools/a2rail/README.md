# a2rail — portable instrument harness (DualSmoke)

This directory is the **gssquared-local home** for the short dual-platform rail
gate (FOUNDATION_MILESTONES M9). It is enough to challenge the CTRL rail on
**IIe + IIgs** without cloning a2engine.

## Run

From the gssquared repo root (or any cwd):

```powershell
.\tools\a2rail\dualsmoke.ps1
.\tools\a2rail\dualsmoke.ps1 -SkipOffline
```

Requires a built `build\GSSquared.exe` (or `GSSQUARED_ROOT` / `A2_GSSQUARED`).

## What runs

| Suite | Platforms |
|---|---|
| gssquared ctest (offline) | host |
| railcheck | 3 and 5 |
| execcheck | 3 and 5 |
| altzpcheck | 3 and 5 |

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
