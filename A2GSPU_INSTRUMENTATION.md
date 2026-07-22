# A2GSPU instrumentation — inventory & gaps

Instrumentation added to this gssquared fork for headless/agent-driven diagnosis
(env-gated; no behavior change unless an `A2GSPU_*` var or `A2GSPU_CTRL` is set).

## Current instrumentation

**Headless spike** (one-shot, env-gated):
- `A2GSPU_SPIKE_FRAMES=N` — run N frames headless, dump `spike_frame.bmp`, exit.
- `A2GSPU_SPIKE_KEYS` / `_KEYS_AT` — inject keystrokes (keyboard paste buffer), paced by the guest's `$C000` polls.
- `A2GSPU_TEXTDUMP=<file>` — dump text page 1 (2KB: main $0400-07FF + aux for 80-col).

**Interactive ctrl rail** (`A2GSPU_CTRL=<dir>`; cmd.N→ack.N atomic files, 20Hz poll):
- `run <frames>` · `keys <s>` · `press <hex> [hold]` (physical key: latch+AKD)
- `text <file>` · `shot <file>` · `read <hex> <len> <file>` (flat bank-0 RAM)
- `mount sXdY <path>` (runtime media swap) · `save`/`restore <file>` (CPU+MMU snapshot)
- `cpu` → `PC A X Y SP P + KBD strobe + AKD + TEXT/HIRES` (halted-vs-waiting-vs-executing)
- `quit`

**Other rails**: `A2GSPU_SNAP_SAVE/LOAD`, `A2GSPU_SAVE_AT` (break-triggered snapshot),
`A2GSPU_CPUTEST`/`A2GSPU_MMUTEST` (self-tests), `A2GSPU_RUN_BIN` (IIgs), `GSDIAG:` summary
line (status/rc/gsos_err/brk/scb/hash).

## Gap closure — 2026-07-22

Rationale: this fork is the **diagnostic surface for closed-loop Rosetta work**, so a
partial rail means blind spots in everything downstream. Notably, `lib65816/src/sim.c`
executes 65816 instructions with no ROM, Tool Locator or GS/OS, so it structurally
cannot validate the toolbox/GS-OS interface layer — this emulator is the only oracle
that reaches it.

**Already closed (this doc was stale):**

| # | Gap | Status |
|---|-----|--------|
| 1 | Disassembler | `dis <addr> <count> <file>` exists, reads through the MMU |
| 2 | Bank-aware read | `read` now uses `probe_peek()` on a full 24-bit address; the old flat `get_memory_base()` index was IIe-only and read the wrong region on `-p 5` |
| 7 | IIgs keyboard inject | `press` covers the keygloo/ADB path (buffer + IRQ + sticky hold) |

**Closed now:**

| # | Gap | Verb |
|---|-----|------|
| 3 | Breakpoint / run-until | `run-until <BB:AAAA\|AAAAAA> [max_instr]` — step until PBR:PC matches, or budget exhausted. Reports `hit`/`budget`/`halted` + full CPU line. Lets a harness assert **at a point** rather than "after N frames", which is flaky by construction. |
| 4 | Single-step | `step [n]` — execute N instructions (default 1), report state. |
| 5 | Stack dump | `stack [n]` — N bytes above SP (bank 0). SP was visible but its *contents* were not, so a call could be seen to be deep but never traced to its caller. |
| 8 | Video-mode decode | `vid` — `TEXT MIXED PAGE2 HIRES 80COL 80STORE ALTCHAR DHIRES`, read through the MMU rather than cached flags. Replaces the ambiguous `TEXT=1 HIRES=1` pair. |

**Also closed:**

| # | Gap | Verb |
|---|-----|------|
| 6 | General `$C0xx` access ring | `iolog on [cap]` / `iolog off` / `iolog dump <winpath>` / `iolog reset` — every soft-switch read AND write, ordered, all machines (`src/io_trace.hpp`, tapped in `MMU_II::read/write`). The old `iolog` (kept, bare form) only counted five keyboard switches. |

All verbs verified end-to-end against a headless `-p 5` boot:

```
step 5              -> stepped=5 PC=00:FA62 -> 00:FA6B
run-until FA6B 1000 -> hit instr=4 PC=00:FA6B
stack 8             -> SP=0129 00 00 FF FF 00 00 FF FF
vid                 -> TEXT=0 MIXED=0 PAGE2=0 HIRES=0 80COL=0 80STORE=0 ALTCHAR=0 DHIRES=0
iolog on/run/dump   -> 2588 ordered R/W $C0xx records (C036 R/W, C015 R, ... = real boot)
```

All eight prioritized gaps are now closed.

### Build environment note (the real one)

The headless sandbox build failed until `C:/msys64/ucrt64/bin` was on `PATH`. Without it,
`g++` runs but the spawned `cc1plus.exe` cannot load `libmpfr-6.dll` and dies with a
*silent* non-zero exit (no diagnostic). Ninja inherits the same broken environment, so
`cmake --build` fails identically on stock `HEAD`. Fix: `export PATH=/c/msys64/ucrt64/bin:$PATH`
before building. (An earlier note here blamed an `-O3` lambda for a silent failure; that
was a mis-diagnosis caused by checking `head`'s exit code through a pipe instead of the
compiler's — the lambda→free-function change was kept for clarity but was never the cause.)

### Implementation notes

`a2gspu_step_one()` mirrors the per-instruction bookkeeping `run_one_frame()` performs —
the three event timers (c14m / video / cpu) are pumped around each `execute_next`.
Omitting that starves the video scanner and any timer-driven device, so the machine
drifts out from under the very code being diagnosed.

`a2gspu_cpu_line()` is shared by `cpu`, `step` and `run-until` so a harness parses one
format regardless of which verb produced it.

**Build caveat (`-O3`):** the `vid` helper was originally a lambda inside
`a2gspu_ctrl_loop()`. That function is already very large, and the extra inlining
candidate made g++ 15 fail *silently* at `-O3` — non-zero exit, no diagnostic — while
`-O0/-O1/-O2` compiled fine. It is now a free function (`a2gspu_sw`). Avoid adding
lambdas to that function.

## Gaps (prioritized by value for RE/debug — from live pain points)

| # | Gap | Why it hurt (concrete) | Proposed verb/rail |
|---|-----|------------------------|--------------------|
| 1 | **Disassembler** | Hand-decoded bytes to read the `$9D14` loop | `dis <addr> [n]` — N instrs, 6502/65C02/65816 |
| 2 | **Bank/PC-aware read** | `read` gave bank-0 flat → wrong page on IIgs (`FF FF 00 00`); couldn't see the code at PC | `read <bank>:<addr>` and/or `read pc` (follows PBR/langcard/aux) |
| 3 | **Breakpoint / watchpoint** | Only blind `run <frames>`; can't stop AT CHKSYNCH's entry or on `$C0EC` access | `bp <addr>` / `watch <softswitch>` / `run-until` |
| 4 | **Single-step + trace** | Frame-granular only; can't watch a tight loop or its entry | `step [n]` (instr-granular) + optional reg trace |
| 5 | **Stack / call trace** | `cpu` gives SP but not stack contents → can't find CHKSYNCH's *caller* | `stack [n]` (dump return addrs) |
| 6 | **I/O access log** | Inferred slot-6 from `X=$60`; a `$C0xx` r/w log would show the protection's I/O directly | `iolog on/off` (ring buffer of soft-switch touches) |
| 7 | **IIgs keyboard inject** | `keys`/`press`/AKD are no-ops on `-p 5` (MODULE_KEYBOARD null; IIgs = ADB) | ADB-path injection for the IIgs |
| 8 | **Video-mode decode** | `TEXT=1 HIRES=1` ambiguous | full mode word: text/lores/hires/dhr/mixed/page/80col/annunc |

**The two that would immediately unblock the current work (CHKSYNCH patch):**
disassembler (#1) + a breakpoint/watchpoint (#3). Flow: `watch $C0EC` (slot-6 read) →
break at CHKSYNCH's read loop → `stack` for the caller in WIZARDRY.CODE → `dis` it →
patch. Today that requires guessing from `cpu` snapshots alone.

## UPDATE 2026-07-13 — `dis` shipped; IIgs-awareness is now the top gap

`dis <addr> <n>` added (reuses the debugger `Disassembler`, reads via MMU). But it
exposed the real blocker on `-p 5` (IIgs): diagnosing a **65816** hang needs state the
IIe-oriented rails don't provide. At the Route-N1 hang, `cpu` reports `PC=9D14` (stable)
but `read`/`dis` show only fill (`FF FF 00 00` bank 0, `CC CC 00 00` bank 1) there — so
the tools and the CPU disagree about what's at PC. Missing, in priority order:

1. **Full `PBR:PC` + `E` flag + `clock_stopped`/`halt`** in `cpu` — a stable PC over fill
   memory is meaningless without knowing the bank, emulation-vs-native, and whether the
   CPU is even running. `cpu` currently masks `full_pc & 0xFFFF` (drops PBR) and omits E/halt.
2. **IIgs-correct `read`/`dis` addressing** — `read`'s flat `main / +0x10000` map is an
   *IIe* model; on the IIgs `get_memory_base()` is bank-organized, so `read <bank>:<addr>`
   (and `dis` following PBR) is required. The current fill-pattern reads are the wrong region.
3. **Banking soft-switch readout** — RAMRD/RAMWRT/ALTZP/80STORE (+ IIgs shadow) so a reader
   knows which physical page `$9D14` resolves to.

Only after these can the CHKSYNCH-vs-BIOS-mismatch question (does Wizardry's `SYSTEM.STARTUP`
crash because it calls `RTSTRP.APPLE`-specific BIOS vectors absent from AP 1.3's `SYSTEM.APPLE`,
vs. spin in slot-6 copy protection?) be answered. That is the Route-N1 diagnosis blocker.

## UPDATE 2026-07-13 (2) — IIgs keyboard injection works for AP1.3; #6 I/O log is next

IIgs `press` now injects via the ADB key buffer (`KeyGloo::store_key_to_buffer`, +AKD) — **PROVEN:
`F` opens the Filer at AP1.3's Command line on `-p 5`.** But **none** of three methods (raw latch,
ADB buffer, +AKD) reach **Wizardry's RTSTRP-based menu read** (qkumba ProDOS build). RTSTRP reads
the keyboard via a path the injection doesn't touch. **Gap #6 (I/O access log — a ring buffer of
`$C0xx` reads/writes) is now the priority**: it would show *which* soft-switch RTSTRP actually polls
(`$C000`? `$C010`? `$C025`? an ADB data-reg read `$C026`?), turning the keyboard-path guessing into a
one-look answer. Build it before a fourth injection guess. (Same log also cracks the CHKSYNCH-vs-BIOS
question for N1 by showing the exact I/O at the crash.)

## UPDATE 2026-07-21 — the "Gaps" table above is SUPERSEDED

⚠️ **The `## Gaps` table (and the two `UPDATE 2026-07-13` notes) are STALE.** A live audit of
the code (`grep A2GSPU_ src/`, the CTRL rail in `gs2.cpp` `a2gspu_ctrl_loop`, the env parse
block ~1956–2470, `Docs/HeadlessDiagnostics.md`) found the instrument suite far exceeds this
doc: **~75 env rails + 14 CTRL verbs + the `A2GSPU_TAP_*` family (`src/generic_tap.hpp`)**,
covering nearly everything this doc still lists as a gap. (An earlier "~140" figure over-counted
by including `A2GSPU_CMD_*`/`REG_*`/`STATUS_*` card-protocol compile-time constants, which are
NOT env rails.) See `Docs/A2GSPU_RAILS.md` for the code-verified catalog.

➡️ **The accurate, current contract catalog is now [`Docs/A2GSPU_RAILS.md`](Docs/A2GSPU_RAILS.md)**
— every rail grouped (run / observe / trace / break / manipulate / snapshot / assert /
CTRL-verb / device-determinism), each with its one-line effect and env-gating, plus a
"closed-loop contract" recipe cross-linked to [`Docs/CLOSED_LOOP_INTEGRATION.md`](Docs/CLOSED_LOOP_INTEGRATION.md).

**Gaps now IMPLEMENTED (verified against code):**

| Old gap | Now provided by |
|---------|-----------------|
| #1 Disassembler | CTRL `dis <addr> <n> <file>` (MMU-routed, bank/langcard-correct) |
| #2 Bank/PC-aware read | CTRL `read <addr> <len> <file>` + `A2GSPU_DUMP` — both go through `probe_peek` (bank-correct on `-p 5`); `dis` follows the MMU |
| #3 Breakpoint / watchpoint | `A2GSPU_BREAK` (24-bit PC), `A2GSPU_WATCH`/`_READ`/`_CHANGE`, `A2GSPU_VALTRAP`, `A2GSPU_PCTRAP`, `A2GSPU_CONDTRAP`, `A2GSPU_STACKTRAP`, `A2GSPU_STACKWATCH`, `A2GSPU_SAVE_AT` |
| #4 Single-step + trace | `A2GSPU_ITRACE_*` (per-instruction, PC/frame-armed, windowed, file sink), `A2GSPU_CALLTRACE`/`CALLSTREAM`, `A2GSPU_TRACE_EXT` |
| #5 Stack / call trace | `A2GSPU_STACKWATCH`/`STACKTRAP`, `A2GSPU_RETGUARD`, `A2GSPU_CALLTRACE`; CTRL `cpu` reports 16-bit SP |
| #6 I/O access log | CTRL `iolog` (keyboard soft-switch read counts `C000/C010/C024/C025/C026`); `A2GSPU_LCTRACE` (Language-Card); `A2GSPU_INTLOG` (IRQ/BRK/COP) |
| #7 IIgs keyboard inject | CTRL `press`/`holdkey` (KeyGloo + IRQ path), `A2GSPU_FORCE_KEY` |
| #8 Video-mode decode | `A2GSPU_VIDEOSUM` (per-line SCB + palette + pixel histogram), `A2GSPU_VIDEOMAP`, `A2GSPU_MODETRACE` |
| IIgs-aware `cpu` (2026-07-13 note) | CTRL `cpu` now prints full `PC=BB:AAAA` + `E`/`HALT`/`STP`/`RDY` (bank + emulation-vs-native + run state) |

Beyond the old gap list, the fork also has the **manipulate** half (the agent's hands):
`A2GSPU_POKE`/`POKE_NTH` (reg/flag/mem/force-branch/RTS/RTL/SKIP), CTRL `mount`/`save`/`restore`;
snapshot rails (`A2GSPU_SNAP_SAVE`/`SNAP_LOAD`/`RESTORE`); the assert/golden gate
(`A2GSPU_ASSERT`, `A2GSPU_GOLDEN`) + the `GSDIAG:` status line; determinism knobs
(`A2GSPU_SEED`, `A2GSPU_FAKETIME`); and `A2GSPU_RUN_BIN` for the runtime value-harness.

This section is additive; the historical log above is retained for provenance but should be
read as **history**, not as the current gap list.
