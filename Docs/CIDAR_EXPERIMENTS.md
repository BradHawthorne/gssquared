# CIDAR experiment bindings and the block-copy pilot

`tools/a2rail/experiment.py` joins a declared primitive contract to owned
sources, build artifacts and measured guest execution. It is a fork-side
companion; Rosetta v3's compiler defaults and CLI are unchanged.

## Declared observations

An `a2experiment-plan-v1` can additionally declare:

- `loads`: relative digest-pinned input files and their CPU addresses. Loads
  cannot overlap the program, one another or output checks.
- A memory check `role` of `preserved-input`. Its independent expected bytes
  must match a declared input before execution and remain unchanged afterward.
  Ordinary `output` checks retain poisoning and the deliberately wrong assertion.
- `preserve_registers`: explicitly selected CPU registers to compare across
  execution. Missing register fields refuse rather than passing vacuously.
- `profile: true`: retained `a2profile-v1` per-PC instruction counts and cycles.
  Their sums must equal the native execution interval.
- `footprint`: bank-local watched `windows`, allowed write regions,
  `max_records`, and optionally `exact_writes`. All allowed regions must be
  watched. An unexpected write, suppression record or unrecognized record fails.

These form `a2experiment-contract-v2`, hashed into the workload identity.
Both variants must share the contract, platform, executable, pins and initial
clock setup before their costs can be compared. Comparison rehashes retained
images, input loads, context snapshots and optional observation artifacts,
recomputes profiles/footprints, and rechecks preserved registers.

The footprint model observes **CPU bus writes in declared windows**. It does
not establish completeness outside those windows or for DMA, physical signals
and device-internal changes. The pilot watches all of bank zero and permits
only its destination. It attributes writes to the native recorded PC; it does
not infer universal data flow from temporal adjacency. Observers arm after host
setup and disarm before result inspection. Armed/unarmed fixtures require
identical guest bytes and CPU cycles on IIe and IIgs.

## Optional CIDAR join

`cidar` names three relative digest-pinned files: `spec`, `candidate` and
`loadmap`. `tools/a2rail/cidarbindings.py` checks them before starting a machine.

| Record | Required binding |
| --- | --- |
| `cidar-primitive-spec-v1` | Primitive ID, positive version, exact experiment contract and explicitly qualified lineage |
| `cidar-candidate-v1` | Candidate ID/version, spec/image/load-map SHA256, owned source roles/digests, build-receipt path/digest |
| `cidar-build-receipt-v1` | Explicit executable/argv, tool SHA256, successful exit, matching source inputs and retained output identities |
| `cidar-raw-loadmap-v1` | Raw address/size/image identity, debug-file identity, absolute `~MAIN` entry/target labels checked against Rosetta's emitted house debug text |
| `cidar-experiment-binding-v1` | Joined identities, declared lineage, artifact digests and the scope of this local evidence |

Every referenced artifact must also be a pinned plan input. Versioned identities
and local digest bindings prevent accidental substitutions; these records do
not independently authenticate a historical binary or attest a third party's
build. The local build tool must still match its receipt during verification.
Comparison requires the same primitive/spec identity, while candidate identities
may differ. Raw absolute debug labels are the only load-map model supported
here. Rosetta's raw build JSON currently has null symbol/segment fields; its
debug text supplies these labels. No OMF relocation, loader or GS/OS boot
acceptance follows from this model.

## Real library pilot

```text
python -I tools/a2rail/cidarpilot.py --exe <tested-emulator> --asmiigs <Rosetta-asmiigs> --library <Rosetta>/stdlib/primitives/system/blockmove.macros.asm --out <new-directory>
```

The reference genuinely includes and invokes `_BlockMoveBytes`. The owned
candidate unrolls four ascending byte copies per loop and specializes the tail.
The independent host oracle supplies byte data, pointer values and guards.
Both use count 1..255, 8-bit A/index, disjoint bank-zero RAM and preserved X/SP,
pointers, source and guards. A/Y/status are clobbered by contract.

The complete fixture uses counts 1, 3, 4, 5, 16, 63, 64 and 255, aligned and
page-crossing source placements, on IIe and IIgs: 32 comparisons / 64 passing
variant executions. Costs also must match an independent declared instruction
cost calculation. Planted omitted stores, clobbered X, extra writes and trace
loss must fail. The foundation gate's `experimentinputcheck.py` uses owned raw
opcodes and needs neither Rosetta nor private corpus data; the real-library
pilot remains an explicit integration check.

At the measured checkpoint, one byte improves from 19 to 15 CPU cycles and
12 to 8 image bytes. Four bytes improve from 73 to 58 cycles but grow from
12 to 27 bytes. At 255 aligned bytes the pair is 4591 to 3631 cycles and
12 to 42 bytes. Every image includes the same one-byte stop sentinel; it is
not executed. Master/video intervals are retained separately, without treating
CPU cycles as elapsed IIgs time at a fixed conversion ratio.

The curation decision retains the reference and the experimental specialization
with their constraints and code-size tradeoffs. No global optimum, automatic
promotion, whole-library correctness or physical-IIgs result is established.
The original ABM provenance remains curator-reported; this pilot tests an owned
Rosetta re-expression, not recovery fidelity against ABM's original binary.
Current evidence is in the CIDAR queue report linked from the roadmap.
