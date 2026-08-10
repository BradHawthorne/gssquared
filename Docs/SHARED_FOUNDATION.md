# Shared foundation — multi-consumer contract

This fork of gssquared is a **shared Apple II / IIgs emulator and agentic
oracle**, not the private test harness of any single product. The CTRL rail,
SPIKE/headless gates, device models, and observability stack serve many
concurrent projects. This document is the standing rule for anyone — human or
agent — editing this tree, including when the session was opened from a product
repo (for example `a2engine`) so that product needs can drive the work.

**Companion:** the assemble/link/disk half of the closed loop lives in
Rosetta v3 (`…/toolchain/rosetta_v3`) — see that tree’s
`docs/SHARED_FOUNDATION.md`. Together they are the foundation layer; product
trees consume them and must not own their public contracts.

**Related north-star docs in this fork:**

- [`DUAL_PLATFORM.md`](DUAL_PLATFORM.md) — IIe + IIgs merge bar; DualSmoke
- [`AGENTIC_ORACLE.md`](AGENTIC_ORACLE.md) — no black boxes; named models; discovery
- [`CLOSED_LOOP_INTEGRATION.md`](CLOSED_LOOP_INTEGRATION.md) — Rosetta closed-loop mission
- [`A2GSPU_RAILS.md`](A2GSPU_RAILS.md) — env rail catalog

---

## Why product sessions edit this repo

Enhancements and bug fixes are often discovered while building a *demanding
consumer*: dirty-cell cycle budgets, disk backends that stamp screen holes,
DHGR dumps, audio that must be real samples not register green, rail verbs that
answered `OK` while doing nothing. Working **in context of that consumer** is
intentional: the instrument is stressed by a real loop.

That does **not** make this fork a product-specific emulator. The consumer is a
stress test and a regression client. Verb semantics, cycle accounting, bank
paths, and fail-loud behaviour remain **general machine + rail contracts**.

---

## Hard lesson: IIe work must not break IIgs

**This has already happened.** Extended focus on IIe-centric instrumentation for
a product (a2engine’s HGR/DHGR/65C02 loop) regressed substantial **IIgs**
behaviour — MMU/bank resolution, native-mode leftovers after `run`, KeyGloo /
holdkey paths, SHR/oracle surfaces, ROM bank `$FF` sizing, audio/DOC vs drive
routing, and rail paths that silently took the wrong memory image on a IIgs
while looking green on a IIe.

That is the multi-consumer failure mode in platform clothing: the driving
product’s preferred machine is treated as “the” machine, and the shared fork’s
other first-class platform rots.

### Dual-platform is not optional

| Rule | Meaning |
|---|---|
| **Both platforms are first-class** | Enhanced IIe (e.g. platform 3) **and** IIgs (e.g. platform 5) are peer oracles. Neither is a proxy for the other (see also `AGENTIC_ORACLE.md` G8). |
| **IIe-green is not ship-green** | A rail/device/MMU change that only has IIe evidence is **incomplete**. Run or extend an IIgs gate before calling it done. |
| **Shared code needs dual evidence** | Anything that touches MMU, banks, `read`/`load`/`verify`, `run`/`step`/`run-until`, softswitches, keyboard, audio, or cycle accounting must be considered on **both** machines unless it is explicitly IIe-only or IIgs-only hardware. |
| **Platform-specific code must stay sealed** | IIe-only paths must not change IIgs control flow “for simplicity.” IIgs-only paths must not be deleted because the current product is IIe. |
| **Native mode is a landmine** | IIgs `run` can leave the machine in 65816 native mode; IIe/65C02 product code then misbehaves and looks like an engine bug. Instrumentation and harnesses must not paper over this by assuming IIe reset state after a IIgs session. |
| **GS/OS and SHR consumers still count** | a2os, A2GSPU, Rosetta closed-loop, toolbox traces — if those paths go red while a2engine IIe stays green, the change failed. |

### Practical bar before merging rail/device work

1. If the change is in **shared** code: evidence on **IIe and IIgs** (product suite,
   `gscheck` dual-platform suites, or fork-local equivalent).
2. If the change is **IIe-only**: prove IIgs suites that share the same entry
   points still pass (or document why they cannot run and add a sealed guard).
3. If the change is **IIgs-only**: prove IIe product loops that share CTRL verbs
   still pass.
4. Prefer gates that already exist on both platforms (`execcheck`, `altzpcheck`,
   railcheck, …) over inventing a second copy of the truth.

“We fixed it for a2engine on platform 3” is a **progress report**, not a
**completion criterion**.

---

## Known consumers (non-exhaustive)

| Consumer | Typical use of this fork |
|---|---|
| **a2engine** | Headless CTRL rail: deploy, `run-until` cycles, screen, rescheck, `gscheck` suites |
| **a2tile** | Shared measurement rail; DHGR/HGR goldens |
| **a2os** / GS/OS bring-up | Headless boot, toolbox traces, SPIKE asserts |
| **a2gpu** / **a2vga** / A2GSPU | Device rails, bus streams, coprocessor instrumentation |
| **Rosetta** closed loop | Build → boot → assert; oracle for toolchain defects |
| Manual / interactive use | Windowed stock emulator — dual-mode invariant |

A green product suite (e.g. a2engine `validate.ps1` / `gscheck.ps1`) is
**necessary evidence**, not **sole ownership** of a rail change.

---

## Standing rules when changing this tree

1. **Product-aware, not product-only.** Use the current product’s gates to find
   and falsify defects. Design the fix so every consumer of the rail or device
   model gets correct, discoverable behaviour.

2. **Do not bake product layout into the emulator.** No hard-coded a2engine
   addresses, session directories, gate flag names, or single-project defaults
   in CTRL verbs, SPIKE, or device code. Product policy stays in product
   scripts (`session.ps1`, `ctl.ps1`, harnesses).

3. **Prefer fail-loud over plausible-wrong.** Bare `status=OK` on no-ops, cycle
   counters that lie across command boundaries, bank paths that silently read
   the wrong image, and silent audio “success” are foundation bugs. Name the
   observation model (`cycles=` in-command only, `profile=`, `path=`, …) per
   [`AGENTIC_ORACLE.md`](AGENTIC_ORACLE.md).

4. **Regression surface is multi-project.** Prefer foundation-local suites and
   the fork’s own checks; when a2engine (or another product) hosts a suite that
   exercises a verb, keep that suite honest without making the verb *only*
   useful to that suite.

5. **Regression surface is dual-platform (IIe + IIgs).** See the hard lesson
   above. IIe-only green is not enough for shared instrumentation. Do not
   simplify the IIgs path away to make an IIe product loop happier.

6. **Dual-mode remains hard.** Instrumentation stays env-gated off by default
   and must not degrade windowed interactive use. Observe-don’t-disturb applies
   to the human operator as well as the guest. (Dual-*mode* = windowed vs
   instrumented; dual-*platform* = IIe vs IIgs — both are hard invariants.)

7. **Keep surfaces self-describing.** New verbs and rails earn a place in
   `help` / `manifest` (and docs). If an agent can only learn a capability by
   reading product code, the foundation is still a black box.

8. **Upstream discipline.** This is a permanent instrumentation fork; pull
   upstream in carefully. Keep instrumentation additive and separable so
   multi-consumer behaviour and stock UX both survive merges.

---

## How this pairs with Rosetta

```
edit source  →  Rosetta (asmiigs / cciigs / linkiigs / diskiigs / …)
                    ↓
              binary / disk image
                    ↓
              gssquared-fork (CTRL / SPIKE / devices)
                    ↓
              measured verdict back to product gates
```

- **Rosetta** answers: “Did we build what we meant?”
- **gssquared-fork** answers: “Does the machine behave as claimed?”

Product trees should **not** paper over instrument lies. Fix the rail or device
here so every consumer inherits the truth. Product-side `tools/*check.ps1`
suites are clients of that truth; keep them as external gates, not as the
definition of the emulator API.

---

## Agent entry points

| File | Role |
|---|---|
| **`Docs/SHARED_FOUNDATION.md`** (this file) | Multi-consumer contract — read before behavioural rail/device changes |
| `Docs/AGENTIC_ORACLE.md` | North-star observability / control contract |
| `Docs/CLOSED_LOOP_INTEGRATION.md` | Rosetta closed-loop mission and dual-mode rules |
| `Docs/A2GSPU_RAILS.md` | Env rail catalog |
| `README.md` | What this fork adds over upstream |
| Product harnesses (e.g. a2engine `tools/gscheck.ps1`) | Consumer gates — evidence, not ownership |

When a session is rooted in a product repo but the work is an emulator or rail
defect, edit this fork under these rules. Keep product workarounds temporary
and prefer a foundation fix with a gate that can fail for everyone.
