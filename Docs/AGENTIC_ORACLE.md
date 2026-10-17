# Agentic oracle — north-star contract for gssquared

**Goal:** nothing in gssquared is a black box to agentic AI. Agents have
**maximal control**, **maximal observability**, and **named, fail-loud
understanding** of every answer the emulator gives — so closed-loop toolchain
development (Rosetta, a2engine, a2tile, …) can treat this fork as a trustworthy
hands-and-eyes platform.

This is not “more debug prints.” It is a **product definition**.

---

**2026-10-16 direction:** expose a connected, explainable machine in real time.
Native `context` supplies synchronized state, compiled/installed/absent models,
source references, bounded causal evidence, changes and named explanations.
Socket read queries work while running; uncertainty and missing coverage stay
visible. The Rosetta companion binds correctness to measured efficiency.
See [`MACHINE_CONTEXT.md`](MACHINE_CONTEXT.md). Current inventory: 74 native
verbs and 85 MCP tools; 161 rails and 32 socket types.

## 1. Non-negotiable properties

| # | Property | Meaning for agents |
|---|----------|-------------------|
| G1 | **No silent modes** | Every observation names *what model* produced it (`profile=`, `path=probe_peek`, `cycles=in-command`) |
| G2 | **No plausible-wrong meters** | If a quantity can lie across command boundaries, it is invalid and documented as such (or fixed) |
| G3 | **Discoverability** | An agent can learn the full control surface from the running process (`help`, `manifest`) without reading C++ |
| G4 | **Control completeness** | Anything useful as env/SPIKE capability is reachable from CTRL (or explicitly marked env-only with a reason) |
| G5 | **Manipulate is deliberate** | Hands (`load`/`poke`/`press`/…) are documented as state-changing; acks label them |
| G6 | **Observe does not disturb** | Default reads use `probe_peek` / flat image; exceptions are named |
| G7 | **Dual-mode** | Zero `A2GSPU_*` ⇒ stock windowed emulator; instrumentation never is the default UX tax |
| G8 | **Platform honesty** | IIe DHGR and IIgs SHR are both first-class oracles, never one pretending to be the other. **IIe-focused instrumentation must not regress IIgs** (MMU, banks, native mode, KeyGloo, SHR, ROM size); IIe-green is not ship-green — see [`SHARED_FOUNDATION.md`](SHARED_FOUNDATION.md) |

---

## 2. What “not a black box” requires

For every **question** an agent asks, the answer must include enough metadata to
re-derive trust:

```
Q: What is on the screen?
A: status=OK pngc profile=4dot page=1 560x192 -> out.png

Q: How long did that blit take?
A: status=OK instr=10401 cycles=40040 (in-command) … PC=00:0CF5 …

Q: Did observing it change it?
A: tools/a2rail/unarmedhash.ps1 — armed runs reproduce the unarmed hash and cycle count

Q: What can I do next?
A: help / manifest → full verb + env catalog
```

Black-box anti-patterns (forbidden):

- bare `ok` after an actuator that might have done nothing  
- colour without `profile=`  
- cycles without saying what they span (`(in-command)`, or `model=cross-command` for a mark)
- restore without the snapshot's manifest, or a failed restore that changed the machine
- an observe rail that reads through the CPU's path (`a2obs_peek`, never `mmu->read`)
- SHR golden used as proxy for IIe DHGR correctness  

---

## 3. Control surfaces (all must stay self-describing)

| Surface | Role | Self-description |
|---------|------|------------------|
| **CTRL** `cmd.N`→`ack.N` | Interactive closed loop | `help`, `manifest` |
| **SPIKE** env + `-n` | Batch CI / one-shot verdict | `GSDIAG:` + `A2GSPU_RAILS.md` |
| **Env rails** | Arm traces/watch/assert | catalogued in RAILS; `manifest` lists groups |
| **Windowed co-pilot** | Human drives, agent reads and nudges | `copilot` |
| **Debug socket agent tools** | Shared native CTRL handlers on the machine thread | `MANIFEST`, `AGENT_TOOLS`; invoke with `AGENT_COMMAND` |
| **MCP stdio companion** | Direct tool discovery/calls for AI clients | `initialize`, `tools/list`, `gs2_agent_tools`; native registry plus structured debugger observations, artifact transport and batch adapter |

Windowed co-pilot uses `A2GSPU_COPILOT=<dir>`, a `cmd.N`/`ack.N` rail polled on
the SDL frame thread after each normal frame, so it never races the human from a
worker thread. It answers observe verbs and the controls a user has anyway —
`pause`, `resume`, `key`, `press`, `reset`, `mount`, `quit` — and **refuses** memory
and register splices (`poke`, `load`, `setreg`). It is not read-only, and an
earlier version of this paragraph said it was. Live contract: `copilot`. Full
takeover remains `A2GSPU_CTRL` and is headless.

---

## 4. Capability maturity model

| Level | Definition | Target for closed-loop platform |
|-------|------------|----------------------------------|
| L0 | Hidden / only in source | **None** of mission-critical paths |
| L1 | Documented in markdown | Acceptable interim for rare rails |
| L2 | Listed by live `manifest` | **Minimum** for all CTRL verbs |
| L3 | Scriptable + ack schema stable | Target for run/step/assert/load/pngc |
| L4 | Bidirectional (agent can arm env rails live) | Target for watch/assert/trace |

Current state (2026-09-16): every CTRL verb is L2 (`railparity.ps1` fails a verb
the dispatcher answers that `verbs[]` does not list, and the reverse); the daily
loop verbs (`run`, `step`, `run-until`, `assert`, `load`, `pngc`) have stable,
documented acks (L3); every env rail is L2 (registry, `rail find`, a catalogue row
checked by `railparity.ps1`). L4 exists for `watch bus`, `valtrap`, `itrace`,
`tbtrace`, `callstream`, `cov`, `memvu`, `shr-golden` and `dhgr-golden`; the rest of
the 144 rails are launch-only, and `rail live` lists exactly which reach a verb.

**2026-10-15 inventory correction:** rail parity now measures 160 rails (157
before P0), 67 CTRL verbs (66 before P2.1) and 70 live-settable rails. The older counts above
describe the earlier inventory. Launch-only rails can still have registered
readback; consult the reach table and `rail live`, rather than inferring control
from a rail's presence in that table.

**2026-10-15 interface work:** 72 CTRL verbs after `inspect`, `capture`, `fault`, `input` and
`subscribe`; rail/setter counts remain 160/70. Native inspection exports typed
CPU/device/signal/capability data with explicit coverage and availability.
Ordered JSONL subscriptions cover the central record writer and selected
console/control events, with loss counters. This is a measured subset; the
active interface-completeness report tracks remaining semantic/control/replay
gaps. Runtime RTC snapshot guidance now agrees with the per-file manifest.

**2026-10-15 tool integration:** 73 verbs after `ui`, 32 socket types after
AGENT_TOOLS/AGENT_COMMAND, 161 rails after the explicit `A2GSPU_AGENT` bootstrap
mode, and initially 75 generated MCP tools including native catalog and isolated batch
discovery. The file rail and socket invoke one dispatcher. Useful menu settings
have native state, availability and effect receipts. The stdio companion can
launch an owned machine with shared CTRL startup initialization and registered
interactive rails, run separate one-shot batches, or attach without owning
its lifetime. It does not install an AI application's configuration. Launch-only
rails and unsupported physical host flows remain explicit, rather than being
advertised as live controls or complete GUI proof.

**2026-10-15 upstream observation audit:** 79 MCP tools after four structured
debugger adapters (trace, memory-domain reads, masked search and native text
page metadata). Reads report unreadability, searches exclude I/O placeholders,
trace clock deltas refer only to adjacent retained records, and extended trace
text is repeatable without a fixed-speed scanline estimate. See
`reports/gssquared-upstream-observability-20261015/README.md` for the upstream
main/development-branch comparison and validation.

**2026-10-15 autonomous feature-shape pass:** 82 MCP tools after scoped
create-once input upload, artifact discovery and digest-bound evidence reads,
including native PNG image content. Socket timeouts preserve partial framing
and the original request; wait recovery never resends. Execution validates
before effects and distinguishes completed goals from bounded work. The native
73-verb interface inventory and remaining model/shape limits are in
`reports/gssquared-autonomy-shape-20261015/README.md`.

---

## 5. Observation models (named — never implicit)

| ID | What it answers | CTRL / rail |
|----|-----------------|-------------|
| `bits-mono` | 1-bit HGR/DHGR dots | `png` |
| `colour-4dot` | discrete DHGR 4-dot RGB | `pngc … 4dot` |
| `colour-ntsc560` | FIR composite LUT | interactive + `dhgr-export` |
| `vram-raw` | 16K aux‖main bytes | `vram` |
| `text-page` | $0400 dump | `text` |
| `softswitch` | video mode bits | `vid` |
| `cpu-state` | PBR:PC regs halt kbd | `cpu` |
| `cycles-in-cmd` | guest cycles **during** run/step/run-until | those acks only |
| `shr-window` | IIgs SHR hash/map | SPIKE GOLDEN / VIDEOMAP |
| `shr-linear` | IIgs SHR framebuffer as PNG, per-line 320/640 by SCB | `shr` |
| `hgr-ascii-coarse` | "is anything drawn" density map of an HGR page, not pixel-exact | `hgr` |
| `sdl-human` | the SDL backbuffer, for a person to look at — never an oracle | `shot` |
| `cross-command` | cycles since a `cycles reset` mark, across every command in between | `cycles` (`delta_from_mark=`) |
| `memory-peek` | side-effect-free byte | `read`, ASSERT `peek:`, every observe rail (`a2obs_peek`) |

Agents must pick a model. Toolchain recipes pin models in scripts.

---

## 6. Manipulation models (named)

| ID | Effect | Notes |
|----|--------|-------|
| `inject-ram` | `load` / `poke` / RUN_BIN | does not auto-run unless said |
| `inject-key` | `key` (with consume receipt), `press`, `keys` | prefer `key` when proof matters |
| `inject-media` | `mount` | host file side effects |
| `time-travel` | `save`/`restore` | Class **derived per file**, not fixed: `A` cpu+mmu+softswitches+RAM, `B` adds video/scanner, `C` adds ADB+Ensoniq+IWM. A IIgs `save` writes `adb, ensoniq, vgcint, iwm, pdblock` today, so it is not Class A. The `devices=`/`included=`/`excluded=` lists in the file's own manifest are the contract; the letter is a summary of them. The `restore` ack carries the manifest text verbatim — read it, do not assume it |
| `force-control` | `setreg`, `bp`, `reset` | label in ack |

---

## 7. Live discovery (required verbs)

| Verb | Purpose |
|------|---------|
| `help` | short verb list |
| `help <file>` | full verb catalog written to file |
| `manifest <file>` | machine-oriented capability dump (verbs + env groups + models + contracts) |
| `oracle` | one-screen north-star + cycle/colour rules |
| `protocol` | version/framing/ownership/JSON contract |
| `systems [file]` | running binary's platform/config/ROM/device inventory |
| `limitations [file]` | machine-readable known fidelity gaps |

If an agent cannot discover a capability without reading this repo, that capability
is still a black box — fix discovery, not the agent.

The contract is deliberately agent-vendor-neutral: atomic files, UTF-8 text,
JSON/NDJSON, numeric exit codes, and host paths only. ChatGPT/Codex, Grok, and
Claude Code can all negotiate `protocol`, consume `json <command>`, and retain
the `a2rail-event-v1` transcript without a proprietary SDK or prompt-specific
parser. Human-readable replies remain useful diagnostics, not the sole API.

---

## 8. Related docs

- [`SHARED_FOUNDATION.md`](SHARED_FOUNDATION.md) — multi-consumer contract (product-aware, product-agnostic)
- [`A2GSPU_RAILS.md`](A2GSPU_RAILS.md) — env rail catalog (batch)  
- [`INSTRUMENTATION_AUDIT.md`](INSTRUMENTATION_AUDIT.md) — first-principles audit of gaps  
- [`CLOSED_LOOP_INTEGRATION.md`](CLOSED_LOOP_INTEGRATION.md) — Rosetta harness mission  
- [`DoubleHiRes.md`](DoubleHiRes.md) — DHGR colour/bit contracts  

---

## 9. Success criteria (platform ready for ideal closed loop)

| # | Criterion | State (2026-09-16) |
|---|-----------|--------------------|
| 1 | Cold agent session: `oracle` + `manifest` → can operate without external wiki | met: `manifest <file>` carries verbs, rail families, reach and record schemas |
| 2 | No recipe uses cross-ack cycle subtraction | met in the fork's own gates; a mark delta is labelled `model=cross-command` |
| 3 | Every image ack carries `profile=` | met: `png`, `pngc`, `vram`, `shr`, `hgr`, `shot`, `dhgr-golden` |
| 4 | IIe DHGR path has goldens/assert recipes peer to SHR | met: `dhgr-golden` verb and `A2GSPU_DHGR_GOLDEN` gate |
| 5 | Env-only rails either gain CTRL or are declared launch-only | met by declaration: the reach table (`rail live`), generated into the manifest's `env_only_rationale` |
| 6 | Dual-mode gate still holds (no A2GSPU ⇒ stock UX) | the headless half is mechanical (`unarmedhash.ps1`); the windowed no-environment check is still by hand |

This document is the bar. Implementations are judged against it.

## 10. Design review for architectural understanding

**Owner direction, 2026-10-16:** maximal instrumentation serves agent reasoning
about how software should be shaped. Historical code is evidence to understand
and challenge. Familiarity, archive frequency and a successful run do not
establish an efficient implementation. The paired design contract is
[`SHARED_FOUNDATION.md`](SHARED_FOUNDATION.md#corpus-informed-architectural-refinement-owner-direction-2026-10-16).

These are review requirements for new or revised features, not a claim that
every existing model already exposes everything below. Enforcement is by
review, supplemented by the named regression gates for implemented surfaces.

| Design question | Required shape and evidence |
| --- | --- |
| Can an agent explain the result? | Expose relevant state, transitions, mappings, device dependencies and source/model references through discoverable typed interfaces. Identify unsupported fields, unreadable bytes and absent devices explicitly. A hidden dependency is an observability gap to record and address. |
| What actually caused it? | Distinguish observed events, dynamic nesting, sampled changes and inferred data flow. Identify instruction/bus/command/frame boundaries, clock domains, ordering, retention loss and reset/session identity as applicable. A parent event ID alone is not proof of a value's last writer. |
| Can the explanation be challenged? | Pair inspection with explicit controls for bounded execution and relevant input/state/fault interventions, reporting the intervention and completion reason. Use fresh comparable sessions where restore/replay is incomplete; never invent replay precision. |
| Can CIDAR and Rosetta join the evidence? | Retain artifact/tool identity, loaded address/segment mapping and available source provenance so observations can be joined to a workload and candidate. Keep primitive IDs, product policy and source interpretation in companions, without embedding archive or product maps in the native emulator. |
| Is the measured improvement meaningful? | Name workload, machine/configuration, memory placement, CPU mode, initial conditions, measurement interval and clock units. Report guest cycles/instructions/bytes and relevant latency/resource effects separately from host capture/transport cost. Missing channels stay unavailable rather than becoming zero. |
| Could instrumentation have changed the answer? | Check guest behavior and timing with observation armed and unarmed on the applicable IIe/IIgs paths; disclose sampling omissions and host overhead. Instrumentation must not manufacture the architectural gain it reports. |
| Does the claim exceed the evidence? | Preserve passing reference cases, failing negative controls, boundary/adversarial coverage and counterexamples. State tested-domain bounds separately from sampled best/worst cases; emulator evidence alone proves neither a global optimum nor physical-IIgs equivalence. |

Prefer additions that let an agent resolve a concrete uncertainty and falsify
its explanation over additions that merely increase field or tool counts.
Offer bounded queries, selective capture and durable expanded artifacts so
maximal exposure remains usable within an agent's context and execution budget.
Any bounded view must expose its truncation, sampling and availability limits.

The next integration work is tracked in `AGENTIC_ROADMAP.md`; the current
`context` and experiment interfaces remain the implemented subset described in
`MACHINE_CONTEXT.md`. Complete data flow, all device transitions and physical
hardware validation remain open.

## 11. Future IIgs+ design exploration

**Owner direction, 2026-10-16:** after this foundation reaches production
quality, it may support a parallel hobby IIgs+ clone-mainboard project:
identify pressure points in the ROM 1/3/4 configurations under study, model
proposed corrections, validate software behavior and eventually consider a
short PCB fabrication run. This is a future target, not an implemented machine
profile or a board-design/fabrication task in the current queue.

The instrument should make that exploration possible without weakening its
role as an oracle for existing machines. Apply these review rules:

| Question | Design requirement |
| --- | --- |
| Which machine produced the result? | Preserve stock reference profiles. A hypothetical change needs a separate explicit model/configuration identity, revision and declared differences; never silently improve the reference machine. Bind experiments, captures and snapshot compatibility to that identity. |
| What limitation is being addressed? | Retain a minimal reproducer, affected software, relevant bus/device/clock evidence and the measured cost. Separate a modeled hardware constraint, ROM/software policy and an emulator defect before proposing a correction. |
| What must remain compatible? | State whether existing binaries run unchanged, need a ROM/software patch, or require an enhanced-machine target. Test reset/boot, memory and I/O mapping, timing-sensitive behavior, interrupts, DMA/bus ownership where modeled, and affected peripherals under a named compatibility tier. |
| Does the proposed change help? | Use the same workload and declared input bytes on reference and candidate profiles. Measure latency, cycles, throughput and resource use in the applicable domains; preserve regressions and counterexamples as well as gains. Explore interactions after isolated changes pass. |
| Can it become a board contract? | Retain proposed register/bus behavior, timing assumptions and unmodeled behavior in a machine-readable specification that can later drive HDL or hardware tests. Emulator observations validate the implemented model; electrical timing, signal integrity, power and physical implementation require separate evidence. |

Rosetta/CIDAR can supply recovered workloads, compatibility contracts and
alternative software forms. Keep enhanced-machine targeting explicit and
retain tests for existing IIgs deployments. Do not optimize libraries for an
imagined machine while presenting the result as a stock-machine gain.

Before a hardware-design phase, establish a model-quality ledger and a
repeatable compatibility campaign, then select a bounded candidate change.
Actual board work belongs to a separately started parallel project. Existing
drift, baseline re-bless and model-merge decisions remain unchanged.
