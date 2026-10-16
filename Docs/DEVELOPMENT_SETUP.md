# Developer setup for this fork

Read [HANDOFF.md](HANDOFF.md) and [AGENTS.md](../AGENTS.md) before changing
shared behavior. The repository README gives the base build prerequisites;
the default editor tasks use a separate `build-local` directory and never
replace the owner's protected `build/GSSquared.exe`. The owner separately
approved its 2026-10-16 refresh; the current binary, complete validation and
whole-build rollback are recorded in
[the consumer refresh report](reports/gssquared-consumer-refresh-20261016/README.md).

On this host, direct task-local build/test scratch to `D:/gsqs/tmp` with `TEMP`,
`TMP` and `TMPDIR` in the launched process environment. The low-space cleanup
preserved inactive C: tests under `E:\agent-temp-archive`, with hash manifests
and forwarding notes. Application-wide environment settings remain unchanged.

## Editor configuration

`.vscode/tasks.json` separates configure and build tasks and uses Ninja with
`GS2_PROGRAM_FILES=ON` for a developer executable. Windows overrides select
MSYS2 UCRT64 GCC/CMake and add its runtime directory to PATH. Install Ninja
and keep it on PATH. Other hosts use their configured `cmake`/compiler.
`build-debug` is the one default build task. CMake Tools also points to
`build-local`, with automatic configure-on-open disabled.

The launch entries require the CodeLLDB extension (`type: lldb`). They refer
to current CMake targets and workspace-relative paths. macOS Instruments
profiling tasks require `xcrun` and are macOS-specific. The configuration
audit checks JSON, task dependencies and target existence; it does not claim
an interactive debugger or Instruments session was tested on every host.

CPU fixture launch entries ask for a working directory. `cputest` expects
the selected Klaus Dormann `.bin` in that directory. `cpu816test` currently
probes `../snes-tests/cputest/cputest-full.sfc` relative to its working directory;
select a directory matching that layout. These third-party inputs are not
supplied by the editor configuration. Missing fixtures exit 77, a declared
skip rather than a passing conformance test. These executables are separate
from the native CTest/gate evidence.

The old repository-root `launch.json` targeted a nonexistent `hello_world`;
it was removed. `.vscode/launch.json` is the maintained editor launch file.

## Test and publication identities

Set `A2_GSSQUARED` to the tested executable and `GSSQUARED_ROOT` to its source
checkout before running `tools/a2rail/gates.ps1`. Native CTests come from the
selected executable's build directory. Preserve run/corpus pins and identify
the executable bytes in evidence. File-session receipts bind exact executable
path and native UTC process-start ticks, including renamed executables. A
recycled/unrelated PID or incomplete receipt refuses before publication. Restart
old sessions whose receipts predate these identity fields; PID/name guesses are
not a fallback.

When validating exported source, freeze the implementation before compiling.
If a reviewed overlay is copied into an already built export, copy changed
contents with a current destination timestamp and verify that Ninja recompiles
the affected targets. Preserving an older source mtime can hide changed content
from an incremental build. The fleet phase's end-to-end capability check caught
exactly that stale-object case; it now requires the advertised schema option.
Use a clean build when object provenance is uncertain. A configure-time Git
label alone does not identify a dirty overlay: retain input digests, executable
identity, native capability proof and actual validation results.

`scripts/release/gs2-release` and its local skill target only the owner's fork:
the default ref is `origin/a2gspu`, host commands refuse non-fork origins and
uploads refuse any destination other than `BradHawthorne/gssquared`, including
CLI/environment overrides. The Windows recipe selects UCRT64 GCC/Ninja.
Policy fixtures run with `python -I scripts/release/test_fork_policy.py` without
SSH, fetch or upload. Actual package builds still need configured host evidence
and an explicit package-release request. Ordinary validated phase publication
uses the fast-forward destination in handoff section 7.
