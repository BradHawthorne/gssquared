# Interface completeness implementation (2026-10-15)

The owner asked to fully work through the suggestions from the live black-box
audit. This is an implementation queue, not a declaration that the emulator is
fully transparent. Existing P0/P2.1/P2.4 work remains in the working tree.
No pushing, vendor edits, consumer binary replacement, engine merge or baseline
re-bless is authorized by this queue. Upstream drift stages retain their
individual approval requirement.

| Work | Acceptance | State |
| --- | --- | --- |
| Accurate discovery | Runtime snapshot contract agrees with included/excluded state; gate catches stale claims | RTC exclusion corrected and gated; broader coverage audit continues |
| Native typed observations | CPU/device/signal capabilities and values are JSON with models, units, availability and explicit coverage; no prose parsing | Checkpoint 1 passed: `inspect`, native CPU/execution replies, default IIgs native observers, typed signals. Readability-aware memory capture and a public Python file client added next |
| Device coverage matrix | Every installed device identifies state/control/snapshot coverage and missing semantics | Implemented declarations plus actual per-bundle coverage; optional native observers and dual-platform noninterference checked; full field coverage remains partial |
| Semantic device diffs | Named register/FIFO/timer/IRQ changes; unsupported payload versions remain explicit | Implemented for hash-bound native checkpoint companions; arbitrary historical payload semantics remain opaque and identified |
| Live control | Declared launch-only reasons, additional safe live bindings, deliberate device controls and readback | Live SCC/WDM capture and owned input edges implemented; remaining launch-only bindings are explicitly labeled |
| Fault injection | Scheduled NMI/ABORT/IRQ and card fault cases, off-by-default noninterference | Boundary sources implemented and checked on IIe/IIgs modes; mid-bus ABORT rollback remains unsupported; existing card fault rails retain their gates |
| Events | Ordered subscriptions for stops/traps/console/device events with loss and overflow reporting | File/co-pilot and socket streams implemented; IRQ/audio and sampled device changes included; focused dual-platform checks passed; complete device transitions remain outside the sampling model |
| Guest meaning | Configured source maps/kernel layouts, tasks/log/panic views; corrupt layouts fail loudly | Bound assembler and C statement maps implemented and exercised with real Rosetta output; configured kernel readers checked on synthetic layouts; actual kernel profile/DWARF remain open |
| Output assertions | Audio content, SHR text and tolerant image comparisons with planted wrong-result tests | Implemented for generated DOC/SFX samples, decoded PNGs and configured bitmap glyphs; real DOC/SHR evidence checked; physical host mix/speaker waveform excluded |
| Input macros | Cycle/frame-stamped key/mouse/paddle recording and replay with receipts | Implemented agent-input plans, exact/late receipts and replay timestamp checks; physical host-event recording and full-machine replay excluded |
| Checkpoint branching/replay | Complete coverage contract, independent branches, replay/seek/reverse proof; clock prerequisite tracked separately | Open |
| Boundary and GUI coverage | External host state and uninstrumented GUI gaps explicitly reported and exercised where accessible | Open |

Implementation evidence and outstanding prerequisites will be recorded here as
each slice is built and tested. Completion requires IIe/IIgs evidence and the
full gate matrix with existing baseline witnesses unchanged.

## Checkpoint 1: native interfaces and ordered event subset

Fresh Release, ctest **14/14**, umbrella gates **14/14**, existing baseline
witnesses unchanged. Evidence: `checkpoint1-gates.log` and
`checkpoint1-gates.ndjson`. The exact binary is retained as
`D:\gsqs\coherency-20261015\build\GSSquared-interface1.exe`, SHA256
`e05b75430cdd4c2da0eee5c9eea9443432c1a5006673cbde940c655f8d677d16`.
The in-tree consumer remains SHA256
`af7e0bbd72bee417ffddb5e86a00597df280fba904e8b2ed1ff9ad0738e2956c`.

The fixture gate proves native CPU values/widths, direct instruction/cycle
counts, all default IIgs handlers with native observers, selected SCC field
types, exact saved-state noninterference on IIe/IIgs, explicit unavailable
signals/invalid commands, event ordering/filtering, central trap fan-out exactly
once, and explicit overflow. The standalone C++ test covers JSON escaping,
oversize failure, signal availability, released owners and duplicate fields.

Native observers are registered separately from human debug displays: some
human displays keep host-side history and cannot be treated as pure getters.
Native field coverage remains partial; `complete_typed_state=false` is deliberate.
No full-machine replay, complete semantic snapshot decoder or socket subscription
is claimed by this checkpoint.

## Subsequent work in progress

`inspect memory` exposes CPU-visible bytes and per-byte readability. Its first
dual-platform test caught an implementation error: the IIgs machine's Mega II
MMU differs from its CPU's FPI MMU. The reader now uses the established
`rail_mmu` helper; the original IIgs failure is retained in the scratch logs.

`CtrlClient` adds a vendor-neutral Python file-rail interface with a single
writer lease, atomic non-overwriting command publication, native JSON validation,
and pending-command recovery without duplicate issuance. Source and kernel
adapters consume explicit layouts, placements and memory artifacts rather than
compiling product layouts into the emulator.

## Checkpoint 2: public client and bound guest views

Umbrella gates **14/14**, including ctest **14/14** and unchanged pinned
baselines. Evidence: `checkpoint2-gates.log` and `checkpoint2-gates.ndjson`.
Retained binary: `D:\gsqs\coherency-20261015\build\GSSquared-interface2.exe`,
SHA256 `99a6dafff33cd2398254d0438f2c00b24833a6c4116ef04a426e2d63443296d0`.

The public file client, layout schema, tasks/log/frame-chain/panic readers and
Rosetta line-sidecar adapter have independent fixtures and live IIe/IIgs checks.
Layouts require immutable byte-hash bindings; corrupt lists, unreadable bytes,
stale source bytes and mixed observation epochs fail. A real `asmiigs -g` /
`linkiigs` six-byte fixture stopped at `$0805` resolves to source line 5 on both
machines, with a changed-byte refusal (`rosetta-source-proof.json`). The kernel
fixtures are synthetic configured layouts, not evidence of a running Linux
kernel. At checkpoint 2, Rosetta `cciigs -g` lacked usable C source markers;
checkpoint 4 remedies that producer gap. DWARF and a real kernel profile remain
open.

Work after checkpoint 2 adds live capture bindings, installed-model inventory,
semantic checkpoint observations and generated audio content; these additions
need their own verification before being counted complete.

## Checkpoint 3: content, inputs and boundary faults

Fresh Release, ctest **14/14**, umbrella gates **14/14**, pinned baseline
witnesses unchanged. Evidence: `checkpoint3-gates.log` and
`checkpoint3-gates.ndjson`. The first launcher omitted Python from PATH; its
six Python-backed gates did not execute. The corrected complete run passed.
Retained binary `D:\gsqs\coherency-20261015\build\GSSquared-interface3.exe`,
SHA256 `278f4d39d2c4bf9f2bb9c69a6efd92e1ecae5f2ed331da3d4e45c780d0fe1fcf`.
The in-tree consumer retains SHA256
`af7e0bbd72bee417ffddb5e86a00597df280fba904e8b2ed1ff9ad0738e2956c`.

Live IIe/IIgs checks passed for quoted checkpoint paths, identical semantic
bundles, changed paddle fields, tampered artifacts, real DOC content, planted
wrong samples and configured SHR glyph extraction. Input checks cover cycle
lateness, exact-deadline refusal, frame timing, stale code refusal, replay
timestamps, owned keys and unread ADB-packet refusal. Optional U2/SSC/Mockingboard/
mouse/Thunderclock/VOC native observers preserve saved-state bytes on their
supported platforms. Boundary fault checks pin vectors, stack widths/contents,
7/8-cycle entries, decimal clearing, IRQ ownership, scheduled lateness and
snapshot refusal with active injector state.

The fault fixture exposed that the native IIe stack pointer had been labeled
as a bank-zero address while reporting its low-byte storage. Native inspection
now returns `$0100 | S` in emulation/6502 modes and full S in native mode.
Native CPU state also names WAI as `waiting_for_interrupt`, rather than
mislabeling the internal wait flag as readiness.

At checkpoint 3, open work included socket subscriptions and C source metadata;
the subsequent slice below implements both. Remaining work includes complete
device transitions, DWARF production, a selected real kernel profile, full replay /
branching after its clock prerequisite, and the uninstrumented manual GUI gap.
Approval requests for drift C and D are pending. Nothing in this queue silently
authorizes drift, baseline changes, an engine merge or a consumer rebuild.

## Subsequent slice: socket streams and C statement provenance

SUBSCRIBE adds a second independently owned observer beside the file stream.
Discovery exposes all 30 wire types and accepted filters from the same runtime
registry. Explicit count/byte caps, dropped counts and sequence gaps bound loss;
retained means enqueued, never consumed. Focused live checks passed on IIe/IIgs
for delivery/order, filtering, malformed requests, bounded loss, duplicate arm,
disconnect cleanup and reconnect. IRQ edges and audio capture use native
records. Device baseline/change records explicitly sample commands/video
frames; they do not claim to see every transient register transition.

Shutdown now closes capture/subscription outputs before destroying their clocks
and devices. Live quit-with-active-output fixtures passed on both machines.
The C++ contract also verifies that closing either observer leaves the other
working. Key injection rejects an unavailable/full/intercepted input path;
capture close-out retains the final byte count, and active-fault restore
refusals identify preflight rather than incorrectly reporting a file error.

The related Rosetta changes add opt-in `cciigs -g` C statement markers and fix
preprocessor source-name lifetime, include-error success codes and generated
built-in spelling. A real C/assembler/linker chain maps `$0808` to C line 2 and
returns 66 on a 65816 IIe and a IIgs (`rosetta-c-source-proof.json`). Loaded code
and assembly provenance are bound; current C source text, DWARF and a running
Linux kernel are not verified by this fixture. Rosetta's external CPU harness
now selects matching out-of-tree headers/archives rather than linking the stale
consumer; its 103 vectors retain the three known COP/BRK stop-policy differences.

The final default-mode reference corpus comparison covers 1,183 C/Pascal files:
232 accepted outputs are byte-identical, 936 are rejected by both versions,
and 15 now refuse missing headers that previously printed errors with exit 0.
All 15 changes were individually classified. No valid accepted output bytes
changed and no new input was accepted. These are intentional fail-loud changes,
so the raw differential is not labeled an unqualified zero-change pass.

## Checkpoint 4: final verified state

Fresh out-of-tree Release, ctest **14/14**, full umbrella gates **14/14**, and
unchanged pinned witnesses. Raw evidence: `checkpoint4-gates.log` and
`checkpoint4-gates.ndjson`. Preserved executable:
`D:\gsqs\coherency-20261015\build\GSSquared-interface4.exe`, SHA256
`e27cceb38e978b9041eaa8db0d4c77ee770ce9bb54ab39f78e8db9d02083ec45`.
The protected in-tree consumer still has SHA256
`af7e0bbd72bee417ffddb5e86a00597df280fba904e8b2ed1ff9ad0738e2956c`.
No baseline, engine-merge, upstream drift or consumer replacement was applied.

Rosetta's final serial `ctest -j1 -LE slow` reports no failures across 376
registrations: 375 passed and the declared `byte_diff_ad35driver` skip. The
unchanged optional GUI suite passed in the preceding full 377-registration run
(1,271 seconds); its one CPU-reference failure was corrected and rechecked in
the final run. Final tracked-doc/source checks passed 5/5. The real C source
proof was repeated using the final compiler and preserved emulator executable.
The 15 corpus refusal changes are retained in
`rosetta-corpus-acceptance-changes.json`.

Rosetta changes are five local commits (`62fb3de`, `3e3ecbb`, `f04a08f`,
`3c9c8dc`, `4a7f358`); its pre-existing Port7 reference-listing edit remains
untouched. This fork's changes remain in the working tree. Nothing was pushed.

## Remaining boundaries and owner decisions

- Full checkpoint branching, seek and reverse/replay still require the clock
  prerequisite. Drift D has a pending owner decision; drift C's serial changes
  also remain separately pending. `Docs/HANDOFF.md` rule 2 requires the owner's
  yes for each drift stage and unchanged baseline witnesses on evaluation.
- Device fields are partial and command/frame changes are sampled. Hidden
  device phases, external connections, host mix/GPU state and physical hardware
  are not promoted to complete semantic snapshots or exhaustive event traces.
- Kernel views require an explicitly selected real kernel image/layout/symbol
  fixture. Current kernel tests are synthetic. C statement maps work; DWARF,
  source-text verification and full variable/unwind information remain open.
- Co-pilot and socket/CTRL flows are exercised. The uninstrumented manual GUI
  workflow still needs a human check; native desktop interaction is unavailable
  through this session's enabled computer-use tools.

These are retained gaps, not silent successes. The next agent can reproduce the
verified interfaces without changing the consumer binary, and can continue the
conditional work once its required decision or fixture is available.
