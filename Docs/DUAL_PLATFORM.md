# Dual-platform bar (IIe + IIgs)

**IIe-green is not ship-green** for shared instrumentation.

This fork emulates both Enhanced Apple IIe and Apple IIgs. Product work often
stresses one machine first (a2engine is primarily IIe / 65C02). That is fine for
*discovery*. It is not enough to *merge* a change that touches shared code.

Full multi-consumer rules: [`SHARED_FOUNDATION.md`](SHARED_FOUNDATION.md).

---

## When both platforms are required

Any change to:

- MMU / banks / `load` / `read` / `verify` / `poke`
- `run` / `step` / `run-until` / cycle accounting
- softswitches, keyboard, audio probes that share code paths
- CTRL verb dispatch and ack schema

…needs evidence on **platform 3 (IIe Enhanced)** and **platform 5 (IIgs)**
unless the code is sealed behind an explicit IIe-only or IIgs-only branch.

## What to run

From the **a2engine** tree (hosts the current instrument suites):

```powershell
.\tools\dualsmoke.ps1
```

That runs offline ctest plus railcheck / execcheck / altzpcheck on **both**
platforms. Full depth remains:

```powershell
.\tools\gscheck.ps1
```

## Commit message habit

After a shared rail/MMU change, note dual evidence:

```
DualSmoke green (IIe+IIgs)
```

or attach the matrix from `dualsmoke.ps1` output.

## Known failure mode (already paid)

IIe-centric instrumentation previously regressed IIgs MMU resolve, native-mode
leftovers after `run`, KeyGloo/holdkey, SHR paths, ROM bank `$FF` sizing, and
silent wrong-bank reads. Do not relearn this.
