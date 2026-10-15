# gssquared: upstream vs fork agentic surfaces, the gaps neither has, and a phased task proposal (2026-10-15)

The owner: "work up a full task proposal group in implementation phases. Looking at their agentic tools and our
agentic tools what are the gaps in ours that neither have?"

**Trees read** (read-only; the scratch clone `D:\gsqs\crosswalk`):
- upstream `main` at `643e6c21`;
- the fork's `a2gspu` at `053a4e14`, and `local/slot-engine-model` at `0dd0216d`.

**Builds on:**
- `captures/gssquared-assessment-20261015`;
- `captures/gssquared-crosswalk-20261015`;
- `captures/gssquared-engine-20261015`;
- the fork's `Docs/AGENTIC_ROADMAP.md`, whose Phase 0 this proposal keeps.

## 1. Inventory, from code

**Upstream's agentic and debug surface** (`src/debugger/DebugProtocolServer.cpp`, 2,304 lines; `BreakpointTable.*`;
`debugwindow.cpp`; `clients/python/gs2debug`; `apps/gstrace`; `Docs/DebugProtocol*.md`):
- **The protocol:** binary, v1, a 12-byte frame over a Unix-domain socket (AF_UNIX, also on Windows), with **27 message
  types**:
  - **Session:** hello, ping, error, event, quit.
  - **Run control:** get-status, reset, pause, continue, step-into.
  - **Registers:** get-regs, set-regs (PC, PB, DB, A, X, Y, SP, D, P, E by mask).
  - **Trace:** get-trace, a ring of up to 16,384 forty-byte records.
  - **Memory:** read, write and find (up to 64 KiB, a 256-byte pattern with a mask, 256 hits). The domains are main,
    Mega II, Ensoniq DOC RAM, ADB micro, main-raw and Mega II-raw.
  - **Device state:** state-get and state-set, per device.
  - **Breakpoints:** set, clear, clear-all, enable, list. A typed stop engine matches address, access kind, mask and an
    ignore count, which covers instruction, data and I/O watches.
  - **Input:** key events and paste-text.
  - **Video:** text 40 or 80 columns, with mode flags.
  - **Media:** mount and unmount.
- **Pushed events:** *stopped* (with the breakpoint id and access) and *run-state*.
- **Around it:**
  - a Python client library;
  - `gstrace`, an offline trace disassembler that takes VICE label files;
  - the in-app debugger window and monitor;
  - `Docs/DebugProtocolVsDAP.md`, which states the design: assembly-first, the host owns symbols, no DAP.

**The fork's surface** (all of upstream's above, plus):
- **CTRL:** atomic `cmd.N` → `ack.N` files with schemas.
- **65 verbs** (from `src/a2gspu_ctrl_cmds.cpp` and `a2gspu_ctrl.cpp`):

  ```
  assert audio boot bp callstream copilot cov cpu cycles devstate dhgr-calibrate dhgr-export dhgr-golden dis help hgr
  holdkey iolog itrace joymode json key keys limitations load manifest memvu mount oracle paddle pause pause-status
  pbutton png pngc poke press protocol quit rail read reset restore resume run run-until save screen session setreg shot
  shr shr-golden speaker stack step systems tbtrace tbuf text trap valtrap verify vid vram watch
  ```

- **Traps** (`a2gspu_trap.hpp`): as configuration, not code. A trigger (a PC or range with a bank, an exit PC, a store
  or read range), a condition (expressions over registers and memory), a policy (once, every, first N per key) and a
  payload, all written to one record sink (`a2record.hpp`).
- **Observation:**
  - `cov`, a coverage bitmap of what executed, fed to `deasmiigs`;
  - `itrace` and `tbuf` traces, `iolog`, and bus watch;
  - `devstate`;
  - reads with no side effects (`a2obs_peek`);
  - `screen`, `text`, `shr`, `png` and `pngc`;
  - `speaker`, the count and time of `$C030` toggles, and `audio`;
  - `memvu` processor measurement.
- **IIgs understanding:** `callstream` and `tbtrace` (Toolbox and GS/OS call naming and tracing), symbols, the load map,
  I/O register names.
- **State:**
  - `save`/`restore`: a transactional checkpoint with per-device blocks;
  - a `SAVE_AT` breakpoint-checkpoint;
  - `load` and `setreg` (a 24-bit PC).
- **Input:** `key`, `keys`, `press`, `holdkey`, `paddle`, `pbutton`, `joymode`, and paste.
- **Determinism:** a seed, fake time, a BRAM pin, a run hash, liveness.
- **About 144 rails,** with a registry that `railparity` keeps in agreement.
- **Headless:** spike frames, load and boot splicing, pacing, no focus stealing.
- **12 gates** (`tools/a2rail/gates.ps1`): unarmedhash, hwtruth, mbaudit, personality, railparity, artifacts, gdiff,
  buscompare, dualsmoke, protocol, copilot, assertparity. Plus `conformtest` and `coveragetest`.
- **Tooling:** `session.ps1`, `ctl.ps1`, `gsosmap.py`, `prodosfs.py`, `romfpt.py`.

| Capability | Upstream | Fork | Notes |
| --- | --- | --- | --- |
| Run control (pause, step, run-until) | Yes (socket) | Yes, plus cycle-exact `run-until` | Fork superset |
| Breakpoints | Typed: address, access, mask, ignore count | Upstream's, plus condition/policy/payload traps and value traps | Fork superset |
| Memory and registers | Domains including DOC and ADB | Same, plus side-effect-free observers | Fork superset |
| Trace | A 16K ring, offline `gstrace` | `itrace`, `tbuf`, `iolog`, bus watch | |
| Coverage | No | A bitmap (executed or not) | **No counts on either side** |
| Snapshots | Per-device state get/set | Transactional save/restore, `SAVE_AT` | **No branching or diff on either side** |
| Events | Pushed *stopped* and *run-state* | Upstream's socket, plus CTRL file polling | **No general subscription on either side** |
| Determinism and gates | No | Yes (seed, fake time, BRAM pin, run hash, 12 gates) | Fork only |
| OS awareness | No | IIgs Toolbox and GS/OS names and tracing | **Nothing kernel-aware for Linux** |
| Screen assertions | Text 40/80 | Text, SHR, PNG, golden images | **Exact only: no tolerant compare, no SHR text read** |
| Audio | WAV out (device) | Speaker toggle count, audio probe | **No assertion on audio content** |
| Input injection | Keys, paste | Keys, holds, paddles, buttons, joystick | **No record-and-replay of input** |
| Debugger interop | Own protocol, Python client | The same | **No GDB remote and no DAP on either side** |
| Interrupt sources | IRQ from devices | The same | **No NMI or ABORT source on either side** |
| Fleets | No | One session per process, by hand | **No orchestration or aggregation** |

## 2. The gaps neither side has

| # | Gap | Why the owner's work needs it | Building blocks either side has | Size |
| --- | --- | --- | --- | --- |
| G1 | **Headless capture of the SCC serial console** | GSnux's serial console and dmesg, gated without a window | The SCC model; upstream's `file`/`echo` serial devices | S |
| G2 | **Counting coverage and per-function cycle profiles:** per-PC execution counts, `instructions=` and `cycles=` in acks, roll-up by symbol | N0-style profiles for kernel and compiler tuning (the tick, the keystroke path, `cciigs` output against the floor) | The `cov` bitmap, `A2MAP1` maps, cycle counters | S-M |
| G3 | **A guest debug console:** `WDM` as a print or trace hook | printf from a kernel or a test program with no device driver yet; early boot | None (WDM is a NOP) | S |
| G4 | **Kernel awareness for Linux:** read `dmesg` from the log ring in memory, walk the task list, detect a panic or oops, decode stacks against `System.map` | GSnux bring-up: "where did it die" without a console | The symbols and load map (IIgs-specific today), traps, side-effect-free reads | M |
| G5 | **A 65816 GDB remote-protocol stub** (RSP over TCP: registers, memory, breakpoints, watchpoints, step, `qXfer` memory map) | Lets llvm-mos's lldb or gdb, or a cross gdb, attach for source-level debugging of GSnux and Rosetta output | Upstream's protocol core (run control, breakpoints, memory) to bridge from | M |
| G6 | **Source-level mapping** from compiler debug info (DWARF from llvm-mos; `cciigs`'s `-g` output) to PC ranges, for traps, profiles and traces | Profiles and traces by function and line, not address | `A2MAP1` and VICE label files (symbols only) | M |
| G7 | **Record and replay, and reverse stepping:** a deterministic input and event log, replay to any cycle, step backwards from a fault | Reproduce a rare GSnux or 14a-style fault exactly, then rewind to its cause | Determinism (seed, fake time, run hash) makes replay possible; snapshots | L |
| G8 | **Checkpoint branching and state diffs:** fork from a snapshot, run variants, diff memory, registers and devices between two states | Explore "what if" (which write corrupted this), A/B of a driver change in one run | `save`/`restore`, per-device blocks | M |
| G9 | **Fault injection:** interrupt storms, a stuck IRQ, NMI and ABORT sources, slot card failures (a card that answers wrongly), bus glitches, timing skew | Test GSnux's interrupt hooks, the engine's watchdog, the card-brick guard, and the ROM map's NMI path, which the emulator couldn't generate | Device models, the trap engine | M |
| G10 | **Structured event streams:** subscribe to traps, stops, console lines and device events as a stream, not by polling `ack` files | Long agentic runs without polling; reacting to a panic line as it happens | Upstream's pushed *stopped*/*run-state* events; the CTRL record sink | M |
| G11 | **Multi-instance fleets:** launch N headless instances with variants (ROM 01/3, seeds, configs), aggregate results, gate on the set | The engine matrix, differential compiler runs at scale, regates in parallel | `session.ps1`, headless spike, determinism | M |
| G12 | **Slot-bus traces exported as VCD,** with the same signal names a logic analyser or the A2Fusion PB snoop produces | Compare emulated card timing with a real capture (the engine's PH0-high drive window, M1-M9) | The slot-bus model, bus watch, `buscompare` | M |
| G13 | **Hardware-in-the-loop trace comparison:** align an emulator trace with a real-hardware capture (PB snoop, logic analyser) and report divergence | Proves the emulator against the ROM 01 bench; closes the open bench items | `buscompare`, hwtruth verdicts | L |
| G14 | **Tolerant screen assertions:** read text drawn on SHR (a font-matching reader for the console's 8×8 glyphs, Toolbox fonts), and perceptual or tolerance image compare | Assert GSnux's 40×25 SHR ANSI console and dithered output; stable goldens across palette tweaks | Exact PNG goldens, `text` for the text page | M |
| G15 | **Audio assertions:** capture DOC or speaker output headless and assert frequency, timing or a fingerprint | Sound tests for GSnux's later sound rung; DOC timer behaviour | The speaker toggle count, the audio probe, WAV output | M |
| G16 | **Input macro record and replay:** record a key, mouse and paddle session and replay it deterministically | Reproducible interactive tests (shell sessions, desk use) | Key and paddle injection, fake time | S-M |
| G17 | **Cross-emulator differential:** run the same image in MAME, KEGS or GSplus and compare traces and state | Fidelity arbitration where hardware isn't to hand | gdiff, `dualsmoke` (fork-internal) | L |
| G18 | **Bisect across emulator versions:** build two fork commits, rerun a scenario, find the change that altered behaviour | Drift stages B-E (the clock refactor) and fidelity regressions | Run hashes, the gates | S-M |
| G19 | **A Fast816 local-memory tier model** (rung 2) | Measure GSnux at Fast816 speeds | memvu rails, free-run clock | M-L (belongs to the Fast816 project) |
| G20 | **Machine-readable self-description of the debug socket** (a manifest like CTRL's) | Agents discover both surfaces the same way | The CTRL manifest | S |

## 3. The phased task proposal

**Where each task lives:**
- **(F)** the fork;
- **(R)** Rosetta: compiler-side debug info, profile consumers;
- **(G)** GSnux: kernel-side hooks such as the `WDM` print macro or symbols.

**GSnux-critical** is marked **★**.

**Gate convention:** every task has a known-pass and a known-fail case (the owner's evidence method), and the pinned
baselines (unarmedhash, hwtruth) stay unchanged unless the owner approves a re-bless.

### Phase 0: quick wins that unblock GSnux now (already in the roadmap §3)

| ID | Task | Files (likely) | Size | Depends | Gate | Home |
| --- | --- | --- | --- | --- | --- | --- |
| P0.1 ★ | **The headless SCC capture rail** (`A2GSPU_SCC_A`/`B=file:`/`pipe:`), in spike and CTRL | `scc8530/Z85C30.hpp`, a new `scc_capture.hpp`, rails registry, `Docs/A2GSPU_RAILS.md` | S | - | Pass: a guest prints a known string and the capture matches. Fail: the wrong channel or no binding gives an empty capture, and the gate fails | F |
| P0.2 ★ | **Counting coverage:** per-PC counts (`cov counts`), `instructions=` and `cycles=` in every run ack, per-function roll-up via `A2MAP1` or a symbol file | `a2gspu_coverage.hpp`, `a2gspu_ctrl_cmds.cpp`, `tools/a2rail/profile.py` (new) | S-M | - | Pass: a loop of exactly N iterations gives count N at its PC and N×k cycles. Fail: a planted off-by-one loop is caught | F (consumers in R and G) |
| P0.3 ★ | **The `WDM` debug console:** `WDM $nn` with a string pointer in A/X (or a byte in A) prints to the CTRL log or console file; opt-in rail; NOP when off | `cpus/base_6502.cpp` (one opcode hook), rail, `Docs` | S | - | Pass: a string round-trips. Fail: with the rail off, `WDM` stays a 2-cycle NOP (unarmedhash unchanged) | F (and a macro in G) |
| P0.4 | **A manifest for the debug socket (G20),** so both surfaces self-describe | `a2gspu_manifest.hpp`, `DebugProtocolServer` | S | - | Pass: the manifest lists all 27 message types. Fail: a missing type fails `protocol` | F |
| **Drift B** | **The realistic reset** (U2), keeping I=1 | `cpus/base_6502.cpp` | S-M | - | A hwtruth post-reset SP verdict; an unarmedhash re-bless with the owner's yes | F |

### Phase 1: kernel-aware debugging (GSnux bring-up)

| ID | Task | Files | Size | Depends | Gate | Home |
| --- | --- | --- | --- | --- | --- | --- |
| P1.1 ★ | **The Linux awareness module:** given a `System.map`, read `__log_buf` (printk ring) to a `dmesg` verb, walk `init_task`'s list to a `tasks` verb (pid, comm, state), and detect panic or oops (a trap on `panic`, `oops_begin`), stopping with a decoded stack | `linux_aware.hpp` (new), `a2gspu_ctrl_cmds.cpp` | M | P0.2 (symbols), and a GSnux kernel to point it at | Pass: on the QEMU-proxy-derived test images (or a synthetic memory image) dmesg text and the task list match. Fail: a corrupted ring head is reported as corrupt, not garbage | F (with symbol conventions in G) |
| P1.2 ★ | **A 65816 GDB RSP stub** (TCP): `g`/`G`, `m`/`M`, `Z0`/`Z2`-`Z4` mapped onto the trap engine, `s`/`c`, `qXfer:memory-map`, 24-bit addresses, the E/M/X state as pseudo-registers | `gdbstub.cpp` (new), bridging the trap engine and run control | M | P0.4 | Pass: a scripted gdb session (break, step, read, watch) on a test program. Fail: a wrong register count or address width is rejected by the client test | F (used by llvm-mos/lldb in R) |
| P1.3 ★ | **Source-level maps:** ingest DWARF line tables (llvm-mos) and `cciigs -g` output, and map PC to file:line for traps, profiles and traces | `srcmap.hpp` (new), `tools/a2rail/profile.py` | M | P0.2, and the compiler emitting debug info | Pass: a known function's lines map exactly. Fail: a stale map (wrong build hash) is refused | F and R |
| **Drift C** | **Serial** (U3-U5) fused into the fork's SCC and 6551 | `Z85C30.hpp`, `MOS6551.hpp`, `uartbaudtest` | M | P0.1 | `uartbaudtest`; P0.1's capture still passes | F |

### Phase 2: reproducibility and exploration

| ID | Task | Files | Size | Depends | Gate | Home |
| --- | --- | --- | --- | --- | --- | --- |
| **Drift D** | **The clock-domain refactor** (U1), *before anything timing-heavy* | 25 overlapping files | M-L | B, C | unarmedhash unchanged, or the stage is reverted | F |
| P2.1 ★ | **State diffs:** `diff <snapA> <snapB>` over memory domains, registers and device blocks, with a summary and ranges | `a2gspu_snapshot.*`, `snapdiff.hpp` (new) | S-M | - | Pass: one planted byte change is found exactly. Fail: identical snapshots give an empty diff | F |
| P2.2 | **Checkpoint branching:** `fork <name>` from the current state into a child instance (or an in-process copy), run variants, compare outcomes | `a2gspu_snapshot.*`, `session.ps1` | M | P2.1, and P4.2 for multi-instance | Pass: two branches with different inputs diverge and both report. Fail: branches that share mutable state are detected | F |
| P2.3 ★ | **Record and replay:** log every nondeterministic input (keys, host time, network, disk writes) with cycle stamps; replay reproduces the run hash; seek to a cycle by restore plus replay; `reverse-step` by checkpointing every K cycles | `a2record.hpp`, input paths, `a2gspu_faketime.hpp`, `replay.hpp` (new) | L | Drift D, P2.1 | Pass: record then replay gives an identical run hash; reverse-step lands on the prior instruction. Fail: a planted unlogged input (raw host time) breaks the hash and is reported | F |
| P2.4 | **Bisect across fork versions:** `bisect.ps1` builds two commits, reruns a scenario, compares run hashes and gates, and bisects | `tools/a2rail/bisect.ps1` (new) | S-M | - | Pass: finds a planted behaviour change in a 4-commit range. Fail: no change reports none | F |

### Phase 3: fault injection and interrupt sources

| ID | Task | Files | Size | Depends | Gate | Home |
| --- | --- | --- | --- | --- | --- | --- |
| P3.1 ★ | **NMI and ABORT sources:** a CTRL verb or rail to assert NMI and ABORT at a cycle or on a trap | `cpu.hpp`, the 65816 core, rails | S-M | - | Pass: the ROM map's NMI path (`$00:03FB`) and ABORT vector are reached as documented. Fail: with nothing asserted, unarmedhash is unchanged | F |
| P3.2 ★ | **An interrupt-fault suite:** IRQ storms, a stuck IRQ, a missed acknowledge, delayed VBL | the device IRQ plumbing, `faults.hpp` (new) | M | P3.1 | Pass: the engine model's watchdog releases a stuck IRQ within 10 ms. Fail: injection disabled changes nothing | F |
| P3.3 | **Card faults:** a slot card that returns wrong bytes, drops writes, holds /IRQ, or vanishes (for the card-brick guard and driver robustness) | `slot_bus.hpp`, device models | M | P3.2 | Pass: a GSnux-style driver's identity check refuses a card with a wrong signature. Fail: an unfaulted card passes | F |

### Phase 4: event streams and fleets

| ID | Task | Files | Size | Depends | Gate | Home |
| --- | --- | --- | --- | --- | --- | --- |
| P4.1 | **Structured event streams:** `subscribe <kinds>` over the debug socket (and as a JSONL stream file for CTRL): traps, stops, console lines (P0.1), WDM prints (P0.3), panics (P1.1), device events | `DebugProtocolServer`, `a2record.hpp`, `a2gspu_ctrl.cpp` | M | P0.1, P0.3 | Pass: a subscriber sees a planted event once, in order. Fail: an unsubscribed kind isn't delivered | F |
| P4.2 ★ | **Fleets:** `fleet.ps1` (or Python) launches N headless instances from a matrix (ROM 01/3 × seeds × configs × images), each with its own control directory, and aggregates acks, events and gates into one report | `tools/a2rail/fleet.*` (new), `session.ps1` | M | P4.1 recommended | Pass: a 2×2 matrix runs with every cell reported. Fail: one planted failing cell is the only red one | F (heavy use by R: differential compiler runs) |

### Phase 5: hardware in the loop

| ID | Task | Files | Size | Depends | Gate | Home |
| --- | --- | --- | --- | --- | --- | --- |
| P5.1 ★ | **VCD export of slot-bus traces:** PH0, R/W, the selects, D0-D7, A0-A15, /IRQ, with names matching the engine's BUSDRIVE observer criteria M1-M9 | `slot_bus.hpp`, `vcd_writer.hpp` (new) | M | - | Pass: the engine's ID read shows data valid in PH0-high in the VCD. Fail: a planted drive into PH0-low is flagged by a checker | F |
| P5.2 | **Trace comparison with real captures:** align an emulator VCD or trace with a logic-analyser or A2Fusion PB snoop capture and report the first divergence (cycle, signal) | `tools/a2rail/hwcompare.py` (new), `buscompare` | L | P5.1, and real captures from the ROM 01 bench | Pass: identical traces report no divergence. Fail: a one-cycle skew is found at the right place | F (captures from a2nux/GSnux) |

### Phase 6: assertion tooling

| ID | Task | Files | Size | Depends | Gate | Home |
| --- | --- | --- | --- | --- | --- | --- |
| P6.1 ★ | **SHR text reading:** match 8×8 (and Toolbox) glyph cells back to characters, for GSnux's 40×25 SHR console and the desk | `shr_text.hpp` (new), `a2gspu_png.hpp` | M | - | Pass: a known screen reads back exactly. Fail: a single changed glyph is detected | F |
| P6.2 | **Tolerant image compare:** per-pixel tolerance, palette-insensitive and perceptual modes for goldens | `a2gspu_png.hpp`, `tools/gdiff` | S-M | - | Pass: a palette-shifted golden passes under tolerance. Fail: a moved glyph fails | F |
| P6.3 | **Audio assertions:** headless capture of DOC and speaker output; assert dominant frequency, timing, or a fingerprint | `util/audio_probe.hpp`, `audio_assert.hpp` (new) | M | - | Pass: a generated 1 kHz tone is recognised. Fail: silence or the wrong frequency fails | F |
| P6.4 | **Input macros:** record and replay key, mouse and paddle sessions with cycle stamps | input paths, `macro.hpp` (new) | S-M | P2.3 shares the log format | Pass: a recorded shell session replays to the same screen hash. Fail: a timing-shifted replay is detected | F |

### Phase 7: cross-emulator differential and Fast816

| ID | Task | Files | Size | Depends | Gate | Home |
| --- | --- | --- | --- | --- | --- | --- |
| P7.1 | **Cross-emulator differential:** run one image in MAME or KEGS (headless), normalise traces and state, compare with the fork | `tools/a2rail/xemu.py` (new), adapters | L | P4.2, P2.1 | Pass: agreement on hwtruth's scenarios. Fail: a planted fork divergence shows | F |
| P7.2 | **The Fast816 local-memory tier model** (rung 2) | memvu, a new device | M-L | Drift D | Pass: the Fast816 spec's timing tables are reproduced. Fail: the stock model differs | The Fast816 project, with hooks in F |
| **Drift E** | **BRAM in `.gs2` and Open/Closed-Apple keys** (U7, U8), with the BRAM pin keeping precedence | `.gs2` config, ADB | S | - | The personality gate; BRAM-pin runs unchanged | F |

## 4. The order, and what GSnux gets when

1. **Phase 0, plus Drift B.** This unblocks GSnux bring-up now: console capture, profiles and printf.
2. **Phase 1, plus Drift C.** Source-level, kernel-aware debugging: dmesg, tasks and panics from memory, and a GDB stub
   for llvm-mos's lldb.
3. **Drift D, then Phase 2.** The clock refactor lands before replay and branching, which depend on exact timing.
4. **Phase 3.** NMI and ABORT, and fault injection: closes the ROM map's NMI gap and tests the engine watchdog and the
   card-brick guard.
5. **Phase 4.** Event streams and fleets: Rosetta's differential at scale, and the engine's matrix.
6. **Phase 5.** VCD and hardware-in-the-loop, when the ROM 01 bench produces captures.
7. **Phases 6 and 7, plus Drift E,** as needed.

**Sizes:**

| Phase | Tasks |
| --- | --- |
| P0 | S, S-M, S, S (plus B, S-M) |
| P1 | M, M, M (plus C, M) |
| P2 | D (M-L), S-M, M, L, S-M |
| P3 | S-M, M, M |
| P4 | M, M |
| P5 | M, L |
| P6 | M, S-M, M, S-M |
| P7 | L, M-L (plus E, S) |
