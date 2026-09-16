# CPU cores, interrupts and reset: the ideal shape

Sources: fork `ed2c00fc`, upstream `upstream/main`, merge-base `c4b1fa69`. All read with `git show`. Nothing was built or run, so claims about behaviour come from reading the code (`[code]`). The WDC PDF could not be turned into text in this read-only session. Datasheet facts therefore come through secondary quotes and are tagged as such. Line numbers refer to `src/cpus/base_6502.cpp` unless another file is named. **U** = upstream, **F** = fork.

## 1. Behaviour classes

### 1.1 Register widths and the X/Y high-byte rule

- **Hardware:**
  - When the x flag is 1, XH and YH are forced to $00, and trying to change them has no effect [doc: Clark, "65C816 Opcodes", 6502.org].
  - With x=1, MVN/MVP "will only be able to access the first page of the source and destination banks", and DBR becomes the destination bank [doc: undisbeliever 65816 opcode reference].
  - Native TXS/TCS move 16 bits; emulation mode forces SH to $01 [doc: same].
- **U:**
  - Enforces XH=YH=0 on SEP (4046), PLP, RTI and XCE (`c9883d94`), and on reset.
  - MVN/MVP address `full_db|_X` and step `_X(cpu)++`, which respects the width (2103, 4067, 4077). This matches the documentation.
  - TXS `sp = reg` (1224) is correct as long as the rule holds.
- **F:**
  - Same rule enforcement.
  - TXS `sp = cpu->x` (1263) does not depend on the rule.
  - MVN/MVP build `bank:X16` explicitly, which is better, but **always step X/Y as 16-bit** (2179, 4383, 4394). With x=1 and X=$FF, X becomes $0100. That breaks the rule and addresses page 1 [code].
  - The fork's original symptom (stores landing at `$00nn`) came from the missing PLP/RTI clears, which both trees now have.
- **Verdict on UPSTREAM_SYNC §5:** take upstream's width-aware stepping and the fork's explicit bank. Confirm with SST `44/54.n.json`.

### 1.2 Wrap rules by addressing mode

**Hardware:**
- Page wrap happens only when E=1 **and** DL=$00, and only in the "old" 65C02 modes: d,X; (d); (d,X); and the (d),Y pointer [doc: Clark].
- [d], [d],Y and PEI never wrap [datasheet p.42 caveat, as quoted on nesdev forum t=14281; confirmed on SNES hardware for [d] in the same thread].
- Stack-relative modes and the new stack ops (PEA, PEI, PER, PHD, PLD, PLB, JSL, RTL, JSR (a,X)) do not wrap at page 1 [doc: Clark].

| Mode | U | F |
|---|---|---|
| (d) pointer high byte, E=1, DL=0 | crosses the page (971) ✗ | wraps (997) ✓ |
| (d,X) pointer, E=1, DL≠0 | wraps anyway (926) ✗ | same ✗ |
| New stack ops (`*_new` + `stack_fix_new`) | ✓ | ✓ |

- **6502 core:** push and pull write `sp=(uint8)…` (SH=0), but TXS sets SH=$01. Guest memory is unaffected, but state views are inconsistent [code].

### 1.3 Cycle counts and phantom/IO cycles

| Item | Hardware | U | F |
|---|---|---|---|
| Native taken branch across a page | 3 cycles (`2+t+t*e*p`) [doc: Clark] | 1598 ✓ | 1666 ✓ (same logic) |
| XCE | 2 [doc: Clark] | phantom *after* the exchange (4639) ✓ | phantom *before* (4957) ✓ |
| PLB / WAI / STP | 4 / 3 / 3 [doc: Clark] | ✓ | ✓ |
| IRQ entry | 7 (E) / 8 (N): discarded fetch, second PC read, pushes, vector with VPB [doc: BCS "Investigating 65C816 Interrupts"; netlist sim: visual6502 timing page, via nesdev mirror] | total ✓; the second PC-read cycle is charged **after** the vector reads (2170–2200) | same (2264–2300) |
| 65C02 decimal ADC/SBC | +1 cycle; 65816 +0 [doc: Clark 65C02 / 65C816 tutorials] | missing ✗ | ✓, 65816 excluded |
| 65C02 JMP (abs) | 6 [doc: Clark 65C02] | 5 ✗ (3752) | 6 ✓ |
| 65C02 ASL/LSR/ROL/ROR abs,X | 6, or 7 on page cross; INC/DEC always 7 [doc: Clark 65C02] | always 7 ✗ (740) | same ✗ |
| 65C02 unused NOPs | $44 reads zp, $54/$D4/$F4 read zp,X, $DC/$FC read abs, $5C "reads somewhere" [doc: Clark 65C02] | no reads ✗ (`invalid_nop`, 2126) | same ✗ |
| 65816 abs,X / abs,Y / (d),Y with 16-bit index | the extra cycle is an invalid-address IO cycle [datasheet note (4), quoted in the U comment above 650] | full `phantom_read(eaddr)` (664/689/729), a **second real access to the effective address** ✗ | same (685/710/750) ✗ |
| DL≠0 direct-page cycle | +1 (`w` term) [doc: Clark; datasheet per same]; not confirmed on silicon here | 794/864 | 815/885 |
| NMOS zero-page RMW | write-back of the unmodified value [recollection — unverified] | IO at PC (825) | same |
| Phantom addresses for JSL cycle 5, RTS last cycle, (sr,S),Y cycle 6 | datasheet addresses | full reads at post-push S (3976, 3832, 1180) | same |

- **Guest-visible:**
  - The abs,X double access is the real hazard. `LDA $C0xx,X` with 16-bit X in native mode hits a soft switch twice.
  - The rest are address or cycle fidelity issues.
- **Open hardware question:** does the IIgs decode I/O on VDA=VPA=0 cycles? [recollection — unverified]. The ideal design keeps this question answerable (see §2).

### 1.4 Decimal mode

Rules are from [doc: Clark, "Decimal Mode", Appendix A]. "bin" means the flag comes from the binary result; "dec" means from the decimal result.

| CPU/op | Rule | U | F |
|---|---|---|---|
| NMOS ADC | Z bin (A+B+C); N,V Seq.2 (before the $60 correction) | Z drops C ✗ (1390); N after correction ✗ | Z ✓; N ✗ |
| NMOS SBC | N,V,Z,C all bin | Z `a-b+C` ✗; N,V from the decimal intermediate ✗ | Z ✓; N,V ✗ |
| 65C02 ADC | N,Z dec; V Seq.2 | ✓ | ✓ |
| 65C02 SBC | A Seq.4; V bin | Seq.3 (differs only for invalid BCD) ✗; V ✗ | same ✗ |
| 65816 ADC/SBC | ADC as 65C02; SBC A Seq.3, V bin | ADC ✓; SBC V ✗ | same |

### 1.5 NMOS undocumented opcodes

- **Both trees:** `invalid_opcode` is a 1-cycle no-op that prints to stdout on every execution (2121). That is host I/O noise and it breaks determinism of logs.
- **Hardware:** stable undocumented opcodes (LAX, SAX, DCP, ISC, SLO, RLA, SRE, RRA, ANC, ALR, ARR, SBX, multi-byte NOPs, JAM) are netlist-defined [netlist sim: perfect6502 — recollection].

### 1.6 Interrupt kinds, WAI/STP, WDM

- **NMI:** no input and no entry path in either tree [code].
  - Hardware: edge-sensitive; priority RES > ABORT > NMI > IRQ [doc: BCS].
  - NMOS NMI can hijack BRK/IRQ during the push cycles [netlist sim: visual6502 "Interrupt Hijacking"].
- **ABORT:** not implemented. The IIgs does not use it: the pin is on the RAM expansion connector only, and some IIgs CPUs are ASICs without it [doc: comp.sys.apple2 thread]. The ideal is to document it as not wired and not model it.
- **BRK/COP:**
  - Native: push PBR, PC+2, P; clear D after the push; in E mode BRK pushes B=1 [doc: Clark 65C816]. Both trees ✓ (2062/2131).
  - COP's pushed B bit in E mode (B=1 in both trees) is unverified; SST `02.e` decides.
- **IRQ sampling:**
  - Model: `irq_pipe` shifts in `!I & line` each cycle and fires on bit 1 (66/74). This reproduces the NMOS CLI/SEI one-instruction latency [code reasoning].
  - Missing: a taken branch that does not cross a page delays recognition by one instruction [netlist sim: visual6502 timing]. The 65C02/65816 equivalent is unknown [recollection — unverified].
- **WAI:** hardware wakes on any interrupt input [doc: BCS]; with I=1 it continues past WAI without servicing [recollection — unverified]. U (`8fbca782`, 4602) skips lowering RDY when the line is already asserted and adds an RDY wake (2212). F has only the RDY wake (2312). Observably the same; the fork's latch-style model is better.
- **VPB:** vector reads use `vp_read`, which the IIgs MMU maps to ROM (F `mmu_iigs.cpp:811`) ✓.
- **WDM:** hardware treats it as a 2-byte NOP. U runs Host FST handlers from inside the core (3990), with no trace record. F does not have this.

### 1.7 IRQ line, sources, latency

- **Controller:** both trees OR a `uint64` bitfield down to a `bool` for the CPU. F adds edge records to the observatory.
- **Latency:**
  - Events run only **between instructions** (F `gs2.cpp:425–434`). An assertion due mid-instruction is seen at the boundary, so recognition can be one instruction late.
  - The result is deterministic but not exact.
  - Frame-dispatched devices are quantised further.
- **Stale IRQ after reset:**
  - `irq_pipe` is not cleared by `cpu_state::reset` or `computer_t::reset`.
  - The IRQ test does not re-check I.
  - So an *instantaneous* reset (the fork's CTRL rail, upstream's debug protocol) can take a stale pending IRQ before the first ROM instruction [code reading; not run]. A held RESET line shifts the pipe clear.
- **IIgs sources** [recollection — verify against the IIgs Hardware Reference]: Mega II VBL/¼-second (`$C041`/`$C046`, cleared by writing `$C047`), VGC scanline/1-second (`$C023`, cleared by `$C032` writes), ADB GLU (read `$C026`/`$C024`), SCC, DOC (read register `$E0`), and slot /IRQ (wired-OR). Acknowledgement lives in the devices. The CPU side needs only a snapshot of which sources were asserted when the IRQ was recognised.

### 1.8 Reset

- **Hardware, 65816:** I=1, D=0, E=1, M=X=1, DBR=PBR=0, D=$0000, SH=$01, XH=YH=0; SL undefined; no stack pushes [doc: BCS].
- **Hardware, 65C02:** RESET clears D [doc: Clark 65C02].
- **Hardware, NMOS:** S decremented by 3 with writes suppressed [recollection — unverified; netlist sim would settle it]. The multi-cycle sequence ends in a VPB vector read on *release* [datasheet — not extracted here].
- **Both trees:** `cpu_state::reset` sets I=1 before the core reset (`cpu.cpp`), so fork fix #4 in UPSTREAM_SYNC §4 is redundant in practice (nothing calls `core->reset` directly). 65C02 D is not cleared (U 77, F 85). The vector is read when the line is asserted, in zero cycles. Both print `stack init` to the host.
- **U cold/warm (`35505bd6`, `992a5233`):**
  - `mmu->reset(cold)` sets ROM03 CYAREG bit 6 on cold start.
  - KeyGloo loads the ROM03 µC ROM.
  - Cold start still pokes `$3F2–$3F4`=0 **into guest RAM from the host** (`computer.cpp:366–368`; F 290–292 does the same). That breaks the fork's no-host-writes rule.
- **F:** calls `mmu->reset()` with no flag; CTRL issues `computer->reset(cold)`; power-on SP is seedable (`A2GSPU_SEED`).

### 1.9 Where both trees fixed the same thing (naive-merge hazards)

| Fix | U vs F placement | Naive git merge |
|---|---|---|
| **XCE phantom** | after the `if (cpu->E){…}` block vs before the exchange | Hunks are 7 lines apart, so they **merge cleanly and XCE becomes 3 cycles**. Only upstream's `d981a267` XCE record, which the fork's `main816.cpp` lacks, catches it. |
| IRQ-entry phantom (both CPU paths) | same insertion point, different comments | Conflict; "take both" gives 8/9 cycles. |
| PLB phantoms | same point | Conflict; "take both" gives 6. |
| RTI XH clear | inside the native branch vs after the if/else | Clean and redundant, but idempotent. |
| PLP clear, branch penalty, RDY wake | same lines, same logic | Conflict, equivalent content. |
| WAI guard | upstream only | Clean, equivalent. |
| MVN/MVP | upstream unchanged since base | Fork version wins silently, carrying the 16-bit stepping defect. |
| JMP (abs) | upstream TODO lines vs fork code | Conflict; keep the fork's. |

## 2. Ideal shape

**Bus-cycle core (requirement a):**
- Every cycle is one call: `cycle(kind, addr24, data&)`.
  - `kind` is one of READ, WRITE, IO, FETCH or VECTOR, and carries VDA, VPA, VPB, MLB, E and MX exactly as datasheet Table 5-7 gives them (the SingleStepTests field set).
  - The `_ign` variants disappear. An IO cycle carries its datasheet address and is handed to the machine bus, which decides whether it decodes.
  - The IIgs bus policy is a single documented knob until hardware settles the VDA/VPA question. II-family machines (no VDA/VPA) decode every cycle.
- Keep the compile-time traits and the five 65816 cores (fast path). The descriptors are constants, so the cost is zero.
- The data-sheet cycle order is the code order. The IRQ entry's second PC read comes before the pushes.

**Register rule:** one helper `set_p()` used by SEP, REP, PLP, RTI, XCE and reset. It forces M/X in E mode, XH=YH=0 when x=1, and SH=$01 in E mode. Debug builds assert the rule after every instruction.

**Interrupt lines (requirement b):**
- An `InterruptLines` object holds:
  - IRQ: a level, wired-OR, with per-source bits and **assert/deassert timestamps**.
  - NMI: a falling-edge latch with a timestamp.
  - RESET: a level.
  - ABORT: absent on the IIgs, stated explicitly.
- The CPU samples lines at its own cycle boundaries by timestamp (`asserted_at <= sample_cycle`). An event due mid-instruction is then recognised as if it had been delivered in its own cycle, without processing events every cycle.
- Priority is RES > NMI > IRQ. NMOS hijack of BRK/IRQ during pushes is modelled.
- The branch recognition delay applies on NMOS only until 65C02/65816 behaviour is confirmed.
- The pipe is cleared, and I re-checked, at reset.

**Reset:**
- `ResetLine` assert → devices wired to RESET reset → CPU held (cycles counted).
- Deassert → the CPU runs the reset sequence as bus cycles (VECTOR/VPB) and loads the register state from §1.8, including D=0 on 65C02.
- `power_on(seed)` is a separate path: seeded SP, RAM pattern and undefined registers, recorded in the session manifest. Its power-on side effects include ROM03 CYAREG bit 6 and the Apple II power-up byte, which becomes invalid naturally from RAM contents rather than a host poke.
- "Cold" means power_on followed by a reset pulse. "Warm" means a reset pulse.

**Instrumentation (requirement c):**
- **Per-instruction record:** the existing fields plus E, and an access list (addr, data, R/W, kind, width) covering MVN source/destination and both halves of RMW. It replaces the single `eaddr`/`f_write`.
- **Interrupt-entry record:** kind, vector, source mask, assert and recognition cycles, pushed PBR/PC/P, E, hijack flag. It goes to the ring and the observatory instead of printf rails.
- **Per-cycle observer:** a `TraceTraits` parameter, compiled out by default, which feeds the SST comparator and a cycle ring.
- **Cycle accounting:** Clark's formulas (`4-m+w`, `7-e`, …) as a table asserted per instruction in test builds.
- **Side-effect-free views:** a normalised register snapshot, with memory read only through `probe_peek`. Rails keep the fork's "off → one branch, never moves the machine" contract.
- **WDM:** a 2-byte, 2-cycle NOP unless a configured host-escape device claims the operand. The device then emits a HOST_ESCAPE record.

## 3. Implementation steps (ordered by guest-visible impact)

| # | Step | Size | Risk |
|---|---|---|---|
| 0 | Before porting upstream CPU commits: add XCE, PLB, WAI and IRQ-entry records to `cycletest816`; resolve §1.9 by hand | S | Low |
| 1 | Remove the 65816 16-bit-index double access (abs,X/abs,Y/(d),Y) → IO cycle; audit every full `phantom_read`/`phantom_write` for datasheet address and kind | M | Med: IIgs I/O timing |
| 2 | IRQ timestamps and exact recognition; IRQ-entry cycle order; clear pipe on reset | M | Med: raster titles, golden hashes move |
| 3 | MVN/MVP: width-aware stepping plus explicit bank; `set_p()` rule helper | S | Low |
| 4 | NMI line, edge latch, priority, hijack; slot NMI wiring; interrupt-entry record | M | Low–Med |
| 5 | Reset as level plus sequence on release; `power_on(seed)`; drop the `$3F2–4` pokes; 65C02 D=0; port U `mmu->reset(cold)` onto this shape | M | Med: boot hashes |
| 6 | 65C02 corners: abs,X shift 6/7, NOP reads, U decimal cycle/JMP port check | S | Low |
| 7 | Decimal flags per Clark Appendix A (NMOS N/V, 65C02 Seq.4, SBC V bin) | S | Low |
| 8 | E-mode (d,X) wrap only when DL=0; COP E-mode B bit per SST; 6502 SH normalisation | S | Low |
| 9 | NMOS stable undocumented opcodes plus JAM; no stdout per opcode | M | Low |
| 10 | Bus-cycle descriptor, per-cycle observer trait, access-list trace, E in trace, WDM gating | M–L | Low (compile-time gated) |
| 11 | Settle DL≠0 cycle, E-mode RMW write and IIgs VDA gating on real hardware (see §4) | S | — |

## 4. Validation

**External suites: fetched at test time, never vendored.**
- A CMake option `GS2_FETCH_CPU_VECTORS` runs a script that downloads **pinned commit tarballs**, checks SHA-256, and stores them under the build tree or a user cache.
- Tests carry `SKIP_RETURN_CODE 77` (the fork's existing convention) and a `cpu-external` label, so a missing fixture never reads as a pass.

**SingleStepTests:**
- **65x02:** `6502`, `synertek65c02` (closest to the IIe/IIc part without bit ops [recollection]) and `wdc65c02`. MIT licence; generated by a documentation-conformant emulator, not captured from silicon [doc: README].
- **65816:** `v1/*.e.json` and `*.n.json`. No licence is shown, which is one more reason to fetch rather than vendor. A 6502.org forum post calls it "verified against hardware" [doc: forum post]; the README does not say how it was generated.
- Compare registers, RAM and **every cycle**, including VDA/VPA/VPB/MLB. Skip values the README marks `null`.
- Files exceed 10 MB, so stream-parse. CI samples N tests per file; the full run is nightly. Start from a commented exclusion list that may only shrink.

**Klaus Dormann suite (GPL-3.0):**
- `6502_functional_test`, `65C02_extended_opcodes_test` reassembled for the non-bit-op variant, `6502_decimal_test` (Clark), and `6502_interrupt_test`.
- For the interrupt test, the harness maps its feedback port to the new IRQ/NMI lines.
- Keep `cputest`'s exit-77 behaviour and register it in ctest.

**gilyon `snes-tests` `cputest-full.sfc`:** upstream's `cpu816test` harness; fetch the same way after checking its licence.

**Additions to the fork's own tests:**
- **cycletest816:** run every record in both E and native mode (E-mode branch across a page = 4). Add XCE both ways, PLB/PHB, WAI wake with I=0/I=1, STP, BRK/COP in both modes, IRQ/NMI entry 7/8 with vector reads last under VPB, reset sequence, MVN 7 per byte, and DL≠0 variants.
- **cycletest:** register a `65c02` run in ctest (today only the 6502 default runs). Add ASL abs,X 6/7, JMP (abs) 5/6, decimal ADC/SBC 2/3, IRQ latency after CLI/SEI/PLP, and the NMOS branch recognition delay.
- **conformtest:** add 65C02 and 65816 modes, decimal flags per Clark Appendix A including invalid BCD, MVN with x=1 across $FF, XH/YH after SEP/PLP/RTI/XCE, and E-mode wrap versus non-wrap.
- **New I/O-access test:** a mock MMU counts accesses per address and kind. It asserts one data access for abs,X/(d),Y at I/O addresses, and the NMOS/E-mode RMW write-back.
- **Hardware anchors:** a small program on a real IIgs times disputed sequences against the `$C02E/$C02F` counters. It covers the DL≠0 cycle, E-mode RMW write, IO-cycle decode (count toggles on a soft switch) and IRQ latency. The results become fork-owned fixtures.
