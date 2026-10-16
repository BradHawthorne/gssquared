# gssquared — `a2gspu` development fork

An instrumented Apple II / IIgs emulator for **agentic closed-loop software development**,
built on **[gssquared](https://github.com/jawaidbazyar2/gssquared)** by Jawaid Bazyar.
This fork pairs with the Rosetta v3 toolchain: build software, execute it, inspect the
emulated architecture, challenge the result and use the evidence to improve the next build.
The published development branch is **`a2gspu`** (the default).

Ordinary windowed emulation remains the default; instrumentation is opt-in. This branch
also carries fidelity fixes and selected upstream ports, so its default behavior is
not a claim of identity with every upstream revision. Upstream already provides a
debugger protocol; the fork extends it with shared agent tools, evidence and validation.

> Upstream integration is one-way: selected changes come into this fork by hand.
> We publish to the owner's fork, never push changes or open PRs upstream.
> For the canonical emulator and its releases, visit
> **[upstream](https://github.com/jawaidbazyar2/gssquared)**.

## What an agent can do

| Capability | Available interfaces and evidence |
| --- | --- |
| Discover the machine | Live tool/rail manifests, compiled and installed device topology, absent models and explicit availability limits |
| Observe and explain | CPU/memory/device state, address routing, IRQs, stops, scheduler queues, source references, bounded event history, marks and typed deltas; context reads can sample a running machine |
| Drive execution | Load bytes, set registers, step/run to declared goals, inject input, mount media, use boundary fault injection and inspect completion reasons |
| Capture and assert | Toolbox/GS/OS traces, SCC A/B TX and WDM consoles, memory/video/audio artifacts, coverage/counts/profiles, assertions and golden comparisons |
| Compare experiments | Snapshot-file and semantic checkpoint diffs, context-bound cycle/instruction/code-size comparisons and local revision witness search |
| Exchange evidence | Scoped SHA256-checked MCP input uploads and artifact reads, including native PNGs as MCP image results |

The file CTRL rail and debug socket share native handlers. Python clients and a
stdlib MCP adapter expose those handlers with discovery and native verdicts.
**As of 2026-10-16:** 74 native verbs, 85 MCP tools, 161 rails and 32 socket
wire types. Discover the running executable rather than assuming these counts.
See [Machine context](Docs/MACHINE_CONTEXT.md), the
[rail/command reference](Docs/A2GSPU_RAILS.md) and the
[Python/MCP contract](Docs/DebugClient.md).

Snapshots declare their captured blocks and compatibility checks; they can shorten
edit/run loops for supported configurations. They are not a complete deterministic
replay capsule. Reference pixels explain supported HGR/SHR source models, not GPU/CRT
readback. Sampled changes can miss transients, and retained parent links are not
universal data-flow or last-writer tracking. Full replay, complete device semantics,
DWARF variables/unwind and physical-IIgs validation remain work in progress.
The [roadmap](Docs/AGENTIC_ROADMAP.md) records completed subsets and remaining gaps.

## Rosetta and the development loop

```text
source/specification → Rosetta build → raw image or bootable disk
        ↑                                     ↓
refine candidate ← compare retained evidence ← run / observe / assert
```

Both direct raw-image execution and GS/OS disk-boot tests are supported workflows.
The additive [`experiment.py`](tools/a2rail/experiment.py) companion covers declared
raw fixtures: explicit build tools, source/image identities, independent result checks,
negative controls, native context and compatible cycle/instruction/byte comparisons.
Optional [CIDAR bindings and the real block-copy pilot](Docs/CIDAR_EXPERIMENTS.md)
join versioned specs/candidates to source/build/load identities, preserved inputs
and registers, per-PC costs and checked CPU write footprints.
[Native fleets](Docs/FLEETS.md) run explicit independent matrices with staged
inputs, checked event capture, per-cell assertions and complete aggregate verdicts.
It is not an OMF loader or a full GS/OS build/boot harness. Existing Rosetta
differential and disk-boot tests are separate lanes; see
[Closed-loop integration](Docs/CLOSED_LOOP_INTEGRATION.md).

An oracle is only as useful as its checks. `GSDIAG: status=OK` says the emulator
detected no run fault; it does not prove the guest program succeeded. Pin the inputs,
seed, time and BRAM, assert the behavior the program must produce, and prove the
check fails on a known-bad case. Golden images cover their declared render model;
emulated ROM execution is not a measurement on physical hardware.

The long-term CIDAR direction is to recover contracts and lineage from historical
software, then refine, fuse and specialize primitive libraries through adversarial
experiments. Repeated archive boilerplate does not establish optimality. Archive-wide
ingestion and automatic library promotion are future goals; current evidence supports
measured results for declared workloads. See the
[architectural-refinement contract](Docs/AGENTIC_ORACLE.md#10-design-review-for-architectural-understanding).

This is a shared foundation for Rosetta, a2engine, a2tile, a2os, a2gpu/a2vga and
interactive use. Contracts remain general across consumers and across IIe/IIgs.
See [Shared foundation](Docs/SHARED_FOUNDATION.md).

## Start an agent session

After building the emulator, run this from the checkout with Python available.
Replace both placeholders with absolute paths; on Windows the executable is
`GSSquared.exe` and its MSYS2 UCRT64 runtime directory must be on `PATH`.

```bash
python -I tools/a2rail/mcpserver.py --exe <tested-executable> --platform 5 --out-root <runs-directory>
```

Configure that command as a stdio MCP server in your agent client. It creates a fresh
session directory, pins startup inputs, verifies the native bootstrap and starts the
machine paused. Use discovery, explicitly pause before ordinary tools, load inputs,
run and inspect the result. Context read queries also work while running. Installed
`gs2-mcp` provides the same entry point; attach with `--socket <endpoint>`.
Transport timeouts do not cancel execution: recover the pending reply instead of
resending a mutation. Managed close-out uses bounded waits and retains unresolved
requests and forced termination in the owner receipt; attach only disconnects.
See the [client setup and ownership contract](Docs/DebugClient.md)
and [Python package](clients/python/README.md).

For file-based sessions, use [`session.ps1`](tools/a2rail/session.ps1) and
[`ctl.ps1`](tools/a2rail/ctl.ps1), one command and acknowledgement at a time.
One-shot headless runs use `A2GSPU_SPIKE_FRAMES`, `A2GSPU_ASSERT` and optional goldens.
The [rail reference](Docs/A2GSPU_RAILS.md) gives detailed launch recipes,
including windowed co-pilot. Keep each driven run's outputs in its own directory.

## Build and validate

Use Git, CMake, Ninja and the platform's compiler/development prerequisites.
Initialize the vendored SDL dependencies before configuring:

```bash
git submodule update --init vendored/SDL vendored/SDL_image vendored/SDL_net
git submodule update --init --recursive vendored/SDL_ttf
cmake -S . -B build-local -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-local --parallel
ctest --test-dir build-local --output-on-failure
```

On Windows, use **MSYS2 UCRT64** GCC/CMake/Ninja, with `/ucrt64/bin` first on
`PATH`; keep that directory on `PATH` when launching the executable too. A short
checkout path avoids SDL_ttf's nested dependencies exceeding Windows path limits;
enable Git `core.longpaths` if needed. Base platform prerequisites and UI instructions
are in [upstream's README](https://github.com/jawaidbazyar2/gssquared) and the
[User Documentation](Docs/index.md). Contributors should first read
[AGENTS.md](AGENTS.md) and the [current handoff](Docs/HANDOFF.md), including the
protected consumer-build rule for the owner's existing checkout. Its separately
approved 2026-10-16 refresh passed all 14 gates and 14 native tests; the
[consumer refresh report](Docs/reports/gssquared-consumer-refresh-20261016/README.md)
retains its exact binary identity, tested MCP configuration and whole-build rollback.
Maintained editor tasks and package-tool policy are described in
[Developer setup](Docs/DEVELOPMENT_SETUP.md); package destinations are restricted
to this fork.

The fork gate stack is `tools/a2rail/gates.ps1` (PowerShell 7). For the build above,
set `A2_GSSQUARED` to the absolute `build-local/GSSquared.exe` path on Windows
and `GSSQUARED_ROOT` to this checkout before invoking it. `-Offline` runs five
offline suites; the full stack runs 14 suites, including native CTests, both
platforms, discovery parity, agent interfaces and pinned unarmed/armed boot witnesses.
Missing corpus inputs fail the relevant gates. Some ROM personalities also require
local ignored ROM files; a fresh clone alone cannot reproduce every check. Supply
the inputs described in [corpus/README.md](corpus/README.md) and the
[handoff](Docs/HANDOFF.md#3-how-to-build-and-verify). A manual ordinary-windowed
check remains separate from automated gates.

The [2026-10-16 publication checkpoint](Docs/reports/gssquared-publication-20261016/README.md)
records a fresh exported-source Release build: **14/14 gates, 14/14 native CTests,
21/21 Python tests and 3/3 affected Rosetta checks**. Its 103-vector CPU differential
classified 100 identical results and three known COP/BRK stop-policy differences.
These results describe the recorded inputs and executable, not every possible guest.

Completed phases are reviewed, validated, committed and fast-forward synced to the
owner's `a2gspu` branch. README freshness is part of that closeout; the
[handoff](Docs/HANDOFF.md#7-phase-completion-commits-and-github-sync) owns the policy.
The [upstream ledger](Docs/UPSTREAM_SYNC.md) records selected ports and deferred work.
The separate slot-engine model remains unmerged; drift B–E and consumer executable
replacement remain separate owner decisions.

## Credits

Code merged from upstream carries its own attributions, repeated here because this README replaces
upstream's: the Host FST is Kelvin Sherlock's GSPlus code (GPL 2.0); the Apple Mouse II Interface
Card emulation (PIA + 6805 protocol) is adapted from the
[A2Pico mouse-interface](https://github.com/oliverschmidt/mouse-interface) firmware by Thorsten Brehm
and Oliver Schmidt (MIT License); the ADB mouse tracking technique is based in part on the one used in
KEGS (GPL 2.0). The ThunderClock Plus card derives from the protocol and hardware work in
[web-a2e](https://github.com/mikedaley/web-a2e) by Mike Daley, [a2fpga_core](https://github.com/BrentRector/a2fpga_core)
by Brent Rector, and MAME's `a2thunderclock` / `upd1990a` by R. Belmont; its firmware image is the
Thunderware 2716 (MAME CRC `1b99c4e3`).

The emulator core, models, and UI are **Jawaid Bazyar's** work. This fork inherits upstream's license;
our additions are offered under the same terms. Thanks to the upstream project for a clean, hackable
IIgs emulator to build on.
