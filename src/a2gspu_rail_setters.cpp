/*
 *   GSSquared - Apple ][ / ][+ / //e (Emhanced) / //c / IIgs Emulator
 *   Copyright (C) 2025 Jawaid Bazyar
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
// a2gspu_rail_setters.cpp -- rails that can be armed while the machine runs.
//
// Each of these rails used to be an inline getenv block at headless start
// (a2gspu_spike.cpp), read once, so an agent that found it needed one had to
// relaunch and lose the session. Each block is now ONE apply function,
// registered with the rail (a2rails::SetterRegistrar): the headless start calls
// it with the environment's value, in the order the blocks ran, and the CTRL
// verbs `rail set NAME VALUE` / `rail unset NAME` call the same function live.
// One parser per rail, so the launch form and the live form cannot disagree.
//
// Everything here arms observation, tracing or break reporting. Nothing writes
// guest memory; the unarmed-hash gate covers the ones that tap the CPU.
#include "gs2_app.hpp"
#include "a2gspu_rail_setters.hpp"
#include "devices/a2gspu/a2gspu_cmdreg.hpp"   // the card model's command log
#include "devices/uthernet2/u2_rail.hpp"      // the Uthernet II's records
#include "a2gspu_writeback.hpp"
#include "a2gspu_ctrl_json.hpp"

namespace {
void writeback_policy_format(char *out,size_t capacity) {
    const auto policy=a2writeback::current();
    a2ctrljson::native_payload=policy.json();
    snprintf(out,capacity,"status=OK schema=a2writeback-policy-v1 requested=%d suppressed=%d flush_at_spike_end=%d model=current-env-presence/spike-end-flush-only",
        policy.requested()?1:0,policy.suppressed()?1:0,policy.flush()?1:0);
}
a2rails::Registrar read_writeback{"A2GSPU_WRITEBACK",writeback_policy_format};
a2rails::Registrar read_no_writeback{"A2GSPU_NO_WRITEBACK",writeback_policy_format};
a2rails::Registrar read_legacy_writeback{"WIZ5_WRITEBACK",writeback_policy_format};
a2rails::Registrar read_legacy_no_writeback{"WIZ5_NO_WRITEBACK",writeback_policy_format};

using a2rails::SET_OK;
using a2rails::SET_REFUSED;
using a2rails::SET_INVALID;

// ---- shared parsing ------------------------------------------------------------
// Strict where the old blocks were lenient only in ways that hid a mistake: a
// number that does not parse is INVALID, where strtoul used to arm address 0.
bool parse_hex(const char *s, uint32_t &out) {
    if (!s || !*s) return false;
    char *e = nullptr;
    unsigned long v = strtoul(s, &e, 16);
    if (e == s) return false;
    out = (uint32_t)v;
    return true;
}
bool parse_int(const char *s, int &out) {
    if (!s || !*s) return false;
    char *e = nullptr;
    long v = strtol(s, &e, 10);
    if (e == s || *e) return false;
    out = (int)v;
    return true;
}
// A flag rail: any value arms it (the environment's presence did), nullptr disarms.
int flag(bool &g, const char *value, const char *what, char *msg, size_t n) {
    g = (value != nullptr);
    snprintf(msg, n, "%s: %s", what, g ? "enabled" : "disabled");
    return SET_OK;
}

// ---- flags ------------------------------------------------------------------------
int set_errhook(const char *v, char *m, size_t n)   { return flag(g_iigs_errhook_enabled, v, "A2GSPU_ERRHOOK (GS/OS dispatch errors in the toolbox trace)", m, n); }
int set_brkdump(const char *v, char *m, size_t n)   { return flag(g_iigs_brkdump_enabled, v, "A2GSPU_BRKDUMP (registers on BRK)", m, n); }
int set_stopfault(const char *v, char *m, size_t n) { return flag(g_iigs_stop_on_fault, v, "A2GSPU_STOP_ON_FAULT (halt on BRK/COP/SysFail)", m, n); }
int set_brkmem(const char *v, char *m, size_t n)    { return flag(g_brkmem_on, v, "A2GSPU_BRKMEM (PC ring + crash-site bytes on BRK)", m, n); }
int set_symsuspect(const char *v, char *m, size_t n){ return flag(g_iigs_sym_suspect, v, "A2GSPU_SYM_SUSPECT (flag reused local labels)", m, n); }

int set_lctrace(const char *v, char *m, size_t n) {
    g_lctrace_on = (v != nullptr);
    g_lctrace_hits = 0;
    snprintf(m, n, "A2GSPU_LCTRACE: %s (language-card switch accesses; hit cap reset)", g_lctrace_on ? "enabled" : "disabled");
    return SET_OK;
}
int set_intlog(const char *v, char *m, size_t n) {
    g_intlog_on = (v != nullptr);
    g_intlog_hits = 0;
    snprintf(m, n, "IIGS INTLOG: %s (IRQ/BRK/COP entry logging)", g_intlog_on ? "enabled" : "disabled");
    return SET_OK;
}
int set_traceext(const char *v, char *m, size_t n) {
    g_a2gspu_trace_ext = (v != nullptr);
    snprintf(m, n, "IIGS TRACE_EXT: %s (recorded DBR/DP/NClock cycles; no inferred video phase)", g_a2gspu_trace_ext ? "enabled" : "disabled");
    return SET_OK;
}
int set_modetrace(const char *v, char *m, size_t n) {
    g_modetrace_on = (v != nullptr);
    snprintf(m, n, "IIGS MODETRACE: %s (e/M/X transitions)", g_modetrace_on ? "enabled" : "disabled");
    return SET_OK;
}
int set_retguard(const char *v, char *m, size_t n) {
    g_retguard_on = (v != nullptr);
    g_retguard_strict = v && !strcmp(v, "strict");
    g_retguard_hits = 0;
    if (!v) { snprintf(m, n, "IIGS RETGUARD: disabled"); return SET_OK; }
    snprintf(m, n, "IIGS RETGUARD: enabled (%s)",
             g_retguard_strict ? "strict: BRK-target + symbol-gap" : "BRK-target returns into $00/$01");
    return SET_OK;
}

// ---- numbers (unset restores the default the program started with) -------------
int set_hang(const char *v, char *m, size_t n) {
    static const int def = g_iigs_hang_threshold;
    int x = def;
    if (v && (!parse_int(v, x) || x <= 0)) { snprintf(m, n, "A2GSPU_HANG_THRESHOLD: want a frame count > 0, got '%s'", v); return SET_INVALID; }
    g_iigs_hang_threshold = x;
    snprintf(m, n, "A2GSPU_HANG_THRESHOLD: %d frames%s", x, v ? "" : " (default)");
    return SET_OK;
}
int set_watchmax(const char *v, char *m, size_t n) {
    static const int def = g_watch_max;
    int x = def;
    if (v && (!parse_int(v, x) || x < 0)) { snprintf(m, n, "A2GSPU_WATCH_MAX: want a count >= 0, got '%s'", v); return SET_INVALID; }
    g_watch_max = x;
    snprintf(m, n, "A2GSPU_WATCH_MAX: %d%s", x, v ? "" : " (default)");
    return SET_OK;
}
int set_valtrapmax(const char *v, char *m, size_t n) {
    static const int def = g_valtrap_max;
    int x = def;
    if (v && (!parse_int(v, x) || x < 0)) { snprintf(m, n, "A2GSPU_VALTRAP_MAX: want a count >= 0, got '%s'", v); return SET_INVALID; }
    g_valtrap_max = x;
    snprintf(m, n, "A2GSPU_VALTRAP_MAX: %d%s", x, v ? "" : " (default)");
    return SET_OK;
}
int set_condtrapmax(const char *v, char *m, size_t n) {
    static const int def = g_condtrap_max;
    int x = def;
    if (v && (!parse_int(v, x) || x <= 0)) { snprintf(m, n, "A2GSPU_CONDTRAP_MAX: want a count > 0, got '%s'", v); return SET_INVALID; }
    g_condtrap_max = x;
    snprintf(m, n, "A2GSPU_CONDTRAP_MAX: %d%s", x, v ? "" : " (default)");
    return SET_OK;
}
int set_loadtracemin(const char *v, char *m, size_t n) {
    static const uint32_t def = g_lt_min;
    int x = (int)def;
    if (v && (!parse_int(v, x) || x <= 0)) { snprintf(m, n, "A2GSPU_LOADTRACE_MIN: want a byte count > 0, got '%s'", v); return SET_INVALID; }
    g_lt_min = (uint32_t)x;
    snprintf(m, n, "A2GSPU_LOADTRACE_MIN: %u%s", g_lt_min, v ? "" : " (default)");
    return SET_OK;
}
int set_stackwatchjump(const char *v, char *m, size_t n) {
    static const int def = g_stackwatch_jump;
    uint32_t x = (uint32_t)def;
    if (v && (!parse_hex(v, x) || x == 0)) { snprintf(m, n, "A2GSPU_STACKWATCH_JUMP: want a hex jump > 0, got '%s'", v); return SET_INVALID; }
    g_stackwatch_jump = (int)x;
    snprintf(m, n, "A2GSPU_STACKWATCH_JUMP: $%X%s", (unsigned)g_stackwatch_jump, v ? "" : " (default)");
    return SET_OK;
}
int set_calltracen(const char *v, char *m, size_t n) {
    static const int def = g_calltrace_n;
    int x = def;
    if (v && (!parse_int(v, x) || x <= 0)) { snprintf(m, n, "A2GSPU_CALLTRACE_N: want a count > 0, got '%s'", v); return SET_INVALID; }
    g_calltrace_n = x;
    snprintf(m, n, "A2GSPU_CALLTRACE_N: %d%s", x, v ? "" : " (default)");
    return SET_OK;
}
int set_calltraceskip(const char *v, char *m, size_t n) {
    static const int def = g_calltrace_skip;
    int x = def;
    if (v && (!parse_int(v, x) || x < 0)) { snprintf(m, n, "A2GSPU_CALLTRACE_SKIP: want a count >= 0, got '%s'", v); return SET_INVALID; }
    g_calltrace_skip = x;
    snprintf(m, n, "A2GSPU_CALLTRACE_SKIP: %d%s", x, v ? "" : " (default)");
    return SET_OK;
}
int set_pokenth(const char *v, char *m, size_t n) {
    int x = 1;
    if (v && (!parse_int(v, x) || x <= 0)) { snprintf(m, n, "A2GSPU_POKE_NTH: want a hit number > 0, got '%s'", v); return SET_INVALID; }
    g_poke_nth = x;
    snprintf(m, n, "A2GSPU_POKE_NTH: hit #%d%s", x, v ? "" : " (default)");
    return SET_OK;
}
int set_livenessstall(const char *v, char *m, size_t n) {
    static const int def = g_liveness_stall;
    int x = def;
    if (v && (!parse_int(v, x) || x <= 0)) { snprintf(m, n, "A2GSPU_LIVENESS_STALL: want a sample count > 0, got '%s'", v); return SET_INVALID; }
    g_liveness_stall = x;
    snprintf(m, n, "A2GSPU_LIVENESS_STALL: %d samples%s", x, v ? "" : " (default)");
    return SET_OK;
}
// A2GSPU_LOADMAP=<file>|1 -- what the System Loader placed where. Arms the call
// tracker so loads are seen from now on, and gives each load a record: <file>,
// or with 1 the global sink / stdout. Unset closes the sink and stops the
// records; the map already built keeps naming PCs.
int set_loadmap(const char *v, char *m, size_t n) {
    g_iigs_loadmap_armed=false;
    a2outputs::finish(g_iigs_loadmap_out,"g_iigs_loadmap_out");
    g_iigs_loadmap_path.clear();
    if (!v) {
        g_iigs_loadmap_armed = false;
        snprintf(m, n, "A2GSPU_LOADMAP: records off (%u load(s) kept for names)", (unsigned)g_iigs_loads.size());
        return SET_OK;
    }
    if (!*v) { snprintf(m, n, "A2GSPU_LOADMAP: want a file or 1, got ''"); return SET_INVALID; }
    if (strcmp(v, "1")) {
        g_iigs_loadmap_out = fopen(v, "wb");
    a2outputs::track(g_iigs_loadmap_out,v);
        if (!g_iigs_loadmap_out) { snprintf(m, n, "A2GSPU_LOADMAP: cannot open '%s'", v); return SET_INVALID; }
        g_iigs_loadmap_path = v;
    }
    g_iigs_loadmap_armed = true;
    g_iigs_loadmap_armed_frame = a2record::frame_now();
    iigs_callctx_arm();
    snprintf(m, n, "A2GSPU_LOADMAP: System Loader loads -> %s (%u already known)",
             g_iigs_loadmap_out ? v : (a2record::g_global ? "A2GSPU_RECORDS" : "stdout"), (unsigned)g_iigs_loads.size());
    return SET_OK;
}
// ---- progress proof and memory visibility ----------------------------------------------
// The platform comes from the CPU the names use: only a IIgs MMU can read the Mega II
// image at $E1 (the liveness rail's own test). Both launch paths name it first.
int open_sink(FILE *&sink, const char *v, const char *rail, char *m, size_t n);   // below, with the other sinks
bool rail_platform_iigs() {
    return g_a2gspu_cpu_for_names && g_a2gspu_cpu_for_names->mmu && g_a2gspu_cpu_for_names->mmu->probe_readable(0xE12000);
}
int set_liveness(const char *v, char *m, size_t n) {
    if (!v) {
        const bool was = g_liveness_armed;
        a2gspu_liveness_stop();
        snprintf(m, n, "A2GSPU_LIVENESS: %s", was ? "disarmed (its close-out is printed)" : "not armed");
        return SET_OK;
    }
    int x = 0;
    if (!parse_int(v, x) || x <= 0) { snprintf(m, n, "A2GSPU_LIVENESS: want a frame interval > 0, got '%s'", v); return SET_INVALID; }
    a2gspu_liveness_start(x, rail_platform_iigs());
    snprintf(m, n, "A2GSPU_LIVENESS: every %d frame(s), counted from now; out=%s", x, g_liveness_out ? "ndjson" : "stdout");
    return SET_OK;
}
int set_livenessout(const char *v, char *m, size_t n) { return open_sink(g_liveness_out, v, "A2GSPU_LIVENESS_OUT (NDJSON)", m, n); }

// STOREVIS and LOADVIS share the classifier and the per-granule table that names
// their top writers and readers; either one arms both of those.
void memvu_vis_common() {
    memvu_sv_have_shadow = rail_platform_iigs();
    if (!memvu_blame_on && memvu_blame_init()) {
        memvu_blame_reset(); memvu_blame_on = true; iigs_callctx_arm(); memvu_callctx_fn = iigs_call_context_word;
    }
}
int set_memvu_storevis(const char *v, char *m, size_t n) {
    if (!v) { memvu_sv_on = false; snprintf(m, n, "MEMVU_STOREVIS: disarmed"); return SET_OK; }
    memvu_reset();
    memvu_sv_on = true;
    memvu_vis_common();
    snprintf(m, n, "MEMVU: armed (STOREVIS, model=%s) -- store and load counters zeroed", memvu_sv_have_shadow ? "iigs-shadow-v1" : "iie-basic-v1");
    return SET_OK;
}
int set_memvu_loadvis(const char *v, char *m, size_t n) {
    if (!v) { memvu_lv_on = false; snprintf(m, n, "MEMVU_LOADVIS: disarmed"); return SET_OK; }
    memvu_reset();
    memvu_lv_on = true;
    memvu_vis_common();
    snprintf(m, n, "MEMVU: armed (LOADVIS, model=%s) -- store and load counters zeroed", memvu_sv_have_shadow ? "iigs-shadow-v1" : "iie-basic-v1");
    return SET_OK;
}
int set_memvu_banks(const char *v, char *m, size_t n) { return flag(g_memvu_sv_banks, v, "MEMVU_STOREVIS_BANKS (per-bank lines in the STOREVIS report)", m, n); }
int set_memvu_opmix(const char *v, char *m, size_t n) {
    if (!v) { memvu_op_on = false; snprintf(m, n, "MEMVU_OPMIX: disarmed"); return SET_OK; }
    memvu_istream_reset();
    memvu_op_on = true;
    snprintf(m, n, "MEMVU ISTREAM: armed (OPMIX) -- instruction-stream counters zeroed");
    return SET_OK;
}
int set_memvu_ireuse(const char *v, char *m, size_t n) {
    if (!v) { memvu_ir_on = false; snprintf(m, n, "MEMVU_IREUSE: disarmed"); return SET_OK; }
    if (!memvu_ir_init(v)) {
        memvu_ir_on = false;
        snprintf(m, n, "MEMVU_IREUSE: ** REFUSED ** '%s' -- want <lines>,<linesize>, both powers of two (e.g. 512,64)", v);
        return SET_REFUSED;
    }
    memvu_istream_reset();
    memvu_ir_on = true;
    snprintf(m, n, "MEMVU ISTREAM: armed (IREUSE %s) -- instruction-stream counters zeroed", v);
    return SET_OK;
}
int set_memvu_workset(const char *v, char *m, size_t n) {
    if (!v) { memvu_ws_on = false; snprintf(m, n, "MEMVU_WORKSET: disarmed"); return SET_OK; }
    if (!memvu_ws_init()) { memvu_ws_on = false; snprintf(m, n, "MEMVU_WORKSET: ** REFUSED ** allocation failed"); return SET_REFUSED; }
    memvu_istream_reset();
    memvu_ws_on = true;
    snprintf(m, n, "MEMVU ISTREAM: armed (WORKSET) -- instruction-stream counters zeroed");
    return SET_OK;
}
// MEMVU_SEAM=<ratio>,<slowmul>,<bufdepth>,<shadowreads> -- the seam model. No
// defaults: a model whose parameters are implicit invites its output to be quoted
// as a property of the software rather than of software-plus-parameters.
int set_memvu_seam(const char *v, char *m, size_t n) {
    if (!v) { memvu_seam_on = false; snprintf(m, n, "MEMVU_SEAM: disarmed"); return SET_OK; }
    if (!memvu_seam_init(v)) {
        snprintf(m, n, "MEMVU_SEAM: ** REFUSED ** '%s' -- want <ratio>,<slowmul>,<bufdepth>,<shadowreads 0|1> (e.g. 36,3,8,1)", v);
        return SET_REFUSED;
    }
    // Stall attribution (M2) needs the per-instruction code address, which the
    // blame tracker keeps; arm both before the model starts charging.
    const bool attr = memvu_seam_attr_init();
    if (memvu_blame_init()) { if (!memvu_blame_on) memvu_blame_reset(); memvu_blame_on = true; }
    memvu_seam_reset();
    memvu_seam_on = true;
    memvu_sv_on = true;   // the model rides the store/load taps
    memvu_lv_on = true;
    memvu_sv_have_shadow = rail_platform_iigs();
    snprintf(m, n, "MEMVU SEAM: armed ratio=%u slowmul=%u bufdepth=%u shadowreads=%s%s",
             memvu_seam_ratio, memvu_seam_slowmul, memvu_seam_bufdepth,
             memvu_seam_shadowreads_local ? "local" : "routed",
             attr ? "" : " -- stall attribution tables could not be allocated: totals only");
    return SET_OK;
}
// MEMVU_BLAME[=<topN>] -- which code generates the externally-visible traffic.
int set_memvu_blame(const char *v, char *m, size_t n) {
    if (!v) { g_memvu_blame_topn = 0; snprintf(m, n, "MEMVU_BLAME: report off (the table stays while STOREVIS/LOADVIS use it)"); return SET_OK; }
    if (!memvu_blame_init()) { snprintf(m, n, "MEMVU_BLAME: ** REFUSED ** allocation failed"); return SET_REFUSED; }
    memvu_blame_reset();
    memvu_blame_on = true; iigs_callctx_arm(); memvu_callctx_fn = iigs_call_context_word;
    memvu_sv_on = true;   // blame rides the store/load classification
    memvu_lv_on = true;
    memvu_sv_have_shadow = rail_platform_iigs();
    int x = atoi(v); if (x <= 0) x = 20;
    g_memvu_blame_topn = x;
    snprintf(m, n, "MEMVU BLAME: armed (top %d code granules) -- blame table zeroed", x);
    return SET_OK;
}

// ---- the generic tap (A2GSPU_TAP*) ------------------------------------------------------
int set_tap_pcs(const char *v, char *m, size_t n) {
    gtap_set_pcs(v);
    snprintf(m, n, "A2GSPU_TAP_PCS: %d watch PC(s)%s", g_gtap.nwatch, (v && !g_gtap.nwatch) ? " -- nothing parsed; the tap will never fire" : "");
    return (v && !g_gtap.nwatch) ? SET_INVALID : SET_OK;
}
int set_tap_ptrs(const char *v, char *m, size_t n) {
    gtap_set_ptrs(v);
    snprintf(m, n, "A2GSPU_TAP_PTRS: %d pointer field(s)", g_gtap.nptr);
    return (v && !g_gtap.nptr) ? SET_INVALID : SET_OK;
}
int set_tap_max(const char *v, char *m, size_t n) {
    int x = 0;
    if (v && (!parse_int(v, x) || x < 0)) { snprintf(m, n, "A2GSPU_TAP_MAX: want an event cap >= 0 (0 = unlimited), got '%s'", v); return SET_INVALID; }
    g_gtap.cap = (uint64_t)x;
    snprintf(m, n, "A2GSPU_TAP_MAX: %s", x ? "capped" : "unlimited");
    return SET_OK;
}
int set_tap_dump_on(const char *v, char *m, size_t n) {
    snprintf(g_gtap.dump_on, sizeof g_gtap.dump_on, "%s", v ? v : "");
    gtap_apply_dump_on();
    int k = 0; for (int i = 0; i < g_gtap.nwatch; i++) if (g_gtap.watch[i].dumpop) k++;
    snprintf(m, n, "A2GSPU_TAP_DUMP_ON: %s (%d watch PC(s) take a resident dump)", v ? v : "off", k);
    return SET_OK;
}
int set_tap_cyc_ms(const char *v, char *m, size_t n) {
    double x = v ? atof(v) : 1020.484;
    if (v && x <= 0) { snprintf(m, n, "A2GSPU_TAP_CYC_PER_MS: want cycles per ms > 0, got '%s'", v); return SET_INVALID; }
    g_gtap.cyc_per_ms = x; g_gtap.cyc_per_sec = x * 1000.0;
    snprintf(m, n, "A2GSPU_TAP_CYC_PER_MS: %.3f (per second %.0f)", x, g_gtap.cyc_per_sec);
    return SET_OK;
}
int set_tap_cyc_sec(const char *v, char *m, size_t n) {
    double x = v ? atof(v) : g_gtap.cyc_per_ms * 1000.0;
    if (v && x <= 0) { snprintf(m, n, "A2GSPU_TAP_CYC_PER_SEC: want cycles per second > 0, got '%s'", v); return SET_INVALID; }
    g_gtap.cyc_per_sec = x;
    snprintf(m, n, "A2GSPU_TAP_CYC_PER_SEC: %.0f", x);
    return SET_OK;
}
int set_tap(const char *v, char *m, size_t n) {
    if (!gtap_set_out(v)) { snprintf(m, n, "A2GSPU_TAP: cannot open '%s'", v); return SET_INVALID; }
    if (!v) { snprintf(m, n, "A2GSPU_TAP: closed"); return SET_OK; }
    static bool s_atexit = false;
    if (!s_atexit) { s_atexit = true; atexit(generic_tap_exit_close); }
    snprintf(m, n, "A2GSPU_TAP: armed -> %s (%d watch PCs, %d ptr fields)%s", v, g_gtap.nwatch, g_gtap.nptr,
             g_gtap.nwatch ? "" : " -- WARNING no A2GSPU_TAP_PCS set: the tap will never fire");
    return SET_OK;
}
int set_tap_session(const char *v, char *m, size_t n) {
    if (!gtap_set_session(v)) { snprintf(m, n, "A2GSPU_TAP_SESSION: cannot write into '%s' (does the directory exist?)", v); return SET_INVALID; }
    if (!v) { snprintf(m, n, "A2GSPU_TAP_SESSION: closed"); return SET_OK; }
    static bool s_atexit = false;
    if (!s_atexit) { s_atexit = true; atexit(generic_tap_exit_close); }
    snprintf(m, n, "A2GSPU_TAP_SESSION: armed -> %s (dump=%ds zp=%ds key_post=%df)", v, g_gtap.dump_every_s, g_gtap.zp_every_s, g_gtap.key_post_frames);
    return SET_OK;
}
int tap_seconds(int &g, int def, const char *v, const char *rail, char *m, size_t n) {
    int x = def;
    if (v && (!parse_int(v, x) || x <= 0)) { snprintf(m, n, "%s: want seconds > 0, got '%s'", rail, v); return SET_INVALID; }
    g = x;
    snprintf(m, n, "%s: %ds%s", rail, x, v ? "" : " (default)");
    return SET_OK;
}
int set_tap_dump_every(const char *v, char *m, size_t n) { return tap_seconds(g_gtap.dump_every_s, 60, v, "A2GSPU_TAP_DUMP_EVERY", m, n); }
int set_tap_zp_every(const char *v, char *m, size_t n)   { return tap_seconds(g_gtap.zp_every_s, 5, v, "A2GSPU_TAP_ZP_EVERY", m, n); }
int set_tap_key_post(const char *v, char *m, size_t n) {
    int x = 30;
    if (v && (!parse_int(v, x) || x < 0)) { snprintf(m, n, "A2GSPU_TAP_KEY_POST_FRAMES: want frames >= 0, got '%s'", v); return SET_INVALID; }
    g_gtap.key_post_frames = x;
    snprintf(m, n, "A2GSPU_TAP_KEY_POST_FRAMES: %d%s", x, v ? "" : " (default)");
    return SET_OK;
}

// ---- milestones, ROM symbols, the watch record's interpreter pointer ----------------------
int set_milestones(const char *v, char *m, size_t n) {
    if (!v) { g_milestones.clear(); g_milestones_on = false; g_milestones_unhit = 0; snprintf(m, n, "A2GSPU_MILESTONES: cleared"); return SET_OK; }
    std::string why;
    if (!iigs_milestones_load(v, why)) { snprintf(m, n, "A2GSPU_MILESTONES: %s", why.c_str()); return SET_INVALID; }
    snprintf(m, n, "A2GSPU_MILESTONES: %zu loaded from '%s' (the ledger is replaced; frames count from here)", g_milestones.size(), v);
    return SET_OK;
}
int set_rom_symbols(const char *v, char *m, size_t n) {
    if (!v) { snprintf(m, n, "A2GSPU_ROM_SYMBOLS: loaded symbols stay loaded (nothing to unset)"); return SET_REFUSED; }
    FILE *probe = fopen(v, "rb");
    if (!probe) { snprintf(m, n, "A2GSPU_ROM_SYMBOLS: cannot open '%s'", v); return SET_INVALID; }
    fclose(probe);
    const size_t before = g_iigs_rom_syms.size();
    iigs_rom_symbols_load(v);
    snprintf(m, n, "A2GSPU_ROM_SYMBOLS: '%s' -> %zu ROM symbols active (%zu added)", v, g_iigs_rom_syms.size(), g_iigs_rom_syms.size() - before);
    return SET_OK;
}
int set_watch_ipc_zp(const char *v, char *m, size_t n) {
    uint32_t zp = 0;
    if (v && (!parse_hex(v, zp) || zp > 0xFF)) { snprintf(m, n, "A2GSPU_WATCH_IPC_ZP: want a hex zero-page address, got '%s'", v); return SET_INVALID; }
    g_watch_ipc_zp = v ? (int)zp : 0;
    snprintf(m, n, "A2GSPU_WATCH_IPC_ZP: %s", v ? "watch records carry ipc from that pointer" : "off (ipc=0)");
    return SET_OK;
}
int set_keyram(const char *v, char *m, size_t n) {
    g_wiz5_keyram_on = (v != nullptr);
    if (g_wiz5_keyram_on && g_gtap.sess_on) g_wiz5_session_dir = g_gtap.dir;
    snprintf(m, n, "A2GSPU_KEYRAM: %s -> %s", g_wiz5_keyram_on ? "per-keystroke RAM dump ($0800-$BFFF) enabled" : "disabled",
             g_wiz5_session_dir.empty() ? "(no session dir: set A2GSPU_TAP_SESSION)" : g_wiz5_session_dir.c_str());
    return SET_OK;
}

// A2GSPU_CARD_CMDLOG=<file>|1 -- every frame the host sends the card's command
// register, and every byte run the assembler drops, as records.
int set_card_cmdlog(const char *v, char *m, size_t n) {
    a2outputs::finish(g_a2gspu_cmdlog_out,"g_a2gspu_cmdlog_out");
    g_a2gspu_cmdlog_path.clear();
    if (!v) { g_a2gspu_cmdlog_on = false; snprintf(m, n, "A2GSPU_CARD_CMDLOG: off"); return SET_OK; }
    if (!*v) { snprintf(m, n, "A2GSPU_CARD_CMDLOG: want a file or 1, got ''"); return SET_INVALID; }
    if (strcmp(v, "1")) {
        g_a2gspu_cmdlog_out = fopen(v, "wb");
    a2outputs::track(g_a2gspu_cmdlog_out,v);
        if (!g_a2gspu_cmdlog_out) { snprintf(m, n, "A2GSPU_CARD_CMDLOG: cannot open '%s'", v); return SET_INVALID; }
        g_a2gspu_cmdlog_path = v;
    }
    g_a2gspu_cmdlog_on = true;
    snprintf(m, n, "A2GSPU_CARD_CMDLOG: card command frames -> %s%s", g_a2gspu_cmdlog_out ? v : (a2record::g_global ? "A2GSPU_RECORDS" : "stdout"),
             g_a2gspu_cmdreg ? "" : " (no card in the card model in this machine yet)");
    return SET_OK;
}

// A2GSPU_U2_EVENTS=<file>|1 -- the Uthernet II's socket commands, host-caused
// status changes and drops, as records.
int set_u2_events(const char *v, char *m, size_t n) {
    a2outputs::finish(g_u2_events_out,"g_u2_events_out");
    g_u2_events_path.clear();
    if (!v) { g_u2_events_on = false; snprintf(m, n, "A2GSPU_U2_EVENTS: off"); return SET_OK; }
    if (!*v) { snprintf(m, n, "A2GSPU_U2_EVENTS: want a file or 1, got ''"); return SET_INVALID; }
    if (strcmp(v, "1")) {
        g_u2_events_out = fopen(v, "wb");
    a2outputs::track(g_u2_events_out,v);
        if (!g_u2_events_out) { snprintf(m, n, "A2GSPU_U2_EVENTS: cannot open '%s'", v); return SET_INVALID; }
        g_u2_events_path = v;
    }
    g_u2_events_on = true;
    snprintf(m, n, "A2GSPU_U2_EVENTS: Uthernet II records -> %s%s", g_u2_events_out ? v : (a2record::g_global ? "A2GSPU_RECORDS" : "stdout"),
             uthernet2_A2GSPU_present() ? "" : " (no Uthernet II in this machine yet)");
    return SET_OK;
}

int set_keydelay(const char *v, char *m, size_t n) {
    int x = 0;
    if (v && (!parse_int(v, x) || x < 0)) { snprintf(m, n, "A2GSPU_KEYSCHED_DELAY: want frames >= 0, got '%s'", v); return SET_INVALID; }
    g_wiz5_key_delay = x;
    snprintf(m, n, "A2GSPU_KEYSCHED_DELAY: %d frame(s) per key%s", x, x ? "" : " (unpaced)");
    return SET_OK;
}

// ---- addressed traps ---------------------------------------------------------------
// A2GSPU_CONDTRAP="bank:pc@f=v" -- at PC, when flag f (c/z/i/d/x/m/v/n) == v, report
// the PC that last changed that flag.
int set_condtrap(const char *v, char *m, size_t n) {
    if (!v) { g_condtrap_on = false; snprintf(m, n, "A2GSPU_CONDTRAP: disabled"); return SET_OK; }
    const char *p = v;
    char *e = nullptr;
    uint32_t bank = (uint32_t)strtoul(p, &e, 16);
    if (e == p || *e != ':') { snprintf(m, n, "A2GSPU_CONDTRAP: want bank:pc@f=v, got '%s'", v); return SET_INVALID; }
    p = e + 1;
    uint32_t pc = (uint32_t)strtoul(p, &e, 16);
    if (e == p) { snprintf(m, n, "A2GSPU_CONDTRAP: want bank:pc@f=v, got '%s'", v); return SET_INVALID; }
    p = e;
    uint8_t bit = 0, want = 1;
    if (*p == '@') {
        p++;
        char f = *p ? *p++ : 'c';
        int val = 1;
        if (*p == '=') { p++; val = atoi(p); }
        const char *fl = "czidxmvn";
        const char *pos = strchr(fl, f);
        if (!pos || !f) { snprintf(m, n, "A2GSPU_CONDTRAP: flag must be one of c z i d x m v n, got '%c'", f); return SET_INVALID; }
        bit = (uint8_t)(pos - fl);
        want = val ? 1 : 0;
    }
    g_condtrap_pc = ((bank & 0xFF) << 16) | (pc & 0xFFFF);
    g_condtrap_bit = bit; g_condtrap_want = want; g_condtrap_hits = 0;
    g_condtrap_on = true;
    snprintf(m, n, "A2GSPU_CONDTRAP: trap $%06X when flag-bit-%d=%d max=%d",
             g_condtrap_pc, g_condtrap_bit, g_condtrap_want, g_condtrap_max);
    return SET_OK;
}
// A2GSPU_LOADTRACE="bank[:lo-hi]" -- log contiguous write bursts into the region.
int set_loadtrace(const char *v, char *m, size_t n) {
    if (!v) { g_loadtrace_on = false; snprintf(m, n, "A2GSPU_LOADTRACE: disabled"); return SET_OK; }
    const char *p = v;
    char *e = nullptr;
    uint32_t bank = (uint32_t)strtoul(p, &e, 16);
    if (e == p) { snprintf(m, n, "A2GSPU_LOADTRACE: want bank[:lo-hi], got '%s'", v); return SET_INVALID; }
    p = e;
    uint32_t lo = 0x0000, hi = 0xFFFF;
    if (*p == ':') { p++; lo = (uint32_t)strtoul(p, &e, 16); p = e; if (*p == '-') { p++; hi = (uint32_t)strtoul(p, &e, 16); } }
    if (hi < lo) { snprintf(m, n, "A2GSPU_LOADTRACE: range end below start in '%s'", v); return SET_INVALID; }
    g_lt_lo = ((bank & 0xFF) << 16) | (lo & 0xFFFF);
    g_lt_hi = ((bank & 0xFF) << 16) | (hi & 0xFFFF);
    g_loadtrace_on = true;
    snprintf(m, n, "A2GSPU_LOADTRACE: track %06X-%06X min=%u", g_lt_lo, g_lt_hi, g_lt_min);
    return SET_OK;
}
// A2GSPU_PCTRAP="bank:lo-hi" -- one-shot dump of registers + PC ring on first entry.
int set_pctrap(const char *v, char *m, size_t n) {
    if (!v) { g_pctrap_active = false; snprintf(m, n, "A2GSPU_PCTRAP: disabled"); return SET_OK; }
    const char *p = v;
    char *e = nullptr;
    uint32_t bank = (uint32_t)strtoul(p, &e, 16);
    if (e == p || *e != ':') { snprintf(m, n, "A2GSPU_PCTRAP: want bank:lo-hi, got '%s'", v); return SET_INVALID; }
    p = e + 1;
    uint32_t lo = (uint32_t)strtoul(p, &e, 16);
    if (e == p) { snprintf(m, n, "A2GSPU_PCTRAP: want bank:lo-hi, got '%s'", v); return SET_INVALID; }
    p = e;
    uint32_t hi = lo;
    if (*p == '-') { p++; hi = (uint32_t)strtoul(p, &e, 16); }
    if (hi < lo) { snprintf(m, n, "A2GSPU_PCTRAP: range end below start in '%s'", v); return SET_INVALID; }
    g_pctrap_lo = ((bank & 0xFF) << 16) | (lo & 0xFFFF);
    g_pctrap_hi = ((bank & 0xFF) << 16) | (hi & 0xFFFF);
    g_pctrap_active = true;
    g_pctrap_fired = false;
    snprintf(m, n, "A2GSPU_PCTRAP: one-shot on first entry to %06X-%06X", g_pctrap_lo, g_pctrap_hi);
    return SET_OK;
}
// A2GSPU_TRAPDUMP="base:len" -- extra memory dumped by PCTRAP.
int set_trapdump(const char *v, char *m, size_t n) {
    if (!v) { g_trap_dump_len = 0; snprintf(m, n, "A2GSPU_TRAPDUMP: disabled"); return SET_OK; }
    const char *p = v;
    char *e = nullptr;
    uint32_t base = (uint32_t)strtoul(p, &e, 16);
    if (e == p) { snprintf(m, n, "A2GSPU_TRAPDUMP: want base:len, got '%s'", v); return SET_INVALID; }
    p = e;
    uint32_t len = 48;
    if (*p == ':') { p++; len = (uint32_t)strtoul(p, &e, 16); }
    if (len == 0 || len > 256) len = 48;
    g_trap_dump_base = base;
    g_trap_dump_len = len;
    snprintf(m, n, "A2GSPU_TRAPDUMP: dump $%06X..+%u at PCTRAP", g_trap_dump_base, g_trap_dump_len);
    return SET_OK;
}
// A2GSPU_STACKTRAP="lo-hi" -- one-shot dump when S first enters the range.
int set_stacktrap(const char *v, char *m, size_t n) {
    if (!v) { g_stacktrap_active = false; snprintf(m, n, "A2GSPU_STACKTRAP: disabled"); return SET_OK; }
    const char *p = v;
    char *e = nullptr;
    uint32_t lo = (uint32_t)strtoul(p, &e, 16);
    if (e == p || *e != '-') { snprintf(m, n, "A2GSPU_STACKTRAP: want lo-hi, got '%s'", v); return SET_INVALID; }
    p = e + 1;
    uint32_t hi = (uint32_t)strtoul(p, &e, 16);
    if (e == p || hi < lo) { snprintf(m, n, "A2GSPU_STACKTRAP: want lo-hi with hi >= lo, got '%s'", v); return SET_INVALID; }
    g_stacktrap_lo = (uint16_t)lo;
    g_stacktrap_hi = (uint16_t)hi;
    g_stacktrap_active = true;
    g_stacktrap_fired = false;
    snprintf(m, n, "A2GSPU_STACKTRAP: one-shot when S first enters $%04X-$%04X", g_stacktrap_lo, g_stacktrap_hi);
    return SET_OK;
}
// A2GSPU_STACKWATCH=1 (imbalance only) or ="lo-hi" (+ over/underflow window).
int set_stackwatch(const char *v, char *m, size_t n) {
    if (!v) { g_stackwatch_on = false; g_stackwatch_has_win = false; snprintf(m, n, "A2GSPU_STACKWATCH: disabled"); return SET_OK; }
    bool has_win = false;
    uint32_t lo = 0, hi = 0;
    if (strchr(v, '-')) {
        const char *p = v;
        char *e = nullptr;
        lo = (uint32_t)strtoul(p, &e, 16);
        if (e == p || *e != '-') { snprintf(m, n, "A2GSPU_STACKWATCH: want 1 or lo-hi, got '%s'", v); return SET_INVALID; }
        p = e + 1;
        hi = (uint32_t)strtoul(p, &e, 16);
        if (e == p || hi < lo) { snprintf(m, n, "A2GSPU_STACKWATCH: window hi must be >= lo, got '%s'", v); return SET_INVALID; }
        has_win = true;
    }
    g_stackwatch_on = true;
    g_stackwatch_hits = 0; g_stackwatch_last_s = -1; g_stackwatch_was_out = false;
    g_stackwatch_has_win = has_win;
    if (has_win) { g_stackwatch_lo = (uint16_t)lo; g_stackwatch_hi = (uint16_t)hi; }
    if (has_win)
        snprintf(m, n, "A2GSPU_STACKWATCH: continuous (imbalance>$%X); over/underflow window [$%04X,$%04X]",
                 (unsigned)g_stackwatch_jump, g_stackwatch_lo, g_stackwatch_hi);
    else
        snprintf(m, n, "A2GSPU_STACKWATCH: continuous (imbalance>$%X; no window)", (unsigned)g_stackwatch_jump);
    return SET_OK;
}
// A2GSPU_TRACE_FROM=<hexPC> -- start the toolbox trace when PC first reaches it.
int set_tracefrom(const char *v, char *m, size_t n) {
    uint32_t pc = 0;
    if (v && !parse_hex(v, pc)) { snprintf(m, n, "A2GSPU_TRACE_FROM: want a hex PC, got '%s'", v); return SET_INVALID; }
    g_iigs_trace_from = pc & 0xFFFFFF;
    g_iigs_trace_armed = false;
    if (v) snprintf(m, n, "A2GSPU_TRACE_FROM: toolbox trace starts when PC first reaches $%06X", (unsigned)g_iigs_trace_from);
    else   snprintf(m, n, "A2GSPU_TRACE_FROM: disabled (trace from the start)");
    return SET_OK;
}
// A2GSPU_CALLTRACE="1" (from now) or "<hexPC>" (armed when PC first reaches it).
int set_calltrace(const char *v, char *m, size_t n) {
    if (!v) { g_calltrace_enabled = false; g_calltrace_armed = false; snprintf(m, n, "IIGS CALLTRACE: disabled"); return SET_OK; }
    bool use_pc = false;
    uint32_t from = 0;
    if (v[0] && !(v[0] == '1' && v[1] == '\0')) {
        if (!parse_hex(v, from)) { snprintf(m, n, "IIGS CALLTRACE: want 1 or a hex PC, got '%s'", v); return SET_INVALID; }
        use_pc = true;
    }
    g_calltrace_enabled = true;
    g_calltrace_use_pc = use_pc;
    g_calltrace_from = from & 0xFFFFFF;
    g_calltrace_armed = !use_pc;
    g_calltrace_logged = 0; g_calltrace_depth = 0;
    snprintf(m, n, "IIGS CALLTRACE: enabled (%s, n=%d)", use_pc ? "PC-armed" : "from frame 0", g_calltrace_n);
    return SET_OK;
}
// ITRACE window pieces. The master gate is on while any arm mode is set.
void itrace_gate() {
    g_iigs_itrace_enabled = g_iigs_itrace_use_pc || g_iigs_itrace_frame >= 0 ||
                            g_iigs_itrace_lo != 0 || g_iigs_itrace_hi != 0;
}
int set_itraceframe(const char *v, char *m, size_t n) {
    int f = -1;
    if (v && (!parse_int(v, f) || f < 0)) { snprintf(m, n, "A2GSPU_ITRACE_FRAME: want a frame >= 0, got '%s'", v); return SET_INVALID; }
    g_iigs_itrace_frame = f;
    itrace_gate();
    if (v) snprintf(m, n, "A2GSPU_ITRACE_FRAME: per-instruction trace armed at frame %d", f);
    else   snprintf(m, n, "A2GSPU_ITRACE_FRAME: disabled");
    return SET_OK;
}
int set_itracelo(const char *v, char *m, size_t n) {
    uint32_t a = 0;
    if (v && !parse_hex(v, a)) { snprintf(m, n, "A2GSPU_ITRACE_LO: want a hex 24-bit PC, got '%s'", v); return SET_INVALID; }
    g_iigs_itrace_lo = a & 0xFFFFFF;
    itrace_gate();
    snprintf(m, n, "A2GSPU_ITRACE_LO: $%06X", (unsigned)g_iigs_itrace_lo);
    return SET_OK;
}
int set_itracehi(const char *v, char *m, size_t n) {
    uint32_t a = 0;
    if (v && !parse_hex(v, a)) { snprintf(m, n, "A2GSPU_ITRACE_HI: want a hex 24-bit PC, got '%s'", v); return SET_INVALID; }
    g_iigs_itrace_hi = a & 0xFFFFFF;
    itrace_gate();
    snprintf(m, n, "A2GSPU_ITRACE_HI: $%06X", (unsigned)g_iigs_itrace_hi);
    return SET_OK;
}
int set_itracerearm(const char *v, char *m, size_t n) {
    int x = 0;
    if (v && (!parse_int(v, x) || x < 0)) { snprintf(m, n, "A2GSPU_ITRACE_REARM: want a count >= 0, got '%s'", v); return SET_INVALID; }
    g_iigs_itrace_rearm = x;
    snprintf(m, n, "A2GSPU_ITRACE_REARM: %d", x);
    return SET_OK;
}
int set_symbase(const char *v, char *m, size_t n) {
    uint32_t a = 0;
    if (v && !parse_hex(v, a)) { snprintf(m, n, "A2GSPU_SYM_BASE: want a hex 24-bit base, got '%s'", v); return SET_INVALID; }
    g_iigs_sym_base = a & 0xFFFFFF;
    g_iigs_sym_base_locked = (v != nullptr);
    if (v) snprintf(m, n, "A2GSPU_SYM_BASE: pinned $%06X (no auto-inference)", (unsigned)g_iigs_sym_base);
    else   snprintf(m, n, "A2GSPU_SYM_BASE: unpinned (inferred again)");
    return SET_OK;
}

// ---- file sinks ------------------------------------------------------------------
// Unset closes the sink; a new value closes the old one first. An unopenable path is
// INVALID: fatal at launch as before, a FAIL ack live with the sink left closed.
int open_sink(FILE *&sink, const char *v, const char *rail, char *m, size_t n) {
    a2outputs::finish(sink,"sink");
    if (!v) { snprintf(m, n, "%s: closed", rail); return SET_OK; }
    sink = fopen(v, "wb");
    a2outputs::track(sink,v);
    if (!sink) { snprintf(m, n, "%s: FAIL cannot open '%s'", rail, v); return SET_INVALID; }
    snprintf(m, n, "%s: -> '%s'", rail, v);
    return SET_OK;
}
int set_watchout(const char *v, char *m, size_t n) { return open_sink(g_watch_out, v, "A2GSPU_WATCH_OUT (NDJSON)", m, n); }
int set_itraceout(const char *v, char *m, size_t n) { return open_sink(g_iigs_itrace_out, v, "IIGS ITRACE_OUT", m, n); }

// ---- SNAP region logger: LO, HI and PCS configure; OUT arms --------------------------
int set_snaplo(const char *v, char *m, size_t n) {
    uint32_t a = 0;
    if (v && !parse_hex(v, a)) { snprintf(m, n, "A2GSPU_SNAP_LO: want a hex 24-bit address, got '%s'", v); return SET_INVALID; }
    g_snap_lo = a & 0xFFFFFF;
    snprintf(m, n, "A2GSPU_SNAP_LO: $%06X", (unsigned)g_snap_lo);
    return SET_OK;
}
int set_snaphi(const char *v, char *m, size_t n) {
    uint32_t a = 0;
    if (v && !parse_hex(v, a)) { snprintf(m, n, "A2GSPU_SNAP_HI: want a hex 24-bit address, got '%s'", v); return SET_INVALID; }
    g_snap_hi = a & 0xFFFFFF;
    snprintf(m, n, "A2GSPU_SNAP_HI: $%06X", (unsigned)g_snap_hi);
    return SET_OK;
}
int set_snappcs(const char *v, char *m, size_t n) {
    g_snap_npc = 0;
    if (v) {
        char buf[256]; strncpy(buf, v, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
        for (char *t = strtok(buf, ",; "); t && g_snap_npc < 8; t = strtok(nullptr, ",; ")) {
            uint32_t pc = 0;
            if (!parse_hex(t, pc)) { g_snap_npc = 0; snprintf(m, n, "A2GSPU_SNAP_PCS: '%s' is not a hex PC", t); return SET_INVALID; }
            g_snap_pcs[g_snap_npc++] = pc & 0xFFFF;
        }
    }
    snprintf(m, n, "A2GSPU_SNAP_PCS: %d trigger PC(s)", g_snap_npc);
    return SET_OK;
}
int set_snapmax(const char *v, char *m, size_t n) {
    static const int def = g_snap_max;
    int x = def;
    if (v && (!parse_int(v, x) || x <= 0)) { snprintf(m, n, "A2GSPU_SNAP_MAX: want a count > 0, got '%s'", v); return SET_INVALID; }
    g_snap_max = x;
    snprintf(m, n, "A2GSPU_SNAP_MAX: %d%s", x, v ? "" : " (default)");
    return SET_OK;
}
int set_snapout(const char *v, char *m, size_t n) {
    a2outputs::finish(g_snap_out,"g_snap_out");
    g_snap_count = 0;
    if (!v) { snprintf(m, n, "A2GSPU_SNAP: logging off"); return SET_OK; }
    // Arming needs the window and at least one PC. The launch block skipped a sink
    // it could not use in silence; a refusal now says what is missing.
    if (g_snap_npc <= 0 || g_snap_hi < g_snap_lo) {
        snprintf(m, n, "A2GSPU_SNAP: not armed -- set A2GSPU_SNAP_PCS and a window with HI >= LO first (npc=%d lo=$%06X hi=$%06X)",
                 g_snap_npc, (unsigned)g_snap_lo, (unsigned)g_snap_hi);
        return SET_REFUSED;
    }
    g_snap_out = fopen(v, "wb");
    a2outputs::track(g_snap_out,v);
    snprintf(m, n, "A2GSPU_SNAP: %s window $%06X-$%06X at %d PC(s) -> '%s'",
             g_snap_out ? "logging" : "OPEN-FAILED", (unsigned)g_snap_lo, (unsigned)g_snap_hi, g_snap_npc, v);
    return g_snap_out ? SET_OK : SET_INVALID;
}
int set_snapzp(const char *v, char *m, size_t n) {
    if (v) snprintf(m, n, "A2GSPU_SNAP_ZP: %s (zp correlation key; an annotation, nothing is armed)", v);
    else   snprintf(m, n, "A2GSPU_SNAP_ZP: cleared");
    return SET_OK;
}

}  // namespace

// ---- registrations: rail, apply function, windowed-safe, launch message on stderr ----
namespace {
a2rails::SetterRegistrar r_errhook      {"A2GSPU_ERRHOOK",         set_errhook};
a2rails::SetterRegistrar r_brkdump      {"A2GSPU_BRKDUMP",         set_brkdump};
a2rails::SetterRegistrar r_stopfault    {"A2GSPU_STOP_ON_FAULT",   set_stopfault, false};   // halts: a windowed session would end
a2rails::SetterRegistrar r_brkmem       {"A2GSPU_BRKMEM",          set_brkmem};
a2rails::SetterRegistrar r_lctrace      {"A2GSPU_LCTRACE",         set_lctrace};
a2rails::SetterRegistrar r_hang         {"A2GSPU_HANG_THRESHOLD",  set_hang};
a2rails::SetterRegistrar r_watchmax     {"A2GSPU_WATCH_MAX",       set_watchmax};
a2rails::SetterRegistrar r_watchout     {"A2GSPU_WATCH_OUT",       set_watchout};
a2rails::SetterRegistrar r_valtrapmax   {"A2GSPU_VALTRAP_MAX",     set_valtrapmax};
a2rails::SetterRegistrar r_symsuspect   {"A2GSPU_SYM_SUSPECT",     set_symsuspect};
a2rails::SetterRegistrar r_condtrapmax  {"A2GSPU_CONDTRAP_MAX",    set_condtrapmax};
a2rails::SetterRegistrar r_condtrap     {"A2GSPU_CONDTRAP",        set_condtrap};
a2rails::SetterRegistrar r_loadtracemin {"A2GSPU_LOADTRACE_MIN",   set_loadtracemin};
a2rails::SetterRegistrar r_loadtrace    {"A2GSPU_LOADTRACE",       set_loadtrace};
a2rails::SetterRegistrar r_pctrap       {"A2GSPU_PCTRAP",          set_pctrap};
a2rails::SetterRegistrar r_trapdump     {"A2GSPU_TRAPDUMP",        set_trapdump};
a2rails::SetterRegistrar r_stacktrap    {"A2GSPU_STACKTRAP",       set_stacktrap};
a2rails::SetterRegistrar r_swjump       {"A2GSPU_STACKWATCH_JUMP", set_stackwatchjump};
a2rails::SetterRegistrar r_stackwatch   {"A2GSPU_STACKWATCH",      set_stackwatch};
a2rails::SetterRegistrar r_pokenth      {"A2GSPU_POKE_NTH",        set_pokenth};
a2rails::SetterRegistrar r_intlog       {"A2GSPU_INTLOG",          set_intlog, true, true};
a2rails::SetterRegistrar r_traceext     {"A2GSPU_TRACE_EXT",       set_traceext, true, true};
a2rails::SetterRegistrar r_modetrace    {"A2GSPU_MODETRACE",       set_modetrace, true, true};
a2rails::SetterRegistrar r_tracefrom    {"A2GSPU_TRACE_FROM",      set_tracefrom};
a2rails::SetterRegistrar r_ctn          {"A2GSPU_CALLTRACE_N",     set_calltracen};
a2rails::SetterRegistrar r_cts          {"A2GSPU_CALLTRACE_SKIP",  set_calltraceskip};
a2rails::SetterRegistrar r_calltrace    {"A2GSPU_CALLTRACE",       set_calltrace, true, true};
a2rails::SetterRegistrar r_retguard     {"A2GSPU_RETGUARD",        set_retguard, true, true};
a2rails::SetterRegistrar r_itframe      {"A2GSPU_ITRACE_FRAME",    set_itraceframe, true, true};
a2rails::SetterRegistrar r_itlo         {"A2GSPU_ITRACE_LO",       set_itracelo, true, true};
a2rails::SetterRegistrar r_ithi         {"A2GSPU_ITRACE_HI",       set_itracehi, true, true};
a2rails::SetterRegistrar r_itrearm      {"A2GSPU_ITRACE_REARM",    set_itracerearm, true, true};
a2rails::SetterRegistrar r_itout        {"A2GSPU_ITRACE_OUT",      set_itraceout, true, true};
a2rails::SetterRegistrar r_symbase      {"A2GSPU_SYM_BASE",        set_symbase};
a2rails::SetterRegistrar r_snaplo       {"A2GSPU_SNAP_LO",         set_snaplo};
a2rails::SetterRegistrar r_snaphi       {"A2GSPU_SNAP_HI",         set_snaphi};
a2rails::SetterRegistrar r_snappcs      {"A2GSPU_SNAP_PCS",        set_snappcs};
a2rails::SetterRegistrar r_snapmax      {"A2GSPU_SNAP_MAX",        set_snapmax};
a2rails::SetterRegistrar r_snapout      {"A2GSPU_SNAP_OUT",        set_snapout};
a2rails::SetterRegistrar r_snapzp       {"A2GSPU_SNAP_ZP",         set_snapzp};
a2rails::SetterRegistrar r_livestall    {"A2GSPU_LIVENESS_STALL",  set_livenessstall};
a2rails::SetterRegistrar r_loadmap      {"A2GSPU_LOADMAP",         set_loadmap};
a2rails::SetterRegistrar r_liveness     {"A2GSPU_LIVENESS",        set_liveness};
a2rails::SetterRegistrar r_livenessout  {"A2GSPU_LIVENESS_OUT",    set_livenessout};
a2rails::SetterRegistrar r_mv_storevis  {"MEMVU_STOREVIS",         set_memvu_storevis};
a2rails::SetterRegistrar r_mv_loadvis   {"MEMVU_LOADVIS",          set_memvu_loadvis};
a2rails::SetterRegistrar r_mv_banks     {"MEMVU_STOREVIS_BANKS",   set_memvu_banks};
a2rails::SetterRegistrar r_mv_opmix     {"MEMVU_OPMIX",            set_memvu_opmix};
a2rails::SetterRegistrar r_mv_ireuse    {"MEMVU_IREUSE",           set_memvu_ireuse};
a2rails::SetterRegistrar r_mv_workset   {"MEMVU_WORKSET",          set_memvu_workset};
a2rails::SetterRegistrar r_mv_seam      {"MEMVU_SEAM",             set_memvu_seam};
a2rails::SetterRegistrar r_mv_blame     {"MEMVU_BLAME",            set_memvu_blame};
a2rails::SetterRegistrar r_tap_pcs      {"A2GSPU_TAP_PCS",         set_tap_pcs};
a2rails::SetterRegistrar r_tap_ptrs     {"A2GSPU_TAP_PTRS",        set_tap_ptrs};
a2rails::SetterRegistrar r_tap_max      {"A2GSPU_TAP_MAX",         set_tap_max};
a2rails::SetterRegistrar r_tap_dumpon   {"A2GSPU_TAP_DUMP_ON",     set_tap_dump_on};
a2rails::SetterRegistrar r_tap_cycms    {"A2GSPU_TAP_CYC_PER_MS",  set_tap_cyc_ms};
a2rails::SetterRegistrar r_tap_cycsec   {"A2GSPU_TAP_CYC_PER_SEC", set_tap_cyc_sec};
a2rails::SetterRegistrar r_tap          {"A2GSPU_TAP",             set_tap};
a2rails::SetterRegistrar r_tap_session  {"A2GSPU_TAP_SESSION",     set_tap_session};
a2rails::SetterRegistrar r_tap_dumpev   {"A2GSPU_TAP_DUMP_EVERY",  set_tap_dump_every};
a2rails::SetterRegistrar r_tap_zpev     {"A2GSPU_TAP_ZP_EVERY",    set_tap_zp_every};
a2rails::SetterRegistrar r_tap_keypost  {"A2GSPU_TAP_KEY_POST_FRAMES", set_tap_key_post};
a2rails::SetterRegistrar r_milestones   {"A2GSPU_MILESTONES",      set_milestones, true, true};
a2rails::SetterRegistrar r_romsyms      {"A2GSPU_ROM_SYMBOLS",     set_rom_symbols};
a2rails::SetterRegistrar r_watchipc     {"A2GSPU_WATCH_IPC_ZP",    set_watch_ipc_zp};
a2rails::SetterRegistrar r_keyram       {"A2GSPU_KEYRAM",          set_keyram};
a2rails::SetterRegistrar r_cardcmdlog   {"A2GSPU_CARD_CMDLOG",     set_card_cmdlog};
a2rails::Registrar       g_reader_cardcmdlog{"A2GSPU_CARD_CMDLOG", a2gspu_cmdreg_format};
a2rails::SetterRegistrar r_u2events     {"A2GSPU_U2_EVENTS",       set_u2_events};
a2rails::Registrar       g_reader_u2events{"A2GSPU_U2_EVENTS",   u2_format_counters};
a2rails::SetterRegistrar r_keydelay     {"A2GSPU_KEYSCHED_DELAY",  set_keydelay};
}  // namespace

// ---- the launch side ------------------------------------------------------------------
int a2gspu_rail_arm_value(const char *rail, const char *value, const char *from) {
    const a2rails::SetterReg *s = a2rails::setter_find(rail);
    if (!s) {
        fprintf(stderr, "a2gspu_rail_arm_value: no setter registered for %s\n", rail);
        exit(2);
    }
    if (!value) return a2rails::SET_OK;
    char msg[512] = "";
    const int rc = s->fn(value, msg, sizeof msg);
    FILE *out = (s->to_stderr || rc != a2rails::SET_OK) ? stderr : stdout;
    if (msg[0]) fprintf(out, "%s (from %s)%s\n", msg, from, rc == a2rails::SET_INVALID ? " -- aborting (exit 2)" : "");
    if (rc == a2rails::SET_INVALID) { fflush(stdout); exit(2); }
    return rc;
}
int a2gspu_rail_arm_env(const char *rail) {
    const a2rails::SetterReg *s = a2rails::setter_find(rail);
    if (!s) {
        fprintf(stderr, "a2gspu_rail_arm_env: no setter registered for %s (a startup call names a rail that lost its registration)\n", rail);
        exit(2);
    }
    const char *v = SDL_getenv(rail);
    if (!v) return a2rails::SET_OK;
    char msg[512] = "";
    const int rc = s->fn(v, msg, sizeof msg);
    FILE *out = (s->to_stderr || rc != a2rails::SET_OK) ? stderr : stdout;
    if (msg[0]) fprintf(out, "%s%s\n", msg, rc == a2rails::SET_INVALID ? " -- aborting (exit 2)" : "");
    if (rc == a2rails::SET_INVALID) { fflush(stdout); exit(2); }
    return rc;
}
