# gssquared — `a2gspu` development fork

A working fork of **[gssquared](https://github.com/jawaidbazyar2/gssquared)** — Jawaid Bazyar's
cross-platform Apple II / Apple IIgs emulator. The upstream emulator is excellent and complete;
this fork keeps all of that and adds a layer of **headless development + automated-test tooling**
used to build and validate Apple IIgs / GS/OS software with an external cross-development toolchain.

Everything here lives on a single branch, **`a2gspu`** (also this fork's default branch).

---

## Relationship to upstream

This fork is deliberately one-way and self-contained:

- **It never pushes to, or opens pull requests against, upstream.** We pull upstream changes *in*;
  we never push *out*. There is no path by which this fork can pollute upstream `main` — that is the
  whole reason it is pared down to a single branch.
- **Upstream is welcome to cherry-pick.** This is just a public branch. If any fix or feature here is
  useful to upstream gssquared, the maintainer is welcome to take it on their own terms — no
  expectation and no obligation, in either direction.
- **For the canonical emulator, use [upstream](https://github.com/jawaidbazyar2/gssquared).** This fork
  optimizes for one specific automated-development workflow rather than general interactive use.

---

## How this fork deviates from upstream

The core emulator is upstream's. Our additions are **additive and, wherever possible,
environment-variable–gated**, so with the extra features off the emulator behaves as stock. They exist
to turn gssquared from an interactive emulator into a **machine-drivable CI / development rig** for
Apple IIgs software.

### Headless GS/OS bring-up diagnostics
An env-gated, stdout-only diagnostic suite (the `A2GSPU_*` family) that makes headless GS/OS app
development debuggable without a window:
- Toolbox-call trace, GS/OS dispatch trace, and a GS/OS error-code hook
- Per-scanline SCB video-mode + palette + pixel-index summary, plus an ASCII screen map
- Memory-range hexdump, BRK CPU-state dump, headless breakpoints, and symbol resolution

### Closed-loop assertion harness
A declarative **assertion gate** — e.g. `A2GSPU_ASSERT="scb_mode==320;qd_carry==0;idx6==28800"` —
evaluated against the final machine + video state at the end of a headless run, which sets the
process **exit code (0 / 1)**. With a pinned RNG seed (`A2GSPU_SEED`) and an SHR-render golden compare
(`A2GSPU_GOLDEN`), gssquared becomes a deterministic pass/fail oracle that an external build system
can drive unattended.

### Deterministic golden / differential testing
SHR-render goldens plus a behavioral-differential model that verify emulator changes do not drift the
boot/render baseline, and that validate toolchain-produced binaries against real GS/OS behavior on the
actual ROM.

### Other additions
- A **warm-boot snapshot** (save/restore the post-boot machine state) for a fast edit→run loop that
  skips the cold boot.
- Expanded, size-agnostic Apple IIgs ROM personalities (ROM 01 / 03 / 04).
- An experimental coprocessor/peripheral device model — the `a2gspu` device the fork is named after —
  used for instrumentation and protocol experiments.

---

## Toolchain integration

This fork is the **emulator half of a closed development loop** with an external Apple IIgs
cross-development toolchain (assembler, linker, C compiler, resource compiler, and disk-image tools).
One iteration looks like:

```
edit .s / .c
  → assemble + link             (the toolchain)
  → mint a bootable disk image  (the toolchain)
  → boot headless in this fork  (-n)
  → A2GSPU_ASSERT gate          → exit 0 / 1
```

The toolchain produces IIgs binaries; this fork **runs and verifies** them against real GS/OS and
reports precise diagnostics (toolbox returns, render state, error codes) back to the build system — so
a toolchain bug surfaces immediately as a failed assertion instead of a silent wrong-pixel or a
corrupted object file. The emulator is, in effect, the toolchain's regression oracle.

> **Automation is headless-only.** The standalone CPU/MMU test programs under `apps/` open a GUI dialog
> when their (external) test ROMs are absent, so they are **not** used in automated runs. All automation
> drives the main emulator headless (`-n`) through the harness; CPU/MMU behavior is checked with small
> headless micro-tests instead.

---

## Developed with Claude Code

This fork is developed with **[Claude Code](https://claude.com/claude-code)** (Anthropic's agentic CLI).
The closed loop above is wired so the agent and its **hooks** can, on every change:

1. edit → `cmake --build` → boot headless → read the `A2GSPU_ASSERT` / golden verdict, fully
   unattended; and
2. gate the change on the SHR / boot golden, so the differential oracle the rest of the work relies on
   cannot silently drift.

That is why the diagnostics are stdout-only and validation collapses to a single exit code: the entire
develop → build → test loop is designed to be driven by an automated agent, not a human at a window.
Hooks enforce the headless-only and golden-gate rules so an automated run cannot wedge on a GUI prompt
or land a baseline-moving change unnoticed.

---

## Building

For the base build, prerequisites, and packaged binaries, see
**[upstream's README](https://github.com/jawaidbazyar2/gssquared)** and the
[User Documentation](Docs/index.md). Our additions are header-mostly and env-gated: a standard
`cmake --build build` produces the emulator, and the `A2GSPU_*` environment variables turn on the
development features at runtime — nothing changes for ordinary interactive use.

---

## License & credits

The Apple II / IIgs emulator core, models, and UI are **Jawaid Bazyar's** work — see
[upstream](https://github.com/jawaidbazyar2/gssquared). This fork inherits upstream's license, and our
additions are offered under the same terms. Sincere thanks to the upstream project for a clean,
hackable IIgs emulator to build on.
