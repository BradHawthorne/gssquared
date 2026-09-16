# INPUT: the ideal architecture (ADB, Apple Mouse card, game port)

Sources: fork `ed2c00fc` (F), `upstream/main` (U). Citations are `file:line` at those refs.

Evidence key:
- **[HWR]**: *Apple IIgs Hardware Reference*, 2nd ed. (ch.2 speed, ch.6 ADB, ch.7 Game I/O).
- **[FWR]**: *Apple IIgs Firmware Reference* (1987) (ch.8 interrupts, ch.9 ADB microcontroller).
- **[IIeTR]**: *Apple IIe Technical Reference* (ch.2 keyboard/switches, ch.7 game I/O circuits).
- **[558]**: Philips NE558 datasheet.
- **[MAME]**: `apple2gs.cpp`. It runs the real M50740/M50741 firmware, so it is strong secondary evidence.
- **[PR10798]**: MAME PR "make kmstatus match observed IIgs behavior".

I could not read the ROM3 schematic PDF. It is scanned images, and rendering it would have meant creating files.

---

## 1. ADB microcontroller + GLU ($C000/$C010/$C024-$C027)

### 1.1 Hardware
- **Two chips.** An M50740 (6502 superset, 96 B RAM, 3K ROM) [HWR ch.6]. ROM 3 uses an M50741 with 4K firmware 341S0632 [MAME]. The micro is clocked at 28.636 MHz/8 (= 14M/4) and gets VBL on INT1 [MAME]. Its own firmware does everything: key scanning over ADB, the type-ahead buffer, auto-repeat, layout-to-ASCII translation, AKD, the modifier register, reset/flush/desk-manager sequences, SRQ polling and mouse polling [FWR ch.9]. The GLU is only "mailbox" registers, flags and the IRQ [FWR ch.9, HWR ch.6].
- **GLU flags.**
  - Each data register sets a flag on write and clears it on read [HWR ch.6].
  - `$C027` is writable only at bits 6, 4 and 2 [MAME, F].
  - Bit 0 is set by a `$C026` write and cleared when the micro reads the byte [HWR].
  - Bit 1 is set after the X read and cleared after the Y read [PR10798]. HWR's "1 = X available" and "first read returns Y" contradict the observed hardware.
  - A `$C026` read clears data-full only if `$C027` was read first [MAME].
  - One IRQ = (data full & bit 4) | (key strobe & bit 2) | (mouse full & bit 6) [MAME].
- **Timing.** Commands take milliseconds: a bus timeout is 10 ms, SYNC takes up to 20 ms, `$10` pulls RESET low for 4 ms, and `$40` holds the bus low for 4 ms [FWR ch.9]. The micro writes the mouse pair (Y then X) and the mouse IRQ comes "in synchronization with VBL", at most 60/s [FWR ch.8]. The GLU turns the mouse interrupt off at reset [FWR ch.8].
- **`$C025`** [HWR fig 6-6]: b7 Command, b6 Option, b5 latch updated with no key, b4 keypad, b3 repeat/key held, b2 Caps, b1 Control, b0 Shift.
- **`$C010`** bit 7 is AKD. Reading or writing `$C010`-`$C01F` clears the strobe [HWR].
- **Command/Option drive SW0/SW1** (`$C061`/`$C062`) through micro port P3 bits 5/4, and the ROM samples them across reset [MAME]. Ctrl-Cmd-Reset makes the micro assert system RESET (P2 bit 5) [MAME, HWR].
- **Argument counts** [FWR ch.9]:
  - `$04`/`$05` +1, `$06` +3, `$07` +4 (ROM 01), `$08` +2, `$09` +2 (high byte 0 = RAM), `$11` +1, `$80`-`$BF` +2.
  - `$48` is a "2-byte command", so +1.
  - `$49`-`$4F` is internally inconsistent in the manual ("num+1 data bytes" vs "3- to 9-byte command").
  - Poll returns a status byte, then the data reversed.
  - ROM 3 SYNC length: probably 8 [recollection — unverified].

### 1.2 Fork (F) and upstream (U)
Both are an HLE command interpreter (`ADB_Micro.hpp`). Commands answer instantly, and host SDL events stand in for polls.
- **Memory safety (F only).**
  - `$08` does `ram[cmd[1]]=` with an index up to 255 into a 96-byte array, corrupting KeyGloo members (F `ADB_Micro.hpp:455`).
  - `$09` addresses 96..$13FF underflow `rom[addr-0x1400]` (F `:462`).
  - `repeat_rate[dlyrpt&0xF]` and `delay_to_repeat[hi]` are not clamped (F `:1043`, `:1162`).
  - U bounds and clamps all of these (U `:112-131`, `:191-211`).
- **ROM 3 micro.** F always loads the ROM 01 image at base $1400 (F `:175`). U selects 341S0632 at $1000 (U `:242-261`).
- **Response count.** F fixed `num_response_bytes` to `uint8_t:3` (F `:157`). U still has `bool:3` (U `:229`), so every multi-byte count reads as 1.
- **Status bit 0** is never set (F `:900-972`, U same).
- **Mouse IRQ** asserts immediately, not at VBL (F `:987-999`). The IRQ is recomputed only on register access or a host event (F `keygloo.cpp:29-45`). U also recomputes per frame (U `keygloo.cpp:156-160`).
- **Mouse data loss.** Every host mouse event re-polls and overwrites an unread pair (F `:1128-1151`, U `:1048-1053`). The micro refills only when the register is empty [Cortland ERS, quoted in F `Docs/ADB.md`].
- **Argument consumption.** `$48` consumes 2 arguments (F `:944-945`). `$49`-`$4F` consume `num` (F `:947-949`). A mis-count desynchronises every later command.
- **`$10`** holds reset for 4 **frames** (F `:494`), not 4 ms.
- **`$C025`** never sets b3/b5 (F `:803-806`). Repeats drop the keypad bit (F `:1161`). The desk-manager sequence fires on key-**up** although the comment says key-down (F `:1107`).
- **`$C026` read** clears data-full without the status-read handshake (F `:837-877`).
- **Correct in F:** the X/Y read order and status bit 1 (F `:1002-1013`), the `$C000`-`$C00F` mirrors (F `keygloo.cpp:193`), and a snapshot of the micro and its devices (F `:723-758`). U has no snapshot.
- **U-only additions:** the ROM 03 repeat table (U `:187`) and per-frame paste into the buffer (U `:1082-1089`).

### 1.3 Neither models
- The firmware itself. Layouts (F US only), dual/4x repeat, lock-shift, the reset-only mode bit, third-party SRQ devices, the error byte, and abort/flush status and IRQs are all guessed.
- Micro latency, and the VBL-synchronous mouse write.
- SW0/SW1 driven from the ADB keyboard. Both read the host's `SDL_GetModState` instead (F `gamecontroller.cpp:281-284`).
- A warm reset wipes micro state (F `:204-226`). The real micro keeps running, since it is what generates RESET.

### 1.4 Ideal
- **(a) Fidelity.** Run the 740-family core on the dumps the fork already ships (`roms/cards/keyglu/rom01-adb-341s0345.bin`, `rom03-adb-341s0632-2.bin`). Wire ports the way [MAME] does:
  - P0 = GLU data bus.
  - P2 = GLU register select, RESET out, ADB in.
  - P3 = ADB out, Command→SW0, Option→SW1.
  - INT1 = VBL, plus the micro's timers.

  The GLU becomes a pure register/flag/IRQ block. The ADB bus is modelled per transaction with spec timing (bit-level is optional).
- **(b) Determinism.** Micro cycles derive from the 14M master in the main scheduler. No host calls anywhere in the device.
- **(c) Instrumentation.**
  - Debug view: micro CPU/RAM/ports/timers, the GLU image, and a ring log of ADB transactions.
  - Full snapshot.
  - Rails: `glu.write{reg,val}`, `glu.read{reg}`, `adb.txn{addr,cmd,reg,bytes,timeout}`, `reset.assert{source}`.
  - `peek_c0xx()` for observers, like MAME's `side_effects_disabled`.
- **(d) Injection.** Keys enter as ADB keyboard switch transitions (§2).

---

## 2. ADB bus devices (keyboard, mouse)

### 2.1 Hardware
- **Mouse register 0:** b15 button (0 = down), b14-8 Y and b6-0 X as 7-bit two's complement [F `Docs/ADB.md`; confirmed by the negative-delta debugging in F `Docs/AppleIIgsDiagnostics.md`].
- **Talk R0.** A device answers only when it has data. Otherwise the bus times out.
- **R3.** High byte: b13 SRQ enable, b11-8 address. Low byte: handler. A bus reset restores the default addresses.
- **Keyboard.** R0 carries up to two transitions. The power/reset key is `$7F7F`. Rollover: modifiers n-key, alphanumerics 2-key lockout [HWR ch.6].
- The mouse accumulates counts between polls, and Caps Lock is a mechanically latching key [recollection — unverified].

### 2.2 F / U
- **R3 and Reset/Flush.** F fixed the R3 byte order (F `ADB_Mouse.hpp:62-93`), Talk honouring `reg` (F `:94-115`) and Reset/Flush defaults (F `ADB_Device.hpp:56-73`, `ADB_Keyboard.hpp:443-494`). U still has swapped R3 fields (U `ADB_Mouse.hpp:54-55`), a Talk that ignores `reg` (U `:59-67`), a keyboard Talk that clears any register (U `ADB_Keyboard.hpp:456`), and empty reset/flush (U `:438-440`).
- **Mouse motion.**
  - Motion *replaces* R0 instead of accumulating.
  - F clamps only the positive side (F `ADB_Mouse.hpp:213-215`), so a delta of −100 encodes as −36. U clamps ±63 (U `:82-85`).
  - F scales host deltas by the window render scale (F `keygloo.cpp:18-27`, `ADB_Mouse.hpp:176-179`), so what the guest sees depends on window size.
  - F snapshots a host-derived `abs_x/abs_y` (F `:238-248`).
- **No data.** A mouse with no data answers `00 00` instead of timing out (F `:105-114`).

### 2.3 Neither models
Accumulation/saturation, the no-response timeout, device SRQ assertion, Listen R2, handler `$FE`/`$FD` address moves, the extended-keyboard handler, and rollover limits.

### 2.4 Ideal
- Devices own their physical state: the set of keys down, and the mouse count accumulators plus button levels. They speak only ADB.
- A Talk consumes and saturates.
- Snapshot everything; rail `adb.dev.talk{addr,reg,data|none}`.
- Inputs arrive only from the input timeline (§3).

---

## 3. Host input, automation injection, closed-loop mouse sync

### 3.1 F / U
- **Keys.**
  - F `force_key`/`force_key_mods` insert ASCII into the micro buffer and force `keysdown=1` (F `ADB_Micro.hpp:689-706`).
  - `hold_key` and the `A2GSPU_FORCE_KEY` env var make `$C000` return a value that is **not in the latch** (F `:784-801`).
  - `key_intercept` swallows keys inside the micro for a host terminal (F `:661`; `a2gspu.cpp:1211-1214`).
  - CTRL `press` (F `a2gspu_ctrl_cmds.cpp:2096-2127`).
- **Mouse.** The file mouse rail writes the GLU pair directly, resets the X/Y phase and overwrites unread data (F `:1018-1033`; `a2gspu.cpp:495-523`).
- **U paste.** Shift-Insert paste as ASCII (U `keygloo.cpp:121-128`).
- **U closed-loop Event Manager (EM) sync.**
  - Reads the Tool Locator and Event Manager globals from guest RAM (U `keygloo_mouse_sync.hpp:90-106`, `:218-254`).
  - Reads `$C029` with a bus read (U `:209-216`).
  - Calls `SDL_GetMouseState` in the frame handler (U `ADB_Micro.hpp:1058-1068`).
  - Injects bounded R0 steps (U `:312-388`) and hides the host cursor (U `:417-446`).
  - Keeps the dead `write_em_axis` (U `:113-134`).
- **Verdict.** U enters at the right place (ADB mouse R0), but its control law sits inside a hardware device and depends on guest globals and host window geometry, so it cannot be replayed. F enters at register level (latch, GLU), the wrong layer.

### 3.2 Ideal
- **(b) One `InputTimeline`** of `{cycle, target, payload}` records:
  - ADB key transition (keycode, down/up)
  - IIe matrix transition
  - mouse counts + buttons
  - card mouse counts
  - paddle resistance or OPEN
  - switch level
  - Joyport switch closures

  Host adapters (SDL keyboard/mouse/gamepad, paste, CTRL rail, the sync) stamp events at the next frame boundary, or at a rail-specified cycle. Every event is appended to a replay log. Devices read only the timeline. No `SDL_Get*` call ever runs from an emulated I/O handler.
- **(d) Injection at the physical layer.**
  - Typing and paste = key down/up transitions with emulated hold times, paced by observing buffer space through `peek`.
  - Mouse = counts into the ADB mouse (or the card's inputs).
  - Retire `hold_key`, `FORCE_KEY` and `inject_mouse_data`, or keep them as explicitly labelled "manipulate" rails that emit a rail event on every use.
  - The terminal intercept moves in front of the timeline.
- **Closed-loop sync is a host adapter.** It reads the guest cursor with side-effect-free peeks and emits counts to the log. A replay reproduces the run without the loop or the window.
- **(c) Rails.** `input.event{cycle,target,payload,source}`, with a debug view of pending timeline events and adapter state.

---

## 4. Apple Mouse card (II/IIe slot)

### 4.1 Hardware
- **Parts.** An R6520 PIA and a 68705 MCU. The 2K 6502 EPROM (342-0270) is banked into `$Cn00` by PB1-3. PB4-7 carry the handshake; PB0 is the sync input [F `Docs/AppleMouse.md`, from board reading].
- **IRQ timer.** The MCU timer fires every 17,030 6502 cycles (NTSC) or 20,280 (PAL). INITMOUSE firmware sets its phase: it watches `$C019` on the IIe and vapour-locks on the II+. The IRQ lands on VBL "down to the cycle" [test program: colino.net 2025-05-08 measurements on IIc/IIe].
- **IRQ causes.** "IRQ at the end of the current screen refresh" [AppleMouse II manual, quoted in F `Docs/AppleMouse.md`]. The default clamp is 0..1023.

### 4.2 F / U
- **F** is paravirtual: a custom register map and a non-Apple ROM (F `mouse.cpp:35-158`, `:295-302`).
  - Motion and button IRQs fire immediately (F `:160-212`).
  - The VBL event is scheduled in emulated time (F `:244-262`).
  - Motion is taken straight from SDL `xrel` (F `:164-178`).
  - No snapshot.
- **U** runs Apple's firmware 342-0270-C, a PIA register model (U `PIA6520.cpp`) and an A2Pico HLE of the MCU protocol (U `MouseController.cpp`).
  - ROM banking is correct (U `applemouseiii.cpp:17-26`).
  - The handshake completes inside a single CPU access (U `MouseController.cpp:353-374`, `:402-413`).
  - Move/button IRQs fire immediately (U `:298-344`).
  - The VBL tick comes from the video frame, not the INITMOUSE sync. TIMEMOUSE is stored but never used for scheduling (U `:185-187`; `applemouseiii.cpp:37-64`).
  - Motion is SDL `xrel/2` (U `:83-106`).
  - It has a state blob, but no snapshot/restore.

### 4.3 Neither models
The phase set by the INITMOUSE handshake, the free-running MCU timer, deferring every IRQ cause to the tick, MCU command latency, and 50 Hz scheduling.

### 4.4 Ideal
- Take U's shape: the Apple ROM plus the PIA.
- Add the MCU:
  - Low-level emulation of the MC68705P3 if its firmware is dumped (MAME's slot mouse is believed to do this [recollection — unverified]).
  - Otherwise, an HLE driven by an emulated-time tick whose phase the sync handshake sets. All IRQ causes latch and are delivered on the tick.
- Inputs = counts + button from the timeline.
- Snapshot the PIA, the MCU and the tick phase. Rails: `mousecard.cmd{op,bytes}`, `mousecard.irq{cause}`.

---

## 5. Game port and Joyport ($C060-$C067, $C070)

### 5.1 Hardware
- **IIe decode.** A 74LS251 mux puts each input on bit 7.
- **Switch inputs** are plain LS TTL inputs. The 560 Ω pull-downs are in the hand controller [IIeTR ch.7], so an absent controller floats high and reads "pressed" (TTL input behaviour, inferred).
- **IIe Apple keys.** Open/Solid Apple are wired to SW0/SW1 [IIeTR ch.2]. A motherboard pull-down there is implied but unverified.
- **IIgs.**
  - The Mega II handles PDL0-3 and SW0-SW3. SW3 is new, at `$C060`. The Slotmaker drives AN0-3, and there is no STROBE output [HWR ch.7].
  - Command/Option are ORed onto SW0/SW1 [MAME].
  - `$C070` is the only trigger; `$C071`-`$C07F` are interrupt ROM and run fast [HWR ch.2].
- **Paddles.** An NE558, T = RC: 150 kΩ × 0.022 µF ≈ 3.3 ms [IIeTR ch.7, 558]. An open input stays high "indefinitely" [IIeTR, HWR ch.7]. The timer is edge-triggered [558] and a running timer is not restarted [MAME].

### 5.2 F / U
- **Host state in I/O handlers.** Every switch or paddle access calls SDL: host state sampled mid-instruction, never recorded (F `gamecontroller.cpp:128-164`, `:233-403`; U same).
- **Open/Closed Apple** come from host modifiers on every platform, including the II/II+ (F `:281-284`; U `:271`, `:305`).
- **Absent pad, switches.** F reads not pressed (F `:264-265`, `:327-328`, `:386-387`). U floats SW0/SW1 on the II/II+ only and SW2 everywhere (U `:263-264`, `:297-298`, `:329-330`). U is closer to [IIeTR].
- **Paddles.**
  - The strobe retriggers unconditionally (F `:112-166`, U `:199-241`).
  - With no pad, F fakes a centred stick at 128 (F `:151-157`). U makes "disconnected" an option that is off by default (U `:78-83`, `:216-228`).
  - PDL2/3 move only under F injection.
- **F injection** correctly enters as the equivalent resistance (F `:120-126`). Switch injection replaces the host wholesale (F `:238-240`).
- **Joyport.** The mux and inverted polarity are correct. The post-reset suspend window (F `:228-231`, `:666-667`) is invented; the real Joyport trips the self-test [F `Docs/Joysticks.md`].
- **Snapshot.** There is none for triggers or annunciators.

### 5.3 Neither models
SW3 at `$C060` on the IIgs, the non-retriggerable timer, open-circuit by default, float per platform, Apple-key wiring per platform, or snapshot state.

### 5.4 Ideal
- **Device.** A `GamePort` with 4 RC channels (Ω or OPEN, deadline, running) and 4 switch lines. Each line is a wired-OR of its drivers (controller, IIe keyboard or IIgs micro P3, Joyport mux), with per-platform termination.
- **Controllers** plug in: `AppleJoystick`, `Paddles`, `Joyport(select switch, Atari closures)`, `None`. The Joyport reset behaviour is a physical select or unplug action, not a timer.
- **(c)** Snapshot everything. Rails: `game.trigger{cycle}`, `game.expire{ch,cycle}`, `game.switch{line,level}`.
- **(d)** Automation sets resistance or switch levels through the timeline.

---

## 6. Implementation steps (ordered by guest-visible impact)

| # | Step | Size | Risk |
|---|------|------|------|
| S1 | F bounds on `$08`/`$09`/repeat tables; ROM 3 loads 341S0632 at $1000 (port U `:106-131`, `:191-211`) | S | Low |
| S2 | SW0/SW1 on IIgs from ADB modifier state; SDL out of the switch readers; no Apple keys on II/II+ | S | Med: OA-Reset cold-boot path |
| S3 | `InputTimeline` + host adapters + replay log; fixed mouse scaling in the adapter | M | Med |
| S4 | GamePort RC/switch model: non-retrigger, OPEN when absent (preference), float per platform, SW3, snapshot | M | Med: joystick detection in software changes |
| S5 | ADB devices accumulate/saturate/time out; micro refills the mouse pair only when empty, at VBL; GLU status-read handshake; `$48`/`$10` fixes | S-M | Med |
| S6 | Automation through transitions/counts; retire latch/GLU pokes; port U's sync as a host adapter using `peek` (never `mmu->read`) | M | Med |
| S7 | LLE ADB micro (740 core, ports, GLU, transaction bus) behind a switch; diff against HLE; then default | L (~2-3k lines) | High |
| S8 | Mouse card: U's PIA+ROM plus tick-synced IRQs and INITMOUSE phase; 68705 LLE if dumped | M | Med |
| S9 | Joyport: replace the suspend timer with the physical select/unplug model | S | Low |

## 7. Validation

- **ADB command stream.** The ROM boot stream must match byte for byte: `07 00 32 00 24`, `73`, `09 51 00`, `08 51 A5` [F `Docs/ADB.md` trace].
- **Diagnostics.** Apple IIgs Diagnostics 2.0 keyboard and mouse tests (F `Docs/AppleIIgsDiagnostics.md`). Built-in self-test (Ctrl-Cmd-Option-Reset).
- **Repeat timing.** Emulated cycles between latch writes for every Control Panel repeat/delay setting must match the [FWR] table (40..4/s; 1/4..1 s).
- **65816 probe program.**
  - Logs the `$C027`/`$C026` sequence for `$0B`, `$0E`, `$49`, `$C2` and Ctrl-Cmd-Esc, on ROM 01 and ROM 3.
  - Compare with [MAME]. Where a real IIgs exists, also settle the open items: `$C025` b3/b5, `$C027` b3 cleared by a status read, the `$49` count, the ROM 3 SYNC length.
- **Mouse.**
  - Phase per [PR10798].
  - The F GS/OS delta proof: six (+30,+20) moves → cursor (+180,+120).
  - No-loss test: 500 counts in one frame → 500 px within the clamp.
  - Cycle of the GLU mouse IRQ vs the VBL line.
- **Game port.**
  - `PDL(n)` with nothing attached returns 255.
  - PREAD is linear in the injected Ω.
  - Retrigger test: two `$C070` hits 1 ms apart; expiry is measured from the first.
  - `PEEK(49248..49251)` with nothing attached, measured on a real II+/IIe/IIgs.
  - OA-Reset cold boot driven only by the emulated keyboard.
- **Mouse card.** Shufflepuck, Dazzle Draw, A2Desktop; the colino IRQ-phase test (IRQ cycle = VBL cycle).
- **Gates affected.**
  - `unarmedhash`: the IIgs gsos601, IIgs ROM-only and IIe ROM-only baselines move under S2/S5/S7 (IIgs) and S4 (IIe). Re-baseline deliberately, one step at a time.
  - `dualsmoke`/`railcheck` and `copilotcheck` (`press 41 0`): S6.
  - `personalitycheck`, mouse configs 4/6/15: S8.
  - Rosetta v3 golden (`_finder.po`): S3/S6/S7.
  - `apps/adbtest`: S5/S7.

## Prioritised summary
1. **S1**: real fork bugs (`$08` corrupts memory; wrong micro ROM on ROM 3). Small fix.
2. **S2**: IIgs Command/Option must reach `$C061`/`$C062` through ADB; host modifier state leaks into the guest today.
3. **S3**: the input timeline, which determinism, replay and honest injection all need.
4. **S4/S5**: absent-controller semantics, non-retriggerable paddles, lossless VBL-synced mouse delivery.
5. **S6**: inject where hardware input enters; keep U's sync only as a recorded host adapter.
6. **S7**: run the micro firmware. It is the only route to full fidelity and the largest step.
7. **S8/S9**: mouse card timing, Joyport cleanup.
