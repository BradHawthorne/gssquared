# Slot bus, from the card's side of the connector (IIgs slots 1–7)

Fork = `ed2c00fc`, upstream = `upstream/main` (`d380e055`). Everything was read with `git show`.

**Evidence tags.**
- **[HWR p/T]**: *Apple IIgs Hardware Reference*, 2nd ed., printed page or table. Chapter 8 was read as the archive.org OCR text, so a few digits are degraded where marked.
- **[TN68] [TN32] [TN30]**: the Apple IIgs Technical Notes.
- **[FWR]**: *Apple IIgs Firmware Reference*, archive.org OCR.
- **[SCH n/9]**: the ROM 3 "1 MB schematic diagram, Part n of 9".
- **[CPLD: file]**: the Apple IIe MMU/HAL reimplementation zips.
- **[MAME] [Clemens]**: those emulators' card headers, which I fetched and read.
- **[derived]**: a logical consequence of the cited sources that no source states outright.
- **[recollection — unverified]**: not checked against any source.
- **`f:` / `u:`**: fork or upstream `file:line`.

---

## 1. What the card actually sees

### 1.1 One 1.024 MHz (Mega II) cycle, about 980 ns

| Phase | What is on the connector | Evidence |
|---|---|---|
| φ0 low (≥480 ns) | Video data is driven **outward** onto D7–D0 for the display fetch. | [TN68 "Bus Fights"] |
| φ0↓ + ≤100 ns | A15–A0 and R/W are valid; they hold ≥15 ns past the cycle. | [HWR T8-9] |
| φ0↓ + ≤160 ns | /M2SEL goes low. | [HWR T8-9] |
| φ0↓ + 140…280 ns | M2B0 is driven (slot 3 only); latch it on Q3↓. | [TN68 Fig 1] |
| φ0↓ + ≤100/120 ns | A card asserts or releases /DMA, and its DMA address must be stable by then. | [TN68 p5] |
| φ0 high | /DEVSEL, /IOSEL and /IOSTRB are asserted **only while φ0 is high**. | [HWR p177] |
| φ0↑ | Read data: start driving at the first 7M↑ after φ0↑ (the buffers turn around in ≤30 ns). Data must be set up 140 ns before φ0↓, held 10 ns, and released as soon as possible. Write data is valid ≤30 ns after φ0↑ and held 30 ns. | [TN68 Fig 3; HWR T8-9] |
| ≥60 ns before φ0↓ | /IRQ, /NMI, /RST and RDY must be stable to be recognised on the following cycle. The CPU samples them at PH2↓, and PH0 and PH2 can be skewed by up to 25 ns. | [TN68 Fig 2] |

**Clocks.** φ0 low and high are each ≥480 ns. Q3 is high ≥270 ns and low ≥200 ns; 7M high ≥60 ns [HWR T8-8]. All slot clocks are re-buffered, and delayed, by the Slotmaker IC [HWR p195; SCH 7/9: UC10 outputs B7M, BQ3, BPH0, BPH1].

**The long cycle.** On the IIe every 65th φ0 cycle is stretched by two 14M half-periods [CPLD: TIMING_HAL `3410170A (ABEL).txt`, the PHASE_0/RAS terms on H0·CREF]. That the IIgs Mega II does the same is [recollection — unverified]; the fork models it as +2 c14M per scanline [f: NClock.hpp:72,267-270].

### 1.2 The signals

- **/DEVSEL (pin 41)**
  - Asserted for `$C0n0-$C0nF`, where n is the slot [HWR T8-1, T8-3]. The Slotmaker generates DEV1–7 [SCH 7/9].
  - For slots 1, 2, 5, 6 and 7, bit n of `$C02D` = 1 enables *both* the card's `$Cn00` ROM and its `$C0n0` I/O; bit = 0 selects the built-in device [HWR T8-2].
  - Slot 3's I/O is always enabled [HWR p181]. The slot 4 I/O footnote is OCR-degraded; I read it as always enabled too.
  - For slot 6 the Slotmaker steers `$C0Ex` to INTDEV6, which is the IWM's DEV input, instead of to DEV6 [SCH 7/9 → 4/9].
- **/IOSEL (pin 1)**
  - Asserted for `$Cn00-$CnFF` [HWR T8-1].
  - A slot set to Internal ROM asserts no /M2SEL and runs at 2.8 MHz [TN68 p2]. The card therefore gets no /IOSEL for that access [derived].
- **/IOSTRB (pin 20)**
  - Asserted for `$C800-$CFFF`. It is one line shared by all slots [HWR T8-1; SCH 7/9: a single IOSTRB.L].
- **Expansion-ROM ownership lives on each card, not in the motherboard** [HWR pp185-186].
  - The card sets a flip-flop on /IOSEL and enables its ROM on flip-flop · /IOSTRB. An access to `$CFFF` clears it; a card may decode only `$CFxx` and give up the top page.
  - "Only one card active at a time" is a convention, not enforced. Firmware writes `$Cn` to MSLOT (`$07F8`) before `$CFFF` so the interrupt handler can restore the card's ROM [HWR p185; FWR interrupt handler note].
  - IIe comparison: the motherboard's INTC8 flip-flop is set by a `$C3xx` access during φ0 with SLOTC3ROM reset, and cleared by a full `$CFFF` decode qualified by φ0 or by power-on [CPLD: MMU_INTERNALS.vhdl, MMU_ADDR_DECODER.vhdl `MCFFF_N`]. The data buffer turns inward only for reads of a peripheral `$Cxxx` address, so no card can drive a read under INTCXROM or INTC8 [CPLD: MMU_CXXXOUT.vhdl, MMU_RW245.vhdl].
- **/INH (pin 32)** is an **output from the card**.
  - Pulling it low during φ1 disables motherboard memory except `$C000-$CFFF` [HWR T8-1, p177].
  - The FPI ignores it when running at 2.8 MHz, or in a bank without I/O and language-card mapping. It must be asserted within 200 ns of φ0↓, and the machine then runs that access as a slow cycle [HWR p177, T8-10].
  - Because the IIgs interleaves its DRAM, /INH is safe only over some logical address ranges [TN32].
- **/M2SEL (pin 39)** replaces the IIe's µPSYNC.
  - It is asserted for:
    - `$E0` and `$E1`
    - shadowed video writes
    - internal I/O and the slots.
  - It is **not** asserted for:
    - slot ROM set to Internal
    - `$C035`, `$C036` and `$C037`
    - reads of `$C02D` and `$C068`.
  - [TN68 p2; HWR p176]
- **M2B0 (pin 35, slot 3 only; CREF on slot 7)** is the CPU's or DMA's bank LSB.
  - It is high for any access to `$E1` and for a shadowed write or I/O access *through bank `$01`*.
  - It does **not** reflect RAMRD, RAMWRT, ALTZP, 80STORE or PAGE2 [TN68 p2].
  - A shadowed write is therefore **one** /M2SEL cycle carrying the CPU's bank bit [TN68].
- **/IRQ (30), /NMI (29), /RST (31)** are open-collector [TN68].
- **Daisy chains.** INT IN/OUT are pins 28/23 and DMA IN/OUT are pins 27/24. The OUT pins on slot 7 are not connected [HWR T8-1 (OCR); "Interrupt and DMA daisy chains"].
- **RDY (21)** halts the 65C816 with the address held.
  - On the IIgs a card may change it only while /M2SEL is low and PH0 high, or while /DEVSEL, /IOSEL or /IOSTRB is active. Otherwise the FPI can latch a bad bank byte. It must be driven open-collector [TN68 p3].
- **/DMA (22)** disconnects the CPU from the address bus; the bank comes from `$C037` [HWR p176; TN30], and the FPI asserts /M2SEL for Mega II or slot targets [HWR p176].
  - At 2.8 MHz only fast RAM and ROM work, and the CPU may repeat a cycle; I/O and video need 1.024 MHz [HWR p177, p197]. The CPU may be held off indefinitely [TN68; HWR p177].
  - Bug: /DMA during a vector pull makes DMA reads of `$D000-$FFFF` return ROM [TN68 p5]. Every slot has the pin; priority comes only from the daisy chain [HWR].
- **SYNC (pin 19) and CREF** are present on slot 7 only [HWR T8-1].

### 1.3 Speed, and what a card can see of other cycles

- **Slow (1.024 MHz):**
  - I/O reads and writes
  - reads of slot firmware in `$Cxxx`
  - the soft switches and slot I/O `$C090-$C0FF`
  - `$E0` and `$E1`
  - shadowed video writes
  - inhibited memory.
- **Fast:**
  - the FPI registers `$C035-$C037`
  - reads of `$C02D` and `$C068` (writes to them are slow)
  - the interrupt ROM `$C071-$C07F`
  - slot ROM set to Internal.
- **Evidence and cost:** [HWR ch.2 "The Mega II cycle", "Speed"; TN68]. An isolated slow cycle averages about 1.5 µs; consecutive slow cycles take 980 ns [TN68 T1].
- **Motor-on detectors.** `$C036` bits 0–3 cover slots 4–7: an access to `$C0n9` drops the machine to 1.024 MHz until `$C0n8` is accessed [HWR T2-2].
- **Address visibility.** In fast mode the slot address bus is not valid for the whole φ0 cycle, so /M2SEL qualifies it. Phantom-slotting cards must decode with it [HWR p196].
- **Data visibility.** There are three data buses: DBUS (CPU, FPI, fast RAM), MDBUS (Mega II side) and SDBUS (slots) [HWR Fig 8-3]. SDBUS↔MDBUS is an HCT245 enabled by the Slotmaker's EN245 [SCH 7/9]; MDBUS↔DBUS is a 245 gated by the FPI's DBENB [SCH 1/9, 2/9]. A card therefore sees data only on Mega II-side cycles, and only a transient address on fast cycles [derived].
- **Floating bus.** On a slot read no card drives, so SDBUS keeps the video byte driven during φ0 low [derived from TN68 bus-fight text]. That this matches the IIe's floating-bus mechanism is [recollection — Sather, unverified on IIgs].

---

## 2. How the fork and upstream model it

### 2.1 The card API both trees share

- **Registration:** a `power_on(computer, slot)` callback [f: devices.hpp:32-40]; `SlotManager` is only a pointer table [f: slots.hpp:8-22].
- **`$C0nX`:** at most two handlers per address, reads ORed, floating bus if none [f: mmu_ii.cpp:212-239, 301-331]. **`$Cn` ROM:** a page pointer or page handler [f: mmu_ii.cpp:95-165].
- **`$C800`:** a handler runs when a `$C1xx-$C7xx` access differs from the single global `C8xx_slot`; `$CFFF` resets the map *before* that byte is served [f: mmu_ii.cpp:173-179, 246-250, 286-289]; the IIe skips the release while INTCXROM is on [f: mmu_iie.cpp:37-41].
- **IRQ:** a level bitfield per slot [f: InterruptController.hpp:50-81], sampled through a two-cycle pipe [f: base_6502.cpp:76, 2264]. **Time:** an EventTimer in c14M, processed **between instructions** [f: gs2.cpp:387-404].

### 2.2 What a card cannot observe or do, in either tree

1. **`$C02D` gating is ignored.** `$C0nX` handlers are dispatched whatever the slot register says [f: mmu_ii.cpp:212-239]. A card in slot 6 and the IWM would both answer. Only the ROM page follows `reg_slot` [f: mmu_iie.cpp:43-66].
2. **Phantom /IOSEL.** A card's C8 handler is called on `$Cn` accesses even when `$C02D` or INTCXROM selects internal ROM [f: mmu_ii.cpp:246-248]. That contradicts [TN68; HWR T8-2].
3. **`$C800` ownership is centralised and exclusive.**
   - A card has no flip-flop of its own, and never sees the `$CFFF` or `$CFxx` event.
   - Two enabled cards (contention) cannot occur.
   - The release timing is wrong, and the release can be suppressed while INTCXROM is on.
4. **No sub-cycle view.** A handler runs somewhere inside the access, before the cycle is counted [f: base_6502.cpp:129-137]. It sees no φ0, Q3 or 7M phase, no /M2SEL and no M2B0.
5. **Card outputs are missing.**
   - RDY, /INH, /DMA, card-driven /RESET and the daisy chains do not exist.
   - The core never dispatches NMI [f: iigs_toolbox.hpp:1151-1165].
   - Upstream stores `$C037` and nothing more [u: mmu_iigs.cpp:480-490; mmu_iigs.hpp:224-225].
6. **Speed.**
   - Every `$C0xx-$CFxx` access is SYNC, internal ROM pages included [f: mmu_iigs.cpp:556-560; u: :569].
   - The fork also makes the FPI registers and `$C071-$C07F` slow; upstream fixed those [u: mmu_iigs.cpp:478-490 and the related handlers].
   - The motor-on detectors are defined as flags and never used [f: iigs_shadow_flags.hpp:16-19].
7. **Dummy PC reads are dropped.** They are counted as cycles but produce no bus access (`full_phantom_reads=false`) [f: cpu_traits.hpp:34; base_6502.cpp:171-177]. A trace comparison will miss them, and so will an address-only decoder.
8. **Late IRQ.** An IRQ raised from a timer event is late by up to one instruction [derived from gs2.cpp:387-404].
9. **Floating bus.** It is one value, set by the video scanner *after* each cycle [f: VideoScannerIIgs.cpp:151; NClock.hpp:258-260]. A read in cycle N returns the byte from cycle N−1. Whether that phase is right is unverified.

### 2.3 The fork's slot-bus stream

The stream records `{cycle, A15-0, data, ctl}` per cycle [f: slot_bus.hpp:24-40].

- **a. Hard-wired to slot 3.** /DEVSEL and /IOSEL are decoded for slot 3 whichever slot the card is in [f: slot_bus.hpp:44; mmu_iigs.hpp:167-178].
- **b. /IOSEL and /IOSTRB are never set.** Only `$C0xx` accesses are emitted [f: mmu_iigs.hpp:94-104]; `$C100-$CFFF` never reaches the stream.
- **c. M2B0 is taken from the aux-resolved address.** RAMWRT and 80STORE are applied first [f: mmu_iigs.cpp:616-622 → mmu_iigs.hpp:142-157], which contradicts [TN68]. `$E1` writes with the bank latch off emit M2B0=0 [f: mmu_iigs.cpp:53-55], though TN68 says M2B0 is high.
- **d. Missing cycles.** `$E0` writes [f: mmu_iigs.cpp:22-28] and Mega II RAM reads are never emitted, although a card sees both qualified by /M2SEL.
- **e. `INH` is set when IOLC shadowing is off** [f: mmu_iigs.hpp:171]. Real /INH is driven by a card; IOLC state has nothing to do with it.
- **f. `M2SEL` is always 1** [f: slot_bus.hpp:34]. The stream therefore cannot record addresses that are on the pins without /M2SEL, which in 1 MHz mode is every cycle [HWR p196]. The header's claim that only 1 MHz cycles are visible [f: slot_bus.hpp:8-10] holds for data, not for addresses.
- **g. A question already answered.** bus_trace.hpp:77-85 leaves the visibility of shadowed writes open. TN68 settles it: one cycle, with M2B0 equal to the CPU bank LSB.
- **h. The slot subsystem ID is unused.** `OBS_SUB_SLOT` is declared and never emitted [f: obs_signal.hpp:60].

### 2.4 The fork's bus-watching card model

- **Faithful part.** The card is one write-only register at `$C0nF` with no read handler, so reads float, just as on a card that never drives D7–D0 [f: devices/a2gspu/a2gspu.cpp:1173-1177; a2gspu_cmdreg.hpp:5-12].
- **Divergences from the physical card:**
  - It is fed the defective stream from §2.3, filtered through a host-side whitelist [f: a2gspu.cpp:61-77]. The physical card's pins would see a superset.
  - "16 commands per video frame" is host policy, not bus behaviour [f: a2gspu_cmdreg.hpp:20-21, 142].
  - The legacy model uses a host write-observer on `$C0-$CF` writes, which is not faithful to the slot [f: mmu_iigs.cpp:596-599].

### 2.5 Upstream's newer cards

- **ThunderClock Plus:** real ROM, the whole 2 KB image at `$C800` through the C8 handler [u: thunderclockplus.cpp:356-371, 411-419]. It reads the host's `time(nullptr)`, so it is non-deterministic [u: :73-98].
- **Apple Mouse card:** real `342-0270-C` ROM, with PIA port B selecting the ROM bank [u: applemouseiii.cpp:17-26]. The MCU is a C++ controller [u: MouseController.hpp:23-120], PIA IRQs are unused [u: PIA6520.hpp:31], and VBL comes from frame arithmetic [u: :37-63]. Registers are faithful; MCU handshake timing is not.
- **Super Serial Card:** real `341-0065` ROM; the `$Cn` page is offset `$700`, and the whole image maps at `$C800` [u: ssc.cpp:67-75, 162-167].
- **Uthernet II:** the four W5100 registers repeat across `$C0n0-F` [u: uthernet2.cpp:63-68]; the network runs on a worker thread, which is non-deterministic [u: :55-60].
- **The fork's mouse** is paravirtual: a custom ROM driven by SDL events [f: mouse.cpp:295-331].

---

## 3. What neither tree models

Everything in §2.2 items 1–7, and also:
- a `$C800` flip-flop on each card, with contention, partial `$CFxx` decode and clearing on reset;
- /INH, RDY, /DMA (the `$C037` bank, the 1 MHz rule, the vector-pull bug), /NMI dispatch, card-driven /RESET, and the daisy chains;
- M2B0 per [TN68], and /M2SEL as a signal a card can see;
- phases, the card-side clocks (Q3, 7M), and data-valid windows;
- the phase of the floating bus relative to the video fetch;
- a bus export that can be compared with a logic-analyzer capture.

Secondary evidence, for comparison:
- MAME's card interface already has C0nX, CnXX and C800 access with `take_c800()`, `inh_*`, and slot IRQ, NMI and DMA lines plus a DMA bank [MAME: a2bus.h:74-177].
- Clemens has per-access DEVSEL and CARD flags, `io_sync` reporting IRQ, NMI and DMA, and a per-cycle `io_dma` [Clemens: clem_shared.h:90-136].
- Neither of them models phases or exports the stream.

---

## 4. The ideal card-emulation interface

### (a) Signals at cycle granularity

A central `SlotBus` decodes every CPU or DMA cycle once and presents each card with the view its own connector would have:

```c
struct SlotPins {              // one cycle, as THIS connector sees it
  uint64_t cycle, t14m_phi0_fall, t14m_phi0_rise, t14m_end; // phases; Q3/7M edges derivable
  uint16_t a; uint8_t rw;      // A15-0 and R/W (valid flag: 1 MHz-synced or not)
  uint8_t m2sel, m2b0;         // m2b0 only on slot 3; else floating
  uint8_t devsel, iosel, iostrb;
  uint8_t d;                   // write: CPU data; read: bus value before any card drives
  uint8_t kind;                // FAST | SYNC_ISOLATED | SYNC | REFRESH | DMA
};
struct SlotDrive { uint8_t drive_d, d, irq, nmi, rdy_low, inh, dma, rst, int_out, dma_out; };
typedef struct {
  void (*cycle)(void *ctx, const struct SlotPins *in, struct SlotDrive *out);
  uint32_t (*wants)(void *ctx);   // DEVSEL|IOSEL|IOSTRB|M2SEL|ALL — the decode table is built from these
  void (*reset_pin)(void *ctx, int asserted);
} SlotCardOps;
```

- **The bus owns motherboard behaviour:** `$C02D` gating and Slotmaker steering, the speed class, /M2SEL and M2B0 per [TN68], daisy chains, RDY holds, /INH and DMA arbitration.
- **Cards own card behaviour, including their `$C800` flip-flop.** The bus gathers every card's `SlotDrive`; two cards driving at once is recorded as contention, not silently resolved.
- **Phase is a field, not a callback,** so it costs nothing unless read. Card-driven pins are sampled by the CPU pipe at the cycle boundary.

### (b) Firmware and hardware together

- A card's hardware state machine is plain C over `SlotPins` and `SlotDrive`, with no host calls, so the same source can run inside the physical card's bus processor, fed from real pin samples.
- Card firmware (6502/65816 ROM) is only bytes behind /IOSEL and /IOSTRB.
- A card's own microcontroller is either emulated or reached through an explicit, cycle-stamped port.

### (c) A canonical event stream

- **One record per cycle:** `{cycle, t14m, kind/speed, A15-0, bank (internal-only), R/W, D, driver ∈ {CPU, MOTHERBOARD, SLOTn, FLOAT, CONTENTION}, /M2SEL, M2B0, per-slot DEVSEL/IOSEL, IOSTRB, IRQ, NMI, RDY, INH, DMA}`. Fields are marked BUS_OBSERVABLE or INTERNAL (bank, driver).
- **Logic-analyzer comparison:** capture at ≥28 MHz. Extract cycles at φ0↓; sample A and R/W at φ0↑+20 ns, D at φ0↓−20 ns, and M2B0 at Q3↓. Align on the reset-vector fetch (`$FFFC`/`$FFFD`). Compare observable fields only; A and D are don't-care on fast cycles without /M2SEL. Report the first divergence with ±N cycles of context.

### (d) Determinism

- Card time is bus cycles and 14M ticks only.
- Host inputs (network, wall clock, mouse) enter through a cycle-stamped log that can be replayed.
- No card code runs on another thread.
- EventTimers that drive a pin fire at cycle granularity.

### (e) Instrumentation

- **Per card:** `describe()` (a read-only view of registers, the C800 flip-flop and pending IRQ), `save`/`load`, and `peek(reg)`, which never calls `cycle()`.
- **Rail events:** STROBE (per-slot DEVSEL/IOSEL/IOSTRB edges), C800 (set, clear, contention), IRQ/NMI/RDY/INH/DMA edges, SPEED (sync start, motor detect), DRIVE (who drove a read). Plus a ring buffer of the last N `SlotPins`.

---

## 5. Implementation steps, ordered by value for validating card firmware

| # | Step | Size | Risk |
|---|---|---|---|
| 1 | **Fix the stream.** M2B0 from the CPU bank LSB; emit `$E0` writes, Mega II reads and `$C100-$CFFF` with IOSEL and IOSTRB; decode for the card's real slot; drop the misuse of INH; fill bit 7 with the speed kind. The existing hashes that include `ctl` must be deliberately re-baselined. | S | Low |
| 2 | **`$C02D` gating** of `$C0nX` dispatch and of the C8 handler call, always-enabled I/O for slots 3 and 4, and fast cycles for internal slot ROM (plus upstream's FPI register speed fix). | S–M | Med (slot 6 and IWM, ROM slot scan, cycle gates) |
| 3 | **`SlotBus` + `SlotCardOps`**, with an adapter that wraps today's handler-style cards unchanged. | M–L | Med |
| 4 | **C800 flip-flop inside the card**, with contention detection. The adapter defaults to a full `$CFFF` decode, and `$CFxx` is an option. Release happens *after* the `$CFFF` byte is served. | M | Low–Med |
| 5 | **Card-driven pins:** IRQ at cycle granularity, NMI dispatch in the 65816 core, RDY hold, /INH, /DMA with `$C037` and its rules, and the daisy chains. | L | High (CPU core) |
| 6 | **Put dummy reads on the bus** (`full_phantom_reads` on the IIgs, at least while a tap is armed). | S | Med (cycle goldens, I/O side effects) |
| 7 | **Capture format, extractor and comparator** as a standalone tool. | M | Low |
| 8 | **Motor-on detect slowdown**; check the phase of the floating bus against a capture. | S | Low–Med |
| 9 | **Rails and `describe()`/snapshot** for every card; emit `OBS_SUB_SLOT`. | M | Low |
| 10 | **Determinism before porting upstream cards:** a time pin for the ThunderClock, a replayable log for Uthernet, and a cycle-stamped port for the mouse MCU. | S–M | Low |

**Validation.**

- **Known card ROMs** (SSC `341-0065`, ThunderClock, Apple Mouse `342-0270-C`), run unmodified: the stream must show IOSEL → IOSTRB → `$CFFF` and the MSLOT write in execution order, and the `$C0nX` register sequences must match captures from the real cards.
- **The IIgs ROM's slot scan:** with a slot set to Your Card, expect the scan from slot 7 down [FWR Boot/scan] reading `$Cn01=$20`, `$Cn03=$00`, `$Cn05=$03`, plus `$Cn07=$00` and `$CnFB` for SmartPort [FWR ch.7]. Set to Internal, expect **no** IOSEL or DEVSEL for that slot, and fast cycles. Pascal 1.1 ID bytes are `$Cn05`/`$Cn07`/`$Cn0B`/`$Cn0C` [FWR mouse table; exact values are recollection].
- **A small exerciser ROM** (write and read `$C0nX`, jump through `$CnXX`, claim and release `$C800`, request DMA): run it on the emulator and on a real IIgs, recording slot 3 with an analyzer, or with the fork's bus-watching card sampling `A15-0`, `D7-0`, R/W, /DEVSEL3, /IOSEL3, /IOSTRB, /M2SEL, M2B0, φ0, Q3, 7M, /IRQ, RDY, /INH and /DMA. The two canonical streams must match on every observable field.
