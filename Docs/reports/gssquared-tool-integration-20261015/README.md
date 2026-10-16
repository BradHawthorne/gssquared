# Native tool integration (2026-10-15)

Owner direction: if a useful tool is inaccessible to agents, integrate it into
the emulator. This continuation closes concrete access gaps without changing
the protected consumer executable, pinned baselines, vendor code or upstream
drift stages. Earlier P0/P2/interface work remains in the working tree.

| Gap | Implementation | Evidence |
| --- | --- | --- |
| Socket agents could not reach most CTRL instruments | AGENT_TOOLS/AGENT_COMMAND; one shared dispatcher rather than copied handlers; all 73 verbs discoverable, QUIT keeps its native acknowledged route | Live IIe/IIgs catalog, execution, device controls, quoted artifacts, snapshots, negative framing/tool/ownership cases |
| Useful presentation/menu settings required GUI interaction | `ui` status plus monitor, CRT and speed settings with native availability/before/after/effect/changed fields | IIe/IIgs file and co-pilot checks; socket access; no guest time advance for presentation, explicit deferred clock application, unsupported/no-change cases |
| AI clients needed custom binary/file protocol code | Stdlib MCP stdio adapter and installed `gs2-mcp` entry point; 75 tools including live catalog and isolated batches; launch or attach | Independent wire client drives real emulator on both machines; lifecycle/order/error/argument/JSON limits, native execution and output evidence |
| Startup-only rails lacked a managed launch path | Explicit A2GSPU_AGENT mode shares CTRL bootstrap, starts paused and suppresses host preferences/input; native completion proof, interactive startup rails and isolated batch-only rails | Pacing and coverage fail before the bootstrap fix and apply afterward; positive/wrong batch assertions produce opposite native exits while the current machine remains unchanged |
| A long command could execute but lose its completed reply | Bridge rechecks completion after reacquiring the execution lock at its wait deadline | A real six-second run failed before the fix and retains its result afterward |

MCP startup, initialized notification, tools/list and tools/call follow the
documented `2025-11-25` stdio tools profile. HTTP/tasks/cancellation are not
advertised. Native tool errors remain structured and are also serialized as text
for clients. No shell is involved; notifications never invoke tools. An attached
session is left alive on EOF, while an owned session is gracefully quit and its
exit receipt retained. An existing output directory is never overwritten.

The mode adds one discoverable rail (161 total; 70 generic live setters remain).
Managed MCP launch verifies native `a2agent-mode-v1` enabled/complete state
before serving tools. Batch-only rails refuse interactive launch and direct the
caller to `gs2_batch`, a separate cold child with bounded timeout and retained
native exit/log evidence. This does not claim checkpoint branching or full
replay. Fixed `--out-root` configurations generate fresh per-launch directories;
explicit `--out-dir` preserves its refusal to overwrite previous evidence.

The interim bridge-only matrix passed 14/14 (`bridge-gates.*`). The subsequent
startup audit then reproduced ignored pacing/coverage settings under plain
`--debug`. Explicit agent mode now uses the same instrument initialization as
CTRL; the test checks actual pacing/coverage effects and native mode state.
The adapter's regression fixture checks the classification of every getenv
in the one-shot tail, so those rails cannot silently be accepted as interactive
startup controls when that tail changes.

Known-fail logs were captured against the preceding binary for missing socket
tools and missing `ui`; the absent MCP adapter also failed its independent
fixture before implementation. The long-operation result-loss bug was
reproduced before its bridge fix. Final acceptance is recorded below.

The first umbrella run caught four parenthesized `sizeof(result)` expressions
that still measured a pointer after dispatcher extraction. One truncated the
coverage-map acknowledgement; another could underflow its remaining-capacity
argument. All now use the explicit capacity parameter. The existing coverage
fixture exposed the regression before acceptance. The failed matrix is retained
as `first-gates.log`/`first-gates.ndjson`; it is not counted as a passing run.

This is tool access, not complete physical GUI automation or a new fidelity
claim. The UI status names unsupported host-only flows. Native settings do not
verify physical pixels, host audio or hardware. Full-machine replay, DWARF and
real-kernel fixtures retain their previously recorded prerequisites. No upstream
drift, engine merge, consumer replacement or baseline re-bless is part of this
change. Nothing is pushed.

## Final validation and launch artifact

Release build, ctest **14/14**, umbrella gates **14/14**, pinned unarmed/armed
boot hashes and cycle counts unchanged. Exact matrix: `final-gates.log` and
`final-gates.ndjson`. The current native inventory is 73 verbs, 161 rails,
70 generic setters and 32 wire types; MCP provides 75 tools including catalog
and independent batch jobs. The in-tree consumer executable is unchanged.

Preserved tested executable:
`D:\gsqs\coherency-20261015\build\GSSquared-tools.exe`, SHA256
`c55c06bf4bd0b49a99762ec5f925bccae8dd25eab216aa0f5ee9c74975b005cd`.
Protected consumer SHA256 remains
`af7e0bbd72bee417ffddb5e86a00597df280fba904e8b2ed1ff9ad0738e2956c`.

Python package fixtures **7/7**. Rosetta's external CPU differential was rerun
against the fresh libraries: **103 vectors**, 100 identical, the three already
classified COP/BRK stop-policy differences, and zero cycle-only differences.
No Rosetta source or its pre-existing Port7 listing edit was changed here.

The full agentic gate includes actual startup pacing/coverage effects and native
bootstrap proof, positive and planted-wrong one-shot memory assertions, isolated
CPU state, malformed framing/requests, unsupported tools/settings, source-backed
one-shot classification, invalid native agent configurations, output-directory
preservation, startup conflicts/unknown rails, attached lifetime, EOF shutdown,
repeat launches, co-pilot restrictions and the six-second bridge regression.
Batch CPU/RAM is separate; selected media/host endpoints may still be shared.

`D:\gsqs\interface-completeness-20261015\mcp-launch.json` is a local, concrete
stdio launch descriptor using that preserved executable, required runtime PATH,
IIgs and a restartable output root. An independent stdio initialization/list/
native-catalog smoke test passed with **75 tools / 73 verbs / 161 rails** and
clean owned EOF shutdown. This file is an integration artifact, not a silent
change to any AI application's settings. Usage is in `Docs/DebugClient.md`.

Fork changes remain in the working tree. No vendor edits, upstream drift,
baseline re-bless, engine merge, consumer replacement or push occurred.
