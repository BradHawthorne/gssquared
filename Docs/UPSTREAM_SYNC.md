# Upstream sync ledger

This fork pulls upstream ([jawaidbazyar2/gssquared](https://github.com/jawaidbazyar2/gssquared))
in by hand and never pushes out ([`CLOSED_LOOP_INTEGRATION.md`](CLOSED_LOOP_INTEGRATION.md),
constraint 2). This file is the record that discipline requires: which upstream range was
reviewed, what was ported, what was judged already present, what was deferred and why, and what
is still owed. Until 2026-09-16 no such record existed; the only trace of the one past sync was
nine commit messages.

**How to use it.** Before porting, read the order and dependencies in section 2 — several fixes
only apply on top of others, and one (`18f5d04c`) changes the `$E1` memory layout that the fork's
direct Mega II reads and every recorded SHR hash depend on, so it moves `tools/a2rail/unarmed.baseline.json`
on purpose and must be re-blessed with the reason in the commit. After a sync, add a row to the
ledger below and change the affected dispositions in the table.

## Ledger

| Date | Upstream range reviewed | Result | Fork commits |
|------|-------------------------|--------|--------------|
| 2026-06-13 | fork point `c4b1fa69` (upstream 2026-06-07) | the fork's customisations forward-ported onto upstream as one commit | `3fbb454d` |
| 2026-08-02 – 08-04 | about 23 upstream commits, chosen by subject and file | **12 ported** (`6b91d9c1`, `9420cac6`, `f6a9a5c6`, `e0729c93`, `be5c869e`, `a359889a`, `cf946618`, `6f296ee6`, `160721cc`, `d2a0e056`, `5bd804a2`, half of `605a2c3f`); **5 judged present** (`94c1f2de`, `a9c7d1c6`, `282f8973`, `ad5f436b`, `ba91cf63`); **6 deferred** (`3d4efff1`, `0d95d93c`, `829bd5ea`, `d79be3c0`, `0e606364`, `cba9c847`) | `f646760c`, `3999d40e`, `486fb60e`, `0c601905`, `49822387`, `8d6f6cb5`, `05c2a46b`, `5814d86c`, `c7ae46e9` |
| 2026-09-16 | **all of** `c4b1fa69..d380e055` (258 commits, to upstream 2026-09-14) | triage only — nothing ported. Dispositions: 30 PORT, 89 PORT-LATER, 12 PORTED, 12 HAVE, 6 DEFERRED, 83 SKIP, 26 MERGE. The fork removed its ungated mouse-coordinate writes into guest memory (`a820c356`), leaving upstream's closed-loop ADB mouse sync (`d03c9783`, `b299b98d`, `19fe1f67`, `1f95ff78`, `533d8763`, `d380e055`) as the faithful replacement to port | this file |
| 2026-09-16 | stage 1: `c4b1fa69..29dadbbb` (upstream first-parent 1-133, to 2026-08-05; every commit in that range) | **ported by hand** as one fork commit with a single parent (no upstream parent, see "How syncs land"). Conflicts resolved per component (section 7); fork hooks carried; boot baseline re-blessed with attribution | this row's commit |
| 2026-09-16 | stage 2: `29dadbbb..d380e055` (upstream first-parent 134-194, to 2026-09-14; every commit in that range, so the fork now covers all of upstream to `d380e055`) | **ported by hand** as one single-parent fork commit; conflicts resolved per component (section 8); boot baseline re-blessed with attribution | this row's commit |

### How syncs land

Upstream is pulled in **by hand**, and each sync lands on `a2gspu` as ordinary commits with a
single parent: never a merge commit with upstream as a parent. A true merge would make `a2gspu` a
clean superset of upstream `main`, so a merge or pull request from this branch would carry all of
the fork's instrumentation into upstream with no conflicts: the easy path the README's one-way
rule forbids. Git's merge machinery may be used to compute and resolve conflicts, but the
recorded commit has one parent, and this ledger names the upstream range it covers. The next sync
starts from the last range's end commit, not from a git merge-base.

Where the fork and upstream both implement something, neither side wins by default: each
component takes the shape closest to real Apple IIgs hardware and is instrumented to the fork's
standard (no black boxes). The per-component choices are in section 7.

### Fork edits to upstream files after the triage

The triage below analysed the fork at `e35908d2`. These later commits changed
upstream-owned files, so a port that takes an upstream version of one of them
must carry the fork's lines across. Each is a thin, gated hook or a small
accessor; the machinery lives in fork-owned files.

| File | Fork lines | Commits |
|------|-----------|---------|
| `src/cpus/base_6502.cpp` | progress taps in `bus_write`/`execute_next` (M4, M9); the breakpoint park (`if (iigs_tb_on_landing(cpu)) return 0;`); the trap read trigger in `bus_read` | `f3d00823`, `31482937`, `bf77f99e`, `6ece2987` |
| `src/gs2.cpp` | CPU loops stop on `cpu->halt`; `frame_video_update` only for a completed frame; `A2GSPU_PACE`; the windowed frame clock; `run_one_frame`'s one-time arm also applies `A2GSPU_WATCH_IPC_ZP` and `A2GSPU_CARD_CMDLOG` | `3be31a3a`, `bf77f99e`, `a1b68c4d`, `90a5791f`, `414c5603`, `83b2547a` |
| `src/mmus/mmu.hpp`, `src/mmus/mmu_iigs.hpp` | `probe_peek`/`probe_readable` for `$E0/$E1:C000-CFFF`; `probe_poke` returns whether it landed | `114bbd59` |
| `src/mmus/mmu_iigs.cpp`, `src/devices/adb/keygloo.cpp` | progress taps (shadowed screen store, keyboard reads) | `f3d00823`, `31482937` |
| `src/devices/keyboard/keyboard.cpp` | progress taps; `g_tap_key_event` in `kb_key_pressed` | `f3d00823`, `31482937`, `858c1cdb` |
| `src/devices/adb/ADB_Micro.hpp` | `g_tap_key_event` at both key-latch sites (`load_key_from_buffer`, the unbuffered path of `store_key_to_buffer`) | `858c1cdb` |
| `src/devices/diskii/diskii_controller.hpp` | `g_tap_disk_write_event` after `write_pulse` | `858c1cdb` |
| `src/devices/floppy/Floppy_woz.hpp` | `a2gspu_is_mounted()` accessor | `858c1cdb` |
| `src/devices/displaypp/VideoScannerII.{hpp,cpp}`, `VideoScannerIIgs.hpp`, `src/display/display.{hpp,cpp}` | Class B snapshot of the scanner and `new_video` | `bbff1818` |
| `src/Device_ID.hpp`, `src/devices.cpp` | upstream ID order restored with a positional check | `3be31a3a` |
| `src/debugger/ExecuteCommand.hpp`, `src/devices/scc8530/Z85C30.hpp`, `src/devices/speaker/{EventBuffer,NEventBuffer,SpeakerFX}.hpp` | Linux/Clang build fixes | `700f6d2c` |

`git diff e35908d2..HEAD --stat -- src` regenerates the list; exclude the fork-owned prefixes
(`a2gspu_`, `iigs_`, `memvu_`, `wiz5_`, `generic_tap`, `a2record`, `devices/a2gspu/`).

## Triage of `c4b1fa69..upstream/main` (2026-09-16)

- **Fork side analysed:** `a2gspu` at `e35908d2`, with `3be31a3a` (the control plane moved out of
  `gs2.cpp`) landing during the analysis. That commit shrinks the fork's `gs2.cpp` delta against the
  merge-base from +3553/-14 to +224/-84, so the `gs2.cpp` conflicts noted below are smaller at HEAD.
- **Method:** emulation-affecting commits were classified from their diff hunks (`git show`), docs,
  UI and build commits from `git show --stat`, the 2026-08 batch from its fork commit messages, and
  existing equivalents by locating them in the fork with `git log -S`.

Dispositions:

| Disposition | Meaning |
|---|---|
| **HAVE** | The fork already has the change or an equivalent. The fork commit is cited. |
| **PORTED** | Hand-copied in the 2026-08 port batch. The fork commit is cited. |
| **PORT** | Correctness or fidelity fix to take in the upstream-integration goal. |
| **PORT-LATER** | Useful feature, not fidelity-critical. |
| **DEFERRED** | Explicitly deferred or declined in the 2026-08 batch. The reason comes from the fork commit. |
| **SKIP** | Docs or notes only, upstream-only tooling, or conflicts with a deliberate fork divergence (the divergence is named). |
| **MERGE** | Merge commit. Merges that carry conflict-resolution hunks are flagged in the note. |

The table's subject column is the upstream subject line, truncated to 70 characters. Person names, handles and a private mirror URL are replaced with bracketed placeholders.


## 1. Counts

### Per disposition

| Disposition | Count |
|---|---:|
| PORT | 30 |
| PORTED | 12 |
| HAVE | 12 |
| PORT-LATER | 89 |
| DEFERRED | 6 |
| SKIP | 83 |
| MERGE | 26 |
| **Total** | **258** |

### Per area (with disposition breakdown)

The `other` area holds the 26 merges plus three non-merge commits: the two ROM 3 platform commits and one comment-only commit.

| area | PORT | PORTED | HAVE | PORT-LATER | DEFERRED | SKIP | MERGE | total |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| cpu |  | 1 | 4 |  |  |  |  | 5 |
| mmu | 5 | 2 | 1 |  |  |  |  | 8 |
| video | 3 |  | 2 | 2 | 2 |  |  | 9 |
| adb/mouse/kbd | 13 | 1 |  | 3 |  |  |  | 17 |
| disk/iwm/smartport/fst | 3 | 1 | 2 | 8 | 1 |  |  | 15 |
| audio/ensoniq/mockingboard | 2 | 3 | 1 |  |  |  |  | 6 |
| clock | 2 |  | 1 |  |  |  |  | 3 |
| secondsight/voc |  |  |  | 10 |  |  |  | 10 |
| net/serial |  |  |  | 8 |  | 3 |  | 11 |
| joystick |  | 3 | 1 | 2 | 3 |  |  | 9 |
| debugger/debugproto |  | 1 |  | 13 |  | 13 |  | 27 |
| ui/config/menus/osd/shaders | 2 |  |  | 39 |  | 1 |  | 42 |
| build/ci |  |  |  | 4 |  | 6 |  | 10 |
| docs |  |  |  |  |  | 57 |  | 57 |
| other |  |  |  |  |  | 3 | 26 | 29 |
| **total** | **30** | **12** | **12** | **89** | **6** | **83** | **26** | **258** |

Batch cross-check. The 2026-08 batch's 12 ported commits, 5 already-present commits and 6 deferred/declined commits appear here as follows:

- **PORTED (12):**
  - `6b91d9c1`, `9420cac6`: in `f646760c`.
  - `f6a9a5c6`, `e0729c93`, `be5c869e`: in `3999d40e`.
  - `a359889a`, `cf946618`: in `486fb60e`.
  - `6f296ee6`: in `8d6f6cb5`.
  - `160721cc`: in `05c2a46b`.
  - `d2a0e056`: in `5814d86c`.
  - `5bd804a2`: in `c7ae46e9`.
  - Half of `605a2c3f`: in `0c601905`.
- **Already present (5):** `94c1f2de`, `a9c7d1c6`, `282f8973`, `ad5f436b`, `ba91cf63`. All are HAVE; the carrying fork commits are cited in the table.
- **DEFERRED (6):** `3d4efff1`, `0d95d93c`, `829bd5ea`, `d79be3c0`, `0e606364`, `cba9c847`.

The other seven HAVE rows are equivalents the fork had or found separately: `06856c9f`, `a5573baf`, `78e309fa`, `34022cf7`, `c9883d94`, `d981a267` and `8fbca782`.

## 2. Conflict notes for the PORT items

"Fork-modified" means the file differs between `c4b1fa69` and the fork HEAD, and existed at the merge-base. A file that both sides added is called out separately.

### 2.1 Recommended order and dependencies

The groups are listed in dependency order. Within a group, keep upstream order.

1. **MMU core (take first; everything SHR- or mouse-related builds on it).**
   1. `904ddd42`, mmu hunks only. `MMU::get_memory_base()` and `MMU::get_memory_size()` become virtuals. The commit itself is SKIP, but `18f5d04c` calls `get_memory_size()` in `bank_shadow_read`/`bank_shadow_write`.
   2. `18f5d04c`. Adds `iigs_aux_linear.hpp` and `MMU_IIgs::is_aux_linear()`.
   3. `35505bd6`. Adds `reset(bool cold_start)`.
   4. `42fd845e`. Adds `e1_aux_index()`; needs `is_aux_linear()` and the bank-`$E1` code from `18f5d04c`.
   5. `52dcd30f`.
   6. `7333660a`. Its context includes `reset(true)` from `35505bd6`.
   7. `bd182ac7`, mmu.hpp/mmu_iigs.hpp hunks. Makes `is_aux_linear()` a virtual on `MMU`. Its keygloo_mouse_sync.hpp hunk goes with the mouse group.
2. **ADB micro and ROM 03:** `992a5233` (needs `35505bd6`'s `reset(cold_start)`), then `887d9439` (needs `uc_rom03` from `992a5233`).
3. **Closed-loop ADB mouse sync.** This replaces the guest-memory poke that `a820c356` removed.
   1. Prerequisites: `d03c9783`, then `b299b98d`, then `19fe1f67`. They provide `video_system_t::osd_control_panel_open` and `video_system_t::content`.
   2. Then `4d761b08`, `70075531`, `eb582afd`, `a6949f8c`, `d36c06c0` and `90afdeb0`.
   3. Then `1f95ff78`. It must come after `18f5d04c`, because it includes `mmus/iigs_aux_linear.hpp` and calls `is_aux_linear()`.
   4. Then the mouse-sync hunk of `bd182ac7`, then `533d8763` and `d380e055`.
   5. `keygloo_mouse_sync.hpp` and `keygloo_state.hpp` are created by `4d761b08`. Every later mouse commit edits `keygloo_mouse_sync.hpp`, so the chain cannot be cherry-picked out of order.
4. **Ensoniq:** `67a84603` (the `halt_osc(..., newCtrl)` signature and the `$E0` read), then `6eefad52` (uses `newCtrl`; `$C03D` pipeline).
5. **Video and clock:** `b87142f3` (independent), then `03c52c82`, then `e169b2bc` (it rewrites the frame loop that `03c52c82` patched).
6. **Media (independent of the groups above):** `709653da` (media.cpp hunk only), `e2f6ab5a`, then `f1691706`. The last two both touch 2MG and `data_offset` semantics.
7. **Devices (independent):** `10ad57a4` (Apple Mouse card) and `bd4ca81e` (ThunderClock Plus).

### 2.2 Per-commit touch list

| upstream | Fork-modified files touched | Other notes |
|---|---|---|
| `67a84603` | `src/devices/es5503/ensoniq.cpp`, `ensoniq.hpp`, `soundglu.cpp`, `soundglu.hpp` | The catch-up half is already in the fork, from `d4c57c67`. The fork's `$E0` case has its own index guard and fixed bits (`3e28f2c3`), and the fork also changed these files in `15c8d121`, `6915c206`, `05c2a46b` and `c8652cc6` (Ensoniq snapshot). Take only the `$E0` read and the `halt_osc` signature. |
| `6eefad52` | `ensoniq.cpp`, `soundglu.cpp`, `soundglu.hpp`, `src/util/AudioSystem.cpp` | The fork's DOC output path (per-channel routing `05c2a46b`, audio probe `6915c206`/`42c963c3`) differs from upstream's SDL staging/resampler. Port the DOC register, IRQ and wrap semantics; re-derive or skip the host resampler and volume-slew hunks. New GLU state (`doc_read_latched_addr`) belongs in the fork's Ensoniq snapshot (`ensoniq_A2GSPU_snapshot`). Upstream `65ea90dd` (SKIP) sits between this commit and `67a84603` in soundglu.cpp; expect context fuzz only. |
| `d03c9783` | none (`src/ui/OSD.*` unmodified by the fork) | Host-side flag only. |
| `b299b98d` | `src/videosystem.cpp`, `src/videosystem.hpp` | The fork added headless screenshot readback there. |
| `19fe1f67` | `src/display/display.cpp` | The fork has the `display_read_vbl` guard, the scanline-latch fix and observer changes in the same file. |
| `4d761b08` | `src/devices/adb/ADB_Micro.hpp`, `ADB_Mouse.hpp`, `keygloo.cpp`, `keygloo.hpp` | Heaviest ADB conflict. The fork's ADB_Mouse has an absolute-position layer (`abs_x/abs_y`, clamp, 320-mode flag) from its forward-port, plus ROM-faithful GLU delta injection (`65ba5485`), a snapshot (`c8652cc6`) and Reset/Flush default-address fixes (`d4c57c67`). Upstream refactors motion encoding into `encode_relative_motion()` and moves `keygloo_state_t` into `keygloo_state.hpp`. The fork's `keygloo.cpp` frame handler no longer pokes guest memory after `a820c356`; keep it that way, and never enable upstream's `em_memory_patch_enabled()` path. |
| `70075531`, `eb582afd`, `533d8763` | `ADB_Micro.hpp` | New `KeyGloo` members and methods in the class body; `533d8763` adds a button-event gate in `process_event`, beside the fork's key-injection path. |
| `a6949f8c`, `d36c06c0`, `90afdeb0`, `1f95ff78`, `d380e055` | none (upstream-only `keygloo_mouse_sync.hpp`) | `90afdeb0` tests `computer->platform->id == PLATFORM_APPLE_IIGS_ROM3`, which does not exist in the fork; use the MMU's `is_rom03`. `d380e055` samples `$C029` through `mmu->read()`, a bus read the fork's I/O log, coverage and observers can see; prefer a side-effect-free NEWVIDEO accessor. |
| `bd182ac7` | `src/mmus/mmu.hpp`, `src/mmus/mmu_iigs.hpp` | Adds the `virtual bool is_aux_linear()` on `MMU`. |
| `18f5d04c` | `src/mmus/mmu_iigs.cpp`, `mmu_iigs.hpp`, `src/devices/displaypp/VideoScannerIIgs.cpp`, `src/gs2.cpp`, `Docs/AppleIIgs.md`; both sides added `src/mmus/iigs_memory.hpp` (fork copy from `3999d40e`) | This is the biggest behavioural change in the set. (a) It remaps the physical `$E1:2000-9FFF` layout while linearized or SHR. Every fork reader that indexes Mega II aux memory by CPU address (SHR layout/summary, CTRL-rail read/verify, diagnostics, bus-trace provenance in `bank_e1_write`, `c6532d20`'s probe-resolves-like-CPU path) has to go through `iigs_aux_linear_to_phys()`, and all SHR golden hashes must be regenerated. (b) It caps fast RAM at 8MB (`kMaxFastRamBytes = 0x80` banks), which contradicts the ROM01 `$81` and ROM03 `$8F` top bank the fork asserts after porting `e0729c93`. (c) Unpopulated banks float and shadow under `SPEED_SHADOW_ALL`. (d) HGR shadow applies to even banks only. |
| `35505bd6` | `mmu.cpp`, `mmu.hpp`, `mmu_ii.cpp`, `mmu_iie.hpp`, `mmu_iigs.cpp`, `mmu_iigs.hpp` | Signature change across the MMU hierarchy; the fork's probe/snapshot overrides in the same headers must follow. |
| `992a5233` | `ADB_Micro.hpp`, `keygloo.cpp`, `src/gs2.cpp` (plus unmodified `src/computer.cpp`) | Selects ROM 03 micro firmware on `PLATFORM_APPLE_IIGS_ROM3`; the fork must use `is_rom03`. The asset `roms/cards/keyglu/rom03-adb-341s0632-2.bin` already exists in the fork. The `KeyGloo` constructor gains an `is_rom03` parameter; update the fork's construction site in `keygloo.cpp`. |
| `42fd845e` | `mmu_iigs.cpp`, `mmu_iigs.hpp` | The fork's `bank_e1_write` carries `bus_trace_note`/`obs_note`/`slot_emit` hooks and the early return from `e17e046c`. Rebuild them around `e1_aux_index()` rather than taking the hunk verbatim. |
| `887d9439` | `ADB_Micro.hpp` | Repeat-rate table and delay clamp. |
| `52dcd30f` | `mmu_iigs.hpp` | Changes cycle types for banks `$F0-$FB`, which moves timing, so cycle-count gates may shift. |
| `7333660a` | `mmu_iigs.cpp`, `mmu_iigs.hpp` | New `dma_bank_register` (`$C037`) state; add it to the fork's MMU snapshot (`MMU_IIgs::A2GSPU_snapshot`). Fast cycle types for `$C035`/`$C036`/`$C068`/`$C02D` change cycle timing, so the fork's cycle-count gates may shift. |
| `b87142f3` | `VideoScanGenerator_RGB.cpp`, `VideoScanGenerator_RGB.hpp` | The fork changed SHR fill-mode seeding in the same generators (`4bcab908`). |
| `03c52c82` | `src/display/display.cpp`, `src/gs2.cpp` | `frame_video_update()` in gs2.cpp. |
| `e169b2bc` | `src/NClock.hpp`, `src/gs2.cpp`, `src/display/display.cpp`, `src/devices/es5503/soundglu.cpp` | Rewrites `run_one_frame` and `slow_incr_cycles`. The fork's NClock carries a per-cycle 14M cost reclaim and cycle timeline (`d7380d95`, `6828a03a`); its run loop carries the instrumentation funnels (now partly moved out by `3be31a3a`). Free-run mode only, but the IIgs `slow_incr_cycles` fast path is on the hot path of every run. |
| `709653da` | `src/util/media.cpp` (also `diskii_controller.hpp`, `ndiskii_woz.cpp`, `Floppy525_woz.hpp` for protocol hooks that should not be taken) | Take the `.po` `dos33_volume = 254` line only. Nibblized `.po` images change, so any disk-surface golden hashes change. |
| `e2f6ab5a` | `src/util/woz_nibblizer_35.cpp` (the fork already has `r+b`) | The real change is in unmodified `woz_nibblizer_525.cpp/.hpp`. |
| `f1691706` | `src/util/media.cpp` | The fork's 2MG block rejects `block_count == 0` (`f4bca2ca`) and zero or inconsistent `bytes_count` (`e17e046c`), and range-validates `data_offset` (`d4c57c67`). Upstream instead accepts such headers and derives sizes from the file. Merge by hand; keep the fork's comment/creator blob validation (`f4bca2ca`). |
| `10ad57a4` | `src/devices.cpp`, `src/devices/applemouseii/mouse.cpp`, `mouse.hpp`, `src/systemconfig.cpp`, `CMakeLists.txt`, `README.md` | Adds `src/devices/applemouseiii/*` and the firmware ROM asset. The new `applemouseiii.cpp` calls `computer->register_device_debug()` and `DEVOP_STATE_*`, which come from the DebugProtocol infrastructure the fork does not have; stub that call. Move the fork's headless `mouse_inject()` rail and `MODULE_MOUSECARD` registration onto the new `MouseController`. The `SystemConfig.cpp` hunk targets a file the fork does not have. |
| `bd4ca81e` | only `README.md`, `Docs/Documentation.md` (the ThunderClock sources are unmodified in the fork) | Uses `time(nullptr)`/`localtime`. Route it through the fork's deterministic time pin (`A2GSPU_FAKETIME`); otherwise any run with a ThunderClock in a slot is no longer reproducible. Needs `DH_THUNDERCLOCK` (0x16) in `DebugHandlerIDs.hpp`; upstream allocates 0x13-0x15 to the mouse card, SSC and VOC first. |

### 2.3 Deferred items that are still high value

The 2026-08 batch deferred these for stated reasons, so they stay DEFERRED in the table. The integration goal should still schedule them.

- **`3d4efff1`: IWM drive-enable model, empty-drive sense, LSS clocking.** It unlocks the rest of `605a2c3f` (the `read_sense` PHASE1 guard). Fork-modified files: `src/devices/iwm/IWM2.hpp`, `src/devices/floppy/Floppy525_woz.cpp`. `Floppy_woz.cpp` is unmodified.
- **`0d95d93c` then `829bd5ea`: video soft-switch mode-change latency.** Fork-modified files: `VideoScannerII.cpp/.hpp`, `VideoScannerIIe.hpp`, `VideoScannerIIePAL.hpp`, `VideoScannerIIgs.cpp/.hpp`. `ScanBuffer.hpp` is unmodified. Validate against the fork's SHR and video gates before trusting it.

## 3. Divergences the port must respect

Each item below was checked against both trees.

1. **The `Device_ID` enum is renumbered on each side.**
   - The two sides agree through `DEVICE_ID_PD_BLOCK3` (= 25).
   - **Fork:** `DEVICE_ID_A2GSPU` (26), `DEVICE_ID_UTHERNET2` (27), `DEVICE_ID_SECOND_SIGHT` (28), `NUM_DEVICE_IDS`.
   - **Upstream:** `DEVICE_ID_SECOND_SIGHT` (26), `DEVICE_ID_HOST_FST` (27), `DEVICE_ID_UTHERNET2` (28), `DEVICE_ID_SUPER_SERIAL` (29), `DEVICE_ID_VOC` (30). These were added by `51a34a3f`, `a1b2a00b`, `5e35081a` and `da900dcd`.
   - `Devices[]` in devices.cpp and upstream's `DeviceInfos[]` (device_info.cpp, which the fork does not have) are positional tables.
   - The fork's systems catalog prints numeric slot device IDs (`slotN=<id>` in systemconfig.cpp), so any renumbering is visible to catalog consumers.
   - Upstream also appended `MODULE_ENSONIQ` (`65ea90dd`) at the same `Module_ID` position as the fork's `MODULE_MOUSECARD`.
   - Platform IDs do not collide: upstream appends `PLATFORM_APPLE_IIGS_ROM3` after `PLATFORM_APPLE_IIGS`.
2. **Uthernet II is implemented differently on the two sides.**
   - **Fork:** `src/devices/uthernet2/{uthernet2,vnat,w5100}.{cpp,hpp}`, built-in virtual NAT, added in the forward-port.
   - **Upstream:** `uthernet2.cpp` plus `w5100_chip.*`, `net_worker.*`, `slirp_backend.*`, `slirp_netpick.*`, `glib_compat/`, `vendored/libslirp_cmake` and a libslirp submodule (`a1b2a00b`, `8498cc0a`, `9ac68b20`, `6fe61065`).
   - Both add `uthernet2.cpp`/`uthernet2.hpp` with different contents. Treat upstream's device as SKIP.
   - `6fe61065`'s buffer-overrun data-drop fix should be checked against the fork's `w5100.cpp` on its own.
3. **SecondSight PPU mode.**
   - At the merge-base, `secondsight.hpp` had `SS_MODE_PPU`, `PPURender` and `apply_ppu_mode()`. The fork's forward-port (`3fbb454d`) removed them from `secondsight.hpp`; `ppu_render.cpp/.hpp` remain in the tree, not included or built anywhere.
   - Upstream kept PPU mode, has 52 PPU references in `secondsight.hpp`, and layers GPU mode, host text, the VGA text overlay and the version command on top (`7de4cafc` through `d4db2e50`).
   - Any SecondSight PORT-LATER work has to re-apply the fork's removal or re-decide it.
   - Upstream `88c9a8e8` deletes `secondsight1.hpp`, which the fork only annotated as unbuilt.
4. **ROM 03 selection.**
   - **Upstream** has a separate platform (`PLATFORM_APPLE_IIGS_ROM3`, `PLATFLAG_APPLE_IIGS_ROM3`, `PLATFLAG_ANY_IIGS`, `platform_is_iigs()`, from `4fdb2eae`/`6fc73cd4`). It bundles ROM 03 images under `assets/roms/apple2gs_rom3/` and branches on the platform ID in the mouse sync, the ADB micro, NClockFactory, display, speaker, gamecontroller and OSD.
   - **The fork** keeps one `PLATFORM_APPLE_IIGS`. It uses SystemConfig personalities with a ROM-folder override (`load_platform_roms(..., rom_dir_override)`; `apple2gs3` and `apple2gs4` configs, `21a33233`) and derives `MMU_IIgs::is_rom03` from ROM size (`> 0x20000`). That flag persists in snapshots.
   - Every ported `== PLATFORM_APPLE_IIGS_ROM3` test becomes an `is_rom03` test.
5. **DebugProtocolServer versus the fork's CTRL rail.**
   - Upstream adds `src/debugger/DebugProtocolServer.*` (socket protocol), `clients/python/gs2debug` and `BreakpointTable`, with emit hooks in the `run_one_frame` breakpoint loop (`948ff765` onward, Windows support in `0df48213`).
   - The fork's agent and debug control surface is the file-based CTRL rail (`src/a2gspu_ctrl*.{hpp,cpp}`, manifest, rails registry). The fork has no DebugProtocolServer.
   - Upstream protocol verbs cover much the same ground: memory domains, step, trace buffer, register get/set, text snapshot, mount, paste, device state.
   - Protocol-only commits are SKIP. In-app debugger commits are PORT-LATER, with the protocol emit calls stubbed.
6. **`trace.cpp` rewrite versus the fork's trace tools.**
   - Upstream `1f671467` rewrites `src/debugger/trace.cpp` (876 lines changed).
   - Upstream `423811db` reworks `disasm.cpp` (230 lines), `trace.cpp` (253 lines), `trace_opcodes.cpp/.hpp` and adds `trace_format.hpp`.
   - The fork modified `trace.cpp`, `trace.hpp` (`f_write` flag), `trace_opcodes.cpp` and `disasm.cpp/.hpp` (+108 lines) for its trace-ring verb, itrace and the MMU-based disassembler rail.
   - Expect a manual merge, and re-run the fork's trace and disassembly gates.
7. **Run loop and computer.cpp.** Only part of the run loop moved.
   - On `upstream/main`, `run_one_frame()`, `run_cpus_init()` and `frame_sleep()` are still in `src/gs2.cpp`; they already were at the merge-base.
   - What `e169b2bc` moved into `src/computer.cpp`/`computer.hpp` is the ludicrous-speed calibration state machine (`begin_ludicrous_calibration`, `update_ludicrous_calibration`, `LS_CAL_*` state) and related frame state. It also rewrote about 268 lines of `run_one_frame`.
   - Later upstream commits (`16066ec9`, `99fbb448`, `bf8471d4`, `29b57cba`, `fb52cc70`, `a65700ea`, `7de4cafc`, `992a5233`) add more behaviour to `computer.cpp`, a file the fork never modified.
   - The fork's run-loop instrumentation lives in `gs2.cpp` and, since `3be31a3a`, in `a2gspu_spike.cpp`, `a2gspu_ctrl_cmds.cpp` and related files.
8. **Deliberate fork semantics that upstream contradicts:**
   - **Game switches.** The fork keeps an absent game switch reading not-pressed; upstream's `d79be3c0`/`0e606364` make it float. DEFERRED.
   - **Guest memory.** The fork never writes guest memory from the host (`a820c356`). Upstream's mouse sync still defines the Event Manager write path (`write_em_axis`), compiled out by `KEYGLOO_MOUSE_SYNC_INJECTION_ONLY=1` and with no remaining callers. Do not carry it over.
   - **BRAM.** The fork pins BRAM per run (`a2gspu_bram_pin.hpp`); upstream `a8ad06bb` keys BRAM files by config UUID.
   - **DOC audio output.** The fork uses per-access catch-up with per-channel routing; upstream uses SDL staging and a resampler (`6eefad52`, `160721cc`).

## 4. Fork fixes upstream lacks

Each item was checked by reading `upstream/main` source.

**The three requested:**

1. **Native TXS copies the full 16-bit X** (fork `4bcab908`, `transfer_x_s()`).
   - Upstream `transfer_x_s()` does `cpu->sp = reg` with `reg = _X(cpu)`. `_X` returns `x_lo` when the X flag is set, so upstream zero-extends 8-bit X.
   - Upstream now clears `x_hi` on SEP, PLP, RTI and XCE-to-emulation (`c9883d94`). While that `x_hi == 0` invariant holds, the two agree. The fork's version does not depend on the invariant.
2. **MVN/MVP use 16-bit X and Y** (fork `4bcab908`, `move_memory()` and opcodes `$44`/`$54`).
   - **Upstream:** builds source and destination as `full_db | _X(cpu)` and `full_db | _Y(cpu)`, and steps with `_X(cpu)++`/`_Y(cpu)++`. With X=1 those are `x_lo`/`y_lo`, so addresses use 8-bit indices and the indices wrap at `$FF`. It also sets `db = SBA` temporarily before the read.
   - **Fork:** always uses `(uint16_t)cpu->x`/`cpu->y` with explicit bank bytes, steps `cpu->x`/`cpu->y` as 16-bit, and sets DBR to the destination bank.
   - The hardware claim rests on the fork's cited W65C816 behaviour. It was not independently re-derived here.
3. **SHR fill-mode seed from `SCB & 0x1F`** (fork `4bcab908`, `shr_fillmode_init[]` in `generate/AppleIIgs.hpp`, applied on `VM_SHR_MODE`).
   - Upstream still indexes `fillmode_init[vcount & 0x1F]` on HSYNC in `VideoScanGenerator.cpp`, `VideoScanGenerator_Comp.cpp` and `VideoScanGenerator_RGB.cpp`.
   - Upstream's static `generate/AppleII.cpp` does the same with `src_line & 0x1F`.

**Additional fork fixes, also absent on `upstream/main`:**

4. **65816 reset sets I=1** (fork `3e28f2c3`). Upstream `reset()` sets I only in the non-65816 branch.
5. **`$C02E`/`$C02F` reads have no interrupt side effect** (fork `3e28f2c3`). Upstream `display_read_C02EF` still clears `f_scanline_asserted` and calls `update_vgc_interrupt()`.
6. **IIgs `$C011-$C01F` status reads return floating bus in bits 6-0** (fork `30c1e8fa`). Upstream `mmu_iigs.cpp` still returns `fl` with bits 6-0 zero.
7. **Emulation-mode `(d)` pointer high-byte wrap when DL=0** (fork `f4bca2ca`, `address_direct_indirect()`). Upstream reads `eaddr_16 + 1`.
8. **NMOS 6502 decimal-mode Z from the binary result including carry** (fork `f4bca2ca`, `add_bcd_8_set_flags`/`sub_bcd_8_set_flags`). Upstream uses `a+b` and `a-b+C`.
9. **NMOS `JMP ($xxFF)` page bug and CMOS 6-cycle `JMP (abs)`.**
   - The page bug is fork `f4bca2ca`; the 65C02 extra cycle (not on 65816) is `d4c57c67`.
   - Upstream has the `has_indirect_bug` trait but `OP_JMP_IND` still reads `aa + 1` unconditionally, with a TODO.
10. **ADB device Reset/Flush restore the power-on address and SRQ bits** (fork `d4c57c67`, `ADB_Device::reset_to_default_address()`, mouse and keyboard `reset()`/`flush()`). Upstream's `ADB_Mouse` and `ADB_Keyboard` `reset()`/`flush()` are still empty, and `ADB_Device.hpp` is unchanged since the merge-base.

## 5. Open questions not settled here

- **`18f5d04c` impact count.** The "~44 direct-read sites" figure was not re-counted exactly.
  - A quick grep of `e35908d2` finds 18 `get_megaii_memory_base()`/`megaii->get_memory_base()` call sites outside `src/mmus`, in `a2gspu_ctrl_cmds`, `a2gspu.cpp`, `keygloo.cpp`, `gs2.cpp` and others.
  - There are also hard-coded `0x1xxxx` aux offsets in `iigs_shr_layout.hpp` and the CTRL commands.
  - The authoritative list needs a per-site review.
- **Whether the fork's 16-bit MVN/MVP rule matches silicon.** Upstream and the fork disagree, and this triage did not settle which is right.
- **Whether `f1691706`'s acceptance of zero-length 2MG headers is safe** alongside the fork's comment/creator blob validation. This needs a merged implementation and the fork's 2MG gates.
- **Whether `6fe61065`'s W5100 buffering defect exists in the fork's own `w5100.cpp`.** Not checked. Moot now: that file was replaced by the fused card, which is built on `6fe61065` (section 9).

## 6. Triage table (upstream order)

| upstream sha (8) | date | subject (truncate 70) | area | disposition | note |
|---|---|---|---|---|---|
| 01bc459a | 2026-06-26 | initial emscripten proof of concept work | build/ci | SKIP | Upstream-only web (Emscripten) build target. Native paths unchanged: the scc8530.cpp, RGBA.hpp and applekeys.hpp hunks are all `__EMSCRIPTEN__`-guarded. |
| 94c1f2de | 2026-06-26 | II/II+ vid scanner should ignore dblres flag | video | HAVE | d4c57c67 (`supports_dblres` on VideoScannerII; confirmed present during the 3999d40e review). |
| 06856c9f | 2026-06-28 | support cycle_handler in clock_iigs | clock | HAVE | d4c57c67 (IIgs clock now dispatches per-video-cycle handlers too). |
| 67a84603 | 2026-06-28 | ensoniq interrupt fix. and change-during-frame fix #102, #107, | audio/ensoniq/mockingboard | PORT | Fork already has the per-access DOC catch-up and cycle-handler part (d4c57c67). Still missing: the MAME-style `$E0` read (report and clear the first pending oscillator, deassert the IRQ line, re-assert it if any remain, IR bit in bit 7) and the `halt_osc(newCtrl)` signature that 6eefad52 needs. |
| f176593d | 2026-06-29 | updated | docs | SKIP | Compatibility notes only. |
| 821c3165 | 2026-06-29 | initial commit PPU mode spec | docs | SKIP | SecondSight PPU-mode spec doc (fork removed PPU mode). |
| 1a8b1bca | 2026-06-29 | add folders related to emscripten build | build/ci | SKIP | .gitignore entries for the web build. |
| a19bdedb | 2026-06-29 | Merge branch 'emscripten' | other | MERGE | Merges the emscripten branch; no resolution content. |
| fb677e24 | 2026-06-29 | try using borderless window for fullscreen instead of exclusive mode. | ui/config/menus/osd/shaders | PORT-LATER | Borderless-window fullscreen (videosystem.cpp, which the fork also modified). |
| 30c48bfa | 2026-06-29 | implement high-dpi mode and correct scaling of OSD components | ui/config/menus/osd/shaders | PORT-LATER | High-DPI mode and OSD scaling. Touches gs2.cpp and videosystem.*, which the fork also modified. |
| 5c6ba767 | 2026-06-30 | enhance high-DPI support by adjusting UI scaling and adding display... | ui/config/menus/osd/shaders | PORT-LATER | High-DPI follow-up (gs2.cpp, videosystem.cpp). |
| 282f8973 | 2026-06-30 | fix #127 - soundctl reg lo 4 bits are write only and should return ... | audio/ensoniq/mockingboard | HAVE | d4c57c67 (fork found the same soundctl read-back defect on its own; recorded in f646760c and 3999d40e). |
| a9c7d1c6 | 2026-06-30 | fix broken DHGR introduced when we fixed II+ delay color in VideoSc... | video | HAVE | d4c57c67 (`supports_dblres` overrides in all three scanner subclasses; per 3999d40e). |
| 144ee877 | 2026-06-30 | implement GPU shader pipeline for testing | ui/config/menus/osd/shaders | PORT-LATER | GPU CRT shader pipeline. Also compiles out the F7 hires dump key in display.cpp. |
| ef4532e1 | 2026-06-30 | Merge branch 'crtshader' | other | MERGE | Merges the crtshader branch; no resolution content. |
| b3338b4f | 2026-07-02 | load shader source from file at runtime | ui/config/menus/osd/shaders | PORT-LATER | Loads shader source from a file at runtime. |
| a397bcb8 | 2026-07-02 | forgot: part of load shader source from file at startup | ui/config/menus/osd/shaders | PORT-LATER | CMake step that copies shader assets (goes with b3338b4f). |
| 07995edd | 2026-07-02 | notes | docs | SKIP | Notes (development log, external-debug and memory-protection design docs). |
| ce798ae9 | 2026-07-03 | notes | docs | SKIP | Development log. |
| 2128ce24 | 2026-07-03 | improve shader parameters; scale properly to screen resolution | ui/config/menus/osd/shaders | PORT-LATER | Shader parameters and screen-resolution scaling. |
| c4ea4cab | 2026-07-03 | Merge branch 'crtshader' | other | MERGE | Merges crtshader; no resolution content. |
| d0428dba | 2026-07-03 | refactor CRT shader handling; add option to enable shader at boot a... | ui/config/menus/osd/shaders | PORT-LATER | CRT shader refactor plus a boot-time enable option. |
| b5d727a4 | 2026-07-03 | Merge branch 'crtshader' | other | MERGE | Merges crtshader; no resolution content. |
| da4f8bf1 | 2026-07-04 | notes | docs | SKIP | Development log. |
| 8204b5ac | 2026-07-04 | Merge branch 'main' of [upstream mirror] | other | MERGE | Merge between upstream mirrors. Carries conflict-resolution hunks (CMake, shader, display). |
| 0004439d | 2026-07-04 | add bit on CRT shader | docs | SKIP | Displays doc. |
| d03c9783 | 2026-07-04 | add helper routines for detecting when CP or modal is open | ui/config/menus/osd/shaders | PORT | Prerequisite for the mouse sync: `video_system_t::osd_control_panel_open`, set by the OSD and read by keygloo_mouse_sync.hpp to give back the host cursor. |
| b299b98d | 2026-07-04 | calculate and remember the content area of the frame based on "inse... | ui/config/menus/osd/shaders | PORT | Prerequisite for the mouse sync: `render_frame` computes the guest content rect (`video_system_t::content`). |
| 19fe1f67 | 2026-07-04 | send an inset rect to render_frame to help it calculate the "conten... | video | PORT | Prerequisite for the mouse sync: display.cpp passes content insets for II and SHR frames. Its own TODO: the fixed insets misalign when monitor H/V calibration is non-zero. |
| 4d761b08 | 2026-07-04 | this commit works well, but is writing to guest memory, which we di... | adb/mouse/kbd | PORT | Creates keygloo_mouse_sync.hpp and keygloo_state.hpp, and adds the relative-motion encoder to ADB_Mouse. This first version still wrote Event Manager globals into guest memory. Port it only as the base: 70075531 compiles that path out and eb582afd removes its callers. Drop the write path entirely in the fork. |
| 70075531 | 2026-07-04 | Add closed-loop injection support for mouse synchronization in KeyGloo | adb/mouse/kbd | PORT | Closed-loop sync: read the guest's Event Manager cursor back, inject bounded ADB relative steps until it converges. Selected by `KEYGLOO_MOUSE_SYNC_INJECTION_ONLY=1`. |
| eb582afd | 2026-07-04 | remove deprecated routines | adb/mouse/kbd | PORT | Removes the `$C024`-read hooks that patched Event Manager coordinates (the guest-memory write callers). |
| d4de0c7e | 2026-07-04 | NOTES | docs | SKIP | Roadmap. |
| 2e86d1d9 | 2026-07-05 | docs update | docs | SKIP | Mouse doc. |
| 32f2713a | 2026-07-06 | new applet for testng TOML file processing | ui/config/menus/osd/shaders | PORT-LATER | Test app and fixtures for TOML config. |
| c884e444 | 2026-07-06 | implement TOML config file parsing, loading, and execution | ui/config/menus/osd/shaders | PORT-LATER | TOML config parse, load and run (gs2.cpp, computer.hpp, menus). |
| f38da57f | 2026-07-06 | notes | docs | SKIP | README. |
| 5706de46 | 2026-07-06 | included for toml file processing | ui/config/menus/osd/shaders | PORT-LATER | Vendored toml.hpp (dependency of c884e444). |
| 60791ee9 | 2026-07-07 | add reader/loader for [user]'s Settings.txt files; add support to t... | ui/config/menus/osd/shaders | PORT-LATER | Loader for external Settings.txt profiles, plus tests. |
| 6abb54cd | 2026-07-07 | notes | docs | SKIP | Development log. |
| 6e3237dd | 2026-07-07 | add doc for config files | docs | SKIP | Config-file doc. |
| d634e92a | 2026-07-07 | notes | docs | SKIP | Changelog and roadmap. |
| 5c456470 | 2026-07-07 | fix adbtest build on mac mini | build/ci | PORT-LATER | macOS-only link fix for apps/adbtest. |
| cd176629 | 2026-07-07 | windows build fix | build/ci | PORT-LATER | Windows CMake link fixes (scc8530 to serial devices, device registry to ntsc). Needed once the config/serial refactors land. |
| 6f296ee6 | 2026-07-07 | implement "keyboard buffer disabled" mode | adb/mouse/kbd | PORTED | 8d6f6cb5. The two redundant `load_key_from_buffer` guards were intentionally not taken, because the fork's function already guarantees them. |
| e5443817 | 2026-07-07 | Merge branch 'main' of github.com:jawaidbazyar2/gssquared | other | MERGE | Mirror merge; no resolution content. |
| a6949f8c | 2026-07-07 | fix #129 detect 320 mode more accurately | adb/mouse/kbd | PORT | Mouse sync: 320-column SHR detection from line-0 SCB. The physical-address read is replaced by 1f95ff78. |
| bcfad583 | 2026-07-09 | Clean up button constructors to encap asset ids; do some code reuse... | ui/config/menus/osd/shaders | PORT-LATER | OSD button constructor cleanup. |
| 2a88074e | 2026-07-09 | Add StorageButtonFactory and SlotsPanel for dynamic button creation | ui/config/menus/osd/shaders | PORT-LATER | Config editor, StorageButtonFactory, SlotsPanel (touches gs2.cpp). |
| b8cd62f1 | 2026-07-09 | Add support for .gs2 file type in macOS save dialogs | ui/config/menus/osd/shaders | PORT-LATER | macOS .gs2 save dialog. |
| 60d24990 | 2026-07-09 | Update menu item titles and documentation for config file launch | ui/config/menus/osd/shaders | PORT-LATER | Menu titles for config launch (gs2.cpp, menus). |
| 1f7873e6 | 2026-07-09 | add --parallel to macos build for the speedz | docs | SKIP | README build tip. |
| 0335aa81 | 2026-07-09 | roadmap for 0.9 is complete!!!! | docs | SKIP | Roadmap. |
| 9817fce0 | 2026-07-09 | add some docs to cover config profile | docs | SKIP | Config editor doc. |
| e5a86ac7 | 2026-07-09 | notes | docs | SKIP | Development log. |
| 43bd5782 | 2026-07-09 | rename a bit shorter to fit in Editor button | ui/config/menus/osd/shaders | PORT-LATER | Shorter platform display name (platforms.cpp, which the fork also modified). |
| ac6eee0e | 2026-07-09 | refactor to remove duplicate device flags and centralize in device_... | ui/config/menus/osd/shaders | PORT-LATER | Centralizes device flags in device_info and refuses duplicate cards. Rewrites devices.cpp, which also carries the fork's A2GSPU/UTHERNET2 registrations. |
| 98b174e7 | 2026-07-09 | Merge branch 'configpane' | other | MERGE | Merges configpane; no resolution content. |
| a5573baf | 2026-07-09 | Add unique instanceID for Floppy525_woz to handle multi-drive events | disk/iwm/smartport/fst | HAVE | d4c57c67 (unique per-slot/per-drive Floppy525_woz event instanceID). |
| 4cd79e68 | 2026-07-09 | notes | docs | SKIP | AGENTS.md and notes. |
| b8cb3ea5 | 2026-07-09 | bump version | build/ci | SKIP | Upstream version bump. |
| b693acb3 | 2026-07-10 | include clock be default on Enh IIe for A2Desktop | ui/config/menus/osd/shaders | PORT-LATER | Changes the built-in IIe Enhanced slot layout (adds a clock, moves mouse/Disk II/BazFast). Configuration, not a fidelity fix; fork gates and goldens reference built-in configs. |
| c95348f1 | 2026-07-10 | fix description (no clock on IIe unenhanced) | ui/config/menus/osd/shaders | PORT-LATER | Built-in config description string. |
| bd3a75b2 | 2026-07-10 | move custom configs into gs2 files; copy them into Prefs on startup... | ui/config/menus/osd/shaders | PORT-LATER | Moves custom built-in configs out to .gs2 files, which removes rows from the systemconfig.cpp table. The fork's config numbering depends on that table. |
| 55650332 | 2026-07-10 | update .gitignore to allow .gs2 files | build/ci | PORT-LATER | .gitignore allows .gs2 (goes with bd3a75b2). |
| 6f33a97b | 2026-07-10 | Refactor system configuration handling and UI components | ui/config/menus/osd/shaders | PORT-LATER | SystemSettings store and system selection UI. |
| 2c19e827 | 2026-07-10 | show descriptions of + and Edit at bottom when hovering | ui/config/menus/osd/shaders | PORT-LATER | Hover descriptions. |
| 7e9afc47 | 2026-07-10 | modified descriptions | ui/config/menus/osd/shaders | PORT-LATER | .gs2 description strings. |
| a734321b | 2026-07-10 | redo style of the new/edit buttons | ui/config/menus/osd/shaders | PORT-LATER | New/Edit button style. |
| 2518af54 | 2026-07-10 | notes | docs | SKIP | Changelog. |
| a8ad06bb | 2026-07-10 | use (auto generate and save when appropriate) ID field for .gs2 fil... | ui/config/menus/osd/shaders | PORT-LATER | Per-config UUID and per-config BRAM file (RTC.hpp, rtc_pram.cpp). Conflicts with the fork's BRAM pinning for deterministic runs. |
| 3305462b | 2026-07-10 | Merge branch 'configpane' | other | MERGE | Merges configpane; no resolution content. |
| 66f6349d | 2026-07-10 | tweak name of trace dump | debugger/debugproto | PORT-LATER | Renames the shutdown trace dump file (gs2.cpp). |
| 235b9662 | 2026-07-10 | link to new web site | docs | SKIP | README link. |
| e1443a0e | 2026-07-10 | display version number in places | ui/config/menus/osd/shaders | PORT-LATER | Shows the version number in the UI. |
| 6d526656 | 2026-07-10 | add launch button | ui/config/menus/osd/shaders | PORT-LATER | Launch button. |
| 9b944511 | 2026-07-10 | Merge branch 'configpane' | other | MERGE | Merges configpane; no resolution content. |
| 948ff765 | 2026-07-11 | Enhance debugger functionality by adding DebugProtocolServer suppor... | debugger/debugproto | SKIP | Divergence: DebugProtocolServer and Python client overlap the fork's CTRL rail. |
| 7fd2a990 | 2026-07-11 | Implement RESET command in DebugProtocolServer and update Python cl... | debugger/debugproto | SKIP | Divergence: DebugProtocolServer RESET plus client docs (CTRL rail overlap). |
| a73d3e3d | 2026-07-11 | code cleanup / reuse | debugger/debugproto | SKIP | Divergence: DebugProtocolServer cleanup (CTRL rail overlap). |
| e951d159 | 2026-07-11 | Add support for MEGAII memory domain in DebugProtocolServer | debugger/debugproto | SKIP | Divergence: DebugProtocolServer MEGAII domain (CTRL rail overlap). |
| 904ddd42 | 2026-07-11 | Add support for MAIN_RAW and MEGAII_RAW memory domains in DebugProt... | debugger/debugproto | SKIP | Divergence: DebugProtocolServer raw domains (CTRL rail overlap). Its mmu.hpp/mmu_ii/mmu_iigs hunks add the virtuals `MMU::get_memory_base()/get_memory_size()`, which 18f5d04c calls. Take those hunks, or an equivalent, before 18f5d04c. |
| d36c06c0 | 2026-07-15 | adb: read the ADB mouse cursor from the Event Manager bank globals | adb/mouse/kbd | PORT | Mouse sync reads the cursor from the `$E1/0190-0193` bank globals. Reworked to depend on ROM by 90afdeb0. |
| f6a9a5c6 | 2026-07-15 | mmu/iigs: derive bank $FF offset from ROM size — ROM 03 (256K) su... | mmu | PORTED | 3999d40e (bank `$FF` offset from ROM size, single `rom_bank_ff_offset()`). |
| 3d4efff1 | 2026-07-15 | iwm/floppy: correct drive-enable model + empty-drive sense + LSS cl... | disk/iwm/smartport/fst | DEFERRED | Larger IWM rework: drive-enable model, empty-drive sense, LSS clocking. 0c601905 took only the handshake half of 605a2c3f, because the PHASE1 sense guard needs this commit's `is_mounted` and applied alone it hung POST. High value for the integration goal. |
| c0570460 | 2026-07-15 | add donate link to the menus | ui/config/menus/osd/shaders | SKIP | Upstream donation link in the menus. |
| 4fdb2eae | 2026-07-15 | platforms: add an Apple IIgs (ROM 3) machine | other | SKIP | Divergence: upstream adds a separate "Apple IIgs (ROM 3)" platform ID. The fork selects ROM 3/ROM 4 through ROM-folder-override personalities (21a33233) and detects ROM 03 behaviour from ROM size. |
| 9420cac6 | 2026-07-15 | fix bug eaddr wrong in trace on LDA [d,X]; define and use a R/W fla... | debugger/debugproto | PORTED | f646760c (eaddr fixed only at the two `(d,X)` sites; per-instruction `f_write` R/W flag). |
| 16066ec9 | 2026-07-15 | Add BreakpointTable class for managing breakpoints in the debugger | debugger/debugproto | PORT-LATER | BreakpointTable (exec/data/IO) for the in-app debugger. Adds hooks to the run_one_frame loop in gs2.cpp, which the fork also modified; its protocol emit calls need stubbing without DebugProtocolServer. |
| b92dccd5 | 2026-07-15 | implement step_info with count in protocol | debugger/debugproto | SKIP | Divergence: protocol step_info (CTRL rail overlap). |
| 74b2ac20 | 2026-07-15 | implement access to trace buffer | debugger/debugproto | SKIP | Divergence: protocol trace-buffer access (fork has its own trace-ring verb, f646760c). |
| 65ea90dd | 2026-07-16 | ensoniq device introspection via debug protocol | debugger/debugproto | SKIP | Divergence: protocol Ensoniq introspection. Also adds `MODULE_ENSONIQ` at the same enum position as the fork's `MODULE_MOUSECARD`. |
| 6eefad52 | 2026-07-16 | round of ensoniq bug fixes | audio/ensoniq/mockingboard | PORT | DOC fidelity: CPU-initiated halt fires an IRQ only if the new control value enables it; free-run loop wrap subtracts `(wtsize-1)`; halt when `altram >= wtsize`; one-access `$C03D` read pipeline with latched address; `$C03C` write clears busy and, in register mode, `soundadrh`. The host-side SDL staging/resampler and per-sample volume slew sit on an output path the fork replaced (05c2a46b), so treat those as optional. |
| 82ddc936 | 2026-07-16 | add some scancodes for arrows | debugger/debugproto | SKIP | Python client scancodes (upstream-only tooling). |
| be5c869e | 2026-07-17 | reevaluate IRQ pending to system on reset | audio/ensoniq/mockingboard | PORTED | 3999d40e (re-evaluate DOC IRQ after reset). |
| 52839086 | 2026-07-17 | python scripts for testing ensoniq apps that were misbehaving | debugger/debugproto | SKIP | Python debug scripts (upstream-only tooling). |
| 1e53380b | 2026-07-17 | implement feature request #134 | ui/config/menus/osd/shaders | PORT-LATER | Screenshot feature (videosystem.cpp, which the fork also modified; the fork has its own headless screenshot readback). |
| 92ea8380 | 2026-07-17 | notes | docs | SKIP | Compatibility notes. |
| 78e309fa | 2026-07-17 | implement items from issue #139. | joystick | HAVE | 49822387 (null-gamepad guards on switch readers and strobe; stick centred when no pad). Not taken: the optional "Disconnected When No Gamepad" host preference, which needs SystemSettings. |
| cacab7dd | 2026-07-17 | implement IIgs French Keyboard layout | adb/mouse/kbd | PORT-LATER | French AZERTY ADB key map, chosen by the language nibble of the KeyGLU config byte. US layout unchanged. |
| 160721cc | 2026-07-18 | implement DOC stereo output; when mono detected, copy to both channels | audio/ensoniq/mockingboard | PORTED | 05c2a46b (stereo CA0 routing plus mono mirror, ported as semantics; the resampler and the `#if 0` volume experiment were not taken). |
| 8d45c69f | 2026-07-18 | test of (final?) fix to french kbd layout. pending testing | adb/mouse/kbd | PORT-LATER | French layout follow-up. Adds a non-ADB keycode `0x0A` for the ISO key. |
| 8fa4ce40 | 2026-07-18 | Merge branch 'feature140' | other | MERGE | Merges feature140; no resolution content. |
| 17175bf8 | 2026-07-18 | Refactor file dialog handling to use SystemSettings for tracking la... | ui/config/menus/osd/shaders | PORT-LATER | File-dialog last-path tracking in SystemSettings (gs2.cpp). |
| 5b0d8373 | 2026-07-18 | add bpd and bpi breakpoint commands | debugger/debugproto | PORT-LATER | In-app debugger bpd/bpi commands. |
| 7f1e34dc | 2026-07-18 | Add Monitor class for enhanced debugging capabilities | debugger/debugproto | PORT-LATER | Monitor class refactor of the in-app debugger. |
| ab948317 | 2026-07-18 | Quality of Life : Enhance debug window with scroll functionality | debugger/debugproto | PORT-LATER | Debug window scrolling and ScrollBar widget. |
| a44b3589 | 2026-07-18 | Merge branch 'debuginterface' | other | MERGE | Merges debuginterface. Has conflict-resolution hunks in the debugger files. |
| ad5f436b | 2026-07-19 | mmu/iigs: enable ROM 03 text page 2 shadowing | mmu | HAVE | 21a33233 (`is_rom03` from ROM size; the TEXT2 shadow gate was already at the merge-base). |
| 709653da | 2026-07-19 | DebugProtocol hooks for diskii. .po file now uses vol=>0 for compat... | disk/iwm/smartport/fst | PORT | media.cpp: nibblized .po DOS 3.3 volume goes 1 -> 254 (some ProDOS drivers misstep with volume 1). The DiskII DebugProtocol hooks in the same commit are CTRL-rail overlap and not needed. |
| 10ad57a4 | 2026-07-19 | Add Apple Mouse III support | adb/mouse/kbd | PORT | Replaces the paravirtual mouse card behind `DEVICE_ID_MOUSE` with a real Apple Mouse card emulation (PIA 6520, 6805 protocol, Apple firmware ROM). The fork's headless `mouse_inject()` hook and `MODULE_MOUSECARD` registration in applemouseii/mouse.cpp have to move to the new controller. |
| cf52d5ec | 2026-07-19 | updates | docs | SKIP | Compatibility notes. |
| 51a34a3f | 2026-07-19 | initial port IIgs Host FST support. | disk/iwm/smartport/fst | PORT-LATER | Host FST: a paravirtual WDM `$42` operand dispatch added to the CPU core, and auto-added to the IIgs motherboard device list. Must stay off by default for oracle runs. Appends `DEVICE_ID_HOST_FST` (see Device_ID divergence). |
| 99fbb448 | 2026-07-19 | implement built-in drivers volume (a'la vmware) that the user can t... | disk/iwm/smartport/fst | PORT-LATER | Built-in drivers volume toggle (Host FST). |
| e1f62d6a | 2026-07-19 | documentation for host fst and drivers | docs | SKIP | Host FST docs. |
| 855c93bf | 2026-07-19 | display buttons to easily control debug module displays | debugger/debugproto | PORT-LATER | Debug-module display buttons. |
| 0d95d93c | 2026-07-20 | not quite fully working attempt to implement latency in video softs... | video | DEFERRED | Video soft-switch latency. Titled by its author as not fully working; the fork has no mode-change delay mechanism (3999d40e). |
| 829bd5ea | 2026-07-22 | fixes to pass vsync and vidmodes tests. unsure about some of the GS... | video | DEFERRED | Latency tuning on top of 0d95d93c; the author is unsure about some GS switches (3999d40e). Both are high value once validated against the fork's SHR/video gates. |
| ce854e28 | 2026-07-22 | notes | docs | SKIP | Notes and to-do list. |
| ba91cf63 | 2026-07-22 | fix offset error in .2mg files | disk/iwm/smartport/fst | HAVE | d4c57c67 (2MG data_offset honoured, with range validation; per 3999d40e). |
| 6b91d9c1 | 2026-07-23 | fix Mad Effects 2 - IRQ was only taking 6 cycles, supposed to be 7,... | cpu | PORTED | f646760c (IRQ entry phantom opcode read on every CPU type; `display_read_vbl` null guard). |
| 3815a316 | 2026-07-23 | notes | docs | SKIP | Notes. |
| 695f4402 | 2026-07-23 | support GS-style '/' bank separator and memory; display 24-bit addr... | debugger/debugproto | PORT-LATER | Monitor: GS-style bank/address notation. |
| d2a0e056 | 2026-07-23 | stereo-ify drive sound effects | audio/ensoniq/mockingboard | PORTED | 5814d86c (per-drive stereo placement of drive sounds, plus leak and null fixes). |
| 6fc73cd4 | 2026-07-23 | update enum and flags to identify rom03 gs as well as rom01 | other | SKIP | Divergence: adds the `PLATFORM_APPLE_IIGS_ROM3` enum, `PLATFLAG_ANY_IIGS`, `platform_is_iigs()` and bundled ROM 03 images. The fork has no separate ROM 3 platform, so later PORT items that test `PLATFORM_APPLE_IIGS_ROM3` must use the MMU `is_rom03` flag instead. |
| 70837e95 | 2026-07-23 | merge pr #137: correct iwm drive-enable | other | MERGE | Merges PR #137 (3d4efff1); no resolution content. |
| 605a2c3f | 2026-07-23 | pull in pr137 as well for getting around hang during boot | disk/iwm/smartport/fst | PORTED | 0c601905, partial: reset starts the write handshake READY, plus the `async_buffer_valid` guard. Not taken: the `read_sense` PHASE1 guard (needs 3d4efff1) and the iigsmmutest additions. |
| 2985e83a | 2026-07-23 | merge pr #135: read the ADB mouse cursor from the Event Manager ban... | other | MERGE | Merges PR #135 (d36c06c0); no resolution content. |
| 90afdeb0 | 2026-07-23 | fix rom03 mouse tracking | adb/mouse/kbd | PORT | Mouse sync reads its feedback from the bank globals on ROM 03 and from the screen holes on ROM 01. Upstream keys this on `PLATFORM_APPLE_IIGS_ROM3`; the fork must key it on `is_rom03`. |
| 4c9184a3 | 2026-07-23 | Merge branch 'rom03_138' | other | MERGE | Merges rom03_138. Its conflict resolution dropped Host FST entries, which 0556e3c1 restored. |
| 0556e3c1 | 2026-07-23 | I hosed the merge, adding host_fst back in and correcting secondsig... | ui/config/menus/osd/shaders | PORT-LATER | device_info/platforms/OSD fixups for Host FST and the ROM 3 platform flags. |
| a1b2a00b | 2026-07-27 | add Uthernet II card (using slirp, works out of the box on macos/wi... | net/serial | SKIP | Divergence: Uthernet II is implemented differently on the two sides (fork vnat.cpp/w5100.cpp, upstream libslirp slirp_backend/w5100_chip). Both add src/devices/uthernet2/. |
| d948aad2 | 2026-07-27 | give agents a bit more to chew on | docs | SKIP | AGENTS.md. |
| 63a69e79 | 2026-07-27 | notes | docs | SKIP | Notes. |
| 8498cc0a | 2026-07-27 | pin this particular version of libslirp | net/serial | SKIP | libslirp submodule pin (upstream Uthernet implementation). |
| 73e89ba3 | 2026-07-27 | notes | docs | SKIP | Notes. |
| 5e35081a | 2026-07-28 | add super serial card | net/serial | PORT-LATER | Super Serial Card (6551 plus firmware). New device; appends `DEVICE_ID_SUPER_SERIAL`. |
| 190a919d | 2026-07-28 | notes | docs | SKIP | Notes. |
| 93d3d718 | 2026-07-28 | FileDevice throws up a toast whenever a file is closed with the fil... | net/serial | PORT-LATER | FileDevice worker thread and parallel card refactor (parallel.cpp and scc8530.cpp, both also modified by the fork). |
| d4cc5bcf | 2026-07-28 | notes | docs | SKIP | To-do list. |
| 5bd8570a | 2026-07-28 | fix use-after-free bug in upstream host_fst.c file | disk/iwm/smartport/fst | PORT-LATER | Use-after-free fix in vendored host_fst.c (take with 51a34a3f). |
| 29b57cba | 2026-07-29 | add UI for managing serial/parallel connections | net/serial | PORT-LATER | Serial/parallel connection management UI (scc8530.cpp and gs2.cpp, both also modified by the fork). |
| 5d97516e | 2026-07-29 | notes | docs | SKIP | To-do list. |
| e0729c93 | 2026-07-29 | fix RAM sizing bug on GS (8MB mem exp + mobo RAM on rom01 128k or r... | mmu | PORTED | 3999d40e (motherboard + expansion fast-RAM sizing, iigs_memory.hpp). 18f5d04c later caps this at bank `$7F`. |
| d79be3c0 | 2026-07-31 | implement floating gc switch behaviors | joystick | DEFERRED | Floating game switches: makes PB2 float, so `$C063` reads pressed with no pad. Contradicts the fork's game-controller gate and has no hardware citation (f646760c, 49822387). |
| 0e606364 | 2026-07-31 | finished tweaking this #143 | joystick | DEFERRED | Follow-up to d79be3c0 (SW2 floats on every platform). Upstream was still iterating, so the value stays open (49822387). |
| cba9c847 | 2026-07-31 | code reuse refactor, hopefully makes it more readable | joystick | DEFERRED | 242-line readability refactor of gamecontroller.cpp with no behaviour change and real conflict risk (c7ae46e9). |
| 68910aff | 2026-07-31 | notes | docs | SKIP | To-do list. |
| 80e02e1d | 2026-07-31 | not used externally | joystick | PORT-LATER | Removes two unused prototypes from gamecontroller.hpp. |
| a359889a | 2026-07-31 | fix debug output (was wrongly hardcoded to gps[0]) | joystick | PORTED | 486fb60e (debug output used `gps[0]` for every pad). |
| cf946618 | 2026-07-31 | fix multi gamepad on startup bug | joystick | PORTED | 486fb60e (first pad opened when two are present; no `SDL_Quit` on second-pad failure). |
| 1f671467 | 2026-07-31 | code sharing for 6502/65816 trace | debugger/debugproto | PORT-LATER | Shared 6502/65816 trace formatting rewrite of trace.cpp. Divergence risk with the fork's trace tooling (trace.cpp/trace.hpp modified). |
| b5528bb8 | 2026-07-31 | color the trace text | debugger/debugproto | PORT-LATER | Coloured trace text in the debug window. |
| 423811db | 2026-07-31 | Add support for M/X width in disassembly and refactor related funct... | debugger/debugproto | PORT-LATER | Disassembly that respects M/X width. Touches disasm.cpp, trace.cpp and trace_opcodes.cpp, all also modified by the fork. |
| 0f2be42f | 2026-07-31 | notes | docs | SKIP | To-do list. |
| 5bd804a2 | 2026-07-31 | Enhance Joyport functionality and timing handling | joystick | PORTED | c7ae46e9 (post-reset Joyport suspend window scaled per platform). |
| 783e5f90 | 2026-08-01 | bump version for release | build/ci | SKIP | Upstream release version bump. |
| 7315428c | 2026-08-01 | if recent_configs empty, populate with configs in the distributed c... | ui/config/menus/osd/shaders | PORT-LATER | Fills recent configs from the distributed configs. |
| 7521af52 | 2026-08-01 | update documentation | docs | SKIP | Changelog and card docs. |
| 9ac68b20 | 2026-08-01 | fix windows build | build/ci | SKIP | libslirp Windows build fix (upstream Uthernet implementation). |
| 77d17efb | 2026-08-01 | fix menu in selectsystem on linux | ui/config/menus/osd/shaders | PORT-LATER | Linux menu in the system selector (gs2.cpp). |
| e2f6ab5a | 2026-08-04 | fix serious truncation/misread bugs related to .2mg files with miss... | disk/iwm/smartport/fst | PORT | The 5.25" nibblizer now seeks to `data_offset` before reading .nib data, so a nibblized .2mg reads correctly. The 3.5" `r+b` write half is already in the fork (d4c57c67). |
| 343082ce | 2026-08-04 | don't allow mounting 140K files on hard drive | disk/iwm/smartport/fst | PORT-LATER | BazFast (paravirtual) refuses non-512-byte and 140K images. |
| f1691706 | 2026-08-04 | sanity checking and improvement to 2mg header parsing | disk/iwm/smartport/fst | PORT | 2MG header parsing accepts images with zero `data_length` or `block_count` (DOS-order, nibblized, some creators) and derives block_count and data_size from the bytes actually present. The fork currently rejects these images (f4bca2ca, e17e046c). Merge with the fork's defensive data_offset validation. |
| 89202548 | 2026-08-04 | update to include installer, and marinetti/uthernet drivers | disk/iwm/smartport/fst | PORT-LATER | drivers.hdv asset update (Host FST/network drivers). |
| 46710e50 | 2026-08-04 | Merge branch 'main' of github.com:jawaidbazyar2/gssquared | other | MERGE | Mirror merge; no resolution content. |
| 5971314b | 2026-08-05 | notes | docs | SKIP | Notes. |
| 84b83b30 | 2026-08-05 | add debug commands to snag text display (linearized); get and set r... | debugger/debugproto | SKIP | Divergence: protocol text snapshot and register get/set (CTRL rail overlap). Adds display/text_page_layout.hpp. |
| 29dadbbb | 2026-08-05 | add ability to mount/unmount images from storage, in debugprotocol | debugger/debugproto | SKIP | Divergence: protocol mount/unmount (CTRL rail overlap). |
| 2b9622d2 | 2026-08-07 | comment discussing coldstart/warmstart reset logic | other | SKIP | Comment-only change in computer.cpp (992a5233 edits the lines next to it). |
| 18f5d04c | 2026-08-07 | fix linearization and misc shadow bugs based on [tester] test; add ... | mmu | PORT | IIgs MMU and video: `$C029` bit 6 or bit 7 linearizes `$E1:2000-9FFF` onto the interleaved physical layout (new iigs_aux_linear.hpp, `is_aux_linear()`); VideoScannerIIgs fetches SHR pixels, SCBs and palettes through that mapping; the HGR shadow is gated to even banks only; `SPEED_SHADOW_ALL` shadows unpopulated banks and floats reads; fast RAM is capped at 8MB (banks `$00-$7F`). This changes the `$E1` physical layout that about 44 fork direct-read sites and every SHR golden hash depend on, and it overrides the `$81`/`$8F` top bank from the fork's e0729c93 port. |
| cfc1c415 | 2026-08-07 | notes on persistent Zany Golf issue | docs | SKIP | Compatibility note. |
| b8eb1432 | 2026-08-09 | notes | docs | SKIP | README. |
| 35505bd6 | 2026-08-09 | mmu schema now takes cold start flag on reset() | mmu | PORT | `MMU::reset(bool cold_start)` across the MMU hierarchy; on a ROM 03 cold start, `$C036` powers on with bit 6 set (`0xC0`). |
| 992a5233 | 2026-08-09 | implement coldstart method for rom03 mmu | adb/mouse/kbd | PORT | ROM 03 ADB microcontroller: loads the ROM 03 micro firmware (the asset is already in the fork), ROM-base-aware uC memory read/write, `computer_t::reset` passes cold_start to the MMU. Upstream selects on `PLATFORM_APPLE_IIGS_ROM3`; the fork must use `is_rom03`. |
| 42fd845e | 2026-08-11 | fix incomplete handling of LC and some other stuff in bank E1 | mmu | PORT | Bank `$E1`: the `$FF` ROM overlays the LC window when LC read is off, LC write protect applies, LC bank 1 parks at `$E1:C000`, and `megaii_compose_map` recomposes the LC window on ALTZP change. Return-after-`$C0xx` write is already in the fork (e17e046c). Conflicts with the fork's bus/observer hooks in `bank_e1_write`. |
| 7de4cafc | 2026-08-12 | first implement issue 156; use apple 2-ified font from actual secon... | secondsight/voc | PORT-LATER | SecondSight: renders Apple II text through the card's VGA font (menu toggle). |
| bbbe1fa7 | 2026-08-13 | second version for issue #156: load fonts from compact 4K .bin file; | secondsight/voc | PORT-LATER | SecondSight font .bin assets. |
| 34022cf7 | 2026-08-13 | fix issue #157 - in native mode, 816 does not charge 1 cycle penalt... | cpu | HAVE | f4bca2ca (native-mode taken branch has no page-cross penalty). |
| 887d9439 | 2026-08-13 | fix for issue #145. | adb/mouse/kbd | PORT | ROM 03 key-repeat rate table (nibble 0 = slowest, 0-9) plus delay clamp. Depends on `uc_rom03` from 992a5233. |
| d981a267 | 2026-08-13 | fix for [tester]-reported issue of xce only taking 1 cycle when it ... | cpu | HAVE | 4bcab908 (XCE = 2 cycles with a phantom read). |
| 1713b7ea | 2026-08-14 | PR oplicy | docs | SKIP | PR template and CONTRIBUTING. |
| d8c5b696 | 2026-08-15 | implement win64 host fst | disk/iwm/smartport/fst | PORT-LATER | Win64 Host FST backend. |
| 0892702a | 2026-08-15 | make File->Quit work on windows | ui/config/menus/osd/shaders | PORT-LATER | File->Quit on Windows. |
| 25ee9685 | 2026-08-15 | tweaks to windows path handling in SystemSettings | ui/config/menus/osd/shaders | PORT-LATER | Windows path handling in SystemSettings (plus AGENTS.md). |
| 52dcd30f | 2026-08-09 | all bank F0-FF should be fast ROM but these are really not the plac... | mmu | PORT | All of banks `$F0-$FF` are fast ROM-space cycles (was `>= $FC0000`). |
| 9943e1ec | 2026-08-16 | Merge branch 'main' of github.com:jawaidbazyar2/gssquared | other | MERGE | Mirror merge; no resolution content. |
| 5496a40d | 2026-08-16 | notes | docs | SKIP | Notes. |
| 6bbc98f8 | 2026-08-16 | pulling in stuff from lappy | other | MERGE | Merge between upstream work machines; no resolution content. |
| 03c52c82 | 2026-08-16 | fix #159, using temporary hack. | video | PORT | Frame-based (ludicrous/step) path advances the video scanner in `frame_video_update`, so VBL, scanline and QTR IRQs still fire when another display processor owns the frame. Superseded in part by e169b2bc; take both. |
| e169b2bc | 2026-08-16 | refactor ludicrous speed to be fixed multiples of 14M clock. This r... | clock | PORT | Ludicrous speed becomes N CPU cycles per 14M tick with the scanner kept hooked, so VBL/IRQs, speaker and Ensoniq stay live; calibration state moves to computer.cpp/.hpp; run_one_frame in gs2.cpp is rewritten (about 268 lines). Behaviour changes only in free-run mode. Heavy conflict with the fork's gs2.cpp and NClock.hpp. |
| 1be8d2e8 | 2026-08-16 | stop emitting scan_buffer even if we just accidentally hit wrong key. | video | PORT-LATER | Stops F8 writing scan_buffer.txt (VideoScanGenerator*.cpp, also modified by the fork). |
| 641bc4cf | 2026-08-16 | Merge branch 'lsrefactor' | other | MERGE | Merges lsrefactor; no resolution content. |
| 77b27299 | 2026-08-17 | notes | docs | SKIP | Notes. |
| ce56bea5 | 2026-08-17 | Merge branch 'main' of github.com:jawaidbazyar2/gssquared | other | MERGE | Mirror merge; no resolution content. |
| 8ff08718 | 2026-08-17 | notes | docs | SKIP | Notes. |
| 158c75d7 | 2026-08-17 | Merge branch 'main' of [upstream mirror] | other | MERGE | Mirror merge; no resolution content. |
| 464890f4 | 2026-08-17 | notes | docs | SKIP | 65816 notes. |
| 62a2b70b | 2026-08-17 | notes | docs | SKIP | Release notes. |
| fffa140a | 2026-08-17 | implement CRT Shader on Windows | ui/config/menus/osd/shaders | PORT-LATER | CRT shader on Windows (touches apps/vgatext, also modified by the fork). |
| 6d3834b3 | 2026-08-17 | Don't automatically clear buffer - some callers want to retain. | ui/config/menus/osd/shaders | PORT-LATER | TextInput keeps its buffer. |
| 7738b646 | 2026-08-17 | add concept to reverse "0" point of scrollbar | ui/config/menus/osd/shaders | PORT-LATER | ScrollBar reversed origin. |
| b87142f3 | 2026-08-18 | a missing initializer; better sharing of GS color tables | video | PORT | Initializes `color_mode` in all three VideoScanGenerators (it was uninitialized, a first-frame determinism hazard); shares the GS text colour table. |
| b76b2729 | 2026-08-18 | a few tweaks but no longer used anywhere | video | PORT-LATER | Tweaks to GSRGB560.hpp, which is no longer used by any renderer; no runtime effect. |
| c914232a | 2026-08-18 | finish implementation of #77; big refactor of AppleII.cpp for fast ... | debugger/debugproto | PORT-LATER | DebugVideoView plus a static-page AppleII.cpp renderer. The static renderer does not replace the live scanner. |
| 3a43616a | 2026-08-18 | notes | docs | SKIP | Notes (mapping, VOC spec, reference HTML). |
| 64adfa41 | 2026-08-18 | notes | docs | SKIP | Compatibility notes. |
| aed56cfb | 2026-08-18 | Merge branch 'main' of github.com:jawaidbazyar2/gssquared | other | MERGE | Mirror merge; no resolution content. |
| 881ccb3a | 2026-08-19 | add debugger docs | docs | SKIP | Debugger docs. |
| 8fbca782 | 2026-08-19 | fix WAI implementation; fix PLB cycle count (was 2, supposed to be 4) | cpu | HAVE | e17e046c (RDY wakes on a pending IRQ) and 4bcab908 (PLB = 4 cycles). Not needed: upstream's WAI-opcode change (skip setting RDY if an IRQ is already asserted), because the fork's RDY test clears RDY at the next boundary with the same cycle count. |
| 4bd3d0b0 | 2026-08-19 | notes | docs | SKIP | Development log. |
| 925ff2d7 | 2026-08-19 | uhh | other | MERGE | Mirror merge (subject "uhh"); no resolution content. |
| 5963009d | 2026-08-19 | support APM partition tables on BazFast volumes - primarily for mou... | disk/iwm/smartport/fst | PORT-LATER | APM partition tables and read-only .iso CD-ROM on BazFast (pdblock2/3, media.*, mount.cpp, all also modified by the fork). |
| 7333660a | 2026-08-19 | fix #150 and #101; implement "fast" FPI register cycles. | mmu | PORT | FPI register accesses (`$C035`, `$C036`, `$C068`, `$C02D`, the new `$C037` DMA bank register, and the LC ROM overlay read) become fast cycles instead of sync/slow. |
| b0664095 | 2026-08-19 | don't allow mounting same image multiple times | disk/iwm/smartport/fst | PORT-LATER | BazFast refuses to mount the same host file twice. |
| da0a3120 | 2026-08-19 | notes | docs | SKIP | To-do list. |
| a029c3fc | 2026-08-20 | notes | docs | SKIP | Changelog and roadmap. |
| bf8471d4 | 2026-08-21 | Complete #167. Implement PASTE support for IIgs ADB. Add support fo... | adb/mouse/kbd | PORT-LATER | Host clipboard paste into the ADB key buffer, plus protocol PASTE_TEXT. Also adds a per-frame `keygloo_update_interrupt_status`; check it against the fork's key-injection path. |
| 0df48213 | 2026-08-21 | Support debug protocol on Windows | debugger/debugproto | SKIP | Divergence: DebugProtocolServer on Windows (CTRL rail overlap). Author Henri Asseily. |
| 344abf22 | 2026-08-21 | Merge pull request #168 from [contributor]/fix/windows-debug-protocol | other | MERGE | Merges PR #168 (0df48213); no resolution content. |
| 1f95ff78 | 2026-08-22 | fix #170 mouse tracking bug introduced when we added linearized shr | adb/mouse/kbd | PORT | Mouse sync reads line-0 SCB through the linearized mapping. Needs `iigs_aux_linear.hpp` and `is_aux_linear()` from 18f5d04c. |
| 22278e5b | 2026-08-22 | Merge branch 'main' of github.com:jawaidbazyar2/gssquared | other | MERGE | Mirror merge; no resolution content. |
| d7173f7e | 2026-08-22 | fix #169: jump monitor pane to newest output after a command | debugger/debugproto | PORT-LATER | Monitor pane follows the newest output. |
| bd182ac7 | 2026-08-22 | clean up fix for #170 to reduce cross-dependency between modules | adb/mouse/kbd | PORT | Adds the `MMU::is_aux_linear()` virtual (mmu.hpp, mmu_iigs.hpp) so the mouse sync needs no RTTI. |
| 3258974a | 2026-08-22 | complete #165; implement print-to-clipboard serial port device. | net/serial | PORT-LATER | Print-to-clipboard serial device. |
| 8fa41f53 | 2026-08-22 | Closes #165: changelog and todo for print-to-clipboard. | docs | SKIP | Changelog. |
| fb52cc70 | 2026-08-24 | implement real HostSerial serial port connectivity | net/serial | PORT-LATER | HostSerial (real host serial ports). Z85C30.hpp, also modified by the fork, pushes line parameters to the backend; plus the 6551. |
| 0473dd52 | 2026-08-24 | notes | docs | SKIP | Notes. |
| e75e005c | 2026-08-24 | implment Win32 Host Serial Support | net/serial | PORT-LATER | Win32 HostSerial. |
| f6f9005d | 2026-08-25 | implement HostSerial for linux | net/serial | PORT-LATER | Linux HostSerial. |
| 5b280b4a | 2026-08-25 | fix some issues with the linux hostserial - handling of carrier det... | net/serial | PORT-LATER | Linux HostSerial carrier-detect and open-order fixes. |
| 84d92a52 | 2026-08-25 | implement CRT shader on Linux. | ui/config/menus/osd/shaders | PORT-LATER | CRT shader on Linux. |
| c64c77a6 | 2026-08-25 | fix type mismatch on this after recent changes | build/ci | PORT-LATER | apps/diskid type fix (goes with 5963009d). |
| b7c41d5e | 2026-08-25 | Merge branch 'main' of github.com:jawaidbazyar2/gssquared | other | MERGE | Mirror merge; no resolution content. |
| 641121c1 | 2026-08-25 | tweaks to more cleanly get mmu/megaii state DebugVideoView needs. | debugger/debugproto | PORT-LATER | MMU accessors for DebugVideoView (mmu.hpp, mmu_iigs.hpp, both also modified by the fork). |
| 2458e131 | 2026-08-25 | build orchestration | build/ci | SKIP | Release orchestration and upstream version bump. |
| cd1b2481 | 2026-08-25 | documentation, changelog, and readme updates, and notes. | docs | SKIP | Docs, changelog and release scripts (upstream-only tooling). |
| 3cb35c7f | 2026-08-25 | remove duplication | docs | SKIP | README. |
| c9883d94 | 2026-08-26 | fix a few spots where setting X to 1 did not clear hi bytes of XY | cpu | HAVE | f4bca2ca (PLP and RTI clear the X/Y high bytes when the pulled P has X=1). |
| 6fe61065 | 2026-08-26 | fix #172 there were cases where attempt to buffer more data than bu... | net/serial | SKIP | Divergence: fixes a buffer-overrun data drop in upstream's slirp w5100_chip. Separately check whether the fork's w5100.cpp has the same defect. |
| da900dcd | 2026-08-26 | Initial implementation of VOC 640x40 mode | secondsight/voc | PORT-LATER | Video Overlay Card 640x400 mode (new device; appends `DEVICE_ID_VOC`; AppleII.cpp static renderer). |
| 3bd73d05 | 2026-08-26 | Merge branch 'voc' | other | MERGE | Merges voc. Has conflict-resolution hunks. |
| 23460916 | 2026-08-26 | initial commit SecondSight GPU mode | secondsight/voc | PORT-LATER | SecondSight GPU mode, an emulator-defined extension rather than card hardware, built on the upstream secondsight.hpp that still has PPU mode. |
| 5a215ad4 | 2026-08-26 | exit to gsos the right way | secondsight/voc | PORT-LATER | GPU-mode test program (testdev). |
| 88c9a8e8 | 2026-08-27 | initial spec and implementation of secondsight host text mode (memo... | secondsight/voc | PORT-LATER | SecondSight host text mode (emulator-defined). Deletes the unbuilt secondsight1.hpp, which the fork only annotated. |
| 6df4af4e | 2026-09-07 | implement hosttext 80x43 mode. | secondsight/voc | PORT-LATER | Host text 80x43 mode. |
| d41659aa | 2026-09-08 | notes | docs | SKIP | Development log. |
| a65700ea | 2026-09-08 | implement support for two-joystick joyport | joystick | PORT-LATER | Two-joystick Joyport select. Built on the cba9c847 helpers (DEFERRED), so it needs that refactor or a re-derivation. |
| ee5f6180 | 2026-09-08 | appledouble notes | docs | SKIP | Host FST AppleDouble notes. |
| f57c5df6 | 2026-09-08 | notes - handling of 65816 behavior switching to 8-bit X/Y registers. | docs | SKIP | Notes on 65816 8-bit X/Y behaviour. |
| 61846835 | 2026-09-08 | this was fixed by correctly handling "zero out high byte of x/y reg... | docs | SKIP | Compatibility note. |
| 533d8763 | 2026-09-08 | Fixed #180: gate GS/OS mouse clicks to the guest content area. | adb/mouse/kbd | PORT | Mouse sync gates mouse button events to the guest content area while Event Manager tracking is on, and still delivers the release for a press that was accepted inside. |
| bd4ca81e | 2026-09-08 | Complete ThunderClock Plus: uPD1990 protocol, IRQ/TP, and firmware ... | clock | PORT | ThunderClock Plus rework: uPD1990 serial protocol (hold/shift/set/read), TP 64/256/2048/4096 Hz and 1 Hz timers, IRQ status. The emulated card reads host time; route it through the fork's deterministic RTC time source. |
| 7d23dae0 | 2026-09-10 | tweaks to the SS GPU Text mode: temporary enable/disable VGA mode. | secondsight/voc | PORT-LATER | SecondSight GPU text mode plus the ssgputexttest app. |
| 7092605f | 2026-09-10 | render SS video modes with a border so they fit in same area as app... | secondsight/voc | PORT-LATER | Border around SecondSight modes (videosystem.cpp/hpp, also modified by the fork). |
| aa6fa477 | 2026-09-11 | move file | docs | SKIP | Moves XY.md. |
| d4db2e50 | 2026-09-13 | implement version, and getcapabilities | secondsight/voc | PORT-LATER | SecondSight version and getcapabilities commands. |
| d380e055 | 2026-09-14 | also gate mouse-tracking on shr active | adb/mouse/kbd | PORT | Mouse sync is active only while SHR is on. It reads `$C029` through `mmu->read()`, a bus read that fork observers and I/O logs can see; prefer a direct NEWVIDEO accessor. |

## 7. Port of `c4b1fa69..29dadbbb` (stage 1, 2026-09-16)

Resolved component by component. "Upstream" or "fork" below names where each part of the result
came from; items marked OPEN are recorded to be re-decided on hardware evidence.

| Component | Result |
|---|---|
| Device IDs | Upstream's order (`HOST_FST`, `UTHERNET2`, `SUPER_SERIAL`), fork's `A2GSPU` last; `device_info.cpp` gains the `A2GSPU` row. The Apple Mouse card is also allowed on the IIgs, for the fork's `-c 15` machine. |
| Built-in machines | The fork's `-c N` indices are kept, including the five configs upstream moved to `.gs2` files; each has a machine UUID, reusing the `.gs2` counterpart's where one exists. Upstream's device maps are taken for configs 0-3 and 8 (the IIe Enhanced now has a clock in slot 1, the mouse card in 5 and the block device in 7). Upstream's ROM 03 platform machine is appended as `-c 16` (`-p 6`); the fork's ROM 03 personality is renamed "Apple IIgs (ROM 3 Tenspeed)". |
| ROM generation | The fork's size-derived `is_rom03` stays authoritative; `MMU_IIgs::rom03()` is the accessor, and upstream's mouse sync uses it instead of the platform ID. |
| IWM | Upstream `3d4efff1` drive-enable model, empty-drive sense and LSS clocking (previously DEFERRED) are taken. The fork snapshot now saves the derived /ENBL level and the holdover time left, and resyncs the drives on restore (block version 2). |
| Video scanner | Upstream's soft-switch latency queues (`0d95d93c`, `829bd5ea`, previously DEFERRED) are taken. The fork snapshot saves the switches as they will be once the queue drains. |
| Game port | Upstream's structure is taken, plus the fork's injection-first readers. **OPEN:** the fork keeps "absent pad reads not pressed"; upstream floats SW0/SW1 on the II/II+ and SW2 everywhere. |
| ADB | Upstream's closed-loop mouse sync (injection only) and its KeyGloo changes are taken, with the fork's key interceptor, taps, unemulated-command reports, SRQ and transmit commands, and snapshot. The Event Manager probe reads through `probe_peek`, not the CPU bus. |
| Apple Mouse card | Upstream's PIA + 6805-protocol card replaces the fork's high-level card. The fork's file-injection rail moves onto it (`applemouseiii_inject`), and the card registers `MODULE_MOUSECARD`. Snapshot support is owed. |
| Ensoniq / GLU | Upstream's DOC register, IRQ and pipeline fixes are taken, along with its host staging and resampler. The fork's audio probe observes at emulated time, before host staging. The snapshot adds the pipeline's latched address (block version 2). |
| Uthernet II | Upstream's slirp card is taken for this stage. It will be replaced by a fused card (upstream register model and slirp, plus the fork's interrupt, state-machine, listen and reporting behaviour, plus new fixes and instrumentation) once stage 2 lands. |
| Trace / disassembler | Upstream's shared trace formatter and M/X-aware disassembler are taken. The fork adds the `A2GSPU_TRACE_EXT` columns (after the label column), side-effect-free disassembly reads, `<unreadable>` lines, and the conservative XCE width reset. |
| 2MG | Upstream's parsing is taken, which accepts real images with zeroed length fields. The fork's bounds-checked comment and creator blobs stay. |
| Parallel card | Upstream's connection-based file device is taken. The fork adds a flush after each pass that wrote. |
| Host preferences | A headless or rail-driven run (`A2GSPU_SPIKE_FRAMES`, `A2GSPU_CTRL`) neither loads nor saves `system_settings.toml`, and seeds no `.gs2` files: a preference can change guest-visible input (the gamepad-disconnect setting). |
| Windows | Automated launches (`A2GSPU_CTRL`, `A2GSPU_BACKGROUND_WINDOW`, `A2GSPU_SPIKE_FRAMES`, `-n`) create a window that cannot take focus and is never raised. |
| Upstream tests | `systemconfigtest` failed on upstream's own tree. Two tests were corrected to upstream's current rules (the `aliases` and `parallel_output` tests; new fixture `aliases_smartport.gs2`). |
| Docs | The profile-pack documentation is carried without naming third parties. |

**Baseline shift, attributed** (`tools/a2rail/unarmed.baseline.json`; SHR hashes unchanged):

- `iigs-rom`: cycles 7,892,937 to 6,043,351; `iigs-gsos601`: cycles 10,635,389 to 10,635,192. Both runs first diverge at the ROM's IWM status poll (`FF/65DE`, `$C0EE`): bit 7 reads 1 for an empty drive under upstream `3d4efff1`. Found with `tools/gdiff/itracediff.py` against the pre-port build.
- `iie-rom`: run witness only. The ROM's slot scan reads `$C705` = `$03` (the block device now in slot 7) instead of `$38` (the mouse card), from upstream's IIe Enhanced device map.

## 8. Port of `29dadbbb..d380e055` (stage 2, 2026-09-16)

Merged against an explicit base of `29dadbbb`, straight into `a2gspu`. The next sync starts from `d380e055`.

| Component | Result |
|---|---|
| CPU | The fork's implementations of `34022cf7` (no native-mode page-cross penalty), `8fbca782` (WAI wake, PLB = 4 cycles) and `c9883d94` (PLP/RTI clear XH/YH) stay; upstream's equivalents merged to the same cycle counts. Upstream `d981a267` merged cleanly on top of the fork's own XCE fix and made XCE 3 cycles. The duplicate phantom read is removed, and upstream's `cycletest816` XCE record now guards it. |
| `$E1` layout (`18f5d04c`, `42fd845e`) | Taken: with the aux window linearized (NEWVIDEO bit 6, or SHR on) the Mega II store keeps `$E1:$2000-$9FFF` interleaved, and upstream's own readers (scanner, mouse sync, debug view) translate. Fork additions: bus-trace and observer records keep the LOGICAL address, and `MMU_IIgs::megaii_cpu_index/peek/poke/view` give every fork observer CPU-order bytes. That covers the probes, the CTRL video verbs and asserts, the spike's `$E1` dumps, hash and golden, `A2GSPU_DUMP`, the A2GSPU card's SHR reads and the KeyGloo 640-mode test. The observatory's SCB and palette windows pointed at fixed store offsets; they become one `vgc.shr_store` window over the physical store. Snapshots keep the physical store. |
| RAM size | Upstream's 23-bit FPI decode is taken: fast RAM ends at bank `$7F` on ROM 01 and ROM 03, replacing the `$81`/`$8F` top bank from `e0729c93`. |
| FPI registers (`7333660a`) | Taken: `$C035`/`$C036`/`$C037`/`$C068`/`$C02D` are fast cycles, and `$C037` is the DMA bank register. |
| Reset (`35505bd6`, `992a5233`) | `reset(cold_start)` across the MMU hierarchy. The ADB microcontroller firmware (ROM 01 or ROM 03) is chosen from `MMU_IIgs::is_rom03_machine()`, the image size, not the platform ID, so the fork's ROM 03 and ROM 04 personalities get the ROM 03 micro. |
| Run loop (`e169b2bc`) | Upstream's ludicrous-speed calibration is taken, but not run under `A2GSPU_PACE=fast`: it measures host time. The calibration probe loops stop on a rail halt like the frame loops. |
| SecondSight | Upstream's card is taken whole: PPU, GPU, host text, GPU text, the VGA fence, and version/capabilities. That also replaces the fork's older forward-ported copy, which had dropped PPU mode. `secondsight1.hpp` is deleted as upstream did. |
| Video Overlay Card, ThunderClock Plus, two-joystick Joyport, host serial and clipboard devices | Taken. `DEVICE_ID_VOC` sits before the fork's `DEVICE_ID_A2GSPU`. |
| Scanner | Upstream's `is_80store()` accessor replaces the fork's duplicate. |

**Owed after this port** (the per-component study of real-hardware behaviour, 2026-09-16, is the input):
- **Fused Uthernet II:** done, see section 9.
- **Snapshot blocks** for the new cards (Apple Mouse, SecondSight, VOC, Super Serial; Uthernet II and ThunderClock are done). ThunderClock time through `A2GSPU_FAKETIME` is done (section 10).
- **Side-effect-free reads** for the debug protocol's `READMEM`/`FINDMEM`, which today read through the CPU's bus path.
- **Hardware-evidence decisions still open:**
  - game switches with nothing attached (the IIe manual's pull-downs are in the hand controller, which favours upstream's floating reads);
  - `$C011-$C01F` low bits and the `$C02E`/`$C02F` scan-line clear (the fork's fixes 5 and 6 contradict MAME);
  - MVN/MVP stepping with x=1 (the documentation favours upstream's 8-bit stepping with the fork's explicit banks).

**Baseline shift, attributed** (`tools/a2rail/unarmed.baseline.json`; `iie-rom` unchanged):
- `iigs-rom`: cycles 6,043,351 to 6,045,727; SHR hash unchanged.
- `iigs-gsos601`: cycles 10,635,192 to 10,647,366; the frame-300 SHR hash changes (`5F78F7C15131F43F` to `D29F204D690D417F`).
- Both runs first diverge in frame 0 right after `STA $C036`: the next `$C015` read samples the floating bus one beam position earlier because `$C036` is now a fast cycle (`7333660a`). With that one cycle type reverted, frame 0 matches the pre-port build.
- The GS/OS boot converges: at frame 900 both builds show the identical desktop (`1691807AD9B30BAA`), which also confirms the CPU-order SHR hash reads the same picture through the new store layout.

## 9. Fused Uthernet II (after stage 2, 2026-09-16)

Neither side's card was taken whole. Upstream's register model (adapted from AppleWin), its libslirp
network and its host-socket worker are the base. The fork's earlier card supplied the behaviour a
guest written to the W5100 datasheet depends on. The rest is new.

| Part | Result |
|---|---|
| Register model, buffers, auto-increment, MACRAW/IPRAW framing, slirp | Upstream. |
| `Sn_IR` / `IR` | Fork. Every interrupt source is raised: CON, DISCON, RECV, TIMEOUT, SEND_OK. `Sn_IR` is write-1-to-clear. `IR` bits 3-0 are derived from "`Sn_IR` != 0" on every read, so clearing a socket clears the mirror; bits 7-5 are write-1-to-clear. Upstream raised no interrupt at all. |
| Command states | Fork. LISTEN and CONNECT only from a TCP `SOCK_INIT`; DISCON only on a connection; SEND only on a socket that can send; SEND_KEEP only on `ESTABLISHED`. Anything else is ignored, as the chip ignores it, and recorded as `ignored-state`. Upstream acted on every command in every state. |
| Remote close | Fork. The peer's FIN gives `CLOSE_WAIT` with DISCON, and the host socket is kept until the guest closes, so data can still be sent. Upstream went straight to `CLOSED`. A reset or receive error gives `CLOSED` with DISCON (upstream left the socket `ESTABLISHED` on a receive error); a refused connect or send gives TIMEOUT. |
| LISTEN | Fork. Bound on the emulation thread, synchronously, with an exclusive address on Windows. A failure gives `CLOSED` with TIMEOUT in the same register write and is reported with the port and the reason. Upstream set `SOCK_LISTEN` whether or not the bind worked. |
| MACRAW | Fork. Socket 0 only (datasheet); an OPEN on another socket stays `CLOSED` and is reported once. |
| Unemulated behaviour | Fork. SEND_MAC, PPPoE (`Sn_MR` $05 and `MR` bit 3) and unknown command codes are reported once on stderr as NOT EMULATED, because `Sn_CR` self-clears either way and the guest cannot tell. |
| Emulated time | New. Host network events enter the machine only once per video frame, from the frame dispatcher. Upstream applied them inside every card register access (the fork's earlier card did so only on `Sn_SR` and `Sn_RX_RSR` reads), so host timing changed the machine at whichever instruction touched the card next. A guest now sees host results at most one frame late, whatever register it polls. |
| Stale events | New. Every OPEN, CLOSE, DISCON and reset starts a new socket generation, and requests and events carry it. A late "connected" or "closed" for a socket the guest has since replaced is discarded and counted. |
| Sending | New. A short host `send` keeps the rest queued in the worker instead of dropping it, and SEND_OK is raised when everything has reached the host stack (TIMEOUT if the host refused it). UDP sends report the same way. |
| Receiving | New. A UDP datagram stays in the host stack while the event ring is full; a datagram or frame larger than the free buffer is dropped and counted. A UDP bind failure is reported. |
| Robustness | New. `RMSR`/`TMSR` values that exhaust the 8K pool give later sockets a zero-size buffer; every ring operation guards that case (upstream divided by zero). Read-only registers ignore writes. A zero `Sn_CR` write dispatches nothing. |
| Instrumentation | New. `A2GSPU_U2_EVENTS` records (`u2.cmd`, `u2.state`, `u2.drop`) and counters through `rail read`; `devstate uthernet2` (MR, address latch, IR, addresses, the slirp network the run was given, host byte and ring counters, every socket's rings and generation); the slirp network printed once at startup; `W5100Chip::peek()` reads any register with no side effect. |
| Snapshot | New. Block `u2` holds the 32K chip memory, mode, address latch and each socket's rings, pending data, status and generation. A connection cannot be saved: a socket that was connecting or connected comes back `CLOSED` with DISCON raised and the restore warns with the count. A listening socket is bound again, and an INIT or UDP socket is reopened. |

**OPEN:** `MR` bit 0 (IND). The fork held it set as hardwired on the Uthernet II; upstream clears `MR` on
reset. It stays set until card evidence settles it, and a2engine's `netcheck` asserts it.

**Known limits:** SEND_MAC and PPPoE are not emulated. SEND_KEEP emits no packet, so a NAT binding is not
refreshed. Retransmission timing (`RTR`/`RCR`) belongs to the host stack. Whether the card drives the
slot's interrupt line is not modelled.

**Verified:** a2engine `netcheck` (8), `listencheck` (11) and `multicheck` (10) all pass. A scratch run of
a2engine's unharnessed probes also passed 21 checks: a refused connect gives TIMEOUT through `IR` alone,
connect, SEND and RECV round-trip against a local echo peer, SEND_KEEP gives SEND_OK, SEND_MAC is
reported, records are written, and a snapshot taken mid-connection restores with the socket `CLOSED`,
DISCON raised and the loss reported.

## 10. Clocks under the time pin (2026-09-16)

`A2GSPU_FAKETIME` reached only the IIgs RTC. Two cards and one interrupt still read the host clock,
so a pinned run did not repeat if a clock card was installed or the one-second interrupt was enabled.

| Part | Result |
|---|---|
| ProDOS clock card | The GETLN line comes from the pinned instant's calendar fields (`a2gspu_faketime.hpp`). The helper applies no host timezone, the same as the RTC. |
| ThunderClock Plus | Power-on and TIME_READ use the pinned fields, frozen until the guest's TIME_SET, then counted in emulated seconds. The 1 Hz count and the TP output were rescheduled from the handler's dispatch time; they now follow their own due times, so the counter keeps real time. Snapshot block `tclock` holds the registers, the 40-bit shift register and counter, the TP rate, and the distance to the next 1 Hz and TP edges. `devstate thunderclock` was already upstream's. |
| VGC one-second interrupt | The first edge's phase came from the host clock even in a pinned run. Under the pin it is now exactly one second after power-on; without it, the host-synced phase stays, which matches the RTC's rollover. Each edge follows the last by exactly one second rather than one second after dispatch. The `vgcint` block is version 2 and saves the distance to the next edge; version 1 files still load. `devstate display` shows the next edge. |

**Verified:** a scratch run (10 checks) under the pin. The ThunderClock counter and TIME_READ read
`5428202640`, a `tclock` block round-trips, and the ProDOS clock card's GETLN line reads
`05,04,28,20,26`. Two IIgs sessions put the next one-second edge at the same 14M count, a whole
number of seconds after power-on, and a restore keeps the ticks to the edge.
