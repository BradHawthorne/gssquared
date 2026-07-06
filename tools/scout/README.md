# scout — the golden-splice-scout (gdiff G4)

The predictive control-forward superpower, **packaged**. Given a wall (a BRK/stall PC)
and a DELIBERATE splice, `scout.ps1` boots the emulator **forward, bounded**, and reports
**how far** it got and the **next wall** (PC + a class hint) — then optionally **repeats**,
forcing past N successive walls to MAP the remaining boot path ("spike the path, then pave").

This was PROVEN predictive during the owned-GS/OS campaign: the SIG-014 splice (POKE
golden's 6 bytes past a wall) forecast the durable fix's EXACT faithful outcome (advanced
to frame 336). scout makes that repeatable.

> **DELIBERATE, never faithful.** scout is READ-ONLY on the oracle: it only sets `A2GSPU_*`
> env and launches `GSSquared.exe`. The splice is a control experiment — the emulator prints
> a loud `IIGS POKE: *** DELIBERATE state injection ... NOT faithful HW behaviour ***` banner
> that scout echoes, and scout's own header says the same. A splice-forward is a **forecast**,
> not a boot solution. scout pins the G5 determinism manifest (`Set-OracleEnv -Mode Scout`)
> so the forward boot rides the same oracle env as the floor, minus the deliberate splice.

## Run

```powershell
# AUTO-spike: force past the current stall to the next on-path milestone, name the next wall
& tools/scout/scout.ps1 -Img .../owned_boot.po -Frames 300

# An explicit golden-bytes splice (the named write-24 $E06014 case), fired on the Nth PC hit
& tools/scout/scout.ps1 -Splice "FF0C64:ME119A0=D0;ME119A1=19;ME119A2=E1" -Nth 2 -Label "write-24 record-addr"

# Convenience form (builds "<At>:<Force>"); force a branch past a stuck loop
& tools/scout/scout.ps1 -At 00F963 -Force "PB=00;PC=FA42" -Label "cont->map_vrn"

# Map three hops of the remaining path (auto-spike successive milestones)
& tools/scout/scout.ps1 -Repeat 3
```

| param | meaning |
|-------|---------|
| `-Img` | boot image (default `owned_boot.po`) |
| `-Splice "<pc>:<acts>"` | a raw `A2GSPU_POKE` spec (POKE golden's bytes / force a branch) |
| `-At <pc> -Force "<acts>"` | convenience: builds `"<pc>:<acts>"` |
| `-Nth <n>` | fire the splice on the **Nth** hit of the trigger PC (`A2GSPU_POKE_NTH`) — targets a "write-N" wall (e.g. the 24th queue write) |
| `-Label "<text>"` | human name of the wall (banner) |
| `-Frames <n>` | forward frame cap (baseline + splice both), default 300 |
| `-Repeat <n>` | spike past N successive walls, mapping the path |
| `-Ndev <n>` | `A2GSPU_SP_NDEV` (oracle pin, default 32) |
| `-NoBaseline` | skip the baseline run (wall already known) |

**Splice actions** (from `A2GSPU_POKE`): `M<hex24>=<hexbyte>` (poke golden bytes),
`PB=`/`PC=` (force-branch), `A/X/Y/S/D/P/DBR=`, `RTS`/`RTL` (force-return), `SKIP=<n>`.

**If no splice is given, scout AUTO-spikes**: it forces from the last reached milestone to
the first not-reached on-path milestone (`PB=;PC=`), so a bare `scout.ps1` maps the very next
hop with zero arguments.

## What it reports

```
--- baseline (no splice) ---
milestones 10/12  distinct 26  hash 196C114F...
    wall: SPIKE-END @FF/0E1B <ROM>       on-path not-reached: map_vrn

--- hop 1 splice: 00F963:PB=00;PC=FA42   [auto-spike cont->map_vrn] ---
    IIGS POKE: *** DELIBERATE state injection at PC=00/F963 ... ***
    IIGS POKE:   PB=$00 / force-branch PC=$FA42
milestones 10/12  distinct 25  hash CF6F1315...
    wall: BRK @00/5BB0
  ADVANCED: +1 milestone (map_vrn), reached map_vrn @frame 174
  NEXT WALL:
    WALL CLASS: BRK
      crash BRK/COP at 00/5BB0 ...
      class hint: symbolize the BRK PC; RETGUARD names a bad return; base-loss/wild family.
```

- **advance?** — compares the splice fingerprint `(milestones, distinct, reached-set)` to the
  baseline; reports `ADVANCED: +N milestones / +M distinct / reached <name> @frame` or
  `NO ADVANCE (necessary-but-not-sufficient OR wrong bytes)`.
- **next wall** — the wall PC (BRK > HANG > SPIKE-END) symbolized, its **class** (STALL / BRK /
  HANG / WILD / COMPLETE), and a next-step **hint** from `scout_sym.py` (the on-path next
  milestone for a stall; RETGUARD/base-loss for a BRK).
- exit `0` if any hop advanced, `3` if none did.

## The bounded forward boot

scout's `Invoke-ScoutBoot` is the **G3 run-until pattern** (a hard frame cap **and** a
wall-clock `Stop-Process` watchdog — the 371-CPU-sec wedge can't recur), plus the wall-PC
extraction the raw harness doesn't surface. It shares `run_until.ps1`'s env hygiene (clears
every arming/trace var so nothing leaks across runs) and the G5 oracle-env pin.

## `A2GSPU_POKE_NTH` (the occurrence-targeting primitive)

scout hardens the batch-1 `A2GSPU_POKE` with `A2GSPU_POKE_NTH=<n>`: the splice fires on the
Nth time PC reaches the trigger (default 1 = first hit), so a scout can target a specific
occurrence of a hot PC — the exact capability the write-24 case needs (the divergent queue
head-write is the 2nd hit of `$FF0C64`). Fully gated: unset ⇒ first-hit (unchanged); the
golden run sets no `A2GSPU_POKE` at all, so the flag-OFF determinism golden is byte-identical
by construction (verified: gsos601.po 150f → E1 `108683C9C967EEBF`, MMU `0925578EE375CA76`).

## Validation (reproducible, on the current boot)

- **Advance + next-wall** (auto-spike `cont→map_vrn`): the splice fires (banner), **map_vrn
  now reaches** (10/12 → milestone 11), scout reports `ADVANCED: +1 milestone` and names the
  next wall (`BRK @00/5BB0`). This is the campaign's scout behavior, packaged.
- **The named write-24 case** (`-Splice ...E119A0=D0... -Nth 2`): the pristine record-link
  bytes ($E119D0) are spliced over ours' $E06014; scout reports **NO ADVANCE** (still 10/12)
  and names the next wall (STALL @01/FA51 `DoMove`) — independently **corroborating** the
  campaign's documented "necessary-but-not-sufficient" finding (the head pointer isn't the
  root; the record allocation upstream is). A splice that names its own insufficiency is the
  point: scout tells you *whether* a candidate fix advances, before you build it.

## Files

```
scout.ps1        the scout runner (baseline -> splice -> forward -> next-wall -> repeat)
scout_sym.py     wall symbolizer + classifier (resolve <pc> | classify < telemetry.json)
README.md        this file
```

Relies on `m0/oracle_env.ps1` (G5 manifest pin), `m0/_kernel/kernel.sym.dbg` +
`kernel.milestones.txt` (symbolization). Not committed (Brad-gated).
