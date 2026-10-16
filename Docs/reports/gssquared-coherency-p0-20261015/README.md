# Coherency audit and approved P0 continuation (2026-10-15)

The owner requested a coherency audit and continuation of the handoff, then
explicitly approved **P0.1-P0.4**: SCC capture, counting coverage, a WDM console,
and socket discovery. Drift B-E, merging the engine model, and replacing the
consumer-facing binary remain separate decisions.

## Repository state and corrected claims

The audit began on a clean `a2gspu` at `834028cd`, 68 commits ahead of cached
`origin/a2gspu`. The continuation is uncommitted working-tree changes on that
branch. Nothing was pushed, no remotes changed, and no branches/worktrees were
created. The cached upstream ref remains `643e6c21`; it was not refreshed from
the network. No archive was modified, and the forbidden credential archive was
not opened.

| Claim / mismatch | Verified state and action |
| --- | --- |
| HANDOFF said `9a62b013`, about 66 ahead | Actual base was `834028cd`, 68 ahead. Updated HANDOFF and roadmap |
| Proposal was “being finished” | It landed in `d933c77b`, before the north-star commit. Fixed current handoff |
| Upstream socket had 27 types | Its server declares **28** types, including ERROR/EVENT. The new MANIFEST brings this fork to **29**. Historical reports remain snapshots; current summaries explain the correction |
| Inventory said 65 verbs / about 144 rails / 42 live setters | Rail parity measures **66 CTRL verbs, 157 base rails (160 after P0), and 70 live setters**. Corrected current summaries and kept historical measurements dated |
| WDM had no building blocks / was only a NOP | `cpu_state::wdm_handlers[256]` and opcode dispatch already exist; Host FST registers `$FF`. The new console registers `$FD` and `$FE`, without changing the opcode implementation |
| A2MAP1 could supply function names | It binds binary segment bytes and identity. Function names/ranges come from a separate `a2symbols-v1` file; mapped profiles bind it with the binary-plan hash |
| Engine report head/check count | The historical report describes `c94d5d4f` and 53 initial checks; the separate branch is now `0dd0216d`, with the later block-device/boot additions. It is still unmerged |
| Engine example appeared usable on mainline | Its fixture exists only on the engine branch. HANDOFF now says so |
| Snapshot status said RTC, clock and serial were unsaved | Source has RTC, clock, SCC and SSC blocks. Corrected the current status table; each saved file's manifest remains authoritative |
| Fork build and verbatim upstream shell instructions conflicted | Fork UCRT64/Ninja instructions take precedence. Commands run through MSYS2 bash; fork-mandated PowerShell gates are invoked from bash |
| Out-of-tree emulator override could test stale in-tree ctest executables | `dualsmoke` now runs ctest beside the selected executable |

The archived catalog and project summary were read first, then compared with
local history, branch state, the handoff, roadmap, reports, registry, dispatchers,
CPU, SCC, coverage/snapshot implementation and Python client. Historical reports
were preserved rather than rewritten to look like fresh measurements.

## Implemented contracts

### P0.1: completed SCC TX capture

`A2GSPU_SCC_A` and `A2GSPU_SCC_B` accept `file:<path>` or
`pipe:<existing-endpoint>`. They observe completed transmitted bytes, after
SCC timing, outside local loopback, while leaving the configured serial device
connected. The launch hooks and TX hook are thin; sink implementation lives in
fork-owned headers. Unsupported machines and bad bindings fail with exit 2.

Each sink reports its resolved binding and a close-out byte count, including
zero. `rail read` reports the current binding and bytes. Relative files respect
`A2GSPU_OUT_DIR`. Duplicate canonical file paths and identical pipe names are refused to prevent two
captures from truncating each other. Files flush per write; pipes require a
reader and fail on disconnection/backpressure (Windows 100 ms deadline; POSIX
nonblocking FIFO).

### P0.2: actual instruction counts and function profiles

`run`, `step`, and `run-until` append `instructions=`, measured at opcode
execution, excluding pre-instruction stops and idle waits. Existing ack fields
remain intact. Counter collection is disabled outside explicit CTRL execution
or armed counting coverage. The host observation total is outside saved guest
state. Block moves count each executed iteration.

Arm `cov on <range>` or `cov map <A2MAP1>`, then `cov counts on`. Write counts
with `cov counts write <file.json>`; `cov reset` clears them and byte coverage,
and rearming/disarming coverage disables counting. Existing A2COV1/A2COV2
artifacts are unchanged. New `a2profile-v1` documents contain per-PC counts,
instruction cycles, checked totals, generation and binary-plan identity.
Unarmed writes, stale mapped code and failed file writes are refused.

`tools/a2rail/profile.py` validates the document and rolls PCs into explicit
half-open function ranges from `a2symbols-v1`, retaining `<unassigned>` work.
`--map` verifies A2MAP1 identity/ranges; mapped symbols must carry the matching
`map_hash`. `--expect-instructions` gates the total. Profile cycles exclude
interrupt entry and idle time, so they can be less than command elapsed cycles.

### P0.3: WDM console using existing handlers

`A2GSPU_WDM_CONSOLE=file:<path>` (or `pipe:<endpoint>`) registers `$FE`
for A.low byte output and `$FD` for Y bytes at X.low:A (max 4096). All registers
and flags are preserved; WDM remains two cycles. Strings are validated in full
with `a2obs_readable`/`a2obs_peek` before writing. Invalid/unreadable ranges exit 2.
Unset operands retain their prior unhandled behavior. Host FST's `$FF` handler
is untouched; operand collisions are refused.

### P0.4: socket self-description

After HELLO, an empty MANIFEST request (`0x00000006`) returns UTF-8 JSON with
schema `gs2-debug-manifest-v1`, framing/version/limits, type IDs/names/kinds,
and event IDs. The registry supplies server constants and discovery, avoiding
a second hand-maintained server list. Existing IDs and protocol v1 are intact.
Malformed payloads receive ERROR. CTRL's own manifest remains discoverable.

The Python client exposes `Client.manifest()`. Windows Python builds lacking
`socket.AF_UNIX` use Winsock for the address conversion/connect, then ordinary
Python socket I/O. This was needed to test the installed Python on this host.

## Regression evidence

The clean Release build is outside the source checkout:
`D:\gsqs\coherency-20261015\build\GSSquared.exe`. It uses this checkout's
source, UCRT64 GCC and Ninja, without replacing `build\GSSquared.exe`.
The original consumer binary is still the September 17 build.

Binary SHA-256:
`dd52aa5bc9fed83aec6d8715782aad5196c7629ad104ab15265167cd20e25c62`.

**Later queue iteration:** the incremental P2.1 build reuses this out-of-tree
build directory. The exact P0 executable above is preserved beside it as
`D:\gsqs\coherency-20261015\build\GSSquared-p0.exe`; current binary/test
state is in `../gssquared-queue-p21-20261015/README.md`.

Build recipe inside MSYS2 bash (use a fresh output directory):

```bash
source ~/.profile
export PATH=/c/msys64/ucrt64/bin:/d/dev/ninja:/c/Users/X99/AppData/Local/Programs/Python/Python313:$PATH
cmake -S . -B /d/gsqs/coherency-20261015/build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe \
  -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe \
  -DCMAKE_MAKE_PROGRAM=D:/dev/ninja/ninja.exe
cmake --build /d/gsqs/coherency-20261015/build --parallel 8
ctest --test-dir /d/gsqs/coherency-20261015/build --output-on-failure
A2_GSSQUARED=D:/gsqs/coherency-20261015/build/GSSquared.exe \
GSSQUARED_ROOT=D:/projects/gssquared-fork \
  /c/Program\ Files/PowerShell/7/pwsh.exe -NoProfile \
    -File tools/a2rail/gates.ps1 -Report D:/gsqs/coherency-20261015/logs/gates.ndjson
```

**Final verification: Release build succeeded; ctest 12/12; all 13 gates PASS.**
The coverage test reports 80 cases, zero failures. `unarmedhash` passes 42
checks across four boots, including the new console-binding checks. hwtruth's
24 verdicts and MB-Audit's 1,922 tests pass. The baseline JSON files have no diff.

| Boot | Hash | Cycles | Run witness |
| --- | --- | --- | --- |
| iigs-rom | `6D9886E3B3EE0383` | 6,236,744 | `800AEA5075763504` |
| iie-rom | `92F1E19D6FE2A383` | 2,043,601 | `9D45F479767B943C` |
| iigs-gsos601 | `D29F204D690D417F` | 10,647,413 | `9CFCBEBB3B604B4E` |
| iigs-finder | `2E304BF0147B220A` | 22,377,216 | `994DCC2DF8B6A75A` |

Evidence: [ctest](logs/ctest.log), [P0 positive/negative checks](logs/p0.log),
[full gate transcript](logs/gates.log), [gate matrix events](logs/gates.ndjson).
The final gate process exited 0. `git diff --check` is clean; vendored sources
are untouched.

The new `agentic` gate runs `p0check.py` serially in the umbrella gate stack.
Its evidence includes:

- IIe and IIgs: an independently specified three-iteration loop is **7
  instructions / 16 cycles**, and the loop function accounts for **6 / 14**;
  a planted expected count of 8 exits 1. Operand bytes do not gain PC counts.
- Mapped-profile identity and function roll-up; stale binary plans, changed
  executed code, malformed profiles and missing PC rows are refused.
- Coverage reset clears counts; unarmed writes fail; `run 0` fails and reports
  zero work. Unit coverage tests also exclude WAI idle cycles.
- WDM string plus byte yields `abc!`; disabled output is absent; both retain
  two-cycle timing and register values.
- Completed SCC A output is exactly `SCC\n`, channel B is empty, and local
  loopback never reaches the capture. A frame run reports actual instructions.
- Unsupported machines, invalid syntax, nonexistent output directories,
  missing pipes and aliased channel paths fail with exit 2.
- A real Windows named-pipe reader receives the emitted byte.
- Live socket discovery covers all 29 types. A planted omission and a nonempty
  MANIFEST request fail; PING and protocol QUIT still work.
- Pinned IIgs boots additionally arm SCC A/B and WDM capture, require binding
  and close-out accounting, and compare hash/cycles/run against unarmed boots.

The new tests also exposed a pre-existing CTRL parser bug: stripping CR before
LF left `\r` attached to Windows text commands. A single CR/LF trim loop fixes
it; the P0 client exercises ordinary Windows CRLF command files.

The record-parity gate initially classified the standalone JSON profile as an
NDJSON event. Its explicit artifact exemption now documents the actual format
and checked reader. Existing rail-record scans remain active for event writers.

The manual stock-windowed interaction check and the external a2engine suite
are not claimed by this report. No guest timing baseline was re-blessed.
Windows file and named-pipe paths were exercised here; POSIX FIFO code was not
executed on this Windows host.

## Next decisions

P0 approval does not authorize drift B-E, the engine merge, or replacing the
consumer-facing binary. The roadmap keeps each separate. Later phases remain
proposals; no changes were made in Rosetta or other consumer repositories.
