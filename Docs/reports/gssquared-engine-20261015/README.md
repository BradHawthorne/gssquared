# gssquared-fork: drift stage A applied, and the GSnux engine's device model (2026-10-15)

Owner-approved work in `D:\Projects\gssquared-fork`. Everything is local and nothing was pushed. Remotes are unchanged
(`origin` is the public fork, `upstream` is jawaidbazyar2/gssquared). The fork's public branch carries no private
names; this report may.

**Before:** `a2gspu` was clean at `e6462493`, with one branch and no stash.

**After:**

| Branch | Head | State |
| --- | --- | --- |
| `a2gspu` | `053a4e14` | 63 commits ahead of `origin/a2gspu` (was 53). The working tree is clean and checked out |
| `local/slot-engine-model` | `c94d5d4f` | Branched from `053a4e14`. No upstream tracking. **Never to be pushed**: the owner's interfacing methodology stays private until released |

## (a) Drift stage A: the nine take-now commits

Fetched through the fork's existing `upstream` remote (upstream `main` is still `643e6c21`). They were cherry-picked onto
`a2gspu` with `-x`, in the assessment's trial order. Each is single-parent, keeps upstream's author (Jawaid Bazyar), and
carries the porting trailer.

| Upstream | Fork | What |
| --- | --- | --- |
| `7634b151` | `7481a130` | DOC `$E0` acknowledges the lowest pending oscillator IRQ (the IIgs interrupt fidelity a DOC-timer tick relies on) |
| `b22a2345` | `07f8efea` | #191: pointer ignored while another window has mouse focus |
| `87f56ac9` | `8def93c6` | #194: speed-boost save and restore, factored out |
| `cc0a9eec` | `fa62f107` | The adbtest link |
| `d5b63752` | `6cbc3395` | Second Sight 80×50 and 132×60 text |
| `599db0ae` | `9dcb330e` | IIe keyboard: legacy and modern glyph locations |
| `7eb29076` | `cd2e4508` | Either gamepad face button fires |
| `293e0fc1` | `a0936b0e` | Windows build information (upstream's agent section) |
| `cb8ff63a` | `854e40b7` | `.hda` (BlueSCSI) images |
| (fork docs) | `053a4e14` | `Docs/UPSTREAM_SYNC.md`: a ledger row and section 23. The triage's tallies are given without reference to this private record |

**Verified** with a clean Release build in a scratch clone at `854e40b7` (`D:\gsqs\stagea`, the assessment's recipe:
vendored trees, the ROM 3/ROM 4 images and `corpus/` copied in):
- the build had 86 warnings, the same as the baseline;
- **ctest 12/12; 12 gates all green;**
- **every unarmed baseline unchanged:**

  | Boot | Hash | Cycles | Run |
  | --- | --- | --- | --- |
  | `iigs-rom` | `6D9886E3B3EE0383` | 6,236,744 | `800AEA5075763504` |
  | `iie-rom` | `92F1E19D6FE2A383` | 2,043,601 | `9D45F479767B943C` |
  | `iigs-gsos601` | `D29F204D690D417F` | 10,647,413 | `9CFCBEBB3B604B4E` |
  | `iigs-finder` | `2E304BF0147B220A` | 22,377,216 | `994DCC2DF8B6A75A` |

- hwtruth's 24 verdicts unchanged; MB-Audit passes.

**The Second Sight follow-up is a static check only:**
- block `ssight` saves the whole host-text raster plus `vga_mode_num`;
- 132 columns and 1,056 px fit the saved widths;
- restore has no mode allowlist, so the new modes round-trip without a block change;
- a save and restore exercised in those modes is still owed.

## (b) The engine's device model (`local/slot-engine-model`, `c94d5d4f`)

**An independent behavioural model written from `gsnux/engine/SPEC.md` (v0) alone.** No firmware source was read or used
(the owner's boundary: no picovu, a2gspu, A2Fusion PB/PV or engine firmware code in the fork). The neutral name is
"Pico2Lite engine (test register set)".

**Files:**
- `src/devices/pico2lite_engine/`:
  - `pico2lite_engine_core.hpp`: the header-only register model;
  - `pico2lite_engine.{hpp,cpp}`: the slot wiring, rail, devstate and snapshot;
- `apps/pico2liteenginetest/`: the test, plus an example `.gs2`;
- thin hooks in `Device_ID.hpp` (`DEVICE_ID_PICO2LITE_ENGINE` after `A2GSPU`), `devices.cpp`, `device_info.cpp`,
  `util/SystemConfig.cpp`, `util/SettingsConfig.cpp` and `CMakeLists.txt`;
- the rail in `a2gspu_rails_registry.hpp` and `Docs/A2GSPU_RAILS.md`;
- block `p2le` in `a2gspu_snapshot.cpp`;
- a hooks row in `UPSTREAM_SYNC.md`.

**Time:** the microsecond counter is `c14M × 1,000,000 / c14M_per_second`, using the 14M master clock (14,318,180/s
NTSC, 14,250,450 PAL). That's emulated elapsed time, **independent of the CPU speed mode**: in 1.02 MHz slow mode,
2.8 MHz fast mode, or faster, a microsecond is the same 14.318 14M ticks, as a card with its own crystal sees it. The
tick is an event-timer edge at `ceil(due_us × c14M_per_second / 1e6)`.

**Where the spec was silent, the choices made** (please confirm or change in SPEC.md):
1. `CMD $02` while already enabled is a no-op (the cadence stands).
2. `CMD $03` (disable) also clears a pending tick, releasing /IRQ.
3. Period 0 with a tick pending: the tick stops, and /IRQ holds until acknowledged or disabled.
4. A new period written while enabled restarts the cadence from that write.
5. An unacknowledged tick stays pending: /IRQ is a level, not a count.
6. After a long stop the cadence resumes one period from now, with no burst.
7. Warm RESET keeps the counter running and the period. A **cold** start (power) restarts the counter at 0 and reseeds.
8. The write-only registers (+2, +8, +9) read `$00`.
9. The RNG byte is always ready (STATUS bit 2 is always 1).
10. The default generator is splitmix64 with seed `0x5EED2354`, XORed per slot so two cards differ.

**How to use it:**
- **In a config file:** `card = "pico2lite_engine"`. The example is
  `apps/pico2liteenginetest/fixtures/IIgs_Pico2LiteEngine.gs2`, slot 4. Start a session with
  `tools\a2rail\session.ps1 -ConfigFile <that file>`.
- **The random source:** `A2GSPU_PICO2LITE_RNG` unset gives the fixed seed (runs repeat); `host` seeds from the host;
  a number is the seed; anything else exits 2. `rail read A2GSPU_PICO2LITE_RNG` says which.
- **Inspection:** `devstate pico2lite_engine` shows the counter, latch and tick, and snapshot block `p2le` holds the
  card.

**Tests:**
- **Unit:** `pico2liteenginetest`, **53 checks**, registered in ctest with a non-zero-count PASS regex. It covers IDs and
  reserved registers, latch consistency and wrap, restart, the tick's assert, acknowledge, cadence, no burst, period
  changes and disable, RESET, the signature page against the ProDOS and Pascal 1.1 signatures, and the generator,
  pinned to an independent splitmix64.
- **Through the real bus** (`D:\gsqs\p2le-e2e\e2e.ps1`, scratch): a IIgs with the card in slot 4, and a 65816 stub in
  bank 0 (`SEI`; DBR=0):

  | Read | Value | Meaning |
  | --- | --- | --- |
  | ID0, ID1 | `$E6`, `$00` | |
  | STATUS | `$84` | alive, byte ready |
  | CNT | 500,694 µs | after 30 frames |
  | the tick | 1,116 µs after enable | one 1,008 µs period plus loop overhead |
  | CAUSE | `$01` | |
  | STATUS after the acknowledge | `$86` | |
  | `$C4F8`, `$C4F0`, `$C4F9`, `$C401` | `$E6`, `'S'`, `$00`, `$00` | with INTCXROM off |

  A `save`/`restore` carried block `p2le` (the manifest lists it), and devstate matched after the restore.
- **Gates:** ctest 13/13. **12 gates green with the model built in** (railparity accepts the new rail, reader and doc
  row), and all four unarmed baselines unchanged (as above).

## Findings for GSnux

1. **SPEC §5 "Detect" needs a IIgs caveat.**
   - **What happened:** on a IIgs the `$Cn00` page reaches the card only when **INTCXROM is off** and **`$C02D` selects
     the slot**. After a ROM boot INTCXROM is on, and the first e2e pass read the internal slot-4 firmware (`$85` at
     `$C4F8`).
   - **I/O decoding differs by slot:** the fork models `$C02D` as gating slots 1, 2, 5, 6 and 7's `$C0nX` I/O too
     (slots 3 and 4 always give their I/O to the card) [HW Ref Table 8-2].
   - **So the driver should:** probe `$C0n0`=`$E6` first (valid for slots 3 and 4 always, and for the others when
     `$C02D` selects them), and set the slot's `$C02D` bit and clear INTCXROM before reading the signature page.
2. **Session determinism:** an unpinned CTRL session's boot varies (594,857 against 597,602 cycles in 30 frames), so
   absolute counter values differ run to run. Pin `A2GSPU_SEED` (and `A2GSPU_FAKETIME`) for repeatable engine runs.
   The model itself is deterministic.

## Left open

- **The fork's in-tree `build\` is stale** (from 2026-09-17), as it was before. Neither stage A nor the model is
  built there; everything was built and gated in `D:\gsqs\stagea`. Rosetta's differential harness and other consumers
  use the in-tree binary, so rebuild it (`cmake --build build`) when they should see stage A.
- **The e2e script is scratch,** not a committed gate. A hwtruth verdict needs the real card.
- **Stages B-E** await approval (`UPSTREAM_SYNC.md` §23).
- **The Second Sight round-trip in the new modes** is unexercised.
- **Scratch to delete when done:** `D:\gsqs\stagea`, `D:\gsqs\p2le-e2e` and `D:\gsqs\logs-stagea`.
