# Agentic oracle — north-star contract for gssquared

**Goal:** nothing in gssquared is a black box to agentic AI. Agents have
**maximal control**, **maximal observability**, and **named, fail-loud
understanding** of every answer the emulator gives — so closed-loop toolchain
development (Rosetta, a2engine, a2tile, …) can treat this fork as a trustworthy
hands-and-eyes platform.

This is not “more debug prints.” It is a **product definition**.

---

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
