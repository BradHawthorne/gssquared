# Queue iteration: checked snapshot state diffs (2026-10-15)

The owner requested “iterate task queue” after P0 completed. This iteration
implements **P2.1**, the next standalone proposal item: compare saved registers,
memory, MMU/page-map fields and device blocks, with a summary and ranges.
It requires no clock refactor, compiler debug-info fixture or kernel layout.
The queue now distinguishes completed tasks, integration preparation, the next
standalone candidate (P2.4), and the individually gated drift/merge/build choices.

P0 and P2.1 remain uncommitted on `a2gspu` over `834028cd` (68 commits ahead of
the cached remote). Nothing was pushed, no remotes or branches changed, and the
consumer-facing `build/GSSquared.exe` remains the September 17 binary. No drift
stage, engine merge or baseline re-bless was performed.

## What is available

- **CTRL/co-pilot:** `diff <A.snap> <B.snap> <report.json>`; quote paths with
  spaces. The ack reports `equal`, changed register/section counts and changed
  section bytes, or a fail-loud parsing/output error.
- **CLI:** `<build>/snapdiff A.snap B.snap [report.json] [--expect-different]`.
  Exit 0 means equal, 1 means different, 2 means invalid input/output.
  `--expect-different` reverses 0/1 to gate an expected divergence.
- **Artifact:** schema `a2snapdiff-v1`, model `serialized-saved-state`; named
  CPU-field changes, MMU/physical RAM/page-map/device byte ranges, block
  presence/version changes, and separate manifest metadata.

Both entry points use **one reader** in `src/a2gspu_snapdiff.hpp`. It parses
current little-endian GSPS v1 CPU fields, IIe/IIgs memory geometry, relocatable
page tables, length-prefixed device blocks, the sentinel and optional manifest.
It does not scan guest RAM for marker-like bytes. Unsupported/ambiguous formats,
truncation, malformed page references, duplicate device blocks, invalid framing,
trailing garbage and files over 256 MiB are refused. Different memory geometries
are not silently aligned. Reports cannot overwrite their input snapshots,
including aliases/hard links recognized by the filesystem.

Ranges are half-open **physical section offsets**, not guest-visible addresses.
Up to 128 ranges per section and 16 preview bytes per range keep output bounded;
totals remain exact and truncation is explicit. `section_changed_bytes` excludes
CPU-field changes, which have their own named before/after list.

`equal` describes saved serialized state. Device payloads are opaque versioned
bytes; their internal semantics are not decoded. Manifests are metadata and do
not affect equality. Excluded host/device state, CPU type, original ROM contents
and full-machine equivalence are not inferred. The snapshot format does not
have an end-to-end payload checksum; validation proves structure/geometry.
No snapshot writer, format, restore path or live MMU read was changed.

## Evidence

Owned synthetic GSPS fixtures live in `apps/snapdifftest/main.cpp`, registered
as ctest `snapshot_diff`. They test identical states, one planted RAM byte,
named CPU changes, device bytes, version/presence changes, pre-manifest files,
truncations, bad versions/lengths/page references, trailing data and incompatible
IIe/IIgs layouts. An incorrect expected offset is distinguished from the true
planted offset. The executable also serves as the standalone CLI.

`tools/a2rail/snapshotcheck.py` uses real CTRL saves on **both IIe and IIgs**:
identical input, changed A plus one known RAM byte, exact range/value assertions,
unchanged live CPU state, CLI 0/1/2 behavior and a deliberately failing divergence
expectation, quoted paths with Windows backslashes, input-overwrite refusal,
truncated/missing inputs, unwritable output and preservation of existing files
when input validation fails. It is called by the existing `agentic` gate, so
the umbrella remains 13 gates. Ctest grows from 12 to 13 without the engine.

**Final validation: Release build succeeded; ctest 13/13; all 13 gates PASS.**
The standalone snapshot unit test passes 31 checks. Live IIe/IIgs snapshot
checks and all earlier P0 cases pass inside the agentic suite. The final gate
process exits 0; unarmedhash passes 42 checks over four boots, hwtruth's 24
verdicts are unchanged and MB-Audit passes all 1,922 tests. Pinned hash/cycle/run
baselines and their JSON files are unchanged. `git diff --check` is clean and
vendored sources are untouched.

Evidence: [ctest](logs/ctest.log), [live snapshot checks](logs/snapshot.log),
[full gate transcript](logs/gates.log), [gate events](logs/gates.ndjson).

The build remains out of tree at
`D:\gsqs\coherency-20261015\build\GSSquared.exe`; standalone tool:
`D:\gsqs\coherency-20261015\build\snapdiff.exe`. The exact earlier P0 emulator
is preserved as `GSSquared-p0.exe` in that directory. Incremental build/test logs
for this iteration are under `D:\gsqs\queue-p21-20261015\logs`.

SHA-256:

| Executable | Hash |
| --- | --- |
| Current emulator | `64056eb715e2947b47f9127fc9647045e2b5d2fe47ac00b6ee92c9e14e37f3dc` |
| snapdiff | `74fd300e40a59b58824bb72f3fb4741bd6de8000adae4633f9724814e5a1e459` |

The stock windowed interaction check and the external a2engine suite are not
claimed here. Historical P0 measurements remain in their original report.

## Queue after this iteration

P2.1 is complete with the full-stack evidence above. The next standalone
candidate is **P2.4**, a local-version bisect harness with a scratch-build recipe
that preserves current work and dependency inputs. P1 can proceed after selecting
kernel-layout fixtures, a 65816 debugger-client contract and compiler debug-info
fixtures, respectively. These are preparation tasks, not claims that a particular
consumer repository is blocked or that no such input exists elsewhere.

Drift B-E still need the owner's individual yes; the engine merge, baseline
re-blessing and consumer binary replacement remain separate decisions.
