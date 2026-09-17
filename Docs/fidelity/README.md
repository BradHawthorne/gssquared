# Fidelity study: the ideal shape of each subsystem

Written 2026-09-16, during the port of upstream `c4b1fa69..d380e055`, to answer one question per
subsystem: what a real IIgs (or the real card) does, what the fork and upstream each model, what
neither models, and the shape an implementation should have. Neither tree was assumed right.
Evidence is tagged in each report: datasheet, Apple documentation, test program, emulator (secondary
evidence) or recollection (unverified).

**These are studies, not verified results.** Nothing in them was run on hardware. Code citations
refer to the fork at `ed2c00fc` (before the port) and upstream at `d380e055`. The status column
below records what has been done since.

| Report | Subject |
|---|---|
| [memory_video.md](memory_video.md) | FPI decode, shadowing, Mega II `$E0`/`$E1`, soft switches, VGC interrupts and counters, the video scanner |
| [cpu_irq.md](cpu_irq.md) | 6502, 65C02 and 65816 cores: widths, wraps, cycle counts, decimal mode, interrupts, reset |
| [input.md](input.md) | ADB microcontroller and GLU, ADB devices, host input and injection, Apple Mouse card, game port |
| [storage.md](storage.md) | IWM, Disk II, the 3.5 drive, media identification, the block device, Host FST, host-file safety |
| [sound_time.md](sound_time.md) | Ensoniq DOC and Sound GLU, speaker, Mockingboard, master clock, RTC, ThunderClock |
| [serial_hostio.md](serial_hostio.md) | Z85C30 SCC, Super Serial Card, parallel card, the host-connection framework |
| [video_cards.md](video_cards.md) | SecondSight (all modes), VOC, Videx, VidHD, the A2GSPU card model, and SecondSight ideas mapped onto it |
| [slot_bus.md](slot_bus.md) | The slot bus as a card sees it, and the card-emulation interface card-firmware validation needs |
| [control_plane.md](control_plane.md) | CTRL rail, debug protocol, observation guarantees, snapshots, triggers, records, configuration |

## Status of the headline findings

| Finding | Report | Status |
|---|---|---|
| Aux linearization of `$E1:2000-9FFF` (`18f5d04c`) and the ALTZP recompose (`42fd845e`) not in the fork | memory_video | **Done**: stage 2 (`6083b03c`), with CPU-order observers |
| Fast FPI register cycles (`7333660a`) | memory_video | **Done**: stage 2. The motor-on detect slowdown is opt-in (`A2GSPU_DISK_TIMING=1`, §19) |
| `$C037` not handled; `$C035` reset to `$08` | memory_video | **Done**: `7bf8b405` (§18): the DMA bank register's handlers, which the port brought but never installed, are in; `$C035` resets to `$00` [HW Ref Table 2-1] and the ROM sets `$08` itself. `$C036`'s reset value still needs a hardware answer (the doc and MAME differ) |
| One-second interrupt phase from the host clock | memory_video, sound_time | **Done**: `a3b13b1d` (pinned under `A2GSPU_FAKETIME`, no drift, saved in `vgcint` v2) |
| ThunderClock needs a time pin | sound_time, slot_bus | **Done**: `a3b13b1d` |
| ADB micro ROM on ROM 3 machines | input | **Done**: stage 2 (`is_rom03_machine`) |
| Upstream's IWM /ENBL model | storage | **Done**: stage 1 (`edb4ddb6`) |
| IWM registers to the IWM specification (step 2): all-ones and floating reads, handshake ones, 1.17 s holdover, delayed clear keyed on H, underrun latch, no EN35 gating | storage | **Done**: `7d07dde9` (§18). After an underrun the buffer still takes bytes and they go out with /WRREQ off; the IWM specification names only /WRREQ and /underrun, and a model that stops the buffer hangs the ROM's SmartPort probe (next row) |
| Disk clock: the 5.25 drive counts CPU cycles, so a IIgs at 2.8 MHz has 1.4 µs bit cells and the ROM's SmartPort probe underruns where hardware does not | storage, memory_video | **Opt-in** (§19): `A2GSPU_DISK_TIMING=1` gives the `$C036` motor-on detect, 5.25 drives in real time and write cells that start with the write state; the probe's packet then goes out whole. The default keeps the pinned timing (flipping it is the owner's; it moves every IIgs cycle hash). Open: the mode-driven bit cell, and whether the FPI gates slot 6's detect on EN35 |
| `$C03D` one-access pipeline with latched address | sound_time | **Done**: stage 1 |
| Uthernet II: no interrupts, state machine, silent LISTEN failure, host timing inside register reads | serial_hostio, slot_bus | **Done**: fused card `25c787d2` (a replayable network log is still open) |
| No snapshot for the new cards | video_cards, serial_hostio, sound_time | **Done**: `u2`, `tclock`, `mousecd`, `voc`, `ssc`, `ssight` (SecondSight GPU mode is unsaveable and says so) |
| Debug protocol reads actuate I/O (`READMEM`/`FINDMEM`, stop snapshots) | control_plane (U1, U2) | **Done**: `b96f1c3d` (peek by default, explicit `MAIN_BUS`, `*_READABLE`) |
| SecondSight `ClearScreen`/`ScrollScreen` host heap overrun; `GetVGAReg` stale remainder; later upload chunks unchecked | video_cards | **Done**: clipped to the card's 1 MB and reported; whole upload checked; `GetVGAReg` completes (`UPSTREAM_SYNC.md` §15) |
| ADB `$08` writes past the 96-byte RAM; `$09` underflows the ROM index; repeat tables unclamped | input | **Done**: stage 2 took upstream's bounded `read_uc_memory`/`write_uc_memory` and clamps (the fork's 3-bit response count kept) |
| IRQ-line recomputation | memory_video | **Done**: `$C023`/`$C041` writes recompute the line; Mega II VBL and quarter-second flags latch only while enabled (§15) |
| SCC interrupt block (S1-S4, WR0 commands, IUS, software INTACK) | serial_hostio | **Done** (§15) |
| SCC receiver and transmitter (S6-S8): 1-byte buffer pulled only on a guest access; All Sent = TBE | serial_hostio | **Done**: `cfb84ce5` continuous receiver, 3-byte FIFO with overrun, two-stage transmitter (§16); `4d4f72a1` clocks as WR11/WR14 route them, External/Status latches, modem lines and break both ways, and `aa103ba7` a `scc` snapshot block (§18). Parity and framing errors are still open |
| Host framework H1-H4: racy rings, thread started in the base constructor, silent drops, shutdown hang, 40 s blocking dial | serial_hostio | **Done**: `e483676c` (atomic rings, `start()`, counted drops, retrying `stop()`), `10b82f97` (abortable dial) (§16). The emulated-time HostLink boundary (H6) is still open |
| Parallel card: bytes lost when the guest outruns the file device; CRLF inserted on Windows | serial_hostio | **Done** for capture: `e483676c` binary files (§16); `70fd55a2` an emulator-side spill queue, lossless with no host wait (§17). STROBE/ACK/FF2 in emulated time is still open |
| Host-file safety: `.nib` overwrite, zeroed sectors on export, truncating saves, writeback without `modified` | storage | **Done**: no write unless modified; atomic saves; no lossy export (a `.written.woz` sidecar instead) (§15); snapshot media identity by content hash, `aa30b820` (§16) |
| Slot bus stream defects (step 1): M2B0, `$E0` writes and Mega II reads never emitted, `$C100-$CFFF` never emitted, decode for a fixed slot, `/INH` misused, fast cycles emitted, no speed context | slot_bus | **Done, opt-in**: `ea674b6b` `A2GSPU_SLOT_BUS_FORMAT=2` (§16); `2dceb087` fast cycles dropped, bit 7 = fast mode, the `buscompare.py` comparator (§17). Format 1 stays the default until the card's firmware decodes format 2 and one real capture compares clean |
| Slot bus: `$C02D` gating of `$C0nX` and the C8 handler | slot_bus | **Done**: `575e1703`, from Apple's ROM evidence (§17; confirmed or reversed by `tools/hwtruth`). It also stopped the motherboard's `$C800` ROM (INTC8ROM) mapping and crashed the pinned Finder boot in InitTextDev until `9c4841f5`, which restores it and adds unarmedhash row `iigs-finder` (§18) |
| Slot bus: per-card `$C800` flip-flop, card-driven pins, SlotBus/SlotCardOps | slot_bus | **Done** for the flip-flop: `c31353b6`, one per card, contention counted, `$CFFF` released after its byte (§18). Card-driven pins and SlotBus/SlotCardOps are still open |
| Super Serial Card 6551 (serial step 7, A1-A7): programmed reset, transmit interrupt, modem inputs, receiver clock, stop bits, overrun, IRQ latch | serial_hostio | **Done**: `9a5f93fb`, per the Rockwell R6551 datasheet; `ssc` block v2 (§18) |
| Sound: host and debugger state gating DOC IRQs | sound_time | **Done**: the DOC runs on emulated time with or without a host stream, and while stepping (§15) |
| Decimal-mode flags (CPU step 7): NMOS ADC N after the correction, NMOS SBC N and V from the decimal value, 65C02 SBC accumulator Seq.3 | cpu_irq | **Done**: `fe7ffe13` (§18), Clark's Appendix A. `conformtest` checks every 8-bit operand and carry on the 6502, 65C02 and 65816, and a lattice of 16-bit BCD operands. The study's "65816 SBC V ✗" did not hold: V taken before the correction equals the binary rule on every input |
| 65C02 ASL/LSR/ROL/ROR abs,X always 7 cycles (CPU step 6, part); 65816 emulation-mode (d,X) pointer wrapping whatever DL holds (step 8, part) | cpu_irq | **Done** for the (d,X) wrap: `c93a9ea5` (§18). The (d,X) pointer wraps inside its page only when DL is `$00`, as `d,X` and `(d)` already did. A `cpu_cycles_65c02` ctest holds the 65C02's own counts (JMP (abs) 6). **Reverted** for the shifts: `c93a9ea5` gave them 6 cycles (7 across a page) from WDC's documentation, but Rockwell lists 7 flat and the Apple IIe/IIc parts are not WDC's (a2engine cpucheck records the same dispute and failed on it). They take 7 again. The unused-NOP reads, COP's E-mode B bit and the 6502 SH view are still open |
| RTC registers: `z010ab01` aliased to BRAM `$00-$03`, write-protect and test registers ignored, no snapshot | sound_time | **Done**: `4978614e` (§18): `$10-$13`; write-protect bit 7 locks the clock, the test register and BRAM; the test register is kept; block `rtc` v1 (BRAM included; a restore keeps the restoring session's clock). Busy still clears after two `$C034` reads: no document gives the transfer time |
| Time pin frozen and guest-visible: RTC writes overwritten, no ticking origin | sound_time | **Done, opt-in**: `ef506b39` `A2GSPU_FAKETIME_TICK=1` ticks in emulated time on the one-second IRQ's edges; guest-set time sticks under either form (§16). Frozen stays the default: the owner decides |
| Probes answer `$E0`/`$E1` from raw Mega II RAM (structural defect 3) | memory_video | **Done** for `$E0`/`$E1`: `e5524f13` (bank latch, LC ROM and bank 1, RAMRD) (§16). Shadow-all handler pages in banks `$02-$7F` are still open |
| Rail replies that were not true (control-plane step 1: F1-F3, F7, F8, F10-F13) | control_plane | **Done**: `f69c16ac` (§18): JSON status, `load` through probe_poke, `keys` reports what ran, strict parsing, the machine and slots in PINS and the manifest |
| Control plane: one op registry, one trigger engine, one record stream, hermetic configuration | control_plane | Open |
| SecondSight: the host setting `ss_text_mode` rewrites card state; handshakes advance on polls, not time | video_cards | Open (recorded in `UPSTREAM_SYNC.md` §12) |

## Decisions that need hardware evidence

These can't be settled from documents or emulators. Each needs a test program run on a real
machine (or card) before either tree's behaviour is kept.

**The test program exists:** `tools/hwtruth` (README there) builds a bootable 800K disk that
measures the questions marked **[hwtruth]**, writes the raw bytes back to the disk, and prints
them. `report.py` turns a real machine's run and the emulator's into answers side by side, and
the `hwtruth` gate holds the emulator to its recorded answers until a hardware run changes one.
Its answers today are in `tools/hwtruth/emulator_verdicts.json`.

- **[hwtruth]** Game switches with nothing attached (the IIe manual puts the pull-downs in the hand controller, which favours upstream's floating reads).
- **[hwtruth]** The low bits of `$C011-$C01F`, and whether reading `$C02E`/`$C02F` clears the scan-line interrupt. The fork's fixes 5 and 6 contradict MAME.
- **[hwtruth]** MVN/MVP stepping with x=1 (the documentation favours upstream's 8-bit stepping, with the fork's explicit banks).
- VBL and scan-line interrupt alignment; `$C035`/`$C036` reset values; **[hwtruth]** ROM 3 `$FF` float (unpopulated bank reads) and ghosting.
- The 3.5 drive's status polarities (disk-switched, drive-installed, head select).
- W5100 `MR` bit 0 (IND) after reset on the Uthernet II.
- SecondSight `SetPaletteEntry` length (fixed to 4 data bytes; confirm against the card's library).

**Also from the study's hardware list:** **[hwtruth]** whether the Mega II VBL and quarter-second
flags latch while disabled. The fork now follows MAME and KEGS (they do not), from recollection of
those sources. a2engine's `intcheck` assumed they did until `e462595`.

**Added by the second pass (§16):**
- **[hwtruth]** Whether `$C02D` bit 6 set to "Your Card" deselects the IWM at `$C0E0-$C0EF`. **Evidence found since, and acted on:** before calling the built-in slot 5 firmware, ROM 01 saves `$C02D`, clears bits 5 and 6 and restores it afterwards (`$FE7026`; `$FE6F63`/`$FE6F87`), which only makes sense if bit 6 takes the IWM away. The gating is in (`575e1703`). Config 9 has no Disk II card; it uses the built-in port. The hardware run confirms or reverses it.
- **[hwtruth]** Whether bank `$E0` follows RAMRD and RAMWRT. **Reasoning since:** TN68 says the Mega II learns the bank only from M2B0, which does not reflect the auxiliary switches and is high for every `$E1` access. The Mega II holds those switches itself, so a `$00` access and an `$E0` access look the same to it: `$E0` should follow them and `$E1` should not, which is what the emulator does. (An earlier line here recalled the Hardware Reference saying otherwise; that recollection is probably wrong.)
- **[hwtruth]** The IWM's data register with the motor off. The IWM spec says all ones, and the emulator now reads `$FF` (`7d07dde9`, §18); it used to read a stale latch (`$D4` after a boot, `$00` before one). The hardware run confirms or reverses it.
- The IWM after an asynchronous underrun: whether the buffer keeps feeding the shift register (the emulator's reading) or stops until L7 clears. Needs a write-mode test program on a real machine.
- **[hwtruth]** The IWM at odd addresses (the emulator drives nothing), an even read with Q6 and Q7 both on (the IWM specification lists no read register there; the emulator answers `$FF`), and the handshake register's bits 0-5 and bit 6 outside the write state.
- The 65C02's ASL/LSR/ROL/ROR abs,X without a page cross: 6 cycles (WDC) or 7 (Rockwell). It needs the IIe enhanced or IIc part measured, or its datasheet; hwtruth runs on the IIgs's 65816, so it cannot answer.
- **[hwtruth]** Whether the IIgs decodes I/O on the 65816's invalid-address cycle (VDA=VPA=0; `cpu_irq.md` §1.3). The emulator's core turns that cycle of every `STA abs,X` into a real read, so it answers YES. The IWM's chip-level rule that an odd *read* with Q6 and Q7 on latches the bus (it has no R/W line) waits on this answer: with the extra read reaching I/O, DOS 3.3's `STA Q6H,X` latches the floating bus a cycle before its nibble, and the sync write path, which shifts the data register itself, puts that on the disk.
- **[hwtruth]** How long a clock transfer holds `$C034` bit 7, in polls (the emulator: two reads each), and the one-byte BRAM commands' addresses.
- The parallel card's ACK and FF2 timing, and the PROM's "Printer-Not-Available" alteration.
