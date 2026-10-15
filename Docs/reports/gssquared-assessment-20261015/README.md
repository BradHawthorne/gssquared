# gssquared-fork: a full assessment (2026-10-15)

**Why this assessment.** The owner asked for it because gssquared-fork is integral to Rosetta and to closed-loop work.
It's the behaviour oracle for every IIgs project, and it also decides how GSnux boots a native 65816 Linux in emulation
before it runs on the real IIgs.

**Constraints.** The fork was only read. Every build, test, fetch and trial ran in scratch clones under `D:\gsqs`. The
fork's own tree still shows zero changes, and nothing was committed or pushed anywhere. Rosetta was only read, plus
three runs of its built `diskiigs`.

**The owner's framing.** The fork is "all our agentic ai integrated emulator work on top of gssquared main, but never
pushed up to it to keep their project vanilla". So divergence from upstream is deliberate and permanent, and is not a
defect. "Drift" means assessing upstream's changes one by one and taking them in on our terms (§9).

**The headline:**
- **The fork is healthy.** A clean Release build passes its ctest (12 of 12) and all 12 gates, once the git-ignored ROM
  images are present.
- **Rosetta's ctest #26 isn't a fork bug.** Rosetta's test fixture made a 140K image, and the fork rightly refuses to put
  that in a block-device slot. The concurrent Rosetta job already fixed it in commit `fbc2d73` on `gsnux/r0`.
- **Upstream has 56 non-merge commits** (62 with merges) since the fork's last by-hand sync. Nine are ready to take
  now. Applied together they build, pass ctest and all 12 gates, and every pinned boot's hash and cycle count is
  unchanged.
- **GSnux's emulation needs are mostly met already.** The real gaps are a model of the real storage card, headless SCC
  capture, and an instruction-count profile.

## 1. State

| | |
| --- | --- |
| Branch | `a2gspu`, the only branch (the fork's rule: no side branches, worktrees or stash) |
| HEAD | `e6462493`, 2026-09-17 05:09 -0500, "a2gspu: the card model's budget follows emulated frames; snapshot block a2gspu". The tree is clean |
| Commits | 1,858 |
| Remotes | `origin` = github.com/BradHawthorne/gssquared (**public**; live head `ed2c00fc`). `upstream` = github.com/jawaidbazyar2/gssquared (live `main` = `643e6c21`, 2026-09-30) |
| Unpushed | 53 local commits ahead of `origin/a2gspu`. Pushes are the owner's call |
| Against upstream | git merge-base `c4b1fa69` (2026-06-07), because ports are by hand as single-parent commits. **In content the fork covers upstream through `d380e055` (2026-09-14)** (`Docs/UPSTREAM_SYNC.md`: the 258-commit triage, then stages 1 and 2 ported by hand). Upstream has moved 62 commits past that (§9) |
| Size | 1,068 tracked files. The working tree is 899 MB including `build/` and the vendored SDL trees (about 450 MB) |
| The built binary | `build/GSSquared.exe`, 2026-09-17 04:43:33. It's newer than HEAD's last source edit (04:43:29), so it matches HEAD's code. Rosetta's driver runs this binary |
| Governing docs | `AGENTS.md` (rules 1-9, each with who enforces it), `Docs/SHARED_FOUNDATION.md`, `AGENTIC_ORACLE.md`, `CLOSED_LOOP_INTEGRATION.md`, `A2GSPU_RAILS.md`, `INSTRUMENTATION_SHAPE.md` (M1-M12), `UPSTREAM_SYNC.md`, `Docs/fidelity/` (nine ideal-shape studies) |
| Catalog summary | `E:\20261009_archive\summaries\d-projects-gssquared-fork.md`: consistent with the repository, except that it predates upstream's 62 new commits |

## 2. The owner's layer, and how cleanly it sits on upstream

Measured as the diff from upstream `d380e055` to HEAD, with `vendored/` excluded (`data/fork_vs_d380.txt`,
`data/numstat.txt`, `scripts/layer.py`):

| | Files | Lines |
| --- | --- | --- |
| **Fork-owned files added**: rails, CTRL, spike, snapshots, selftests, manifest, traps, the toolbox and GS/OS tracers, the card model, the a2rail gates, docs | 159 | +42,923 |
| **Upstream files the fork modifies** | 176 | +8,776 / -1,671 |
| …of which in `src/` | 150 | +7,975 / -1,369 |
| …thin hooks (20 lines changed or fewer) | 85 | |
| …medium (21-150 lines) | 49 | |
| …heavy (over 150 lines) | 16 | |

**The heavy edits are fidelity rewrites,** the "fuse the best version" policy, not instrumentation:

| File | Change |
| --- | --- |
| `devices/scc8530/Z85C30.hpp` | +596 / -202 |
| `devices/uthernet2/w5100_chip.cpp` | +567 / -95 |
| `cpus/base_6502.cpp` | +443 / -73 |
| `mmus/mmu_iigs.hpp` | +473 / -24 |
| `devices/ssc/MOS6551.hpp` | +316 / -129 |
| `gs2.cpp` | +305 / -114 |
| `systemconfig.cpp` | +297 |
| `IWM2.hpp` | +246 / -56 |
| `ADB_Micro.hpp` | +267 / -20 |
| `pdblock3.cpp` | +263 / -24 |

**Could the layer rebase onto a newer upstream?**
- **The instrumentation itself rebases well.** It lives in fork-owned files, with thin env-gated hooks in upstream files
  (AGENTS.md's rule and the `A2CTRL-DISPATCH` markers).
- **The conflict surface is the 16 heavy fidelity rewrites,** and they meet upstream exactly where upstream keeps
  working: the serial stack, the clock domains and the CPU's reset.
- **The by-hand single-parent port method suits that.** Each component is resolved on its merits and recorded in
  `UPSTREAM_SYNC.md`.
- **It does mean git can't compute what has been ported.** The ledger is the only record, so it must stay complete.

## 3. Build and tests (in the scratch clone)

| Step | Result |
| --- | --- |
| Configure (Ninja, MSYS2 UCRT64 gcc/g++, Release) | OK, 71 s (`logs/configure.log`) |
| Build | OK, 133 s. 701 targets, 29 executables, 86 compiler warnings (`logs/build-warnings-summary.txt`) |
| ctest | **12 of 12 pass** in about 1 s: CPU conformance and coverage, cycle tables for the 6502, 65C02 and 65816, 6522 T6-T9, disk ID, system config, SecondSight text (`logs/ctest.log`) |
| `tools/a2rail/gates.ps1` (12 gates) | **11 of 12 at first, in 284 s.** `personality` failed for "ROM 4 Mark Twain" and "ROM 3 Tenspeed": `Failed to stat …/roms/apple2gs4/main.rom`. Those ROM images are **git-ignored**, so a fresh clone lacks them. With them copied in, `personality` passes 17 of 17 machines, so **all 12 gates pass** (`logs/gates.log`, `logs/personality-rerun.log`) |
| What passed | railparity, artifacts, gdiff, buscompare, dualsmoke, personality, protocol, copilot, assertparity, **unarmedhash** (36 checks over 4 boots, armed equals unarmed), **hwtruth** (all 24 verdicts unchanged), **mbaudit** (MB-Audit v1.61, all tests) |
| Not run | a2engine's `gscheck.ps1` (a separate repo's suite) and the windowed hand check (rule 5) |

## 4. IIgs capability matrix

Sureness: **high** means read in source and exercised by a passing gate or ctest; **medium** means read in source only.

| Area | Status | Where | Sureness |
| --- | --- | --- | --- |
| **65816 core:** all opcodes and modes, native and emulation | Supported | `src/cpus/base_6502.cpp` (one templated core with `cpu_65816.cpp` traits); ctest `cpu_conformance`, `cpu_coverage`, `cpu_cycles_65816` | High |
| **Cycle accuracy** | Supported: cycle tables tested. The fork fixed E-mode `(d,X)` wrap, decimal flags, and abs,X RMW phantom reads | `base_6502.cpp`; `apps/cycletest`, `apps/conformtest` | High |
| **MVN/MVP** | 7 cycles per byte (opcode, two banks, read, write, two internal cycles; the instruction re-executes per byte). 16-bit X/Y always; DBR becomes the destination bank | `base_6502.cpp:2216` `move_memory` | High |
| **BRK, COP** | Supported, with E and N vectors. `A2GSPU_INTLOG` logs entries | `base_6502.cpp:4052`, `:4312` | High |
| **IRQ** | Supported, including the VGC/Mega II line recompute fix | `UPSTREAM_SYNC.md` §15 | High |
| **NMI** | Vectors defined (`cpu.hpp:38/44`); I found no NMI source wired | | Medium |
| **ABORT** | Not modelled. The IIgs doesn't drive ABORT | | Medium |
| **WAI, STP** | Implemented. WAI wakes on any pending interrupt, as on the part; STP stops until RESET | `base_6502.cpp:4964`, `:4977` | High |
| **RESET sequence** | Upstream's new realistic reset (§9, `86527159`) isn't in the fork yet | | High |
| **RAM** | 8 MB expansion plus on-board (`ROM01 mobo 128KB + exp 8MB = 8388608 bytes (banks $00-$7F)`). Unpopulated banks read the bank number (a hwtruth verdict) | `mmus/mmu_iigs.*`, `systemconfig.cpp` | High |
| **Memory switches** | `$C035` shadowing, `$C036` speed (and motor-on detect, opt-in), `$C068`, `$C02D`, `$C037`, banks `$E0`/`$E1`, the language card, FPI/CYA | `mmu_iigs.cpp/.hpp` | High |
| **ROMs** | ROM 01, ROM 03 (and the "upstream platform" ROM 3), ROM 3 Tenspeed, ROM 4 Mark Twain prototype | `systemconfig.cpp`; personality gate, 17 of 17 | High |
| **Mega II** | Supported (`M2B0`, RAMRD on `$E0`) | `mmu_iigs.*` | Medium |
| **VGC** | Text, SHR, VBL and scanline interrupts (`$C023`, `$C032`, `$C041`, `$C046`, `$C047`), the `$C02E`/`$C02F` counters | `display/display.cpp` (`VS_EVENT_SCB_INTERRUPT`), `devices/displaypp/VideoScannerIIgs.*` | High |
| **DOC 5503** | Oscillators and their IRQ. The fork runs the DOC on emulated time **even with no host audio device**, so oscillator interrupts fire headless. Upstream's `$E0` IRQ-ack fix is pending (§9, take now) | `devices/es5503/ensoniq.hpp`, `soundglu.cpp` | High |
| **SCC 8530** | Supported, with a heavy fork fidelity rewrite | `devices/scc8530/Z85C30.hpp` | High |
| **ADB GLU** | `$C000`, `$C010`, `$C025`-`$C027`, keyboard and mouse interrupts | `devices/adb/keygloo.cpp`, `ADB_Micro.hpp` | High |
| **RTC / BRAM** | `$C033`/`$C034`, write-protect, test register, address mapping; BRAM pin | `devices/rtc/` (hwtruth RTC verdicts) | High |
| **IWM / SmartPort** | IWM, real-time cells (opt-in `A2GSPU_DISK_TIMING`), 3.5" and 5.25" WOZ; SmartPort from the real ROM's probe | `devices/iwm/IWM2.hpp`, `floppy/` | High |
| **Mass storage** | **pdblock3 ("BazFast")**, upstream's *virtual* SmartPort block card with its own ROM; also pdblock2, prodos_block and Host FST (a WDM `$FF` trap). **No model of a real CFFA, Xdrive or BOOTi** | `devices/pdblock3/`, `devices/hostfst/` | High |
| **Network** | Uthernet II, a fused W5100: indirect mode, **MACRAW raw frames**, slirp backend | `devices/uthernet2/` | High |
| **Accelerators** | Clock modes 1.024, 2.8, 7.159 and 14.3 MHz, plus free-run with `cpu_per_14m`. **No ZipGS or TransWarp register model** | `NClock.hpp` | High |
| **Snapshots** | `save` / `restore` with device blocks for most devices | `a2gspu_snapshot.cpp` | High |

## 5. Instrumentation and control

| | |
| --- | --- |
| **Rails** | About 144 environment rails in `src/a2gspu_rails_registry.hpp` (`A2GSPU_*`, `MEMVU_*`, the standing `WIZ5_*` exception). Some can be set live with `rail set`. The `railparity` gate keeps the registry, `getenv`, the docs and the CTRL dispatcher in agreement |
| **CTRL session** | `A2GSPU_CTRL=<dir>`, then atomic `cmd.N` → `ack.N`, one command at a time. About 65 verbs with schemas in `src/a2gspu_manifest.hpp` (`help`, `manifest`) |
| **Control verbs** | `run-until` (to a PC within an instruction budget; its `cycles=` is an in-command delta, "THE timing primitive"); `step`; `bp`; `trap` (on reaching a PC, a store or read, or S moving into a range, with conditions and payloads); `watch bus add BB:lo-hi` |
| **Observation verbs** | `cov` (an executed-at-all bitmap, raw or source-bound through an `A2MAP1` map); `itrace` and `tbuf` (instruction traces); `iolog`; `devstate`; `read`, `poke`, `setreg` (24-bit PC); `load` (a host file into guest memory at bank:addr); `save`/`restore`; `screen`, `text`, `shr`, `png` |
| **Headless** | `A2GSPU_SPIKE_FRAMES=N` with `-n` runs a fixed frame budget. `A2GSPU_LOAD` splices files into RAM; `A2GSPU_LOAD_BOOT` sets the frames before it. Startup prints an `A2GSPU PINS` line |
| **Determinism** | `A2GSPU_SEED`, `A2GSPU_FAKETIME` (frozen by default), `A2GSPU_BRAM=default`. **unarmedhash** proves armed equals unarmed on screen hash, cycle count and run witness. Observers read through `a2obs_peek`, never `mmu->read` (rule 8) |
| **Debugger** | Upstream's `DebugProtocolServer`, beside CTRL. The fork made the debugger look without touching (`UPSTREAM_SYNC.md` §13) |
| **Rails for the private projects** | `MEMVU_*` (the processor-measurement cover); `A2GSPU_*` card model (`A2GSPU_CARD_MODEL=card`, a write-only `$C0nF` command register, `A2GSPU_CARD_CMDLOG`) |

## 6. Rosetta integration, and ctest #26

**How Rosetta drives it.** `scripts/differential/emulator.py`:
- `GssquaredDriver` finds `GSSquared.exe` from `ROSETTA_EMULATOR_GSSQUARED`, from sibling `gssquared-fork/build/`, or from
  `D:/projects/gssquared-fork/build/`.
- It launches `-p 5 -d s7d1=<image> -n`, with a 600-frame deterministic spike and `FAKETIME` 1000000000.
- `CtrlSession` speaks the `cmd.N`/`ack.N` protocol.
- The pinned Finder boot (`corpus/_finder.po`) is a golden: hash `2E304BF0147B220A`, cycles 22,377,216, and both the
  fork's gate and Rosetta check it.

**The #26 `gssquared_coverage_mapping` diagnosis:**
- **The symptom:** `emulator exited (rc=2) while awaiting ack.1 for 'protocol'`, in 1.8 s.
- **The root cause:** the test made its boot volume with `diskiigs create … 280 COVTEST`, a 140K image (143,360 B; the
  run's temp directory still holds it). Since the fork's stage-1 upstream port (`edb4ddb6`, 2026-09-16), the slot-7
  SmartPort card (pdblock3/BazFast) refuses that, by design: a 140K image belongs on a 5.25" drive. The session log
  reads `BazFast: refusing …ctrl-only.po: not a 512-byte block image` and
  `A2GSPU MOUNT: FAIL … aborting (exit 2)`. It exits before CTRL starts.
- **Reproduced in scratch with the clean build:**
  - a 280-block image from Rosetta's `diskiigs` exits 2 with the same two lines;
  - a 1600-block (800K) image mounts in slot 7 and the run exits 0 (`logs/r26/`).
- **The fix belongs in Rosetta** (the test fixture), not the fork. **It's already done:** commit `fbc2d73` on Rosetta's
  `gsnux/r0` makes the test create an 800K volume.
- **A small fork-side improvement, the owner's call:** in `pdblock3.cpp`'s `media_reject_reason`, a 140K `.po` is typed
  as nybble media, so the generic "not a 512-byte block image" fires before the clearer 140K message. Checking 140K
  first would make the refusal say why.

**The closed loop's single points:**
- the fork's own `build\GSSquared.exe` (Rosetta's default path);
- the corpus images and ROMs, which are local and not in git;
- the deterministic pins.

## 7. GSnux's needs against what the fork supports

From GSnux README §5 (the bare-metal back end) and WORKLOAD R4/R5 (the harness and full-system boot). Sizes: S is a
contained change, M a subsystem, L a major piece.

| GSnux need | Status | The work to close it |
| --- | --- | --- |
| Machine setup (`$C035`, `$C036`, `$C068`, `$C029`) | Supported | - |
| Tick from VBL or scanline interrupts | Supported | - |
| Tick from a DOC oscillator | Supported, and it works headless. Take upstream's `$E0` ack fix (§9) | S (taken in the take-now set) |
| Clocksource from `$C02E`/`$C02F` | Supported | - |
| 80-column text page; SHR | Supported, with `text`, `screen`, `shr` and `png` to observe them | - |
| ADB keyboard with interrupts | Supported; `key`/`keys` drive it | - |
| **SCC serial console, captured headless** | **Partial.** The SCC is modelled, and upstream's serial devices (`file`, `echo`, host port) exist, but no headless rail binds an SCC channel to a capture file for gating | **S:** a rail or config binding SCC A/B to a file or pipe in spike and CTRL runs |
| **Storage: the real Xdrive or BOOTi** | **Missing:** no model of a real card. pdblock3 (BazFast) is a virtual SmartPort card | **Interim, S:** bring the kernel up on BazFast's register protocol in emulation. **Then M-L:** a model of the real card once its slot ROM is read (`deasmiigs`, GSnux WORKLOAD C1) |
| RTC and BRAM | Supported | - |
| Uthernet II, W5100 raw frames | Supported (MACRAW, slirp) | - |
| DOC sound | Supported | - |
| 8 MB, banks, shadowing, speed | Supported | - |
| Boot from slot 7 block 0 | Supported (BazFast boots ProDOS boot blocks) | - |
| Load a raw image at an address and jump | Supported: `load` plus `setreg pc` plus `resume`, or `A2GSPU_LOAD` | - |
| Interrupt-vector fetch through the ROM | Runs the real ROM, so behaviour is the ROM's | **S:** a hwtruth verdict for the native-mode vector path, to compare with a real IIgs later |
| A debug or semihosting channel | Precedent: Host FST traps WDM `$FF` | **S:** a WDM-based console or exit code for kernel bring-up in emulation, kept clear of `$FF` |
| **Instruction-count and per-function profiles** (N0-style) | **Partial.** `cov` is executed-at-all, not counts; `itrace`/`tbuf` give traces; `run-until`/`step` report cycles | **S-M:** a counting mode for `cov` (counts per PC), an `instructions=` field in acks, and per-function roll-up through an `A2MAP1` map |
| Symbols for GSnux code | `A2MAP1` source-bound maps exist (`cov map`); GS/OS symbol support exists | **S, on the GSnux/Rosetta side:** emit `A2MAP1` from the GSnux link map |
| Deterministic runs | Supported (seed, time and BRAM pins; unarmedhash) | - |
| Batch gating like p2nux's regates | Supported in pattern (`gates.ps1` matrix, ndjson report, CTRL sessions) | **S-M, on the GSnux side:** a GSnux gate suite as a client, as Rosetta is |
| Fast816 speeds (rung 2) | Partial: free-run clock mode for speed; no model of Fast816's local-memory tier | That model belongs to the Fast816 project (the memvu rails), **M-L** |

**The verdict for GSnux.** Full-system boot in emulation needs little new work:
- **Ready now:** the CPU, memory map, VGC, DOC, ADB, RTC, network and the control surface.
- **Small:** headless SCC capture, a counting profiler with `instructions=`, a WDM console, and a vector-path verdict.
- **The one real gap:** a model of the real storage card. Until it exists, BazFast stands in for bring-up.

## 8. Relevance to a2nux and Fast816

- **a2nux:**
  - It was the C1 keyboard oracle: the IIgs forwarder's `$C0BF` frames are checked here.
  - The `A2GSPU_*` card model, with its write-only command register and command log, is the A2Fusion stand-in for card
    firmware work.
  - Per the catalog, the A2Fusion name is lifted since 2026-10-12, because A2Fusion is a public product. `MEMVU` stays
    a cover.
- **Fast816:**
  - The `MEMVU_*` rails are its measurement half. Fast816's spec is driven by measurements taken here.
  - GSnux's rung 2 would reuse the same rails to measure the 65816 bus while GSnux runs.

## 9. Upstream drift triage: every upstream commit since the fork's sync point

**The range.**
- **The git merge-base is `c4b1fa69`, but in content the fork is current to `d380e055`.** All 258 commits
  `c4b1fa69..d380e055` were triaged and ported by hand (`UPSTREAM_SYNC.md` §§1-8).
- **So the open range is `d380e055..643e6c21`:** 62 commits from 2026-09-17 to 2026-09-30, 6 of them merges with
  nothing of their own to port.
- **How it was assessed** (`data/drift.json`, `scripts/drift.py`, `data/upstream-compare-…json`): each commit's files
  were checked against the 176 upstream files the fork modifies. Candidates were trial-cherry-picked one at a time in a
  scratch clone, and the take-now set together (§9.1).

### 9.1 Take now: nine commits, verified together

They apply in sequence onto `e6462493`, build, pass ctest (12 of 12) and pass **all 12 gates**. unarmedhash's pinned
boots are unchanged in hash and cycles, and the 24 hwtruth verdicts are unchanged (`logs/trial-ctest.log`,
`logs/trial-gates.log`).

| Upstream | Date | What | Value | Overlap with the layer |
| --- | --- | --- | --- | --- |
| `7634b151` | 09-20 | DOC: reading `$E0` acks the lowest pending oscillator IRQ. The ROM's two-read idiom acked two and serviced one; now `$E0` is fetched once per transaction | **IIgs interrupt fidelity.** GSnux may tick from a DOC oscillator | `soundglu.*`; applies clean |
| `b22a2345` | 09-24 | #191: ignore the pointer when another window has mouse focus | Windowed UX | `keygloo_mouse_sync.hpp`; clean |
| `87f56ac9` | 09-24 | #194: temporary speed boost save and restore, factored out | Bug fix | `computer.*`; clean |
| `cc0a9eec` | 09-24 | Fix the adbtest link | Build | clean |
| `d5b63752` | 09-17 | SecondSight 80x50 and 132x60 text modes | Video-card fidelity | `secondsight.hpp`. **Follow-up: check the fork's SecondSight snapshot block carries the new mode state** |
| `599db0ae` | 09-23 | IIe keyboard: the legacy and modern glyph locations | II/IIe input | clean |
| `7eb29076` | 09-25 | Either gamepad face button fires | Input | clean |
| `293e0fc1` | 09-19 | Windows build info in upstream's AGENTS section | Keeps the verbatim upstream section in step | clean |
| `cb8ff63a` | 09-17 | Supports `.hda` (BlueSCSI) images | Storage formats | `media.cpp`, docs; clean |

### 9.2 Already in the fork

| Upstream | What | Why it's skipped |
| --- | --- | --- |
| `ae574e1b` | A null dereference with no audio device (Disk II, IWM) | The fork already returns early when the sounds are missing (`IWM2.hpp:789`, `diskii_controller.hpp:162`) |

### 9.3 Take with adaptation: own stages, conflict files named

| Upstream | What | Value | Conflicts | The adaptation |
| --- | --- | --- | --- | --- |
| `86527159` + `3947c859` | The real 6502/65816 RESET: three stack false reads, the vector pulled in cycles 6-7, SP from `$00` (so `$FD` after reset) | **CPU fidelity and determinism** | `cpu.cpp` (the fork's `A2GSPU_SEED` `srand`), `base_6502.cpp` | Keep the seed for other `rand` users and take upstream's reset. **Expect unarmedhash re-blessing** (SP and boot cycles change), which needs the owner's approval. Add a hwtruth verdict for the post-reset SP |
| `0f774f48` | SCC and 6551 receive metered by baud on the 14 MHz rail, plus a `uartbaudtest` ctest | **Serial timing fidelity** (GSnux's SCC console) | `Z85C30.hpp`, `MOS6551.hpp`, `CMakeLists.txt` | Port by hand into the fork's rewritten SCC and 6551. Adopt `uartbaudtest` as a ctest |
| `58751766`, `63f1717c` | DCD in the serial stack; serial and modem handling | Serial fidelity | `Z85C30.hpp`, `MOS6551.hpp`, `ssc.cpp`, `SerialDevice.hpp`, `ModemDevice.hpp` | With `0f774f48`, as one serial stage |
| `643e6c21` | Serial device thread start order (the Windows listen fix, #196) | Bug fix | `SerialDevice.*`, `ModemDevice.hpp` and others | With the serial stage |
| `4a14e989` | User systems path; BRAM stored in the `.gs2` file | Windowed BRAM persistence | `RTC.hpp`, `rtc_pram.cpp`, `gs2.cpp`, `systemconfig.cpp`, `SystemSettings.cpp` | **Keep `A2GSPU_BRAM` pin precedence for automated runs** (determinism, rule 9). Adopt upstream's storage only for windowed runs |
| `c7046029` | Open- and Closed-Apple mapping settings | Input configurability | `gamecontroller.cpp`, `SystemSettings.hpp`, `applekeys.hpp` (plus web and doc files the fork lacks) | Reconcile with the fork's `a96dbb0c` (SW0/SW1 follow the machine's Apple keys) |
| `49ca2a34`, `84f172ee`, `271b254d`, `77f95eb8`, `9dbe1138` | **The clock-domain refactor:** event timers move into `NClock` rails (`CpuRail`, `VidRail`, `C14mRail`); per-domain scheduling; the legacy Mockingboard (`mb.cpp`, `AY8910.hpp`) is deleted; clock-mode config | **High long-term value:** upstream's future device code will build on these rails, so deferring raises every later port's cost | `NClock.hpp`, `computer.*`, `gs2.cpp`, plus 25 overlapping files | **A dedicated stage.** Port onto the fork's `NClock` (faketime, `slow_mode`, `get_vid_cycles` under `A2GSPU_DISK_TIMING`). Point the fork's `mb` snapshot block at `mb2`. **Acceptance:** unarmedhash reproduces *unchanged*, since it's a pure refactor, and mbaudit stays green |

### 9.4 Defer

| Upstream | What | Why defer |
| --- | --- | --- |
| `d9b67a89` | WIP: auto-create missing WOZ tracks | Upstream calls it WIP. It interacts with the fork's WOZ write rules (the sidecar, write-protect). It applies clean; revisit when upstream finishes it |
| `023b657b`, `318ecda6` | A "new disk images" UI; a 32MB ProDOS volume option | UI conveniences. Rosetta's `diskiigs` already makes volumes |
| `7cc19e0b`, `6824aab5`, `38334fee`, `4e3d5911` | Inbound telnet, `ATVn`/`ATS0`, telnet fixes, 57600 response code | Modem and BBS features. Bring them in with the serial stage if wanted |
| `bfbced8c`, `c2f4efda`, `59d7823f` | HUD buttons, host-folder display, Linux serial names | Trivial UI |
| `87b115ab`, `15073dbe`, `ad1c09f8`, `8ea38312`, `47684462`, `55441cb1`, `9d44c167`, `79bbed99`, `dfa61f38` | Docs, notes, spec, version and changelog | Take the parts that describe ported code when porting that code |

### 9.5 Skip, because they're outside the fork's purpose (the owner can reverse any of these)

| Upstream | What |
| --- | --- |
| `5e8243fa`, `60049b84`, `84b7d420`, `2e4b2e5e`, `ab3d782b` | The Emscripten and web build, web pacing, Safari dialogs |
| `fb6d074d`, `13b8df52`, `0f071cc3`, `1651597e`, `6047e501` | `.gs2pack` and arqyv cloud storage, including network writeback. They touch the `rtc_pram` and `computer` BRAM flush paths, so revisit alongside `4a14e989` |
| `de39c18b`, `b59054eb`, `de0ceea7` | `.gs2` icons and file associations |
| `27b88218` | "Check for updates", which opens gssquared.net from the menu |

The six merges (`d40d5d5f`, `cfcae6db`, `0980f9da`, `c30983d0`, `f7705be7`, `aff788b4`) have no content of their own.

**The tally:** 9 take now, 1 already in the fork, 13 with adaptation, 19 defer, 14 skip. That's all 56 non-merge
commits.

### 9.6 The integration plan, for the owner to approve change by change

1. **Stage A, take now:** the nine commits of §9.1, as by-hand single-parent ports with `UPSTREAM_SYNC.md` rows. They
   were verified: build, ctest 12/12, gates 12/12, baselines unchanged. Then check that the SecondSight snapshot carries
   the new text-mode state.
2. **Stage B, CPU reset:** `86527159` and `3947c859`, with an unarmedhash re-blessing the owner approves, and a new
   hwtruth verdict.
3. **Stage C, serial:** `0f774f48`, `58751766`, `63f1717c` and `643e6c21` (plus the modem items if wanted), with
   `uartbaudtest` adopted.
4. **Stage D, clock domains:** the five clock commits, accepted only if unarmedhash reproduces unchanged.
5. **Stage E, BRAM and keys:** `4a14e989` with pin precedence kept, and `c7046029` reconciled with `a96dbb0c`.
6. **Deferred and skipped** as listed, to be re-triaged at the next sync. **The next sync starts from `643e6c21`.**

## 10. Risks

1. **Keeping the layer maintainable across upstream updates.**
   - The 16 heavy fidelity rewrites (serial, W5100, CPU, MMU, IWM) are the conflict surface.
   - Upstream's clock refactor touches 25 files the fork modifies, and every later upstream device change will assume
     it. The longer Stage D waits, the costlier each later port.
   - The ledger is the only record of what has been ported (single-parent ports), so a missed row is invisible to git.
2. **Public exposure of the fork's own remote.**
   - `origin` is public, with 53 unpushed commits.
   - A scan of tracked files and all commit messages found **none** of: Fast816, fpgaiigs, GSnux, a2nux, p2nux, Mike
     Neil, "processor design". The "Ralle" hits are all the substring of "parallel"; a whole-word search finds none.
   - The catalog notes firmware labels in already-pushed history (`ec38de76`), the owner's decision. A disclosure scan
     before each push stays necessary. GSnux's name, like the others, must never enter the fork.
3. **The closed loop's single points of failure.**
   - The ROM 3 Tenspeed and ROM 4 images and every corpus image are local and git-ignored. A fresh clone fails
     `personality`, and Rosetta's goldens depend on `corpus/_finder.po`.
   - They can't be public, but a **private manifest of their hashes and a private archive copy** (E:) would make the
     loop rebuildable.
   - Rosetta's driver defaults to one machine's path for the binary; `ROSETTA_EMULATOR_GSSQUARED` already overrides it.
4. **Determinism that depends on discipline.**
   - Automated runs must pin BRAM (rule 9) and run from a scratch cwd.
   - Upstream's `4a14e989` moves windowed BRAM into `.gs2` files; the adaptation must keep the pin's precedence.

## Files

| Path | What |
| --- | --- |
| `scripts/reproduce.sh` | The whole procedure, in scratch clones only |
| `scripts/layer.py`, `scripts/drift.py` | The layer measurement and the per-commit drift map |
| `data/fork_vs_d380.txt`, `data/numstat.txt` | The fork's changes against upstream `d380e055` |
| `data/drift.json` | Every upstream commit `d380e055..643e6c21`: files, size, overlap with the layer |
| `data/upstream-compare-d380e055..643e6c21.json` | GitHub's compare of the same range |
| `logs/configure.log`, `build-tail.log`, `build-warnings-summary.txt`, `ctest.log`, `gates.log`, `personality-rerun.log` | The baseline build and tests |
| `logs/trial-ctest.log`, `logs/trial-gates.log` | The take-now set's verification |
| `logs/r26/` | The #26 reproduction (140K exits 2; 800K mounts) and Rosetta's retained failure record |

## Addendum (coordinator, 2026-10-15): the W5100 model also does socket mode

The capability table above lists the Uthernet II as "MACRAW raw frames". The fork's
`src/devices/uthernet2/net_worker.cpp` also models the W5100's **TCP and UDP socket modes** (`NetMsg::OpenTcp` and
`OpenUdp`, backed by host sockets, with partial sends queued) and **IPRAW** (`IpRawTx`). So GSnux's offload design, an
`AF_INET` provider over the chip's hardware sockets (IDEAL-SHAPE §6), can be developed and gated in emulation before
the real card.
