# The shape instrumentation needs so that nothing is a black box

**Date:** 2026-09-02
**Builds on:** `INSTRUMENTATION_AUDIT.md` (2026-07-31), `AGENTIC_ORACLE.md`, `A2GSPU_RAILS.md`
**Evidence:** one week of using the fork as a measurement instrument for a
memory-behaviour study, and one night of using it as an archaeology tool for a
1986 demo. Every claim below is anchored to something that went wrong, or
nearly did, in that week.

The goal, in the owner's words: *one gssquared-fork that can be used for all
our projects, maximally instrumented, and nothing is a black box.*

The audit settled the first half — rails exist, are additive, are gated, are
discoverable from the running process, and a parity gate keeps the registry
honest. This note is about the second half: what "not a black box" has to
mean once the rails exist, and what shape the instrumentation has to take so
that a consumer who did not write the fork cannot be misled by it.

---

## 1. Six ways the fork was a black box this week

None of these is a bug in the sense of a wrong emulation. In every case the
instrument answered a question truthfully and the consumer drew a wrong
conclusion, because the answer was missing something the instrument knew.

**A. A class without a cause.** `MEMVU_STOREVIS` reported the Dealer Demo's
show as 24% visible with 469,605 stores in the `device` class. `device` pools
three things the machine treats differently — soft switches, the sound GLU,
and anything in `$C000-$CFFF` reached through bank `$E1` — and the summary
does not say which. It took three rails (`STOREVIS_BANKS`, `BLAME`, a
`WATCH` on `E1:C000-CFFF`) to learn the number was one register, `$E1:C03D`,
written by ROM. The first draft of a report attributed it wrongly. A total
without its top contributors is a number that invites a story.

**B. Silence shaped like success.** Three times.
- `A2GSPU_RUN_BIN` named a file that was not there. The emulator booted the
  disk, ran the frames, and reported `status=OK`. The harness recorded the
  idle desktop as the blit row, byte for byte, and nothing in the report said
  so.
- The Dealer Demo with one of its two disks halts on *file not found — Hit
  any key*. A run of that state produces store counts, a visible fraction, a
  coverage bitmap and `status=OK`. Only running it at three lengths and seeing
  the SHR hash never change exposed it.
- (Previous session) `A2GSPU_SPIKE_KEYS` on a IIgs printed "no keyboard module
  — skipped" and the run succeeded. Fixed then; the pattern is the point.

**C. Streams that interleave.** Rail summaries share stdout with device
chatter and with each other. Twice in one evening a `MEMVU LOADVIS` line was
cut in half by another writer (`A2GSPU COVERAGE: wrote …`, `SerialDevice:
FileDevice shutting down`). A consumer reading the log with a regex gets a
partial line and no error. The sweep survived because it reads stdout alone;
any `2>&1` capture is corrupt by construction.

**D. A snapshot that claims to be the machine.** A Class A snapshot is CPU,
MMU and soft switches. `AGENTIC_ORACLE.md` says so. The `restore` ack and
`A2GSPU_SNAP_LOAD` do not. A restored desktop never polls the keyboard again
(the ADB micro is not in it), so injected keys land and are never collected,
and a workload that needs the Ensoniq cannot be windowed by snapshot at all.
The workaround — differencing two cold runs — is exact and costs a full boot
per window.

**E. An artifact with a header and no checked reader.** `A2GSPU_COVERAGE_OUT`
writes sixteen bytes of header before the bitmap. The consumer popcounted the
file. Every coverage figure in a downstream report was forty bytes high and the
denominator was 1,048,704. The writer prints what it wrote; the reader never
compared.

**F. Attribution to an unnamed address.** `BLAME` located the sound writer as
granule `$FF/4600`, correctly. It stayed an address for an hour. `ROM_SYMBOLS`
exists; it is not on by default, and an unsymbolized granule looks the same
as a symbolized one that happens to have no name.

Two more from the previous session, already in the record: rail
*existence* is solved and rail *reachability* is not (which rails can be
reached live, without a relaunch, is not inferable from names and produced
three confident falsehoods when inferred); and per-project logic compiled
into the fork (three hard-coded WIZ5 PC traps) meant every new "dump X when
the guest reaches Y" was a C++ change. `A2GSPU_TRAP` closes the second.

---

## 2. What "not a black box" has to mean

`AGENTIC_ORACLE.md` §2 says every answer must carry enough metadata to
re-derive trust, and lists anti-patterns. The week adds these, stated as
properties an instrument must have rather than as things to avoid:

| | Property | Failure it names |
|---|---|---|
| **P1** | **Every total names its causes.** A count is reported with its top contributors and the reason they count. | A |
| **P2** | **Every silence is a statement.** Armed rails acknowledge with their resolved configuration; every rail prints a close-out; a rail that never fired says *never reached*; an input that cannot be honoured is fatal, not skipped. | B |
| **P3** | **One record, one channel, one schema.** Machine-readable output is a record with a named event, written whole, on a sink that carries nothing else. Human summaries are prefixed lines. Nothing writes mid-line. | C |
| **P4** | **Every artifact is self-describing and every reader checks.** Magic, version and parameters in the header; the writer prints a count and a hash; the reader refuses what it does not recognise and re-derives the count. | E |
| **P5** | **The instrument declares its blind spots.** A snapshot says what it holds and what it does not; a peek says *unreadable* instead of returning bus noise; an observation model is named on every answer. | D |
| **P6** | **Configuration over code.** Observing a new interpreter, title or OS needs a spec, not a compile. | (WIZ5 traps) |
| **P7** | **Reachability is declared, not inferred.** Each rail states whether it is launch-only or live, and how its state is read back. | (previous session) |
| **P8** | **Determinism is asserted, not assumed.** Every run echoes its pins and prints a run hash; the harness can demand one. | (differencing rests on it) |
| **P9** | **Liveness is observable.** A run that has stopped progressing is distinguishable from one that has not, without looking at the screen. | B (second case) |
| **P10** | **Names by default.** Addresses in ROM and in the OS are symbolized without asking, and an unnamed address is marked as unnamed. | F |

P2 and P4 are the ones the audit's "fail-loud" invariant already implied;
the week shows they have to be *mechanical*, not a discipline.

---

## 3. The shape: mechanisms

Each mechanism names the properties it delivers and the event it would have
prevented. Sizes are rough: **S** an afternoon, **M** a day or two, **L** a
design change.

### M1 — The record contract  (P3) — S/M

One writer, `a2record.hpp`, used by every rail that emits machine-readable
output: `{"event":"<rail>.<kind>","frame":N,"cycles":N,…}` as NDJSON, to a
sink. `A2GSPU_RECORDS=<file>` is a global sink that takes every rail's records
when the rail has no sink of its own. Rules, enforced by review and by a grep
in `railparity`: rails never `printf` a partial line; human summaries are one
line each, prefixed `RAILNAME:`; device chatter goes to stderr. `WATCH`,
`TAP`, `CALLSTREAM` and `TRAP` already write NDJSON in four slightly
different shapes; the writer makes them one shape.

*Would have prevented C.*

### M2 — Attribution in every summary  (P1) — S

`MEMVU_STOREVIS` prints, when armed, the top five device addresses and the
top five writer granules beside the class totals; the classifier's classes
carry the reason (`io-register`, `mega2-ram`, `shadow-mirror`) rather than a
name that sounds like a peripheral. The same for `LOADVIS`'s must-serve loads
and for `SEAM`'s stalls. The rule generalises: a rail that reports a total
over addresses reports its top contributors in the same line group.

*Would have prevented A, and the wrong first draft.*

### M3 — Inputs that cannot be honoured are fatal  (P2) — S

Every rail that names a file — `RUN_BIN`, `LOAD`, `VRAM_LOAD`, `SNAP_LOAD`,
`KEYSCHED`, `TRAP_FILE`, `SYMBOLS`, the disk images themselves — exits
non-zero at arm time when the file is missing or unreadable, with the rail
and the path named. `GSDIAG:` gains `warnings=N`, counting every "skipped"
the run printed, and `A2GSPU_STRICT=1` turns any warning into a non-zero
exit. The harness stops being the only line of defence (the sweep now checks
its inputs; it should not have needed to).

*Would have prevented B (first case).*

### M4 — A liveness record  (P9) — M

`A2GSPU_LIVENESS=N` records, every N frames, `{frame, insns, shr_hash,
text_hash, last_visible_write_cycle, last_key_poll_cycle}`. `GSDIAG:` gains
`stalled_since=<frame>` when the hashes have not changed for a configurable
number of frames. A stopped workload becomes a field, not an inference from
three runs.

*Would have prevented B (second case), and makes the MEMVU campaign's rule ("quote a
workload only after its screen hash has changed between lengths") a gate.*

### M5 — Snapshot manifests, then snapshot classes  (P5) — S then L

`save` writes a manifest — class, the devices included, the devices excluded
— and `restore` / `SNAP_LOAD` print it. A rail that needs an excluded device
(`KEYSCHED`, `SPIKE_KEYS` on a restored machine) fails at arm with that
reason, instead of injecting keys nothing will collect. That is the S part.
The L part is the roadmap the audit named: Class B adds scanner and video
state, Class C adds ADB, Ensoniq and IWM, so that the Dealer Demo can be
windowed from a snapshot like everything else.

*Would have prevented D, and the previous session's day of ADB debugging.*

### M6 — Artifact discipline and a shared reader  (P4) — S

Every binary artifact (`A2COV1`, snapshots, `spike_*.bin`, `vram-raw`) has a
header with magic, version and its parameters; the writer prints the count
and an FNV hash of the body; `tools/a2rail/artifacts.py` is the one reader
the harnesses import, and it refuses unknown magic and re-derives the count.
A consumer's sweep harness grew this check by hand this week; it belongs in the fork.

*Would have prevented E.*

### M7 — Symbols by default  (P10) — M

Ship ROM symbol tables for each ROM personality under `resources/symbols/`
and auto-load the matching one; ship a GS/OS 6.0.1 table derived from the
published source. Every address a rail prints carries `"sym":"NAME+off"` or
`"sym":null` — explicitly null, so an unnamed address cannot pass for a named
one. `BLAME`'s granule `$FF/4600` becomes a Sound Tool Set routine at the
moment it is printed.

*Would have prevented F.*

### M8 — Reachability and readback as a registration  (P7) — M

The rail registry gains two columns: `live` (`env`, `ctrl`, `both`) and a
`read` function. Registering a rail means providing the function that returns
its current state as a record — the thing `memvu` did by hand with a
formatter per counter. `rail read <name>` in CTRL calls it; the manifest
lists it; the parity gate fails a rail that registers without one. The
previous session's three confident falsehoods came from inferring `live` from
names; this makes it a declaration the code has to make.

### M9 — Determinism, asserted  (P8) — S

Every spike prints `PINS: seed= faketime= ramdisk= rom= build=` and a run hash
over instruction count, store count and the SHR hash. `A2GSPU_EXPECT_HASH=<h>`
exits non-zero on mismatch. A sweep harness's differencing technique currently
*assumes* two runs share a prefix; with this it checks. The `build=` field
closes a consumer report's open item (a figure without the build that
produced it).

### M10 — Configuration over code, continued  (P6) — M

`A2GSPU_TRAP` (this commit) makes PC traps a spec. The same shape should
absorb: value triggers (a store to an address, S entering a range — `WATCH`
and `STACKTRAP` become trigger kinds of `TRAP`), `KEYSCHED` wait conditions
written as `TRAP` expressions instead of text-page substrings, and eventually
the WIZ5 traps themselves, retired when their consumers read the new records.
The test of P6 is simple: the next project that needs to observe its
interpreter adds zero lines of C++.

### M11 — A peek that works everywhere  (P5) — M

`probe_peek` returns floating bus for the IIgs handler-paged banks `$00/$01`
(`mmu_iigs.hpp`), so a side-effect-free read of bank 0 is not available and
every rail that needs one falls back to `mmu->read`. `probe_readable` exists;
readers should use it and print `unreadable` rather than `$EE`, and the MMU
should resolve handler pages to their backing RAM for the probe path. Until
then every "observe" rail on the IIgs is choosing between a lie and a side
effect on those two banks.

### M12 — The manifest carries record schemas  (P3, P7) — S

`manifest <file>` already enumerates verbs and rails. Add, per rail, the
event names it emits and their fields, generated from the same table M1's
writer uses. A consumer can then validate records against the schema and a
schema change is a visible diff.

---

## 4. Order

Cheap and prevents the most: **M3, M2, M6, M1** — an afternoon each, and
between them they cover four of the six events. Then **M4** and **M9**, which
turn two of the MEMVU campaign's hard-won rules into gates. Then **M5**'s manifest,
**M7**, **M8**. **M5**'s classes, **M10**, **M11** and **M12** are the
longer road.

## 5. The week, replayed

With the shape in place: the missing blit binary is a fatal error at arm
(M3); the one-disk demo reports `stalled_since=312` (M4); the `device` line
names `$E1:C03D` and `SoundToolSet+…` on the first run (M2, M7); the
`LOADVIS` line is whole because nothing else is on its channel (M1); the
coverage figure is right the first time because the reader checks the header
(M6); the snapshot restore prints *ADB: not included* and the keystroke
workload refuses to start from it (M5). Six wrong turns, none taken.

## 6. Non-goals

Not a debugger UI. Not a change to stock behaviour — every mechanism above is
env-gated and the unarmed hash stays part of the gate. Not a merger of SPIKE
and CTRL; the audit was right that they have different jobs. And not a
replacement for reading the code: the aim is that reading the code is never
*required* to trust a number.
