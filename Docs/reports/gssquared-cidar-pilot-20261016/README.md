# CIDAR queue: bound real primitive experiment (2026-10-16)

Status: implemented and validated, including the complete fork gate stack.
This report closes the first four CIDAR integration slices
under their explicitly scoped raw-image/owned-source model. It does not close
archive-wide ingest, full GS/OS acceptance, device-complete observation,
OMF loader mapping, automatic promotion or physical-hardware validation.

## Result

The reference genuinely includes Rosetta's owned `blockmove.macros.asm` and
invokes `_BlockMoveBytes`. The candidate is an owned unroll-four specialization
with a specialized tail. Both execute against independent expected bytes,
poisoned outputs, preserved source/pointers/guards/X/SP, a failing wrong-value
assertion, per-PC instruction/cycle totals and a checked CPU write footprint.
All of bank zero is watched; only the destination may be written.

32 reference/candidate pairs pass: counts 1, 3, 4, 5, 16, 63, 64, 255, aligned
and page-crossing source placements, IIe and IIgs. Every measured CPU cost also
matches an independent declared instruction-cost calculation. Representative
aligned pairs are:

| Count | CPU cycles, reference / candidate | Image bytes, reference / candidate |
| --- | --- | --- |
| 1 | 19 / 15 | 12 / 8 |
| 4 | 73 / 58 | 12 / 27 |
| 64 | 1153 / 913 | 12 / 27 |
| 255 | 4591 / 3631 | 12 / 42 |

Images include the same unexecuted stop sentinel. The full study retains native
master-14M and video intervals separately; CPU cycles are not a fixed-MHz
elapsed-time substitute. These finite comparisons establish neither a proven
floor nor ceiling. Count one refutes the previous universal optimality wording;
larger cases preserve explicit code-size/speed tradeoffs.

## Implemented shape

- `experiment.py` adds declared input loads, preservation checks/registers,
  per-PC profiles and bounded, PC-attributed CPU write footprints. Comparison
  rechecks artifacts and derived evidence instead of trusting saved success flags.
- `cidarbindings.py` joins versioned primitive/spec/candidate identities,
  owned source roles, build/tool/output receipts and raw absolute debug mapping
  to native execution/context artifacts. Changed inputs refuse before launch.
- `cidarpilot.py` is the reproducible real-library integration fixture;
  `cidarcheck.py` revalidates every retained pair and exercises source/image/map/
  spec substitutions. `experimentinputcheck.py` supplies a foundation-local
  native regression without requiring Rosetta or private media.
- Rosetta's INDEX, curation log, evidence page, acceptance queue and macro
  comments record the feedback. Library instructions and caller compositions
  are unchanged. The complete primitive assembly/double-expansion artifact is
  byte-identical before/after: SHA256
  `dc32f9051f2c9bf33d0f52391301c8057806db8097dc57e650447a44f31e7274`.

The small specialization stays experimental in the companion. This iteration
does not replace a library form or claim to validate callers it did not run.
Wider bank/overlap/interrupt/ABI compositions require additional evidence.
ABM recovery lineage remains curator-reported; the tested reference is the owned
Rosetta re-expression, without an original ABM binary witness.

The native MVN/MVP documentation correction is separately grounded in
[WDC's W65C816S datasheet](https://www.westerndesigncenter.com/wdc/documentation/w65c816s.pdf),
sections 3.5.9, 7.8 and 7.18: fixed instruction bank operands, count/index
constraints, operation in emulation mode with restricted addressing and DBR
effects. It removes unsupported universal throughput/optimality claims.

## Validation and retained evidence

| Check | Result | Evidence |
| --- | --- | --- |
| Real Rosetta primitive | 32 pairs / 64 passing variant executions | `study.json`, `pilot.log` |
| Adversarial side effects and noninterference | 10 controls: armed/unarmed plus omitted store, clobbered X, extra write and trace loss, on both platforms | Study controls; full local bundles |
| Retained binding revalidation | All 32 pairs reverified; 8 source/image/map/spec substitutions refuse before machine creation | `binding-checks.json`, `binding-checks.log` |
| General native preservation/observers | 12 focused checks, including independent 54-cycle fixture, changed-profile refusal, wrong write region and loss | `input-observers.log` |
| New offline validators | 9 unittest cases in the umbrella agentic gate; before-implementation failures retained | `inputs-before.log`, `bindings-before.log`, gate log |
| Existing Python client | 21/21 pass | `python-tests.log` |
| Rosetta docs | 3/3 pass; no compiler behavior change | `rosetta-docs.log` |
| Complete fork stack | 14/14 gates, 14/14 native CTests, no skips; 42 pinned unarmed/armed boot checks unchanged | `gates.log`, `gates.ndjson` |
| Static changed files | 8 Python files parse; 4 JSON artifacts parse; 59 local Markdown destinations resolve | `static.log` |
| README and discovery docs | Reviewed and updated: README companion scope, machine-context link, CIDAR schemas/recipe and MkDocs navigation | Tracked source/docs |

Full retained execution bundles are under
`D:\gsqs\cidar-20261016-final`. `retained-artifacts.json` lists every local
file identity without publishing binary fixtures. Invalid binding runs are in
`D:\gsqs\cidar-20261016-bindchecks`; the raw observer fixture is in
`D:\gsqs\cidar-20261016-inputchecks`. Artifact paths are local evidence locations,
not dependencies on consumer session directories. The generator accepts explicit
emulator, assembler, owned library and fresh output paths.

```text
python -I tools/a2rail/cidarpilot.py --exe D:/gsqs/pub-20261016/build/GSSquared.exe --asmiigs D:/gsqs/interface-completeness-20261015/rosetta-build/apps/asmiigs/asmiigs.exe --library D:/projects/toolchain/rosetta_v3/stdlib/primitives/system/blockmove.macros.asm --out D:/gsqs/cidar-20261016-final
python -I tools/a2rail/cidarcheck.py --study D:/gsqs/cidar-20261016-final --out D:/gsqs/cidar-20261016-bindchecks
```

Native executable/source checkpoint and protected consumer identities are in
`checks.json`. Native source is unchanged from `bd276467`; validation selects
the preserved out-of-tree publication executable. No drift, model merge,
baseline re-bless, protected consumer replacement or upstream publication is
part of this phase. Rosetta retains its no-push policy.

The first ad hoc `python -I -m pytest` invocation failed collection because isolated
Python did not have the source package on its import path. The existing explicit
script runner adds the selected checkout's client path and produces the 21-pass
result retained here. This was a test invocation error, not an emulator defect.
