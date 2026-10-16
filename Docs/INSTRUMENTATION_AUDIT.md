# First-principles audit — A2GSPU instrumentation & hooks

**Date:** 2026-07-31  
**Scope:** env rails, CTRL rail, spike, hooks into CPU/MMU/video, dual-mode invariant  
**Audience:** agentic closed-loop (Rosetta + a2engine + a2tile), co-pilot mode, dual-mode human use  

This is an audit, not a changelog. Claims are checked against `A2GSPU_RAILS.md`,
`CLOSED_LOOP_INTEGRATION.md`, live `gs2.cpp` verbs, and known failures recorded in
a2engine `ARCHITECTURE.md` (cycle rail).

---

## Closure (added later — the body below is the dated 2026-07-31 snapshot)

**Read this first if you were handed this file as current.** It is not. The body
is a snapshot and its value is that it is one; do not rewrite its findings. But
`AGENTS.md`'s reading list and `INSTRUMENTATION_SHAPE.md`'s "Builds on" header
both present it without saying what has since closed, and most of §5's programme
has. An agent taking §5 as a work list sets out to build things that exist; an
agent reading §2's scorecard concludes the interactive surface is skeletal when
it already had 65 CTRL verbs at that closure (74 as of 2026-10-16, with live
discovery authoritative). The cost is wasted work
and mis-set expectations, which is why this section is here and the body is not
touched.

| §5 item | State | Closed by |
|---------|-------|-----------|
| P0.1 cycles contract + `cycles=` on run/step/run-until | **done** | all three acks carry `cycles=N (in-command)`; the rule is in `A2GSPU_RAILS.md`, `oracle` and every `help` row |
| P0.2 profile field on image rails | **done** | `png`/`pngc`/`vram`/`dhgr-golden` acks carry `profile=` |
| P0.3 stale-doc quarantine | **done** | root `A2GSPU_INSTRUMENTATION.md` is a 15-line SUPERSEDED pointer |
| P1.4 CTRL `assert` | **done** | `assert peek:…` verb, `status=PASS\|FAIL` |
| P1.5 CTRL `watch` | **done** | `watch add\|list\|sample\|clear` and live `watch bus` |
| P1.6 `run-bin` / IIe load recipes | **partial** | `load`/`poke`/`setreg` are live verbs; there is no `run-bin` verb — the SPIKE `A2GSPU_RUN_BIN` rail is still the only form |
| P1.7 DHGR golden rail | **done** | `dhgr-golden` verb + `A2GSPU_DHGR_GOLDEN` SPIKE gate with a named profile |
| P2.8 split the CTRL dispatcher out of `gs2.cpp` | **done** | the CTRL loop and co-pilot rail are `src/a2gspu_ctrl.cpp`, the verb families `src/a2gspu_ctrl_cmds.cpp`, the spike `src/a2gspu_spike.cpp`, snapshots `src/a2gspu_snapshot.cpp`, micro-tests `src/a2gspu_selftests.cpp`; `gs2.cpp` is the application again. `railparity.ps1` finds verbs by `A2CTRL-DISPATCH` markers, not by file name |
| P2.9 verb registry auto-listed by `help` | **done** | `verbs[]` in `a2gspu_manifest.hpp` drives `help`, `help <file>` and `manifest <file>` identically, and `railparity.ps1` now fails a verb the dispatcher answers but the table omits |
| P2.10 platform-matrix ctest | **partial** | `dualsmoke.ps1` runs railcheck + exec + ALTZP on IIe **and** IIgs, and `unarmedhash.ps1` pins a IIe and three IIgs boots; native CTests also include snapshot and interface contracts. `tools/a2rail/gates.ps1` runs the fork suites |
| P3.11 `A2GSPU_PROFILE` preset bundles | **open** | no such rail |
| P3.12 batch `script` verb | **open** (and arguably a non-goal — `session.ps1`'s header argues against batching the rail) | |
| P3.13 snapshot classes documented and expanded | **partial** | supported blocks now include scanner, storage/media, RTC/ThunderClock, serial/network and several cards. Session clocks, host connections and some synthesis state remain excluded; the file manifest is authoritative — see `INSTRUMENTATION_SHAPE.md` M5 |

§2's scorecard is likewise 2026-07-31. Two of its stated reasons have since
gone: "root `A2GSPU_INSTRUMENTATION.md` stale" (P0.3) and the D for CTRL↔env parity.
For the current state read the **Status** paragraph of each of M1–M12 in
[`INSTRUMENTATION_SHAPE.md`](INSTRUMENTATION_SHAPE.md), which is the document
that carries the live picture.

---

## 0. What “first principles” means here

An instrumented emulator is not “a debugger bolted on.” For this fork it must be:

1. **An oracle** — answers about guest state are true under a named observation model  
2. **Hands** — deliberate, documented manipulation when the agent needs it  
3. **Orthogonal to UX mode** — telemetry works headless *and* windowed; OFF by default  
4. **Compositional** — rails combine without hidden coupling  
5. **Fail-loud** — instruments that can return a plausible wrong answer are worse than absent  

If a rail violates (1) or (5), it actively corrupts agent reasoning.

---

## 1. Design that is correct (keep and protect)

### 1.1 Three hard invariants (CLOSED_LOOP)

| Invariant | Verdict |
|-----------|---------|
| Additive + env-gated; no `A2GSPU_*` ⇒ stock UX | **Sound.** Dual-mode gate is real. |
| Isolated/separable hooks for upstream sync | **Sound.** Device + `gs2` loops + thin taps. |
| Observe-don’t-disturb for observe/trace/break/assert | **Sound as policy**; see §3 for holes. |

These should remain non-negotiable. Any new rail that runs without env is a regression.

### 1.2 Dual control surfaces with different jobs

| Surface | Job | Strength |
|---------|-----|----------|
| **SPIKE** (`SPIKE_FRAMES` + `-n`) | One-shot boot→verdict, CI, goldens | Exit code + `GSDIAG:` is harness-friendly |
| **CTRL** (`cmd.N`→`ack.N`) | Multi-step interactive agent loop | Pause, inject, inspect, resume |

Splitting “batch oracle” from “interactive microscope” is right. Do not merge them into one mega-API.

### 1.3 `probe_peek` / side-effect-free observation (when used)

Reading guest state without firing `$C0xx` is the difference between a logic analyzer and
a probe that collapses the waveform. The rails that document `probe_peek` for E0/E1 and
text dumps are doing the right thing. **Policy:** every new *observe* path must state
whether it is side-effect-free; default must be free.

### 1.4 Named machine-readable verdict

`GSDIAG: status=… rc=… gate=…` collapses a long run into something a harness parses.
Taxonomy (`OK`, `GATE_FAIL`, `CRASH_BRK`, `HANG`, …) beats “exit 1 somewhere.”

### 1.5 Manipulation is first-class (not only observe)

`POKE`, `RUN_BIN`, CTRL `load`/`poke`/`press`/`keys` match the mission: the agent has hands.
The critical discipline is **labelling** deliberate splices (already done for some pokes).

### 1.6 Provenance traps (WATCH / VALTRAP / CONDTRAP)

“Who wrote address X / value V / flag F” is the right abstraction for RE and toolchain
bugs. These are high-leverage and should stay thin and side-effect-free on the guest bus.

### 1.7 Determinism knobs

`SEED` + `FAKETIME` + RAMDISK are the minimum for golden hashes. Without them goldens lie.

### 1.8 Recent DHGR colour rails (pngc / dhgr-export / dhgr-calibrate)

Align with the art stack’s need for **named colour profiles** rather than SDL screenshots.
That is instrumentation improving the oracle, not feature creep—if documented as profiles.

---

## 2. Architecture scorecard

*Scored 2026-07-31 and not rescored since — see **Closure** above for what has
changed, and `INSTRUMENTATION_SHAPE.md`'s M1–M12 Status paragraphs for the
current state.*

| Principle | Score | Notes |
|-----------|-------|-------|
| Env-gated default OFF | A | Core promise |
| Observe ≠ manipulate | B+ | Mostly clear; some grey areas (§3.2) |
| One observation model per rail | C | Video/colour multi-headed; clocks multi-headed |
| CTRL ↔ env parity | D | Large env surface; CTRL incomplete; docs lag |
| Fail-loud instruments | C | Cycle bracket historically lied; soft fails elsewhere |
| IIe vs IIgs first-class | C | Heavy IIgs bias; a2engine is IIe DHGR |
| Doc as contract | B- | `A2GSPU_RAILS.md` good; root `A2GSPU_INSTRUMENTATION.md` stale |
| Composition safety | C | Combinatorial explosion of rails; little schema |
| Upstream-merge cost | B | Separable, but `gs2.cpp` is a god-file |
| Agentic latency | C | File poll 20 Hz is OK; giant ack texts / shot BMPs less so |

---

## 3. Failures and tensions (ordered by severity)

### 3.1 CRITICAL — Instruments that can lie

#### A. Cross-command cycle accounting (measured in a2engine)

Bracketing `cycles` across CTRL commands once returned:

- jumps of exactly 6_000_000  
- **backwards** clock  
- while in-command `step`+`cycles` agreed with ISA timing  

**First principle violated:** measurement must be *inside* the same atomic run unit as the
work.  
**Status (2026-07-31):** partially fixed for `run-until` (cycles reported inside the call).  
**Status now: closed.** `run`, `step` **and** `run-until` all report `cycles=N
(in-command)` inside the call; `cycles` on its own is documented and acked as an
absolute counter whose cross-ack deltas are not valid timing.  
**Still required (as written in 2026-07-31):**

- Document that **only** cycles reported *inside* `run` / `run-until` / `step` are valid  
- Forbid / warn harnesses that diff `cycles` across `ack` boundaries  
- Single monotonic guest-cycle counter, never host wall or mixed clocks, never reset mid-session without an explicit `cycles reset` verb  

#### B. Multi-headed video observation

| Path | Model | Safe for |
|------|-------|----------|
| `png` | mono dots from RAM | bit/layout |
| `pngc 4dot` | discrete 4-dot | art seams (named) |
| `shot` | SDL renderer | human eyeball only |
| `spike_frame.bmp` | interactive render path | golden of *that* path only |
| `VIDEOSUM` / `VIDEOMAP` | **SHR / IIgs** | GS titles |

**First principle:** an oracle answer must name its model.  
**Failure mode:** agent optimizes tiles against mono `png` or SDL shot.  
**Fix policy:** every visual rail returns `profile=` in the ack (pngc already names profile);
deprecate “colour from shot” in agent recipes.

#### C. `probe_peek` vs flat image vs live RAMRD

Documented carefully for `vram`/`png` (IIe aux at +$10000). Easy to reintroduce:

- `read` through wrong path on wrong platform  
- toggling RAMRD to “see” aux (disturbs guest)  

**Gate:** platform matrix tests: IIe DHGR aux dump == flat image; IIgs bank read == MMU map.

#### D. Always-on SPIKE noise vs “no env = stock”

Rails doc says always-on SPIKE lines (`SPIKE E1:`, files, `GSDIAG`) when spike runs.
That is fine **inside** a spike. Ensure:

- pure windowed launch with **zero** A2GSPU env never prints SPIKE  
- SPIKE always-on artifacts are not confused with “instrumentation off”  

(If any SPIKE file write happens without SPIKE_FRAMES, that is a bug.)

---

### 3.2 HIGH — Observe/manipulate boundary erosion

| Pattern | Risk |
|---------|------|
| `shot` updates display | May force render work; usually OK, not pure observe |
| `press` / `keys` | Documented manipulate — good |
| `mount` | Persist side effects on host media — must be explicit |
| `POKE` / `load` | Deliberate; label in ack — good where done |
| Snapshot exclude devices | Restore “self-heals” — can look like flaky guest |

**First principle:** restore is not time travel unless the snapshot domain is complete.
Document **snapshot completeness class**:

- Class A: CPU+MMU+softswitches (current)  
- Class B: + scanner/video mode  
- Class C: + devices (disk, ADB)  

Agents must know class A is “quiescent prompt only.”

---

### 3.3 HIGH — Dual surface asymmetry (env vs CTRL)

Rough inventory:

- **~80+ env rails** in `gs2.cpp` getenv list alone (more in devices/taps)  
- **~35 CTRL verbs** including recent dhgr/pngc/load/…

Many powerful env rails have **no CTRL equivalent**:

WATCH, VALTRAP, TBTRACE, ITRACE, ASSERT mid-session, GOLDEN, RUN_BIN mid-session,
MILESTONES, CALLSTREAM, COVERAGE, …

**First principle:** interactive closed loop needs the same *capability set* as batch,
or the agent must relaunch (lossy, slow, non-compositional).

**Recommended shape (not all implemented):**

```
CTRL: rail set <NAME>=<val>     # arm an env-class rail live
CTRL: rail list
CTRL: rail clear
```

Or promote the highest-value env rails to first-class verbs first:

1. `watch` / `unwatch`  
2. `assert` (one-shot check now)  
3. `ittrace` / `tbtrace` on|off  
4. `run-bin`  
5. `golden` check  

---

### 3.4 HIGH — Platform bias (IIgs-centric oracle, IIe consumer)

Much of the golden/spike surface is **SHR / GS/OS / Tool Locator**:

- `GOLDEN` = SHR window hash  
- `VIDEOMAP` / `VIDEOSUM` = SCB/SHR  
- `RUN_BIN` default `$7E0000`  
- TBTRACE toolbox  

**a2engine + a2tile** need **IIe DHGR** as a first-class oracle path:

- DHGR golden (aux‖main bits or named 4dot hash)  
- Softswitch assert: `DHIRES && 80COL && !80STORE` patterns  
- Cycle-accurate blit budgets already depend on CTRL  

Without IIe-class goldens, the fork’s mission for Rosetta GS is strong and for a2engine
is accidental.

---

### 3.5 MEDIUM — God-file / hook placement

`a2gspu_ctrl_loop` + spike parse in `gs2.cpp` is enormous. Documented silent `-O3`
lambda failure is a smell: **the instrumentation control plane is too centralized.**

**First principles for hooks:**

| Layer | Should hold |
|-------|-------------|
| CPU execute path | thin: break, itrace, watch, retguard |
| MMU read/write | thin: watch, valtrap, probe |
| Video scanner | mode sample, optional frame hash |
| Control plane | verb dispatch, session, recipes |
| Devices | self-contained env rails |

**Improvement:** move CTRL verb table to `a2gspu_ctrl_*.cpp`; keep `gs2` as wiring only.
Reduces merge pain and compile-time landmines.

---

### 3.6 MEDIUM — Documentation debt

| Doc | Issue |
|-----|--------|
| `A2GSPU_RAILS.md` | Best contract; must stay canonical |
| `A2GSPU_INSTRUMENTATION.md` | Stale “gaps”; actively misleads if read first |
| CTRL verb list in RAILS | Missing newer verbs: `step`, `run-until`, `vid`, `stack`, `png`, `pngc`, `vram`, `load`, `cycles`, `dhgr-*`, … |

**First principle:** one catalog. Stale catalogs are false instruments.

**Action:** RAILS is sole catalog; root INSTRUMENTATION becomes a pointer + history.
CTRL table regenerated from code or a static assert list of verb strings.

---

### 3.7 MEDIUM — File-based CTRL protocol

cmd.N / ack.N at ~20 Hz is:

- robust on Windows  
- easy for PowerShell agents  
- high latency for tight step loops  
- racy if two writers  

**First principles:**

- Correctness > speed for now  
- But `step 1` × 10_000 should not require 10_000 file round-trips if avoidable  

**Optional evolution:**

- batch verb: `script <file>` multi-line  
- or named pipe / localhost JSON-RPC **as additive** surface, same verb set  

Do not replace files until a second surface shares one dispatcher.

---

### 3.8 MEDIUM — Combinatorial composition

Example: WATCH + ITRACE + TBTRACE + TAP_SESSION + COVERAGE + GOLDEN.

No schema for:

- ordering of end-of-run reports  
- disk space blowups  
- interaction of STOP_ON_FAULT with CTRL  

**First principle:** composition should be either free or explicitly forbidden.

**Action:** `A2GSPU_PROFILE=ci|re|art|perf` presets that turn on coherent bundles;
document incompatible pairs (if any).

---

### 3.9 LOW — Naming and taxonomy

- `A2GSPU_*` mixes **env rails**, **card protocol constants**, **file magics**  
- RAILS already notes card constants — good  
- CTRL verbs mix spaces (`iolog dump`) and hyphen (`run-until`, `dhgr-export`)  

Not fatal; pick a grammar and stick to it for new verbs: `family-action` or `family action`.

---

### 3.10 LOW — Co-pilot orthogonality (claimed vs tested)

Spec says telemetry works with window open + human input.  
Likely true for many observe verbs; less tested for:

- `press` racing human keyboard  
- `shot` while window resized  
- SPIKE vs CTRL mutual exclusion  

**Action:** one automated co-pilot smoke: windowed + CTRL `cpu`/`read` while idle.

---

## 4. What a2engine / a2tile actually need from this surface

| Need | Have | Gap |
|------|------|-----|
| Inject bins at $0800/$6000 | `load`, SPIKE RUN_BIN (GS default) | IIe recipes first-class |
| Pause CPU between commands | CTRL | OK |
| Key inject with proof of consume | `press` / keys + cpu | document poll patterns |
| Bit-true DHGR dump | `vram`, `png` | OK |
| Named colour dump | `pngc` | OK after fidelity pass |
| Softswitch truth | `vid` | OK |
| Cycle cost of a routine | `run-until` internal cycles | document only; no cross-cmd |
| DHGR golden in CI | SHR golden only | **missing** |
| Assert softswitches / peeks | ASSERT env | CTRL one-shot assert missing |
| Non-disturbing mem read | probe_peek paths | platform tests |

---

## 5. Recommended program of work (priority)

### P0 — Stop lying

1. **Cycles contract** written into RAILS + CTRL help: only in-command deltas are valid; add `cycles` field to every `run`/`step`/`run-until` ack.  
2. **Visual profile field** mandatory on every image rail ack.  
3. **Stale doc quarantine:** A2GSPU_INSTRUMENTATION.md → “see RAILS”.  

### P1 — Close agent loops without relaunch

4. CTRL: `assert <expr>` (reuse ASSERT parser).  
5. CTRL: `watch` / `unwatch` (subset of WATCH).  
6. CTRL: `run-bin` / IIe-friendly load recipes.  
7. **DHGR golden** rail: hash aux‖main page or 4dot PNG FNV under SEED.  

### P2 — Structure

8. Split CTRL dispatcher out of `gs2.cpp`.  
9. Verb registry with help strings auto-listed by `help` CTRL verb.  
10. Platform matrix ctest: IIe DHGR vram/pngc/vid; IIgs SHR golden.  

### P3 — Scale

11. Preset bundles `A2GSPU_PROFILE=…`.  
12. Optional batch `script` verb.  
13. Snapshot completeness classes documented and optionally expanded (scanner state).  

---

## 6. Explicit non-goals

- Making instrumentation always-on (breaks dual-mode)  
- Replacing file CTRL before a second surface shares one registry  
- Full device state in every snapshot (expensive; wrong default)  
- One universal colour model (named profiles forever)  
- Merging SPIKE and CTRL into one entrypoint  

---

## 7. Verdict

The instrumentation design is **strategically correct** and **tactically overgrown**:

- The **mission model** (oracle + hands, env-gated, dual-mode, SPIKE vs CTRL) is first-principles sound and rare among emulators.  
- The **implementation** succeeded by accretion: many rails, uneven CTRL parity, IIgs-weighted goldens, and at least one class of **plausible-wrong** measurement (cycles) that taught the right lesson.  
- The **highest ROI** is not more rails — it is **contract honesty** (what each answer means), **CTRL/env parity for the closed loop**, and **IIe DHGR as a first-class golden path** beside SHR.

Protect the three invariants. Fix lying instruments first. Then make the interactive surface as complete as the env surface for the workflows we actually run (a2engine, Rosetta value-harness, a2tile validation).

---

## 8. Appendix — CTRL verbs observed in `gs2.cpp` (2026-07-31)

`boot`, `bp`, `cov`, `cpu`, `cycles`, `dhgr-calibrate`, `dhgr-export`, `dis`, `hgr`,
`holdkey`, `iolog` (+ on/off/dump/reset), `key`, `keys`, `load`, `mount`, `png`, `pngc`,
`poke`, `press`, `quit`, `read`, `reset`, `restore`, `run`, `run-until`, `save`, `screen`,
`setreg`, `shot`, `stack`, `step`, `text`, `vid`, `vram`

**Missing from RAILS CTRL table at audit time:** several of the above (including
`step`, `run-until`, `vid`, `stack`, `png`/`pngc`, `vram`, `load`, `cycles`, `dhgr-*`).
Catalog refresh is part of P0.

**Closed.** The refresh was applied to the verbs this appendix names in *prose*
and not to the list beside them, so `boot`, `bp`, `cov`, `hgr`, `reset` and
`screen` — all six printed above — stayed out of the RAILS CTRL table for
six weeks, along with thirteen verbs added since. All 65 now have a row (`json`
got its own later), the
`run` row no longer states a reply format the binary cannot emit, and
`railparity.ps1` fails a verb the CTRL dispatcher answers that `verbs[]` does
not list. `help` from the running process remains the authority for the *set*;
the table carries the semantics.
