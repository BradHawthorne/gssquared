# gdiff — golden-differential runtime toolkit (self-validating)

First-class, durable home for the emulator-trace differential tools that cracked most
of the owned-GS/OS boot campaign (SIG-001..031). Promotes the scratchpad one-offs —
reconstructed almost every `/debug` cycle — into one coherent, versioned, **self-validating**
module, and hardens the exact **lying sensors** that cost the campaign hours and false
graveyards.

Scope: **RUNTIME / emulator-trace diffs** (this dir) + the **run-until boot harness**.
The build-time per-segment byte-diff (`segdiff`) lives with the Rosetta side (R2), not here.

> Everything here is READ-ONLY analysis. The tools never change the emulator's boot
> behaviour; the C++ self-checks are gated behind their env vars (one untaken branch when
> off) and the flag-OFF determinism golden is unchanged (verified: gsos601.po 150f →
> E1 `108683C9C967EEBF`, MMU `0925578EE375CA76`, status=OK).
> Use `python` on this box (`python3` is a Windows Store alias). Not committed (Brad-gated).

## One entry point

```
python gdiff.py <subcommand> [args]      # or run any module standalone
python gdiff.py --help
python gdiff.py selftest                 # 30 checks against the real campaign cases
```

| subcommand  | what it does | the lying sensor it kills |
|-------------|--------------|---------------------------|
| `wdiff`     | positional align of two `A2GSPU_WATCH` NDJSON streams ($E1 queue) + auto little-endian record-pointer decode at the divergence | manual byte-by-byte pointer decode |
| `itrdiff`   | execution-trace resync diff (shift-tolerant `--nopc`, interrupt-skew resync, `@ea=val` DATA/CONTROL classification) | one extra IRQ line cascading into 1000s of false diffs |
| `overlay`   | overlay-aware OMF symbolizer + **dup-destination OVERLAY WARNING**; index-keyed (never dest-keyed); flags the LIVE overlay by ITRACE **content** | **SIG-031**: a dest-keyed dict collapsed two $01D000 segs → mislabeled init3 as check_express_seg |
| `watchspec` | validate a WATCH spec (`bank:lo-hi`; reject the bare-24-bit trap) + lint a WATCH NDJSON for probe_peek/$EE floating-bus liars | the "WATCH didn't fire" trap; the probe_peek `$EE` liar (×6) |
| `bearings`  | cross-check ≥2 sensor readings of one fact; loud "**suspect a liar**" on a split (trust-hierarchy) | trusting one confident-but-wrong sensor |
| `selftest`  | runs all of the above against `fixtures/` (the REAL captures) | regressions in the tools themselves |

`run_until.ps1` — the sibling **run-until boot primitive** (G3), below.

## Recipes

```bash
# The SCM notify-queue bug: ours 32 nodes vs pristine 64, first divergence + record ptr
python gdiff.py wdiff  fixtures/watch_ours.ndjson fixtures/watch_pris.ndjson
#   -> "FIRST DIVERGENCE at write 24 ... ours links a record at $E06014 vs pristine $E119D0"

# Overlay-aware symbolization of the SIG-031 address (names BOTH; flags live with --itrace)
python gdiff.py overlay fixtures/gsos_ours.omf fixtures/gsos_gold.omf --addr 01:DB2D --itrace some.itr
#   -> "** DUP-DEST OVERLAYS ... $01D000 <- segs [1, 12] [Loader_LC | init3] **"

# Before arming a watch, check the spec (catches the bare-24-bit trap at the tooling edge)
python gdiff.py watchspec E1:19A0-19A3      # OK
python gdiff.py watchspec E119A0            # ** BARE-24-BIT TRAP ** exit 1

# Two sensors disagree about the same fact -> surface the split, don't pick one
python gdiff.py bearings --fact "what@01D000" segdiff="two segs" dest_dict="one seg"

# Execution divergence between our boot and pristine (interrupt-skew tolerant)
python gdiff.py itrdiff ours.itr pris.itr --nopc --max 3
```

### Producing the inputs (from the emulator)

```
# WATCH NDJSON (wdiff):  arm the $E1 queue node, uncapped, to a file
A2GSPU_WATCH=E1:19A0-19A3  A2GSPU_WATCH_MAX=0  A2GSPU_WATCH_OUT=watch_ours.ndjson
# ITRACE (itrdiff/overlay live-flag):  PC-range window, @ea, to a file
A2GSPU_ITRACE_LO=00D000 A2GSPU_ITRACE_HI=00DFFF A2GSPU_ITRACE_OUT=ours.itr
```
Both drive through `run_until.ps1` (bounded) or the scratchpad runners.

## `run_until.ps1` — bounded run-until boot primitive (G3)

Formalizes `rb.ps1`. Runs the emulator until a **named condition** and returns a decisive
verdict; **never unbounded** (hard frame cap **and** a wall-clock `Stop-Process` watchdog —
the 371-CPU-sec wedge can't recur). Auto-classifies a **NO-BRK STALL** vs **SLOW-PROGRESS**
by comparing the state fingerprint `(SHR-hash, distinct, milestones)` across two checkpoints
— the ≥600-frame lesson, mechanized.

```powershell
& tools/gdiff/run_until.ps1 -Img .../owned_boot.po -Until stall -Frames 300 -Env @{A2GSPU_SP_NDEV="32"}
#  checkpoint-A cap=300 -> STALLED d26 m10/12 hash=196C114F...
#  checkpoint-B cap=450 -> STALLED d26 m10/12 hash=196C114F...   (identical)
#  VERDICT: NO-BRK STALL   (genuinely wedged; not a crash, not slow-progress)
```

Conditions: `BRK | HANG | COMPLETE | MILESTONE:<n|name> | DISTINCT:<n> | STALL | TIMEOUT`.
`-Until` is a comma-list of the positive conditions you're waiting for; exit 0 if met, 3 if
not (with the stall/progress classification always reported). Keeps the in-process
`& run_until.ps1 -Env @{...}` form and pins the oracle env (`-Ndev`, default 32).

## Validation (against the current boot state — reproducible)

`python gdiff.py selftest` → **30/30**. The fixtures are the ACTUAL campaign captures:
- **G1 wdiff** reproduces the `$E119A0` 32-vs-64-node diff at **write-24** (`$E06014` vs `$E119D0`).
- **G2 overlay** WARNING fires on the real `$01D000` Loader_LC/init3 overlap (SIG-031) and,
  with ITRACE opcode evidence, flags the live overlay (content match, since the windows overlap).
- **G2 watchspec** rejects `E119A0` (bare-24-bit) and accepts `E1:19A0-19A3`; probe_peek `$EE`
  bank $00/$01 lint fires.
- **G3** classifies the current splash-stall as **NO-BRK STALL** (300 vs 450 identical) and an
  early window as **SLOW-PROGRESS** (40 vs 200 advances).
- Emulator C++ self-checks: WATCH format-check fires on `E119A0` at the source; flag-OFF golden
  unchanged.

## Lying-sensor incidents these now prevent

| incident | before | now |
|----------|--------|-----|
| **SIG-031** dest-keyed dict collapsed two `$01D000` segs | init3 mislabeled `check_express_seg`; a burned /debug cycle + G-015/G-016 | `overlay` is index-keyed, WARNS on the dup, names BOTH, flags the live one by ITRACE content |
| **WATCH-format** bare-24-bit | "the write never happened" (silent dead watch) | `watchspec` + the emulator parse both WARN loudly (`E119A0` → arms `$A00000`, never fires) |
| **probe_peek `$EE`** (×6) | floating-bus byte trusted as memory truth | `probe_suspect()` / `watchspec --probe-lint` / the SYSFAIL dump flag bank $00/$01 `$EE` as SUSPECT |
| interrupt-skew | one extra IRQ line → thousands of false trace diffs | `itrdiff` resync reports "N extra line(s) on OURS/PRIS" and re-aligns |
| single confident sensor | trusted silently | `bearings` surfaces the split ("suspect a liar") |

## Files

```
gdiff.py        entry point / dispatcher
watchdiff.py    wdiff        watchspec.py   watchspec + probe lint
itracediff.py   itrdiff      bearings.py    bearings-split cross-check
overlay.py      overlay symbolizer (the SIG-031 anti-liar core; imported by others)
selftest.py     30 checks    run_until.ps1  G3 bounded boot harness
fixtures/       the real campaign captures (watch_*.ndjson, gsos_*.omf)
```
