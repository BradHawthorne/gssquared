# A2GSPU rails — the instrumentation contract catalog

This is the **accurate, current** catalog of every `A2GSPU_*` instrumentation rail in this
gssquared fork. It supersedes the stale "Gaps" table in `../A2GSPU_INSTRUMENTATION.md`
(most of those gaps are now implemented — see that file's appended note).

## What this fork is (the contract)

gssquared is Rosetta's **closed-loop oracle/microscope** (see
[`CLOSED_LOOP_INTEGRATION.md`](CLOSED_LOOP_INTEGRATION.md)). Every rail below obeys three
hard invariants:

1. **Additive + env-gated.** Nothing here runs unless an `A2GSPU_*` env var (or the
   `A2GSPU_CTRL` rail) is set. **No `A2GSPU_*` env set ⇒ byte-identical stock emulator**,
   windowed and interactive. A default run pays at most one untaken branch per rail.
2. **Isolated / separable.** Rails live in the `a2gspu` device, the env-gated hooks in
   `gs2.cpp` (`run_headless_spike` / `a2gspu_ctrl_loop`), and thin call-sites in
   core CPU/MMU/video — so upstream merges stay tractable.
3. **Observe-don't-disturb** for the *observe/trace/break/snapshot/assert* groups: they read
   emulated state externally (like a logic analyzer) and never change bus behavior. The
   *manipulate* and *run* groups are the agent's **hands** (deliberate, documented state
   changes), and the CTRL rail's input verbs co-operate with (do not lock out) live human input.

Groups: **run** · **observe** · **trace** · **break** (breakpoints/watchpoints/traps) ·
**manipulate** · **snapshot** · **assert** · **CTRL-verb** · **device/determinism**.
Every rail is env-gated (that is the whole design); the "Gated" column flags the rare
always-on emission (the `GSDIAG:`/`SPIKE …` summary lines) so a harness knows what prints
without any env set.

Source anchors (verified against code): `src/gs2.cpp` `run_headless_spike` (~1899) and
`a2gspu_ctrl_loop` (~1660); `src/iigs_diag.hpp` (`iigs_emit_status`, ~130); env parse block
`gs2.cpp` ~1956–2470; device rails in `src/devices/a2gspu/a2gspu.cpp`,
`src/devices/adb/ADB_Micro.hpp`, `src/devices/rtc/RTC.hpp`,
`src/devices/pdblock3/pdblock3.cpp`, `src/cpu.cpp`, `src/cpus/base_6502.cpp`,
`src/iigs_toolbox.hpp`, `src/iigs_video_summary.hpp`.

---

## Run — drive the machine headless to a verdict

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `A2GSPU_SPIKE_FRAMES=N` | run `N` deterministic frames headless (with `-n`), then evaluate the dump/assert/golden gates and exit. The master headless switch. | run | env |
| `A2GSPU_SPIKE_KEYS=<str>` | inject keystrokes via the keyboard paste buffer (`\n`→`\r`), paced by the guest's own `$C000` polls | run | env |
| `A2GSPU_SPIKE_KEYS_AT=N` | frame at which `SPIKE_KEYS` is injected (default `spike_frames/2`) | run | env |
| `A2GSPU_RUN_BIN=path[@HEX]` | after boot frames, inject a flat binary into IIgs RAM (default load/exec `$7E0000`), reset the traces to isolate it, set PC to it | run | env |
| `A2GSPU_RUN_BOOT=N` | boot frames to run before `RUN_BIN` injection (default `spike_frames/2`) | run | env |
| `A2GSPU_LOAD=file@HEX[;…]` | multi-file deliberate RAM splice (IIe+IIgs); **no PC change**. Also accepts `HEX@file`. Separators: `;` or `,`. Hard-fails SPIKE on any error. | run | env |
| `A2GSPU_LOAD_BOOT=N` | frames before `A2GSPU_LOAD` (default `0`) | run | env |
| `A2GSPU_VRAM_LOAD=<file>` | inject 16K aux‖main (`profile=vram-raw`) into HGR page flat image — a2tile offline paint path | run | env |
| `A2GSPU_VRAM_PAGE=1\|2` | page for `VRAM_LOAD` (default 1) | run | env |
| `vram load <file> [page]` | CTRL live inject of same 16K layout | CTRL-verb (manipulate) |
| `A2GSPU_CPUTEST=<case>` | short-circuit the spike: run one CPU micro-test case, print `=== CPUTEST COMPLETE (PASS/FAIL) ===`, exit with its rc | run | env |
| `A2GSPU_MMUTEST=<case>` | short-circuit the spike: run one MMU+video (FPI mapping / SHR decode) micro-test, exit with its rc | run | env |
| `A2GSPU_HANG_THRESHOLD=N` | frame budget for the no-BRK degenerate-loop hang detector (`status=HANG` instead of burning frames) | run | env |

## Observe — dump state, video, symbols, bus

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `A2GSPU_DUMP="BANK/LO-HI[,…]"` | hexdump memory ranges (hex). `$E0`/`$E1` read the Mega II image directly (side-effect-free); other banks via the MMU | observe | env |
| `A2GSPU_TEXTDUMP=<file>` | dump text page 1 (2KB): main `$0400-07FF` via `probe_peek` (no `$C0xx` side effects) + aux `$0400-07FF` (80-col even columns) | observe | env |
| `A2GSPU_TEXT40=1` | at spike end, ASCII-decode the 40-col text page (`$E0:0400-07FF`) to stdout | observe | env |
| `A2GSPU_VIDEOSUM=1` | per-line SCB (320/640) + palette histogram (RGB888) + mode-correct pixel-index histogram | observe | env |
| `A2GSPU_VIDEOMAP=1` | a 40×25 ASCII dominant-index map of the Super Hi-Res window (SEE layout headless) | observe | env |
| `A2GSPU_ROM=<path>` | ROM image path for the lazy-loaded ROM-vs-kernel PC classifier (default `resources/roms/apple2gs/main.rom`) | observe | env |
| `A2GSPU_SYMBOLS=<file>` | load a symbol table (annotate trace/PCs with names) | observe | env |
| `A2GSPU_ROM_SYMBOLS=<file>` | name ROM-resident PCs (else `<ROM>`) | observe | env |
| `A2GSPU_SYM_BASE=<hex>` | pin the symbol-relocation base (disables auto-inference) | observe | env |
| `A2GSPU_SYM_SUSPECT=1` | append `[SUSPECT reused]` to any symbol whose name resolves at >1 address (advisory only) | observe | env |
| `A2GSPU_BUS_DUMP=<path>` | `~BUS` cycle dump (source = slot_bus) for the gssquared→card host gate | observe | env |
| `A2GSPU_BUS_STREAM=1` | live `~BUS` slot-bus CDC streaming (→ PB `INGRESS_SOURCE=0`) | observe | env |

*Always-on spike output (no env needed):* the spike prints `SPIKE E1:` (SHR-window
nonzero/distinct/FNV hash + SCB row), `SPIKE TRACE/SLOT/MMU/OBS/BRACKET:` lines, writes
`spike_e1.bin` / `spike_trace.bin` / `spike_frame.bmp`, and the `GSDIAG:` status line (below).

## Trace — execution / dispatch / load / mode streams

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `A2GSPU_TBTRACE=1` | trace Tool Locator (`$E10000`) + GS/OS class-0/1 dispatch (`$E100A8`/`$E100B0`): call name on entry, carry+A on return | trace | env |
| `A2GSPU_TBTRACE_BANK=<hex>` | scope `TBTRACE` to a single bank | trace | env |
| `A2GSPU_TRACE_FROM=<hex24>` | arm tracing only once `full_pc` first hits this 24-bit PC | trace | env |
| `A2GSPU_ERRHOOK=1` | surface carry-set GS/OS dispatch errors (`<-- SYSTEM/LOADER ERROR`) | trace | env |
| `A2GSPU_ITRACE_FROM=<hex24>` | per-instruction trace, armed when PC first hits this 24-bit address | trace | env |
| `A2GSPU_ITRACE_FRAME=<N>` | per-instruction trace, armed at the start of headless frame N | trace | env |
| `A2GSPU_ITRACE_N=<n>` | cap logged instructions (default 256) | trace | env |
| `A2GSPU_ITRACE_LO/_HI=<hex24>` | restrict ITRACE to a PC window `[LO,HI]` | trace | env |
| `A2GSPU_ITRACE_REARM=<n>` | re-arm ITRACE this many times (windowed re-capture) | trace | env |
| `A2GSPU_ITRACE_OUT=<file>` | write ITRACE to a file instead of stderr | trace | env |
| `A2GSPU_TRACE_EXT=1` | append DBR/DP/cycle-delta/scanline columns to 65816 trace lines | trace | env |
| `A2GSPU_CALLTRACE=1\|<hexPC>` | JSR/JSL call trace, armed from frame 0 (`1`) or at a PC | trace | env |
| `A2GSPU_CALLTRACE_N=<n>` | cap CALLTRACE entries | trace | env |
| `A2GSPU_CALLTRACE_SKIP=<n>` | skip the first n calls before logging | trace | env |
| `A2GSPU_CALLSTREAM=<file>` | symbol-free NDJSON of the toolbox/GS-OS call sequence (for `tools/gdiff/calldiff.py` first-divergence diffing) | trace | env |
| `A2GSPU_LOADTRACE="BANK[:LO-HI]"` | segment-overlay tracker: log contiguous write-bursts (segment loads) into the region + when they overlay prior loads | trace | env |
| `A2GSPU_LOADTRACE_MIN=<n>` | minimum burst length counted as a load | trace | env |
| `A2GSPU_LCTRACE=1` | Language-Card soft-switch access log | trace | env |
| `A2GSPU_MODETRACE=1` | CPU mode-transition events (XCE / REP / SEP → e/M/X changes) | trace | env |
| `A2GSPU_INTLOG=1` | interrupt-entry logger (IRQ/BRK/COP entries) | trace | env |
| `A2GSPU_MILESTONES=<file>` | load a boot-milestone list; print a reached / NOT-REACHED table at spike end | trace | env |
| `A2GSPU_SNAP_LO/_HI=<hex24>` | memory-window `[LO,HI]` for the SNAP region-logger | trace | env |
| `A2GSPU_SNAP_PCS=<hex,…>` | up to 8 trigger PCs at which to dump the SNAP window | trace | env |
| `A2GSPU_SNAP_OUT=<file>` | destination file for the SNAP region-logger (requires LO/HI + PCS) | trace | env |

## MEMVU — memory-behaviour accounting

A second rail family, separate from `A2GSPU_*` because it answers a different kind of
question: not "what is the machine doing right now" but "what shape does this workload's
memory behaviour have". Same three invariants (env-gated, isolated, observe-don't-disturb).

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `MEMVU_STOREVIS=1` | **store-visibility accounting.** Classifies every CPU store as DEVICE (`$Cxxx` in banks `$00/$01/$E0/$E1`), SLOWSIDE (direct Mega II `$E0/$E1`), SHADOWED (the machine mirrored it per its own `$C035` configuration) or PRIVATE, and prints one summary line at spike end | observe | env |
| `MEMVU_STOREVIS_BANKS=1` | add the per-bank `stores`/`visible` breakdown (one line per bank that saw traffic) | observe | env |

```
MEMVU STOREVIS: model=iigs-shadow-v1 stores=1091936 visible=295658 (27.08%) \
                private=796278 device=155935 slowside=114697 shadowed=25026 phantom=7
```

**Named observation model.** `model=` states which definition of "observer" produced the
number. `iigs-shadow-v1` uses the machine's own shadow decision, counted at the MMU decision
point rather than re-derived — there is no second copy of the shadow rules to drift. On a
platform that cannot shadow the rail reports `model=iie-basic-v1` and `shadowed=n/a`, never a
confident `0`.

**What PRIVATE does and does not mean.** It means no non-CPU reader is implied by the
machine's configuration as this emulator models it. It does not mean provably unobservable: a
bus-mastering card can read any RAM, and nothing here models that.

**Coverage.** Taps every CPU store funnel in `base_6502.cpp` — `bus_write` (ordinary stores
and stack pushes) and `phantom_write` / `phantom_write_ign` (RMW dummy writes, counted in the
total because they are real bus cycles on silicon, and reported separately so they can be
subtracted). `write_word` is dead code and is deliberately untapped; **if it is ever revived it
must be tapped, or this rail silently undercounts 16-bit stores.**

### Instruction-stream shape

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `MEMVU_OPMIX=1` | executed-opcode histogram, split by CPU mode context `(E, M-width, X-width)`, plus hand-derived group totals (branch / RMW / stack / call-return / block-move / mode-switch) and the raw per-opcode counts | observe | env |
| `MEMVU_IREUSE=<lines>,<linesize>` | instruction-line reuse against a direct-mapped model, reporting hit / miss / **modemiss**, plus the workload's instruction footprint. Both parameters must be powers of two; a bad geometry is REFUSED, not rounded. `=1` takes the 512×64 default | observe | env |
| `MEMVU_WORKSET=1` | direct-page access count (exact, at the two direct-page data funnels), how many distinct direct pages are live, the D-change count, and the stack pointer's observed range and page histogram | observe | env |

`modemiss` is the one that does not exist in a conventional instruction cache: on this
ISA an instruction's *length* depends on M and X, so a resident line decoded under one
mode cannot be reused under another. It bounds the value of caching anything decoded.

Measured on the idle Finder desktop (GS/OS 6.0.1, 300 frames, 3,005,841 instructions):

```
OPMIX : branch=316216 (10.5%)  stack=511396 (17.0%)  callret=259550 (8.6%)
        rmw=7264 (0.24%)  blockmove=0  modeswitch=21798 (0.73%)
CTX   : N/m16/x16=2912856 (96.9%)   N/m8/x8=92985 (3.1%)   emulation mode=0
IREUSE: 512x64B  hit=98.22%  miss=1.15%  modemiss=0.63%
        footprint = ~5 KB of distinct executed code
WORKSET:dp_accesses=416930 (r=137033 w=279897)  distinct_D=32  D_changes=54725
        S range=[$1785..$17FD] span=121 bytes, entirely in page $17
```

Direct-page accesses are counted **exactly**, at the funnels that form a direct-page
address — not by testing whether an address happens to land inside the current D
window, which would be indistinguishable from an ordinary absolute access to the same
byte. D and S are sampled once per instruction, so the live-page count and the stack
range are unbiased by how busy any one page or frame happens to be.

The group table is **hand-derived from the opcode map** and is good enough to steer
attention, not to quote normatively — which is why the raw per-opcode histogram is
emitted beside it, so any aggregate can be recomputed from primary data.

**`phantom` near zero on a IIgs is correct, not a dead tap.** Only an NMOS 6502 and a 65816 in
*emulation* mode write during the RMW modify cycle; a 65C02, and a 65816 in *native* mode, read
instead. The field therefore tracks time spent in emulation mode — ~83 for a IIgs sitting in ROM
firmware, 7 across a GS/OS boot, 0 on the Finder desktop. A large value on a native-mode
workload would be the surprising result.

**Boot is not a representative workload**, and the spread is wide enough to change conclusions.
Measured on GS/OS 6.0.1, each window taken from its own snapshot:

| window | stores | visible | note |
|--------|--------|---------|------|
| boot, first 300f | 1,091,936 | **27.08%** | firmware probing hardware — 155,935 stores into I/O space |
| boot, 400–800f | 1,690,879 | **8.04%** | |
| boot, 800–1200f | 2,044,310 | **6.39%** | I/O-space stores down to 1,912 — a 99% fall |
| Finder desktop, idle | 1,548,694 | **5.32%** | `device` = 0; an event loop *reads* soft switches |

Stores per frame *rise* across those windows (3,640 → 5,162) while the visible share falls: the
later phases do more work and more of it is private. Quote the phase, not the average, and treat
the early-boot figure as the outlier it is.

## Break — breakpoints, watchpoints, provenance traps

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `A2GSPU_BREAK=BBAAAA` | headless breakpoint at a 24-bit PC | break | env |
| `A2GSPU_STOP_ON_FAULT=1` | halt the spike on `BRK`/`COP`/SysFail instead of burning remaining frames | break | env |
| `A2GSPU_BRKDUMP=1` | dump CPU registers on `BRK` (crash under GS/OS) + a SPIKE-END register dump | break | env |
| `A2GSPU_BRKMEM=1` | on `BRK`, dump the recent-PC history + bytes around the crash PC and stacked return | break | env |
| `A2GSPU_WATCH="BANK:LO-HI[,…]"` | write-watchpoint on address range(s): log each write with writer PC/S/D/DBR ("who wrote address X"). Warns on a bare 24-bit addr (missing `:`) that would never fire | break | env |
| `A2GSPU_WATCH_READ=1` | also watch reads of the WATCH ranges | break | env |
| `A2GSPU_WATCH_CHANGE=1` | log only when the value differs from the last seen for that range | break | env |
| `A2GSPU_WATCH_MAX=N` | WATCH hit cap (default 256; `0` = unlimited) | break | env |
| `A2GSPU_WATCH_OUT=<file>` | emit WATCH as NDJSON to a file instead of stdout (adds `ts` frame-stamp + `ipc` fields) | break | env |
| `A2GSPU_WATCH_IPC_ZP=<hexZP>` | generic interpreter-IP attribution: makes the NDJSON `ipc` field read a 2-byte LE ZP pointer (observation-free) — attributes a store to the interpreter's segment. Default-off (`ipc`=0). For UCSD p-code (Wizardry) set `9E` | break | env |
| `A2GSPU_VALTRAP="<hexval>[:<w>]"` | value-provenance store trap: bind the PC that stores VALUE into memory (LE reassembled across consecutive bytes; `w`=1–4, default 3). "Who wrote value V" | break | env |
| `A2GSPU_VALTRAP_MAX=N` | VALTRAP hit cap (default 64) | break | env |
| `A2GSPU_PCTRAP="BANK:LO-HI"` | one-shot dump of registers + recent-PC ring on first entry to a PC range | break | env |
| `A2GSPU_TRAPDUMP="BASE:LEN"` | extra memory region (hex) dumped by PCTRAP (default 48 bytes) | break | env |
| `A2GSPU_CONDTRAP="BANK:PC@f=v"` | conditional flag-provenance trap: at PC, when flag `f`(c/z/i/d/x/m/v/n)==v, report the PC that last changed that flag | break | env |
| `A2GSPU_CONDTRAP_MAX=N` | CONDTRAP hit cap | break | env |
| `A2GSPU_STACKTRAP="LO-HI"` | one-shot dump when S first enters the range | break | env |
| `A2GSPU_STACKWATCH=1\|"LO-HI"` | continuous stack-pointer tripwire for garbage-S crashes (`1`=imbalance only; `LO-HI`=over/underflow window) | break | env |
| `A2GSPU_STACKWATCH_JUMP=<hex>` | imbalance threshold (single-step S delta that trips STACKWATCH) | break | env |
| `A2GSPU_SAVE_AT=<hexPC>[@<file>]` | snapshot + halt at the first hit of an arbitrary 24-bit PC (a breakpoint that also checkpoints) | break/snapshot | env |
| `A2GSPU_RETGUARD=1\|strict` | return-target guard: flag RTS/RTL that returns into `$00/$01` (or, `strict`, also a symbol-gap) | break | env |

## Manipulate — the agent's hands (deliberate state injection)

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `A2GSPU_POKE="<hexPC>:<act>[;…]"` | one-shot deliberate splice at a PC. Actions: `A/X/Y/S/D/P/DBR/PB=<hex>` (reg/flag), `PC=<hex>` (force-branch), `M<hex24>=<hexbyte>` (poke mem), `RTS`/`RTL` (force-return), `SKIP=<n>` (skip bytes) | manipulate | env |
| `A2GSPU_POKE_NTH=<n>` | fire the POKE on the Nth hit of the PC (default 1) | manipulate | env |
| `A2GSPU_TAP="PC[,PC…]"` | generic title-agnostic call tap (`src/generic_tap.hpp`, from the harvested wiz5 taps): NDJSON record on each hit of the watched PC(s) | trace | env |
| `A2GSPU_TAP_SESSION=<dir>` | periodic capture session: ZP stream + `$0400-BFFF` RAM / `$2000-5FFF` HGR + soft-switch `.meta` + text-page snapshots to `<dir>` | trace | env |
| `A2GSPU_TAP_PTRS`, `TAP_PCS`, `TAP_MAX`, `TAP_DUMP_ON`, `TAP_DUMP_EVERY`, `TAP_ZP_EVERY`, `TAP_CYC_PER_MS`, `TAP_CYC_PER_SEC` | tap tuning: extra ZP-pointer register fields, which PCs, hit cap, dump-on-op / dump cadence, ZP-stream cadence, cycle-rate calibration | trace | env |
| `A2GSPU_FORCE_KEY=<hex-ascii>` | force the ADB `$C000` keyboard latch to return this key (drives a `$C000`-polling guest headlessly) | manipulate | env |

*(Live/interactive manipulation is the CTRL rail's `keys`/`press`/`holdkey`/`mount`/`save`/`restore` verbs, below.)*

## Snapshot — save/restore machine state

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `A2GSPU_SNAP_SAVE=<file>` | at spike end, save CPU regs + full MMU snapshot (128K + page tables + soft-switch state) + trailing sentinel | snapshot | env |
| `A2GSPU_SNAP_LOAD=<file>` | restore that snapshot at start and skip the ~1200-frame boot (the frame loops self-heal excluded device/scanner/clock state) | snapshot | env |
| `A2GSPU_RESTORE=<file>` | alias for `SNAP_LOAD` (load-at-start) | snapshot | env |

*(File-format tokens `A2GSPU_SNAP_MAGIC/_VER/_END/_FREAD` are snapshot on-disk constants, not env rails.)*

## Assert — gate the exit code

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `A2GSPU_ASSERT="name OP val[;…]"` | `;`/`,`-separated `name OP value` checks (`OP` ∈ `== != <= >= < >`); the spike exits non-zero if any fails. Fields: `scb_mode`, `qd_carry/qd_err`, `lt_carry`, `cs_carry/pr_carry`, `gsos_err`, `nonzero/distinct`, `idxN` (N=0..15), and `peek:HHHHHH` (observation-free 24-bit byte read) | assert | env |
| `A2GSPU_GOLDEN=<file>` | hash the SHR window (`$E1:2000-9FFF`, house FNV-1a-64); if the file exists compare (DIFF ⇒ gate fail; corrupt/empty file ⇒ hard fail); if absent, bless it | assert | env |

*Always-on:* at spike end `iigs_emit_status` prints one machine-readable line —
`GSDIAG: status=<OK\|GATE_FAIL\|CRASH_BRK\|HANG\|GSOS_ERROR\|STALLED> rc=N gate=<PASS\|FAIL\|none>
gsos_err=$XXXX brk=N scb=NNN hash=................`. The **process exit code** stays
golden/assert-gate-driven (`0`/`1`) for harness compatibility; `status` is the richer category.

## Device / determinism — reproducible oracle knobs

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `A2GSPU_SEED=<n>` | seed the cold-boot SP entropy (`srand`) so a from-scratch spike is byte-reproducible (warm restores overwrite SP anyway) | determinism | env |
| `A2GSPU_FAKETIME=<epoch-s>` | freeze the RTC at that instant (bypasses `localtime`/timezone) so RTC-derived OS state is run-to-run stable | determinism | env |
| `A2GSPU_RAMDISK=1` | serve a mounted image from a copy-on-write RAM buffer; writes never touch the host file | device | env |
| `A2GSPU_SP_NDEV=<n>` | override the SmartPort device count (A/B faithfulness; `32` reproduces pre-fix enumeration) | device | env |
| `A2GSPU_SERIAL_PORT=<COMx>` | override the real-card CDC probe port (default tries `COM10` first) | device | env |
| `A2GSPU_SERIAL_DEBUG=1` | verbose real-card serial/reconnect logging | device | env |

## CTRL rail — the interactive `cmd.N`→`ack.N` protocol

`ctl.ps1 -Report <file.ndjson>` (or `A2RAIL_REPORT`) records every issued,
acknowledged, refused, or stalled transaction as `a2rail-event-v1`, including
sequence, session PID/owner, exact command/reply, timestamp, and host latency.
This is the forensic transcript for long agent runs; `cmd.N`/`ack.N` remain the
protocol authority.

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `A2GSPU_CTRL=<dir>` | enable the interactive stepping rail: 20 Hz poll of `<dir>/cmd.N`, atomic `<dir>/ack.N` reply; replaces the fixed-frame spike. Also forces headless when set | CTRL | env |
| `A2GSPU_CTRL_TIMEOUT=<s>` | idle timeout in seconds before an orphaned session quits (default 300) | CTRL | env |
| `A2GSPU_COPILOT=<dir>` | windowed, read-only telemetry rail; human input stays active | co-pilot | env |

**Verbs** (one per `cmd.N`; reply text lands in `ack.N`), verified in `a2gspu_ctrl_loop`:

| Verb | Effect | Group |
|------|--------|-------|
| `run <frames>` | step N frames; reply `ok` or `halted@<i>` | CTRL-verb (run) |
| `keys <str>` | append to the keyboard paste buffer (paced by guest polls) | CTRL-verb (manipulate) |
| `press <hex2> [hold]` | full physical keypress — latch + any-key-down, held `hold` frames (default 15), then released; IIe latch path AND IIgs KeyGloo+IRQ path | CTRL-verb (manipulate) |
| `holdkey <hex-ascii>` | set the sticky IIgs hold-key WITHOUT running (`0` clears) — hold, step in chunks, watch for consume, release | CTRL-verb (manipulate) |
| `iolog` | cumulative keyboard soft-switch read counts (`C000/C010/C024/C025/C026`); diff two calls across a `run` to see which switch a wedged menu polls | CTRL-verb (observe) |
| `cpu` | 65816 state: `PC=BB:AAAA A X Y SP=16bit P E HALT STP RDY KBD AKD` — distinguishes waiting-for-key vs halted vs executing | CTRL-verb (observe) |
| `dis <hexaddr> <n> <file>` | disassemble N instructions via the debugger `Disassembler` (reads through the MMU: bank/langcard-correct) to `<file>` | CTRL-verb (observe) |
| `text <file>` | dump the text page to `<file>` | CTRL-verb (observe) |
| `shot <file>` | update the display and save a screenshot to `<file>` | CTRL-verb (observe) |
| `read <hexaddr> <len> <file>` | dump `len` bytes from `<addr>` via `probe_peek` (side-effect-free, bank-correct on `-p 5`) to `<file>` | CTRL-verb (observe) |
| `mount sXdY <path>` | runtime media swap (save-and-unmount the drive first so writes persist) | CTRL-verb (manipulate) |
| `save <file>` | CPU regs + full MMU snapshot + sentinel (take at a quiescent point) | CTRL-verb (snapshot) |
| `restore <file>` | restore a `save`d snapshot and force-run | CTRL-verb (snapshot) |
| `quit` | ack and end the session | CTRL-verb (run) |
| `oracle` | north-star contracts (cycles/colour/snapshot/dual-mode) | CTRL-verb (meta) |
| `help` / `help <file>` | list CTRL verbs; optional full catalog to file | CTRL-verb (meta) |
| `manifest <file>` | machine-oriented capability dump (verbs+env groups+models) | CTRL-verb (meta) |
| `step [n]` | execute N instructions; `cycles=` **in-command** | CTRL-verb (run) |
| `run-until <addr> [max]` | stop at PC; `cycles=` **in-command** | CTRL-verb (run) |
| `vid` | softswitch decode TEXT…DHIRES | CTRL-verb (observe) |
| `stack [n]` | stack contents above SP | CTRL-verb (observe) |
| `png` / `pngc` | mono / named-colour DHGR from RAM | CTRL-verb (observe) |
| `vram` | 8K aux + 8K main raw | CTRL-verb (observe) |
| `load` / `poke` / `setreg` | deliberate RAM/reg splice | CTRL-verb (manipulate) |
| `cycles` | absolute counter; **WARN** cross-ack deltas unreliable | CTRL-verb (observe) |
| `dhgr-calibrate` / `dhgr-export` | 4-dot self-test + profile files for a2tile | CTRL-verb (calibrate) |
| `dhgr-golden <file> [page] [vram-raw\|4dot] [bless]` | FNV page golden; `vram-raw`=16K AUX‖MAIN; `4dot`=discrete RGB 560×192 | CTRL-verb (calibrate) |
| `A2GSPU_DHGR_GOLDEN=<file>` | SPIKE-end IIe DHGR gate (bless if missing; MATCH/DIFF exit) | env (run) |
| `A2GSPU_DHGR_PAGE=1\|2` | which page for DHGR SPIKE golden (default 1) | env (run) |
| `A2GSPU_DHGR_GOLDEN_PROFILE=` | `vram-raw` (default) or `4dot` — must match how the golden file was blessed | env (run) |
| `assert peek:…==…[;…]` | live probe_peek checks; `status=PASS\|FAIL` | CTRL-verb (observe) |
| `watch add\|list\|sample\|clear` | soft watch: sample peeks across run/step | CTRL-verb (observe) |
| `watch bus add bank:lo-hi` | **LIVE** bus write-watch (same as A2GSPU_WATCH, mid-session) | CTRL-verb (break) |
| `watch bus list\|hits\|clear\|read\|change` | bus watch control | CTRL-verb (break) |
| `rail list` / `rail env-only [file]` | discover launch-only env rails (L4 gap, not a black box) | CTRL-verb (meta) |
| `valtrap set <hex>[:w]` / `clear` / `status` | LIVE value-provenance trap (A2GSPU_VALTRAP) | CTRL-verb (break) |
| `itrace from <pc>` / `now` / `n` / `clear` | LIVE per-instruction trace | CTRL-verb (trace) |
| `tbtrace on\|off\|bank` | LIVE toolbox/GSOS dispatch trace | CTRL-verb (trace) |
| `callstream on <file>` / `off` / `status` | LIVE NDJSON toolbox/GSOS call stream | CTRL-verb (trace) |
| `key` | inject key + wait for consume receipt | CTRL-verb (manipulate) |
| `protocol` | CTRL v2 framing, ownership and `json <command>` reply wrapper | CTRL-verb (meta) |
| `systems [file]` | platform/configuration/ROM/slot-device inventory | CTRL-verb (meta) |
| `limitations [file]` | known unsupported or partial fidelity surfaces | CTRL-verb (meta) |
| `shr-golden <file> [bless]` | live IIgs SHR-window golden without relaunch | CTRL-verb (calibrate) |

**Cycle contract:** only `cycles=` on `run` / `step` / `run-until` acks are valid timing.
Never subtract two standalone `cycles` acks across other commands. See AGENTIC_ORACLE.md.

---

## The closed-loop contract — the Rosetta harness recipe

The Rosetta `GssquaredDriver` (see [`CLOSED_LOOP_INTEGRATION.md`](CLOSED_LOOP_INTEGRATION.md),
task #87) closes the loop on **this** tool. Two shapes:

### A. One-shot boot-to-verdict (the SPIKE)
For "does this image/toolchain-output boot cleanly":
```sh
# ⚠️ ucrt64 runtime DLLs MUST be on PATH or the exe dies with 0xC0000135 (STATUS_DLL_NOT_FOUND)
PATH=C:\msys64\ucrt64\bin;$PATH
A2GSPU_SPIKE_FRAMES=180 A2GSPU_STOP_ON_FAULT=1 \
  build/GSSquared.exe -p 5 -d s7d1=<image>.po -n
```
The driver parses the `GSDIAG:` line (`status/rc/gsos_err/brk/scb/hash`) plus the `SPIKE …`
hash lines → `EmulatorState.exit_reason` (`prompt-reached`/`crashed`/`timeout`). Add
`A2GSPU_ASSERT="scb_mode==640; gsos_err==0"` and/or `A2GSPU_GOLDEN=<file>` to make the
**process exit code** the pass/fail gate.

### B. Multi-step interactive (the CTRL rail)
For scenarios that type, swap disks, checkpoint, and read back state:
```sh
A2GSPU_CTRL=/path/ctldir build/GSSquared.exe -p 5 -d s7d1=<image>.po -n
# then the harness writes cmd.1='run 600', reads ack.1;
#   cmd.2='keys PR#5\r'; cmd.3='cpu'; cmd.4='read 00:2000 256 out.bin'; cmd.N='quit'
```

### The value-harness (assert *computed* values, not just boot)
The runtime value test for the fixed-point runtime (`~FMUL`, transcendentals) — closes the
`-ffixed` follow-up (audit #86) on the substrate we own. **PROVEN 2026-07-21** as ctest
`rt_fixed_fmul_value` (rosetta_v3): a flat fixture calls the real `~FMUL` to compute
`256.0 × 1/256 = 1.0` (raw `$00010000`, routed through the exact `_umul16($0100,$0100)` the
audit fixed — pre-fix it returned 0), stores the result at `$E11E00`, and asserts it.
```sh
# assemble+link a FLAT binary (asmiigs+linkiigs), then:
A2GSPU_SPIKE_FRAMES=180 A2GSPU_RUN_BIN=fmul_value.bin@7E0000 A2GSPU_RUN_BOOT=0 \
  A2GSPU_ASSERT="peek:E11E00==0x00; peek:E11E01==0x00; peek:E11E02==0x01; peek:E11E03==0x00" \
  build/GSSquared.exe -p 5 -d s7d1=boot.po -n
```
⚠️ **`peek:` reads ONE byte** (`MMU::probe_peek` → `uint8_t`, 0–255) — you CANNOT write
`peek:ADDR==0x00010000` (a byte can't equal 65536; it silently always-fails). Assert a
multi-byte value as **little-endian byte peeks** (as above). Also note `256*256` overflows
16.16 and wraps to 0 in *both* the broken and fixed multiply, so it is NOT a discriminating
test — pick a product that fits (e.g. the `1.0` case above). `RUN_BIN` injects+jumps to the
flat binary after `A2GSPU_RUN_BOOT` frames; drive it to a sentinel spin/`BRK`, then peek the
result cell.

### Determinism
Pin `A2GSPU_SEED=<n>` (cold-boot SP entropy) and `A2GSPU_FAKETIME=<epoch>` (frozen RTC) so
the SHR-window golden hash and any RTC-derived state are byte-reproducible across runs.

### The dual-mode gate (release invariant)
Every change is proven to leave the **normal windowed emulator** unchanged: launch
`GSSquared -p 5 -d s7d1=<image>` with **no** `A2GSPU_*` env set — identical interactive
behavior. Instrumentation OFF-by-default is a gate, not a preference.

## Related docs
- [`HeadlessDiagnostics.md`](HeadlessDiagnostics.md) — the spike harness, WATCH/VALTRAP,
  FAKETIME, ASSERT/GOLDEN, `GSDIAG:` taxonomy, `MMU::probe_peek` for developers.
- [`CLOSED_LOOP_INTEGRATION.md`](CLOSED_LOOP_INTEGRATION.md) — why this fork exists, the
  `GssquaredDriver` architecture, upstream-sync discipline, the proven launch recipe.
- [`../A2GSPU_INSTRUMENTATION.md`](../A2GSPU_INSTRUMENTATION.md) — original inventory; its
  "Gaps" table is **superseded** by this catalog (see its appended note).

> Note on the `A2GSPU_CMD_*` / `A2GSPU_REG_*` / `A2GSPU_STATUS_*` / `A2GSPU_MODE_*` /
> `A2GSPU_SHR_*` / `A2GSPU_UHR_*` / `A2GSPU_PAL_OFFSET` / `A2GSPU_SCB_OFFSET` /
> `A2GSPU_IDENT_BYTE` identifiers in `src/devices/a2gspu/`: those are the **a2gspu card
> protocol** compile-time constants (register offsets, command opcodes, status bits), **not**
> env-gated instrumentation rails — they are not listed here.
