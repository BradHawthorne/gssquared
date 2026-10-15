# gssquared: upstream main against our fork, a feature crosswalk (2026-10-15)

The owner: "compare the current main gssquared to our gssquared-fork feature set. I haven't done a crosswalk recently
between the two."

**The trees:**
- **Upstream main:** `643e6c21` (jawaidbazyar2/gssquared). **It hasn't moved since the assessment:** 0 commits past
  `643e6c21`.
- **Our fork:** `a2gspu` at `053a4e14` (after drift stage A), plus the local branch `local/slot-engine-model` at
  `0dd0216d`.

**The method:**
- Read-only: a scratch clone at `D:\gsqs\crosswalk`, with the fork's tree untouched.
- It builds on `captures/gssquared-assessment-20261015` (the layer measurement and the commit-level drift triage) and
  the fork's `Docs/UPSTREAM_SYNC.md` (§§1-23).
- This report is about **features, not commits.** Each upstream-only feature names the commits and its triage verdict.
- Presence was checked three ways: by file sets (upstream-only 55 files, fork-only 157, shared 814), by grepping each
  feature's signature in both trees, and by targeted per-file checks.

**At a glance:**
- **The fork already carries almost all of upstream's user-visible features.** Stages 1, 2 and A of the syncs took
  shaders, high-DPI, the config editor, ROM 03, Host FST, APM/ISO, SecondSight in full, VOC, ThunderClock, HostSerial,
  Joyport, paste, French keyboard, the debug socket, `.hda` and SecondSight's 80×50/132×60 modes.
- **The device sets are identical,** plus the fork's `a2gspu` (the A2Fusion card model) and `slot_bus`.
- **Upstream leads in 14 features,** all among the drift triage's 13 adapt, 19 defer and 14 skip items.
- **The fork leads in about 30 feature areas,** the owner's agentic and oracle layer and its fidelity work.
- **About 9 features exist on both sides but differ.**

## 1. Upstream has, the fork lacks (14)

| # | Feature | Upstream commits and files | Triage verdict | Value to the owner's work |
| --- | --- | --- | --- | --- |
| U1 | **The clock-domain refactor:** `CpuRail`, `VidRail` and `C14mRail` in `util/ClockRail.hpp`; event timers moved into NClock; per-domain scheduling; clock-mode configuration; the legacy Mockingboard deleted in favour of `mb2` | `49ca2a34`, `84f172ee`, `271b254d`, `77f95eb8`, `9dbe1138` | **Adapt, stage D.** Accepted only if unarmedhash reproduces unchanged | **High.** Every later upstream device change builds on these rails, so deferring raises each port's cost |
| U2 | **The realistic 6502/65816 RESET:** a fake BRK, three stack false reads, the vector pulled in cycles 6-7, SP from `$00` (so `$FD` after reset) | `86527159`, `3947c859` (`base_6502.cpp` "handle reset more like an interrupt") | **Adapt, stage B,** with an owner-approved unarmedhash re-blessing | **High** for CPU fidelity. GSnux's reset/re-entry work and the ROM map's warm-reset traces depend on reset timing |
| U3 | **Serial receive metered by baud** on the 14 MHz rail (SCC and 6551), plus the `uartbaudtest` ctest | `0f774f48`, `apps/uartbaudtest` | **Adapt, stage C** | **High** for GSnux's SCC console and any serial timing |
| U4 | **DCD in the serial stack,** and better Hayes handling | `58751766`, `63f1717c` | **Adapt, stage C** | Medium. The fork's SCC already models DCD its own way (§3) |
| U5 | **The serial device thread start order** (the Windows listen fix, #196) | `643e6c21` | **Adapt, stage C** | Medium (a Windows bug fix) |
| U6 | **Inbound telnet** on port 6502, with `ATVn`/`ATS0=n` (GBBS-ready), protocol fixes and the 57600 response code | `7cc19e0b`, `6824aab5`, `38334fee`, `4e3d5911` (`ModemDevice.hpp`, +627 lines upstream) | **Defer** (bring with stage C if wanted) | Low to medium: BBS hosting |
| U7 | **A user systems path, and BRAM stored in the `.gs2` file** | `4a14e989` (`SystemConfig.cpp`, about 50 BRAM sites) | **Adapt, stage E.** Keep the fork's `A2GSPU_BRAM` pin precedence for automated runs | Low for automation; nice for windowed use |
| U8 | **Open-Apple and Closed-Apple mapping settings** | `c7046029` | **Adapt, stage E.** Reconcile with the fork's `a96dbb0c` | Low |
| U9 | **Display monitor configuration** (the monitor type per config) | `9dbe1138` (rides with U1) | Adapt, stage D | Low to medium |
| U10 | **A new-disk UI:** blank floppies and ProDOS formatting (`BlankDisk`, `ProDOSFormat`), including a 32 MB ProDOS volume | `023b657b`, `318ecda6` | **Defer.** Rosetta's `diskiigs` already makes volumes | Low |
| U11 | **WOZ: auto-create missing tracks** | `d9b67a89` (upstream marks it WIP) | **Defer** until upstream finishes it | Low to medium |
| U12 | **`.gs2pack` and arqyv cloud storage:** local packs, web fetch, cloud writeback, the Simple Browser, `Gs2Url` protocol handlers | `fb6d074d`, `13b8df52`, `0f071cc3`, `1651597e`, `6047e501` (`Gs2Pack.*`, `Gs2Url.*`, `HttpsGet.*`, `PackDiskBrowser`, `PackFetchModal`) | **Skip.** Revisit alongside U7 (it touches BRAM flush) | None for the owner's work |
| U13 | **The web (Emscripten) build:** web pacing, Safari dialogs, a deploy script | `5e8243fa`, `60049b84`, `84b7d420`, `2e4b2e5e`, `ab3d782b` | **Skip** | None |
| U14 | **Desktop conveniences:** `.gs2` icons and file associations, "Check for updates", HUD buttons, a host-folder display, Linux serial names, the launch-config modal | `de39c18b`, `b59054eb`, `de0ceea7`, `27b88218`, `bfbced8c`, `c2f4efda`, `59d7823f` | **Skip or defer** | None |

**Docs only upstream has** (18): `Features.md` (a full catalog), `CommandLine.md`, `Tests.md`, `SerialPortSpec.md`,
`ClockTypes.md`, `Cards_Clock`/`Mockingboard`/`SecondSight`/`VIDHD`/`Videx`, `ReleaseNotes-v1.0.0.md`, the Gs2Pack and
Arqyv docs, and `Web.md`. The card docs describe devices the fork *does* have, so carry them when convenient.

## 2. The fork has, upstream lacks (the owner's layer, about 30 areas)

| Area | Features | Main files |
| --- | --- | --- |
| **Agentic control** | **The CTRL session protocol:** atomic `cmd.N` → `ack.N`, about 65 verbs with schemas (`help`, `manifest`) | `a2gspu_ctrl.*`, `a2gspu_ctrl_cmds.*` (+2,824 lines), `a2gspu_ctrl_json.hpp`, `a2gspu_manifest.hpp` |
| | **Execution control:** `run-until` (cycle-delta timing), `step`, `bp`, `trap` (on a PC, store, read or stack range, with conditions and payloads), bus watch | `a2gspu_trap.hpp`, `generic_tap.hpp` |
| | **Observation:** `cov` (coverage bitmap, source-bound through `A2MAP1`), `itrace`/`tbuf`, `iolog`, `devstate`, side-effect-free `read`, `poke`, `setreg` (24-bit PC), `load` (a host file into guest memory), `screen`/`text`/`shr`/`png` | `a2gspu_coverage.hpp`, `io_trace.hpp`, `bus_trace.hpp`, `a2gspu_observe.hpp`, `obs_iigs.hpp`, `obs_signal.hpp`, `a2gspu_png.hpp` |
| | **Snapshots:** `save`/`restore` with device blocks (MMU, Ensoniq, ADB, SecondSight, mouse card, VOC, SSC, the engine...) | `a2gspu_snapshot.*`, `secondsight_snapshot.hpp` |
| | **Rails:** about 144 environment rails (`A2GSPU_*`, `MEMVU_*`, `WIZ5_*`), live `rail set`, and a registry kept in agreement by `railparity` | `a2gspu_rails_registry.hpp`, `a2gspu_rail_setters.*`, `Docs/A2GSPU_RAILS.md` |
| | **Headless runs:** `A2GSPU_SPIKE_FRAMES` with `-n`, `A2GSPU_LOAD`/`LOAD_BOOT` (splice into RAM), a pin line at startup, pacing, and windows that can't steal focus | `a2gspu_spike.*`, `a2gspu_pins.hpp`, `a2gspu_pace.hpp`, `a2gspu_run_kind.hpp` |
| **Determinism and oracles** | `A2GSPU_SEED`, `A2GSPU_FAKETIME` (time frozen by default), a BRAM pin, a run hash, liveness, and observers that read through `a2obs_peek`, never the bus | `a2gspu_faketime.hpp`, `a2gspu_bram_pin.hpp`, `a2gspu_runhash.hpp`, `a2gspu_liveness.hpp`, `house_fnv.hpp` |
| | **The gates** (`tools/a2rail/gates.ps1`, 12): **unarmedhash** (armed equals unarmed), **hwtruth** (24 hardware verdicts), **mbaudit** (MB-Audit v1.61), personality (17 machines), railparity, artifacts, gdiff, buscompare, dualsmoke, protocol, copilot, assertparity | `tools/a2rail/*`, `tools/hwtruth/*`, `tools/gdiff/*`, `tools/scout/*` |
| | **Self-tests and asserts** | `a2gspu_selftests.*`, `a2gspu_assert.hpp` |
| **IIgs understanding** | Toolbox and GS/OS call naming and tracing, a load map, symbols, I/O register names, SHR layout, video summary, a diagnostics view, MMU state trace | `iigs_toolbox.hpp`, `iigs_gsos_names.hpp`, `iigs_loadmap.hpp`, `iigs_symbols.hpp`, `iigs_ioregs.hpp`, `iigs_shr_layout.hpp`, `iigs_video_summary.hpp`, `iigs_diag.hpp`, `mmu_state_trace.hpp` |
| **Processor measurement** (the Fast816 cover) | Store and load visibility, instruction-stream shape (opcode mix, reuse), seams, reports | `memvu_storevis.hpp`, `memvu_istream.hpp`, `memvu_seam.hpp`, `memvu_report.hpp` |
| **Fidelity rewrites** (the "fuse the best version" policy; 16 heavy files) | **The CPU:** native TXS copies the full 16-bit X; MVN/MVP with 16-bit X/Y; reset sets I=1; the E-mode `(d)` wrap; NMOS decimal Z; the NMOS `JMP ($xxFF)` bug and the CMOS 6-cycle `JMP (abs)`; the E-mode `(d,X)` wrap; abs,X RMW phantom reads (`UPSTREAM_SYNC` §4, 1-9) | `cpus/base_6502.cpp` (+443/-73) |
| | **The IIgs MMU:** floating-bus bits 6-0 on `$C011`-`$C01F`; `$C02E`/`$C02F` reads with no interrupt side effect; Mega II CPU-order accessors for observers | `mmus/mmu_iigs.*` (+473) |
| | **Serial:** a rewritten Z85C30 SCC, including its own DCD, and a rewritten 6551 | `scc8530/Z85C30.hpp` (+596/-202), `ssc/MOS6551.hpp` (+316/-129) |
| | **The fused Uthernet II:** upstream's register model and slirp, plus the fork's interrupt, state machine, listen and reporting behaviour, socket-mode TCP/UDP, IPRAW and MACRAW, and a rail (`u2_rail.hpp`) | `uthernet2/w5100_chip.cpp` (+567/-95) |
| | **IWM and disk:** real-time disk cells (opt-in `A2GSPU_DISK_TIMING`), WOZ write rules (sidecar, write-protect), media identity, file safety | `IWM2.hpp` (+246/-56), `util/media_identity.hpp`, `util/file_safety.hpp` |
| | **ADB:** Reset/Flush restore the power-on address and SRQ; a key interceptor and taps; no host write to guest memory (`a820c356`) | `ADB_Micro.hpp` (+267/-20) |
| | **Audio and DOC:** the DOC runs on emulated time even with no audio device, so its interrupts fire headless; an audio probe at emulated time | `util/audio_probe.hpp`, `es5503/*` |
| | **Video:** the SHR fill-mode seed taken from `SCB & 0x1F`, and DHGR helpers | `a2gspu_dhgr.hpp`, the scan generators |
| | **The legacy Mockingboard kept** beside `mb2` (upstream deleted it in U1) | `mockingboard/mb.cpp`, `AY8910.hpp` |
| **Cards** | **The A2Fusion card model** (`a2gspu`): its bus protocol, an emulated card, a VT100 terminal, and a `$C0nF` command register | `devices/a2gspu/*` (18 files) |
| | **A slot-bus model** | `devices/slot_bus/slot_bus.hpp` |
| | **The GSnux slot-engine model,** local branch only: v0-v3 registers, an IRQ watchdog, and a bootable ProDOS disk role | `devices/pico2lite_engine/*`, `apps/pico2liteenginetest` (`local/slot-engine-model`) |
| **Tests** | `conformtest` (the 6502-family conformance matrix, with Clark's decimal tests) and `coveragetest` | `apps/conformtest`, `apps/coveragetest` |
| **Docs** | Instrumentation shape and audit, the agentic oracle, closed-loop integration, dual platform, the per-component fidelity studies, `UPSTREAM_SYNC.md` | `Docs/fidelity/*`, `Docs/A2GSPU_*`, `A2GSPU_INSTRUMENTATION.md` |

## 3. Both have it, implemented differently

| Feature | Upstream | Fork | More faithful or capable | Conflict cost |
| --- | --- | --- | --- | --- |
| **Clocking and timing** | Domain rails (`CpuRail`/`VidRail`/`C14mRail`), clock modes, monitor config (U1) | NClock with per-cycle 14M reclaim, a cycle timeline, faketime, `slow_mode`, `get_vid_cycles` under disk timing | Upstream for structure; the fork for determinism and measurement | **High: 25 overlapping files.** Stage D |
| **The serial stack** (SCC, 6551, DCD, baud) | Baud metering on the 14 MHz rail, DCD and modem features | A rewritten SCC with its own DCD (more DCD sites than upstream) and a rewritten 6551 | Mixed. Upstream leads on baud timing; the fork leads on register fidelity. Fuse them in stage C | High: the two heaviest rewrites |
| **CPU reset** | Realistic: three stack false reads, the vector pulled in cycles 6-7, SP `$FD` | I=1 forced on reset (upstream sets I only in the non-65816 branch) | Both are partial. Take upstream's sequence *and* keep the fork's I=1 | Medium. Re-blesses unarmedhash |
| **BRAM** | Stored in the `.gs2` file, with a per-config UUID | Pinned per run (`A2GSPU_BRAM`) for determinism | The fork for automation, upstream for windowed use. Keep both, with the pin taking precedence | Low to medium (stage E) |
| **Uthernet II** | Its register model, slirp, worker | Fused: upstream's base plus the fork's behaviour and fixes | **The fork** | Low (already fused) |
| **The debugger** | `DebugProtocolServer` socket, in-app monitor, BreakpointTable | All of upstream's, plus CTRL beside it, with side-effect-free reads (§13) | **The fork** (a superset) | Low |
| **Mockingboard** | `mb2` only | `mb2` plus the legacy `mb` | Equivalent. The fork keeps the old one for its snapshot block, which points at `mb2` after stage D | Low |
| **Game switches with nothing attached** | Floating (`d79be3c0`, `0e606364`) | Not pressed | Open. Hardware evidence favours upstream (the pull-downs are in the hand controller) | Low. A decision for the owner |
| **MVN/MVP with x=1** | 8-bit stepping | 16-bit X/Y with explicit banks | Open. The documentation favours upstream's 8-bit stepping with the fork's explicit banks | Low. Settle on hardware |

## 4. Recommendations

**Upstream features worth adapting next,** ordered by value to the owner's IIgs, A2Fusion and GSnux work:
1. **U2, the realistic RESET (stage B).**
   - GSnux's warm-reset re-entry and the ROM map's reset traces depend on it.
   - Keep the fork's I=1, and add a hwtruth verdict for the post-reset SP.
   - The unarmedhash re-blessing needs the owner's yes.
2. **U3-U5, the serial stage (stage C):** baud metering, DCD and the Windows start order.
   - GSnux's SCC console and any serial timing benefit.
   - Fuse upstream's metering into the fork's SCC and 6551 rewrites, and adopt `uartbaudtest`.
3. **U1, the clock-domain refactor (stage D).**
   - Every future upstream device builds on it, so every month it waits raises the port cost.
   - Accept it only if unarmedhash reproduces unchanged. Repoint the fork's `mb` snapshot block to `mb2`.
4. **U7 and U8 (stage E):** BRAM in `.gs2`, with the pin taking precedence, and OA/CA mapping. Conveniences.
5. **U11 when upstream finishes it;** **U6** only if BBS hosting matters.
6. **Skip U12-U14** unless the owner wants the web build or cloud packs.
7. **Copy the upstream card docs** (Clock, Mockingboard, SecondSight, VIDHD, Videx) for devices the fork has.

**Fork features mature enough that the owner might share them upstream someday.** This is a list only: the owner keeps
upstream vanilla by choice.
- **The CPU fidelity fixes** (`UPSTREAM_SYNC` §4, 1-9): small, self-contained, each with a cited behaviour.
- **The ADB Reset/Flush fix,** and **the `$C02E`/`$C02F` and `$C011`-`$C01F` fixes.** The last two contradict MAME, so
  they'd need hardware evidence first.
- **The SHR fill-mode seed from SCB.**
- **The DOC running headless on emulated time** (its interrupts fire with no audio device).
- **The fused Uthernet II's socket-mode and listen behaviour.**
- **`conformtest` and `coveragetest`.**
- **Snapshot blocks for upstream's cards.**
