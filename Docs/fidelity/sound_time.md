# Sound, clocks and master timing: the ideal shape

Sources: fork `ed2c00fc`, upstream `upstream/main` (`d380e055`), read with `git show`. Fork and upstream Mockingboard and speaker sources are identical apart from trivia. Unmarked `file:line` citations are fork; `up:` marks upstream.

**Evidence gaps.** The IIgs ROM3 schematic, the Ensoniq DOC ERS (Cortland v4.13), the ES5503, uPD1990AC and R6522 datasheets are scanned PDFs. I could not render them here (no `pdftoppm`). So nothing below is tagged [datasheet] or [schematic]. Hardware claims rest on docs quoted in text form, on emulators that agree with each other, or they are marked unverified.

## 0. Cross-cutting defects (both trees unless noted)

1. **The host audio path gates emulation.** `ensoniq_catch_up` returns early when there is no SDL stream (soundglu.cpp:80; up:141). `create_stream` returns nullptr when the device cannot be bound (AudioSystem.cpp:97,101). On a host with no audio device, the DOC never advances and oscillator IRQs never fire.
2. **Stepping diverges from running.** DOC time is pinned while `execution_mode != EXEC_NORMAL` (soundglu.cpp:87; up:148), so a stepped machine loses DOC time.
3. **The wall clock enters without a pin.** The VGC one-second IRQ phase is seeded from `SDL_GetCurrentTime` (display.cpp:1047-1058; up:~1018-1028). Upstream's ludicrous calibration picks the CPU-cycles-per-14M ratio (N) from wall time (up:computer.cpp:419-520). ThunderClock uses `localtime` (thunderclockplus.cpp:61-86; up:73). In the fork, ludicrous mode runs the CPU until a wall-clock deadline (gs2.cpp:497,528).
4. **Snapshots are incomplete.** The fork saves only the Ensoniq in this area. It lists `rtc` and `clock` as excluded (a2gspu_snapshot.cpp:98) and has no Mockingboard, speaker or ThunderClock blocks. The DOC sample phase and the in-flight read are discarded on restore (soundglu.cpp:442-445).

---

## 1. ES5503 DOC core

**Hardware**
- Clock input is 7M (7.159 MHz). One oscillator update takes 8 clocks. A scan covers N enabled oscillators plus 2 extra slots, so the rate is 7159090/8/(N+2): 26,320 Hz with 32 oscillators [emulator: MAME apple2gs.cpp `ES5503(..., A2GS_7M)`, es5503.cpp; Clemens "2 extra cycles"]. Oscillators are updated in sequence, one slot each [emulator: Clemens, a2fpga doc5503.sv].
- Registers: `$00/$20` frequency, `$40` volume, `$60` last sample, `$80` table page, `$A0` control (b0 halt, b2-1 mode, b3 IE, b7-4 CA), `$C0` bank/size/resolution. Address = (acc >> (9+res−size)) ORed with the page, low `size` bits masked [emulator: MAME, Clemens, a2fpga agree].
- A zero sample halts the oscillator in any mode. At the end of the table, free-run loops, one-shot halts, and swap halts and starts its partner at accumulator 0 [emulator: MAME, Clemens, a2fpga]. An even oscillator whose partner is in swap mode retriggers ("verified on IIgs hardware") [emulator: MAME 2.2, SWAPTEST]. A CPU halt with IE clear in the new control value raises no IRQ [emulator: MAME 2.4.1, Bard's Tale].
- The highest enabled oscillator counts 3× in the output. A swap transition emits one centerline (0x80) sample [emulator: MAME header].
- **Disputed:** the loop-wrap arithmetic. MAME subtracts (size−1)<<shift once the pre-increment position reaches size−1; a2fpga wraps modulo size. Also which partner a sync-mode halt resets: MAME resets onum−1, Clemens onum^1.
- `$E0` register: b7=0 means IRQ pending, b5-1 hold the oscillator number, b6 and b0 read 1. Reading clears that oscillator's pending flag [emulator: MAME, KEGS 0.91 via GSport, Clemens]. **Disputed:** when several are pending, the lowest-numbered is reported (MAME) or the first in time (KEGS, Clemens). KEGS also clears a pending IRQ on any control-register write.
- `$E1`: N = ((v>>1)&0x1F)+1. The value read back is normalized as (N−1)<<1 by MAME and KEGS. `$E2` is the A/D register; it reads `$80` with nothing attached [emulator: KEGS, Clemens].

**Fork and upstream**
- Fork `$E0` (ensoniq.cpp:360-376) clears `irqpend` but never re-drives the line, which then drops only at the next `$E0` read or halt: one spurious IRQ entry per DOC interrupt. Upstream ports MAME (drop the line, clear one, re-assert if more) (up:ensoniq.cpp:323-354).
- The fork lacks the `newCtrl` rule (ensoniq.cpp:176-182; up:143), and its wrap is a third variant (ensoniq.cpp:153-159, 266-267); upstream follows MAME (up:121-122, 230).
- Both return raw `$E1` (ensoniq.cpp:418); `$E2` reads 0 because no ADC callback is ever set (422-426).
- The fork reformats the SDL stream on every `$E1` write (491-496, 92-129); upstream made that a no-op (up:90).
- Both render whole per-oscillator batches, not in slot order (ensoniq.cpp:196-287).

**Neither models:** slot-ordered updates and per-slot IRQ time, the swap 0x80 sample, `$E2`/A/D, `$E1` normalization, stopping oscillators above a lowered N (KEGS does), the pending-IRQ reporting order as measured, or clear-on-control-write.

**Ideal**
- (a) A slot-stepped core: `advance_to(c14m)` runs whole slots and IRQs carry exact slot timestamps. Each disputed rule is a named, tested switch.
- (b) Depends only on 14M time: no SDL, no execution-mode checks. Snapshot writes slot index and 14M phase.
- (c) Side-effect-free `peek(reg)`. Events: `doc.halt{osc,cause}`, `doc.irq{osc,assert}`, `doc.e0read{osc}`, `doc.rate{N}`. Debug view adds pending order and scan phase.

**Steps:** (1) port the upstream `$E0` read and `newCtrl` rule (S, low); (2) remove the stream and execution-mode gates and the SDL call from `$E1` (S, low; largest determinism gain); (3) wrap rule and sync partner, after the loop-length test settles them (S, medium: pitch); (4) slot-ordered core plus remaining quirks (M, medium).

**Validation.** A new test program: free-run one oscillator with IE and count IRQs per 600 VBLs at known size, resolution and frequency, with goldens from a real IIgs. Plus SWAPTEST, Bard's Tale, Skate or Die, synthLAB/Arkanoid (pitch), FTA Delta Demo, SoundSmith POLY.SYNTH, Thexder/Nucleus [emulator: MAME changelog names these].

## 2. Sound GLU ($C03C-$C03F, 64 KB DOC RAM)

**Hardware**
- `$C03C`: b7 busy (read-only), b6 1=RAM/0=registers, b5 auto-increment, b3-0 volume [doc: "SOUND GS Part 6"]. **Disputed readback:** MAME returns ctrl|0x1F, the fork and upstream |0x0F, KEGS and Clemens the written value.
- `$C03D` reads lag by one access: "read the data register eleven times, discarding the first byte" [doc: Apple Ensoniq DOC ERS, quoted at Docs/Ensoniq5503.md:64-69; emulator: MAME, KEGS, Clemens citing HW Ref ch.5 p.107]. A write also loads the latch [emulator: KEGS, Clemens].
- The 16-bit address auto-increments on both reads and writes. MAME clears the high address byte on a `$C03C` write in register mode [emulator: MAME only].
- The RAM is 64 KB; the bank bit is unused on the IIgs.
- Stereo convention: odd channels left, even right [doc: Apple IIgs TN #19, via Bumbershoot Software]. The motherboard sums all channels to mono through the volume control; stereo cards demux CA on the sound expansion connector. Whether the card taps before the volume control is [recollection — unverified].

**Fork and upstream**
- **The fork's pipeline is wrong** (soundglu.cpp:142-168, 194-201). It alternates between starting and completing a read, and completes against the live (already incremented) address. An auto-increment run returns stale, d[A+1], d[A+1], d[A+3], and so on, which contradicts the ERS.
- Upstream implements the one-access lag with a latched address (up:soundglu.cpp:272-291) and clears the high address byte (up:339-341).
- The fork snapshot drops the busy deadline (soundglu.cpp:444).

**Neither models:** busy-bit timing, measured volume readback, or side-effect-free debugger reads (MAME guards `$C03D` with `side_effects_disabled()`).

**Ideal**
- (a) A one-access latch. The address is latched at the priming read. Busy is modeled from DOC access timing only if a hardware test shows it; otherwise it reads a constant 0.
- (b) The latch, latched address and any busy deadline are held as absolute 14M values.
- (c) `glu.peek()`. Events: `glu.prime{addr}`, `glu.consume{addr,val}`, `glu.vol{n}`.

**Steps:** port the 6eefad52 pipeline and add the latch to the fork snapshot (S, low; biggest guest-visible GLU fix). Then the `$C03C` high-byte clear and readback, after testing (S, low).

**Validation:** the ROM sound RAM self-test, NinjaTrackerPlus (it changes `$C03E` between the priming read and the data read, per up:soundglu.hpp:41-43), and a test program that auto-increment-reads 256 bytes of known RAM.

## 3. Output path, volume and audio probe

**Hardware:** the analog DOC output is time-multiplexed; the motherboard low-pass filters it, applies the volume nibble and mixes in the speaker. Card outputs follow §2 [recollection — unverified; filter corner unknown].

**Fork and upstream**
- **Fork:** catch-up on every access plus a per-video-cycle handler (soundglu.cpp:79-136, 399-401). It clamps the backlog and discards the excess 14M time (107-118; upstream fixed this at up:174-181). It applies volume as SDL gain at playback time (238; AudioSystem.cpp:154-160). The probe sees the stream after the mono-mirror heuristic (125-131), so it observes host policy, not hardware output.
- **Upstream:** stages samples and linearly resamples once per frame, trimming the ratio by SDL queue depth (up:67-128). This is host-only, so acceptable. DOC volume is disabled (up:194-219), and there is no probe.

**Neither:** an emulated-time audio bus that is independent of the host, a motherboard mono mix separate from the stereo-card outputs, a speaker probe, or a Mockingboard probe.

**Ideal:** (a) devices write 14M-stamped frames to an **emulated audio bus** with taps `doc.card.L/R`, `mb.mono` (DOC plus speaker, times volume) and `mockingboard.L/R`; mono mirroring becomes an explicit output profile (stereo card none/installed), not a buffer rewrite. (b) The host renderer only reads the bus: resampling, drift trim, pause, device changes, a missing device and pace never touch emulation, and a null sink behaves identically. (c) The probe sits on taps, per source and channel, readable by 14M window and included in snapshots.

**Steps:** the bus, with host rendering from upstream's resampler (M, medium); volume at emulated time on the mono tap (S, low); probe on taps with speaker and Mockingboard sources (S, low).

**Validation:** identical `audio` totals across fast/realtime pace, no audio device, and stepped vs run; Alien Mind (stereo) and a mono title under both profiles.

## 4. Speaker ($C030)

**Hardware:** any access to `$C030` (read or write) toggles the flip-flop; the IIgs decodes only `$C030`. The level scales with the GLU volume nibble at toggle time [emulator: MAME apple2gs.cpp `level_w(m_sndglu_ctrl&0xf)`].

**Fork and upstream:** toggles are stamped in 14M with the current volume (speaker.cpp:110-120), which is correct. Rendering is skipped while the SDL queue holds 4410 samples or more (87-107), and a skew reset drops events (57-66). What gets rendered therefore depends on how fast the host drains the queue. The fork adds toggle counters (113-115). A read returns the floating bus (122-127).

**Neither:** unconditional rendering, a snapshot of pending events and filter state, a probe tap, or a side-effect-free peek (a debugger read of `$C030` toggles today).

**Ideal:** render every toggle to the bus in emulated time; `speaker.toggle{c14m,vol}` event; peek does not toggle. **Steps:** move rendering to the bus (S, low). **Validation:** a square-wave test of fixed period comparing bus sample edges with toggle timestamps (existing spkcheck logic, run outside `gates.ps1`).

## 5. Mockingboard (6522 VIA, AY-3-8910)

**Hardware**
- 6522 [recollection — unverified; R6522 datasheet not read]: a write to T1C-H loads the counter and clears T1IF. A one-shot fires N+1.5 cycles later; the free-run period is N+2. Reading T1C-L clears T1IF. T2 is one-shot and keeps decrementing; only a T2C-H write re-arms it. An IER write changes enables only. The chip is clocked by slot Φ2.
- AY [doc: Docs/MockingboardBugAnalysis.md; recollection for the formulas]: tone = f/(16·TP), with TP=0 behaving as 1. Output = (tone|tone_dis)&(noise|noise_dis) × level; both disabled gives a DC level, which sample playback uses. Noise comes from a 17-bit LFSR.

**Both trees (identical):** an IER write reschedules both timers (mb.cpp:456,464), and EventTimer replaces events by ID, so the timer phase moves. Free-run reschedules at now+N+1 while the counter view adds N (262, 474-496), so IRQ timing and T1C readback drift apart. A T1L-H write clears IFR (365-366). The tone toggles twice per period (AY8910.hpp:336-342); both-disabled and TP=0 are silent (552). Writes are stamped in `double` seconds (mb.cpp:341,350), an AY read calls `printf` (357), and there is no snapshot or probe.

**Neither:** exact counters, PB6 pulse counting, SR modes, or handshake lines.

**Ideal:** (a) counters as absolute Φ2 deadlines with datasheet reload timing, AY per the bug analysis; (b) integer 14M timestamps; (c) `via.ifr{chip,bits}` and `ay.reg{chip,r,v}` events, a peek that does not clear IFR, full snapshot.

**Steps:** 6522 timing (M, medium); AY mixer, tone and period-0 fixes (S, low); snapshot (S, low).

**Validation:** mb-audit (the `N6522.hpp-worksbutdoesntpassaudit` file shows it is already in use), Ultima V, Skyfox, and a pinned dual-card config (`assets/gs2/DualMockingboard.gs2`, upstream).

## 6. Master clock and frame loop

**Hardware**
- The 14M master is 14.31818 MHz. PH0 = 14 14M ticks, with every 65th cycle +2: 912 ticks per line, 1.02048 MHz average [doc: Empson "cpucycles"].
- IIgs fast PH2 = 5 14M ticks, stretched to sync with PH0 on Mega II access. Fast RAM refresh steals cycles, for ~2.5 MHz effective [doc: comp.sys.apple2.programmer "GS memory expansion timing"]. The exact refresh cadence is [recollection — unverified].

**Fork**
- The IIgs cost model is SYNC = 14+(14−phase), fast = 5, and +5 per 50 14M ticks of fast cycles (NClock.hpp:304-384). A sync cycle ignores alignment with the long cycle.
- In ludicrous mode, `c_14M` is frozen (223-226) and advanced by a whole frame at the end (gs2.cpp:543). DOC IRQs bunch at frame end, and the CPU count per frame depends on the host.

**Upstream:** N CPU cycles per 14M tick (up:NClock.hpp:280-290,352) is a coherent "faster CPU", but N is calibrated from the wall clock (up:gs2.cpp:411-520).

**Both:** the one-second IRQ has a wall-clock phase and drifts, because it reschedules from the callback's own time (display.cpp:746-754).

**Ideal:** (a) one 14M counter from which every clock derives (PH0 with long cycles, verified PH2 sync and refresh, DOC 7M/8, RTC 1 Hz); (b) wall time only paces frames, and ludicrous N is a pinned run input that calibration may propose and log but never change in a pinned run; (c) snapshot of the clock and all event queues (retiring the `clock` exclusion), with `clock.mode`, `clock.N` and `frame.end` events.

**Steps:** derive the one-second IRQ from the RTC tick (§7) (S, low); port e169b2bc with N pinned (M, high: hot path); snapshot the event queues (M, medium); measure refresh cadence (S, evidence-gated).

**Validation:** a VBL-counting delay loop at 1 MHz and 2.8 MHz against real-IIgs goldens; stepped vs run hash equality.

## 7. RTC ($C033/$C034) and battery RAM

**Hardware**
- The chip is the Mac-family RTC. `$C034` b7 is start/busy (cleared when the serial transfer ends), b6 read, b5 chip enable/last byte, b3-0 border.
- Commands: `z0000001…z0001101` seconds (LSB first); `00110001` test; `00110101` write-protect; `z010ab01` BRAM `$10-$13`; `z1abcd01` BRAM `$00-$0F`; `z0111abc 0defgh00` 256 bytes.
- ROM ReadTime rereads until two passes match; WriteTime verifies by reading back. The BRAM checksum lives at `$FC-$FF` [doc: Neil Parker, "Using the IIGS Battery-Backed-Up RAM and Clock", in Docs/GSClockBatteryRAM.md].
- MAME shifts 8 bits on a b7 write and drops CE when b5=0. The chip's 1 Hz output drives the VGC one-second IRQ [emulator: MAME apple2gs.cpp `process_clock`, `rtc_vgc`].

**Fork:** `A2GSPU_FAKETIME` overwrites `seconds` on every command (RTC.hpp:232-237), so guest writes never stick, ROM WriteTime cannot verify, and Control Panel set-time fails: the pin itself is guest-visible. `z010ab01` maps to BRAM `$00-$03`, not `$10-$13` (317-326). Test and WP registers are ignored. Busy clears after two `$C034` reads (219, 355-362), not after elapsed time. The BRAM pin keeps input separate from output (94-118), which is good.

**Upstream:** BRAM file keyed by machine UUID and always written back (up:rtc_pram.cpp:47-66), no time pin. **Neither:** a ticking seconds counter in emulated time, a coherent 1 Hz source, or an RTC snapshot.

**Ideal**
- (a) A serial chip model with all registers, WP, and busy for a modeled transfer time.
- (b) A `TimeSource` latched once at power-on (host now, or `A2GSPU_FAKETIME` as the **origin**; `frozen` is explicit opt-in). Seconds = origin + offset + ⌊(c14m−c14m₀)/14318180⌋, and the same edge raises the VGC IRQ. BRAM input is a pin, a per-config file (upstream keying) or the default; output is a separate path, never the pin; the manifest carries both hashes.
- (c) Snapshot of BRAM, seconds, sub-second phase, serial state and WP. Events: `rtc.cmd{cmd,addr,val,rw}`, `rtc.tick`.

**Steps:** ticking pinned time plus the RTC-driven one-second IRQ (M, medium: re-bless); fix the alias, WP and test registers (S, low); timed busy (S, low); per-config BRAM input policy alongside the pin (S, low); snapshot (S, low).

**Validation:** Control Panel set-time then readback; ReadTime stability; Parker's checksum; GS/OS clock display after 600 frames under FAKETIME, which should advance by exactly 10 s.

## 8. ThunderClock Plus (uPD1990AC)

**Hardware**
- STB latches C0-C2: 0 hold (DATA OUT 1 Hz, TP 64 Hz), 1 shift (a CLK rising edge shifts 40 bits; DATA OUT = LSB), 2 time set (loads, stops counting, resets the sub-second divider), 3 time read, 4/5/6 TP 64/256/2048 Hz, 7 **test** on the 1990A (4096 Hz exists only on the 4990A) [emulator: MAME upd1990a.cpp].
- Fields: BCD s/m/h/day, 4-bit weekday, binary month, no year.
- Card: `$C0n0` write b0 DI, b1 CLK, b2 STB, b5-3 C; read b7 DATA OUT [emulator: MAME a2thunderclock.cpp]. The IRQ enable, flag and TP bits come from upstream bd4ca81e and are [recollection — unverified against the manual].

**Fork:** global state, host `localtime` that ignores FAKETIME (thunderclockplus.cpp:53-86), falling-edge STB and CLK (106,113), one register decoded (158-159), `fprintf` on every access, no set, TP or IRQ.

**Upstream:** full protocol with an emulated-time 1 Hz and TP (up:238-295) and IRQ, but host time (up:73), shifting also in TIME_READ (up:345), cmd 7 = 4096 Hz (up:40), and a read that clears the IRQ (up:327).

**Ideal:** port bd4ca81e; take time from the §7 TimeSource; shift only in SHIFT; cmd 7 = test; peek without clearing the IRQ; snapshot; `tcp.cmd` events. **Steps:** port plus pin (S, low; required before any TCP run is reproducible). **Validation:** upstream `clients/python/examples/test_thunderclock.py`, the ProDOS TCP driver date, and a TP IRQ count per second.

---

## Fork gates that shift

- `unarmedhash`: the `iigs-rom` and `iigs-gsos601` rows (hash, cycles, run) move with the GLU pipeline, `$E0`, the RTC-driven one-second IRQ, ticking FAKETIME, or clock-cost changes; `iie-rom` moves only with II clock changes.
- `dualsmoke`/`assertparity` move only with cycle costs; `railparity` must register new rails.
- The spkcheck, periphcheck and audio checks run outside `gates.ps1` (a2engine gscheck) and need the bus-tap form.

## Prioritised summary

| # | Change | Size/risk | Why first |
|---|---|---|---|
| 1 | Remove the `!stream` and execution-mode gates; drop the SDL call from `$E1` | S/low | Host and debugger state currently change guest IRQs |
| 2 | Port the `$C03D` one-access pipeline with latched address | S/low | Fork reads return wrong bytes (ROM self-test, trackers) |
| 3 | Port the `$E0` read and the `newCtrl` halt rule | S/low | Spurious IRQs; Bard's Tale crash class |
| 4 | TimeSource: FAKETIME as a ticking origin, RTC 1 Hz drives the VGC IRQ, ThunderClock on the same source | M/medium | Removes every unpinned wall-clock input; fixes set-time under the pin |
| 5 | Emulated audio bus, probe on hardware taps, host renderer from upstream | M/medium | Probe observes hardware output, not host policy |
| 6 | Snapshots: GLU latch and phase, RTC, clock and event queues, Mockingboard, speaker, ThunderClock | M/medium | Restore today is not exact |
| 7 | 6522 timing and AY mixer, tone and period-0 fixes | M/medium | Mockingboard music and timing |
| 8 | Ludicrous N as a pinned input (e169b2bc shape) | M/high | Determinism in fast runs |
| 9 | Evidence-gated DOC quirks (wrap rule, slot timing, swap 0x80, IRQ order, `$E1`/`$E2`, volume readback, busy timing) | S each/low | Needs the hardware test programs from §1-2 first |
