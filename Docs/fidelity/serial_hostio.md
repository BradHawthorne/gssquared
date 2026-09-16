# Serial, parallel and host I/O: the ideal shape

Sources: FORK = `ed2c00fc`, UP = `upstream/main` (read with `git show`).

Evidence tags:
- **[datasheet Z]**: Zilog Z80C30/Z85C30 Product Specification PS011708-0115.
- **[datasheet R]**: Rockwell R65C51 ACIA. The SSC uses an NMOS 6551, so minor differences are possible.
- **[doc P]**: Apple II Parallel Printer Interface Card Installation and Operating Manual 030-0005-01.

I could not reach the IIgs Hardware and Firmware References or the SSC manual. Claims that depend on them are tagged [recollection — unverified].

## 1. Z85C30 SCC (IIgs built-in)

### 1.1 Real hardware
- **Addressing.** $C038/$C039 are the B/A command registers; $C03A/$C03B are the B/A data registers. WR0 D2-0 is the register pointer (Point High adds 8), and it returns to 0 after the next access [datasheet Z].
  - Reading data consumes the byte. The IIgs firmware writes via `STA $BFFD,Y` to avoid a phantom read [doc: Docs/SCC8530_Serial.md, Feb 3 2026].
- **Interrupts [datasheet Z: Interrupts].**
  - Six sources, each with IE, IP and IUS bits. "If the IE bit is not set … the IP for that source is never set." MIE (WR9 D3) gates INT.
  - Priority: channel A over B; within a channel, Receive > Transmit > External/Status.
  - RR3A holds all six IP bits; RR3B reads 0. RR2B is the vector modified by the highest pending IP.
  - With WR9 D5 (software INTACK) set, reading RR2 sets IUS, which then needs a Reset Highest IUS command.
  - Receive modes: first char / all chars / special condition only. Special condition = overrun, framing, optional parity.
  - Ext/Status sources: CTS, DCD and SYNC edges, BRG zero count, break start and end.
  - The "nothing pending" status code is 011 [recollection — unverified].
- **Buffers [datasheet Z].** A 3-byte receive FIFO plus the shift register; one transmit buffer plus the shift register. All Sent (RR1 D0) is distinct from Tx Buffer Empty (RR0 D2).
  - Tx IP sets when the buffer empties after a character. It clears on Reset Tx IP or a data write [doc: Docs/SCC8530_Serial.md, quoting Zilog].
- **Clocking [datasheet Z].** The BRG source is PCLK or RTxC (WR14 D1), enabled by WR14 D0. WR11 routes the Rx and Tx clocks.
  - TC = f / (2 × baud × mode) − 2, and a new TC is loaded at the next counter reload.
  - IIgs: TC $2E at ×16 gives 2400 baud, which implies a 3.6864 MHz input [doc: Docs/SCC8530_Serial.md]. Which pin carries that clock is [recollection — unverified]; check the schematic.
- **Loopback and reset.** Local loopback ties internal TX to RX; CTS and DCD stop acting as enables but still interrupt [datasheet Z].
  - WR9 11 is a hardware reset; 01/10 reset one channel, and the masks differ (reset table cited at FORK Z85C30.hpp:604-669).
  - Ctrl-Reset resets the chip via RD/WR logic [doc: Docs/SCC8530_Serial.md].
- **IIgs port and timing.** The port carries DTR, CTS (HSKi) and DCD (GPi), with no DSR or RI [recollection — unverified].
  - A character lasts (1 + data + parity + stop) bit periods. TBE sets when the byte reaches the shift register; Rx available comes at the stop-bit sample [recollection — unverified].

### 1.2 Fork and upstream
`Z85C30.hpp` is the same in both trees, with two exceptions:
- UP pushes line parameters to the backend (UP :315-346, :478, :490, :861).
- FORK adds an Observatory window (FORK :70-72, scc8530.cpp:64).

Device wiring also differs. FORK hard-wires File on channel A and Modem on channel B (scc8530.cpp:98-101). UP goes through Connections (scc8530.cpp:84-106).

Defects (FORK lines):

| # | Defect | Lines |
|---|---|---|
| S1 | The IRQ line is computed from the accessed channel only, so touching A drops a pending B IRQ | 374-384 |
| S2 | Tx IP is consumed by the next `update_interrupts`, so any RR0 or RR3 read acknowledges it | 318-331 |
| S3 | Only Rx mode 10 exists. Missing: special conditions, Error Reset, Enable Int on Next Rx, Reset Highest IUS, software INTACK | 336, 403-414 |
| S4 | B's IP bits are stored in A's RR3 while the vector reads B's copy; priority coded as Rx>Ext>Tx; status-low mask `&0x0E` corrupts the vector | 327-357, 369 |
| S5 | No External/Status. The RR0 read forces DCD=1 and CTS=1 (a read that writes) | 522-523 |
| S6 | 1-byte receive buffer. The next host byte is pulled only on a guest access, so receive stalls if the ISR exits without one, and arrival time equals access time | 786-811 |
| S7 | The host path never overruns, but loopback overruns after 1 byte | 866-869, 904-909 |
| S8 | All Sent := TBE. A write is dropped when TX is disabled or the buffer is full (real behaviour: [recollection — unverified]) | 530, 723-754 |
| S9 | WR11/WR14 routing and BRG enable are ignored; float math; above 115200 is untimed | 25, 296-316 |
| S10 | Break, DTR and RTS are never sent. UP re-pushes line parameters on every WR3/WR5 write | 456-459; UP :478,:490 |
| S11 | Not snapshotted (`serial` is always in the excluded list) | a2gspu_snapshot.cpp:98 |

### 1.3 Neither models
The FIFO, External/Status, IUS, clock routing, auto-echo, modem lines in either direction, framing and parity errors, deterministic arrival, and side-effect-free peeks.

## 2. Super Serial Card (6551): UP 5e35081a only

### 2.1 Real hardware [datasheet R]
- **Registers.** 0 = data; 1 = status (read) or programmed reset (write); 2 = command; 3 = control. The SSC places them at $C0n8-B, with DIP switches at $C0n1 and $C0n2 (bit 0 = CTS) [recollection — unverified; UP Docs/SSC.md].
- **Resets.**
  - Hardware reset: command = control = 0; status = 0 except the DCD/DSR pins and TDRE = 1.
  - Programmed reset: clears command bits 4-0 and status bit 2 only; control is unchanged.
- **Status and IRQ.** The IRQ bit clears on a status read. DCD/DSR changes interrupt immediately, and the levels latch until the status read.
  - PE, FE and OE never interrupt; they clear on the next data read.
  - On overrun, the old byte is kept and the new byte is lost.
- **Transmit.** TDRE sets when the byte moves to the shift register, and the TX IRQ fires at the start bit.
  - If the CPU does not reload, TxD marks and "IRQ interrupts continue to occur at the same rate".
  - CTS high: the current character finishes, TDRE shows not-empty, and no IRQ is raised.
- **Command and control bits.** Command bit 0 = 0 disables the receiver and all IRQs. Echo mode requires command bits 3-2 = 00.
  - Control bit 4 selects the clock for the **receiver only**.
  - Stop bits: 1.5 for 5-bit with no parity, 1 for 8-bit with parity, otherwise 2.
  - The crystal is 1.8432 MHz.

### 2.2 Upstream defects (`MOS6551.hpp`)
| # | Defect | Lines |
|---|---|---|
| A1 | Programmed reset does a full reset, clearing control | 176-181, 97-115 |
| A2 | TX IRQ fires once per character; it never repeats while idle and never fires on enable | 147-158, 353-371, 388 |
| A3 | DCD/DSR are hard-asserted with no change IRQ; CTS, echo and break are ignored | 102, 193-195 |
| A4 | Control bit 4 = 0 untimes both TX and RX | 241-249 |
| A5 | Missing the "8-bit + parity = 1 stop bit" rule | 273-276, 292-296 |
| A6 | RX is pulled only on guest access; the host path never overruns; PE/FE never set | 308-319 |
| A7 | A status read clears IRQ, but an unrelated later write re-asserts it | 162-174, 382-400 |
| A8 | A write with the transmitter disabled is dropped (real behaviour: [recollection — unverified]) | 136-138 |
| A9 | No snapshot or rails; the MODEM/TERMINAL jumper block is not modelled | ssc.cpp |

## 3. Apple Parallel Interface card
- **Real hardware [doc P].** A write to $C0n0 latches the data and fires STROBE, and sets flip-flop FF2; the ACK edge clears FF2 (both polarities are jumper-selectable).
  - While FF2 is set, the ACK logic alters the PROM address, putting the 256-byte PROM into its "Printer-Not-Available" program.
  - So the firmware waits inside the PROM: the printer throttles the CPU, and with no ACK the machine hangs.
- **The repo image** is 256 bytes, with self-loops `90 FE B0 FE` at $80 and a single `STA $C080,Y` at $84 [test program: byte scan of `roms/cards/parallel/parallel.rom`]. The exact alteration mapping is unverified.
- **FORK** (parallel.cpp:6-28) does `fputc` + `fflush` on the emulator thread. It is deterministic, but blocks on disk and always writes `parallel.out`.
- **UP** (parallel.cpp:9-18) ignores a full 128-entry queue, so bytes are silently lost when the guest outruns the 10 ms worker poll (FileDevice.hpp:161-189).
- **Neither** models STROBE, ACK, FF2 or the PROM alteration; both act like a printer that ACKs instantly.

## 4. Host-connection framework (UP 29b57cba, 93d3d718, 3258974a, fb52cc70, e75e005c)
A registry of jacks (Connections.hpp:166-208) creates one threaded SerialDevice per attachment, each with two 128-entry rings (SerialDevice.hpp:21-80).
- **H1** Ring `head`/`tail` are plain `uint32_t` shared across threads, which is a data race (SerialDevice.hpp:26-27, both trees).
- **H2** The thread starts in the base constructor (SerialDevice.cpp:18), before the derived object exists.
- **H3** `send()==false` is ignored everywhere: SCC (UP :913), 6551 (:356), parallel (:16), Echo (:30), modem replies (ModemDevice.hpp:71). Loss depends on host speed. A dropped MESSAGE_SHUTDOWN hangs the destructor's `SDL_WaitThread` (FileDevice.hpp:115-121 and peers).
- **H4** The modem's dial blocks its worker for ≤40 s (ModemDevice.hpp:192, 211), and `Connections::attach` joins that worker on the UI thread (Connections.cpp:116-121 via OSD.cpp:653), freezing the emulator.
  - The +++ guard uses host ticks (433-506).
  - No DCD or RI.
  - Telnet SB bytes leak into the data stream (276-279).
- **H5** HostSerial forces DTR and RTS on and only logs CTS/DCD (linux :168-188, win32 :98-105). A 0-byte timed-out write loses the byte (SerialPortDevice.hpp:207, win32 :255-266).
- **H6** No emulated-time contract: bytes enter at the next guest access. There is no journal and no replay.

FORK lacks Connections, the SSC, Clipboard and HostSerial, but its devices share H1-H4.

## 5. Ideal shape

### 5.1 Fidelity
- **Channel model.** Transmitter = holding register → shift register → wire. Receiver = wire → shift register → FIFO (3 deep on the SCC, 1 on the 6551).
  - Every transition is an EventTimer event.
  - Timing comes from an integer phase accumulator over the real clock ratio (3.6864 or 1.8432 MHz against 14.31818 MHz), honouring WR11/WR14 or control bit 4, the clock mode and the framing. There is no untimed path.
- **Wire records.** Each character is `{start_cycle, byte, bits, parity, stop, baud, break}`. A mismatched receiver deterministically gets framing or parity errors.
- **SCC interrupts.** IE/IP/IUS latches follow [datasheet Z] exactly. INT = MIE ∧ (an IP not masked by a higher IUS); RR3 and RR2B are pure functions of the latches.
  - Ext/Status latches RR0 when IP sets and releases it on Reset Ext/Status.
- **6551.** Follows [datasheet R]: repeating TX IRQ, DCD/DSR latching, CTS gating, echo mode, and the two resets.
- **Modem lines.** DTR, RTS and break go out; CTS, DCD, DSR and RI come in. Each change is a cycle-stamped event.
- **Parallel.** Data latch, a STROBE event, the FF2 busy flag, and an ACK event after a configurable emulated delay (0 = instant). Add the PROM alteration once the schematic confirms it.

### 5.2 Determinism
- **HostLink.** One per jack, owned by the emulator thread. Workers are dumb byte pipes behind SPSC rings with `std::atomic` indices.
- **Ingest point.** A periodic EventTimer event (1 ms emulated, plus one at frame end) drains the rings into an **inbound journal**, stamping each item with that cycle.
  - The receiver starts a character at max(stamp, previous stop-bit end).
  - Nothing crosses at a register access.
- **Outbound.** TX-complete appends `{cycle, byte}` to an **outbound journal** and hands the byte over through an emulator-side spill queue. Nothing is dropped silently; overflow is a loud counter.
- **Flow control.** Live flow control (host CTS, socket backpressure) enters only as journaled line events.
- **Protocols run in emulated time on the emulator side.** Hayes AT, the +++ guard, telnet, DCD/RI and DTR hangup all move there; the worker moves socket bytes only, so modem sessions replay.
- **Backends.**
  - `loopback`: TX to RX, no thread.
  - `nullmodem`: jack ↔ jack.
  - `file-in`: scripted `cycle byte` input.
  - `capture`: file, clipboard, printer.
  - `modem`, `hostserial`.
  - `replay`: no worker.
  
  Gates use only `loopback`, `nullmodem`, `file-in` and `replay`.
- **Attach/detach** are machine events applied at an ingest point; worker joins happen off the UI and emulator threads.

### 5.3 Instrumentation
- **Pure `peek()`** for every register. Debugger and Observatory read through it; `read()` = `peek()` + side effects.
- **Rails:** `hostio.tx{jack,cycle,byte}`, `hostio.rx.arrive`, `hostio.rx.consume`, `hostio.line{sig,level}`, `hostio.err{kind}`, `scc.irq{src,ip,ius}`, `par.strobe/ack`.
- **Debug view:** registers, baud per direction, FIFO, shift-register progress, the IE/IP/IUS matrix, line levels both ways, journal counters, backend state.
- **Snapshot blocks** `scc`, `ssc`, `parallel`, `hostio`: registers, FIFOs, in-flight character start cycles, pending event times, the unconsumed journal and cursors. The manifest states that live links reattach and are not restored.

### 5.4 Common framework
`HostLink{kind: serial|parallel|net-socket|net-frame; journals; ingest tick; lines; backend}`.
- **Uthernet II:** UP's NetWorker request/event rings and `rx_credit` (net_worker.hpp) become journaled events, with credit applied in emulated time. FORK's vnat maps to `net-frame`.
- **Future devices** (MIDI, an ImageWriter model, AppleTalk) use the same contract.

## 6. Implementation steps (by guest-visible impact)
| # | Step | Size | Risk |
|---|---|---|---|
| 1 | SCC interrupt block: S1-S4, the WR0 commands, IUS, software INTACK | M (~300 lines) | Med: firmware and GS/OS drivers depend on it |
| 2 | Continuous receiver, 3-byte FIFO, overrun/special conditions; two-stage TX and All Sent (S6-S8) | M | Med |
| 3 | Atomic rings, no silent drops, safe thread start, hang-proof shutdown (H1-H3) | S | Low |
| 4 | Modem lines and break both ways; DCD from modem/host; DTR hangup (S5, S10, H5) | M | Low-Med |
| 5 | HostLink boundary: ingest tick, journals, replay/loopback/nullmodem/file-in; modem logic in emulated time (H4, H6) | L | Med |
| 6 | SCC clock routing, integer timing, zero count (S9) | S-M | Low |
| 7 | 6551 corrections A1-A8, applied when porting 5e35081a | M | Low |
| 8 | Parallel on HostLink with STROBE/ACK/FF2 (PROM alteration after the schematic check) | S (+M) | Low |
| 9 | `peek`, rails, debug view, snapshot blocks (S11, A9) | M | Low |

## 7. Validation
- **IIgs ROM self-test 06**: register R/W, internal loop, All Sent, Rx Available [doc: Docs/SCC8530_Serial.md].
- **New gates** (corpus programs, headless, loopback/file-in backends only):
  - (a) `scc_loop_timing`: local loopback at 300-57600 baud; Rx-available minus write cycle must be within one bit of bits × bit time, measured from rails.
  - (b) `scc_irq_matrix`: source × channel × WR1 mode × MIE, checking RR3, RR2B (status high and low) and the IRQ line.
  - (c) `ssc_6551`: both resets, repeating TX IRQ, overrun keeps the old byte.
  - (d) `nullmodem_scc_ssc`: 64 KB each way, zero loss.
  - (e) `hostio_replay`: record a live modem session, replay twice; the outbound-journal hash and GSDIAG `cycles=` must match.
  - (f) `serial_snapshot_midchar`: save mid-character, restore, identical journals.
  - (g) `parallel_capture`: PR#1 listing hash with ACK delay 0 and 10 ms.
- **Software:**
  - ProTERM: interrupts, Apple-B break, DTR hangup.
  - Spectrum, Teleworks, FreeTerm, ANSITerm.
  - GS/OS Print Manager to the ImageWriter port.
  - SSC firmware `PR#n` / `IN#n` and its Ctrl-A commands.
  - ADTPro at 115200 as an overrun stress test [recollection — unverified that it supports both chips].

## Prioritised summary
1. **SCC interrupt block.** Lost IRQs and wrong vectors are the largest guest-visible fault.
2. **Continuous receiver and FIFO.** They fix stalls and unrealistic overrun.
3. **Queue and thread fixes.** They remove the undefined behaviour, silent drops and the shutdown hang.
4. **Modem lines end to end.**
5. **HostLink ingest tick, journals and replay.** These are the prerequisite for every serial gate.
6. **Clock routing, 6551 corrections at port time, the parallel handshake, instrumentation and snapshot.**
