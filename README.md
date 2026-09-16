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

**Snapshots.** Save/restore the machine — CPU, MMU, RAM and the device blocks it has (on a IIgs the
ADB micro, the Ensoniq, the interrupt registers, the disk controller and the block device) — for a
fast edit→run loop that skips the cold boot. Every file carries a manifest of what it holds; a restore
that does not fit is rolled back, never half-applied.

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

**Shared foundation.** This is not a single-product harness. a2engine, a2tile, a2os, a2gpu/a2vga,
Rosetta closed-loop work, and interactive use all share the same rail and device contracts. Product
sessions often drive fixes here; behaviour must stay general. See
[`Docs/SHARED_FOUNDATION.md`](Docs/SHARED_FOUNDATION.md) (and [`AGENTS.md`](AGENTS.md) for agents).

> Automation never opens a window that can take focus or a modal dialog. The emulator runs headless
> (`-n`), or with `A2GSPU_BACKGROUND_WINDOW` for the one gate that needs a window. Of the `apps/`
> programs, the ctest set (`conformtest`, `coveragetest`, `cycletest`, `cycletest816`, `w6522`) is
> headless; `cputest` and `cpu816test` exit 77 with instructions when their third-party fixture is
> absent instead of raising a dialog.

---

## Developed with Claude Code

The loop is built to be driven by an agent — **[Claude Code](https://claude.com/claude-code)**
(Anthropic's agentic CLI) in practice, though the contract is vendor-neutral files, text and exit
codes. A change runs edit → build → headless boot → read the `A2GSPU_ASSERT` / golden verdict,
unattended, which is why diagnostics are machine-readable lines and validation collapses to an exit
code.

What keeps it honest is the gate set, not the agent: `tools/a2rail/gates.ps1` runs every fork-local
gate — the registry/catalogue/dispatcher parity check, the dual-platform CTRL suites, every built-in
machine configuration, the assert-surface parity check, and `unarmedhash.ps1`, which pins stock IIe
and IIgs boots against a recorded baseline and proves that armed observation rails change neither.
Nothing enforces running it automatically; the windowed, no-instrumentation check is still done by
hand.

---

## Build & credits

Base build, prerequisites, and packaged binaries: see
**[upstream's README](https://github.com/jawaidbazyar2/gssquared)** and the
[User Documentation](Docs/index.md). The fork's additions are env-gated and live mostly in their own
files (`src/a2gspu_*`, `src/iigs_*`, `src/memvu_*`) — `cmake --build build` produces the emulator,
unchanged for ordinary interactive use. On Windows the MSYS2 UCRT64 toolchain's `bin` directory must be
on `PATH` to run the emulator (ctest carries it itself). Test inputs — disk images and snapshots, which
are not redistributed — go in `corpus/`; see `corpus/README.md`. What has been ported from upstream, and
what is owed, is `Docs/UPSTREAM_SYNC.md`.

Code merged from upstream carries its own attributions, repeated here because this README replaces
upstream's: the Host FST is Kelvin Sherlock's GSPlus code (GPL 2.0); the Apple Mouse II Interface
Card emulation (PIA + 6805 protocol) is adapted from the
[A2Pico mouse-interface](https://github.com/oliverschmidt/mouse-interface) firmware by Thorsten Brehm
and Oliver Schmidt (MIT License); the ADB mouse tracking technique is based in part on the one used in
KEGS (GPL 2.0).

The emulator core, models, and UI are **Jawaid Bazyar's** work. This fork inherits upstream's license;
our additions are offered under the same terms. Thanks to the upstream project for a clean, hackable
IIgs emulator to build on.
