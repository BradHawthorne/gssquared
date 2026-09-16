#pragma once
// memvu_report.hpp -- every armed MEMVU report at once, for the spike's end and for
// `memvu report <file>` mid-session. A CTRL session never reaches the spike's end,
// so until this existed a session that armed BLAME, OPMIX or SEAM got no report.
#include <cstdio>
#include <string>
#include "memvu_storevis.hpp"
#include "memvu_istream.hpp"

// Every armed MEMVU report, in the spike-end order. Returns the sections written.
inline std::string memvu_report_all(FILE *f) {
    std::string s;
    auto add = [&](const char *nm) { if (!s.empty()) s += ","; s += nm; };
    memvu_sv_report(f);                          if (memvu_sv_on) add("storevis");
    if (g_memvu_sv_banks && memvu_sv_on) { memvu_sv_report_banks(f); add("banks"); }
    memvu_lv_report(f);                          if (memvu_lv_on) add("loadvis");
    memvu_istream_report(f);
    if (memvu_op_on) add("opmix");
    if (memvu_ir_on) add("ireuse");
    if (memvu_ws_on) add("workset");
    memvu_seam_report(f);                        if (memvu_seam_on) add("seam");
    if (g_memvu_blame_topn > 0) { memvu_blame_report(f, g_memvu_blame_topn); if (memvu_blame_on) add("blame"); }
    return s;
}
