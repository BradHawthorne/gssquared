#pragma once
// ============================================================================
// a2gspu_run_kind.hpp -- is this run automated?
//
// One test, used everywhere a host-side default differs between a person at the
// window and a harness: host preferences (gs2.cpp), where battery RAM lives
// (a2gspu_bram_pin.hpp), and what a capture file is called (FileDevice.hpp).
// Automated means a headless spike (A2GSPU_SPIKE_FRAMES) or a CTRL-rail session
// (A2GSPU_CTRL). A harness needs inputs it controls and outputs it can find; a
// person gets upstream's per-user behaviour.
// ============================================================================
#include <cstdlib>

inline bool a2gspu_automated_run() {
    return std::getenv("A2GSPU_SPIKE_FRAMES") || std::getenv("A2GSPU_CTRL");
}
