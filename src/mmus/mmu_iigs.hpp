#pragma once

#include "mmu.hpp"
#include "mmu_iie.hpp"
#include "iigs_shadow_flags.hpp"
#include "debug.hpp"
#include "NClock.hpp"
#include "bus_trace.hpp"
#include "mmu_state_trace.hpp"
#include "devices/slot_bus/slot_bus.hpp"
#include "devices/languagecard/LanguageCardLogic.hpp"

class MMU_IIgs : public MMU {
    protected:
        uint32_t ram_banks;
        uint8_t *main_ram = nullptr;
        uint32_t rom_banks;
        uint8_t *main_rom = nullptr;
        constexpr static uint32_t BANK_SIZE = 65536;

        uint8_t reg_slot = 0;
        uint8_t reg_shadow = 0;
        uint8_t reg_speed = 0x08;
        union {
            uint8_t reg_state = 0;
            struct {
                uint8_t g_intcxrom : 1;
                uint8_t g_rombank : 1;
                uint8_t g_lcbnk2 : 1; // 1 = LC Bank 2 Selected; 0 = LC Bank 1 Selected
                uint8_t g_rdrom : 1;
                uint8_t g_ramwrt : 1;
                uint8_t g_ramrd : 1;
                uint8_t g_page2 : 1;
                uint8_t g_altzp : 1; // 1 = ZP, Stack, LC are in Aux; 0 = in Main
            };
        };
        uint8_t g_80store;
        uint8_t g_hires;
        bool g_text;
        bool g_mixed;
            
        union {
            uint8_t reg_new_video=0;
            struct {
                uint8_t g_bank_latch: 1;
                uint8_t g_reserved1 : 4;
                uint8_t g_dhr_mono : 1;
                uint8_t g_aux_linear : 1;
                uint8_t g_shr_enabled : 1;                
            };
        };
        /* bool f_intcxrom = false;
        bool f_slotc3rom = false; */

        // "current memory map state" flags.
        // does not imply we need to track text s/s
        bool m_zp = false; // this is both read and write.
        bool m_text1_r = false; // 
        bool m_text1_w = false; // 
        bool m_hires1_r = false; // 
        bool m_hires1_w = false; // 
        bool m_all_r = false; // 
        bool m_all_w = false; //

        bool map_initialized = false;

        bool is_rom03 = false;

        //cpu_state *cpu = nullptr;
        NClock *clock = nullptr;

    public:
        MMU_IIe *megaii = nullptr;
        LanguageCardLogic ll;

        MMU_IIgs(size_t num_banks, int ram_size, uint32_t rom_size, uint8_t *rom, MMU_IIe *mmu_iie) : MMU(num_banks, BANK_SIZE), megaii(mmu_iie) {
            ram_banks = ram_size / BANK_SIZE;
            main_ram = new uint8_t[ram_banks * BANK_SIZE];
            rom_banks = rom_size / BANK_SIZE;
            is_rom03 = (rom_size > 0x20000); // 256KB image = ROM3/ROM4 memory controller (enables TEXT2 shadowing)
            main_rom = rom;
            map_initialized = false;
            reset();
        };
        virtual ~MMU_IIgs() { delete[] main_ram; /* main_rom is owned by caller */ }

        virtual uint8_t read(uint32_t address) override {
            if (address >= 0xFC0000) set_next_cycle_type(CYCLE_TYPE_FAST_ROM); // rom access is fast.

            uint8_t value = MMU::read(address);
            // $C0xx I/O is slot-visible; soft-switches toggle on READS too (per the IIe IOU 74LS259
            // decode), so the slot must see read accesses. Video-memory writes are tapped at the
            // resolved $E1 points, NOT here (no double-count).
            if (g_slot_bus_enabled && (address & 0xFF00) == 0xC000)
                slot_emit(address & 0xFFFF, value, true, (address >> 16) & 1);
            return value;
        }

        virtual void write(uint32_t address, uint8_t value) override {
            if (address >= 0xFC0000) set_next_cycle_type(CYCLE_TYPE_FAST_ROM); // rom access is fast.

            MMU::write(address, value);
            if (g_slot_bus_enabled && (address & 0xFF00) == 0xC000)
                slot_emit(address & 0xFFFF, value, false, (address >> 16) & 1);
        }

        inline bool shadow_is_enabled(uint32_t address) {
            uint32_t address_16 = address & 0xFFFF;
            uint32_t address_17 = address & 0x1FFFF;
            
            if ((address_16 >= 0x0400 && address_16 <= 0x07FF) && !(reg_shadow & SHADOW_INH_TEXT1)) {
                return true;
            }
            if (is_rom03 && ((address_16 >= 0x0800 && address_16 <= 0x0BFF) && !(reg_shadow & SHADOW_INH_TEXT2))) {
                return true;
            }
            if ((address_16 >= 0x2000 && address_16 <= 0x3FFF) && !(reg_shadow & SHADOW_INH_HGR1)) {
                return true;
            }
            if ((address_16 >= 0x4000 && address_16 <= 0x5FFF) && !(reg_shadow & SHADOW_INH_HGR2)) {
                return true;
            }
            if ((address_17 >= 0x12000 && address_17 <= 0x19FFF) && !(reg_shadow & SHADOW_INH_SHR)) {
                return true;
            }
            if ((address_17 >= 0x12000 && address_17 <= 0x15FFF) && !(reg_shadow & SHADOW_INH_AUXHGR)) {
                return true;
            }
            
            
            return false;
        }

        // TODO: remove, not used anywhere any more. bank_e1_read handles bank latch directly.
        /* inline uint8_t megaiiRead(uint32_t address) {
            if ((address & 0x1'0000) && g_bank_latch) {
                return megaii->get_memory_base()[address & 0x1'FFFF]; // does not currently have an interface for this
            }
            else return megaii->read(address & 0xFFFF);
        } */

        inline void megaiiWrite(uint32_t address, uint8_t value) {
            if ((address & 0x1'0000) && g_bank_latch) {
                megaii->get_memory_base()[address & 0x1'FFFF] = value;
                bus_trace_note(get_cycle_count(), address & 0x1'FFFF, value, 1); // shadowed SHR write (provenance 1)
                slot_emit(address & 0xFFFF, value, false, true);             // Mega-II write to $E1/aux (M2B0=1)
            }
            else {
                megaii->write(address & 0xFFFF, value);
                slot_emit(address & 0xFFFF, value, false, (address & 0x1'0000) != 0);
            }
            set_next_cycle_type(CYCLE_TYPE_SYNC);
        }

        void megaii_compose_map();

        inline bool is_iolc_shadowed() { return !(reg_shadow & SHADOW_INH_IOLC); }
        inline bool is_bank_latch() { return g_bank_latch; }

        // Faithful slot-3 signal decode for a Mega-II-side (slot-visible) cycle (the virtual slot).
        inline uint8_t slot_ctl(uint16_t a, bool is_read, bool m2b0) {
            uint8_t ctl = SLOT_CTL_M2SEL;                       // a valid Mega-II cycle
            if (is_read) ctl |= SLOT_CTL_READ;
            if (m2b0)    ctl |= SLOT_CTL_M2B0;
            if (!is_iolc_shadowed()) ctl |= SLOT_CTL_INH;
            if (a >= 0xC000 && a <= 0xCFFF) {                   // slot I/O strobes (this card's slot)
                if ((a & 0xFFF0) == (uint16_t)(0xC080 + SLOT_BUS_SLOT * 0x10)) ctl |= SLOT_CTL_DEVSEL;
                if ((a >> 8)     == (uint16_t)(0xC0   + SLOT_BUS_SLOT))        ctl |= SLOT_CTL_IOSEL;
                if (a >= 0xC800)                                              ctl |= SLOT_CTL_IOSTRB;
            }
            return ctl;
        }
        inline void slot_emit(uint16_t a, uint8_t data, bool is_read, bool m2b0) {
            if (!g_slot_bus_enabled) return;
            slot_bus_note(get_cycle_count(), a, data, slot_ctl(a, is_read, m2b0));
            mmu_state_emit();   // cycle-aligned ground-truth mapping state (change-gated; for the bus-snoop comparator)
        }
        // Sample the live INTERNAL mapping state into the ground-truth stream (change-gated). Runs AFTER
        // MMU::read/write has applied the soft-switch, so the bytes are the post-access state.
        inline void mmu_state_emit() {
            mmu_state_trace_note(get_cycle_count(), reg_state, reg_shadow, reg_new_video, reg_speed,
                (uint8_t)((g_80store ? 1 : 0) | (g_hires ? 2 : 0) | (g_text ? 4 : 0) | (g_mixed ? 8 : 0)),
                reg_slot, g_disp_vmode, g_disp_mode);
        }

        inline void set_shadow_register(uint8_t value) { if (DEBUG(DEBUG_MMUGS)) printf("setting shadow register: %02X\n", value); reg_shadow = value; }
        inline void set_speed_register(uint8_t value) { 
            if (DEBUG(DEBUG_MMUGS)) printf("setting speed register: %02X\n", value); 
            reg_speed = value;
            if (clock) ((NClockIIgs *)clock)->set_slow_mode(value & 0x80 ? false : true);
        }
        inline void set_state_register(uint8_t value) { 
            if (DEBUG(DEBUG_MMUGS)) printf("setting state register: %02X\n", value); 
            reg_state = value; 
            ll.FF_READ_ENABLE = !g_rdrom; // sync LC state with state reg 
            ll.FF_BANK_1 = !g_lcbnk2; // this was missing.. 
        }
        inline uint8_t shadow_register() { return reg_shadow; }
        inline uint8_t speed_register() { return reg_speed; }
        inline uint8_t state_register() { return reg_state; }
        inline uint8_t new_video_register() { return reg_new_video; }

        void set_ram_shadow_banks();
        //void shadow_register(uint16_t address, bool rw); // track accesses to softswitches the FPI also tracks.
        inline bool is_lc_bank1() { return ll.FF_BANK_1 == 1; }
        inline void set_lc_bank1(bool value) { ll.FF_BANK_1 = value; g_lcbnk2 = !value; }
        inline bool is_lc_read_enable() { return ll.FF_READ_ENABLE == 1; }
        inline void set_lc_read_enable(bool value) { ll.FF_READ_ENABLE = value; g_rdrom = !value; }
        inline bool is_lc_pre_write() { return ll.FF_PRE_WRITE == 1; }
        inline void set_lc_pre_write(bool value) { ll.FF_PRE_WRITE = value; }
        inline bool is_lc_write_enable() { return ll._FF_WRITE_ENABLE == 0; } // reverse sense since this is active low
        inline void set_lc_write_enable(bool value) { ll._FF_WRITE_ENABLE = value; }
        inline bool is_page2() { return g_page2; }
        inline void set_page2(bool value) { g_page2 = value; }
        inline bool is_hires() { return g_hires; }
        inline void set_hires(bool value) { g_hires = value; }
        inline bool is_text() { return g_text; }
        inline void set_text(bool value) { g_text = value; }
        inline bool is_mixed() { return g_mixed; }
        inline void set_mixed(bool value) { g_mixed = value; }
        inline bool is_altzp() { return g_altzp; }
        inline void set_altzp(bool value) { g_altzp = value; }
        
        inline bool is_80store() { return g_80store ? true : false; }
        inline bool is_slotc3rom() { return megaii->f_slotc3rom ? true : false; }
        inline void set_intcxrom(bool value);
        
        inline void set_slot_register(uint8_t value);
        
        inline uint8_t get_slot_register() { return reg_slot; }

        void bsr_map_memory();
        virtual uint8_t vp_read(uint32_t address) override;

        uint32_t calc_aux_write(uint32_t address);
        uint32_t calc_aux_read(uint32_t address);
        void init_c0xx_handlers();
        void write_c0xx(uint16_t address, uint8_t value);
        uint8_t read_c0xx(uint16_t address);
        
        virtual uint8_t *get_rom_base() { return main_rom; };
        // Base of ROM bank $FF (the last 64KB) — the language-card / $D000-$FFFF ROM
        // region. Bank $FF lives at the TOP of the image: offset (rom_banks-1)*64KB =
        // 0x10000 for a 128KB ROM01, 0x30000 for a 256KB ROM03/ROM04. Use this instead
        // of a hardcoded 0x10000 so the LC-ROM read path is size-agnostic.
        inline uint8_t *get_lc_rom_base() { return main_rom + (rom_banks - 1) * BANK_SIZE; }
        virtual uint8_t *get_memory_base() { return main_ram; };
        inline uint8_t *get_megaii_memory_base() { return megaii ? megaii->get_memory_base() : nullptr; }
        // Observation-free 24-bit peek for the introspection floor: NO soft-switch
        // dispatch, NO slot-bus emit, NO clock tick — an external probe, never a hook
        // the emulated OS can see. $E0/$E1 read the Mega II image directly (SHR window
        // + toolbox work areas); all else via MMU::read_raw (never the IO handler path).
        uint8_t probe_peek(uint32_t addr24) override {
            uint32_t bank = (addr24 >> 16) & 0xFF;
            if ((bank | 1) == 0xE1) {
                uint8_t *mb = get_megaii_memory_base();
                if (mb) return mb[((bank & 1) << 16) | (addr24 & 0xFFFF)];
            }
            return read_raw(addr24);
        }
        inline uint64_t get_cycle_count() { return clock ? clock->get_cycles() : 0; } // for the bus-trace oracle
        virtual void init_map();
        virtual void reset() override;
        void debug_dump(DebugFormatter *df);

        inline void set_clock(NClockII *clock) { this->clock = clock; }
        inline void set_clock_mode(clock_mode_t mode) { clock->set_clock_mode(mode); }
        inline void set_next_cycle_type(cycle_type_t type) { if (clock) ((NClockIIgs *)clock)->set_next_cycle_type(type); }
        inline void set_slow_mode(bool value) { ((NClockIIgs *)clock)->set_slow_mode(value); }

        // Bus write observer — called on shadowed soft-switch writes. Used by A2GSPU.
        typedef void (*write_observer_func)(void *context, uint32_t address, uint8_t value);
        write_observer_func write_observer = nullptr;
        void *write_observer_context = nullptr;
        void set_write_observer(write_observer_func func, void *context) {
            write_observer = func; write_observer_context = context;
        }

        // ===== A2GSPU warm-boot snapshot =====
        // Serializes all FPI state that affects the page table or SHR. Construction-fixed
        // surface intentionally EXCLUDED: main_rom (caller-owned), clock (NClock, self-heals),
        // megaii pointer (recursed, not copied), write_observer/_context (re-wired by the host
        // after restore), the I/O/shadow handlers (re-installed by THIS run's init_map ctor).
        void A2GSPU_snapshot(FILE *f) {
            // sizes/identity first (load validates main_ram blob + computes bases)
            uint32_t rb = ram_banks, rmb = rom_banks;
            fwrite(&rb, 4, 1, f);
            fwrite(&rmb, 4, 1, f);
            uint8_t b_rom03 = is_rom03 ? 1 : 0;           fwrite(&b_rom03, 1, 1, f);
            uint8_t b_mapinit = map_initialized ? 1 : 0;  fwrite(&b_mapinit, 1, 1, f);
            // value regs / soft-switches (unions written as their full byte)
            fwrite(&reg_slot, 1, 1, f);
            fwrite(&reg_shadow, 1, 1, f);
            fwrite(&reg_speed, 1, 1, f);
            fwrite(&reg_state, 1, 1, f);          // union byte: g_intcxrom..g_altzp ride along
            fwrite(&g_80store, 1, 1, f);
            fwrite(&g_hires, 1, 1, f);
            uint8_t b_text = g_text ? 1 : 0;   fwrite(&b_text, 1, 1, f);
            uint8_t b_mixed = g_mixed ? 1 : 0; fwrite(&b_mixed, 1, 1, f);
            fwrite(&reg_new_video, 1, 1, f);      // union byte: g_shr_enabled/g_bank_latch ride along
            // current-map-state (m_*) trackers (1 byte each)
            uint8_t mflags[7] = {
                (uint8_t)m_zp, (uint8_t)m_text1_r, (uint8_t)m_text1_w,
                (uint8_t)m_hires1_r, (uint8_t)m_hires1_w, (uint8_t)m_all_r, (uint8_t)m_all_w };
            fwrite(mflags, 1, 7, f);
            // language-card logic POD (4x u16, no pointers)
            fwrite(&ll, sizeof(ll), 1, f);
            // FPI RAM contents (8MB)
            fwrite(main_ram, 1, (size_t)ram_banks * BANK_SIZE, f);
            // Page table, relocatable. The ONLY non-null IIgs read_p/write_p point into
            // main_ram (bases[0]) or main_rom banks 0xFE/0xFF (bases[2]). $E0/$E1 are
            // handler pages (read_p==write_p==nullptr -> serialize as -1); their SHR bytes
            // live in megaii RAM, saved wholesale by megaii->A2GSPU_snapshot below. bases[1]
            // (megaii RAM) is therefore unused by the IIgs table but kept defensively.
            uint8_t *bases[3] = { get_memory_base(), get_megaii_memory_base(), get_rom_base() };
            size_t   sizes[3] = { (size_t)ram_banks * BANK_SIZE, (size_t)0x20000,
                                  (size_t)rom_banks * BANK_SIZE };
            A2GSPU_save_pages(f, bases, sizes, 3);
            // recurse into the embedded Mega-II
            megaii->A2GSPU_snapshot(f);
        }

        bool A2GSPU_restore(FILE *f) {
            uint32_t rb = 0, rmb = 0;
            if (fread(&rb, 4, 1, f) != 1) return false;
            if (fread(&rmb, 4, 1, f) != 1) return false;
            if (rb != ram_banks || rmb != rom_banks) return false; // geometry mismatch -> bail hard
            uint8_t b_rom03 = 0, b_mapinit = 0;
            if (fread(&b_rom03, 1, 1, f) != 1) return false;   is_rom03 = (b_rom03 != 0);
            if (fread(&b_mapinit, 1, 1, f) != 1) return false; map_initialized = (b_mapinit != 0);
            if (fread(&reg_slot, 1, 1, f) != 1) return false;
            if (fread(&reg_shadow, 1, 1, f) != 1) return false;
            if (fread(&reg_speed, 1, 1, f) != 1) return false;
            if (fread(&reg_state, 1, 1, f) != 1) return false;   // do NOT call set_state_register (would re-derive
                                                 // only ll.FF_READ_ENABLE/FF_BANK_1 and leave
                                                 // FF_PRE_WRITE/_FF_WRITE_ENABLE stale)
            if (fread(&g_80store, 1, 1, f) != 1) return false;
            if (fread(&g_hires, 1, 1, f) != 1) return false;
            uint8_t b_text = 0, b_mixed = 0;
            if (fread(&b_text, 1, 1, f) != 1) return false;  g_text = (b_text != 0);
            if (fread(&b_mixed, 1, 1, f) != 1) return false; g_mixed = (b_mixed != 0);
            if (fread(&reg_new_video, 1, 1, f) != 1) return false;
            uint8_t mflags[7] = {0};
            if (fread(mflags, 1, 7, f) != 7) return false;
            m_zp = mflags[0]; m_text1_r = mflags[1]; m_text1_w = mflags[2];
            m_hires1_r = mflags[3]; m_hires1_w = mflags[4]; m_all_r = mflags[5]; m_all_w = mflags[6];
            if (fread(&ll, sizeof(ll), 1, f) != 1) return false; // restore ll verbatim (captured consistent w/ reg_state)
            // RAM into the EXISTING constructor-allocated buffer (do NOT realloc)
            if (fread(main_ram, 1, (size_t)ram_banks * BANK_SIZE, f) != (size_t)ram_banks * BANK_SIZE) return false;
            // Page table: bases captured FRESH from this run's live objects (ROM heap ptr differs!)
            uint8_t *bases[3] = { get_memory_base(), get_megaii_memory_base(), get_rom_base() };
            bool ok = A2GSPU_load_pages(f, bases, 3);
            if (!ok) return false;               // page geometry mismatch -> FILE* unusable; do NOT
                                                 // recurse into megaii (would read from a desynced cursor)
            // recurse into the embedded Mega-II
            return megaii->A2GSPU_restore(f);
        }
};
