# Mockingboard implementation and reference map

This is the current disposition of the similarly named Mockingboard files.
Use the build/include edges below when assessing behavior; a defect in a
retained alternative is not automatically a defect in the running emulator.

| Files | Current role | Disposition |
| --- | --- | --- |
| `src/devices/mockingboard/mb2.cpp`, `mb2.hpp` | Root CMake target `gs2_devices_mockingboard` compiles `mb2.cpp`; it includes `mb2.hpp`, `W6522.hpp` and `AY8910-2.hpp` | Active implementation; changes require dual-platform regression and the Mockingboard audit |
| `W6522.hpp` | Cycle-at-a-time VIA used by the active implementation and `apps/mbtest/w6522.cpp` | Active device/test code; nine VIA CTests and `mbaudit` exercise their declared cases |
| `AY8910-2.hpp` | Included by the active `mb2.cpp` | Active audio device; keep its observation/snapshot contracts |
| `mb.hpp` | Included by `src/devices.cpp`, even though root CMake builds `mb2.cpp` | Load-bearing compatibility declarations; preserve this path until its callers migrate and compile/link validation proves the replacement |
| `mb.cpp`, `AY8910.hpp` | Alternate implementation; `mb.cpp` includes `AY8910.hpp`; not selected by the root Mockingboard target | Retained reference/compatibility paths; not evidence about the active model without a matching execution fixture |
| `N6522.hpp`, `apps/mbtest/n6522.cpp` | Callback-based alternative; its executable target is commented out in `apps/mbtest/CMakeLists.txt` | Disabled comparison fixture; source retained, not a passing registered test |
| `N6522.hpp-mostrecent`, `N6522.hpp-worksbutdoesntpassaudit` | No current compiled include edge found in `src/` or `apps/` | Historical reference copies; filename descriptions are not current validation results |
| `src/devices/mockingboard/MB.md` | Development notebook with old experiments, failures and hypotheses | Historical evidence; consult current build, fidelity ledger and gate receipts before applying a claim |

This consolidates reference ownership without relocating files or changing
public include paths. Removal is a separate compatibility retirement: absence
from this build does not prove that no external consumer includes a path.
Keep the active declarations and disabled fixtures until a named migration
provides that evidence. No deletion is required to keep the current build and
its evidence unambiguous.

The current audit gate is `tools/a2rail/mbaudit.ps1`, run by `gates.ps1`. It
requires the declared local MB-Audit v1.61 corpus image and two Mockingboards;
its pass is scoped to that suite. See the current disposition in
[`fidelity/README.md`](fidelity/README.md), the earlier source classification in
[`the harness queue report`](reports/gssquared-queue-harness-20261016/README.md),
and the current validation in the fleet queue report. This map does not turn
historical hardware hypotheses or disabled targets into new proof.
