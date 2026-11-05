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
 *   --machine flat|iigs
 *                flat (the default) is the memory model above. iigs runs the
 *                same cases on the Apple IIgs memory map: the FPI (MMU_IIgs)
 *                with its Mega II, the 2.8 MHz IIgs clock (NClockIIgs) and a
 *                IIgs video scanner, which is what advances the 1 MHz phase a
 *                slow-side cycle waits for. The ROM image is 128 KB of zeros
 *                (ROM01 size; nothing is read from disk) and RAM is the
 *                motherboard plus 8 MB. Before each case the FPI registers go
 *                back to their reset values ($C036 fast, $C035 = $00: every
 *                shadow on, language card ROM) and the clock and scanner
 *                restart at 0, so a case starts at 14M tick 0 on a 1 MHz edge
 *                with the refresh counter at 0; Mega II soft switches the CPU
 *                changed are not reset. load, window and the clear between
 *                cases go through the MMU's observation-free probe_poke and
 *                probe_peek (a load the map cannot take -- ROM, $C0xx -- is
 *                an input error, exit 2); the write set comes from the CPU
 *                write log. cyc= stays the CPU cycle count.
 *   --buslog F   write the bus access log (src/a2gspu_buslog.hpp, format v1)
 *                to F: its header line, then for each case `case NAME`, one
 *                record per CPU cycle of the case, and
 *                `end NAME runner_cyc=N clock_c14m=N|- dropped=N` -- the cycle
 *                count of the case line and the clock's 14M ticks over the
 *                case (`-` on the flat machine, which has no bus-side model:
 *                its records carry side and costs `-`). Only the first pass of
 *                --repeat is logged. apps/img816/buslogcheck checks a log.
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
#include "mmus/mmu_iie.hpp"
#include "mmus/mmu_iigs.hpp"
#include "mmus/iigs_memory.hpp"
#include "devices/displaypp/VideoScannerIIgs.hpp"
#include "NClock.hpp"
#include "a2gspu_buslog.hpp"
#include "a2gspu_writelog.hpp"

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

// The flat machine's clock when --buslog is given: the plain clock, plus the bus access
// log's record for each cycle (the plain NClock writes none, so without --buslog the
// flat runner pays nothing for the log).
class FlatLogClock : public NClock {
public:
    FlatLogClock() : NClock(CLOCK_SET_US, CLOCK_FREE_RUN) {}
    void slow_incr_cycles() override {
        cycles++;
        if (a2buslog::g_on) a2buslog::end_cycle(cycles - 1);
    }
};

// The IIgs clock, restartable between cases (its counters are protected members).
class CaseClock : public NClockIIgs {
public:
    CaseClock() : NClockIIgs(CLOCK_SET_US, CLOCK_2_8MHZ) {}
    void restart() {
        cycles = 0; c_14M = 0; video_cycles = 0;
        video_cycle_14M_count = 0; scanline_14M_count = 0;
        frame_start_c14M = 0; frame_end_c14M = current.c14M_per_frame; frame_count = 0;
        cpu_div = 0;
        ram_refresh_cycles = 0; vidlinecycles = 0; video_c14m = 0;
        cycle_type = CYCLE_TYPE_FAST;
    }
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
    FlatMMU *mmu = nullptr;          // flat machine
    MMU_IIgs *gs = nullptr;          // iigs machine
    VideoScannerIIgs *scanner = nullptr;
    CaseClock *gs_clock = nullptr;
    cpu_state *cpu;
    NClock *clock;
    FILE *buslog = nullptr;          // --buslog, first pass only
};

static uint8_t peek(Machine &m, uint32_t a) { return m.gs ? m.gs->probe_peek(a) : m.mmu->mem[a]; }

static std::string run_case(Machine &m, const Case &c, bool &input_error) {
    FlatMMU *mmu = m.mmu;
    cpu_state *cpu = m.cpu;
    if (m.gs) {
        m.gs->reset(false);
        m.gs_clock->restart();
        m.scanner->reset();
        for (const Load &ld : c.loads)
            for (size_t k = 0; k < ld.bytes.size(); k++)
                if (!m.gs->probe_poke(ld.addr + (uint32_t)k, ld.bytes[k])) {
                    fprintf(stderr, "img816: case %s: the iigs map cannot load %06X\n", c.name.c_str(),
                            ld.addr + (unsigned)k);
                    input_error = true;
                    return std::string();
                }
        a2writelog::mark(a2writelog::kMaxCap);
    } else {
        for (const Load &ld : c.loads) std::copy(ld.bytes.begin(), ld.bytes.end(), mmu->mem.begin() + ld.addr);
        mmu->wlog.clear();
    }
    if (m.buslog) a2buslog::mark(a2buslog::kMaxCap);

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
    uint64_t t0 = m.clock->get_c14m();
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
    if (m.buslog) {
        a2buslog::off();
        std::fprintf(m.buslog, "case %s\n", c.name.c_str());
        a2buslog::dump_range(m.buslog, 0, a2buslog::g_log.size());
        if (m.gs)
            std::fprintf(m.buslog, "end %s runner_cyc=%llu clock_c14m=%llu dropped=%llu overlap=%llu\n",
                         c.name.c_str(), (unsigned long long)total,
                         (unsigned long long)(m.clock->get_c14m() - t0), (unsigned long long)a2buslog::g_dropped,
                         (unsigned long long)a2buslog::g_overlap);
        else
            std::fprintf(m.buslog, "end %s runner_cyc=%llu clock_c14m=- dropped=%llu overlap=%llu\n",
                         c.name.c_str(), (unsigned long long)total, (unsigned long long)a2buslog::g_dropped,
                         (unsigned long long)a2buslog::g_overlap);
        a2buslog::g_log.clear();
    }
    // The write set: the flat memory's own log, or the CPU write log on the iigs map.
    static std::vector<uint32_t> gs_wlog;
    if (m.gs) {
        a2writelog::off();
        gs_wlog.clear();
        for (const a2writelog::Entry &e : a2writelog::g_log) gs_wlog.push_back(e.addr);
    }
    const std::vector<uint32_t> &wlog = m.gs ? gs_wlog : mmu->wlog;

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
            uint8_t b = peek(m, c.windows[i].addr + k);
            out += hx[b >> 4]; out += hx[b & 15];
        }
    }
    std::vector<uint32_t> ws(wlog);
    std::sort(ws.begin(), ws.end());
    ws.erase(std::unique(ws.begin(), ws.end()), ws.end());
    snprintf(buf, sizeof buf, "\tnw=%zu\twr=", wlog.size());
    out += buf;
    for (size_t i = 0; i < ws.size(); i++) {
        snprintf(buf, sizeof buf, i ? ",%06X" : "%06X", ws[i]);
        out += buf;
    }

    // clear everything this case loaded or wrote
    if (m.gs) {
        // probe_poke refuses $C0xx and ROM; those were not memory to begin with.
        for (const Load &ld : c.loads)
            for (size_t k = 0; k < ld.bytes.size(); k++) m.gs->probe_poke(ld.addr + (uint32_t)k, 0);
        for (uint32_t ad : wlog) m.gs->probe_poke(ad, 0);
        a2writelog::g_log.clear();
    } else {
        for (const Load &ld : c.loads) std::fill(mmu->mem.begin() + ld.addr, mmu->mem.begin() + ld.addr + ld.bytes.size(), 0);
        for (uint32_t ad : mmu->wlog) mmu->mem[ad] = 0;
    }
    return out;
}

int main(int argc, char **argv) {
    const char *path = nullptr, *check = nullptr, *buslog_path = nullptr, *machine = "flat";
    long repeat = 1;
    bool quiet = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--repeat") && i + 1 < argc) repeat = atol(argv[++i]);
        else if (!strcmp(argv[i], "--machine") && i + 1 < argc) machine = argv[++i];
        else if (!strcmp(argv[i], "--buslog") && i + 1 < argc) buslog_path = argv[++i];
        else if (!strcmp(argv[i], "--quiet")) quiet = true;
        else if (!strcmp(argv[i], "--check") && i + 1 < argc) check = argv[++i];
        else path = argv[i];
    }
    const bool iigs = !strcmp(machine, "iigs");
    if (!path || repeat < 1 || (!iigs && strcmp(machine, "flat"))) {
        fprintf(stderr, "usage: img816 [--machine flat|iigs] [--buslog FILE] [--repeat N] [--quiet] [--check EXPECTED] FILE|-\n");
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
    m.cpu = new cpu_state(PROCESSOR_65816);
    static std::vector<uint8_t> rom;
    if (iigs) {
        // All-zero ROM01-sized image: the map, not the firmware, is under test.
        const size_t rom_size = 128 * 1024;
        rom.assign(rom_size, 0);
        const size_t fast_ram = iigs_memory::fast_ram_bytes(rom_size);
        MMU_IIe *megaii = new MMU_IIe(256, 128 * 1024, rom.data() + (rom_size - 65536) + 0xC000);
        m.gs = new MMU_IIgs(256, (int)fast_ram, (uint32_t)rom_size, rom.data(), megaii);
        m.gs->init_map();
        m.gs_clock = new CaseClock();
        m.clock = m.gs_clock;
        m.gs->set_clock(m.gs_clock);
        m.scanner = new VideoScannerIIgs(megaii);
        m.scanner->initialize();
        m.gs_clock->set_video_scanner(m.scanner);
    } else {
        m.mmu = new FlatMMU();
        if (buslog_path) m.clock = new FlatLogClock();
        else m.clock = new NClock(CLOCK_SET_US, CLOCK_FREE_RUN);
    }
    m.cpu->cpun = createCPU(PROCESSOR_65816, m.clock);
    m.cpu->core = m.cpu->cpun.get();
    m.cpu->trace = false;
    if (iigs) m.cpu->set_mmu(m.gs);
    else m.cpu->set_mmu(m.mmu);
    m.cpu->reset();
    FILE *blf = nullptr;
    if (buslog_path) {
        blf = fopen(buslog_path, "wb");
        if (!blf) { fprintf(stderr, "img816: cannot open %s\n", buslog_path); return 2; }
        a2buslog::header(blf);
        fprintf(blf, "# img816 machine=%s\n", machine);
    }

    std::vector<std::string> first(cases.size());
    long mismatch = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (long r = 0; r < repeat; r++) {
        m.buslog = r == 0 ? blf : nullptr;
        for (size_t i = 0; i < cases.size(); i++) {
            bool input_error = false;
            std::string line = run_case(m, cases[i], input_error);
            if (input_error) return 2;
            if (r == 0) first[i] = std::move(line);
            else if (line != first[i]) mismatch++;
        }
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (blf && fclose(blf) != 0) { fprintf(stderr, "img816: cannot write %s\n", buslog_path); return 2; }
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
