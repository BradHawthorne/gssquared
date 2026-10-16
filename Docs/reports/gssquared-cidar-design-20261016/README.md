# CIDAR-informed foundation design (2026-10-16)

Type: evidence record for the existing shared-foundation, oracle and curation
contracts. Status: design direction recorded; runtime evidence integration
remains pending in the existing queues. Retain this record with those changes;
it is not a second live-status owner.

The owner directs both foundations toward continuous architectural reasoning:
maximally expose execution so agents can understand historical software,
challenge assumptions, synthesize improved forms and return measured findings
to CIDAR's specifications and primitive libraries. The long-horizon corpus
includes Asimov and Internet Archive. A thousand copies can represent inherited
1980s/1990s boilerplate, not a thousand independent demonstrations of quality.

## Design decisions recorded

- **General emulator evidence:** typed state/transitions/dependencies, explicit
  observation boundaries, source/model references, availability and loss;
  distinguish observed causality from nesting, sampling and inference.
- **Experiment-driven visibility:** expose the controls and channels an agent
  needs to test an explanation. Prefer resolving a concrete uncertainty over
  increasing tool counts. Bound queries without hiding omissions; measure host
  observation cost separately from guest architectural cost.
- **Paired responsibilities:** CIDAR owns recovered understanding and provenance,
  Rosetta owns reproducible build/code identity and curation, the emulator owns
  general machine observations/interventions, and consumers own layouts and
  workload contracts. No product/primitive address maps enter native models.
- **Exposed toolchain decisions:** review Rosetta features for inspectable
  macro expansion, CPU-width assumptions, code/data recovery, IR/optimization,
  symbol/archive selection, relocations and final layout. Bind available
  explanations/intermediates to artifacts and preserve located refusals;
  complete pass-by-pass exposure remains a design target.
- **Continuous refinement:** agent proposals can refine, fuse, specialize or
  replace historical forms. Atomic means a coherent independently testable
  contract. Preserve faithful recovery separately from changed candidates.
- **Conditional selection:** one understood contract can have several useful
  implementations, including variants in the same language. Compare under
  named targets, objectives and conditions; retain incomparable tradeoffs.
- **Evidence before promotion:** identity-bound reference/candidate execution,
  independent expected outcomes, failing negative controls, adversarial cases,
  explicit metric/clock/coverage limits and revalidation of affected callers.
  Specifications, scaffolds, executed conformance, measured improvement and
  physical-hardware validation are distinct evidence states.
- **Closed feedback:** counterexamples, failed alternatives and uncertainty
  revise the specification, selection rationale and existing debt queues.
  Historical frequency is not a selection score or proof of optimality.

These are design/review criteria. They do not claim complete device/data-flow
coverage, automatic library promotion, archive-wide ingestion, a complete
rebuilt GS/OS or a global efficiency bound. Existing source-compatibility,
dual-platform, default-off observation and individually gated owner decisions
remain in force.

## Canonical locations

| Topic | Owner |
| --- | --- |
| Emulator foundation direction | `Docs/SHARED_FOUNDATION.md`, corpus-informed refinement |
| Emulator feature review | `Docs/AGENTIC_ORACLE.md` section 10 |
| Implemented context/experiment subset | `Docs/MACHINE_CONTEXT.md`; prior machine-context report |
| Emulator pending integration | `Docs/AGENTIC_ROADMAP.md`, CIDAR design checkpoint |
| Rosetta foundation direction | Rosetta `docs/SHARED_FOUNDATION.md`, corpus-informed refinement |
| Primitive selection/promotion | Rosetta `stdlib/primitives/CURATION.md`, measured refinement |
| Recovery doctrine and entry points | Rosetta `.claude/commands/cidar.md`, workflow comment, CIDAR README, INDEX and paired AGENTS/CLAUDE guidance |
| Rosetta pending integration | Rosetta `docs/internal/ACCEPTANCE_QUEUE.md`, CIDAR-EVIDENCE/PILOT/FEEDBACK |

The first integration sequence is: join versioned contract/artifact evidence;
pilot a genuine source-grounded primitive; expand general observations around
the pilot's uncertainties; close measured findings into curation. Both queues
name ownership and acceptance evidence. No speculative runtime schema or
compiler selection engine is advertised as implemented.

## Validation

The first gate invocation selected the preserved `GSSquared-awareness.exe`
alias. Five suites failed because file-harness liveness checks require the
process name `GSSquared`; some diagnostics called live alias processes "gone".
The initial log/matrix are retained as `gates-alias-initial.*`. Five leftover
owned sessions were verified by their declared executable, PID and process
creation time, then quit through native CTRL; `alias-cleanup.log` records that
cleanup. No machine-wide process reaper was used. The roadmap records a future
identity/diagnostic fix with renamed-executable and unrelated-PID controls.

The corrected full invocation selects
`D:/gsqs/coherency-20261015/build/GSSquared.exe`, whose bytes match the preserved
awareness alias. This is an out-of-tree executable; the protected in-tree
consumer is untouched. Selecting the expected filename addresses this run's
harness precondition, not the pending general alias-support defect.

This checkpoint edits documentation and a workflow comment only. No executable
source, baseline, consumer binary or reference corpus is changed by this
checkpoint. The source trees already contain earlier working-tree changes;
their preceding results are recorded in the machine-context report.

| Check | Final result | Evidence |
| --- | --- | --- |
| Full fork gate stack, expected executable filename | 14/14 PASS, no skips | `gates.log`, `gates.ndjson` |
| Native CTests within dualsmoke | 14/14 PASS | `gates.log` |
| Pinned unarmed/armed boot witnesses | 42 checks over four boots pass; baseline hashes/cycles unchanged | `gates.log`, unarmedhash |
| Rosetta documentation gates | 3/3 PASS: docs_index, agent_docs_pair, agent_docs_gate | `rosetta-docs.log` |
| Selected changed-file whitespace, added local links/fragments, paired guidance and section structure | PASS; 12 added local Markdown destinations/fragments checked | `design-checks.log` |

The full fork invocation ran from MSYS2 bash with UCRT64/Python on PATH,
`A2_GSSQUARED=D:/gsqs/coherency-20261015/build/GSSquared.exe` and
`GSSQUARED_ROOT=D:/projects/gssquared-fork`, calling the repo-owned
`pwsh -NoProfile -File tools/a2rail/gates.ps1 -Report <this-dir>/gates.ndjson`.
Rosetta's existing out-of-tree Release build ran serial CTest with
`-R '^(docs_index|agent_docs_pair|agent_docs_gate)$'`. This is a targeted
documentation selection, not a new full Rosetta validation claim.

Selected-file diff checks exclude the owner's pre-existing Port7 listing edit,
whose CRLF/trailing-whitespace differences still appear in a whole-Rosetta
`git diff --check`. That reference file was not edited by this checkpoint.
Final executable/consumer hashes, selected document identities and repository
heads are recorded in `validation.json`; `final-record-check.log` records the
identity checks. No new runtime feature is introduced or claimed by this
design checkpoint. Changes remain local and uncommitted.
