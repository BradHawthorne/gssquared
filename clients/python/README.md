# gs2debug

Python client for the GSSquared external debug protocol.

Native machine context is available through `gs2debug.context.call` and the
MCP `gs2_context_read` / `gs2_context_track` tools. Reads can sample a running
machine; observer controls require explicit pause. The API connects live
state, model topology, source references, retained causal events and typed
changes. See [Machine context](../../Docs/MACHINE_CONTEXT.md).

- **Agent cookbook:** [Docs/gs2debug.md](../../Docs/gs2debug.md)
- Wire format: [Docs/DebugProtocol.md](../../Docs/DebugProtocol.md)
- Client design notes: [Docs/DebugClient.md](../../Docs/DebugClient.md)

## Install (editable)

```bash
cd clients/python
pip install -e .
```

## Smoke test

With the emulator listening (separate terminals):

```bash
# terminal 1 — IIe
./build/GSSquared --debug /tmp/gs2.sock -p 2

# terminal 2
cd clients/python
PYTHONPATH=src python examples/hello_ping.py /tmp/gs2.sock
```

`hello_ping.py` prints `execution_mode` and `platform_id` from `GET_STATUS`.

## Native agent tools and MCP

After HELLO, `Client.agent_tools()` discovers all native instruments;
`pause()` then `agent_command('step 1')` uses the shared CTRL handler and native
JSON verdict. Commands can deliberately change guest/host state. See the
[client contract](../../Docs/DebugClient.md) for pause, timeout and ownership.

`gs2-mcp --exe <tested-executable> --platform 5 --out-root <runs-directory>`
starts a stdio MCP adapter with an owned emulator, or `gs2-mcp --socket
<endpoint>` attaches without owning its lifetime. From the source checkout use
`python -I tools/a2rail/mcpserver.py` with the same arguments. The adapter
generates its tools from live discovery; it does not require a third-party MCP
package. Configure the executable's normal runtime DLL PATH on Windows.

Four structured observation adapters supplement the native command tools:
`gs2_trace_read`, `gs2_memory_read`, `gs2_memory_search`, and
`gs2_video_text_read`. They use the upstream debugger protocol and require an
explicitly paused session. Memory reads mark unreadable I/O bytes as null;
searches exclude those placeholders. Trace records retain native registers,
access direction, addresses and decimal-string 64-bit clock values. These
clocks do not determine a video scanline. Python callers can use
`gs2debug.observations.observe(client, name, parameters)` and
`decode_trace_entry(blob)` directly.

Managed startup uses the native agent bootstrap and verifies its receipt.
`gs2_batch` exposes one-shot assertions/goldens/microtests in an isolated cold
child; interactive tools retain the current paused machine. A stable output
root creates a separate session directory on every restart.

`gs2_artifact_put/list/read` completes the MCP-only input/evidence loop: upload
a digest-checked input once, pass its path to native load, run/assert, then read
typed files or receive a native PNG as MCP image content. The managed output
directory is the default artifact root; attach requires an explicit
`--artifact-root`. Range, size, hash and producer-finalization limits are
documented in [DebugClient.md](../../Docs/DebugClient.md).

Socket `RequestPending` preserves partial replies and blocks another request.
Use `wait_agent_command()` or raw `wait_response()` to recover the original
result without reissuing; terminal transport failures close the connection.
Neither a timeout nor disconnection establishes cancellation.

## Tests (framing and MCP protocol fixtures)

```bash
cd clients/python
pip install -e ".[dev]"
pytest
```
