# A2GSPU rails — the instrumentation contract catalog

## Native inspection

`inspect cpu|devices|signals|capabilities <file> [signal-glob]` writes native
`a2inspect-v1` JSON on CTRL or the windowed co-pilot. Quote paths with spaces.
It samples internal model fields without guest bus reads or parsing human
diagnostics. CPU register widths are explicit. The absolute session cycle stamp
is a decimal string, labelled `session-absolute-NClock`; it does not promise
continuity across restore. Other 64-bit values use decimal-string encoding.

Device inventory identifies native typed fields and remaining coverage gaps;
unregistered native observers are unavailable rather than fabricated. Initial
native RTC, SCC and ADB observers expose registers, FIFOs, line/IRQ status and
BRAM. Human `devstate` output remains separate. Registered Observatory scalar,
array and byte-window descriptors include types, widths, flags and availability.
Event-only, missing-owner, unarmed clock and undeclared-array-length values
carry explicit reasons. Array/window previews cap at 256 entries with a
truncation flag. Registry coverage is partial, not a complete machine schema.

`json cpu` adds native `a2cpu-v1` data; `json run`, `json step` and
`json run-until` add `a2execution-v1` in-command cycle/instruction counts and
CPU state. `json inspect ...` returns an `a2inspect-ref-v1` artifact reference.
Other commands may return `data:null`. Existing envelope/text fields are
preserved. Control characters are escaped losslessly; oversized envelopes fail
explicitly with `reply-too-large` instead of silently cutting their contents.

`inspect memory <file> <hex-address> <decimal-length>` captures up to 65,536
CPU-visible bytes through the canonical CPU MMU and `a2obs_peek`. A parallel
`readable` boolean array marks unavailable bytes; their zeros are placeholders,
never observations. The artifact reports completeness and unreadable count.
Inspection identifies its serving surface (`ctrl` or `copilot`). Live compound
guest views require a CTRL observation epoch; they refuse co-pilot and changes
to the guest cycle stamp during the read.

## Presentation and clock settings

`ui [status] | monitor composite|rgb|green|amber|white | crt on|off | speed
free|1.024|2.8|7.159|14.3` is available on CTRL, co-pilot and socket agent tools.
Native `a2ui-v1` status lists window flags, monitor, CRT availability/enabled
state, requested/effective clock mode and pending application. Setters return
`a2ui-action-v1` with before/after state, effect and `changed`; an unchanged
request explicitly reports `changed=false`. Unknown values refuse before apply.
Missing GPU shaders fail instead of acknowledging an unavailable effect.

Monitor/CRT operations change host presentation without advancing guest time.
Speed uses the existing menu's request/normal-frame transition and reports that
deferred application. Native settings/readback do not prove physical display
appearance. Host dialogs, clipboard, fullscreen and pointer capture are outside
this tool; explicit `mount`/artifact paths and input injection serve the agent's
guest-facing work. The manual uninstrumented GUI check remains separate.

The socket's AGENT_TOOLS/AGENT_COMMAND expose the full shared command registry;
the Python MCP adapter makes it directly discoverable to MCP clients. See
`Docs/DebugClient.md` for launch/attach and ownership, and `Docs/DebugProtocol.md`
for framing and pause policy. Startup-only rails are configured at managed
launch; discovery never claims every rail is live-settable.

## Live capture and device coverage

`capture scc-a|scc-b|wdm status|off|on <file:path|pipe:endpoint>` binds the
existing TX/console models at runtime on CTRL or co-pilot. Quote the whole
binding when it contains spaces. Active rebinding, duplicate endpoint aliases,
unsupported machines and occupied WDM operands fail. Turn a capture off before
rebinding; the off path releases only its owned operands. A failed arm leaves
other active captures intact. Native status is `a2capture-v1`. `file:` requires
an ordinary file. Byte/write failures retain the original fail-loud contract.

`inspect devices` separately lists every powered-on model (device ID and slot)
and every debug handler. Each model declares observer names, control surfaces,
candidate snapshot blocks and incomplete semantic coverage. A candidate block
is not an inclusion claim: `snapdiff --describe <snapshot>` reports the actual
checked GSPS sections, versions, sizes and per-file manifest.

`python -I tools/a2rail/checkpoint.py capture --ctrl <dir> --snapdiff <exe>
--out <new-directory>` saves a paused GSPS file and native CPU/device/signal/
capability observations from one cycle epoch. SHA256 binds each artifact; the
bundle joins installed models to their available fields and actual snapshot
blocks. `diff <bundleA> <bundleB> --snapdiff <exe> [--out <new-report>]` compares
named native fields plus saved-state bytes. Missing fields remain missing;
uninterpreted payload versions are identified. This is a semantic observation
companion, not a complete decoder for arbitrary historical device payloads.

## Deliberate boundary faults

`fault schedule <delay-cycles-decimal> nmi|abort|irq-on|irq-off | status | clear`
is CTRL-only. It schedules relative to the current CPU cycle counter and
delivers at the first instruction boundary on or after the deadline. Native
`a2fault-v1` status and `fault` subscription events expose requested/actual
cycles and lateness. IRQ uses a separate injector bit; it never borrows a real
card's IRQ source. Redundant IRQ transitions, duplicate pending NMI/ABORT and
unsupported ABORT refuse the command. Reset clears injector state.

NMI and the 65816 boundary ABORT use the architectural vectors and stack
formats. The ABORT model runs before the next opcode: no mid-instruction pin
timing, register rollback or bus-write rollback is claimed. Priority and vector
semantics follow the [WDC datasheet](https://www.westerndesigncenter.com/wdc/documentation/w65c816s.pdf),
sections 2.13, 2.21 and 7.19. Active queued/latched faults, held injector IRQ,
or an owned input key block save/restore because host ownership is not saved.
Existing SCC/SSC/U2 fault rails remain independently discoverable.

## Output content assertions

`audio capture doc|sfx <file> [maxframes] | capture off|status` records bounded
`a2audio-v1` JSONL, independently of aggregate `audio on`. Samples are signed
int16 interleaved values before host resampling: DOC batches include c14m end
ticks and ticks/sample, SFX batches their source sample rate. Sources are never
summed. The default/maximum cap is 1,000,000 frames. Stop finalizes evidence;
overflow, unsupported formats and I/O failures make it incomplete. This does
not capture the host mixer, speaker waveform, DAC or physical sound.

`python -I tools/a2rail/outputassert.py image <expected.png> <actual.png>
[--channel-delta N] [--bad-pixel-fraction F]` compares decoded RGBA pixels with
explicit tolerance, changed-pixel bounds and input hashes. Supported PNGs are
noninterlaced 8-bit gray/RGB/gray-alpha/RGBA; unsupported shapes fail.
`audio <expected.jsonl> <actual.jsonl> [--sample-tolerance N]
[--rms-tolerance F]` checks lifecycle/completeness, source/channel/frame shape,
relative DOC timing and per-channel content errors/RMS. No alignment is guessed.

`text <image.png> <font.json> --columns N --rows N [--x N --y N --threshold N]`
recovers configured bitmap glyphs, including native `shr` PNGs. The
`a2bitmap-font-v1` input has integer `width`, `height` and `glyphs` mapping one
character to rows of `.`/`#`. Unknown or ambiguous glyphs refuse a passing
result. This is explicit grid/font recognition, not universal OCR. Every
assertion supports `--out <new-report>` and returns 0 pass, 1 mismatch, 2 invalid
evidence. Reports include models, thresholds and input hashes.

## Input edges and macros

`input keydown <ASCII-hex> [modifier-hex] | keyup | mouse adb|card <dx-decimal>
<dy-decimal> <buttons-mask-decimal> | status` injects without advancing time.
Keys are owned until keyup; duplicate downs/ups fail. Modifiers use C025 bits
`C3`; IIe supports Apple-key modifier edges and takes shift/control as ASCII.
Mouse deltas must be -63..63; buttons bit 0 is left and bit 1 right (card path
supports left only). An unread ADB packet refuses overwrite. `a2input-v1`
receipts prove injection, not guest consumption. Paddle/button input continues
through `paddle`/`pbutton` with native readback in device fields.

`python -I tools/a2rail/inputmacro.py --ctrl <dir> --plan <plan.json>
--out <new-directory>` records an agent-input timeline. `--replay <record.json>`
repeats the recorded plan and checks relative input timestamps. The
`a2input-plan-v1` contract requires `clock` (`cycles` or `frames`), `initial_pc`,
`max_lateness`, SHA256 `bindings` (`start`, `length`, `sha256`) and ordered
`actions` (`at`, `op` plus the operation's arguments). Operations: `keydown`
(`ascii`, optional `modifiers`), `keyup`, `mouse` (`endpoint`, `dx`, `dy`,
`buttons`), `paddle` (`index`, `value`), `pbutton` (`index`, `down`). Plans must
release held keys; optional `expect_memory` assertions have `start` and `bytes`.
Cycles use instruction boundaries and report lateness; frame mode uses the
NClock video-frame counter. A million execution calls bounds each run. Stale
code, unexpected stops, no progress and exceeded lateness fail with retained
receipts. This records agent inputs, not physical host events or full-machine
replay; a passing injection does not imply consumption unless separately tested.

## Configured guest views

`python -I tools/a2rail/guestview.py schema` gives the machine-readable contract
for source placements and kernel layouts. `inspect capabilities` advertises
this companion and its requirements. It is available with the source checkout;
discovery does not claim a standalone executable package ships it.

`normalize-rosetta --debug <sidecar.dbg> --placement <json> --out <map.json>`
reads Rosetta assembler file/line/segment records. `source --map <map.json>
--pc <hex> [--memory <capture.json>]` returns all matching source locations,
including ambiguity and byte-binding status. Unbound lookup never claims live
verification; a stale byte binding fails. Segment placements and SHA256s are
explicit inputs. Repeat `--compiler-assembly <file.s>` to translate assembler
rows through Rosetta's opt-in `cciigs -g` statement comments
(`;@rosetta-source-v1 line=N filehex=<UTF8-hex>`, reset `line=0 filehex=-`).
File identities must match the assembler sidecar exactly after path
normalization. Generated assembly bytes and loaded code are hash-bound;
`source_text_verified=false` distinguishes this from verifying today's C text.
Columns are unavailable. This supports Rosetta's line-sidecar subset and C
statement markers; DWARF remains separate work.

`dmesg|tasks|stack|panic --layout <json> --memory <capture.json>` reads configured
structures. Alternatively `--ctrl <session-dir> [--owner <token>]` reads live
memory through `CtrlClient` in one paused observation epoch, retaining captured
pages. Offline panic views additionally take `--cpu <native-cpu.json>`; stack
and panic accept `--map <source-map.json>`. `--symbols <System.map>` resolves
layout symbol references. Layouts require immutable code-region SHA256 bindings
so a profile for another kernel cannot silently produce plausible output.

Models are explicitly limited: a monotonic byte ring, a circular intrusive task
list, a configured linked frame chain and configured panic entry PCs. No kernel
version, structure offsets or ABI is inferred. Unknown memory, corrupt indices,
cyclic/over-limit lists, missing terminators and stale bindings refuse the view.
These primitives do not claim that every modern Linux printk layout has been
decoded. Production profiles and additional formats need their own fixtures.
Every kernel result reports its layout hash and binding status.

## Ordered event subscriptions

`subscribe on <kinds> <file> [max-records] | kinds | status | off` is available
on CTRL and co-pilot. `json subscribe kinds` lists accepted comma-separated
filters; unknown kinds refuse the command. The file is a flushed JSONL stream
using `a2subscription-v1`, with `event=subscription.started|event|overflow|stopped`,
monotonic `sequence`, source frame/cycle stamps and a `payload` object. Event
payloads contain a `kind`, clock availability and native `data` (the original
rail record, console bytes, or the command's JSON reply). This includes records
even when their original destination is stdout or a private rail sink.

Coverage is the central record writer, command completion/stops, completed SCC
TX bytes, armed WDM console/audio capture, IRQ edges and command/frame-sampled
native device fields. `device.baseline`/`device.change` include before/after
values and a sampling model; transitions between samples can be missed. It is
an ordered subset, not replay. Output requires an ordinary file;
there is one active subscription per process. The default data-record limit is
100,000 (maximum 1,000,000). Overflow writes an explicit warning record and
counts dropped events; `json subscribe status` exposes `complete=false` and
loss counts, and a lossy or failed subscription reports FAIL. `off` writes a
close-out with retained/dropped totals. Agents must check completeness before
treating an event trace as evidence. File I/O failures are also reported on
stderr and in status. The debug socket exposes the same kinds through its
independent bounded SUBSCRIBE observer; MANIFEST discovers the filters and
queue/loss contract. See `Docs/DebugProtocol.md` and `Docs/DebugClient.md`.

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
> rail set <NAME> <value>   # arm a settable rail NOW -- the same apply function the launch
> rail unset <NAME>         # uses with the environment's value; `rail live` shows live=rail
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


## What this fork is (the contract)

gssquared-fork is an instrumented development platform for several projects, the Rosetta
toolchain's closed loop first among them (see
[`CLOSED_LOOP_INTEGRATION.md`](CLOSED_LOOP_INTEGRATION.md)). Every rail below obeys three hard
invariants:

1. **Additive + env-gated.** Nothing here runs unless an `A2GSPU_*`/`MEMVU_*` env var (or the
   `A2GSPU_CTRL` rail) is set. **No instrumentation env set ⇒ stock emulator**, windowed and
   interactive. A default run pays one untaken branch per rail at its hook — measured as about
   twenty flag tests per instruction in total, with no master gate.
2. **Isolated / separable.** Rails live in fork-only units — the spike (`a2gspu_spike.cpp`), the
   CTRL and co-pilot rails (`a2gspu_ctrl.cpp`, `a2gspu_ctrl_cmds.cpp`), snapshots
   (`a2gspu_snapshot.cpp`), the `a2gspu_*`/`iigs_*`/`memvu_*` headers, the `a2gspu` device — and
   thin gated call sites in core CPU/MMU/video, so an upstream change to the application loop
   and a fork change to an instrument land in different files.
3. **Observe-don't-disturb** for the *observe/trace/break/snapshot/assert* groups: they read
   emulated state through the observer read (`a2obs_peek`) and never change bus behaviour — and
   `tools/a2rail/unarmedhash.ps1` checks it, by requiring armed runs to reproduce the unarmed
   hash and cycle count. The *manipulate* and *run* groups are the agent's **hands**
   (deliberate, documented state changes), and the CTRL rail's input verbs co-operate with
   (do not lock out) live human input.

Groups (the registry's families, which `rail list <family>` uses and `railparity.ps1` holds the
Group column below to): **run** · **observe** · **trace** · **break** · **manipulate** ·
**snapshot** · **assert** · **memvu** · **tap** · **ctrl** · **meta** · **determinism** ·
**device** · **wiz5** · **alias**.
Every rail is env-gated (that is the whole design); the "Gated" column flags the rare
always-on emission (the `GSDIAG:`/`SPIKE …` summary lines) so a harness knows what prints
without any env set.

Where the code lives (by function, not line number — line numbers here went stale twice):

| Piece | Source |
|-------|--------|
| The headless spike: env arming, boot, gates, `GSDIAG:` | `src/a2gspu_spike.cpp` `run_headless_spike`; `src/iigs_diag.hpp` `iigs_emit_status` |
| The CTRL loop and the co-pilot rail | `src/a2gspu_ctrl.cpp` `a2gspu_ctrl_loop`, `a2gspu_copilot_poll` |
| The CTRL verb families | `src/a2gspu_ctrl_cmds.cpp` (`a2ctrl::try_*`) |
| Snapshots, device blocks, manifest, SAVE_AT | `src/a2gspu_snapshot.cpp` |
| CPU/MMU micro-tests | `src/a2gspu_selftests.cpp` |
| What `help`/`manifest`/`rail` serve | `src/a2gspu_manifest.hpp` (`verbs[]`, record schemas), `src/a2gspu_rails_registry.hpp` (rails, reach, readers) |
| Hook sites in the CPU | `src/cpus/base_6502.cpp` (`bus_read`/`bus_write` funnels, the per-instruction block) |
| The one observer read | `src/a2gspu_observe.hpp` (`a2obs_peek`) |
| Traps, taps, liveness, records, pins, BRAM | `src/a2gspu_trap.hpp`, `src/generic_tap.hpp`, `src/a2gspu_liveness.hpp`, `src/a2record.hpp`, `src/a2gspu_pins.hpp`, `src/a2gspu_bram_pin.hpp` |
| Device rails | `src/devices/a2gspu/a2gspu.cpp`, `src/devices/adb/ADB_Micro.hpp`, `src/devices/rtc/RTC.hpp`, `src/devices/pdblock3/pdblock3.cpp` |
| Toolbox tracing, symbols, video summaries | `src/iigs_toolbox.hpp`, `src/iigs_symbols.hpp`, `src/iigs_video_summary.hpp` |

---

## Run — drive the machine headless to a verdict

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `A2GSPU_SPIKE_FRAMES=N` | run `N` deterministic frames headless (with `-n`), then evaluate the dump/assert/golden gates and exit. The master headless switch. Its raw artifacts (`spike_e1.bin`, `spike_e1_init.bin`, `spike_trace.bin`, `spike_slot.bin`, `spike_mmu_truth.bin`) keep their headerless bytes, and each gets `<file>.meta.json` (schema `a2artifact-v1`: format, record layout, count, the writer's content hash and what it covers, the file's own hash) plus a `SPIKE ARTIFACT:` line; `tools/a2rail/artifacts.py raw <file>` reads and verifies one | run | env |
| `A2GSPU_PACE=fast\|realtime` | **headless frame pacing.** Every frame used to end in a busy-wait to the next 16.69 ms deadline, headless or not: `run 600` on the CTRL rail took 10 s whatever it did, and each session spun a core. `fast` — the default for every headless run, spike or CTRL — skips that wait. Frames end at a cycle count, not a time, so the hash, `cycles=` and `run=` do not move (`unarmedhash.ps1` runs paced-by-default and proves it). Audio is the one thing that read the wall clock: the speaker generated samples only while SDL's real-time-drained queue was short. So `fast` pauses the audio device and empties the streams after each frame; samples are still generated every frame and counted by the `audio` probes, which makes them deterministic rather than host-speed-dependent. `realtime` keeps the stock wait and sound. Windowed runs are never changed. An unknown value is fatal (exit 2). `A2GSPU PINS:` carries `pace=`, and `rail read A2GSPU_PACE` answers it. Wall-clock devices (an unpinned RTC or clock card) see less host time per emulated second when fast: pin `A2GSPU_FAKETIME` for runs that must repeat | run | env |
| `A2GSPU_SPIKE_KEYS=<str>` | inject keystrokes via the keyboard paste buffer (`\n`→`\r`), paced by the guest's own `$C000` polls | run | env |
| `A2GSPU_SPIKE_KEYS_AT=N` | frame at which `SPIKE_KEYS` is injected (default `spike_frames/2`) | run | env |
| `A2GSPU_RUN_BIN=path[@HEX]` | after boot frames, inject a flat binary into IIgs RAM (default load/exec `$7E0000`), reset the traces to isolate it, set PC to it. **Writes are verified by read-back and confined to one bank**: every byte is re-read through `probe_peek` and the run aborts (exit 2) if any differs, so `@00D000` can no longer report "injected 13 bytes" into ROM; a binary that would cross the bank end is **refused** before the first write, naming the byte that would wrap, because a binary silently folded back over its own entry point is worse than a refusal; and a bank-0 `$D000+` load banks the language card in (two reads of `$C08B` — a real soft-switch side effect, and the only way such a write lands at all) | run | env |
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
| `A2GSPU_SCC_A=file:<path>` or `pipe:<endpoint>` | Passive SCC channel A TX-wire capture, in SPIKE and CTRL: records completed bytes excluding local loopback. Requires an SCC. Also live through `capture scc-a`; `rail read A2GSPU_SCC_A` reports binding and byte count. Empty capture means no completed bytes. Startup and sink write errors are fatal (exit 2); live arm errors return FAIL. | observe | env + capture |
| `A2GSPU_SCC_B=file:<path>` or `pipe:<endpoint>` | SCC channel B, independently bound and counted. Also live through `capture scc-b`; `rail read A2GSPU_SCC_B` reports binding and byte count. | observe | env + capture |
| `A2GSPU_WDM_CONSOLE=file:<path>` or `pipe:<endpoint>` | 65816 console: `$FE` emits A.low; `$FD` emits Y bytes at X.low:A (maximum 4096, full A/Y even in 8-bit mode). Reads use `a2obs_peek`; unreadable or overflowing ranges fail before output. Preserves registers/flags and two-cycle timing. Operands unhandled when unset; Host FST `$FF` separate. Also live through `capture wdm`; readback reports binding and bytes. | observe | env + capture |
| `A2GSPU_DUMP="BANK/LO-HI[,…]"` | hexdump memory ranges (hex). `$E0`/`$E1` read the Mega II image directly (side-effect-free); other banks via the MMU | observe | env |
| `A2GSPU_TEXTDUMP=<file>` | dump text page 1 (2KB): main `$0400-07FF` via `probe_peek` (no `$C0xx` side effects) + aux `$0400-07FF` (80-col even columns) | observe | env |
| `A2GSPU_TEXT40=1` | at spike end, ASCII-decode the 40-col text page (`$E0:0400-07FF`) to stdout | observe | env |
| `A2GSPU_VIDEOSUM=1` | per-line SCB (320/640) + palette histogram (RGB888) + mode-correct pixel-index histogram | observe | env |
| `A2GSPU_VIDEOMAP=1` | a 40×25 ASCII dominant-index map of the Super Hi-Res window (SEE layout headless) | observe | env |
| `A2GSPU_LIVENESS=N` | **progress proof.** Every `N` frames hash the screen (IIgs: SHR `$E1:2000-9FFF` from the Mega II image; II-family: HGR `$2000-3FFF`) and the text page, side-effect-free, and print `frame= cycles= screen= text= kbd= changed=<first\|screen\|text\|both\|none> insns= screen_store= text_store= kbd_poll=`. The last four say **why** a hash did not move: `insns` is instructions executed since arm; `screen_store`/`text_store` are the cycle of the last store that landed on that range (IIgs: a direct `$E1`/`$E0` store or one the MMU shadowed there; II family: the CPU address), and `kbd_poll` the cycle of the last `$C000`/`$C010` read, each `none` until the first. A static screen with a recent `screen_store` is a redraw of identical bytes; with an old one, drawing stopped; with `insns` not advancing, the CPU stopped. `kbd=` counts reads on both families (it was `n/a` on the II family). Close-out gives `screen_last_changed=` / `screen_unchanged_since=<frame>\|none` and the same for `text_`, tracked separately (a halted SHR workload keeps redrawing its text page from its wait loop — measured), plus `cpu_stopped_since=<frame>\|none` and the four tap values; the three `_since` fields are appended to `GSDIAG:` while armed. A **signal, not a verdict**: an idle desktop is static too; what it rules out is a run that halted on an error screen while its counters kept climbing (the one-disk Dealer Demo measured as a workload until its hash was seen not to move) | observe | env |
| `A2GSPU_LIVENESS_STALL=K` | unchanged samples in a row before the screen counts as static (default 3) | observe | env |
| `A2GSPU_LIVENESS_OUT=<file>` | NDJSON sink for the samples instead of stdout lines | observe | env |
| `A2GSPU_LOADMAP=<file>\|1` | **what the System Loader placed where.** Every `InitialLoad2`/`InitialLoad` the call tracker sees becomes a `loadmap.load` record as it returns — `path` as the caller passed it (`*:SYSTEM:FSTS:Char.FST`; `AppleDisk3.5` for a driver, which GS/OS loads by leaf name; `null` for a load from memory), `name` (`Load.<file>`, or `Load.uid$XXXX` without a path), `uid`, `start`, and `blocks[{handle,addr,size,owner}]`: the Memory Manager handles the Loader allocated inside the call that the load's userID still owns at the return (its own buffers are not). `start_block` is the block holding `start` — `-1` would mean the handle record was read wrong, and every load of a GS/OS 6.0.1 boot has one. A failed load is `loadmap.fail` with `err`. `<file>` is the sink; `1` sends records to `A2GSPU_RECORDS`, else stdout prefixed `A2GSPU LOADMAP:`. **Names:** a PC in a load's block is `Load.<file>+$off` (`Load.<file>#<n>+$off` for the nth block of a multi-block load) — in `TRAP` records' `sym`, `STOREVIS`, anywhere names by default reach — only while that handle's master pointer record still says the same owner and size and a non-nil address, so a purged or disposed block is unnamed rather than misnamed and a moved block is named where it is now; and a RAM tool entry's name stops at the end of the block it is in, where it used to run on for `$800` bytes. Loads are seen only while the call tracker runs (`TRAP`, `BLAME`, `TBTRACE`, `CALLSTREAM`, or this rail, which arms it — at launch for the boot's drivers, or live with `rail set`, which sees later loads). Without this rail the map is still kept and names still work; nothing is printed. `rail read A2GSPU_LOADMAP` lists each load with how many of its blocks are still live. Measured on a pinned GS/OS 6.0.1 boot to the Finder: 26 loads — five from memory, the three drivers, `Char.FST`, the `SYSTEM.SETUP` files, the Control Panel, `START`, the Finder (10 blocks) and ten RAM tool sets; a trap in `AppleDisk3.5`'s block records `Load.AppleDisk3.5+$4E7` with `incall` `GSOS.$202D`. Not seen: segments loaded later by `LoadSegNum`, and loads before the tracker ran. `A2GSPU_LOADTRACE` is the other view — write bursts into a region, with no Loader involved | observe | env |
| `A2GSPU_RECORDS=<file>` | **the record contract's global sink.** Every rail record has one shape — `{"event":"<rail>.<kind>","frame":N,"cycles":N,…}` then the rail's own fields, one whole line (`src/a2record.hpp`) — and a record whose rail has no sink of its own (`A2GSPU_TRAP_OUT`/`out=`, `A2GSPU_LIVENESS_OUT`, `A2GSPU_WATCH_OUT`, `A2GSPU_CALLSTREAM`, `A2GSPU_TAP`) lands here. Without it those rails print their stdout line as always. `rail read A2GSPU_RECORDS` reports the sink, and **per rail two numbers** — `<rail>=<N>sink+<M>own` — where `N` reached *that* sink and `M` went to the rail's own sink or stdout (`<rail>=<M>own` when there is no global sink). One number beside `sink=` read as the sink's count and was not: a session with per-rail sinks answered `sink=…\global.ndjson rails=2 watch=257 callstream=46` while `global.ndjson` was zero bytes long. Global readback flushes first and returns FAIL on a stream error; failed late global close exits 2. Counters describe accepted writes and require finalization for completeness. `rail schema <RAIL>` gives the fields. `railparity.ps1` fails JSON printed outside the writer | observe | env |
| `A2GSPU_ROM=<path>` | ROM image path for the lazy-loaded ROM-vs-kernel PC classifier (default `resources/roms/apple2gs/main.rom`) | observe | env |
| `A2GSPU_SYMBOLS=<file>` | load a symbol table (annotate trace/PCs with names). **Two accepted forms:** the linkiigs `.final.dbg` line `symbol "NAME" type LABEL addr $XXXXXX` (only `LABEL`/`ENTRY` are kept — `EQU` constants are not code addresses), and a plain `HEXADDR name` line (`#` comments skipped) — the same plain form `A2GSPU_ROM_SYMBOLS` and the shipped `assets/symbols/apple2gs/<ROM-FNV>.sym` use. A file that cannot be opened, **or that yields 0 symbols in either form**, is a `WARNING` (so `GSDIAG: warnings=N` is non-zero and `A2GSPU_STRICT=1` exits 3) rather than a silent `loaded 0 code symbols` — an unloaded table makes every downstream `sym` field null, which reads as "this program has no symbols". `rail read A2GSPU_SYMBOLS` answers `A2GSPU_SYMBOLS=<path> format=<dbg\|plain\|mixed\|none> count=<n> base=$XXXXXX` | observe | env |
| `A2GSPU_ROM_SYMBOLS=<file>` | name ROM-resident PCs in the trace region tags (else `<ROM>`). A file that cannot be opened is fatal at launch (it used to fall back to the seeded names without a word); `rail set` loads one mid-session, adding to the names already loaded. **Names by default:** even without it, the documented firmware entry points (the Apple II Monitor vectors `COUT RDKEY GETLN HOME …` in banks `$00` and `$FF`, the IIgs `ToolDisp`/`GSOS_Disp` vectors, ProDOS 8's `MLI`) **and the ROM's own toolbox function entry points** — 526 of them for ROM 01, read from the system tool pointer table by `tools/a2rail/romfpt.py` and shipped as `assets/symbols/apple2gs/<ROM house-FNV>.sym` (installed by the build beside the ROMs, so it loads from any working directory) so a different ROM gets no wrong names — name PCs in the new attribution outputs, `STOREVIS writers top:` and `TRAP` records' `sym`. `$FF/46ED` is `SoundTools.FFStartSound+$2FC`. A PC between two entries is named after the preceding one, which is right for a function body and wrong for a shared helper; a `TRAP` at `E1:0000` recording `x` is the authority for helpers. RAM-resident tool sets — most of them under System 6, which patches the ROM sets and loads the rest — are named from the **live** tool pointer table (`$E1/03C0`), walked side-effect-free when a RAM address misses and the table has moved or grown; one `IIGS SYMS: live tool pointer table …` line reports each walk. **GS/OS's kernel** is named `GSOS.<Segment>+$off` (`GSOS.Dispatcher+$1FD`, `GSOS.SCMMainBody+…`) from `assets/symbols/apple2gs/gsos.segs`, which `tools/a2rail/gsosmap.py` writes from the boot volume's own `SYSTEM/GS.OS` (segment addresses and per-page hashes, no code) — and only while the live page hash-matches the file, so an overlapping, banked-out or replaced segment is unnamed, not misnamed. The System Loader's calls are tool set 17 (`SystemLoader.InitialLoad2`). An address nothing covers prints as `(unnamed)` / `null` | observe | env |
| `A2GSPU_SYM_BASE=<hex>` | pin the symbol-relocation base (disables auto-inference) | observe | env |
| `A2GSPU_SYM_SUSPECT=1` | append `[SUSPECT reused]` to any symbol whose name resolves at >1 address (advisory only) | observe | env |
| `A2GSPU_BUS_DUMP=<path>` | `~BUS` cycle dump (source = slot_bus) for the gssquared→card host gate | observe | env |
| `A2GSPU_BUS_STREAM=1` | live `~BUS` slot-bus CDC streaming to the card's raw-cycle input | observe | env |
| `A2GSPU_SLOT_BUS_FORMAT=1\|2` | the slot-bus stream's format. **`1`** (the default) is the `~BUS` stream as card firmware decodes it today, unchanged. **`2`** (`~BS2` frames) is the stream the fidelity study describes (`Docs/fidelity/slot_bus.md` §2.3): `/DEVSEL` and `/IOSEL` decode for the card's actual slot; `$C0xx` is slot-visible only in banks `$00`/`$01` with IOLC shadowing on, or `$E0`/`$E1` (format 1 emits it in fast-RAM banks too); `$C100-$C7FF` is emitted with `/IOSEL` when the page belongs to a card (INTCXROM off and the `$C02D` bit, SLOTC3ROM for `$C3`) and `$C800-$CFFF` with `/IOSTRB` when a card owns the expansion ROM; M2B0 is the CPU's bank bit for a shadowed write and high for every `$E1` access (TN68); `$E0` writes and `$E0`/`$E1` RAM reads are emitted; bit 6 (`/INH`) is never set, because `/INH` is driven by a card; cycles that assert no `/M2SEL` are not emitted (`$C035-$C037`, reads of `$C02D`, `$C068` and `$C071-$C07F` [TN68]); bit 7 is set when the machine was in fast mode, so the slow cycle was an isolated synchronised one; `/DEVSEL` and `/IOSEL` follow the slot register (`$C02D`), the same decode the emulator's I/O dispatch uses. Any other value is fatal (exit 2). The default changes when the card firmware decodes format 2 | observe | env |
| `A2GSPU_CARD_CMDLOG=<file>\|1` | **the A2GSPU card model's command register, as records.** The card watches the slot bus and never drives it, so the host's one channel to it is the write-only register `$C0nF` (`$C0BF` in slot 3), a byte per write: `[SYNC $A2][seq u16 LE][op][len][payload × len]`. The model assembles frames by the card's rules — bytes before a SYNC are skipped and counted; a `len` over 32 drops the frame and resyncs at the header, a `len` of `$A2` being a new SYNC; `len` 0 completes at once; `seq` wraps at 16 bits, a forward jump of d adding d−1 to `gaps`, zero or backward counting as a duplicate; at most 16 commands per video frame, the rest counted as `overflow` — and writes one `card.cmd` per frame (`seq op len payload gap dup overflow`, `name` NOP for op `$00`, HELLO for `$01`, empty otherwise: any other op is recorded by number and payload with no meaning given) and one `card.drop` per skipped run or oversize frame. `rail read A2GSPU_CARD_CMDLOG` and `devstate A2GSPU` give the counters (writes, frames, presync_skipped, oversize, gaps, duplicates, overflow, nop/hello/other, the last op, HELLO's version). Nothing here changes the machine | observe | env |
| `A2GSPU_U2_EVENTS=<file>\|1` | **the Uthernet II's W5100, as records.** One `u2.cmd` per socket command the guest writes to `Sn_CR` (`slot sock cmd name from to ir result`: the status before and after, the `Sn_IR` bits it raised, and `done`, `ignored-state` for a command the chip ignores in that state — LISTEN or CONNECT outside a TCP `SOCK_INIT`, DISCON off a connection, SEND on a socket that cannot send — `refused` for one the chip or host turned down — MACRAW on a socket other than 0, a LISTEN whose host port could not be bound — or `not-emulated` for one that did nothing: SEND_MAC, PPPoE, an unknown code, each also reported once on stderr, because `Sn_CR` self-clears either way and the guest cannot tell). One `u2.state` per status change the host network caused (`cause` connected, remote-closed → `CLOSE_WAIT`, reset or failed), applied **only at the video-frame boundary** — the one place host network timing crosses into emulated time. One `u2.drop` per datagram or frame the receive buffer had no room for, or host event for a socket the guest had since closed or reopened (`stale-event`: every OPEN, CLOSE, DISCON and reset starts a new socket generation). `rail read A2GSPU_U2_EVENTS` gives the counters; `devstate uthernet2` gives the chip (MR, the address latch, IR, the addresses, the slirp network the run was given, host byte and ring counters, and every socket's rings and generation). Snapshot block `u2` holds the chip; a connection cannot be saved, so a restore brings a connected socket back `CLOSED` with DISCON raised and says how many. Nothing here changes the machine | observe | env |
| `A2GSPU_COVERAGE="LO-HI"\|"BANK:LO-HI"` | **per-byte execution bitmap** over that range, one bit per byte, set when the byte was part of an executed instruction — this is what turns deasmiigs's code/data split from a heuristic into a measurement (`--coverage`). A lower bound, never an upper one: a byte that did not execute in this run is *not proven code*, not *proven data*; union several runs to widen it. Live equivalent: the `cov` verb, which also binds a source plan (`A2COV2`) | observe | env |
| `A2GSPU_COVERAGE_OUT=<file>` | file the bitmap is flushed to. Written **before** the gate block, which exits on any gate and would otherwise throw the run's coverage away | observe | env |
| `A2GSPU_HGR_PAGE=1\|2` | which HGR page the II-family dump/golden gate reads — for a workload that gates on the page it just drew | observe | env |
| `A2GSPU_KEYRAM=1` | dump RAM once per keystroke; needs a session directory to write into (`A2GSPU_TAP_SESSION`). Canonical name for `WIZ5_KEYRAM` | observe | env |

*Always-on spike output (no env needed):* the spike prints `SPIKE E1:` (SHR-window
nonzero/distinct/FNV hash + SCB row), `SPIKE TRACE/SLOT/MMU/OBS/BRACKET:` lines, writes
`spike_e1.bin` / `spike_trace.bin` / `spike_frame.bmp`, and the `GSDIAG:` status line (below).
It also prints, first, an `A2GSPU PINS:` line — the identity of the run: `seed= faketime= ramdisk=`
as the rails read them (`unset` when unset), `bram=` — the battery RAM the machine started from, kind and hash
(see `A2GSPU_BRAM`), `platform= frames=`, the loaded ROM's size and
house-FNV, the configure-time build version (labelled, because it lags the tree), and the house-FNV
of the running executable, which cannot lag. With `GSDIAG:`'s `cycles=` and `hash=` this is what
makes a number traceable to the binary and inputs that produced it, and what a harness checks before
it differences two runs.

## Trace — execution / dispatch / load / mode streams

Console files are binary byte streams, truncated at launch, flushed after each
write, and closed with a byte-count summary (including zero). Relative file
paths resolve under `A2GSPU_OUT_DIR` when set, otherwise the working directory.
Pipes must already exist and have a reader: Windows named pipes use a bounded
100 ms write deadline; POSIX FIFOs use nonblocking writes. Missing endpoints,
disconnects, backpressure, unsupported machines and invalid binding syntax exit 2.
Use distinct resolved paths for channel A, channel B and the WDM console.

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `A2GSPU_TBTRACE=1` | trace Tool Locator (`$E10000`) + GS/OS class-0/1 dispatch (`$E100A8`/`$E100B0`): call name on entry, carry+A on return. **A harness that counts `IIGS TOOLBOX: ret` lines sees more of them since `e68c82e5`:** returns buried under an unmatched GS/OS inline call used to print nothing. The pinned 600-frame Finder boot went from 1236 returns (62 `carry=1`) to 1269 (84); an A/B build of the old tracker reproduces the old numbers, and the 22 new failures are 18 `NewHandle` `err=$0201` and 4 tool `$2111` `err=$1101`. Rosetta v3's `max_tool_errors` budget was re-measured to match | trace | env |
| `A2GSPU_TBTRACE_BANK=<hex>` | scope `TBTRACE` to a single bank | trace | env |
| `A2GSPU_TRACE_FROM=<hex24>` | arm tracing only once `full_pc` first hits this 24-bit PC | trace | env |
| `A2GSPU_ERRHOOK=1` | surface carry-set GS/OS dispatch errors (`<-- SYSTEM/LOADER ERROR` for the stack-based entry), and print every inline-entry return as `IIGS GSOS: ret inline $NNNN carry= err=` (see `A2GSPU_CALLSTREAM`) | trace | env |
| `A2GSPU_ITRACE_FROM=<hex24>` | per-instruction trace, armed when PC first hits this 24-bit address | trace | env |
| `A2GSPU_ITRACE_FRAME=<N>` | per-instruction trace, armed at the start of headless frame N | trace | env |
| `A2GSPU_ITRACE_N=<n>` | cap logged instructions (default 256) | trace | env |
| `A2GSPU_ITRACE_LO=<hex24>`, `A2GSPU_ITRACE_HI=<hex24>` | restrict ITRACE to a PC window `[LO,HI]`; setting either one also **enables** ITRACE (they are the master switch that has no bare `A2GSPU_ITRACE` behind it — that name is not a rail and the binary rejects it) | trace | env |
| `A2GSPU_ITRACE_REARM=<n>` | re-arm ITRACE this many times (windowed re-capture) | trace | env |
| `A2GSPU_ITRACE_OUT=<file>` | write ITRACE to a file instead of stderr | trace | env |
| `A2GSPU_TRACE_EXT=1` | append recorded DBR/DP/NClock-cycle columns to trace lines; formatting is repeatable and does not infer video phase | trace | env |
| `A2GSPU_CALLTRACE=1\|<hexPC>` | JSR/JSL call trace, armed from frame 0 (`1`) or at a PC | trace | env |
| `A2GSPU_CALLTRACE_N=<n>` | cap CALLTRACE entries | trace | env |
| `A2GSPU_CALLTRACE_SKIP=<n>` | skip the first n calls before logging | trace | env |
| `A2GSPU_CALLSTREAM=<file>` | symbol-free NDJSON of the toolbox/GS-OS call sequence (for `tools/gdiff/calldiff.py` first-divergence diffing). A GS/OS call's record carries `gsos`, its own call number (`$00xx` class 0, `$20xx` class 1 — `$2010` Open …), beside `word` (the tool call number, `0` for GS/OS); `kind` is the entry it came through, `1` the inline entry `$E100A8` and `2` the stack entry `$E100B0`, each of which takes both classes. **Inline returns are matched** since 2026-09-16: an inline call (`JSL $E100A8`, call number, parameter pointer) returns 6 bytes past its JSL, and the tracker had waited at the JSL's return address — 664 of 664 went unmatched on a GS/OS boot, and each stale entry hid the return of the call beneath it (40 toolbox returns, including every `InitialLoad2` after the first driver's). Inline errors are routine (a boot to the Finder returns 49: `GetDirEntry`'s end of directory `$61`, `ExpandPath`'s buffer too small `$4F`), so they appear here and under `A2GSPU_ERRHOOK`, and do **not** feed `GSDIAG`'s `gsos_err`/`GSOS_ERROR` or the `SYSTEM/LOADER ERROR` banner, which the stack-entry returns alone drive, as they always did — harnesses read those as a fault | trace | env |
| `A2GSPU_LOADTRACE="BANK[:LO-HI]"` | segment-overlay tracker: log contiguous write-bursts (segment loads) into the region + when they overlay prior loads. For what the System Loader loaded, from which file, into which Memory Manager blocks, see `A2GSPU_LOADMAP` | trace | env |
| `A2GSPU_LOADTRACE_MIN=<n>` | minimum burst length counted as a load | trace | env |
| `A2GSPU_LCTRACE=1` | Language-Card soft-switch access log | trace | env |
| `A2GSPU_MODETRACE=1` | CPU mode-transition events (XCE / REP / SEP → e/M/X changes) | trace | env |
| `A2GSPU_INTLOG=1` | interrupt-entry logger (IRQ/BRK/COP entries) | trace | env |
| `A2GSPU_MILESTONES=<file>` | load a boot-milestone list (`HEXADDR name` lines); each is announced on stderr as it is reached, and the spike end prints a reached / NOT-REACHED table. `rail read A2GSPU_MILESTONES` answers the same table mid-session (`RESET@0 RDKEY@277 NEVER:NOT-REACHED`); `rail set` replaces the ledger. A file that cannot be read, or holds no milestone, is fatal at launch | trace | env |
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
| `MEMVU_STOREVIS=1` | **store-visibility accounting.** Classifies every CPU store as DEVICE (`$Cxxx` in banks `$00/$01/$E0/$E1`), SLOWSIDE (direct Mega II `$E0/$E1`), SHADOWED (the machine mirrored it per its own `$C035` configuration) or PRIVATE, and prints one summary line at spike end, followed by `device top:` — the five most-written I/O registers, named from `iigs_ioregs.hpp` — and `writers top:` — the five code granules that generated the most visible stores, symbolized when a symbol table is loaded, `(unnamed)` otherwise. A total never travels without its causes | memvu | env |
| `MEMVU_STOREVIS_BANKS=1` | add the per-bank `stores`/`visible` breakdown (one line per bank that saw traffic) | memvu | env |
| `MEMVU_LOADVIS=1` | **load-visibility accounting**, the read-side twin of STOREVIS: classifies every CPU read (instruction fetch, data, vector, phantom) as must-serve (the machine has to answer it), cacheable-slow or private, and prints `MEMVU LOADVIS: model=iigs-read-v1 loads= mustserve= cacheable_slow= private=` plus a `LOADVIS KIND:` line split by read kind, and — so the total names its causes — `MEMVU LOADVIS device top:` (the five most-read I/O registers, named) and `MEMVU LOADVIS readers top:` (the five code granules doing the most device and slow-side reads, named or `unnamed`). Live counterpart: `memvu load` | memvu | env |

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
subtracted). There is no other store path: the untapped `write_word` helper that wrote through
`mmu->write` directly was dead code and has been deleted, so a new 16-bit store has to go
through `bus_write` and is counted.

### Instruction-stream shape

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `MEMVU_OPMIX=1` | executed-opcode histogram, split by CPU mode context `(E, M-width, X-width)`, plus hand-derived group totals (branch / RMW / stack / call-return / block-move / mode-switch) and the raw per-opcode counts | memvu | env |
| `MEMVU_IREUSE=<lines>,<linesize>` | instruction-line reuse against a direct-mapped model, reporting hit / miss / **modemiss**, plus the workload's instruction footprint. Both parameters must be powers of two; a bad geometry is REFUSED, not rounded. `=1` takes the 512×64 default | memvu | env |
| `MEMVU_WORKSET=1` | direct-page access count (exact, at the two direct-page data funnels), how many distinct direct pages are live, the D-change count, and the stack pointer's observed range and page histogram | memvu | env |

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
| `MEMVU_BLAME[=<topN>]` | attributes every externally-visible store and machine-served read to the **opcode address** of the instruction responsible, and reports the worst code granules, each named, with the toolbox call in progress when its stores had one (`; in SoundTools.FFStartSound 98%`) — a shared helper is named by the call that used it, not by the entry point that precedes it | memvu | env |
| `MEMVU_SEAM=<ratio>,<slowmul>,<bufdepth>,<shadowreads>` | **a MODEL, not a counter** — cost of routing part of a workload's memory traffic to the machine. Parameters are mandatory; an incomplete set is REFUSED. Output is an upper bound. Each modeled stall is charged as it happens to the 256-byte address page accessed and to the code granule accessing it, and the report names both: `MEMVU SEAM stall pages top:` and `MEMVU SEAM stall code top:`, each with modeled stall cycles and its share of all stalls | memvu | env |

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
| `A2GSPU_WATCH_IPC_ZP=<hexZP>` | generic interpreter-IP attribution: makes the NDJSON `ipc` field read a 2-byte LE ZP pointer (observation-free) — attributes a store to the interpreter's segment. Default-off (`ipc`=0). For UCSD p-code (Wizardry) set `9E`. A value that is not a hex zero-page address (`00`-`FF`) is refused (fatal at launch); it used to parse whatever `strtol` made of it | break | env |
| `A2GSPU_VALTRAP="<hexval>[:<w>]"` | value-provenance store trap: bind the PC that stores VALUE into memory (LE reassembled across consecutive bytes; `w`=1–4, default 3). "Who wrote value V" | break | env |
| `A2GSPU_VALTRAP_MAX=N` | VALTRAP hit cap (default 64) | break | env |
| `A2GSPU_PCTRAP="BANK:LO-HI"` | one-shot dump of registers + recent-PC ring on first entry to a PC range | break | env |
| `A2GSPU_TRAPDUMP="BASE:LEN"` | extra memory region (hex) dumped by PCTRAP (default 48 bytes) | break | env |
| `A2GSPU_TRAP="<spec>[;<spec>]"` | **Traps from configuration.** Each spec: a trigger — `at=PC[-PC]` (`HHHH` any bank, `BB:HHHH` one bank), `store=ADDR[-ADDR]` (a byte written into the range; the record names the storing instruction and the byte), `read=ADDR[-ADDR]` (a byte read from it as operand data), or `sp=LO-HI` (S entering the range) — optional `exit=PC` pairing for `at=`, `when=` conditions over registers and memory (`z16(30)`, `m8(s16(1)+1)`, `x==1`, `z8(2d)~{4,5,9,a,b,c}`), a policy (`once` default, `every`, `first=N[/key]`), payloads `do=`/`heavy=`/`xdo=`/`xheavy=` from `regs ring zp stack mem=<expr>:<len> field=<name>:<expr> text scan=…`, `cap=N`, `out=<file>`. NDJSON with frame and cycle stamps. Malformed spec = fatal at arm; every spec echoed at arm and summarised at close (`records=0` is printed, never implied). Also armed **live** with the `trap` verb. The general form of PCTRAP and of the three wiz5 traps — see [`A2GSPU_TRAP.md`](A2GSPU_TRAP.md) | break | env |
| `A2GSPU_TRAP_FILE=<file>` | specs one per line, `#` comments; combines with `A2GSPU_TRAP` | break | env |
| `A2GSPU_TRAP_OUT=<file>` | default sink for specs without `out=`; else `A2GSPU_RECORDS`; otherwise records go to stdout as `IIGS TRAP: {…}` | break | env |
| `A2GSPU_CONDTRAP="BANK:PC@f=v"` | conditional flag-provenance trap: at PC, when flag `f`(c/z/i/d/x/m/v/n)==v, report the PC that last changed that flag | break | env |
| `A2GSPU_CONDTRAP_MAX=N` | cap on CONDTRAP hits reported (default 8) | break | env |
| `A2GSPU_STACKTRAP="LO-HI"` | one-shot dump when S first enters the range | break | env |
| `A2GSPU_STACKWATCH=1\|"LO-HI"` | continuous stack-pointer tripwire for garbage-S crashes (`1`=imbalance only; `LO-HI`=over/underflow window) | break | env |
| `A2GSPU_STACKWATCH_JUMP=<hex>` | imbalance threshold (single-step S delta that trips STACKWATCH) | break | env |
| `A2GSPU_SAVE_AT=<hexPC>[@<file>]` | IIgs-only; snapshot I/O failure exits 2. Snapshot + halt at the first hit of an arbitrary 24-bit PC (a breakpoint that also checkpoints) | break/snapshot | env |
| `A2GSPU_RETGUARD=1\|strict` | return-target guard: flag RTS/RTL that returns into `$00/$01` (or, `strict`, also a symbol-gap) | break | env |

## Manipulate — the agent's hands (deliberate state injection)

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `A2GSPU_POKE="<hexPC>:<act>[;…]"` | one-shot deliberate splice at a PC. Actions: `A/X/Y/S/D/P/DBR/PB=<hex>` (reg/flag), `PC=<hex>` (force-branch), `M<hex24>=<hexbyte>` (poke mem), `RTS`/`RTL` (force-return), `SKIP=<n>` (skip bytes) | manipulate | env |
| `A2GSPU_POKE_NTH=<n>` | fire the POKE on the Nth hit of the PC (default 1) | manipulate | env |
| `A2GSPU_TAP=<file>` | generic title-agnostic call tap (`src/generic_tap.hpp`, from the harvested wiz5 taps): the NDJSON file a `tap.hit` record is written to on each hit of an `A2GSPU_TAP_PCS` address. The PCs are **not** this rail's value (this row said `A2GSPU_TAP="PC[,PC…]"` until 2026-09-16); without `A2GSPU_TAP_PCS` the tap never fires and says so | tap | env |
| `A2GSPU_TAP_SESSION=<dir>` | capture session into `<dir>`: `zp_stream.ndjson`; every `DUMP_EVERY` s `ram_dump_periodic_tNNN.bin` (`$0400-BFFF`), `hgr_dump_periodic_tNNN.bin` (`$2000-5FFF`, plus the soft-switch `.bin.meta`) and `ram_full_periodic_tNNN.bin`; `text_pages.ndjson`; `ram_full_<tag>.bin` on `DUMP_ON`. A **full** dump on a IIe or IIc is the MMU's 128K RAM array as stored — main `$0000-$BFFF`, the language card's bank-1 `$D000` page at `$C000` and bank-2 at `$D000`, `$E000-$FFFF`, and aux the same at `+$10000` — whichever soft switches are showing which part; elsewhere `$0000-$BFFF` as the CPU sees it (it was 48K on every machine). Every dump has an M6 `.meta.json` sidecar (`artifacts.py raw`). `keypress_events.ndjson`: a `tap.key` record (`seq ascii chr ts_ms ts_wall text_page_pre`) for every key that reaches the keyboard latch — host, paste buffer, `KEYSCHED`, CTRL `keys`; the II family's latch and the IIgs ADB micro's — with `keypress_NNNN_pre_hgr.bin` taken then and `keypress_NNNN_post_hgr.bin` plus a `tap.keypost` record (`text_page_post`) `KEY_POST_FRAMES` later: the per-key picture, as memory rather than a host rendering. `disk_writes.ndjson`: a `tap.diskwr` record (`drive qtrack track ts_ms`) per drive and quarter-track per emulated second in which a Disk II lays bits on a mounted 5.25 disk. Both event files were created empty on every session until 2026-09-16: their writers had no callers | tap | env |
| `A2GSPU_TAP_KEY_POST_FRAMES=<n>` | frames between a key reaching the latch and its post capture (default 30) | tap | env |
| `A2GSPU_TAP_PTRS`, `A2GSPU_TAP_PCS`, `A2GSPU_TAP_MAX`, `A2GSPU_TAP_DUMP_ON`, `A2GSPU_TAP_DUMP_EVERY`, `A2GSPU_TAP_ZP_EVERY`, `A2GSPU_TAP_CYC_PER_MS`, `A2GSPU_TAP_CYC_PER_SEC` | tap tuning: extra ZP-pointer register fields, which PCs, hit cap, dump-on-op / dump cadence, ZP-stream cadence, cycle-rate calibration. (Spelled in full. They were written here under short names, which is invisible to a reader — and to a checker — grepping for the name they must actually set.) Every tap rail is live-settable: `rail set A2GSPU_TAP_PCS FDED:COUT`, then `rail set A2GSPU_TAP <file>`, arms a call tap mid-session, and `rail set A2GSPU_TAP_SESSION <dir>` opens a capture session into an existing directory; `unset` closes either (the tap prints its event count). Setting the PC list re-applies `DUMP_ON` | tap | env |
| `A2GSPU_FORCE_KEY=<hex-ascii>` | force the ADB `$C000` keyboard latch to return this key (drives a `$C000`-polling guest headlessly) | manipulate | env |
| `A2GSPU_KEYSCHED=<file>` | a **schedule** of keystrokes read from a file, injected entry by entry. Lines: `<frame> <keys>` (with `{RETURN}` `{ESC}` `{SPACE}`), `<frame> PRESS <hex2> [mods]`, `WAIT <substring>` (the 40-column text page, polled 4x a second), or `WHEN <cond>[,<cond>]` — a wait written as a **machine condition** in the `A2GSPU_TRAP` grammar (`WHEN z8(24)==0`, `WHEN pc>=6000,m8(E1C000)>7f`), checked every frame through the probe, all must hold. A wait entry gates the entries after it. A malformed `WHEN` is fatal at arm (exit 2), like a missing file. At exit the schedule states how far it got — `all N entries reached`, or `K of N entries reached; stopped at entry K+1: WHEN '<cond>' never held` (or the `WAIT` text, or the frame that never came) — where a stalled schedule used to leave no trace. Distinct from `SPIKE_KEYS`, which is one literal string injected at one frame. Canonical name for `WIZ5_KEYS` | manipulate | env |
| `A2GSPU_KEYSCHED_DELAY=N` | frames between scheduled keystrokes. Canonical name for `WIZ5_KEY_DELAY` | manipulate | env |
| `A2GSPU_WRITEBACK=1` | write in-memory (possibly modified) disks back to their **host files** at spike end. OFF by default under mutation-guard discipline — use only on writable scratch images. Canonical name for `WIZ5_WRITEBACK`. `rail read` returns the shared typed `a2writeback-policy-v1` predicate | manipulate | env; read |
| `A2GSPU_NO_WRITEBACK=1` | suppress write-back even when `WRITEBACK` is set. Honoured across **both** spellings, so the guard cannot be bypassed by setting the other name. Canonical for `WIZ5_NO_WRITEBACK`. `rail read` returns requested/suppressed/flush-at-spike-end plus canonical/alias presence flags; this predicate does not govern explicit save/unmount, BRAM or arbitrary host endpoints. These flags test **presence**, including a value of `0`; unset them to remove them | manipulate | env; read |

*(Live/interactive manipulation is the CTRL rail's `keys`/`press`/`holdkey`/`mount`/`save`/`restore` verbs, below.)*

## Snapshot — save/restore machine state

`diff <snapshotA> <snapshotB> <report.json>` compares saved files without
restoring either or advancing the machine. Quote paths containing spaces;
Windows backslashes remain literal. Available through CTRL and co-pilot, and
through the standalone `snapdiff` executable in the build directory.

The `a2snapdiff-v1` JSON document separates named CPU register changes from
MMU registers, physical RAM, page maps, and opaque versioned device payloads.
Memory/device ranges use half-open **section offsets**, not CPU-visible
addresses. Each range includes up to 16 before/after bytes as hex; at most
128 ranges per section are emitted, with exact byte/range totals and an
explicit truncation flag. `section_changed_bytes` counts section bytes; CPU
register changes are reported separately. Device block presence and version
changes are reported even if payload bytes match.

`equal` means the serialized saved-state fields match. Manifests are compared
separately as metadata; their differences do not change `equal`. This is not
proof of equality for devices or host state absent from the snapshots. Device
payloads are compared as bytes, without claiming semantic decoding of their
private formats. The JSON includes both manifest texts and these scope limits.
CPU field names describe the saved register container: processor type is not
serialized, so a field's presence does not imply that register exists on the
guest's physical CPU (for example DB on a 65C02).

The reader supports current little-endian GSPS v1 IIe and IIgs layouts, including
structurally valid pre-manifest files. It follows lengths and page tables rather
than scanning guest RAM for marker strings. Malformed/truncated files, duplicate
device blocks, invalid page references, unsupported/ambiguous layouts, trailing
garbage, incompatible RAM/ROM geometries and files over 256 MiB are refused.
The report must not alias either input. No snapshot format or restore behavior
is changed by this reader.
Validation checks structure and geometry; the existing snapshot format has no
end-to-end payload checksum.

The CTRL ack reports comparison completion as `status=OK ... equal=0|1`, or
`status=FAIL` for invalid input/output. CLI usage is
`snapdiff A.snap B.snap [report.json] [--expect-different]`: exit 0 for equal,
1 for different, 2 for invalid input/output. `--expect-different` reverses the
first two outcomes so an identical pair fails a divergence expectation.

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `A2GSPU_SNAP_SAVE=<file>` | IIgs SPIKE only (`save` supports both IIe/IIgs); output failure exits 2. At spike end, save CPU regs + full MMU snapshot (RAM + page tables + soft-switch state) + device blocks + trailing sentinel + a **manifest** naming what the file holds, what it omits, the frame, the pins and the binary. **Do not read the lists from here — read them from the file.** They vary by platform and by what the machine had when the save ran (`tools/a2rail/artifacts.py snap <file>` prints the trailer; the `restore` ack carries the same text) | snapshot | env |
| `A2GSPU_SNAP_LOAD=<file>` | restore that snapshot at start and skip the ~1200-frame boot (the ADB micro, the Ensoniq, the interrupt registers, the disk controller and the block device's RAM-disk overlay restore from the snapshot when it holds them; floppy media, scanner and clock self-heal). Prints the manifest, or that it is absent and what Class A means. `SPIKE_KEYS` into a restored machine warns **only when the snapshot does not hold the ADB micro** — a pre-device-block file, or a platform with no ADB. From such a machine a restored desktop never polls `$C000` again (measured); from one that holds the ADB it does, and there is no warning | snapshot | env |
| `A2GSPU_RESTORE=<file>` | alias for `SNAP_LOAD` (load-at-start) | snapshot | env |

*(File-format tokens `A2GSPU_SNAP_MAGIC/_VER/_END/_FREAD` are snapshot on-disk constants, not env rails.)*

**The manifest, and why this file no longer quotes one.** It used to, and the
quote was wrong on *both* platforms. The generator is
`a2gspu_snap_manifest_text()` in `a2gspu_snapshot.cpp`; its fields are:

| Field | Meaning |
|-------|---------|
| `class=` | `A` cpu+mmu+softswitches+RAM · `B` adds video/scanner state · `C` adds the ADB micro, the Ensoniq and the IWM (`INSTRUMENTATION_SHAPE.md` M5's roadmap). **Derived from the blocks actually written**, and claimed only when the definition is met — a save holding a device the roadmap never named (a IIe holding only `pdblock`) stays `class=A` and lets `devices=` carry it, because inventing a `B` for it would be a second false claim |
| `devices=` | the device blocks in the file, comma-separated, or `-` |
| `included=` / `excluded=` | the full state vocabulary split in two. **This pair is the contract**; the letter is a summary of it |
| `frame= seed= faketime= ramdisk= bram= build= exe=` | the pins and the binary identity, so a restored run is traceable to what produced the file |

Read it from the running process, not from here: the `restore` ack carries the
manifest text verbatim after its `skipped=` field, and
`python tools/a2rail/artifacts.py snap <file>` reads the `GSPM` trailer straight
off disk. A worked IIgs example — not a contract, a sample — is
`class=C devices=adb,ensoniq,vgcint,scanner,iwm,media,pdblock` with `rtc`, `serial`
and `clock` still in `excluded=`; a IIe with a Disk II in slot 6 saves
`class=B devices=scanner,diskii,media,pdblock`.

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
| `A2GSPU_ASSERT="name OP val[;…]"` | `;`/`,`-separated `name OP value` checks (`OP` ∈ `== != <= >= < >`); the spike exits non-zero if any fails. Fields: `scb_mode`, `qd_carry/qd_err`, `lt_carry`, `cs_carry/pr_carry`, `gsos_err`, `nonzero/distinct`, `idxN` (N=0..15), and `peek:HHHHHH` (observation-free 24-bit byte read). **Values are decimal** unless prefixed `$` or `0x` (`0d` forces decimal; a bare *leading zero* makes it octal — `== 010` is 8), and a `peek:` comparand **must carry an explicit base**: a bare hex-shaped literal such as `peek:7E0100 == 22` is refused with `write $22 or 0d22` rather than guessed, because `== 34`, `== 0x22` and `== $22` all used to print the identical result line. Every result line now shows both bases: `peek:7E0100 == $22 (34)  (actual=$22 (34)) -> PASS`. **A spec that evaluates nothing is not a pass:** a clause with no comparison operator (the single-`=` typo `qd_carry=0`) is counted, warned and failed, a field that cannot be evaluated fails as `unevaluatable`, and a non-empty spec that yields 0 checks fails. A spec set to `""` or whitespace arms no gate at all (`gate=none`), so a harness that exports the variable unconditionally-empty stays green | assert | env |
| `A2GSPU_STRICT=1` | exit 3 at spike end if the run printed any `WARNING` line (see `GSDIAG: warnings=N` below). Missing inputs and unopenable sinks are already fatal (exit 2) without it | assert | env |
| `A2GSPU_EXPECT_HASH=<hex16>` | the SHR-window (IIgs) or HGR-page (II-family) hash the run must end on; prints `MATCH`/`MISMATCH` with both values and gates the exit like `GOLDEN`, without a file. The way a harness pins a row, and the way a differencing harness proves the shorter run reproduced before subtracting | assert | env |
| `A2GSPU_EXPECT_RUN=<hex16>` | the **run hash** the run must end on: `GSDIAG`'s `run=`, house FNV-1a-64 over four little-endian 64-bit values — instructions executed, CPU stores (RMW phantom writes excluded), the screen hash and cycles. Two runs can share a screen and a clock and still have done different work; this gate refuses that. Prints `MATCH`/`MISMATCH` with `insns=` and `stores=`; a mismatch, or a value that is not hex, fails the gate. Every headless run prints `insns= stores= run=` on `GSDIAG` whether or not this is set | assert | env |
| `A2GSPU_GOLDEN=<file>` | hash the SHR window (`$E1:2000-9FFF`, house FNV-1a-64); if the file exists compare (DIFF ⇒ gate fail; corrupt/empty file ⇒ hard fail); if absent, bless it — and a path that can be neither opened nor created is a **hard gate failure**, not the silent nothing it used to be (no `GOLDEN` line at all, `gate=PASS`, exit 0). **Blessing is its own verdict:** a bless compared nothing, so it reports `gate=BLESSED` (not `PASS`) and raises a `WARNING`, which means `A2GSPU_STRICT=1` refuses to call a bless run OK. `rc` and the exit code stay `0`. Both the IIgs SHR and the II-family HGR gate now go through the one implementation, `a2dhgr::golden_gate_file` | assert | env |

*Always-on:* at spike end `iigs_emit_status` prints one machine-readable line —
`GSDIAG: status=<OK\|GATE_FAIL\|CRASH_BRK\|HANG\|GSOS_ERROR\|STALLED> rc=N gate=<PASS\|FAIL\|BLESSED\|none>
gsos_err=$XXXX brk=N scb=NNN hash=................ warnings=N cycles=N`. The **process exit code** stays
golden/assert-gate-driven (`0`/`1`) for harness compatibility; `status` is the richer category.
`gate=BLESSED` is the **third outcome** — a golden that did not exist was written, so nothing was
compared. `PASS` and `BLESSED` both exit `0`, and a consumer grepping the literal `gate=PASS` will
(deliberately) stop matching bless runs; the same word appears in `=== SPIKE COMPLETE (gate …) ===`.
`warnings=N` counts every `WARNING` line the run printed — a skipped injection, a rail that could
not do what it was asked — and `A2GSPU_STRICT=1` turns a non-zero count into exit code 3. A missing
input file (a disk image under `-n`, `RUN_BIN`, `SNAP_LOAD`, `KEYSCHED`, `MILESTONES`, `TRAP_FILE`) or
an output sink that cannot be opened (`WATCH_OUT`, `CALLSTREAM`, `ITRACE_OUT`, `TRAP_OUT`, the WIZ5
sinks) is not a warning: it aborts at arm with exit code 2, because a run that measured something
other than what it was asked to is worse than no run.

### One evaluator, two surfaces

`A2GSPU_ASSERT` (env, spike) and the CTRL `assert` verb (live) are the **same** rail and now
share the same evaluator primitives (`src/a2gspu_assert.hpp` -> `src/iigs_diag.hpp`). They
used to be two programs behind one name, and against the same binary they disagreed on the
grammar (CTRL took `==` only), the field set (CTRL took `peek:` only, so `assert scb_mode==0`
answered `parse-fail` on the surface this table advertises it for), whether a handler-backed
address may be asserted against at all (CTRL asserted against the floating bus and reported
`PASS`), how a value parses (`==169` silently became `$69`), and what base the observed value
printed in (`got=A9` against `actual=169` for the same byte).

**Values.** `$HH` and `0xHH` are hex, `0dNN` is decimal. A bare literal is decimal *except*
that a `peek:` comparand which is ambiguous (more than one digit, every digit a hex digit) is
**refused**, not guessed: write `$22` or `0d22`. A `peek:` address is ONE 24-bit hex number,
`peek:HHHHHH` -- the live rail refuses anything else rather than keeping the prefix that
parsed and asserting against the wrong byte.

**Three outcomes, not two.** A check that ran and was false is `fail`. A check the rail could
not perform at all -- an unknown field, a clause with no operator, an unparsable or ambiguous
value, an address that is not `probe_readable`, an SHR field on a machine with no Mega II
bank-`$E1` image, a toolbox call this run never recorded -- is `unevaluated` and is reported
separately: the CTRL ack says `status=FAIL assert-unevaluated n=N pass=P fail=F`, and the
spike prints the reason plus a `WARNING`. **Unevaluated still fails the gate.** A spec that
produces zero checks fails too, on both surfaces: an assert that evaluates nothing is not a
pass.

`tools/a2rail/gs816/assertparity.ps1` feeds one expression list to both surfaces and fails if
the verdicts differ. It is the only check that can see this class of defect, because each
surface on its own looked entirely self-consistent.

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
| `WIZ5_WRITEBACK` | legacy alias for `A2GSPU_WRITEBACK`; `rail read` returns the same shared writeback policy | alias | env; read |
| `WIZ5_NO_WRITEBACK` | legacy alias for `A2GSPU_NO_WRITEBACK`; the suppression is honoured under **either** spelling; `rail read` returns the same shared policy | alias | env; read |

## Device / determinism — reproducible oracle knobs

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `A2GSPU_SEED=<n>` | seed the cold-boot SP entropy (`srand`) so a from-scratch spike is byte-reproducible (warm restores overwrite SP anyway) | determinism | env |
| `A2GSPU_BRAM=default\|<file>` | pin the **battery RAM** a run starts from. Unset, an automated run (`A2GSPU_SPIKE_FRAMES` or `A2GSPU_CTRL`) reads `./gs2-bram.bin` from the working directory when present and **writes it back at exit**; a windowed run uses upstream's per-machine file in the preferences folder (`bram/<machine-uuid>.bin`), which would otherwise make every unpinned harness run on the workstation share one warm BRAM. Either way a run's starting BRAM depends on where it ran and on every run before it there — measured: one pinned 300-frame IIgs boot hashed `5F78F7C15131F43F` from a fresh directory and `89FCD3DA7D947DFF` beside a rewritten BRAM file. `default` starts from the built-in contents and touches no file; `<file>` loads 256 bytes (missing or short is fatal, exit 2) and never writes back. `A2GSPU PINS:` and the snapshot manifest carry `bram=<cwd\|cwd-absent\|prefs\|prefs-absent\|default\|file>/<house-FNV>` (or `none` on a machine with no RTC) | determinism | env |
| `A2GSPU_FAKETIME=<epoch-s>` | freeze every clock the guest can read at that instant — the IIgs RTC, the ProDOS clock card and the ThunderClock Plus, with no host timezone applied — and pin the VGC one-second interrupt's phase (first edge one second after power-on), so clock-derived state is run-to-run stable. A guest's write to the RTC seconds sticks, kept as an offset from the pin (it was overwritten on the next command, so ROM WriteTime could not verify and Control Panel could not set the time) | determinism | env |
| `A2GSPU_DISK_TIMING=1` | **IIgs disk timing as the hardware has it**, three parts that only work together. (1) The speed register's **disk motor-on detect** [HW Ref Table 2-2]: `$C036` bits 0-3 name slots 4-7, and while a named slot's Disk II motor is on (its `$C0n9` was accessed after its `$C0n8`) the machine runs at 1 MHz, as it does when bit 7 is clear. Slot 6 counts only while `$C031` EN35 selects the 5.25 port (unverified: whether the FPI gates the detect on EN35). (2) The IWM's **5.25 drives in real time**: they count 1 MHz video cycles, not CPU cycles, so a bit cell is 4 µs at any CPU speed (it was 1.4 µs at 2.8 MHz, which made the ROM's SmartPort probe underrun; the IWM does not know the CPU's speed). Without (1), 1 MHz disk code writing a real-time drive at 2.8 MHz fails, as it does on a IIgs with the detect off. (3) The IWM's **write cells start with the write state**: the chip clocks them itself from L7's entry, so the first boundary is a whole cell away and the byte written with the entry is always in time. The drives' cells follow the spindle, and `STA $C08F,X` touches the address a cycle before the write (the 65816's abs,X extra cycle), so without (3) a boundary in that cycle underran the ROM's probe at its first byte. Unset keeps the timing the pinned hashes were recorded against; set, the PINS line says `disktiming=1`. `devstate mmugs` shows the motors and the speed | determinism | env |
| `A2GSPU_FAKETIME_TICK=1` | with `A2GSPU_FAKETIME`, the pin becomes an **origin**: the clocks show the pinned instant at emulated time zero plus one second per 14,318,180 emulated 14M ticks — never host time, so the run still repeats exactly — and the VGC one-second interrupt's edges fall on the same whole seconds, so the RTC and the interrupt step together. A frozen clock is one a real machine never shows for long (GS/OS's clock display stands still; code waiting for the seconds to change waits forever). Unset or `0` keeps the frozen pin, which consumers' pinned hashes rely on. The run's identity says which: `faketime=<value>/tick` on the PINS line, in a snapshot's manifest and in `rail read A2GSPU_FAKETIME_TICK` (a frozen pin prints the value alone, as before) | determinism | env |
| `A2GSPU_RAMDISK=1` | serve a mounted image from a copy-on-write RAM buffer; writes never touch the host file | device | env |
| `A2GSPU_SP_NDEV=<n>` | override the SmartPort device count (A/B faithfulness; `32` reproduces pre-fix enumeration) | device | env |
| `A2GSPU_CARD_MODEL=card\|legacy` | which A2GSPU card a configuration with one gets. **`card`** (the default): the card watches the slot bus and never drives the data lines, so every read of its I/O block floats and there is no ident byte — software finds the card from its own configuration; the one host-writable address is the command register `$C0nF` (see `A2GSPU_CARD_CMDLOG`). **`legacy`**: the earlier register protocol at `$C0n0-$C0nF` (a status read and the `$A2` ident at `$C0nF`), its local UHR/TERM renderer and its video-state serial payload, kept for software written against it. Any other value is fatal (exit 2). Until 2026-09-16 the legacy model was the only one | device | env |
| `A2GSPU_SERIAL_PORT=<COMx>` | the physical card's stream port. **Serial hardware is opt-in:** nothing is opened unless this or `A2GSPU_BUS_STREAM` is set (the stream alone tries `COM10 COM9 COM7 COM6 COM8 COM5`). Any run of a configuration with the card used to open a port and write into it — gates included — locking a separate live session out | device | env |
| `A2GSPU_SERIAL_TELEMETRY=<COMx>` | the physical card's output-only telemetry port: read without waiting every frame and appended raw to `A2GSPU_SERIAL_TELEMETRY_OUT` (default `<A2GSPU_OUT_DIR>/card_telemetry.bin`, else `./card_telemetry.bin`); the emulator does not parse it. `devstate A2GSPU` shows the byte count; a lost port is retried about once a second | device | env |
| `A2GSPU_SERIAL_TELEMETRY_OUT=<file>` | the file the telemetry capture appends to | device | env |
| `A2GSPU_SERIAL_DEBUG=1` | verbose real-card serial/reconnect logging | device | env |
| `A2GSPU_OUT_DIR=<dir>` | directory prefix applied to rail output filenames. An absolute path (or a Windows drive letter) is honoured verbatim, so a path a caller chose explicitly is never rewritten | meta | env |

## CTRL rail — the interactive `cmd.N`→`ack.N` protocol

`ctl.ps1 -Report <file.ndjson>` (or `A2RAIL_REPORT`) records every issued,
acknowledged, refused, or stalled transaction as `a2rail-event-v1`, including
sequence, session PID/owner, exact command/reply, timestamp, and host latency.
This is the forensic transcript for long agent runs; `cmd.N`/`ack.N` remain the
protocol authority.

Use a fresh owned session directory and strictly increasing sequences. ACKs
are written, flushed and closed in `ack.N.tmp`, then published by checked
rename. An existing `ack.N` is refused and preserved. Publication failure
prints `status=FAIL ack-publication sequence=N effects=may-have-occurred`
and exits 2; the command may already have run. Inspect process exit, logs and
artifacts before any retry. A completed temporary ACK can remain after failed
rename. Successful QUIT acknowledgement still requires a successful process
exit to establish output close-out. This is not power-loss durability or a
cross-process no-clobber promise. Evidence and covered exports:
[`gssquared-rail-close-20261016`](reports/gssquared-rail-close-20261016/README.md).

| Rail | Effect | Group | Gated |
|------|--------|-------|-------|
| `A2GSPU_AGENT=0\|1` | default off. With 1, require `--debug <endpoint>` and an explicit machine (`-p`, `-c` or config file); reuse the full interactive CTRL instrumentation bootstrap and start paused, without entering its file loop. Suppresses host preferences/input; no fixed-frame spike/verdict loop. Cannot combine CTRL, COPILOT or SPIKE_FRAMES. Invalid values/configurations fail before starting a machine. `rail read A2GSPU_AGENT` returns native `a2agent-mode-v1` enabled/bootstrap-complete proof. Managed MCP launch sets this rail and checks that proof before serving tools | ctrl | env |
| `A2GSPU_CTRL=<dir>` | enable the interactive stepping rail: 20 Hz poll of `<dir>/cmd.N`, atomic `<dir>/ack.N` reply; replaces the fixed-frame spike. Also forces headless when set | ctrl | env |
| `A2GSPU_CTRL_TIMEOUT=<s>` | idle timeout in seconds before an orphaned session quits (default 300) | ctrl | env |
| `A2GSPU_CTRL_ATOMIC=1` | the client declares it writes `cmd.N` atomically (a temp file, then rename). The rail then skips the 20 ms re-read guard it keeps for clients that write in place, which could otherwise be seen mid-write. `tools/a2rail/session.ps1` sets it; `protocol` reports `cmd_guard=`. With the poll hot for a second after each ack, the emulator's side of a command is its execution time plus about a millisecond | ctrl | env |
| `A2GSPU_BACKGROUND_WINDOW=1` | keep the window out of the way: created not-focusable, not activated when shown or raised, pushed to the back of the z-order. `A2GSPU_CTRL` implies this; this rail gives it to an automated **windowed** run without the CTRL rail — the co-pilot gate, whose window otherwise took focus mid-suite. Interactive use never sets it | ctrl | env |
| `A2GSPU_COPILOT=<dir>` | windowed agent rail: human input stays live. Observe verbs, plus `pause`, `resume`, `key`, `press`, `reset`, `mount` and `quit`, and **observation armed live**: `rail` (discovery, readback, and `set`/`unset` of any rail that does not halt the machine) and `trap`. It **refuses** memory and register splices (`poke`, `load`, `setreg`) rather than racing the human, and a rail that halts (`rail set A2GSPU_STOP_ON_FAULT` answers `copilot-rail-not-windowed`), because a halt ends a windowed session. Records a windowed session writes carry the executed-frame count (the frame clock used to advance only under the spike and CTRL `run`). The `copilot` verb states the contract | ctrl | env |

**Verbs** (one per `cmd.N`; reply text lands in `ack.N`), verified in `a2gspu_ctrl_loop`:

| Verb | Effect | Group |
|------|--------|-------|
| `context overview` / `topology [offset]` / `device <id>` / `address <hex>` / `irq` / `stop` / `pixel <x> <y>` / `schedule [offset]` / `on [capacity]` / `off` / `history [after-id] [count]` / `mark [hex-address decimal-length]` / `changes <mark-id> [offset]` / `snapshot <file>` | `a2context-v1`: one machine-thread observation stamp with native CPU/14M/video clocks; connected compiled/installed/absent inventory and source references; device fields, mapping/IRQ/stop/reference-pixel explanations; timer queues in their own domains; bounded causal history and sampled changes. Reads work over the socket while running without implicit pause. `on`, `off`, and `mark` change observer state and require socket pause; snapshot writes a host file. Capacity 1..4096, event pages 1..8 with byte caps, topology/change pages eight; four retained marks, optional 1..256-byte CPU memory window. Loss, omitted payloads, capture host cost and incomplete models are explicit. Full snapshot captures devices, signals and core graph at one boundary; this is not complete machine replay. Paths may be quoted | CTRL-verb (mixed) |
| `run <frames>` | step N frames; reply `status=OK ran=N cycles=N (in-command) PC $bb:aaaa->$bb:aaaa`, **both program counters banked** -- they are 24-bit and printing them masked to 16 bits made a cold IIgs boot (really $00:FA62 -> $FF:FCDE) ack as `PC $FA62->$FCDE`, two addresses in a bank the ack never named; the format now matches `cpu`'s `PC=BB:AAAA` so one parser reads both, or `status=HALT ran=N …` if the guest halted inside the call. **Match this as a PREFIX, not a whole line:** `arm_cpu()` appends ` [cleared RESET]`, ` [cleared STOP]` or ` [cleared RESET+STOP]` when it had to un-assert the CPU before advancing, and the first `run` of a session is exactly the case that does -- so the most common ack an agent will ever meet carries the suffix. (It never replies `ok`, and never `halted@<i>` — that belongs to `press`/`key`, which report which injected key was consumed) | CTRL-verb (run) |
| `keys <str>` | append to the keyboard paste buffer (paced by guest polls) | CTRL-verb (manipulate) |
| `press <hex2> [hold] [mods]` | full physical keypress — latch + any-key-down, held `hold` frames (default 15), then released; IIe latch path AND IIgs KeyGloo+IRQ path. `mods` is letters `o` (Open-Apple) `c` (Closed-Apple) `s` (shift) `k` (control), carried in the key's modifier byte (what `$C025` and the Event Manager report): `press 6F 2 o` is Open-Apple-O, which is how the Finder is told to **open** a selected icon — a bare Return on one starts a rename (measured). `o` and `c` also hold SW0 and SW1 (`$C061`/`$C062`) for the press: the IIe's Open/Solid Apple keys, the IIgs's Command/Option through the ADB micro; the II and II+ have no Apple keys | CTRL-verb (manipulate) |
| `holdkey <hex-ascii>` | set the sticky IIgs hold-key WITHOUT running (`0` clears) — hold, step in chunks, watch for consume, release | CTRL-verb (manipulate) |
| `iolog` / `iolog on [n]` / `off` / `dump <file>` / `reset` | bare: cumulative keyboard soft-switch read counts (`C000/C010/C024/C025/C026`) — diff two calls across a `run` to see which switch a wedged menu polls. `on [n]` records every `$C0xx` access in a ring of `n`, `off` stops it (reporting retained and dropped counts), `dump <file>` writes the ring, `reset` clears it | CTRL-verb (observe) |
| `cpu` | 65816 state: `PC=BB:AAAA A X Y SP=16bit P E HALT STP RDY KBD AKD kbd_src=` — distinguishes waiting-for-key vs halted vs executing. **`kbd_src=` names where the keyboard byte came from** (`module` = MODULE_KEYBOARD, `adb-latch` = the IIgs ADB micro's observation-free `read_key_latch()`, `none` = no keyboard, and then `KBD=?? AKD=?`). It exists because the field used to fall back to `probe_peek($C000)` when MODULE_KEYBOARD was absent — which is every IIgs — so every IIgs ack reported the floating bus as a keystroke (`KBD=EE AKD=-1`, unchanged across millions of cycles) while `read C000 1` in the same session correctly refused the same address as handler-backed. No probed byte reaches this field now | CTRL-verb (observe) |
| `dis <addr> <n> <file> [m8\|m16] [x8\|x16]` | disassemble N instructions via the debugger `Disassembler` to `<file>`. The address takes the same `BB:AAAA` form as `load`/`read`/`poke`. **Reads are side-effect-free:** `$xxC000-$xxC0FF` is never touched **in any bank** (the old guard compared the full 24-bit address, so `dis E1C055 1 …` fell through to the CPU's read path and physically flipped `PAGE2` and `HIRES` — an observe verb actuating the machine), and everything else goes through `probe_readable`/`probe_peek` rather than `mmu->read`. A byte the MMU will not show is reported as `<unreadable>` instead of being decoded from the `$EE` placeholder into a fictional `INC $EEEE`, and the ack carries `unreadable=N`. **65816 immediate width:** `m`/`x` are seeded from the live CPU (override with the trailing tokens for code that has not run yet), `REP`/`SEP` are followed as the listing decodes them, and `XCE` resets to the conservative 8-bit reading; the first line of the output file and the ack's `mx=m8x16@start src=<cpu\|arg>` both name the assumption. Without this the listing desynchronised from the first 16-bit immediate: `A9 34 12` read as `LDA #$34` / `ORA ($8F)` while the CPU executed `LDA #$1234` | CTRL-verb (observe) |
| `text <file>` | dump the text page to `<file>` | CTRL-verb (observe) |
| `shot <file>` | update the display and save the SDL backbuffer to `<file>`; ack `status=OK shot profile=sdl-human <file>` — for a human to look at, never an oracle (use `pngc`/`shr`) | CTRL-verb (observe) |
| `diff <snapshotA> <snapshotB> <report.json>` | checked saved-state comparison, schema `a2snapdiff-v1`, model `serialized-saved-state`; reports registers, physical RAM/MMU/page-map and opaque device-byte changes; metadata separate; quote paths with spaces | CTRL-verb (observe) |
| `read <hexaddr> <len> <file>` | dump `len` bytes from `<addr>` via `probe_peek` (side-effect-free, bank-correct on `-p 5`) to `<file>` | CTRL-verb (observe) |
| `poke [BB:]<addr> <hexbytes…>` | deliberate splice. Bare `AAAA` writes through the MMU, so soft switches reach their handlers. `BB:AAAA` writes observation-free where the CPU will read it (IIe: the flat image; IIgs: `probe_poke`, resolved through aux and language-card state). A byte the MMU will not place — `$C0xx`, ROM, a write-inhibited language card, `$E0/$E1:C000-CFFF` — fails the ack: `status=FAIL poke-refused refused=N landed=M first=$BB:AAAA`, never `poked N byte(s)` | CTRL-verb (manipulate) |
| `mount sXdY <path>` | runtime media swap (save-and-unmount the drive first so writes persist) | CTRL-verb (manipulate) |
| `save <file>` | CPU regs + full MMU snapshot + the device blocks this machine has (IIgs: `adb`, `ensoniq`, `vgcint`, `scanner`, `iwm`, `media`, `pdblock`, `scc` -- the SCC's registers, FIFOs, shift registers, interrupt block, line inputs and the time left on characters in flight, not the host connections behind its ports -- `rtc` -- the clock chip's serial state, registers, write-protect and all 256 bytes of BRAM; a restore keeps the restoring session's clock (its pin with the saved guest offset, or the host's) -- `motor`, the motor-on detect `A2GSPU_DISK_TIMING` reads -- and the card blocks (`a2gspu` for the A2GSPU card model: its command assembler mid-frame, sequence tracking, forwarding budget and counters; `mb` for each Mockingboard: both 6522s, registers, counters, latches and underflow bookkeeping, and each AY's address latch and registers; the AY sound generator's phases are host audio and are not saved); II family: `scanner`, `diskii` and `media` for each Disk II controller, and `pdblock` when present) + sentinel + manifest. `diskii` is each Disk II controller by slot: latch, motor and the time left on its off-timer, data register, sequencer and both heads. `media` is the disks behind the IWM's and every Disk II's heads: every track of a disk written since it was mounted, applied on restore only to the same image with the same track layout (else a counted warning). "The same image" is the content: each drive's block carries a hash of the file as mounted, which decides whatever the path, and the name decides only for a block written before the hash (`util/media_identity.hpp`), so a restored machine reads what the saved one wrote; the host file is never written. `scanner` (Class B) is the video scanner's own switches, colours, SCB and palette index plus the display's NEWVIDEO, so a restored machine draws in the saved mode from its first frame; scan position belongs to the clock, which is not saved, so the restoring session keeps its own. Take it at a quiescent point; the manifest says what it holds | CTRL-verb (snapshot) |
| `restore <file>` | restore a `save`d snapshot and force-run. The ack is `status=OK restore <file> manifest=present\|absent devices=<restored> skipped=<skipped> \| <the manifest text>` — there is no `class=` field of its own; the letter is inside the manifest text, which the ack now carries verbatim instead of leaving it only in the session log. **Transactional:** the current machine is checkpointed first, and a file that does not restore is rolled back — the ack is then `status=FAIL restore-fail stage=<open|cpu|mmu|devices|sentinel|rollback-save> rolled-back=<yes|n/a|FAILED(stage)>`, and `rolled-back=yes` means the machine is exactly as it was before the command. (It used to overwrite the registers, fail the MMU geometry check, and answer a bare `restore-fail` over a half-restored machine) | CTRL-verb (snapshot) |
| `quit` | ack and end the session | CTRL-verb (run) |
| `oracle` | north-star contracts (cycles/colour/snapshot/dual-mode) | CTRL-verb (meta) |
| `help` / `help <file>` | list CTRL verbs; optional full catalog to file | CTRL-verb (meta) |
| `manifest <file>` | machine-oriented capability dump (verbs+env groups+models) | CTRL-verb (meta) |
| `step [n]` | request N instructions (default 1), bounded by N instruction-boundary attempts; positive decimal through 2147483647 only, with no extra arguments. Invalid requests have zero effects. `cycles=` **in-command**; `stepped=` and `instructions=` count instructions actually executed, excluding idle/interrupt-only attempts and pre-instruction breakpoint stops. Native `completed`, `requested_instructions`, `attempt_budget`, `attempts`, `stop_reason` distinguish completion, HALT and BUDGET | CTRL-verb (run) |
| `run <frames>` input/completion | positive decimal 1..1000000 only, with no extra arguments; invalid requests report zero work before arming/clearing CPU state. Native `requested_frames`, `completed_frames`, `completed`, `stop_reason` supplement in-command cycles/instructions. Frame completion is distinct from executing a requested number of CPU instructions | CTRL-verb (run) |
| `run-until <addr> [max]` | test the current PC first; an already-reached target executes nothing and does not arm/clear CPU state. Positive decimal boundary-attempt budget, default 2000000 and maximum 2147483647; invalid/extra arguments refuse before effects. Native `target_reached`, `completed`, `target`, `attempt_budget`, `attempts`, `budget_model`, `stop_reason` distinguish target, halt and budget exhaustion. Idle/interrupt-only attempts consume the bound; `instr=`/`instructions=` count actual instructions and `cycles=` is **in-command** | CTRL-verb (run) |
| `vid` | softswitch decode TEXT…DHIRES | CTRL-verb (observe) |
| `stack [n]` | stack contents above SP | CTRL-verb (observe) |
| `png` / `pngc` | mono (`profile=bits-mono`) / named-colour (`profile=4dot`, `profile=mono`) DHGR from RAM | CTRL-verb (observe) |
| `vram` | 8K aux + 8K main raw, with `<file>.meta.json` (`vram-raw-v1`, page, hashes) for `artifacts.py raw` | CTRL-verb (observe) |
| `load` / `poke` / `setreg` | deliberate RAM/reg splice | CTRL-verb (manipulate) |
| `load <addr> <file>` | binary load with readback verification; quote filenames containing spaces. Malformed, missing or extra arguments fail before memory writes. MCP inputs can come from create-once `gs2_artifact_put` with a checked SHA256 | CTRL-verb (manipulate) |
| `cycles` / `cycles reset` | the absolute guest cycle counter. `cycles reset` sets a mark; a later `cycles` adds `delta_from_mark=N model=cross-command(every-command-since-mark)` — true, but it counts every command run since the mark, so it is a routine's cost only when nothing else ran. A routine's cost is the `cycles=` on `run`/`step`/`run-until` | CTRL-verb (observe) |
| `dhgr-calibrate` / `dhgr-export` | 4-dot self-test + profile files for a2tile | CTRL-verb (calibrate) |
| `dhgr-golden <file> [page] [vram-raw\|4dot] [bless]` | FNV page golden; `vram-raw`=16K AUX‖MAIN; `4dot`=discrete RGB 560×192 | CTRL-verb (calibrate) |
| `A2GSPU_DHGR_GOLDEN=<file>` | SPIKE-end IIe DHGR gate (bless if missing; MATCH/DIFF exit). A bless compared nothing, so it reports `gate=BLESSED` rather than `gate=PASS` and raises a `WARNING` that `A2GSPU_STRICT=1` turns into exit 3 | assert |
| `A2GSPU_DHGR_PAGE=1\|2` | which page for DHGR SPIKE golden (default 1) | assert |
| `A2GSPU_DHGR_GOLDEN_PROFILE=` | `vram-raw` (default) or `4dot` — must match how the golden file was blessed | assert |
| `assert peek:…==…[;…]` | live probe_peek checks; `status=PASS\|FAIL` | CTRL-verb (observe) |
| `watch add\|list\|sample\|clear` | soft watch: sample peeks across run/step | CTRL-verb (observe) |
| `watch bus add bank:lo-hi` | **LIVE** bus write-watch (same as A2GSPU_WATCH, mid-session) | CTRL-verb (break) |
| `watch bus list\|hits\|clear\|read\|change` | bus watch control | CTRL-verb (break) |
| `rail list` / `rail env-only [file]` | discover launch-only env rails (L4 gap, not a black box) | CTRL-verb (meta) |
| `rail find <NAME>` | one rail's family, synopsis, `live=<verb>` or `launch-only`, and `read=fn:…\|verb:…\|none` | CTRL-verb (meta) |
| `rail live` | the declared reachability table, checked: every row names a registered rail and an implemented verb, or the ack is `FAIL reach-table-inconsistent` | CTRL-verb (meta) |
| `rail set <NAME> <value>` / `rail unset <NAME>` | arm or disarm a rail **while the machine runs**, without a relaunch. The value is exactly what the environment variable would hold, and it goes to the **same apply function** the headless start calls with the environment's value (`src/a2gspu_rail_setters.cpp`), so the live form cannot mean something the launch form does not. A value that cannot be honoured returns FAIL (at launch it is still fatal, exit 2); closing/replacing an old sink can consume it before a later failure, so FAIL does not promise rollback; `unset` turns a flag off, returns a number to its default, or closes a sink. `rail live` marks these rails `live=rail`; a rail with another live verb names it (`rail-not-settable A2GSPU_WATCH -- its live equivalent is the verb watch`); a launch-only rail says so. Settable now (68): the break/trace reporters `ERRHOOK BRKDUMP STOP_ON_FAULT BRKMEM LCTRACE HANG_THRESHOLD SYM_SUSPECT CONDTRAP(_MAX) LOADTRACE(_MIN) PCTRAP TRAPDUMP STACKTRAP STACKWATCH(_JUMP) INTLOG TRACE_EXT MODETRACE TRACE_FROM CALLTRACE(_N,_SKIP) RETGUARD SYM_BASE POKE_NTH`, the `ITRACE_FRAME/_LO/_HI/_REARM/_OUT` window, the `SNAP_LO/_HI/_PCS/_MAX/_OUT/_ZP` logger, `WATCH_MAX/_OUT`, `VALTRAP_MAX`, `LIVENESS(_OUT,_STALL)`, `LOADMAP`, `MILESTONES`, `ROM_SYMBOLS`, `WATCH_IPC_ZP`, `KEYRAM` and `KEYSCHED_DELAY`, the whole generic tap — `TAP`, `TAP_PCS/_PTRS/_MAX/_DUMP_ON/_CYC_PER_MS/_CYC_PER_SEC` and `TAP_SESSION`, `TAP_DUMP_EVERY/_ZP_EVERY/_KEY_POST_FRAMES` (all `A2GSPU_`; the `WIZ5_TAP`, `WIZ5_SESSION`, `WIZ5_DUMP_EVERY` and `WIZ5_ZP_EVERY` aliases reach the same setters at launch, and the arm line says `(from WIZ5_…)`) — and every MEMVU measurement: `MEMVU_STOREVIS LOADVIS STOREVIS_BANKS OPMIX IREUSE WORKSET SEAM BLAME`. Arming a MEMVU rail zeroes the counters it shares (the ack says which), so a live arm starts a clean window; `memvu report <file>` reads the result. Unsetting `A2GSPU_LIVENESS` prints its close-out. `railparity.ps1` and `rail live` both fail a `live=rail` row with no registered apply function, and an apply function with no row | CTRL-verb (meta) |
| `rail read <NAME>` | the rail's state now: counters for `MEMVU_*`, per-spec hits/records for `A2GSPU_TRAP`, the liveness sample state, the warning count, the determinism pins and binary identity; `read-via-verb` when a CTRL verb owns the readback; `FAIL no-readback` when the rail speaks only at spike end | CTRL-verb (meta) |
| `rail schema [NAME]` | bare: the rails that write NDJSON records and their tags; with a name: `tag=` and `fields=` (`?` marks optional fields) and a note. `FAIL no-records` for a rail that writes none. The same table is the `record_schemas` section of `manifest <file>`, so a consumer validates against the binary, not the C++ | CTRL-verb (meta) |
| `trap` / `trap add <spec>` / `trap list` / `trap clear [<name>]` | `A2GSPU_TRAP` armed **live**: the same spec grammar (`at=`, `store=`, `read=`, `sp=` triggers; `when=`, policies, payloads, `out=`). A malformed spec fails the ack and arms nothing — it no longer has to be fatal, because a typo must not end the session. See [`A2GSPU_TRAP.md`](A2GSPU_TRAP.md) | CTRL-verb (break) |
| `valtrap set <hex>[:w]` / `clear` / `status` | LIVE value-provenance trap (A2GSPU_VALTRAP) | CTRL-verb (break) |
| `itrace [status]` / `from <pc>` / `now` / `n <count>` / `clear` \| `off` | LIVE per-instruction trace: arm at a 24-bit PC or immediately, cap the count, clear | CTRL-verb (trace) |
| `tbtrace [status]` / `on` / `off` / `bank <hex>` | LIVE toolbox/GS-OS dispatch trace, optionally scoped to one bank | CTRL-verb (trace) |
| `callstream on <file>` / `off` / `status` | LIVE NDJSON toolbox/GSOS call stream | CTRL-verb (trace) |
| `key` | inject key + wait for consume receipt | CTRL-verb (manipulate) |
| `protocol` | CTRL v2 framing, ownership and `json <command>` reply wrapper | CTRL-verb (meta) |
| `json <command>` | run any CTRL command and wrap its reply as an `a2ctrl-reply-v2` JSON envelope (`ok`, `status`, `command`, `text`), for a consumer that must not scrape human-oriented text. `status` is the reply's own word (`OK`, `PASS`, `HALT`, `BUDGET`, `HANG`, `FAIL`); `ok` is true for `OK`, `PASS` (an assertion held) and `HALT` (a run stopped where a breakpoint asked) -- it used to be true for `OK` alone | CTRL-verb (meta) |
| `systems [file]` | platform/configuration/ROM/slot-device inventory | CTRL-verb (meta) |
| `limitations [file]` | known unsupported or partial fidelity surfaces | CTRL-verb (meta) |
| `shr-golden <file> [bless]` | live IIgs SHR-window golden without relaunch | CTRL-verb (calibrate) |
| `audio [status]\|doc\|sfx\|on\|off\|reset` | generated sample streams per SOURCE (`doc`=Ensoniq, `sfx`=drive sounds; never summed): count, non-silent, min/max, average, and the same per channel so a dropped side is visible. Register round-trips pass on a chip emitting silence; this does not | CTRL-verb (observe) |
| `boot <slot>` | enter slot firmware at `$Cs00` (1–7) the way autoboot would — for a block device the ROM's own autoboot cannot reach | CTRL-verb (run) |
| `bp [addr\|off]` | interactive breakpoint; bare reports state. A hit **halts in front of the instruction**: the machine stops with PC at the address and that instruction not yet executed (`run` acks `status=HALT … pc=$BB:AAAA at=bp`), under `run`, `step` and `run-until` alike. `run`/`step`/`run-until`/`resume` continue by executing it once, so the next pass through the address breaks again. (It used to let the instruction run and stop only at the end of the frame, up to a frame of instructions past the address, re-dumping registers on each) | CTRL-verb (manipulate) |
| `copilot` | the windowed agent rail's contract (`A2GSPU_COPILOT`): observe + pause/key/mount/quit with human input still live | CTRL-verb (meta) |
| `cov [status]\|on <LO-HI>\|map <A2MAP1>\|off\|reset\|write <file>` | execution coverage: raw `A2COV1`, or source-bound `A2COV2` once a plan is mapped. The write ack carries `format=` and `hash=`; read either with `tools/a2rail/artifacts.py cov`. Mismatched executed bytes refuse the write rather than blessing stale evidence | CTRL-verb (observe) |
| `devstate [name\|all <file>]` | per-device state the emulator already computes (ADB regs, disk head, mouse clamp, DOC, memory map); bare lists handlers. Read before assuming a slot is populated | CTRL-verb (observe) |
| `hgr [page] [file]` | coarse ASCII view of an HGR page — "is anything drawn"; ack carries `profile=hgr-ascii-coarse`. Not pixel-exact; use `pngc` for colour truth | CTRL-verb (observe) |
| `joymode [status]\|gamepad\|mouse\|atari` | controller shape at `$C061-$C063`, and whether the Joyport post-reset suspend window is still open. The only way to reach Atari/mouse modes headlessly | CTRL-verb (manipulate) |
| `memvu [status]\|store\|load\|report <file>\|reset` | live `MEMVU_STOREVIS`/`LOADVIS` counters, readable and zeroable mid-session so a measurement can be a bounded window. `report <file>` writes every armed MEMVU report now — STOREVIS (and its bank lines), LOADVIS, OPMIX, IREUSE, WORKSET, SEAM, BLAME — the same text the spike prints at its end, which a CTRL session never reaches: until 2026-09-16 a session that armed BLAME, OPMIX or SEAM got no report at all. The ack names the sections written. **Refuses** rather than reporting zeros when nothing is armed | CTRL-verb (observe) |
| `paddle <0-3> <0-255>` / `paddle off` | analogue paddle; seeds the 558 decay so `PREAD` counts the value back | CTRL-verb (manipulate) |
| `pause` | stop the machine (`EXEC_PAUSED`), the Machine▸Pause equivalent. Matters on the **windowed** `A2GSPU_COPILOT` rail, where frames run on their own; a headless CTRL session is already stopped between commands | CTRL-verb (manipulate) |
| `pause-status` | `paused\|step\|running` plus the raw `execution_mode`, without changing it | CTRL-verb (observe) |
| `pbutton <0-2> <0\|1>` | game switch PB0–PB2 (`$C061-$C063`) | CTRL-verb (manipulate) |
| `reset [cold]` | the guest reset path (warm by default, `cold` for a restart) — recover a wedged machine | CTRL-verb (manipulate) |
| `resume` | clear a user halt (breakpoint / `save-at`) and unpause, so the CPU advances again. **Refuses** to paper over a jammed CPU: a halting instruction needs `reset` | CTRL-verb (manipulate) |
| `screen` | text-page helper | CTRL-verb (observe) |
| `session [status]\|reset` | what probe state is armed, and one command to disarm all of it. Names what it cannot reach rather than reporting a confident zero. Run it before trusting a measurement, and before handing the machine on | CTRL-verb (meta) |
| `shr <file> [scale]` | IIgs super hi-res as PNG, 320 or 640 per line by SCB; ack carries `profile=shr-linear` | CTRL-verb (observe) |
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

**Cycle contract:** only `cycles=` on `run` / `step` / `run-until` acks are the cost of what that command ran.
A `delta_from_mark` or the difference of two `cycles` acks includes every command in between. See AGENTIC_ORACLE.md.

---

## The closed-loop contract — the Rosetta harness recipe

### Instruction counts and function profiles

`run`, `step`, and `run-until` append `instructions=`: actual opcode executions
during that command, excluding parked breakpoints, reset waits, WAI idle cycles
and stopped clocks. Existing `instr=`/`stepped=` fields remain compatible.
Each block-move iteration counts as one opcode execution. The observation total
is monotonic across reset/restore; it is not restored guest state.

For a bounded profile, issue `cov on 008000-008FFF` (or `cov map <A2MAP1>`),
then `cov counts on`, execute the workload, and `cov counts write <file.json>`.
`cov reset` clears both bits and counts; rearming/disarming coverage disables
counting. `cov counts off` discards counts without disarming byte coverage.
Counts have schema `a2profile-v1`: range, generation, binary-plan `map_hash`
(zero for raw ranges), and sorted PC rows with `instructions` and `cycles`.
Writes refuse unarmed counting, stale mapped code, and unwritable paths.
Empty `pcs` is a valid observation of no execution, not a workload pass.

The `instruction-cycles` model attributes each instruction's own CPU cycles to
its starting PC. Interrupt entry before an opcode and idle waits are excluded,
so profile cycles may be less than a command's elapsed `cycles=`. Existing
`A2COV1`/`A2COV2` bitmap formats stay unchanged.

`python -I tools/a2rail/profile.py counts.json --symbols functions.json`
rolls PCs into named, half-open absolute ranges. Symbol JSON has schema
`a2symbols-v1` and `functions: [{"name":"loop","start":32768,"end":32784}]`.
Unmatched PCs remain `<unassigned>`; no total is silently dropped. `A2MAP1`
contains binary segment identity, not function names: pass `--map plan.a2map`
to validate segment ranges and identity alongside the symbol file. For mapped
profiles the symbol document must carry the matching `map_hash`. Use
`--expect-instructions N` to gate a total; a mismatch exits 1.

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

## Worked examples

**Who wrote address X, who first made value V.** `WATCH` keys on the destination address,
`VALTRAP` on the value. When a bad value reaches memory through an intermediary (a ROM
routine stores a pointer the OS computed with a lost base), a `WATCH` on the landing site
names only the copier; `VALTRAP` on the value names the code that first materialised it.
`bus_write` is byte-granular, so `VALTRAP` reassembles consecutive ascending-address
little-endian byte writes before matching: a multi-byte pointer store fires once, at the top
byte, with that instruction's PC.

```sh
# who writes into the SCM notify-queue region:
A2GSPU_WATCH="E1:19A0-1FFF" A2GSPU_WATCH_MAX=0 GSSquared -p 5 -d s7d1=boot.po -n
# who first materialises the 24-bit pointer $E06014:
A2GSPU_VALTRAP="E06014" GSSquared -p 5 -d s7d1=boot.po -n
```

**A frozen clock.** `A2GSPU_FAKETIME=<unix-epoch-seconds>` seeds the RTC's accumulator to that
instant and bypasses `localtime`/timezone entirely (freezing `time()` alone is not enough —
`tm_gmtoff`/`_timezone` still vary by host). The guest reads it through the ordinary
`$C033`/`$C034` path, exactly as it would read a real clock stopped at that instant:
`A2GSPU_FAKETIME=1000000000` makes the seconds registers read `0xB7C07A80` on every run.
The clock cards read the same instant's calendar fields (`1780000000` is 2026-05-28 20:26:40,
a Thursday: the ProDOS clock card's GETLN line is `05,04,28,20,26`, the ThunderClock's counter
`5428202640`), frozen like the RTC until the guest sets the ThunderClock, which then counts
emulated seconds. Without the pin, the VGC one-second interrupt keeps its host-synced phase; with
it, the first edge is exactly one second after power-on. Either way each edge follows the last by
exactly one emulated second (`devstate display` shows the next one), and the `vgcint` snapshot
block (version 2) saves how far away it is.

## For developers: the observer read

An observe rail reads guest memory through `a2obs_peek(mmu, addr)` (`src/a2gspu_observe.hpp`),
never through `mmu->read`. On a IIgs that is `MMU_IIgs::probe_peek`, which resolves banks
`$00/$01` through the same aux and language-card mapping the CPU uses, answers `$C100-$CFFF`
from the Mega II page table and `$E0/$E1` from the Mega II image, and returns a placeholder only
for the `$C0xx` soft switches — `probe_readable` says so, and a rail prints `unreadable` rather
than the placeholder. On the II family it is the MMU's read outside `$Cxxx` (where it has no
side effect, and where the base `read_raw` probe would not resolve the language card) and the
probe inside it. The CPU's path is not side-effect-free: it throws soft switches, selects slot
`$C800` ROM, and on a IIgs sets the cycle type of what it reads. `tools/a2rail/unarmedhash.ps1`
is the gate: observation rails armed must reproduce the unarmed hash and cycle count.

## Output lines consumers parse

These strings are an interface. Harnesses outside this repository match them, so a change to
any of them is a contract change: say so in the commit, and update the consumer.

| Line | Shape consumers rely on |
|------|-------------------------|
| `A2GSPU PINS:` | `seed= faketime= ramdisk= bram= platform= frames= rom=<size>/<fnv> build= exe=` |
| `GSDIAG:` | `status= rc= gate= gsos_err=$ brk= scb= hash= warnings= cycles=` (+ liveness fields when armed) |
| `SPIKE E1:` | `SHR-window $2000-$9FFF nonzero=N/32768 distinct=N hash=<fnv>` |
| `IIGS ASSERT:` summary | `N checks -> ALL PASS` / `N checks -> FAILED`; `(K unparseable)` appears only when K > 0 |
| `IIGS TOOLBOX: ret` | `ret <name> ($<call>) carry=<0\|1> err=$<code>` (the count of `carry=1` is the success signal `GSDIAG status=OK` is not) |
| `DHGR GOLDEN:` | `MATCH` / `DIFF` / `blessed` with the profile |
| CTRL `run` ack | prefix `status=OK ran=N cycles=N (in-command) PC $bb:aaaa->$bb:aaaa` (a suffix may follow) |
| CTRL `assert` literals | a `peek:` comparand needs an explicit base (`$22`, `0x22`, `0d34`); a bare hex-shaped literal is refused |
| `tools/a2rail/artifacts.py` | `read_cov(path)` is imported by harnesses; its return shape is an API |
| `corpus/` | the file names in `corpus/README.md` are what consumer harnesses open |

## Related docs
- [`CLOSED_LOOP_INTEGRATION.md`](CLOSED_LOOP_INTEGRATION.md) — the mission, the operating modes,
  the launch recipe, upstream-sync discipline.
- [`A2GSPU_TRAP.md`](A2GSPU_TRAP.md) — traps from configuration.
- [`INSTRUMENTATION_SHAPE.md`](INSTRUMENTATION_SHAPE.md) — the mechanisms (M1–M12) and their status.
- [`UPSTREAM_SYNC.md`](UPSTREAM_SYNC.md) — what has been ported from upstream, and what is owed.

> Note on the `A2GSPU_CMD_*` / `A2GSPU_REG_*` / `A2GSPU_STATUS_*` / `A2GSPU_MODE_*` /
> `A2GSPU_SHR_*` / `A2GSPU_UHR_*` / `A2GSPU_PAL_OFFSET` / `A2GSPU_SCB_OFFSET` /
> `A2GSPU_IDENT_BYTE` identifiers in `src/devices/a2gspu/`: those are the **legacy card
> model's protocol** compile-time constants (register offsets, command opcodes, status bits), **not**
> env-gated instrumentation rails — they are not listed here.
