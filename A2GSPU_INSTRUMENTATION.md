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
