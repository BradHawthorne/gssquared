# Storage: the ideal architecture (fork `ed2c00fc` vs `upstream/main`)

**Evidence key:** **[IWM-spec]** = [datasheet: *IWM Device Specification* rev 19, 1982, archive.org OCR, page]; **[Parker]** = [doc: Neil Parker, *Controlling the 3.5 Drive Hardware on the IIGS*, `Docs/NeilA235Floppy.md`]; **[WOZ]** = [doc: WOZ 2.1 reference]; **[2IMG]** = [doc: 2MG format, GS WorldView 2003]; **[MAME]** = [emulator: MAME `iwm.cpp`, `floppy.cpp` mac_floppy, `apple2gs.cpp`]; **[rec]** = [recollection — unverified]. The ROM 3 schematic PDF is image-only and was not read (rendering needs image files). Citations are `ed2c00fc` unless marked `up:`.

---

## 1. IWM chip

**1. Hardware**
- **State bits:** 8 bits addressed by A1–A3, value on A0, all cleared by /RESET; mode register resets to 0 [IWM-spec p.7–8].
- **Register select** (L7, L6, Motor-On): 000 reads **all ones**; 001 data; 01x status; 10x handshake; 110 mode write; 111 data write. Reads only at even addresses (A0 is /read), so odd reads leave the bus floating; writes only at odd addresses. A switch change selects its register in the same access [p.7].
- **Mode:** b0 latch, b1 async, b2 timer disable, b3 fast (2 µs), b4 8 MHz, b5 test, b6 MZ-reset [p.8].
- **Status:** b0–4 = mode; b5 = an /ENBL low (holdover included); b6 = MZ, reads 0; b7 = SENSE [p.9]. SENSE has a 10 kΩ pull-up, so it reads 1 when undriven [p.10]; MAME returns 1 with no drive selected [MAME].
- **Handshake:** b0–5 read ones; b6 = 1 in the write state, cleared by underrun; b7 = buffer ready [p.9]. MAME resets it to `$BF` [MAME].
- **Holdover:** with the timer on, /ENBL stays low **2^23+100 FCLK (≈1.17 s)**; at most one /ENBL low [p.6, p.8].
- **Read:** bit windows anchored to the last flux transition (28 FCLK slow, 14 fast). Sync mode: shift register visible mid-byte, stalls 2 cells + 4 CLK once the MSB sets. Async mode: data latched on MSB, **cleared 14 FCLK after a valid read** [p.3–4].
- **Async write:** buffer feeds the shift register every 8 cells. Underrun forces /WRREQ high and /underrun=0 until L7 clears [p.2–3].

**2. Fork and upstream**
- **Both right:** decode before register select; mode writable only while /ENBL (holdover included) is off (`IWM2.hpp:590`); fork's `async_buffer_valid` guard (`:394-404`).
- **Data register:** returned whatever Motor-On is (`:562-569`, `up:585-598`); hardware reads all ones. Odd reads return 0 (`:571`, `up:598`); hardware floats.
- **Handshake b0–5:** `reg_handshake` has no initializer (`:96-103`) and reset sets only b6/b7 (`:222`), so they are indeterminate. Hardware reads ones.
- **Holdover:** exactly 1 s of 14M (`:152`, `up:221`), not 1.17 s.
- **Fork /ENBL is a latch** that drops only in the frame handler (`:232-240`, `iwm_device.cpp:88-99`). Step mode dispatches that once per host frame (`gs2.cpp:360`), so status b5 and the state machine depend on how the machine was stepped. This breaks (b).
  - SELECT during the holdover uses the raw switch (`:523-530`); the holdover does not move to the other drive.
  - Upstream derives /ENBL from the clock at each use and routes it to one drive (`up:191-215`). **Right shape.**
- **L/H modes:** fork gates both on `$C031` EN35 (`:350-351`), which the chip cannot see. Upstream ungates (`up:410-411`) but fills the async buffer only with EN35 set (`up:625`). Both clear data *on* read, keyed on L (`:566-568`); spec says 14 FCLK later, keyed on H (MAME agrees).
- **Bit cell** comes from the drive family (`Floppy525_woz.hpp:54-58`, `Floppy35_woz.hpp:105-109`), not mode b3/b4, so reads in the wrong mode succeed.
- **Underrun:** writing re-arms if a byte arrives afterwards (`:394-407`); spec keeps /WRREQ off until L7 clears.
- **WP shift:** the IWM copies the Disk II WP shift into its data register (`:380-390`).
- **Fork restore bug:** the holdover deadline is restored in CPU cycles but compared in 14M (`:814`), so it expires at once.

**3. Neither models:** all-ones and floating reads, handshake ones, 1.17 s holdover, delayed async clear, mode-driven bit cell, flux-window data separator, underrun latch. Whether Ctrl-Reset reaches IWM /RESET needs the schematic.

**4. Ideal shape**
- **(a) Pure chip model.** State latch, mode, handshake, shift/data/buffer registers, sequencer phase, and a holdover deadline in FCLK ticks. Inputs: SENSE and RDDATA transition times. Outputs: PHASE0-3, /ENBL1/2, /WRREQ, WRDATA. Drives sit on a cable; the IIgs glue (`$C031`, CYAREG) lives outside.
- **(b) Clocking:** FCLK ticks derived from c14m only; deadlines checked at every access.
- **(c) Instrumentation:** side-effect-free `peek()`; rails for mode writes, /ENBL edges, write-state begin/end, underrun; snapshot every field with remaining-tick deadlines.

---

## 2. Disk II controller and 5.25 drive

**1. Hardware**
- **Latch and switches:** even addresses return the data latch; the 1 s holdover is required; reading `$C08D,X` resets the sequencer and clears the latch, which E7 protections use [WOZ]. Switches toggle on any access, and the P6 sequencer steps at 2 MHz (8 steps per cell) [rec: Sather *UTAIIe* ch.9].
- **MC3470 recipe:** 4-bit window, output bit 1, random bit when the window is 0; 32-byte table ~30% ones, also used for empty tracks; empty tracks 51,200 bits; relative angle kept across track changes [WOZ].
- **Sense:** undriven SENSE reads 1 [IWM-spec p.10; MAME]. Upstream says WP "returns through PHASE1" [emulator comment, unverified].
- **Unverified:** stepper moving only while enabled; drive-side write inhibit [rec].

**2. Fork and upstream (shared drive code)**
- **Phase writes do nothing:** write handlers exist only for `$C0n8-F` (`ndiskii_woz.cpp:79`), and the MMU has no fallback (`mmu_ii.cpp:269-275`).
- **READLOAD:** shifts WP once per cell (`diskii_controller.hpp:244-252`); no clear on `$C08D`, so E7 checks diverge.
- **Fake bits:** fixed 64-bit rotating pattern, ~59% ones (`Floppy_woz.hpp:87`, `.cpp:266-270`). Deterministic, but wrong period and density.
- **Lag snap (32 bits)** also applies in write mode (`Floppy_woz.cpp:201,231-237`); skipped bits are never written, so old data survives.
- **Stepper settle:** event at CPU cycles +520 (`Floppy525_woz.cpp:86`).
  - The Disk II passes the CPU event timer (`ndiskii_woz.cpp:87`); the IWM passes the 14M timer (`iwm_device.cpp:47`), which fires at once. Two stepper models.
  - The past-event check uses `get_cycles` (`EventTimer.cpp:23`); pending events are not snapshotted.
- **Stepping ignores /ENBL** (`IWM2.hpp:515`, `diskii_controller.hpp:285`); head clamped to the image's TMAP span (`Floppy525_woz.cpp:49-58`).
- **Rotation in CPU cycles** (`Floppy_woz.hpp:93`): a IIgs 5.25 disk spins 2.8× fast. This masks the missing CYAREG motor-on slowdown (`mmu_iigs.hpp:193-197` handles b7 only); MAME slows on `$C0n9` [MAME].
- **Write protect:** `write_pulse` ignores WP (`Floppy_woz.cpp:278-298`); WOZ INFO WP ignored (`:61`).
- **Empty-drive sense:** fork 0 (`Floppy525_woz.cpp:90-92`); upstream 1 plus the PHASE1 guard (`up:106`).
- **Restore** resets `last_cycle` (`Floppy_woz.hpp:232`), dropping rotation since the last access; a restored run reads different bits.

**3. Neither models:** any-access switches, `$C08D` clear, Q3-rate sequencer, WOZ MC3470 recipe, mechanical head travel, CYAREG slowdown, write inhibit.

**4. Ideal shape:** a `DiskIIController` (74LS259 + P6 at Q3 resolution) and a `Drive525` (stepper magnets with settle, 51,200-bit empty track, per-image seeded MC3470 table). One disk angle in c14m ticks, shared with the IWM. Rails for steps, motor edges, and per-track writes.

---

## 3. Sony 3.5 drive

**1. Hardware**
- **Selector:** CA2 CA1 CA0 SEL; status on SENSE; control on LSTRB rise [Parker]. MAME's numbering matches every documented row [MAME].
- **Conflicts:**
  - disk-switched: Parker: 0 = ejected (marked unsure) [Parker]; MAME 1 = changed [MAME];
  - selector `$F`: Parker: "0 = drive installed"; MAME 1 for an 800K drive;
  - head select: MAME follows SEL [MAME]; Parker says reading `$8`/`$9` sets it [Parker].
- **Absent drive:** all status bits read 1 [MAME; pull-up].
- **Mechanics:** tachometer 60 pulses/rev; RPM zones 394–590 [MAME]; step ≈12 ms, track-0 valid 12 ms after; eject ≈0.5 s [Parker]; `$C031` b6 cleared stops motors 0.5 s later [MAME].

**2. Fork and upstream (same code)**
- **Right:** tables (`Floppy35_woz.cpp:141-275`); real-time `vid_cycles` clock (`.hpp:76`).
- **Stubs:** tachometer 0 (`:174-176`); installed 0 always (`:201-203`).
- **Head side** changes only when `$8`/`$9` is sampled, including from `fast_forward` (`:177-192`) — a read with side effects.
- **Timing:** instant step with a ≈5 ms busy flag (`:232-248`); motor-off via a non-snapshotted event (`:127-137`); timers restore expired (`.hpp:263`).
- **Eject** sets flags only; bits stay readable, host not told (`:265-268`).
- **Mount** resets track/side (`:56-58`); **SENSE** reported while not enabled.

**3. Neither models:** SmartPort bus over IWM (UniDisk 3.5, SmartPort hard disks), absent drives, RPM/tachometer, 12 ms steps, physical eject.

**4. Ideal shape:** a `Drive35` with polarities and head select taken from hardware tests; tachometer from disk angle; step/ready/eject as tick deadlines; eject raises a host event through writeback policy; rails for strobes and selector reads.

---

## 4. Media identification

**1. Specs:**
- **2IMG:** block_count "should be zero unless ProDOS order"; b31 = locked, b8 = volume valid (else 254) [2IMG].
- **WOZ:** INFO write-protect flag; INFO v3 means a FLUX chunk exists; unknown chunks skipped [WOZ].

**2. Fork and upstream**

| Item | Fork | Upstream |
|---|---|---|
| block_count 0 | Rejected (`media.cpp:363-374`), which rejects valid DOS/NIB 2MGs | File-size fallback (`up:316ff`) |
| comment/creator blobs | Bounded (`:148-174`) | Unbounded `new[len+1]` (`up:127`) |
| `.po` volume | 1 (`:443`) | 254 |

Both: `.dsk` assumed DOS order (`:412`); WOZ WP ignored; `woz.save` writes INFO v3 without FLUX/WRIT and drops unknown chunks (`woz.cpp:541-557,645-657`), corrupting WOZ 2.1 files.

**4. Ideal shape:** identify by content (magic, size, catalog probe, VTOC volume); descriptor with offsets and content hash; upstream fallback plus fork bounds; round-trip every WOZ chunk.

---

## 5. BazFast / PDBlock3 (paravirtual: truth = ProDOS/SmartPort protocol)

- **Fork good:** RAM copy-on-write (`pdblock3.cpp:128-143`), Format, real device count (`:335-345`), deterministic motor indicator (`:648`).
- **Fork problems:** unchecked `fseek`/`fwrite` (`:221-226`); a failed read still copies the buffer to guest memory (`:172-181`); no `$2B` WP status (`:511-519`); single snapshot instance (`:829`); same file mountable twice.
- **Upstream:** refuses duplicate files and non-512 media (`up:505-509`), but uses the wall-clock `SDL_GetTicksNS` for the motor indicator (`up:183,217`), reports 32 devices (`up:273`), and asserts on Format (`up:338`).
- **Ideal shape:** overlay journal with a commit policy, protocol-correct errors, multiple instances, a rail per block operation.

---

## 6. Host FST (upstream only)

- **Mechanism:** WDM `$FF` trap; the CPU thread blocks while a worker thread runs `host_fst()` against guest memory (`up:hostfst.cpp`).
- **No hardware analogue:** WDM is a no-op on a 65816 [rec].
- **Determinism:** directory listings sorted (`host_fst.c:1441`), but host contents and timestamps are outside input; open file descriptors cannot be snapshotted.
- **Ideal shape:** synchronous on the CPU thread; armed only when enabled; call rails plus a replay log of host bytes read; reference numbers invalidated on restore.

---

## 7. Host-file safety (code reading; nothing executed)

- **`.nib` / NIB-2MG overwritten with WOZ bytes:** `MEDIA_PRENYBBLE` writeback falls to `woz.save(filename)` (`Floppy_woz.cpp:106-137`; `up:136`). Writeback is not gated on `modified`, and control-rail mount always calls `SAVE_AND_UNMOUNT` (`a2gspu_ctrl.cpp:406,1006`).
- **Block export zeroes** undecodable sectors and still writes (`woz_nibblizer_525.cpp:470-485`; 3.5 same).
- **5.25 export checks `bool != 0`** (`:481`): success reported as failure, `modified` stuck, buffer leaked.
- **`woz.save` truncates in place** (`woz.cpp:666`).
- **Snapshot media identity is the filename only** (`Floppy_woz.hpp:227-229,271`).

**Ideal shape:** guest writes live in an overlay. Commit only if the overlay is non-empty: temp file, fsync, atomic rename, backup. Lossless export or a `.woz` sidecar. Content hash as identity; snapshot = hash + overlay.

---

## 8. Implementation steps

| # | Step | Size | Risk |
|---|---|---|---|
| 0 | Safety: gate on `modified`; refuse PRENYBBLE writeback; atomic save; lossless-or-refuse export; bool fix; content hash | S | Low |
| 1 | Upstream /ENBL model (3d4efff1); 2^23+100 FCLK holdover; per-access deadline; restore-units fix | M | Med (IIgs hashes) |
| 2 | IWM registers: all-ones/floating reads, handshake `$BF`, underrun latch, H-keyed delayed clear, no EN35 gating | M | Med |
| 3 | SENSE 1 when no drive enabled; PHASE1/empty-drive only after hardware check | S | Med (POST) |
| 4 | Disk II: any-access switches, `$C08D` clear, write-mode snap fix, enable-gated stepper, one event base | M | Med |
| 5 | c14m clock for all drives + CYAREG b0–3 slowdown | M | High |
| 6 | 3.5 drive: verified polarities, tachometer, 12 ms step, real eject, absent drive, `$C031` b6 motor-off, head kept on mount | M | Med |
| 7 | Media: merged 2MG, WOZ WP, WOZ 2.1 chunks, `.po` volume 254, `.dsk` probe | S-M | Low |
| 8 | WOZ MC3470 recipe | S | Low |
| 9 | Snapshot pending events and rotation since last access; `peek()`; storage rails | M | Low |
| 10 | BazFast hardening; Host FST port under determinism rules | M | Low-Med |
| 11 | Flux-level data separator (WOZ FLUX) | L | High |

---

## 9. Validation

- **Firmware:** IIgs ROM 01/03 self-test and POST with drives empty, mounted and write-protected (measure result and boot cycles); IIe DOS 3.3 and ProDOS boots.
- **Register harness** (`apps/*test` pattern, no ROM): assert IWM spec tables p.7–9, handshake after reset/write/underrun, the b5 edge time, and reads in each mode.
- **Protection corpus:** WOZ images for E7 bit-slip, quarter tracks, cross-track sync, fake bits, WOZ 2.1 flux; Locksmith/Copy II Plus speed checks.
- **3.5:** GS/OS Initialize 800K, Finder disk swap, eject, and the John Brooks track reader (`Docs/IWM.md`).
- **Needs real hardware:** disk-switched polarity, `$F`, head select, WP-through-PHASE1, stepping with /ENBL off, write inhibit, Ctrl-Reset to IWM /RESET, 3.5 motor after /ENBL loss.
- **Gates affected:** `unarmedhash` (re-baseline per step), `dualsmoke`, `personality`, Rosetta v3 golden, a2engine `drivecheck`/`gscheck`, snapshot media restore (334fe6ed), fork 2MG checks.

---

## Prioritised summary

1. **Stop image destruction:** `.nib`→WOZ overwrite, zeroed sectors on export, truncating saves, writeback without `modified`.
2. **Adopt upstream's /ENBL model** with the IWM specification's 1.17 s holdover; the fork's frame-quantized drop breaks determinism under stepping.
3. **IWM registers to spec,** independent of `$C031`.
4. **Disk II any-access switches and `$C08D` latch clear** (main copy-protection gaps).
5. **One c14m drive clock plus CYAREG slowdown;** two errors currently cancel on the IIgs.
6. **Verify 3.5 polarities on hardware before changing them;** then tachometer, step timing, eject, absent drives.
7. **Media parsing:** merged 2MG, WOZ write-protect, WOZ 2.1 chunk preservation.
8. **Snapshot completeness and storage rails.**
