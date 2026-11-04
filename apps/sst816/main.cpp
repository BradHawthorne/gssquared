/**
 * sst816: run the 65816 core against SingleStepTests-format vectors.
 *
 * Input is one JSON file in the SingleStepTests ProcessorTests 65816 v1 format
 * (an array of { name, initial, final, cycles } objects), read from a path or
 * from stdin ("-"), so a large archive member can be streamed in, e.g.
 *
 *     unzip -p ProcessorTests-main.zip ProcessorTests-main/65816/v1/a9.n.json | sst816 -
 *
 * Each vector runs in-process on a flat 16 MB memory (no Apple II mapping, no
 * I/O), one instruction per vector. Checked per vector:
 *   cycles     cycles counted by the core == SST bus cycles (len(cycles); a
 *              trailing halted-bus record [null,null,"--------"] after WAI/STP
 *              is not an instruction cycle and is excluded)
 *   registers  pc, s, p, a, x, y, dbr, d, pbr, e (full 16-bit a/x/y)
 *   ram        every final RAM byte, and no write outside the final RAM list
 *
 * MVN/MVP: SST vectors run a fixed bus-cycle budget (len(cycles)), so a move
 * that is still going is cut part-way into an iteration. This core executes a
 * move iteration (7 cycles) as one instruction, so the runner executes whole
 * iterations while they fit in the budget. If the move did not finish, the
 * remaining r = budget - cycles (must be 0..3) are the opcode/operand fetches
 * of the next iteration; SST's final PC is then op_pc + r, so the runner
 * expects pc + r there. Everything else is compared as above. These "cut"
 * vectors are counted separately.
 *
 * Emulation-mode initial states: SST states with e=1 may carry an arbitrary
 * stack high byte, X/Y high bytes and P bits 4-5. On the 65816 e=1 forces
 * SH=$01, XH=YH=0 and M=X=1, so the runner applies that invariant before the
 * run and counts how many vectors it changed ("normalised").
 *
 * Output (stdout): one TSV summary line
 *   label vectors pass fail cyc_fail reg_fail ram_fail stray_fail cut normalised
 * and, with --detail N, up to N failure descriptions on stderr.
 * Exit status: 0 if every vector passed, 1 if any failed, 2 on input error.
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
#include <string>
#include <vector>

gs2_app_t gs2_app_inues;
uint64_t debug_level = 0;

/* ---------------------------------------------------------------- memory */

// Flat 24-bit RAM. Every write is logged so the runner can (a) report writes
// outside the expected set and (b) clear exactly what a vector touched.
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

/* ---------------------------------------------------------------- JSON */

// Minimal parser for the SST format: objects, arrays, integers, strings, null.
struct JV {
    enum T { NUL, NUM, STR, ARR, OBJ } t = NUL;
    long long n = 0;
    std::string s;
    std::vector<JV> a;                                 // ARR elements, or OBJ values
    std::vector<std::string> k;                        // OBJ keys
    const JV *get(const char *key) const {
        for (size_t i = 0; i < k.size(); i++) if (k[i] == key) return &a[i];
        return nullptr;
    }
};

struct Parser {
    const char *p, *e;
    bool err = false;
    void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t' || *p == ',' || *p == ':')) p++; }
    bool value(JV &v) {
        ws();
        if (p >= e) return false;
        char c = *p;
        if (c == '{') {
            v.t = JV::OBJ; p++;
            for (;;) {
                ws();
                if (p >= e) return false;
                if (*p == '}') { p++; return true; }
                JV key;
                if (!value(key) || key.t != JV::STR) return false;
                v.k.push_back(key.s);
                v.a.emplace_back();
                if (!value(v.a.back())) return false;
            }
        }
        if (c == '[') {
            v.t = JV::ARR; p++;
            for (;;) {
                ws();
                if (p >= e) return false;
                if (*p == ']') { p++; return true; }
                v.a.emplace_back();
                if (!value(v.a.back())) return false;
            }
        }
        if (c == '"') {
            v.t = JV::STR; p++;
            const char *s = p;
            while (p < e && *p != '"') { if (*p == '\\') p++; p++; }
            if (p >= e) return false;
            v.s.assign(s, p - s); p++;
            return true;
        }
        if (c == 'n') { if (e - p < 4 || strncmp(p, "null", 4)) return false; p += 4; v.t = JV::NUL; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            char *end; v.t = JV::NUM; v.n = strtoll(p, &end, 10);
            if (end == p) return false;
            p = end;
            return true;
        }
        return false;
    }
};

/* ---------------------------------------------------------------- runner */

struct Regs { long long pc, s, p, a, x, y, dbr, d, pbr, e; };

static bool get_regs(const JV *o, Regs &r) {
    if (!o || o->t != JV::OBJ) return false;
    const char *names[] = {"pc", "s", "p", "a", "x", "y", "dbr", "d", "pbr", "e"};
    long long *dst[] = {&r.pc, &r.s, &r.p, &r.a, &r.x, &r.y, &r.dbr, &r.d, &r.pbr, &r.e};
    for (int i = 0; i < 10; i++) {
        const JV *v = o->get(names[i]);
        if (!v || v->t != JV::NUM) return false;
        *dst[i] = v->n;
    }
    return true;
}

static FILE *open_input(const char *path) {
    if (strcmp(path, "-") == 0) return stdin;
    return fopen(path, "rb");
}

int main(int argc, char **argv) {
    const char *path = nullptr;
    std::string label = "-";
    int detail = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--detail") && i + 1 < argc) detail = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--label") && i + 1 < argc) label = argv[++i];
        else path = argv[i];
    }
    if (!path) {
        fprintf(stderr, "usage: sst816 [--label L] [--detail N] FILE|-\n");
        return 2;
    }
    FILE *f = open_input(path);
    if (!f) { fprintf(stderr, "sst816: cannot open %s\n", path); return 2; }
    std::string buf;
    {
        char chunk[1 << 16];
        size_t n;
        while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) buf.append(chunk, n);
        if (f != stdin) fclose(f);
    }

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

    Parser ps{buf.data(), buf.data() + buf.size()};
    ps.ws();
    if (ps.p >= ps.e || *ps.p != '[') { fprintf(stderr, "sst816: input is not a JSON array\n"); return 2; }
    ps.p++;

    long vectors = 0, pass = 0, cyc_fail = 0, reg_fail = 0, ram_fail = 0, stray_fail = 0, cut = 0, normalised = 0;
    int shown = 0;
    for (;;) {
        ps.ws();
        if (ps.p >= ps.e) { fprintf(stderr, "sst816: truncated input\n"); return 2; }
        if (*ps.p == ']') break;
        JV t;
        if (!ps.value(t) || t.t != JV::OBJ) { fprintf(stderr, "sst816: parse error near byte %lld\n", (long long)(ps.p - buf.data())); return 2; }
        const JV *name = t.get("name"), *ini = t.get("initial"), *fin = t.get("final"), *cyc = t.get("cycles");
        Regs I, F;
        if (!name || !get_regs(ini, I) || !get_regs(fin, F) || !cyc || cyc->t != JV::ARR) {
            fprintf(stderr, "sst816: malformed vector %ld\n", vectors); return 2;
        }
        const JV *iram = ini->get("ram"), *fram = fin->get("ram");
        if (!iram || !fram) { fprintf(stderr, "sst816: vector %ld has no ram\n", vectors); return 2; }
        vectors++;

        // --- load
        bool norm = false;
        if (I.e) {
            Regs G = I;
            I.s = 0x100 | (I.s & 0xFF); I.p |= 0x30; I.x &= 0xFF; I.y &= 0xFF;
            norm = (G.s != I.s || G.p != I.p || G.x != I.x || G.y != I.y);
        }
        if (norm) normalised++;
        for (const JV &kv : iram->a) mmu->mem[kv.a[0].n & 0xFFFFFF] = (uint8_t)kv.a[1].n;
        mmu->wlog.clear();
        cpu->halt = 0; cpu->clock_stopped = false; cpu->rdy = false;
        cpu->irq_asserted = 0; cpu->irq_pipe = 0; cpu->nmi_pending = false; cpu->boundary_abort_pending = false;
        cpu->E = I.e ? 1 : 0;
        cpu->p = (uint8_t)I.p;
        cpu->pc = (uint16_t)I.pc; cpu->pb = (uint8_t)I.pbr;
        cpu->full_db = 0; cpu->db = (uint8_t)I.dbr;
        cpu->sp = (uint16_t)I.s; cpu->a = (uint16_t)I.a;
        cpu->x = (uint16_t)I.x; cpu->y = (uint16_t)I.y; cpu->d = (uint16_t)I.d;

        // --- expected cycles
        long budget = (long)cyc->a.size();
        while (budget > 0) {
            const JV &last = cyc->a[budget - 1];
            if (last.t == JV::ARR && last.a.size() == 3 && last.a[0].t == JV::NUL && last.a[1].t == JV::NUL &&
                last.a[2].t == JV::STR && last.a[2].s == "--------") budget--;
            else break;
        }

        // --- run
        uint8_t opcode = mmu->mem[((uint32_t)I.pbr << 16) | (uint16_t)I.pc];
        uint64_t c0 = clock->get_cycles();
        long r = 0;
        bool is_cut = false;
        if (opcode == 0x44 || opcode == 0x54) {
            uint16_t op_pc = (uint16_t)I.pc;
            for (;;) {
                if ((long)(clock->get_cycles() - c0) + 7 > budget) { is_cut = true; break; }
                cpu->core->execute_next(cpu);
                if (cpu->pc != op_pc || cpu->pb != (uint8_t)I.pbr) break;
            }
            r = budget - (long)(clock->get_cycles() - c0);
            if (is_cut) cut++;
        } else {
            cpu->core->execute_next(cpu);
        }
        long got = (long)(clock->get_cycles() - c0);

        // --- compare
        std::string why;
        char tmp[160];
        bool cyc_bad = is_cut ? !(r >= 0 && r <= 3) : (got != budget);
        if (cyc_bad) { snprintf(tmp, sizeof tmp, "cycles core %ld sst %ld; ", got, budget); why += tmp; }
        long long pc_adj = is_cut ? (long long)((cpu->pc + r) & 0xFFFF) : cpu->pc;
        long long have[10] = {pc_adj, cpu->sp, cpu->p, cpu->a, cpu->x, cpu->y, cpu->db, cpu->d, cpu->pb, cpu->E};
        long long want[10] = {F.pc, F.s, F.p, F.a, F.x, F.y, F.dbr, F.d, F.pbr, F.e};
        const char *rn[10] = {"pc", "s", "p", "a", "x", "y", "dbr", "d", "pbr", "e"};
        bool reg_bad = false;
        for (int i = 0; i < 10; i++) if (have[i] != want[i]) {
            reg_bad = true;
            snprintf(tmp, sizeof tmp, "%s core %04llX sst %04llX; ", rn[i], have[i], want[i]); why += tmp;
        }
        bool ram_bad = false;
        int ram_shown = 0;
        std::vector<uint32_t> expect_addrs;
        expect_addrs.reserve(fram->a.size());
        for (const JV &kv : fram->a) {
            uint32_t ad = (uint32_t)kv.a[0].n & 0xFFFFFF;
            expect_addrs.push_back(ad);
            if (mmu->mem[ad] != (uint8_t)kv.a[1].n) {
                ram_bad = true;
                if (ram_shown++ < 3) { snprintf(tmp, sizeof tmp, "ram %06X core %02X sst %02X; ", ad, mmu->mem[ad], (unsigned)kv.a[1].n); why += tmp; }
            }
        }
        bool stray_bad = false;
        for (uint32_t ad : mmu->wlog) {
            bool found = false;
            for (uint32_t e : expect_addrs) if (e == ad) { found = true; break; }
            if (!found) { stray_bad = true; snprintf(tmp, sizeof tmp, "stray write %06X; ", ad); why += tmp; break; }
        }
        bool bad = cyc_bad || reg_bad || ram_bad || stray_bad;
        if (cyc_bad) cyc_fail++;
        if (reg_bad) reg_fail++;
        if (ram_bad) ram_fail++;
        if (stray_bad) stray_fail++;
        if (!bad) pass++;
        else if (shown < detail) {
            shown++;
            fprintf(stderr, "%s\t%s\t%s%s%s\n", label.c_str(), name->s.c_str(), why.c_str(),
                    is_cut ? "(cut) " : "", norm ? "(normalised)" : "");
        }

        // --- clear everything this vector touched
        for (const JV &kv : iram->a) mmu->mem[kv.a[0].n & 0xFFFFFF] = 0;
        for (uint32_t ad : expect_addrs) mmu->mem[ad] = 0;
        for (uint32_t ad : mmu->wlog) mmu->mem[ad] = 0;
    }

    long fail = vectors - pass;
    printf("%s\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\t%ld\n", label.c_str(), vectors, pass, fail,
           cyc_fail, reg_fail, ram_fail, stray_fail, cut, normalised);
    return fail ? 1 : 0;
}
