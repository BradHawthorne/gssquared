---
name: gs2-release
description: >-
  Cut GSSquared native package builds on macOS (local), Windows (SSH/MSYS2), and
  Linux (SSH), verify artifacts, collect them, and upload to GitHub Releases.
  Use when the user asks to cut a release, package builds, build release DMG/zip/AppImage,
  or upload GitHub release assets.
---

# GSSquared release packages

> **Fork policy (2026-10-16):** this workflow targets only
> `BradHawthorne/gssquared`, using `origin/a2gspu` by default. Host commands
> refuse a non-fork origin; upload refuses any other CLI/environment destination.
> Never target upstream. Package upload still requires the owner's explicit
> release request. Ordinary phase sync follows `Docs/HANDOFF.md` section 7 and
> does not run this package workflow. Policy fixtures are
> `python -I scripts/release/test_fork_policy.py`; actual host package builds
> require configured hosts and separate platform evidence.

Orchestrator: `scripts/release/gs2-release` (Python 3.9+, stdlib only). Native builds only — do not cross-compile.

```bash
scripts/release/gs2-release <ping|unpushed|sync|build|status|collect|upload> [options]
```

## Setup

If `scripts/release/hosts.toml` is missing, copy `scripts/release/hosts.toml.example` there, tell the user to fill in Windows/Linux SSH aliases and repo paths, and **stop**. Do not invent hostnames.

## Workflow

Copy this checklist:

```
Release packages:
- [ ] hosts.toml present
- [ ] ping
- [ ] unpushed
- [ ] sync (origin/a2gspu)
- [ ] build all (background; poll status)
- [ ] collect
- [ ] wait for explicit upload
```

1. **Ping.** `scripts/release/gs2-release ping`  
   Fix SSH/toolchain/repo issues before continuing. `--platform macos|windows|linux` to test one host.

2. **Unpushed.** `scripts/release/gs2-release unpushed`  
   On each host's everyday clone, **no fetch**: fail if `HEAD` is ahead of the existing `origin/a2gspu` ref (`git rev-list --count origin/a2gspu..HEAD`). Dirty files are ignored. Close the coherent phase and sync it under handoff section 7 before package sync; do not discard local commits.

3. **Sync.** Detached worktree beside the clone (`<repo>-release`, e.g. `~/src/gssquared-fork-release`). Default ref is **`origin/a2gspu`** after fetch. The everyday clone is not checked out and may stay dirty.
   `scripts/release/gs2-release sync`  
   `scripts/release/gs2-release sync --ref <published-fork-tag>`
   `--force-clean` only affects the release worktree (`git clean -fdx`, wipes ignored `build/`).

4. **Build.** Package recipes match README (not the `GS2_PROGRAM_FILES=ON` dev build):
   - macOS: app bundle + CPack `.dmg`
   - Windows: CPack `win64` zip (MSYS2 UCRT64 GCC/CMake/Ninja; runtime on PATH)
   - Linux: `appimage` target

   Builds take a long time (vendored SDL). Start in the background (`block_until_ms` 0). Poll; do not kill with SIGTERM to “check”.

   ```bash
   scripts/release/gs2-release build --platform all
   scripts/release/gs2-release status
   ```

   Logs: `.release/logs/<platform>.log`. Status JSON: `.release/status.json`.

   On any `state: fail`, **stop**. Do not collect/upload. Show the log tail and the error.

5. **Collect.** After all platforms are `ok`:  
   `scripts/release/gs2-release collect`  
   Files land in `.release/dist/`. Report names and sizes.

6. **Upload.** Only after the user explicitly says to upload (a tag). Never implied by build/collect.

   ```bash
   scripts/release/gs2-release upload --tag vX.Y.Z
   scripts/release/gs2-release upload --tag vX.Y.Z --create   # if the GitHub release does not exist
   ```

## Do not

- Cross-compile or package Windows/Linux on the Mac
- Bump `VERSION_*` in CMakeLists, write changelog, or `git tag` unless the user asked
- Run `upload` without an explicit tag and go-ahead
- Put secrets in `hosts.toml.example`
