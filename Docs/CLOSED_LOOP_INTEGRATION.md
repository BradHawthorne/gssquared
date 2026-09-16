# Closed-loop integration — gssquared-fork as the toolchain's oracle

> **Mission (owner, 2026-08-08):** *"Our mandate for our fork is nothing is a black box and
> you have full instrumentation and control over it."* The fork is used by several projects
> for debugging and development, so its success condition is **completeness as a development
> platform**. The Rosetta toolchain's closed loop (build → boot → assert) is the first of those
> uses and the one this document is about; it is not the only one. The multi-consumer rules are
> in [`SHARED_FOUNDATION.md`](SHARED_FOUNDATION.md), the agentic contract in
> [`AGENTIC_ORACLE.md`](AGENTIC_ORACLE.md).
>
> *(An earlier version of this page, 2026-07-21, said the fork existed "solely" to be Rosetta's
> oracle and was "not a general-purpose emulator". Both were superseded within three weeks —
> by the multi-consumer rules and by constraint 3 below, which keeps the stock emulator whole —
> and the sentence stayed here contradicting them.)*

## Governing constraints

1. **The agent's eyes and hands.** Instrumentation is agentic-first: an agent must be able to
   observe *and deliberately manipulate* the machine (`load`, `poke`, `setreg`, `press`,
   `mount`, `save`/`restore`, `A2GSPU_POKE`, `A2GSPU_RUN_BIN`). Manipulation is labelled as
   such in every ack. Permanent fork, never merged away.
2. **Upstream sync is manual integration.** The fork pulls upstream in, never pushes out. Every
   ported upstream feature is brought under the instrumentation. The record of what was ported,
   skipped and deferred is [`UPSTREAM_SYNC.md`](UPSTREAM_SYNC.md).
3. **Dual-mode is a hard invariant.** With no `A2GSPU_*`/`MEMVU_*` environment the emulator is
   the stock windowed emulator, and instrumentation never taxes that use.
4. **Co-pilot.** Telemetry is orthogonal to windowed-vs-headless and human-vs-agent input: an
   agent can read state (`A2GSPU_COPILOT`) while a human drives the window.
5. **Parallel-safe.** Many agents drive one install. Every run writes into its own directory
   (`A2GSPU_OUT_DIR`, a per-run CTRL directory, a per-run working directory), and no run reads
   state another run left behind — which is why the battery RAM a run starts from is a pin
   (`A2GSPU_BRAM`) and not whatever `gs2-bram.bin` the working directory happens to hold.

## Operating modes

Instrumentation is orthogonal to these; any combination is valid.

| Mode | Window | Who drives | Instrumentation | Use |
|------|--------|-----------|-----------------|-----|
| Pure emulator | yes | human | off | ordinary Apple II / IIgs use |
| Headless agentic | no (`-n`) | agent (SPIKE or CTRL) | on | closed loop, CI, measurement |
| Co-pilot | yes | human drives, agent reads | on (read-mostly) | UI the agent cannot navigate |

Read-only verbs never perturb human input. Input-injecting verbs co-operate with live input
rather than locking it out. The co-pilot rail refuses memory and register splices (`poke`,
`load`, `setreg`); it accepts `pause`, `resume`, `key`, `press`, `reset`, `mount` and `quit`.

## The loop, as the toolchain drives it

```
edit source  →  Rosetta (assemble / compile / link / mint disk)
                    ↓
              binary / disk image
                    ↓
              gssquared-fork  (SPIKE one-shot, or CTRL session)
                    ↓
              GSDIAG verdict + asserts + records  →  back to the build
```

Two shapes, both specified with worked commands in
[`A2GSPU_RAILS.md`](A2GSPU_RAILS.md), section *The closed-loop contract*:

- **One-shot boot-to-verdict** — `A2GSPU_SPIKE_FRAMES=N … -n`. The process exit code is the
  gate (asserts, goldens); `GSDIAG:` is the richer verdict.
- **Multi-step session** — `A2GSPU_CTRL=<dir> … -n`, one `cmd.N` → `ack.N` at a time. Drive it
  closed-loop: the next command is a function of the last reply.

**`GSDIAG: status=OK` is not a success oracle for the software under test.** It means the run
completed without a fault the emulator can see. A boot can report `status=OK`, `gsos_err=$0000`
and a stable SHR hash while the program under test fails tens of thousands of toolbox calls: the
SHR hash of a Finder desktop tracks what the directory window shows, not whether a driver works.
Gate on what the program must do — `A2GSPU_ASSERT` on computed values, a `TRAP` record, or the
count of `carry=1` returns in an `A2GSPU_TBTRACE` — and prove the gate can fail against a known-bad
artifact before believing a green run.

The toolchain-side driver lives in the Rosetta tree (`scripts/differential/emulator.py`); this
page does not restate its interface.

## Launching it (Windows)

```bat
rem the MinGW UCRT runtime; without it the process exits 0xC0000135
set PATH=C:\msys64\ucrt64\bin;%PATH%
rem spike artifacts are written to the working directory, so give each run its own
cd /d <a per-run scratch directory>
set A2GSPU_SPIKE_FRAMES=300
set A2GSPU_SEED=1
set A2GSPU_FAKETIME=1780000000
set A2GSPU_RAMDISK=1
set A2GSPU_BRAM=default
<checkout>\build\GSSquared.exe -p 5 -d s7d1=<image>.po -n
```

The binary is `build/GSSquared.exe` (target `GSSquared`). The first output line is
`A2GSPU PINS:` — the run's identity; the last is `GSDIAG:`. Two runs with equal PINS lines must
agree on `GSDIAG`'s `hash=` and `cycles=`; `A2GSPU_EXPECT_HASH` makes that a gate.

## Upstream-sync discipline

- Pull upstream in by hand, feature by feature; bring each port under an instrument (a rail, a
  verb or a gate) as part of the port, and record it in [`UPSTREAM_SYNC.md`](UPSTREAM_SYNC.md).
- Keep instrumentation additive and separable: fork-only headers, thin gated call sites in core
  code. Measured, not assumed: about three quarters of the fork's added source lines are in files
  upstream does not have; the largest remaining in-file block is the CTRL/SPIKE control plane,
  which is why it lives in its own translation units rather than in `gs2.cpp`.
- Prefer probes and public accessors over reaching into internals, so an upstream refactor of the
  code an instrument observes does not silently change what the instrument reports.

## The dual-mode gate

Two halves, and only one of them is mechanical:

- **Mechanical** — `tools/a2rail/gates.ps1` runs the unarmed-hash gate (`unarmedhash.ps1`): a
  pinned IIgs and IIe boot must reproduce their recorded SHR/HGR hash and cycle count, and the
  same boots with observation rails armed must reproduce them too (observe-don't-disturb, proven
  rather than asserted).
- **By hand** — launch the windowed emulator with **no** instrumentation environment and use it.
  Nothing automates the interactive check, and no document should claim otherwise.

## History

The 2026-07-21 plan behind this page (task #87) was delivered:

| Planned | Where it landed |
|---------|-----------------|
| A first-class gssquared driver in the toolchain's differential harness | Rosetta `scripts/differential/emulator.py` |
| An accurate, current instrumentation contract | [`A2GSPU_RAILS.md`](A2GSPU_RAILS.md), checked by `railparity.ps1` against the registry and the dispatcher |
| Manipulation verbs | `poke`, `setreg`, `load`, `vram load`, `A2GSPU_POKE`, `A2GSPU_RUN_BIN` (there are no `patch` or `inject` verbs) |
| A runtime value harness | `A2GSPU_RUN_BIN` + `A2GSPU_ASSERT peek:` — proven by the toolchain's `rt_fixed_fmul_value` test |
| One tool instead of a fork per title | the title taps became `A2GSPU_TAP` / `A2GSPU_TRAP`; the remaining title configuration is `src/wiz5_config.hpp` (the standing exception in `AGENTS.md`) |
| Consolidated docs, tests, CI | the catalogue above; the gate set in `tools/a2rail/` |
