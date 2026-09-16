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
| Fast FPI register cycles (`7333660a`) | memory_video | **Done**: stage 2. Motor-on detect slowdown is still open |
| One-second interrupt phase from the host clock | memory_video, sound_time | **Done**: `a3b13b1d` (pinned under `A2GSPU_FAKETIME`, no drift, saved in `vgcint` v2) |
| ThunderClock needs a time pin | sound_time, slot_bus | **Done**: `a3b13b1d` |
| ADB micro ROM on ROM 3 machines | input | **Done**: stage 2 (`is_rom03_machine`) |
| Upstream's IWM /ENBL model | storage | **Done**: stage 1 (`edb4ddb6`) |
| `$C03D` one-access pipeline with latched address | sound_time | **Done**: stage 1 |
| Uthernet II: no interrupts, state machine, silent LISTEN failure, host timing inside register reads | serial_hostio, slot_bus | **Done**: fused card `25c787d2` (a replayable network log is still open) |
| No snapshot for the new cards | video_cards, serial_hostio, sound_time | **Done**: `u2`, `tclock`, `mousecd`, `voc`, `ssc`, `ssight` (SecondSight GPU mode is unsaveable and says so) |
| Debug protocol reads actuate I/O (`READMEM`/`FINDMEM`, stop snapshots) | control_plane (U1, U2) | **Done**: `b96f1c3d` (peek by default, explicit `MAIN_BUS`, `*_READABLE`) |
| SecondSight `ClearScreen`/`ScrollScreen` host heap overrun; `GetVGAReg` stale remainder; later upload chunks unchecked | video_cards | **Done**: clipped to the card's 1 MB and reported; whole upload checked; `GetVGAReg` completes (`UPSTREAM_SYNC.md` §15) |
| ADB `$08` writes past the 96-byte RAM; `$09` underflows the ROM index; repeat tables unclamped | input | **Done**: stage 2 took upstream's bounded `read_uc_memory`/`write_uc_memory` and clamps (the fork's 3-bit response count kept) |
| IRQ-line recomputation | memory_video | **Done**: `$C023`/`$C041` writes recompute the line; Mega II VBL and quarter-second flags latch only while enabled (§15) |
| SCC interrupt block (S1-S4, WR0 commands, IUS, software INTACK) | serial_hostio | **Done** (§15) |
| SCC receiver and transmitter (S6-S8): 1-byte buffer pulled only on a guest access; All Sent = TBE | serial_hostio | **Done**: `cfb84ce5` continuous receiver, 3-byte FIFO with overrun, two-stage transmitter (§16). Modem lines, break and parity/framing errors are still open |
| Host framework H1-H4: racy rings, thread started in the base constructor, silent drops, shutdown hang, 40 s blocking dial | serial_hostio | **Done**: `e483676c` (atomic rings, `start()`, counted drops, retrying `stop()`), `10b82f97` (abortable dial) (§16). The emulated-time HostLink boundary (H6) is still open |
| Parallel card: bytes lost when the guest outruns the file device; CRLF inserted on Windows | serial_hostio | **Done** for capture: `e483676c` waits for room and writes binary (§16). STROBE/ACK/FF2 in emulated time is still open |
| Host-file safety: `.nib` overwrite, zeroed sectors on export, truncating saves, writeback without `modified` | storage | **Done**: no write unless modified; atomic saves; no lossy export (a `.written.woz` sidecar instead) (§15); snapshot media identity by content hash, `aa30b820` (§16) |
| Slot bus stream defects (step 1): M2B0, `$E0` writes and Mega II reads never emitted, `$C100-$CFFF` never emitted, decode for a fixed slot, `/INH` misused | slot_bus | **Done, opt-in**: `ea674b6b` `A2GSPU_SLOT_BUS_FORMAT=2`; format 1 stays the default until the card's firmware decodes format 2 (§16) |
| Slot bus: `$C02D` gating of `$C0nX` and the C8 handler, per-card `$C800` flip-flop, card-driven pins | slot_bus | Open. Gating waits on a hardware fact (below) |
| Sound: host and debugger state gating DOC IRQs | sound_time | **Done**: the DOC runs on emulated time with or without a host stream, and while stepping (§15) |
| Time pin frozen and guest-visible: RTC writes overwritten, no ticking origin | sound_time | **Done, opt-in**: `ef506b39` `A2GSPU_FAKETIME_TICK=1` ticks in emulated time on the one-second IRQ's edges; guest-set time sticks under either form (§16). Frozen stays the default: the owner decides |
| Probes answer `$E0`/`$E1` from raw Mega II RAM (structural defect 3) | memory_video | **Done** for `$E0`/`$E1`: `e5524f13` (bank latch, LC ROM and bank 1, RAMRD) (§16). Shadow-all handler pages in banks `$02-$7F` are still open |
| Control plane: one op registry, one trigger engine, one record stream, hermetic configuration | control_plane | Open |
| SecondSight: the host setting `ss_text_mode` rewrites card state; handshakes advance on polls, not time | video_cards | Open (recorded in `UPSTREAM_SYNC.md` §12) |

## Decisions that need hardware evidence

These can't be settled from documents or emulators. Each needs a test program run on a real
machine (or card) before either tree's behaviour is kept.

- Game switches with nothing attached (the IIe manual puts the pull-downs in the hand controller, which favours upstream's floating reads).
- The low bits of `$C011-$C01F`, and whether reading `$C02E`/`$C02F` clears the scan-line interrupt. The fork's fixes 5 and 6 contradict MAME.
- MVN/MVP stepping with x=1 (the documentation favours upstream's 8-bit stepping, with the fork's explicit banks).
- VBL and scan-line interrupt alignment; `$C035`/`$C036` reset values; ROM 3 `$FF` float and ghosting.
- The 3.5 drive's status polarities (disk-switched, drive-installed, head select).
- W5100 `MR` bit 0 (IND) after reset on the Uthernet II.
- SecondSight `SetPaletteEntry` length (fixed to 4 data bytes; confirm against the card's library).

**Also from the study's hardware list:** whether the Mega II VBL and quarter-second flags latch
while disabled. The fork now follows MAME and KEGS (they do not), from recollection of those
sources. A hardware test belongs with the other decisions above.

**Added by the second pass (§16):**
- Whether `$C02D` bit 6 set to "Your Card" deselects the IWM at `$C0E0-$C0EF` (which would also take the slot 5 3.5 drives away), and whether bit 5 does anything to it. `$C0nX` gating (slot bus step 2) waits on this, because a Disk II card in slot 6 (config 9) and the ROM's slot scan both depend on the answer.
- Whether bank `$E0` honours RAMRD, RAMWRT, ALTZP and 80STORE. The fork's CPU path sends `$E0` through the Mega II page table, so it does; the probe now mirrors that. The Hardware Reference is recalled as saying the auxiliary switches affect only banks `$00`/`$01`.
- The parallel card's ACK and FF2 timing, and the PROM's "Printer-Not-Available" alteration.
