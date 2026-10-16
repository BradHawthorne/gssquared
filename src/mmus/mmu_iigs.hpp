#pragma once

#include <vector>
#include <cstring>

#include "mmu.hpp"
#include "mmu_iie.hpp"
#include "iigs_shadow_flags.hpp"
#include "iigs_aux_linear.hpp"
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
        /* The FPI decodes banks $F0-$FF as ROM space and runs those cycles at full
           speed with no RAM-refresh stretch, whether or not a ROM answers. Motherboard
           ROM sits at the top of the window (ROM01: $FE-$FF, ROM03: $FC-$FF); the rest
           is where expansion-card ROM lands. */
        constexpr static uint32_t ROM_SPACE_BASE = 0xF00000;

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

        uint8_t dma_bank_register = 0;

        //cpu_state *cpu = nullptr;
        NClock *clock = nullptr;

        std::vector<uint8_t> megaii_view_buf;   // FORK: megaii_cpu_view()'s copy

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
            dma_bank_register = 0;
            reset(true); // power-on: ROM03 CYAREG bit 6 set when cold_start
        };

        virtual ~MMU_IIgs() { delete[] main_ram; /* main_rom is owned by caller */ }

        virtual uint8_t read(uint32_t address) override {
            if (address >= ROM_SPACE_BASE) set_next_cycle_type(CYCLE_TYPE_FAST_ROM); // rom access is fast.

            uint8_t value = MMU::read(address);
            // $C0xx I/O is slot-visible; soft-switches toggle on READS too (per the IIe IOU 74LS259
            // decode), so the slot must see read accesses. Video-memory writes are tapped at the
            // resolved $E1 points, NOT here (no double-count).
            if (g_slot_bus_enabled) {
                if (slot_bus_format() == 2) slot_emit_cxxx_v2(address, value, true);
                else if ((address & 0xFF00) == 0xC000) slot_emit(address & 0xFFFF, value, true, (address >> 16) & 1);
            }
            return value;
        }

        virtual void write(uint32_t address, uint8_t value) override {
            if (address >= ROM_SPACE_BASE) set_next_cycle_type(CYCLE_TYPE_FAST_ROM); // rom access is fast.

            MMU::write(address, value);
            if (g_slot_bus_enabled) {
                if (slot_bus_format() == 2) slot_emit_cxxx_v2(address, value, false);
                else if ((address & 0xFF00) == 0xC000) slot_emit(address & 0xFFFF, value, false, (address >> 16) & 1);
            }
        }

        inline bool shadow_is_enabled(uint32_t address) {
            uint32_t address_16 = address & 0xFFFF;
            uint32_t address_17 = address & 0x1FFFF;
            const bool odd_bank = (address & 0x10000) != 0;

            if ((address_16 >= 0x0400 && address_16 <= 0x07FF) && !(reg_shadow & SHADOW_INH_TEXT1)) {
                return true;
            }
            if (is_rom03 && ((address_16 >= 0x0800 && address_16 <= 0x0BFF) && !(reg_shadow & SHADOW_INH_TEXT2))) {
                return true;
            }
            // HGR1/HGR2: even banks only. Odd-bank $2000-$5FFF is gated by AUXHGR|SHR
            // (inhibit only when both are set; see SHADOW.s inhbt1).
            if (!odd_bank) {
                if ((address_16 >= 0x2000 && address_16 <= 0x3FFF) && !(reg_shadow & SHADOW_INH_HGR1)) {
                    return true;
                }
                if ((address_16 >= 0x4000 && address_16 <= 0x5FFF) && !(reg_shadow & SHADOW_INH_HGR2)) {
                    return true;
                }
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

        // cpu_bank_lsb: the bank bit of the address the CPU wrote, before RAMWRT/80STORE
        // resolution (-1 = unknown, use the resolved address) -- slot stream format 2's M2B0.
        inline void megaiiWrite(uint32_t address, uint8_t value, int cpu_bank_lsb = -1) {
            if ((address & 0x1'0000) && g_bank_latch) {
                uint32_t idx = address & 0x1'FFFF;
                if (is_aux_linear()) {
                    idx = 0x1'0000 | iigs_aux_linear_to_phys((uint16_t)idx);
                }
                megaii->get_memory_base()[idx] = value;
                // Records carry the LOGICAL address (see bank_e1_write).
                uint64_t _cyc = get_cycle_count();
                bus_trace_note(_cyc, address & 0x1'FFFF, value, 1); // shadowed SHR write (provenance 1)
                // Observatory: the shadowed store as a BUS_TXN (provenance flag), so obs_hash_bus
                // reproduces bus_trace across BOTH provenance paths — the superset holds by
                // construction, not just when zero shadowed writes happen to occur.
                obs_note(_cyc, obs_sigid(OBS_SUB_BUS, 0, 0), OBS_K_BUS_TXN, address & 0x1'FFFF, value,
                         ((uint32_t)g_obs_c14m_cost << 8) | g_obs_cycle_type, OBS_F_PROVENANCE_SH);
                slot_emit(address & 0xFFFF, value, false,
                          (slot_bus_format() == 2 && cpu_bank_lsb >= 0) ? cpu_bank_lsb != 0 : true);   // M2B0 (format 2: the CPU's bank bit, TN68)
            } else {
                megaii->write(address & 0xFFFF, value);
                slot_emit(address & 0xFFFF, value, false,
                          (slot_bus_format() == 2 && cpu_bank_lsb >= 0) ? cpu_bank_lsb != 0 : (address & 0x1'0000) != 0);
            }
            set_next_cycle_type(CYCLE_TYPE_SYNC);
        }

        void megaii_compose_map();

        inline bool is_iolc_shadowed() { return !(reg_shadow & SHADOW_INH_IOLC); }
        inline bool is_bank_latch() { return g_bank_latch; }
        /* Enabling SHR ($C029 bit 7) linearizes aux $2000-$9FFF too, not just the
           explicit linearize bit 6. GS/OS draws its boot screen with $41 (linear,
           display off) then switches to $81, and expects the mapping to persist. */
        inline bool is_aux_linear() override { return g_aux_linear || g_shr_enabled; }
        MMU *get_megaii_mmu() override { return megaii; }

        /* Physical aux index for a direct bank $E1 access (bank latch on). Aux is
           forced, so ALTZP / RAMRD / RAMWRT / 80STORE / PAGE2 do not apply here -
           those select main vs aux for bank $E0. The LC bank 1 $D000 image (parked in
           the $C000 I/O hole, as in bsr_map_memory) and the linear/SHR aux mapping do
           still apply; the two ranges are disjoint. */
        inline uint32_t e1_aux_index(uint16_t a16) {
            if (a16 >= 0xD000 && a16 <= 0xDFFF && is_lc_bank1()) return 0x1'0000 | (a16 - 0x1000);
            if (is_aux_linear()) return 0x1'0000 | iigs_aux_linear_to_phys(a16);
            return 0x1'0000 | a16;
        }

        // FORK: THE MEGA II IMAGE AS THE CPU ADDRESSES IT. Since upstream 18f5d04c the
        // physical Mega II store keeps $E1:$2000-$9FFF interleaved whenever the aux
        // window is linearized (NEWVIDEO bit 6, or SHR on, which implies it), so
        // megaii_base[0x1xxxx] is no longer the byte the CPU reads at $E1:xxxx.
        // Every observer that means "what the program sees" -- SHR hashes and
        // goldens, the $E1 dumps, SCB and palette reads, the probes -- goes through
        // these; the snapshot keeps saving the physical store, which is what
        // restores it. `a17` is the 17-bit Mega II address: bit 16 = bank $E1.
        inline uint32_t megaii_cpu_index(uint32_t a17) {
            const uint16_t a16 = (uint16_t)a17;
            if ((a17 & 0x1'0000) && is_aux_linear()) return 0x1'0000 | iigs_aux_linear_to_phys(a16);
            return a17 & 0x1'FFFF;
        }
        inline uint8_t megaii_cpu_peek(uint32_t a17) {
            uint8_t *mb = megaii ? megaii->get_memory_base() : nullptr;
            return mb ? mb[megaii_cpu_index(a17)] : 0;
        }
        inline void megaii_cpu_poke(uint32_t a17, uint8_t v) {
            uint8_t *mb = megaii ? megaii->get_memory_base() : nullptr;
            if (mb) mb[megaii_cpu_index(a17)] = v;
        }
        // The whole 128 KB image in CPU order. Outside linearization that is the
        // store itself (no copy); inside it, a de-interleaved copy refreshed on
        // every call -- a read-only view for readers that index at fixed offsets.
        // Do not write through it, and do not hold it across a run.
        const uint8_t *megaii_cpu_view() {
            uint8_t *mb = megaii ? megaii->get_memory_base() : nullptr;
            if (!mb || !is_aux_linear()) return mb;
            megaii_view_buf.resize(0x2'0000);
            memcpy(megaii_view_buf.data(), mb, 0x2'0000);
            for (uint32_t a = 0x2000; a <= 0x9FFF; a++)
                megaii_view_buf[0x1'0000 | a] = mb[0x1'0000 | iigs_aux_linear_to_phys((uint16_t)a)];
            return megaii_view_buf.data();
        }

        // Faithful slot-3 signal decode for a Mega-II-side (slot-visible) cycle (the virtual slot).
        inline uint8_t slot_ctl(uint16_t a, bool is_read, bool m2b0) {
            const bool v2 = slot_bus_format() == 2;
            const int slot = v2 ? g_slot_bus_slot : SLOT_BUS_SLOT;
            uint8_t ctl = SLOT_CTL_M2SEL;                       // a valid Mega-II cycle
            if (is_read) ctl |= SLOT_CTL_READ;
            if (m2b0)    ctl |= SLOT_CTL_M2B0;
            if (!v2 && !is_iolc_shadowed()) ctl |= SLOT_CTL_INH;   // format 2: /INH is a card output
            if (v2 && machine_fast()) ctl |= SLOT_CTL_FAST;       // format 2: an isolated slow cycle in a fast machine (FORK: after the motor detect)
            if (a >= 0xC000 && a <= 0xCFFF) {                   // slot I/O strobes (this card's slot)
                // FORMAT 2: /DEVSEL only when the slot register gives this slot's I/O to the card.
                if ((a & 0xFFF0) == (uint16_t)(0xC080 + slot * 0x10) && (!v2 || !megaii || megaii->c0xx_gate((uint8_t)a) != 0))
                    ctl |= SLOT_CTL_DEVSEL;
                if ((a >> 8)     == (uint16_t)(0xC0   + slot))        ctl |= SLOT_CTL_IOSEL;
                if (a >= 0xC800)                                     ctl |= SLOT_CTL_IOSTRB;
            }
            return ctl;
        }

        // FORMAT 2: whether a $C100-$CFFF access reaches a card's pins. $Cn00 pages go to
        // the card when INTCXROM is off and its $C02D bit (SLOTC3ROM for $C3) selects the
        // slot; $C800-$CFFF when a card owns the expansion ROM (C8xx_slot 1-7).
        inline bool cxxx_card_visible(uint16_t a) {
            if (!megaii || megaii->f_intcxrom) return false;
            const uint8_t page = (uint8_t)(a >> 8);
            if (page >= 0xC1 && page <= 0xC7) return megaii->slot_card_selected(page & 0x07);   // the one slot decode
            if (page >= 0xC8) {
                const int owner = megaii->get_C8xx_slot();
                return owner >= 1 && owner <= 7;
            }
            return false;
        }
        // FORMAT 2: a CPU access to $xx:Cxxx. Slot-visible only in banks $00/$01 with the
        // IOLC area shadowed, or $E0/$E1 (in fast-RAM banks $C0xx is RAM).
        inline void slot_emit_cxxx_v2(uint32_t address, uint8_t value, bool is_read) {
            const uint8_t bank = (uint8_t)(address >> 16);
            const uint16_t a = (uint16_t)address;
            if (a < 0xC000 || a > 0xCFFF) return;
            const bool io_bank = ((bank == 0x00 || bank == 0x01) && is_iolc_shadowed()) || bank == 0xE0 || bank == 0xE1;
            if (!io_bank) return;
            if (a >= 0xC100 && !cxxx_card_visible(a)) return;   // internal ROM: no /M2SEL, no strobe
            // Fast cycles assert no /M2SEL, so a card never sees them: the FPI's own registers
            // $C035-$C037, reads of $C02D and $C068 (writes to them are slow), and the
            // interrupt ROM at $C071-$C07F [TN68 p2; HW Ref ch.2 "Speed"].
            if (a >= 0xC035 && a <= 0xC037) return;
            if (is_read && (a == 0xC02D || a == 0xC068 || (a >= 0xC071 && a <= 0xC07F))) return;
            slot_emit(a, value, is_read, (bank & 1) != 0);
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
            update_speed();
        }
        // FORK: 1 MHz when bit 7 is clear, or (A2GSPU_DISK_TIMING=1) while a slot whose
        // motor detect bit is set has its Disk II motor on [HW Ref Table 2-2].
        bool motor_detect = false;
        inline void update_speed() {
            const uint8_t motors = (motor_detect && megaii) ? megaii->gs_motors_active : 0;
            const bool slow = !(reg_speed & 0x80) || ((motors & reg_speed & 0x0F) != 0);
            if (clock) ((NClockIIgs *)clock)->set_slow_mode(slow);
        }
        inline bool machine_fast() { return clock ? !((NClockIIgs *)clock)->get_slow_mode() : (reg_speed & 0x80) != 0; }
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

        inline void set_dma_bank_register(uint8_t value) { dma_bank_register = value; }
        inline uint8_t get_dma_bank_register() { return dma_bank_register; }

        void bsr_map_memory();
        virtual uint8_t vp_read(uint32_t address) override;

        uint32_t calc_aux_write(uint32_t address);
        uint32_t calc_aux_read(uint32_t address);
        void init_c0xx_handlers();
        void write_c0xx(uint16_t address, uint8_t value);
        uint8_t read_c0xx(uint16_t address);

        virtual uint8_t *get_rom_base() { return main_rom; };

        // Byte offset within main_rom of the start of virtual bank $FF (the
        // top bank, always the *last* physical bank in the ROM image
        // regardless of total ROM size). ROM01 is 2 banks (128KB) -> 0x10000;
        // ROM03 is 4 banks (256KB) -> 0x30000. Fixed-offset lookups (LC-area
        // ROM overlay reads, the $C100-$CFFF slot-firmware mapping, the
        // embedded IIe-compat ROM pointer) key off bank $FF and used to
        // hardcode 0x10000, silently only correct for a 128KB ROM.
        inline uint32_t rom_bank_ff_offset() const { return (rom_banks - 1) * BANK_SIZE; }
        uint8_t *get_memory_base() override { return main_ram; }
        uint32_t get_memory_size() override { return ram_banks * BANK_SIZE; }
        // Base of ROM bank $FF (the last 64KB) — the language-card / $D000-$FFFF ROM
        // region. Bank $FF lives at the TOP of the image: offset (rom_banks-1)*64KB =
        // 0x10000 for a 128KB ROM01, 0x30000 for a 256KB ROM03/ROM04. Use this instead
        // of a hardcoded 0x10000 so the LC-ROM read path is size-agnostic.
        inline uint8_t *get_lc_rom_base() { return main_rom + (rom_banks - 1) * BANK_SIZE; }
        inline uint8_t *get_megaii_memory_base() { return megaii ? megaii->get_memory_base() : nullptr; }
        // Observation-free 24-bit peek for the introspection floor: NO soft-switch
        // dispatch, NO slot-bus emit, NO clock tick — an external probe, never a hook
        // the emulated OS can see. $E0/$E1 read the Mega II image directly (SHR window
        // + toolbox work areas); all else via MMU::read_raw (never the IO handler path).
        uint8_t probe_peek(uint32_t addr24) override {
            uint32_t bank = (addr24 >> 16) & 0xFF;
            if ((bank | 1) == 0xE1) {
                // $C000-$CFFF IN $E0/$E1 IS NOT RAM TO THE CPU. bank_e0_read and
                // bank_e1_read send that range through megaii->read, i.e. the Mega
                // II's page table: soft switches at $C0xx, and whichever of slot
                // card and internal firmware is selected at $C100-$CFFF. The raw
                // image below answered with the RAM underneath instead -- the same
                // lie banks $00/$01 had, and the one imperfection M11 still named.
                // Resolved like bank $00: ROM from the Mega II's page table, and
                // $C0xx unreadable (probe_readable says so), never a switch read.
                if ((addr24 & 0xF000) == 0xC000) {
                    if ((addr24 & 0xFF00) != 0xC000 && megaii) return megaii->probe_peek(addr24 & 0xFFFF);
                    return floating_bus_read();
                }
                // FORK: the rest of $E0/$E1 is resolved the way bank_e0_read and
                // bank_e1_read resolve it, not as raw Mega II RAM. $E0 -- and $E1 with
                // the bank latch (NEWVIDEO bit 0) off, which bank_e1_read serves as
                // $E0 -- goes through the Mega II page table: RAMRD, ALTZP, 80STORE
                // and the language card's ROM/bank-1 state all apply there, so with
                // ROM selected `E0:D000` is ROM to the CPU. $E1 with the latch on is
                // aux RAM at e1_aux_index (bank 1 slides $D000-$DFFF down $1000) with
                // the $FF ROM over $D000-$FFFF while LC reads are disabled.
                const uint16_t a16 = (uint16_t)addr24;
                if (bank == 0xE0 || !is_bank_latch()) {
                    if (megaii) return megaii->probe_peek(a16);
                } else {
                    if (a16 >= 0xD000 && !is_lc_read_enable()) return get_rom_base()[rom_bank_ff_offset() + a16];
                    uint8_t *mb = get_megaii_memory_base();
                    if (mb) return mb[e1_aux_index(a16)];
                }
                return floating_bus_read();
            }
            // BANKS $00/$01 ARE HANDLER PAGES. set_ram_shadow_banks() installs
            // bank_shadow_read/write over them, so read_raw sees no read pointer
            // and returns floating bus -- while the CPU, going through the
            // handler, reads main_ram at the aux-resolved offset. Two different
            // answers for one address is how a rail can read a byte back
            // correctly and still watch the CPU execute a zero.
            //
            // Resolved here the way bank_shadow_read resolves it, honouring
            // ramrd/ramwrt/altzp. a2gspu_state_dump() has done exactly this for
            // its own diagnostic all along; it just was not wired to the probe.
            // THE SHORTCUT ABOVE COVERED THE WHOLE BANK, AND THE BANK IS NOT ALL
            // RAM. bank_shadow_read resolves three regions before it ever reaches
            // calc_aux_read: $C0xx-$CFxx go to the Mega II, $D000-$FFFF go to LC
            // RAM (with the bank-1 $1000 slide) or to ROM. Returning main_ram for
            // those is returning the memory UNDERNEATH them.
            //
            // MEASURED on Platform 5: `read C7FF` answered $F8 while the CPU,
            // executing LDA $C7FF at the same instant, read $10 -- the slot-7
            // ProDOS driver offset. The rail was reporting FPI RAM beneath the
            // I/O space as though it were firmware, with probe_readable saying
            // the data was real, and an A/B of two disk images built on those
            // bytes "proved" something about memory nobody had looked at.
            //
            // The language-card case is worse than merely wrong, because it is
            // intermittent: a2engine's COREIMG gate compares guest $D000 against
            // the core image and read $00 across it once, then passed on an
            // identical re-run. $D000-$DFFF is the bank-switched 4K and $E000+ is
            // the common 8K, so whenever the guest is on LC bank 2 or has ROM
            // banked in, exactly the $D000 half disagrees and the $E000 half does
            // not. That is a FALSE FAILURE in a gate, which is worse than no
            // check at all -- it spends a debugging session on the instrument.
            //
            // Resolved the way bank_shadow_read resolves it. Where an
            // observation-free answer is impossible ($C0xx is soft switches:
            // reading one THROWS it, which is the one thing a probe must never
            // do) the answer is "I cannot see this", via probe_readable below.
            if (bank <= 0x01) {
                uint32_t page = (addr24 & 0xFF00) >> 8;
                if (is_iolc_shadowed() && page >= 0xC0 && page <= 0xCF) {
                    // $C100-$CFFF is genuine ROM and worth dumping; ask the Mega
                    // II, whose page table already holds whichever of slot card
                    // and internal firmware is currently selected. $C0xx has no
                    // side-effect-free answer at all.
                    if (page > 0xC0 && megaii) return megaii->probe_peek(addr24 & 0xFFFF);
                    return floating_bus_read();
                }
                if (is_iolc_shadowed() && page >= 0xD0) {
                    if (!is_lc_read_enable()) return get_lc_rom_base()[addr24 & 0xFFFF];
                    uint32_t a = addr24;
                    if (is_lc_bank1() && page <= 0xDF) a -= 0x1000;
                    uint8_t *mb = get_memory_base();
                    if (mb) return mb[a + calc_aux_read(a)];
                }
                uint8_t *mb = get_memory_base();
                if (mb) return mb[addr24 + calc_aux_read(addr24)];
            }
            return read_raw(addr24);
        }

        // The write-side twin, resolved identically -- with calc_aux_WRITE,
        // because a IIgs can read main and write aux for the same logical
        // address and putting bytes through the read mapping would land them
        // where the CPU will not look for them.
        bool probe_poke(uint32_t addr24, uint8_t value) override {
            uint32_t bank = (addr24 >> 16) & 0xFF;
            if ((bank | 1) == 0xE1) {
                // $C000-$CFFF refused for the reason probe_peek names: the CPU sees
                // switches and firmware there, so a byte stored in the RAM beneath
                // is a byte nothing will read.
                if ((addr24 & 0xF000) == 0xC000) return false;
                // FORK: as bank_e0_write and bank_e1_write -- see probe_peek.
                const uint16_t a16 = (uint16_t)addr24;
                if (bank == 0xE0 || !is_bank_latch()) return megaii && megaii->probe_poke(a16, value);
                if (a16 >= 0xD000 && !is_lc_write_enable()) return false;   // LC write-protected
                uint8_t *mb = get_megaii_memory_base();
                if (mb) { mb[e1_aux_index(a16)] = value; return true; }
                return false;
            }
            if (bank <= 0x01) {
                uint32_t page = (addr24 & 0xFF00) >> 8;
                // Symmetric with the read side, and refusing for the same reason:
                // poking a soft switch is throwing it, and poking ROM writes to
                // the RAM underneath where the CPU will never read it back. A
                // silently-dropped write that acks success is what probe_poke
                // was introduced to stop.
                if (is_iolc_shadowed() && page >= 0xC0 && page <= 0xCF) return false;
                uint32_t a = addr24;
                if (is_iolc_shadowed() && page >= 0xD0) {
                    // is_lc_WRITE_enable, not read. The language card has two
                    // independent enables and the classic $C081-twice setup has
                    // ROM readable while LC RAM takes writes -- reusing the read
                    // side here would silently drop every poke in that state,
                    // which is the exact failure probe_poke exists to prevent.
                    if (!is_lc_write_enable()) return false; // writes are inhibited
                    if (is_lc_bank1() && page <= 0xDF) a -= 0x1000;
                }
                a += calc_aux_write(a);
                // bank_shadow_write mirrors shadowed pages into the Mega II
                // before storing. Skipping it leaves the copy the video scanner
                // and $E0/$E1 read from holding the OLD byte, so a rail poke
                // would be half-visible: correct through `read`, stale on screen.
                //
                // DIRECTLY, not via megaiiWrite. That helper is the CPU's path
                // and it EMITS -- slot_emit, bus_trace_note, obs_note, and
                // set_next_cycle_type(CYCLE_TYPE_SYNC). Calling it from here
                // would make an observation-free poke change the next
                // instruction's timing and inject phantom transactions into the
                // very traces a caller pokes memory in order to study. probe_
                // peek already reaches the Mega II image this way for $E0/$E1;
                // this is the write-side twin of that, and nothing else.
                if (shadow_is_enabled(a)) {
                    uint8_t *mm = get_megaii_memory_base();
                    if (mm) mm[megaii_cpu_index(a & 0x1FFFF)] = value;   // CPU order -> store
                }
                uint8_t *mb = get_memory_base();
                if (mb) { mb[a] = value; return true; }
            }
            return MMU::probe_poke(addr24, value);
        }

        // Handler pages ARE readable now that probe_peek resolves them; saying
        // otherwise would make every rail read of bank $00 warn about floating
        // bus while returning correct data.
        bool probe_readable(uint32_t addr24) override {
            uint32_t bank = (addr24 >> 16) & 0xFF;
            if ((bank | 1) == 0xE1) {
                if ((addr24 & 0xF000) == 0xC000)                    // tracks probe_peek above
                    return (addr24 & 0xFF00) != 0xC000 && megaii != nullptr;
                if (bank == 0xE0 || !is_bank_latch())                // tracks probe_peek above
                    return megaii != nullptr && megaii->probe_readable(addr24 & 0xFFFF);
                return get_megaii_memory_base() != nullptr;
            }
            if (bank <= 0x01) {
                // Must track probe_peek exactly. It used to claim the whole bank
                // readable, which is how `read C7FF` came back status=OK holding
                // RAM from underneath the I/O space: the caller was told the
                // bytes were real, so it had no reason to doubt them.
                uint32_t page = (addr24 & 0xFF00) >> 8;
                if (is_iolc_shadowed() && page >= 0xC0 && page <= 0xCF)
                    return page > 0xC0 && megaii != nullptr;   // ROM yes, switches no
                return get_memory_base() != nullptr;
            }
            return MMU::probe_readable(addr24);
        }

        // A2GSPU diagnostic: print the master soft-switch state + how bank-$00/$01
        // $A600 (the loader CallTable) actually resolves. Banks $00/$01 are handler
        // pages (bank_shadow_read/write), so READ_RAW returns floating-bus $EE for
        // them -- probe_peek above does NOT: it resolves those banks through
        // calc_aux_read, the language-card bank/ROM state and the Mega II page
        // table, and returns floating bus only for page $C0, where no
        // observation-free answer exists. (The old text here said probe_peek
        // returned $EE for the whole of banks $00/$01. That was true before the
        // probe was fixed nineteen lines above, and INSTRUMENTATION_SHAPE.md M11
        // was written from this sentence rather than from the code -- then three
        // more consumers were built on it, each flagging every genuine $EE in
        // bank 0 as "floating bus, do NOT trust as memory truth". $EE is INC abs
        // and an ordinary data byte. A stale comment beside working code is a
        // black box of the documentation kind, and this one seeded four.)
        // This function replicates bank_shadow's calc_aux_read/write resolution
        // (honouring g_ramrd/g_ramwrt/g_altzp) and reads the megaii RAM DIRECTLY, so
        // it shows the TRUE content + whether a read vs a write of the same logical
        // address land in MAIN vs AUX (the split we are hunting). Observation-free.
        void a2gspu_state_dump() override {
            printf("IIGS SWITCHES: state=$%02X ramrd=%d ramwrt=%d altzp=%d 80st=%d page2=%d lcbnk2=%d rdrom=%d shadow=$%02X\n",
                   (unsigned)reg_state, g_ramrd, g_ramwrt, g_altzp, g_80store, g_page2, g_lcbnk2, g_rdrom, (unsigned)reg_shadow);
            uint8_t *mb = get_memory_base();   // main_ram (FPI) -- where bank_shadow_read/write actually land
            uint32_t probes[] = { 0x00A600, 0x01A600, 0x00A680, 0x01A680 };
            for (uint32_t a : probes) {
                uint32_t ridx = a + calc_aux_read(a);    // matches bank_shadow_read: get_memory_base()[addr + calc_aux_read]
                uint32_t widx = a + calc_aux_write(a);   // matches bank_shadow_write
                printf("  logical $%06X  read->main_ram[$%06X]  write->main_ram[$%06X]  @read:",
                       a, ridx, widx);
                if (mb) for (int i = 0; i < 8; i++) printf(" %02X", mb[ridx + i]);
                printf("%s\n", (ridx != widx) ? "   <-- READ/WRITE SPLIT" : "");
            }
        }
        inline uint64_t get_cycle_count() { return clock ? clock->get_cycles() : 0; } // for the bus-trace oracle
        inline bool is_rom03_machine() const { return is_rom03; }

        virtual void init_map();
        virtual void reset(bool cold_start = false) override;
        void debug_dump(DebugFormatter *df,bool native_only=false);

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
