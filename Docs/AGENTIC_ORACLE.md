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
| G8 | **Platform honesty** | IIe DHGR and IIgs SHR are both first-class oracles, never one pretending to be the other |

---

## 2. What “not a black box” requires

For every **question** an agent asks, the answer must include enough metadata to
re-derive trust:

```
Q: What is on the screen?
A: pngc 4dot … profile=4dot source=ram-aux|main path=a2dhgr phase_offset=1

Q: How long did that blit take?
A: run-until … cycles=39580 (IN-COMMAND ONLY; do not diff cycles across acks)

Q: What can I do next?
A: help / manifest → full verb + env catalog
```

Black-box anti-patterns (forbidden):

- bare `ok` after an actuator that might have done nothing  
- colour without `profile=`  
- cycles without saying they are in-command deltas  
- restore without snapshot class  
- SHR golden used as proxy for IIe DHGR correctness  

---

## 3. Control surfaces (all must stay self-describing)

| Surface | Role | Self-description |
|---------|------|------------------|
| **CTRL** `cmd.N`→`ack.N` | Interactive closed loop | `help`, `manifest` |
| **SPIKE** env + `-n` | Batch CI / one-shot verdict | `GSDIAG:` + `A2GSPU_RAILS.md` |
| **Env rails** | Arm traces/watch/assert | catalogued in RAILS; `manifest` lists groups |
| **Windowed co-pilot** | Human drives, agent reads | observe verbs work without `-n` |

---

## 4. Capability maturity model

| Level | Definition | Target for closed-loop platform |
|-------|------------|----------------------------------|
| L0 | Hidden / only in source | **None** of mission-critical paths |
| L1 | Documented in markdown | Acceptable interim for rare rails |
| L2 | Listed by live `manifest` | **Minimum** for all CTRL verbs |
| L3 | Scriptable + ack schema stable | Target for run/step/assert/load/pngc |
| L4 | Bidirectional (agent can arm env rails live) | Target for watch/assert/trace |

Current state is mixed L1–L3. The program of work is to eliminate L0 and raise
CTRL to L3 for daily loops, L4 for high-value env rails.

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
| `memory-peek` | side-effect-free byte | `read`, ASSERT `peek:` |

Agents must pick a model. Toolchain recipes pin models in scripts.

---

## 6. Manipulation models (named)

| ID | Effect | Notes |
|----|--------|-------|
| `inject-ram` | `load` / `poke` / RUN_BIN | does not auto-run unless said |
| `inject-key` | `key` (with consume receipt), `press`, `keys` | prefer `key` when proof matters |
| `inject-media` | `mount` | host file side effects |
| `time-travel` | `save`/`restore` | Class A: CPU+MMU only |
| `force-control` | `setreg`, `bp`, `reset` | label in ack |

---

## 7. Live discovery (required verbs)

| Verb | Purpose |
|------|---------|
| `help` | short verb list |
| `help <file>` | full verb catalog written to file |
| `manifest <file>` | machine-oriented capability dump (verbs + env groups + models + contracts) |
| `oracle` | one-screen north-star + cycle/colour rules |

If an agent cannot discover a capability without reading this repo, that capability
is still a black box — fix discovery, not the agent.

---

## 8. Related docs

- [`A2GSPU_RAILS.md`](A2GSPU_RAILS.md) — env rail catalog (batch)  
- [`INSTRUMENTATION_AUDIT.md`](INSTRUMENTATION_AUDIT.md) — first-principles audit of gaps  
- [`CLOSED_LOOP_INTEGRATION.md`](CLOSED_LOOP_INTEGRATION.md) — Rosetta harness mission  
- [`DoubleHiRes.md`](DoubleHiRes.md) — DHGR colour/bit contracts  

---

## 9. Success criteria (platform ready for ideal closed loop)

1. Cold agent session: `oracle` + `manifest` → can operate without external wiki  
2. No recipe uses cross-ack cycle subtraction  
3. Every image ack carries `profile=`  
4. IIe DHGR path has goldens/assert recipes peer to SHR  
5. Env-only rails either gain CTRL or are tagged `env-only:` with rationale in manifest  
6. Dual-mode gate still holds (no A2GSPU ⇒ stock UX)  

This document is the bar. Implementations are judged against it.
