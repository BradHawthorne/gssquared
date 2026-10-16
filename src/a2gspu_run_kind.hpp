#pragma once
// ============================================================================
// a2gspu_run_kind.hpp -- is this run automated?
//
// One test, used everywhere a host-side default differs between a person at the
// window and a harness: host preferences (gs2.cpp), where battery RAM lives
// (a2gspu_bram_pin.hpp), and what a capture file is called (FileDevice.hpp).
// Automated means a headless spike (A2GSPU_SPIKE_FRAMES), a CTRL-rail session
// (A2GSPU_CTRL), or explicit agent socket mode (A2GSPU_AGENT). A harness needs
// inputs it controls and outputs it can find; a
// person gets upstream's per-user behaviour.
// ============================================================================
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include "a2gspu_rails_registry.hpp"
#include "a2gspu_ctrl_json.hpp"

inline bool g_a2gspu_agent_bootstrap_complete=false;
inline bool a2gspu_agent_socket_enabled() {
    const char *value=std::getenv("A2GSPU_AGENT");
    if(!value || !*value || !std::strcmp(value,"0"))return false;
    if(!std::strcmp(value,"1"))return true;
    std::fprintf(stderr,"A2GSPU_AGENT: FAIL expected 0 or 1\n");std::exit(2);
}
inline void a2gspu_agent_mode_format(char *out,size_t size) {
    const bool enabled=a2gspu_agent_socket_enabled();
    a2ctrljson::native_payload="{\"schema\":\"a2agent-mode-v1\",\"enabled\":"+std::string(enabled?"true":"false")+",\"bootstrap_complete\":"+(g_a2gspu_agent_bootstrap_complete?"true":"false")+",\"model\":\"shared-CTRL-instrument-bootstrap; paused socket ownership\"}";
    std::snprintf(out,size,"status=OK agent=%d bootstrap=%d model=shared-CTRL-instrument-bootstrap",enabled?1:0,g_a2gspu_agent_bootstrap_complete?1:0);
}
inline a2rails::Registrar g_reader_agent_mode{"A2GSPU_AGENT",a2gspu_agent_mode_format};

inline bool a2gspu_automated_run() {
    return std::getenv("A2GSPU_SPIKE_FRAMES") || std::getenv("A2GSPU_CTRL") || a2gspu_agent_socket_enabled();
}
