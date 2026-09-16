/*
 *   Copyright (c) 2025-2026 Jawaid Bazyar

 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.

 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.

 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

// ============================================================================
// gs2_app.hpp -- what the application unit (gs2.cpp) and the instrumentation
// control plane share: the include set they were written against, the SDL app
// state, and the handful of functions each side calls in the other.
//
// The control plane -- snapshots, the CPU/MMU micro-tests, the CTRL and co-pilot
// rails, the headless spike -- used to be ~3,300 lines inside gs2.cpp, the file
// upstream owns and edits most. It now lives in its own units
// (a2gspu_snapshot.cpp, a2gspu_selftests.cpp, a2gspu_ctrl.cpp, a2gspu_spike.cpp),
// so an upstream change to the application loop and a fork change to an
// instrument no longer land in the same file.
// ============================================================================

#include <iostream>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <time.h>
#include <getopt.h>
#include <regex>
#include <memory>

#include "gs2.hpp"
#include "platform-specific/menu.h"
#include "Module_ID.hpp"
#include "paths.hpp"
#include "cpu.hpp"
#include "display/display.hpp"
#include "devices/iwm/iwm_device.hpp"      // the disk controller's snapshot functions (M5)
#include "devices/pdblock3/pdblock3.hpp"   // the block device's snapshot functions (M5)
#include "devices/speaker/speaker.hpp"
#include "platforms.hpp"
#include "util/dialog.hpp"
#include "util/mount.hpp"
#include "util/Connections.hpp"
#include "util/SystemConfig.hpp"
#include "util/SystemSettings.hpp"
#include "ui/OSD.hpp"
#if defined(__EMSCRIPTEN__)
#include "platform-specific/emscripten/web_file_dialog.hpp"
#endif
#include "systemconfig.hpp"
#include "slots.hpp"
#include "videosystem.hpp"
#include "debugger/debugwindow.hpp"
#include "debugger/DebugProtocolServer.hpp"
#include "debugger/BreakpointTable.hpp"
#include "computer.hpp"
#include "mmus/mmu_ii.hpp"
#include "mmus/mmu_iie.hpp"
#include "mmus/mmu_iigs.hpp"
#include "mmus/iigs_memory.hpp"
#include "debugger/disasm.hpp"
#include "devices/adb/keygloo.hpp"
#include "devices/adb/ADB_Micro.hpp"       // the ADB micro's snapshot methods (M5)
#include "devices/es5503/soundglu.hpp"     // the Ensoniq's snapshot functions (M5)
#include "house_fnv.hpp"
#include "bus_trace.hpp"
#include "memvu_storevis.hpp"  // MEMVU_STOREVIS: store-visibility accounting
#include "memvu_istream.hpp"   // MEMVU_OPMIX / MEMVU_IREUSE
#include "memvu_report.hpp"    // memvu_report_all: the spike end and `memvu report`
#include "io_trace.hpp"
#include "mmu_state_trace.hpp"
#include "obs_signal.hpp"    // the Observatory spine (default-OFF; wired in later seams)
#include "obs_iigs.hpp"      // IIgs LEVEL bindings + the boot-fault-context view
#include "iigs_video_summary.hpp"
#include "iigs_toolbox.hpp"
#include <filesystem>
#include "generic_tap.hpp"   // A2GSPU_TAP: generic title-agnostic tap (env-gated, off => no-op)
#include "a2gspu_trap.hpp"   // A2GSPU_TRAP: configurable PC traps (env-gated, off => no-op)
#include "a2gspu_liveness.hpp" // A2GSPU_LIVENESS: screen-hash progress proof every N frames (env-gated)
#include "a2gspu_pace.hpp"     // A2GSPU_PACE: headless frames do not wait for the wall clock
#include "a2record.hpp"        // the record contract: one writer, one shape, one global sink (A2GSPU_RECORDS)

#include "a2gspu_pins.hpp"     // A2GSPU PINS: the identity of a headless run, printed before it starts
#include "iigs_diag.hpp"
#include "a2gspu_coverage.hpp"  // A2GSPU_COVERAGE: execution-coverage bitmap (env-gated)
#include "a2gspu_png.hpp"       // CTRL `png` / `pngc`: HGR/DHGR agent-readable PNG
#include "a2gspu_dhgr.hpp"      // discrete DHGR 4-dot colour + profile export
#include "a2gspu_manifest.hpp"
#include "a2gspu_ctrl_json.hpp"
#include "a2gspu_ctrl_cmds.hpp"  // extracted CTRL verb families
  // help/manifest/oracle — no black boxes
#include "display/filters.hpp"  // generate_filters for dhgr-export NTSC LUT
#include "devices/slot_bus/slot_bus.hpp"
#include "devices/keyboard/keyboard.hpp"
#include "wiz5_config.hpp"   // WIZ5_* title/interp-specific config layer (env-gated, off => no-op)
#include "util/EventTimer.hpp"
#include "ui/SelectSystem.hpp"
#include "ui/EditSystem.hpp"
#include "ui/MainAtlas.hpp"
#include "cpus/cpu_implementations.hpp"
#include "NClock.hpp"
#include "opcodes.hpp"
#include "version.h"
#include "util/Metrics.hpp"
#include "util/DebugHandlerIDs.hpp"
#include "util/printf_helper.hpp"



enum AppPhase {
    PHASE_SYSTEM_SELECT,
    PHASE_EDIT_SYSTEM,
    PHASE_EMULATION,
    PHASE_SHUTTING_DOWN,
};

/* This is "application state" as passed by SDL into the various AppCallbacks routines */
struct GS2AppState {
    AppPhase phase = PHASE_SYSTEM_SELECT;

    // Parsed from command line (persistent across system-select cycles)
    int platform_id = PLATFORM_APPLE_II_PLUS;
    std::vector<disk_mount_t> disks_to_mount;
    std::unique_ptr<SystemConfig> loaded_config;

    // True when the user gave -p PLATFORM or a config file path and we skipped
    // the system selector at startup. In that mode, closing the emulator window
    // quits the app (rather than bouncing back to the selector) — the
    // user expressly asked for one specific machine and there's no
    // "back" to return to.
    bool auto_launched = false;

    // System selection / config editor
    SelectSystem *select_system = nullptr;
    EditSystem *edit_system = nullptr;
    AssetAtlas_t *aa = nullptr;

    // Emulation state
    computer_t *computer = nullptr;

    // External debug protocol (optional; started via --debug / -D)
    std::unique_ptr<DebugProtocolServer> debug_protocol;

    // MMU pointers tracked for cleanup
    MMU_II *mmu_ii = nullptr;
    MMU_IIe *mmu_iie = nullptr;
    MMU_IIgs *mmu_iigs = nullptr;

    // a2gspu headless-boot spike (env-gated; no behaviour change unless
    // A2GSPU_SPIKE_FRAMES is set). Runs N deterministic frames, dumps a
    // renderer-free $E1 oracle + a backbuffer screenshot, then exits.
    bool headless = false;
    int  spike_frames = 0;
};

// ---- defined in gs2.cpp, used by the control plane ------------------------
extern OSD *osd;
bool run_one_frame(computer_t *computer);
void a2record_register(NClock *clk);

#include "a2gspu_snapshot.hpp"
#include "a2gspu_selftests.hpp"
#include "a2gspu_ctrl.hpp"
#include "a2gspu_spike.hpp"
