#pragma once

#include <string>
#include <cstdint>
#include <vector>

#include "mmus/mmu.hpp"
#include "cpus/processor_type.hpp"
#include "debugger/trace.hpp"

class Disassembler {
    public:
        Disassembler(MMU *mmu, processor_type cputype);
        ~Disassembler();

        void setAddress(uint32_t address);
        std::vector<std::string> disassemble(int count);
        void disassemble_one();

        /** Seed assumed M/X widths (1 = 8-bit, 0 = 16-bit). Also used after SEP/REP while walking. */
        void set_mx(bool m_8bit, bool x_8bit);

        /** Match trace column layout and Bytes toggle for prospective / list output. */
        void set_format(const trace_column_layout &layout, bool show_opbytes);

        // FORK: the CTRL `dis` verb's names for the same seed, and what the walk
        // assumed. setMX is set_mx under the name the rail used first.
        void setMX(bool m, bool x) { set_mx(m, x); }
        bool mIs8() const { return m_8bit; }
        bool xIs8() const { return x_8bit; }

        // Bytes the MMU could not show (I/O and other handler-backed pages).
        // A listing built on the placeholder byte is fiction, so the caller
        // reports the count rather than letting it pass as a clean listing —
        // the three-outcome contract the `read` verb already keeps.
        int  unreadable() const { return n_unreadable; }
        void resetUnreadable() { n_unreadable = 0; }

    private:
        uint32_t address;
        MMU *mmu;
        std::vector<std::string> output_buffer;
        processor_type cpu_type;
        int16_t cpu_mask;
        bool m_8bit = true;
        bool x_8bit = true;
        trace_column_layout layout_{};
        bool show_opbytes_ = true;
        bool layout_set_ = false;
        int  n_unreadable = 0;       // FORK: bytes the MMU refused to show
        bool last_read_ok = true;    // FORK: did the most recent read_mem see memory?

        uint8_t read_mem(uint32_t address);
        int resolve_insn_size(const disasm_entry *da, const address_mode_entry *am) const;
        void apply_sep_rep(const disasm_entry *da, uint8_t mask);
};
