# Bus access log

The bus access log answers, per CPU cycle: which address the 65816 used and how
(read, write, program fetch, vector pull, dummy access, internal cycle), whether
the cycle ran on the fast side or the slow (1 MHz Mega II) side, and how many 14M
ticks the IIgs timing model charged for it, with the reason for any extra cost.
It exists for cost models: before it, a slow-side access could only be estimated
from the address, and the per-cycle cost the clock computes was thrown away.

Since protocol minor 5 (`features buslog`, format version 1). Source:
`src/a2gspu_buslog.hpp`.

## What it observes

The CPU cycle counter counts one per cycle whatever the cycle costs; the cost of a
cycle is in 14M ticks (the 14.318 MHz master clock), and `NClockIIgs` computes it
per cycle:

| Part | 14M ticks | When |
| --- | --- | --- |
| `base` | 5 | a fast cycle at 2.8 MHz (the mode's ticks per CPU cycle) |
| `base` | 14 | a slow cycle: one 1 MHz (Mega II) cycle |
| `wait` | 0-13 | a slow cycle waits for the next 1 MHz edge first |
| `refresh` | 5 | fast RAM refresh: the counter gains each cycle's ticks; when it reaches 50 it drops by 50 and a fast RAM cycle costs 5 more (slow and fast-ROM cycles add their ticks and drop it by 50 without paying) |
| `stretch` | 2 | the scan line's long cycle, added to the cycle that completes the 65th video cycle |

The log records those parts next to the access the CPU core made in that cycle.
The record point is the clock (`NClockIIgs::slow_incr_cycles`), so every counted
cycle has exactly one record; the access comes from the core's bus helpers
(`bus_read`, `bus_write`, `fetch_pc`, `vp_read`, the phantom helpers).

Reasons (comma-joined, or `-`):

| Reason | Meaning |
| --- | --- |
| `megaii` | a Mega II RAM/ROM cycle: banks `$E0`/`$E1`, or `$C100-$CFFF` through the IOLC window |
| `io` | a `$C0xx` I/O cycle that reaches the Mega II (banks `$00`/`$01` with IOLC shadowing on, `$E0`, `$E1`) |
| `shadow` | a write the FPI also shadowed into the Mega II (text, hi-res, SHR pages per `$C035`) |
| `slowmode` | the whole machine runs at 1 MHz (`$C036` bit 7 clear, a disk motor with its speed bit set, or the 1 MHz clock mode) |
| `refresh` | the RAM refresh landed on this cycle |
| `stretch` | the scan-line long cycle landed on this cycle |
| `fastrom` | the MMU's fast-without-refresh cycle type: ROM (`$F0-$FF`, the IOLC ROM) and the FPI registers modelled that way (reads of `$C02D` and `$C068`, reads and writes of `$C035-$C037`) |

`megaii`, `io` and `shadow` are the MMU's reason for a cycle it asked to run slow;
the log derives them from the address and the access kind with the MMU's own
decode. A cycle that is slow only because the machine is at 1 MHz says `slowmode`.

Off, the log costs one branch per bus access in the core and one per cycle in the
IIgs clock. Measured on img816's flat runner (no bus-side model, 587k-cycle loop,
300 passes, four alternating runs): 1.375-1.408 s before the change, 1.378-1.403 s after.

## How to call it

On the CTRL rail (any machine; side and costs are `-` on a II/IIe clock, which has
no bus-side model):

```
buslog mark [cap]    clear the log and record every CPU cycle from now on
                     (default cap 1048576 records; later cycles count as dropped)
buslog               status=OK buslog v=1 on=1 dropped=0 n=6 cycles=6 c14m=56 fast=4
                     fast_c14m=20 slow=2 slow_c14m=36 megaii=2 io=0 shadow=0
                     slowmode=0 refresh=0 stretch=0 fastrom=0
buslog dump <file>   the records, format v1 (below)
buslog off           stop recording; the log is kept for report and dump
```

On flat memory images, without a running machine: `apps/img816`.

```
img816 --machine iigs --buslog LOG [--check EXPECTED] CASES
buslogcheck LOG [EXPECTED]
```

`--machine iigs` runs img816's cases on the IIgs memory map (FPI, Mega II, the
2.8 MHz IIgs clock and a IIgs video scanner, which advances the 1 MHz phase; an
all-zero 128 KB ROM image and 8 MB of expansion RAM). Each case starts with the FPI
registers at their reset values, at 14M tick 0 on a 1 MHz edge with the refresh
counter at 0. The case lines are unchanged (`cyc=` is still the CPU cycle count).
`--buslog` writes, per case, `case NAME`, one record per cycle and
`end NAME runner_cyc=N clock_c14m=N dropped=N`. `--buslog` also works on the flat
machine, whose records have side and costs `-`.

`buslogcheck` parses a log on its own: records numbered 0..n-1 with consecutive
cycle numbers, n equal to the runner's cycle count, each `c14m` the sum of its
parts, each record starting where the previous one ended, side slow exactly when
`base` is 14, and the 14M total equal to the clock's own advance. With EXPECTED it
also compares hand-written records (`case NAME` ... `end`) and summaries
(`expect NAME key=value ...`).

## Format v1

A dump starts with

```
# buslog v1 fields: seq cycle t14m addr kind value side c14m base wait refresh stretch reasons
```

then one line per CPU cycle, single spaces between fields:

| Field | Content |
| --- | --- |
| `seq` | record number from 0 at the mark (per case in img816) |
| `cycle` | the CPU cycle number of this cycle |
| `t14m` | the 14M tick the cycle started at (`-` without a bus-side model) |
| `addr` | 24-bit address, 6 hex digits; `------` for an internal cycle |
| `kind` | `read`, `write`, `fetch` (program bytes), `vector` (vector pull), `phantom-read`, `phantom-write` (dummy accesses the core performs), `skipped-read` (a dummy read the core counts but does not perform; the address is the one the cycle names), `internal` (no access) |
| `value` | byte read or written, 2 hex digits; `--` for `internal` and `skipped-read` |
| `side` | `fast`, `slow`, or `-` without a bus-side model |
| `c14m` | 14M ticks charged: `base + wait + refresh + stretch` |
| `base`, `wait`, `refresh`, `stretch` | the parts above (`base` is 0 when the free-run clock folds a fast cycle into a shared 14M tick) |
| `reasons` | the reasons above, comma-joined, or `-` |

Effective cycles at 2.8 MHz are `c14m / 5`; one 14M tick is 69.8 ns.

A change that adds a field or changes a field's meaning bumps the format version
(the header line and `v=` in the reply) and the `buslog` row's version in
`src/a2gspu_features.hpp`.

## Example: a 16-bit LDA long from bank $00 and from bank $E1

Native mode, m=0, code in bank `$00` (`apps/img816/buslog_cases.txt`, cases
`lda16_bank00` and `lda16_bank_e1`). Both take 6 CPU cycles.

```
# LDA $001234
4 4 20 001234 read CD fast 5 5 0 0 0 -
5 5 25 001235 read AB fast 5 5 0 0 0 -
# LDA $E11234
4 4 20 E11234 read CD slow 22 14 8 0 0 megaii
5 5 42 E11235 read AB slow 14 14 0 0 0 megaii
```

The instruction costs 30 14M ticks from bank `$00` and 56 from bank `$E1`: the
first slow read waits 8 ticks for the 1 MHz edge (the four fetches ended 6 ticks
into a 1 MHz cycle), the second starts on the edge.

## Tests

`apps/img816` (ctest):

- `img816_buslog_iigs` runs `buslog_cases.txt` on the IIgs map and checks the case
  lines against `buslog_cases.expected`; `img816_buslog_iigs_records` checks the log
  against `buslog.expected`, whose records were worked out by hand from the 65816 bus
  cycles and the timing model before the first run: a fast-RAM loop (all fast, two
  refreshes); the 16-bit LDA from `$00` and `$E1`; reads and writes of `$E0`/`$E1`
  and `$C0xx` (`$C029`, slow `io`; `$C036`, fast `fastrom`); a shadowed bank-`$00`
  write (slow `shadow`, the copy visible at `$E0`) next to an unshadowed one; the
  machine dropped to 1 MHz through `$C036` (`slowmode`); and a 321-cycle loop that
  crosses a scan line (one `stretch`, 32 refreshes, 1767 ticks).
- `img816_buslog_rejects_wrong_record`: the same log against `buslog.wrong` (one
  record's wait off by one) must fail.
- `img816_buslog_flat` / `img816_buslog_iigs_same_image` run the hand-checked
  `cases.txt` on both machines with the log on; the case lines must still match
  `cases.expected`, and `buslogcheck` checks that each log holds one record per
  counted cycle (and, on the IIgs map, that the 14M total equals the clock's).

`tools/a2rail/protocolcheck.ps1` checks the verb live: mark, an empty log, one step
logging one timed record per cycle, the v1 dump header, cap refusal, off.
