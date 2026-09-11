# A2GSPU rails — the instrumentation contract catalog

> **Which rails exist is answered by the binary, not by this file.**
>
> `src/a2gspu_rails_registry.hpp` is the source of truth for the *set* of rails;
> this document carries their *semantics* — the groups, invariants, worked
> combinations and gotchas that a one-line synopsis cannot hold.
>
> Ask the running process instead of trusting a list:
>
> ```
> rail                      # count, families, discovery syntax
> rail list [family|PREFIX] # names, e.g. `rail list memvu`, `rail list A2GSPU_TAP`
> rail find <NAME>          # one rail's family and synopsis
> rail env-only <file>      # the whole table, tab-separated (schema a2rails-v1)
> rail live                 # which rails a CTRL verb reaches live, and which read back --
>                           # declared per rail and CHECKED against the verb table
> rail read <NAME>          # a rail's state now (MEMVU_*, A2GSPU_TRAP, A2GSPU_LIVENESS,
>                           # A2GSPU_RECORDS, A2GSPU_STRICT, the determinism pins), or the verb that reads it
> rail schema [NAME]        # the shape of the NDJSON records a rail writes (tag + fields),
>                           # declared once beside the writers; `manifest <file>` carries the table
> ```
>
> `tools/a2rail/railparity.ps1` (run by `dualsmoke.ps1`) fails if the code reads a
> rail the registry does not declare, or declares one no code reads — **and now
> also if a declared rail's name appears nowhere in this file.**
>
> This warning exists because the file earned it: it described **96** of the
> **128** rails the code actually read. The 32 it omitted included `MEMVU_LOADVIS`,
> `MEMVU_BLAME`, the whole `A2GSPU_TAP_*` family and `A2GSPU_COVERAGE` — which
> feeds the deasmiigs code/data split. A hand-maintained list in
> `a2gspu_manifest.hpp` had drifted separately to 36. Three copies of one fact,
> and both copies a client could reach were short.
>
> **And it happened again**, because the warning was the only mechanism. At the
> 142-rail mark this file had no entry for 24 of them: all twelve `WIZ5_*` rails,
> `A2GSPU_COVERAGE` and `COVERAGE_OUT` (the subject of the note above), the SNAP
> region-logger's `HI`/`MAX`/`ZP`, `HGR_PAGE`, `KEYRAM`, `KEYSCHED_DELAY`,
> `MOUSETRACE`, `OUT_DIR` and the `WRITEBACK` pair — while the `A2GSPU_TAP_*`
> family was present but written under short names, which is invisible to anyone
> grepping for the name they must actually set. The hand-maintained list in
> `a2gspu_manifest.hpp` had drifted again too, to 53 of 142, and had acquired a
> rail that does not exist (`ITRACE`, which `rail find` rejects). That list is
> now generated from the registry, and the name check below is a gate. A
> catalogue nothing checks is a catalogue that is wrong; this one has now proved
> it twice.

This document supersedes the stale "Gaps" table in `../A2GSPU_INSTRUMENTATION.md`
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
| `A2GSPU_LIVENESS=N` | **progress proof.** Every `N` frames hash the screen (IIgs: SHR `$E1:2000-9FFF` from the Mega II image; II-family: HGR `$2000-3FFF`) and the text page, side-effect-free, and print `frame= cycles= screen= text= kbd= changed=<first\|screen\|text\|both\|none>`. Close-out gives `screen_last_changed=` / `screen_unchanged_since=<frame>\|none` and the same for `text_`, tracked separately (a halted SHR workload keeps redrawing its text page from its wait loop — measured), and both `_unchanged_since` fields are appended to `GSDIAG:` while armed. A **signal, not a verdict**: an idle desktop is static too; what it rules out is a run that halted on an error screen while its counters kept climbing (the one-disk Dealer Demo measured as a workload until its hash was seen not to move) | observe | env |
| `A2GSPU_LIVENESS_STALL=K` | unchanged samples in a row before the screen counts as static (default 3) | observe | env |
| `A2GSPU_LIVENESS_OUT=<file>` | NDJSON sink for the samples instead of stdout lines | observe | env |
| `A2GSPU_RECORDS=<file>` | **the record contract's global sink.** Every rail record has one shape — `{"event":"<rail>.<kind>","frame":N,"cycles":N,…}` then the rail's own fields, one whole line (`src/a2record.hpp`) — and a record whose rail has no sink of its own (`A2GSPU_TRAP_OUT`/`out=`, `A2GSPU_LIVENESS_OUT`, `A2GSPU_WATCH_OUT`, `A2GSPU_CALLSTREAM`, `A2GSPU_TAP`) lands here. Without it those rails print their stdout line as always. `rail read A2GSPU_RECORDS` reports the sink and the count per rail; `rail schema <RAIL>` the fields. `railparity.ps1` fails JSON printed outside the writer | observe | env |
| `A2GSPU_ROM=<path>` | ROM image path for the lazy-loaded ROM-vs-kernel PC classifier (default `resources/roms/apple2gs/main.rom`) | observe | env |
| `A2GSPU_SYMBOLS=<file>` | load a symbol table (annotate trace/PCs with names) | observe | env |
| `A2GSPU_ROM_SYMBOLS=<file>` | name ROM-resident PCs in the trace region tags (else `<ROM>`). **Names by default:** even without it, the documented firmware entry points (the Apple II Monitor vectors `COUT RDKEY GETLN HOME …` in banks `$00` and `$FF`, the IIgs `ToolDisp`/`GSOS_Disp` vectors, ProDOS 8's `MLI`) **and the ROM's own toolbox function entry points** — 526 of them for ROM 01, read from the system tool pointer table by `tools/a2rail/romfpt.py` and shipped as `assets/symbols/apple2gs/<ROM house-FNV>.sym` (installed by the build beside the ROMs, so it loads from any working directory) so a different ROM gets no wrong names — name PCs in the new attribution outputs, `STOREVIS writers top:` and `TRAP` records' `sym`. `$FF/46ED` is `SoundTools.FFStartSound+$2FC`. A PC between two entries is named after the preceding one, which is right for a function body and wrong for a shared helper; a `TRAP` at `E1:0000` recording `x` is the authority for helpers. RAM-resident tool sets — most of them under System 6, which patches the ROM sets and loads the rest — are named from the **live** tool pointer table (`$E1/03C0`), walked side-effect-free when a RAM address misses and the table has moved or grown; one `IIGS SYMS: live tool pointer table …` line reports each walk. An address nothing covers prints as `(unnamed)` / `null` | observe | env |
| `A2GSPU_SYM_BASE=<hex>` | pin the symbol-relocation base (disables auto-inference) | observe | env |
| `A2GSPU_SYM_SUSPECT=1` | append `[SUSPECT reused]` to any symbol whose name resolves at >1 address (advisory only) | observe | env |
| `A2GSPU_BUS_DUMP=<path>` | `~BUS` cycle dump (source = slot_bus) for the gssquared→card host gate | observe | env |
| `A2GSPU_BUS_STREAM=1` | live `~BUS` slot-bus CDC streaming (→ PB `INGRESS_SOURCE=0`) | observe | env |
| `A2GSPU_COVERAGE="LO-HI"\|"BANK:LO-HI"` | **per-byte execution bitmap** over that range, one bit per byte, set when the byte was part of an executed instruction — this is what turns deasmiigs's code/data split from a heuristic into a measurement (`--coverage`). A lower bound, never an upper one: a byte that did not execute in this run is *not proven code*, not *proven data*; union several runs to widen it. Live equivalent: the `cov` verb, which also binds a source plan (`A2COV2`) | observe | env |
| `A2GSPU_COVERAGE_OUT=<file>` | file the bitmap is flushed to. Written **before** the gate block, which exits on any gate and would otherwise throw the run's coverage away | observe | env |
| `A2GSPU_HGR_PAGE=1\|2` | which HGR page the II-family dump/golden gate reads — for a workload that gates on the page it just drew | observe | env |
| `A2GSPU_KEYRAM=1` | dump RAM once per keystroke; needs a session directory to write into (`A2GSPU_TAP_SESSION`). Canonical name for `WIZ5_KEYRAM` | observe | env |

*Always-on spike output (no env needed):* the spike prints `SPIKE E1:` (SHR-window
nonzero/distinct/FNV hash + SCB row), `SPIKE TRACE/SLOT/MMU/OBS/BRACKET:` lines, writes
`spike_e1.bin` / `spike_trace.bin` / `spike_frame.bmp`, and the `GSDIAG:` status line (below).
It also prints, first, an `A2GSPU PINS:` line — the identity of the run: `seed= faketime= ramdisk=`
as the rails read them (`unset` when unset), `platform= frames=`, the loaded ROM's size and
house-FNV, the configure-time build version (labelled, because it lags the tree), and the house-FNV
of the running executable, which cannot lag. With `GSDIAG:`'s `cycles=` and `hash=` this is what
makes a number traceable to the binary and inputs that produced it, and what a harness checks before
it differences two runs.

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
| `A2GSPU_ITRACE_LO=<hex24>`, `A2GSPU_ITRACE_HI=<hex24>` | restrict ITRACE to a PC window `[LO,HI]`; setting either one also **enables** ITRACE (they are the master switch that has no bare `A2GSPU_ITRACE` behind it — that name is not a rail and the binary rejects it) | trace | env |
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
| `A2GSPU_SNAP_LO=<hex24>`, `A2GSPU_SNAP_HI=<hex24>` | memory-window `[LO,HI]` for the SNAP region-logger. **Nothing to do with the snapshot rails** — `SNAP_SAVE`/`SNAP_LOAD` save a machine, `SNAP_LO/_HI/_PCS/_OUT/_MAX/_ZP` dump a memory window at trigger PCs | trace | env |
| `A2GSPU_SNAP_PCS=<hex,…>` | up to 8 trigger PCs at which to dump the SNAP window | trace | env |
| `A2GSPU_SNAP_OUT=<file>` | destination file for the SNAP region-logger (requires LO/HI + PCS) | trace | env |
| `A2GSPU_SNAP_MAX=N` | cap on SNAP records emitted | trace | env |
| `A2GSPU_SNAP_ZP=<hexZP>` | zero-page pointer correlation key, recorded as an **annotation** on each record. The emitter keys on PC, so this labels a record; it never triggers one | trace | env |
| `A2GSPU_MOUSETRACE=1` | append each ADB mouse injection to `kg_mouse_trace.txt` — proves a mouse delta reached the micro, not just the host | trace | env |

## MEMVU — memory-behaviour accounting

A second rail family, separate from `A2GSPU_*` because it answers a different kind of
question: not "what is the machine doing right now" but "what shape does this workload's
memory behaviour have". Same three invariants (env-gated, isolated, observe-don't-disturb).

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `MEMVU_STOREVIS=1` | **store-visibility accounting.** Classifies every CPU store as DEVICE (`$Cxxx` in banks `$00/$01/$E0/$E1`), SLOWSIDE (direct Mega II `$E0/$E1`), SHADOWED (the machine mirrored it per its own `$C035` configuration) or PRIVATE, and prints one summary line at spike end, followed by `device top:` — the five most-written I/O registers, named from `iigs_ioregs.hpp` — and `writers top:` — the five code granules that generated the most visible stores, symbolized when a symbol table is loaded, `(unnamed)` otherwise. A total never travels without its causes | observe | env |
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

### Who causes the expensive traffic

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `MEMVU_BLAME[=<topN>]` | attributes every externally-visible store and machine-served read to the **opcode address** of the instruction responsible, and reports the worst code granules, each named, with the toolbox call in progress when its stores had one (`; in SoundTools.FFStartSound 98%`) — a shared helper is named by the call that used it, not by the entry point that precedes it | observe | env |
| `MEMVU_SEAM=<ratio>,<slowmul>,<bufdepth>,<shadowreads>` | **a MODEL, not a counter** — cost of routing part of a workload's memory traffic to the machine. Parameters are mandatory; an incomplete set is REFUSED. Output is an upper bound | observe | env |

`MEMVU_BLAME` turns the visibility rails around. They say how much a workload costs;
this says where the cost comes from. Pointed at a system whose source you have, the
output stops being a performance measurement and becomes a work list.

Attribution is to the address of the **opcode**, captured at dispatch — not the PC at
the moment of the access, which for any multi-byte instruction has already walked past
the operand and would blame a point inside the instruction itself. Counts are per
256-byte granule: fine enough to land on a routine, coarse enough to stay a fixed
512 KB however much code runs. Pair with `A2GSPU_SYMBOLS` to get names.

Measured on the idle Finder desktop — **17 granules, roughly 4 KB of code, generate
every externally-visible access in the system**:

```
$E1/0000  visible_writes=0      routed_reads=104624
$07/0E00  visible_writes=19360  routed_reads=73568
$FE/0000  visible_writes=0      routed_reads=81376
$FF/C000  visible_writes=23252  routed_reads=23252
```

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
| `A2GSPU_TRAP="<spec>[;<spec>]"` | **Traps from configuration.** Each spec: a trigger — `at=PC[-PC]` (`HHHH` any bank, `BB:HHHH` one bank), `store=ADDR[-ADDR]` (a byte written into the range; the record names the storing instruction and the byte), or `sp=LO-HI` (S entering the range) — optional `exit=PC` pairing for `at=`, `when=` conditions over registers and memory (`z16(30)`, `m8(s16(1)+1)`, `x==1`, `z8(2d)~{4,5,9,a,b,c}`), a policy (`once` default, `every`, `first=N[/key]`), payloads `do=`/`heavy=`/`xdo=`/`xheavy=` from `regs ring zp stack mem=<expr>:<len> field=<name>:<expr> text scan=…`, `cap=N`, `out=<file>`. NDJSON with frame and cycle stamps. Malformed spec = fatal at arm; every spec echoed at arm and summarised at close (`records=0` is printed, never implied). The general form of PCTRAP and of the three wiz5 traps — see [`A2GSPU_TRAP.md`](A2GSPU_TRAP.md) | break | env |
| `A2GSPU_TRAP_FILE=<file>` | specs one per line, `#` comments; combines with `A2GSPU_TRAP` | break | env |
| `A2GSPU_TRAP_OUT=<file>` | default sink for specs without `out=`; else `A2GSPU_RECORDS`; otherwise records go to stdout as `IIGS TRAP: {…}` | break | env |
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
| `A2GSPU_TAP_PTRS`, `A2GSPU_TAP_PCS`, `A2GSPU_TAP_MAX`, `A2GSPU_TAP_DUMP_ON`, `A2GSPU_TAP_DUMP_EVERY`, `A2GSPU_TAP_ZP_EVERY`, `A2GSPU_TAP_CYC_PER_MS`, `A2GSPU_TAP_CYC_PER_SEC` | tap tuning: extra ZP-pointer register fields, which PCs, hit cap, dump-on-op / dump cadence, ZP-stream cadence, cycle-rate calibration. (Spelled in full. They were written here under short names, which is invisible to a reader — and to a checker — grepping for the name they must actually set) | trace | env |
| `A2GSPU_FORCE_KEY=<hex-ascii>` | force the ADB `$C000` keyboard latch to return this key (drives a `$C000`-polling guest headlessly) | manipulate | env |
| `A2GSPU_KEYSCHED=<file>` | a **schedule** of keystrokes read from a file, injected entry by entry. Distinct from `SPIKE_KEYS`, which is one literal string injected at one frame. A missing file is fatal at arm (exit 2), not a warning. Canonical name for `WIZ5_KEYS` | manipulate | env |
| `A2GSPU_KEYSCHED_DELAY=N` | frames between scheduled keystrokes. Canonical name for `WIZ5_KEY_DELAY` | manipulate | env |
| `A2GSPU_WRITEBACK=1` | write in-memory (possibly modified) disks back to their **host files** at spike end. OFF by default under mutation-guard discipline — use only on writable scratch images. Canonical name for `WIZ5_WRITEBACK` | manipulate | env |
| `A2GSPU_NO_WRITEBACK=1` | suppress write-back even when `WRITEBACK` is set. Honoured across **both** spellings, so the guard cannot be bypassed by setting the other name. Canonical for `WIZ5_NO_WRITEBACK` | manipulate | env |

*(Live/interactive manipulation is the CTRL rail's `keys`/`press`/`holdkey`/`mount`/`save`/`restore` verbs, below.)*

## Snapshot — save/restore machine state

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `A2GSPU_SNAP_SAVE=<file>` | at spike end, save CPU regs + full MMU snapshot (128K + page tables + soft-switch state) + device blocks + trailing sentinel + a **manifest** naming what the file holds, what it omits, the frame, the pins and the binary. **Do not read the lists from here — read them from the file.** They vary by platform and by what the machine had when the save ran (`tools/a2rail/artifacts.py snap <file>` prints the trailer; the `restore` ack carries the same text) | snapshot | env |
| `A2GSPU_SNAP_LOAD=<file>` | restore that snapshot at start and skip the ~1200-frame boot (the ADB micro, the Ensoniq, the interrupt registers, the disk controller and the block device's RAM-disk overlay restore from the snapshot when it holds them; floppy media, scanner and clock self-heal). Prints the manifest, or that it is absent and what Class A means. `SPIKE_KEYS` into a restored machine warns **only when the snapshot does not hold the ADB micro** — a pre-device-block file, or a platform with no ADB. From such a machine a restored desktop never polls `$C000` again (measured); from one that holds the ADB it does, and there is no warning | snapshot | env |
| `A2GSPU_RESTORE=<file>` | alias for `SNAP_LOAD` (load-at-start) | snapshot | env |

*(File-format tokens `A2GSPU_SNAP_MAGIC/_VER/_END/_FREAD` are snapshot on-disk constants, not env rails.)*

**The manifest, and why this file no longer quotes one.** It used to, and the
quote was wrong on *both* platforms. The generator is
`a2gspu_snap_manifest_text()` in `gs2.cpp`; its fields are:

| Field | Meaning |
|-------|---------|
| `class=` | `A` cpu+mmu+softswitches+RAM · `B` adds video/scanner state · `C` adds the ADB micro, the Ensoniq and the IWM (`INSTRUMENTATION_SHAPE.md` M5's roadmap). **Derived from the blocks actually written**, and claimed only when the definition is met — a save holding a device the roadmap never named (a IIe holding only `pdblock`) stays `class=A` and lets `devices=` carry it, because inventing a `B` for it would be a second false claim |
| `devices=` | the device blocks in the file, comma-separated, or `-` |
| `included=` / `excluded=` | the full state vocabulary split in two. **This pair is the contract**; the letter is a summary of it |
| `frame= seed= faketime= ramdisk= build= exe=` | the pins and the binary identity, so a restored run is traceable to what produced the file |

Read it from the running process, not from here: the `restore` ack carries the
manifest text verbatim after its `skipped=` field, and
`python tools/a2rail/artifacts.py snap <file>` reads the `GSPM` trailer straight
off disk. A worked IIgs example — not a contract, a sample — is
`class=C devices=adb,ensoniq,vgcint,iwm,pdblock` with `scanner` and `media`
still in `excluded=`.

> **A restored machine cannot accept keyboard input — measured before device blocks
> existed, and true today only of a snapshot that holds neither `adb` nor `vgcint`.**
> The ADB micro and the VGC interrupt state are now saved and restored on a IIgs
> (`M5`), which is what closed this; check the manifest's `devices=` rather than
> assuming either way. The measurement below is kept because it is the reason those
> blocks exist and it still describes a pre-device-block file exactly.
>
> Measured on GS/OS 6.0.1 at the Finder desktop: a cold-booted machine reads `$C000`
> **1,213 times** over 2,000 frames and keeps polling indefinitely; a machine restored from
> a snapshot of that same desktop reads it **zero** times, ever. The Finder falls back to
> polling only `$C025` from one ROM routine. Injected keys land correctly in the ADB latch
> and are simply never collected.
>
> Two consequences. Any workload that needs input **must cold-boot** — restoring is what
> makes a workload cheap and is exactly what makes it unable to be driven. And a restored
> desktop is not behaviourally identical to a booted one: measured over comparable windows
> (~3.005 M instructions each) the restored desktop shows **5.32%** of stores externally
> visible against **6.20%** cold-booted, so a snapshot understates display-side traffic by
> about a sixth. Fine for measuring boot phases; not fine if the number is load-bearing.

## Assert — gate the exit code

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `A2GSPU_ASSERT="name OP val[;…]"` | `;`/`,`-separated `name OP value` checks (`OP` ∈ `== != <= >= < >`); the spike exits non-zero if any fails. Fields: `scb_mode`, `qd_carry/qd_err`, `lt_carry`, `cs_carry/pr_carry`, `gsos_err`, `nonzero/distinct`, `idxN` (N=0..15), and `peek:HHHHHH` (observation-free 24-bit byte read) | assert | env |
| `A2GSPU_STRICT=1` | exit 3 at spike end if the run printed any `WARNING` line (see `GSDIAG: warnings=N` below). Missing inputs and unopenable sinks are already fatal (exit 2) without it | assert | env |
| `A2GSPU_EXPECT_HASH=<hex16>` | the SHR-window (IIgs) or HGR-page (II-family) hash the run must end on; prints `MATCH`/`MISMATCH` with both values and gates the exit like `GOLDEN`, without a file. The way a harness pins a row, and the way a differencing harness proves the shorter run reproduced before subtracting | assert | env |
| `A2GSPU_GOLDEN=<file>` | hash the SHR window (`$E1:2000-9FFF`, house FNV-1a-64); if the file exists compare (DIFF ⇒ gate fail; corrupt/empty file ⇒ hard fail); if absent, bless it | assert | env |

*Always-on:* at spike end `iigs_emit_status` prints one machine-readable line —
`GSDIAG: status=<OK\|GATE_FAIL\|CRASH_BRK\|HANG\|GSOS_ERROR\|STALLED> rc=N gate=<PASS\|FAIL\|none>
gsos_err=$XXXX brk=N scb=NNN hash=................ warnings=N cycles=N`. The **process exit code** stays
golden/assert-gate-driven (`0`/`1`) for harness compatibility; `status` is the richer category.
`warnings=N` counts every `WARNING` line the run printed — a skipped injection, a rail that could
not do what it was asked — and `A2GSPU_STRICT=1` turns a non-zero count into exit code 3. A missing
input file (a disk image under `-n`, `RUN_BIN`, `SNAP_LOAD`, `KEYSCHED`, `MILESTONES`, `TRAP_FILE`) or
an output sink that cannot be opened (`WATCH_OUT`, `CALLSTREAM`, `ITRACE_OUT`, `TRAP_OUT`, the WIZ5
sinks) is not a warning: it aborts at arm with exit code 2, because a run that measured something
other than what it was asked to is worse than no run.

## WIZ5 — the title-specific streams, and the aliases that outlived them

These are the last per-project rails in the fork: three PC traps compiled into
`src/wiz5_config.hpp` for one 1988 title, plus the legacy spellings of rails
that have since been generalised. `Docs/INSTRUMENTATION_SHAPE.md` section 1 names
them as a known exception and M10 sets the retirement condition — they stand
until their consumers read the generic `A2GSPU_TRAP` records. They are here
because an undocumented rail is the black box the registry exists to remove, not
because a new consumer should reach for one: **for new work use `A2GSPU_TRAP`.**
Their record shapes are the three that predate the M1 envelope (`t=`, `op=`,
none) and are the named exemption in `railparity.ps1`.

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `WIZ5_EC19_OUT=<file>` | PC trap at `$EC19` (SCREENDEV driver entry) → NDJSON `t=e\|x` records. Rides the generic-tap per-instruction gate | wiz5 | env |
| `WIZ5_DISKWR_OUT=<file>` | PC traps at `$DF09`/`$DF04` (UNITWRITE/UNITREAD) → `op=BLKWR\|BLKRD` records with unit, block, length and data | wiz5 | env |
| `WIZ5_CSP6_OUT=<file>` | PC trap capturing CSP-6 eval-stack entries (no tag; `ipc ts_ms sp page1 zp bufs[]`) | wiz5 | env |
| `WIZ5_TAP` | legacy alias for `A2GSPU_TAP`. `A2GSPU_TAP*` is canonical; the alias lets one merged tool serve the wiz5/pascal projects with no script change | alias | env |
| `WIZ5_SESSION` | legacy alias for `A2GSPU_TAP_SESSION` | alias | env |
| `WIZ5_ZP_EVERY` | legacy alias for `A2GSPU_TAP_ZP_EVERY` | alias | env |
| `WIZ5_DUMP_EVERY` | legacy alias for `A2GSPU_TAP_DUMP_EVERY` | alias | env |
| `WIZ5_KEYS` | legacy alias for `A2GSPU_KEYSCHED` | alias | env |
| `WIZ5_KEY_DELAY` | legacy alias for `A2GSPU_KEYSCHED_DELAY` | alias | env |
| `WIZ5_KEYRAM` | legacy alias for `A2GSPU_KEYRAM` | alias | env |
| `WIZ5_WRITEBACK` | legacy alias for `A2GSPU_WRITEBACK` | alias | env |
| `WIZ5_NO_WRITEBACK` | legacy alias for `A2GSPU_NO_WRITEBACK`; the suppression is honoured under **either** spelling | alias | env |

## Device / determinism — reproducible oracle knobs

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `A2GSPU_SEED=<n>` | seed the cold-boot SP entropy (`srand`) so a from-scratch spike is byte-reproducible (warm restores overwrite SP anyway) | determinism | env |
| `A2GSPU_FAKETIME=<epoch-s>` | freeze the RTC at that instant (bypasses `localtime`/timezone) so RTC-derived OS state is run-to-run stable | determinism | env |
| `A2GSPU_RAMDISK=1` | serve a mounted image from a copy-on-write RAM buffer; writes never touch the host file | device | env |
| `A2GSPU_SP_NDEV=<n>` | override the SmartPort device count (A/B faithfulness; `32` reproduces pre-fix enumeration) | device | env |
| `A2GSPU_SERIAL_PORT=<COMx>` | override the real-card CDC probe port (default tries `COM10` first) | device | env |
| `A2GSPU_SERIAL_DEBUG=1` | verbose real-card serial/reconnect logging | device | env |
| `A2GSPU_OUT_DIR=<dir>` | directory prefix applied to rail output filenames. An absolute path (or a Windows drive letter) is honoured verbatim, so a path a caller chose explicitly is never rewritten | meta | env |

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
| `run <frames>` | step N frames; reply `status=OK ran=N cycles=N (in-command) PC $xxxx->$yyyy`, or `status=HALT ran=N …` if the guest halted inside the call. **Match this as a PREFIX, not a whole line:** `arm_cpu()` appends ` [cleared RESET]`, ` [cleared STOP]` or ` [cleared RESET+STOP]` when it had to un-assert the CPU before advancing, and the first `run` of a session is exactly the case that does -- so the most common ack an agent will ever meet carries the suffix. (It never replies `ok`, and never `halted@<i>` — that belongs to `press`/`key`, which report which injected key was consumed) | CTRL-verb (run) |
| `keys <str>` | append to the keyboard paste buffer (paced by guest polls) | CTRL-verb (manipulate) |
| `press <hex2> [hold] [mods]` | full physical keypress — latch + any-key-down, held `hold` frames (default 15), then released; IIe latch path AND IIgs KeyGloo+IRQ path. `mods` is letters `o` (Open-Apple) `c` (Closed-Apple) `s` (shift) `k` (control), carried in the key's modifier byte (what `$C025` and the Event Manager report): `press 6F 2 o` is Open-Apple-O, which is how the Finder is told to **open** a selected icon — a bare Return on one starts a rename (measured) | CTRL-verb (manipulate) |
| `holdkey <hex-ascii>` | set the sticky IIgs hold-key WITHOUT running (`0` clears) — hold, step in chunks, watch for consume, release | CTRL-verb (manipulate) |
| `iolog` | cumulative keyboard soft-switch read counts (`C000/C010/C024/C025/C026`); diff two calls across a `run` to see which switch a wedged menu polls | CTRL-verb (observe) |
| `cpu` | 65816 state: `PC=BB:AAAA A X Y SP=16bit P E HALT STP RDY KBD AKD` — distinguishes waiting-for-key vs halted vs executing | CTRL-verb (observe) |
| `dis <hexaddr> <n> <file>` | disassemble N instructions via the debugger `Disassembler` (reads through the MMU: bank/langcard-correct) to `<file>` | CTRL-verb (observe) |
| `text <file>` | dump the text page to `<file>` | CTRL-verb (observe) |
| `shot <file>` | update the display and save a screenshot to `<file>` | CTRL-verb (observe) |
| `read <hexaddr> <len> <file>` | dump `len` bytes from `<addr>` via `probe_peek` (side-effect-free, bank-correct on `-p 5`) to `<file>` | CTRL-verb (observe) |
| `mount sXdY <path>` | runtime media swap (save-and-unmount the drive first so writes persist) | CTRL-verb (manipulate) |
| `save <file>` | CPU regs + full MMU snapshot + sentinel + manifest (take at a quiescent point) | CTRL-verb (snapshot) |
| `restore <file>` | restore a `save`d snapshot and force-run. The ack is `status=OK restore <file> manifest=present\|absent devices=<restored> skipped=<skipped> \| <the manifest text>` — there is no `class=` field of its own; the letter is inside the manifest text, which the ack now carries verbatim instead of leaving it only in the session log | CTRL-verb (snapshot) |
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
| `rail find <NAME>` | one rail's family, synopsis, `live=<verb>` or `launch-only`, and `read=fn:…\|verb:…\|none` | CTRL-verb (meta) |
| `rail live` | the declared reachability table, checked: every row names a registered rail and an implemented verb, or the ack is `FAIL reach-table-inconsistent` | CTRL-verb (meta) |
| `rail read <NAME>` | the rail's state now: counters for `MEMVU_*`, per-spec hits/records for `A2GSPU_TRAP`, the liveness sample state, the warning count, the determinism pins and binary identity; `read-via-verb` when a CTRL verb owns the readback; `FAIL no-readback` when the rail speaks only at spike end | CTRL-verb (meta) |
| `rail schema [NAME]` | bare: the rails that write NDJSON records and their tags; with a name: `tag=` and `fields=` (`?` marks optional fields) and a note. `FAIL no-records` for a rail that writes none. The same table is the `record_schemas` section of `manifest <file>`, so a consumer validates against the binary, not the C++ | CTRL-verb (meta) |
| `valtrap set <hex>[:w]` / `clear` / `status` | LIVE value-provenance trap (A2GSPU_VALTRAP) | CTRL-verb (break) |
| `itrace from <pc>` / `now` / `n` / `clear` | LIVE per-instruction trace | CTRL-verb (trace) |
| `tbtrace on\|off\|bank` | LIVE toolbox/GSOS dispatch trace | CTRL-verb (trace) |
| `callstream on <file>` / `off` / `status` | LIVE NDJSON toolbox/GSOS call stream | CTRL-verb (trace) |
| `key` | inject key + wait for consume receipt | CTRL-verb (manipulate) |
| `protocol` | CTRL v2 framing, ownership and `json <command>` reply wrapper | CTRL-verb (meta) |
| `systems [file]` | platform/configuration/ROM/slot-device inventory | CTRL-verb (meta) |
| `limitations [file]` | known unsupported or partial fidelity surfaces | CTRL-verb (meta) |
| `shr-golden <file> [bless]` | live IIgs SHR-window golden without relaunch | CTRL-verb (calibrate) |
| `audio [status]\|doc\|sfx\|on\|off\|reset` | generated sample streams per SOURCE (`doc`=Ensoniq, `sfx`=drive sounds; never summed): count, non-silent, min/max, average, and the same per channel so a dropped side is visible. Register round-trips pass on a chip emitting silence; this does not | CTRL-verb (observe) |
| `boot <slot>` | enter slot firmware at `$Cs00` (1–7) the way autoboot would — for a block device the ROM's own autoboot cannot reach | CTRL-verb (run) |
| `bp [addr\|off]` | interactive breakpoint; bare reports state. A hit **halts** — pair with `resume` or `run`/`step` | CTRL-verb (manipulate) |
| `copilot` | the windowed agent rail's contract (`A2GSPU_COPILOT`): observe + pause/key/mount/quit with human input still live | CTRL-verb (meta) |
| `cov [status]\|on <LO-HI>\|map <A2MAP1>\|off\|reset\|write <file>` | execution coverage: raw `A2COV1`, or source-bound `A2COV2` once a plan is mapped. The write ack carries `format=` and `hash=`; read either with `tools/a2rail/artifacts.py cov`. Mismatched executed bytes refuse the write rather than blessing stale evidence | CTRL-verb (observe) |
| `devstate [name\|all <file>]` | per-device state the emulator already computes (ADB regs, disk head, mouse clamp, DOC, memory map); bare lists handlers. Read before assuming a slot is populated | CTRL-verb (observe) |
| `hgr` | coarse ASCII view of an HGR page — "is anything drawn". Not pixel-exact; use `pngc` for colour truth | CTRL-verb (observe) |
| `joymode [status]\|gamepad\|mouse\|atari` | controller shape at `$C061-$C063`, and whether the Joyport post-reset suspend window is still open. The only way to reach Atari/mouse modes headlessly | CTRL-verb (manipulate) |
| `memvu [status]\|store\|load\|reset` | live `MEMVU_STOREVIS`/`LOADVIS` counters, readable and zeroable mid-session so a measurement can be a bounded window. **Refuses** rather than reporting zeros when the rail is unarmed | CTRL-verb (observe) |
| `paddle <0-3> <0-255>` / `paddle off` | analogue paddle; seeds the 558 decay so `PREAD` counts the value back | CTRL-verb (manipulate) |
| `pause` | stop the machine (`EXEC_PAUSED`), the Machine▸Pause equivalent. Matters on the **windowed** `A2GSPU_COPILOT` rail, where frames run on their own; a headless CTRL session is already stopped between commands | CTRL-verb (manipulate) |
| `pause-status` | `paused\|step\|running` plus the raw `execution_mode`, without changing it | CTRL-verb (observe) |
| `pbutton <0-2> <0\|1>` | game switch PB0–PB2 (`$C061-$C063`) | CTRL-verb (manipulate) |
| `reset [cold]` | the guest reset path (warm by default, `cold` for a restart) — recover a wedged machine | CTRL-verb (manipulate) |
| `resume` | clear a user halt (breakpoint / `save-at`) and unpause, so the CPU advances again. **Refuses** to paper over a jammed CPU: a halting instruction needs `reset` | CTRL-verb (manipulate) |
| `screen` | text-page helper | CTRL-verb (observe) |
| `session [status]\|reset` | what probe state is armed, and one command to disarm all of it. Names what it cannot reach rather than reporting a confident zero. Run it before trusting a measurement, and before handing the machine on | CTRL-verb (meta) |
| `shr <file> [scale]` | IIgs super hi-res as PNG, 320 or 640 per line by SCB | CTRL-verb (observe) |
| `speaker` | `$C030` toggle count and the c14m stamp of the last one — sound is WHEN, not whether | CTRL-verb (observe) |
| `tbuf [status]\|on\|off\|clear\|dump <file> [n]` | the CPU trace ring: per-instruction registers, effective address, data, read vs write. Lighter than `itrace`; use it for a few instructions, `itrace` for a whole path | CTRL-verb (trace) |
| `verify <addr\|BANK:addr> <len> <file>` | compare guest memory to a host file **inside** the emulator and answer with the verdict: identical, or the first differing offset and both bytes | CTRL-verb (observe) |

> **This table is checked.** `tools/a2rail/railparity.ps1` fails if the CTRL
> dispatcher matches a command word with no row in the verb registry, or the
> registry names a verb the dispatcher never matches. It does **not** check that
> a verb has a row *here* — `help` and `manifest <file>` are the authority for
> the set; this table carries the semantics. `pause` and `pause-status` were
> dispatched and in neither surface, which is how `bogusverb` came to be answered
> "not a verb. `help` lists all of them" — a false statement the process made
> about itself.

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
