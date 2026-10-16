// WDM $FE: A.low byte. WDM $FD: Y bytes at (X.low << 16) | A, max 4096.
// Registers/flags are preserved, memory is observed without guest bus reads.
#pragma once
#include "a2gspu_output.hpp"
#include "a2gspu_observe.hpp"
#include "a2gspu_rails_registry.hpp"
#include "computer.hpp"
#include "cpu.hpp"
#include "a2gspu_events.hpp"
inline A2ByteSink g_wdm_console;
inline void a2_wdm_byte(cpu_state *cpu, void *) { g_wdm_console.byte((uint8_t)cpu->a);a2events::byte("wdm",(uint8_t)cpu->a); }
inline void a2_wdm_string(cpu_state *cpu, void *) {
    uint32_t addr = ((cpu->x & 255u) << 16) | (cpu->a & 65535u);
    unsigned n = cpu->y & 65535u;
    if (n > 4096 || addr + n > 0x1000000u) g_wdm_console.fail("invalid WDM string range (max 4096)");
    unsigned char data[4096];
    for (unsigned i = 0; i < n; ++i) {
        if (!a2obs_readable(cpu->mmu, addr + i)) g_wdm_console.fail("unreadable WDM string byte");
        data[i] = a2obs_peek(cpu->mmu, addr + i);
    }
    g_wdm_console.write(data, n);
    if(a2events::enabled())for(unsigned i=0;i<n;i++)a2events::byte("wdm",data[i]);
}
inline void a2_wdm_read(char *buf, size_t n) {
    std::snprintf(buf, n, "status=OK enabled=%d bytes=%llu operands=FD,FE model=wdm-byte-string binding=%s",
        g_wdm_console.enabled, g_wdm_console.bytes, g_wdm_console.binding.c_str());
}
inline a2rails::Registrar a2_wdm_reader{"A2GSPU_WDM_CONSOLE", a2_wdm_read};
inline void a2_wdm_init(computer_t *computer) {
    const char *spec = std::getenv("A2GSPU_WDM_CONSOLE");
    if (!spec) return;
    if (computer->cpu->cpu_type != PROCESSOR_65816) {
        std::fprintf(stderr, "A2GSPU WDM_CONSOLE: FAIL requires a 65816\n"); std::exit(2);
    }
    if (computer->cpu->wdm_handlers[0xFD].handler || computer->cpu->wdm_handlers[0xFE].handler) {
        std::fprintf(stderr, "A2GSPU WDM_CONSOLE: FAIL operand already registered\n"); std::exit(2);
    }
    g_wdm_console.open("WDM_CONSOLE", spec);
    computer->cpu->set_wdm_handler(0xFD, {a2_wdm_string, nullptr});
    computer->cpu->set_wdm_handler(0xFE, {a2_wdm_byte, nullptr});
}
