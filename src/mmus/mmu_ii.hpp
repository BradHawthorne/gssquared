#pragma once

#include "gs2.hpp"
#include "mmu.hpp"
#include "mmu_ii.hpp"


struct C8XX_handler_t {
    void (*handler)(void *context, SlotType_t slot);
    void *context;
};

class MMU_II : public MMU {
    protected:
        int ram_pages;
        uint32_t ram_size_ = 0;
        uint8_t *main_ram = nullptr;
        //uint8_t *main_io_4 = nullptr;
        uint8_t *main_rom_D0 = nullptr;

        read_handler_pair_t C0xx_memory_read_handlers[C0X0_SIZE] = { {nullptr, nullptr}, {nullptr, nullptr} };          // Table of memory read handlers
        write_handler_pair_t C0xx_memory_write_handlers[C0X0_SIZE] = { {nullptr, nullptr}, {nullptr, nullptr} };        // Table of memory write handlers
        // FORK: which of each address's two handlers belongs to a BUILT-IN device (bit k set:
        // hs[k] is internal) rather than a slot card. Only the IIgs's slot register uses it.
        uint8_t C0xx_read_internal[C0X0_SIZE] = { 0 };
        uint8_t C0xx_write_internal[C0X0_SIZE] = { 0 };

        int8_t C8xx_slot;
        C8XX_handler_t C8xx_handlers[8] = {nullptr};
        // FORK: THE $C800 FLIP-FLOP LIVES ON EACH CARD [HW Ref pp185-186]. A card with
        // expansion ROM sets its own flip-flop on /IOSEL (a $Cn00-$CnFF access that selects
        // it) and enables its ROM on flip-flop AND /IOSTRB; a $CFFF access clears it. Nothing
        // on the motherboard stops two cards being set at once -- "one card active" is a
        // firmware convention -- so a claim made while another card's flip-flop is still set
        // is CONTENTION, counted here. The page table keeps showing the card that claimed
        // last, as it always did, so what the CPU reads is unchanged; what is new is that
        // the machine knows, and says, when two cards would both be driving the bus.
        uint8_t  c8_flipflops = 0;         // bit n: slot n's flip-flop is set
        uint64_t c8_contention = 0;        // claims made while another card's flip-flop was set
        uint8_t  c8_contention_last = 0;   // the flip-flops at the most recent contention (with the claimer)
        void c8_select(uint8_t slot);
        page_table_entry_t slot_rom_ptable[15]; // handle C1-CF

        virtual void power_on_randomize(uint8_t *ram, int ram_size);
        
    public:
        bool f_intcxrom = false;

        MMU_II(int page_table_size, int ram_amount, uint8_t *rom_pointer);
        MMU_II();
        virtual ~MMU_II();
        uint8_t read(uint32_t address) override;
        void write(uint32_t address, uint8_t value) override;
        
        virtual void set_C8xx_handler(SlotType_t slot, void (*handler)(void *context, SlotType_t slot), void *context);
        virtual void set_C0XX_read_handler(uint16_t address, read_handler_t handler);
        // FORK: a built-in device's $C0xx handler (the IIgs IWM at $C0E0-$C0EF), which the
        // IIgs slot register ($C02D) switches against the slot card's (c0xx_gate).
        void set_C0XX_read_handler_internal(uint16_t address, read_handler_t handler);
        void set_C0XX_write_handler_internal(uint16_t address, write_handler_t handler);
        // FORK: THE SLOT DECODE, one place for both questions a slot access asks.
        //   c0xx_gate(sub): -1 every handler at $C0<sub> answers; 0 only built-in ones;
        //                   1 only the slot card's.
        //   slot_card_selected(n): a $Cn00-$CnFF access selects slot n's card (its /IOSEL,
        //                   and so its claim on $C800-$CFFF).
        // The II and II+ have no switches for either; MMU_IIe answers for the IIe and the
        // IIgs's Mega II.
        virtual int c0xx_gate(uint8_t sub) { (void)sub; return -1; }
        virtual bool slot_card_selected(uint8_t slot) { (void)slot; return true; }
        //   motherboard_c8_access(n): a $Cn00-$CnFF access that gives $C800-$CFFF to the
        //                   motherboard's own ROM instead (the IIe's INTC8ROM: $C3xx with
        //                   SLOTC3ROM reset). map_motherboard_c8() maps that ROM; the owner
        //                   then reads C8XX_MOTHERBOARD. No card's flip-flop is involved.
        virtual bool motherboard_c8_access(uint8_t slot) { (void)slot; return false; }
        //   c0xx_motor_touch(sub): an access to $C0n8/$C0n9 for slots 4-7, the Disk II motor
        //                   off/on switches the IIgs speed register's motor detect watches.
        virtual void c0xx_motor_touch(uint8_t sub) { (void)sub; }
        virtual void map_motherboard_c8() {}
        static constexpr int8_t C8XX_MOTHERBOARD = 8;
        virtual void set_C0XX_write_handler(uint16_t address, write_handler_t handler);
/*         virtual void get_C0XX_read_handler(uint16_t address, read_handler_t &handler);
        virtual void get_C0XX_write_handler(uint16_t address, write_handler_t &handler); */
        
        virtual void call_C8xx_handler(SlotType_t slot);
        virtual uint8_t *get_rom_base();
        uint8_t *get_memory_base() override { return main_ram; }
        uint32_t get_memory_size() override { return ram_size_; }
        virtual void init_map();
        virtual void set_default_C8xx_map();
        virtual void set_slot_rom(SlotType_t slot, uint8_t *rom, const char *name);
        virtual int get_C8xx_slot() { return C8xx_slot; };
        uint8_t get_c8_flipflops() const { return c8_flipflops; }
        uint64_t get_c8_contention() const { return c8_contention; }
        uint8_t get_c8_contention_last() const { return c8_contention_last; }
        void set_c8_flipflops(uint8_t v) { c8_flipflops = v; }   // snapshot restore
        virtual void reset(bool cold_start = false) override;
        virtual void dump_C0XX_handlers();
        /* Handlers for "Slot ROM" area C1 - CF */
        virtual void compose_c1cf();
        virtual void map_c1cf_page_both(uint8_t page, uint8_t *data, const char *read_d);
        virtual void map_c1cf_page_read_only(page_t page, uint8_t *data, const char *read_d);
        virtual void map_c1cf_page_read_h(page_t page, read_handler_t handler, const char *read_d);
        virtual void map_c1cf_page_write_h(page_t page, write_handler_t handler, const char *write_d);
};

