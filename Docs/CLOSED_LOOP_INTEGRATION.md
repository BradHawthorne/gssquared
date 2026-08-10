# Closed-Loop Integration — gssquared as Rosetta's oracle

> **Mission (Brad, 2026-07-21):** this gssquared exists *solely* to be the fully-instrumented
> closed-loop tool for Rosetta toolchain work. It is Rosetta's oracle/microscope, not a
> general-purpose emulator. Every refactor optimizes toward that single mission. Built on
> top of the in-flight fidelity WIP (accurate emulation = a trustworthy oracle).
>
> **Agentic north star (2026-07-31):** *nothing is a black box.* Agentic AI must have
> maximal control, observability, and **named understanding** of every answer (cycles,
> colour profile, memory path, snapshot class). See [`AGENTIC_ORACLE.md`](AGENTIC_ORACLE.md).
> Live discovery: CTRL verbs `oracle`, `help`, `manifest <file>`.
>
> **Multi-consumer (2026-08-10):** the same instrumented surfaces also serve other product
> trees (a2engine, a2tile, a2os, a2gpu/a2vga, …). Rosetta closed-loop remains the primary
> mission; product sessions may drive fixes, but rail/device contracts stay general.
> See [`SHARED_FOUNDATION.md`](SHARED_FOUNDATION.md).

This is the design spec for task #87: make **all** closed-loop work coherently and fully
integrated into gssquared, refactoring as needed to fulfill all needs.

## Governing constraints (Brad, 2026-07-21)

These three shape every decision below:

1. **Permanent fork = the agent's eyes + hands.** Maximally instrumented for Rosetta AI
   *agentic closed-loop interactive* use. It is the eyes the agent has to directly see
   toolchain output — so the agent can validate **and directly manipulate** the work
   (full coherence + internal testing). ⇒ instrumentation is **agentic-first** and includes
   **manipulation** verbs (poke / patch / write / inject), not just observation. This is a
   permanent fork, never merged away.
2. **Periodic upstream sync = ongoing manual feature integration.** We periodically pull
   relevant actively-updated pieces from upstream main gssquared for feature parity. We have
   deviated substantially, so this is **manual** integration; every integrated upstream
   feature must **coexist with and be brought under** our instrumentation. ⇒ keep our
   instrumentation **additive and separable** (isolated hooks / the a2gspu device / env-gated
   layers) so upstream merges stay tractable. See the *Upstream-sync discipline* section.
3. **Dual-mode is a hard invariant.** Brad also runs disk images manually on this build as a
   **regular emulator** (windowed, interactive, any Apple II software). ⇒ gssquared MUST
   retain full normal-emulator interface mode(s); all instrumentation is **env-gated OFF by
   default** and must **never degrade the regular-emulator UX**. "Observe-don't-disturb" is
   not just for the guest — it is for the human operator's normal workflow too.
4. **Co-pilot (human-in-the-loop with live telemetry).** There will be times the agent guides
   Brad to **manually drive the windowed emulator** through UI navigation the agent struggles
   with (visual/UI reasoning), **while full instrumentation telemetry stays live** so the
   agent keeps seeing state/memory/traces and can read what the manual navigation reached,
   then guide the next step or resume automated work. ⇒ instrumentation is **ORTHOGONAL** to
   the windowed↔headless and human-input↔agent-input axes: telemetry works with a window open
   and human input flowing, and the agent's **read-only** observation (`read`/`cpu`/`text`/
   `iolog`/traces/`GSDIAG`) coexists safely with live keyboard/mouse. Any combination is valid.

## Operating modes (instrumentation is orthogonal, not a mode switch)

| Mode | Window | Who drives | Instrumentation | Use |
|------|--------|-----------|-----------------|-----|
| **M1 Pure emulator** | yes | Brad (kbd/mouse) | **OFF** | Brad's normal Apple II / project use (constraint 3) |
| **M2 Headless agentic** | no (`-n`) | agent (CTRL rail) | **ON** | automated closed loop / CI / value-harness (constraint 1) |
| **M3 Co-pilot** | yes | Brad drives UI | **ON (live)** | agent guides Brad through UI it can't navigate, reads telemetry live, hands back (constraint 4) |

Design rule: the telemetry rails (`read`/`cpu`/`text`/`shot`/`iolog`/`dis`/`stack`/traces/
`GSDIAG`) must **not** require `-n`. They attach the same way whether the window is open or not.
Read-only verbs never perturb human input; input-injecting verbs (`keys`/`press`/`poke`/`patch`)
are documented as co-operating with (not locking out) live human input. Handoff is seamless:
Brad clicks → agent `read`/`cpu` to see the reached state → agent issues the next instruction
or resumes M2 automation.

## The incoherence (why this exists)

Closed-loop work is scattered across three places and the loop does **not** close on the
tool we own:

1. **rosetta_v3 `scripts/differential/emulator.py`** — the harness has only STUBBED drivers
   for GSplus / KEGS / MAME; every `run_to_prompt()` returns `exit_reason="stub"`,
   "not yet implemented". So the differential / execution-gate leg can run on **no** emulator.
2. **this gssquared (`a2gspu` branch)** — already a capable oracle, but the harness never
   drives it. It has, today:
   - `A2GSPU_CTRL=<dir>` cmd.N→ack.N rail (gs2.cpp:1621, 20 Hz atomic files): verbs
     `run/keys/press/holdkey/text/shot/read/mount/save/restore/cpu/dis/iolog/quit`.
   - `GSDIAG: status=… rc=… gate=… gsos_err=$… brk=… scb=… hash=…` summary line
     (iigs_diag.hpp:132) — the machine-readable run verdict.
   - Headless spike: `A2GSPU_SPIKE_FRAMES=N` + `-n` (deterministic, no window);
     `_SPIKE_KEYS`, `_TEXTDUMP`.
   - Tracing: `A2GSPU_TBTRACE` (Tool Locator + GS/OS class-0/1 dispatch), `_ERRHOOK`,
     `_BRKDUMP`, `_STOP_ON_FAULT`, `A2GSPU_BREAK=BBAAAA` (24-bit PC), symbol tables.
   - `A2GSPU_RUN_BIN`, snapshot save/restore, CPU/MMU self-tests, `iigs_diag.hpp`.
3. **`D:\projects\wiz\gssquared-wiz`** — this gssquared **+ 2 unmerged files**
   (`wiz5_session.hpp`, `wiz5_tap.hpp`) — a fork-per-title instead of one tool.

## Target architecture — the single coherent substrate

### 1. Keystone: `GssquaredDriver` in the harness (make the loop close on our tool)
Add a first-class driver to `rosetta_v3/scripts/differential/emulator.py` alongside the
(retained but deprioritized) GSplus/KEGS/MAME stubs:
- `detect()` — `ROSETTA_EMULATOR_GSSQUARED` env, then `D:\projects\a2vga\gssquared\build\gs2(.exe)`,
  then PATH. gssquared is the **default/preferred** driver.
- `run_to_prompt(disk_image, timeout, capture_memory)` — spawn headless:
  `gs2 -p 5 -d s7d1=<image> -n` with `A2GSPU_SPIKE_FRAMES` (or `A2GSPU_CTRL`) + `A2GSPU_STOP_ON_FAULT`,
  parse the `GSDIAG:` line for `status/rc/gsos_err/brk` → `EmulatorState.exit_reason`
  (`prompt-reached` / `crashed` / `timeout`).
- `capture_state` — `screen_text` from `A2GSPU_TEXTDUMP` / `text` verb; `memory_regions`
  from the `read <bank>:<addr> <len>` verb for each requested `(start,len)`.
- Prefer the CTRL rail for multi-step scenarios; the one-shot SPIKE for simple boot-to-verdict.

### 2. Instrumentation — LARGELY ALREADY PRESENT (2026-07-21 re-scope)
⚠️ The "Gaps" list in `A2GSPU_INSTRUMENTATION.md` is **badly stale**: a live audit found
**~140 `A2GSPU_*` rails** already implemented, covering nearly everything that doc calls a gap:
- memory read: `A2GSPU_DUMP` (bank-aware; `$E0`/`$E1` direct, others via MMU), `peek:` in `A2GSPU_ASSERT`
- **manipulation**: `A2GSPU_POKE`/`POKE_NTH`, `A2GSPU_REG_*` (register writes), `A2GSPU_FORCE_KEY`
- breakpoints/watch: `A2GSPU_BREAK`, `PCTRAP`, `CONDTRAP`, `STACKTRAP`, `WATCH`/`WATCH_READ`/`WATCH_CHANGE`, `VALTRAP`
- trace/step/stack: `ITRACE`(+`_FRAME/_FROM/_N`), `CALLTRACE`/`CALLSTREAM`, `STACKWATCH`, `LOADTRACE`, `LCTRACE`
- video/mode: `VIDEOMAP`, `VIDEOSUM`, `MODETRACE`; determinism: `SEED`; render golden: `GOLDEN`
- snapshot: `SNAP`/`SNAP_SAVE`/`SNAP_LOAD`/`RESTORE`; run: `RUN_BIN`, `RUN_BOOT`; assert: `ASSERT`
- CTRL rail verbs (gs2.cpp ~1690): `run`, `keys`, `press`, `text`, `shot`, `read`, `mount`,
  `save`/`restore`, `cpu`, `dis`, `iolog`, `holdkey`, `quit`

So item 2 is **mostly DONE**. The real remaining instrumentation work is a **residual audit**:
compare the ~140 rails against the closed-loop needs and add only what is genuinely missing
(e.g. a live CTRL-rail equivalent of an env-only rail, or a specific `cpu` field like full
`PBR:PC`+E+halt if still masked). Do NOT re-implement rails that already exist. The larger,
higher-value need is **item 5 (an accurate, current contract doc)** — the rich rail set has no
coherent catalog and the gap doc actively misleads.

Also add the **manipulation** half (constraint 1 — the agent's *hands*): `poke <bank:addr>
<bytes>`, `setreg <reg> <val>`, `patch <bank:addr> <bytes>` (live code patch), and `inject`
paths — so the agent can not only observe but *directly manipulate* the running toolchain
(patch a routine, seed a value, force a branch) inside the closed loop. All env-gated / CTRL-only.

### 3. Runtime VALUE-HARNESS verb (closes the `-ffixed` follow-up on our substrate)
A verb/flow to **load an `.omf`/S16, run it, and read back a memory value** so the harness
can assert *computed values*, not just boot success. This is the proper runtime-value test
for the fixed-point runtime (`~FMUL`, transcendentals) that the audit (#86) flagged — it
replaces the "structural regression guard" with a real value check on the emulator we own.
Likely: extend `A2GSPU_RUN_BIN` to run a linked S16 to completion (or to a sentinel BRK),
then `read <bank:addr>` the result cell; wrap it in a rosetta_v3 ctest that compiles a tiny
`-ffixed` program, runs it via gssquared, and asserts e.g. `256*256==65536`, `sin(pi/2)≈1.0`.

### 4. Merge the wiz taps → one gssquared (no fork-per-title)
Fold `wiz5_session.hpp` / `wiz5_tap.hpp` from `gssquared-wiz` into this tree as a **generic,
title-agnostic tap mechanism** (env-gated, off by default), so title-specific RE
(Wizardry, etc.) is a configuration of the one oracle, not a divergent fork. Then
`gssquared-wiz` reduces to a scenario/config on top of this tree.

### 5. Consolidate — one contract, tests, CI
- One instrumentation-contract doc (fold `A2GSPU_INSTRUMENTATION.md` +
  `Docs/HeadlessDiagnostics.md` + this file into a coherent set; mark gaps closed as they land).
- Tests: the existing CPU/MMU self-tests + a headless boot-forward smoke + the value-harness
  ctest. Wire a CI target that runs the headless spike on a known image and asserts the
  `GSDIAG:` verdict.

## Upstream-sync discipline (constraint 2)
Our fork has deviated substantially, so parity with upstream `main` is **recurring manual
integration**, not a merge. To keep it tractable:
- **Isolate our instrumentation** so it reads as a clean additive layer over stock behavior:
  concentrate it in the `a2gspu` device, the `A2GSPU_*`/`GSDIAG` env-gated hooks, and thin
  call-sites — minimize edits scattered through core CPU/MMU/video so an upstream diff mostly
  touches code we did *not* fork.
- Keep `D:\projects\a2vga\gssquared-upstream` as the pristine tracking clone; diff upstream
  main against it to find "relevant actively-updated pieces," then hand-port each into our
  fork **and bring it under instrumentation** (env-gated hook + doc entry) as part of the port.
- Record each sync in a running ledger (what upstream commit range, what was ported, what was
  intentionally skipped as fork-divergent). Ties task #61 (upstream accuracy merge).
- Every instrumentation verb we add should be written to survive an upstream refactor of the
  code it observes (prefer public accessors / MMU reads over reaching into internals).

## Proven launch recipe (verified 2026-07-21 on glass)
The main binary is **`build/GSSquared.exe`** (target `GSSquared`, not "gs2"). A headless
spike boot WORKS today:
```
# ⚠️ ucrt64 runtime DLLs MUST be on PATH or the exe dies with 0xC0000135 (STATUS_DLL_NOT_FOUND)
PATH=C:\msys64\ucrt64\bin;%PATH%
cd D:\projects\a2vga\gssquared\build
set A2GSPU_SPIKE_FRAMES=180
GSSquared.exe -p 5 -d s7d1=..\_finder.po -n
```
Verified output (exit 0): `A2GSPU SPIKE: headless mode enabled` → SHR-window hash
(`SPIKE E1: ... hash=…`), a 38606-write SHR trace (`content-hash=…`), bus-transaction MATCH
proof, slot/MMU state dumps, and `spike_frame.bmp`. So the `GssquaredDriver` parses these
`SPIKE …`/`GSDIAG:` lines + hashes for `capture_state`. **The DLL-on-PATH requirement is the
one launch gotcha the driver's `detect()`/spawn must handle** (prepend ucrt64 bin to the
subprocess env PATH on Windows).

## Build & verify
- gssquared is CMake (`build/` already configured). Build: `cmake --build build`.
- **Preserve the 17 dirty fidelity files** (video/disk/sound + roms.json + the instrumentation
  MD) — all closed-loop work is **additive on top**; never revert them.
- Verify each seam: gssquared builds; the `GssquaredDriver` boots a real image headless and
  captures state **on glass** (the authoritative rail); the value-harness reads a known
  fixed-point result. Adversarially verify.
- **Dual-mode gate (constraint 3):** every change must be proven to leave the **normal
  windowed emulator** path unchanged — launch `gs2 -p 5 -d s7d1=<image>` with **no** `A2GSPU_*`
  env set and confirm identical interactive behavior. Instrumentation OFF-by-default is a
  release gate, not a preference.
- gssquared is a **separate git repo** — commit/push only when Brad asks.

## Ties
Task #87 (this). Closes the audit #86 runtime value-test follow-up. Consolidates the intent of
#59 (Observatory/fidelity), #60 (card-snoop rail), #61 (upstream accuracy merge) under the
single closed-loop mission.
