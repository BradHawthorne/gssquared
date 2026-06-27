# gssquared — `a2gspu` development fork

A fork of **[gssquared](https://github.com/jawaidbazyar2/gssquared)** (Jawaid Bazyar's Apple II / IIgs
emulator) carrying one thing upstream doesn't: a **headless, machine-drivable development + test layer**
for building and validating Apple IIgs / GS/OS software with an external cross-toolchain. Everything
lives on one branch, **`a2gspu`** (the default).

> **This fork is one-way.** We pull upstream *in*, never push *out* — no PRs, no path to pollute
> upstream `main`. It's just a public branch; upstream is welcome to cherry-pick anything useful on
> their own terms. For the canonical emulator, use
> **[upstream](https://github.com/jawaidbazyar2/gssquared)**.

Everything below is **what this fork adds.** It is all additive and env-gated — with the features off,
the emulator is stock.

---

## The development layer — what makes this fork different

**Headless GS/OS bring-up diagnostics.** An env-gated, stdout-only suite (`A2GSPU_*`): toolbox +
GS/OS-dispatch + error-code traces, a per-scanline SCB video-mode / palette / pixel-index summary and
an ASCII screen map, memory-range hexdump, BRK CPU-state dump, headless breakpoints, and symbol
resolution. Debug a GS/OS app with no window open.

**A closed-loop assertion gate.** `A2GSPU_ASSERT="scb_mode==320;qd_carry==0;idx6==28800"` is evaluated
against the final machine + video state at the end of a headless run and sets the process **exit code
(0 / 1)**. With `A2GSPU_SEED` (pinned determinism) and `A2GSPU_GOLDEN` (SHR-render compare), the
emulator becomes a deterministic pass/fail oracle a build system drives unattended.

**Deterministic golden + behavioral-differential testing.** SHR-render goldens that catch any
boot/render drift, plus a differential model that validates toolchain-produced binaries against real
GS/OS behavior on the actual ROM.

**Warm-boot snapshot.** Save/restore the post-boot machine state for a fast edit→run loop that skips
the cold boot.

**Expanded ROM personalities.** Size-agnostic Apple IIgs ROM 01 / 03 / 04.

**The `a2gspu` device.** An experimental coprocessor/peripheral device model — the fork's namesake —
used for instrumentation and protocol experiments.

---

## Toolchain integration

This fork is the **emulator half of a closed loop** with an external Apple IIgs cross-toolchain
(assembler, linker, C compiler, resource compiler, disk-image tools):

```
edit .s/.c → assemble + link → mint a bootable disk → boot headless (-n) → A2GSPU_ASSERT → exit 0/1
```

The toolchain emits IIgs binaries; this fork **runs and verifies** them against real GS/OS and reports
precise diagnostics (toolbox returns, render state, error codes) back to the build system — so a
toolchain bug shows up immediately as a failed assertion instead of a silent wrong-pixel or a corrupt
object file. The emulator is the toolchain's regression oracle.

> Automation is **headless-only**: the `apps/` CPU/MMU test programs pop a GUI dialog when their
> external test ROMs are absent, so they are never used in automation — the loop drives the main
> emulator headless (`-n`), and CPU/MMU behavior is checked with small headless micro-tests instead.

---

## Developed with Claude Code

The loop is built to be driven by **[Claude Code](https://claude.com/claude-code)** (Anthropic's
agentic CLI) and its **hooks**. Every change runs edit → `cmake --build` → headless boot → read the
`A2GSPU_ASSERT` / golden verdict, fully unattended, and is gated on the SHR / boot golden so the oracle
the rest of the work relies on can't silently drift. That is why the diagnostics are stdout-only and
validation collapses to a single exit code — and why hooks enforce the headless-only and golden-gate
rules, so an automated run can't wedge on a GUI prompt or land a baseline-moving change unnoticed.

---

## Build & credits

Base build, prerequisites, and packaged binaries: see
**[upstream's README](https://github.com/jawaidbazyar2/gssquared)** and the
[User Documentation](Docs/index.md). Our additions are header-mostly and env-gated — `cmake --build build`
produces the emulator, unchanged for ordinary interactive use.

The emulator core, models, and UI are **Jawaid Bazyar's** work. This fork inherits upstream's license;
our additions are offered under the same terms. Thanks to the upstream project for a clean, hackable
IIgs emulator to build on.
