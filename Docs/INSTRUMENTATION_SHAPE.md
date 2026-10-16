# The shape instrumentation needs so that nothing is a black box

**Date:** 2026-09-02
**Builds on:** `INSTRUMENTATION_AUDIT.md` (2026-07-31), `AGENTIC_ORACLE.md`, `A2GSPU_RAILS.md`
**Evidence:** one week of using the fork as a measurement instrument for a
memory-behaviour study, and one night of using it as an archaeology tool for a
1986 demo. Every claim below is anchored to something that went wrong, or
nearly did, in that week.

## Status at a glance

**2026-10-16 context layer:** [`MACHINE_CONTEXT.md`](MACHINE_CONTEXT.md)
connects the mechanisms below through one observation identity, live topology,
named explanations, scheduler labels, bounded parented events and state marks.
The Rosetta experiment companion binds correctness and native execution to
measured efficiency. Complete device data flow, presentation and replay remain
open rather than implied by this interface.

This table summarizes current status. The mechanism sections retain the historical
implementation progression; later entries and the table supersede earlier
intermediate states. Items listed as open are real work, not wording.

| Mechanism | State | Still open |
|-----------|-------|------------|
| M1 record contract | done | coverage bitmap and snapshots stay binary artifacts with manifests (by design) |
| M2 attribution in summaries | done | — |
| M3 unhonourable inputs are fatal | done | a few "skipped" paths remain counted warnings by choice |
| M4 liveness | done | — |
| M5 snapshot manifests + device blocks | partial | CPU/MMU plus per-device blocks include scanner, storage, RTC/ThunderClock, SCC/SSC, network and several cards; the session clock, host connections and some synthesis phases remain outside the snapshot. The file's included/excluded manifest is authoritative |
| M6 artifact discipline | done | — |
| M7 names by default | done | segments loaded later by `LoadSegNum`, and loads that happen before the call tracker runs, are not named |
| M8 reachability + readback | done | the reach table declares control and readback separately; `rail live` lists live setters and launch-only bindings (70 live setters as of 2026-10-15) |
| M9 determinism asserted | done | — |
| M10 configuration over code | done, one standing exception | the WIZ5 title traps stand until their readers move (AGENTS.md rule 2) |
| M11 a peek that works everywhere | done | — |
| M12 record schemas in the manifest | done | — |

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
writer fails the gate, with three named exemptions (the CTRL reply envelope,
which is protocol; the writer itself, `a2record.hpp`; the WIZ5 title streams,
which convert when their readers do). Not done: the coverage bitmap and the snapshot stay binary artifacts
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
symbol resolves. **Loads and the seam model followed (2026-09-16):**
`MEMVU LOADVIS device top:` / `readers top:` name the registers the machine must
serve and the code reading the Mega II side, and the seam model charges every
stall, as it happens, to the page accessed and the code accessing it
(`MEMVU SEAM stall pages top:` / `stall code top:`, with shares). On the pinned
GS/OS boot the stalls are 47% `$E1:C0xx` and 37% one Sound Tool Set granule --
the same `$FF/4100` STOREVIS names as the top writer, now visible as the model's
dominant cost rather than inferred.

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
`RUN_BIN` also verifies every injected byte by read-back and aborts (exit 2) if
any differs, and refuses a binary that would cross the end of its bank — an
injection that reported "injected 13 bytes at $00D000" into ROM was the same
class of lie as a missing file. An unwritable `A2GSPU_GOLDEN` path is a hard
gate failure rather than the silent nothing it used to be. `SYMBOLS` is a
**warning**, not fatal: a table that cannot be opened, or that yields 0 symbols
in either accepted form, now goes through `a2gspu_warn` (it used to be a bare
printf, so `GSDIAG` said `warnings=0` and `A2GSPU_STRICT=1` could not see it).
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
`stalled_since`, because an idle desktop is static too. **Completed
2026-09-16:** the sample carries `insns` and the cycle of the last store onto
each hashed range (`screen_store`, `text_store`, counted where the machine
placed the byte, so a IIgs store the MMU shadowed counts and one it did not
mirror does not) and of the last keyboard read (`kbd_poll`, on both families,
so `kbd=` is a count on the II family too). The close-out and `GSDIAG:` gain
`cpu_stopped_since=`. The three separate what two hashes could not: a program
redrawing identical bytes, one that stopped drawing, and a CPU that stopped.
The taps (`src/a2gspu_progress_taps.hpp`) are one untaken branch each unarmed,
and the armed IIe and IIgs boots reproduce the unarmed hash and cycle count.

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
manifest says `media` is excluded. A fifth, `pdblock`, carries the slot 7
block device — the corpus's own volumes — with its command buffer, each unit's
state, and, for a unit served from its RAM copy (`A2GSPU_RAMDISK`), the
volume's overlay as dirty blocks: every block written since the image was
loaded, so a restored GS/OS sees the volume it left rather than the file on
disk. The image stays out; a unit's block names its image and block count and
is applied only when the same image is mounted now and only in RAM mode (the
host file is never written from a snapshot), and anything else is a counted
warning. **Class B followed (2026-09-16):** a `scanner` block holds the video
scanner's own copy of the display switches, its text and border colours, the SCB
and palette index being scanned, the per-scanline state, and the display module's
NEWVIDEO. The MMU block had restored the switches in the MMU but not the copies
the scanner and the display module draw from, so a restored GS/OS desktop
rendered black (the scanner still in text mode) and, once the scanner was
restored, stretched (the display still choosing the non-SHR frame rectangle).
Measured against the continuing run: `vid` agrees exactly after restore, and the
rendered frame differs in 336 pixels of host window chrome instead of everywhere.
**The media followed (2026-09-16),** with the II family's disk controller. A
`diskii` block carries each Disk II controller by slot — the addressable latch,
the motor and the time left on its 555 timer (re-armed from the restore
instant), the data register and sequencer, and both drives' heads — so a IIe is
no longer restored with its disk controller at power-on state. A `media` block
carries, for the IWM's drives and each Disk II's, every track of the WOZ image
of a disk written since it was mounted (an unwritten disk contributes its name
only); each controller's part is length-prefixed, so a machine without that
controller steps over it and says so. A restore applies a disk's tracks only
when the same image is mounted with the same track map and count, and reports
either way: `media: N written disk(s) restored track by track`, or a counted
warning that the disk reads what its file holds. Measured on a IIe and a IIgs,
each booting DOS 3.3 from a 5.25 disk: `SAVE T`, `CATALOG`, save the machine;
restore into a session whose copy of the image was never written, clear the
screen, `CATALOG` again — `T` is listed; the same restore with the disk under
another name warns and the fresh listing has no `T`. The host file is never
written from a snapshot, as with `pdblock`.
The scan position is saved but not applied: it belongs to the clock, which is not
in the snapshot, and restoring it against another clock's frame boundaries drew
the picture shifted. Still out: the session clock; floppy media support is
described above and has its own compatibility limits. The test the roadmap
named passes: the Dealer Demo, saved at frame 1200 and restored for 1200
frames with STOREVIS armed, reports 3,517,548 stores, 851,529 visible,
469,605 device, 46,494 slow-side, 335,430 shadowed; the cold-run difference
the report had to use instead is 3,517,553 / 851,529 / 469,605 / 46,494 /
335,430 — every visible class exact, five stores and 157 instructions apart
in 3.5 million and 14 million, which is the rebased sound and motor timers.
Event D is closed for the machine the corpus measures.

### M6 — Artifact discipline and a shared reader  (P4) — S

Binary artifacts (`A2COV1`/`A2COV2`, snapshots, `spike_*.bin`, `vram-raw`)
declare their format through a header or a raw-file sidecar.
`tools/a2rail/artifacts.py` is the shared reader the harnesses import. It
checks the format-specific layout, counts and integrity fields described
below; snapshot checks are narrower than raw-stream hash validation.

*Would have prevented E.*

**Status:** done for coverage, in **both** formats. The `wrote` line carries
the body's house-FNV hash — `A2COV1`'s always did, `A2COV2`'s did not until it
was noticed that the newer format, the source-bound one that actually feeds the
deasmiigs code/data split, was the one whose reader had nothing to check
against. `tools/a2rail/artifacts.py` is the reader: it dispatches on the magic,
refuses anything that is neither, refuses a wrong magic, a short body, a plan
with trailing bytes or map data that does not reach the trailer, re-derives the
count and recomputes the trailing hash over header+plan+maps, and self-tests
each of those refusals against a file built to trip it. The CTRL `cov write`
ack now says `format=` and `hash=` too, because a caller could not otherwise
tell which of the two formats a run had produced — any `cov map <plan>` quietly
switches it. Snapshots get a header and manifest-trailer check only.

**The raw artifacts followed (2026-09-16).** `spike_e1.bin` and its siblings
cannot take a header -- a consumer's harness reads `spike_e1.bin` as 65,536 raw
bytes -- so each keeps its bytes and gains a sidecar record, `<file>.meta.json`
(`src/a2gspu_artifact_meta.hpp`): format, record layout in words, count, the
writer's content hash with what it covers, and the file's own house-FNV. Covered:
`spike_e1.bin`, `spike_e1_init.bin`, `spike_trace.bin`, `spike_slot.bin`,
`spike_mmu_truth.bin` and the `vram` dump. `artifacts.py raw <file>` refuses a
missing sidecar, an unknown format, a length that is not count x record size, and
either hash failing to re-derive (the content hash is recomputed from the records
by the layout), and its self-test trips each refusal. `spike_frame.bmp` is a BMP
and describes itself.

**Raw write failures (2026-10-16).** The raw SPIKE, CTRL `vram` and generic
tap CPU-view/full-RAM writers check open, complete write, flush and close.
Sidecar generation first checks that the payload exists, can be read and has
exactly `count * record_size` bytes; a missing file cannot impersonate a valid
zero-record capture. A real empty stream remains valid. Invalid sizes and
overflow refuse before opening the sidecar. Failed sidecar finalization removes
the sidecar opened by that attempt and reports any removal failure.

SPIKE and generic tap failures print `status=FAIL artifact-io` and exit 2;
CTRL `vram` returns `status=FAIL vram-artifact-io` and keeps the session usable.
Only finalized payloads and sidecars receive a successful artifact receipt.
The raw bytes, record layouts and content hashes remain compatible.
`artifactiocheck.py` runs dual-platform positive/refusal cases in the agentic
gate; native interface-contract checks inject short-write, flush and close
failures without production fault rails. Evidence:
`reports/gssquared-artifact-io-20261016/README.md`.

This checks C stdio completion, not `fsync`, power-loss durability or atomic
publication of a payload/sidecar pair. A failed attempt may leave a partial
payload; use fresh run directories and require both a successful command/run
and the checked reader. Existing files can survive a refusal before opening
them. Record observer notifications can precede a later close failure, so they
are not final publication receipts. General NDJSON sink lifecycle, screenshots,
coverage and snapshot writer hardening are separate paths and are not covered
by this raw-writer guarantee.

### M7 — Symbols by default  (P10) — M

Ship ROM symbol tables for each ROM personality under `assets/symbols/` (copied to `resources/symbols/` in the build)
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
under System 6 on ROM 01 the system TPT itself is in RAM and 24 of 35 sets
have RAM function tables (measured 2026-09-16 at frames 1,600 and 3,000 of a
pinned GS/OS 6.0.1 boot — seed, faketime, ramdisk and BRAM — where the table
sat at `$7E/F5ED`; an earlier unpinned run found it at `$7E/F599`, and this
paragraph said 26 while the commit that measured it said 24) — System 6
patches the ROM sets and loads the rest wherever the Memory Manager puts
them on that boot — so no file can hold their addresses. The namer now walks
the live TPT through `probe_peek` when a RAM address misses and the table has
moved or grown since the last walk (an application loading a tool set does
exactly that), keeps the RAM entry points in a second sorted table, and
names them from `toolbox.names` (`romfpt.py --names`, installed with the
resources). Proving it with a trap found an off-by-one in both tables: an
FPT entry is the function's address *minus one* — the dispatcher pushes it
and executes RTL — so every toolbox name had been a byte early, and a trap
on an entry never fired. Both tables add one now. Shared helpers followed: the call tracker's pending
stack is kept whenever BLAME or TRAP is armed (not only for TBTRACE and
CALLSTREAM), `TRAP` records carry `incall`, and BLAME's writers and granule
lines carry the dominant call per granule (`; in SoundTools.FFStartSound
98%`), so a helper is named by the call that used it and the entry-point
table's guess stands beside it rather than alone. Still open: GS/OS's own
kernel and drivers, which are not tool sets and need the published source. One correction after the fact: moving the toolbox load to
arm time left the lookup's seed-and-sort step keyed on the loader's own flag,
so every search ran over an unseeded, unsorted table and the Tool Locator
dispatch at `$E1/0000` named null — caught by the record contract's smoke
test, not by the gate, which has no naming check. `iigs_names_ready()` now
seeds, loads and sorts before anything searches, whichever side arrives
first.

The kernel followed (2026-09-16), without the source: Apple's GS/OS source is
not published, so no label comes from it. What is published is the file
format, and the boot volume's own `SYSTEM/GS.OS` names its parts — a chain of
segments behind `$30`-byte headers carrying the load address, length, flags
and an identifier (`System Loader,Pt 2 v04.00`). `tools/a2rail/gsosmap.py`
reads the file out of the volume (`prodosfs.py`, a read-only ProDOS reader)
and writes `assets/symbols/apple2gs/gsos.segs`: 14 segments for 6.0.1, each
with a house-FNV of every 256-byte page and no kernel code. The namer calls a
PC `GSOS.<Segment>+$off` only while the live bytes of its page hash-match the
file, checked through `probe_peek` and cached per frame, because an address
alone proves nothing here: the Init Manager parts load over other segments
during boot, the language card banks some out, and a ProDOS 8 program
replaces the kernel. A page that does not match stays unnamed rather than
misnamed. The System Dispatch Table at `$01/FC00` could never hash-match —
GS/OS writes its jump targets at run time — so a segment that is mostly
4-byte `JML` slots is stored as the opcode of each slot and verified by shape.
The System Loader's calls are tool set 17 (`LoaderInitialization` …
`InitialLoad2`), named from the published call list and taking precedence
over the segment name. `unarmedhash.ps1` now traps one landing in the Loader,
the dispatch table and the Dispatcher during the pinned GS/OS boot and fails
unless their records carry those names; removing `gsos.segs` turns it red.

The rest of what GS/OS brings in has no fixed address, so it is named from the
load itself (`A2GSPU_LOADMAP`, `src/iigs_loadmap.hpp`). The call tracker
already saw every Tool Locator call; at `InitialLoad2`/`InitialLoad` it now
reads the pathname the caller passed, notes each `NewHandle` inside the load,
and at the return keeps the handles the load's userID still owns. A PC in one
of those blocks is `Load.<file>+$off` while the block's master pointer record
still says the same owner and size — the Memory Manager's own bookkeeping,
read live, so a purged block goes unnamed and a moved one is named where it
now is. On the pinned boot to the Finder that is 26 loads: the three drivers,
`Char.FST`, the `SYSTEM.SETUP` files, the Control Panel, `START`, the Finder
and ten RAM tool sets, and every load's start address lies in a block it owns
(the check that the handle record layout was read right).

Building it found the call tracker broken for GS/OS. A call through the
inline entry `$E100A8` returns six bytes past its `JSL` (the call number and
parameter pointer follow it), and the tracker waited at the `JSL`'s own
return address: 664 of 664 inline calls on that boot never matched, and each
stale entry hid the return of whatever call lay beneath it — 40 toolbox
returns, among them every `InitialLoad2` after the first driver's. Returns
match now, and records carry the GS/OS call number (`gsos`, and `incall`
`GSOS.$202D`); the number, not the entry, says the class — both entries take
both. What the fix deliberately did not change: inline errors are routine (49
on that boot), so they are not fed to `GSDIAG`'s `gsos_err`, its `GSOS_ERROR`
status or the `SYSTEM/LOADER ERROR` banner, which the stack-entry returns
alone always drove and which harnesses — Rosetta v3's among them — read as a
fault. An A/B build with the old return address gives the same `GSDIAG` line
and the same three banner lines on that boot. (That a normal boot to the
Finder reports `GSOS_ERROR $0046` at all, from three stack-entry returns — an
`Open` of a missing file among them — predates this and is left as it is.) `unarmedhash.ps1` arms the load map on the GS/OS
boot and requires the three drivers and the FST, each with a start block.

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
rail's state now for `MEMVU_STOREVIS`, `MEMVU_LOADVIS`, `A2GSPU_TRAP`,
`A2GSPU_LIVENESS`, `A2GSPU_RECORDS`, `A2GSPU_STRICT`, `A2GSPU_SYMBOLS` and the
pins (seed, faketime, ramdisk, BRAM), or names the verb that reads it, or says
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

**Live arming followed (2026-09-16), on the same pattern.** A rail can register
one apply function with itself (`a2rails::SetterRegistrar`,
`src/a2gspu_rail_setters.cpp`). The headless start calls it with the
environment's value in the order the old inline blocks ran; `rail set NAME VALUE`
/ `rail unset NAME` call it live, on the CTRL rail and on the windowed co-pilot
for rails that do not halt. One parser per rail, so the two forms cannot drift.
The reach table marks those rails `verb=rail`, and both `rail live` and
`railparity.ps1` fail a row with no registration and a registration with no row.
42 rails are settable; the ones that still need a relaunch are the mode switches
(SPIKE_FRAMES, CTRL, PACE, platform and media), the spike-end reports, and the
rails that allocate or install hooks at arm (MEMVU accounting, TAP sessions, the
WIZ5 streams). Unsettable does not mean undiscoverable: `rail find` says which.

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
the executable hash is the field to trust. **The run hash, built 2026-09-16:**
every headless run's `GSDIAG:` carries `insns= stores= run=`, where `run` is
house FNV over instructions executed, CPU stores, the screen hash and cycles
(`src/a2gspu_runhash.hpp`), and `A2GSPU_EXPECT_RUN=<hex>` gates on it. The
counts come from the progress taps the liveness rail also uses, armed before
the first frame; the armed boots in `unarmedhash.ps1` reproduce the unarmed hash
and cycle count with them on.
**Added later, because a pin was missing:** `bram=`. The stock RTC reads and
rewrites `./gs2-bram.bin` in the working directory, so two runs with equal
seed, faketime and ramdisk hashed differently depending on where they ran
(`5F78F7C15131F43F` fresh, `89FCD3DA7D947DFF` beside a rewritten BRAM file) —
which is also why an "unarmed hash" quoted in earlier commit messages was never
reproducible. `A2GSPU_BRAM=default|<file>` pins it and PINS reports its hash.

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
was three rails. **Added 2026-09-16:** `read=ADDR[-ADDR]` (a byte read as
operand data, from the CPU's data-read funnel) and live arming — `trap add
<spec>` / `list` / `clear` on the CTRL rail, where a malformed spec fails the
ack instead of ending the session. `KEYSCHED` waits can be `WHEN <cond>` in the
same condition grammar instead of a text-page substring, and a schedule that
stops short says where and why at exit. The WIZ5 traps still stand until their
consumers move.

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
names-by-default check now read through the probe everywhere.

**The comment outlived the note, and seeded four more consumers.** Saying the
note was wrong did not delete the sentence that produced it: `mmu_iigs.hpp`
still said "banks `$00/$01` are handler pages … so `read_raw`/`probe_peek`
return floating-bus `$EE` for them", nineteen lines below a `probe_readable`
whose own comment says the opposite. Four places had been built on it — the
`OBS FAULT VIEW`'s `'?'` flag and its printed legend (`obs_iigs.hpp`),
`iigs_probe_suspect()` and the `SYSFAIL` dump's `[** N SUSPECT $EE … do NOT
trust as memory truth **]` banner (`iigs_toolbox.hpp`), and the second
`read()`-path row in the `PCTRAP` dump (`cpus/base_6502.cpp`). `$EE` is `INC
abs` and an ordinary data byte, so every genuine `$EE` in bank `$00` or `$01`
was being labelled untrustworthy on the one platform where the probe is right —
a false instrument in the sense of AGENTS.md rule 3, since making an agent
distrust correct data costs what making it trust wrong data costs.

The comment is rewritten, and the two flags in `obs_iigs.hpp` and
`iigs_toolbox.hpp` now ask `probe_readable` — the MMU's own statement about the
address, kept in lockstep with `probe_peek` by the MMU that implements both —
instead of guessing from the value. That is strictly wider as well as correct:
it catches a handler-backed page returning *any* value, not only `$EE`, which
is what the II family needs.

**Closed: the call tracker and every other observer read.** `iigs_instr_peek()`
routed every read outside `$C000-$CFFF` through `cpu->mmu->read`, so
`CALLTRACE`, `CALLSTREAM` and the attribution `BLAME` builds on them were on the
CPU's path — and so were the `A2GSPU_TAP` session dumps, the WIZ5 traps,
`BRKMEM`, the `PCTRAP` dumps and `A2GSPU_DUMP`. They now share one read,
`a2obs_peek` (`src/a2gspu_observe.hpp`): the probe on a IIgs, and on the II
family the MMU read outside `$Cxxx` (where it has no side effect, and where the
II family's `read_raw` probe would not resolve the language card — the reason
this was left open). The before/after the paragraph that stood here asked for:
a 300-frame IIgs boot and a 120-frame IIe boot, each run unarmed and with
CALLTRACE, CALLSTREAM, BLAME, DUMP, BRKMEM and a TAP session armed. Before, the
IIgs TAP-session run ended **754 cycles short** of unarmed (10,634,635 against
10,635,389) with the same SHR hash — a real disturbance only a cycle count
could see. After, every armed run on both platforms reproduces the unarmed hash
and cycle count exactly, and the CALLTRACE, CALLSTREAM, DUMP and BLAME outputs
are byte-identical to before. The TAP session's display flags now come from the
video scanner (what `vid` reads) rather than from reading `$C01A-$C01D`. The
stale `PCTRAP` justification is gone with the `read()` row it justified.

**A third site, found by an audit and now closed: the disassembler.**
`Disassembler::read_mem` (`debugger/disasm.cpp`) guarded `$C000-$C0FF` with a
comparison against the *full 24-bit* address, so the guard only ever fired in
bank zero and `dis E1C055 1 <file>` fell straight through to `mmu->read`. One
`observe` verb physically set `PAGE2`, and — because the decoder also read the
operand bytes at `$C056/$C057` — `HIRES`. The bound was exclusive too, so
`$C0FF` itself was never covered. It now tests the low 16 bits inclusively and
reads everything else through `probe_readable`/`probe_peek`; the low-16 guard
has to come *first*, because `MMU_IIgs::probe_peek` takes an early exit for
banks `$E0/$E1` into the Mega II image with no `$C0xx` test. The `$C400`
mockingboard hack is deleted rather than widened: it masked only bits 8–15, so
it returned `$DD` for `$7EC4xx` — real RAM — in every bank, and `probe_peek`
cannot disturb a 6522 anyway. An unreadable byte prints as `<unreadable>`
instead of being decoded from the `$EE` placeholder into a fictional
`INC $EEEE`, and the ack carries `unreadable=N`: M11's "print `unreadable`
rather than `$EE`" rule, applied where it was still being broken. One
imperfection was named rather than hidden: at `$E1:C100-CFFF` `probe_peek`
answered with Mega II RAM rather than the slot ROM the CPU would fetch.

**Closed 2026-09-16.** The CPU reads `$E0/$E1:C000-CFFF` through the Mega II's
page table (`bank_e0_read`, `bank_e1_read`), and the probe now does the same:
`read E1:C600` returns the slot firmware `read 00:C600` does, and `$E0/$E1:C0xx`
is `unreadable`, as `$00:C0xx` always was. The write side had the matching lie
one level up: `probe_poke` refused switches, ROM and write-inhibited memory by
returning silently, and the `poke` verb acked `poked N byte(s)` over the refusal.
`probe_poke` now says whether the byte landed, and a refused splice fails the ack
(`status=FAIL poke-refused refused=N landed=M first=$BB:AAAA`). The base MMU's
version also stopped storing through a page with a read pointer and no write
pointer (ROM), which `write_raw` would have done.

What remains of M11 is the rule — an observe rail reads through `a2obs_peek`,
never through the CPU's read path. `tools/a2rail/unarmedhash.ps1` makes the rule
a gate: armed observation runs must reproduce the unarmed hash and cycle count,
and every armed rail's sink must hold records, so a rail that silently failed to
arm cannot make the comparison vacuous. The instruction trace, the region logger,
the call tracker's stack reads and the `SysFail` decoder read through
`a2obs_peek` too, and the gate arms the first two.

### M12 — The manifest carries record schemas  (P3, P7) — S

`manifest <file>` already enumerates verbs and rails. Add, per rail, the
event names it emits and their fields, generated from the same table M1's
writer uses. A consumer can then validate records against the schema and a
schema change is a visible diff.

**Status:** done as a declared table rather than one generated from the
writer — by choice, so the shape is declared beside the writers and a change
of shape is a diff in one table. (This paragraph used to give the reason as
"M1's writer does not exist yet". M1 shipped: `src/a2record.hpp` is the one
writer, and `railparity.ps1` enforces it. Four screens of the same document
said both things, which is the M11 failure again in the file that named it.)

`record_schemas` in `a2gspu_manifest.hpp` lists, for every rail that writes
NDJSON — **ten** of them: the `A2GSPU_RECORDS` envelope row, TRAP, LIVENESS,
WATCH_OUT, CALLSTREAM, SNAP_OUT, TAP and the three WIZ5 sinks — the tag that
names a record and the fields a consumer may rely on, optional ones marked.
`rail schema [NAME]` serves it live and checks every row against the registry
(`status=OK record-writing rails=10`); `manifest <file>` carries it as a
section.

**Seven of the ten now share the `event=` envelope**, precisely because M1
landed; the three that do not are the WIZ5 title sinks (`t=`, `op=`, none),
which are the named exemption in `railparity.ps1` and convert when their
readers do. So a consumer writes one envelope parser plus three legacy cases,
not ten parsers. The old closing sentence here — "the eight shapes are visibly
eight different conventions, which is the argument for M1" — was the argument
for a change that has since been made, and left standing it argued a consumer
out of trusting the surface M1 built.

---

## 4. Order

Cheap and prevents the most: **M3, M2, M6, M1** — an afternoon each, and
between them they cover four of the six events. Then **M4** and **M9**, which
turn two of the MEMVU campaign's hard-won rules into gates. Then **M5**'s manifest,
**M7**, **M8**. **M5**'s classes, **M10**, **M11** and **M12** are the
longer road.

## 5. The week, replayed

With the shape in place: the missing blit binary is a fatal error at arm
(M3); the one-disk demo reports `screen_unchanged_since=312` (M4); the `device` line
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
