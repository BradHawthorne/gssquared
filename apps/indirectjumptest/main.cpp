/**
 * indirectjumptest: JMP (a,x) and JSR (a,x) against a fixture of cases with
 * independent expected values (cases.txt: hand-worked cases and
 * SingleStepTests vectors).
 *
 * Covers the program-bank rule for the pointer: both pointer bytes are read
 * from the program bank, so a pointer at $FFFF takes its high byte from
 * $0000 of the same bank. Also checks the other registers, the pushed return
 * address (JSR), memory and the cycle count.
 *
 * Each case runs one instruction on a flat 16 MB memory.
 *
 *     indirectjumptest <cases.txt>
 * Prints one line per case and "Failed tests: N"; exit 1 if any case failed.
 */
#include <SDL3/SDL.h>

#include "clock.hpp"
#include "gs2.hpp"
#include "cpus/cpu_implementations.cpp"
#include "mmus/mmu.hpp"
#include "NClock.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <utility>
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

struct Case {
    std::string name;
    std::map<std::string, unsigned long> in, want;
    std::vector<std::pair<uint32_t, uint8_t>> init, wantmem;
};

// "key=value" tokens -> map (cycles is decimal, everything else hex).
static void parse_kv(std::istringstream &ss, std::map<std::string, unsigned long> &m) {
    std::string tok;
    while (ss >> tok) {
        size_t eq = tok.find('=');
        if (eq == std::string::npos) continue;
        std::string k = tok.substr(0, eq);
        m[k] = strtoul(tok.c_str() + eq + 1, nullptr, k == "cycles" ? 10 : 16);
    }
}
static void parse_mem(std::istringstream &ss, std::vector<std::pair<uint32_t, uint8_t>> &v) {
    std::string tok;
    while (ss >> tok) {
        size_t eq = tok.find('=');
        if (eq == std::string::npos) continue;
        v.push_back({(uint32_t)strtoul(tok.substr(0, eq).c_str(), nullptr, 16),
                     (uint8_t)strtoul(tok.c_str() + eq + 1, nullptr, 16)});
    }
}

static bool load_cases(const char *path, std::vector<Case> &cases) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[8192];
    Case cur;
    bool open = false;
    while (fgets(line, sizeof line, f)) {
        std::istringstream ss(line);
        std::string kw;
        if (!(ss >> kw) || kw[0] == '#') continue;
        if (kw == "case") { cur = Case(); ss >> cur.name; parse_kv(ss, cur.in); open = true; }
        else if (kw == "init" && open) parse_mem(ss, cur.init);
        else if (kw == "want" && open) parse_kv(ss, cur.want);
        else if (kw == "wantmem" && open) parse_mem(ss, cur.wantmem);
        else if (kw == "end" && open) { cases.push_back(cur); open = false; }
        else { fclose(f); return false; }
    }
    fclose(f);
    return !open;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: indirectjumptest <cases.txt>\n"); return 2; }
    std::vector<Case> cases;
    if (!load_cases(argv[1], cases) || cases.empty()) { fprintf(stderr, "indirectjumptest: cannot read %s\n", argv[1]); return 2; }

    gs2_app_inues.base_path = "./";
    gs2_app_inues.pref_path = gs2_app_inues.base_path;
    gs2_app_inues.console_mode = false;

    FlatMMU *mmu = new FlatMMU();
    cpu_state *cpu = new cpu_state(PROCESSOR_65816);
    NClock *clock = new NClock(CLOCK_SET_US, CLOCK_FREE_RUN);
    cpu->cpun = createCPU(PROCESSOR_65816, clock);
    cpu->core = cpu->cpun.get();
    cpu->trace = false;
    cpu->set_mmu(mmu);
    cpu->reset();

    int failed = 0;
    for (Case &c : cases) {
        std::fill(mmu->mem.begin(), mmu->mem.end(), 0);
        for (auto &m : c.init) mmu->mem[m.first & 0xFFFFFF] = m.second;
        cpu->halt = 0; cpu->clock_stopped = false; cpu->rdy = false;
        cpu->E = c.in["e"] ? 1 : 0;
        cpu->p = (uint8_t)c.in["p"];
        cpu->a = (uint16_t)c.in["a"]; cpu->x = (uint16_t)c.in["x"]; cpu->y = (uint16_t)c.in["y"];
        cpu->sp = (uint16_t)c.in["s"]; cpu->d = (uint16_t)c.in["d"];
        cpu->full_db = 0; cpu->db = (uint8_t)c.in["dbr"];
        cpu->pb = (uint8_t)c.in["pbr"]; cpu->pc = (uint16_t)c.in["pc"];

        uint64_t c0 = clock->get_cycles();
        cpu->core->execute_next(cpu);
        unsigned long cycles = (unsigned long)(clock->get_cycles() - c0);

        std::string why;
        char tmp[96];
        struct { const char *k; unsigned long have; } regs[] = {
            {"pc", cpu->pc}, {"s", cpu->sp}, {"p", cpu->p}, {"a", cpu->a}, {"x", cpu->x}, {"y", cpu->y},
            {"dbr", cpu->db}, {"d", cpu->d}, {"pbr", cpu->pb}, {"e", cpu->E}, {"cycles", cycles}};
        for (auto &r : regs) {
            if (!c.want.count(r.k)) continue;
            if (c.want[r.k] != r.have) { snprintf(tmp, sizeof tmp, " %s=%lX(want %lX)", r.k, r.have, c.want[r.k]); why += tmp; }
        }
        for (auto &m : c.wantmem) {
            uint8_t have = mmu->mem[m.first & 0xFFFFFF];
            if (have != m.second) { snprintf(tmp, sizeof tmp, " mem %06X=%02X(want %02X)", m.first, have, m.second); why += tmp; }
        }
        printf("%-24s %s%s\n", c.name.c_str(), why.empty() ? "ok" : "FAILED", why.c_str());
        if (!why.empty()) failed++;
    }
    printf("Failed tests: %d\n", failed);
    return failed ? 1 : 0;
}
