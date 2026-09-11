// Execution coverage must describe consumed instruction bytes, never the gap
// between two PCs. These owned fixtures execute the real CPU over flat RAM.
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>
#include <filesystem>

#include "clock.hpp"
#include "gs2.hpp"
#include "cpu.hpp"
#include "cpus/cpu_implementations.hpp"
#include "mmus/mmu.hpp"
#include "NClock.hpp"
#include "a2gspu_coverage.hpp"

static uint8_t memory[0x30000];
static int failures = 0, cases = 0;

struct Harness {
    MMU mmu{0x300, GS2_PAGE_SIZE};
    NClock clock{CLOCK_SET_US, CLOCK_FREE_RUN};
    cpu_state cpu;
    processor_type type;

    explicit Harness(processor_type t) : cpu(t), type(t) {
        for (int i = 0; i < 0x300; ++i)
            mmu.map_page_both(i, &memory[i * 256], "COVERAGE TEST RAM");
        cpu.cpun = createCPU(t, &clock);
        cpu.core = cpu.cpun.get();
        cpu.trace = false;
        cpu.set_mmu(&mmu);
        cpu.reset();
    }

    void prepare(uint32_t pc, bool e = true, uint8_t p = 0x34) {
        std::memset(memory, 0xEA, sizeof(memory));
        cpu.full_pc = pc;
        cpu.full_db = 0;
        cpu.a = cpu.x = cpu.y = cpu.d = 0;
        cpu.sp = 0x1FF;
        cpu.E = e;
        cpu.p = p;
        cpu.irq_pipe = 0;
        cpu.irq_asserted = false;
        cpu.clock_stopped = cpu.reset_asserted = cpu.rdy = false;
        a2gspu_cov_reset();
    }

    void code(uint32_t pc, std::initializer_list<uint8_t> bytes) {
        for (uint8_t b : bytes) {
            memory[pc] = b;
            pc = (pc & 0xFF0000) | ((pc + 1) & 0xFFFF);
        }
    }

    void execute(unsigned n = 1) {
        while (n--) cpu.cpun->execute_next(&cpu);
    }

    void expect(const char *name, uint32_t pc,
                std::initializer_list<uint32_t> bytes) {
        ++cases;
        std::vector<bool> want(sizeof(memory), false);
        for (uint32_t b : bytes) want[b] = true;
        bool bad = cpu.full_pc != pc || g_cov_marked != bytes.size();
        unsigned unexpected = 0, missing = 0;
        for (uint32_t i = 0; i < sizeof(memory); ++i) {
            bool got = (g_cov_bits[i >> 3] & (1u << (i & 7))) != 0;
            if (got && !want[i] && unexpected++ == 0)
                std::printf("FAIL %s (%d): unexecuted byte $%06X marked\n", name, type, i);
            if (!got && want[i] && missing++ == 0)
                std::printf("FAIL %s (%d): instruction byte $%06X missing\n", name, type, i);
        }
        if (bad || unexpected || missing) {
            ++failures;
            std::printf("FAIL %s (%d): PC=$%06X expected=$%06X, marked=%llu expected=%zu\n",
                        name, type, cpu.full_pc, pc,
                        (unsigned long long)g_cov_marked, bytes.size());
        }
    }
};

static void shared_cases(processor_type type) {
    Harness h(type);
    // BRA +1 skips one data byte. The former next-PC delta falsely proved it
    // was code; executing the target NOP also checks the final instruction.
    h.prepare(0x1000);
    h.code(0x1000, {uint8_t(type == PROCESSOR_6502 ? 0xD0 : 0x80), 0x01, 0x42, 0xEA});
    h.execute(2);
    h.expect("taken branch skips data", 0x1004, {0x1000, 0x1001, 0x1003});

    h.prepare(0x1000);
    h.code(0x1000, {0xD0, 0x02, 0x42, 0x42});
    h.execute(2);
    h.expect("taken branch skips two bytes", 0x1005, {0x1000, 0x1001, 0x1004});

    h.prepare(0x1000);
    h.code(0x1000, {0xD0, 0xFE});
    h.execute(2);
    h.expect("backward branch retains operand", 0x1000, {0x1000, 0x1001});

    h.prepare(0x10FE, true, 0x36); // Z=1: BNE is not taken.
    h.code(0x10FE, {0xD0, 0x7F});
    h.execute();
    h.expect("untaken branch at page boundary", 0x1100, {0x10FE, 0x10FF});

    h.prepare(0x1000);
    h.code(0x1000, {0x4C, 0x04, 0x10, 0x42});
    h.execute(2);
    h.expect("absolute jump skips data", 0x1005, {0x1000, 0x1001, 0x1002, 0x1004});

    h.prepare(0x1000);
    h.code(0x1000, {0x6C, 0x00, 0x20});
    memory[0x2000] = 0x00; memory[0x2001] = 0x30;
    h.execute();
    h.expect("indirect jump excludes pointer reads", 0x3000, {0x1000, 0x1001, 0x1002});

    h.prepare(0xFFFF);
    h.code(0xFFFF, {0xA9, 0x42});
    h.execute();
    h.expect("immediate wraps inside program bank", 0x0001, {0xFFFF, 0x0000});

    h.prepare(0x1000);
    h.execute();
    h.expect("single NOP excludes phantom fetch", 0x1001, {0x1000});

    a2gspu_cov_reset();
    h.expect("reset leaves no pending instruction", 0x1001, {});
    h.cpu.clock_stopped = true;
    h.execute();
    h.expect("stopped CPU proves no new code", 0x1001, {});

    h.prepare(0x1000);
    h.code(0x1000, {0x00, 0x42});
    memory[0xFFFE] = 0x00; memory[0xFFFF] = 0x30;
    h.execute();
    h.expect("BRK includes signature, excludes vector", 0x3000, {0x1000, 0x1001});

    h.prepare(0x1000);
    memory[0xFFFE] = 0x00; memory[0xFFFF] = 0x30;
    h.cpu.irq_pipe = 0x02;
    h.execute();
    h.expect("hardware IRQ discards fetch, proves no code", 0x3000, {});

    // Observe-don't-disturb: the same real-core program has identical state,
    // memory and cycles with the rail disabled, and leaves its bitmap untouched.
    h.prepare(0x1000);
    h.code(0x1000, {0xA9, 0x42, 0x8D, 0x00, 0x20, 0xEA});
    uint64_t start = h.clock.get_cycles();
    h.execute(3);
    uint64_t cycles = h.clock.get_cycles() - start;
    auto a = h.cpu.a, x = h.cpu.x, y = h.cpu.y, sp = h.cpu.sp;
    auto p = h.cpu.p;
    std::vector<uint8_t> ram(memory, memory + sizeof(memory));
    h.expect("enabled observation", 0x1006, {0x1000, 0x1001, 0x1002, 0x1003, 0x1004, 0x1005});
    h.prepare(0x1000);
    h.code(0x1000, {0xA9, 0x42, 0x8D, 0x00, 0x20, 0xEA});
    std::vector<uint8_t> bits(g_cov_bits, g_cov_bits + g_cov_nbytes);
    auto *bitmap = g_cov_bits;
    g_cov_on = false;
    start = h.clock.get_cycles();
    h.execute(3);
    bool unchanged = cycles == h.clock.get_cycles() - start &&
        a == h.cpu.a && x == h.cpu.x && y == h.cpu.y && sp == h.cpu.sp && p == h.cpu.p &&
        std::memcmp(ram.data(), memory, sizeof(memory)) == 0 &&
        bitmap == g_cov_bits && std::memcmp(bits.data(), g_cov_bits, g_cov_nbytes) == 0;
    g_cov_on = true;
    h.expect("disabled observation", 0x1006, {});
    if (!unchanged) {
        ++failures;
        std::printf("FAIL coverage changed guest state/cycles or disabled bitmap (%d)\n", type);
    }
}

static void bound_cases() {
    auto dir=std::filesystem::temp_directory_path()/
        ("gs2-cov-map-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    auto plan=dir/"image.map",artifact=dir/"run.cov";
    auto write_plan=[&](std::initializer_list<uint8_t> bytes) {
        std::vector<uint8_t> data(68+bytes.size(),0);
        memcpy(data.data(),"A2MAP1\0",8);a2cov_put32(data.data()+8,1);a2cov_put32(data.data()+12,1);
        a2cov_put64(data.data()+16,bytes.size());
        a2cov_put32(data.data()+60,0x1000);a2cov_put32(data.data()+64,(uint32_t)bytes.size());
        std::copy(bytes.begin(),bytes.end(),data.begin()+68);
        FILE *f=fopen(plan.string().c_str(),"wb");
        bool ok=f && fwrite(data.data(),1,data.size(),f)==data.size();if(f)fclose(f);
        if(!ok){++failures;printf("FAIL cannot create owned map fixture\n");}
    };
    auto check=[&](const char *label,bool ok){++cases;if(!ok){++failures;printf("FAIL bound coverage: %s\n",label);}};
    Harness h(PROCESSOR_65816);
    write_plan({0x80,0x01,0x42,0xEA,0x00});
    check("bind map",a2gspu_cov_map_load(plan.string().c_str()));
    h.prepare(0x1000);
    h.code(0x1000,{0x80,0x01,0x42,0xEA,0x00});
    memory[0x1004]=0xFF; // Unexecuted data may change without invalidating code.
    h.execute(2);
    check("precise bytes and instruction starts",g_cov_maps[0].bits[0]==0x0B &&
          g_cov_maps[0].starts==std::vector<uint8_t>({0x94,0,0,0x94,0}));
    check("mutable data permits write",a2gspu_cov_write(artifact.string().c_str()));
    uint64_t generation=g_cov_generation;
    a2gspu_cov_reset();
    check("reset separates generations",g_cov_generation>generation && !g_cov_marked && g_cov_maps[0].bits[0]==0);
    h.cpu.full_pc=0x1000;memory[0x1003]=0xA9;
    h.execute(2);
    check("stale executed code refuses artifact",g_cov_map_error[0] && !a2gspu_cov_write(artifact.string().c_str()));

    write_plan({0xA9,0x43,0xEA});
    generation=g_cov_generation;
    check("overlay rebind clears prior proof",a2gspu_cov_map_load(plan.string().c_str()) &&
          g_cov_generation>generation && !g_cov_marked && !g_cov_map_error[0]);
    h.prepare(0x1000,false,0x34);
    h.code(0x1000,{0xA9,0x43,0xEA});h.execute();
    check("new overlay records own bytes",a2gspu_cov_write(artifact.string().c_str()) && g_cov_marked==2);
    h.cpu.full_pc=0x1000;h.cpu.p=0x14;h.execute();
    check("mixed instruction widths refuse union",strstr(g_cov_map_error,"ambiguous") &&
          !a2gspu_cov_write(artifact.string().c_str()));
    check("missing plan disarms previous mapping",!a2gspu_cov_map_load((dir/"absent.map").string().c_str()) &&
          !g_cov_on && g_cov_maps.empty());
    write_plan({0xA9});
    check("bind clipped instruction fixture",a2gspu_cov_map_load(plan.string().c_str()));
    h.prepare(0x1000);h.code(0x1000,{0xA9,0x42});h.execute();
    check("partial instruction mapping refuses evidence",strstr(g_cov_map_error,"boundary") &&
          !a2gspu_cov_write(artifact.string().c_str()));
    std::filesystem::remove(plan);std::filesystem::remove(artifact);std::filesystem::remove(dir);
}

int main() {
    if (!a2gspu_cov_init("000000-02FFFF")) return 1;
    for (auto type : {PROCESSOR_6502, PROCESSOR_65C02, PROCESSOR_65816})
        shared_cases(type);

    Harness h(PROCESSOR_65816);
    for (uint8_t p : {0x04, 0x14, 0x24, 0x34}) {
        h.prepare(0x11000, false, p);
        h.code(0x11000, {0xA9, 0x34, 0x12});
        h.execute();
        if (p & 0x20) h.expect("native M8 immediate", 0x11002, {0x11000, 0x11001});
        else h.expect("native M16 immediate", 0x11003, {0x11000, 0x11001, 0x11002});

        h.prepare(0x11000, false, p);
        h.code(0x11000, {0xA2, 0x34, 0x12});
        h.execute();
        if (p & 0x10) h.expect("native X8 immediate", 0x11002, {0x11000, 0x11001});
        else h.expect("native X16 immediate", 0x11003, {0x11000, 0x11001, 0x11002});
    }

    h.prepare(0x1000, true, 0x04); // E still selects 8-bit operands.
    h.code(0x1000, {0xA9, 0x34, 0x12});
    h.execute();
    h.expect("emulation overrides M bit", 0x1002, {0x1000, 0x1001});

    h.prepare(0x1FFFE, false, 0x04);
    h.code(0x1FFFE, {0xA9, 0x34, 0x12});
    h.execute();
    h.expect("native M16 bank wrap", 0x10001, {0x1FFFE, 0x1FFFF, 0x10000});

    h.prepare(0x11000, false);
    h.code(0x11000, {0x5C, 0x00, 0x20, 0x02});
    h.execute();
    h.expect("long jump includes all four bytes", 0x22000, {0x11000, 0x11001, 0x11002, 0x11003});

    h.prepare(0x11000, false);
    h.code(0x11000, {0x82, 0x01, 0x00, 0x42});
    h.execute(2);
    h.expect("BRL skips data", 0x11005, {0x11000, 0x11001, 0x11002, 0x11004});

    h.prepare(0x1FFFE, false);
    h.code(0x1FFFE, {0x22, 0x00, 0x20, 0x02});
    h.execute();
    h.expect("JSL operand wraps before program bank changes", 0x22000,
             {0x1FFFE, 0x1FFFF, 0x10000, 0x10001});

    h.prepare(0x1000, false);
    h.code(0x1000, {0xC2, 0x20, 0xA9, 0x34, 0x12, 0xE2, 0x20, 0xA9, 0x56});
    h.execute(4);
    h.expect("REP SEP use preinstruction width", 0x1009,
             {0x1000, 0x1001, 0x1002, 0x1003, 0x1004, 0x1005, 0x1006, 0x1007, 0x1008});

    Harness c(PROCESSOR_65C02);
    c.prepare(0x1000);
    c.code(0x1000, {0x02, 0x42});
    c.execute();
    c.expect("65C02 two-byte NOP", 0x1002, {0x1000, 0x1001});
    c.prepare(0x1000);
    c.code(0x1000, {0x5C, 0x42, 0x42});
    c.execute();
    c.expect("65C02 three-byte NOP", 0x1003, {0x1000, 0x1001, 0x1002});

    a2gspu_cov_off();
    bound_cases();
    a2gspu_cov_off();
    std::printf("Coverage: %d cases, %d failures\n", cases, failures);
    return failures ? 1 : 0;
}
