/**
 * img816: run flat 65816 memory images on the CPU core, in-process, in batch.
 *
 * Each case loads bytes into a flat 16 MB memory (no Apple II mapping, no
 * I/O, no interrupts), sets every register (including DBR and PBR), runs
 * until the program bank:PC reaches a stop address (checked before each
 * instruction, so the instruction there is not executed), and reports:
 *   - the total cycle count and the cycle count of the last instruction,
 *   - all registers (A, X, Y as full 16-bit values, so hidden high bytes show),
 *   - one or more memory windows,
 *   - the write set: every address written (sorted, unique), plus the raw
 *     number of write accesses (a read-modify-write that writes twice, as in
 *     emulation mode, counts 2).
 * Between cases, every byte a case loaded or wrote is cleared to 0, so each
 * case starts from all-zero memory plus its own load records.
 *
 * Input (a file, or "-" for stdin), one directive per line; '#' starts a
 * comment line:
 *   case NAME                    start a case
 *   load AAAAAA HEXBYTES         put bytes at 24-bit address AAAAAA (repeatable)
 *   reg K=V ...                  initial registers, hex: pc (24-bit, bank = PBR),
 *                                a x y s d (16-bit), p dbr (8-bit), e (0/1);
 *                                unspecified registers are 0
 *   stop AAAAAA                  stop when PBR:PC == AAAAAA
 *   window AAAAAA N              report N bytes from AAAAAA (repeatable)
 *   limit N                      instruction limit (default 1000000)
 *   run                          end of the case
 * With e=1 the 65816 invariant is applied before the run (S high byte $01,
 * X/Y high bytes 0, P bits 4-5 set). With e=0 and x=1 (P bit 4) the index
 * high bytes are forced to 0 the same way: the CPU cannot hold a non-zero
 * XH/YH with 8-bit index registers, so `x=1234 p=10` runs as X=$0034.
 *
 * Output: one line per case, tab-separated:
 *   NAME STATUS cyc=N last=N insn=N pc=PPPPPP a=AAAA x=XXXX y=YYYY s=SSSS
 *   p=PP d=DDDD dbr=BB e=E win=AAAAAA:HEX[,...] nw=N wr=AAAAAA[,...]
 * STATUS is ok (stop address reached), limit (instruction limit hit) or halt
 * (WAI/STP stopped the processor; the run ends there).
 *
 * Options:
 *   --repeat N   run the whole batch N times; every pass must produce output
 *                identical to the first (reported as repeat_mismatch); only
 *                the first pass is printed
 *   --quiet      do not print case lines
 *   --check F    compare the case lines with file F (same format); exit 1 on
 *                any difference
 * A summary line goes to stderr: cases, runs, run seconds, runs per second,
 * repeat mismatches.
 * Exit status: 0 on success, 1 on a check difference or repeat mismatch,
 * 2 on input error.
 */
#include <SDL3/SDL.h>

#include "clock.hpp"
#include "gs2.hpp"
#include "cpus/cpu_implementations.cpp"
#include "mmus/mmu.hpp"
#include "NClock.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

gs2_app_t gs2_app_inues;
uint64_t debug_level = 0;

class FlatMMU : public MMU {
public:
    std::vector<uint8_t> mem;
    std::vector<uint32_t> wlog;
    FlatMMU() : MMU(256, 0x10000), mem(1u << 24, 0) {}
    uint8_t read(uint32_t a) override { return mem[a & 0xFFFFFF]; }
    uint8_t vp_read(uint32_t a) override { return mem[a & 0xFFFFFF]; }
    void write(uint32_t a, uint8_t v) override { a &= 0xFFFFFF; mem[a] = v; wlog.push_back(a); }
    uint8_t probe_peek(uint32_t a) override { return mem[a & 0xFFFFFF]; }
};

struct Load { uint32_t addr; std::vector<uint8_t> bytes; };
struct Window { uint32_t addr; uint32_t len; };
struct Case {
    std::string name;
    std::vector<Load> loads;
    std::vector<Window> windows;
    uint32_t pc = 0, stop = 0;
    uint16_t a = 0, x = 0, y = 0, s = 0, d = 0;
    uint8_t p = 0, dbr = 0, e = 0;
    bool has_stop = false;
    long limit = 1000000;
};

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_hex(const std::string &s, unsigned long max, unsigned long &out) {
    if (s.empty() || s.size() > 8) return false;
    unsigned long v = 0;
    for (char c : s) { int h = hexval(c); if (h < 0) return false; v = v * 16 + (unsigned)h; }
    if (v > max) return false;
    out = v;
    return true;
}

static bool parse_input(FILE *f, std::vector<Case> &cases) {
    std::string all;
    char chunk[1 << 16];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) all.append(chunk, n);
    std::istringstream in(all);
    std::string line;
    long lineno = 0;
    Case cur;
    bool open = false;
    auto fail = [&](const char *why) {
        fprintf(stderr, "img816: line %ld: %s\n", lineno, why);
        return false;
    };
    while (std::getline(in, line)) {
        lineno++;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string kw;
        if (!(ls >> kw) || kw[0] == '#') continue;
        unsigned long v = 0, w = 0;
        if (kw == "case") {
            if (open) return fail("case inside a case");
            cur = Case();
            if (!(ls >> cur.name)) return fail("case needs a name");
            open = true;
            continue;
        }
        if (!open) return fail("directive outside a case");
        if (kw == "load") {
            std::string a, hx;
            if (!(ls >> a >> hx) || !parse_hex(a, 0xFFFFFF, v) || hx.size() % 2) return fail("bad load");
            Load ld{(uint32_t)v, {}};
            for (size_t i = 0; i < hx.size(); i += 2) {
                int h = hexval(hx[i]), l = hexval(hx[i + 1]);
                if (h < 0 || l < 0) return fail("bad load bytes");
                ld.bytes.push_back((uint8_t)(h * 16 + l));
            }
            if (v + ld.bytes.size() > 0x1000000) return fail("load past 16 MB");
            cur.loads.push_back(std::move(ld));
        } else if (kw == "reg") {
            std::string kv;
            while (ls >> kv) {
                size_t eq = kv.find('=');
                if (eq == std::string::npos) return fail("bad reg");
                std::string k = kv.substr(0, eq), val = kv.substr(eq + 1);
                if (k == "pc") { if (!parse_hex(val, 0xFFFFFF, v)) return fail("bad pc"); cur.pc = (uint32_t)v; }
                else if (k == "a") { if (!parse_hex(val, 0xFFFF, v)) return fail("bad a"); cur.a = (uint16_t)v; }
                else if (k == "x") { if (!parse_hex(val, 0xFFFF, v)) return fail("bad x"); cur.x = (uint16_t)v; }
                else if (k == "y") { if (!parse_hex(val, 0xFFFF, v)) return fail("bad y"); cur.y = (uint16_t)v; }
                else if (k == "s") { if (!parse_hex(val, 0xFFFF, v)) return fail("bad s"); cur.s = (uint16_t)v; }
                else if (k == "d") { if (!parse_hex(val, 0xFFFF, v)) return fail("bad d"); cur.d = (uint16_t)v; }
                else if (k == "p") { if (!parse_hex(val, 0xFF, v)) return fail("bad p"); cur.p = (uint8_t)v; }
                else if (k == "dbr") { if (!parse_hex(val, 0xFF, v)) return fail("bad dbr"); cur.dbr = (uint8_t)v; }
                else if (k == "e") { if (!parse_hex(val, 1, v)) return fail("bad e"); cur.e = (uint8_t)v; }
                else return fail("unknown register");
            }
        } else if (kw == "stop") {
            std::string a;
            if (!(ls >> a) || !parse_hex(a, 0xFFFFFF, v)) return fail("bad stop");
            cur.stop = (uint32_t)v; cur.has_stop = true;
        } else if (kw == "window") {
            std::string a, l;
            if (!(ls >> a >> l) || !parse_hex(a, 0xFFFFFF, v)) return fail("bad window");
            char *end;
            w = strtoul(l.c_str(), &end, 10);
            if (*end || w == 0 || v + w > 0x1000000) return fail("bad window length");
            cur.windows.push_back({(uint32_t)v, (uint32_t)w});
        } else if (kw == "limit") {
            std::string l;
            char *end;
            if (!(ls >> l)) return fail("bad limit");
            cur.limit = strtol(l.c_str(), &end, 10);
            if (*end || cur.limit <= 0) return fail("bad limit");
        } else if (kw == "run") {
            if (!cur.has_stop) return fail("case has no stop address");
            cases.push_back(std::move(cur));
            open = false;
        } else {
            return fail("unknown directive");
        }
    }
    if (open) { lineno++; return fail("unterminated case (no run)"); }
    return true;
}

struct Machine {
    FlatMMU *mmu;
    cpu_state *cpu;
    NClock *clock;
};

static std::string run_case(Machine &m, const Case &c) {
    FlatMMU *mmu = m.mmu;
    cpu_state *cpu = m.cpu;
    for (const Load &ld : c.loads) std::copy(ld.bytes.begin(), ld.bytes.end(), mmu->mem.begin() + ld.addr);
    mmu->wlog.clear();

    uint16_t s = c.s, x = c.x, y = c.y;
    uint8_t p = c.p;
    if (c.e) { s = 0x100 | (s & 0xFF); p |= 0x30; x &= 0xFF; y &= 0xFF; }
    else if (p & 0x10) { x &= 0xFF; y &= 0xFF; }   // native, 8-bit index: XH = YH = 0
    cpu->halt = 0; cpu->clock_stopped = false; cpu->rdy = false;
    cpu->irq_asserted = 0; cpu->irq_pipe = 0; cpu->nmi_pending = false; cpu->boundary_abort_pending = false;
    cpu->E = c.e;
    cpu->p = p;
    cpu->pc = (uint16_t)c.pc; cpu->pb = (uint8_t)(c.pc >> 16);
    cpu->full_db = 0; cpu->db = c.dbr;
    cpu->sp = s; cpu->a = c.a; cpu->x = x; cpu->y = y; cpu->d = c.d;

    uint64_t c0 = m.clock->get_cycles();
    uint64_t last = 0;
    long insn = 0;
    const char *status = "ok";
    for (;;) {
        uint32_t at = ((uint32_t)cpu->pb << 16) | cpu->pc;
        if (at == c.stop) break;
        if (insn >= c.limit) { status = "limit"; break; }
        uint64_t b = m.clock->get_cycles();
        cpu->core->execute_next(cpu);
        last = m.clock->get_cycles() - b;
        insn++;
        if (cpu->halt || cpu->clock_stopped || cpu->rdy) { status = "halt"; break; }
    }
    uint64_t total = m.clock->get_cycles() - c0;

    char buf[256];
    std::string out = c.name;
    snprintf(buf, sizeof buf, "\t%s\tcyc=%llu\tlast=%llu\tinsn=%ld", status, (unsigned long long)total,
             (unsigned long long)last, insn);
    out += buf;
    snprintf(buf, sizeof buf, "\tpc=%02X%04X a=%04X x=%04X y=%04X s=%04X p=%02X d=%04X dbr=%02X e=%d",
             cpu->pb, cpu->pc, cpu->a, cpu->x, cpu->y, cpu->sp, cpu->p, cpu->d, cpu->db, cpu->E ? 1 : 0);
    out += buf;
    out += "\twin=";
    static const char hx[] = "0123456789ABCDEF";
    for (size_t i = 0; i < c.windows.size(); i++) {
        if (i) out += ',';
        snprintf(buf, sizeof buf, "%06X:", c.windows[i].addr);
        out += buf;
        for (uint32_t k = 0; k < c.windows[i].len; k++) {
            uint8_t b = mmu->mem[c.windows[i].addr + k];
            out += hx[b >> 4]; out += hx[b & 15];
        }
    }
    std::vector<uint32_t> ws(mmu->wlog);
    std::sort(ws.begin(), ws.end());
    ws.erase(std::unique(ws.begin(), ws.end()), ws.end());
    snprintf(buf, sizeof buf, "\tnw=%zu\twr=", mmu->wlog.size());
    out += buf;
    for (size_t i = 0; i < ws.size(); i++) {
        snprintf(buf, sizeof buf, i ? ",%06X" : "%06X", ws[i]);
        out += buf;
    }

    // clear everything this case loaded or wrote
    for (const Load &ld : c.loads) std::fill(mmu->mem.begin() + ld.addr, mmu->mem.begin() + ld.addr + ld.bytes.size(), 0);
    for (uint32_t ad : mmu->wlog) mmu->mem[ad] = 0;
    return out;
}

int main(int argc, char **argv) {
    const char *path = nullptr, *check = nullptr;
    long repeat = 1;
    bool quiet = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--repeat") && i + 1 < argc) repeat = atol(argv[++i]);
        else if (!strcmp(argv[i], "--quiet")) quiet = true;
        else if (!strcmp(argv[i], "--check") && i + 1 < argc) check = argv[++i];
        else path = argv[i];
    }
    if (!path || repeat < 1) {
        fprintf(stderr, "usage: img816 [--repeat N] [--quiet] [--check EXPECTED] FILE|-\n");
        return 2;
    }
    FILE *f = strcmp(path, "-") ? fopen(path, "rb") : stdin;
    if (!f) { fprintf(stderr, "img816: cannot open %s\n", path); return 2; }
    std::vector<Case> cases;
    bool ok = parse_input(f, cases);
    if (f != stdin) fclose(f);
    if (!ok) return 2;

    gs2_app_inues.base_path = "./";
    gs2_app_inues.pref_path = gs2_app_inues.base_path;
    gs2_app_inues.console_mode = false;

    Machine m;
    m.mmu = new FlatMMU();
    m.cpu = new cpu_state(PROCESSOR_65816);
    m.clock = new NClock(CLOCK_SET_US, CLOCK_FREE_RUN);
    m.cpu->cpun = createCPU(PROCESSOR_65816, m.clock);
    m.cpu->core = m.cpu->cpun.get();
    m.cpu->trace = false;
    m.cpu->set_mmu(m.mmu);
    m.cpu->reset();

    std::vector<std::string> first(cases.size());
    long mismatch = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (long r = 0; r < repeat; r++) {
        for (size_t i = 0; i < cases.size(); i++) {
            std::string line = run_case(m, cases[i]);
            if (r == 0) first[i] = std::move(line);
            else if (line != first[i]) mismatch++;
        }
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!quiet) for (const std::string &l : first) printf("%s\n", l.c_str());
    long runs = (long)cases.size() * repeat;
    fprintf(stderr, "img816: cases=%zu runs=%ld run_s=%.3f runs_per_s=%.0f repeat_mismatch=%ld\n", cases.size(), runs,
            secs, secs > 0 ? runs / secs : 0.0, mismatch);

    int rc = mismatch ? 1 : 0;
    if (check) {
        FILE *ef = fopen(check, "rb");
        if (!ef) { fprintf(stderr, "img816: cannot open %s\n", check); return 2; }
        std::vector<std::string> want;
        char lb[1 << 16];
        while (fgets(lb, sizeof lb, ef)) {
            std::string l(lb);
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
            if (!l.empty() && l[0] != '#') want.push_back(l);
        }
        fclose(ef);
        long diff = 0;
        size_t n = std::max(want.size(), first.size());
        for (size_t i = 0; i < n; i++) {
            const std::string &g = i < first.size() ? first[i] : std::string("(missing)");
            const std::string &w = i < want.size() ? want[i] : std::string("(missing)");
            if (g != w) {
                diff++;
                fprintf(stderr, "img816: case %zu differs\n  got  %s\n  want %s\n", i, g.c_str(), w.c_str());
            }
        }
        fprintf(stderr, "img816: check %zu cases, %ld differ\n", n, diff);
        if (diff) rc = 1;
    }
    return rc;
}
