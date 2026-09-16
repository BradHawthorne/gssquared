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
| SCC interrupt block (S1-S4, WR0 commands, IUS, software INTACK) | serial_hostio | **Done** (§15). Receiver FIFO, special conditions in the data path, modem lines, host queue drops and the shutdown hang are still open |
| Host-file safety: `.nib` overwrite, zeroed sectors on export, truncating saves, writeback without `modified` | storage | **Done**: no write unless modified; atomic saves; no lossy export (a `.written.woz` sidecar instead) (§15). Content-hash media identity is still open |
| Slot bus: `$C02D` gating, per-card `$C800` flip-flop, stream defects, card-driven pins | slot_bus | Open |
| Sound: host and debugger state gating DOC IRQs | sound_time | **Done**: the DOC runs on emulated time with or without a host stream, and while stepping (§15). A ticking FAKETIME origin is still open |
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
