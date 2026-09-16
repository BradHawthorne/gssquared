# Control plane, observability and machine configuration: the ideal shape

Sources, read only through `git show`: the fork at `ed2c00fc`, upstream at `upstream/main` (`d380e055`), and their merge-base `c4b1fa69`. Citations starting `F:` refer to the fork and `U:` to upstream.

## 1. Inventory

| Capability | Fork | Upstream | Relation |
|---|---|---|---|
| Transport | Headless `cmd.N`/`ack.N` file rail (the machine advances only inside verbs); a windowed co-pilot file rail; about 144 environment rails applied at launch; SPIKE one-shot runs | AF_UNIX binary frames on a protocol thread, plus a single-flight bridge serviced once per host frame; the in-app Monitor | Same purpose, no shared core |
| Discovery | `help`, `manifest`, `rail list/find/live/read/schema`, `protocol`, `systems`, `limitations` | `HELLO` (version, `flags=0`) | Upstream gap |
| Run control | `run N`, `step N`, `run-until`, `pause`/`resume`, `reset`, `boot`, `quit`; cycle counts are measured within the command | `PAUSE`/`CONTINUE`, asynchronous `STEP_INTO`, `RESET`, `QUIT` | **Divergent**: synchronous and deterministic versus wall-clock |
| Registers | `cpu`, `setreg` (`e` applies XCE's side effects) | `GET_REGS` (no E flag), masked `SET_REGS` (E written raw) | **Divergent** |
| Memory read | `read`/`verify`/`assert peek:`/`dis` through the side-effect-free probe; unreadable bytes are reported | `READMEM`/`FINDMEM` through the bus (`MAIN`/`MEGAII`), plus raw and DOC RAM domains | **Divergent**: upstream reads actuate I/O |
| Memory write | `poke` (bus, or probe with refusals reported), `load`, `vram load` | `WRITEMEM` (bus or raw) | Overlap |
| Breakpoints | `bp`: one PC, which parks before the instruction; `SAVE_AT` | `BP_SET` table: EXEC/DATA/IO kinds, mask, ignore-count, temporary, "Policy A" (continuing runs the instruction once) | **Divergent** |
| Watch/trap | `watch bus` sees every bus access; `valtrap`; `trap` with at/store/read/sp triggers, `when=` conditions and payloads; PCTRAP, CONDTRAP, STACKTRAP, POKE | DATA/IO see one effective address per instruction; conditions are left to the host | **Divergent** granularity |
| Trace | `tbuf`, `itrace`, `tbtrace`, `callstream`, `iolog`, `cov` | `GET_TRACE` raw window | Overlap |
| Events | NDJSON records (one envelope, 10 schemas), written to files | `EVT_STOPPED` and `EVT_RUN_STATE` pushed to the client | No common schema |
| Device state | `devstate` (text); snapshot device blocks | `STATE_GET` for Ensoniq, Disk II and the mouse; `STATE_SET` (mouse motion) | **Two serializations** |
| Snapshots | Transactional `save`/`restore` with a manifest | None | Upstream gap |
| Input | `keys`, `press`, `key` (with a consume receipt), `holdkey`, `paddle`, `pbutton`, all at the device | `KEYEVENT` through the SDL queue; `PASTE_TEXT` | **Divergent** |
| Media | `mount` checks the path, then saves and unmounts | `MOUNT`/`UNMOUNT`, which discards unsaved writes | **Divergent** |
| Live arming | `rail set/unset`, `trap add` | None | Upstream gap |
| Identity/config | PINS line; 16 builtins selected by index (`-c N`) | `GET_STATUS` platform id; `.gs2` UUID; Settings.txt; config editor; host preferences | **Gap on both** |

## 2. Defects

### Fork

- **F1. JSON `ok` is wrong.** `ok` requires the text to start with `status=OK` (`F:src/a2gspu_ctrl_json.hpp:17`), so `status=PASS` (`assert`, `shr-golden`), `HALT` and `BUDGET` report `ok:false`.
- **F2. `load` actuates the machine without saying so.**
  - It throws `$C08B` twice through the bus and never restores it (`F:src/a2gspu_ctrl_cmds.cpp:319-320`).
  - It writes bank 0 through `mmu->write` (`:325`), so the live RAMWRT/80STORE state decides where the bytes land. `load 00:A100` and `poke 00:A100` can therefore hit different memory.
  - The writes appear in `iolog`/bus traces as guest traffic (`F:src/mmus/mmu.hpp:219,245`).
- **F3. `keys` advances time without reporting it.** On a IIgs it runs two frames per character (`:1833-1836`) and acks a bare `status=OK`, with no cycle count and no rail-frame tick.
- **F4. `step` and `run` evolve differently.** `step_one` crosses frame boundaries without device-frame dispatch (`:1529-1545`). The inherited `STEP_INTO` branch dispatches on every host iteration (`F:src/gs2.cpp:360`).
- **F5. The two file transports disagree on the same verbs.**
  - `reset` acks differently on CTRL and co-pilot (`F:src/a2gspu_ctrl.cpp:272` vs `:978`).
  - `quit` returns on one and halts on the other.
  - `mount` is implemented twice (`:406`, `:1006`).
  - Co-pilot has no `json` wrapper (`:1023`).
- **F6. Three breakpoint engines.**
  - CTRL `bp` is one global slot (`F:src/a2gspu_ctrl_cmds.cpp:2515`).
  - The inherited debug window truncates to 16 bits (`F:src/debugger/debugwindow.cpp:168,185`).
  - Once window checks are armed, any BRK stops the machine (`F:src/gs2.cpp:412,520`).
  - About a dozen trigger rails each keep their own matcher.
- **F7. Verb parsers are lenient where launch parsers are strict.** `valtrap set` (`:1227`) and `itrace from` (`:1270`) arm garbage as address 0; the launch setters reject it (`F:src/a2gspu_rail_setters.cpp:43`).
- **F8. No configuration identity.**
  - PINS (`F:src/a2gspu_pins.hpp:88`) and the snapshot manifest (`F:src/a2gspu_snapshot.cpp:134`) omit the config index, slot map and card model, so `-c 10` and `-c 15` print equal PINS lines.
  - The manifest's pins are the saving process's environment (`:93`), not the lineage of the state.
  - `restore` never checks machine identity.
- **F9. `A2GSPU_FAKETIME` pins only the IIgs RTC.** The ProDOS clock card (`F:src/devices/prodos_clock/prodos_clock.cpp:81`, used by builtin 1) and the ThunderClock (`thunderclockplus.cpp:62`, used by builtin 14) still read wall time.
- **F10. `-c` fails badly.** A non-numeric value makes `std::stoi` throw (`F:src/gs2.cpp:967`). A bad index "stays in selector" instead of exiting non-zero (`:1045`).
- **F11. Builtin labels do not match their slot maps.** "Apple IIe" and "IIe Enhanced" claim a clock and a parallel port that are not in their slots (`F:src/systemconfig.cpp:182,201`). The Mouse config's comment says slot 4 but the code uses slot 1 (`:458` vs `:465`).
- **F12. Stale live contracts.**
  - `oracle` says cycles "can jump/go backwards" and that the scanner and media "self-heal" (`F:src/a2gspu_manifest.hpp:22,25,93`).
  - Two documents give different settable-rail counts, 42 and 68.
  - `protocol` claims `ownership=session-json` (`F:src/a2gspu_ctrl_cmds.cpp:963`), which only the client script enforces.
- **F13. The observe read is not guaranteed side-effect-free on the II family.** `a2obs_peek` uses the bus read there (`F:src/a2gspu_observe.hpp:49`); freedom from side effects is asserted, not enforced.
- **F14. No arm registry.** `session reset` cannot reach valtrap, itrace, tbtrace or callstream (`:2643`).
- **F15. Snapshots exclude the clock and scheduler.** Cycles and the run hash do not continue across a restore.

### Upstream

- **U1. Debug reads go through the bus.** `READMEM` and `FINDMEM` use `mmu->read` (`U:src/debugger/DebugProtocolServer.cpp:270,287`).
  - A `FINDMEM` across `$C000` throws every soft switch.
  - On a IIgs the read also sets the next cycle type (`U:src/mmus/mmu_iigs.hpp:95`; `U:src/mmus/mmu_iigs.cpp:14`), so observing changes timing.
- **U2. Every stop snapshot is a bus read.** `fill_live_trace` fetches the opcode through the bus (`:1161`), so every `GET_REGS` and every PC-breakpoint or pause `EVT_STOPPED` actuates slot ROM paging and cycle type.
- **U3. `WRITEMEM MAIN` writes through the bus** (`:572,587`). It follows live RAMWRT and triggers IIgs sync cycles.
- **U4. The in-app debugger observes through the bus.**
  - Monitor peek, dump and save (`U:src/debugger/Monitor.cpp:411,437,763`).
  - The debug-window memory watch reads through `mmu->read` on every rendered frame (`U:src/debugger/debugwindow.cpp:1193`).
  - The disassembler guard compares a 24-bit address with an exclusive bound, then calls `mmu->read` (`U:src/debugger/disasm.cpp:40-46`). The fork fixed this defect.
- **U5. Monitor `load` stores one byte too many.** Its `while(!feof)` loop writes an extra trailing byte (`Monitor.cpp:791-796`).
- **U6. A stopped machine is not stopped.** Breakpoint stops set `EXEC_STEP_INTO`, and that branch runs device-frame dispatch and video on every host iteration even with zero instructions to execute (`U:src/gs2.cpp:352-386`).
- **U7. Arming any breakpoint changes behaviour.** Every BRK then halts, and no event is emitted (`:459,558`).
- **U8. DATA and IO watches are shallow.** They match one `trace_entry.eaddr` per instruction (`U:src/debugger/BreakpointTable.cpp:248`), missing stack pushes, pointer fetches, block moves and second bytes. `domain` is neither validated (`:13`) nor matched.
- **U9. `STEP_INTO` ignores breakpoints** (`Docs/DebugProtocol.md:340`). The fork's `step` stops at them.
- **U10. `SET_REGS` sets `E` raw** (`DebugProtocolServer.cpp:780`). It skips XCE's M/X and high-byte effects, which can produce a state the CPU cannot reach.
- **U11. A machine switch silently breaks the session.** Returning to the selector builds a new `computer_t` (`U:src/gs2.cpp:1041`) without rebinding `debug_protocol`, which is bound only at `:1231`. Events stop while commands still work, and the breakpoint table, which lives on the computer, is lost.
- **U12. Device state get/set is not snapshot-consistent.**
  - There is one handler per device *type* (`U:src/computer.cpp:339`), so a second instance overwrites the first.
  - The mouse `STATE_SET` injects motion (`U:src/devices/applemouseiii/applemouseiii.cpp:223`).
  - Ensoniq and Disk II are get-only, and the Disk II blob holds an absolute deadline.
  - `device_id` values are the renumbered enum.
- **U13. `KEYEVENT` goes through the host.** It is pushed onto the SDL queue from the protocol thread (`:2271`), so the host keymap and hotkeys apply and timing is asynchronous.
- **U14. The Python client drops events.** `request()` swallows EVENT frames that arrive mid-request (`U:clients/python/src/gs2debug/client.py:680-681`), so a polling agent loses its `EVT_STOPPED`.
- **U15. Configuration pulls in host state and writes files.**
  - Loading a `.gs2` without an `id` mints a UUID and rewrites the input file (`U:src/util/SystemConfig.cpp:941-943`).
  - Settings.txt mints a new UUID on every load (`U:src/util/SettingsConfig.cpp:555`), so each run starts a new BRAM file (`U:src/devices/rtc/rtc_pram.cpp:54`).
  - Every launch loads preferences and seeds the preference directory (`U:src/gs2.cpp:1109-1110`).
  - Host preferences are guest-visible: gamepad-disconnect and joyport select (`U:src/devices/game/gamecontroller.cpp:81,427`), and the host FST folder (`U:src/devices/hostfst/hostfst.cpp:25`).
  - Wall time and the stack-pointer seed are unpinned (`U:src/cpu.cpp:27`).
- **U16. Smaller problems.**
  - `GET_STATUS` carries no config, ROM or build identity (`:523`).
  - The stop reason is a process global (`:200`).
  - `GET_TRACE` copies struct padding onto the wire (`:707`).

## 3. The ideal architecture

### 3.1 One semantic core

- **Operations are declared once.** Each op in a registry has:
  - a name and a class: observe / run / manipulate / trigger / state / configure / meta;
  - typed arguments and a typed result;
  - its observation model;
  - its precondition (stopped only, or also while running);
  - its effect label.
- **Transports are codecs over that registry:**
  - the CTRL text grammar;
  - the binary frames;
  - the environment rails, as a launch script of ops applied at frame 0;
  - the in-app Monitor.
- **Co-pilot is a policy filter** on effect class, not a second dispatcher.
- **One reply formatter.** The parity gate checks the registry against every codec.
- **Execution is pinned in time.** Ops run only on the emulation thread, at instruction boundaries.
  - **Lockstep** (headless): time advances only inside run ops, so every op has a defined cycle.
  - **Free-run** (windowed): ops queue and apply at the next boundary. Every reply and record carries `at_cycle`/`at_frame`, so a session replays from its log, as emulator input movies do.
- **Stopped means stopped.** A halted machine dispatches no device frames and drains no paste buffers. Per-frame device work is scheduled on emulated time, which removes F4 and U6.

### 3.2 The observation-free guarantee

- **One memory API with an explicit mode**, with `peek` as the default for every host reader, UI included:
  - **`peek`**: no handler side effects, no cycle-type or scheduler change, no taps. It returns `{value, readable, source}`.
  - **`bus`**: CPU-equivalent access, labelled manipulation, with taps stamped `origin=host`.
  - **`raw`**: physical buffers by domain (FPI, Mega II, DOC RAM, slot ROM).
- **Device handlers get peek twins.** This is MAME's side-effects-disabled read and Mesen's debug memory reads. `$C0xx` is answered where a device can compute the value without actuation and reported `unreadable` otherwise.
- **Contract:** no observe op may call a side-effecting handler, change timing, emit into a tap, or advance device frames.
- **Gate:** extend `unarmedhash` into an observer-conformance run. Every transport polls every observe op each frame, and the run hash, cycles, stores and every non-op record must match the unarmed baseline.
- **Traps never alter control flow.** The coupling that makes BRK halt whenever breakpoints are armed (F6, U7) is removed; BRK/COP stops become explicit triggers.

### 3.3 Device state is the snapshot

- **One versioned state schema per device instance.** The instance key is device name plus slot or port. The schema lists fields with width and time base; deadlines are stored relative to the snapshot clock, the fork's rebasing rule.
- **Snapshots come from the schemas.** A snapshot is CPU + MMU + clock and scheduler (fixing F15) + device blocks. The manifest's included/excluded lists are generated from the registry, so a device without a schema is excluded automatically.
- **`state get`/`state set` reuse the snapshot code.** `state get <dev>` renders the block; `state set <dev> field=value` is a transactional partial restore through the same loader and re-drives IRQ lines. `STATE_GET` v2 is that block, and `devstate` is a view of it.
- **Inputs are not state.** Mouse deltas and keys are `input` ops, injected at the device.

### 3.4 One trigger engine

- **One table with ids**, holding enable, temporary, ignore-count and policy (`once`/`every`/`first=N`). These all compile into entries:
  - `bp`, `trap`, `valtrap`, `watch bus`;
  - the environment trigger rails;
  - `BP_SET`;
  - Monitor `bp`.
- **Trigger kinds:**
  - exec (PC range, bank mask);
  - bus access at CPU-funnel granularity (read, write, fetch), as with gdb's Z2-Z4 watchpoints and DAP data breakpoints;
  - io, as sugar for the bank whitelist;
  - value, stack range, flag condition;
  - machine events (BRK, COP, IRQ, reset, toolbox or GS/OS call);
  - frame or cycle.
- **Conditions** use the fork's `when=` grammar, evaluated over peek and registers. Hosts can still continue from a stop.
- **Actions:** record, halt, snapshot, count, splice (labelled).
- **Stop semantics:**
  - exec halts before the instruction, and continue runs it once;
  - access triggers stop after the instruction and carry the access;
  - `step` honours triggers unless told otherwise.
- **Tables survive reset.** Replacing the machine emits an event and explicitly rebinds or clears (U11).

### 3.5 Records are the one event stream

- **Everything is a record** with the envelope `{event, seq, frame, cycles, origin, session}`, with schemas in the manifest. That includes:
  - stops, run-state changes and trigger hits;
  - applied ops (`ctrl.cmd`/`ctrl.ack`/`op.applied`);
  - manipulations, rail arm and disarm, warnings, snapshots and mounts.
- **One stream, several sinks:**
  - socket `EVENT` frames are a subscription;
  - NDJSON files are sinks;
  - stdout lines are derived views.
- **The emulator keeps the transcript**, not only the client.

### 3.6 Configuration and machine identity

- **One canonical, data-only machine description.** It covers:
  - platform;
  - ROM, named by content hash;
  - clock set and scanner;
  - RAM;
  - slot devices by name, with options such as card model and joyport select;
  - storage and connections.
- **`.gs2` is its file form.** The fork's builtins ship embedded in that form.
- **Identity:**
  - `machine=` is a hash of the normalized hardware description. It is printed on PINS, in the manifest and in `GET_STATUS`, and `restore` refuses a mismatch unless forced.
  - `run=` combines machine, ROM, media hashes, pins (seed, time, BRAM content, ramdisk, pace), exe hash and an op-script hash.
  - The UUID remains only as the key for interactive persistent BRAM.
- **Stable builtin names.** A frozen alias table maps `-c N` to `builtin:<slug>`; it only grows. `systems` lists index, slug and machine hash.
- **Hermetic mode.** Any agent transport, `-n`, or `--hermetic` means:
  - no preference reads or writes and no UUID minting;
  - BRAM from the pin, with the built-in default when unset;
  - one time service pinning every clock, including FST timestamps;
  - host gamepads absent, and the FST folder given explicitly.
  - PINS prints `host_inputs=`, and STRICT fails when it is non-empty.

### 3.7 Transports

- **Upstream's framing becomes a first-class transport:**
  - Existing frame types map to ops.
  - A generic `INVOKE` frame (op name plus typed arguments) makes every CTRL verb reachable, and `SUBSCRIBE` attaches records.
  - `HELLO` advertises the registry hash.
  - `MAIN` gets peek semantics; a new `MAIN_BUS` domain is for deliberate actuation.
  - `DATA` means bus-level access.
  - Under `-n` the socket is pumped by the lockstep driver, so it is as deterministic as CTRL.
- **The Python client** queues events instead of dropping them, adds `invoke()` and a lockstep `run(frames)`, and gains a file-rail backend.
- **The file rail stays.** The CTRL grammar is a codec, and `json` adds typed `fields`.

## 4. Migration, ordered by value

| # | Step | Size | Risk |
|---|---|---|---|
| 1 | **Truth fixes.** F1, F2, the F3 ack, F7, F9, F10, F11; config identity in PINS and the manifest; the restore identity check | S | Low |
| 2 | **Op registry.** Table-driven dispatch; co-pilot as a policy filter; one formatter (fixes F5) | M | Medium: build a golden ack corpus first |
| 3 | **Memory modes.** `peek`/`bus`/`raw`; device peek twins; the observer-conformance gate | M | Medium |
| 4 | **Hermetic config.** Embedded canonical builtins with the frozen alias table; machine hash; `.gs2` loader; time service | M-L | Medium: personalities and baselines |
| 5 | **Records.** The single stream (`ctrl.*`, `stop.*`, `run.state`, `op.applied`) | M | Low |
| 6 | **Socket transport.** Port the socket server and the Python client onto the core, fixing U1-U3, U6-U11, U13, U14 | L | Medium-high: threading, Windows sockets |
| 7 | **Trigger engine.** One engine; bus-level data triggers; `step` honours triggers; BRK decoupled | L | High: hot path, cycle-count gates |
| 8 | **Device state.** Schemas as snapshot blocks; clock block; `state get/set`; `STATE_GET` v2 | L | Medium |

Contracts that must stay stable:

- **Machine selection.** `-p 0..5`, and the meaning of `-c 0..15` (append only).
- **CTRL verb grammar:**
  - the leading `status=` token;
  - `ran=`, `cycles= (in-command)`, `PC $BB:AAAA->$BB:AAAA`, `stepped=`, `instr=`;
  - halt and resume semantics;
  - `a2ctrl-reply-v2`, fixing `ok` without renaming it.
- **Records and inventories.** The record envelope and the fields of all ten schemas; `a2systems-v1`, `a2rails-v1` and `a2limitations-v1` (columns append only).
- **Snapshot format.** GSPS v1, GSDV blocks and the GSPM trailer: new block versions only, and readers keep stepping over unknown blocks.
- **Run lines and baselines.** GSDIAG and PINS fields append only. `unarmed.baseline.json` must not move because of control-plane work.
- **Artifacts.** `A2COV1`/`A2COV2` and `.meta.json` sidecars.
- **Upstream wire v1.** Frame types and the 40-byte trace layout, for existing clients.
- **Gates.** `railparity`, `unarmedhash`, `personalitycheck`, `protocolcheck`, `copilotcheck`, `assertparity`, `execcheck`, `altzpcheck`, `railcheck`, `dualsmoke`.
