// Passive TX-wire capture, after SCC timing and outside local loopback.
#pragma once
#include "a2gspu_output.hpp"
#include "a2gspu_rails_registry.hpp"
#include "a2gspu_events.hpp"
inline A2ByteSink g_scc_capture[2];
inline void a2_scc_read_a(char *b, size_t n) {
    std::snprintf(b,n,"status=OK enabled=%d bytes=%llu model=scc-tx-wire binding=%s",g_scc_capture[0].enabled,g_scc_capture[0].bytes,g_scc_capture[0].binding.c_str());
}
inline void a2_scc_read_b(char *b, size_t n) {
    std::snprintf(b,n,"status=OK enabled=%d bytes=%llu model=scc-tx-wire binding=%s",g_scc_capture[1].enabled,g_scc_capture[1].bytes,g_scc_capture[1].binding.c_str());
}
inline a2rails::Registrar a2_scc_reader_a{"A2GSPU_SCC_A", a2_scc_read_a};
inline a2rails::Registrar a2_scc_reader_b{"A2GSPU_SCC_B", a2_scc_read_b};
inline void a2_scc_capture_init(bool present) {
    const char *a = std::getenv("A2GSPU_SCC_A"), *b = std::getenv("A2GSPU_SCC_B");
    if ((a || b) && !present) {
        std::fprintf(stderr,"A2GSPU SCC: FAIL capture requested on a machine without SCC\n"); std::exit(2);
    }
    if (a && b && !std::strcmp(a,b)) {
        std::fprintf(stderr,"A2GSPU SCC: FAIL channel bindings must differ\n"); std::exit(2);
    }
    g_scc_capture[0].open("SCC_A",a); g_scc_capture[1].open("SCC_B",b);
}
