# IIgs memory and video core: the ideal shape

Fork `ed2c00fc`, upstream `upstream/main` (`d380e055`). `F:`/`U:` prefix a path under `src/` unless given. None of `18f5d04c`, `42fd845e`, `7333660a`, `0d95d93c`, `829bd5ea` is in the fork. The ROM 3 schematic PDF could not be rendered here (no PDF rasterizer), and the IIe CPLD sources say nothing about the FPI or VGC. Every claim is tagged with its evidence.

## 0. Three structural defects under every piece

1. **Mode state is kept in three places.**
   - **Where it lives.** PAGE2, HIRES and NEWVIDEO live in the MMU (`F:mmus/mmu_iigs.hpp:24-51`), in `display_state_t` (`F:display/display.hpp:95-105,188`) and in the scanner (`F:devices/displaypp/VideoScannerII.hpp:146-162`).
   - **How the copies stay in step.** Two `$C0xx` handlers per address, whose read results are OR-ed (`F:mmus/mmu_ii.cpp:212-228`).
   - **They already disagree after a IIgs reset.** The MMU clears `g_hires` (`F:mmus/mmu_iigs.cpp:743`), but the reset handler resets scanner HIRES only below `PLATFORM_APPLE_IIGS` (`F:display/display.cpp:964-971`).
   - **`$C068` spreads PAGE2 with synthetic bus writes** to `$C054`/`$C055` (`F:mmu_iigs.cpp:176-182`). io-trace and the slot bus log these as guest accesses that never happened.
2. **There is no explicit Mega II cycle phase.**
   - The CPU does the bus access, then advances the clock, which runs `video_cycle()` (`F:cpus/base_6502.cpp:129-137`, `F:NClock.hpp:353-381`). The floating bus, `$C019`, `$C02E/F` and every switch's on-screen effect are therefore defined by call order.
   - Upstream's latency table was tuned against that order: "delay 0" means patching the previous sample (`U:devices/displaypp/ScanBuffer.hpp:23-33`). Its values are not portable hardware numbers.
3. **Probes reimplement the decode logic instead of sharing it.** The fork documents `probe_peek` as resolving banks the way the CPU does, but three cases break that:

   | Case | What the CPU reads | What `probe_peek` returns |
   |---|---|---|
   | `$E0`/`$E1` (`F:mmu_iigs.hpp:285-286`) | `$E0` through the Mega II page table, honouring RAMRD, ALTZP, 80STORE and LC ROM or bank 1 (`F:mmu_iigs.cpp:14-20,337-392`) | raw Mega II RAM; with RDROM set, `$E0:D000` shows RAM while the CPU gets ROM |
   | `$E1` with the bank latch off | `$E0` | aux RAM |
   | banks `$02-$7F` under shadow-all | handler pages | `read_raw`'s floating bus |

## 1. FPI decode: RAM size, unpopulated banks, cycle classes, `$C036`/`$C037`

**Hardware**
- **Bank allocation.** Expansion RAM is `$02-$7F` (256K board) or `$10-$7F` (1 MB board), and "memory spaces from `$80` through `$EF` are not available". [doc: HW Ref ch.3 "Memory bank allocation"]
- **Shadow-all reach.** Shadow-all covers "all RAM banks, `$00` through `$7F`". [doc: HW Ref Table 2-2, bit 4]
- **Unpopulated banks.** They read `$FF` on ROM 3 and the bank number on ROM 1, and expansion RAM ghosts across power-of-two sizes. [emulator: MAME `ghostram_r`]
- **Cycle classes.**
  - Fast: `$C035`, `$C036`, `$C037` and `$C071-$C07F` reads.
  - Written at 1.024 MHz, read fast: `$C068` and `$C02D`.
  - Slow: `$C090-$C0FF` and firmware fetches at `$C100-$CFFF`.
  - Cost: a synchronised slow cycle costs about 3 fast cycles, plus at most one extra slow cycle when unsynchronised. [doc: HW Ref ch.2, speed-estimation text]
- **`$C036` reset.**
  - **Doc:** the register is cleared on reset and power-up, except bit 6, which power-up sets (1 MB board). Bit 6 is read/write, and key-combination boots do not alter it. Bits 0-3 drop to 1 MHz on Disk II motor-on addresses for slots 4-7. [doc: HW Ref Table 2-2]
  - **MAME:** resets the register to `$80`. [emulator: MAME]

**Fork**
- Caps RAM at `$DF0000`, which gives top banks `$81`/`$8F` (`F:mmus/iigs_memory.hpp:43-44`). This contradicts the doc.
- Unpopulated banks read as the bank number on both ROMs (`F:mmu_iigs.cpp:9-12,705-717`).
- Every FPI register access is SYNC, because it enters through `bank_shadow_read` (`:556-559`).
- There is no `$C037` handler.
- Reset writes `$C036=$80` (`:750`), and bits 0-3 do nothing (`F:mmu_iigs.hpp:193-197`).

**Upstream**
- The 8 MB cap matches the doc (`U:mmus/iigs_memory.hpp:43-44`).
- Fast FPI register cycles and `$C037` storage match the doc (`U:mmus/mmu_iigs.cpp:454-490`). Banks `$F0-$FF` are fast (`U:mmus/mmu_iigs.hpp:22`).
- Shadow-all is extended to `$80-$DF` and `$E2-$EF` (`U:mmu_iigs.cpp:717-735`), which contradicts the doc.
- Reset writes `cold&&rom03 ? $C0 : $80` (`:792`). A warm reset therefore clears bit 6 and sets bit 7, both against the doc.

**Neither models:** the ROM 3 `$FF` float, RAM ghosting, the motor-on-detect slowdown, or the doc's `$00` reset value (only the ROM's first cycles would run at 1 MHz).

**Ideal:**
- One `FpiDecode` built from (ROM revision, card size).
  - Per bank it holds: RAM offset, ghost, float value, Mega II or ROM.
  - Per `$C0xx` address it holds a cycle class: fast, read-fast/write-slow, or slow.
- The CPU handlers, probes and cycle accounting all read that one table.
- Speed is `bit7 && !motor_detect`, re-evaluated on every `$C0x8`/`$C0x9` access.

**Steps**

| # | Step | Size | Risk |
|---|---|---|---|
| a | Port `7333660a` and `52dcd30f` | S | medium: cycle counts move |
| b | 8 MB cap | S | low |
| c | ROM 3 `$FF` float and ghosting | S | low |
| d | Motor-on detect | M | 5.25" timing loops change |
| e | `$C036` reset value | S | doc and MAME conflict: needs a hardware answer |

**Validate:** ROM 03 self-test, iigsmmutest `0x99` (extend it for ROM 3), `unarmedhash` `cycles` on both IIgs rows (re-bless with the reason).

## 2. Shadowing: `$C035`, IOLC, all-bank

**Hardware** [doc: HW Ref Table 2-1]
- **Bits 0 and 5 (text pages).** They inhibit text page 1 and 2, main and aux. Bit 5 exists only on the 1 MB board.
- **Bits 1 and 2 (Hi-Res pages).** They inhibit Hi-Res page 1 and 2 "and auxiliary" page 1 and 2.
- **Bit 4 (aux Hi-Res).** It inhibits the Hi-Res pages "as determined by bits 0 through 3" in odd banks only.
- **Bit 3 (SHR).** It inhibits the 32K SHR buffer.
- **Bit 6 (IOLC).** It affects banks `$00`/`$01` only; `$E0`/`$E1` I/O "is always enabled".
- **Reset.** The register is cleared on reset, so everything is shadowed.

Upstream encodes odd-bank `$2000-$5FFF` as inhibited only when bits 3 AND 4 are both set, ignoring bits 1 and 2, citing `SHADOW.s inhbt1=$18` (`U:mmu_iigs.hpp:117-126`). [ROM diagnostic source, as cited by upstream; unverified here] That rule and the doc disagree when bit 1 is set together with bit 3 but not bit 4.

**Fork:** applies bits 1 and 2 to odd banks too (`F:mmu_iigs.hpp:107-132`). Odd-bank HGR therefore still shadows with bits 3 and 4 set, which violates both the doc and upstream. Reset leaves `$08` (`F:mmu_iigs.cpp:751`); the doc says `$00`.

**Upstream:** the even/odd split, plus tests 28-32 (`U:apps/iigsmmutest/tests.hpp:493-555`).

**Neither:** resolves the doc-versus-`SHADOW.s` case, or resets to `$00`.

**Ideal:**
- A pure `shadow_target(parity, a16, reg, rom_rev) -> {none, E0, E1}`, with the disputed term on one line and its source named.
- A rail record on every `$C035` write: old value, new value, CPU cycle.

**Steps**

| # | Step | Size | Risk |
|---|---|---|---|
| a | Port the even/odd rule | S | changes which writes reach `$E1` |
| b | Write a 10-line hardware probe for the bits 1+3 case | S | none |
| c | Reset `$C035` to `$00` | S | low: firmware masks it |

**Validate:** tests 28-32; ROM 03 diagnostic test 04 (`RAM_ADDR`).

## 3. Mega II banks `$E0`/`$E1`

**Hardware**
- **Bank latch.** NEWVIDEO bit 0 enables it; with the latch off, `$E1` behaves as `$E0`. [doc: HW Ref Tables 4-17 and 4-18]
- **Linearize (bit 6).** It linearizes `$E1:2000-9FFF`. Bit 7 "overrides bit 6" and also changes the map. "Set bit 6 to 0 whenever using Double Hi-Res." [doc: HW Ref Table 4-17]
- **Physical layout.** Memory is interleaved: even linear offsets map to `$2000+x/2` and odd ones to `$6000+x/2`, and the VGC fetches the pairs. [emulator: MAME `auxram0000_w`; upstream `U:mmus/iigs_aux_linear.hpp:15-21`, commit "based on Arekkusu test"]
- **Language-card area with the latch on.** The `$FF` ROM overlays `$E1:D000-FFFF` under RDROM, LC bank 1 at `$D000` is a separate page, and write-protect applies. [measured on a real ROM 01, `U:Docs/AppleIIgs-Memory.md`]
- **ALTZP.** An ALTZP change must also move the `$E0` language-card window. [ROM behaviour: ROM 03 diagnostic test 04, per upstream doc]

**Fork**
- `bank_e1_read/write` index `ram[addr&0x1FFFF]` with no ROM overlay, no bank-1 slide, no write protection and no linear map (`F:mmu_iigs.cpp:32-69`).
- An ALTZP change remaps only pages `$00`/`$01` (`:260-271`).
- The SHR scanner reads CPU-linear addresses (`F:VideoScannerIIgs.cpp:96-105,190`). That is self-consistent for SHR display, but not for software that toggles bit 6 and reads the other view.

**Upstream**
- `e1_aux_index` (`U:mmu_iigs.hpp:174-178`).
- A linear shadow store (`:146-157`).
- Interleaved SHR, SCB and palette fetches (`U:VideoScannerIIgs.cpp:185-217`).
- `bsr_map_memory` on ALTZP (`U:mmu_iigs.cpp:269`).

**Neither:** remaps instrumentation readers, or knows the cycle class of the `$E1` ROM-overlay read (upstream calls it "unmeasured").

**Ideal:**
- Physical Mega II RAM is the only store.
- The CPU path, shadow writes, the scanner and probes all call one `megaii_phys(bank_bit, a16, newvideo, lc, switches)`.
- Rails name the view explicitly: the CPU view (`E1:2001`) or the physical view (`megaii:16000`).

**Steps**

| # | Step | Size | Risk |
|---|---|---|---|
| a | Port `42fd845e` and the ALTZP recompose, rebuilding the fork's bus/observer hooks around `e1_aux_index` | S | low |
| b | Port `18f5d04c`, routing roughly 18 fork direct-read sites plus `iigs_shr_layout.hpp` through the map | M | medium |
| c | Make `$E0`/`$E1` probe peek/poke use the same resolver | S | low |

**Validate:** tests 35-47 and the ROM 03 self-test. `protocolcheck`'s SHR golden and `unarmedhash` move only if they hash Mega II memory by CPU address; rendered-frame hashes should not.

## 4. Mega II switches, `$C068`, language card, status reads, reset

**Hardware**
- **`$C000-$C00F`.** Latched on writes, gated by PHI1 low. [CPLD equations: `SOFT_SWITCHES_C00X.vhdl`]
- **`$C050-$C05F`.** Toggle on any access. Reset clears PG2, HIRES and AN0-AN3 but not TEXT or MIX, and clears all the `$C00x` latches. [CPLD equations: `SOFT_SWITCHES_C05X.vhdl`, citing Sather]
- **Status reads.** On the IIe they drive MD7 only. [CPLD: `IOU_MD7.vhdl`] On the IIgs, MAME, maintained by the author of the FLOATBUS test, returns bits 6-0 as zero for `$C011-$C01F`. [emulator: MAME apple2gs.cpp]
- **Language card after reset.** Read ROM, write RAM, bank 2. [doc: HW Ref ch.3]
- **Mega II reset.** That it clears HIRES and AN3 like the IIe IOU is [recollection — unverified].

**Fork**
- `$C011-$C018` return the floating bus in bits 6-0 (`F:mmu_iigs.cpp:449`, fix `30c1e8fa`, argued from IIe VHDL). `$C019-$C01F` return zero (`F:display.cpp:485,614`). The two ranges disagree, and the first contradicts MAME.
- No ALTZP recompose.
- HIRES is split after reset (section 0).

**Upstream:** returns `fl` for the status reads (`U:mmu_iigs.cpp:445`), matching MAME. It asserts on an unhandled `$C0xx` access; the fork's no-op is the right behaviour.

**Ideal:**
- One `MegaIISwitches` block, owned once.
- The MMU map and the scanner's latency pipeline (section 6) subscribe to it.
- `$C068` applies its bits directly, never through bus writes.
- Status reads come from one function whose IIgs low-bit rule is chosen by test.

**Steps**

| # | Step | Size | Risk |
|---|---|---|---|
| a | Consolidate switch ownership | M | medium |
| b | Decide the low bits of `$C011-$C01F` with FLOATBUS | S | low |

**Validate:** Arekkusu FLOATBUS and "Switches Test" (`Docs/DevelopLog.md:9695`); `dualsmoke` `altzpcheck`.

## 5. Interrupts and counters

**Hardware**
- **`$C019`.** Bit 7 is 1 during VBL on the IIgs, and VBL begins at line 192 in every mode. [doc: IIgs Tech Note #40]
- **Scan-line IRQ.** It fires "at the beginning of the right-hand border", in SHR only. [doc: HW Ref ch.4 "VGC interrupts"]
  - The Mega II refreshes RAM in the first five HBL cycles, so the SCB is unknowable before cycle 6.
  - The IRQ fires at cycle 6 or 8.
  - "The entire MegaII appears to be delayed one cycle relative to the VGC."
  - [test author note, quoted in `Docs/DevelopLog.md:8839-8842`]
- **`$C023` and `$C032`.** Only the enable bits of `$C023` are writable. Bit 7 is set, and the IRQ is asserted, whenever status AND enable holds for any source. Writing 0 to bit 5 or 6 of `$C032` clears that status. [doc: HW Ref Tables 4-1, 4-2]
- **Quarter-second IRQ.** Fires at line 256 every 16 frames (3.75 Hz).
- **One-second IRQ.** RTC-driven; it drifts about 1320 cycles per second against video.
- **`$C044`/`$C045`.** Read 0. [test program: IRQTEST, MAME PR 14749]
- **MAME's choices.** It clears scan-line status on a `$C02E`/`$C02F` read, starts VBL one cycle before active video ends, and reads the counters 2 cycles ahead. [emulator: MAME]

**Fork**
- **VBL edge** at hc0 of line 192 (`F:VideoScannerIIgs.cpp:234`); **SCB IRQ** at hc6 (`:230`).
- **`$C02E`/`$C02F` reads** have no side effect (`F:display.cpp:685-697`, fix `3e28f2c3`, argued from the HW Ref's silence). This contradicts MAME.
- **Bug:** a `$C023` or `$C041` write calls `update_*_interrupt(ds,false)` (`:618-646`), which drops the IRQ line even while status AND enable still holds.
- **The one-second IRQ phase comes from `SDL_GetCurrentTime`** (`:1047-1059`). That is host-time nondeterminism, and `A2GSPU_FAKETIME` does not reach it.
- **Free-run mode detaches the scanner** from the CPU clock (`:699-707`).

**Upstream:** clears status on a counter read (`U:display.cpp:664`). It has the same host-time one-second IRQ (`:1021`), but its scanner is always clocked.

**Ideal:**
- IRQ lines are a pure function of (status, enable), recomputed on every change.
- VBL, quarter-second, scan-line IRQ and counter reads are all defined on one beam timeline, using named constants: `VBL_EDGE`, `SCB_IRQ_HC`, `CNT_READ_AHEAD`, `MEGAII_LAG=1`.
- The one-second source is an RTC divider on the 14M timeline, seeded from pinned RTC state.
- Latches, enables and next-event timestamps are all snapshotted.

**Steps**

| # | Step | Size | Guest impact |
|---|---|---|---|
| a | Fix IRQ-line recomputation | S | high: lost IRQs |
| b | Deterministic one-second IRQ | S | low |
| c | Scanner clocked in every pacing mode | S | medium |
| d | Set the alignment constants from tests | M | beam-racing software |
| e | Decide counter-read clearing by test | S | low |

**Validate:** Arekkusu CALIBRATE/GSVBL, GSIRQ, GSSCB, BEAMPOS and IRQTEST (VidModes_260421, MAME issue 16099). `unarmedhash` shifts once when the one-second phase is pinned.

## 6. Video scanner

**Hardware**
- **IIgs latencies relative to the write.** [emulator: MAME `apple2video.cpp` `delayed_update`, tuned on Arekkusu VIDMODES]

  | Switch | Latency |
  |---|---|
  | TEXT, MIXED | 3 cycles |
  | PAGE2 | 2 cycles |
  | HIRES, AN3, 80COL, ALTCHAR | 1 cycle |
  | LANGSEL | 1 cycle, latched at vpos wrap |
  | NEWVIDEO, border, text colour | immediate |

  MAME's PR notes that 80COL, ALTCHAR, LANGSEL and MONO really change 7 14M ticks later, and that some transitions glitch on hardware.
- **Fill mode.** A first pixel of 0 gives an "undetermined" colour. [doc: HW Ref "Color Fill mode"] Hardware seeds it from `SCB&$1F`. [emulator: MAME, citing J. Brooks]
- **SHR buffer access.** In SHR the VGC has "sole access" to the buffer. [doc: HW Ref "The New-Video register"] During refresh cycles and below line 199 nothing drives the floating bus. [emulator: MAME]

**Fork**
- Switches take effect immediately (`F:VideoScannerII.hpp:222-264`).
- Has the fill-seed fix.
- The floating bus is `ram[addr]` from main RAM even in SHR (`F:VideoScannerIIgs.cpp:150-151`).
- Restore drops the scan position (`F:VideoScannerII.cpp:537`).
- Publishes a per-line `scanline_state` rail (`F:VideoScannerIIgs.cpp:213-220`).

**Upstream**
- **Delay table** (`U:VideoScannerIIgs.hpp:8-11`): TEXT 1, MIXED 1, PAGE2 0, HIRES 0, 80STORE/80COL/ALTCHAR 0, AN3 1, SHR 1.
  - Against MAME's relative order: PAGE2 equals HIRES (MAME has it one cycle later), AN3 is later than 80COL (MAME has them equal), and SHR is delayed (MAME applies it immediately).
  - The author calls the GS values unsure (`829bd5ea`).
- **Delay 0** patches only the previous sample's flags (`U:ScanBuffer.hpp:27`).
- **Queue handling.**
  - Pending same-switch entries are cancelled (`U:VideoScannerII.cpp:265,275`), which erases real glitches.
  - A full queue applies entries early (`:278`).
  - The queue is not snapshotted.

**Neither models:** 14M-resolution latency, glitch transitions, IIgs open-bus floating cycles, PAL/LANGSEL timing, or beam position in the snapshot.

**Ideal:**
- **Queue.** Per-switch events are timestamped in 14M on the Mega II timeline (`effect_at = write_14M + LUT14M[sw] + MEGAII_LAG`). Toggles never collapse. Capacity is fixed, with a rail alarm on overflow and no silent early application.
- **Snapshot and rail.** The queue is snapshotted, and each effect emits {switch, value, write cycle, beam at write, beam at effect}.
- **Scanner.** It reads mode state only from the queue.
- **Floating bus.** It is sampled per phase from the scan address, with an open-bus rule for IIgs refresh and SHR cycles.

**Steps**

| # | Step | Size | Risk |
|---|---|---|---|
| a | Timeline and queue skeleton with all delays equal to current behaviour | M | zero guest change; proves the snapshot and gates |
| b | Load latencies from VIDMODES results | S per switch | visible to beam racers |
| c | IIgs open-bus floating bus | S-M | low |
| d | Snapshot beam position and clock phase | M | low |

**Validate:** VIDMODES, FLOATBUS, VIDSYNC, FLASHHZ and LANGSEL; the SATHER.SPLIT and SATHER.WINDOW examples. SHR goldens must hold at step (a) and may move at (b) wherever a mode changes mid-frame.

## 7. Instrumentation and determinism, across all pieces

- **One resolver.** `resolve(addr24, R/W, phase) -> {store, offset, cycle_class, shadow_target, side_effect}` serves the CPU, probes, CTRL `read`/`poke`, bus-trace provenance and SHR tooling. A new gate compares probe and CPU answers for random addresses under random switch states (extends `altzpcheck`).
- **Side-effect-free register peeks.** `probe_io` covers `$C011-$C01F`, `$C019`, `$C023`, `$C02E/F`, `$C035-$C037`, `$C041`, `$C046` and `$C068`. It returns what the CPU would read, without clearing anything.
- **Snapshot contents.**
  - FPI registers, including `$C037`.
  - The single switch block.
  - Interrupt latches and enables, plus next-event timestamps: VBL, quarter-second and the one-second divider phase.
  - Beam position and 14M phase.
  - The pending latency queue.

  A new gate: restoring must reproduce the next frame's hash bit-exactly.
- **Rails.** Change-gated records for `$C035`, `$C036`, `$C068`, `$C029`, latency effects and IRQ raise/clear, each with CPU cycle and beam position. Internal propagation never uses synthetic bus writes.

## Prioritised summary

1. **Fix IRQ-line recomputation and pin the one-second IRQ phase.** S; guest-visible IRQ loss plus a determinism hole.
2. **Build a single resolver for CPU and probes**, covering `$E0`/`$E1`, shadow-all banks and ALTZP. S-M; every rail depends on it.
3. **Port `42fd845e` and the ALTZP recompose.** S; the ROM 03 diagnostic proves it.
4. **Port `18f5d04c`** (linearization plus the 8 MB cap), with the fork's direct readers rerouted. M; re-bless the affected SHR and `unarmedhash` baselines.
5. **Adopt the even/odd shadow rule**, and settle the doc-versus-`SHADOW.s` case with a hardware probe. S.
6. **Port the fast FPI cycle classes** (`7333660a`, `52dcd30f`), then add motor-on detect. S-M; the cycle gates move.
7. **Consolidate switch ownership** and drop the synthetic `$C054`/`$C055` writes. M.
8. **Build the 14M latency queue skeleton**, then take its values from VIDMODES. M, then S.
9. **Decide the test-dependent items:**
   - `$C02E/F` read clearing (IRQTEST/GSSCB);
   - the low bits of `$C011-$C01F` (FLOATBUS);
   - VBL and scan-line IRQ alignment (CALIBRATE/GSVBL/BEAMPOS);
   - `$C035`/`$C036` reset values;
   - ROM 3 `$FF` float and ghosting.

   The fork's fixes 5 and 6 (UPSTREAM_SYNC §4) contradict MAME and should be re-verified first.
