# a2rail — the fork's instrument gates

This directory is the home of the gates that challenge the CTRL rail, the env rails and
stock emulation on **IIe and IIgs**, runnable from this repository alone.

## Run

The connected observation API and additive Rosetta experiment runner are
documented in [Machine context](../../Docs/MACHINE_CONTEXT.md).
`contextcheck.py` exercises live/paused observations, source-pixel models,
causal history, losses and non-disturbance on IIe and IIgs. `experimentcheck.py`
checks correctness-bound cycle/instruction/size comparisons, including actual
wrong-result runs. Both are included in the umbrella's `agentic` gate.

From the gssquared repo root (or any cwd):

```powershell
.\tools\a2rail\gates.ps1                 # every gate below, one matrix (about 7 minutes)
.\tools\a2rail\gates.ps1 -Offline        # railparity + artifact/gdiff/buscompare/bisect selftests
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
| `artifacts.py selftest` | gates | offline | the artifact reader refuses every malformed file it should: coverage (A2COV1/A2COV2), snapshot headers, and raw artifacts through their `.meta.json` sidecars |
| `../gdiff/selftest.py` | gates | offline | the differential tools against real captures |
| `buscompare.py selftest` | gates | offline | missing/extra slot-bus records and field mismatches are detected |
| `revisionbisect.py --selftest` | gates | offline | tiny real CMake builds locate a planted change, reject a wrong boundary, check unchanged/reverted histories, build errors, nondeterminism and timeouts |
| `p0check.py --exe <binary>` | gates (`agentic`) | 3 and 5 | exact counts/profiles, SCC capture, WDM console, pipes, socket discovery, plus `snapshotcheck.py` saved-state comparisons and refusals |
| `interfacecheck.py`, `guestlivecheck.py`, `capturecheck.py`, `contentcheck.py`, `faultcheck.py`, `inputcheck.py`, `devicecoveragecheck.py`, `socketstreamcheck.py`, `lifecyclecheck.py` | gates (`agentic`, through p0check) | 3 and 5 | native state/SDK, bound guest views, live sinks, semantic bundles, real DOC/SHR content, boundary interrupts, timed inputs, optional-card observation noninterference, bounded socket streams and active-output shutdown |
| `sockettoolscheck.py`, `uicheck.py`, `mcpcheck.py` | gates (`agentic`, through p0check) | 3 and 5; long bridge wait on 3 | shared socket tool catalog/invocation, native presentation/clock settings on file/socket/co-pilot, MCP framing/lifecycle/native execution/startup configuration, and preservation of completed long-command replies |
| `observabilitycheck.py` | gates (`agentic`, through mcpcheck) | 3 and 5 | structured upstream trace/memory/search/text adapters, unreadable-I/O exclusions, bounded/truncated results, platform refusals, stable repeated extended trace formatting |
| `autonomycheck.py`, `executionshapecheck.py` | gates (`agentic`, through mcpcheck) | 3 and 5 | MCP-only upload/load/run/assert/PNG/evidence loop; cold-batch evidence, digest/scope/overwrite failures, evidence after quit, exact binary identity, real timeout recovery without resending, strict execution validation and already-satisfied PC goals |
| `contextcheck.py`, `experimentcheck.py` | gates (`agentic`, through p0check) | 3 and 5 | native topology/state/address/IRQ/scheduler/reference-pixel explanations, running context reads, bounded history/marks, snapshot witnesses, typed MCP adapters and correctness-bound experiment comparisons |
| ctest | dualsmoke | host | CPU conformance/coverage/cycles, 6522 VIA, checked snapshot comparison fixtures |
| `railcheck.ps1` | dualsmoke | 3 and 5 | CTRL verb honesty |
| `gs816/execcheck.ps1` | dualsmoke | 3 and 5 | run / step / bp / banked PCs |
| `gs816/altzpcheck.ps1` | dualsmoke | 3 and 5 | ALTZP / language card / RAMRD |
| `personalitycheck.ps1` | gates | every built-in configuration, platforms 0–5 | every machine cold-starts and answers `cpu`; expected count comes from source |
| `protocolcheck.ps1` | gates | 5 | protocol v2, `systems` discovery, JSON replies, ownership refusal, live SHR golden |
| `copilotcheck.ps1` | gates | 3 | the windowed co-pilot rail observes, controls, and refuses `poke` (its window cannot take focus) |
| `gs816/assertparity.ps1` | gates | 5 | the env and CTRL assert surfaces give the same verdicts |
| `unarmedhash.ps1` | gates | 3 and 5 | pinned stock boots reproduce `unarmed.baseline.json`; observation rails armed change nothing, every armed rail records, and trap records carry ROM, System Loader and GS/OS kernel names |
| `../hwtruth/gate.ps1` | gates | 5 | all 24 recorded emulator hardware verdicts stay pinned |
| `mbaudit.ps1` | gates | dual Mockingboard | MB-Audit v1.61's card-derived tests pass |

`personalitycheck.ps1` writes a JSON ledger to `$TEMP\a2rail-personalities\personality-results.json`
and derives the configuration count from `src/systemconfig.cpp`, exercising
every index rather than maintaining a second list.

P0 usage and formats are in `Docs/A2GSPU_RAILS.md`. `profile.py` consumes
`cov counts write` artifacts and explicit function ranges; invoke it with
`python -I tools/a2rail/profile.py counts.json --symbols functions.json`.
For an out-of-tree build, set `A2_GSSQUARED` to its executable and
`GSSQUARED_ROOT` to the source checkout; dualsmoke selects ctest beside that
executable. The full umbrella contains 14 gates; ctest/rail/exec/ALTZP
rows above are sub-suites of dualsmoke.

## Data tools

These write the name tables the emulator ships in `assets/symbols/`; none of them ships code
of the ROM or the operating system.

| Tool | Writes | From |
|---|---|---|
| `romfpt.py <rom> --sym <file>` | `apple2gs/<ROM house-FNV>.sym`, the ROM's toolbox entry points | the ROM's own tool pointer table |
| `romfpt.py --names <file>` | `apple2gs/toolbox.names`, tool set and function names for RAM-resident sets | the published call lists transcribed in the script |
| `gsosmap.py <volume.po>` | `apple2gs/gsos.segs`, GS/OS kernel segments and per-page hashes | the volume's `SYSTEM/GS.OS` (read with `prodosfs.py`) |

`<build>/snapdiff A.snap B.snap [report.json] [--expect-different]` compares
saved CPU/MMU/RAM/page-map/device bytes. It shares its checked reader with
CTRL/co-pilot `diff`; ctest runs `snapdiff --selftest`, and the agentic gate
runs the live IIe/IIgs checks in `snapshotcheck.py`. See
`Docs/A2GSPU_RAILS.md` for equality scope and the 0/1/2 exit contract.

## Local revision witness search (P2.4)

`revisionbisect.py` (or the `bisect.ps1` wrapper) builds selected local commits in a new
scratch directory and finds the first witness different from the starting
commit. For example, with UCRT64 tools on PATH:

```powershell
python -I tools/a2rail/revisionbisect.py --before d933c77b --after 834028cd --scenario tools/a2rail/bisect.example.json --scratch D:/gsqs/my-bisect --expect none
```

Copy/edit the example scenario for another machine, build recipe or guest input.
Its commands are argv arrays, executed without a shell. Paths `{source}`,
`{build}`, `{run}`, `{inputs}` and the integer `{repeat}` expand where available;
`{run}` exists only during guest runs. `executable` is relative to the build
directory. Optional `inputs` entries have `source`, relative `destination`, and
`scope` (`run`, the default, or `source`). Sources are resolved relative to the
scenario file, copied once, hashed, and copied into each fresh run/source tree;
they cannot overwrite different archived source bytes. Stage ignored ROMs as
source inputs when the selected checks need them. Stage writable disks as run
inputs and refer to their scratch paths in arguments or env values.

The scenario is trusted local code: its build/check commands must themselves
avoid pushes and writes to the source checkout. Phase-end publication is an
explicit closeout step (`Docs/HANDOFF.md` section 7), never a bisect scenario
side effect. The harness never
checks out, stashes, changes refs/remotes, fetches, registers a worktree, or
replaces the consumer executable. It archives tracked sources, creates detached
Git metadata only inside scratch (read-only object alternates preserve revision
identity), and copies initialized dependency bytes excluding `.git`. Dependency
checkout HEADs must match the selected gitlinks, which must remain fixed through
the range. Actual local dependency bytes, including any local changes, are
hashed and fixed for all builds; this does not certify clean vendor checkouts.

The search follows **first-parent history**. A binary boundary search is followed
by verification of every earlier candidate, so reversions cannot produce a
false first change. Matching endpoints trigger a check of the whole range.
The default limit is 64 revisions; every required revision must build and pass
the configured `checks`. The example runs ctest; add the full gate command and
required corpus/ROM inputs when that is the desired per-revision criterion.
Builds can take minutes and retained scratch trees can take several GB.

Every guest run fixes seed, time, RAM disk, BRAM, pacing and frame budget, strips
inherited instrumentation rails, and uses a fresh output directory. At least
two runs must agree on `status/hash/cycles/run` from exactly one `GSDIAG` line.
Required `PINS` identities must match across runs and revisions, excluding only
build/executable identity, which is expected to change and is recorded separately.
Use `-n` for headless execution as in the example. Build/check failures, missing
pins, timeouts, inconsistent repeats, changing inputs, and changed source
fingerprints are **inconclusive**, never a regression verdict.

`<scratch>/report.json` (`a2bisect-report-v1`) records full commit/tree IDs,
scenario/input/dependency/executable SHA256s, commands, log paths, check outcomes,
individual witnesses and the source-preservation check. Complete search exits 0;
`--expect change|none` exits 1 for an unmet expectation; invalid/inconclusive
search exits 2. No scratch directory is recursively deleted. `--selftest` needs
Git, CMake, Ninja and a C++ compiler on PATH and compiles small real fixtures.

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
