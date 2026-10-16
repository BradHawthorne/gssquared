# Upstream telemetry and observation audit — 2026-10-15

The owner asked whether upstream does any telemetry, interface or observation
better, and authorized improvements. Yes: upstream's structured debugger API
was easier to consume directly than the equivalent artifact-oriented MCP
commands in this fork. The underlying protocol was already inherited; four
adapters now make it available as structured MCP observations. The audit also
found and fixed an inaccurate extension in the fork's trace text formatter.

## Sources and scope

Fetched the existing `upstream` remote on 2026-10-15. Main remains `643e6c21`
(2026-09-30). The separate `upstream/1.1/ntscshader` branch is `0aed0214`
(2026-10-10). Neither a cached main ref nor a comparison with fork HEAD alone
describes the full situation: this audit includes the fork's existing working
changes, including its native socket tools, device inspection, subscriptions,
MCP adapter and agent bootstrap. Fork base is `834028cd` on `a2gspu`.

Primary source comparisons:

- [Upstream main wire implementation](https://github.com/jawaidbazyar2/gssquared/blob/643e6c21/src/debugger/DebugProtocolServer.cpp)
  and [wire contract](https://github.com/jawaidbazyar2/gssquared/blob/643e6c21/Docs/DebugProtocol.md):
  GET_TRACE, READMEM domains, FINDMEM masks, VIDEO_TEXT, STATE_GET and stops.
- [Upstream trace formatter](https://github.com/jawaidbazyar2/gssquared/blob/643e6c21/src/debugger/trace.cpp),
  Monitor, MemoryWatch, DebugFormatter and the Python client. Main has no newer
  telemetry protocol missing from the fork; its useful native records were
  less directly available through our MCP tools.
- [Development-branch renderer design and verification](https://github.com/jawaidbazyar2/gssquared/blob/0aed0214/Docs/NTSCShader.md),
  `apps/vsgtest`, `GsCompositeGpu`, `NtscGpu`, and menu/debug-window changes.
  These are separate from main and from the drift-stage A comparison.

## Findings and disposition

| Area | Where upstream does better | What changed here |
| --- | --- | --- |
| Structured trace | GET_TRACE exposes the retained ring as registers, opcode/operand, effective address, data and direction without parsing formatted text | `gs2_trace_read`: named fields, native uint64 clock preserved as a decimal string, availability/window bounds and deltas only between adjacent returned records |
| Explicit memory domains | READMEM directly exposes CPU, Mega II, DOC, ADB and raw memory views; simpler than opening a file for a small read | `gs2_memory_read`: six explicit non-bus domains, machine restrictions, null unreadable bytes and per-byte readability |
| Masked search | FINDMEM provides a useful mask-based search primitive absent from our MCP list | `gs2_memory_search` uses a bounded paused READMEM snapshot and the same bit-mask comparison model. It excludes unreadable I/O candidates, including wildcard matches; it reports a cap and whether further hits exist. It does not call FINDMEM or claim its placeholder bytes are valid memory |
| Native text page | VIDEO_TEXT retains raw character codes and resolved page/mode/flags together | `gs2_video_text_read` returns native metadata and bytes with convenient ASCII-ish lines; graphics and glyph rendering are explicitly outside this observation |
| Trace presentation | Upstream's formatter depends on the record, not a previously formatted line | Removed the fork extension's static last-cycle variable. Re-reading history no longer changes its output or produces unsigned-underflow deltas. Removed the fixed 2.8 MHz scanline/VBL estimate; DBR/DP and recorded NClock cycles remain |
| Serial timing and startup | Baud-metered receive, DCD/Hayes changes, Windows listener startup fix | Existing drift C decision remains separate; this audit does not quietly mix it into read-only observation work |
| Clock structure | Explicit clock domains and scheduling make timing ownership clearer | Existing drift D remains separate, with its unchanged-baseline acceptance rule. No fixed-speed video inference is used as a substitute |
| Device/control/event coverage | Main's device blobs and stops are useful; the fork already retains them and adds typed device coverage, IRQ/audio/device streams, loss reporting and shared native tools | Kept the existing surfaces. No second device-control implementation or new wire type was needed |
| Development renderer validation | `vsgtest --gpu` compares actual GPU readback with CPU/reference output and checks composite properties | Recorded as an upstream advantage. The new decoder/composite GPU implementations are Metal-only; Windows DXIL and Linux SPIR-V builds are not supplied. The current fork has neither those renderer classes nor their packed-frame pipeline. Porting a Metal-only renderer test into this Windows tree would not validate its current output |

The development renderer also adds packed scanlines, GS composite, persistence
and scaling controls. These are rendering changes, not additional debugger
telemetry. When that renderer is ported, carry its GPU readback comparison
harness and expose its settings/model names with the existing `ui`/manifest
contracts. Existing raw/reference PNG tools do not establish actual GPU-output
equivalence, and this report makes no such claim. Physical window verification
remains manual.

## Contract and implementation

The MCP catalog now has **79** tools: 73 native verbs, catalog and cold-batch
adapters, and four structured observations. The native counts remain 73 verbs,
161 rails and 32 wire types. `gs2debug.observations` supplies the same adapters
and a fixed-offset little-endian trace decoder for Python clients.

All four require explicit pause and exclusive ownership of the session. There
is no implicit pause, resume or guest execution. CPU probe bytes and their
readability mask use two bridge calls; paused ownership supplies consistency,
not a claim that multiple controllers form an atomic transaction. Explicit
bus-read domains are excluded. Platform/device restrictions still come from
the native server. Inputs and output sizes are bounded: trace 512 records,
memory 65,536 bytes, pattern 256 bytes, returned search hits 512 maximum.
First/backward trace deltas are null; trace clocks alone cannot recover video
phase. Memory output is inline JSON rather than an artifact-only response.

Source changes are in `clients/python/src/gs2debug/observations.py`, the MCP
adapter and `src/debugger/trace.*`, with registry/help text kept consistent.
The foundation `agentic` gate runs the new real MCP observation fixture through
`mcpcheck.py`; it exercises both IIe and IIgs. Protocol/unit fixtures also run
there without requiring pytest.

## Evidence

- `before.log`: previous MCP implementation fails discovery of the four
  structured observations.
- `before-trace-fix.log`: with the adapters present but the old executable,
  actual instructions, masked search, unreadability, bounds and platform
  refusals pass; two dumps of unchanged paused history differ, reproducing the
  trace-formatting bug independently of the replacement formatter.
- `clients/python/tests/test_observations.py`: independent 40-byte wire fixture
  pins offsets, ABI padding, uint64 precision, access flags and registers;
  malformed records/arguments refuse, backward clocks have no duration.
- `observabilitycheck.py`: real guest LDA/STA/NOP records, independently
  expected byte pattern and instruction costs (2 and 4 cycles), I/O false-hit
  exclusion, wildcard caps, raw text metadata, running-state refusal and
  IIgs-only DOC-domain behavior.

## Final validation and launch

- Release out-of-tree build succeeded; final rebuild reports no work pending.
- **14/14 CTests and all 14 foundation gates pass.** Full matrix and individual
  checks are in `gates.log` and `gates.ndjson`; the new fixture runs inside the
  `agentic` gate on both platforms. All four pinned boot hashes/cycle counts
  and all 24 hwtruth verdicts remain unchanged; MB-Audit passes 1,922 tests.
- Python pytest: **10/10 pass**. The new protocol/observation fixtures also
  pass in the foundation gate without pytest.
- Rosetta's CPU differential test passes its existing classification contract:
  **103 vectors, 100 identical, three known COP/BRK stop-policy state
  differences, zero cycle-only differences**. This is not a claim of 103
  identical CPU outcomes. See `rosetta-cpu.log` and `rosetta-compare.txt`.
- The preserved executable is
  `D:\gsqs\coherency-20261015\build\GSSquared-observability.exe`, SHA256
  `121e1f18fc0d4e770acb6053485c0bd75a08239e6d099a4ea446bbad79f5158a`.
  Its adjacent runtime assets are retained. `validation.json` records identity
  and counts.
- [mcp-launch.json](mcp-launch.json) is a ready source-checkout launch
  descriptor targeting that executable. An actual IIgs stdio client initialized,
  discovered 79 tools and 73 native commands/161 rails, read memory through the
  new adapter, and closed its owned emulator with exit zero. The retained native
  startup/closed receipt was checked. See `mcp-launch-proof.json`. No AI client
  configuration was installed or changed.
- The consumer-facing `build\GSSquared.exe` still has SHA256
  `af7e0bbd72bee417ffddb5e86a00597df280fba904e8b2ed1ff9ad0738e2956c`.
  The earlier `GSSquared-tools.exe` and its launch descriptor are also preserved.

Changes remain local working-tree changes. No consumer binary replacement,
baseline re-blessing, push, remote change, vendor edit or drift-stage B–E
application is part of this change. Physical stock-window verification remains
the manual gap described above.
