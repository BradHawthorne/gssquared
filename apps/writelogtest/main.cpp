// writelogtest -- the CPU write log (CTRL `writes`) and the bus watch, against
// the real 65816 core on a flat memory.
//
// Every expected value is the WDC W65C816S datasheet's bus behaviour:
//   * emulation-mode INC abs is read, write old value (the phantom write),
//     write new value: two writes to the one address, the first a phantom;
//   * native-mode INC abs (m=1) is read, internal cycle, write: one write;
//   * native STA abs with m=0 writes DBR:addr and DBR:addr+1;
//   * PHA with m=0 writes S then S-1 in bank 0.
// The watch must see the phantom write too (it used to see only bus_write),
// and the log's mark / off / cap / report behave as documented.
#include <SDL3/SDL.h>

#include "clock.hpp"
#include "gs2.hpp"
#include "cpus/cpu_implementations.cpp"
#include "mmus/mmu.hpp"
#include "NClock.hpp"
#include "a2gspu_writelog.hpp"
#include "iigs_toolbox.hpp"   // the bus watch (g_watch_*)

#include <cstdio>
#include <string>
#include <vector>

gs2_app_t gs2_app_inues;
uint64_t debug_level = 0;

class FlatMMU : public MMU {
public:
    std::vector<uint8_t> mem;
    FlatMMU() : MMU(256, 0x10000), mem(1u << 24, 0) {}
    uint8_t read(uint32_t a) override { return mem[a & 0xFFFFFF]; }
    uint8_t vp_read(uint32_t a) override { return mem[a & 0xFFFFFF]; }
    void write(uint32_t a, uint8_t v) override { mem[a & 0xFFFFFF] = v; }
    uint8_t probe_peek(uint32_t a) override { return mem[a & 0xFFFFFF]; }
};

static int failures = 0;
static void check(bool ok, const std::string &what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
}

struct Rig {
    FlatMMU *mmu;
    cpu_state *cpu;
    NClock *clock;
    // Run exactly one instruction at 00:1000 with the given state (the code
    // page is cleared first; data the caller planted elsewhere is kept).
    void one(std::vector<uint8_t> code, int e, uint8_t p, uint16_t a, uint8_t dbr, uint16_t s) {
        std::fill(mmu->mem.begin() + 0x1000, mmu->mem.begin() + 0x1100, 0);
        for (size_t i = 0; i < code.size(); i++) mmu->mem[0x1000 + i] = code[i];
        cpu->halt = 0; cpu->clock_stopped = false; cpu->rdy = false;
        cpu->irq_asserted = 0; cpu->irq_pipe = 0; cpu->nmi_pending = false; cpu->boundary_abort_pending = false;
        cpu->E = e;
        cpu->p = e ? (p | 0x30) : p;
        cpu->pc = 0x1000; cpu->pb = 0;
        cpu->full_db = 0; cpu->db = dbr;
        cpu->sp = e ? (0x100 | (s & 0xFF)) : s;
        cpu->a = a; cpu->x = 0; cpu->y = 0; cpu->d = 0;
        cpu->core->execute_next(cpu);
    }
};

static std::string report(size_t bufsz = 512) {
    std::vector<char> b(bufsz);
    a2writelog::format_report(b.data(), b.size());
    return std::string(b.data());
}

int main() {
    gs2_app_inues.base_path = "./";
    gs2_app_inues.pref_path = gs2_app_inues.base_path;
    gs2_app_inues.console_mode = false;

    Rig r;
    r.mmu = new FlatMMU();
    r.cpu = new cpu_state(PROCESSOR_65816);
    r.clock = new NClock(CLOCK_SET_US, CLOCK_FREE_RUN);
    r.cpu->cpun = createCPU(PROCESSOR_65816, r.clock);
    r.cpu->core = r.cpu->cpun.get();
    r.cpu->trace = false;
    r.cpu->set_mmu(r.mmu);
    r.cpu->reset();

    // Watch one address; hits are counted by the watch itself.
    g_watch_ranges[0] = {0x002000, 0x002000};
    g_watch_count = 1;
    g_watch_max = 0;
    g_watch_on = true;

    // 1. emulation INC $2000: phantom write of the old value, then the new value.
    a2writelog::mark();
    g_watch_hits = 0;
    r.mmu->mem[0x2000] = 0x41;
    r.one({0xEE, 0x00, 0x20}, 1, 0x00, 0, 0, 0x01FF);
    check(r.mmu->mem[0x2000] == 0x42, "emulation INC abs incremented the byte");
    check(a2writelog::g_log.size() == 2, "emulation INC abs: 2 writes logged (got " + std::to_string(a2writelog::g_log.size()) + ")");
    if (a2writelog::g_log.size() == 2) {
        const auto &w0 = a2writelog::g_log[0], &w1 = a2writelog::g_log[1];
        check(w0.addr == 0x002000 && w0.data == 0x41 && w0.phantom == 1, "first write is the phantom write of the old value");
        check(w1.addr == 0x002000 && w1.data == 0x42 && w1.phantom == 0, "second write is the new value");
    }
    check(g_watch_hits == 2, "watch saw both writes, phantom included (got " + std::to_string(g_watch_hits) + ")");
    check(report() == "status=OK writes on=1 n=2 phantom=1 unique=1 dropped=0 wr=002000", "report: " + report());

    // 2. native INC $2000 with m=1: one write, no phantom write.
    a2writelog::mark();
    g_watch_hits = 0;
    r.mmu->mem[0x2000] = 0x41;
    r.one({0xEE, 0x00, 0x20}, 0, 0x20, 0, 0, 0x01FF);
    check(a2writelog::g_log.size() == 1 && a2writelog::g_log[0].phantom == 0 && a2writelog::g_log[0].data == 0x42,
          "native INC abs (m=1): exactly one ordinary write");
    check(g_watch_hits == 1, "watch saw one write for native INC abs");

    // 3. native STA $2000 with m=0 and DBR=$7E: two writes in bank $7E.
    a2writelog::mark();
    r.one({0x8D, 0x00, 0x20}, 0, 0x00, 0xBEEF, 0x7E, 0x01FF);
    check(report() == "status=OK writes on=1 n=2 phantom=0 unique=2 dropped=0 wr=7E2000,7E2001", "STA abs m=0 DBR=7E: " + report());

    // 4. PHA with m=0: S then S-1, bank 0; the report is sorted.
    a2writelog::mark();
    r.one({0x48}, 0, 0x00, 0x1234, 0x7E, 0x01F0);
    check(a2writelog::g_log.size() == 2 && a2writelog::g_log[0].addr == 0x0001F0 && a2writelog::g_log[0].data == 0x12 &&
          a2writelog::g_log[1].addr == 0x0001EF && a2writelog::g_log[1].data == 0x34, "PHA m=0 writes $12 at 01F0 then $34 at 01EF");
    check(report() == "status=OK writes on=1 n=2 phantom=0 unique=2 dropped=0 wr=0001EF,0001F0", "PHA report: " + report());

    // 5. off stops recording but keeps the log; mark clears it.
    a2writelog::off();
    r.one({0x48}, 0, 0x00, 0x1234, 0, 0x01F0);
    check(a2writelog::g_log.size() == 2 && !a2writelog::g_on, "off: nothing more recorded, log kept");
    check(report().rfind("status=OK writes on=0 n=2 ", 0) == 0, "off is reported: " + report());
    a2writelog::mark();
    check(a2writelog::g_log.empty() && a2writelog::g_on && report() == "status=OK writes on=1 n=0 phantom=0 unique=0 dropped=0 wr=",
          "mark clears and arms: " + report());

    // 6. cap: writes beyond it are counted as dropped.
    a2writelog::mark(1);
    r.one({0x48}, 0, 0x00, 0x1234, 0, 0x01F0);
    check(a2writelog::g_log.size() == 1 && a2writelog::g_dropped == 1, "cap 1: one stored, one dropped");

    // 7. a reply buffer too small for the list says so instead of cutting it.
    a2writelog::mark();
    r.one({0x8D, 0x00, 0x20}, 0, 0x00, 0xBEEF, 0x7E, 0x01FF);
    std::string small = report(70);
    check(small.find("wr=truncated") != std::string::npos && small.find("7E2000") == std::string::npos,
          "small buffer: wr=truncated, no partial list: " + small);

    a2writelog::off();
    g_watch_on = false;
    if (failures) { std::fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    std::printf("writelogtest: OK\n");
    return 0;
}
