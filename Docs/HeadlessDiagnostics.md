# Headless Diagnostics — the env-gated spike harness

GSSquared has a **headless "spike" mode** for automated and CI testing of Apple IIgs / GS/OS bring-up: run
a fixed number of deterministic frames, then dump, assert on, or golden-hash the emulated machine state and
exit with a meaningful code. Every knob is an **environment variable**, off by default.

All of these are **observe-don't-disturb**: they observe the emulated machine externally (like a logic
analyzer) or place it in a deterministic test condition a real machine could equally be in. They never
change how the emulated hardware behaves on the bus — code that runs under a probe runs identically without
one.

## Running a spike

| Variable | Effect |
|----------|--------|
| `A2GSPU_SPIKE_FRAMES=N` | run `N` frames headless, then evaluate the dump/assert/golden gates and exit |

```sh
A2GSPU_SPIKE_FRAMES=200 GSSquared -p 5 -d s7d1=mydisk.po -n
```

`-n` runs without opening a window. The spike is deterministic frame-for-frame.

## Tracing and breakpoints

| Variable | Effect |
|----------|--------|
| `A2GSPU_TBTRACE` | trace Tool Locator dispatch (`$E10000`) and GS/OS class-0/1 dispatch (`$E100A8`/`$E100B0`): call name on entry, carry + A (error code) on return |
| `A2GSPU_ERRHOOK` | surface carry-set GS/OS dispatch errors (`<-- SYSTEM/LOADER ERROR`) |
| `A2GSPU_BRKDUMP` | dump CPU registers on `BRK` (a crash under GS/OS) |
| `A2GSPU_STOP_ON_FAULT` | halt the spike on `BRK`/`COP`/SysFail instead of burning the remaining frames |
| `A2GSPU_BREAK=BBAAAA` | headless breakpoint at a 24-bit PC |
| `A2GSPU_TBTRACE_BANK`, `A2GSPU_TRACE_FROM`, `A2GSPU_SYM_BASE`, `A2GSPU_SYMBOLS` | scope/annotate the trace (only-this-bank, start-when-PC-hits, symbol table) |

The call-name table is generated; it maps the 16-bit call word `(func<<8)|toolset` to a public name.

## State dumps

| Variable | Effect |
|----------|--------|
| `A2GSPU_DUMP="BANK/LO-HI[,…]"` | hexdump memory ranges (hex). `$E0`/`$E1` read the Mega II image directly (side-effect-free); other banks via the MMU |
| `A2GSPU_VIDEOSUM` | per-line SCB (320/640) + palette histogram, palettes as RGB888, mode-correct pixel-index histogram |
| `A2GSPU_VIDEOMAP` | a 40×25 ASCII dominant-index map of the Super Hi-Res window |

## Deterministic clock — `A2GSPU_FAKETIME`

| Variable | Effect |
|----------|--------|
| `A2GSPU_FAKETIME=<unix-epoch-seconds>` | the RTC reports that instant **frozen**: the seconds registers read identically across runs |

Without it the RTC tracks the host wall clock (so the seconds registers — and any GS/OS state derived from
them — vary run to run, defeating reproducible goldens). `A2GSPU_FAKETIME` seeds the RTC's differential
accumulator to `epoch + UNIX_EPOCH_DELTA` and **bypasses `localtime`/timezone entirely** (freezing `time()`
alone is insufficient — `tm_gmtoff`/`_timezone` still vary by host). The emulated OS reads the result
through the ordinary `$C033`/`$C034` register path, exactly as it would a real RTC stopped at that instant.

```sh
# seed 1000000000 -> the seconds registers read 0xB7C07A80 (lo=80 ntl=7A nth=C0 hi=B7), every run
A2GSPU_FAKETIME=1000000000 GSSquared -p 5 -d s7d1=mydisk.po -n
```

## Assertions — `A2GSPU_ASSERT`

A `;`/`,`-separated list of `name OP value` checks (`OP` ∈ `== != <= >= < >`); the spike exits non-zero if
any fails.

| Field | Meaning |
|-------|---------|
| `scb_mode` | 320 or 640 (line-0 SCB bit 7) |
| `qd_carry` / `qd_err` | QuickDraw `QDStartUp` ($0204) carry / error |
| `lt_carry` | `LoadTools` ($0E01) carry |
| `cs_carry` / `pr_carry` | `ClearScreen` ($1504) / `PaintRect` ($5404) carry |
| `gsos_err` | last GS/OS dispatch error code |
| `nonzero` / `distinct` | SHR-window byte stats |
| `idxN` (N=0..15) | mode-correct pixel count of palette index N (e.g. `idx6==28800` ⇒ a 240×120 colour-6 rectangle) |
| `peek:HHHHHH` | **observation-free byte at the 24-bit address** `bank<<16\|addr` (e.g. `peek:E11E00`, `peek:0003FC`) — for structural asserts on toolset work-area pointers and `$E1` structures |

```sh
A2GSPU_ASSERT="scb_mode == 640; gsos_err == 0; peek:E10000 >= 0" …
```

## Golden — `A2GSPU_GOLDEN`

| Variable | Effect |
|----------|--------|
| `A2GSPU_GOLDEN=<file>` | hash the SHR window (`$E1:2000-9FFF`); if the file exists, compare (DIFF ⇒ gate fail); if not, bless it |

## The status line + exit taxonomy

At the end of a spike, one machine-readable line is printed for a harness to parse:

```
GSDIAG: status=<name> rc=N gate=<PASS|FAIL|none> gsos_err=$XXXX brk=N scb=NNN hash=................
```

| `status` | Meaning |
|----------|---------|
| `OK` | clean run |
| `GATE_FAIL` | a golden/assert gate failed |
| `CRASH_BRK` | one or more `BRK`s executed (the crash canary `g_iigs_brk_count`) |
| `GSOS_ERROR` | a GS/OS dispatch returned a carry-set error |

The **process exit code** stays driven by the golden/assert gate (`0`/`1`) for harness compatibility; the
`status` *name* is the richer diagnostic category.

## For developers — `MMU::probe_peek`

The `peek:` assert is backed by `MMU::probe_peek(uint32_t addr24)`, a **virtual** observation-free 24-bit
read: the base `MMU` returns `read_raw` (the page-table pointer, no IO/handler dispatch); `MMU_IIgs`
overrides it to route `$E0`/`$E1` to the Mega II image. It never invokes a soft-switch handler, emits a
slot-bus event, or ticks the clock — so a harness can read emulated memory without the CPU or the emulated
OS being able to observe the access. Use it (not `read()`/`vp_read()`) for any structural state probe.
