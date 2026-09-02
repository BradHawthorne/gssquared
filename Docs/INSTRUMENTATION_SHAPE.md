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

**Status:** done. `src/a2record.hpp` is the one writer: a `Record` opens
with `{"event":"<rail>.<kind>","frame":N,"cycles":N`, takes the rail's fields
by typed setters (or a pre-built fragment, for the trap's payloads), and
`write()` closes it and puts one whole line on the sink. `TRAP`, `LIVENESS`,
`WATCH`, `CALLSTREAM` and `TAP` (its five session streams included) write
through it; their field names are unchanged, so `calldiff.py` and every
reader keyed on fields keeps working, and only the `event` values moved
(`trap` → `trap.entry|exit`, `watch` → `watch.<kind>`, `callstream` gains one,
`tap` gains one). `A2GSPU_RECORDS=<file>` is the global sink a rail without
its own falls to; with nothing set every rail prints the stdout line it
always did, so the dual-mode hash is untouched. `rail read A2GSPU_RECORDS`
counts records per rail; `rail schema` carries the envelope as its first
row. The rule is in `railparity.ps1`: a JSON literal printed outside the
writer fails the gate, with two named exemptions (the CTRL reply envelope,
which is protocol; the WIZ5 title streams, which convert when their readers
do). Not done: the coverage bitmap and the snapshot stay binary artifacts
with a manifest, which is the right shape for them.

### M2 — Attribution in every summary  (P1) — S

`MEMVU_STOREVIS` prints, when armed, the top five device addresses and the
top five writer granules beside the class totals; the classifier's classes
carry the reason (`io-register`, `mega2-ram`, `shadow-mirror`) rather than a
name that sounds like a peripheral. The same for `LOADVIS`'s must-serve loads
and for `SEAM`'s stalls. The rule generalises: a rail that reports a total
over addresses reports its top contributors in the same line group.

*Would have prevented A, and the wrong first draft.*

**Status:** done for stores (commit after this note): `MEMVU STOREVIS device
top:` names the five most-written I/O registers from `src/iigs_ioregs.hpp`, and
`writers top:` names the five heaviest code granules, `(unnamed)` when no
symbol resolves. Loads and the seam model still report bare totals.

### M3 — Inputs that cannot be honoured are fatal  (P2) — S

Every rail that names a file — `RUN_BIN`, `LOAD`, `VRAM_LOAD`, `SNAP_LOAD`,
`KEYSCHED`, `TRAP_FILE`, `SYMBOLS`, the disk images themselves — exits
non-zero at arm time when the file is missing or unreadable, with the rail
and the path named. `GSDIAG:` gains `warnings=N`, counting every "skipped"
the run printed, and `A2GSPU_STRICT=1` turns any warning into a non-zero
exit. The harness stops being the only line of defence (the sweep now checks
its inputs; it should not have needed to).

*Would have prevented B (first case).*

**Status:** done: a missing disk image under `-n`, `RUN_BIN`, `SNAP_LOAD`,
`KEYSCHED`, `MILESTONES` or `TRAP_FILE`, and an unopenable `WATCH_OUT`,
`CALLSTREAM`, `ITRACE_OUT`, `TRAP_OUT` or WIZ5 sink, abort at arm with exit 2.
`GSDIAG:` carries `warnings=N`; `A2GSPU_STRICT=1` exits 3 on any. The
remaining "skipped" paths (`SPIKE_KEYS` with no keyboard, `RUN_BIN` on a
II-family machine, `SAVE_AT` without a IIgs MMU) are warnings.

### M4 — A liveness record  (P9) — M

`A2GSPU_LIVENESS=N` records, every N frames, `{frame, insns, shr_hash,
text_hash, last_visible_write_cycle, last_key_poll_cycle}`. `GSDIAG:` gains
`stalled_since=<frame>` when the hashes have not changed for a configurable
number of frames. A stopped workload becomes a field, not an inference from
three runs.

*Would have prevented B (second case), and makes the MEMVU campaign's rule ("quote a
workload only after its screen hash has changed between lengths") a gate.*

**Status:** done as `A2GSPU_LIVENESS=N` (+ `_STALL`, `_OUT`): screen and text
hashes every N frames, `changed=` per sample, `screen_unchanged_since=` and
`text_unchanged_since=` in the close-out and on `GSDIAG:` while armed — two
fields, because the first run showed the halted demo's text page cycling under
a static SHR window, and one field would have hidden it. Named for what it measures rather than
`stalled_since`, because an idle desktop is static too; `last_visible_write`
and an instruction count are not in the sample yet.

### M5 — Snapshot manifests, then snapshot classes  (P5) — S then L

`save` writes a manifest — class, the devices included, the devices excluded
— and `restore` / `SNAP_LOAD` print it. A rail that needs an excluded device
(`KEYSCHED`, `SPIKE_KEYS` on a restored machine) fails at arm with that
reason, instead of injecting keys nothing will collect. That is the S part.
The L part is the roadmap the audit named: Class B adds scanner and video
state, Class C adds ADB, Ensoniq and IWM, so that the Dealer Demo can be
windowed from a snapshot like everything else.

*Would have prevented D, and the previous session's day of ADB debugging.*

**Status:** the S half is done: every snapshot now carries a manifest (class,
included, excluded, frame, pins, build, executable hash) after its sentinel,
`SNAP_LOAD` and `restore` print it or say it is absent and what Class A means,
and `SPIKE_KEYS` into a restored machine is a counted warning. The L half has
its first two devices: the ADB micro (its variables, registers, the key latch
and hold, and the keyboard and mouse on its bus) and the Ensoniq (GLU
registers, the 64 KB DOC RAM, the chip's registers and 32 oscillators) are
written as named device blocks between the MMU block and the sentinel, each
with a version and a length so an unknown block is stepped over and named,
and a snapshot from before device blocks reads exactly as it did. The
manifest now says `devices=adb,ensoniq` and moves those two from `excluded`
to `included`; the restore prints `devices restored=… skipped=…`, a skipped
block is a counted warning, and `SPIKE_KEYS` into a snapshot that holds the
ADB no longer warns. Sound timing kept as 14M timestamps is rebased to the
restore instant, not restored. A third block, `vgcint`, came out of the
smoke test: with the ADB micro restored, a typed key still never reached
the Finder. The iolog counters showed why — the restored machine polled
`$C025` (the Finder's main loop) but never `$C000`, because the heartbeat
that polls the keyboard runs off the VBL interrupt, and the VBL enable
(`$C041`) lives in the display module, which the snapshot did not hold. Event
D's "the ADB micro is not in it" was half the cause; a restored machine also
had every interrupt source disabled. `vgcint` carries INTEN, VGCINT, INTFLAG,
the one-second and quarter-second counters, LANGSEL and the text-flash phase,
and the restore re-drives the IRQ lines from them. A fourth block, `iwm`, carries the disk
controller (switches, registers, sequencer, the motor-off timer restarted at
the restore instant) and each drive's head: position, phases and quarter-track
for a 5.25, control lines, track, side and motor for a 3.5. Not the media: a
drive's block names the image it had mounted and is applied only when the
same image is mounted now, and a mismatch is a counted warning — under
`A2GSPU_RAMDISK` the image's contents are not the file on disk either, and the
manifest says `media` is excluded. Still out: the scanner (Class B — the
display pipeline is in the owner's working tree, so it waits).

### M6 — Artifact discipline and a shared reader  (P4) — S

Every binary artifact (`A2COV1`, snapshots, `spike_*.bin`, `vram-raw`) has a
header with magic, version and its parameters; the writer prints the count
and an FNV hash of the body; `tools/a2rail/artifacts.py` is the one reader
the harnesses import, and it refuses unknown magic and re-derives the count.
A consumer's sweep harness grew this check by hand this week; it belongs in the fork.

*Would have prevented E.*

**Status:** done for coverage: the `wrote` line carries the range and the
body's house-FNV hash, and `tools/a2rail/artifacts.py` is the reader — it
refuses a wrong magic or a short body, re-derives the count and the hash, and
has a self-test. Snapshots get a header check only; the other `spike_*.bin`
artifacts are not yet covered.

### M7 — Symbols by default  (P10) — M

Ship ROM symbol tables for each ROM personality under `resources/symbols/`
and auto-load the matching one; ship a GS/OS 6.0.1 table derived from the
published source. Every address a rail prints carries `"sym":"NAME+off"` or
`"sym":null` — explicitly null, so an unnamed address cannot pass for a named
one. `BLAME`'s granule `$FF/4600` becomes a Sound Tool Set routine at the
moment it is printed.

*Would have prevented F.*

**Status:** the default half is done: the documented Apple II Monitor entry
points, the IIgs dispatch vectors and ProDOS 8's `MLI` are seeded without any
env, `STOREVIS writers top:` and `TRAP` records use them (`sym`), and an
address they do not cover prints as `(unnamed)` / `null`. The old trace
region tags keep their `A2GSPU_ROM_SYMBOLS` gate so no parser moves. The data
half followed the same day, from the ROM itself: the Tool Locator reads the
system tool pointer table's address from `$E1/03C0` (`$FE/012F` in ROM 01),
and that table yields every ROM tool set's function entry points — 526 for
ROM 01 — which `tools/a2rail/romfpt.py` writes as
`assets/symbols/apple2gs/<ROM hash>.sym`, installed by the build beside the
ROMs so it loads from any working directory (the gate's sessions had run
unnamed from theirs), and the default namer loads for the matching ROM. `$FF/46ED` is `SoundTools.FFStartSound+$2FC`, verified against a
dispatcher trap. RAM-resident tool sets followed, and turned out to be most of them:
under System 6 on ROM 01 the system TPT itself is in RAM (`$7E/F599` on the
measured desktop) and 26 of 35 sets have RAM function tables — System 6
patches the ROM sets and loads the rest wherever the Memory Manager puts
them on that boot — so no file can hold their addresses. The namer now walks
the live TPT through `probe_peek` when a RAM address misses and the table has
moved or grown since the last walk (an application loading a tool set does
exactly that), keeps the RAM entry points in a second sorted table, and
names them from `toolbox.names` (`romfpt.py --names`, installed with the
resources). Proving it with a trap found an off-by-one in both tables: an
FPT entry is the function's address *minus one* — the dispatcher pushes it
and executes RTL — so every toolbox name had been a byte early, and a trap
on an entry never fired. Both tables add one now. Still open: GS/OS's own kernel and drivers, which are not tool
sets and need the published source, and shared helpers, which either table
names after the preceding entry. One correction after the fact: moving the toolbox load to
arm time left the lookup's seed-and-sort step keyed on the loader's own flag,
so every search ran over an unseeded, unsorted table and the Tool Locator
dispatch at `$E1/0000` named null — caught by the record contract's smoke
test, not by the gate, which has no naming check. `iigs_names_ready()` now
seeds, loads and sorts before anything searches, whichever side arrives
first.

### M8 — Reachability and readback as a registration  (P7) — M

The rail registry gains two columns: `live` (`env`, `ctrl`, `both`) and a
`read` function. Registering a rail means providing the function that returns
its current state as a record — the thing `memvu` did by hand with a
formatter per counter. `rail read <name>` in CTRL calls it; the manifest
lists it; the parity gate fails a rail that registers without one. The
previous session's three confident falsehoods came from inferring `live` from
names; this makes it a declaration the code has to make.

**Status:** the declaration half is done. `src/a2gspu_rails_registry.hpp`
carries a reach table (rail → live verb, readback), `rail live` checks every
row against the rail and verb tables and fails the whole ack on one stale
entry, `rail find` reports `live=`/`read=`, and `rail read <NAME>` returns a
rail's state now for `MEMVU_*`, `A2GSPU_TRAP`, `A2GSPU_LIVENESS`,
`A2GSPU_STRICT` and the pins, or names the verb that reads it, or says
`no-readback`. The hand-written "live:" sentence in the `rail` ack is gone.
The registration half followed: a rail that can be read back registers its
reader with itself — `a2rails::Registrar{"A2GSPU_TRAP", a2trap::format_status}`
in the trap's own header, and likewise LIVENESS, the MEMVU accounting, the
pins, RECORDS, and STRICT — so the reach table now says only `fn`, and `rail
read` dispatches through the registry with no formatter chain to drift. Both
directions are checked: `rail live` fails a `fn` row nothing registered and a
registration the table does not declare, and `railparity.ps1` does the same
offline from the source, so a rail cannot gain a reader the surface does not
show or claim one it does not have. What "impossible to add without" still
does not cover is a new *launch-only* rail: absence from the reach table is a
declared state (`rail find` says launch-only), and forcing an explicit row for
every one of the 142 would be noise, not information.

### M9 — Determinism, asserted  (P8) — S

Every spike prints `PINS: seed= faketime= ramdisk= rom= build=` and a run hash
over instruction count, store count and the SHR hash. `A2GSPU_EXPECT_HASH=<h>`
exits non-zero on mismatch. A sweep harness's differencing technique currently
*assumes* two runs share a prefix; with this it checks. The `build=` field
closes a consumer report's open item (a figure without the build that
produced it).

**Status:** done: `A2GSPU PINS:` prints seed, faketime, ramdisk, platform,
frames, the ROM's size and hash, the configure-time build string (labelled)
and the executable's own hash; `GSDIAG:` carries `cycles=`; `A2GSPU_EXPECT_HASH`
is the inline gate. The build string still lags the tree between configures;
the executable hash is the field to trust.

### M10 — Configuration over code, continued  (P6) — M

`A2GSPU_TRAP` (this commit) makes PC traps a spec. The same shape should
absorb: value triggers (a store to an address, S entering a range — `WATCH`
and `STACKTRAP` become trigger kinds of `TRAP`), `KEYSCHED` wait conditions
written as `TRAP` expressions instead of text-page substrings, and eventually
the WIZ5 traps themselves, retired when their consumers read the new records.
The test of P6 is simple: the next project that needs to observe its
interpreter adds zero lines of C++.

**Status:** two trigger kinds added to `A2GSPU_TRAP` beyond the PC:
`store=ADDR[-ADDR]` (one branch in the CPU's store funnel; the record names
the storing instruction and the byte) and `sp=LO-HI` (S entering a range,
edge-triggered). Both share the condition, policy and payload machinery, so
"who feeds `$E1:C03D`, with registers and the PC ring" is one spec where it
was three rails. Read triggers and `KEYSCHED` conditions are not done; the
WIZ5 traps still stand until their consumers move.

### M11 — A peek that works everywhere  (P5) — M

`probe_peek` returns floating bus for the IIgs handler-paged banks `$00/$01`
(`mmu_iigs.hpp`), so a side-effect-free read of bank 0 is not available and
every rail that needs one falls back to `mmu->read`. `probe_readable` exists;
readers should use it and print `unreadable` rather than `$EE`, and the MMU
should resolve handler pages to their backing RAM for the probe path. Until
then every "observe" rail on the IIgs is choosing between a lie and a side
effect on those two banks.

**Status: this was already done before the note was written, and the note
was wrong.** `MMU_IIgs::probe_peek` resolves banks `$00/$01` through the
aux and language-card mapping the CPU uses (`calc_aux_read`, LC bank/ROM
state), answers `$C100–$CFFF` from the Mega II's page table, and returns
floating bus only for `$C0xx`, where no observation-free answer exists;
`probe_readable` tracks it exactly. The paragraph above was written from a
comment describing the *old* behaviour that sits above the code that fixed
it — a black box of the documentation kind. `A2GSPU_TRAP` and the
names-by-default check now read through the probe everywhere. What remains
of M11 is only the rule: an observe rail reads through the probe, never
through the CPU's read path.

### M12 — The manifest carries record schemas  (P3, P7) — S

`manifest <file>` already enumerates verbs and rails. Add, per rail, the
event names it emits and their fields, generated from the same table M1's
writer uses. A consumer can then validate records against the schema and a
schema change is a visible diff.

**Status:** done as a declared table rather than one generated from the
writer (M1's writer does not exist yet): `record_schemas` in
`a2gspu_manifest.hpp` lists, for every rail that writes NDJSON — TRAP,
LIVENESS, WATCH_OUT, CALLSTREAM, TAP and the three WIZ5 sinks — the tag that
names a record and the fields a consumer may rely on, optional ones marked.
`rail schema [NAME]` serves it live and checks every row against the
registry; `manifest <file>` carries it as a section. The eight shapes are
visibly eight different conventions (`event=`, `ev=`, `t=`, `op=`, none),
which is the argument for M1 written down where a consumer will read it.

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
