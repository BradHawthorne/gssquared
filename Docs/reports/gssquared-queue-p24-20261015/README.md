# Queue iteration: local revision witness search (2026-10-15)

The owner requested “iterate next steps” after P2.1. This iteration implements
P2.4, the remaining standalone P2 proposal item: build local revisions in
scratch and locate the first changed deterministic behavior witness. No drift,
engine merge, baseline re-bless or consumer binary replacement is involved.

## Implementation

`tools/a2rail/revisionbisect.py` is the implementation; `bisect.ps1` is its Windows
wrapper, `bisect.example.json` a Release IIgs/ctest scenario, and the a2rail
README documents the schema, commands and limits. The new offline `bisect`
umbrella gate invokes `bisectcheck.py` through `--selftest`.

Tracked sources come from local Git archives; dependencies are fixed snapshots
of initialized local bytes, not a claim of pristine vendor trees. Full Git IDs,
SHA256s, argv, logs, configured checks, repeated guest witnesses and source
fingerprints are recorded in `a2bisect-report-v1`. Scratch detached metadata
preserves configure-time revision identity using read-only object alternates.
No source checkout/stash/ref/config change or network operation is performed.
No scratch directory is reused or recursively removed.

The witness compares GSDIAG status, SHR hash, cycle count and run hash. Seed,
fake time, BRAM, RAM disk, frame budget and pace are fixed. Other PINS must
agree across all runs/revisions, except build/executable identity. Repeat
disagreement or build/check/launch/input failures produce an inconclusive
report. A first-parent binary search plus exhaustive earlier-prefix checking
finds the first changed witness even with reversions; matching endpoints check
every intervening revision. This can require building the whole range.

## Validation

The real-history search built the two documentation-only revisions
`d933c77be4236c2b807a5325fa13fd6326f06b92` and
`834028cd5ab3d553a9398e0ca88f5ec72de63b68`. Each passed its committed ctest
suite **12/12** and two pinned 30-frame IIgs runs. Both repeated witnesses were:

| Field | Value |
| --- | --- |
| status | `OK` |
| SHR hash | `6D9886E3B3EE0383` |
| cycles | `594890` |
| run hash | `1B83AC4DB8C39F65` |

The result was **none**, scoped to that scenario. The source fingerprint before
and after was identical (`97948e53fa0495b1cd98bd47efe6eabad40aa6f921e1582972f122e7d0ddffba`),
and dependency/input bytes remained unchanged. Full provenance is in
`real-history.json`; the console summary is `real-history.log`. The retained
scratch builds and logs are under
`D:\gsqs\queue-p24-20261015\real-history`.

The final umbrella run passed **14/14 gates**, including current ctest **13/13**,
the 42 observation/noninterference checks, 24 hardware-truth checks and 1,922
Mockingboard assertions. Baselines remain unchanged, including the IIgs ROM
boot's 6,236,744 cycles / run hash `800AEA5075763504`. The matrix and full log
are `gates.ndjson` and `gates.log`. Its offline bisect suite passed nine tests:
an actual four-commit planted change with the expected first commit, a deliberately
wrong CLI expectation returning 1, no change, endpoint reversion, a build failure
reported inconclusive, nondeterminism and timeout with specific error checks,
scratch/path refusals, strict output parsing, and fixed input/dependency copies.

The first umbrella attempt exposed a host integration error: naming the module
`bisect.py` shadowed Python's standard library in an existing non-isolated tool.
It was stopped during the fixture suite, before emulator suites, and the tool
was renamed `revisionbisect.py`. The actual-history run preceded that filename
change; the final CLI and wrapper passed in the new umbrella run.

No emulator C++ changed in this iteration. The current P0/P2.1 Release binary
was `D:\gsqs\coherency-20261015\build\GSSquared.exe`, preserved before the later
interface work as `D:\gsqs\coherency-20261015\build\GSSquared-p21.exe`, SHA256
`64056eb715e2947b47f9127fc9647045e2b5d2fe47ac00b6ee92c9e14e37f3dc`.
The in-tree consumer binary remains SHA256
`af7e0bbd72bee417ffddb5e86a00597df280fba904e8b2ed1ff9ad0738e2956c`.
Nothing was committed or pushed; branch heads, remotes, baseline witnesses and
the separate engine model are unchanged.

## Queue after this iteration

P0, P2.1 and P2.4 are implemented. Next integration preparation is selecting
the P1 kernel-layout fixtures, 65816 debugger-client contract or compiler
debug-info fixtures. Replay/branching still require the clock work in drift D;
drift B-E, the engine merge and rebuilding the consumer binary remain separate
owner decisions. The stock windowed interaction check and external consumer
suites are not claimed by this host-tool change.
